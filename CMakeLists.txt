if(EXISTS "F:/vcpkg/installed/x64-windows")
    list(APPEND CMAKE_PREFIX_PATH "F:/vcpkg/installed/x64-windows")
endif()
set(VCPKG_USER_DOCS_INSTALLED_PATH "C:/Users/Administrator/Documents/vcpkg/installed/x64-windows") 
if(EXISTS "${VCPKG_USER_DOCS_INSTALLED_PATH}")
    list(APPEND CMAKE_PREFIX_PATH "${VCPKG_USER_DOCS_INSTALLED_PATH}")
endif()
cmake_minimum_required (VERSION 3.12...3.28) # Require 3.12+, adopt policies up to 3.28

# Set policies to suppress warnings and use modern behavior
# CMP0074 (introduced 3.12), CMP0167 (introduced 3.28), and CMP0141 (introduced 3.24)
# are automatically set to NEW by 'cmake_minimum_required' with policy version 3.28.

# The CMAKE_MSVC_DEBUG_INFORMATION_FORMAT variable is used when CMP0141 is NEW.
# This setting controls the debug information format for MSVC.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")

set(DEVELOPER_NAME "Naekkori")
set(GITHUB "https://github.com/Naekkori/OmochaEngine")
project("OmochaEngine" VERSION 1.0.0)

if(MSVC)
    add_compile_options(/utf-8) # MSVC uses /utf-8 for UTF-8 source/execution charsets
    # C++ 예외 처리 모델을 /EHa (비동기 예외 처리)로 설정
    string(REPLACE "/EHsc" "/EHa" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    if(NOT CMAKE_CXX_FLAGS MATCHES "/EHa")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHa")
    endif()
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8) # For GCC/Clang
endif()

if(WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
endif()
set(RESOURCE_RC_FILE ${CMAKE_SOURCE_DIR}/resource.rc)
set(RESOURCE_H_FILE ${CMAKE_SOURCE_DIR}/resource.h)
file(GLOB OMOCHA_ENGINE_SOURCES
    "*.cpp" "*.h"
    "engine/*.cpp" "engine/*.h"
    "engine/blocks/*.cpp" "engine/blocks/*.h"
    "engine/audio/miniaudio.h"
    "util/*.cpp" "util/*.h"
)

# OMOCHA_ENGINE_SOURCES 변수는 MainProgram.cpp (루트 디렉토리에 있다면) 및
# engine/*.cpp, util/*.cpp 등의 모든 필요한 .cpp 파일을 포함해야 합니다.
if(WIN32)
    set(RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc")
    if(EXISTS "${RESOURCE_FILE}")
        message(STATUS "Using resource file: ${RESOURCE_FILE}")
        # OMOCHA_ENGINE_SOURCES에 .cpp 파일들이 이미 포함되어 있다고 가정하고, 리소스 파일을 추가합니다.
        add_executable(OmochaEngine WIN32 ${OMOCHA_ENGINE_SOURCES} "${RESOURCE_FILE}")
    else()
        message(WARNING "Resource file not found: ${RESOURCE_FILE}. Application icon will be missing.")
        add_executable(OmochaEngine WIN32 ${OMOCHA_ENGINE_SOURCES})
    endif()
else()
    add_executable(OmochaEngine ${OMOCHA_ENGINE_SOURCES})
endif()
set_property(TARGET OmochaEngine PROPERTY CXX_STANDARD 20)
find_package(rapidjson CONFIG REQUIRED)
target_link_libraries(OmochaEngine PRIVATE rapidjson)
#SDL
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(OmochaEngine PRIVATE SDL3::SDL3)
find_package(SDL3_image CONFIG REQUIRED)
target_link_libraries(OmochaEngine PRIVATE $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>)
find_package(SDL3_ttf CONFIG REQUIRED)
target_link_libraries(OmochaEngine PRIVATE $<IF:$<TARGET_EXISTS:SDL3_ttf::SDL3_ttf>,SDL3_ttf::SDL3_ttf,SDL3_ttf::SDL3_ttf-static>)
#boost
find_package(Boost REQUIRED)
# config file
configure_file(
    "${PROJECT_SOURCE_DIR}/version_config.h.in" 
    "${PROJECT_BINARY_DIR}/version_config.h"
    @ONLY
)

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
    copy_dll_to_target_dir(OmochaEngine "jpeg62.dll")
    copy_dll_to_target_dir(OmochaEngine "bz2.dll")
    copy_dll_to_target_dir(OmochaEngine "tiff.dll")
    copy_dll_to_target_dir(OmochaEngine "brotlidec.dll")
    copy_dll_to_target_dir(OmochaEngine "brotlicommon.dll")
    copy_dll_to_target_dir(OmochaEngine "freetype.dll")
    copy_dll_to_target_dir(OmochaEngine "libpng16.dll")
    copy_dll_to_target_dir(OmochaEngine "libwebpdemux.dll")
    copy_dll_to_target_dir(OmochaEngine "libwebp.dll")
    copy_dll_to_target_dir(OmochaEngine "liblzma.dll")
    copy_dll_to_target_dir(OmochaEngine "libsharpyuv.dll")
    copy_dll_to_target_dir(OmochaEngine "zlib1.dll")
    #copy_dll_to_target_dir(OmochaEngine "imgui.dll")
    # jsoncpp.dll의 경우, vcpkg가 공유 라이브러리로 빌드했다면 ${VCPKG_BIN_DIR}에 존재합니다.
    # 정적으로 링크된 경우에는 이 DLL이 필요 없으며, 파일이 없다면 복사되지 않습니다.
    #copy_dll_to_target_dir(OmochaEngine "rapidjson.dll")
endif()

# Installation rules for CPack
# -----------------------------------------------------------------------------

# Install the main executable
install(TARGETS OmochaEngine
    RUNTIME DESTINATION . COMPONENT Application
)

# Install the license file
set(LICENSE_FILE_PATH "")
set(LICENSE_FILE_NAME_WITH_EXT "LICENSE.txt")
set(LICENSE_FILE_NAME_NO_EXT "LICENSE")
if(EXISTS "${PROJECT_SOURCE_DIR}/${LICENSE_FILE_NAME_WITH_EXT}")
    set(LICENSE_FILE_PATH "${PROJECT_SOURCE_DIR}/${LICENSE_FILE_NAME_WITH_EXT}")
    set(ACTUAL_LICENSE_FILE_NAME ${LICENSE_FILE_NAME_WITH_EXT})
elseif(EXISTS "${PROJECT_SOURCE_DIR}/${LICENSE_FILE_NAME_NO_EXT}")
    set(LICENSE_FILE_PATH "${PROJECT_SOURCE_DIR}/${LICENSE_FILE_NAME_NO_EXT}")
    set(ACTUAL_LICENSE_FILE_NAME ${LICENSE_FILE_NAME_NO_EXT})
endif()
if(NOT LICENSE_FILE_PATH STREQUAL "")
    install(FILES "${LICENSE_FILE_PATH}" DESTINATION . COMPONENT Application)
    message(STATUS "License file (${ACTUAL_LICENSE_FILE_NAME}) will be packaged.")
else()
    message(WARNING "License file not found (checked for ${LICENSE_FILE_NAME_WITH_EXT} and ${LICENSE_FILE_NAME_NO_EXT}). It will not be packaged.")
endif()

# Install project-specific licenses from LICENSES directory
set(PROJECT_LICENSES_DIR_PATH "${PROJECT_SOURCE_DIR}/LICENSES")

if(IS_DIRECTORY "${PROJECT_LICENSES_DIR_PATH}")
    file(GLOB PROJECT_LICENSE_FILES LIST_DIRECTORIES false "${PROJECT_LICENSES_DIR_PATH}/*")

    if(PROJECT_LICENSE_FILES)
        message(STATUS "Found project licenses in directory: ${PROJECT_LICENSES_DIR_PATH}")
        install(FILES ${PROJECT_LICENSE_FILES}
                DESTINATION "licenses/project" # Installs to <prefix>/licenses/project/
                COMPONENT LicensesProject)
        message(STATUS "Packaging project licenses from ${PROJECT_LICENSES_DIR_PATH} to 'licenses/project/'.")
        foreach(lic_file ${PROJECT_LICENSE_FILES})
            get_filename_component(fname ${lic_file} NAME)
            message(STATUS "  - Queued for packaging: ${fname}")
        endforeach()
    else()
        message(STATUS "Project licenses directory '${PROJECT_LICENSES_DIR_PATH}' is empty. No additional project licenses will be packaged.")
    endif()
else()
    message(STATUS "Project licenses directory not found: ${PROJECT_LICENSES_DIR_PATH}. No additional project licenses will be packaged.")
endif()
# Install fonts
set(FONT_DIR_RELATIVE "font")
set(FONT_DIR_ABSOLUTE "${PROJECT_SOURCE_DIR}/${FONT_DIR_RELATIVE}")

if(EXISTS "${FONT_DIR_ABSOLUTE}")
    # This will install the 'fonts' directory into 'assets' in the package.
    # e.g., package_root/assets/fonts/your_font.ttf
    install(DIRECTORY "${FONT_DIR_ABSOLUTE}/" DESTINATION "${FONT_DIR_RELATIVE}" COMPONENT Assets)
    message(STATUS "Fonts from ${FONT_DIR_ABSOLUTE} will be packaged into ${FONT_DIR_RELATIVE}.")
else()
    message(WARNING "Font directory not found: ${FONT_DIR_ABSOLUTE}. Fonts will not be packaged.")
endif()

# Install sysmenu
set(SYSMENU_DIR_RELATIVE "sysmenu")
set(SYSMENU_DIR_ABSOLUTE "${PROJECT_SOURCE_DIR}/${SYSMENU_DIR_RELATIVE}")

if(EXISTS "${SYSMENU_DIR_ABSOLUTE}")
    # This will install the 'sysmenu' directory into the package root.
    # e.g., package_root/sysmenu/your_file
    install(DIRECTORY "${SYSMENU_DIR_ABSOLUTE}/" DESTINATION "${SYSMENU_DIR_RELATIVE}" COMPONENT Assets)
    message(STATUS "Sysmenu files from ${SYSMENU_DIR_ABSOLUTE} will be packaged into ${SYSMENU_DIR_RELATIVE}.")
else()
    message(WARNING "Sysmenu directory not found: ${SYSMENU_DIR_ABSOLUTE}. Sysmenu files will not be packaged.")
endif()

#install README.md
set(README_FILE_PATH "${PROJECT_SOURCE_DIR}/README.md")
if(EXISTS "${README_FILE_PATH}")
    install(FILES "${README_FILE_PATH}" DESTINATION . COMPONENT Assets)
    message(STATUS "README.md will be packaged.")
else()
    message(WARNING "README.md not found. It will not be packaged.")
endif()

# Install DLLs for Windows
if(WIN32)
    # VCPKG_BIN_DIR should be set from the DLL copying section above.
    # If not, ensure CMAKE_PREFIX_PATH is correctly set and vcpkg integration is working.
    if(NOT DEFINED VCPKG_BIN_DIR)
        if(CMAKE_PREFIX_PATH)
            list(GET CMAKE_PREFIX_PATH 0 VCPKG_INSTALL_TRIPLET_DIR_TEMP)
            # Ensure the fallback path also points to a 'bin' directory
            set(VCPKG_BIN_DIR_TEMP "${VCPKG_INSTALL_TRIPLET_DIR_TEMP}/bin")
            if(IS_DIRECTORY "${VCPKG_BIN_DIR_TEMP}")
                set(VCPKG_BIN_DIR ${VCPKG_BIN_DIR_TEMP})
            endif()
        endif()
    endif()

    if(DEFINED VCPKG_BIN_DIR AND IS_DIRECTORY "${VCPKG_BIN_DIR}")
        set(PROJECT_DLLS
        "SDL3.dll"
        "SDL3_image.dll"
        "SDL3_ttf.dll"
        "jpeg62.dll"
        "bz2.dll"
        "tiff.dll"
        "brotlidec.dll"
        "brotlicommon.dll"
        "freetype.dll"
        "libpng16.dll"
        "libwebpdemux.dll"
        "libwebp.dll"
        "liblzma.dll"
        "libsharpyuv.dll"
        "zlib1.dll"
        )

        foreach(DLL_BASENAME ${PROJECT_DLLS})
            set(DLL_SOURCE_PATH "${VCPKG_BIN_DIR}/${DLL_BASENAME}")
            if(EXISTS "${DLL_SOURCE_PATH}")
                install(FILES "${DLL_SOURCE_PATH}" DESTINATION . COMPONENT RuntimeLibraries)
            else()
                message(WARNING "DLL to install for packaging not found: ${DLL_SOURCE_PATH}")
            endif()
        endforeach()
    else()
        message(WARNING "VCPKG_BIN_DIR is not defined or not a directory. DLLs will not be packaged.")
    endif()
endif()

# Install third-party library licenses (vcpkg, manual, or downloaded)
set(VCPKG_INSTALL_ROOT_FOR_LICENSES "")
if(CMAKE_PREFIX_PATH)
    foreach(prefix_candidate ${CMAKE_PREFIX_PATH})
        if(IS_DIRECTORY "${prefix_candidate}/share") # Heuristic for a vcpkg-like install prefix (for vcpkg licenses)
            set(VCPKG_INSTALL_ROOT_FOR_LICENSES "${prefix_candidate}")
            message(STATUS "Determined vcpkg installation root for licenses: ${VCPKG_INSTALL_ROOT_FOR_LICENSES}")
            break()
        endif()
    endforeach()
endif()

if(VCPKG_INSTALL_ROOT_FOR_LICENSES AND IS_DIRECTORY "${VCPKG_INSTALL_ROOT_FOR_LICENSES}/share")
    message(STATUS "Processing third-party licenses. Vcpkg share directory: ${VCPKG_INSTALL_ROOT_FOR_LICENSES}/share")
    # The following message about project root license is for context, not directly tied to each vcpkg lib.
    if(NOT LICENSE_FILE_PATH STREQUAL "")
        message(STATUS "프로젝트 루트 라이선스 '${ACTUAL_LICENSE_FILE_NAME}'가 발견되었습니다. 개별 의존성 라이선스 패키징을 진행합니다.")
    else()
        message(STATUS "프로젝트 루트 라이선스를 찾지 못했습니다. 개별 vcpkg 라이선스 패키징을 진행합니다.")
    endif()

    set(VCPKG_LIC_PORTS
        "rapidjson"     # From find_package
        "sdl3"          # From find_package
        "sdl3-image"    # From find_package
        "sdl3-ttf"      # From find_package
        "boost"         # Used for asio, license often general
        "bzip2"         # From bz2.dll (Windows) or linked dependency
        "libjpeg-turbo" # From jpeg62.dll (Windows) or linked dependency
        "libtiff"       # From tiff.dll (Windows) or linked dependency
        "liblzma"       # From liblzma.dll (Windows) or linked dependency
        "brotli"        # From brotlidec.dll, brotlicommon.dll (Windows) or linked dependency
        "freetype"      # From freetype.dll (Windows) or linked dependency
        "libpng"        # From libpng16.dll (Windows) or linked dependency
        "libwebp"       # From libwebp.dll, libwebpdemux.dll (Windows) or linked dependency
        "xz-utils"      # From liblzma.dll (Windows) or linked dependency (port name for xz/liblzma)
        "zlib"          # From zlib1.dll (Windows) or linked dependency
        "miniaudio"     # From miniaudio.dll (Windows) or linked dependency
    )

    set(MANUAL_LICENSES_DIR "${PROJECT_SOURCE_DIR}/licenses_manual_vcpkg")
    set(DOWNLOAD_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/downloaded_licenses_staging")
    file(MAKE_DIRECTORY "${DOWNLOAD_STAGING_DIR}") # For storing downloaded files before install

    # --- 사용자 정의: 다운로드할 라이선스 URL ---
    set(LICENSE_URL_boost "https://www.boost.org/LICENSE_1_0.txt")
    set(LICENSE_URL_libtiff "https://gitlab.com/libtiff/libtiff/-/raw/master/LICENSE.md") # 실제 URL 확인 필요
    set(LICENSE_URL_miniaudio "https://raw.githubusercontent.com/mackron/miniaudio/refs/heads/master/LICENSE")
    set(LICENSE_URL_xz_utils "https://git.tukaani.org/?p=xz.git;a=blob_plain;f=COPYING;hb=HEAD")
    # -----------------------------------------

    foreach(PORT_NAME ${VCPKG_LIC_PORTS})
        set(TARGET_LICENSE_FILE_IN_PACKAGE "copyright") # 패키지 내 최종 파일명
        set(DEST_DIR_IN_PACKAGE "licenses/${PORT_NAME}") # 패키지 내 최종 경로

        set(SOURCE_LICENSE_FILE_PATH "") # 설치할 라이선스 파일의 실제 경로
        set(LICENSE_SOURCE_DESCRIPTION "") # 라이선스 출처 설명

        # 1. vcpkg 경로에서 찾기
        set(VCPKG_COPYRIGHT_FILE "${VCPKG_INSTALL_ROOT_FOR_LICENSES}/share/${PORT_NAME}/copyright")
        if(EXISTS "${VCPKG_COPYRIGHT_FILE}")
            set(SOURCE_LICENSE_FILE_PATH "${VCPKG_COPYRIGHT_FILE}")
            set(LICENSE_SOURCE_DESCRIPTION "vcpkg")
        endif()

        # 2. vcpkg에 없으면, 프로젝트 내 수동 추가 폴더에서 찾기
        if(SOURCE_LICENSE_FILE_PATH STREQUAL "")
            set(MANUAL_COPYRIGHT_FILE "${MANUAL_LICENSES_DIR}/${PORT_NAME}/copyright")
            if(EXISTS "${MANUAL_COPYRIGHT_FILE}")
                set(SOURCE_LICENSE_FILE_PATH "${MANUAL_COPYRIGHT_FILE}")
                set(LICENSE_SOURCE_DESCRIPTION "manual project copy from ${MANUAL_LICENSES_DIR}/${PORT_NAME}")
                message(STATUS "Using manual copy for ${PORT_NAME} license: ${MANUAL_COPYRIGHT_FILE}")
            endif()
        endif()

        # 3. 그래도 없으면, 인터넷에서 다운로드 시도
        if(SOURCE_LICENSE_FILE_PATH STREQUAL "")
            set(CURRENT_PORT_LICENSE_URL_VAR "LICENSE_URL_${PORT_NAME}")
            string(REPLACE "-" "_" CURRENT_PORT_LICENSE_URL_VAR ${CURRENT_PORT_LICENSE_URL_VAR}) # 변수명 표준화 (예: libjpeg-turbo -> LICENSE_URL_libjpeg_turbo)

            if(DEFINED ${CURRENT_PORT_LICENSE_URL_VAR})
                set(LICENSE_URL "${${CURRENT_PORT_LICENSE_URL_VAR}}")
                set(DOWNLOADED_FILE_STAGING_PATH "${DOWNLOAD_STAGING_DIR}/${PORT_NAME}_license_downloaded.txt") # 임시 저장 파일명

                message(STATUS "License for ${PORT_NAME} not found locally. Attempting download from: ${LICENSE_URL}")
                file(DOWNLOAD "${LICENSE_URL}" "${DOWNLOADED_FILE_STAGING_PATH}"
                     TIMEOUT 15
                     STATUS download_status
                     LOG download_log SHOW_PROGRESS)

                list(GET download_status 0 error_code)
                if(error_code EQUAL 0 AND EXISTS "${DOWNLOADED_FILE_STAGING_PATH}")
                    message(STATUS "Successfully downloaded license for ${PORT_NAME} to ${DOWNLOADED_FILE_STAGING_PATH}")
                    set(SOURCE_LICENSE_FILE_PATH "${DOWNLOADED_FILE_STAGING_PATH}")
                    set(LICENSE_SOURCE_DESCRIPTION "downloaded")
                else()
                    message(WARNING "Failed to download license for ${PORT_NAME} from ${LICENSE_URL}. Error: ${error_code}. See log for details.")
                    message(DEBUG "Download log for ${PORT_NAME}: ${download_log}")
                endif()
            else()
                message(STATUS "License for ${PORT_NAME} not found (vcpkg, manual) and no download URL (e.g. ${CURRENT_PORT_LICENSE_URL_VAR}) configured.")
            endif()
        endif()

        # 4. 라이선스 파일을 찾았거나 다운로드했다면 설치
        if(NOT SOURCE_LICENSE_FILE_PATH STREQUAL "")
            install(FILES "${SOURCE_LICENSE_FILE_PATH}"
                    DESTINATION "${DEST_DIR_IN_PACKAGE}"
                    RENAME "${TARGET_LICENSE_FILE_IN_PACKAGE}" # 패키지에는 'copyright' 이름으로 설치
                    COMPONENT LicensesVcpkg)
            message(STATUS "Packaging license for ${PORT_NAME} (source: ${LICENSE_SOURCE_DESCRIPTION}) to ${DEST_DIR_IN_PACKAGE}/${TARGET_LICENSE_FILE_IN_PACKAGE}")
        else()
            message(WARNING "Could not find, copy, or download license for ${PORT_NAME}. It will not be packaged.")
        endif()
    endforeach()
else()
    message(STATUS "Could not determine a suitable vcpkg installation directory for licenses from CMAKE_PREFIX_PATH, or its 'share' subdirectory is missing. Vcpkg licenses will not be packaged by this method.")
endif()

# CPack configuration
# -----------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "${DEVELOPER_NAME}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OmochaEngine Game Engine")
set(CPACK_PACKAGE_HOMEPAGE_URL "${GITHUB}")
set(CPACK_PACKAGE_CONTACT "${DEVELOPER_NAME}") # You can use an email here

set(CPACK_COMPONENTS_ALL Application RuntimeLibraries Assets LicensesVcpkg LicensesProject) # Added LicensesVcpkg and LicensesProject

if(WIN32)
    set(CPACK_GENERATOR "ZIP") # Creates a .zip file
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop") # Creates a .dmg file on macOS
elseif(UNIX)
    set(CPACK_GENERATOR "TGZ") # Creates a .tar.gz archive
else()
    set(CPACK_GENERATOR "ZIP") # Default fallback
endif()

include(CPack)
