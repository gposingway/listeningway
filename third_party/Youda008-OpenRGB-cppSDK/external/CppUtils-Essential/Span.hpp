//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: own span substituting span from C++20
//======================================================================================================================

#ifndef CPPUTILS_SPAN_INCLUDED
#define CPPUTILS_SPAN_INCLUDED


#include "Essential.hpp"

#include "TypeTraits.hpp"
#include "CriticalError.hpp"

#include <array>    // fixed_span construction, size_t, begin, end, ...


namespace own {


//======================================================================================================================
/// Generalization over continuous-memory containers.
/** To be used instead of pair of buffer pointer and size. */

template< typename ElemType >
class span
{
	ElemType * _begin;
	ElemType * _end;

 public:

	span() noexcept : _begin( nullptr ), _end( nullptr ) {}

	// construct manually from pair of pointers
	span( ElemType * begin, ElemType * end ) noexcept : _begin( begin ), _end( end ) {}

	// construct manually from a data pointer and size
	span( ElemType * data, size_t size ) noexcept : _begin( data ), _end( data + size ) {}

	// deduce from generic container
	template< typename ContType, REQUIRES( is_range_of< ContType, typename std::remove_const<ElemType>::type >::value ) >
	span( ContType & cont ) noexcept : span( fut::data(cont), fut::size(cont) ) {}

	template< typename ContType, REQUIRES( is_range_of< const ContType, ElemType >::value ) >
	span( const ContType & cont ) noexcept : span( fut::data(cont), fut::size(cont) ) {}

	// construct from compatible span
	// The OtherType is required to allow constructing  span< const char > from span< char >
	// or  span< BaseClass > from span< SubClass >.
	template< typename OtherType, REQUIRES( std::is_convertible< OtherType *, ElemType * >::value ) >
	span( span< OtherType > other ) noexcept : span( other.data(), other.size() ) {}

	// assign
	span & operator=( const span & other ) noexcept = default;

	ElemType * begin() const noexcept { return _begin; }
	ElemType * end() const noexcept { return _end; }
	ElemType * data() const noexcept { return _begin; }
	size_t size() const noexcept { return size_t( _end - _begin ); }
	bool empty() const noexcept { return _begin == _end; }

	span shorter( size_t newSize ) const noexcept
	{
		if (newSize > size())
			critical_error( "attempted to increase span size from %zu to %zu", size(), newSize );
		return span( _begin, newSize );
	}

	template< typename OtherType >
	span< OtherType > cast() const noexcept
	{
		return { reinterpret_cast< OtherType * >( _begin ), reinterpret_cast< OtherType * >( _end ) };
	}

	// specialized functions only available for some ElemTypes
	template< typename T = ElemType,
		REQUIRES( std::is_same< T, ElemType >::value && is_same_except_cv< T, char >::value ) >
	span< typename corresponding_constness< T, uint8_t >::type > as_bytes() const noexcept
	{
		return cast< typename corresponding_constness< T, uint8_t >::type >();
	}
	template< typename T = ElemType,
		REQUIRES( std::is_same< T, ElemType >::value && is_same_except_cv< T, uint8_t >::value ) >
	span< typename corresponding_constness< T, char >::type > as_chars() const noexcept
	{
		return cast< typename corresponding_constness< T, char >::type >();
	}
};


//======================================================================================================================
/// Variant of own::span with compile-time length.
/** Use this when you know how long the span is at compile-time. */

template< typename ElemType, size_t size_ >
class fixed_span
{
	ElemType * _begin;

 public:

	fixed_span() noexcept : _begin( nullptr ) {}

	// construct from static containers
	fixed_span( ElemType (& arr) [size_] ) noexcept : _begin( arr ) {}
	fixed_span( std::array< ElemType, size_ > & arr ) noexcept : _begin( arr.data() ) {}
	fixed_span( const std::array< ElemType, size_ > & arr ) noexcept : _begin( arr.data() ) {}

	// copy from compatible spans
	// The OtherType is required to allow constructing  span< const char > from span< char >
	// or  span< BaseClass > from span< SubClass >.
	template< typename OtherType, REQUIRES( std::is_convertible< OtherType *, ElemType * >::value ) >
	fixed_span( fixed_span< OtherType, size_ > other ) noexcept
		: _begin( other.begin() ) {}

	// assign
	fixed_span & operator=( const fixed_span & other ) noexcept = default;

	ElemType * begin() const noexcept { return _begin; }
	ElemType * end() const noexcept { return _begin + size_; }
	ElemType * data() const noexcept { return _begin; }
	size_t size() const noexcept { return size_; }
	bool empty() const noexcept { return size_ == 0; }

	// convert to dynamic span
	span< ElemType > dynamic() const noexcept { return span< ElemType >( _begin, size_ ); }
	operator span< ElemType >() const noexcept { return dynamic(); }

	template< size_t newSize >
	fixed_span< ElemType, newSize > shorter() const noexcept
	{
		static_assert( newSize <= size_, "newSize must be smaller than current size" );
		return from_ptr< ElemType, newSize >( _begin );
	}

	template< typename OtherType >
	fixed_span< OtherType, size_ > cast() const noexcept
	{
		return from_ptr< OtherType, size_ >( reinterpret_cast< OtherType * >( _begin ) );
	}

	template< typename T = ElemType,
		REQUIRES( std::is_same< T, ElemType >::value && is_same_except_cv< T, char >::value ) >
	fixed_span< typename corresponding_constness< T, uint8_t >::type, size_ > as_bytes() const noexcept
	{
		return cast< typename corresponding_constness< T, uint8_t >::type >();
	}
	template< typename T = ElemType,
		REQUIRES( std::is_same< T, ElemType >::value && is_same_except_cv< T, uint8_t >::value ) >
	fixed_span< typename corresponding_constness< T, char >::type, size_ > as_chars() const noexcept
	{
		return cast< typename corresponding_constness< T, char >::type >();
	}

 private:

	// Members of this class with different template arguments are not normally accessible.
	template< typename OtherType, size_t otherSize >
	friend class fixed_span;

	template< typename OtherType, size_t otherSize >
	static fixed_span< OtherType, otherSize > from_ptr( OtherType * begin ) noexcept
	{
		fixed_span< OtherType, otherSize > s;
		s._begin = begin;
		return s;
	}

};


//======================================================================================================================
//  aliases

using byte_span = span< uint8_t >;
using const_byte_span = span< const uint8_t >;
using char_span = span< char >;
using const_char_span = span< const char >;

template< size_t size_ > using fixed_byte_span = fixed_span< uint8_t, size_ >;
template< size_t size_ > using fixed_const_byte_span = fixed_span< const uint8_t, size_ >;
template< size_t size_ > using fixed_char_span = fixed_span< char, size_ >;
template< size_t size_ > using fixed_const_char_span = fixed_span< const char, size_ >;


//======================================================================================================================
//  automatic template argument deduction

// from pointers
template< typename ElemType >
auto make_span( ElemType * begin, ElemType * end ) noexcept
 -> span< ElemType >
{
	return { begin, end };
}
template< typename ElemType >
auto make_span( ElemType * data, size_t size ) noexcept
 -> span< ElemType >
{
	return { data, size };
}

// from generic containers
template< typename ContType >
auto make_span( ContType & cont ) noexcept
 -> span< typename range_value<ContType>::type >
{
	return { fut::data(cont), fut::size(cont) };
}
template< typename ContType >
auto make_span( const ContType & cont ) noexcept
 -> span< const typename range_value<ContType>::type >
{
	return { fut::data(cont), fut::size(cont) };
}

// from static containers
template< typename ElemType, size_t size_ >
auto make_fixed_span( ElemType (& arr) [size_] ) noexcept
 -> fixed_span< ElemType, size_ >
{
	return { arr };
}
template< typename ElemType, size_t size_ >
auto make_fixed_span( std::array< ElemType, size_ > & arr ) noexcept
 -> fixed_span< ElemType, size_ >
{
	return { arr };
}
template< typename ElemType, size_t size_ >
auto make_fixed_span( const std::array< ElemType, size_ > & arr ) noexcept
 -> fixed_span< const ElemType, size_ >
{
	return { arr };
}


//======================================================================================================================
//  automatic template argument deduction with forced const

// from pointers
template< typename ElemType >
auto make_const_span( ElemType * begin, ElemType * end ) noexcept
 -> span< const ElemType >
{
	return { begin, end };
}
template< typename ElemType >
auto make_const_span( ElemType * data, size_t size ) noexcept
 -> span< const ElemType >
{
	return { data, size };
}

// from generic containers
template< typename ContType >
auto make_const_span( ContType & cont ) noexcept
 -> span< const typename range_value<ContType>::type >
{
	return { fut::data(cont), fut::size(cont) };
}

// from static containers
template< typename ElemType, size_t size_ >
auto make_fixed_const_span( ElemType (& arr) [size_] ) noexcept
 -> fixed_span< const ElemType, size_ >
{
	return { arr };
}
template< typename ElemType, size_t size_ >
auto make_fixed_const_span( std::array< ElemType, size_ > & arr ) noexcept
 -> fixed_span< const ElemType, size_ >
{
	return { arr };
}


//======================================================================================================================


} // namespace own


#endif // CPPUTILS_SPAN_INCLUDED
