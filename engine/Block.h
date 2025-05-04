#pragma once

#include <string>
#include <vector>
#include <json/value.h>

struct Script;

class Block {
public:
    std::string id;
    std::string type;
    Json::Value paramsJson;
    std::vector<Script> statementScripts;

    Block() = default;
    Block(const std::string& blockType) : type(blockType) {}

    void printInfo(int indent = 0) const;
};

struct Script {
    std::vector<Block> blocks;
};
