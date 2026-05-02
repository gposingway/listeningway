cmake_minimum_required(VERSION 3.13)

project(OpenRGB-cppSDK)

# the main output of this project
add_library(orgbsdk STATIC)

# add this include directory to everyone who imports this library using target_link_libraries()
target_include_directories(orgbsdk PUBLIC include)

# add all local source files to this project, but not to the project of those who import this library
file(GLOB SrcFiles CONFIGURE_DEPENDS "src/*.hpp" "src/*.cpp")
target_sources(orgbsdk PRIVATE ${SrcFiles})

# get source files and compiler options of these submodules
add_subdirectory(external/CppUtils-Essential)
add_subdirectory(external/CppUtils-Network)

# build cpp utils submodules
if(CPP_UTILS MATCHES "separate")
	# build them as separate libraries - required when multiple static libraries use cpp utils
	message("Building CppUtils as separate static libraries")
	
	add_library(cppbase STATIC)
	target_include_directories(cppbase PUBLIC ${CppEssential_IncludeDirs})
	target_sources(cppbase PRIVATE ${CppEssential_SrcFiles})
	target_compile_definitions(cppbase PUBLIC ${CppEssential_CompDefs})
	target_link_libraries(cppbase ${CppEssential_LinkedLibs})
	
	add_library(cppnet STATIC)
	target_include_directories(cppnet PUBLIC ${CppNetwork_IncludeDirs})
	target_sources(cppnet PRIVATE ${CppNetwork_SrcFiles})
	target_compile_definitions(cppnet PUBLIC ${CppNetwork_CompDefs})
	target_link_libraries(cppnet ${CppNetwork_LinkedLibs})
	
	target_link_libraries(orgbsdk cppnet cppbase)
else()
	# build them as a private part this library (default)
	message("Building CppUtils as a part of this library")
	
	target_include_directories(orgbsdk PRIVATE ${CppEssential_IncludeDirs})
	target_sources(orgbsdk PRIVATE ${CppEssential_SrcFiles})
	target_compile_definitions(orgbsdk PRIVATE ${CppEssential_CompDefs})
	target_link_libraries(orgbsdk ${CppEssential_LinkedLibs})
	
	target_include_directories(orgbsdk PRIVATE ${CppNetwork_IncludeDirs})
	target_sources(orgbsdk PRIVATE ${CppNetwork_SrcFiles})
	target_compile_definitions(orgbsdk PRIVATE ${CppNetwork_CompDefs})
	target_link_libraries(orgbsdk ${CppNetwork_LinkedLibs})
endif()

# add targets from sub-directories
add_subdirectory(tools/orgbcli EXCLUDE_FROM_ALL)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
	# add these defitions to all targets in this file
	add_compile_definitions(DEBUG)
endif()

# generate documentation of Doxygen is present
find_package(Doxygen)
if(DOXYGEN_FOUND)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile ${CMAKE_CURRENT_BINARY_DIR}/doc/Doxyfile)
	add_custom_target(doc
		COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/doc
		COMMENT "Generating API documentation with Doxygen"
	)
endif()

# install the static library file to lib and copy the whole include directory
install(TARGETS orgbsdk DESTINATION lib)
install(DIRECTORY include DESTINATION include)
