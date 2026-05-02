//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: stream-oriented utils
//======================================================================================================================

#ifndef CPPUTILS_STREAM_INCLUDED
#define CPPUTILS_STREAM_INCLUDED


#include "Essential.hpp"

#include <string>
#include <istream>
#include <stdexcept>
#include <typeinfo>


namespace own {


//----------------------------------------------------------------------------------------------------------------------
//  input parsing

#ifndef NO_EXCEPTIONS
template< typename DestType >
DestType read( std::istream & is )
{
	DestType dest;
	is >> dest;
	if (!is.good())
	{
		throw std::invalid_argument( std::string("input does not contain valid ") + typeid( DestType ).name() );
	}
	return dest;
}
#endif

void read_until( std::istream & is, std::string & dest, char delim );
std::string read_until( std::istream & is, char delim );


//----------------------------------------------------------------------------------------------------------------------
//  output utils

/// Helper object that will output certain characted n times into a std::ostream.
/** Intended usage: std::cout << repeat_char('a', 5) << std::endl; */
class repeat_char
{
 public:
	repeat_char( char c, size_t count ) noexcept : c(c), count(count) {}
	friend std::ostream & operator<<( std::ostream & os, repeat_char repeat ) noexcept
	{
		while (repeat.count --> 0)
			os << repeat.c;
		return os;
	}
 private:
	char c;
	size_t count;
};


//----------------------------------------------------------------------------------------------------------------------


} // namespace own


#endif // CPPUTILS_STREAM_INCLUDED
