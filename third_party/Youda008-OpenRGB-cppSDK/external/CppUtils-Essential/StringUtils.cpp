//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: string-oriented utils
//======================================================================================================================

#include "StringUtils.hpp"

#include <cctype>  // isprint, isspace, tolower


namespace own {


bool is_printable( const_char_span str ) noexcept
{
	for (char c : str)
		if (!isprint(c))
			return false;
	return true;
}

void to_lower_in_place( std::string & str ) noexcept
{
	for (size_t i = 0; i < str.size(); ++i)
	{
		str[i] = char( tolower( str[i] ) );
	}
}

std::string to_lower( const std::string & str ) noexcept
{
	std::string copy( str );
	to_lower_in_place( copy );
	return copy;
}

bool starts_with( const std::string & str, const std::string & prefix ) noexcept
{
	return str.compare( 0, prefix.size(), prefix ) == 0;
}


} // namespace own
