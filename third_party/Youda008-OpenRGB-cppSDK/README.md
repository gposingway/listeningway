# OpenRGB-cppSDK

A C++ library allowing you to control OpenRGB (https://gitlab.com/CalcProgrammer1/OpenRGB) via network.
Designed for minimal CPU and memory overhead, does not create any threads in the background, gives you full control your application main loop.
It can be built without exception support and should work on any CPU architecture. Requires only C++11.


## Usage

Include the library declarations.
(optionally declare what elements you will be using, for shorter identifiers)
```cpp
#include "OpenRGB/Client.hpp"
using orgb::ConnectStatus;
using orgb::DeviceListResult;
using orgb::RequestStatus;
using orgb::DeviceType;
```

Create the client and connect to an OpenRGB server.
```cpp
orgb::Client client( "My OpenRGB Client" );

ConnectStatus status = client.connect( "127.0.0.1" );  // you can also use Windows computer name
if (status != ConnectStatus::Success)
{
    fprintf( stderr, "failed to connect: %s (error code: %d)\n",
        enumString( status ), int( client.getLastSystemError() ) );
    return;
}
```

Download a list of devices and their settings.
```cpp
DeviceListResult result = client.requestDeviceList();
if (result.status != RequestStatus::Success)
{
    fprintf( stderr, "failed to get device list: %s (error code: %d)\n",
        enumString( result.status ), int( client.getLastSystemError() ) );
    return;
}
```

Find the device you want to control. You can search by type or name.
```cpp
const Device * cpuCooler = result.devices.find( DeviceType::Cooler );
if (!cpuCooler)
{
    fprintf( stderr, "device CPU cooler not found.\n" );
    return;
}
```

Some devices don't accept manual color changes until you set them to "Direct" mode.
This is how you do it.
```cpp
const Mode * directMode = cpuCooler->findMode( "Direct" );
if (!directMode)
{
    fprintf( stderr, "\"Direct\" mode not found in CPU cooler.\n" );
    return;
}
client.changeMode( *cpuCooler, *directMode );
```

Finally, set any color you want.
```cpp
client.setDeviceColor( *cpuCooler, Color::Red );
```

You can create any color by using the `Color` constructor.
```cpp
Color customColor( 255, 128, 64 );
```
It is also possible to create color from strings like "black", "red", "cyan", ... and from hex notation in format "#1267AB" by using method `fromString`
```cpp
Color color;
if (!color.fromString( input ))
    fprintf( stderr, "Invalid input.\n" );
```

#### !!WARNING!!
Between any color or mode change requests there should be at least few millisecond delay. Current implementation of OpenRGB is unreliable and bugs itself when you send it multiple requests at once.

Add this between any two calls to client.
```cpp
std::this_thread::sleep_for( milliseconds( 50 ) );
```

#### Exceptions vs return values
If you don't like the old-school way of checking return values, there are exception-throwing variants for each method of the client (they are postfixed with `X`). The code can be then written in slightly simplier way.
```cpp
try
{
    orgb::Client client( "My OpenRGB Client" );

    client.connectX( "127.0.0.1" );

    DeviceList devices = client.requestDeviceListX();

    const Device & cpuCooler = devices.findX( DeviceType::Cooler );

    const Mode & directMode = cpuCooler.findModeX( "Direct" );

    client.changeModeX( cpuCooler, directMode );

    client.setDeviceColorX( cpuCooler, Color::Red );
}
catch (const orgb::Exception & ex)
{
    fprintf( stderr, "Error: %s\n", ex.errorMessage() );
}
```
If you are developing for a platform that does not support exceptions or you just generally don't want to use exceptions, execute the cmake command with additional parameter `-DNO_EXCEPTIONS` and all the code throwing exceptions will be left out of the library.

#### Building your application
Depending on your IDE or build system, you must add the directory `include` to your include directories and the directory where you built this library to your link library directories. Then you must link library `orgbsdk` to your app. The library is static, so you don't have to worry about moving any dynamic libraries around together with your app.

Simpliest example with GCC
```
g++ -I"<path to OpenRGB-cppSDK>/include" -L"<path to OpenRGB-cppSDK>/build" -o myapp.exe myapp.cpp -lorgbsdk
```

#### More examples
Complete examples of working apps using this library can be found in directory `examples`, feel free to copy&paste anything.

### API documentation
Detailed documentation of the API is generated using Doxygen, see [Doxygen documentation](#doxygen-documentation).


## How to build the library

First initialize all submodules, either with via any graphical tool or
```
git submodule update --init
```
Then proceed according to your OS and build chain.

### Windows - Msys2
```
mkdir build-release
cd build-release
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ../
mingw32-make
```

### Windows - Visual Studio
```
mkdir build-release
cd build-release
cmake -G "Visual Studio 16 2019" -DCMAKE_BUILD_TYPE=Release ../
```
Then open the resulting solution in Visual Studio and build it.

### Linux
```
mkdir build-release
cd build-release
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ../
make
```


## Optional build targets

### CLI demo utility
There is also a command line tool that demonstrates the library functionality and helps with debugging the client-server connectivity. You can build it by building a target `orgbcli` after you generate the build files with cmake, for example on Linux by writing
```
make orgbcli
```
The tool can either be controlled by command line arguments or interactively while running. Write `orgbcli --help` to learn more about the usage or start the tool without arguments and follow the instructions.

### Doxygen documentation
More detailed documentation can be generated by Doxygen. Install Doxygen, then build a target `doc` after generating the build files with cmake, and then open file `<build_dir>/doc/html/index.html` in your browser.
