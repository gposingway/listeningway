#ifndef CLI_CMDREG_INCLUDED
#define CLI_CMDREG_INCLUDED

#include <CppUtils-Essential/Essential.hpp>

#include <CppUtils-Essential/StringUtils.hpp>  // from_string

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

namespace orgb {
	class Client;
}


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

	template< typename ... Args >
	void emplaceArg( Args && ... args )
	{
		_args.emplace_back( std::forward< Args >( args ) ... );
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

struct RegisteredCommand
{
	using HandlerFunc = bool (*)( orgb::Client & client, const ArgList & args );

	std::string name;  ///< name of the command, for example "connect"
	std::string argDesc;  ///< description of arguments in the Unix man page format
	std::string description;  ///< brief single-line description of the command
	HandlerFunc handler;  ///< function that tried to perform the command

	RegisteredCommand( const char * name, const char * argDesc, const char * description, HandlerFunc handler )
		: name( name ), argDesc( argDesc ), description( description ), handler( handler ) {}

	friend std::ostream & operator<<( std::ostream & os, const RegisteredCommand & regCmd );
};

/// Database of the registered commands.
/** It doesn't store the commands, it saves only a non-owning reference, so the command objects must live elsewhere. */
class RegisteredCommands
{
	std::unordered_map< std::string, const RegisteredCommand * > cmdMap;
	std::vector< const RegisteredCommand * > cmdList;  // we need to keep the original order

 public:

	/// Registers a new command.
	/** It doesn't store the command, it saves only a non-owning reference,
	  * so the command objects must live elsewhere. */
	void registerCommand( const RegisteredCommand & newCommand );

	const RegisteredCommand * findCommand( const std::string & name );

	// allow range-for iteration
	decltype(cmdList)::const_iterator begin() const  { return cmdList.begin(); }
	decltype(cmdList)::const_iterator end() const    { return cmdList.end(); }
};


#endif // CLI_CMDREG_INCLUDED
