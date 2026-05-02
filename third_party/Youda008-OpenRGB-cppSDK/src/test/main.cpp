#include <CppUtils-Essential/Essential.hpp>

#include <OpenRGB/Client.hpp>
using namespace orgb;

#include <CppUtils-Essential/StringUtils.hpp>
#include <CppUtils-Essential/StreamUtils.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace std;


//======================================================================================================================

class ArgList
{
	std::vector< std::string > _args;
	mutable size_t _currentArgIdx = size_t(-1);  // first call to next() will make it 0

 public:

	ArgList() {}
	ArgList( const std::vector< std::string > & argVec ) : _args( argVec ) {}
	ArgList( std::vector< std::string > && argVec ) : _args( move(argVec) ) {}
	ArgList( std::initializer_list< std::string > initList ) : _args( initList ) {}

	void addArg( const std::string & arg )
	{
		_args.push_back( arg );
	}

	void addArg( std::string && arg )
	{
		_args.push_back( move(arg) );
	}

	const std::string & operator[]( size_t idx ) const { return _args.at( idx ); }

	const std::string & next() const { return _args.at( ++_currentArgIdx ); }

	/** Converts string argument at idx to DstType.
	  * Throws \c std::out_of_range when idx is out of range,
	  * or \c std::invalid_argument if the string cannot be converted to \c DstType. */
	template< typename DstType >
	DstType get( size_t idx ) const
	{
		DstType arg;
		const std::string & argStr = _args.at( idx );  // throws std::out_of_range
		arg = own::from_string< DstType >( argStr );  // throws std::invalid_argument
		return arg;
	}

	/** Converts the next string argument at idx to DstType.
	  * Throws \c std::out_of_range when idx is out of range,
	  * or \c std::domain_error if the string cannot be converted to \c DstType. */
	template< typename DstType >
	DstType getNext() const
	{
		return get< DstType >( ++_currentArgIdx );
	}

	void rewind() const { _currentArgIdx = 0; }

	size_t size() const { return _args.size(); }
};


//======================================================================================================================
//  compound arguments used by the commands

struct Endpoint
{
	std::string hostName;
	uint16_t port;
};
istream & operator>>( istream & is, Endpoint & endpoint )
{
	own::read_until( is, endpoint.hostName, ':' );  // TODO: until : or space
	if (!is.good())  // ':' not found
	{
		endpoint.port = 0;
		return is;
	}

	is >> endpoint.port;

	return is;
}

struct PartID
{
	// usecase for std::variant but that is C++17 and we are only C++11
	std::string str;
	uint32_t idx;
};
istream & operator>>( istream & is, PartID & partID )
{
	getline( is, partID.str );
	// int representation is optional, don't make the stream fail if it's not int
	if (sscanf( partID.str.c_str(), "%u", &partID.idx ) != 1)
		partID.idx = UINT32_MAX;
	return is;
}

struct PartSpec
{
	enum class Type { Zone, Led } type;
	PartID id;

	bool isEmpty() const { return id.str.empty(); }
};
istream & operator>>( istream & is, PartSpec & partSpec )
{
	std::string typeStr = own::read_until( is, ':' );  // TODO: until : or space
	if (!is.good())  // ':' was not found
	{
		is.setstate( std::ios::failbit );
		return is;
	}

	is >> partSpec.id;

	own::to_lower( typeStr );
	if (typeStr == "zone")
		partSpec.type = PartSpec::Type::Zone;
	else if (typeStr == "led")
		partSpec.type = PartSpec::Type::Led;
	else
		is.setstate( std::ios::failbit );

	return is;
}


//======================================================================================================================
//  helpers

const Device * findDevice( const DeviceList & devices, const PartID & deviceID )
{
	const Device * device = nullptr;
	if (deviceID.idx != UINT32_MAX)
	{
		if (deviceID.idx >= devices.size())
			cout << "Device with index " << deviceID.idx << " does not exist." << endl;
		else
			device = &devices[ deviceID.idx ];
	}
	else
	{
		device = devices.find( deviceID.str );
		if (!device)
			cout << "Device with name " << deviceID.str << " not found." << endl;
		// TODO: maybe according to vendor?
	}
	return device;
}

const Zone * findZone( const Device & device, const PartID & zoneID )
{
	const Zone * zone = nullptr;
	if (zoneID.idx != UINT32_MAX)
	{
		if (zoneID.idx >= device.zones.size())
			cout << "Zone with index " << zoneID.idx << " does not exist." << endl;
		else
			zone = &device.zones[ zoneID.idx ];
	}
	else
	{
		zone = device.findZone( zoneID.str );
		if (!zone)
			cout << "Zone with name " << zoneID.str << " not found." << endl;
		// TODO: maybe according to vendor?
	}
	return zone;
}

const LED * findLED( const Device & device, const PartID & ledID )
{
	const LED * led = nullptr;
	if (ledID.idx != UINT32_MAX)
	{
		if (ledID.idx >= device.leds.size())
			cout << "LED with index " << ledID.idx << " does not exist." << endl;
		else
			led = &device.leds[ ledID.idx ];
	}
	else
	{
		led = device.findLED( ledID.str );
		if (!led)
			cout << "LED with name " << ledID.str << " not found." << endl;
		// TODO: maybe according to vendor?
	}
	return led;
}

const Mode * findMode( const Device & device, const PartID & modeID )
{
	const Mode * mode = nullptr;
	if (modeID.idx != UINT32_MAX)
	{
		if (modeID.idx >= device.modes.size())
			cout << "Mode with index " << modeID.idx << " does not exist." << endl;
		else
			mode = &device.modes[ modeID.idx ];
	}
	else
	{
		mode = device.findMode( modeID.str );
		if (!mode)
			cout << "Mode with name " << modeID.str << " not found." << endl;
		// TODO: maybe according to vendor?
	}
	return mode;
}


//======================================================================================================================
//  commands

static bool help( const ArgList & )
{
	cout << '\n';
	cout << "Possible commands:\n";
	cout << "  help                                         # prints this list of commands\n";
	cout << "  exit                                         # quits this application\n";
	cout << "  connect <host_name>[:<port>]                 # orgb::Client::connect\n";
	cout << "  disconnect                                   # orgb::Client::disconnect\n";
	cout << "  listdevs                                     # orgb::Client::requestDeviceList\n";
	cout << "  getcount                                     # orgb::Client::requestDeviceCount\n";
	cout << "  getdev                                       # orgb::Client::requestDeviceInfo\n";
	cout << "  setdevcolor <device_id> <color>              # orgb::Client::setDeviceColor\n";
	cout << "  setzonecolor <device_id> <zone_id> <color>   # orgb::Client::setZoneColor\n";
	cout << "  setledcolor <device_id> <led_id> <color>     # orgb::Client::setLEDColor\n";
	cout << "  custommode <device_id>                       # orgb::Client::switchToCustomMode\n";
	cout << "  changemode <device_id> <mode>                # orgb::Client::changeMode\n";
	cout << "  savemode <device_id> <mode>                  # orgb::Client::saveMode\n";
	cout << "  setzonesize <device_id> <zone_id> <size>     # orgb::Client::setZoneSize\n";
	cout << "  listprofiles                                 # orgb::Client::requestProfileList\n";
	cout << "  saveprofile                                  # orgb::Client::saveProfile\n";
	cout << "  loadprofile                                  # orgb::Client::loadProfile\n";
	cout << "  delprofile                                   # orgb::Client::deleteProfile\n";
	cout << '\n';
	cout.flush();

	return true;
}

static orgb::Client client( "OpenRGB-cppSDK" );
static DeviceListResult listResult = { RequestStatus::NotConnected, {} };

static bool connect( const ArgList & args )
{
	Endpoint endpoint = { "127.0.0.1", orgb::defaultPort };
	if (args.size() > 0)
	{
		endpoint = args.getNext< Endpoint >();
		if (endpoint.port == 0)
			endpoint.port = orgb::defaultPort;
	}

	cout << "Connecting to " << endpoint.hostName << ":" << endpoint.port << endl;
	ConnectStatus status = client.connect( endpoint.hostName, endpoint.port );

	if (status == ConnectStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << " (error code: " << client.getLastSystemError() << ")" << endl;
		return false;
	}
}

static bool disconnect( const ArgList & )
{
	client.disconnect();
	listResult.status = RequestStatus::NotConnected;
	cout << "Disconnected." << endl;
	return true;
}

static bool listdevs( const ArgList & )
{
	cout << "Requesting the device list." << endl;
	listResult = client.requestDeviceList();

	if (listResult.status != RequestStatus::Success)
	{
		cout << " -> failed: " << enumString( listResult.status ) << " (error code: " << client.getLastSystemError() << ")" << endl;
		return false;
	}

	cout << '\n';
	cout << "devices = [\n";
	for (const orgb::Device & device : listResult.devices)
	{
		print( cout, device, 1 );
	}
	cout << "]\n";
	cout << '\n';
	cout.flush();

	return true;
}

static bool getcount( const ArgList & )
{
	cout << "Requesting the device count." << endl;
	DeviceCountResult countResult = client.requestDeviceCount();

	if (countResult.status != RequestStatus::Success)
	{
		cout << " -> failed: " << enumString( countResult.status ) << " (error code: " << client.getLastSystemError() << ")" << endl;
		return false;
	}

	cout << "device count: " << countResult.count << endl;

	return true;
}

static bool getdev( const ArgList & args )
{
	uint32_t deviceIdx = args.getNext< uint32_t >();

	cout << "Requesting info about device " << deviceIdx << endl;
	DeviceInfoResult deviceResult = client.requestDeviceInfo( deviceIdx );

	if (deviceResult.status != RequestStatus::Success)
	{
		cout << " -> failed: " << enumString( deviceResult.status ) << " (error code: " << client.getLastSystemError() << ")" << endl;
		return false;
	}

	cout << '\n';
	print( cout, *deviceResult.device, 1 );
	cout << '\n';
	cout.flush();

	listResult.devices.replace( deviceIdx, move( deviceResult.device ) );

	return true;
}

static bool setdevcolor( const ArgList & args )
{
	PartID deviceID = args.getNext< PartID >();
	Color color = args.getNext< Color >();

	if (listResult.status != RequestStatus::Success)
	{
		cout << "Device list not initialized, run 'list' first" << endl;
		return false;
	}

	const Device * device = findDevice( listResult.devices, deviceID );
	if (!device)
		return false;

	cout << "Changing color of device " << deviceID.str << " to " << color << endl;
	RequestStatus status = client.setDeviceColor( *device, color );
	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool setzonecolor( const ArgList & args )
{
	PartID deviceID = args.getNext< PartID >();
	PartID zoneID = args.getNext< PartID >();
	Color color = args.getNext< Color >();

	if (listResult.status != RequestStatus::Success)
	{
		cout << "Device list not initialized, run 'list' first" << endl;
		return false;
	}

	const Device * device = findDevice( listResult.devices, deviceID );
	if (!device)
		return false;

	const Zone * zone = findZone( *device, zoneID );
	if (!zone)
		return false;

	cout << "Changing color of zone " << zoneID.str << " to " << color << endl;
	RequestStatus status = client.setZoneColor( *zone, color );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool setledcolor( const ArgList & args )
{
	PartID deviceID = args.getNext< PartID >();
	PartID ledID = args.getNext< PartID >();
	Color color = args.getNext< Color >();

	if (listResult.status != RequestStatus::Success)
	{
		cout << "Device list not initialized, run 'list' first" << endl;
		return false;
	}

	const Device * device = findDevice( listResult.devices, deviceID );
	if (!device)
		return false;

	const LED * led = findLED( *device, ledID );
	if (!led)
		return false;

	cout << "Changing color of LED " << ledID.str << " to " << color << endl;
	RequestStatus status = client.setLEDColor( *led, color );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool custommode( const ArgList & args )
{
	PartID deviceID = args.getNext< PartID >();

	if (listResult.status != RequestStatus::Success)
	{
		cout << "Device list not initialized, run 'list' first" << endl;
		return false;
	}

	const Device * device = findDevice( listResult.devices, deviceID );
	if (!device)
		return false;

	cout << "Swithing device " << deviceID.str << " to custom mode" << endl;
	RequestStatus status = client.switchToCustomMode( *device );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool changemode( const ArgList & args )
{
	PartID deviceID = args.getNext< PartID >();
	PartID modeID = args.getNext< PartID >();

	if (listResult.status != RequestStatus::Success)
	{
		cout << "Device list not initialized, run 'list' first" << endl;
		return false;
	}

	const Device * device = findDevice( listResult.devices, deviceID );
	if (!device)
		return false;

	const Mode * mode = findMode( *device, modeID );
	if (!mode)
		return false;

	cout << "Changing mode of device " << deviceID.str << " to " << modeID.str << endl;
	RequestStatus status = client.changeMode( *device, *mode );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool savemode( const ArgList & args )
{
	PartID deviceID = args.getNext< PartID >();
	PartID modeID = args.getNext< PartID >();

	if (listResult.status != RequestStatus::Success)
	{
		cout << "Device list not initialized, run 'list' first" << endl;
		return false;
	}

	const Device * device = findDevice( listResult.devices, deviceID );
	if (!device)
		return false;

	const Mode * mode = findMode( *device, modeID );
	if (!mode)
		return false;

	cout << "Saving mode of device " << deviceID.str << " to " << modeID.str << endl;
	RequestStatus status = client.saveMode( *device, *mode );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool setzonesize( const ArgList & args )
{
	PartID deviceID = args.getNext< PartID >();
	PartID zoneID = args.getNext< PartID >();
	uint32_t zoneSize = args.getNext< uint32_t >();

	if (listResult.status != RequestStatus::Success)
	{
		cout << "Device list not initialized, run 'list' first" << endl;
		return false;
	}

	const Device * device = findDevice( listResult.devices, deviceID );
	if (!device)
		return false;

	const Zone * zone = findZone( *device, zoneID );
	if (!zone)
		return false;

	cout << "Changing size of zone " << zoneID.str << " to " << zoneSize << endl;
	RequestStatus status = client.setZoneSize( *zone, zoneSize );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool listprofiles( const ArgList & )
{
	cout << "Requesting the profile list." << endl;
	ProfileListResult listResult = client.requestProfileList();

	if (listResult.status != RequestStatus::Success)
	{
		cout << " -> failed: " << enumString( listResult.status ) << " (error code: " << client.getLastSystemError() << ")" << endl;
		return false;
	}

	cout << "profiles = [\n";
	for (const std::string & profile : listResult.profiles)
	{
		cout << "    \"" << profile << "\"\n";
	}
	cout << "]\n";
	cout.flush();

	return true;
}

static bool saveprofile( const ArgList & args )
{
	string profileName = args.getNext< string >();

	cout << "Saving the current configuration as \"" << profileName << "\"" << endl;
	RequestStatus status = client.saveProfile( profileName );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool loadprofile( const ArgList & args )
{
	string profileName = args.getNext< string >();

	cout << "Loading existing profile \"" << profileName << "\"" << endl;
	RequestStatus status = client.loadProfile( profileName );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}

static bool delprofile( const ArgList & args )
{
	string profileName = args.getNext< string >();

	cout << "Deleting existing profile \"" << profileName << "\"" << endl;
	RequestStatus status = client.deleteProfile( profileName );

	if (status == RequestStatus::Success)
	{
		cout << " -> success" << endl;
		return true;
	}
	else
	{
		cout << " -> failed: " << enumString( status ) << endl;
		return false;
	}
}


//======================================================================================================================

static void printBanner()
{
	cout <<
		"OpenRGB C++ SDK tester\n"
		"\n"
		"Type 'help' to see the list of all possible commands or 'exit' to quit the application.\n"
	<< endl;
}

// takes into account quotes
static bool readArg( istringstream & is, std::string & arg )
{
	bool singleQuotes = false;
	bool doubleQuotes = false;

	// first skip leading whitespaces
	while (true)
	{
		char c = char( is.get() );
		if (!is.good())
			return false;

		if (!isspace( c ))
		{
			if (c == '\'')
				singleQuotes = true;
			else if (c == '"')
				doubleQuotes = true;
			else
				arg += c;
			break;
		}
	}

	while (true)
	{
		char c = char( is.get() );
		if (!is.good())
			break;

		if (c == '\'' && !doubleQuotes)
		{
			if (singleQuotes)
				break;
			else
				singleQuotes = true;
			continue;
		}
		if (c == '"' && !singleQuotes)
		{
			if (doubleQuotes)
				break;
			else
				doubleQuotes = true;
			continue;
		}
		if (isspace( c ) && !singleQuotes && !doubleQuotes)
		{
			break;
		}
		arg += c;
	}
	return is.eof() || !is.fail();
}

struct Command
{
	string name;
	ArgList args;
};
static Command splitCommandLine( const string & line )
{
	Command command;

	istringstream stream( line );

	if (!readArg(stream, command.name))
	{
		return command;
	}
	own::to_lower_in_place( command.name );

	string arg;
	while (readArg(stream, arg))
	{
		command.args.addArg( move(arg) );
	}

	return command;
}

static void executeCommand( const Command & command, const ArgList & args )
{
	bool (* handler)( const ArgList & args );

	if (command.name == "help")
		handler = help;
	else if (command.name == "connect")
		handler = connect;
	else if (command.name == "disconnect")
		handler = disconnect;
	else if (command.name == "listdevs")
		handler = listdevs;
	else if (command.name == "getcount")
		handler = getcount;
	else if (command.name == "getdev")
		handler = getdev;
	else if (command.name == "setdevcolor")
		handler = setdevcolor;
	else if (command.name == "setzonecolor")
		handler = setzonecolor;
	else if (command.name == "setledcolor")
		handler = setledcolor;
	else if (command.name == "custommode")
		handler = custommode;
	else if (command.name == "changemode")
		handler = changemode;
	else if (command.name == "savemode")
		handler = savemode;
	else if (command.name == "setzonesize")
		handler = setzonesize;
	else if (command.name == "listprofiles")
		handler = listprofiles;
	else if (command.name == "saveprofile")
		handler = saveprofile;
	else if (command.name == "loadprofile")
		handler = loadprofile;
	else if (command.name == "delprofile")
		handler = delprofile;
	else
	{
		cout << "Unknown command. Use 'help' to see the list of all possible commands" << endl;
		return;
	}

	try
	{
		handler( args );
	}
	catch (const out_of_range &)
	{
		cout << "Not enough arguments for this command." << "'\n";
	}
	catch (const invalid_argument & ex)
	{
		cout << "Invalid arguments for this command: " << ex.what() << "'\n";
	}
}

static bool equalsToOneOf( const string & str, const initializer_list< const char * > & list )
{
	for (const char * listItem : list)
		if (str == listItem)
			return true;
	return false;
}

int main( int /*argc*/, char * /*argv*/ [] )
{
	printBanner();

	while (true)
	{
		cout << "> " << flush;  // prompt

		string line = own::read_until( cin, '\n' );
		if (cin.eof())
		{
			return 0;
		}
		else if (cin.fail())
		{
			cout << "Failed to read the input." << endl;
			return 255;
		}

		if (line.empty())
		{
			// enter has been hit without writing anything -> behave the same as a normal terminal
			continue;
		}

		Command command = splitCommandLine( line );

		if (equalsToOneOf( command.name, { "exit", "quit" } ))
		{
			return 0;
		}
		else
		{
			executeCommand( command, command.args );
		}
	}
}
