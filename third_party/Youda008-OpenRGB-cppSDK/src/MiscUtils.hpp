//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: miscellaneous utilities
//======================================================================================================================

#ifndef OPENRGB_MISC_UTILS_INCLUDED
#define OPENRGB_MISC_UTILS_INCLUDED


#include <CppUtils-Essential/Essential.hpp>

#include <iosfwd>


namespace orgb {


void indent( unsigned int indentLevel );

void indent( std::ostream & os, unsigned int indentLevel );


} // namespace orgb


#endif // OPENRGB_MISC_UTILS_INCLUDED
