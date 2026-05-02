//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: declarations that should be built-in features of the languge, but aren't
//======================================================================================================================

#ifndef CPPUTILS_ESSENTIAL_INCLUDED
#define CPPUTILS_ESSENTIAL_INCLUDED


#include <cstddef>
#include <cstdint>
#include <climits>
#include <utility>      // move, forward, swap

using uint = unsigned int;
using ushort = unsigned short;
using ulong = unsigned long;
using ullong = unsigned long long;
//using byte = uint8_t;

using std::move;
using std::forward;


// These things are so commonly used that they are moved from LangUtils here.
namespace fut {


// C++17
template< typename ContType >
constexpr size_t size( const ContType & cont ) noexcept
{
	return cont.end() - cont.begin();
}
// In C++11 std::begin and std::end is not constexpr so we have to make a specialization
template< typename ElemType, size_t size_ >
constexpr size_t size( const ElemType (&) [size_] ) noexcept
{
	return size_;
}

// C++17
template< typename ContType >
constexpr auto data( ContType & cont ) noexcept -> decltype( cont.data() )
{
    return cont.data();
}
template< typename ContType >
constexpr auto data( const ContType & cont ) noexcept -> decltype( cont.data() )
{
    return cont.data();
}
template< typename ElemType, size_t size_ >
constexpr ElemType * data( ElemType (& array) [size_] ) noexcept
{
    return array;
}


} // namespace fut


namespace own {


template< typename Type >
Type & unconst( const Type & x ) noexcept
{
	return const_cast< Type & >( x );
}


} // namespace own


// Compiler dependent way to silence unused variable warnings in C++11.
/* If the project is limited to C++11 this is to be used instead of [[maybe_unused]]. This works in gcc and clang.
 * Users of other compilers will have to deal with occasional warnings or disable them with command line argument. */
#ifdef __GNUC__
	#define MAYBE_UNUSED __attribute__((unused))
#else
	#define MAYBE_UNUSED
#endif


#endif // CPPUTILS_ESSENTIAL_INCLUDED
