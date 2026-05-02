//======================================================================================================================
//  continuously keep updating colors of CPU cooler until an INTERRUPT signal
//    - variant handling errors by catching exceptions
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
using orgb::DeviceListResult;
using orgb::DeviceList;
using orgb::DeviceType;
using orgb::Device;
using orgb::Color;

#include "OpenRGB/Exceptions.hpp"


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

		try
		{
			// reconnect if the connection was lost
			if (!client.isConnected())
			{
				printf( "trying to connect to %s\n", hostName );
				client.connectX( hostName );
			}

			// update our local device list, if it has changed on the server
			if (client.isDeviceListOutdatedX())
			{
				printf( "updating device list\n" );
				devices = client.requestDeviceListX();
				cpuCooler = &devices.findX( DeviceType::Cooler );
				// let's wait for next iteration, OpenRGB doesn't like when you send multiple requests at once
				continue;
			}

			// some devices don't accept colors until you set them to custom mode
			if (!isInCustomMode)
			{
				printf( "setting CPU cooler to custom mode\n" );
				client.switchToCustomModeX( *cpuCooler );
				isInCustomMode = true;
				// let's wait for next iteration, OpenRGB doesn't like when you send multiple requests at once
				continue;
			}

			// connected and updated, let's change our colors
			currentColorIdx = (currentColorIdx + 1) % (sizeof(colors) / sizeof(Color));
			printf( "setting CPU cooler to " ); print( colors[ currentColorIdx ] ); putchar('\n');
			client.setDeviceColorX( *cpuCooler, colors[ currentColorIdx ] );
		}
		catch (const orgb::UserError & error)
		{
			printf( "Error: %s\n", error.errorMessage() );
			break;
		}
		catch (const orgb::ConnectionError & error)
		{
			printf( "Error: %s (system error code: %d)\n", error.errorMessage(), int( error.systemErrorCode() ) );
			// reset everything and try again
			client.disconnect();
			continue;
		}
		catch (const orgb::Exception & error)
		{
			printf( "Error: %s\n", error.errorMessage() );
			// reset everything and try again
			client.disconnect();
			continue;
		}
	}

	return 0;
}
