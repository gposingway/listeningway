//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: data structures containing device information
//======================================================================================================================

#ifndef OPENRGB_DEVICE_INCLUDED
#define OPENRGB_DEVICE_INCLUDED


#include "Exceptions.hpp"

#include "Color.hpp"

#include <string>
#include <vector>
#include <memory>  // unique_ptr<Device>


namespace orgb {


//======================================================================================================================
//  enums

/// Type of device with RGB LEDs
enum class DeviceType : uint32_t
{
	Motherboard   = 0,
	DRAM          = 1,
	GPU           = 2,
	Cooler        = 3,
	LedStrip      = 4,
	Keyboard      = 5,
	Mouse         = 6,
	MouseMat      = 7,
	Headset       = 8,
	HeadsetStand  = 9,
	Gamepad       = 10,
	Light         = 11,
    Speaker       = 12,
    Virtual       = 13,
	Unknown,
};
const char * enumString( DeviceType ) noexcept;

/// Which features the mode supports
enum ModeFlags : uint32_t
{
	HasSpeed              = (1 << 0),  ///< the speed attribute in ModeDescription is present
	HasDirectionLR        = (1 << 1),  ///< the direction attribute in ModeDescription can have Left or Right values
	HasDirectionUD        = (1 << 2),  ///< the direction attribute in ModeDescription can have Up or Down values
	HasDirectionHV        = (1 << 3),  ///< the direction attribute in ModeDescription can have Horizontal or Vertical values
	HasBrightness         = (1 << 4),  ///< the brightness attribute in ModeDescription is present
	HasPerLedColor        = (1 << 5),  ///< the color_mode attribute in ModeDescription can be set to PerLed
	HasModeSpecificColor  = (1 << 6),  ///< the color_mode attribute in ModeDescription can be set to ModeSpecific
	HasRandomColor        = (1 << 7),  ///< the color_mode attribute in ModeDescription can be set to Random
};
std::string modeFlagsToString( uint32_t flags );

/// Direction of the color effect
enum class Direction : uint32_t
{
	Left        = 0,
	Right       = 1,
	Up          = 2,
	Down        = 3,
	Horizontal  = 4,
	Vertical    = 5,
};
const char * enumString( Direction ) noexcept;

/// How the colors of a mode are set
enum class ColorMode : uint32_t
{
	None          = 0,  ///< mode has no colors
	PerLed        = 1,  ///< mode has per LED colors
	ModeSpecific  = 2,  ///< mode specific colors
	Random        = 3,  ///< mode has random colors
};
const char * enumString( ColorMode ) noexcept;

/// Type of RGB zone
enum class ZoneType : uint32_t
{
	Single  = 0,
	Linear  = 1,
	Matrix  = 2,
};
const char * enumString( ZoneType ) noexcept;


//======================================================================================================================
/// Represents a particular LED on an RGB device.

class LED
{

 public:

	// metadata
	const uint32_t idx;        ///< index of this LED in the device's list of LEDs
	const uint32_t parentIdx;  ///< index of the parent device in the device list

	// LED description
	const std::string  name;
	const uint32_t     value;  ///< device-specific value

 private:  // for internal use only

	friend struct protocol;
	LED();
	size_t calcSize( uint32_t protocolVersion ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t protocolVersion ) const;
	bool deserialize( own::BinaryInputStream & stream, uint32_t protocolVersion, uint32_t idx, uint32_t parentIdx ) noexcept;

};


//======================================================================================================================
/// Represents a group of LEDs on an RGB device. Only some devices have zones.

class Zone
{

 public:

	// metadata
	const uint32_t idx;        ///< index of this zone in the device's list of zones
	const uint32_t parentIdx;  ///< index of the parent device in the device list

	// zone description
	const std::string  name;
	const ZoneType     type;
	const uint32_t     leds_min;       ///< minimum size of the zone
	const uint32_t     leds_max;       ///< maximum size of the zone
	const uint32_t     leds_count;     ///< current size of the zone
	// optional
	const uint32_t     matrix_height;  ///< if the zone type is matrix, this is its height
	const uint32_t     matrix_width;   ///< if the zone type is matrix, this is its width
	const std::vector< uint32_t >  matrix_values;  ///< TODO: what is this?

 private:  // for internal use only

	friend struct protocol;
	Zone();
	size_t calcSize( uint32_t protocolVersion ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t protocolVersion ) const;
	bool deserialize( own::BinaryInputStream & stream, uint32_t protocolVersion, uint32_t idx, uint32_t parentIdx ) noexcept;

};


//======================================================================================================================
/// Represents a color mode of an RGB device, like "breathing", "flashing", "rainbow" or "direct".

class Mode
{

 public:

	// metadata
	const uint32_t idx;        ///< index of this mode in the device's list of modes
	const uint32_t parentIdx;  ///< index of the parent device in the device list

	// Attributes that are not marked with const are the mode parameters that can be set,
	// others are just informational and need to remain constant.
	const std::string  name;
	const uint32_t     value;           ///< device-specific value
	const uint32_t     flags;           ///< see ModeFlags for possible bit flags
	const uint32_t     speed_min;       ///< minimum speed value, this attribute is valid only if ModeFlags::HasSpeed is set, otherwise it's uninitialized
	const uint32_t     speed_max;       ///< maximum speed value, this attribute is valid only if ModeFlags::HasSpeed is set, otherwise it's uninitialized
	const uint32_t     brightness_min;  ///< minimum brightness value, this attribute is valid only if ModeFlags::HasBrightness is set, otherwise it's uninitialized
	const uint32_t     brightness_max;  ///< maximum brightness value, this attribute is valid only if ModeFlags::HasBrightness is set, otherwise it's uninitialized
	const uint32_t     colors_min;      ///< minimum number of mode colors
	const uint32_t     colors_max;      ///< maximum number of mode colors
	/// Speed of the effect.
	/** This attribute is enabled only if ModeFlags::HasSpeed is set, otherwise it's uninitialized.
	  * The possible values are determined by #speed_min and #speed_max. */
	      uint32_t      speed;
	/// Brightness of the lights.
	/** This attribute is enabled only if ModeFlags::HasBrightness is set, otherwise it's uninitialized.
	  * The possible values are determined by #brightness_min and #brightness_max. */
	      uint32_t      brightness;
	/// Direction of the color effect.
	/** This attribute is enabled only if any of ModeFlags::HasDirectionXY is set in #flags, otherwise it's uninitialized.
	  * The possible values are also determined by #flags. */
	      Direction     direction;
	const ColorMode     color_mode;  ///< how the colors of a mode are set
	/// Mode-specific list of colors.
	      std::vector< Color >  colors;

 private:  // for internal use only

	friend struct protocol;
	friend class Device;
	friend struct UpdateMode;
	friend struct SaveMode;
	Mode();
	size_t calcSize( uint32_t protocolVersion ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t protocolVersion ) const;
	bool deserialize( own::BinaryInputStream & stream, uint32_t protocolVersion, uint32_t idx, uint32_t parentIdx ) noexcept;

};


//======================================================================================================================
/// Represents an RGB-capable device. Device can have modes, zones and individual LEDs.

class Device
{

 public:

	// metadata
	const uint32_t idx;  ///< index of this device in the device list

	// device description
	const DeviceType   type;
	const std::string  name;
	const std::string  vendor;
	const std::string  description;
	const std::string  version;
	const std::string  serial;
	const std::string  location;
	const uint32_t     active_mode;

	// device subobjects
	const std::vector< Mode > modes;
	const std::vector< Zone > zones;
	const std::vector< LED >  leds;
	const std::vector< Color > colors;

 public:

	/// Finds the first mode with a specific name.
	/** \returns nullptr when mode with this name is not found. */
	const Mode * findMode( const std::string & name ) const noexcept
	{
		for (const auto & mode : modes)
			if (mode.name == name)
				return &mode;
		return nullptr;
	}

	/// Finds the first zone with a specific name.
	/** \returns nullptr when zone with this name is not found. */
	const Zone * findZone( const std::string & name ) const noexcept
	{
		for (const auto & zone : zones)
			if (zone.name == name)
				return &zone;
		return nullptr;
	}

	/// Finds the first LED with a specific name.
	/** \returns nullptr when LED with this name is not found. */
	const LED * findLED( const std::string & name ) const noexcept
	{
		for (const auto & led : leds)
			if (led.name == name)
				return &led;
		return nullptr;
	}

#ifndef NO_EXCEPTIONS

	/// Exception-throwing variant of findModeX( const std::string & ) const.
	/** \throws NotFound when mode with this name is not found. */
	const Mode & findModeX( const std::string & name ) const
	{
		for (const auto & mode : modes)
			if (mode.name == name)
				return mode;
		throw NotFound( "Mode of such name was not found" );
	}

	/// Exception-throwing variant of findZoneX( const std::string & ) const.
	/** \throws NotFound when zone with this name is not found. */
	const Zone & findZoneX( const std::string & name ) const
	{
		for (const auto & zone : zones)
			if (zone.name == name)
				return zone;
		throw NotFound( "Zone of such name was not found" );
	}

	/// Exception-throwing variant of findLEDX( const std::string & ) const.
	/** \throws NotFound when LED with this name is not found. */
	const LED & findLEDX( const std::string & name ) const
	{
		for (const auto & led : leds)
			if (led.name == name)
				return led;
		throw NotFound( "LED of such name was not found" );
	}

#endif // NO_EXCEPTIONS

 private:  // for internal use only

	friend struct ReplyControllerData;
	size_t calcSize( uint32_t protocolVersion ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t protocolVersion ) const;
	bool deserialize( own::BinaryInputStream & stream, uint32_t protocolVersion, uint32_t deviceIdx ) noexcept;

	friend class DeviceList;
	friend class Client;
	Device();
	Device( const Device & other ) = default;
	Device( Device && other ) = default;

};


//======================================================================================================================
/** Convenience wrapper around iterator to container of pointers
  * that skips the additional needed dereference and returns a reference directly. */

template< typename IterType >
class PointerIterator
{
	IterType _origIter;
 public:
	PointerIterator( const IterType & origIter ) : _origIter( origIter ) {}
	auto operator*() -> decltype( **_origIter ) const { return **_origIter; }
	auto operator->() -> decltype( *_origIter ) const { return *_origIter; }
	PointerIterator & operator++() { ++_origIter; return *this; }
	PointerIterator operator++(int) { auto tmp = *this; ++_origIter; return tmp; }
	friend bool operator==(const PointerIterator & a, const PointerIterator & b) { return a._origIter == b._origIter; }
	friend bool operator!=(const PointerIterator & a, const PointerIterator & b) { return a._origIter != b._origIter; }
};


//======================================================================================================================
/// Searchable list of all RGB-capable devices detected by OpenRGB.

class DeviceList
{

	// Pointers are more practical here, because they are faster to move and we can easily wrap them arround in the API
	using DeviceListType = std::vector< std::unique_ptr< Device > >;
	DeviceListType _list;

 public:

	size_t size() const noexcept { return _list.size(); }

	/// Use this if you intend to populate the DeviceList manually using individual calls to Client::requestDeviceInfo().
	void append( std::unique_ptr< Device > && device )  { _list.push_back( std::move(device) ); }

	/// Use this to update your DeviceList after the call to Client::requestDeviceInfo().
	void replace( uint32_t deviceIdx, std::unique_ptr< Device > && device )  { _list[ deviceIdx ] = std::move(device); }

	void clear() noexcept  { _list.clear(); }

	PointerIterator< DeviceListType::const_iterator > begin() const noexcept  { return _list.begin(); }
	PointerIterator< DeviceListType::const_iterator > end() const noexcept    { return _list.end(); }

	const Device & operator[]( uint32_t deviceIdx ) const noexcept { return *_list[ deviceIdx ]; }

	/// Iterate over all devices of specific type.
	template< typename FuncType >
	void forEach( DeviceType deviceType, FuncType loopBody ) const
	{
		for (const Device & device : *this)
			if (device.type == deviceType)
				loopBody( device );
	}

	/// Iterate over all devices of specific vendor.
	template< typename FuncType >
	void forEach( const std::string & vendor, FuncType loopBody ) const
	{
		for (const Device & device : *this)
			if (device.vendor == vendor)
				loopBody( device );
	}

	/// Finds the first device of specific type.
	/** \returns nullptr when device of this type is not found */
	const Device * find( DeviceType deviceType ) const noexcept
	{
		for (const Device & device : *this)
			if (device.type == deviceType)
				return &device;
		return nullptr;
	}

	/// Finds the first device with a specific name.
	/** \returns nullptr when device with this name is not found */
	const Device * find( const std::string & deviceName ) const noexcept
	{
		for (const Device & device : *this)
			if (device.name == deviceName)
				return &device;
		return nullptr;
	}

#ifndef NO_EXCEPTIONS

	/// Exception-throwing variant of find( DeviceType ) const
	/** \throws NotFound when device of this type is not found */
	const Device & findX( DeviceType deviceType ) const
	{
		for (const Device & device : *this)
			if (device.type == deviceType)
				return device;
		throw NotFound( "Device of such type was not found" );
	}

	/// Exception-throwing variant of find( const std::string & ) const.
	/** \throws NotFound when device with this name is not found */
	const Device & findX( const std::string & deviceName ) const
	{
		for (const Device & device : *this)
			if (device.name == deviceName)
				return device;
		throw NotFound( "Device of such name was not found" );
	}

#endif // NO_EXCEPTIONS

 private:  // for internal use only

	// this should only be used by the Client when constructing the list from the server response
	friend class Client;
	void reserve( size_t newSize )   { _list.reserve( newSize ); }
	void append( Device && device )  { _list.emplace_back( new Device( std::move(device) ) ); }

};


//======================================================================================================================
//  printing utils

void print( const Device & device, unsigned int indentLevel = 0 );
void print( const Mode & mode, unsigned int indentLevel = 0 );
void print( const Zone & zone, unsigned int indentLevel = 0 );
void print( const LED & led, unsigned int indentLevel = 0 );

void print( std::ostream & os, const Device & device, unsigned int indentLevel = 0 );
void print( std::ostream & os, const Mode & mode, unsigned int indentLevel = 0 );
void print( std::ostream & os, const Zone & zone, unsigned int indentLevel = 0 );
void print( std::ostream & os, const LED & led, unsigned int indentLevel = 0 );


//======================================================================================================================


} // namespace orgb


#endif // OPENRGB_DEVICE_INCLUDED
