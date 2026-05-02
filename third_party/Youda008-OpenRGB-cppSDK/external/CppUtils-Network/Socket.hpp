//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: abstraction over low-level system socket calls
//======================================================================================================================

#ifndef CPPUTILS_SOCKET_INCLUDED
#define CPPUTILS_SOCKET_INCLUDED


#include "SystemErrorInfo.hpp"
#include "NetAddress.hpp"

#include <CppUtils-Essential/Span.hpp>

#include <chrono>  // timeout
#include <vector>  // recv
#include <unordered_set>  // waitForAny

struct sockaddr;


namespace own {


//======================================================================================================================
//  types shared between multiple socket classes

/// our own unified error codes, because error codes from system calls vary from OS to OS
enum class SocketError
{
	Success = 0,                ///< The operation was successful.
	// our own error states that don't have anything to do with the system sockets
	AlreadyConnected = 1,       ///< Connect operation failed because the socket is already connected. Call disconnect() first.
	NotConnected = 2,           ///< Operation failed because the socket is not connected. Call connect() first.
	// errors related to connect attempt
	NetworkingInitFailed = 10,  ///< Operation failed because underlying networking system could not be initialized. Call getLastSystemError() for more info.
	HostNotResolved = 11,       ///< The hostname you entered could not be resolved to IP address. Call getLastSystemError() for more info.
	ConnectFailed = 12,         ///< Could not connect to the target server, either it's down or the port is closed. Call getLastSystemError() for more info.
	// errors related to send operation
	SendFailed = 20,            ///< Send operation failed. Call getLastSystemError() for more info.
	// errors related to receive operation
	ConnectionClosed = 30,      ///< Server has closed the connection.
	Timeout = 31,               ///< Operation timed-out.
	WouldBlock = 32,            ///< Socket is set to non-blocking mode and there is no data in the system input buffer.
	// errors related to opening a server
	AlreadyOpen = 40,           ///< Opening server failed because the socket is already listening. Call close() first.
	NotOpen = 41,               ///< Operation failed because the socket has not been opened. Call open() first.
	BindFailed = 42,            ///< Failed to bind the socket to a specified network address and port. Call getLastSystemError() for more info.
	ListenFailed = 43,          ///< Failed to switch the socket to a listening state. Call getLastSystemError() for more info.

	Other = 255                 ///< Other system error. Call getLastSystemError() for more info.
};
const char * enumString( SocketError error ) noexcept;

#ifdef _WIN32
	using socket_t = uintptr_t;  // should be SOCKET but let's not include the whole big winsock2.h just because of this
#else
	using socket_t = int;
#endif


//======================================================================================================================
/// Abstract base class for all sockets
/** Used for generic operations working on any sockets, like waitForAny(). */

class ASocket
{

 public:

	virtual ~ASocket() noexcept = 0;

	/// Returns the native system handle used for the low-level system APIs.
	socket_t getSystemHandle() const noexcept { return _socket; }

	bool setBlockingMode( bool enable ) noexcept;
	bool isBlocking() const noexcept  { return _isBlocking; }

	/// Returns the system error code that was recorded the last time an operation on this socket failed.
	system_error_t getLastSystemError() const noexcept  { return _lastSystemError; }

 protected: // functions

	ASocket( socket_t sock ) noexcept;

	ASocket() noexcept;
	ASocket( const ASocket & other ) = delete;
	ASocket( ASocket && other ) noexcept;
	ASocket & operator=( const ASocket & other ) = delete;
	ASocket & operator=( ASocket && other ) noexcept;

 protected: // members

	socket_t _socket;
	system_error_t _lastSystemError;
	bool _isBlocking;

};


//======================================================================================================================
/// Abstraction over low-level TCP socket system calls.
/** This class is used either by a client connecting to a server
  * or by a server after it accepts a connection from a client. */

class TcpSocket : public ASocket
{

 public:

	TcpSocket() noexcept;
	~TcpSocket() noexcept;

	TcpSocket( const TcpSocket & other ) = delete;
	TcpSocket( TcpSocket && other ) noexcept;
	TcpSocket & operator=( const TcpSocket & other ) = delete;
	TcpSocket & operator=( TcpSocket && other ) noexcept;

	/// Connects to a specified endpoint determined by host name and port.
	/** First the host name is resolved to an IP address and then a connection to that address is established. */
	SocketError connect( const std::string & host, uint16_t port ) noexcept;

	/// Connects to a specified endpoint determined by IP address and port.
	SocketError connect( const IPAddr & addr, uint16_t port );

	/// Disconnects from the currently connected server.
	SocketError disconnect() noexcept;

	bool isConnected() const noexcept;

	/// This needs to be checked after TcpServerSocket::accept().
	/** False means the server socket has been closed or there was an error accepting the client. */
	bool isAccepted() const noexcept;

	operator bool() const noexcept { return isAccepted(); }

	/// Sets the timeout for further receive operations.
	bool setTimeout( std::chrono::milliseconds timeout ) noexcept;

	/// Sends given number of bytes to the socket.
	/** If the system does not accept that amount of data all at once,
	  * it repeats the system calls until all requested data are sent. */
	SocketError send( const_byte_span buffer ) noexcept;

	/// Convenience wrapper of send( const_byte_span ) for sending textual data.
	/** \param[in] message null-terminated array of chars */
	SocketError send( const char * message ) noexcept;

	/// Receives the given number of bytes from the socket.
	/** If the requested amount of data don't arrive all at once,
	  * it repeats the system calls until all requested data are received.
	  * The number of bytes actually received is stored in an output parameter.
	  * \param[out] received how many bytes were really received */
	SocketError receive( byte_span buffer, size_t & received ) noexcept;

	/// Receives the given number of bytes from the socket.
	/** If the requested amount of data don't arrive all at once,
	  * it repeats the system calls until all requested data are received.
	  * After the call, the size of the vector will be equal to the number of bytes actually received.
	  * \param[in] size how many bytes to receive */
	SocketError receive( std::vector< uint8_t > & buffer, size_t size ) noexcept;

	/// Performs exactly one receive system call.
	/** If no data has arrived yet, waits until the first chunk arrives and returns it.
	  * If some data has already arrived prior to this call, it returns all we got so far. */
	SocketError receiveOnce( std::vector< uint8_t > & buffer ) noexcept;

 protected:

	 // allow creating socket object from already initialized socket handle, but only for TcpServerSocket
	 friend class TcpServerSocket;
	 TcpSocket( socket_t sock ) noexcept : ASocket( sock ) {}

	 SocketError _connect( int family, int addrlen, struct sockaddr * addr ) noexcept;

};


//======================================================================================================================
/// Abstraction over low-level TCP server socket system calls.
/** This class is used by a server to listen to incomming connections. */

class TcpServerSocket : public ASocket
{

 public:

	TcpServerSocket() noexcept;
	~TcpServerSocket() noexcept;

	TcpServerSocket( const TcpServerSocket & other ) = delete;
	TcpServerSocket( TcpServerSocket && other ) noexcept;
	TcpServerSocket & operator=( const TcpServerSocket & other ) = delete;
	TcpServerSocket & operator=( TcpServerSocket && other ) noexcept;

	/// Opens a TCP server on selected port.
	SocketError open( uint16_t port ) noexcept;

	SocketError close() noexcept;

	bool isOpen() const noexcept;

	/// Waits for an incomming connection request and then returns a socket representing the established connection.
	/** If the server is closed by another thread or an error occurs, the returned socket is invalid and isAccepted() returns false. */
	TcpSocket accept( Endpoint & endpoint );

};


//======================================================================================================================
/// Abstraction over low-level UDP socket system calls.

class UdpSocket : public ASocket
{

 public:

	UdpSocket() noexcept;
	~UdpSocket() noexcept;

	UdpSocket( const UdpSocket & other ) = delete;
	UdpSocket( UdpSocket && other ) noexcept;
	UdpSocket & operator=( const UdpSocket & other ) = delete;
	UdpSocket & operator=( UdpSocket && other ) noexcept;

	/// Opens an UDP socket on selected port.
	SocketError open( uint16_t port = 0 ) noexcept;

	SocketError close() noexcept;

	bool isOpen() const noexcept;

	/// Sends a datagram to a specified address and port.
	SocketError sendTo( const Endpoint & endpoint, const_byte_span buffer );

	/// Convenience wrapper of sendTo( const Endpoint &, const_byte_span ) for sending textual data.
	/** \param[in] message null-terminated array of chars */
	SocketError sendTo( const Endpoint & endpoint, const char * message );

	/// Waits for an incomming datagram and returns the packet data and the address and port it came from.
	SocketError recvFrom( Endpoint & endpoint, byte_span buffer, size_t & received );

};


//======================================================================================================================
//  multi-socket operations

bool waitForAny(
	const std::unordered_set< ASocket * > & activeSockets, std::vector< ASocket * > & readySockets,
	std::chrono::milliseconds timeout
);


//======================================================================================================================


} // namespace own


#endif // CPPUTILS_SOCKET_INCLUDED
