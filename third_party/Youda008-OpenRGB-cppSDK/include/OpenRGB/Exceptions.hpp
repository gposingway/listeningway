//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: exceptions thrown by OpenRGB network client
//======================================================================================================================

#ifndef OPENRGB_EXCEPTIONS_INCLUDED
#define OPENRGB_EXCEPTIONS_INCLUDED

#ifndef NO_EXCEPTIONS


#include "SystemErrorType.hpp"


namespace orgb {


//======================================================================================================================

/// Base class for all orgb::Client exceptions.
/** This library should not throw anything else than this.
  * Additionally if you want to completely elimite all exceptions, add preprocessor definition NO_EXCEPTIONS. */
class Exception
{
 public:
	Exception( const char * message ) noexcept : _message( message ) {}
	virtual ~Exception() noexcept = default;
	virtual const char * errorMessage() const noexcept;
 protected:
	const char * _message;
};


/// The client was used in an invalid way. Call errorMessage() for more info.
/** This is thrown for example when you request a color change when your client is not connected to a server. */
class UserError : public Exception
{
 public:
	UserError( const char * message ) noexcept : Exception( message ) {}
};


/// Network error that prevented the client to connect or perform a request. Check the systemErrorCode() for more info.
class ConnectionError : public Exception
{
 public:
	ConnectionError( const char * message, system_error_t systemErrorCode ) noexcept
		: Exception( message ), _errorCode( systemErrorCode ) {}
	system_error_t systemErrorCode() const noexcept { return _errorCode; }
 protected:
	system_error_t _errorCode;
};


/// Error that occured inside the operating system. Check the systemErrorCode() for more info.
class SystemError : public Exception
{
 public:
	SystemError( const char * message, system_error_t systemErrorCode ) noexcept
		: Exception( message ), _errorCode( systemErrorCode ) {}
	system_error_t systemErrorCode() const noexcept { return _errorCode; }
 protected:
	system_error_t _errorCode;
};


/// The device, mode, zone or LED you searched for was not found.
/** This is thrown by the DeviceList returned from Client::requestDeviceList() */
class NotFound : public Exception
{
 public:
	NotFound( const char * message ) noexcept : Exception( message ) {}
};


//======================================================================================================================


} // namespace orgb


#endif // NO_EXCEPTIONS

#endif // OPENRGB_EXCEPTIONS_INCLUDED
