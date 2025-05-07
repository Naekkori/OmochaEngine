#pragma once

#include <string>
#include <vector>
#include <rapidjson/document.h>

struct Script;

class Block {
public:
    std::string id;
    std::string type;
    rapidjson::Value paramsJson;
    std::vector<Script> statementScripts;

    Block() : paramsJson(rapidjson::kNullType) {} // paramsJson을 Null 타입으로 초기화
    Block(const std::string& blockType) : type(blockType) {}

    // 복사 생성자
    Block(const Block& other) : id(other.id), type(other.type), statementScripts(other.statementScripts) {

        if (other.paramsJson.IsNull()) {
            paramsJson.SetNull();
        } else {
            paramsJson.SetNull(); // 안전하게 Null로 설정 (실제 데이터 복사 필요 시 수정)
        }
    }

    // 이동 생성자 (권장)
    Block(Block&& other) noexcept
        : id(std::move(other.id)),
          type(std::move(other.type)),
          paramsJson(std::move(other.paramsJson)), // rapidjson::Value는 이동 가능
          statementScripts(std::move(other.statementScripts)) {}

    // 복사 대입 연산자
    Block& operator=(const Block& other) {
        if (this == &other) return *this;
        id = other.id;
        type = other.type;
        statementScripts = other.statementScripts;
        // paramsJson 복사 (복사 생성자와 동일한 주의사항 적용)
        if (other.paramsJson.IsNull()) {
            paramsJson.SetNull();
        } else {
            paramsJson.SetNull(); // 안전하게 Null로 설정 (실제 데이터 복사 필요 시 수정)
        }
        return *this;
    }

    Block& operator=(Block&& other) noexcept {
        if (this == &other) return *this;
        id = std::move(other.id);
        type = std::move(other.type);
        paramsJson = std::move(other.paramsJson);
        statementScripts = std::move(other.statementScripts);
        return *this;
    }

    void printInfo(int indent = 0) const;
};

struct Script {
    std::vector<Block> blocks;
};
