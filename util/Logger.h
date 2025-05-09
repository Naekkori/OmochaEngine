#include <fstream>
#include <string>
#include <iostream>
#include <chrono>
#include <iomanip> 

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
