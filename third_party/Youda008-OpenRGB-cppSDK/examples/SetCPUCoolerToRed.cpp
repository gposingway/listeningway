//======================================================================================================================
//  sets all leds of a CPU cooler to red color
//======================================================================================================================

/// \file

#include <cstdio>
#include <thread>  // sleep
using std::chrono::milliseconds;

#include "OpenRGB/Client.hpp"
using orgb::ConnectStatus;
using orgb::DeviceListResult;
using orgb::RequestStatus;
using orgb::enumString;
using orgb::DeviceType;
using orgb::Device;
using orgb::Mode;
using orgb::Color;


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
		fprintf( stderr, "failed to get device list: %s (error code: %d)\n", enumString( status ), int( client.getLastSystemError() ) );
		return 2;
	}

	const Device * cpuCooler = result.devices.find( DeviceType::Cooler );
	if (!cpuCooler)
	{
		fprintf( stderr, "device CPU cooler not found.\n" );
		return 3;
	}

	// let's wait a little, OpenRGB doesn't like when you send multiple requests at once
	std::this_thread::sleep_for( milliseconds( 50 ) );

	// some devices don't accept colors until you set them to "Direct" mode
	const Mode * directMode = cpuCooler->findMode( "Direct" );
	if (!directMode)
	{
		fprintf( stderr, "\"Direct\" mode not found in CPU cooler.\n" );
		return 4;
	}
	client.changeMode( *cpuCooler, *directMode );

	// let's wait a little, OpenRGB doesn't like when you send multiple requests at once
	std::this_thread::sleep_for( milliseconds( 50 ) );

	client.setDeviceColor( *cpuCooler, Color::Red );

	return 0;
}
