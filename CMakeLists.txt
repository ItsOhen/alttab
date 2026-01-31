cmake_minimum_required(VERSION 3.27)

get_filename_component(PROJECT_NAME_FROM_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}"
    NAME
)

set(PROJECT_AUTHOR "Ergon")

project(${PROJECT_NAME_FROM_DIR}
    DESCRIPTION "Alttab carousel preview thing"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")

set(ENABLE_PROTOCOLS_DEFAULT OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/protocols")
  set(ENABLE_PROTOCOLS_DEFAULT ON)
endif()

option(ENABLE_PROTOCOLS "Enable protocol generation and hyprwire support" ${ENABLE_PROTOCOLS_DEFAULT})

if(ENABLE_PROTOCOLS)
  message(STATUS "Protocols enabled")
else()
  message(STATUS "Protocols disabled")
endif()

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

if(ENABLE_PROTOCOLS)
  file(GLOB_RECURSE PROTOCOL_SRC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/protocols/*.cpp"
    )
  list(APPEND SRC ${PROTOCOL_SRC})
endif()

add_library(${PROJECT_NAME} SHARED ${SRC})

find_package(PkgConfig REQUIRED)
pkg_check_modules(hyprland REQUIRED IMPORTED_TARGET hyprland)

if(ENABLE_PROTOCOLS)
  pkg_check_modules(hyprwire REQUIRED IMPORTED_TARGET hyprwire)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE rt PkgConfig::hyprland)

if(ENABLE_PROTOCOLS)
  target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::hyprwire)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PLUGIN_NAME="${PROJECT_NAME}"
    PLUGIN_DESCRIPTION="${PROJECT_DESCRIPTION}"
    PLUGIN_VERSION="${PROJECT_VERSION}"
    PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    PLUGIN_AUTHOR="${PROJECT_AUTHOR}"
    ENABLE_PROTOCOLS=$<BOOL:${ENABLE_PROTOCOLS}>
)

function(hyprprotocol protoPath protoName)
  set(src_path ${CMAKE_CURRENT_SOURCE_DIR}/${protoPath})
  set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/${protoPath})

  file(MAKE_DIRECTORY ${out_dir})

  set(client_cpp ${out_dir}/${protoName}-client.cpp)
  set(client_hpp ${out_dir}/${protoName}-client.hpp)
  set(spec_hpp ${out_dir}/${protoName}-spec.hpp)

  add_custom_command(
        OUTPUT ${client_cpp} ${client_hpp} ${spec_hpp}
        COMMAND hyprwire-scanner --client ${src_path}/${protoName}.xml ${out_dir}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating ${protoName} client sources"
        VERBATIM
    )

  target_sources(${PROJECT_NAME} PRIVATE ${client_cpp} ${client_hpp} ${spec_hpp})
  target_include_directories(${PROJECT_NAME} PRIVATE ${out_dir})
endfunction()

if(ENABLE_PROTOCOLS)
  hyprprotocol(protocols hyprpaper_core)
endif()

file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json")
file(CREATE_LINK
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
    SYMBOLIC
)

message(STATUS "Linked compile_commands.json to project root")

install(TARGETS ${PROJECT_NAME})

