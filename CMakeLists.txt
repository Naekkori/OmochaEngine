list(APPEND CMAKE_PREFIX_PATH "F:/vcpkg/installed/x64-windows")
cmake_minimum_required (VERSION 3.8)


if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()
set(DEVELOPER_NAME "Maiteil")
set(GITHUB "https://github.com/maiteil/OmochaEngine")
project("OmochaEngine" VERSION 1.0.0) 

set(RESOURCE_RC_FILE ${CMAKE_SOURCE_DIR}/resource.rc)
set(RESOURCE_H_FILE ${CMAKE_SOURCE_DIR}/resource.h)
file(GLOB OMOCHA_ENGINE_SOURCES
    "*.cpp" "*.h"
    "engine/*.cpp" "engine/*.h"
    "engine/blocks/*.cpp" "engine/blocks/*.h"
    "engine/audio/miniaudio.h"
    "util/*.cpp" "util/*.h"
)
add_executable (OmochaEngine ${OMOCHA_ENGINE_SOURCES} ${RESOURCE_RC_FILE})
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET OmochaEngine PROPERTY CXX_STANDARD 20)
endif()
find_package(RapidJSON CONFIG REQUIRED)
target_link_libraries(OmochaEngine PRIVATE RapidJSON rapidjson)
#SDL
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(OmochaEngine PRIVATE SDL3::SDL3)
find_package(SDL3_image CONFIG REQUIRED)
target_link_libraries(OmochaEngine PRIVATE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
find_package(SDL3_ttf CONFIG REQUIRED)
target_link_libraries(OmochaEngine PRIVATE $<IF:$<TARGET_EXISTS:SDL3_ttf::SDL3_ttf>,SDL3_ttf::SDL3_ttf,SDL3_ttf::SDL3_ttf-static>)
configure_file(
    "${PROJECT_SOURCE_DIR}/version_config.h.in" 
    "${PROJECT_BINARY_DIR}/version_config.h"
    @ONLY
)
if(MSVC)
    add_compile_options("/utf-8")
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    # set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /utf-8")
endif()

target_include_directories(OmochaEngine PRIVATE "${PROJECT_BINARY_DIR}")

target_include_directories(OmochaEngine PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    # CMAKE_PREFIX_PATH 리스트의 첫 번째 경로를 vcpkg 설치 경로로 가정합니다.
    # 예: F:/vcpkg/installed/x64-windows
    list(GET CMAKE_PREFIX_PATH 0 VCPKG_INSTALL_TRIPLET_DIR)
    set(VCPKG_BIN_DIR "${VCPKG_INSTALL_TRIPLET_DIR}/bin")

    # 대상 타겟의 출력 디렉터리로 DLL을 복사하는 함수
    function(copy_dll_to_target_dir TARGET_NAME DLL_BASENAME)
        set(DLL_SOURCE_PATH "${VCPKG_BIN_DIR}/${DLL_BASENAME}")
        if(EXISTS "${DLL_SOURCE_PATH}")
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL_SOURCE_PATH}"
                $<TARGET_FILE_DIR:${TARGET_NAME}>
                COMMENT "Copying ${DLL_BASENAME} to output directory for ${TARGET_NAME}"
            )
        else()
            message(WARNING "DLL to copy not found: ${DLL_SOURCE_PATH}")
        endif()
    endfunction()

    # OmochaEngine 타겟을 위해 필요한 DLL들을 복사합니다.
    copy_dll_to_target_dir(OmochaEngine "SDL3.dll")
    copy_dll_to_target_dir(OmochaEngine "SDL3_image.dll")
    copy_dll_to_target_dir(OmochaEngine "SDL3_ttf.dll")
    copy_dll_to_target_dir(OmochaEngine "tiff.dll")
    copy_dll_to_target_dir(OmochaEngine "jpeg62.dll")
    copy_dll_to_target_dir(OmochaEngine "libpng16.dll")
    copy_dll_to_target_dir(OmochaEngine "libwebpdemux.dll")
    copy_dll_to_target_dir(OmochaEngine "libwebp.dll")
    copy_dll_to_target_dir(OmochaEngine "liblzma.dll")
    copy_dll_to_target_dir(OmochaEngine "libsharpyuv.dll")
    # jsoncpp.dll의 경우, vcpkg가 공유 라이브러리로 빌드했다면 ${VCPKG_BIN_DIR}에 존재합니다.
    # 정적으로 링크된 경우에는 이 DLL이 필요 없으며, 파일이 없다면 복사되지 않습니다.
    #copy_dll_to_target_dir(OmochaEngine "rapidjson.dll")
endif()