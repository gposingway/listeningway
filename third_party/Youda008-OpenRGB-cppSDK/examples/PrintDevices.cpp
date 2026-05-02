//======================================================================================================================
//  lists all devices and their properties in console
//======================================================================================================================

/// \file

#include <cstdio>

#include "OpenRGB/Client.hpp"
using orgb::ConnectStatus;
using orgb::DeviceListResult;
using orgb::RequestStatus;
using orgb::enumString;


int main( int /*argc*/, char * /*argv*/ [] )
{
	orgb::Client client( "My OpenRGB Client" );

	ConnectStatus status = client.connect( "127.0.0.1" );
	if (status != ConnectStatus::Success)
	{
		fprintf( stderr, "failed to connect: %s (error code: %d)\n", enumString( status ), int( client.getLastSystemError() ) );
		return 1;
	}

	DeviceListResult result = client.requestDeviceList();
	if (result.status != RequestStatus::Success)
	{
		fprintf( stderr, "failed to get device list: %s (error code: %d)\n", enumString( result.status ), int( client.getLastSystemError() ) );
		return 2;
	}

	printf( "devices = [\n" );
	for (const orgb::Device & device : result.devices)
	{
		print( device, 1 );
	}
	printf( "]\n" );

	printf( "press enter to exit\n" );
	while (getchar() != '\n') {}

	return 0;
}
