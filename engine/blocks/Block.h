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
        // other.paramsJson (Document)이 Null이 아니면, this.paramsJson (Document)으로 복사합니다.
        // this.paramsJson.GetAllocator()를 사용하여 자체 할당자로 복사합니다.
        if (!other.paramsJson.IsNull()) {
            paramsJson.CopyFrom(other.paramsJson, paramsJson.GetAllocator());
        } else {
            // paramsJson은 기본적으로 Null 상태이므로 별도 처리가 필요 없을 수 있습니다.
            // 명시적으로 Null로 설정하려면 paramsJson.SetNull();
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
            paramsJson.CopyFrom(other.paramsJson, paramsJson.GetAllocator());
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
};

struct Script {
    std::vector<Block> blocks;
};
