//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: helpers that require declarations of STL containers
//======================================================================================================================

#ifndef CPPUTILS_CONTAINERS_INCLUDED
#define CPPUTILS_CONTAINERS_INCLUDED


#include "Essential.hpp"

#include "Span.hpp"

#include <iterator>   // advance, begin, end
#include <algorithm>  // find_if
#include <array>
#include <vector>


namespace own {


//----------------------------------------------------------------------------------------------------------------------
//  range-like helpers

template< typename ContType, typename ValType >
typename ContType::iterator find( ContType & cont, const ValType & val )
{
	return std::find( std::begin(cont), std::end(cont), val );
}
template< typename ContType, typename ValType >
typename ContType::const_iterator find( const ContType & cont, const ValType & val )
{
	return std::find( std::begin(cont), std::end(cont), val );
}
template< typename ContType, typename Predicate >
typename ContType::iterator find_if( ContType & cont, Predicate pred )
{
	return std::find_if( std::begin(cont), std::end(cont), pred );
}
template< typename ContType, typename Predicate >
typename ContType::const_iterator find_if( const ContType & cont, Predicate pred )
{
	return std::find_if( std::begin(cont), std::end(cont), pred );
}
template< typename ContType, typename Predicate >
typename ContType::iterator find_if_not( ContType & cont, Predicate pred )
{
	return std::find_if_not( std::begin(cont), std::end(cont), pred );
}
template< typename ContType, typename Predicate >
typename ContType::const_iterator find_if_not( const ContType & cont, Predicate pred )
{
	return std::find_if_not( std::begin(cont), std::end(cont), pred );
}

// How is this still not in C++ even in 2020?
template< typename ContType, typename ValType >
bool contains( const ContType & cont, const ValType & val )
{
	return std::find( std::begin(cont), std::end(cont), val ) != std::end(cont);
}
template< typename ContType, typename Predicate >
bool contains_if( const ContType & cont, Predicate pred )
{
	return std::find_if( std::begin(cont), std::end(cont), pred ) != std::end(cont);
}
template< typename ContType, typename Predicate >
bool contains_if_not( const ContType & cont, Predicate pred )
{
	return std::find_if_not( std::begin(cont), std::end(cont), pred ) != std::end(cont);
}

template< typename ContType1, typename ContType2 >
bool equal( const ContType1 & cont1, const ContType2 & cont2 )
{
	return std::equal( std::begin(cont1), std::end(cont1), std::begin(cont2), std::end(cont2) );
}

template< typename DstContType, typename SrcContType >
void append( DstContType & dstCont, const SrcContType & srcCont )
{
	dstCont.insert( std::end(dstCont), std::begin(srcCont), std::end(srcCont) );
}


//----------------------------------------------------------------------------------------------------------------------
//  misc

template< typename Elem, size_t size >
std::array< Elem, size > to_std_array( const Elem (& cArray) [size] )
{
	std::array< Elem, size > cppArray;
	std::copy( std::begin(cArray), std::end(cArray), cppArray.begin() );
	return cppArray;
}

// this allows us to write  make_array< 16 >( u"Blabla" )
// and it returns an array of 16 elements that start with the given constant and whose type is automatically deduced
template< size_t dstSize, typename Elem, size_t srcSize >
std::array< Elem, dstSize > make_std_array( const Elem (& cArray) [srcSize] )
{
	static_assert( srcSize <= dstSize, "array is too small" );
	std::array< Elem, dstSize > cppArray{};
	std::copy( cArray, cArray + srcSize, cppArray.begin() );
	return cppArray;
}

template< typename Type, REQUIRES( std::is_trivial<Type>::value ) >
size_t sizeofVector( const std::vector< Type > & vec ) noexcept
{
	return vec.size() * sizeof( Type );
}

template< typename Type, size_t Size, REQUIRES( std::is_trivial<Type>::value ) >
size_t sizeofArray( const std::array< Type, Size > & arr ) noexcept
{
	return arr.size() * sizeof( Type );
}


//----------------------------------------------------------------------------------------------------------------------


} // namespace own


#endif // CPPUTILS_CONTAINER_INCLUDED
