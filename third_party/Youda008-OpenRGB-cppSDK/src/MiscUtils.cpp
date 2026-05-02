//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: miscellaneous utilities
//======================================================================================================================

#include "MiscUtils.hpp"

#include <cstdio>
#include <iostream>


namespace orgb {


void indent( unsigned int indentLevel )
{
	for (uint i = 0; i < indentLevel; ++i)
		putchar('\t');
}

void indent( std::ostream & os, unsigned int indentLevel )
{
	for (uint i = 0; i < indentLevel; ++i)
		os << '\t';
}


} // namespace orgb
