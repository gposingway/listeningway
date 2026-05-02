//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: types and functions dealing with endianity
//======================================================================================================================

#ifndef CPPUTILS_ENDIANITY_INCLUDED
#define CPPUTILS_ENDIANITY_INCLUDED


#include "Essential.hpp"

#include "TypeTraits.hpp"

#include <cstdint>


namespace own {


enum class Endianity
{
	Little,
	Big
};


//======================================================================================================================
//  integer serialization and conversion
//  NOTE: The following functions perform no boundary checking, caller must ensure there is enough space in the buffer
//        to do the read or write. Preffer using ByteStream.h whenever possible.
//  Reasoning: This code is shared between ByteStream and BitStream, so it needs to exist outside to prevent duplication
//             but protocol implementor should use the ByteStream or BitStream and not these utils directly.

//----------------------------------------------------------------------------------------------------------------------
//  integers with statically known size

/// Converts an arbitrary integral number from native format to little endian and writes it into the buffer.
template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
void writeLittleEndian( uint8_t * const bufferPos, Type native ) noexcept
{
	using IntType = typename int_type< Type >::type;
	IntType nativeInt = IntType( native );

	// indexed variant is more optimizable than variant with pointers https://godbolt.org/z/McT3Yb
	size_t pos = 0;
	while (pos < sizeof( nativeInt )) {
		bufferPos[ pos ] = uint8_t( nativeInt & 0xFF );
		nativeInt >>= 8;
		++pos;
	}
}

// if the Type is uint8_t, shift by 8 is undefined, so we need to have a separate version
/*template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) == 1 ) >
void writeLittleEndian( uint8_t * const bufferPos, Type native ) noexcept
{
	*bufferPos = uint8_t( native );
}*/

/// Converts an arbitrary integral number from native format to big endian and writes it into the buffer.
template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
void writeBigEndian( uint8_t * const currentPos, Type native ) noexcept
{
	using IntType = typename int_type< Type >::type;
	IntType nativeInt = IntType( native );

	// indexed variant is more optimizable than variant with pointers https://godbolt.org/z/McT3Yb
	size_t pos = sizeof( nativeInt );
	while (pos > 0) {  // can't use traditional for-loop approach, because index is unsigned
		--pos;         // so we can't check if it's < 0
		currentPos[ pos ] = uint8_t( nativeInt & 0xFF );
		nativeInt >>= 8;
	}
}

// if the Type is uint8_t, shift by 8 is undefined, so we need to have a separate version
/*template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) == 1 ) >
void writeBigEndian( uint8_t * const bufferPos, Type native ) noexcept
{
	*bufferPos = uint8_t( native );
}*/

/// Reads an arbitrary integral number from the buffer and converts it from little endian to native format.
template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
Type readLittleEndian( const uint8_t * const bufferPos ) noexcept
{
	using IntType = typename int_type< Type >::type;
	IntType nativeInt = 0;

	// indexed variant is more optimizable than variant with pointers https://godbolt.org/z/McT3Yb
	size_t pos = sizeof( IntType );
	while (pos > 0) {  // can't use traditional for-loop approach, because index is unsigned
		--pos;         // so we can't check if it's < 0
		nativeInt <<= 8;
		nativeInt = IntType( nativeInt | bufferPos[ pos ] );
	}

	return Type( nativeInt );
}

// if the Type is uint8_t, shift by 8 is undefined, so we need to have a separate version
/*template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) == 1 ) >
Type readLittleEndian( const uint8_t * const bufferPos ) noexcept
{
	return Type( *bufferPos );
}*/

/// Reads an arbitrary integral number from the buffer and converts it from big endian to native format.
template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
Type readBigEndian( const uint8_t * const bufferPos ) noexcept
{
	using IntType = typename int_type< Type >::type;
	IntType nativeInt = 0;

	// indexed variant is more optimizable than variant with pointers https://godbolt.org/z/McT3Yb
	size_t pos = 0;
	while (pos < sizeof( IntType )) {
		nativeInt <<= 8;
		nativeInt = IntType( nativeInt | bufferPos[ pos ] );
		++pos;
	}

	return Type( nativeInt );
}

// if the Type is uint8_t, shift by 8 is undefined, so we need to have a separate version
/*template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) == 1 ) >
Type readBigEndian( const uint8_t * const bufferPos ) noexcept
{
	return Type( *bufferPos );
}*/


} // namespace own


#endif // CPPUTILS_ENDIANITY_INCLUDED
