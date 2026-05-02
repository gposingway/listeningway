//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: host info retrieval
//======================================================================================================================

#ifndef CPPUTILS_HOSTINFO_INCLUDED
#define CPPUTILS_HOSTINFO_INCLUDED


#include "NetAddress.hpp"
#include "SystemErrorInfo.hpp"

#include <string>


namespace own {


//======================================================================================================================

struct AddrResult
{
	IPAddr addr;
	system_error_t error;
};
AddrResult getAddrByHostname( const std::string & hostName );


//======================================================================================================================


} // namespace own


#endif // CPPUTILS_HOSTINFO_INCLUDED
