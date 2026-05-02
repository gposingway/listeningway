TARGET = orgbsdk-debug

TEMPLATE = app
CONFIG += console
CONFIG += c++11
CONFIG -= app_bundle
CONFIG -= qt

QMAKE_CXXFLAGS += -Wno-old-style-cast -Wno-comment

INCLUDEPATH += include
INCLUDEPATH += external

HEADERS += \
	external/CppUtils-Essential/Assert.hpp \
	external/CppUtils-Essential/BinaryStream.hpp \
	external/CppUtils-Essential/ContainerUtils.hpp \
	external/CppUtils-Essential/CriticalError.hpp \
	external/CppUtils-Essential/Endianity.hpp \
	external/CppUtils-Essential/Essential.hpp \
	external/CppUtils-Essential/LangUtils.hpp \
	external/CppUtils-Essential/MathUtils.hpp \
	external/CppUtils-Essential/Safety.hpp \
	external/CppUtils-Essential/Span.hpp \
	external/CppUtils-Essential/StreamUtils.hpp \
	external/CppUtils-Essential/StringUtils.hpp \
	external/CppUtils-Essential/TypeTraits.hpp \
	external/CppUtils-Network/HostInfo.hpp \
	external/CppUtils-Network/NetAddress.hpp \
	external/CppUtils-Network/Socket.hpp \
	external/CppUtils-Network/SystemErrorInfo.hpp \
	include/OpenRGB/Client.hpp \
	include/OpenRGB/Color.hpp \
	include/OpenRGB/DeviceInfo.hpp \
	include/OpenRGB/Exceptions.hpp \
	include/OpenRGB/SystemErrorType.hpp \
	src/MiscUtils.hpp \
	src/ProtocolCommon.hpp \
	src/ProtocolMessages.hpp

SOURCES += \
	external/CppUtils-Essential/BinaryStream.cpp \
	external/CppUtils-Essential/ContainerUtils.cpp \
	external/CppUtils-Essential/CriticalError.cpp \
	external/CppUtils-Essential/LangUtils.cpp \
	external/CppUtils-Essential/StreamUtils.cpp \
	external/CppUtils-Essential/StringUtils.cpp \
	external/CppUtils-Network/HostInfo.cpp \
	external/CppUtils-Network/NetAddress.cpp \
	external/CppUtils-Network/Socket.cpp \
	external/CppUtils-Network/SystemErrorInfo.cpp \
	src/Client.cpp \
	src/Color.cpp \
	src/DeviceInfo.cpp \
	src/Exceptions.cpp \
	src/MiscUtils.cpp \
	src/ProtocolCommon.cpp \
	src/ProtocolMessages.cpp \
	src/test/main.cpp

DISTFILES += \
	protocol_description.txt

debug {
	DEFINES += DEBUG
}
release {
	DEFINES += CRITICALS_CATCHABLE
}

win32: LIBS += -lws2_32
