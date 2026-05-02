//======================================================================================================================
// Project: CppUtils
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: classes for network addresses
//======================================================================================================================

#ifndef CPPUTILS_NETADDRESS_INCLUDED
#define CPPUTILS_NETADDRESS_INCLUDED


#include <CppUtils-Essential/Essential.hpp>

#include <CppUtils-Essential/Span.hpp>
#include <CppUtils-Essential/CriticalError.hpp>

#include <iosfwd>
#include <initializer_list>

// forward declaration of OS-dependent types
struct in_addr;
struct in6_addr;
struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;
struct sockaddr_storage;


namespace own {


//======================================================================================================================
/// private implementation details

namespace priv {

	void genericCopy( const uint8_t * src, uint8_t * dst, size_t size ) noexcept;
	inline void fastCopy4( const uint8_t * src, uint8_t * dst ) noexcept
	{
		*reinterpret_cast< uint32_t * >( dst ) = *reinterpret_cast< const uint32_t * >( src );
	}
	inline void fastCopy16( const uint8_t * src, uint8_t * dst ) noexcept
	{
		reinterpret_cast< uint64_t * >( dst )[0] = reinterpret_cast< const uint64_t * >( src )[0];
		reinterpret_cast< uint64_t * >( dst )[1] = reinterpret_cast< const uint64_t * >( src )[1];
	}

	inline void copyFromDynamic( uint8_t * addr, size_t addrSize, const uint8_t * data, size_t dataSize )
	{
		if (dataSize != addrSize)
			critical_error( "Attempted to construct address of size %zu from buffer of size %zu.", addrSize, dataSize );
		genericCopy( data, addr, addrSize );
	}
	inline void copy4FromDynamic( uint8_t * addr, const uint8_t * data, size_t dataSize )
	{
		if (dataSize != 4)
			critical_error( "Attempted to construct address of size 4 from buffer of size %zu.", dataSize );
		fastCopy4( data, addr );
	}
	inline void copy16FromDynamic( uint8_t * addr, const uint8_t * data, size_t dataSize )
	{
		if (dataSize != 16)
			critical_error( "Attempted to construct address of size 16 from buffer of size %zu.", dataSize );
		fastCopy16( data, addr );
	}

	int genericCompare( const uint8_t * a1, const uint8_t * a2, size_t size ) noexcept;

	// system addresses are in the same byte order as ours
	inline void ownAddrToSysAddrV4( const uint8_t * ownAddr, struct in_addr * sysAddr ) noexcept
	{
		fastCopy4( ownAddr, reinterpret_cast< uint8_t * >( sysAddr ));
	}
	inline void ownAddrToSysAddrV6( const uint8_t * ownAddr, struct in6_addr * sysAddr ) noexcept
	{
		fastCopy16( ownAddr, reinterpret_cast< uint8_t * >( sysAddr ));
	}
	inline void sysAddrToOwnAddrV4( const struct in_addr * sysAddr, uint8_t * ownAddr ) noexcept
	{
		fastCopy4( reinterpret_cast< const uint8_t * >( sysAddr ), ownAddr );
	}
	inline void sysAddrToOwnAddrV6( const struct in6_addr * sysAddr, uint8_t * ownAddr ) noexcept
	{
		fastCopy16( reinterpret_cast< const uint8_t * >( sysAddr ), ownAddr );
	}

}


//======================================================================================================================
/// common base for all addresses

template< size_t Size >
class GenericAddr
{

 protected:

	uint8_t _data [Size];

 public:

	GenericAddr() noexcept {}

	GenericAddr( std::initializer_list< uint8_t > initList )
	{
		priv::copyFromDynamic( _data, Size, &*initList.begin(), initList.size() );
	}
	GenericAddr( const_byte_span data )
	{
		priv::copyFromDynamic( _data, Size, data.data(), data.size() );
	}
	GenericAddr( fixed_const_byte_span< Size > data ) noexcept
	{
		priv::genericCopy( data.data(), _data, Size );
	}
	GenericAddr( const GenericAddr< Size > & other ) noexcept
	{
		priv::genericCopy( other._data, _data, Size );
	}

	GenericAddr< Size > & operator=( std::initializer_list< uint8_t > initList )
	{
		priv::copyFromDynamic( _data, Size, &*initList.begin(), initList.size() );
		return *this;
	}
	GenericAddr< Size > & operator=( const_byte_span data )
	{
		priv::copyFromDynamic( _data, Size, data.data(), data.size() );
		return *this;
	}
	GenericAddr< Size > & operator=( fixed_const_byte_span< Size > data ) noexcept
	{
		priv::genericCopy( data.data(), _data, Size );
		return *this;
	}
	GenericAddr< Size > & operator=( const GenericAddr< Size > & other ) noexcept
	{
		priv::genericCopy( other._data, _data, Size );
		return *this;
	}

	fixed_byte_span< Size >       data()       noexcept  { return make_fixed_span( _data ) ; }
	fixed_const_byte_span< Size > data() const noexcept  { return make_fixed_const_span( _data ); }

	      uint8_t & operator[]( size_t idx )        { return _data[ idx ]; }
	const uint8_t & operator[]( size_t idx ) const  { return _data[ idx ]; }

	bool operator==( const GenericAddr< Size > & other ) const noexcept
	{
		return priv::genericCompare( _data, other._data ) == 0;
	}
	bool operator< ( const GenericAddr< Size > & other ) const noexcept
	{
		return priv::genericCompare( _data, other._data ) < 0;
	}
	bool operator> ( const GenericAddr< Size > & other ) const noexcept
	{
		return priv::genericCompare( _data, other._data ) > 0;
	}

};


//======================================================================================================================
//  IP addresses

class IPAddr;

enum class IPVer
{
	_4 = 4,
	_6 = 6
};


/// IP address version 4
class IPv4Addr : public GenericAddr<4>
{
	friend class IPAddr;

 public:

	using GenericAddr<4>::GenericAddr;

	IPv4Addr( std::initializer_list< uint8_t > initList )
	{
		priv::copy4FromDynamic( _data, &*initList.begin(), initList.size() );
	}
	IPv4Addr( const_byte_span data )
	{
		priv::copy4FromDynamic( _data, data.data(), data.size() );
	}
	IPv4Addr( fixed_const_byte_span<4> data ) noexcept
	{
		priv::fastCopy4( data.data(), _data );
	}
	IPv4Addr( const IPv4Addr & other ) noexcept : GenericAddr<4>()
	{
		priv::fastCopy4( other.data().data(), _data );
	}

	IPv4Addr & operator=( std::initializer_list< uint8_t > initList )
	{
		priv::copy4FromDynamic( _data, &*initList.begin(), initList.size() );
		return *this;
	}
	IPv4Addr & operator=( const_byte_span data )
	{
		priv::copy4FromDynamic( _data, data.data(), data.size() );
		return *this;
	}
	IPv4Addr & operator=( fixed_const_byte_span<4> data ) noexcept
	{
		priv::fastCopy4( data.data(), _data );
		return *this;
	}
	IPv4Addr & operator=( const IPv4Addr & other ) noexcept
	{
		priv::fastCopy4( other.data().data(), _data );
		return *this;
	}

	friend std::ostream & operator<<( std::ostream & os, IPv4Addr addr );
	friend std::istream & operator>>( std::istream & is, IPv4Addr & addr ) noexcept;
};


/// IP address version 6
class IPv6Addr : public GenericAddr<16>
{
	friend class IPAddr;

 public:

	using GenericAddr<16>::GenericAddr;

	IPv6Addr( std::initializer_list< uint8_t > initList )
	{
		priv::copy16FromDynamic( _data, &*initList.begin(), initList.size() );
	}
	IPv6Addr( const_byte_span data )
	{
		priv::copy16FromDynamic( _data, data.data(), data.size() );
	}
	IPv6Addr( fixed_const_byte_span<4> data ) noexcept
	{
		priv::fastCopy16( data.data(), _data );
	}
	IPv6Addr( const IPv6Addr & other ) noexcept : GenericAddr<16>()
	{
		priv::fastCopy16( other.data().data(), _data );
	}

	IPv6Addr & operator=( std::initializer_list< uint8_t > initList )
	{
		priv::copy16FromDynamic( _data, &*initList.begin(), initList.size() );
		return *this;
	}
	IPv6Addr & operator=( const_byte_span data )
	{
		priv::copy16FromDynamic( _data, data.data(), data.size() );
		return *this;
	}
	IPv6Addr & operator=( fixed_const_byte_span<4> data ) noexcept
	{
		priv::fastCopy16( data.data(), _data );
		return *this;
	}
	IPv6Addr & operator=( const IPv6Addr & other ) noexcept
	{
		priv::fastCopy16( other.data().data(), _data );
		return *this;
	}

	friend std::ostream & operator<<( std::ostream & os, const IPv6Addr & addr );
	friend std::istream & operator>>( std::istream & is, IPv6Addr & addr ) noexcept;
};


/// universal container capable of storing both IPv4 and IPv6 address
class IPAddr : public GenericAddr<16>
{
	IPVer _version;

 public:

	using GenericAddr<16>::GenericAddr;

	IPAddr() noexcept : _version( static_cast<IPVer>(0) ) {}

	IPAddr( std::initializer_list< uint8_t > initList );
	IPAddr( const_byte_span data );

	IPAddr( fixed_const_byte_span<4> data ) noexcept
	{
		priv::fastCopy4( data.data(), _data );
		_version = IPVer::_4;
	}
	IPAddr( fixed_const_byte_span<16> data ) noexcept
	{
		priv::fastCopy16( data.data(), _data );
		_version = IPVer::_6;
	}
	IPAddr( const IPv4Addr & addr ) noexcept
	{
		priv::fastCopy4( addr._data, _data );
		_version = IPVer::_4;
	}
	IPAddr( const IPv6Addr & addr ) noexcept
	{
		priv::fastCopy16( addr._data, _data );
		_version = IPVer::_6;
	}

	IPVer version() const noexcept { return _version; }

	IPv4Addr v4() const
	{
		if (_version != IPVer::_4)
			critical_error( "Attempted to convert IPAddr of version %zu to IPv4Addr.", _version );
		return IPv4Addr( make_fixed_span( _data ).shorter<4>() );
	}
	IPv6Addr v6() const
	{
		if (_version != IPVer::_6)
			critical_error( "Attempted to convert IPAddr of version %zu to IPv6Addr.", _version );
		return IPv6Addr( make_fixed_span( _data ) );
	}

	friend std::ostream & operator<<( std::ostream & os, const IPAddr & addr );
	friend std::istream & operator>>( std::istream & is, IPAddr & addr ) noexcept;
};


//======================================================================================================================
/// MAC address

class MACAddr : public GenericAddr<6>
{
 public:

	using GenericAddr<6>::GenericAddr;

	friend std::ostream & operator<<( std::ostream & os, const MACAddr & addr );
	friend std::istream & operator>>( std::istream & is, MACAddr & addr ) noexcept;
};


//======================================================================================================================

struct Endpoint
{
	IPAddr addr;
	uint16_t port;
};

void endpointToSockaddr( const Endpoint & ep, struct sockaddr * saddr, int & addrlen );

bool sockaddrToEndpoint( const struct sockaddr * saddr, Endpoint & ep ) noexcept;


//======================================================================================================================


} // namespace own


#endif // CPPUTILS_NETADDRESS_INCLUDED
