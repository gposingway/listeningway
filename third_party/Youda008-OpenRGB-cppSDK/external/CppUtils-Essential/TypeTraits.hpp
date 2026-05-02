//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: custom type traits helpers
//======================================================================================================================

#ifndef CPPUTILS_TYPETRAITS_INCLUDED
#define CPPUTILS_TYPETRAITS_INCLUDED


#include "Essential.hpp"

#include <type_traits>
#include <array>    // data, size, begin, end


/// a syntax simplification for constraining template types
#define REQUIRES( ... ) typename std::enable_if< __VA_ARGS__, int >::type = 0


namespace fut {


template< typename T >
struct remove_cvref
{
    using type = typename std::remove_cv< typename std::remove_reference<T>::type >::type;
};


} // namespace fut


namespace own {


/// Compares bare value types without any const, volatile modifiers.
template< typename T1, typename T2 >
struct is_same_except_cv : std::is_same< typename std::remove_cv<T1>::type, typename std::remove_cv<T2>::type > {};

/// Compares value types without added reference.
template< typename T1, typename T2 >
struct is_same_except_ref : std::is_same< typename std::remove_reference<T1>::type, typename std::remove_reference<T2>::type > {};

/// Compares bare value types without any const, volatile modifiers and without reference.
template< typename T1, typename T2 >
struct is_same_except_cvref : std::is_same< typename fut::remove_cvref<T1>::type, typename fut::remove_cvref<T2>::type > {};

/// This determines types that are convertible to integer and serializable to big endian.
template< typename T >
struct is_int_or_enum
{
	static constexpr bool value = std::is_integral<T>::value || std::is_enum<T>::value;
};

/// For integer it returns the integer, and for enum it returns its underlying_type.
// https://stackoverflow.com/questions/56972288/metaprogramming-construct-that-returns-underlying-type-for-an-enum-and-integer-f
template< typename T >
struct int_type
{
	using type = typename std::conditional<
		/*if*/ std::is_enum<T>::value,
		/*then*/ std::underlying_type<T>,
		/*else*/ std::enable_if< std::is_integral<T>::value, T >
	>::type::type;
};

template< typename T1, typename T2 >
struct bigger_type
{
	using type = typename std::conditional<
		/*if*/ (sizeof(T1) >= sizeof(T2)),
		/*then*/ T1,
		/*else*/ T2
	>::type;
};

template< typename SrcT, typename DstT >
struct corresponding_constness
{
	using type = typename std::conditional<
		/*if*/ std::is_const< SrcT >::value,
		/*then*/ std::add_const< DstT >,
		/*else*/ std::remove_const< DstT >
	>::type::type;
};

// https://stackoverflow.com/questions/13830158/check-if-a-variable-type-is-iterable
template< typename T, typename = void >
struct is_range : std::false_type {};
template <typename T>
struct is_range< T,
	std::void_t< decltype( std::begin( std::declval< T & >() ) ), decltype( std::end( std::declval< T & >() ) ) >
> : std::true_type {};

template< typename T >
struct range_element
{
	using type = typename std::remove_reference< decltype( *std::begin( std::declval< T & >() ) ) >::type;
};

template< typename T >
struct range_value
{
	using type = typename fut::remove_cvref< decltype( *std::begin( std::declval< T & >() ) ) >::type;
};

namespace impl
{
	template< typename T, typename E, REQUIRES( is_range< T >::value ) >
	constexpr bool is_range_of()
	{
		return std::is_same< typename range_element<T>::type, E >::value;
	}
	template< typename T, typename E, REQUIRES( !is_range< T >::value ) >
	constexpr bool is_range_of()
	{
		return false;
	}
}
template< typename T, typename E >
struct is_range_of
{
	static constexpr bool value = impl::is_range_of< T, E >();
};

template< typename T >
struct is_byte_range : is_range_of< T, uint8_t > {};

template< typename T >
struct is_trivial_range
{
	static constexpr bool value = is_range< T >::value && std::is_trivial< typename range_value<T>::type >::value;
};

template< typename T >
struct is_c_array : std::false_type {};
template< typename T, size_t N >
struct is_c_array< T [N] > : std::true_type {};
template< typename T, size_t N >
struct is_c_array< T (&)[N] > : std::true_type {};

template< typename T, typename ElemType >
struct is_c_array_of
{
	static constexpr bool value = is_c_array<T>::value && std::is_same< typename range_value<T>::type, ElemType >::value;
};

namespace impl
{
	template< typename T >
	struct is_std_array : std::false_type {};
	template< typename ElemType, size_t size_ >
	struct is_std_array< std::array< ElemType, size_ > > : std::true_type {};
}
template< typename T >
struct is_std_array : impl::is_std_array< typename std::remove_cv<T>::type > {};


} // namespace own


#endif // CPPUTILS_TYPETRAITS_INCLUDED
