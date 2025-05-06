#ifndef OMOCHA_BLOCK_EXECUTOR_H
#define OMOCHA_BLOCK_EXECUTOR_H

#include <string>
#include "Block.h"
// 전방 선언 (순환 참조 방지)
class Engine;
// 스크립트를 실행하는 함수 선언
void executeScript(Engine& engine, const std::string& objectId, const Script* script);
#endif // OMOCHA_BLOCK_EXECUTOR_H