#include "Entity.h"
#include <iostream>
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
      scaleX(initial_scaleX), scaleY(initial_scaleY), rotation(initial_rotation), direction(initial_direction),
      width(initial_width), height(initial_height), visible(initial_visible), rotateMethod(initial_rotationMethod),
      brush(engine), paint(engine), timedMoveObjState(), timedRotationState(), m_logicRunning(false) // Initialize PenState members and m_logicRunning
{
    // 주의: 생성자에서 startLogicThread()를 호출하면 아직 완전히 생성되지 않은 Engine 객체에 접근할 수 있으므로
    // Engine에서 Entity 생성 후 별도로 startLogicThread()를 호출하는 것이 안전합니다.
}

Entity::~Entity() {
    stopLogicThread(); // Ensure thread is stopped and joined
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

void Entity::startLogicThread() {
    if (m_logicRunning.load()) return; // 이미 실행 중이면 반환
    m_logicRunning = true;
    m_logicThread = std::thread(&Entity::logicLoop, this);
    if (pEngineInstance) {
        pEngineInstance->EngineStdOut("Logic thread started for entity: " + id, 0);
    }
}

void Entity::stopLogicThread() {
    if (!m_logicRunning.load()) return; // 이미 중지되었거나 시작되지 않았으면 반환

    m_logicRunning = false;
    m_scriptQueueCV.notify_all(); // 모든 대기 중인 스레드 깨우기 (혹시 여러 조건에 대기 중일 수 있으므로)
    if (m_logicThread.joinable()) {
        m_logicThread.join();
    }
    if (pEngineInstance) {
         pEngineInstance->EngineStdOut("Logic thread stopped for entity: " + id, 0);
    }
}

void Entity::requestScriptExecution(const Script* scriptPtr) {
    if (!scriptPtr) return;
    {
        std::lock_guard<std::mutex> lock(m_scriptQueueMutex);
        m_scriptQueue.push(scriptPtr);
    }
    m_scriptQueueCV.notify_one();
}

void Entity::logicLoop() {
    if (!pEngineInstance) {
        std::cerr << "Entity " << id << " logicLoop: pEngineInstance is null. Thread cannot run." << std::endl;
        m_logicRunning = false; // 스레드 실행 중단
        return;
    }
    pEngineInstance->EngineStdOut("Entity " + id + " logicLoop started.", 0);

    while (m_logicRunning.load()) {
        const Script* scriptToRun = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_scriptQueueMutex);
            // 스크립트 큐가 비어있고 스레드가 계속 실행 중이어야 하면 대기
            m_scriptQueueCV.wait(lock, [this] { return !m_scriptQueue.empty() || !m_logicRunning.load(); });

            if (!m_logicRunning.load() && m_scriptQueue.empty()) { // 스레드 종료 조건
                break;
            }

            if (!m_scriptQueue.empty()) {
                scriptToRun = m_scriptQueue.front();
                m_scriptQueue.pop();
            }
        }
        if (!m_logicRunning.load()) {
            break;
        }
        if (scriptToRun) {
            pEngineInstance->EngineStdOut("Entity " + id + " thread will attempt to execute a script. THIS IS LIKELY UNSAFE.", 1);
            try {
                this->executeScript(scriptToRun);
            } catch (const std::exception& e) {
                if (pEngineInstance && m_logicRunning.load()) { // Check if engine instance is valid and thread should be running
                    pEngineInstance->EngineStdOut("Exception in entity " + id + " logic thread during script execution: " + e.what(), 2);
                } else {
                    std::cerr << "Exception in entity " << id << " logic thread (engine unavailable or stopping): " << e.what() << std::endl;
                }
            } catch (...) {
                if (pEngineInstance && m_logicRunning.load()) { // Check if engine instance is valid and thread should be running
                    pEngineInstance->EngineStdOut("Unknown exception in entity " + id + " logic thread during script execution.", 2);
                } else {
                    std::cerr << "Unknown exception in entity " << id << " logic thread (engine unavailable or stopping)." << std::endl;
                }
            }
        }else{
            std::lock_guard<std::mutex> stateLock(m_stateMutex); // 엔티티 내부 상태 변경 보호

            if (timedMoveState.isActive && timedMoveState.remainingFrames > 0) {
                 double currentX_unsafe = x; // 락 내부에서 직접 접근
                 double currentY_unsafe = y;
                 double dX_total = timedMoveState.targetX - currentX_unsafe;
                 double dY_total = timedMoveState.targetY - currentY_unsafe;
                 double dX_step = (timedMoveState.remainingFrames > 0) ? dX_total / timedMoveState.remainingFrames : 0;
                 double dY_step = (timedMoveState.remainingFrames > 0) ? dY_total / timedMoveState.remainingFrames : 0;
                 x += dX_step;
                 y += dY_step;
                 paint.updatePositionAndDraw(x, y); // 이 함수도 스레드 안전성 검토 필요
                 brush.updatePositionAndDraw(x, y); // 이 함수도 스레드 안전성 검토 필요
                 timedMoveState.remainingFrames--;
                 if (timedMoveState.remainingFrames <= 0) {
                    x = timedMoveState.targetX;
                    y = timedMoveState.targetY;
                    timedMoveState.isActive = false;
                 }
            }
            // timedMoveObjState, timedRotationState 관련 로직도 유사하게 m_stateMutex 보호 하에 처리
        }

        // 루프 지연 (CPU 사용량 조절 및 다른 스레드에게 기회 제공)
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 매우 짧은 지연
    }
    pEngineInstance->EngineStdOut("Entity " + id + " logicLoop finished.", 0);
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
    // pX, pY는 스테이지 좌표 (중앙 (0,0), Y축 위쪽)
    // this->x, this->y는 엔티티의 등록점의 스테이지 좌표
    std::lock_guard<std::mutex> lock(m_stateMutex); // 상태 변수 읽기 보호
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
    std::lock_guard<std::mutex> lock(m_stateMutex); // m_currentDialog 접근 보호
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