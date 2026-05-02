# return a list of source files or compiler options/definitions to the parent project

set(CppNetwork_IncludeDirs "${CMAKE_CURRENT_SOURCE_DIR}/.." PARENT_SCOPE)

file(GLOB LocalSrcFiles CONFIGURE_DEPENDS "*.hpp" "*.cpp")
set(CppNetwork_SrcFiles ${LocalSrcFiles} PARENT_SCOPE)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
	set(CppNetwork_CompDefs DEBUG PARENT_SCOPE)
else()
	set(CppNetwork_CompDefs CRITICALS_CATCHABLE PARENT_SCOPE)
endif()

if(WIN32)
	set(CppNetwork_LinkedLibs ws2_32 PARENT_SCOPE)
else()
	set(CppNetwork_LinkedLibs "" PARENT_SCOPE)
endif()
