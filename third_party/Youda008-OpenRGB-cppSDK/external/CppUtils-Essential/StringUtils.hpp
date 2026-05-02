//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: string-oriented utils
//======================================================================================================================

#ifndef CPPUTILS_STRING_INCLUDED
#define CPPUTILS_STRING_INCLUDED


#include "Essential.hpp"

#include "Span.hpp"

#include <cstring>
#include <string>
#include <sstream>
#include <stdexcept>
#include <typeinfo>


// suffix s is C++14
inline std::string operator "" _s( const char * str, size_t size )
{
    return std::string( str, size );
}


namespace own {


//----------------------------------------------------------------------------------------------------------------------
//  parsing

template< typename DestType >
std::string to_string( const DestType & dest )
{
	std::ostringstream os;
	os << dest;
	return os.str();
}

template< typename DestType >
bool from_string( const std::string & src, DestType & dest )
{
	std::istringstream is( src );
	is >> dest;
	return is.eof() || !is.fail();
}

#ifndef NO_EXCEPTIONS
template< typename DestType >
DestType from_string( const std::string & src )
{
	DestType dest;
	if (!from_string( src, dest ))
	{
		throw std::invalid_argument( "\""+src+"\" is not a valid "+typeid( DestType ).name() );
	}
	return dest;
}
#endif


//----------------------------------------------------------------------------------------------------------------------
//  other

bool is_printable( const_char_span str ) noexcept;  // TODO: this produces ambiguous error
inline bool is_printable( const_byte_span data ) noexcept { return is_printable( data.cast< const char >() ); }

std::string to_lower( const std::string & str ) noexcept;
void to_lower_in_place( std::string & str ) noexcept;

bool starts_with( const std::string & str, const std::string & prefix ) noexcept;

inline char_span span_from_c_string( char * str ) noexcept { return make_span( str, strlen(str) ); }
inline const_char_span span_from_c_string( const char * str ) noexcept { return make_span( str, strlen(str) ); }


//----------------------------------------------------------------------------------------------------------------------


} // namespace own


#endif // CPPUTILS_STRING_INCLUDED
