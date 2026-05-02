//======================================================================================================================
//  continuously keep updating colors of CPU cooler until an INTERRUPT signal
//    - variant handling errors by checking return values
//======================================================================================================================

/// \file

#include <cstdio>    // printf
#include <cstdlib>   // signal
#include <thread>    // sleep
using namespace std::chrono;

#include "OpenRGB/Client.hpp"
using orgb::ConnectStatus;
using orgb::RequestStatus;
using orgb::UpdateStatus;
using orgb::enumString;
using orgb::DeviceListResult;
using orgb::DeviceList;
using orgb::DeviceType;
using orgb::Device;
using orgb::Color;


static bool keepRunning = false;

void signalFunc( int )
{
	keepRunning = false;
	printf( "INTERRUPT signal received, quitting...\n" );
}

int main( int /*argc*/, char * /*argv*/ [] )
{
	static const char * hostName = "127.0.0.1";  // you can also use the NetBIOS computer name

	orgb::Client client( "My OpenRGB Client" );

	// a clean way to quit the application without killing it by force
	keepRunning = true;
	signal( SIGINT, signalFunc );

	DeviceList devices;
	const Device * cpuCooler = nullptr;

	bool isInCustomMode = false;

	const Color colors [] = {
		{ 255,   0,   0 },
		{ 255, 255,   0 },
		{   0, 255,   0 },
		{   0, 255, 255 },
		{   0,   0, 255 },
		{ 255,   0, 255 },
	};
	unsigned int currentColorIdx = 0;

	while (keepRunning)
	{
		// this needs to be in the beginning, so that all 'continue' statements cause a pause
		std::this_thread::sleep_for( milliseconds( 1000 ) );

		// reconnect if the connection was lost
		if (!client.isConnected())
		{
			printf( "trying to connect to %s\n", hostName );
			ConnectStatus status = client.connect( hostName );
			if (status == ConnectStatus::HostNotResolved)
			{
				printf( "host %s not found\n", hostName );
				break;
			}
			else if (status != ConnectStatus::Success)
			{
				printf( "connection failed: %s (error code: %d)\n", enumString( status ), int( client.getLastSystemError() ) );
				continue;
			}
		}

		// update our local device list, if it has changed on the server
		UpdateStatus updateStatus = client.checkForDeviceUpdates();
		if (updateStatus == UpdateStatus::OutOfDate)
		{
			printf( "updating device list\n" );
			DeviceListResult result = client.requestDeviceList();
			if (result.status != RequestStatus::Success)
			{
				printf( "failed to get device list: %s (error code: %d)\n", enumString( result.status ), int( client.getLastSystemError() ) );
				// reset everything and try again
				client.disconnect();
				continue;
			}
			devices = std::move( result.devices );

			cpuCooler = devices.find( DeviceType::Cooler );
			if (!cpuCooler)
			{
				printf( "device CPU cooler not found.\n" );
				// reset everything and try again
				client.disconnect();
				continue;
			}

			// let's wait for next iteration, OpenRGB doesn't like when you send multiple requests at once
			continue;
		}
		else if (updateStatus != UpdateStatus::UpToDate)
		{
			// some error occured while trying to find if the device list is up to date
			// reset everything and try again
			printf( "socket is broken, reseting connection\n" );
			client.disconnect();
			continue;
		}

		// some devices don't accept colors until you set them to custom mode
		if (!isInCustomMode)
		{
			printf( "setting CPU cooler to custom mode\n" );
			client.switchToCustomMode( *cpuCooler );
			isInCustomMode = true;
			// let's wait for next iteration, OpenRGB doesn't like when you send multiple requests at once
			continue;
		}

		// connected and updated, let's change our colors
		currentColorIdx = (currentColorIdx + 1) % (sizeof(colors) / sizeof(Color));
		printf( "setting CPU cooler to " ); print( colors[ currentColorIdx ] ); putchar('\n');
		client.setDeviceColor( *cpuCooler, colors[ currentColorIdx ] );
	}

	return 0;
}
