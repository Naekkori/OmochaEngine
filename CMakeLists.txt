cmake_minimum_required (VERSION 3.8)


if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()
set(DEVELOPER_NAME "Maiteil")
set(GITHUB "https://github.com/maiteil/OmochaEngine")
project("OmochaEngine" VERSION 1.0.0) 

find_package(jsoncpp CONFIG REQUIRED)
set(DXLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/dxlib/)
set(DXLIB_LIB_DIR ${CMAKE_SOURCE_DIR}/dxlib/)
set(RESOURCE_RC_FILE ${CMAKE_SOURCE_DIR}/resource.rc)
set(RESOURCE_H_FILE ${CMAKE_SOURCE_DIR}/resource.h)
file(GLOB OMOCHA_ENGINE_SOURCES
    "*.cpp" "*.h"
    "engine/*.cpp" "engine/*.h"
    "engine/blocks/*.cpp" "engine/blocks/*.h"
    "util/*.cpp" "util/*.h"
)
add_executable (OmochaEngine ${OMOCHA_ENGINE_SOURCES} ${RESOURCE_RC_FILE})
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET OmochaEngine PROPERTY CXX_STANDARD 20)
endif()
target_link_libraries(OmochaEngine PRIVATE JsonCpp::JsonCpp)
target_link_libraries(OmochaEngine PRIVATE DxLib)

configure_file(
    "${PROJECT_SOURCE_DIR}/version_config.h.in" 
    "${PROJECT_BINARY_DIR}/version_config.h"
    @ONLY
)


target_include_directories(OmochaEngine PRIVATE "${PROJECT_BINARY_DIR}")

target_include_directories(OmochaEngine PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(OmochaEngine PRIVATE ${DXLIB_INCLUDE_DIR})


target_link_directories(OmochaEngine PRIVATE ${DXLIB_LIB_DIR})
