#pragma once

#include <iostream> // 임시 로깅을 위해 추가
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h> // OutputDebugStringA를 위해 추가
#include <string>    // std::string을 위해 추가
#include <sstream>   // std::ostringstream을 위해 추가
#endif
struct Script;

class Block
{
public:
    std::string id;
    std::string type;
    nlohmann::json paramsJson; // Value에서 Document로 변경
    std::vector<Script> statementScripts;

    Block() {} // Document는 기본 생성 시 kNullType 입니다.
    Block(const std::string &blockType) : type(blockType) {}

    // 복사 생성자
    Block(const Block &other) : id(other.id), type(other.type), statementScripts(other.statementScripts)
    {
        paramsJson = other.paramsJson;
    }
    // 이동 생성자 (권장)
    Block(Block &&other) noexcept
        : id(std::move(other.id)),
          type(std::move(other.type)),
          paramsJson(std::move(other.paramsJson)), // rapidjson::Document도 이동 가능
          statementScripts(std::move(other.statementScripts))
    {
    }

    // 복사 대입 연산자
    Block &operator=(const Block &other)
    {
        if (this == &other)
            return *this;
        id = other.id;
        type = other.type;
        paramsJson = other.paramsJson;
        statementScripts = other.statementScripts;
        return *this;
    }
    // 이동 대입 연산자
    Block &operator=(Block &&other) noexcept
    {
        if (this == &other)
            return *this;
        id = std::move(other.id);
        type = std::move(other.type);
        paramsJson = std::move(other.paramsJson);
        statementScripts = std::move(other.statementScripts);
        return *this;
    }

    void printInfo(int indent = 0) const;

    // paramsJson 배열 내의 null 값을 제거하는 메서드
    void FilterNullsInParamsJsonArray()
    {
#ifdef _WIN32
        // Helper lambda to send debug messages (defined locally within the method)
        auto DebugOutput = [](const std::string &msg)
        {
            OutputDebugStringA(msg.c_str());
            OutputDebugStringA("\n");
        };
#else
        // For other platforms, you might use std::cout or another logging mechanism
        auto DebugOutput = [](const std::string &msg)
        {
            // std::cout << msg << std::endl; // Placeholder for non-Windows
            (void)msg; // Prevent unused variable warning if std::cout is commented out
        };
#endif
        if (paramsJson.is_array() && !paramsJson.empty())
        {
            // std::cout << "[DEBUG] FilterNulls: Block ID " << id << ", Type " << type << " - Before: " << paramsJson.dump() << std::endl;
            DebugOutput.operator()("[DEBUG] FilterNulls: Block ID " + id + ", Type " + type + " - Before: " + paramsJson.dump());
            nlohmann::json filteredArray = nlohmann::json::array();
            for (const auto &item : paramsJson)
            {
                // std::cout << "[DEBUG] FilterNulls: Checking item: " << item.dump() << ", is_null(): " << item.is_null() << std::endl;
                DebugOutput.operator()("[DEBUG] FilterNulls: Checking item: " + item.dump() + ", is_null(): " + std::to_string(item.is_null()));
                if (!item.is_null())
                {
                    filteredArray.push_back(item); // nlohmann::json은 값 복사
                }
            }
            paramsJson = std::move(filteredArray);
            // std::cout << "[DEBUG] FilterNulls: Block ID " << id << ", Type " << type << " - After: " << paramsJson.dump() << std::endl;
            DebugOutput.operator()("[DEBUG] FilterNulls: Block ID "+id+", Type "+type+" - After: "+paramsJson.dump());
        }
        else
        {
            if (paramsJson.is_array() && paramsJson.empty()) {
            //     std::cout << "[DEBUG] FilterNulls: Block ID " << id << ", Type " << type << " - Input was an empty array." << std::endl;
            DebugOutput.operator()("[DEBUG] FilterNulls: Block ID "+id+", Type "+type+" - Input was an empty array.");
             } else if (!paramsJson.is_array()) {
            //     std::cout << "[DEBUG] FilterNulls: Block ID " << id << ", Type " << type << " - Input was not an array: " << paramsJson.type_name() << std::endl;
            DebugOutput.operator()("[DEBUG] FilterNulls: Block ID "+id+", Type "+type+" - Input was not an array: "+paramsJson.type_name());
            }
        }
    }
};

struct Script
{
    std::vector<Block> blocks;
};