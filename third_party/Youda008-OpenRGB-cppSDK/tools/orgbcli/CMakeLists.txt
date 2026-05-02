add_executable(orgbcli)

file(GLOB SrcFiles CONFIGURE_DEPENDS "src/*.hpp" "src/*.cpp")
target_sources(orgbcli PRIVATE ${SrcFiles})

target_include_directories(orgbcli PRIVATE ${CppEssential_IncludeDirs})

target_link_libraries(orgbcli orgbsdk)

install(TARGETS orgbcli DESTINATION bin)
