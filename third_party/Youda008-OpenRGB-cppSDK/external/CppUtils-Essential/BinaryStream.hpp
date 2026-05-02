//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: classes for binary serialization into binary buffers via operators << and >>
//======================================================================================================================

#ifndef CPPUTILS_BINARY_STREAM_INCLUDED
#define CPPUTILS_BINARY_STREAM_INCLUDED


#include "Essential.hpp"

#include "TypeTraits.hpp"  // is_int_or_enum
#include "Span.hpp"
#include "Endianity.hpp"
#include "Safety.hpp"
#include "CriticalError.hpp"

#include <string>
#include <vector>  // toByteVector, fromByteVector


namespace own {


//======================================================================================================================
/// Binary buffer output stream allowing serialization via operator<< .
/** This is a binary alternative of std::ostringstream. First you allocate a buffer, then you construct this stream
  * object, and then you write all the data you need using operator<< or named methods. */

class BinaryOutputStream
{
	uint8_t * _begPos;  ///< position of the beginning of the buffer
	uint8_t * _curPos;  ///< current position in the buffer
	uint8_t * _endPos;  ///< position of the end of the buffer

 public:

	BinaryOutputStream( const BinaryOutputStream & ) = delete;
	BinaryOutputStream( BinaryOutputStream && ) = default;
	BinaryOutputStream & operator=( const BinaryOutputStream & other ) = delete;
	BinaryOutputStream & operator=( BinaryOutputStream && other ) = default;

	void reset( byte_span buffer ) noexcept
	{
		_begPos = reinterpret_cast< uint8_t * >( buffer.data() );
		_curPos = reinterpret_cast< uint8_t * >( buffer.data() );
		_endPos = reinterpret_cast< uint8_t * >( buffer.data() + buffer.size() );
	}

	/// Initializes a binary output stream operating over any byte container with continuous memory.
	/** WARNING: The class takes non-owning reference to a buffer. You are responsible for making sure the buffer exists
	  * at least as long as this object and for allocating the storage big enough for all write operations to fit in. */
	BinaryOutputStream( byte_span buffer ) noexcept
	{
		reset( buffer );
	}

	//-- atomic elements -----------------------------------------------------------------------------------------------

	void put( uint8_t b )
	{
		checkWrite( 1, "byte" );

		*_curPos = b;
		_curPos++;
	}

	void put( char c )
	{
		checkWrite( 1, "char" );

		*_curPos = uint8_t(c);
		_curPos++;
	}

	BinaryOutputStream & operator<<( uint8_t b )
	{
		put( b );
		return *this;
	}

	BinaryOutputStream & operator<<( char c )
	{
		put( c );
		return *this;
	}

	/// Writes raw bytes of an object as they are, without any byte conversion or deep serialization.
	template< typename Type >
	void writeRaw( const Type & obj )
	{
		checkWrite( 1, "raw object" );

		_writeRaw( reinterpret_cast< const uint8_t * >( &obj ), sizeof( obj ) );
		_curPos += sizeof( obj );
	}

	/// Serializes an object via its operator<< and writes the result into the buffer.
	template< typename Type >
	void writeDeep( const Type & obj )
	{
		*this << obj;
	}

	//-- integers ------------------------------------------------------------------------------------------------------

	/// Converts an arbitrary integral number from native format to little endian and writes it into the buffer.
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
	void writeLittleEndian( Type native )
	{
		checkWrite( sizeof(native), "int" );

		own::writeLittleEndian( _curPos, native );
		_curPos += sizeof(native);
	}

	/// Converts an arbitrary integral number from native format to big endian and writes it into the buffer.
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
	void writeBigEndian( Type native )
	{
		checkWrite( sizeof(native), "int" );

		own::writeBigEndian( _curPos, native );
		_curPos += sizeof(native);
	}

	//-- strings and arrays --------------------------------------------------------------------------------------------

	/// Writes raw bytes of an arbitrary array as they are, without any byte conversion or deep serialization.
	template< typename ContType, REQUIRES( is_trivial_range<ContType>::value ) >
	void writeRawArray( const ContType & array )
	{
		using ElemType = typename own::range_element< ContType >::type;
		const size_t writeSize = fut::size( array ) * sizeof( ElemType );

		checkWrite( writeSize, "array" );

		_writeRaw( fut::data( array ), writeSize );
		_curPos += writeSize;
	}

	/// Writes specified number of bytes from a continuous memory storage to the buffer.
	void writeBytes( const_byte_span buffer );

	/// Writes specified number of chars from a continuous memory storage to the buffer.
	void writeChars( const_char_span buffer );

	/// Writes a string WITHOUT its null terminator to the buffer.
	void writeString( const std::string & str );

	/// Writes a string WITH its null terminator to the buffer.
	void writeString0( const std::string & str );

	BinaryOutputStream & operator<<( const_byte_span buffer )
	{
		writeBytes( buffer );
		return *this;
	}

	BinaryOutputStream & operator<<( const_char_span buffer )
	{
		writeChars( buffer );
		return *this;
	}

	/// Writes specified number of zero bytes to the buffer.
	void writeZeros( size_t numZeroBytes );

	//-- position manipulation -----------------------------------------------------------------------------------------

	/// Returns how many bytes is the current stream position from the beginning of the buffer.
	size_t offset() const noexcept
	{
		return size_t( _curPos - _begPos );
	}

	/// Returns how many bytes is the current stream position from the end of the buffer.
	size_t remaining() const noexcept
	{
		return size_t( _endPos - _curPos );
	}

	/// Returns true when the position in the stream reaches the end.
	bool atEnd() const
	{
		return _curPos >= _endPos;
	}

 private:

	void checkWrite( MAYBE_UNUSED size_t size, MAYBE_UNUSED const char * type )
	{
	 #ifdef SAFETY_CHECKS
		if (_curPos + size > _endPos)
		{
			critical_error(
				"Attempted to write %s of size %zu past the buffer end, remaining size: %zu", type, size, remaining()
			);
		}
	 #endif
	}

	void _writeRaw( const uint8_t * ptr, size_t numBytes ) noexcept;

};


//======================================================================================================================
/// Binary buffer input stream allowing deserialization via operator>> .
/** This is a binary alternative of std::istringstream. First you allocate a buffer, then you construct this stream
  * object, and then you read the data you expect using operator>> or named methods.
  * If an attempt to read past the end of the input buffer is made, the stream sets its internal error flag
  * and returns default values for any further read operations. The error flag can be checked with Failed(). */

class BinaryInputStream
{
	const uint8_t * _begPos;  ///< position of the beginning of the buffer
	const uint8_t * _curPos;  ///< current position in the buffer
	const uint8_t * _endPos;  ///< position of the end of the buffer
	bool _failed;  ///< the end was reached while attemting to read from the buffer

 public:

	BinaryInputStream( const BinaryInputStream & ) = delete;
	BinaryInputStream( BinaryInputStream && ) = default;
	BinaryInputStream & operator=( const BinaryInputStream & other ) = delete;
	BinaryInputStream & operator=( BinaryInputStream && other ) = default;

	void reset( const_byte_span buffer ) noexcept
	{
		_begPos = reinterpret_cast< const uint8_t * >( buffer.data() );
		_curPos = reinterpret_cast< const uint8_t * >( buffer.data() );
		_endPos = reinterpret_cast< const uint8_t * >( buffer.data() + buffer.size() );
		_failed = false;
	}

	/// Initializes a binary input stream operating over any byte container with continuous memory.
	/** WARNING: The class takes non-owning reference to a buffer.
	  * You are responsible for making sure the buffer exists at least as long as this object. */
	BinaryInputStream( const_byte_span buffer ) noexcept
	{
		reset( buffer );
	}

	//-- atomic elements -----------------------------------------------------------------------------------------------

	uint8_t get() noexcept
	{
		if (canRead( 1 )) {
			return *(_curPos++);
		} else {
			return 0;
		}
	}

	char getChar() noexcept
	{
		return char( get() );
	}

	BinaryInputStream & operator>>( uint8_t & b ) noexcept
	{
		b = get();
		return *this;
	}

	BinaryInputStream & operator>>( char & c ) noexcept
	{
		c = getChar();
		return *this;
	}

	/// Reads raw bytes of an object as they are, without any byte conversion or deep deserialization.
	/** (output parameter variant) */
	template< typename Type >
	bool readRaw( Type & obj )
	{
		if (!canRead( sizeof( obj ) )) {
			return false;
		}

		_readRaw( reinterpret_cast< uint8_t * >( &obj ), sizeof( obj ) );
		_curPos += sizeof( obj );
		return true;
	}

	/// Reads raw bytes of an object as they are, without any byte conversion or deep deserialization.
	/** (return value variant) */
	template< typename Type >
	Type readRaw()
	{
		Type obj;
		readRaw( obj );
		return obj;
	}

	/// Deserializes an object via its operator>> and returns the result.
	/** (output parameter variant) */
	template< typename Type >
	bool readDeep( Type & obj )
	{
		*this >> obj;
		return !_failed;
	}

	/// Deserializes an object via its operator>> and returns the result.
	/** (return value variant) */
	template< typename Type >
	Type readDeep()
	{
		Type obj;
		*this >> obj;
		return obj;
	}

	//-- integers ------------------------------------------------------------------------------------------------------

	/// Reads an arbitrary integral number from the buffer and converts it from little endian to native format.
	/** (return value variant) */
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
	Type readLittleEndian() noexcept
	{
		if (!canRead( sizeof(Type) )) {
			return Type(0);
		}

		Type native = own::readLittleEndian< Type >( _curPos );
		_curPos += sizeof(Type);
		return native;
	}

	/// Reads an arbitrary integral number from the buffer and converts it from big endian to native format.
	/** (output parameter variant) */
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
	bool readLittleEndian( Type & native ) noexcept
	{
		if (!canRead( sizeof(Type) )) {
			return false;
		}

		native = own::readLittleEndian< Type >( _curPos );
		_curPos += sizeof(Type);
		return true;
	}

	/// Reads an arbitrary integral number from the buffer and converts it from big endian to native format.
	/** (return value variant) */
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
	Type readBigEndian() noexcept
	{
		if (!canRead( sizeof(Type) )) {
			return Type(0);
		}

		Type native = own::readBigEndian< Type >( _curPos );
		_curPos += sizeof(Type);
		return native;
	}

	/// Reads an arbitrary integral number from the buffer and converts it from big endian to native format.
	/** (output parameter variant) */
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) >
	bool readBigEndian( Type & native ) noexcept
	{
		if (!canRead( sizeof(Type) )) {
			return false;
		}

		native = own::readBigEndian< Type >( _curPos );
		_curPos += sizeof(Type);
		return true;
	}

	//-- strings and arrays --------------------------------------------------------------------------------------------

	// We must have overload for both generic container and span, because temporary rvalue of span will not
	// fit into the non-const container reference.

	/// Reads raw bytes of an arbitrary array as they are, without any byte conversion or deep deserialization.
	template< typename ElemType, REQUIRES( std::is_trivial<ElemType>::value ) >
	bool readRawArray( span< ElemType > array ) noexcept
	{
		const size_t readSize = array.size() * sizeof(ElemType);

		if (!canRead( readSize )) {
			return false;
		}

		_readRaw( array.data(), readSize );
		_curPos += readSize;
		return true;
	}

	/// \copydoc readRawArray( span< ElemType > )
	// This overload is needed because the above function is a template and containers won't convert to span implicitly.
	template< typename ContType, REQUIRES( is_trivial_range<ContType>::value ) >
	bool readRawArray( ContType & array ) noexcept
	{
		return readRawArray( make_span( array ) );
	}

	/// Reads raw bytes of an arbitrary array into a resizable container.
	/** The container is automatically resized before copying the data into it. */
	template< typename ContType, REQUIRES( is_trivial_range<ContType>::value ) >
	bool readResizableRawArray( ContType & cont, size_t size ) noexcept
	{
		using ElemType = typename range_value<ContType>::type;
		const size_t readSize = size * sizeof(ElemType);

		if (!canRead( readSize )) {
			return false;
		}

		cont.resize( readSize );
		_readRaw( cont.data(), readSize );
		_curPos += readSize;
		return true;
	}

	/// Reads a span of bytes from the buffer into a given container.
	bool readBytes( byte_span buffer ) noexcept;

	/// Reads a span of bytes from the buffer into a given container.
	bool readChars( char_span buffer ) noexcept;

	/// Reads a span of bytes from the buffer into a resizable container.
	/** The container is automatically resized before copying the bytes into it. */
	template< typename ContType, REQUIRES( is_byte_range<ContType>::value ) >
	bool readResizableBytes( ContType & cont, size_t size ) noexcept
	{
		return readResizableRawArray( cont, size );
	}

	/// Reads a string of specified size from the buffer.
	/** (output parameter variant) */
	bool readString( std::string & str, size_t size ) noexcept;

	/// Reads a string of specified size from the buffer.
	/** (return value variant) */
	std::string readString( size_t size ) noexcept
	{
		std::string str;
		readString( str, size );
		return str;
	}

	/// Reads a string from the buffer until a null terminator is found.
	/** (output parameter variant) */
	bool readString0( std::string & str ) noexcept;

	/// Reads a string from the buffer until a null terminator is found.
	/** (return value variant) */
	std::string readString0() noexcept
	{
		std::string str;
		readString0( str );
		return str;
	}

	BinaryInputStream & operator>>( byte_span buffer ) noexcept
	{
		readBytes( buffer );
		return *this;
	}

	BinaryInputStream & operator>>( char_span buffer ) noexcept
	{
		readChars( buffer );
		return *this;
	}

	/// Reads the remaining data from the current position until the end of the buffer to a resizable container.
	/** The container is automatically resized before copying the bytes into it. */
	template< typename ContType, REQUIRES( is_byte_range<ContType>::value ) >
	bool readRemaining( ContType & cont ) noexcept
	{
		return readResizableBytes( cont, remaining() );
	}

	//-- position manipulation -----------------------------------------------------------------------------------------

	/// Returns how many bytes is the current stream position from the beginning of the buffer.
	size_t offset() const noexcept
	{
		return size_t( _curPos - _begPos );
	}

	/// Returns how many bytes is the current stream position from the end of the buffer.
	size_t remaining() const noexcept
	{
		return size_t( _endPos - _curPos );
	}

	/// Returns true when the position in the stream reaches the end.
	bool atEnd() const
	{
		return _curPos >= _endPos;
	}

	/// Moves over specified number of bytes without returning them to the user.
	bool skip( size_t numBytes ) noexcept
	{
		if (!canRead( numBytes )) {
			return false;
		}

		_curPos += numBytes;
		return true;
	}

	void rewind( size_t numBytes ) noexcept
	{
		if (_curPos - numBytes < _begPos) {
			_failed = true;
			return;
		}
		_curPos -= numBytes;
		_failed = false;
	}

	void rewindToBeginning() noexcept
	{
		_curPos = _begPos;
		_failed = false;
	}

	//-- error handling ------------------------------------------------------------------------------------------------

	void setFailed() noexcept { _failed = true; }
	void resetFailed() noexcept { _failed = false; }
	bool failed() const noexcept { return _failed; }

 private:

	bool canRead( size_t size ) noexcept
	{
		// the _failed flag can be true already from the previous call, in that case it will stay failed
		_failed |= _curPos + size > _endPos;
		return !_failed;
	}

	void _readRaw( uint8_t * ptr, size_t numBytes ) noexcept;

};


//======================================================================================================================
//  misc utils

template< typename Type >
std::vector< uint8_t > toByteVector( const Type & obj )
{
	std::vector< uint8_t > bytes( obj.size() );
	BinaryOutputStream stream( bytes );
	stream << obj;
	return bytes;
}

template< typename Type >
bool fromBytes( const_byte_span bytes, Type & obj )
{
	BinaryInputStream stream( bytes );
	stream >> obj;
	return !stream.failed();
}


//======================================================================================================================


} // namespace own


//======================================================================================================================
//  this allows you to use operator << and >> for integers and enums without losing the choice between little/big endian

#define MAKE_LITTLE_ENDIAN_DEFAULT \
namespace own {\
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) > \
	inline own::BinaryOutputStream & operator<<( own::BinaryOutputStream & stream, Type val ) \
	{\
		stream.writeLittleEndian( val ); \
		return stream; \
	}\
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) > \
	inline own::BinaryInputStream & operator>>( own::BinaryInputStream & stream, Type & val ) \
	{\
		stream.readLittleEndian( val ); \
		return stream; \
	}\
}\
using own::operator<<;\
using own::operator>>;

#define MAKE_BIG_ENDIAN_DEFAULT \
namespace own {\
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) > \
	inline own::BinaryOutputStream & operator<<( own::BinaryOutputStream & stream, Type val ) \
	{\
		stream.writeBigEndian( val ); \
		return stream; \
	}\
	template< typename Type, REQUIRES( is_int_or_enum<Type>::value && sizeof(Type) != 1 ) > \
	inline own::BinaryInputStream & operator>>( own::BinaryInputStream & stream, Type & val ) \
	{\
		stream.readBigEndian( val ); \
		return stream; \
	}\
}\
using own::operator<<;\
using own::operator>>;


//======================================================================================================================


#endif // CPPUTILS_BINARY_STREAM_INCLUDED
