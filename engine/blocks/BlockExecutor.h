#ifndef OMOCHA_BLOCK_EXECUTOR_H
#define OMOCHA_BLOCK_EXECUTOR_H

#include <string>
#include "Block.h"
#include <rapidjson/document.h> // For rapidjson::Value

// 전방 선언 (순환 참조 방지)
class Engine;
class PublicVariable{
 public:
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
};

// 블록 처리 함수 선언
OperandValue getOperandValue(Engine &engine, const std::string &objectId, const rapidjson::Value &paramField);
OperandValue processMathematicalBlock(Engine &engine, const std::string &objectId, const Block &block);
void Moving(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
OperandValue Calculator(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
void Shape(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
void Sound(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
void Variable(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
void Function(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);

// 스크립트를 실행하는 함수 선언 (Entity의 멤버 함수로 이동 예정이므로 주석 처리 또는 삭제)
// void executeScript(Engine& engine, const std::string& objectId, const Script* script);
#endif // OMOCHA_BLOCK_EXECUTOR_H