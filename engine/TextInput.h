#pragma once
#include <string>
#include <mutex>
#include <condition_variable>

// 텍스트 입력 관련 인터페이스
class TextInputInterface {
public:
    virtual bool isTextInputActive() const = 0;
    virtual void clearTextInput() = 0;
    virtual void deactivateTextInput() = 0;
    virtual std::mutex& getTextInputMutex() = 0;
    virtual ~TextInputInterface() = default;
};
