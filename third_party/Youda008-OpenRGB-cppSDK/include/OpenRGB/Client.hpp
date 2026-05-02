//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: OpenRGB network client
//======================================================================================================================

#ifndef OPENRGB_CLIENT_INCLUDED
#define OPENRGB_CLIENT_INCLUDED


#include "DeviceInfo.hpp"
#include "Color.hpp"
#include "SystemErrorType.hpp"  // HACK: read the comment at the top of that header file

#include <string>  // client name
#include <memory>  // unique_ptr<Socket>
#include <chrono>  // timeout

namespace own {
	class TcpSocket;
}


namespace orgb {


constexpr uint16_t defaultPort = 6742;


//======================================================================================================================

/// All the possible ways how the connect operation can end up
enum class ConnectStatus
{
	Success,                 ///< The operation was successful.
	NetworkingInitFailed,    ///< Operation failed because underlying networking system could not be initialized. Call getLastSystemError() for more info.
	AlreadyConnected,        ///< Connect operation failed because the socket is already connected. Call disconnect() first.
	HostNotResolved,         ///< The hostname you entered could not be resolved to IP address. Call getLastSystemError() for more info.
	ConnectFailed,           ///< Could not connect to the target server, either it's down or the port is closed. Call getLastSystemError() for more info.
	RequestVersionFailed,    ///< Failed to send the client's protocol version or receive the server's protocol version. Call getLastSystemError() for more info.
	VersionNotSupported,     ///< The protocol version of the server is not supported. Please update the OpenRGB app.
	SendNameFailed,          ///< Failed to send the client name to the server. Call getLastSystemError() for more info.
	OtherSystemError,        ///< Other system error. Call getLastSystemError() for more info.
	UnexpectedError,         ///< Internal error of this library. This should not happen unless there is a mistake in the code, please create a github issue.
};
const char * enumString( ConnectStatus status ) noexcept;

/// All the possible ways how a request can end up
enum class RequestStatus
{
	Success,            ///< The request was succesful.
	NotConnected,       ///< Request failed because the client is not connected. Call connect() first.
	SendRequestFailed,  ///< Failed to send the request message.
	ConnectionClosed,   ///< Server has closed the connection.
	NoReply,            ///< No reply has arrived from the server in given timeout. In case this happens too often, you may try to increase the timeout.
	ReceiveError,       ///< There has been some other error while trying to receive a reply. Call getLastSystemError() for more info.
	InvalidReply,       ///< The reply from the server is invalid.
	UnexpectedError,    ///< Internal error of this library. This should not happen unless there is a mistake in the code, please create a github issue.
};
const char * enumString( RequestStatus status ) noexcept;

/// All the possible results of a check whether the locally stored device list is out of date
enum class UpdateStatus
{
	UpToDate,           ///< The current device list seems up to date.
	OutOfDate,          ///< Server has sent a notification message indicating that the device list has changed. Call requestDeviceList() again.
	ConnectionClosed,   ///< Server has closed the connection.
	UnexpectedMessage,  ///< Server has sent some other kind of message that we didn't expect.
	CantRestoreSocket,  ///< Error has occured while trying to restore socket to its original state and the socket has been closed. Call getLastSystemError() for more info. This should never happen, but one never knows.
	OtherSystemError,   ///< Other system error. Call getLastSystemError() for more info.
	UnexpectedError,    ///< Internal error of this library. This should not happen unless there is a mistake in the code, please create a github issue.
};
const char * enumString( UpdateStatus status ) noexcept;

/// Result and output of a device list request
struct DeviceListResult
{
	RequestStatus status;  ///< whether the request suceeded or why it didn't
	DeviceList devices;    ///< output of a successfull request
};

/// Result and output of a device count request
struct DeviceCountResult
{
	RequestStatus status;  ///< whether the request suceeded or why it didn't
	uint32_t count;        ///< output of a successfull request
};

/// Result and output of a single device request
struct DeviceInfoResult
{
	RequestStatus status;  ///< whether the request suceeded or why it didn't
	std::unique_ptr< Device > device;  ///< output of a successfull request
	// The device has to be a pointer because user is not allowed to use the constructors.
};

/// Result and output of a profile list request
struct ProfileListResult
{
	RequestStatus status;  ///< whether the request suceeded or why it didn't
	std::vector< std::string > profiles;  ///< output of a successfull request
};


//======================================================================================================================
/// OpenRGB network client.
/** Use this to communicate with the OpenRGB service in order to set colors on your RGB devices. */

class Client
{

 public:

	/// Creates a client of specified or default name. Does not connect anywhere yet.
	Client( const std::string & clientName = "orgb::Client" ) noexcept;

	~Client() noexcept;

	// The connection cannot be shared.
	Client( const Client & other ) = delete;

	Client( Client && other ) noexcept = default;
	Client & operator=( Client && other ) noexcept = default;

	/// Tells whether the client is currently connected to a server.
	bool isConnected() const noexcept;

	//-- return-value-oriented exception-less API ----------------------------------------------------------------------

	/// Connects to the OpenRGB server determined by a host name and announces our client name.
	ConnectStatus connect( const std::string & host, uint16_t port = defaultPort ) noexcept;

	/// Closes connection to the server.
	/** It will return false if the client is not connected or some rare system error occurs. */
	bool disconnect() noexcept;

	/// Sets a timeout for receiving request answers.
	bool setTimeout( std::chrono::milliseconds timeout ) noexcept;

	/// Queries the server for information about all its RGB devices.
	DeviceListResult requestDeviceList() noexcept;

	/// Queries the server for the number of its RGB devices.
	/** This is useful when for some reason you want to request the devices manually one by one. */
	DeviceCountResult requestDeviceCount() noexcept;

	/// Queries the server for information about a single RGB devices.
	/** After you set a color or change a mode, you can optionally use this to update */
	DeviceInfoResult requestDeviceInfo( uint32_t deviceIdx ) noexcept;

	/// Checks if the device list you downloaded earlier via requestDeviceList() hasn't been changed on the server.
	/** In case it has been changed, you need to call requestDeviceList() again. */
	UpdateStatus checkForDeviceUpdates() noexcept;

	/// Switches the device to a directly controlled color mode.
	/** This seems unsupported by many RGB controllers, and it's probably deprecated in the OpenRGB app. */
	RequestStatus switchToCustomMode( const Device & device ) noexcept;

	/// Updates the parameters of a mode and also switches the device to this mode.
	/** If you just want to switch the mode, use one of the Mode objects received from the server via requestDeviceList().
	  * If you want to change the parameters of a mode, create a copy of the Mode object, change the parameters of the copy
	  * and pass the copy to this function. */
	RequestStatus changeMode( const Device & device, const Mode & mode ) noexcept;

	/// Saves the mode parameters into the device memory to make it persistent??
	/** I don't really know what this does, ask the OpenRGB devs. */
	RequestStatus saveMode( const Device & device, const Mode & mode ) noexcept;

	/// Sets one unified color for the whole device.
	RequestStatus setDeviceColor( const Device & device, Color color ) noexcept;

	/// Sets per-LED colors for the whole device in a single bulk request.
	/** \p colors must have the same number of elements as device.leds. */
	RequestStatus setDeviceColors( const Device & device, const std::vector< Color > & colors ) noexcept;

	/// Sets a color of a particular zone of a device.
	RequestStatus setZoneColor( const Zone & zone, Color color ) noexcept;

	/// Resizes a zone of leds, if the device supports it.
	RequestStatus setZoneSize( const Zone & zone, uint32_t newSize ) noexcept;

	/// Sets a color of a single selected LED.
	RequestStatus setLEDColor( const LED & led, Color color ) noexcept;

	/// Queries the server for a list of saved profiles.
	ProfileListResult requestProfileList();

	/// Saves the current configuration of all devices under a new profile name.
	RequestStatus saveProfile( const std::string & profileName );

	/// Applies an existing profile.
	RequestStatus loadProfile( const std::string & profileName );

	/// Removes an existing profile.
	RequestStatus deleteProfile( const std::string & profileName );

#ifndef NO_EXCEPTIONS

	//-- exception-oriented API ----------------------------------------------------------------------------------------

	/// Exception-throwing variant of connect( const std::string &, uint16_t ).
	/** \throws UserError when the client is already connected
	  * \throws ConnectionError when the network is down, such host does not exist or the host refuses connection
	  * \throws SystemError when there was an error inside the operating system */
	void connectX( const std::string & host, uint16_t port = 6742 );

	/// Exception-throwing variant of disconnect().
	/** \throws UserError when the client is not connected */
	void disconnectX();

	/// Exception-throwing variant of setTimeout().
	/** \throws SystemError when there was an error inside the operating system */
	void setTimeoutX( std::chrono::milliseconds timeout );

	/// Exception-throwing variant of requestDeviceList().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent or no valid reply was received
	  * \throws SystemError when there was an error inside the operating system */
	DeviceList requestDeviceListX();

	/// Exception-throwing variant of requestDeviceCount().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent or no valid reply was received
	  * \throws SystemError when there was an error inside the operating system */
	uint32_t requestDeviceCountX();

	/// Exception-throwing variant of requestDeviceInfo().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent or no valid reply was received
	  * \throws SystemError when there was an error inside the operating system */
	std::unique_ptr< Device > requestDeviceInfoX( uint32_t deviceIdx );

	/// Exception-throwing variant of checkForDeviceUpdates().
	/** \throws ConnectionError when the server closes the connection or sends an invalid packet
	  * \throws SystemError when there was an error inside the operating system */
	bool isDeviceListOutdatedX();

	/// Exception-throwing variant of switchToCustomMode().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void switchToCustomModeX( const Device & device );

	/// Exception-throwing variant of changeMode().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void changeModeX( const Device & device, const Mode & mode );

	/// Exception-throwing variant of saveMode().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void saveModeX( const Device & device, const Mode & mode );

	/// Exception-throwing variant of setDeviceColor().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void setDeviceColorX( const Device & device, Color color );

	/// Exception-throwing variant of setZoneColor().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void setZoneColorX( const Zone & zone, Color color );

	/// Exception-throwing variant of setZoneSize().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void setZoneSizeX( const Zone & zone, uint32_t newSize );

	/// Exception-throwing variant of setLEDColor().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void setLEDColorX( const LED & led, Color color );

	/// Exception-throwing variant of requestProfileList().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent or no valid reply was received
	  * \throws SystemError when there was an error inside the operating system */
	std::vector< std::string > requestProfileListX();

	/// Exception-throwing variant of saveProfile().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void saveProfileX( const std::string & profileName );

	/// Exception-throwing variant of loadProfile().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void loadProfileX( const std::string & profileName );

	/// Exception-throwing variant of deleteProfile().
	/** \throws UserError when the client is not connected
	  * \throws ConnectionError when a request couldn't be sent
	  * \throws SystemError when there was an error inside the operating system */
	void deleteProfileX( const std::string & profileName );

#endif // NO_EXCEPTIONS

	/// Returns the system error code that caused the last failure.
	system_error_t getLastSystemError() const noexcept;

	/// Converts the numeric value of the last system error to a user-friendly string.
	std::string getLastSystemErrorStr() const noexcept;

	/// Converts the given numeric error code to a user-friendly string.
	std::string getSystemErrorStr( system_error_t errorCode ) const noexcept;

 private: // helpers

	ConnectStatus _connect( const std::string & host, uint16_t port );
	bool _disconnect() noexcept;
	bool _setTimeout( std::chrono::milliseconds timeout ) noexcept;
	DeviceListResult _requestDeviceList();
	DeviceCountResult _requestDeviceCount();
	DeviceInfoResult _requestDeviceInfo( uint32_t deviceIdx );
	UpdateStatus _checkForDeviceUpdates() noexcept;
	RequestStatus _switchToCustomMode( const Device & device );
	RequestStatus _changeMode( const Device & device, const Mode & mode );
	RequestStatus _saveMode( const Device & device, const Mode & mode );
	RequestStatus _setDeviceColor( const Device & device, Color color );
	RequestStatus _setDeviceColors( const Device & device, const std::vector< Color > & colors );
	RequestStatus _setZoneColor( const Zone & zone, Color color );
	RequestStatus _setZoneSize( const Zone & zone, uint32_t newSize );
	RequestStatus _setLEDColor( const LED & led, Color color );
	ProfileListResult _requestProfileList();
	RequestStatus _saveProfile( const std::string & profileName );
	RequestStatus _loadProfile( const std::string & profileName );
	RequestStatus _deleteProfile( const std::string & profileName );

	template< typename Message, typename ... ConstructorArgs >
	bool sendMessage( ConstructorArgs ... args );

	template< typename Message >
	struct RecvResult
	{
		RequestStatus status;
		Message message;
	};
	template< typename Message >
	RecvResult< Message > awaitMessage() noexcept;

	UpdateStatus checkForUpdateMessageArrival() noexcept;

#ifndef NO_EXCEPTIONS
	void connectStatusToException( ConnectStatus status );
	void requestStatusToException( RequestStatus status );
#endif // NO_EXCEPTIONS

 private:

	std::string _clientName;

	// a pointer so that we don't have to include the TcpSocket and all its OS dependancies here
	std::unique_ptr< own::TcpSocket > _socket;

	uint32_t _negotiatedProtocolVersion;

	bool _isDeviceListOutOfDate;

};


//======================================================================================================================


} // namespace orgb


#endif // OPENRGB_CLIENT_INCLUDED
