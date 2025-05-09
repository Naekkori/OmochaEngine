@echo off
echo Setting up Visual Studio x64 environment...

:: !! Visual Studio 설치 경로가 다르면 아래 경로를 수정해주세요 !!
:: Visual Studio 2022 Community/Professional/Enterprise 버전에 따라 경로가 다를 수 있습니다.
set VS_INSTALL_DIR=D:\VS
set VS_DEVCMD_PATH="%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat"

:: x64 개발자 환경 설정
call %VS_DEVCMD_PATH% -arch=amd64
if %errorlevel% neq 0 (
    echo ERROR: Failed to set up Visual Studio environment. Check VS_DEVCMD_PATH.
    goto :eof
)

set BUILD_TYPE=Release
set PROJECT_ROOT_DIR=F:\kkori\OmochaEngine
set BUILD_DIR=%PROJECT_ROOT_DIR%\out\build\x64-%BUILD_TYPE%

echo Creating build directory: %BUILD_DIR%
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
if %errorlevel% neq 0 (
    echo ERROR: Failed to change or create directory: %BUILD_DIR%.
    goto :eof
)

echo Configuring CMake for %BUILD_TYPE% build...
cmake -S "%PROJECT_ROOT_DIR%" -B . -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed for %BUILD_TYPE%.
    goto :eof
)

echo Building %BUILD_TYPE% target...
cmake --build . --config %BUILD_TYPE%
if %errorlevel% neq 0 (
    echo ERROR: %BUILD_TYPE% build failed.
    goto :eof
)

echo Creating package for %BUILD_TYPE% build...
cpack --config CPackConfig.cmake
if %errorlevel% neq 0 (
    echo ERROR: Packaging failed for %BUILD_TYPE%.
) else (
    echo %BUILD_TYPE% build and packaging completed successfully.
    echo Package created in %BUILD_DIR%
)

echo.
pause
:eof