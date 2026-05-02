//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: classes for network addresses
//======================================================================================================================

#include "HostInfo.hpp"

#include <CppUtils-Essential/Essential.hpp>

#include <CppUtils-Essential/CriticalError.hpp>

#include <cstring>  // memset

#ifdef _WIN32
	#include <ws2tcpip.h>
#else
	#include <netdb.h>
#endif


namespace own {


//======================================================================================================================

AddrResult getAddrByHostname( const std::string & hostName )
{
	AddrResult result;

	struct addrinfo hint;
	memset( &hint, 0, sizeof(hint) );
	hint.ai_family = AF_UNSPEC;      // IPv4 or IPv6, it doesn't matter

	struct addrinfo * ainfo;
	if (::getaddrinfo( hostName.c_str(), nullptr, &hint, &ainfo ) != 0)
	{
		result.error = getLastError();
	}
	else
	{
		result.error = 0;
		Endpoint endpoint;
		if (!sockaddrToEndpoint( ainfo->ai_addr, endpoint ))
		{
			critical_error( "Socket operation returned unexpected address family." );
		}
		result.addr = endpoint.addr;
	}

	freeaddrinfo( ainfo );

	return result;
}


//======================================================================================================================


} // namespace own
