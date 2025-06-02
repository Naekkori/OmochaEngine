#pragma once // 헤더 가드 추가
#include <fstream>
#include <string>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <codecvt>
#include <locale>
#include <thread>             // Added for std::thread
#include <queue>              // Added for std::queue
#include <mutex>              // Added for std::mutex
#include <condition_variable> // Added for std::condition_variable
#include <atomic>             // Added for std::atomic

#if defined(_DEBUG) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

class SimpleLogger {
private:
    std::ofstream logFile;
    std::string logFilePath;
    bool isOpen = false;

    std::thread m_worker;
    std::queue<std::string> m_logQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stopWorker;

    // Special marker for flush operation
    const std::string FLUSH_MARKER = "::FLUSH_LOG_QUEUE_MARKER::";
    
    void workerThreadFunction() {
        while (true) {
            std::string messageToLog;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_condition.wait(lock, [this] { return m_stopWorker.load(std::memory_order_relaxed) || !m_logQueue.empty(); });

                if (m_stopWorker.load(std::memory_order_relaxed) && m_logQueue.empty()) {
                    break;
                }

                if (m_logQueue.empty()) { // Should ideally not happen if m_stopWorker is false due to wait condition
                    continue;
                }

                messageToLog = m_logQueue.front();
                m_logQueue.pop();
            } // Lock released

            if (this->isOpen && this->logFile.is_open()) { // Access member variables
                if (messageToLog == this->FLUSH_MARKER) {
                    logFile.flush();
                } else {
                    logFile << messageToLog << std::endl; // std::endl also flushes the stream
                }
            }
        }
    }

public:
    SimpleLogger(const std::string &filename = "engine.log")
        : logFilePath(filename), isOpen(false), m_stopWorker(false) { // Initialize isOpen and m_stopWorker
        try {
            if (std::filesystem::exists(logFilePath)) {
                std::filesystem::remove(logFilePath); // 로그파일 삭제 (용량관리)
            }
        }
        catch (...)
        {
        } // Empty catch block, consider logging or rethrowing
        logFile.open(logFilePath, std::ios::app);
        if (!logFile.is_open())
        {
            std::cerr << "Error: Failed to open log file: " << logFilePath << std::endl;
            isOpen = false;
        }
        else
        {
            isOpen = true;
            log("--- Log Session Started ---"); // This will now queue the message
        }
        // Start worker thread only if log file is open and logger is not in a stopped state (relevant for move semantics if added)
        if (this->isOpen && !m_stopWorker.load(std::memory_order_relaxed)) {
             m_worker = std::thread(&SimpleLogger::workerThreadFunction, this);
        }
    }

    ~SimpleLogger()
    {
        if (this->isOpen) {
            log("--- Log Session Ended ---");
            flush(); // Ensure all pending messages including "Ended" are processed
        }

        m_stopWorker.store(true, std::memory_order_relaxed);
        m_condition.notify_all(); // Wake up worker thread to process remaining queue and exit

        if (m_worker.joinable()) {
            m_worker.join();
        }

        if (this->logFile.is_open()) { // Final check before closing
            logFile.close();
        }
    }

    SimpleLogger(const SimpleLogger &) = delete;
    SimpleLogger &operator=(const SimpleLogger &) = delete;
    
    void log(const std::string &message) { // Removed const
        if (!this->isOpen)
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

#if defined(_DEBUG) && defined(_WIN32)
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wideMessage = converter.from_bytes(message);
        std::wstring wideMessageWithNewline = wideMessage + L"\n";
        OutputDebugStringW(wideMessageWithNewline.c_str());
#endif

        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);

        std::ostringstream time_stream;
        time_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
        std::string formatted_message = time_stream.str() + " | " + message;

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_logQueue.push(formatted_message);
        }
        m_condition.notify_one();
    }

    void flush() { // Removed const
        if (this->isOpen) {
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_logQueue.push(FLUSH_MARKER); // Add flush marker
            }
            m_condition.notify_one(); // Notify worker thread
            // For a synchronous flush that waits, a more complex mechanism would be needed.
            // This version just ensures the flush command is queued.
        }
    }
};
