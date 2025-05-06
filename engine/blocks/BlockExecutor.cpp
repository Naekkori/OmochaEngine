#include "BlockExecutor.h"
#include "../Engine.h"
#include "../Entity.h"
void Behavior(std::string BlockType);
void Mathematical (std::string BlockType);
void Shape(std::string BlockType);
void Sound(std::string BlockType);
void Variable(std::string BlockType);
void Function(std::string BlockType);
// 스크립트 블록을 순차적으로 실행하는 함수
void executeScript(Engine& engine, const std::string& objectId, const Script* scriptPtr) // Engine 참조 추가, 이름 수정
{
    if (!scriptPtr) {        
        engine.EngineStdOut("Error: executeScript called with null script pointer for object: " + objectId, 2);
        return;
    }

    // scriptPtr->blocks[0]은 'when_run_button_click' 같은 이벤트 블록이므로 건너뛰고 i=1부터 시작
    engine.EngineStdOut("Executing script for object: " + objectId, 0);
    for (size_t i = 1; i < scriptPtr->blocks.size(); ++i)
    {
        const Block& block = scriptPtr->blocks[i];
        engine.EngineStdOut("  Executing Block ID: " + block.id + ", Type: " + block.type + " for object: " + objectId, 0);
        // TODO: 여기에 block.type에 따른 실제 블록 실행 로직 구현
        // 예: engine.moveEntityX(objectId, block.getParam<double>("DX"));
        Behavior(block.type);
        Mathematical (block.type);
        Shape(block.type);
        Sound(block.type);
        Variable(block.type);
        Function(block.type);
    }
}
/**
 * @brief 행동블럭
 * 
 */
void Behavior(std::string BlockType){
    /*
    예시
    if (block.type == "move_x_by") {
            // 파라미터 접근 예시: paramsJson 배열의 첫 번째 요소가 숫자 값이라고 가정
            if (block.paramsJson.isArray() && block.paramsJson.size() > 0 && block.paramsJson[0].isNumeric()) {
                // 1. 파라미터 값을 double 타입으로 읽어옴
                double dx = block.paramsJson[0].asDouble();
                // 2. 읽어온 값(dx)을 사용하여 엔티티의 X 좌표 변경
                targetEntity->setX(targetEntity->getX() + dx);
                engine.EngineStdOut("     Moved X by: " + std::to_string(dx), 3);
            } else {
                engine.EngineStdOut("     WARN: Invalid or missing parameter for move_x_by block.", 1);
            }
        }
    */
}
/**
 * @brief 수학블럭
 * 
 */
void Mathematical (std::string BlockType){

}
/**
 * @brief 모양블럭
 * 
 */
void Shape(std::string BlockType){

}
/**
 * @brief 사운드블럭
 * 
 */
void Sound(std::string BlockType){

}
/**
 * @brief 변수블럭
 * 
 */
void Variable(std::string BlockType){

}
/**
 * @brief 함수블럭
 * 
 */
void Function(std::string BlockType){

}