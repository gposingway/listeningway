//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: classes for binary serialization into binary buffers via operators << and >>
//======================================================================================================================

#include "BinaryStream.hpp"

#include <string>
using std::string;
#include <algorithm>  // find, copy
using std::find;
using std::copy;
#include <cstring>  // memcpy


namespace own {


//----------------------------------------------------------------------------------------------------------------------
//  strings and arrays

// We do this only to hide <cstring> from the user of BinaryStream and reduce include bloat.
void BinaryOutputStream::_writeRaw( const uint8_t * ptr, size_t numBytes ) noexcept
{
	memcpy( _curPos, ptr, numBytes );
}
void BinaryInputStream::_readRaw( uint8_t * ptr, size_t numBytes ) noexcept
{
	memcpy( ptr, _curPos, numBytes );
}

void BinaryOutputStream::writeBytes( const_byte_span buffer )
{
	checkWrite( buffer.size(), "bytes" );

	memcpy( _curPos, buffer.data(), buffer.size() );
	_curPos += buffer.size();
}

void BinaryOutputStream::writeChars( const_char_span buffer )
{
	checkWrite( buffer.size(), "chars" );

	memcpy( _curPos, buffer.data(), buffer.size() );
	_curPos += buffer.size();
}

void BinaryOutputStream::writeString( const string & str )
{
	checkWrite( str.size(), "string" );

	memcpy( _curPos, reinterpret_cast< const uint8_t * >( str.data() ), str.size() );
	_curPos += str.size();
}

void BinaryOutputStream::writeString0( const string & str )
{
	checkWrite( str.size() + 1, "string" );

	memcpy( _curPos, reinterpret_cast< const uint8_t * >( str.data() ), str.size() + 1 );
	_curPos += str.size() + 1;
}

void BinaryOutputStream::writeZeros( size_t numZeroBytes )
{
	checkWrite( numZeroBytes, "zeros" );

	memset( _curPos, 0, numZeroBytes );
	_curPos += numZeroBytes;
}

bool BinaryInputStream::readBytes( byte_span buffer ) noexcept
{
	if (!canRead( buffer.size() )) {
		return false;
	}

	memcpy( buffer.data(), _curPos, buffer.size() );
	_curPos += buffer.size();
	return true;
}

bool BinaryInputStream::readChars( char_span buffer ) noexcept
{
	if (!canRead( buffer.size() )) {
		return false;
	}

	memcpy( buffer.data(), _curPos, buffer.size() );
	_curPos += buffer.size();
	return true;
}

bool BinaryInputStream::readString( string & str, size_t size ) noexcept
{
	if (!canRead( size )) {
		return false;
	}

	str.resize( size );
	memcpy( const_cast< char * >( str.data() ), _curPos, size );
	_curPos += size;
	return true;
}

bool BinaryInputStream::readString0( string & str ) noexcept
{
	if (!_failed)
	{
		const uint8_t * strEndPos = find( _curPos, _endPos, '\0' );
		if (strEndPos >= _endPos)
		{
			_failed = true;
		}
		else
		{
			const size_t size = size_t( strEndPos - _curPos );
			str.resize( size );
			memcpy( const_cast< char * >( str.data() ), _curPos, size );
			_curPos += str.size() + 1;
		}
	}
	return !_failed;
}


//----------------------------------------------------------------------------------------------------------------------


} // namespace own
