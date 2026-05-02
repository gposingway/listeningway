#include <CppUtils-Essential/Essential.hpp>

#include "Commands.hpp"

#include <OpenRGB/Client.hpp>
using namespace orgb;

#include <CppUtils-Essential/StreamUtils.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace std;


//----------------------------------------------------------------------------------------------------------------------

#define APP_FULL_NAME "OpenRGB C++ SDK command line tool"
#define CLIENT_NAME "OpenRGB-cppSDK CLI"

#define EXECUTABLE_NAME "orgbcli"
#define USAGE EXECUTABLE_NAME " <host_name>[:<port>] <command> [<arg>]..."
#define EXAMPLE EXECUTABLE_NAME " localhost:6743 setmode 2 Direct"


static orgb::Client client( CLIENT_NAME );


//----------------------------------------------------------------------------------------------------------------------

/// This will show when the user runs the app interactively (with no arguments)
static void printBanner()
{
	static const char banner [] =
		APP_FULL_NAME "\n"
		"\n"
		"Running in interactive mode.\n"
		"Type 'help' to see the list of all possible commands or 'exit' to quit the application.\n"
	;
	cout << banner << endl;
}

/// This will show when the user runs the app with --help argument
static void printHelp_nonInteractive()
{
	static const char help [] =
		APP_FULL_NAME "\n"
		"\n"
		"This program can run in iteractive or non-iteractive mode.\n"
		"\n"
		"The non-interactive mode let's you specify the target host and a command\n"
		"using command line arguments, it performs the command and quits.\n"
		"  Usage is as follows: " USAGE "\n"
		"          For example: " EXAMPLE "\n"
		"\n"
		"In interactive mode, you run the app without any arguments and it\n"
		"continuously reads and executes the commands entered into the terminal\n"
		"until command 'exit' or interrupt signal.\n"
	;
	cout << help;
	cout << "\n";
	cout << "Possible commands:\n";
	for (const RegisteredCommand * cmd : g_standardCommands)
	{
		cout << "  " << *cmd << '\n';
	}
	cout << flush;
}

static void printHelp_interactive()
{
	helpCmd.handler( client, {} );
}


//----------------------------------------------------------------------------------------------------------------------

struct Command
{
	string name;
	ArgList args;
};

static Command argvToCommandLine( char * argv [], int argc )
{
	Command command;

	// TODO: take into account quotes on Windows

	if (argc >= 1)
	{
		command.name = own::to_lower( argv[0] );
	}

	for (int i = 1; i < argc; ++i)
	{
		command.args.emplaceArg( argv[i] );
	}

	return command;
}
// takes into account quotes
bool readArg( istringstream & is, std::string & arg )
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

enum class CmdResult
{
	Success,
	InvalidArguments,
	Failed
};
static CmdResult executeCommand( const RegisteredCommand & regCommand, const ArgList & args )
{
	try
	{
		bool success = regCommand.handler( client, args );
		return success ? CmdResult::Success : CmdResult::Failed;
	}
	catch (const out_of_range &)
	{
		cout << "Not enough arguments for this command." << "'\n";
		cout << "  Usage: " << regCommand << endl;
		return CmdResult::InvalidArguments;
	}
	catch (const invalid_argument & ex)
	{
		cout << "Invalid arguments for this command: " << ex.what() << "'\n";
		cout << "  Usage: " << regCommand << endl;
		return CmdResult::InvalidArguments;
	}
}

static bool equalsToOneOf( const string & str, const initializer_list< const char * > & list )
{
	for (const char * listItem : list)
		if (str == listItem)
			return true;
	return false;
}

static int runNonInteractiveMode( int argc, char * argv [] )
{
	if (equalsToOneOf( argv[1], { "-h", "--help", "/?" } ))
	{
		printHelp_nonInteractive();
		return 0;
	}

	if (argc < 3)
	{
		cout << "Not enough arguments." << '\n';
		cout << "  Usage: " << USAGE << endl;
		return 1;
	}

	Command command = argvToCommandLine( argv + 2, argc - 2 );

	if (equalsToOneOf( command.name, { "help", "commands", "exit", "quit", "connect", "disconnect" } ))
	{
		cout << "This command is not available in the non-interactive mode" << endl;
		return 2;
	}

	const RegisteredCommand * regCommand = g_standardCommands.findCommand( command.name );
	if (!regCommand)
	{
		cout << "Unknown command. Use '--help' to see the list of all possible commands" << endl;
		return 2;
	}

	// call the connect command directly so we can print custom error message
	bool connected;
	try{ connected = connectCmd.handler( client, { argv[1] } ); }
	catch (const logic_error &)
	{
		cout << "Invalid arguments." << '\n';
		cout << "  Usage: " << USAGE << endl;
		return 1;
	}
	if (!connected)
	{
		return 3;
	}

	CmdResult cmdRes = executeCommand( *regCommand, command.args );
	if (cmdRes == CmdResult::Success)
	{
		return 0;
	}
	if (cmdRes == CmdResult::InvalidArguments)
	{
		return 1;
	}
	else
	{
		return 3;
	}
}

static int runInteractiveMode()
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
		else if (equalsToOneOf( command.name, { "help", "commands" } ))
		{
			printHelp_interactive();
		}
		else if (const RegisteredCommand * regCommand = g_standardCommands.findCommand( command.name ))
		{
			executeCommand( *regCommand, command.args );
		}
		else
		{
			cout << "Unknown command. Use 'help' to see the list of all possible commands" << endl;
		}
	}
}

int main( int argc, char * argv [] )
{
	if (argc > 1)
	{
		// non-interactive mode - try to execute the command specified with the command line arguments and quit
		return runNonInteractiveMode( argc, argv );
	}
	else
	{
		// interative mode - continuously execute commands entered to the terminal until 'exit' command or interrupt
		return runInteractiveMode();
	}
}
