#include "Entity.h"
#include <iostream>
#include <mutex> // For std::lock_guard
#include <string>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "Engine.h"
#include "blocks/BlockExecutor.h"
#include "blocks/blockTypes.h"
Entity::PenState::PenState(Engine *enginePtr)
    : pEngine(enginePtr),
      stop(false), // 기본적으로 그리기가 중지되지 않은 상태 (활성화)
      isPenDown(false),
      lastStagePosition({0.0f, 0.0f}),
      color{0, 0, 0, 255}
{
}
// Helper function to mimic parseFloat(num.toFixed(2))
static double helper_parseFloatNumToFixed2(double num, Engine *pEngineInstanceForLog)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << num; // 소수점 2자리로 포맷
    std::string s = oss.str();
    try
    {
        return std::stod(s); // 문자열을 double로 변환
    }
    catch (const std::invalid_argument &ia)
    {
        if (pEngineInstanceForLog)
        {
            pEngineInstanceForLog->EngineStdOut("Error in helper_parseFloatNumToFixed2 (invalid_argument): " + std::string(ia.what()) + " for string '" + s + "'", 2);
        }
        return std::nan(""); // 변환 실패 시 NaN 반환
    }
    catch (const std::out_of_range &oor)
    {
        if (pEngineInstanceForLog)
        {
            pEngineInstanceForLog->EngineStdOut("Error in helper_parseFloatNumToFixed2 (out_of_range): " + std::string(oor.what()) + " for string '" + s + "'", 2);
        }
        return std::nan(""); // 변환 실패 시 NaN 반환
    }
}

void Entity::PenState::setPenDown(bool down, float currentStageX, float currentStageY)
{
    isPenDown = down;
    if (isPenDown)
    {
        lastStagePosition = {currentStageX, currentStageY};
    }
}

void Entity::PenState::updatePositionAndDraw(float newStageX, float newStageY)
{
    if (!stop && isPenDown && pEngine)
    { // 그리기 조건: 중지되지 않았고(!stop) 펜이 내려져 있을 때
        SDL_FPoint targetStagePosJSStyle = {newStageX, newStageY * -1.0f};
        pEngine->engineDrawLineOnStage(lastStagePosition, targetStagePosJSStyle, color, 1.0f);
    }
    lastStagePosition = {newStageX, newStageY};
}

void Entity::PenState::reset(float currentStageX, float currentStageY)
{
    lastStagePosition = {currentStageX, currentStageY};
}

void Entity::DialogState::clear()
{
    isActive = false;
    text.clear();
    type.clear();
    if (textTexture)
    {
        SDL_DestroyTexture(textTexture);
        textTexture = nullptr;
    }
    startTimeMs = 0;
    // durationMs = 0; // Old member
    totalDurationMs = 0;        // Clear total duration
    remainingDurationMs = 0.0f; // Clear remaining duration    needsRedraw = true;
    // bubbleScreenRect and tailVertices don't need explicit clearing here,
    // they are recalculated when the dialog becomes active.
}

Entity::Entity(Engine *engine, const std::string &entityId, const std::string &entityName,
               double initial_x, double initial_y, double initial_regX, double initial_regY,
               double initial_scaleX, double initial_scaleY, double initial_rotation, double initial_direction,
               int initial_width, int initial_height, bool initial_visible, Entity::RotationMethod initial_rotationMethod) // timedMoveState 추가
    : pEngineInstance(engine), id(entityId), name(entityName),
      x(initial_x), y(initial_y), regX(initial_regX), regY(initial_regY),
      scaleX(initial_scaleX), scaleY(initial_scaleY), rotation(initial_rotation), direction(initial_direction),
      width(initial_width), height(initial_height), visible(initial_visible), rotateMethod(initial_rotationMethod),
      brush(engine), paint(engine), /*visible(initial_visible),*/ timedMoveObjState(), timedRotationState(),
      m_effectBrightness(0.0), // 0: 원본 밝기
      m_effectAlpha(1.0),      // 1.0: 완전 불투명,
      m_effectHue(0.0)         // 0: 색조 변경 없음
{                              // Initialize PenState members
    // 초기스케일 복사
    OrigineScaleX = initial_scaleX;
    OrigineScaleY = initial_scaleY;
}

Entity::~Entity() = default;

void Entity::setScriptWait(const std::string &executionThreadId, Uint64 endTime, const std::string &blockId, WaitType type)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    auto &threadState = scriptThreadStates[executionThreadId]; // Get or create
    threadState.isWaiting = true;
    threadState.waitEndTime = endTime; // For EXPLICIT_WAIT_SECOND, this is the absolute time
    threadState.blockIdForWait = blockId;
    threadState.currentWaitType = type;
    // resumeAtBlockIndex, scriptPtrForResume, sceneIdAtDispatchForResume are set by executeScript when it decides to pause
}

bool Entity::isScriptWaiting(const std::string &executionThreadId) const
{
    // This function is often called to check if a script *should* pause.
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    auto it = scriptThreadStates.find(executionThreadId);
    if (it != scriptThreadStates.end())
    {
        return it->second.isWaiting;
    }
    return false;
}
// OperandValue 구조체 및 블록 처리 함수들은 BlockExecutor.h에 선언되어 있고,
// BlockExecutor.cpp에 구현되어 있으므로 Entity.cpp에서 중복 선언/정의할 필요가 없습니다.

void Entity::executeScript(const Script *scriptPtr, const std::string &executionThreadId, const std::string &sceneIdAtDispatch, float deltaTime)
{
    if (!pEngineInstance)
    {
        std::cerr << "ERROR: Entity " << id << " has no valid Engine instance for script execution (Thread: " << executionThreadId << ")." << std::endl;
        return;
    }
    if (!scriptPtr)
    {
        pEngineInstance->EngineStdOut("executeScript called with null script pointer for object: " + id, 2, executionThreadId);
        return;
    }

    size_t startIndex = 1;                                        // 기본 시작 인덱스 (0번 블록은 이벤트 트리거)
    std::string blockIdToResumeOnLog;                             // 로그용 변수
    WaitType waitTypeToResumeOnLog = WaitType::NONE;              // 로그용 변수
    {                                                             // Lock scope for initial state check and modification
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex); // Critical section for threadState access
        // 스레드 상태 가져오기 (없으면 생성)
        auto &threadState = scriptThreadStates[executionThreadId];

        // If resuming from a BLOCK_INTERNAL wait, clear the isWaiting flag for this specific type of wait.
        // The block itself will set it again if it still needs more time.
        // 이 블록이 여전히 시간이 필요하면 다시 BLOCK_INTERNAL 대기를 설정할 것임.
        if (threadState.isWaiting && threadState.currentWaitType == WaitType::BLOCK_INTERNAL)
        {
            if (threadState.resumeAtBlockIndex >= 1 && threadState.resumeAtBlockIndex < scriptPtr->blocks.size())
            {
                if (threadState.blockIdForWait != scriptPtr->blocks[threadState.resumeAtBlockIndex].id)
                {
                    pEngineInstance->EngineStdOut("Mismatch in blockIdForWait (" + threadState.blockIdForWait +
                                                      ") and block at resumeAtBlockIndex (" + scriptPtr->blocks[threadState.resumeAtBlockIndex].id +
                                                      ") for " + id + ". Clearing wait.",
                                                  3, executionThreadId);
                }
            }
            // Only clear isWaiting if it was for BLOCK_INTERNAL.
            // Other wait types (like EXPLICIT_WAIT_SECOND) are cleared by their respective handlers (e.g., performActiveWait)
            threadState.isWaiting = false;
            // threadState.currentWaitType = WaitType::NONE; // Also reset type
        }

        // If resumeAtBlockIndex is set, it means we are resuming an existing script execution.
        if (threadState.resumeAtBlockIndex >= 1 && threadState.resumeAtBlockIndex < scriptPtr->blocks.size())
        {
            startIndex = threadState.resumeAtBlockIndex;
            blockIdToResumeOnLog = threadState.blockIdForWait;   // 로그용으로 변수 저장
            waitTypeToResumeOnLog = threadState.currentWaitType; // 로그용으로 변수 저장
            // Don't reset resumeAtBlockIndex here yet. It's used by the loop below.
            // It will be reset if the script completes or if a new wait is set.
        }
        else
        {                                        // Not resuming, or invalid resume index, so start from default.
            threadState.resumeAtBlockIndex = -1; // Ensure it's reset if it was invalid
        }
    } // End lock scope

    if (startIndex > 1)
    { // 실제로 재개하는 경우에만 로그 출력
        pEngineInstance->EngineStdOut("Resuming script for " + id + " at block index " + std::to_string(startIndex) +
                                          " (Block ID: " + blockIdToResumeOnLog + ", Type: " + BlockTypeEnumToString(waitTypeToResumeOnLog) + ")",
                                      5, executionThreadId);
    }

    for (size_t i = startIndex; i < scriptPtr->blocks.size(); ++i)
    {
        { // Scope for checking terminateRequested before executing any block
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
            auto it_thread_state = scriptThreadStates.find(executionThreadId);
            if (it_thread_state != scriptThreadStates.end() && it_thread_state->second.terminateRequested)
            {
                pEngineInstance->EngineStdOut("Script thread " + executionThreadId + " for entity " + this->id + " is terminating as requested before block " + scriptPtr->blocks[i].id, 0, executionThreadId);
                // Clean up the state for this thread before returning
                it_thread_state->second.isWaiting = false;
                it_thread_state->second.resumeAtBlockIndex = -1;
                it_thread_state->second.blockIdForWait = "";
                it_thread_state->second.loopCounters.clear(); // loopCounter 대신 loopCounters 사용
                it_thread_state->second.currentWaitType = WaitType::NONE;
                it_thread_state->second.scriptPtrForResume = nullptr;
                it_thread_state->second.sceneIdAtDispatchForResume = "";
                // The terminateRequested flag remains true.
                return; // Stop executing this script
            }
        }

        // 엔진 종료 또는 씬 변경 시 스크립트 중단 로직 (기존과 동일)
        std::string currentEngineSceneId = pEngineInstance->getCurrentSceneId();
        const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
        bool isGlobalEntity = (objInfo && (objInfo->sceneId == "global" || objInfo->sceneId.empty()));

        if (pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
        {
            pEngineInstance->EngineStdOut("Script execution cancelled due to engine shutdown for entity: " + this->getId(), 1, executionThreadId);
            return;
        }
        if (currentEngineSceneId != sceneIdAtDispatch && !isGlobalEntity)
        {
            pEngineInstance->EngineStdOut("Script execution for entity " + this->id + " (Block: " + scriptPtr->blocks[i].type + ") halted. Scene changed from " + sceneIdAtDispatch + " to " + currentEngineSceneId + ".", 1, executionThreadId);
            return;
        }
        if (!isGlobalEntity && objInfo && objInfo->sceneId != currentEngineSceneId)
        {
            pEngineInstance->EngineStdOut("Script execution for entity " + this->id + " (Block: " + scriptPtr->blocks[i].type + ") halted. Entity no longer in current scene " + currentEngineSceneId + ".", 1, executionThreadId);
            return;
        }

        const Block &block = scriptPtr->blocks[i];
        // 각 블록 실행 시 로깅은 성능에 영향을 줄 수 있으므로, 디버그 시에만 활성화하거나 로그 레벨 조정
        // pEngineInstance->EngineStdOut("  Executing Block ID: " + block.id + ", Type: " + block.type + " for object: " + id, 3, executionThreadId); // LEVEL 5 -> 3

        try
        {
            Moving(block.type, *pEngineInstance, this->id, block, executionThreadId, deltaTime);
            Calculator(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Looks(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Sound(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Variable(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Function(block.type, *pEngineInstance, this->id, block, executionThreadId);
            TextBox(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Event(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Flow(block.type, *pEngineInstance, this->id, block, executionThreadId, sceneIdAtDispatch, deltaTime);
        }
        catch (const ScriptBlockExecutionError &sbee)
        {
            throw;
        }
        catch (const std::exception &e)
        {
            throw ScriptBlockExecutionError("Error during script block execution in entity.", block.id, block.type, this->id, e.what());
        }

        // 블록 실행 후 대기 상태 확인 (뮤텍스로 보호)
        {
            // std::unique_lock<std::mutex> lock(m_stateMutex); // Use std::lock_guard if lock is held for the whole scope
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex); // Critical section
            // scriptThreadStates에서 현재 스레드 상태를 다시 가져와야 할 수 있음 (setScriptWait에서 변경되었으므로)
            auto &currentThreadState = scriptThreadStates[executionThreadId];

            if (currentThreadState.isWaiting)
            {
                // 공통 "대기 처리 진입" 로그
                pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                  ") - Entering wait handling for block " + block.id + " (Type: " + block.type +
                                                  "). CurrentWaitType: " + BlockTypeEnumToString(currentThreadState.currentWaitType),
                                              3, executionThreadId);

                if (currentThreadState.currentWaitType == WaitType::BLOCK_INTERNAL)
                {
                    currentThreadState.resumeAtBlockIndex = i;                         // 현재 블록에서 재개
                    currentThreadState.scriptPtrForResume = scriptPtr;                 // 현재 실행 중인 스크립트의 포인터
                    currentThreadState.sceneIdAtDispatchForResume = sceneIdAtDispatch; // 스크립트가 시작된 씬 ID

                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                      ") - Wait (BLOCK_INTERNAL) for block " + block.id +
                                                      " initiated. Pausing script. Will resume at index " + std::to_string(i) + ".",
                                                  3, executionThreadId);
                    // 공통 "반환" 로그
                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                      ") - Returning from executeScript to free worker thread (BLOCK_INTERNAL).",
                                                  3, executionThreadId);
                    return; // 현재 프레임의 스크립트 실행을 여기서 중단
                }
                else if (currentThreadState.currentWaitType == WaitType::EXPLICIT_WAIT_SECOND ||
                         currentThreadState.currentWaitType == WaitType::TEXT_INPUT ||
                         currentThreadState.currentWaitType == WaitType::SOUND_FINISH)
                {
                    currentThreadState.resumeAtBlockIndex = i + 1;                     // Resume at the next block
                    currentThreadState.scriptPtrForResume = scriptPtr;                 // Store for resume
                    currentThreadState.sceneIdAtDispatchForResume = sceneIdAtDispatch; // Store for resume

                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                      ") - Wait (" + BlockTypeEnumToString(currentThreadState.currentWaitType) +
                                                      ") for block " + block.id + " initiated. Pausing script. Will resume at index " + std::to_string(i + 1) + ".",
                                                  3, executionThreadId);
                    // 공통 "반환" 로그
                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                      ") - Returning from executeScript to free worker thread (EXPLICIT/TEXT/SOUND).",
                                                  3, executionThreadId);
                    return; // 현재 프레임의 스크립트 실행을 여기서 중단
                }
                // 다른 유형의 대기가 추가된다면 여기에 처리 로직 추가
            }
        } // 뮤텍스 범위 끝
        // 대기 상태가 아니거나, EXPLICIT_WAIT_SECOND가 완료된 경우 다음 블록으로 진행 (i++)
    }

    // 모든 블록 실행 완료
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        auto &threadState = scriptThreadStates[executionThreadId]; // 뮤텍스 하에서 threadState 다시 참조
        // 스크립트가 정상적으로 끝까지 실행된 경우, 스레드 상태 초기화
        threadState.isWaiting = false;
        threadState.resumeAtBlockIndex = -1; // 스크립트 완료 시 재개 인덱스 초기화
        threadState.blockIdForWait = "";
        threadState.loopCounters.clear(); // Reset loop counter as well
        threadState.currentWaitType = WaitType::NONE;
        threadState.scriptPtrForResume = nullptr;
        threadState.sceneIdAtDispatchForResume = "";
    }
    pEngineInstance->EngineStdOut("Script for object " + id + " completed all blocks.", 5, executionThreadId);
}

// ... (Entity.h에 추가할 BlockTypeEnumToString 헬퍼 함수 선언 예시)
// namespace EntityHelper { std::string BlockTypeEnumToString(Entity::WaitType type); }
// Entity.cpp에 구현:
string BlockTypeEnumToString(Entity::WaitType type)
{
    switch (type)
    {
    case Entity::WaitType::NONE:
        return "NONE";
    case Entity::WaitType::EXPLICIT_WAIT_SECOND:
        return "EXPLICIT_WAIT_SECOND";
    case Entity::WaitType::BLOCK_INTERNAL:
        return "BLOCK_INTERNAL";
    case Entity::WaitType::TEXT_INPUT:
        return "TEXT_INPUT";
    case Entity::WaitType::SOUND_FINISH:
        return "SOUND_FINISH";
    default:
        return "UNKNOWN_WAIT_TYPE";
    }
}
const std::string &Entity::getId() const { return id; }
const std::string &Entity::getName() const { return name; }

double Entity::getX() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return x;
}
double Entity::getY() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return y;
}
double Entity::getRegX() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return regX;
}
double Entity::getRegY() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return regY;
}
double Entity::getScaleX() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return scaleX;
}
double Entity::getScaleY() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return scaleY;
}
double Entity::getRotation() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return rotation;
}
double Entity::getDirection() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return direction;
}
double Entity::getWidth() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return width;
}
double Entity::getHeight() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return height;
}
bool Entity::isVisible() const
{
    // std::lock_guard<std::recursive_mutex> lock(m_stateMutex); // Lock removed
    return visible.load(std::memory_order_relaxed);
}

SDL_FRect Entity::getVisualBounds() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    // 엔티티의 현재 위치(x, y는 중심점), 크기, 스케일을 기반으로 경계 상자를 계산합니다.
    // 회전은 이 예제에서 고려하지 않았습니다. 필요시 추가 구현이 필요합니다.
    // 엔트리 좌표계 (Y축 위쪽) 및 중심점 기준입니다.
    double actualWidth = width * scaleX;
    double actualHeight = height * scaleY;

    SDL_FRect bounds;
    // 좌상단 x, y 계산
    bounds.x = static_cast<float>(x - actualWidth / 2.0);
    // 엔트리 좌표계에서는 y가 위로 갈수록 크므로, 좌상단 y는 y + height/2 입니다.
    // 하지만 SDL_FRect는 일반적으로 y가 아래로 갈수록 크므로, 변환이 필요할 수 있습니다.
    // 여기서는 Stage 좌표계 (Y 위쪽)를 그대로 사용한다고 가정하고,
    // SDL 렌더링 시점에서 Y축을 뒤집는다고 가정합니다.
    bounds.y = static_cast<float>(y - actualHeight / 2.0); // Stage 좌표계의 좌하단 y
    bounds.w = static_cast<float>(actualWidth);
    bounds.h = static_cast<float>(actualHeight);
    return bounds;
}

void Entity::setX(double newX)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    x = newX;
}
void Entity::setY(double newY)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    y = newY;
}
void Entity::setRegX(double newRegX)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    regX = newRegX;
}
void Entity::setRegY(double newRegY)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    regY = newRegY;
}
void Entity::setScaleX(double newScaleX)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!m_isClone)
    {
        scaleX = newScaleX;
    }
}
void Entity::setScaleY(double newScaleY)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!m_isClone)
    {
        scaleY = newScaleY;
    }
}
void Entity::setRotation(double newRotation)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    rotation = newRotation;
}

void Entity::setDirection(double newDirection)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    // 방향 업데이트
    direction = newDirection;

    // 현재 스케일 값의 절대값을 유지하면서 부호만 변경하기 위함
    double currentScaleXMagnitude = std::abs(scaleX);
    double currentScaleYMagnitude = std::abs(scaleY);

    // 방향 값을 [0, 360) 범위로 정규화
    double normalizedAngle = std::fmod(newDirection, 360.0);
    if (normalizedAngle < 0.0)
    {
        normalizedAngle += 360.0;
    }

    // 회전 방식에 따른 스프라이트 반전 처리
    if (rotateMethod == RotationMethod::HORIZONTAL)
    {
        // normalizedAngle이 [0, 180) 범위면 오른쪽으로 간주하여 scaleX를 양수로,
        // [180, 360) 범위면 왼쪽으로 간주하여 scaleX를 음수로 설정합니다.
        if (normalizedAngle < 180.0)
        {
            scaleX = currentScaleXMagnitude;
        }
        else
        {
            scaleX = -currentScaleXMagnitude;
        }
    }
    else if (rotateMethod == RotationMethod::VERTICAL)
    {
        // normalizedAngle이 [90, 270) 범위면 아래쪽으로 간주하여 scaleY를 음수로,
        // 그 외 ([0, 90) U [270, 360)) 범위면 위쪽으로 간주하여 scaleY를 양수로 설정합니다.
        if (normalizedAngle >= 90.0 && normalizedAngle < 270.0)
        {
            scaleY = -currentScaleYMagnitude;
        }
        else
        {
            scaleY = currentScaleYMagnitude;
        }
    }
    // RotationMethod::FREE 또는 NONE의 경우, 방향(direction)에 따라 scale을 변경하지 않습니다.
    // FREE 회전은 rotation 속성으로 처리됩니다.
}
void Entity::setWidth(double newWidth)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    // 엔티티의 내부 width 값을 업데이트합니다.
    // 이 값은 충돌 감지 등에 사용될 수 있으며, scaleX와 함께 시각적 크기를 결정합니다.
    this->width = newWidth;

    if (pEngineInstance)
    {
        const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
        if (objInfo && !objInfo->costumes.empty())
        {
            const Costume *selectedCostume = nullptr;
            const std::string &currentCostumeId = objInfo->selectedCostumeId;

            for (const auto &costume_ref : objInfo->costumes)
            {
                if (costume_ref.id == currentCostumeId)
                {
                    selectedCostume = &costume_ref;
                    break;
                }
            }

            if (selectedCostume) // Check selectedCostume first
            {
                pEngineInstance->EngineStdOut("DEBUG: setWidth for " + this->id + ", costume '" + selectedCostume->name + "'. imageHandle: " + (selectedCostume->imageHandle ? "VALID_PTR" : "NULL_PTR"), 3);
                if (selectedCostume->imageHandle) // Then check imageHandle
                {
                    float texW = 0, texH = 0;
                    SDL_ClearError(); // Clear previous SDL errors
                    int texture_size_result = SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH);

                    if (texture_size_result == 0) // SDL3: 0 on success
                    {
                        pEngineInstance->EngineStdOut("DEBUG: SDL_GetTextureSize success for " + selectedCostume->name + ". texW: " + std::to_string(texW) + ", texH: " + std::to_string(texH), 3);
                        if (texW > 0.00001f)
                        { // 0으로 나누기 방지 (매우 작은 값보다 클 때)
                            this->scaleX = newWidth / static_cast<double>(texW);
                        }
                        else
                        {
                            this->scaleX = 1.0; // 원본 텍스처 너비가 0이면 스케일 1로 설정 (오류 상황)
                            pEngineInstance->EngineStdOut("Warning: Original texture width is 0 for costume '" + selectedCostume->name + "' of entity '" + this->id + "'. Cannot accurately set scaleX.", 1);
                            pEngineInstance->EngineStdOut("DEBUG: Calculated scaleX: " + std::to_string(this->scaleX) + " (newWidth: " + std::to_string(newWidth) + ", texW: " + std::to_string(texW) + ")", 3);
                        }
                    }
                    else
                    {
                        const char *err = SDL_GetError();
                        std::string sdlErrorString = (err && err[0] != '\0') ? err : "None (or error string was empty)";
                        pEngineInstance->EngineStdOut("Warning: Could not get texture size for costume '" + selectedCostume->name + "' of entity '" + this->id + "'. SDL_GetTextureSize result: " + std::to_string(texture_size_result) + ", SDL Error: " + sdlErrorString + ". ScaleX not changed.", 1);
                        if (err && err[0] != '\0')
                            SDL_ClearError();
                    }
                }
                else // selectedCostume->imageHandle is NULL
                {
                    pEngineInstance->EngineStdOut("Warning: selectedCostume->imageHandle is NULL for costume '" + selectedCostume->name + "' of entity " + this->id + ". ScaleX not changed.", 1);
                }
            }
            else
            {
                pEngineInstance->EngineStdOut("Warning: Selected costume is NULL for entity " + this->id + ". ScaleX not changed.", 1);
            }
        }
        else
        {
            // Only show warning if it's not a textBox, as textBoxes don't rely on costumes for scaling this way.
            if (objInfo && objInfo->objectType != "textBox")
            {
                pEngineInstance->EngineStdOut("Warning: ObjectInfo or costumes not found for sprite entity " + this->id + ". ScaleX not changed.", 1);
            }
            else if (!objInfo)
            {
                // ObjectInfo itself is missing, which is a more general issue.
                pEngineInstance->EngineStdOut("Warning: ObjectInfo not found for entity " + this->id + ". ScaleX not changed.", 1);
            }
        }
    }
}

void Entity::setHeight(double newHeight)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    this->height = newHeight; // 내부 height 값 업데이트

    if (pEngineInstance)
    { // setWidth와 유사한 로직으로 scaleY 업데이트
        const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
        if (objInfo && objInfo->objectType == "textBox")
        {
            // For textBoxes, scaleY should typically be 1.0 unless explicitly set.
            // Direct height setting shouldn't derive scaleY from costumes.
            return; // Skip costume-based scaling for textBox
        }

        if (objInfo && !objInfo->costumes.empty())
        {
            const Costume *selectedCostume = nullptr;
            const std::string &currentCostumeId = objInfo->selectedCostumeId;
            for (const auto &costume_ref : objInfo->costumes)
            {
                if (costume_ref.id == currentCostumeId)
                {
                    selectedCostume = &costume_ref;
                    break;
                }
            }
            if (selectedCostume && selectedCostume->imageHandle)
            {
                float texW = 0, texH = 0;
                if (SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH) == true)
                {
                    if (texH > 0.00001f)
                    { // 0으로 나누기 방지
                        this->scaleY = newHeight / static_cast<double>(texH);
                    }
                    else
                    {
                        this->scaleY = 1.0;
                        pEngineInstance->EngineStdOut("Warning: Original texture height is 0 for costume '" + selectedCostume->name + "' of entity '" + this->id + "'. Cannot accurately set scaleY.", 1);
                    }
                }
                else
                {
                    const char *err = SDL_GetError();
                    std::string sdlErrorString = (err && err[0] != '\0') ? err : "None";
                    pEngineInstance->EngineStdOut("Warning: Could not get texture size for costume '" + selectedCostume->name + "' of entity '" + this->id + "'. SDL Error: " + sdlErrorString + ". ScaleY not changed.", 1);
                    if (err && err[0] != '\0')
                        SDL_ClearError();
                }
            }
            else
            {
                pEngineInstance->EngineStdOut("Warning: Selected costume or image handle not found for entity " + this->id + ". ScaleY not changed.", 1);
            }
        }
        else
        {
            if (objInfo && objInfo->objectType != "textBox")
            {
                pEngineInstance->EngineStdOut("Warning: ObjectInfo or costumes not found for sprite entity " + this->id + ". ScaleY not changed.", 1);
            }
            else if (!objInfo)
            {
                pEngineInstance->EngineStdOut("Warning: ObjectInfo not found for entity " + this->id + ". ScaleY not changed.", 1);
            }
        }
    }
}
void Entity::setVisible(bool newVisible)
{
    // std::lock_guard<std::recursive_mutex> lock(m_stateMutex); // Lock removed
    visible.store(newVisible, std::memory_order_relaxed);
}

Entity::RotationMethod Entity::getRotateMethod() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return rotateMethod;
}

void Entity::setRotateMethod(RotationMethod method)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    rotateMethod = method;
}

bool Entity::isPointInside(double pX, double pY) const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

    if (!visible || m_effectAlpha < 0.1)
    { // 투명도가 90% 이상이면 클릭 불가
        return false;
    }

    // 엔티티의 중심을 (0,0)으로 하는 로컬 좌표계로 변환
    double localPX = pX - this->x;
    double localPY = pY - this->y;
    // 글상자 타입의 경우, 회전 및 복잡한 등록점 계산을 건너뛰고 단순 사각형 충돌 판정
    // Entity 생성 시 objectType을 멤버로 저장했다고 가정합니다.
    // if (this->objectType == "textBox") // Entity에 objectType 멤버가 있다고 가정
    // 또는 pEngineInstance를 통해 ObjectInfo 조회 (성능 및 뮤텍스 고려 필요)
    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (objInfo && objInfo->objectType == "textBox")
    {
        // 글상자는 회전하지 않고, 스케일은 1.0으로 가정합니다.
        // this->width와 this->height는 실제 텍스트의 크기를 반영해야 합니다.
        double halfWidth = this->width / 2.0;
        double halfHeight = this->height / 2.0; // Y축이 위로 향하므로, 아래쪽은 -halfHeight, 위쪽은 +halfHeight

        bool inX = (localPX >= -halfWidth && localPX <= halfWidth);
        // Y축 방향 고려: 엔트리 좌표계는 Y가 위로 갈수록 증가. localPY도 위로 갈수록 증가.
        // 따라서 로컬 Y 경계는 [-halfHeight, halfHeight]
        bool inY = (localPY >= -halfHeight && localPY <= halfHeight);
        return inX && inY;
    }

    // SDL 각도는 시계 방향이므로, 점을 객체 프레임으로 가져오려면 반시계 방향(-rotation)으로 회전
    double angleRad = -this->rotation * (SDL_PI_D / 180.0);
    double rotatedPX = localPX * std::cos(angleRad) - localPY * std::sin(angleRad);
    double rotatedPY = localPX * std::sin(angleRad) + localPY * std::cos(angleRad);

    // rotatedPX, rotatedPY는 엔티티의 로컬 회전은 풀렸지만, 여전히 월드 공간 기준의 스케일.
    // 이를 엔티티의 고유 스케일(scaleX, scaleY로 나눈 값)로 변환하여
    // 엔티티의 원본 크기(width, height) 및 등록점(regX, regY) 기준 경계와 비교.

    const double epsilon = 1e-9; // 부동 소수점 비교를 위한 작은 값

    double checkPX = rotatedPX;
    double checkPY = rotatedPY;

    // X축 스케일 처리
    if (std::abs(this->scaleX) < epsilon)
    { // X축 스케일이 거의 0인 경우
        if (std::abs(this->width) > epsilon)
            return false; // 원본 너비가 있는데 스케일이 0이면 클릭 불가 (선)
        // 너비도 0이면 (점 또는 수직선), rotatedPX가 등록점의 X 위치와 일치해야 함.
        // 로컬 좌표계에서 등록점은 (0,0)이므로, rotatedPX가 0에 가까워야 함.
        if (std::abs(rotatedPX) > epsilon)
            return false;
        // checkPX는 -this->regX와 비교될 것이므로, 해당 값으로 설정 (width=0일 때 halfWidth=0, left/rightBound = -regX)
        checkPX = -this->regX;
    }
    else
    {
        checkPX /= this->scaleX;
    }

    // Y축 스케일 처리
    if (std::abs(this->scaleY) < epsilon)
    { // Y축 스케일이 거의 0인 경우
        if (std::abs(this->height) > epsilon)
            return false; // 원본 높이가 있는데 스케일이 0이면 클릭 불가 (선)
        // 높이도 0이면 (점 또는 수평선), rotatedPY가 등록점의 Y 위치와 일치해야 함.
        if (std::abs(rotatedPY) > epsilon)
            return false;
        checkPY = -this->regY;
    }
    else
    {
        checkPY /= this->scaleY;
    }

    // 이제 checkPX, checkPY는 엔티티의 1x1 스케일 기준 로컬 좌표.
    // 경계는 엔티티의 원본 크기(width, height)와 등록점(regX, regY)을 기준으로 계산.
    // (checkPX, checkPY)는 등록점을 원점으로 하는 좌표.
    // this->regX, this->regY가 코스튬의 좌상단 (0,0)으로부터 등록점까지의 오프셋이라고 가정.
    // 엔진 전체적으로 Y축은 위쪽을 향함. this->regY도 이 규칙을 따른다고 가정.
    // (예: 코스튬 높이가 100이고, 등록점이 코스튬의 가장 위쪽 가장자리에 있다면 regY는 100 또는 0, 가장 아래쪽이면 0 또는 -100 등, 기준점에 따라 달라짐)
    // 여기서는 this->regY가 코스튬의 좌상단(Y축 기준 0)으로부터 Y축 위쪽 방향으로의 오프셋이라고 가정.
    // 즉, 코스튬의 좌상단은 (checkPX, checkPY) 좌표계에서 (-this->regX, -this->regY)에 해당. (이 가정은 일반적이지 않음)

    // 일반적인 경우: this->regX, this->regY는 코스튬의 좌상단(0,0)을 기준으로 한 등록점의 위치.
    // 코스튬의 Y좌표는 아래로 증가한다고 가정하고, checkPY는 위로 증가.
    // 코스튬의 좌상단은 (checkPX, checkPY) 좌표계에서 (-this->regX, this->regY) 에 위치.
    // 코스튬의 우하단은 (checkPX, checkPY) 좌표계에서 (this->width - this->regX, this->regY - this->height) 에 위치.
    double leftBound = -this->regX;
    double rightBound = this->width - this->regX;
    double bottomBound = this->regY - this->height; // (regY가 코스튬 상단 Y좌표일 때)
    double topBound = this->regY;                   // (regY가 코스튬 상단 Y좌표일 때)

    // 스케일 조정된 점(checkPX, checkPY)이 원본 크기 기준 경계 내에 있는지 확인 (부동소수점 오차 감안)
    bool inX = (checkPX >= leftBound - epsilon && checkPX <= rightBound + epsilon);
    bool inY = (checkPY >= bottomBound - epsilon && checkPY <= topBound + epsilon);

    return inX && inY;
}
Entity::CollisionSide Entity::getLastCollisionSide() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return lastCollisionSide;
}

void Entity::setLastCollisionSide(CollisionSide side)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    lastCollisionSide = side;
}
// processMathematicalBlock, Moving, Calculator 등의 함수 구현도 여기에 유사하게 이동/정의해야 합니다.
// 간결성을 위해 전체 구현은 생략합니다. BlockExecutor.cpp의 내용을 참고하세요.
// -> 이 주석은 이제 유효하지 않습니다. 해당 함수들은 BlockExecutor.cpp에 있습니다.

void Entity::showDialog(const std::string &message, const std::string &dialogType, Uint64 duration)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    m_currentDialog.clear();

    m_currentDialog.text = message.empty() ? "    " : message;
    m_currentDialog.type = dialogType;
    m_currentDialog.isActive = true;
    // m_currentDialog.durationMs = duration; // Old member
    m_currentDialog.totalDurationMs = duration;
    m_currentDialog.remainingDurationMs = static_cast<float>(duration);
    m_currentDialog.startTimeMs = SDL_GetTicks(); // Keep startTime for reference if needed
    m_currentDialog.needsRedraw = true;

    if (pEngineInstance)
    {
        pEngineInstance->EngineStdOut("Entity " + id + " dialog: '" + m_currentDialog.text + "', type: " + m_currentDialog.type + ", duration: " + std::to_string(duration), 3);
    }
}

void Entity::removeDialog()
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (m_currentDialog.isActive)
    {
        m_currentDialog.clear();
    }
}

void Entity::updateDialog(float deltaTime) // deltaTime is in seconds
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (m_currentDialog.isActive && m_currentDialog.totalDurationMs > 0) // Check totalDurationMs to see if it's a timed dialog
    {
        m_currentDialog.remainingDurationMs -= (deltaTime * 1000.0f); // Convert deltaTime to ms and decrement
        if (m_currentDialog.remainingDurationMs <= 0.0f)
        {
            // removeDialog()는 내부적으로 m_stateMutex를 다시 잠그려고 시도할 수 있습니다.
            // 이미 m_stateMutex가 잠겨있는 상태이므로, clear()를 직접 호출하거나
            // removeDialog()의 내부 로직을 여기에 직접 구현하는 것이 좋습니다.
            // 여기서는 clear()를 직접 호출하는 것으로 변경합니다.
            m_currentDialog.clear(); // Time's up, clear the dialog
        }
    }
}
bool Entity::hasActiveDialog() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_currentDialog.isActive;
}

std::string Entity::getWaitingBlockId(const std::string &executionThreadId) const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    auto it = scriptThreadStates.find(executionThreadId);
    if (it != scriptThreadStates.end() && it->second.isWaiting)
    {
        return it->second.blockIdForWait;
    }
    return "";
}

Entity::WaitType Entity::getCurrentWaitType(const std::string &executionThreadId) const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    auto it = scriptThreadStates.find(executionThreadId);
    if (it != scriptThreadStates.end() && it->second.isWaiting)
    {
        return it->second.currentWaitType;
    }
    return WaitType::NONE;
}

// Effect Getters and Setters
double Entity::getEffectBrightness() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_effectBrightness;
}
void Entity::setEffectBrightness(double brightness)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    m_effectBrightness = std::clamp(brightness, -100.0, 100.0);
}

double Entity::getEffectAlpha() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_effectAlpha;
}
void Entity::setEffectAlpha(double alpha)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    m_effectAlpha = std::clamp(alpha, 0.0, 1.0);
}

double Entity::getEffectHue() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_effectHue;
}
void Entity::setEffectHue(double hue)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    m_effectHue = std::fmod(hue, 360.0);
    if (m_effectHue < 0)
        m_effectHue += 360.0;
}

void Entity::playSound(const std::string &soundId)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

    if (!pEngineInstance)
    {
        // std::cerr << "ERROR: Entity " << id << " has no pEngineInstance to play sound." << std::endl;
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo)
    {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile : objInfo->sounds)
    {
        if (soundFile.id == soundId)
        {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay)
    {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu)
        {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        }
        else
        {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSound(this->getId(), soundFilePath); // Engine의 public aeHelper 사용
        pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);
    }
    else
    {
        pEngineInstance->EngineStdOut("Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}

void Entity::playSoundWithSeconds(const std::string &soundId, double seconds)
{
    // std::unique_lock<std::recursive_mutex> lock(m_stateMutex); // Lock removed for async wait

    if (!pEngineInstance)
    {
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo)
    {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile : objInfo->sounds)
    {
        if (soundFile.id == soundId)
        {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay)
    {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu)
        {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        }
        else
        {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSoundForDuration(this->getId(), soundFilePath, seconds); // Engine의 public aeHelper 사용
        pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);
    }
    else
    {
        pEngineInstance->EngineStdOut("Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}
void Entity::playSoundWithFromTo(const std::string &soundId, double from, double to)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

    if (!pEngineInstance)
    {
        // std::cerr << "ERROR: Entity " << id << " has no pEngineInstance to play sound." << std::endl;
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo)
    {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile : objInfo->sounds)
    {
        if (soundFile.id == soundId)
        {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay)
    {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu)
        {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        }
        else
        {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSoundFromTo(this->getId(), soundFilePath, from, to); // Engine의 public aeHelper 사용
        pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);
    }
    else
    {
        pEngineInstance->EngineStdOut("Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}
void Entity::waitforPlaysound(const std::string &soundId)
{
    std::unique_lock<std::recursive_mutex> lock(m_stateMutex);

    if (!pEngineInstance)
    {
        // std::cerr << "ERROR: Entity " << id << " has no pEngineInstance to play sound." << std::endl;
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo)
    {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile : objInfo->sounds)
    {
        if (soundFile.id == soundId)
        {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay)
    {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu)
        {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        }
        else
        {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSound(this->getId(), soundFilePath);
        pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);

        // 중요: currentExecutionThreadId와 callingBlockId를 올바르게 가져와야 합니다.
        // 이 함수를 호출하는 컨텍스트에서 이 정보들을 전달받아야 합니다.
        std::string currentExecutionThreadId = "placeholder_thread_id"; // 실제 실행 스레드 ID로 대체 필요
        std::string callingBlockId = "placeholder_block_id";            // 실제 호출 블록 ID로 대체 필요
        // 예시: if (this->scriptThreadStates.count(currentExecutionThreadId)) { ... }

        if (!currentExecutionThreadId.empty() && !callingBlockId.empty() && currentExecutionThreadId != "placeholder_thread_id")
        {
            setScriptWait(currentExecutionThreadId, 0, callingBlockId, WaitType::SOUND_FINISH);
            pEngineInstance->EngineStdOut("Entity " + id + " (Thread: " + currentExecutionThreadId + ") waiting for sound: " + soundToPlay->name + " (Block: " + callingBlockId + ")", 0, currentExecutionThreadId);
        }
        else
        {
            pEngineInstance->EngineStdOut("Entity " + id + " could not set sound wait due to missing/placeholder thread/block ID. Sound will play without wait.", 1);
            // 이전의 블로킹 대기 방식은 제거되었으므로, ID를 모르면 대기 없이 진행됩니다.
            // 또는, 여기서 에러를 발생시키거나 기본 블로킹 대기를 수행할 수 있지만, 비동기 패턴을 권장합니다.
        }
    }
    else
    {
        pEngineInstance->EngineStdOut("Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}
void Entity::waitforPlaysoundWithSeconds(const std::string &soundId, double seconds)
{
    // std::unique_lock<std::recursive_mutex> lock(m_stateMutex); // Lock removed for initial part

    if (!pEngineInstance)
    {
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo)
    {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile : objInfo->sounds)
    {
        if (soundFile.id == soundId)
        {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay)
    {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu)
        {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        }
        else
        {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSoundForDuration(this->getId(), soundFilePath, seconds); // Engine의 public aeHelper 사용
        // pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);
        pEngineInstance->EngineStdOut("Entity " + id + " playing sound for " + std::to_string(seconds) + "s: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);

        // 중요: currentExecutionThreadId와 callingBlockId를 올바르게 가져와야 합니다.
        std::string currentExecutionThreadId = "placeholder_thread_id"; // 실제 실행 스레드 ID로 대체 필요
        std::string callingBlockId = "placeholder_block_id";            // 실제 호출 블록 ID로 대체 필요

        if (!currentExecutionThreadId.empty() && !callingBlockId.empty() && currentExecutionThreadId != "placeholder_thread_id")
        {
            setScriptWait(currentExecutionThreadId, 0, callingBlockId, WaitType::SOUND_FINISH);
            pEngineInstance->EngineStdOut("Entity " + id + " (Thread: " + currentExecutionThreadId + ") waiting for sound (timed): " + soundToPlay->name + " (Block: " + callingBlockId + ")", 0, currentExecutionThreadId);
        }
        else
        {
            pEngineInstance->EngineStdOut("Entity " + id + " could not set timed sound wait due to missing/placeholder thread/block ID. Sound will play for duration without script pause.", 1);
        }
    }
    else
    {
        pEngineInstance->EngineStdOut("Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}

void Entity::waitforPlaysoundWithFromTo(const std::string &soundId, double from, double to)
{
    std::unique_lock<std::recursive_mutex> lock(m_stateMutex);

    if (!pEngineInstance)
    {
        // std::cerr << "ERROR: Entity " << id << " has no pEngineInstance to play sound." << std::endl;
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo)
    {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile : objInfo->sounds)
    {
        if (soundFile.id == soundId)
        {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay)
    {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu)
        {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        }
        else
        {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSound(this->getId(), soundFilePath);

        // 중요: currentExecutionThreadId와 callingBlockId를 올바르게 가져와야 합니다.
        // 이 정보는 이 함수를 호출하는 컨텍스트(예: BlockExecutor)에서 전달받아야 합니다.
        // 아래는 임시 플레이스홀더입니다. 실제 구현에서는 이 부분을 수정해야 합니다.
        std::string currentExecutionThreadId = "";         // 실제 실행 스레드 ID로 대체 필요
        std::string callingBlockId = "unknown_wait_block"; // 실제 호출 블록 ID로 대체 필요

        // 스레드 ID를 얻기 위한 임시 로직 (실제로는 더 안정적인 방법 필요)
        // 예시: executeScript에서 현재 스레드 ID를 ScriptThreadState에 저장하고 여기서 읽어옴
        // 또는, 이 함수에 executionThreadId와 blockId를 인자로 전달
        {
            std::lock_guard<std::recursive_mutex> read_lock(m_stateMutex);
            // 가장 최근에 생성된 스레드 ID를 사용하려고 시도 (매우 불안정하며, 실제 사용 부적합)
            if (!scriptThreadStates.empty())
            {
                // currentExecutionThreadId = scriptThreadStates.rbegin()->first; // 예시일 뿐, 사용 금지
            }
        }

        if (!currentExecutionThreadId.empty() && !callingBlockId.empty())
        {
            setScriptWait(currentExecutionThreadId, 0, callingBlockId, WaitType::SOUND_FINISH);
            pEngineInstance->EngineStdOut("Entity " + id + " (Thread: " + currentExecutionThreadId + ") waiting for sound: " + soundToPlay->name + " (Block: " + callingBlockId + ")", 0, currentExecutionThreadId);
        }
        else
        {
            pEngineInstance->EngineStdOut("Entity " + id + " could not set sound wait due to missing thread/block ID. Sound will play without wait.", 1);
            // 이전의 블로킹 대기 방식은 제거되었으므로, ID를 모르면 대기 없이 진행됩니다.
        }
    }
    else
    {
        pEngineInstance->EngineStdOut("Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}

void Entity::terminateScriptThread(const std::string &threadId)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    auto it = scriptThreadStates.find(threadId);
    if (it != scriptThreadStates.end())
    {
        it->second.terminateRequested = true;
        if (pEngineInstance)
        { // Check pEngineInstance before using
            pEngineInstance->EngineStdOut("Entity " + id + " marked script thread " + threadId + " for termination.", 0, threadId);
        }
    }
    else
    {
        if (pEngineInstance)
        {
            pEngineInstance->EngineStdOut("Entity " + id + " could not find script thread " + threadId + " to mark for termination.", 1, threadId);
        }
    }
}

void Entity::terminateAllScriptThread(const std::string &exceptThreadId)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    int markedCount = 0;
    for (auto &pair : scriptThreadStates)
    {
        if (exceptThreadId.empty() || pair.first != exceptThreadId)
        {
            if (!pair.second.terminateRequested)
            { // Only mark if not already marked
                pair.second.terminateRequested = true;
                markedCount++;
                if (pEngineInstance)
                {
                    pEngineInstance->EngineStdOut("Entity " + id + " marked script thread " + pair.first + " for termination (all/other).", 0, pair.first);
                }
            }
        }
    }
    if (pEngineInstance && markedCount > 0)
    { // Log only if any threads were actually marked now
        pEngineInstance->EngineStdOut("Entity " + id + " marked " + std::to_string(markedCount) + " script threads for termination" + (exceptThreadId.empty() ? "" : " (excluding " + exceptThreadId + ")") + ".", 0);
    }
}
void Entity::processInternalContinuations(float deltaTime)
{
    if (!pEngineInstance || pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
    {
        return;
    }

    std::vector<std::tuple<std::string, const Script *, std::string>> tasksToRunInline;

    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        for (auto it_state = scriptThreadStates.begin(); it_state != scriptThreadStates.end(); /* manual increment */)
        {
            auto &execId = it_state->first;
            auto &state = it_state->second;

            if (state.isWaiting && state.currentWaitType == WaitType::BLOCK_INTERNAL)
            {
                const ObjectInfo *objInfoCheck = pEngineInstance->getObjectInfoById(this->getId());
                bool isGlobal = (objInfoCheck && (objInfoCheck->sceneId == "global" || objInfoCheck->sceneId.empty()));
                std::string engineCurrentScene = pEngineInstance->getCurrentSceneId();
                const std::string &scriptSceneContext = state.sceneIdAtDispatchForResume;

                bool canResume = false;
                if (!state.scriptPtrForResume)
                { // 스크립트 포인터가 유효하지 않으면 재개 불가
                    canResume = false;
                    pEngineInstance->EngineStdOut("WARNING: Entity " + getId() + " script thread " + execId + " is BLOCK_INTERNAL wait but scriptPtrForResume is null. Clearing wait.", 1, execId);
                }
                else if (isGlobal)
                {
                    canResume = true;
                }
                else
                {
                    if (objInfoCheck && objInfoCheck->sceneId == scriptSceneContext)
                    {
                        if (engineCurrentScene == scriptSceneContext)
                        {
                            canResume = true;
                        }
                    }
                }

                if (canResume)
                {
                    tasksToRunInline.emplace_back(execId, state.scriptPtrForResume, scriptSceneContext);
                    // tasksToRunInline에 추가했으므로, 여기서는 isWaiting을 false로 바꾸지 않습니다.
                    // executeScript가 호출될 때 isWaiting이 false로 설정됩니다.
                    // 만약 executeScript가 BLOCK_INTERNAL을 다시 설정하면 다음 틱에 다시 처리됩니다.
                    ++it_state;
                }
                else
                {
                    // 재개할 수 없는 경우, 대기 상태를 해제하여 무한 루프 방지
                    if (state.scriptPtrForResume)
                    { // 로그는 스크립트 포인터가 있을 때만 의미 있음
                        pEngineInstance->EngineStdOut("Internal continuation for " + getId() + " (Thread: " + execId + ") cancelled. Scene/Context mismatch or invalid script. EntityScene: " + (objInfoCheck ? objInfoCheck->sceneId : "N/A") + ", ScriptDispatchScene: " + scriptSceneContext + ", EngineCurrentScene: " + engineCurrentScene, 1, execId);
                    }
                    state.isWaiting = false;
                    state.currentWaitType = WaitType::NONE;
                    state.scriptPtrForResume = nullptr;
                    state.sceneIdAtDispatchForResume = "";
                    // 상태가 변경되었으므로, 다음 반복에서 이 스레드를 다시 처리할 필요 없음
                    // 만약 반복 중 요소를 제거한다면 it_state = scriptThreadStates.erase(it_state); 와 같이 처리해야 하지만, 여기서는 상태만 변경
                    ++it_state;
                }
            }
            else
            {
                ++it_state;
            }
        }
    } // Mutex scope ends

    // 수집된 작업을 현재 스레드에서 직접 실행
    for (const auto &taskDetails : tasksToRunInline)
    {
        const std::string &execId = std::get<0>(taskDetails);
        const Script *scriptToRun = std::get<1>(taskDetails);
        const std::string &sceneIdForRun = std::get<2>(taskDetails);
        // pEngineInstance->EngineStdOut("Executing internal continuation for entity: " + getId() + " (Thread: " + execId + ")", 5, execId);

        try
        {
            this->executeScript(scriptToRun, execId, sceneIdForRun, deltaTime);
        }
        catch (const ScriptBlockExecutionError &sbee)
        {
            Entity *entitiyInfo = pEngineInstance->getEntityById(sbee.entityId);
            Omocha::BlockTypeEnum blockTypeEnum = Omocha::stringToBlockTypeEnum(sbee.blockType);  // sbee.blockType을 사용해야 합니다.
            std::string koreanBlockTypeName = Omocha::blockTypeEnumToKoreanString(blockTypeEnum); // 변환된 enum 사용

            // Helper lambda to truncate strings safely
            auto truncate_string = [](const std::string &str, size_t max_len)
            {
                if (str.length() > max_len)
                {
                    return str.substr(0, max_len) + "...(truncated)";
                }
                return str;
            };

            const size_t MAX_ID_LEN = 128;   // Max length for IDs/types in error messages
            const size_t MAX_MSG_LEN = 512;  // Max length for original message part
            const size_t MAX_NAME_LEN = 256; // Max length for entity name

            std::string entityNameStr = "[정보 없음]";
            if (entitiyInfo)
            {
                entityNameStr = truncate_string(entitiyInfo->getName(), MAX_NAME_LEN);
            }
            else
            {
                entityNameStr = "[객체 ID: " + truncate_string(sbee.entityId, MAX_ID_LEN) + " 찾을 수 없음]";
            }

            std::string detailedErrorMessage = "블럭 을 실행하는데 오류가 발생하였습니다. 블럭ID " + truncate_string(sbee.blockId, MAX_ID_LEN) +
                                               " 의 타입 " + truncate_string(koreanBlockTypeName, MAX_ID_LEN) +            // koreanBlockTypeName 사용
                                               (blockTypeEnum == Omocha::BlockTypeEnum::UNKNOWN && !sbee.blockType.empty() // blockTypeEnum 사용
                                                    ? " (원본: " + truncate_string(sbee.blockType, MAX_ID_LEN) + ")"       // sbee.blockType 사용
                                                    : "") +
                                               " 에서 사용 하는 객체 " + "(" + entityNameStr + ")" +
                                               "\n원본 오류: " + truncate_string(sbee.originalMessage, MAX_MSG_LEN);

            pEngineInstance->EngineStdOut("Script Execution Error (InternalContinuation, Thread " + execId + "): " + detailedErrorMessage, 2, execId);
            if (pEngineInstance->showMessageBox("블럭 처리 오류\n" + detailedErrorMessage, pEngineInstance->msgBoxIconType.ICON_ERROR))
            {
                exit(EXIT_FAILURE);
            }
            // 프로그램 종료 여부는 상위 정책에 따름 (여기서는 Engine::dispatchScriptForExecution의 예외 처리와 유사하게)
        }
        catch (const std::exception &e)
        {
            // Helper lambda to truncate strings safely
            auto truncate_string = [](const std::string &str, size_t max_len)
            {
                if (str.length() > max_len)
                {
                    return str.substr(0, max_len) + "...(truncated)";
                }
                return str;
            };

            const size_t MAX_ID_LEN = 128;  // Max length for entity/thread IDs in error messages
            const size_t MAX_MSG_LEN = 512; // Max length for exception message part

            std::string entityIdStr = truncate_string(getId(), MAX_ID_LEN);
            std::string threadIdStr = truncate_string(execId, MAX_ID_LEN); // execId is the thread ID
            std::string exceptionWhatStr = truncate_string(e.what(), MAX_MSG_LEN);

            // Construct the detailed error message using truncated parts
            std::string detailedErrorMessage = "Generic exception caught in internal continuation for entity " + entityIdStr +
                                               " (Thread: " + threadIdStr + "): " + exceptionWhatStr;

            pEngineInstance->EngineStdOut(detailedErrorMessage, 2, execId);
        }
        catch (...)
        {
            pEngineInstance->EngineStdOut(
                "Unknown exception caught in internal continuation for entity " + getId() +
                    " (Thread: " + execId + ")",
                2, execId);
        }
    }
}
void Entity::resumeExplicitWaitScripts(float deltaTime)
{
    if (!pEngineInstance || pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
    {
        return;
    }

    std::vector<std::tuple<std::string, const Script *, std::string, int>> tasksToDispatch; // execId, script, sceneId, resumeIndex

    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        for (auto it = scriptThreadStates.begin(); it != scriptThreadStates.end(); /* no increment here */)
        {
            auto &execId = it->first;
            auto &state = it->second;

            if (state.isWaiting && state.currentWaitType == WaitType::EXPLICIT_WAIT_SECOND)
            {
                if (SDL_GetTicks() >= state.waitEndTime)
                {
                    if (state.scriptPtrForResume && state.resumeAtBlockIndex != -1)
                    {
                        pEngineInstance->EngineStdOut("Entity " + id + " (Thread: " + execId + ") finished EXPLICIT_WAIT_SECOND for block " + state.blockIdForWait + ". Resuming.", 0, execId);
                        tasksToDispatch.emplace_back(execId, state.scriptPtrForResume, state.sceneIdAtDispatchForResume, state.resumeAtBlockIndex);

                        state.isWaiting = false;
                        state.waitEndTime = 0;
                        state.currentWaitType = WaitType::NONE;
                        // blockIdForWait, scriptPtrForResume, sceneIdAtDispatchForResume, resumeAtBlockIndex will be used by the dispatched task or reset by executeScript.
                        ++it;
                    }
                    else
                    {
                        pEngineInstance->EngineStdOut("WARNING: Entity " + id + " script thread " + execId + " EXPLICIT_WAIT_SECOND finished but missing resume context. Clearing wait.", 1, execId);
                        state.isWaiting = false;
                        state.currentWaitType = WaitType::NONE;
                        state.scriptPtrForResume = nullptr;
                        state.sceneIdAtDispatchForResume = "";
                        state.resumeAtBlockIndex = -1;
                        ++it;
                    }
                }
                else
                {
                    ++it; // Still waiting
                }
            }
            else
            {
                ++it;
            }
        }
    }

    for (const auto &task : tasksToDispatch)
    {
        const std::string &execId = std::get<0>(task);
        const Script *scriptToRun = std::get<1>(task);
        const std::string &sceneIdForRun = std::get<2>(task);
        int resumeIndex = std::get<3>(task);

        // The resumeAtBlockIndex is set in the thread's state when executeScript is called by scheduleScriptExecutionOnPool
        // if existingExecutionThreadId is provided.
        this->scheduleScriptExecutionOnPool(scriptToRun, sceneIdForRun, deltaTime, execId);
    }
}

void Entity::resumeSoundWaitScripts(float deltaTime)
{
    if (!pEngineInstance || pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
    {
        return;
    }

    std::vector<std::tuple<std::string, const Script *, std::string, int>> tasksToDispatch; // execId, script, sceneId, resumeIndex

    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        for (auto it = scriptThreadStates.begin(); it != scriptThreadStates.end(); /* no increment here */)
        {
            auto &execId = it->first;
            auto &state = it->second;

            if (state.isWaiting && state.currentWaitType == WaitType::SOUND_FINISH)
            {
                // Check if the sound associated with this entity (and potentially this specific wait) has finished.
                // AudioEngineHelper::isSoundPlaying(this->getId()) checks if *any* sound for this entity is playing.
                // For more precise control, you might need to store a specific sound handle/ID in ScriptThreadState.
                if (!pEngineInstance->aeHelper.isSoundPlaying(this->getId()))
                {
                    if (state.scriptPtrForResume && state.resumeAtBlockIndex != -1)
                    {
                        pEngineInstance->EngineStdOut("Entity " + id + " (Thread: " + execId + ") finished SOUND_FINISH for block " + state.blockIdForWait + ". Resuming.", 0, execId);
                        tasksToDispatch.emplace_back(execId, state.scriptPtrForResume, state.sceneIdAtDispatchForResume, state.resumeAtBlockIndex);

                        state.isWaiting = false;
                        state.currentWaitType = WaitType::NONE;
                        // blockIdForWait, scriptPtrForResume, etc., will be used by the dispatched task or reset by executeScript.
                        ++it;
                    }
                    else
                    {
                        pEngineInstance->EngineStdOut("WARNING: Entity " + id + " script thread " + execId + " SOUND_FINISH finished but missing resume context. Clearing wait.", 1, execId);
                        state.isWaiting = false;
                        state.currentWaitType = WaitType::NONE;
                        // Clear other relevant state fields
                        ++it;
                    }
                }
                else
                {
                    ++it; // Still waiting for sound to finish
                }
            }
            else
            {
                ++it;
            }
        }
    }

    for (const auto &task : tasksToDispatch)
    {
        const std::string &execId = std::get<0>(task);
        // Reschedule the script execution.
        this->scheduleScriptExecutionOnPool(std::get<1>(task), std::get<2>(task), deltaTime, execId);
    }
}

void Entity::scheduleScriptExecutionOnPool(const Script *scriptPtr,
                                           const std::string &sceneIdAtDispatch,
                                           float deltaTime,
                                           const std::string &existingExecutionThreadId)
{
    if (!pEngineInstance)
    {
        // This error can occur if the Entity was not created correctly.
        // In actual production, a more robust logging/error handling mechanism may be needed.
        std::cerr << "CRITICAL ERROR: Entity " << this->id
                  << " has no valid pEngineInstance. Cannot schedule script execution." << std::endl; // LCOV_EXCL_LINE
        // In addition to simple console output, it is recommended to use the Engine's logger if available.
        // (However, direct calls are not possible as pEngineInstance is null)
        return;
    }

    if (!scriptPtr)
    {
        pEngineInstance->EngineStdOut("Entity::scheduleScriptExecutionOnPool - null script pointer for entity: " + this->id, 2, existingExecutionThreadId);
        return;
    }

    // 새 스크립트의 경우, 실행할 블록이 있는지 확인합니다.
    // (첫 번째 블록은 보통 이벤트 트리거이므로, 1개 초과여야 실행 가능)
    if (existingExecutionThreadId.empty() && (scriptPtr->blocks.empty() || scriptPtr->blocks.size() <= 1))
    {
        pEngineInstance->EngineStdOut(
            "Entity::scheduleScriptExecutionOnPool - Script for entity " + this->id + " has no executable blocks. Skipping.", 1, existingExecutionThreadId);
        return;
    }

    std::string execIdToUse;
    bool isResumedScript = !existingExecutionThreadId.empty();

    if (isResumedScript)
    {
        execIdToUse = existingExecutionThreadId;
    }
    else
    {
        // 새 스크립트 실행을 위한 ID 생성
        // 이전 Engine::dispatchScriptForExecution의 ID 생성 로직을 따르되, Engine의 카운터를 추가하여 고유성을 강화합니다.
        // 참고: 이 ID 생성 방식은 호출 스레드의 ID 해시를 사용하므로, 동시에 여러 스크립트가 시작될 경우 완벽한 고유성을 보장하지 않을 수 있습니다.
        // 더 강력한 고유 ID가 필요하다면 UUID 등의 사용을 고려해야 합니다.
        std::thread::id physical_thread_id = std::this_thread::get_id();
        std::stringstream ss_full_hex;
        ss_full_hex << std::hex << std::hash<std::thread::id>{}(physical_thread_id);
        std::string full_hex_str = ss_full_hex.str();
        std::string short_hex_str;

        if (full_hex_str.length() >= 4)
        {
            short_hex_str = full_hex_str.substr(0, 4);
        }
        else
        {
            short_hex_str = std::string(4 - full_hex_str.length(), '0') + full_hex_str;
        }
        // Engine의 카운터를 사용하여 ID의 고유성을 더욱 강화합니다.
        execIdToUse = "script_" + short_hex_str + "_" + std::to_string(pEngineInstance->getNextScriptExecutionCounter());
    }

    // 스레드 풀에 작업을 게시합니다.
    // 람다 함수는 필요한 변수들을 캡처합니다. pEngineInstance는 this를 통해 접근 가능합니다.
    pEngineInstance->submitTask([self = shared_from_this(), scriptPtr, sceneIdAtDispatch, deltaTime, execIdToUse, isResumedScript]()

                                {
                          if (self->pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
                          {
                              // 엔진 종료 중이면 스크립트 실행을 취소합니다. (선택적 로깅)
                              // self->pEngineInstance->EngineStdOut("Script execution for " + self->id + " (Thread: " + execIdToUse + ") cancelled due to engine shutdown.", 1, execIdToUse);
                              return;
                          }

                          // 스크립트 시작 또는 재개 로깅
                          if (isResumedScript)
                          {
                              self->pEngineInstance->EngineStdOut("Entity " + self->getId() + " resuming script (Thread: " + execIdToUse + ")", 5, execIdToUse);
                          }
                          else
                          {
                              self->pEngineInstance->EngineStdOut("Entity " + self->getId() + " starting new script (Thread: " + execIdToUse + ")", 5, execIdToUse);
                          }

                          try
                          {
                              // 실제 스크립트 실행
                              self->executeScript(scriptPtr, execIdToUse, sceneIdAtDispatch, deltaTime);
                          }
                          catch (const ScriptBlockExecutionError &sbee)
                          {
                             Entity *entity = self->pEngineInstance->getEntityById(sbee.entityId);
                              // Handle script block execution errors
                              Omocha::BlockTypeEnum blockTypeEnum = Omocha::stringToBlockTypeEnum(sbee.blockType);
                              std::string koreanBlockTypeName = Omocha::blockTypeEnumToKoreanString(blockTypeEnum);
                              // Configure error message (sbee.entityId could be the entity referenced by the block where the error occurred,
                              // so explicitly stating self->id for the entity executing the script might be clearer.)
                              std::string detailedErrorMessage = "블록 을 실행하는데 오류가 발생하였습니다.\n(스크립트 소유 객체: " + self->getId() +
                                                                " 블록ID: " + sbee.blockId +
                                                                 ") 의 타입 (" + koreanBlockTypeName +")"+
                                                                 (blockTypeEnum == Omocha::BlockTypeEnum::UNKNOWN && !sbee.blockType.empty()
                                                                      ? " (원본: " + sbee.blockType + ")"
                                                                      : "") +
                                                                 " 에서 사용 하는 객체 (" + entity->getName() +   // Object ID directly referenced by the error block
                                                                 ")\n원본 오류: " + sbee.originalMessage;

                              // EngineStdOut은 이미 상세 메시지를 포함하므로, 여기서는 요약된 메시지 또는 상세 메시지 그대로 사용
                              self->pEngineInstance->EngineStdOut("Script Execution Error (Entity: " + self->getId() + ", Thread " + execIdToUse + "): " + detailedErrorMessage, 2, execIdToUse);
                              self->pEngineInstance->showMessageBox(detailedErrorMessage, self->pEngineInstance->msgBoxIconType.ICON_ERROR);
                              exit(EXIT_FAILURE); // 프로그램 종료
                          }
                          catch (const std::length_error &le)
                          { // Specifically catch std::length_error
                              self->pEngineInstance->EngineStdOut(
                                  "std::length_error caught in script for entity " + self->getId() +
                                      " (Thread: " + execIdToUse + "): " + le.what(),
                                  2, execIdToUse);
                              // Optionally, show a message box or perform other error handling
                              // self->pEngineInstance->showMessageBox("문자열 처리 중 오류가 발생했습니다: " + std::string(le.what()), self->pEngineInstance->msgBoxIconType.ICON_ERROR);
                          }
                          catch (const std::exception &e)
                          {
                              // Handle general C++ exceptions
                              self->pEngineInstance->EngineStdOut(
                                  "Generic exception caught in script for entity " + self->getId() +
                                      " (Thread: " + execIdToUse + "): " + e.what(),
                                  2, execIdToUse);
                          }
                          catch (...)
                          {
                              // Handle other unknown exceptions
                              self->pEngineInstance->EngineStdOut(
                                  "Unknown exception caught in script for entity " + self->getId() +
                                      " (Thread: " + execIdToUse + ")",
                                  2, execIdToUse);
                          } });
}

void Entity::setText(const std::string &newText)
{
    if (pEngineInstance)
    {
        std::lock_guard<std::recursive_mutex> lock(pEngineInstance->m_engineDataMutex);
        // Engine 클래스를 통해 ObjectInfo의 textContent를 업데이트합니다.
        pEngineInstance->updateEntityTextContent(this->id, newText);
    }
    else
    {
        // pEngineInstance가 null인 경우 오류 로깅 (이 경우는 거의 없어야 함)
        std::cerr << "Error: Entity " << id << " has no pEngineInstance to set text." << std::endl;
    }
}

void Entity::appendText(const std::string &textToAppend)
{
    if (pEngineInstance)
    {
        std::lock_guard<std::recursive_mutex> lock(pEngineInstance->m_engineDataMutex);
        // Engine 클래스를 통해 ObjectInfo의 textContent를 가져와서 업데이트합니다.
        // 이 방식은 ObjectInfo가 Engine 내부에만 존재하고 Entity가 직접 접근하지 않는 경우에 적합합니다.
        // 만약 Entity가 ObjectInfo의 복사본을 가지고 있다면, 해당 복사본을 직접 수정해야 합니다.
        // 현재 Engine::updateEntityTextContent는 전체 텍스트를 설정하므로,
        // 기존 텍스트를 가져와서 이어붙이는 로직이 필요합니다.

        // 1. 현재 ObjectInfo 가져오기
        ObjectInfo *objInfo = const_cast<ObjectInfo *>(pEngineInstance->getObjectInfoById(this->id));
        if (objInfo)
        {
            if (objInfo->objectType == "textBox")
            {
                std::string currentText = objInfo->textContent;
                std::string newText = currentText + textToAppend;
                pEngineInstance->updateEntityTextContent(this->id, newText); // 업데이트된 전체 텍스트로 설정
            }
            else
            {
                pEngineInstance->EngineStdOut("Warning: Entity " + id + " is not a textBox. Cannot append text.", 1);
            }
        }
        else
        {
            pEngineInstance->EngineStdOut("Warning: ObjectInfo not found for entity " + id + " when trying to append text.", 1);
        }
    }
    else
    {
        std::cerr << "Error: Entity " << id << " has no pEngineInstance to append text." << std::endl;
    }
}

void Entity::prependText(const std::string &prependToText)
{
    if (pEngineInstance)
    {
        std::lock_guard<std::recursive_mutex> lock(pEngineInstance->m_engineDataMutex);
        // Engine 클래스를 통해 ObjectInfo의 textContent를 가져와서 업데이트합니다.
        // 이 방식은 ObjectInfo가 Engine 내부에만 존재하고 Entity가 직접 접근하지 않는 경우에 적합합니다.
        // 만약 Entity가 ObjectInfo의 복사본을 가지고 있다면, 해당 복사본을 직접 수정해야 합니다.

        // 1. 현재 ObjectInfo 가져오기
        ObjectInfo *objInfo = const_cast<ObjectInfo *>(pEngineInstance->getObjectInfoById(this->id));
        if (objInfo)
        {
            if (objInfo->objectType == "textBox")
            {
                std::string currentText = objInfo->textContent;
                std::string newText = prependToText + currentText;
                pEngineInstance->updateEntityTextContent(this->id, newText); // 업데이트된 전체 텍스트로 설정
            }
            else
            {
                pEngineInstance->EngineStdOut("Warning: Entity " + id + " is not a textBox. Cannot append text.", 1);
            }
        }
        else
        {
            pEngineInstance->EngineStdOut("Warning: ObjectInfo not found for entity " + id + " when trying to append text.", 1);
        }
    }
    else
    {
        std::cerr << "Error: Entity " << id << " has no pEngineInstance to append text." << std::endl;
    }
}
/**
 * @brief 사이즈 가져오기 (엔트리)
 * @param toFixedSize 자리수
 */
double Entity::getSize(bool toFixedSize) const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    // 스테이지 너비를 기준으로 현재 엔티티의 시각적 너비가 차지하는 비율을 계산합니다.
    // this->width는 엔티티의 원본 이미지 너비입니다.
    // this->getScaleX()는 원본 이미지 너비에 대한 스케일 팩터입니다.
    // 따라서 (this->width * std::abs(this->getScaleX())) 가 현재 시각적 너비입니다.

    double visualWidth = this->width * std::abs(this->getScaleX());
    double stageWidth = static_cast<double>(Engine::getProjectstageWidth()); // Engine 클래스에서 스테이지 너비 가져오기
    if (pEngineInstance)
    { // 디버깅 로그 추가
        pEngineInstance->EngineStdOut(format("Entity::getSize - Debug - visualWidth: {}, stageWidth: {}, this->width: {}, this->getScaleX(): {}", visualWidth, stageWidth, this->width, this->getScaleX()), 3);
    }
    double current_percentage = 0.0;
    current_percentage = std::abs(this->getScaleX()) * 100.0; 

    // pEngineInstance가 유효한지 확인 후 로그 출력
    if (pEngineInstance)
    {
        // pEngineInstance->EngineStdOut(format("Entity::getSize W={} H={} V={}", visualWidth, visualHeight, value), 3); // 이전 로그
        // pEngineInstance->EngineStdOut(format("Entity::getSize OW={} OH={}", this->getWidth(),this->getHeight()), 3); // 이전 로그
        pEngineInstance->EngineStdOut(format("Entity::getSize - ScaleX: {}, Calculated Percentage: {}", this->getScaleX(), current_percentage), 3);
    }

    if (toFixedSize)
    {
        // 엔트리는 크기 값을 소수점 첫째 자리까지 표시합니다. (예: 123.4%)
        return std::round(current_percentage * 10.0) / 10.0;
    }
    return current_percentage;
}
/**
 * @bref 크기 정하기 (엔트리)
 * @param size 크기
 */
void Entity::setSize(double size)
{
    lock_guard<recursive_mutex> lock(m_stateMutex);
    // 목표 시각적 너비는 (스테이지 너비 * (size / 100.0)) 입니다.
    // 이 목표 시각적 너비를 엔티티의 원본 너비(this->width)로 나누어
    // 새로운 scaleX (및 scaleY)를 계산합니다.

    double stageWidth = static_cast<double>(Engine::getProjectstageWidth()); // Engine 클래스에서 스테이지 너비 가져오기
    double targetVisualWidth = stageWidth * (size / 83.0); // 80.0을 100.0으로 수정

    double newCalculatedScaleX = 1.0; // 기본값
    if (this->width > 0.00001)
    { // 엔티티의 원본 너비가 0이 아닐 때
        newCalculatedScaleX = targetVisualWidth / this->width;
    }
    else
    {
        // 엔티티의 원본 너비가 0인 경우, 스케일 변경이 의미 없거나 오류 상황입니다.
        // 이 경우, 이전처럼 size/100.0을 직접 스케일로 사용하거나, 오류를 로깅하고 스케일을 1로 설정할 수 있습니다.
        // 여기서는 이전 방식을 따르되, 경고를 출력합니다.
        newCalculatedScaleX = size / 100.0; // 직접 스케일로 사용
        if (pEngineInstance)
        {
            pEngineInstance->EngineStdOut(
                "Warning: Entity::setSize - Entity original width is zero. Applying 'size' directly as scale factor.", 1);
        }
    }
    if (!this->m_isClone)
    { // 복제본 이 아닌경우 변경
        this->setScaleX(newCalculatedScaleX);
        this->setScaleY(newCalculatedScaleX); // 가로세로 비율 유지를 위해 동일한 스케일 적용
    }
    if (pEngineInstance)
    { // pEngineInstance 유효성 검사 추가
        pEngineInstance->EngineStdOut(format("Entity::setSize TargetPercentage: {} NewScaleFactor: {}", size, newCalculatedScaleX), 3);
    }
}
/**
 * @bref 크기 리셋 (엔트리)
 */
void Entity::resetSize()
{
    lock_guard<recursive_mutex> lock(m_stateMutex);
    if (!this->m_isClone)
    {
        // 복제본이 아닌 경우, 저장된 원본 스케일로 복원합니다.
        // 이 원본 스케일은 엔티티 생성 시 project.json에서 로드된 값입니다.
        this->setScaleX(this->OrigineScaleX);
        this->setScaleY(this->OrigineScaleY);
    }
}
