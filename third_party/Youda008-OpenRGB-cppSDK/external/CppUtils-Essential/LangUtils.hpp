//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: template helpers, generic container helpers and backports from newer C++ standards
//======================================================================================================================

#ifndef CPPUTILS_LANGUAGE_INCLUDED
#define CPPUTILS_LANGUAGE_INCLUDED


#include "Essential.hpp"

#include "TypeTraits.hpp"

#include <iterator>  // advance, begin, end
#include <memory>


//----------------------------------------------------------------------------------------------------------------------
//  utils from standard library of a newer C++ standard

namespace fut {


// C++14
template< typename Type, typename ... Args >
std::unique_ptr< Type > make_unique( Args && ... args )
{
	return std::unique_ptr< Type >( new Type( std::forward< Args >( args ) ... ) );
}

// C++14
template< typename Iter >
constexpr std::reverse_iterator< Iter > make_reverse_iterator( Iter it ) noexcept
{
    return std::reverse_iterator< Iter >( it );
}

// C++23
template< typename EnumType, REQUIRES( std::is_enum<EnumType>::value ) >
constexpr typename std::underlying_type< EnumType >::type to_underlying( EnumType num ) noexcept
{
	return static_cast< typename std::underlying_type< EnumType >::type >( num );
}


} // namespace fut


//----------------------------------------------------------------------------------------------------------------------


namespace own {


/// return value variant of std::advance
template< typename Iterator, typename Distance >
Iterator advance( Iterator it, Distance n )
{
	std::advance( it, n );
	return it;
}

template< typename EndFunc >
class scope_guard
{
	EndFunc _atEnd;
 public:
	scope_guard( const EndFunc & endFunc ) : _atEnd( endFunc ) {}
	scope_guard( EndFunc && endFunc ) : _atEnd( move(endFunc) ) {}
	~scope_guard() noexcept { _atEnd(); }
};

template< typename EndFunc >
scope_guard< EndFunc > at_scope_end_do( const EndFunc & endFunc )
{
	return scope_guard< EndFunc >( endFunc );
}

template< typename EndFunc >
scope_guard< EndFunc > at_scope_end_do( EndFunc && endFunc )
{
	return scope_guard< EndFunc >( move(endFunc) );
}


} // namespace own


//----------------------------------------------------------------------------------------------------------------------


#endif // CPPUTILS_LANGUAGE_INCLUDED
