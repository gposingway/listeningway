# return a list of source files or compiler options/definitions to the parent project

set(CppEssential_IncludeDirs "${CMAKE_CURRENT_SOURCE_DIR}/.." PARENT_SCOPE)

file(GLOB LocalSrcFiles CONFIGURE_DEPENDS "*.hpp" "*.cpp")
set(CppEssential_SrcFiles ${LocalSrcFiles} PARENT_SCOPE)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
	set(CppEssential_CompDefs DEBUG PARENT_SCOPE)
else()
	set(CppEssential_CompDefs CRITICALS_CATCHABLE PARENT_SCOPE)
endif()

set(CppEssential_LinkedLibs "" PARENT_SCOPE)
