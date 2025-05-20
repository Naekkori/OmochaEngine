#pragma once

#include <string>
#include <vector>
#include <rapidjson/document.h>
struct Script;

class Block {
public:
    std::string id;
    std::string type;
    rapidjson::Document paramsJson; // Value에서 Document로 변경
    std::vector<Script> statementScripts;

    Block() {} // Document는 기본 생성 시 kNullType 입니다.
    Block(const std::string& blockType) : type(blockType) {}

    // 복사 생성자
    Block(const Block& other) : id(other.id), type(other.type), statementScripts(other.statementScripts) {
        if (!other.paramsJson.IsNull()) {
            if (other.paramsJson.IsArray()) {
                paramsJson.SetArray(); 
                for (const auto& item : other.paramsJson.GetArray()) {
                    if (!item.IsNull()) { // null이 아닌 항목만 복사
                        rapidjson::Value itemCopy(item, paramsJson.GetAllocator());
                        paramsJson.PushBack(itemCopy, paramsJson.GetAllocator());
                    }
                }
            } else {
                paramsJson.CopyFrom(other.paramsJson, paramsJson.GetAllocator());
            }
        }
    }
    // 이동 생성자 (권장)
    Block(Block&& other) noexcept
        : id(std::move(other.id)),
          type(std::move(other.type)),
          paramsJson(std::move(other.paramsJson)), // rapidjson::Document도 이동 가능
          statementScripts(std::move(other.statementScripts)) {}

    // 복사 대입 연산자
    Block& operator=(const Block& other) {
        if (this == &other) return *this;
        id = other.id;
        type = other.type;
        statementScripts = other.statementScripts;
        if (!other.paramsJson.IsNull()) {
            if (other.paramsJson.IsArray()) {
                paramsJson.SetArray(); // this->paramsJson을 빈 배열로 초기화 (기존 내용 삭제)
                for (const auto& item : other.paramsJson.GetArray()) {
                        rapidjson::Value itemCopy(item, paramsJson.GetAllocator());
                        paramsJson.PushBack(itemCopy, paramsJson.GetAllocator());
                }
            } else {
                paramsJson.CopyFrom(other.paramsJson, paramsJson.GetAllocator());
            }
        } else {
            paramsJson.SetNull(); // 원본이 Null이면 대상도 Null로 설정
        }
        return *this;
    }

    Block& operator=(Block&& other) noexcept {
        if (this == &other) return *this;
        id = std::move(other.id);
        type = std::move(other.type);
        paramsJson = std::move(other.paramsJson); // Document 이동 대입
        statementScripts = std::move(other.statementScripts);
        return *this;
    }

    void printInfo(int indent = 0) const;

    // paramsJson 배열 내의 null 값을 제거하는 메서드
    void FilterNullsInParamsJsonArray() {
        if (paramsJson.IsArray() && !paramsJson.Empty()) {
            rapidjson::Document filteredDoc;
            filteredDoc.SetArray();
            rapidjson::Document::AllocatorType& allocator = filteredDoc.GetAllocator();

            for (const auto& item : paramsJson.GetArray()) {
                if (!item.IsNull()) {
                    // null이 아닌 요소만 새 Document에 깊은 복사합니다.
                    rapidjson::Value itemCopy(item, allocator);
                    filteredDoc.PushBack(itemCopy, allocator);
                }
            }
            // 기존 paramsJson을 필터링된 Document로 교체합니다.
            // rapidjson::Document는 MoveAssignable하므로 std::move를 사용합니다.
            paramsJson = std::move(filteredDoc);
        }
    }
};

struct Script {
    std::vector<Block> blocks;
};
