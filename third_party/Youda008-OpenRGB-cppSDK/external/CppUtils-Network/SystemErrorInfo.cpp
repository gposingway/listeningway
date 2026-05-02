//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: universal way of getting information about system errors
//======================================================================================================================

#include "SystemErrorInfo.hpp"

#ifdef _WIN32
	#include <windows.h>
#else
	#include <cerrno>
	#include <cstring>
#endif // _WIN32


namespace own {


//======================================================================================================================

system_error_t getLastError() noexcept
{
 #ifdef _WIN32
	return GetLastError();
 #else
	return errno;
 #endif // _WIN32
}

std::string getErrorString( system_error_t errorCode ) noexcept
{
	char outStr [256];
 #ifdef _WIN32
	DWORD length;
	length = FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM,                     // It´s a system error
	                         nullptr,                                        // No string to be formatted needed
	                         DWORD( errorCode ),                             // Hey Windows: Please explain this error!
	                         MAKELANGID( LANG_ENGLISH, SUBLANG_ENGLISH_US ), // Do it in the english language
	                         outStr,                                         // Put the message here
	                         sizeof(outStr) - 1,                             // Number of bytes to store the message
	                         nullptr
	                       );
	if (length >= 2)
		outStr[ length - 2 ] = '\0'; // cut CR LF
 #else
	strncpy( outStr, strerror(errorCode), sizeof(outStr) );
	outStr[ sizeof(outStr) - 1 ] = '\0';
 #endif // _WIN32
	return std::string( outStr );
}


//======================================================================================================================


} // namespace own
