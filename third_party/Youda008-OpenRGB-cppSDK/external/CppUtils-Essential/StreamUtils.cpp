//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: stream-oriented utils
//======================================================================================================================

#include "StreamUtils.hpp"

#include <cctype>  // isspace
#include <sstream>


namespace own {


// TODO: more efficient version using stream buffers
// https://github.com/gcc-mirror/gcc/blob/16e2427f50c208dfe07d07f18009969502c25dc8/libstdc%2B%2B-v3/include/ext/vstring.tcc
void read_until( std::istream & is, std::string & dest, char delim )
{
	while (true)
	{
		char c = char( is.get() );
		if (!is.good() || c == delim)
			break;
		dest += c;
	}
}

std::string read_until( std::istream & is, char delim )
{
	std::ostringstream os;
	while (true)
	{
		char c = char( is.get() );
		if (!is.good() || c == delim)
			break;
		os << c;
	}
	return os.str();
}


} // namespace own
