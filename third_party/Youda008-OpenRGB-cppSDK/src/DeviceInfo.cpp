//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: data structures containing device information
//======================================================================================================================

#include <OpenRGB/DeviceInfo.hpp>
#include <CppUtils-Essential/Essential.hpp>

#include "ProtocolCommon.hpp"
#include "MiscUtils.hpp"

#include <CppUtils-Essential/BinaryStream.hpp>
using own::BinaryOutputStream;
using own::BinaryInputStream;
#include <CppUtils-Essential/LangUtils.hpp>
using own::unconst;

#include <string>
using std::string;
#include <sstream>
using std::ostringstream;  // flags to string


namespace orgb {


//======================================================================================================================
//  enum strings

const char * enumString( DeviceType type ) noexcept
{
	static const char * const DeviceTypeStr [] =
	{
		"Motherboard",
		"DRAM",
		"GPU",
		"Cooler",
		"LedStrip",
		"Keyboard",
		"Mouse",
		"MouseMat",
		"Headset",
		"HeadsetStand",
		"Gamepad",
		"Light",
		"Speaker",
		"Virtual",
		"Unknown",
	};
	static_assert( size_t(DeviceType::Unknown) + 1 == fut::size(DeviceTypeStr), "update the DeviceTypeStr" );

	// Collapse everything else than known classes to Unknown
	// in case the server adds some classes without increasing protocol version.
	if (size_t( type ) < size_t( DeviceType::Unknown ))
		return DeviceTypeStr[ size_t( type ) ];
	else
		return DeviceTypeStr[ size_t( DeviceType::Unknown ) ];
}

string modeFlagsToString( uint32_t flags )
{
	ostringstream oss;

	bool isFirst = true;
	auto addFlag = [ &isFirst, &oss ]( const char * flagStr )
	{
		if (isFirst) {
			isFirst = false;
		} else {
			oss << " | ";
		}
		oss << flagStr;
	};

	if (flags & ModeFlags::HasSpeed)
		addFlag( "HasSpeed" );
	if (flags & ModeFlags::HasDirectionLR)
		addFlag( "HasDirectionLR" );
	if (flags & ModeFlags::HasDirectionUD)
		addFlag( "HasDirectionUD" );
	if (flags & ModeFlags::HasDirectionHV)
		addFlag( "HasDirectionHV" );
	if (flags & ModeFlags::HasBrightness)
		addFlag( "HasBrightness" );
	if (flags & ModeFlags::HasPerLedColor)
		addFlag( "HasPerLedColor" );
	if (flags & ModeFlags::HasModeSpecificColor)
		addFlag( "HasModeSpecificColor" );
	if (flags & ModeFlags::HasRandomColor)
		addFlag( "HasRandomColor" );

	return oss.str();
}

const char * enumString( Direction dir ) noexcept
{
	static const char * const DirectionStr [] =
	{
		"Left",
		"Right",
		"Up",
		"Down",
		"Horizontal",
		"Vertical",
	};
	static_assert( size_t(Direction::Vertical) + 1 == fut::size(DirectionStr), "update the DirectionStr" );

	if (size_t( dir ) <= size_t( Direction::Vertical ))
		return DirectionStr[ size_t( dir ) ];
	else
		return "<invalid>";
}

const char * enumString( ColorMode mode ) noexcept
{
	static const char * const ColorModeStr [] =
	{
		"None",
		"PerLed",
		"ModeSpecific",
		"Random",
	};
	static_assert( size_t(ColorMode::Random) + 1 == fut::size(ColorModeStr), "update the ColorModeStr" );

	if (size_t( mode ) <= size_t( ColorMode::Random ) )
		return ColorModeStr[ size_t( mode ) ];
	else
		return "<invalid>";
}

const char * enumString( ZoneType type ) noexcept
{
	static const char * const ZoneTypeStr [] =
	{
		"Single",
		"Linear",
		"Matrix",
	};
	static_assert( size_t(ZoneType::Matrix) + 1 == fut::size(ZoneTypeStr), "update the ZoneTypeStr" );

	if (size_t( type ) <= size_t( ZoneType::Matrix ))
		return ZoneTypeStr[ size_t( type ) ];
	else
		return "<invalid>";
}


//======================================================================================================================
//  enum validation

/*static bool isValidDeviceType( DeviceType type )
{
	return size_t( type ) <= size_t( DeviceType::Unknown );
}*/

static bool isValidDirection( Direction dir, uint32_t modeFlags )
{
	bool allowedDirections [ size_t( Direction::Vertical ) + 1 ] = {0};
	bool hasAnyDirections = false;

	if (modeFlags & ModeFlags::HasDirectionLR)
	{
		hasAnyDirections = true;
		allowedDirections[ size_t( Direction::Left ) ] = true;
		allowedDirections[ size_t( Direction::Right ) ] = true;
	}
	if (modeFlags & ModeFlags::HasDirectionUD)
	{
		hasAnyDirections = true;
		allowedDirections[ size_t( Direction::Up ) ] = true;
		allowedDirections[ size_t( Direction::Down ) ] = true;
	}
	if (modeFlags & ModeFlags::HasDirectionHV)
	{
		hasAnyDirections = true;
		allowedDirections[ size_t( Direction::Horizontal ) ] = true;
		allowedDirections[ size_t( Direction::Vertical ) ] = true;
	}

	// in case no direction flag is active, direction will be uninitialized value, so it can be anything
	if (!hasAnyDirections)
	{
		return true;
	}

	return allowedDirections[ size_t( dir ) ] == true;
}

static bool isValidColorMode( ColorMode mode )
{
	return size_t( mode ) <= size_t( ColorMode::Random );
}

static bool isValidZoneType( ZoneType type )
{
	return size_t( type ) <= size_t( ZoneType::Matrix );
}


//======================================================================================================================
//  LED

LED::LED()
:
	idx(),
	parentIdx(),
	name(),
	value()
{}

size_t LED::calcSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += protocol::sizeofString( name );
	size += sizeof( value );

	return size;
}

void LED::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	protocol::writeString( stream, name );
	stream << value;
}

bool LED::deserialize( BinaryInputStream & stream, uint32_t /*protocolVersion*/, uint32_t idx, uint32_t parentIdx ) noexcept
{
	// This hack with const casts allows us to restrict the user from changing attributes that are a static description
	// and allow him to change only the parameters that are meant to be changed.

	// fill in our metadata
	unconst( this->idx ) = idx;
	unconst( this->parentIdx ) = parentIdx;

	protocol::readString( stream, unconst( name ) );
	stream >> unconst( value );

	return !stream.failed();
}

void print( const LED & led, unsigned int indentLevel )
{
	indent( indentLevel ); printf( "[%u] = { name = \"%s\"; value = %u },\n", led.idx, led.name.c_str(), led.value );
}

void print( std::ostream & os, const LED & led, unsigned int indentLevel )
{
	indent( os, indentLevel ); os << "["<<led.idx<<"] = { name = \""<<led.name<<"\"; value = "<<led.value<<" },\n";
}


//======================================================================================================================
//  Zone

Zone::Zone()
:
	idx(),
	parentIdx(),
	name(),
	type(),
	leds_min(),
	leds_max(),
	leds_count(),
	matrix_height(),
	matrix_width(),
	matrix_values()
{}

size_t Zone::calcSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += protocol::sizeofString( name );
	size += sizeof( type );
	size += sizeof( leds_min );
	size += sizeof( leds_max );
	size += sizeof( leds_count );

	size += sizeof( uint16_t );  // length of the optional matrix block
	if (matrix_values.size() > 0)
	{
		size += sizeof( matrix_height );
		size += sizeof( matrix_width );
		size += own::sizeofVector( matrix_values );
	}

	return size;
}

void Zone::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	protocol::writeString( stream, name );
	stream << type;
	stream << leds_min;
	stream << leds_max;
	stream << leds_count;

	uint16_t matrix_length = uint16_t(
		matrix_values.size() > 0
			? sizeof( matrix_height ) + sizeof( matrix_width ) + own::sizeofVector( matrix_values )
			: 0
	);
	stream << matrix_length;  // length of the optional matrix block
	if (matrix_length > 0)
	{
		stream << matrix_height;
		stream << matrix_width;
		for (auto val : matrix_values)
		{
			stream << val;
		}
	}
}

bool Zone::deserialize( BinaryInputStream & stream, uint32_t /*protocolVersion*/, uint32_t idx, uint32_t parentIdx ) noexcept
{
	// This hack with const casts allows us to restrict the user from changing attributes that are a static description
	// and allow him to change only the parameters that are meant to be changed.

	// fill in our metadata
	unconst( this->idx ) = idx;
	unconst( this->parentIdx ) = parentIdx;

	protocol::readString( stream, unconst( name ) );
	stream >> unconst( type );
	stream >> unconst( leds_min );
	stream >> unconst( leds_max );
	stream >> unconst( leds_count );

	uint16_t matrix_length;
	stream >> matrix_length;
	if (matrix_length > 0)
	{
		stream >> unconst( matrix_height );
		stream >> unconst( matrix_width );
		size_t matrixSize = matrix_height * matrix_width;
		unconst( matrix_values ).resize( matrixSize );
		for (size_t i = 0; i < matrixSize; ++i)
		{
			stream >> unconst( matrix_values )[i];
		}
	}

	if (!isValidZoneType( type ))
		stream.setFailed();

	return !stream.failed();
}

void print( const Zone & zone, unsigned int indentLevel )
{
	indent( indentLevel ); printf( "[%u] = {\n", zone.idx );
	indent( indentLevel + 1 ); printf( "name = \"%s\";\n", zone.name.c_str() );
	indent( indentLevel + 1 ); printf( "type = %s;\n", enumString( zone.type ) );
	indent( indentLevel + 1 ); printf( "leds_min = %u;\n", zone.leds_min );
	indent( indentLevel + 1 ); printf( "leds_max = %u;\n", zone.leds_max );
	indent( indentLevel + 1 ); printf( "leds_count = %u;\n", zone.leds_count );
	indent( indentLevel + 1 ); printf( "matrix_height = %u;\n", zone.matrix_height );
	indent( indentLevel + 1 ); printf( "matrix_width = %u;\n", zone.matrix_width );
	indent( indentLevel ); printf( "},\n" );
}

void print( std::ostream & os, const Zone & zone, unsigned int indentLevel )
{
	indent( os, indentLevel ); os << "["<<zone.idx<<"] = {\n";
	indent( os, indentLevel + 1 ); os << "name = \""<<zone.name.c_str()<<"\";\n";
	indent( os, indentLevel + 1 ); os << "type = "<<enumString( zone.type )<<";\n";
	indent( os, indentLevel + 1 ); os << "leds_min = "<<zone.leds_min<<";\n";
	indent( os, indentLevel + 1 ); os << "leds_max = "<<zone.leds_max<<";\n";
	indent( os, indentLevel + 1 ); os << "leds_count = "<<zone.leds_count<<";\n";
	indent( os, indentLevel + 1 ); os << "matrix_height = "<<zone.matrix_height<<";\n";
	indent( os, indentLevel + 1 ); os << "matrix_width = "<<zone.matrix_width<<";\n";
	indent( os, indentLevel ); os << "},\n";
}


//======================================================================================================================
//  Mode

Mode::Mode()
:
	idx(),
	parentIdx(),
	name(),
	value(),
	flags(),
	speed_min(),
	speed_max(),
	brightness_min(),
	brightness_max(),
	colors_min(),
	colors_max(),
	speed(),
	brightness(),
	direction(),
	color_mode(),
	colors()
{}

size_t Mode::calcSize( uint32_t protocolVersion ) const noexcept
{
	size_t size = 0;

	size += protocol::sizeofString( name );
	size += sizeof( value );
	size += sizeof( flags );
	size += sizeof( speed_min );
	size += sizeof( speed_max );
	if (protocolVersion >= 3)
	{
		size += sizeof( brightness_min );
		size += sizeof( brightness_max );
	}
	size += sizeof( colors_min );
	size += sizeof( colors_max );
	size += sizeof( speed );
	if (protocolVersion >= 3)
	{
		size += sizeof( brightness );
	}
	size += sizeof( direction );
	size += sizeof( color_mode );
	size += protocol::sizeofArray( colors );

	return size;
}

void Mode::serialize( BinaryOutputStream & stream, uint32_t protocolVersion ) const
{
	protocol::writeString( stream, name );
	stream << value;
	stream << flags;
	stream << speed_min;
	stream << speed_max;
	if (protocolVersion >= 3)
	{
		stream << brightness_min;
		stream << brightness_max;
	}
	stream << colors_min;
	stream << colors_max;
	stream << speed;
	if (protocolVersion >= 3)
	{
		stream << brightness;
	}
	stream << direction;
	stream << color_mode;
	protocol::writeArray( stream, colors );
}

bool Mode::deserialize( BinaryInputStream & stream, uint32_t protocolVersion, uint32_t idx, uint32_t parentIdx ) noexcept
{
	// This hack with const casts allows us to restrict the user from changing attributes that are a static description
	// and allow him to change only the parameters that are meant to be changed.

	// fill in our metadata
	unconst( this->idx ) = idx;
	unconst( this->parentIdx ) = parentIdx;

	protocol::readString( stream, unconst( name ) );
	stream >> unconst( value );
	stream >> unconst( flags );
	stream >> unconst( speed_min );
	stream >> unconst( speed_max );
	if (protocolVersion >= 3)
	{
		stream >> unconst( brightness_min );
		stream >> unconst( brightness_max );
	}
	stream >> unconst( colors_min );
	stream >> unconst( colors_max );
	stream >> speed;
	if (protocolVersion >= 3)
	{
		stream >> unconst( brightness );
	}
	stream >> direction;
	stream >> unconst( color_mode );
	protocol::readArray( stream, colors );

	if (!isValidDirection( direction, flags ))
		stream.setFailed();
	if (!isValidColorMode( color_mode ))
		stream.setFailed();

	return !stream.failed();
}

void print( const Mode & mode, unsigned int indentLevel )
{
	indent( indentLevel ); printf( "[%u] = {\n", mode.idx );
	indent( indentLevel + 1 ); printf( "name = \"%s\";\n", mode.name.c_str() );
	indent( indentLevel + 1 ); printf( "value = %u;\n", mode.value );
	indent( indentLevel + 1 ); printf( "flags = %s;\n", modeFlagsToString( mode.flags ).c_str() );
	indent( indentLevel + 1 ); printf( "direction = %s;\n", enumString( mode.direction ) );
	indent( indentLevel + 1 ); printf( "speed_min = %u;\n", mode.speed_min );
	indent( indentLevel + 1 ); printf( "speed_max = %u;\n", mode.speed_max );
	indent( indentLevel + 1 ); printf( "speed = %u;\n", mode.speed );
	indent( indentLevel + 1 ); printf( "brightness_min = %u;\n", mode.brightness_min );
	indent( indentLevel + 1 ); printf( "brightness_max = %u;\n", mode.brightness_max );
	indent( indentLevel + 1 ); printf( "brightness = %u;\n", mode.brightness );
	indent( indentLevel + 1 ); printf( "colors_min = %u;\n", mode.colors_min );
	indent( indentLevel + 1 ); printf( "colors_max = %u;\n", mode.colors_max );
	indent( indentLevel + 1 ); printf( "color_mode = %s;\n", enumString( mode.color_mode ) );
	indent( indentLevel + 1 ); printf( "colors = {\n" );
	for (Color color : mode.colors)
	{
		indent( indentLevel + 2 ); print( color ); puts(",");
	}
	indent( indentLevel + 1 ); printf( "};\n" );
	indent( indentLevel ); printf( "},\n" );
}

void print( std::ostream & os, const Mode & mode, unsigned int indentLevel )
{
	indent( os, indentLevel ); os << "["<<mode.idx<<"] = {\n";
	indent( os, indentLevel + 1 ); os << "name = \""<<mode.name<<"\";\n";
	indent( os, indentLevel + 1 ); os << "value = "<<mode.value<<";\n";
	indent( os, indentLevel + 1 ); os << "flags = "<<modeFlagsToString( mode.flags )<<";\n";
	indent( os, indentLevel + 1 ); os << "direction = "<<enumString( mode.direction )<<";\n";
	indent( os, indentLevel + 1 ); os << "speed_min = "<<mode.speed_min<<";\n";
	indent( os, indentLevel + 1 ); os << "speed_max = "<<mode.speed_max<<";\n";
	indent( os, indentLevel + 1 ); os << "speed = "<<mode.speed<<";\n";
	indent( os, indentLevel + 1 ); os << "brightness_min = "<<mode.brightness_min<<";\n";
	indent( os, indentLevel + 1 ); os << "brightness_max = "<<mode.brightness_max<<";\n";
	indent( os, indentLevel + 1 ); os << "brightness = "<<mode.brightness<<";\n";
	indent( os, indentLevel + 1 ); os << "colors_min = "<<mode.colors_min<<";\n";
	indent( os, indentLevel + 1 ); os << "colors_max = "<<mode.colors_max<<";\n";
	indent( os, indentLevel + 1 ); os << "color_mode = "<<enumString( mode.color_mode )<<";\n";
	indent( os, indentLevel + 1 ); os << "colors = {\n";
	for (Color color : mode.colors)
	{
		indent( os, indentLevel + 2 ); os << color << ",\n";
	}
	indent( os, indentLevel + 1 ); os << "};\n";
	indent( os, indentLevel ); os << "},\n";
}


//======================================================================================================================
//  Device

Device::Device()
:
	idx(),
	type(),
	name(),
	vendor(),
	description(),
	version(),
	serial(),
	location(),
	active_mode(),
	modes(),
	zones(),
	leds(),
	colors()
{}

size_t Device::calcSize( uint32_t protocolVersion ) const noexcept
{
	size_t size = 0;

	size += sizeof( type );
	size += protocol::sizeofString( name );
	size += protocol::sizeofString( vendor );
	size += protocol::sizeofString( description );
	size += protocol::sizeofString( version );
	size += protocol::sizeofString( serial );
	size += protocol::sizeofString( location );

	size += sizeof( active_mode );
	size += protocol::sizeofArray( modes, protocolVersion );
	size += protocol::sizeofArray( zones, protocolVersion );
	size += protocol::sizeofArray( leds, protocolVersion );
	size += protocol::sizeofArray( colors );

	return size;
}

void Device::serialize( BinaryOutputStream & stream, uint32_t protocolVersion ) const
{
	stream << type;
	protocol::writeString( stream, name );
	protocol::writeString( stream, vendor );
	protocol::writeString( stream, description );
	protocol::writeString( stream, version );
	protocol::writeString( stream, serial );
	protocol::writeString( stream, location );

	stream << uint16_t( modes.size() );  // the size is not directly before the array, so it must be written manually
	stream << active_mode;
	for (const Mode & mode : modes)
	{
		mode.serialize( stream, protocolVersion );
	}
	protocol::writeArray( stream, zones, protocolVersion );
	protocol::writeArray( stream, leds, protocolVersion );
	protocol::writeArray( stream, colors );
}

bool Device::deserialize( BinaryInputStream & stream, uint32_t protocolVersion, uint32_t deviceIdx ) noexcept
{
	// This hack with const casts allows us to restrict the user from changing attributes that are a static description
	// and allow him to change only the parameters that are meant to be changed.

	// fill in our metadata
	unconst( idx ) = deviceIdx;

	stream >> unconst( type );
	protocol::readString( stream, unconst( name ) );
	protocol::readString( stream, unconst( vendor ) );
	protocol::readString( stream, unconst( description ) );
	protocol::readString( stream, unconst( version ) );
	protocol::readString( stream, unconst( serial ) );
	protocol::readString( stream, unconst( location ) );

	uint16_t num_modes;
	stream >> num_modes;  // the size is not directly before the array, so it must be read manually
	stream >> unconst( active_mode );
	unconst( modes ).reserve( num_modes );
	for (uint32_t modeIdx = 0; modeIdx < num_modes; ++modeIdx)
	{
		Mode mode;
		if (!mode.deserialize( stream, protocolVersion, modeIdx, deviceIdx ))
			return false;
		unconst( modes ).emplace_back( move(mode) );
	}
	protocol::readArray( stream, unconst( zones ), protocolVersion, deviceIdx );
	protocol::readArray( stream, unconst( leds ), protocolVersion, deviceIdx );
	protocol::readArray( stream, unconst( colors ) );

	// Let's tolerate invalid device classes in case the server adds some without increasing protocol version
	//if (!isValidDeviceType( type ))
	//	stream.setFailed();

	return !stream.failed();
}

void print( const Device & device, unsigned int indentLevel )
{
	indent( indentLevel ); printf( "[%u] = {\n", device.idx );
	indent( indentLevel + 1 ); printf( "name = \"%s\";\n", device.name.c_str() );
	indent( indentLevel + 1 ); printf( "type = %s;\n", enumString( device.type ) );
	indent( indentLevel + 1 ); printf( "vendor = \"%s\";\n", device.vendor.c_str() );
	indent( indentLevel + 1 ); printf( "description = \"%s\";\n", device.description.c_str() );
	indent( indentLevel + 1 ); printf( "version = \"%s\";\n", device.version.c_str() );
	indent( indentLevel + 1 ); printf( "serial = \"%s\";\n", device.serial.c_str() );
	indent( indentLevel + 1 ); printf( "location = \"%s\";\n", device.location.c_str() );
	indent( indentLevel + 1 ); printf( "active_mode = %u;\n", device.active_mode );
	indent( indentLevel + 1 ); printf( "modes = {\n" );
	for (const Mode & mode : device.modes)
	{
		print( mode, indentLevel + 2 );
	}
	indent( indentLevel + 1 ); printf( "};\n" );
	indent( indentLevel + 1 ); printf( "zones = {\n" );
	for (const Zone & zone : device.zones)
	{
		print( zone, indentLevel + 2 );
	}
	indent( indentLevel + 1 ); printf( "};\n" );
	indent( indentLevel + 1 ); printf( "leds = {\n" );
	for (const LED & led : device.leds)
	{
		print( led, indentLevel + 2 );
	}
	indent( indentLevel + 1 ); printf( "};\n" );
	indent( indentLevel + 1 ); printf( "colors = {\n" );
	for (Color color : device.colors)
	{
		indent( indentLevel + 2 ); print( color ); puts(",");
	}
	indent( indentLevel + 1 ); printf( "};\n" );
	indent( indentLevel ); printf( "},\n" );
}

void print( std::ostream & os, const Device & device, unsigned int indentLevel )
{
	indent( os, indentLevel ); os << "["<<device.idx<<"] = {\n";
	indent( os, indentLevel + 1 ); os << "name = \""<<device.name<<"\";\n";
	indent( os, indentLevel + 1 ); os << "type = "<<enumString( device.type )<<";\n";
	indent( os, indentLevel + 1 ); os << "vendor = \""<<device.vendor<<"\";\n";
	indent( os, indentLevel + 1 ); os << "description = \""<<device.description<<"\";\n";
	indent( os, indentLevel + 1 ); os << "version = \""<<device.version<<"\";\n";
	indent( os, indentLevel + 1 ); os << "serial = \""<<device.serial<<"\";\n";
	indent( os, indentLevel + 1 ); os << "location = \""<<device.location<<"\";\n";
	indent( os, indentLevel + 1 ); os << "active_mode = "<<device.active_mode<<";\n";
	indent( os, indentLevel + 1 ); os << "modes = {\n";
	for (const Mode & mode : device.modes)
	{
		print( os, mode, indentLevel + 2 );
	}
	indent( os, indentLevel + 1 ); os << "};\n";
	indent( os, indentLevel + 1 ); os << "zones = {\n";
	for (const Zone & zone : device.zones)
	{
		print( os, zone, indentLevel + 2 );
	}
	indent( os, indentLevel + 1 ); os << "};\n";
	indent( os, indentLevel + 1 ); os << "leds = {\n";
	for (const LED & led : device.leds)
	{
		print( os, led, indentLevel + 2 );
	}
	indent( os, indentLevel + 1 ); os << "};\n";
	indent( os, indentLevel + 1 ); os << "colors = {\n";
	for (Color color : device.colors)
	{
		indent( os, indentLevel + 2 ); os << color << ",\n";
	}
	indent( os, indentLevel + 1 ); os << "};\n";
	indent( os, indentLevel ); os << "},\n";
}


//======================================================================================================================


} // namespace orgb
