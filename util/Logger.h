#pragma once // 헤더 가드 추가
#include <fstream>
#include <string>
#include <iostream>
#include <chrono>
#include <iomanip> 


#if defined(_DEBUG) && defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

class SimpleLogger {
private:
    mutable std::ofstream logFile;
    std::string logFilePath;
    bool isOpen = false;

public:
    
    SimpleLogger(const std::string& filename = "engine.log") : logFilePath(filename) {
        
        logFile.open(logFilePath, std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Error: Failed to open log file: " << logFilePath << std::endl;
            isOpen = false;
        } else {
            isOpen = true;
            log("--- Log Session Started ---"); 
        }
    }

    
    ~SimpleLogger() {
        if (isOpen) {
            log("--- Log Session Ended ---"); 
            logFile.close();
        }
    }

    
    SimpleLogger(const SimpleLogger&) = delete;
    SimpleLogger& operator=(const SimpleLogger&) = delete;

    
    void log(const std::string& message) const { // const 추가
        if (!isOpen) {
            
            std::cerr << "Warning: Log file is not open. Cannot write: " << message << std::endl;
            return;
        }

#if defined(_DEBUG) && defined(_WIN32) // 조건 확인 시에도 _WIN32 사용
        OutputDebugStringA(message.c_str());
        OutputDebugStringA("\n");
#endif

        
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);

        logFile << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << " | " << message << std::endl;
    }

    
    void flush() const { // const 추가
         if (isOpen) {
            logFile.flush();
        }
    }
};
