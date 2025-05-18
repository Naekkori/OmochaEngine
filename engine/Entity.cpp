#include "Entity.h"
#include <iostream>
#include <mutex> // For std::lock_guard
#include <string>
#include <cmath>
#include <stdexcept>
#include "Engine.h"
#include "blocks/BlockExecutor.h"
#include "blocks/blockTypes.h"
Entity::PenState::PenState(Engine* enginePtr) 
    : pEngine(enginePtr), 
      stop(false),      // 기본적으로 그리기가 중지되지 않은 상태 (활성화)
      isPenDown(false),
      lastStagePosition({0.0f, 0.0f}),
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
      scaleX(initial_scaleX), scaleY(initial_scaleY), rotation(initial_rotation), direction(initial_direction),
      width(initial_width), height(initial_height), visible(initial_visible), rotateMethod(initial_rotationMethod),
      brush(engine), paint(engine), timedMoveObjState(), timedRotationState() { // Initialize PenState members
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

    pEngineInstance->EngineStdOut("Executing script for object: " + id, 3);
    for (size_t i = 1; i < scriptPtr->blocks.size(); ++i) // 첫 번째 블록은 이벤트 트리거이므로 1부터 시작
    {
        const Block& block = scriptPtr->blocks[i];
        pEngineInstance->EngineStdOut("  Executing Block ID: " + block.id + ", Type: " + block.type + " for object: " + id, 3);
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
            
            Omocha::BlockTypeEnum blockType = Omocha::stringToBlockTypeEnum(block.type);
            std::string koreanBlockTypeName = Omocha::blockTypeEnumToKoreanString(blockType);
            std::string originalBlockTypeForLog = block.type;

            std::string errorMessage = "블럭 을 실행하는데 오류가 발생하였습니다. 블럭ID "+ block.id + " 의 타입 " + koreanBlockTypeName + (blockType == Omocha::BlockTypeEnum::UNKNOWN && !originalBlockTypeForLog.empty() ? " (원본: " + originalBlockTypeForLog + ")" : "") + " 에서 사용 하는 객체 " + id + "\n" + e.what();
            pEngineInstance->EngineStdOut("Error executing block: "+ block.id + " of type: " + originalBlockTypeForLog + " for object: " + id + ". Original error: " + e.what(),2);
            throw std::runtime_error(errorMessage);
        }
    }
}

const std::string& Entity::getId() const { return id; }
const std::string& Entity::getName() const { return name; }

double Entity::getX() const { std::lock_guard<std::mutex> lock(m_stateMutex); return x; }
double Entity::getY() const { std::lock_guard<std::mutex> lock(m_stateMutex); return y; }
double Entity::getRegX() const { std::lock_guard<std::mutex> lock(m_stateMutex); return regX; }
double Entity::getRegY() const { std::lock_guard<std::mutex> lock(m_stateMutex); return regY; }
double Entity::getScaleX() const { std::lock_guard<std::mutex> lock(m_stateMutex); return scaleX; }
double Entity::getScaleY() const { std::lock_guard<std::mutex> lock(m_stateMutex); return scaleY; }
double Entity::getRotation() const { std::lock_guard<std::mutex> lock(m_stateMutex); return rotation; }
double Entity::getDirection() const { std::lock_guard<std::mutex> lock(m_stateMutex); return direction; }
double Entity::getWidth() const { std::lock_guard<std::mutex> lock(m_stateMutex); return width; }
double Entity::getHeight() const { std::lock_guard<std::mutex> lock(m_stateMutex); return height; }
bool Entity::isVisible() const { std::lock_guard<std::mutex> lock(m_stateMutex); return visible; }


void Entity::setX(double newX) { std::lock_guard<std::mutex> lock(m_stateMutex); x = newX; }
void Entity::setY(double newY) { std::lock_guard<std::mutex> lock(m_stateMutex); y = newY; }
void Entity::setRegX(double newRegX) { std::lock_guard<std::mutex> lock(m_stateMutex); regX = newRegX; }
void Entity::setRegY(double newRegY) { std::lock_guard<std::mutex> lock(m_stateMutex); regY = newRegY; }
void Entity::setScaleX(double newScaleX) { std::lock_guard<std::mutex> lock(m_stateMutex); scaleX = newScaleX; }
void Entity::setScaleY(double newScaleY) { std::lock_guard<std::mutex> lock(m_stateMutex); scaleY = newScaleY; }
void Entity::setRotation(double newRotation) { std::lock_guard<std::mutex> lock(m_stateMutex); rotation = newRotation; }

void Entity::setDirection(double newDirection) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    // 방향 업데이트
    direction = newDirection;

    // 현재 스케일 값의 절대값을 유지하면서 부호만 변경하기 위함
    double currentScaleXMagnitude = std::abs(scaleX);
    double currentScaleYMagnitude = std::abs(scaleY);

    // 방향 값을 [0, 360) 범위로 정규화
    double normalizedAngle = std::fmod(newDirection, 360.0);
    if (normalizedAngle < 0.0) {
        normalizedAngle += 360.0;
    }

    // 회전 방식에 따른 스프라이트 반전 처리
    if (rotateMethod == RotationMethod::HORIZONTAL) {
        // normalizedAngle이 [0, 180) 범위면 오른쪽으로 간주하여 scaleX를 양수로,
        // [180, 360) 범위면 왼쪽으로 간주하여 scaleX를 음수로 설정합니다.
        if (normalizedAngle < 180.0) {
            scaleX = currentScaleXMagnitude;
        } else {
            scaleX = -currentScaleXMagnitude;
        }
    } else if (rotateMethod == RotationMethod::VERTICAL) {
        // normalizedAngle이 [90, 270) 범위면 아래쪽으로 간주하여 scaleY를 음수로,
        // 그 외 ([0, 90) U [270, 360)) 범위면 위쪽으로 간주하여 scaleY를 양수로 설정합니다.
        if (normalizedAngle >= 90.0 && normalizedAngle < 270.0) {
            scaleY = -currentScaleYMagnitude;
        } else {
            scaleY = currentScaleYMagnitude;
        }
    }
    // RotationMethod::FREE 또는 NONE의 경우, 방향(direction)에 따라 scale을 변경하지 않습니다.
    // FREE 회전은 rotation 속성으로 처리됩니다.
}
void Entity::setWidth(double newWidth) { std::lock_guard<std::mutex> lock(m_stateMutex); width = newWidth; }
void Entity::setHeight(double newHeight) { std::lock_guard<std::mutex> lock(m_stateMutex); height = newHeight; }
void Entity::setVisible(bool newVisible) { std::lock_guard<std::mutex> lock(m_stateMutex); visible = newVisible; }

Entity::RotationMethod Entity::getRotateMethod() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return rotateMethod;
}

void Entity::setRotateMethod(RotationMethod method) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    rotateMethod = method;
}

bool Entity::isPointInside(double pX, double pY) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
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
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return lastCollisionSide;
}

void Entity::setLastCollisionSide(CollisionSide side) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    lastCollisionSide = side;
}
// processMathematicalBlock, Moving, Calculator 등의 함수 구현도 여기에 유사하게 이동/정의해야 합니다.
// 간결성을 위해 전체 구현은 생략합니다. BlockExecutor.cpp의 내용을 참고하세요.
// -> 이 주석은 이제 유효하지 않습니다. 해당 함수들은 BlockExecutor.cpp에 있습니다.

void Entity::showDialog(const std::string& message, const std::string& dialogType, Uint64 duration) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
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
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_currentDialog.isActive) {
        m_currentDialog.clear();
    }
}

void Entity::updateDialog(Uint64 currentTimeMs) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_currentDialog.isActive && m_currentDialog.durationMs > 0) {
        if (currentTimeMs >= m_currentDialog.startTimeMs + m_currentDialog.durationMs) {
            // removeDialog()는 내부적으로 m_stateMutex를 다시 잠그려고 시도할 수 있습니다.
            // 이미 m_stateMutex가 잠겨있는 상태이므로, clear()를 직접 호출하거나
            // removeDialog()의 내부 로직을 여기에 직접 구현하는 것이 좋습니다.
            // 여기서는 clear()를 직접 호출하는 것으로 변경합니다.
            m_currentDialog.clear();
        }
    }
}
bool Entity::hasActiveDialog() const { std::lock_guard<std::mutex> lock(m_stateMutex); return m_currentDialog.isActive; }