//======================================================================================================================
// Project: OpenRGB - C++ SDK
//----------------------------------------------------------------------------------------------------------------------
// Author:      Jan Broz (Youda008)
// Description: declaration of the protocol messages and types
//======================================================================================================================

#include "ProtocolMessages.hpp"

#include "ProtocolCommon.hpp"

#include <CppUtils-Essential/BinaryStream.hpp>
using own::BinaryOutputStream;
using own::BinaryInputStream;

#include <cstring>  // strncmp
#include <string>
using std::string;
#include <vector>
using std::vector;


namespace orgb {


//======================================================================================================================
//  message header

const char * enumString( MessageType type ) noexcept
{
	// the values of message types are wildly different so we can't use an array
	switch (type)
	{
		case MessageType::REQUEST_CONTROLLER_COUNT:       return "REQUEST_CONTROLLER_COUNT";
		case MessageType::REQUEST_CONTROLLER_DATA:        return "REQUEST_CONTROLLER_DATA";
		case MessageType::REQUEST_PROTOCOL_VERSION:       return "REQUEST_PROTOCOL_VERSION";
		case MessageType::SET_CLIENT_NAME:                return "SET_CLIENT_NAME";
		case MessageType::DEVICE_LIST_UPDATED:            return "DEVICE_LIST_UPDATED";
		case MessageType::REQUEST_PROFILE_LIST:           return "REQUEST_PROFILE_LIST";
		case MessageType::REQUEST_SAVE_PROFILE:           return "REQUEST_SAVE_PROFILE";
		case MessageType::REQUEST_LOAD_PROFILE:           return "REQUEST_LOAD_PROFILE";
		case MessageType::REQUEST_DELETE_PROFILE:         return "REQUEST_DELETE_PROFILE";
		case MessageType::RGBCONTROLLER_RESIZEZONE:       return "RGBCONTROLLER_RESIZEZONE";
		case MessageType::RGBCONTROLLER_UPDATELEDS:       return "RGBCONTROLLER_UPDATELEDS";
		case MessageType::RGBCONTROLLER_UPDATEZONELEDS:   return "RGBCONTROLLER_UPDATEZONELEDS";
		case MessageType::RGBCONTROLLER_UPDATESINGLELED:  return "RGBCONTROLLER_UPDATESINGLELED";
		case MessageType::RGBCONTROLLER_SETCUSTOMMODE:    return "RGBCONTROLLER_SETCUSTOMMODE";
		case MessageType::RGBCONTROLLER_UPDATEMODE:       return "RGBCONTROLLER_UPDATEMODE";
		case MessageType::RGBCONTROLLER_SAVEMODE:         return "RGBCONTROLLER_SAVEMODE";
		default:                                          return "<invalid>";
	}
}

static bool isValidMessageType( MessageType type )
{
	switch (type)
	{
		case MessageType::REQUEST_CONTROLLER_COUNT:       return true;
		case MessageType::REQUEST_CONTROLLER_DATA:        return true;
		case MessageType::REQUEST_PROTOCOL_VERSION:       return true;
		case MessageType::SET_CLIENT_NAME:                return true;
		case MessageType::DEVICE_LIST_UPDATED:            return true;
		case MessageType::REQUEST_PROFILE_LIST:           return true;
		case MessageType::REQUEST_SAVE_PROFILE:           return true;
		case MessageType::REQUEST_LOAD_PROFILE:           return true;
		case MessageType::REQUEST_DELETE_PROFILE:         return true;
		case MessageType::RGBCONTROLLER_RESIZEZONE:       return true;
		case MessageType::RGBCONTROLLER_UPDATELEDS:       return true;
		case MessageType::RGBCONTROLLER_UPDATEZONELEDS:   return true;
		case MessageType::RGBCONTROLLER_UPDATESINGLELED:  return true;
		case MessageType::RGBCONTROLLER_SETCUSTOMMODE:    return true;
		case MessageType::RGBCONTROLLER_UPDATEMODE:       return true;
		case MessageType::RGBCONTROLLER_SAVEMODE:         return true;
		default:                                          return false;
	}
}

void Header::serialize( BinaryOutputStream & stream ) const
{
	stream << magic;
	stream << device_idx;
	stream << message_type;
	stream << message_size;
}

bool Header::deserialize( BinaryInputStream & stream ) noexcept
{
	stream >> magic;
	stream >> device_idx;
	stream >> message_type;
	stream >> message_size;

	if (strncmp( magic, "ORGB", sizeof(magic) ) != 0)
		stream.setFailed();
	if (!isValidMessageType( message_type ))
		stream.setFailed();

	return !stream.failed();
}


//======================================================================================================================
//  main protocol messages

//----------------------------------------------------------------------------------------------------------------------

void ReplyControllerCount::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << count;
}

bool ReplyControllerCount::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> count;

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

void RequestControllerData::serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << protocolVersion;
}

bool RequestControllerData::deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> protocolVersion;

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t ReplyControllerData::calcDataSize( uint32_t protocolVersion ) const noexcept
{
	size_t size = 0;

	size += sizeof( data_size );
	size += device_desc.calcSize( protocolVersion );

	return uint32_t( size );
}

void ReplyControllerData::serialize( BinaryOutputStream & stream, uint32_t protocolVersion ) const
{
	header.serialize( stream );

	stream << data_size;
	device_desc.serialize( stream, protocolVersion );
}

bool ReplyControllerData::deserializeBody( BinaryInputStream & stream, uint32_t protocolVersion ) noexcept
{
	stream >> data_size;
	device_desc.deserialize( stream, protocolVersion, header.device_idx );

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

void RequestProtocolVersion::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << clientVersion;
}

bool RequestProtocolVersion::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> clientVersion;

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

void ReplyProtocolVersion::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << serverVersion;
}

bool ReplyProtocolVersion::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> serverVersion;

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t SetClientName::calcDataSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += name.size() + 1;

	return uint32_t( size );
}

void SetClientName::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream.writeString0( name );
}

bool SetClientName::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream.readString0( name );

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

void ResizeZone::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << zone_idx;
	stream << new_size;
}

bool ResizeZone::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> zone_idx;
	stream >> new_size;

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t UpdateLEDs::calcDataSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += sizeof( data_size );
	size += protocol::sizeofArray( colors );

	return uint32_t( size );
}

void UpdateLEDs::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << data_size;
	protocol::writeArray( stream, colors );
}

bool UpdateLEDs::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> data_size;
	protocol::readArray( stream, colors );

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t UpdateZoneLEDs::calcDataSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += sizeof( data_size );
	size += sizeof( zone_idx );
	size += protocol::sizeofArray( colors );

	return uint32_t( size );
}

void UpdateZoneLEDs::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << data_size;
	stream << zone_idx;
	protocol::writeArray( stream, colors );
}

bool UpdateZoneLEDs::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> data_size;
	stream >> zone_idx;
	protocol::readArray( stream, colors );

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t UpdateSingleLED::calcDataSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += sizeof( led_idx );
	size += color.calcSize();

	return uint32_t( size );
}

void UpdateSingleLED::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << led_idx;
	stream << color;
}

bool UpdateSingleLED::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> led_idx;
	stream >> color;

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t UpdateMode::calcDataSize( uint32_t protocolVersion ) const noexcept
{
	size_t size = 0;

	size += sizeof( data_size );
	size += sizeof( mode_idx );
	size += mode_desc.calcSize( protocolVersion );

	return uint32_t( size );
}

void UpdateMode::serialize( BinaryOutputStream & stream, uint32_t protocolVersion ) const
{
	header.serialize( stream );

	stream << data_size;
	stream << mode_idx;
	mode_desc.serialize( stream, protocolVersion );
}

bool UpdateMode::deserializeBody( BinaryInputStream & stream, uint32_t protocolVersion ) noexcept
{
	stream >> data_size;
	stream >> mode_idx;
	mode_desc.deserialize( stream, mode_idx, header.device_idx, protocolVersion );

	return !stream.failed();
}


//----------------------------------------------------------------------------------------------------------------------

uint32_t SaveMode::calcDataSize( uint32_t protocolVersion ) const noexcept
{
	size_t size = 0;

	size += sizeof( data_size );
	size += sizeof( mode_idx );
	size += mode_desc.calcSize( protocolVersion );

	return uint32_t( size );
}

void SaveMode::serialize( BinaryOutputStream & stream, uint32_t protocolVersion ) const
{
	header.serialize( stream );

	stream << data_size;
	stream << mode_idx;
	mode_desc.serialize( stream, protocolVersion );
}

bool SaveMode::deserializeBody( BinaryInputStream & stream, uint32_t protocolVersion ) noexcept
{
	stream >> data_size;
	stream >> mode_idx;
	mode_desc.deserialize( stream, mode_idx, header.device_idx, protocolVersion );

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t ReplyProfileList::calcDataSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += sizeof( data_size );
	// Unfortunatelly, these strings break the consistency with the rest by not including the '\0' so it must be counted manually.
	size += sizeof( uint16_t );  // num_profiles
	for (const std::string & profile : profiles)
	{
		size += 2 + profile.size();
	}

	return uint32_t( size );
}

void ReplyProfileList::serialize( own::BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream << data_size;
	// Unfortunatelly, these strings break the consistency with the rest by not including the '\0' so it must be written manually.
	stream << uint16_t( profiles.size() );
	for (const auto & profile : profiles)
	{
		stream << uint16_t( profile.size() );
		stream.writeString( profile );
	}
}

bool ReplyProfileList::deserializeBody( own::BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream >> data_size;
	protocol::readArray( stream, profiles );

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t RequestSaveProfile::calcDataSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += profileName.size() + 1;

	return uint32_t( size );
}

void RequestSaveProfile::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream.writeString0( profileName );
}

bool RequestSaveProfile::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream.readString0( profileName );

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t RequestLoadProfile::calcDataSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += profileName.size() + 1;

	return uint32_t( size );
}

void RequestLoadProfile::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream.writeString0( profileName );
}

bool RequestLoadProfile::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream.readString0( profileName );

	return !stream.failed();
}

//----------------------------------------------------------------------------------------------------------------------

uint32_t RequestDeleteProfile::calcDataSize( uint32_t /*protocolVersion*/ ) const noexcept
{
	size_t size = 0;

	size += profileName.size() + 1;

	return uint32_t( size );
}

void RequestDeleteProfile::serialize( BinaryOutputStream & stream, uint32_t /*protocolVersion*/ ) const
{
	header.serialize( stream );

	stream.writeString0( profileName );
}

bool RequestDeleteProfile::deserializeBody( BinaryInputStream & stream, uint32_t /*protocolVersion*/ ) noexcept
{
	stream.readString0( profileName );

	return !stream.failed();
}


//======================================================================================================================


} // namespace orgb
