//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: serialization and deserialization of common parts of the protocol
//======================================================================================================================

#ifndef OPENRGB_PROTOCOL_COMMON_INCLUDED
#define OPENRGB_PROTOCOL_COMMON_INCLUDED


#include <CppUtils-Essential/ContainerUtils.hpp>
#include <CppUtils-Essential/BinaryStream.hpp>
MAKE_LITTLE_ENDIAN_DEFAULT

#include <cstring>
#include <string>
#include <vector>


namespace orgb {


//======================================================================================================================
// Putting these template functions into a struct allows us to collectively mark them as friend and allow them to access
// methods of Mode, Zone, LED that should be private to the user of the library, but accessible to the library itself.
// The struct is basically used as a "friendable" namespace here.

struct protocol
{

	//-- OpenRGB strings -----------------------------------------------------------------------------------------------

	static size_t sizeofString( const std::string & str ) noexcept
	{
		return 2 + str.size() + 1;
	}

	static void writeString( own::BinaryOutputStream & stream, const std::string & str )
	{
		stream << uint16_t( str.size() + 1 );
		stream.writeString0( str );
	}

	static bool readString( own::BinaryInputStream & stream, std::string & str ) noexcept
	{
		uint16_t size = 0;
		stream >> size;
		stream.readString( str, size > 0 ? size - 1 : 0 );  // don't rely on the '\0' terminator, read by the size
		stream.skip(1);  // move reading pointer past the expected '\0'
		return !stream.failed() && strlen( str.c_str() ) + 1 == size;
	}


	//-- OpenRGB arrays ------------------------------------------------------------------------------------------------

	template< typename Type, REQUIRES( !std::is_trivial<Type>::value ) >
	static size_t sizeofVectorOfDynamicObjects( const std::vector< Type > & vec, uint32_t protocolVersion ) noexcept
	{
		size_t size = 0;
		for (const auto & elem : vec)
		{
			size += elem.calcSize( protocolVersion );
		}
		return size;
	}

	static size_t sizeofVectorOfStrings( const std::vector< std::string > & vec ) noexcept
	{
		size_t size = 0;
		for (const auto & elem : vec)
		{
			size += sizeofString( elem );
		}
		return size;
	}

	template< typename Type, REQUIRES( std::is_trivial<Type>::value ) >
	static size_t sizeofArray( const std::vector< Type > & vec ) noexcept
	{
		return 2 + own::sizeofVector( vec );
	}

	static size_t sizeofArray( const std::vector< std::string > & vec ) noexcept
	{
		return 2 + sizeofVectorOfStrings( vec );
	}

	template< typename Type, REQUIRES( !std::is_trivial<Type>::value ) >
	static size_t sizeofArray( const std::vector< Type > & vec, uint32_t protocolVersion ) noexcept
	{
		return 2 + sizeofVectorOfDynamicObjects( vec, protocolVersion );
	}

	template< typename Type, REQUIRES( std::is_trivial<Type>::value ) >
	static void writeArray( own::BinaryOutputStream & stream, const std::vector< Type > & vec )
	{
		stream << uint16_t(vec.size());
		for (const auto & elem : vec)
		{
			stream << elem;
		}
	}

	static void writeArray( own::BinaryOutputStream & stream, const std::vector< std::string > & vec )
	{
		stream << uint16_t(vec.size());
		for (const auto & elem : vec)
		{
			writeString( stream, elem );
		}
	}

	template< typename Type, REQUIRES( !std::is_trivial<Type>::value ) >
	static void writeArray( own::BinaryOutputStream & stream, const std::vector< Type > & vec, uint32_t protocolVersion )
	{
		stream << uint16_t(vec.size());
		for (const auto & elem : vec)
		{
			elem.serialize( stream, protocolVersion );
		}
	}

	template< typename Type, REQUIRES( std::is_trivial<Type>::value ) >
	static bool readArray( own::BinaryInputStream & stream, std::vector< Type > & vec ) noexcept
	{
		uint16_t size = 0;
		stream >> size;
		vec.resize( size );
		for (uint16_t i = 0; i < size; ++i)
		{
			stream >> vec[i];
			if (stream.failed())
				return false;
		}
		return !stream.failed();
	}

	static bool readArray( own::BinaryInputStream & stream, std::vector< std::string > & vec ) noexcept
	{
		uint16_t size = 0;
		stream >> size;
		vec.resize( size );
		for (uint16_t i = 0; i < size; ++i)
		{
			readString( stream, vec[i] );
			if (stream.failed())
				return false;
		}
		return !stream.failed();
	}

	template< typename Type, REQUIRES( !std::is_trivial<Type>::value ) >
	static bool readArray( own::BinaryInputStream & stream, std::vector< Type > & vec, uint32_t protocolVersion, uint32_t parentIdx ) noexcept
	{
		uint16_t size = 0;
		stream >> size;
		vec.reserve( size );
		for (uint16_t i = 0; i < size; ++i)
		{
			Type obj;
			if (!obj.deserialize( stream, protocolVersion, i, parentIdx ))
				return false;
			vec.emplace_back( move(obj) );
		}
		return !stream.failed();
	}

};


//======================================================================================================================


} // namespace orgb


#endif // OPENRGB_PROTOCOL_COMMON_INCLUDED
