TARGET = orgbcli

TEMPLATE = app
CONFIG += console
CONFIG += c++11
CONFIG -= app_bundle
CONFIG -= qt
release: CONFIG += static

QMAKE_CXXFLAGS += -Wno-old-style-cast

INCLUDEPATH += ../../include
INCLUDEPATH += ../../external

HEADERS += \
	src/CommandRegistration.hpp \
	src/Commands.hpp

SOURCES += \
	src/CommandRegistration.cpp \
	src/Commands.cpp \
	src/main.cpp

LIBS += -L"../../../build-windows64-release" -lorgbsdk
LIBS += -lcppnet -lcppbase
win32: LIBS += -lws2_32
