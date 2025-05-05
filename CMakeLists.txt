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

file(GLOB OMOCHA_ENGINE_SOURCES
    "*.cpp" "*.h"
    "engine/*.cpp" "engine/*.h"
)
add_executable (OmochaEngine ${OMOCHA_ENGINE_SOURCES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET OmochaEngine PROPERTY CXX_STANDARD 20)
endif()
target_link_libraries(OmochaEngine PRIVATE JsonCpp::JsonCpp)

# DxLib 라이브러리 디렉토리 추가
target_link_directories(OmochaEngine PRIVATE ${DXLIB_LIB_DIR})

# DxLib 관련 64비트 라이브러리 연결 (디버그 MDd 버전 기준)
# !! DxLib 버전에 따라 실제 파일 이름이 다를 수 있으니 확인 후 수정하세요 !!
target_link_libraries(OmochaEngine PRIVATE
    DxLib_vs2015_x64_MDd          # DxLib 메인 라이브러리
    DxUseCLib_vs2015_x64_MDd      # DxLib C 라이브러리 사용 헬퍼
    libbulletdynamics_vs2015_x64_MDd # Bullet Physics Dynamics
    libbulletcollision_vs2015_x64_MDd # Bullet Physics Collision
    libbulletmath_vs2015_x64_MDd     # Bullet Physics Math
    libpng_vs2015_x64_MDd         # PNG 라이브러리
    libjpeg_vs2015_x64_MDd        # JPEG 라이브러리
    libtiff_vs2015_x64_MDd        # TIFF 라이브러리
    zlib_vs2015_x64_MDd           # Zlib 라이브러리
    vorbis_static_vs2015_x64_MDd  # Vorbis 오디오 (static)
    vorbisfile_static_vs2015_x64_MDd # Vorbis File 오디오 (static)
    ogg_static_vs2015_x64_MDd     # Ogg 컨테이너 (static)
    libtheora_static_vs2015_x64_MDd # Theora 비디오 (static)
    opus_vs2015_x64_MDd           # Opus 오디오
    opusfile_vs2015_x64_MDd       # Opus File 오디오
    celt_vs2015_x64_MDd           # Celt 오디오 (Opus 일부)
    silk_common_vs2015_x64_MDd    # Silk 오디오 (Opus 일부)
    # 필요한 표준 Windows 라이브러리 (CMake가 자동으로 찾지 못할 경우)
    winmm.lib
    imm32.lib
    version.lib # 버전 정보 리소스 사용 시 필요할 수 있음
    # Advapi32.lib # RegCloseKey 관련 오류 시 필요할 수 있음
)

configure_file(
    "${PROJECT_SOURCE_DIR}/version_config.h.in"
    "${PROJECT_BINARY_DIR}/version_config.h"
    @ONLY
)


target_include_directories(OmochaEngine PRIVATE "${PROJECT_BINARY_DIR}")


target_include_directories(OmochaEngine PRIVATE ${DXLIB_INCLUDE_DIR})


# --- 패키징 설정 추가 ---

# 1. 패키지 정보 설정
set(PACKAGE_VERSION ${PROJECT_VERSION}) # 프로젝트 버전 사용
set(PACKAGE_NAME "${PROJECT_NAME}-${PACKAGE_VERSION}-win64") # 최종 패키지 파일 이름 (예: OmochaEngine-1.0-win64)
set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/package_temp") # 파일들을 모을 임시 디렉토리
set(PACKAGE_FINAL_ARCHIVE "${CMAKE_BINARY_DIR}/${PACKAGE_NAME}.zip") # 최종 압축 파일 경로

# 2. 실행 파일 타겟 이름 (add_executable에서 사용한 이름)
set(EXECUTABLE_TARGET_NAME OmochaEngine)

# 3. 패키지에 포함할 에셋 폴더 (소스 디렉토리 기준)
set(SYSMENU_SOURCE_DIR "${CMAKE_SOURCE_DIR}/sysmenu")
# 수정: 실제 폰트 폴더 이름으로 변경 (예: font)
set(FONTS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/font") # <-- 실제 폴더 이름으로 변경하세요
#   (project.json이 assets 폴더 밖에 있다면 별도로 복사 로직 추가)
# set(PROJECT_JSON_SOURCE "${CMAKE_SOURCE_DIR}/project.json")

# 4. 패키지에 포함할 DLL 파일 목록 (경로는 시스템/라이브러리 설치 위치에 따라 다름)
#    DxLib DLL, Visual C++ Redistributable DLL 등을 찾아야 합니다.
#    find_file() 또는 절대 경로를 사용할 수 있습니다.
# set(DXLIB_DLL_PATH "C:/path/to/DxLib.dll") # 예시 경로
# set(MSVC_REDIST_DLL_PATH "C:/path/to/vcruntime140.dll") # 예시 경로

# 5. 패키징 커스텀 타겟 정의 (7-Zip 사용)
find_program(SEVEN_ZIP_EXECUTABLE 7z)
if(SEVEN_ZIP_EXECUTABLE) # 7-Zip 찾았는지 확인
    add_custom_target(create_package
        DEPENDS ${EXECUTABLE_TARGET_NAME}
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${PACKAGE_FINAL_ARCHIVE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${EXECUTABLE_TARGET_NAME}>" "${PACKAGE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SYSMENU_SOURCE_DIR}" "${PACKAGE_DIR}/sysmenu"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${FONTS_SOURCE_DIR}" "${PACKAGE_DIR}/font"
        # jsoncpp DLL 복사 (!! 실제 jsoncpp.dll 경로로 수정 필요 !!)
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/jsoncpp.dll" "${PACKAGE_DIR}/" # 예시: 빌드 디렉토리 루트에 있다고 가정
        # COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_JSON_SOURCE}" "${PACKAGE_DIR}/"
        # COMMAND ${CMAKE_COMMAND} -E copy "${DXLIB_DLL_PATH}" "${PACKAGE_DIR}/"
        # COMMAND ${CMAKE_COMMAND} -E copy "${MSVC_REDIST_DLL_PATH}" "${PACKAGE_DIR}/"
        # 7-Zip으로 압축
        COMMAND ${SEVEN_ZIP_EXECUTABLE} a -tzip "${PACKAGE_FINAL_ARCHIVE}" "${PACKAGE_DIR}\\*"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_DIR}"
        COMMENT "Creating package archive ${PACKAGE_FINAL_ARCHIVE} using 7-Zip"
        VERBATIM
    )
else()
    message(WARNING "7-Zip executable not found. Cannot create package target.")
endif()

