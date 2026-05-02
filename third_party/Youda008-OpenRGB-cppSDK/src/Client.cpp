//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: OpenRGB network client
//======================================================================================================================

#include <OpenRGB/Client.hpp>
#include <CppUtils-Essential/Essential.hpp>

#include <OpenRGB/Exceptions.hpp>
#include "ProtocolMessages.hpp"

#include <CppUtils-Network/Socket.hpp>
using own::TcpSocket;
using own::SocketError;
#include <CppUtils-Network/SystemErrorInfo.hpp>
using own::getLastError;
using own::getErrorString;

#include <CppUtils-Essential/BinaryStream.hpp>
using own::BinaryOutputStream;
using own::BinaryInputStream;
#include <CppUtils-Essential/ContainerUtils.hpp>
using own::span;
using own::make_span;
#include <CppUtils-Essential/LangUtils.hpp>
using fut::make_unique;
#include <CppUtils-Essential/CriticalError.hpp>

#include <string>
using std::string;
#include <vector>
using std::vector;
#include <array>
using std::array;
#include <chrono>
using std::chrono::milliseconds;


namespace orgb {


//======================================================================================================================
//  enum to string conversion

const char * enumString( ConnectStatus status ) noexcept
{
	static const char * const ConnectStatusStr [] =
	{
		"The operation was successful.",
		"Operation failed because underlying networking system could not be initialized.",
		"Connect operation failed because the socket is already connected.",
		"The hostname you entered could not be resolved to IP address.",
		"Could not connect to the target server, either it's down or the port is closed.",
		"Failed to send the client's protocol version or receive the server's protocol version",
		"The protocol version of the server is not supported. Please update the OpenRGB app.",
		"Failed to send the client name to the server.",
		"Other system error.",
		"Internal error of this library. Please create a github issue.",
	};
	static_assert( size_t(ConnectStatus::UnexpectedError) + 1 == fut::size(ConnectStatusStr), "update the ConnectStatusStr" );

	if (size_t(status) < fut::size(ConnectStatusStr))
	{
		return ConnectStatusStr[ size_t(status) ];
	}
	else
	{
		return "<invalid status>";
	}
}

const char * enumString( RequestStatus status ) noexcept
{
	static const char * const RequestStatusStr [] =
	{
		"The request was succesful.",
		"Request failed because the client is not connected.",
		"Failed to send the request message.",
		"Server has closed the connection.",
		"No reply has arrived from the server in given timeout.",
		"There has been some other error while trying to receive a reply.",
		"The reply from the server is invalid.",
		"Internal error of this library. Please create a github issue.",
	};
	static_assert( size_t(RequestStatus::UnexpectedError) + 1 == fut::size(RequestStatusStr), "update the RequestStatusStr" );

	if (size_t(status) < fut::size(RequestStatusStr))
	{
		return RequestStatusStr[ size_t(status) ];
	}
	else
	{
		return "<invalid status>";
	}
}

const char * enumString( UpdateStatus status ) noexcept
{
	static const char * const UpdateStatusStr [] =
	{
		"The current device list seems up to date.",
		"Server has sent a notification message indicating that the device list has changed.",
		"Server has closed the connection.",
		"Server has sent some other kind of message that we didn't expect.",
		"Error has occured while trying to restore socket to its original state and the socket has been closed.",
		"Other system error.",
		"Internal error of this library. Please create a github issue.",
	};
	static_assert( size_t(UpdateStatus::UnexpectedError) + 1 == fut::size(UpdateStatusStr), "update the UpdateStatusStr" );

	if (size_t(status) < fut::size(UpdateStatusStr))
	{
		return UpdateStatusStr[ size_t(status) ];
	}
	else
	{
		return "<invalid status>";
	}
}


//======================================================================================================================
//  Client: main API

Client::Client( const std::string & clientName ) noexcept
:
	_clientName( clientName ),
	_socket( new TcpSocket ),
	_negotiatedProtocolVersion( 0 ),
	_isDeviceListOutOfDate( true )
{}

Client::~Client() noexcept {}

bool Client::isConnected() const noexcept
{
	return _socket->isConnected();
}

ConnectStatus Client::_connect( const std::string & host, uint16_t port )
{
	SocketError connectRes = _socket->connect( host, port );
	if (connectRes != SocketError::Success)
	{
		switch (connectRes)
		{
			case SocketError::AlreadyConnected:      return ConnectStatus::AlreadyConnected;
			case SocketError::NetworkingInitFailed:  return ConnectStatus::NetworkingInitFailed;
			case SocketError::HostNotResolved:       return ConnectStatus::HostNotResolved;
			case SocketError::ConnectFailed:         return ConnectStatus::ConnectFailed;
			default:                                 return ConnectStatus::OtherSystemError;
		}
	}

	// rather set some default timeout for recv operations, user can always override this
	_socket->setTimeout( milliseconds( 500 ) );

	bool sendVersionRes = sendMessage< RequestProtocolVersion >( implementedProtocolVersion );
	if (!sendVersionRes)
	{
		_socket->disconnect();  // revert to the state before this function was called
		return ConnectStatus::RequestVersionFailed;
	}

	auto requestVersionRes = awaitMessage< ReplyProtocolVersion >();
	if (requestVersionRes.status != RequestStatus::Success)
	{
		_socket->disconnect();  // revert to the state before this function was called
		return ConnectStatus::RequestVersionFailed;
	}

	if (requestVersionRes.message.serverVersion == 0)
	{
		// Support for the very first version-less OpenRGB protocol will not be maintained.
		_socket->disconnect();  // revert to the state before this function was called
		return ConnectStatus::VersionNotSupported;
	}

	_negotiatedProtocolVersion = std::min( implementedProtocolVersion, requestVersionRes.message.serverVersion );

	bool sendNameRes = sendMessage< SetClientName >( _clientName );
	if (!sendNameRes)
	{
		_socket->disconnect();  // revert to the state before this function was called
		return ConnectStatus::SendNameFailed;
	}

	// The list isn't trully out of date, because there isn't any list yet. But let's say it is, because
	// it simplifies writing an application loop. This way user can just write
	//
	// while (true)
	// {
	//     if (!client.isConnected())
	//         client.connect();
	//     if (client.isDeviceListOutOfDate())
	//         deviceList = client.requestDeviceList();
	//     ...
	//     change colors
	//     ...
	// }
	_isDeviceListOutOfDate = true;

	return ConnectStatus::Success;
}

bool Client::_disconnect() noexcept
{
	SocketError status = _socket->disconnect();
	if (status == SocketError::Success)
		return true;
	else if (status == SocketError::NotConnected)
		return false;
	else
		// This can happen if the client doesn't respond to a server's FIN packet by calling close() in time and the
		// server just forcibly ends the connection producing a network error.
		// In this case, the user doesn't need to know because he wanted the socket closed and that's what's gonna happen.255
		return true;
}

bool Client::_setTimeout( std::chrono::milliseconds timeout ) noexcept
{
	// Currently we cannot set timeout on a socket that is not connected, because the actual system socket is created
	// during connect operation, so the preceeding setTimeout calls would go to nowhere.
	if (!_socket->isConnected())
	{
		return false;
	}

	return _socket->setTimeout( timeout );
}

DeviceListResult Client::_requestDeviceList()
{
	if (!_socket->isConnected())
	{
		return { RequestStatus::NotConnected, {} };
	}

	DeviceListResult result;

	do
	{
		result.devices.clear();
		_isDeviceListOutOfDate = false;

		bool sent = sendMessage< RequestControllerCount >();
		if (!sent)
		{
			result.status = RequestStatus::SendRequestFailed;
			return result;
		}

		auto deviceCountResult = awaitMessage< ReplyControllerCount >();
		if (deviceCountResult.status != RequestStatus::Success)
		{
			result.status = deviceCountResult.status;
			return result;
		}

		for (uint32_t deviceIdx = 0; deviceIdx < deviceCountResult.message.count; ++deviceIdx)
		{
			sent = sendMessage< RequestControllerData >( deviceIdx, _negotiatedProtocolVersion );
			if (!sent)
			{
				result.status = RequestStatus::SendRequestFailed;
				return result;
			}

			auto deviceDataResult = awaitMessage< ReplyControllerData >();
			if (deviceDataResult.status != RequestStatus::Success)
			{
				result.status = deviceDataResult.status;
				return result;
			}

			result.devices.append( move( deviceDataResult.message.device_desc ) );
		}
	}
	// In the middle of the update we might receive DeviceListUpdated message. In that case we need to start again.
	while (_isDeviceListOutOfDate);

	result.status = RequestStatus::Success;
	return result;
}

DeviceCountResult Client::_requestDeviceCount()
{
	if (!_socket->isConnected())
	{
		return { RequestStatus::NotConnected, 0 };
	}

	DeviceCountResult result;

	bool sent = sendMessage< RequestControllerCount >();
	if (!sent)
	{
		result.status = RequestStatus::SendRequestFailed;
		return result;
	}

	auto deviceCountResult = awaitMessage< ReplyControllerCount >();
	if (deviceCountResult.status != RequestStatus::Success)
	{
		result.status = deviceCountResult.status;
		return result;
	}

	result.count = deviceCountResult.message.count;
	result.status = RequestStatus::Success;
	return result;
}

DeviceInfoResult Client::_requestDeviceInfo( uint32_t deviceIdx )
{
	if (!_socket->isConnected())
	{
		return { RequestStatus::NotConnected, nullptr };
	}

	DeviceInfoResult result;

	bool sent = sendMessage< RequestControllerData >( deviceIdx, _negotiatedProtocolVersion );
	if (!sent)
	{
		result.status = RequestStatus::SendRequestFailed;
		return result;
	}

	auto deviceDataResult = awaitMessage< ReplyControllerData >();
	if (deviceDataResult.status != RequestStatus::Success)
	{
		result.status = deviceDataResult.status;
		return result;
	}

	result.device.reset( new Device( move( deviceDataResult.message.device_desc ) ) );
	result.status = RequestStatus::Success;
	return result;
}

UpdateStatus Client::_checkForDeviceUpdates() noexcept
{
	if (_isDeviceListOutOfDate)
	{
		// Last time we found DeviceListUpdated message in the socket, and user haven't requested the new list yet,
		// no need to look again, keep reporting "out of date" until he calls requestDeviceList().
		return UpdateStatus::OutOfDate;
	}

	// Last time we checked there wasn't any DeviceListUpdated message, but it already might be now, so let's check.
	UpdateStatus status = checkForUpdateMessageArrival();
	if (status == UpdateStatus::OutOfDate)
	{
		// DeviceListUpdated message found, cache this discovery until user calls requestDeviceList().
		_isDeviceListOutOfDate = true;
	}

	return status;
}

RequestStatus Client::_switchToCustomMode( const Device & device )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< SetCustomMode >( device.idx ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_changeMode( const Device & device, const Mode & mode )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< UpdateMode >( device.idx, mode.idx, mode, _negotiatedProtocolVersion ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_saveMode( const Device & device, const Mode & mode )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< SaveMode >( device.idx, mode.idx, mode, _negotiatedProtocolVersion ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_setDeviceColor( const Device & device, Color color )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	std::vector< Color > allColorsInDevice( device.leds.size(), color );
	if (!sendMessage< UpdateLEDs >( device.idx, allColorsInDevice ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_setDeviceColors( const Device & device, const std::vector< Color > & colors )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< UpdateLEDs >( device.idx, colors ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_setZoneColor( const Zone & zone, Color color )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	std::vector< Color > allColorsInZone( zone.leds_count, color );
	if (!sendMessage< UpdateZoneLEDs >( zone.parentIdx, zone.idx, allColorsInZone ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_setZoneSize( const Zone & zone, uint32_t newSize )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< ResizeZone >( zone.parentIdx, zone.idx, newSize ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_setLEDColor( const LED & led, Color color )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< UpdateSingleLED >( led.parentIdx, led.idx, color ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

ProfileListResult Client::_requestProfileList()
{
	if (!_socket->isConnected())
	{
		return { RequestStatus::NotConnected, {} };
	}

	ProfileListResult result;

	bool sent = sendMessage< RequestProfileList >();
	if (!sent)
	{
		result.status = RequestStatus::SendRequestFailed;
		return result;
	}

	auto deviceDataResult = awaitMessage< ReplyProfileList >();
	if (deviceDataResult.status != RequestStatus::Success)
	{
		result.status = deviceDataResult.status;
		return result;
	}

	result.profiles = move( deviceDataResult.message.profiles );
	result.status = RequestStatus::Success;
	return result;
}

RequestStatus Client::_saveProfile( const std::string & profileName )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< RequestSaveProfile >( profileName ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_loadProfile( const std::string & profileName )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< RequestLoadProfile >( profileName ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

RequestStatus Client::_deleteProfile( const std::string & profileName )
{
	if (!_socket->isConnected())
	{
		return RequestStatus::NotConnected;
	}

	if (!sendMessage< RequestLoadProfile >( profileName ))
	{
		return RequestStatus::SendRequestFailed;
	}

	return RequestStatus::Success;
}

system_error_t Client::getLastSystemError() const noexcept
{
	return _socket->getLastSystemError();
}

string Client::getLastSystemErrorStr() const noexcept
{
	return getErrorString( getLastSystemError() );
}

string Client::getSystemErrorStr( system_error_t errorCode ) const noexcept
{
	return getErrorString( errorCode );
}


//======================================================================================================================
//  Client: exception-less wrappers of the API

#define CATCH_ALL( ... ) \
	catch (const std::exception & ex) { \
		fprintf( stderr, "Unexpected std::exception was thrown: %s\n", ex.what() ); \
		__VA_ARGS__ \
	} catch (...) { \
		fprintf( stderr, "Unexpected unknown exception was thrown\n" ); \
		__VA_ARGS__ \
	}

ConnectStatus Client::connect( const std::string & host, uint16_t port ) noexcept
{
	try {
		return _connect( host, port );
	} CATCH_ALL (
		return ConnectStatus::UnexpectedError;
	)
}

bool Client::disconnect() noexcept
{
	try {
		return _disconnect();
	} CATCH_ALL (
		return false;
	)
}

bool Client::setTimeout( std::chrono::milliseconds timeout ) noexcept
{
	return _setTimeout( timeout );
}

DeviceListResult Client::requestDeviceList() noexcept
{
	try {
		return _requestDeviceList();
	} CATCH_ALL (
		return { RequestStatus::UnexpectedError, {} };
	)
}

DeviceCountResult Client::requestDeviceCount() noexcept
{
	try {
		return _requestDeviceCount();
	} CATCH_ALL (
		return { RequestStatus::UnexpectedError, 0 };
	)
}

DeviceInfoResult Client::requestDeviceInfo( uint32_t deviceIdx ) noexcept
{
	try {
		return _requestDeviceInfo( deviceIdx );
	} CATCH_ALL (
		return { RequestStatus::UnexpectedError, {} };
	)
}

UpdateStatus Client::checkForDeviceUpdates() noexcept
{
	return _checkForDeviceUpdates();
}

RequestStatus Client::switchToCustomMode( const Device & device ) noexcept
{
	try {
		return _switchToCustomMode( device );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::changeMode( const Device & device, const Mode & mode ) noexcept
{
	try {
		return _changeMode( device, mode );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::saveMode( const Device & device, const Mode & mode ) noexcept
{
	try {
		return _saveMode( device, mode );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::setDeviceColor( const Device & device, Color color ) noexcept
{
	try {
		return _setDeviceColor( device, color );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::setDeviceColors( const Device & device, const std::vector< Color > & colors ) noexcept
{
	try {
		return _setDeviceColors( device, colors );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::setZoneColor( const Zone & zone, Color color ) noexcept
{
	try {
		return _setZoneColor( zone, color );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::setZoneSize( const Zone & zone, uint32_t newSize ) noexcept
{
	try {
		return _setZoneSize( zone, newSize );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::setLEDColor( const LED & led, Color color ) noexcept
{
	try {
		return _setLEDColor( led, color );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

ProfileListResult Client::requestProfileList()
{
	try {
		return _requestProfileList();
	} CATCH_ALL (
		return { RequestStatus::UnexpectedError, {} };
	)
}

RequestStatus Client::saveProfile( const std::string & profileName )
{
	try {
		return _saveProfile( profileName );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::loadProfile( const std::string & profileName )
{
	try {
		return _loadProfile( profileName );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}

RequestStatus Client::deleteProfile( const std::string & profileName )
{
	try {
		return _deleteProfile( profileName );
	} CATCH_ALL (
		return RequestStatus::UnexpectedError;
	)
}


//======================================================================================================================
//  Client: exception-oriented wrappers of the API

#ifndef NO_EXCEPTIONS

void Client::connectStatusToException( ConnectStatus status )
{
	switch (status)
	{
		case ConnectStatus::Success:
			return;
		case ConnectStatus::AlreadyConnected:
			throw UserError( enumString( status ) );
		case ConnectStatus::HostNotResolved:
		case ConnectStatus::ConnectFailed:
		case ConnectStatus::RequestVersionFailed:
		case ConnectStatus::VersionNotSupported:
		case ConnectStatus::SendNameFailed:
			throw ConnectionError( enumString( status ), getLastSystemError() );
		default:
			throw SystemError( enumString( status ), getLastSystemError() );
	}
}

void Client::requestStatusToException( RequestStatus status )
{
	switch (status)
	{
		case RequestStatus::Success:
			return;
		case RequestStatus::NotConnected:
			throw UserError( enumString( status ) );
		case RequestStatus::SendRequestFailed:
		case RequestStatus::ConnectionClosed:
		case RequestStatus::NoReply:
		case RequestStatus::InvalidReply:
			throw ConnectionError( enumString( status ), getLastSystemError() );
		default:
			throw SystemError( enumString( status ), getLastSystemError() );
	}
}

void Client::connectX( const std::string & host, uint16_t port )
{
	ConnectStatus status = _connect( host, port );
	connectStatusToException( status );
}

void Client::disconnectX()
{
	if (!_disconnect())
	{
		throw UserError( "The client is not connected." );
	}
}

void Client::setTimeoutX( std::chrono::milliseconds timeout )
{
	if (!_setTimeout( timeout ))
	{
		throw SystemError( "Failed to set timeout", getLastSystemError() );
	}
}

DeviceList Client::requestDeviceListX()
{
	DeviceListResult result = _requestDeviceList();
	requestStatusToException( result.status );
	return move( result.devices );
}

uint32_t Client::requestDeviceCountX()
{
	DeviceCountResult result = _requestDeviceCount();
	requestStatusToException( result.status );
	return result.count;
}

std::unique_ptr< Device > Client::requestDeviceInfoX( uint32_t deviceIdx )
{
	DeviceInfoResult result = _requestDeviceInfo( deviceIdx );
	requestStatusToException( result.status );
	return move( result.device );
}

bool Client::isDeviceListOutdatedX()
{
	UpdateStatus status = _checkForDeviceUpdates();
	switch (status)
	{
		case UpdateStatus::UpToDate:
			return false;
		case UpdateStatus::OutOfDate:
			return true;
		case UpdateStatus::ConnectionClosed:
		case UpdateStatus::UnexpectedMessage:
			throw ConnectionError( enumString( status ), getLastSystemError() );
		default:
			throw SystemError( enumString( status ), getLastSystemError() );
	}
}

void Client::switchToCustomModeX( const Device & device )
{
	RequestStatus status = _switchToCustomMode( device );
	requestStatusToException( status );
}

void Client::changeModeX( const Device & device, const Mode & mode )
{
	RequestStatus status = _changeMode( device, mode );
	requestStatusToException( status );
}

void Client::saveModeX( const Device & device, const Mode & mode )
{
	RequestStatus status = _saveMode( device, mode );
	requestStatusToException( status );
}

void Client::setDeviceColorX( const Device & device, Color color )
{
	RequestStatus status = _setDeviceColor( device, color );
	requestStatusToException( status );
}

void Client::setZoneColorX( const Zone & zone, Color color )
{
	RequestStatus status = _setZoneColor( zone, color );
	requestStatusToException( status );
}

void Client::setZoneSizeX( const Zone & zone, uint32_t newSize )
{
	RequestStatus status = _setZoneSize( zone, newSize );
	requestStatusToException( status );
}

void Client::setLEDColorX( const LED & led, Color color )
{
	RequestStatus status = _setLEDColor( led, color );
	requestStatusToException( status );
}

std::vector< std::string > Client::requestProfileListX()
{
	ProfileListResult result = _requestProfileList();
	requestStatusToException( result.status );
	return move( result.profiles );
}

void Client::saveProfileX( const std::string & profileName )
{
	RequestStatus status = _saveProfile( profileName );
	requestStatusToException( status );
}

void Client::loadProfileX( const std::string & profileName )
{
	RequestStatus status = _loadProfile( profileName );
	requestStatusToException( status );
}

void Client::deleteProfileX( const std::string & profileName )
{
	RequestStatus status = _deleteProfile( profileName );
	requestStatusToException( status );
}

#endif // NO_EXCEPTIONS


//======================================================================================================================
//  Client: helpers

template< typename Message, typename ... ConstructorArgs >
bool Client::sendMessage( ConstructorArgs ... args )
{
	Message message( args ... );

	// allocate buffer and serialize (header.message_size is calculated in constructor)
	std::vector< uint8_t > buffer( message.header.size() + message.header.message_size );
	BinaryOutputStream stream( buffer );
	message.serialize( stream, _negotiatedProtocolVersion );

	return _socket->send( buffer ) == SocketError::Success;
}

template< typename Message >
Client::RecvResult< Message > Client::awaitMessage() noexcept
{
	RecvResult< Message > result;

	do
	{
		// receive header into buffer
		array< uint8_t, Header::size() > headerBuffer; size_t received;
		SocketError headerStatus = _socket->receive( headerBuffer, received );
		if (headerStatus != SocketError::Success)
		{
			if (headerStatus == SocketError::ConnectionClosed)
				result.status = RequestStatus::ConnectionClosed;
			else if (headerStatus == SocketError::Timeout)
				result.status = RequestStatus::NoReply;
			else
				result.status = RequestStatus::ReceiveError;
			return result;
		}

		// parse and validate the header
		BinaryInputStream stream( headerBuffer );
		if (!result.message.header.deserialize( stream ))
		{
			result.status = RequestStatus::InvalidReply;
			return result;
		}

		// the server may have sent DeviceListUpdated messsage before it received our request
		if (result.message.header.message_type == MessageType::DEVICE_LIST_UPDATED)
		{
			// in that case just set our "out of date" flag and skip it for now
			_isDeviceListOutOfDate = true;
		}
	}
	while (result.message.header.message_type == MessageType::DEVICE_LIST_UPDATED);

	if (result.message.header.message_type != Message::thisType)
	{
		// the message is neither DeviceListUpdated, nor the type we expected
		result.status = RequestStatus::InvalidReply;
		return result;
	}

	// receive the message body
	vector< uint8_t > bodyBuffer;
	SocketError bodyStatus = _socket->receive( bodyBuffer, result.message.header.message_size );
	if (bodyStatus != SocketError::Success)
	{
		if (bodyStatus == SocketError::ConnectionClosed)
			result.status = RequestStatus::ConnectionClosed;
		else if (bodyStatus == SocketError::Timeout)
			result.status = RequestStatus::NoReply;
		else
			result.status = RequestStatus::ReceiveError;
		return result;
	}

	// parse and validate the body
	BinaryInputStream stream( bodyBuffer );
	if (!result.message.deserializeBody( stream, _negotiatedProtocolVersion ))
	{
		result.status = RequestStatus::InvalidReply;
	}
	else
	{
		result.status = RequestStatus::Success;
	}

	return result;
}

UpdateStatus Client::checkForUpdateMessageArrival() noexcept
{
	// We only need to check if there is any TCP message in the system input buffer, but don't wait for it.
	// So we switch the socket to non-blocking mode and try to receive.

	if (!_socket->setBlockingMode( false ))
	{
		return UpdateStatus::OtherSystemError;
	}

	auto enableBlockingAndReturn = [ this ]( UpdateStatus returnStatus )
	{
		if (!_socket->setBlockingMode( true ))
		{
			// This is bad, we changed the state of the socket and now we're unable to return it back.
			// So rather burn everything to the ground and start from the beginning, than let things be in undefined state.
			disconnect();
			return UpdateStatus::CantRestoreSocket;
		}
		else
		{
			return returnStatus;
		}
	};

	vector< uint8_t > buffer;
	SocketError status = _socket->receive( buffer, Header::size() );
	if (status == SocketError::WouldBlock)
	{
		// No message is currently in the socket, no indication that the device list is out of date.
		return enableBlockingAndReturn( UpdateStatus::UpToDate );
	}
	else if (status == SocketError::ConnectionClosed)
	{
		return enableBlockingAndReturn( UpdateStatus::ConnectionClosed );
	}
	else if (status != SocketError::Success)
	{
		return enableBlockingAndReturn( UpdateStatus::OtherSystemError );
	}

	// We have some message, so let's check what it is.

	Header header;
	BinaryInputStream stream( buffer );
	if (!header.deserialize( stream ) || header.message_type != MessageType::DEVICE_LIST_UPDATED)
	{
		// We received something, but something totally different than what we expected.
		return enableBlockingAndReturn( UpdateStatus::UnexpectedMessage );
	}
	else
	{
		// We have received a DeviceListUpdated message from the server,
		// signal to the user that he needs to request the list again.
		return enableBlockingAndReturn( UpdateStatus::OutOfDate );
	}
}


//======================================================================================================================


} // namespace orgb
