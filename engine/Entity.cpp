#include "Entity.h"
#include <iostream>
#include <mutex> // For std::lock_guard
#include <string>
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
               double initial_width, double initial_height, bool initial_visible, Entity::RotationMethod initial_rotationMethod) // timedMoveState 추가
    : pEngineInstance(engine), id(entityId), name(entityName),
      x(initial_x), y(initial_y), regX(initial_regX), regY(initial_regY),
      scaleX(initial_scaleX), scaleY(initial_scaleY), rotation(initial_rotation), direction(initial_direction),
      width(initial_width), height(initial_height), visible(initial_visible), rotateMethod(initial_rotationMethod),
      brush(engine), paint(engine), timedMoveObjState(), timedRotationState(),
      m_effectBrightness(0.0), // 0: 원본 밝기
      m_effectAlpha(1.0),      // 1.0: 완전 불투명,
      m_effectHue(0.0)         // 0: 색조 변경 없음
{                              // Initialize PenState members
}

Entity::~Entity()
{
}

void Entity::setScriptWait(const std::string &executionThreadId, Uint32 endTime, const std::string &blockId, WaitType type)
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

void Entity::performActiveWait(const std::string &executionThreadId, const std::string &waitedBlockId, Uint32 waitEndTime, Engine *pEngine, const std::string &sceneIdAtDispatchForWait)
{
    // This method is called when the mutex is NOT held by this thread.
    pEngine->EngineStdOut("Entity " + id + " (Thread: " + executionThreadId + ") now actively waiting due to block " + waitedBlockId + " (Type: wait_second) until " + std::to_string(waitEndTime), 0, executionThreadId);

    while (SDL_GetTicks() < waitEndTime)
    {
        { // Check terminateRequested
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
            auto it_thread_state = scriptThreadStates.find(executionThreadId);
            if (it_thread_state != scriptThreadStates.end() && it_thread_state->second.terminateRequested)
            {
                pEngine->EngineStdOut("Wait for block " + waitedBlockId + " on entity " + this->id + " (Thread: " + executionThreadId + ") cancelled due to script termination request.", 1, executionThreadId);
                // No need to reset state here, executeScript will handle it upon return
                // The isWaiting flag will remain true, and executeScript will see terminateRequested.
                return; // Exit wait
            }
        }

        if (pEngine->m_isShuttingDown.load(std::memory_order_relaxed))
        {
            pEngine->EngineStdOut("Wait for block " + waitedBlockId + " cancelled due to engine shutdown for entity: " + this->getId(), 1, executionThreadId);
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
            auto it_cleanup = scriptThreadStates.find(executionThreadId);
            if (it_cleanup != scriptThreadStates.end() && it_cleanup->second.blockIdForWait == waitedBlockId)
            {
                // Instead of erase, reset the state for this thread
                it_cleanup->second.isWaiting = false;
                it_cleanup->second.waitEndTime = 0;
                it_cleanup->second.blockIdForWait = "";
                it_cleanup->second.currentWaitType = WaitType::NONE;
                it_cleanup->second.resumeAtBlockIndex = -1; // Ensure no accidental resume
            }
            return;
        }

        // Check for scene change during wait
        std::string currentEngineScene = pEngine->getCurrentSceneId();
        const ObjectInfo *objInfoPtr = pEngine->getObjectInfoById(this->id); // 'this->id' is the entity's ID
        bool isGlobal = (objInfoPtr && (objInfoPtr->sceneId == "global" || objInfoPtr->sceneId.empty()));

        // If the current scene is different from the scene when the script (containing this wait) was dispatched,
        // and this entity is not global, then interrupt the wait.
        if (!isGlobal && currentEngineScene != sceneIdAtDispatchForWait)
        {
            pEngine->EngineStdOut("Wait for block " + waitedBlockId + " on entity " + this->id + " cancelled. Scene changed from " + sceneIdAtDispatchForWait + " to " + currentEngineScene + " during wait.", 1, executionThreadId);
            std::lock_guard<std::recursive_mutex> lock(m_stateMutex); // Re-lock to clean up
            auto it_cleanup = scriptThreadStates.find(executionThreadId);
            if (it_cleanup != scriptThreadStates.end() && it_cleanup->second.blockIdForWait == waitedBlockId)
            {
                // Reset state
                it_cleanup->second.isWaiting = false;
                it_cleanup->second.waitEndTime = 0;
                it_cleanup->second.blockIdForWait = "";
                it_cleanup->second.currentWaitType = WaitType::NONE;
                it_cleanup->second.resumeAtBlockIndex = -1; // Ensure no accidental resume
            }
            return; // Exit wait
        }
        // Also, a more direct check: if the entity itself is no longer part of the *current* scene (e.g., if it was moved or its sceneId property changed)
        // This is a bit redundant if the above check is comprehensive, but can catch edge cases.
        // The main check in executeScript will handle this after the wait if not caught here.
        // For now, the check against sceneIdAtDispatchForWait is the primary one for wait interruption.

        SDL_Delay(1); // CPU 사용량 감소를 위한 짧은 지연
    }

    // Wait finished normally, re-lock to clean up
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    pEngine->EngineStdOut("Entity " + id + " (Thread: " + executionThreadId + ") finished waiting for block " + waitedBlockId, 0, executionThreadId);
    auto it_cleanup = scriptThreadStates.find(executionThreadId);
    // Check if the state is still for the same block and this thread.
    // It's possible another wait_second block for the same thread overwrote the state,
    // but that would mean this waitEndTime was for an older wait.
    // The crucial part is that *a* wait for this thread, identified by waitEndTime, has completed.
    // For robustness, ensure we are clearing the state that corresponds to the waitEndTime we just used.
    if (it_cleanup != scriptThreadStates.end() &&                               // <--- 수정된 부분
        it_cleanup->second.blockIdForWait == waitedBlockId &&                   // Use blockIdForWait
        it_cleanup->second.currentWaitType == WaitType::EXPLICIT_WAIT_SECOND && // Ensure it was this type of wait
        it_cleanup->second.waitEndTime == waitEndTime)
    {
        it_cleanup->second.isWaiting = false;
        it_cleanup->second.waitEndTime = 0;
        it_cleanup->second.blockIdForWait = "";
        it_cleanup->second.currentWaitType = WaitType::NONE;
        // resumeAtBlockIndex should have been set by executeScript before calling performActiveWait
    }
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

    size_t startIndex = 1;                              // 기본 시작 인덱스 (0번 블록은 이벤트 트리거)
    std::string blockIdToResumeOnLog;                   // 로그용 변수
    WaitType waitTypeToResumeOnLog = WaitType::NONE;    // 로그용 변수
    {                                                   // Lock scope for initial state check and modification
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex); // Critical section for threadState access
        // 스레드 상태 가져오기 (없으면 생성)
        auto &threadState = scriptThreadStates[executionThreadId];

        // If resuming from a BLOCK_INTERNAL wait, clear the isWaiting flag for this specific type of wait.
        // The block itself will set it again if it still needs more time.
        // 이 블록이 여전히 시간이 필요하면 다시 BLOCK_INTERNAL 대기를 설정할 것임.
        if (threadState.isWaiting && threadState.currentWaitType == WaitType::BLOCK_INTERNAL)
        {
            bool canClearWait = true;
            if (threadState.resumeAtBlockIndex >= 1 && threadState.resumeAtBlockIndex < scriptPtr->blocks.size())
            {
                if (threadState.blockIdForWait != scriptPtr->blocks[threadState.resumeAtBlockIndex].id)
                {
                    pEngineInstance->EngineStdOut("Warning: Mismatch in blockIdForWait (" + threadState.blockIdForWait +
                                                      ") and block at resumeAtBlockIndex (" + scriptPtr->blocks[threadState.resumeAtBlockIndex].id +
                                                      ") for " + id + ". Clearing wait.",
                                                  1, executionThreadId);
                }
            }
            if (canClearWait)
            {
                // Only clear isWaiting if it was for BLOCK_INTERNAL.
                // Other wait types (like EXPLICIT_WAIT_SECOND) are cleared by their respective handlers (e.g., performActiveWait)
                threadState.isWaiting = false;
                // threadState.currentWaitType = WaitType::NONE; // Also reset type
            }
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
                it_thread_state->second.loopCounter = 0;
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
        pEngineInstance->EngineStdOut("  Executing Block ID: " + block.id + ", Type: " + block.type + " for object: " + id, 3, executionThreadId); // LEVEL 5 -> 3

        try
        {
            Moving(block.type, *pEngineInstance, this->id, block, executionThreadId, deltaTime);
            Calculator(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Looks(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Sound(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Variable(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Function(block.type, *pEngineInstance, this->id, block, executionThreadId);
            TextBox(block.type,*pEngineInstance,this->id,block,executionThreadId);
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
                // 어떤 블록이든 대기 상태를 설정했고, 해당 대기 유형이 현재 프레임에서 스크립트 실행을 중단해야 하는 경우
                // For EXPLICIT_WAIT_SECOND, the performActiveWait would have blocked and then cleared isWaiting.
                // So if isWaiting is true here, it's most likely for BLOCK_INTERNAL or an interrupted explicit wait.

                // Store the index of the *current block* (i) that caused or is part of the wait.
                // This is the block we need to resume *at* or *after*, depending on the wait type.
                currentThreadState.resumeAtBlockIndex = i;

                if (currentThreadState.currentWaitType == WaitType::BLOCK_INTERNAL)
                {
                    // BLOCK_INTERNAL 대기의 경우, 다음 프레임에 resumeInternalBlockScripts가
                    // 이 스크립트를 다시 디스패치할 수 있도록 필요한 정보를 저장합니다.
                    currentThreadState.scriptPtrForResume = scriptPtr;                 // 현재 실행 중인 스크립트의 포인터
                    currentThreadState.sceneIdAtDispatchForResume = sceneIdAtDispatch; // 스크립트가 시작된 씬 ID

                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                      ") - Block " + block.id + " (Type: " + block.type +
                                                      ") set BLOCK_INTERNAL wait. Pausing script at index " + std::to_string(i) + ".",
                                                  1, executionThreadId);
                    return; // 현재 프레임의 스크립트 실행을 여기서 중단
                }
                else if (currentThreadState.currentWaitType == WaitType::EXPLICIT_WAIT_SECOND ||
                         currentThreadState.currentWaitType == WaitType::TEXT_INPUT)
                {
                    // EXPLICIT_WAIT_SECOND나 TEXT_INPUT의 경우, 해당 블록 핸들러(performActiveWait, activateTextInput)가
                    // 스레드를 블로킹합니다. 정상 완료 시 isWaiting은 false가 됩니다.
                    // 만약 isWaiting이 여전히 true라면, 이는 해당 대기가 외부 요인(예: 씬 변경, 엔진 종료)으로 중단되었음을 의미할 수 있습니다.
                    // 이 경우에도 스크립트 실행을 중단하고 다음 틱에서 상태를 재평가합니다.
                    // The resumeAtBlockIndex should point to the *next* block after the wait_second/ask_and_wait.
                    currentThreadState.resumeAtBlockIndex = i + 1; // Next block
                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                      ") - Wait (" + BlockTypeEnumToString(currentThreadState.currentWaitType) +
                                                      ") for block " + block.id + " is still active (likely interrupted). Pausing script at index " + std::to_string(i) + ".",
                                                  1, executionThreadId);
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
        threadState.loopCounter = 0; // Reset loop counter as well
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
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return visible;
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
    scaleX = newScaleX;
}
void Entity::setScaleY(double newScaleY)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    scaleY = newScaleY;
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

            if (selectedCostume && selectedCostume->imageHandle)
            {
                float texW = 0, texH = 0;
                if (SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH) == 0)
                {
                    if (texW > 0.00001f)
                    { // 0으로 나누기 방지 (매우 작은 값보다 클 때)
                        this->scaleX = newWidth / static_cast<double>(texW);
                    }
                    else
                    {
                        this->scaleX = 1.0; // 원본 텍스처 너비가 0이면 스케일 1로 설정 (오류 상황)
                        pEngineInstance->EngineStdOut("Warning: Original texture width is 0 for costume " + selectedCostume->name + " of entity " + this->id + ". Cannot accurately set scaleX.", 1);
                    }
                }
                else
                {
                    pEngineInstance->EngineStdOut("Warning: Could not get texture size for costume " + selectedCostume->name + " of entity " + this->id + ". ScaleX not changed.", 1);
                }
            }
            else
            {
                pEngineInstance->EngineStdOut("Warning: Selected costume or image handle not found for entity " + this->id + ". ScaleX not changed.", 1);
            }
        }
        else
        {
            pEngineInstance->EngineStdOut("Warning: ObjectInfo or costumes not found for entity " + this->id + ". ScaleX not changed.", 1);
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
                if (SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH) == 0)
                {
                    if (texH > 0.00001f)
                    { // 0으로 나누기 방지
                        this->scaleY = newHeight / static_cast<double>(texH);
                    }
                    else
                    {
                        this->scaleY = 1.0;
                        pEngineInstance->EngineStdOut("Warning: Original texture height is 0 for costume " + selectedCostume->name + " of entity " + this->id + ". Cannot accurately set scaleY.", 1);
                    }
                }
                else
                {
                    pEngineInstance->EngineStdOut("Warning: Could not get texture size for costume " + selectedCostume->name + " of entity " + this->id + ". ScaleY not changed.", 1);
                }
            }
            else
            {
                pEngineInstance->EngineStdOut("Warning: Selected costume or image handle not found for entity " + this->id + ". ScaleY not changed.", 1);
            }
        }
        else
        {
            pEngineInstance->EngineStdOut("Warning: ObjectInfo or costumes not found for entity " + this->id + ". ScaleY not changed.", 1);
        }
    }
}
void Entity::resetScaleSize()
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    this->scaleX = 1.0;
    this->scaleY = 1.0;

    double newOriginalWidth = 100.0;
    double newOriginalHeight = 100.0;

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

                if (selectedCostume && selectedCostume->imageHandle)
                {
                    float texW_float = 0, texH_float = 0;
                    if (SDL_GetTextureSize(selectedCostume->imageHandle, &texW_float, &texH_float) == 0)
                    {
                        newOriginalWidth = static_cast<double>(texW_float);
                        newOriginalHeight = static_cast<double>(texH_float);
                    }
                    else
                    {
                        pEngineInstance->EngineStdOut(
                            "Warning: Entity::resetScaleSize - Could not get texture size for costume '" +
                                selectedCostume->name + "' of entity '" + this->id + "'. SDL Error: " + SDL_GetError() +
                                ". Original width/height will be set to 0.",
                            1);
                        newOriginalWidth = 0.0; // 텍스처 데이터 문제 시 0으로 설정
                        newOriginalHeight = 0.0;
                    }
                }
                else
                {
                    pEngineInstance->EngineStdOut(
                        "Warning: Entity::resetScaleSize - Selected costume or image handle not found for entity '" +
                            this->id + "'. Using default original size (100x100).",
                        1);
                }
            }
        }
        else
        {
            pEngineInstance->EngineStdOut(
                "Warning: Entity::resetScaleSize - ObjectInfo or costumes not found for entity '" +
                    this->id + "'. Using default original size (100x100).",
                1);
        }
    }
    else
    {
        std::cerr << "Critical Warning: Entity::resetScaleSize - pEngineInstance is null for entity '" << this->id << "'. Using default original size (100x100)." << std::endl;
    }
    this->width = newOriginalWidth;
    this->height = newOriginalHeight;
}
void Entity::setVisible(bool newVisible)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    visible = newVisible;
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
        rotatedPY >= localMinY && rotatedPY <= localMaxY)
    {
        return true;
    }

    return false;
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
        pEngineInstance->aeHelper.playSound(this->getId(), soundFilePath); // Engine의 public aeHelper 사용
        // pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);

        lock.unlock();
        while (pEngineInstance->aeHelper.isSoundPlaying(this->getId())) // 루프 조건에서 상태를 계속 확인
        {
            // 현재 스레드를 잠시 멈춥니다.
            // 이 작업은 현재 스크립트 실행 스레드를 블로킹합니다.
            // 메인 게임 루프를 블로킹하지 않도록 주의해야 합니다.
            SDL_Delay(2);

            // 엔진 종료 상태 확인
            if (pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
            {
                pEngineInstance->EngineStdOut("Wait for sound cancelled due to engine shutdown for entity: " + this->getId(), 1);
                break; // 대기 루프 종료
            }
        }
        lock.lock(); // std::unique_lock 사용 시, 루프 후 다시 잠금
        pEngineInstance->EngineStdOut("Entity " + id + " finished waiting for sound: " + soundToPlay->name, 0);
    }
    else
    {
        pEngineInstance->EngineStdOut("Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}

void Entity::waitforPlaysoundWithSeconds(const std::string &soundId, double seconds)
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
        pEngineInstance->aeHelper.playSoundForDuration(this->getId(), soundFilePath, seconds); // Engine의 public aeHelper 사용
        // pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);

        lock.unlock();
        while (pEngineInstance->aeHelper.isSoundPlaying(this->getId())) // 루프 조건에서 상태를 계속 확인
        {
            // 현재 스레드를 잠시 멈춥니다.
            // 이 작업은 현재 스크립트 실행 스레드를 블로킹합니다.
            // 메인 게임 루프를 블로킹하지 않도록 주의해야 합니다.
            SDL_Delay(2);

            // 엔진 종료 상태 확인
            if (pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
            {
                pEngineInstance->EngineStdOut("Wait for sound cancelled due to engine shutdown for entity: " + this->getId(), 1);
                break; // 대기 루프 종료
            }
        }
        lock.lock(); // std::unique_lock 사용 시, 루프 후 다시 잠금
        pEngineInstance->EngineStdOut("Entity " + id + " finished waiting for sound: " + soundToPlay->name, 0);
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
        pEngineInstance->aeHelper.playSoundFromTo(this->getId(), soundFilePath, from, to); // Engine의 public aeHelper 사용
        // pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);

        lock.unlock();
        while (pEngineInstance->aeHelper.isSoundPlaying(this->getId())) // 루프 조건에서 상태를 계속 확인
        {
            // 현재 스레드를 잠시 멈춥니다.
            // 이 작업은 현재 스크립트 실행 스레드를 블로킹합니다.
            // 메인 게임 루프를 블로킹하지 않도록 주의해야 합니다.
            SDL_Delay(2);

            // 엔진 종료 상태 확인
            if (pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
            {
                pEngineInstance->EngineStdOut("Wait for sound cancelled due to engine shutdown for entity: " + this->getId(), 1);
                break; // 대기 루프 종료
            }
        }
        lock.lock(); // std::unique_lock 사용 시, 루프 후 다시 잠금
        pEngineInstance->EngineStdOut("Entity " + id + " finished waiting for sound: " + soundToPlay->name, 0);
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
void Entity::resumeInternalBlockScripts(float deltaTime)
{
    if (!pEngineInstance || pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
    {
        return;
    }

    std::vector<std::tuple<std::string, const Script *, std::string>> tasksToDispatch;

    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        for (auto &[execId, state] : scriptThreadStates)
        { // Iterate by reference
            if (state.isWaiting && state.currentWaitType == WaitType::BLOCK_INTERNAL)
            {
                if (state.scriptPtrForResume && !state.sceneIdAtDispatchForResume.empty())
                {
                    tasksToDispatch.emplace_back(execId, state.scriptPtrForResume, state.sceneIdAtDispatchForResume);
                }
                else
                {
                    pEngineInstance->EngineStdOut("WARNING: Entity " + id + " script thread " + execId + " is BLOCK_INTERNAL wait but missing resume context.", 1, execId);
                    state.isWaiting = false;
                    state.currentWaitType = WaitType::NONE;
                    state.scriptPtrForResume = nullptr;
                    state.sceneIdAtDispatchForResume = "";
                }
            }
        }
    }

    // Capture pEngineInstance by value (it's a pointer, so the pointer value is copied)
    // to ensure the lambda uses the Engine instance that was valid at the time of posting.
    for (const auto &task : tasksToDispatch)
    {
        const std::string &execId = std::get<0>(task);
        const Script *scriptToRun = std::get<1>(task);
        const std::string &sceneIdForRun = std::get<2>(task);
        Engine *capturedEnginePtr = pEngineInstance; // Capture for lambda

        capturedEnginePtr->submitTask([this, scriptToRun, execId, sceneIdForRun, deltaTime, capturedEnginePtr]()
                                      {
            if (capturedEnginePtr->m_isShuttingDown.load(std::memory_order_relaxed)) {
                // capturedEnginePtr->EngineStdOut("Resume for " + this->getId() + " (Thread: " + execId + ") cancelled due to engine shutdown.", 1, execId);
                return;
            }

            // Log that the entity is resuming this script thread.
            // This log replaces the one that was in Engine::dispatchScriptForExecution for resumed scripts.
            capturedEnginePtr->EngineStdOut("Worker thread (resumed by Entity) for entity: " + this->getId() + " (Thread: " + execId + ")", 5, execId);

            try {
                this->executeScript(scriptToRun, execId, sceneIdForRun, deltaTime);
            } catch (const ScriptBlockExecutionError &sbee) {
                // Replicate error handling from Engine::dispatchScriptForExecution
                Omocha::BlockTypeEnum blockTypeEnum = Omocha::stringToBlockTypeEnum(sbee.blockType);
                std::string koreanBlockTypeName = Omocha::blockTypeEnumToKoreanString(blockTypeEnum);

                std::string detailedErrorMessage = "블럭 을 실행하는데 오류가 발생하였습니다. 블럭ID " + sbee.blockId +
                                                   " 의 타입 " + koreanBlockTypeName +
                                                   (blockTypeEnum == Omocha::BlockTypeEnum::UNKNOWN && !sbee.blockType.empty()
                                                        ? " (원본: " + sbee.blockType + ")"
                                                        : "") +
                                                   " 에서 사용 하는 객체 " + sbee.entityId +
                                                   "\n원본 오류: " + sbee.originalMessage;

                capturedEnginePtr->EngineStdOut("Script Execution Error (Thread " + execId + "): " + detailedErrorMessage, 2, execId);
                capturedEnginePtr->showMessageBox("오류가 발생했습니다!\n" + detailedErrorMessage, capturedEnginePtr->msgBoxIconType.ICON_ERROR);
            } catch (const std::exception &e) {
                capturedEnginePtr->EngineStdOut(
                    "Generic exception caught in resumed script for entity " + this->getId() +
                    " (Thread: " + execId + "): " + e.what(), 2, execId);
            } catch (...) {
                capturedEnginePtr->EngineStdOut(
                    "Unknown exception caught in resumed script for entity " + this->getId() +
                    " (Thread: " + execId + ")", 2, execId);
            } });
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
    pEngineInstance->submitTask([this, scriptPtr, sceneIdAtDispatch, deltaTime, execIdToUse, isResumedScript]()

                                {
                          if (this->pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed))
                          {
                              // 엔진 종료 중이면 스크립트 실행을 취소합니다. (선택적 로깅)
                              // this->pEngineInstance->EngineStdOut("Script execution for " + this->id + " (Thread: " + execIdToUse + ") cancelled due to engine shutdown.", 1, execIdToUse);
                              return;
                          }

                          // 스크립트 시작 또는 재개 로깅
                          if (isResumedScript)
                          {
                              this->pEngineInstance->EngineStdOut("Entity " + this->id + " resuming script (Thread: " + execIdToUse + ")", 5, execIdToUse);
                          }
                          else
                          {
                              this->pEngineInstance->EngineStdOut("Entity " + this->id + " starting new script (Thread: " + execIdToUse + ")", 5, execIdToUse);
                          }

                          try
                          {
                              // 실제 스크립트 실행
                              this->executeScript(scriptPtr, execIdToUse, sceneIdAtDispatch, deltaTime);
                          }
                          catch (const ScriptBlockExecutionError &sbee)
                          {
                              // Handle script block execution errors
                              Omocha::BlockTypeEnum blockTypeEnum = Omocha::stringToBlockTypeEnum(sbee.blockType);
                              std::string koreanBlockTypeName = Omocha::blockTypeEnumToKoreanString(blockTypeEnum);

                              // Configure error message (sbee.entityId could be the entity referenced by the block where the error occurred,
                              // so explicitly stating this->id for the entity executing the script might be clearer.)
                              std::string detailedErrorMessage = "블럭 을 실행하는데 오류가 발생하였습니다. (스크립트 소유 객체: " + this->id +
                                                                 ") 블럭ID " + sbee.blockId +
                                                                 " 의 타입 " + koreanBlockTypeName +
                                                                 (blockTypeEnum == Omocha::BlockTypeEnum::UNKNOWN && !sbee.blockType.empty()
                                                                      ? " (원본: " + sbee.blockType + ")"
                                                                      : "") +
                                                                 " 에서 사용 하는 객체 " + sbee.entityId + // Object ID directly referenced by the error block
                                                                 "\n원본 오류: " + sbee.originalMessage;

                              this->pEngineInstance->EngineStdOut("Script Execution Error (Entity: " + this->id + ", Thread " + execIdToUse + "): " + detailedErrorMessage, 2, execIdToUse);
                              this->pEngineInstance->showMessageBox("오류가 발생했습니다!\n" + detailedErrorMessage, this->pEngineInstance->msgBoxIconType.ICON_ERROR);
                          }
                          catch (const std::length_error &le)
                          { // Specifically catch std::length_error
                              this->pEngineInstance->EngineStdOut(
                                  "std::length_error caught in script for entity " + this->id +
                                      " (Thread: " + execIdToUse + "): " + le.what(),
                                  2, execIdToUse);
                              // Optionally, show a message box or perform other error handling
                              // this->pEngineInstance->showMessageBox("문자열 처리 중 오류가 발생했습니다: " + std::string(le.what()), this->pEngineInstance->msgBoxIconType.ICON_ERROR);
                          }
                          catch (const std::exception &e)
                          {
                              // Handle general C++ exceptions
                              this->pEngineInstance->EngineStdOut(
                                  "Generic exception caught in script for entity " + this->id +
                                      " (Thread: " + execIdToUse + "): " + e.what(),
                                  2, execIdToUse);
                          }
                          catch (...)
                          {
                              // Handle other unknown exceptions
                              this->pEngineInstance->EngineStdOut(
                                  "Unknown exception caught in script for entity " + this->id +
                                      " (Thread: " + execIdToUse + ")",
                                  2, execIdToUse);
                          } });
}

void Entity::setText(const std::string& newText) {
    if (pEngineInstance) {
         std::lock_guard<std::mutex> lock(pEngineInstance->m_engineDataMutex);
        // Engine 클래스를 통해 ObjectInfo의 textContent를 업데이트합니다.
        pEngineInstance->updateEntityTextContent(this->id, newText);
    } else {
        // pEngineInstance가 null인 경우 오류 로깅 (이 경우는 거의 없어야 함)
        std::cerr << "Error: Entity " << id << " has no pEngineInstance to set text." << std::endl;
    }
}