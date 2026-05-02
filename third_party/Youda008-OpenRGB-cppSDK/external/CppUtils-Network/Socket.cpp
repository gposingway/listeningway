//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: abstraction over low-level system socket calls
//======================================================================================================================

#include "Socket.hpp"

#include <CppUtils-Essential/LangUtils.hpp>   // scope_guard
#include <CppUtils-Essential/CriticalError.hpp>

#ifdef _WIN32
	#include <winsock2.h>      // socket, closesocket
	#include <ws2tcpip.h>      // addrinfo

	using in_addr_t = unsigned long;  // linux has in_addr_t, windows has unsigned long
	using socklen_t = int;            // linux has socklen_t, windows has int

	constexpr own::socket_t INVALID_SOCK = INVALID_SOCKET;
	constexpr own::system_error_t SUCCESS = ERROR_SUCCESS;
#else
	#include <unistd.h>        // open, close, read, write
	#include <fcntl.h>         // fnctl, O_NONBLOCK
	#include <sys/select.h>    // select
	#include <sys/socket.h>    // socket
	#include <netdb.h>         // getaddrinfo, gethostbyname
	#include <netinet/in.h>    // sockaddr_in, in_addr, ntoh, hton
	#include <arpa/inet.h>     // inet_addr, inet_ntoa

	constexpr own::socket_t INVALID_SOCK = -1;
	constexpr own::system_error_t SUCCESS = 0;
#endif // _WIN32

#include <mutex>
#include <cstring>  // memset, strlen


namespace own {


//======================================================================================================================
//  error strings

const char * enumString( SocketError error ) noexcept
{
	switch (error)
	{
		case SocketError::Success:              return "Success";
		case SocketError::AlreadyConnected:     return "AlreadyConnected";
		case SocketError::NotConnected:         return "NotConnected";
		case SocketError::NetworkingInitFailed: return "NetworkingInitFailed";
		case SocketError::HostNotResolved:      return "HostNotResolved";
		case SocketError::ConnectFailed:        return "ConnectFailed";
		case SocketError::SendFailed:           return "SendFailed";
		case SocketError::ConnectionClosed:     return "ConnectionClosed";
		case SocketError::Timeout:              return "Timeout";
		case SocketError::WouldBlock:           return "WouldBlock";
		case SocketError::AlreadyOpen:          return "AlreadyOpen";
		case SocketError::BindFailed:           return "BindFailed";
		case SocketError::ListenFailed:         return "ListenFailed";
		default:                                return "Other";
	}
}


//======================================================================================================================
//  network subsystem initialization and automatic termination

/// Represents OS-dependent networking subsystem.
/** This is a private class for internal use. */
class NetworkingSubsystem
{

 public:

	NetworkingSubsystem() noexcept : _initialized( false ) {}

	bool initializeIfNotAlready() noexcept
	{
		// this additional check is optimization for cases where it's already initialized, which is gonna be the majority
		if (_initialized)
			return true;

		// this method may get called from multiple threads, so we need to make sure they don't race
		std::unique_lock< std::mutex > lock( mtx );
		if (!_initialized)
		{
			_initialized = _initialize();
		}
		return _initialized;
	}

	~NetworkingSubsystem() noexcept
	{
		if (_initialized)
		{
			_terminate();
		}
	}

 private:

	bool _initialize() noexcept
	{
 #ifdef _WIN32
		return WSAStartup( MAKEWORD(2, 2), &_wsaData ) == NO_ERROR;
 #else
		return true;
 #endif // _WIN32
	}

	void _terminate() noexcept
	{
 #ifdef _WIN32
		WSACleanup();
 #else
 #endif // _WIN32
	}

 private:

 #ifdef _WIN32
	WSADATA _wsaData;
 #else
 #endif // _WIN32

	std::mutex mtx;
	bool _initialized;

};

static NetworkingSubsystem g_netSystem;  // this will get initialized on first use and terminated on process exit


//======================================================================================================================
//  common low-level operations

static bool _shutdownSocket( socket_t sock ) noexcept
{
 #ifdef _WIN32
	return ::shutdown( sock, SD_BOTH ) == 0;
 #else
	return ::shutdown( sock, SHUT_RDWR ) == 0;
 #endif // _WIN32
}

static bool _closeSocket( socket_t sock ) noexcept
{
 #ifdef _WIN32
	return ::closesocket( sock ) == 0;
 #else
	return ::close( sock ) == 0;
 #endif // _WIN32
}

static bool _setTimeout( socket_t sock, std::chrono::milliseconds timeout_ms ) noexcept
{
 #ifdef _WIN32
	DWORD timeout = (DWORD)timeout_ms.count();
 #else
	struct timeval timeout;
	timeout.tv_sec  = long( timeout_ms.count() / 1000 );
	timeout.tv_usec = long( timeout_ms.count() % 1000 ) * 1000;
 #endif // _WIN32

	return ::setsockopt( sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout) ) == 0;
}

static bool _isTimeout( system_error_t errorCode ) noexcept
{
 #ifdef _WIN32
	return errorCode == WSAETIMEDOUT;
 #else
	return errorCode == EAGAIN || errorCode == EWOULDBLOCK || errorCode == ETIMEDOUT;
 #endif // _WIN32
}

static bool _isWouldBlock( system_error_t errorCode ) noexcept
{
 #ifdef _WIN32
	return errorCode == WSAEWOULDBLOCK;
 #else
	return errorCode == EAGAIN || errorCode == EWOULDBLOCK;
 #endif // _WIN32
}

static bool _setBlockingMode( socket_t sock, bool enable ) noexcept
{
#ifdef _WIN32
	unsigned long mode = enable ? 0 : 1;
	return ::ioctlsocket( sock, (long)FIONBIO, &mode ) == 0;
#else
	int flags = ::fcntl( sock, F_GETFL, 0 );
	if (flags == -1)
		return false;

	if (enable)
		flags &= ~O_NONBLOCK;
	else
		flags |= O_NONBLOCK;

	return ::fcntl( sock, F_SETFL, flags ) == 0;
#endif
}


//======================================================================================================================
//  ASocket

ASocket::ASocket() noexcept
:
	_socket( INVALID_SOCK ),
	_lastSystemError( SUCCESS ),
	_isBlocking( true )
{}

ASocket::ASocket( socket_t sock ) noexcept
:
	_socket( sock ),
	_lastSystemError( SUCCESS ),
	_isBlocking( true )
{}

ASocket::~ASocket() noexcept {}

ASocket::ASocket( ASocket && other ) noexcept
{
	*this = move( other );
}

ASocket & ASocket::operator=( ASocket && other ) noexcept
{
	_socket = other._socket;
	_lastSystemError = other._lastSystemError;
	_isBlocking = other._isBlocking;
	other._socket = INVALID_SOCK;
	other._lastSystemError = 0;
	other._isBlocking = false;

	return *this;
}

bool ASocket::setBlockingMode( bool enable ) noexcept
{
	bool success = _setBlockingMode( _socket, enable );
	if (success)
		_isBlocking = enable;
	else
		_lastSystemError = getLastError();
	return success;
}


//======================================================================================================================
//  TcpSocket

TcpSocket::TcpSocket() noexcept : ASocket() {}

TcpSocket::~TcpSocket() noexcept
{
	disconnect();
}

TcpSocket::TcpSocket( TcpSocket && other ) noexcept
{
	*this = move( other );
}

TcpSocket & TcpSocket::operator=( TcpSocket && other ) noexcept
{
	ASocket::operator=( move( other ) );
	return *this;
}

SocketError TcpSocket::connect( const std::string & host, uint16_t port ) noexcept
{
	if (isConnected())
	{
		return SocketError::AlreadyConnected;
	}

	bool initialized = g_netSystem.initializeIfNotAlready();
	if (!initialized)
	{
		_lastSystemError = getLastError();
		return SocketError::NetworkingInitFailed;
	}

	char portStr [8];
	snprintf( portStr, sizeof(portStr), "%hu", ushort(port) );

	struct addrinfo hint;
	memset( &hint, 0, sizeof(hint) );
	hint.ai_family = AF_UNSPEC;      // IPv4 or IPv6, it doesn't matter
	hint.ai_socktype = SOCK_STREAM;  // but only TCP!

	// find protocol family and address of the host
	struct addrinfo * ainfo;
	if (::getaddrinfo( host.c_str(), portStr, &hint, &ainfo ) != SUCCESS)
	{
		_lastSystemError = getLastError();
		return SocketError::HostNotResolved;
	}
	auto ainfo_guard = at_scope_end_do( [ &ainfo ]() { ::freeaddrinfo( ainfo ); } );

	return _connect( ainfo->ai_family, (int)ainfo->ai_addrlen, ainfo->ai_addr );
}

SocketError TcpSocket::connect( const IPAddr & addr, uint16_t port )
{
	if (isConnected())
	{
		return SocketError::AlreadyConnected;
	}

	bool initialized = g_netSystem.initializeIfNotAlready();
	if (!initialized)
	{
		_lastSystemError = getLastError();
		return SocketError::NetworkingInitFailed;
	}

	struct sockaddr_storage saddr; int addrlen;
	endpointToSockaddr( { addr, port }, (struct sockaddr *)&saddr, addrlen );

	return _connect( saddr.ss_family, addrlen, (struct sockaddr *)&saddr );
}

SocketError TcpSocket::_connect( int family, int addrlen, struct sockaddr * addr ) noexcept
{
	// create a corresponding socket
	// The Winsock2 sockets are not reusable (after calling shutdown(), a new socket has to be created),
	// so it's pointless to split this into socket creation (in constructor) and socket connection (here).
	_socket = ::socket( family, SOCK_STREAM, 0 );
	if (_socket == INVALID_SOCK)
	{
		_lastSystemError = getLastError();
		return SocketError::Other;
	}

	if (::connect( _socket, addr, addrlen ) != SUCCESS)
	{
		_lastSystemError = getLastError();
		_closeSocket( _socket );
		_socket = INVALID_SOCK;
		return SocketError::ConnectFailed;
	}

	_lastSystemError = getLastError();
	return SocketError::Success;
}

SocketError TcpSocket::disconnect() noexcept
{
	if (!isConnected())
	{
		return SocketError::NotConnected;
	}

	if (!_shutdownSocket( _socket ))
	{
		critical_error( "shutdown(socket) should not fail, please investigate, error code = %d", getLastError() );
	}

	if (!_closeSocket( _socket ))
	{
		critical_error( "close(socket) should not fail, please investigate, error code = %d", getLastError() );
	}

	_lastSystemError = getLastError();
	_socket = INVALID_SOCK;
	return SocketError::Success;
}

bool TcpSocket::isConnected() const noexcept
{
	return _socket != INVALID_SOCK;
}

bool TcpSocket::isAccepted() const noexcept
{
	return _socket != INVALID_SOCK;
}

bool TcpSocket::setTimeout( std::chrono::milliseconds timeout ) noexcept
{
	bool success = _setTimeout( _socket, timeout );
	_lastSystemError = getLastError();
	return success;
}

SocketError TcpSocket::send( const_byte_span buffer ) noexcept
{
	if (!isConnected())
	{
		return SocketError::NotConnected;
	}

	const uint8_t * sendBegin = buffer.data();
	size_t sendSize = buffer.size();
	while (sendSize > 0)
	{
		int sent = ::send( _socket, (const char *)sendBegin, (int)sendSize, 0 );
		if (sent < 0)
		{
			_lastSystemError = getLastError();
			return SocketError::SendFailed;
		}
		sendBegin += sent;
		sendSize -= size_t( sent );
	}

	_lastSystemError = getLastError();
	return SocketError::Success;
}

// TODO: Add support for proper cancellation of blocking recv from another thread.
//       Calling close/disconnect from another thread is a hack and only works by accident.

SocketError TcpSocket::receive( byte_span buffer, size_t & totalReceived ) noexcept
{
	if (!isConnected())
	{
		return SocketError::NotConnected;
	}

	uint8_t * recvBegin = buffer.data();
	size_t recvSize = buffer.size();
	while (recvSize > 0)
	{
		int received = ::recv( _socket, (char *)recvBegin, (int)recvSize, 0 );
		if (received <= 0)
		{
			_lastSystemError = getLastError();
			totalReceived = buffer.size() - recvSize;  // this is how much we failed to receive

			if (received == 0)
			{
				_closeSocket( _socket );  // server closed, so let's close on our side too
				_socket = INVALID_SOCK;
				return SocketError::ConnectionClosed;
			}
			else if (!_isBlocking && _isWouldBlock( _lastSystemError ))
			{
				return SocketError::WouldBlock;
			}
			else if (_isTimeout( _lastSystemError ))
			{
				return SocketError::Timeout;
			}
			else
			{
				return SocketError::Other;
			}
		}
		recvBegin += received;
		recvSize -= size_t( received );
	}

	_lastSystemError = getLastError();
	totalReceived = buffer.size();
	return SocketError::Success;
}

SocketError TcpSocket::receiveOnce( std::vector< uint8_t > & buffer ) noexcept
{
	if (!isConnected())
	{
		return SocketError::NotConnected;
	}

	// The maximum size of data we can receive is limited by the maximum size of a TCP packet.
	// In theory it can be up to 65536, but in reality it won't be bigger than 1500 most of the time.
	// Some networks support jumbo frames that have up to 9000 bytes, so this should really cover 99%
	// of the cases while not requiring second dynamic allocation and not using too much stack.
	uint8_t tempBuffer [10*1024];

	int received = ::recv( _socket, (char *)tempBuffer, (int)sizeof(tempBuffer), 0 );
	if (received <= 0)
	{
		_lastSystemError = getLastError();
		if (received == 0)
		{
			_closeSocket( _socket );  // server closed, so let's close on our side too
			_socket = INVALID_SOCK;
			return SocketError::ConnectionClosed;
		}
		else if (!_isBlocking && _isWouldBlock( _lastSystemError ))
		{
			return SocketError::WouldBlock;
		}
		else if (_isTimeout( _lastSystemError ))
		{
			return SocketError::Timeout;
		}
		else
		{
			return SocketError::Other;
		}
	}

	buffer.resize( received );
	buffer.assign( tempBuffer, tempBuffer + received );

	_lastSystemError = getLastError();
	return SocketError::Success;
}

//======================================================================================================================
//  TcpServerSocket

TcpServerSocket::TcpServerSocket() noexcept : ASocket() {}

TcpServerSocket::~TcpServerSocket() noexcept
{
	close();
}

TcpServerSocket::TcpServerSocket( TcpServerSocket && other ) noexcept
{
	*this = move( other );
}

TcpServerSocket & TcpServerSocket::operator=( TcpServerSocket && other ) noexcept
{
	return static_cast< TcpServerSocket & >( ASocket::operator=( move( other ) ) );
}

SocketError TcpServerSocket::open( uint16_t port ) noexcept
{
	if (_socket != INVALID_SOCK)
	{
		return SocketError::AlreadyOpen;
	}

	bool initialized = g_netSystem.initializeIfNotAlready();
	if (!initialized)
	{
		_lastSystemError = getLastError();
		return SocketError::NetworkingInitFailed;
	}

	// TODO: IPv4 vs IPv6
	struct sockaddr_in saddr;
	inet_pton( AF_INET, "127.0.0.1", &saddr.sin_addr );
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons( port );

	// create a corresponding socket
	_socket = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if (_socket == INVALID_SOCK)
	{
		_lastSystemError = getLastError();
		return SocketError::Other;
	}

	// bind the socket to a local port
	if (::bind( _socket, (sockaddr *)&saddr, sizeof(saddr) ) != 0)
	{
		_lastSystemError = getLastError();
		_closeSocket( _socket );
		_socket = INVALID_SOCK;
		return SocketError::BindFailed;
	}

	// set the socket to a listen state
	static constexpr uint BACKLOG = 16;  // system queue for incoming connection requests TODO: user specified
	if (::listen( _socket, BACKLOG ) != 0)
	{
		_lastSystemError = getLastError();
		_closeSocket( _socket );
		_socket = INVALID_SOCK;
		return SocketError::ListenFailed;
	}

	_lastSystemError = getLastError();
	return SocketError::Success;
}

SocketError TcpServerSocket::close() noexcept
{
	if (!isOpen())
	{
		return SocketError::NotOpen;
	}

	if (!_closeSocket( _socket ))
	{
		critical_error( "close(socket) should not fail, please investigate, error code = %d", getLastError() );
	}

	_lastSystemError = getLastError();
	_socket = INVALID_SOCK;
	return SocketError::Success;
}

bool TcpServerSocket::isOpen() const noexcept
{
	return _socket != INVALID_SOCK;
}

TcpSocket TcpServerSocket::accept( Endpoint & endpoint )
{
	if (!isOpen())
	{
		return TcpSocket();
	}

	struct sockaddr_storage clientAddr;
	socklen_t claddrSize = sizeof(clientAddr);

	socket_t clientSocket = ::accept( _socket, (struct sockaddr *)&clientAddr, &claddrSize );
	if (clientSocket == INVALID_SOCK)
	{
		_lastSystemError = getLastError();
		return TcpSocket();
	}

	if (!sockaddrToEndpoint( (struct sockaddr *)&clientAddr, endpoint ))
	{
		critical_error( "Socket operation returned unexpected address family." );
	}

	_lastSystemError = getLastError();
	return TcpSocket( clientSocket );
}


//======================================================================================================================
//  UdpSocket

UdpSocket::UdpSocket() noexcept : ASocket() {}

UdpSocket::~UdpSocket() noexcept {}  // delegate to ASocket

UdpSocket::UdpSocket( UdpSocket && other ) noexcept
{
	*this = move( other );
}

UdpSocket & UdpSocket::operator=( UdpSocket && other ) noexcept
{
	return static_cast< UdpSocket & >( ASocket::operator=( move( other ) ) );
}

SocketError UdpSocket::open( uint16_t port ) noexcept
{
	if (_socket != INVALID_SOCK)
	{
		return SocketError::AlreadyOpen;
	}

	bool initialized = g_netSystem.initializeIfNotAlready();
	if (!initialized)
	{
		_lastSystemError = getLastError();
		return SocketError::NetworkingInitFailed;
	}

	// TODO: IPv4 vs IPv6
	struct sockaddr_in saddr;
	inet_pton( AF_INET, "127.0.0.1", &saddr.sin_addr );
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons( port );

	// create a corresponding socket
	_socket = ::socket( AF_INET, SOCK_DGRAM, 0 );
	if (_socket == INVALID_SOCK)
	{
		_lastSystemError = getLastError();
		return SocketError::Other;
	}

	// bind the socket to a local port
	if (port != 0)
	{
		if (::bind( _socket, (sockaddr *)&saddr, sizeof(saddr) ) != 0)
		{
			_lastSystemError = getLastError();
			_closeSocket( _socket );
			_socket = INVALID_SOCK;
			return SocketError::BindFailed;
		}
	}

	_lastSystemError = getLastError();
	return SocketError::Success;
}

SocketError UdpSocket::close() noexcept
{
	if (!isOpen())
	{
		return SocketError::NotOpen;
	}

	if (!_shutdownSocket( _socket ))
	{
		critical_error( "shutdown(socket) should not fail, please investigate, error code = %d", getLastError() );
	}

	if (!_closeSocket( _socket ))
	{
		critical_error( "close(socket) should not fail, please investigate, error code = %d", getLastError() );
	}

	_lastSystemError = getLastError();
	_socket = INVALID_SOCK;
	return SocketError::Success;
}

bool UdpSocket::isOpen() const noexcept
{
	return _socket != INVALID_SOCK;
}

SocketError UdpSocket::sendTo( const Endpoint & endpoint, const_byte_span buffer )
{
	struct sockaddr_storage saddr; int addrlen;
	endpointToSockaddr( endpoint, (struct sockaddr *)&saddr, addrlen );

	int sent = ::sendto( _socket, (const char *)buffer.data(), (int)buffer.size(), 0, (struct sockaddr *)&saddr, addrlen );
	if (sent < 0)
	{
		_lastSystemError = getLastError();
		return SocketError::SendFailed;
	}

	_lastSystemError = getLastError();
	return SocketError::Success;
}

SocketError UdpSocket::recvFrom( Endpoint & endpoint, byte_span buffer, size_t & totalReceived )
{
	struct sockaddr_storage saddr; socklen_t addrlen;
	memset( &saddr, 0, sizeof(saddr) );
	addrlen = sizeof(saddr);

	int received = ::recvfrom( _socket, (char *)buffer.data(), (int)buffer.size(), 0, (struct sockaddr *)&saddr, &addrlen );
	if (received < 0)
	{
		_lastSystemError = getLastError();
		if (!_isBlocking && _isWouldBlock( _lastSystemError ))
		{
			return SocketError::WouldBlock;
		}
		else if (_isTimeout( _lastSystemError ))
		{
			return SocketError::Timeout;
		}
		else
		{
			return SocketError::Other;
		}
	}

	if (!sockaddrToEndpoint( (struct sockaddr *)&saddr, endpoint ))
	{
		critical_error( "Socket operation returned unexpected address family." );
	}

	totalReceived = size_t( received );
	_lastSystemError = getLastError();
	return SocketError::Success;
}


//======================================================================================================================
//  convenience wrappers

SocketError TcpSocket::send( const char * message ) noexcept
{
	return send( make_span( message, strlen(message) ).as_bytes() );
}

SocketError UdpSocket::sendTo( const Endpoint & endpoint, const char * message )
{
	return sendTo( endpoint, make_span( message, strlen(message) ).as_bytes() );
}

SocketError TcpSocket::receive( std::vector< uint8_t > & buffer, size_t size ) noexcept
{
	buffer.resize( size );  // allocate the needed storage
	size_t received;
	SocketError result = receive( make_span( buffer ), received );
	buffer.resize( received );  // let's return the user a vector only as big as how much we actually received
	return result;
}


//======================================================================================================================
//  multi-socket operations

// TODO: more detailed error
bool waitForAny( const std::unordered_set< ASocket * > & activeSockets, std::vector< ASocket * > & readySockets, std::chrono::milliseconds timeout_ms )
{
	fd_set fdset;
	FD_ZERO( &fdset );

	socket_t maxSocketFd = 0;
	for (ASocket * socket : activeSockets)
	{
		socket_t socketFd = socket->getSystemHandle();
		FD_SET( socketFd, &fdset );
		if (socketFd > maxSocketFd)
			maxSocketFd = socketFd;
	}

	struct timeval timeout;
	timeout.tv_sec  = long( timeout_ms.count() / 1000 );
	timeout.tv_usec = long( timeout_ms.count() % 1000 ) * 1000;

	if (::select( int( maxSocketFd ), &fdset, nullptr, nullptr, &timeout ) < 0)
	{
		return false;
	}

	readySockets.reserve( activeSockets.size() );
	for (ASocket * socket : activeSockets)
	{
		socket_t socketFd = socket->getSystemHandle();
		if (FD_ISSET( socketFd, &fdset ))
		{
			readySockets.push_back( socket );
		}
	}

	return true;
}


//======================================================================================================================


} // namespace own
