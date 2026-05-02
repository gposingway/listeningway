//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: declaration of the protocol messages
//======================================================================================================================
/*

How to add a new protocol message:
----------------------------------

1. Add an element to enum MessageType with the correct code and update the enumString(...) and isValidMessageType(...)

2. Create a struct from the following template.
struct NewMessage
{
	Header header;
	... type specific fields ...

 // support for templated processing

	static constexpr MessageType thisType = MessageType::NEW_MESSAGE_TYPE;

	NewMessage() noexcept {}
	NewMessage( uint32_t deviceIdx, ... type specific values ... )
	:
		header(
			/ *message_type* / thisType,
			/ *device_idx* /   deviceIdx,
			/ *message_size* / 0
		),
		... type specific initialization ...
	{
		// If the message size is static, you can make calcDataSize constexpr
		// and move this to the header initializer above.
		header.message_size = calcDataSize();
	}

	uint32_t calcDataSize( uint32_t protocolVersion ) const noexcept;
	void serialize( own::BufferOutputStream & stream, uint32_t protocolVersion ) const;
	bool deserializeBody( own::BufferInputStream & stream, uint32_t protocolVersion ) noexcept;
};

3. Implement calcDataSize(), serialize(...) and deserializeBody(...) in the cpp file.
   If the implementation is really trivial, it can be inline in the header.


How to extend an existing message:
----------------------------------

1. Add the new members to the existing message struct.

2. If required, add this member to the constructor params and to the initialization list.

3. Extend the implementation of calcDataSize(), serialize(...) and deserializeBody(...) to count with the new member.

4. Increment the implementedProtocolVersion constant.

4. Edit the protocol_description.txt to mirror these changes.

*/

#ifndef OPENRGB_PROTOCOL_MESSAGES_INCLUDED
#define OPENRGB_PROTOCOL_MESSAGES_INCLUDED


#include <CppUtils-Essential/Essential.hpp>

#include <OpenRGB/DeviceInfo.hpp>
#include <OpenRGB/Color.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace own {
	class BinaryOutputStream;
	class BinaryInputStream;
}


namespace orgb {


/// version of the protocol this client currently implements
/** The following structs and enums are defined according to this version of the protocol.
  * Older versions will not be supported, sorry guys. */
constexpr unsigned int implementedProtocolVersion = 3;


//======================================================================================================================
//  message header

/// a copy of enum in https://gitlab.com/CalcProgrammer1/OpenRGB/-/blob/master/NetworkProtocol.h
enum class MessageType : uint32_t
{
	REQUEST_CONTROLLER_COUNT       = 0,
	REQUEST_CONTROLLER_DATA        = 1,
	REQUEST_PROTOCOL_VERSION       = 40,
	SET_CLIENT_NAME                = 50,
	DEVICE_LIST_UPDATED            = 100,
	REQUEST_PROFILE_LIST           = 150,
	REQUEST_SAVE_PROFILE           = 151,
	REQUEST_LOAD_PROFILE           = 152,
	REQUEST_DELETE_PROFILE         = 153,
	RGBCONTROLLER_RESIZEZONE       = 1000,
	RGBCONTROLLER_UPDATELEDS       = 1050,
	RGBCONTROLLER_UPDATEZONELEDS   = 1051,
	RGBCONTROLLER_UPDATESINGLELED  = 1052,
	RGBCONTROLLER_SETCUSTOMMODE    = 1100,
	RGBCONTROLLER_UPDATEMODE       = 1101,
	RGBCONTROLLER_SAVEMODE         = 1102,
};
const char * enumString( MessageType ) noexcept;

/// Every protocol message starts with this.
struct Header
{
	char         magic [4];  ///< must always be set to ORGB in all messages
	uint32_t     device_idx;
	MessageType  message_type;
	uint32_t     message_size;  ///< size of message minus size of this header

	Header() noexcept {}
	Header( MessageType messageType, uint32_t deviceIdx ) noexcept
		: magic{'O','R','G','B'}, device_idx( deviceIdx ), message_type( messageType ) {}
	Header( MessageType messageType, uint32_t deviceIdx, uint32_t messageSize ) noexcept
		: magic{'O','R','G','B'}, device_idx( deviceIdx ), message_type( messageType ), message_size( messageSize ) {}

	static constexpr size_t size() noexcept { return sizeof(Header); }  // all members are equally big, no padding will take place

	void serialize( own::BinaryOutputStream & stream ) const;
	bool deserialize( own::BinaryInputStream & stream ) noexcept;
};


//======================================================================================================================
//  main protocol messages

/// Asks server how many RGB devices (controllers) there are.
struct RequestControllerCount
{
	Header header;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_CONTROLLER_COUNT;

	RequestControllerCount()
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0,
			/*message_size*/ calcDataSize()
		)
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return 0;
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const
	{
		header.serialize( stream );
	}
	bool deserializeBody( own::BinaryInputStream & /*stream*/, uint32_t /*protocolVersion*/ = 0 ) noexcept
	{
		return true;
	}
};

/// A reply to RequestControllerCount
struct ReplyControllerCount
{
	Header header;
	uint32_t count;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_CONTROLLER_COUNT;

	ReplyControllerCount() noexcept {}
	ReplyControllerCount( uint32_t count )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0,
			/*message_size*/ calcDataSize()
		),
		count( count )
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return sizeof( count );
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// Asks for all information and supported modes about a specific RGB device (controller).
struct RequestControllerData
{
	Header header;
	uint32_t protocolVersion;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_CONTROLLER_DATA;

	RequestControllerData() noexcept {}
	RequestControllerData( uint32_t deviceIdx, uint32_t protocolVersion )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx,
			/*message_size*/ calcDataSize()
		),
		protocolVersion( protocolVersion )
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return sizeof( protocolVersion );
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// A reply to RequestControllerData
struct ReplyControllerData
{
	Header header;
	uint32_t  data_size;  ///< must always be same as header.message_size, no idea why it's there twice
	Device    device_desc;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_CONTROLLER_DATA;

	ReplyControllerData() noexcept {}
	ReplyControllerData( uint32_t deviceIdx, const Device & device, uint32_t protocolVersion )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx
		),
		device_desc( device )
	{
		header.message_size = data_size = calcDataSize( protocolVersion );
	}

	uint32_t calcDataSize( uint32_t protocolVersion ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t protocolVersion ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t protocolVersion ) noexcept;
};

/// Tells the server in what version of the protocol the client wants to communite in.
struct RequestProtocolVersion
{
	Header header;
	uint32_t clientVersion;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_PROTOCOL_VERSION;

	RequestProtocolVersion( uint32_t clientVersion )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0,
			/*message_size*/ calcDataSize()
		),
		clientVersion( clientVersion )
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return sizeof( clientVersion );
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// A reply to RequestProtocolVersion. Contains the maximum version the server supports.
struct ReplyProtocolVersion
{
	Header header;
	uint32_t serverVersion;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_PROTOCOL_VERSION;

	ReplyProtocolVersion() noexcept {}
	ReplyProtocolVersion( uint32_t serverVersion )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0,
			/*message_size*/ calcDataSize()
		),
		serverVersion( serverVersion )
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return sizeof( serverVersion );
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// Announces a custom name of the client to the server.
struct SetClientName
{
	Header header;
	std::string name;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::SET_CLIENT_NAME;

	SetClientName() noexcept {}
	SetClientName( const std::string & name )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0
		),
		name( name )
	{
		header.message_size = calcDataSize();
	}

	uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// This is sent from the server everytime its device list has changed.
struct DeviceListUpdated
{
	Header header;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::DEVICE_LIST_UPDATED;

	DeviceListUpdated()
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0,
			/*message_size*/ calcDataSize()
		)
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return 0;
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const
	{
		header.serialize( stream );
	}
	bool deserializeBody( own::BinaryInputStream & /*stream*/, uint32_t /*protocolVersion*/ = 0 ) noexcept
	{
		return true;
	}
};

/// Resizes a zone of LEDs, if the device supports it.
struct ResizeZone
{
	Header header;
	uint32_t zone_idx;
	uint32_t new_size;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::RGBCONTROLLER_RESIZEZONE;

	ResizeZone() noexcept {}
	ResizeZone( uint32_t deviceIdx, uint32_t zoneIdx, uint32_t newSize )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx,
			/*message_size*/ calcDataSize()
		),
		zone_idx( zoneIdx ),
		new_size( newSize )
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return sizeof( zone_idx ) + sizeof( new_size );
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// Applies individually selected color to every LED.
struct UpdateLEDs
{
	Header  header;
	uint32_t  data_size;
	std::vector< Color >  colors;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::RGBCONTROLLER_UPDATELEDS;

	UpdateLEDs() noexcept {}
	UpdateLEDs( uint32_t deviceIdx, const std::vector< Color > & colors )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx
		),
		colors( colors )
	{
		header.message_size = data_size = calcDataSize();
	}

	uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// Applies individually selected color to every LED in a specific zone.
struct UpdateZoneLEDs
{
	Header  header;
	uint32_t  data_size;
	uint32_t  zone_idx;
	std::vector< Color >  colors;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::RGBCONTROLLER_UPDATEZONELEDS;

	UpdateZoneLEDs() noexcept {}
	UpdateZoneLEDs( uint32_t deviceIdx, uint32_t zoneIdx, const std::vector< Color > & colors )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx
		),
		zone_idx( zoneIdx ),
		colors( colors )
	{
		header.message_size = data_size = calcDataSize();
	}

	uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// Changes color of a single particular LED.
struct UpdateSingleLED
{
	Header  header;
	uint32_t  led_idx;
	Color     color;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::RGBCONTROLLER_UPDATESINGLELED;

	UpdateSingleLED() noexcept {}
	UpdateSingleLED( uint32_t deviceIdx, uint32_t ledIdx, Color color )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx,
			/*message_size*/ calcDataSize()
		),
		led_idx( ledIdx ),
		color( color )
	{}

	uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// Switches mode of a device to a directly controlled one.
struct SetCustomMode
{
	Header  header;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::RGBCONTROLLER_SETCUSTOMMODE;

	SetCustomMode() noexcept {}
	SetCustomMode( uint32_t deviceIdx )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx,
			/*message_size*/ calcDataSize()
		)
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return 0;
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const
	{
		header.serialize( stream );
	}
	bool deserializeBody( own::BinaryInputStream & /*stream*/, uint32_t /*protocolVersion*/ = 0 ) noexcept
	{
		return true;
	}
};

/// Updates the parameters of a mode and also switches the device to this mode.
struct UpdateMode
{
	Header  header;
	uint32_t  data_size;
	uint32_t  mode_idx;
	Mode      mode_desc;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::RGBCONTROLLER_UPDATEMODE;

	UpdateMode() noexcept {}
	UpdateMode( uint32_t deviceIdx, uint32_t modeIdx, const Mode & mode, uint32_t protocolVersion )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx
		),
		mode_idx( modeIdx ),
		mode_desc( mode )
	{
		header.message_size = data_size = calcDataSize( protocolVersion );
	}

	uint32_t calcDataSize( uint32_t protocolVersion ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t protocolVersion ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t protocolVersion ) noexcept;
};

/// Saves the mode parameters into the device memory to make it persistent.
struct SaveMode
{
	Header  header;
	uint32_t  data_size;
	uint32_t  mode_idx;
	Mode      mode_desc;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::RGBCONTROLLER_SAVEMODE;

	SaveMode() noexcept {}
	SaveMode( uint32_t deviceIdx, uint32_t modeIdx, const Mode & mode, uint32_t protocolVersion )
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   deviceIdx
		),
		mode_idx( modeIdx ),
		mode_desc( mode )
	{
		header.message_size = data_size = calcDataSize( protocolVersion );
	}

	uint32_t calcDataSize( uint32_t protocolVersion ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t protocolVersion ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t protocolVersion ) noexcept;
};

/// Asks for a list of saved profiles.
struct RequestProfileList
{
	Header  header;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_PROFILE_LIST;

	RequestProfileList() noexcept
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0,
			/*message_size*/ calcDataSize()
		)
	{}

	constexpr uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept
	{
		return 0;
	}
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const
	{
		header.serialize( stream );
	}
	bool deserializeBody( own::BinaryInputStream & /*stream*/, uint32_t /*protocolVersion*/ = 0 ) noexcept
	{
		return true;
	}
};

/// A reply to RequestProfileList
struct ReplyProfileList
{
	Header  header;
	uint32_t  data_size;
	std::vector< std::string > profiles;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_PROFILE_LIST;

	ReplyProfileList() noexcept {}
	ReplyProfileList( const std::vector< std::string > & profiles ) noexcept
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0
		),
		profiles( profiles )
	{
		header.message_size = calcDataSize();
	}

	uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};


/// Saves the current configuration of all devices under a new profile name.
struct RequestSaveProfile
{
	Header  header;
	std::string profileName;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_SAVE_PROFILE;

	RequestSaveProfile( const std::string & profileName ) noexcept
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0
		),
		profileName( profileName )
	{
		header.message_size = calcDataSize();
	}

	uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// Applies an existing profile.
struct RequestLoadProfile
{
	Header  header;
	std::string profileName;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_LOAD_PROFILE;

	RequestLoadProfile( const std::string & profileName ) noexcept
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0
		),
		profileName( profileName )
	{
		header.message_size = calcDataSize();
	}

	uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};

/// Removes an existing profile.
struct RequestDeleteProfile
{
	Header  header;
	std::string profileName;

 // support for templated processing

	static constexpr MessageType thisType = MessageType::REQUEST_DELETE_PROFILE;

	RequestDeleteProfile( const std::string & profileName ) noexcept
	:
		header(
			/*message_type*/ thisType,
			/*device_idx*/   0
		),
		profileName( profileName )
	{
		header.message_size = calcDataSize();
	}

	uint32_t calcDataSize( uint32_t /*protocolVersion*/ = 0 ) const noexcept;
	void serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ = 0 ) const;
	bool deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ = 0 ) noexcept;
};


//======================================================================================================================


} // namespace orgb


#endif // OPENRGB_PROTOCOL_MESSAGES_INCLUDED
