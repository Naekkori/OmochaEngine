#ifndef OMOCHA_BLOCK_EXECUTOR_H
#define OMOCHA_BLOCK_EXECUTOR_H

#include <string>
#include "Block.h"
#include <nlohmann/json.hpp>
#include <vector> // For std::vector
#include <thread> // For std::thread
#include <mutex> // For std::mutex
#include <condition_variable> // For std::condition_variable
#include <queue> // For std::priority_queue
#include <functional> // For std::function
#include <atomic> // For std::atomic
#include <SDL3/SDL_stdinc.h> // For Uint32

// 전방 선언 (순환 참조 방지)
class Engine;
class PublicVariable{
 public:
  // TODO: Consider making these configurable or loaded from a file
  std::string user_name="ミケ愛団";
  std::string user_id="mikeaidan351";
};
// OperandValue 구조체 선언
struct OperandValue
{
    enum class Type
    {
        EMPTY,
        NUMBER,
        STRING,
        BOOLEAN,
    };
    Type type = Type::EMPTY;
    bool boolean_val = false;
    std::string string_val = "";
    double number_val = 0.0;

    OperandValue(); // 기본 생성자
    OperandValue(double val);
    OperandValue(const std::string &val);
    OperandValue(bool val);

    double asNumber() const;
    std::string asString() const;
    bool asBool() const;
};

const Uint32 MIN_LOOP_WAIT_MS = 1; // Minimum wait time in milliseconds for loops

class ThreadPool {
private:
    struct TaskPriorityCompare {
        bool operator()(
            const std::pair<int, std::function<void()>>& a,
            const std::pair<int, std::function<void()>>& b) const {
            return a.first > b.first;  // 낮은 숫자가 높은 우선순위
        }
    };

    std::vector<std::thread> workers;
    std::priority_queue<
        std::pair<int, std::function<void()>>,
        std::vector<std::pair<int, std::function<void()>>>,
        TaskPriorityCompare> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    const size_t max_queue_size;
    std::condition_variable queue_space_available;
    Engine& engine; // Engine 참조를 멤버로 가짐

public:
    // Engine 참조를 받는 생성자
    ThreadPool(Engine& eng, size_t threads, size_t maxQueueSize = 1000);

    template<class F>
    void enqueue(F&& f, int priority = 0) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_space_available.wait(lock, [this] {
            return tasks.size() < max_queue_size || stop;
        });
        if (stop) return;
        tasks.emplace(priority, std::forward<F>(f));
        condition.notify_one();
    }

    ~ThreadPool() {
        stop = true;
        condition.notify_all();
        queue_space_available.notify_all(); // 깨워서 종료 확인하도록
        for(std::thread &worker : workers) {
            if(worker.joinable()) worker.join();
        }
    }
};


// 블록 처리 함수 선언
OperandValue getOperandValue(Engine &engine, const std::string &objectId, const nlohmann::json &paramField, const std::string &executionThreadId);
void Moving(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string& executionThreadId, float deltaTime);
OperandValue Calculator(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string& executionThreadId);
void Looks(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string& executionThreadId);
void Sound(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string& executionThreadId);
void Variable(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string& executionThreadId);
void Function(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string& executionThreadId);
void Event(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string& executionThreadId);
void Flow(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string &executionThreadId, const std::string& sceneIdAtDispatch, float deltaTime);
void TextBox(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block, const std::string &executionThreadId);
void executeBlocksSynchronously(Engine& engine, const std::string& objectId, const std::vector<Block>& blocks, const std::string& executionThreadId, const std::string& sceneIdAtDispatch, float deltaTime);
// 스크립트를 실행하는 함수 선언 (Entity의 멤버 함수로 이동 예정이므로 주석 처리 또는 삭제)
// void executeScript(Engine& engine, const std::string& objectId, const Script* script);
#endif // OMOCHA_BLOCK_EXECUTOR_H