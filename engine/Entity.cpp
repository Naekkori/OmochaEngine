#include "Entity.h"
#include <iostream>
#include <string>
#include <cmath>
#include <stdexcept>
#include "Engine.h"
#include "blocks/BlockExecutor.h"

Entity::PenState::PenState(Engine* enginePtr) 
    : pEngine(enginePtr), 
      stop(false),      // 기본적으로 그리기가 중지되지 않은 상태 (활성화)
      isPenDown(false),
      lastStagePosition{0.0f, 0.0f},
      color{0, 0, 0, 255}
{
}

void Entity::PenState::setPenDown(bool down, float currentStageX, float currentStageY) {
    isPenDown = down;
    if (isPenDown) {
        lastStagePosition = {currentStageX, currentStageY};
    }
}

void Entity::PenState::updatePositionAndDraw(float newStageX, float newStageY) {
    if (!stop && isPenDown && pEngine) { // 그리기 조건: 중지되지 않았고(!stop) 펜이 내려져 있을 때
        SDL_FPoint targetStagePosJSStyle = {newStageX, newStageY * -1.0f};
        pEngine->engineDrawLineOnStage(lastStagePosition, targetStagePosJSStyle, color, 1.0f);
    }
    lastStagePosition = {newStageX, newStageY};
}

void Entity::PenState::reset(float currentStageX, float currentStageY) {
    lastStagePosition = {currentStageX, currentStageY};
}

void Entity::DialogState::clear() {
    isActive = false;
    text.clear();
    type.clear();
    if (textTexture) {
        SDL_DestroyTexture(textTexture);
        textTexture = nullptr;
    }
    startTimeMs = 0;
    durationMs = 0;
    needsRedraw = true;
    // bubbleScreenRect and tailVertices don't need explicit clearing here,
    // they are recalculated when the dialog becomes active.
}

Entity::Entity(Engine* engine, const std::string& entityId, const std::string& entityName,
    double initial_x, double initial_y, double initial_regX, double initial_regY,
    double initial_scaleX, double initial_scaleY, double initial_rotation, double initial_direction,
    double initial_width, double initial_height, bool initial_visible, Entity::RotationMethod initial_rotationMethod)
    : pEngineInstance(engine), id(entityId), name(entityName),
    x(initial_x), y(initial_y), regX(initial_regX), regY(initial_regY),
    scaleX(initial_scaleX), scaleY(initial_scaleY), rotation(initial_rotation), direction(initial_direction), // timedRotationState 추가
    width(initial_width), height(initial_height), visible(initial_visible), rotateMethod(initial_rotationMethod),
    brush(engine), paint(engine),timedMoveObjState(), timedRotationState() // Initialize PenState members
{
}

Entity::~Entity() {
}

// OperandValue 구조체 및 블록 처리 함수들은 BlockExecutor.h에 선언되어 있고,
// BlockExecutor.cpp에 구현되어 있으므로 Entity.cpp에서 중복 선언/정의할 필요가 없습니다.

void Entity::executeScript(const Script* scriptPtr)
{
    if (!pEngineInstance) {
        // EngineStdOut은 Engine의 멤버이므로 직접 호출 불가. 로깅 방식 변경 필요
        std::cerr << "ERROR: Entity " << id << " has no valid Engine instance for script execution." << std::endl;
        return;
    }
    if (!scriptPtr) {
        pEngineInstance->EngineStdOut("executeScript called with null script pointer for object: " + id, 2);
        return;
    }

    pEngineInstance->EngineStdOut("Executing script for object: " + id, 0);
    for (size_t i = 1; i < scriptPtr->blocks.size(); ++i) // 첫 번째 블록은 이벤트 트리거이므로 1부터 시작
    {
        const Block& block = scriptPtr->blocks[i];
        pEngineInstance->EngineStdOut("  Executing Block ID: " + block.id + ", Type: " + block.type + " for object: " + id, 0);
        try
        {   
            // 여기서는 기존 함수 시그니처를 유지하고 Engine과 objectId를 전달합니다.
            Moving(block.type, *pEngineInstance, this->id, block);
            Calculator(block.type, *pEngineInstance, this->id, block); // Calculator는 OperandValue를 반환하므로, 결과 처리가 필요하면 수정
            Looks(block.type, *pEngineInstance, this->id, block);
            Sound(block.type, *pEngineInstance, this->id, block);
            Variable(block.type, *pEngineInstance, this->id, block);
            Function(block.type, *pEngineInstance, this->id, block);
        }
        catch (const std::exception& e)
        {
            pEngineInstance->showMessageBox("Error executing block: " + block.id + " of type: " + block.type + " for object: " + id + "\n" + e.what(), pEngineInstance->msgBoxIconType.ICON_ERROR);
            // SDL_Quit(); // 여기서 직접 SDL_Quit()을 호출하는 것은 위험할 수 있습니다. 예외를 다시 던지거나 엔진에 알리는 것이 좋습니다.
            throw; // 또는 에러 플래그 설정
        }
    }
}


const std::string& Entity::getId() const { return id; }
const std::string& Entity::getName() const { return name; }
double Entity::getX() const { return x; }
double Entity::getY() const { return y; }
double Entity::getRegX() const { return regX; }
double Entity::getRegY() const { return regY; }
double Entity::getScaleX() const { return scaleX; }
double Entity::getScaleY() const { return scaleY; }
double Entity::getRotation() const { return rotation; }
double Entity::getDirection() const { return direction; }
double Entity::getWidth() const { return width; }
double Entity::getHeight() const { return height; }
bool Entity::isVisible() const { return visible; }


void Entity::setX(double newX) { x = newX; }
void Entity::setY(double newY) { y = newY; }
void Entity::setRegX(double newRegX) { regX = newRegX; }
void Entity::setRegY(double newRegY) { regY = newRegY; }
void Entity::setScaleX(double newScaleX) { scaleX = newScaleX; }
void Entity::setScaleY(double newScaleY) { scaleY = newScaleY; }
void Entity::setRotation(double newRotation) { rotation = newRotation; }
void Entity::setDirection(double newDirection) {
    // 방향 업데이트
    direction = newDirection;

    // 회전 방식에 따른 스프라이트 반전 처리
    if (rotateMethod == RotationMethod::HORIZONTAL) {
        // direction이 0~180 (오른쪽)이면 scaleX를 양수로, 180~360 (왼쪽)이면 음수로 설정
        scaleX = (direction < 180) ? std::abs(scaleX) : -std::abs(scaleX);
    } else if (rotateMethod == RotationMethod::VERTICAL || rotateMethod == RotationMethod::FREE) {
        // direction이 90~270 (아래쪽)이면 scaleY를 음수로, 그 외 (위쪽)이면 양수로 설정
        // 0도: 위쪽, 90도: 오른쪽, 180도: 아래쪽, 270도: 왼쪽
        if (direction >= 90 && direction < 270) {
            scaleY = -std::abs(scaleY);
        } else {
            scaleY = std::abs(scaleY);
        }
    }
    // RotationMethod::FREE 또는 NONE의 경우는 여기서 처리하지 않음
    // (FREE는 회전각으로 처리, NONE은 반전 없음)
}
void Entity::setWidth(double newWidth) { width = newWidth; }
void Entity::setHeight(double newHeight) { height = newHeight; }
void Entity::setVisible(bool newVisible) { visible = newVisible; }
Entity::RotationMethod Entity::getRotateMethod() const {
    return rotateMethod;
}

void Entity::setRotateMethod(RotationMethod method) {
    rotateMethod = method;
}
bool Entity::isPointInside(double pX, double pY) const {
    // pX, pY는 스테이지 좌표 (중앙 (0,0), Y축 위쪽)
    // this->x, this->y는 엔티티의 등록점의 스테이지 좌표

    // 1. 점을 엔티티의 로컬 좌표계로 변환 (등록점을 원점으로)
    double localPX = pX - this->x;
    double localPY = pY - this->y;

    // 2. 엔티티의 회전만큼 점을 반대로 회전
    // SDL 각도는 시계 방향이므로, 점을 객체 프레임으로 가져오려면 반시계 방향(-rotation)으로 회전
    const double PI = std::acos(-1.0);
    double angleRad = -this->rotation * (PI / 180.0); // 도 -> 라디안, 반대 방향

    double rotatedPX = localPX * std::cos(angleRad) - localPY * std::sin(angleRad);
    double rotatedPY = localPX * std::sin(angleRad) + localPY * std::cos(angleRad);

    // 3. 이제 rotatedPX, rotatedPY는 엔티티의 비회전 로컬 좌표계에 있음 (등록점이 원점)
    //    이 좌표계에서 엔티티의 경계 상자를 확인합니다.
    //    regX, regY는 비스케일 이미지의 좌상단으로부터 등록점까지의 오프셋입니다.
    //    로컬 좌표계 (Y 위쪽)에서:
    //    - 좌측 X: -regX * scaleX
    //    - 우측 X: (-regX + width) * scaleX
    //    - 상단 Y: regY * scaleY
    //    - 하단 Y: (regY - height) * scaleY

    double localMinX = -this->regX * this->scaleX;
    double localMaxX = (-this->regX + this->width) * this->scaleX;

    // Y축이 위를 향하므로, regY가 클수록 위쪽, (regY - height)가 아래쪽
    double localMinY = (this->regY - this->height) * this->scaleY;
    double localMaxY = this->regY * this->scaleY;


    if (rotatedPX >= localMinX && rotatedPX <= localMaxX &&
        rotatedPY >= localMinY && rotatedPY <= localMaxY) {
        return true;
    }

    return false;
}
Entity::CollisionSide Entity::getLastCollisionSide() const {
    return lastCollisionSide;
}

void Entity::setLastCollisionSide(CollisionSide side) {
    lastCollisionSide = side;
}
// processMathematicalBlock, Moving, Calculator 등의 함수 구현도 여기에 유사하게 이동/정의해야 합니다.
// 간결성을 위해 전체 구현은 생략합니다. BlockExecutor.cpp의 내용을 참고하세요.
// -> 이 주석은 이제 유효하지 않습니다. 해당 함수들은 BlockExecutor.cpp에 있습니다.

void Entity::showDialog(const std::string& message, const std::string& dialogType, Uint64 duration) {
    m_currentDialog.clear(); 

    m_currentDialog.text = message.empty() ? "    " : message;
    m_currentDialog.type = dialogType;
    m_currentDialog.isActive = true;
    m_currentDialog.durationMs = duration;
    m_currentDialog.startTimeMs = SDL_GetTicks(); 
    m_currentDialog.needsRedraw = true;

    if (pEngineInstance) {
         pEngineInstance->EngineStdOut("Entity " + id + " dialog: '" + m_currentDialog.text + "', type: " + m_currentDialog.type + ", duration: " + std::to_string(duration), 3);
    }
}

void Entity::removeDialog() {
    if (m_currentDialog.isActive) {
        m_currentDialog.clear();
    }
}

void Entity::updateDialog(Uint64 currentTimeMs) {
    if (m_currentDialog.isActive && m_currentDialog.durationMs > 0) {
        if (currentTimeMs >= m_currentDialog.startTimeMs + m_currentDialog.durationMs) {
            removeDialog();
        }
    }
}