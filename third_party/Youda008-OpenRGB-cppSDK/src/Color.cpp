//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: Representation of a color
//======================================================================================================================

#include <OpenRGB/Color.hpp>
#include <CppUtils-Essential/Essential.hpp>

#include "MiscUtils.hpp"

#include <CppUtils-Essential/BinaryStream.hpp>
using own::BinaryOutputStream;
using own::BinaryInputStream;
#include <CppUtils-Essential/StringUtils.hpp>

#include <cstdio>
#include <iostream>
#include <ios>
#include <iomanip>
#include <string>
#include <unordered_map>


namespace orgb {


//======================================================================================================================
//  Color

const Color Color::Black   (0x00, 0x00, 0x00);
const Color Color::White   (0xFF, 0xFF, 0xFF);
const Color Color::Red     (0xFF, 0x00, 0x00);
const Color Color::Green   (0x00, 0xFF, 0x00);
const Color Color::Blue    (0x00, 0x00, 0xFF);
const Color Color::Yellow  (0xFF, 0xFF, 0x00);
const Color Color::Magenta (0xFF, 0x00, 0xFF);
const Color Color::Cyan    (0x00, 0xFF, 0xFF);

static const std::unordered_map< std::string, Color > colorNames =
{
	{ "black", Color::Black },
	{ "white", Color::White },
	{ "red", Color::Red },
	{ "green", Color::Green },
	{ "blue", Color::Blue },
	{ "yellow", Color::Yellow },
	{ "magenta", Color::Magenta },
	{ "cyan", Color::Cyan },
};

bool Color::fromString( const std::string & str ) noexcept
{
	if (str.empty())
		return false;

	uint red, green, blue;
	if (str[0] == '#' && sscanf( str.c_str() + 1, "%02x%02x%02x", &red, &green, &blue ) == 3)
	{
		r = uint8_t( red );
		g = uint8_t( green );
		b = uint8_t( blue );
		return true;
	}
	else if (sscanf( str.c_str(), "%02x%02x%02x", &red, &green, &blue ) == 3)
	{
		r = uint8_t( red );
		g = uint8_t( green );
		b = uint8_t( blue );
		return true;
	}
	else
	{
		std::string strLower = own::to_lower( str );
		auto colorIter = colorNames.find( strLower );
		if (colorIter != colorNames.end())
		{
			*this = colorIter->second;
			return true;
		}
	}
	return false;
}

BinaryOutputStream & operator<<( BinaryOutputStream & stream, Color color )
{
	stream << color.r << color.g << color.b << color.padding;
	return stream;
}

BinaryInputStream & operator>>( BinaryInputStream & stream, Color & color ) noexcept
{
	stream >> color.r >> color.g >> color.b >> color.padding;
	return stream;
}

std::ostream & operator<<( std::ostream & os, Color color ) noexcept
{
	std::ios_base::fmtflags origFlags( os.flags() );
	os << std::uppercase << std::hex << std::setfill('0');
	os << '#' << std::setw(2) << uint(color.r) << std::setw(2) << uint(color.g) << std::setw(2) << uint(color.b);
	os.flags( origFlags );
	return os;
}

std::istream & operator>>( std::istream & is, Color & color ) noexcept
{
	std::string colorStr;
	is >> colorStr;
	if (!color.fromString( colorStr ))
	{
		is.setstate( std::ios::failbit );
	}
	return is;
}

void print( Color color )
{
	printf( "#%02X%02X%02X", uint(color.r), uint(color.g), uint(color.b) );
}


//======================================================================================================================


} // namespace orgb
