#pragma once // 헤더 가드 추가
#include <fstream>
#include <string>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <codecvt> // For std::codecvt_utf8_utf16
#include <locale>  // For std::wstring_convert

#if defined(_DEBUG) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

class SimpleLogger
{
private:
    mutable std::ofstream logFile;
    std::string logFilePath;
    bool isOpen = false;

public:
    SimpleLogger(const std::string &filename = "engine.log") : logFilePath(filename)
    {
        try
        {
            if (std::filesystem::exists(logFilePath))
            {
                std::filesystem::remove(logFilePath); // 로그파일 삭제 (용량관리)
            }
        }
        catch (...)
        {
        }
        logFile.open(logFilePath, std::ios::app);
        if (!logFile.is_open())
        {
            std::cerr << "Error: Failed to open log file: " << logFilePath << std::endl;
            isOpen = false;
        }
        else
        {
            isOpen = true;
            log("--- Log Session Started ---");
        }
    }

    ~SimpleLogger()
    {
        if (isOpen)
        {
            log("--- Log Session Ended ---");
            logFile.close();
        }
    }

    SimpleLogger(const SimpleLogger &) = delete;
    SimpleLogger &operator=(const SimpleLogger &) = delete;

    void log(const std::string &message) const
    { // const 추가
        if (!isOpen)
        {
            // 로그 파일이 열려있지 않으면 표준 오류 스트림에 경고 메시지를 출력합니다.
            std::cerr << "Warning: Log file is not open. Cannot write: " << message << std::endl;
            return;
        }

        // 디버그 메시지이고 (_DEBUG 매크로가 정의되지 않은) 릴리스 모드일 경우, 해당 로그를 무시합니다.
        if (message.rfind("[DEBUG]", 0) == 0)
        {           // 메시지가 [DEBUG]로 시작하는 경우
#ifndef _DEBUG      // _DEBUG 매크로가 정의되지 않았다면 (즉, 릴리스 모드라면)
            return; // [DEBUG] 로그를 건너뜀
#endif
            // 디버그 모드(_DEBUG가 정의된 경우)이거나, 메시지가 [DEBUG]로 시작하지 않으면 이 조건에 해당하지 않아 계속 진행됩니다.
        }

#if defined(_DEBUG) && defined(_WIN32) // 조건 확인 시에도 _WIN32 사용
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wideMessage = converter.from_bytes(message);
        std::wstring wideMessageWithNewline = wideMessage + L"\n";
        OutputDebugStringW(wideMessageWithNewline.c_str());
#endif

        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);

        logFile << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << " | " << message << std::endl;
    }

    void flush() const
    { // const 추가
        if (isOpen)
        {
            logFile.flush();
        }
    }
};
