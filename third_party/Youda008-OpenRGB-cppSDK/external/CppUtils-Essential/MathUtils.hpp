//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: additional math utils and helpers
//======================================================================================================================

#ifndef CPPUTILS_MATH_INCLUDED
#define CPPUTILS_MATH_INCLUDED


#include "Essential.hpp"

#include "TypeTraits.hpp"


namespace own {


/// Divides 2 ints and returns the ceil of the result without converting to floating points.
template< typename Int1, typename Int2, REQUIRES( std::is_integral<Int1>::value && std::is_integral<Int2>::value ) >
constexpr Int1 div_ceil( Int1 divident, Int2 divisor )
{
	// This will even be inlined and correctly optimized in case of divisor being power of 2 https://godbolt.org/z/1bhaYWvKK
	return ((divident - 1) / divisor) + 1;
}


} // namespace asw::hns


#endif // CPPUTILS_MATH_INCLUDED
