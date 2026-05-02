//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: universal way of getting information about system errors
//======================================================================================================================

#ifndef CPPUTILS_SYSTEM_ERROR_INFO_INCLUDED
#define CPPUTILS_SYSTEM_ERROR_INFO_INCLUDED


#include <cstdint>
#include <string>


namespace own {


#ifdef _WIN32
	using system_error_t = uint32_t;  // should be DWORD but let's not include the whole windows.h just because of this
#else
	using system_error_t = int;
#endif

/// OS-independent way to retrieve the error code of the last system call
system_error_t getLastError() noexcept;

/// OS-independent way to convert an error code to a readable error message
std::string getErrorString( system_error_t errorCode ) noexcept;


} // namespace own


#endif // CPPUTILS_SYSTEM_ERROR_INFO_INCLUDED
