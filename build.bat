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

echo Changing directory to build folder...
cd /d "F:\kkori\OmochaEngine\out\build\x64-debug"
if %errorlevel% neq 0 (
    echo ERROR: Failed to change directory to F:\kkori\OmochaEngine\out\build\x64-debug.
    goto :eof
)

echo Building create_package target...
cmake --build . --target create_package
if %errorlevel% neq 0 (
    echo ERROR: Build failed.
) else (
    echo Build and packaging completed successfully.
    echo Package created at F:\kkori\OmochaEngine\out\build\x64-debug\OmochaEngine-1.0.0-win64.zip
)

echo.
pause
:eof