#include "CommandRegistration.hpp"

#include <CppUtils-Essential/StreamUtils.hpp>  // repeat_char

#include <iostream>


std::ostream & operator<<( std::ostream & os, const RegisteredCommand & regCmd )
{
	os << regCmd.name << " " << regCmd.argDesc
	   << own::repeat_char(' ', 50 - regCmd.name.size() - regCmd.argDesc.size()) << "# " << regCmd.description;
	return os;
}

void RegisteredCommands::registerCommand( const RegisteredCommand & newCommand )
{
	auto inserted = cmdMap.insert({ newCommand.name, &newCommand });
	if (!inserted.second)
	{
		std::cerr << "command '" << newCommand.name << "' is already registered" << std::endl;
		std::terminate();
	}
	cmdList.push_back( &newCommand );
}

const RegisteredCommand * RegisteredCommands::findCommand( const std::string & name )
{
	const auto cmdIter = cmdMap.find( name );
	return (cmdIter != cmdMap.end()) ? cmdIter->second : nullptr;
}
