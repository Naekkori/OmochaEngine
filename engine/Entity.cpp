#include "Entity.h"
#include <iostream>
#include <mutex> // For std::lock_guard
#include <string>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "Engine.h"
#include "SDL3/SDL_pixels.h"
#include "blocks/BlockExecutor.h"
#include "blocks/blockTypes.h"
string THREAD_ID_INTERNAL;
string BLOCK_ID_INTERNAL;

Entity::PenState::PenState(Engine *enginePtr)
    : pEngine(enginePtr),
      stop(false), // 기본적으로 그리기가 중지되지 않은 상태 (활성화)
      isPenDown(false),
      lastStagePosition({0.0f, 0.0f}),
      color{0, 0, 0, 255} {
}

// Helper function to mimic parseFloat(num.toFixed(2))
static double helper_parseFloatNumToFixed2(double num, Engine *pEngineInstanceForLog) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << num; // 소수점 2자리로 포맷
    std::string s = oss.str();
    try {
        return std::stod(s); // 문자열을 double로 변환
    } catch (const std::invalid_argument &ia) {
        if (pEngineInstanceForLog) {
            pEngineInstanceForLog->EngineStdOut(
                "Error in helper_parseFloatNumToFixed2 (invalid_argument): " + std::string(ia.what()) + " for string '"
                + s + "'", 2);
        }
        return std::nan(""); // 변환 실패 시 NaN 반환
    }
    catch (const std::out_of_range &oor) {
        if (pEngineInstanceForLog) {
            pEngineInstanceForLog->EngineStdOut(
                "Error in helper_parseFloatNumToFixed2 (out_of_range): " + std::string(oor.what()) + " for string '" + s
                + "'", 2);
        }
        return std::nan(""); // 변환 실패 시 NaN 반환
    }
}

void Entity::PenState::setPenDown(bool down, float currentStageX, float currentStageY) {
    isPenDown = down;
    if (isPenDown) {
        lastStagePosition = {currentStageX, currentStageY};
    }
}

void Entity::PenState::updatePositionAndDraw(float newStageX, float newStageY) {
    if (!stop && isPenDown && pEngine) {
        // 그리기 조건: 중지되지 않았고(!stop) 펜이 내려져 있을 때
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
    // durationMs = 0; // Old member
    totalDurationMs = 0; // Clear total duration
    remainingDurationMs = 0.0f; // Clear remaining duration    needsRedraw = true;
    // bubbleScreenRect and tailVertices don't need explicit clearing here,
    // they are recalculated when the dialog becomes active.
}

Entity::Entity(Engine *engine, const std::string &entityId, const std::string &entityName,
               double initial_x, double initial_y, double initial_regX, double initial_regY,
               double initial_scaleX, double initial_scaleY, double initial_rotation, double initial_direction,
               int initial_width, int initial_height, bool initial_visible,
               Entity::RotationMethod initial_rotationMethod) // timedMoveState 추가
    : pEngineInstance(engine), id(entityId), name(entityName),
      x(initial_x), y(initial_y), regX(initial_regX), regY(initial_regY),
      scaleX(initial_scaleX), scaleY(initial_scaleY), rotation(initial_rotation), direction(initial_direction),
      width(initial_width), height(initial_height), visible(initial_visible), rotateMethod(initial_rotationMethod),
      brush(engine), paint(engine), /*visible(initial_visible),*/ timedMoveObjState(), timedRotationState(),
      m_effectBrightness(0.0), // 0: 원본 밝기
      m_effectAlpha(1.0), // 1.0: 완전 불투명,
      m_effectHue(0.0) // 0: 색조 변경 없음
{
    // Initialize PenState members
    // 초기스케일 복사
    OrigineScaleX = initial_scaleX;
    OrigineScaleY = initial_scaleY;
}

Entity::~Entity() = default;

void Entity::setScriptWait(const std::string &executionThreadId, Uint64 endTime, const std::string &blockId,
                           WaitType type) {
    std::lock_guard lock(m_stateMutex);
    auto &threadState = scriptThreadStates[executionThreadId]; // Get or create
    threadState.isWaiting = true;
    threadState.waitEndTime = endTime; // For EXPLICIT_WAIT_SECOND, this is the absolute time
    threadState.blockIdForWait = blockId;
    threadState.currentWaitType = type;

    // Initialize or re-initialize promise and future for this wait instance.
    // This ensures a fresh future is available for any new wait period.
    threadState.completionPromise = std::promise<void>();
    threadState.completionFuture = threadState.completionPromise.get_future();

    // resumeAtBlockIndex, scriptPtrForResume, sceneIdAtDispatchForResume are set by executeScript when it decides to pause
}

bool Entity::isScriptWaiting(const std::string &executionThreadId) const {
    // This function is often called to check if a script *should* pause.
    std::lock_guard lock(m_stateMutex);
    auto it = scriptThreadStates.find(executionThreadId);
    if (it != scriptThreadStates.end()) {
        return it->second.isWaiting;
    }
    return false;
}

// OperandValue 구조체 및 블록 처리 함수들은 BlockExecutor.h에 선언되어 있고,
// BlockExecutor.cpp에 구현되어 있으므로 Entity.cpp에서 중복 선언/정의할 필요가 없습니다.

void Entity::executeScript(const Script *scriptPtr, const std::string &executionThreadId,
                           const std::string &sceneIdAtDispatch, float deltaTime, size_t resumeInnerBlockIndex) {
    if (!pEngineInstance) {
        std::cerr << "Entity " << id << " has no valid Engine instance for script execution ThreadID "
                << executionThreadId << std::endl;
        return;
    }
    if (!scriptPtr) {
        pEngineInstance->EngineStdOut(format("executeScript called with null script pointer for object {}", id), 2,
                                      executionThreadId);
        return;
    }

    size_t startIndex = 1; // 기본 시작 인덱스 (0번 블록은 이벤트 트리거)
    std::string blockIdToResumeOnLog;
    WaitType waitTypeToResumeOnLog = WaitType::NONE;
    bool wasBlockInternalResume = false; {
        std::lock_guard lock(m_stateMutex);
        auto it_thread_state = scriptThreadStates.find(executionThreadId);
        if (it_thread_state == scriptThreadStates.end()) {
            pEngineInstance->EngineStdOut(
                "Entity::executeScript: New thread state created for " + id + " (Thread: " + executionThreadId + ")", 5,
                executionThreadId);
        }

        auto &threadState = scriptThreadStates[executionThreadId];

        if (threadState.scriptPtrForResume == nullptr && scriptPtr != nullptr) {
            threadState.scriptPtrForResume = scriptPtr;
            threadState.sceneIdAtDispatchForResume = sceneIdAtDispatch;
        }

        if (threadState.isWaiting && threadState.currentWaitType == WaitType::BLOCK_INTERNAL) {
            wasBlockInternalResume = true;
            startIndex = threadState.resumeAtBlockIndex;
            blockIdToResumeOnLog = threadState.blockIdForWait;
            waitTypeToResumeOnLog = threadState.currentWaitType;

            // blockIdToResumeOnLog를 사용하여 해당 블록의 타입을 가져와야 합니다.
            // scriptPtrForResume (또는 현재 scriptPtr)에서 blockIdToResumeOnLog에 해당하는 블록을 찾아 타입을 확인합니다.
            const Block* resumedBlock = nullptr;
            if (threadState.scriptPtrForResume && startIndex < threadState.scriptPtrForResume->blocks.size()) {
                 // startIndex가 유효한 인덱스인지 확인
                // BLOCK_INTERNAL 대기의 경우, startIndex는 대기를 설정한 블록 자체를 가리켜야 합니다.
                // resumeAtBlockIndex가 대기를 설정한 블록의 인덱스를 가리키고 있다고 가정합니다.
                if (threadState.scriptPtrForResume->blocks[startIndex].id == blockIdToResumeOnLog) {
                    resumedBlock = &threadState.scriptPtrForResume->blocks[startIndex];
                }
            }
            // 만약 resumedBlock을 찾지 못했다면, scriptPtr에서 찾아봅니다. (안전장치)
            if (!resumedBlock && scriptPtr) {
                for(const auto& blk : scriptPtr->blocks) {
                    if (blk.id == blockIdToResumeOnLog) {
                        // 이 경우 startIndex를 해당 블록의 인덱스로 재설정해야 할 수 있습니다.
                        // 하지만 threadState.resumeAtBlockIndex가 정확하다면 이 경로는 타지 않아야 합니다.
                        // 여기서는 threadState.resumeAtBlockIndex가 정확하다고 가정하고 진행합니다.
                        // resumedBlock = &blk; // 필요시 주석 해제 및 startIndex 조정
                        break;
                    }
                }
            }


            bool isLoopTypeBlock = false;
            if (resumedBlock) {
                const std::string& type = resumedBlock->type;
                if (type == "repeat_basic" || type == "repeat_inf" || type == "repeat_while_true" || type == "wait_until_true") {
                    isLoopTypeBlock = true;
                }
            }

            if (isLoopTypeBlock) {
                // 루프 블록의 BLOCK_INTERNAL 대기에서 재개하는 경우, startIndex를 증가시키지 않습니다.
                // 해당 루프 블록을 다시 실행하여 루프 조건을 재평가하거나 다음 반복을 수행합니다.
                threadState.isWaiting = false; // 대기 상태 해제
                threadState.currentWaitType = WaitType::NONE;
                threadState.resumeAtBlockIndex = startIndex;

                if (pEngineInstance) {
                    pEngineInstance->EngineStdOut(
                        "Entity::executeScript: BLOCK_INTERNAL resume for " + id +
                        " (Thread: " + executionThreadId + ") on LOOP block " + blockIdToResumeOnLog +
                        ". Re-evaluating at index " + std::to_string(startIndex) + ".",
                        3, executionThreadId); // 로그 레벨 DEBUG로 변경
                }
            }
            else if (threadState.loopCounters.find(blockIdToResumeOnLog) == threadState.loopCounters.end() && !isLoopTypeBlock) {
                 // 루프 블록이 아니면서, loopCounters에도 없는 경우 (일반 블록의 완료된 대기 또는 비정상 상태)
                if (pEngineInstance) {
                    pEngineInstance->EngineStdOut(
                        "Entity::executeScript: BLOCK_INTERNAL resume for " + id +
                        " (Thread: " + executionThreadId + ") on NON-LOOP block " + blockIdToResumeOnLog +
                        " (or completed loop, check logic). Advancing from index " + std::to_string(startIndex) + " to " + std::to_string(startIndex + 1)
                        + ".",
                        1, executionThreadId);
                }
                startIndex++; // 다음 블록으로 진행
                threadState.isWaiting = false;
                threadState.currentWaitType = WaitType::NONE;
                threadState.blockIdForWait = "";
                threadState.resumeAtBlockIndex = startIndex;
            } else if (!isLoopTypeBlock) {
                threadState.isWaiting = false;
                threadState.currentWaitType = WaitType::NONE;
                if (pEngineInstance) {
                     pEngineInstance->EngineStdOut("Entity::executeScript: Resuming BLOCK_INTERNAL for " + id +
                                                  " (Thread: " + executionThreadId + ") for NON-LOOP block " +
                                                  blockIdToResumeOnLog +
                                                  ". Will re-evaluate at index " + std::to_string(startIndex) + ".",
                                                  3, executionThreadId);
                }
            }
        } else if (threadState.isWaiting &&
                   (threadState.currentWaitType == WaitType::EXPLICIT_WAIT_SECOND ||
                    threadState.currentWaitType == WaitType::TEXT_INPUT ||
                    threadState.currentWaitType == WaitType::SOUND_FINISH)) {
            if (threadState.resumeAtBlockIndex >= 1) {
                startIndex = threadState.resumeAtBlockIndex;
                blockIdToResumeOnLog = threadState.blockIdForWait;
                waitTypeToResumeOnLog = threadState.currentWaitType;
                threadState.isWaiting = false;
                threadState.currentWaitType = WaitType::NONE;
            } else {
                threadState.isWaiting = false;
                threadState.currentWaitType = WaitType::NONE;
                threadState.resumeAtBlockIndex = -1;
            }
        } else if (threadState.resumeAtBlockIndex >= 1 && !threadState.isWaiting) {
            startIndex = threadState.resumeAtBlockIndex;
            blockIdToResumeOnLog = threadState.blockIdForWait;
            waitTypeToResumeOnLog = threadState.currentWaitType;
            if (pEngineInstance && startIndex > 1) {
                pEngineInstance->EngineStdOut("Entity::executeScript: Continuing script for " + id +
                                              " (Thread: " + executionThreadId + ") from index " + std::to_string(
                                                  startIndex) +
                                              " (was not waiting or wait just finished).",
                                              5, executionThreadId);
            }
        }

        if (threadState.resumeAtBlockIndex < 1 && !threadState.isWaiting) {
            threadState.resumeAtBlockIndex = -1;
        }
    }

    if (startIndex > 1 || wasBlockInternalResume) {
        pEngineInstance->EngineStdOut(
            "Entity::executeScript: " + std::string(wasBlockInternalResume ? "Attempting to resume" : "Resuming") +
            " script for " + id + " (Thread: " + executionThreadId + ") at block index " + std::to_string(startIndex) +
            " (Original Resume Block ID: " + blockIdToResumeOnLog + ", Type: " + BlockTypeEnumToString(
                waitTypeToResumeOnLog) + ")",
            3, executionThreadId);
    }

    for (size_t i = startIndex; i < scriptPtr->blocks.size(); ++i) {
        {
            std::lock_guard lock(m_stateMutex);
            auto it_thread_state = scriptThreadStates.find(executionThreadId);
            if (it_thread_state != scriptThreadStates.end() && it_thread_state->second.terminateRequested) {
                pEngineInstance->EngineStdOut(
                    "Script thread " + executionThreadId + " for entity " + this->id +
                    " is terminating as requested before block " + scriptPtr->blocks[i].id, 0, executionThreadId);
                it_thread_state->second.isWaiting = false;
                it_thread_state->second.resumeAtBlockIndex = -1;
                it_thread_state->second.blockIdForWait = "";
                it_thread_state->second.loopCounters.clear();
                it_thread_state->second.currentWaitType = WaitType::NONE;
                it_thread_state->second.scriptPtrForResume = nullptr;
                it_thread_state->second.sceneIdAtDispatchForResume = "";
                return;
            }
        }

        std::string currentEngineSceneId = pEngineInstance->getCurrentSceneId();
        const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
        bool isGlobalEntity = (objInfo && (objInfo->sceneId == "global" || objInfo->sceneId.empty()));

        if (pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed)) {
            pEngineInstance->EngineStdOut(
                "Script execution cancelled due to engine shutdown for entity: " + this->getId(), 1, executionThreadId);
            return;
        }
        if (currentEngineSceneId != sceneIdAtDispatch && !isGlobalEntity) {
            pEngineInstance->EngineStdOut(
                "Script execution for entity " + this->id + " (Block: " + scriptPtr->blocks[i].type +
                ") halted. Scene changed from " + sceneIdAtDispatch + " to " + currentEngineSceneId + ".", 1,
                executionThreadId);
            return;
        }
        if (!isGlobalEntity && objInfo && objInfo->sceneId != currentEngineSceneId) {
            pEngineInstance->EngineStdOut(
                "Script execution for entity " + this->id + " (Block: " + scriptPtr->blocks[i].type +
                ") halted. Entity no longer in current scene " + currentEngineSceneId + ".", 1, executionThreadId);
            return;
        }

        const Block &block = scriptPtr->blocks[i];
        THREAD_ID_INTERNAL = executionThreadId;
        BLOCK_ID_INTERNAL = block.id;
        try {
            blockName = block.type;
            Moving(block.type, *pEngineInstance, this->id, block, executionThreadId, deltaTime);
            Calculator(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Looks(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Sound(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Variable(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Function(block.type, *pEngineInstance, this->id, block, executionThreadId);
            TextBox(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Event(block.type, *pEngineInstance, this->id, block, executionThreadId);
            Flow(block.type, *pEngineInstance, this->id, block, executionThreadId, sceneIdAtDispatch, deltaTime);
        } catch (const ScriptBlockExecutionError &sbee) {
            throw;
        }
        catch (const std::exception &e) {
            throw ScriptBlockExecutionError("Error during script block execution in entity.", block.id, block.type,
                                            this->id, e.what());
        } {
            std::lock_guard lock(m_stateMutex);
            auto &currentThreadState = scriptThreadStates[executionThreadId];

            if (currentThreadState.isWaiting) {
                pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                              ") - Entering wait handling for block " + block.id + " (Type: " + block.
                                              type +
                                              "). CurrentWaitType: " + BlockTypeEnumToString(
                                                  currentThreadState.currentWaitType),
                                              3, executionThreadId);

                // --- Flow 블록 내부 대기 처리 로직 강화 ---
                bool isFlowBlockType = (block.type == "repeat_basic" || block.type == "repeat_inf" ||
                                        block.type == "repeat_while_true" || block.type == "_if" ||
                                        block.type == "if_else" || block.type == "wait_until_true");


                if (isFlowBlockType &&
                    (currentThreadState.currentWaitType == WaitType::EXPLICIT_WAIT_SECOND ||
                     currentThreadState.currentWaitType == WaitType::TEXT_INPUT ||
                     currentThreadState.currentWaitType == WaitType::SOUND_FINISH) &&
                    currentThreadState.blockIdForWait != block.id /* 대기가 Flow 블록 자신이 아닌 내부 블록에 의해 발생 */) {
                    pEngineInstance->EngineStdOut(
                        "Entity::executeScript: Flow block " + block.id + " (Type: " + block.type +
                        ") needs to resume due to inner block wait (Type: " + BlockTypeEnumToString(
                            currentThreadState.currentWaitType) +
                        " on block " + currentThreadState.blockIdForWait + // <-- 이것이 실제 내부 대기 블록 ID
                        "). Setting BLOCK_INTERNAL wait for the Flow block itself.",
                        3, executionThreadId);

                    std::string actualInnerWaitingBlockId = currentThreadState.blockIdForWait; // 원래 대기를 유발한 내부 블록 ID

                    currentThreadState.resumeAtBlockIndex = i; // 현재 Flow 블록에서 재개
                    currentThreadState.blockIdForWait = block.id; // Flow 블록 ID로 대기 대상 변경
                    currentThreadState.currentWaitType = Entity::WaitType::BLOCK_INTERNAL;
                    currentThreadState.scriptPtrForResume = scriptPtr;
                    currentThreadState.sceneIdAtDispatchForResume = sceneIdAtDispatch;
                    currentThreadState.originalInnerBlockIdForWait = actualInnerWaitingBlockId;

                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                  ") - Returning from executeScript to free worker thread (Flow block internal explicit wait). originalInnerBlockIdForWait set to: "
                                                  + actualInnerWaitingBlockId, // 로그 추가
                                                  3, executionThreadId);
                    return; // 워커 스레드 반환
                }
                if (currentThreadState.currentWaitType == WaitType::BLOCK_INTERNAL) {
                    currentThreadState.resumeAtBlockIndex = i;
                    currentThreadState.scriptPtrForResume = scriptPtr;
                    currentThreadState.sceneIdAtDispatchForResume = sceneIdAtDispatch;

                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                  ") - Wait (BLOCK_INTERNAL) for block " + block.id +
                                                  " initiated. Pausing script. Will resume at index " + std::to_string(
                                                      i) + ".",
                                                  3, executionThreadId);
                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                  ") - Returning from executeScript to free worker thread (BLOCK_INTERNAL).",
                                                  3, executionThreadId);
                    return;
                }
                if (currentThreadState.currentWaitType == WaitType::EXPLICIT_WAIT_SECOND ||
                    currentThreadState.currentWaitType == WaitType::TEXT_INPUT ||
                    currentThreadState.currentWaitType == WaitType::SOUND_FINISH) {
                    currentThreadState.resumeAtBlockIndex = i + 1;
                    currentThreadState.scriptPtrForResume = scriptPtr;
                    currentThreadState.sceneIdAtDispatchForResume = sceneIdAtDispatch;

                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                  ") - Wait (" + BlockTypeEnumToString(
                                                      currentThreadState.currentWaitType) +
                                                  ") for block " + block.id +
                                                  " initiated. Pausing script. Will resume at index " + std::to_string(
                                                      i + 1) + ".",
                                                  3, executionThreadId);
                    pEngineInstance->EngineStdOut("Entity::executeScript: " + id + " (Thread: " + executionThreadId +
                                                  ") - Returning from executeScript to free worker thread (EXPLICIT/TEXT/SOUND).",
                                                  3, executionThreadId);
                    return;
                }
            }
        }
    } {
        std::lock_guard lock(m_stateMutex);
        auto &threadState = scriptThreadStates[executionThreadId];
        threadState.isWaiting = false;
        threadState.resumeAtBlockIndex = -1;
        threadState.blockIdForWait = "";
        threadState.loopCounters.clear();
        threadState.currentWaitType = WaitType::NONE;
        threadState.scriptPtrForResume = nullptr;
        threadState.sceneIdAtDispatchForResume = "";
    }
    pEngineInstance->EngineStdOut("Script for object " + id + " completed all blocks.", 5, executionThreadId);
}

// ... (Entity.h에 추가할 BlockTypeEnumToString 헬퍼 함수 선언 예시)
// namespace EntityHelper { std::string BlockTypeEnumToString(Entity::WaitType type); }
// Entity.cpp에 구현:
string BlockTypeEnumToString(Entity::WaitType type) {
    switch (type) {
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
        case Entity::WaitType::SCENE_CHANGE_SUSPEND:
            return "SCENE_CHANGE_SUSPEND";
        default:
            return "UNKNOWN_WAIT_TYPE";
    }
}

const std::string &Entity::getId() const { return id; }
const std::string &Entity::getName() const { return name; }

double Entity::getX() const {
    std::lock_guard lock(m_stateMutex);
    return x;
}

double Entity::getY() const {
    std::lock_guard lock(m_stateMutex);
    return y;
}

double Entity::getRegX() const {
    std::lock_guard lock(m_stateMutex);
    return regX;
}

double Entity::getRegY() const {
    std::lock_guard lock(m_stateMutex);
    return regY;
}

double Entity::getScaleX() const {
    std::lock_guard lock(m_stateMutex);
    return scaleX;
}

double Entity::getScaleY() const {
    std::lock_guard lock(m_stateMutex);
    return scaleY;
}

double Entity::getRotation() const {
    std::lock_guard lock(m_stateMutex);
    return rotation;
}

double Entity::getDirection() const {
    std::lock_guard lock(m_stateMutex);
    return direction;
}

double Entity::getWidth() const {
    std::lock_guard lock(m_stateMutex);
    return width;
}

double Entity::getHeight() const {
    std::lock_guard lock(m_stateMutex);
    return height;
}

bool Entity::isVisible() const {
    // std::lock_guard lock(m_stateMutex); // Lock removed
    return visible.load(std::memory_order_relaxed);
}

SDL_FRect Entity::getVisualBounds() const {
    std::lock_guard lock(m_stateMutex);
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

void Entity::setX(double newX) {
    std::lock_guard lock(m_stateMutex);
    x = newX;
}

void Entity::setY(double newY) {
    std::lock_guard lock(m_stateMutex);
    y = newY;
}

void Entity::setRegX(double newRegX) {
    std::lock_guard lock(m_stateMutex);
    regX = newRegX;
}

void Entity::setRegY(double newRegY) {
    std::lock_guard lock(m_stateMutex);
    regY = newRegY;
}

void Entity::setScaleX(double newScaleX) {
    std::lock_guard lock(m_stateMutex);
    if (!m_isClone) {
        scaleX = newScaleX;
    }
}

void Entity::setScaleY(double newScaleY) {
    std::lock_guard lock(m_stateMutex);
    if (!m_isClone) {
        scaleY = newScaleY;
    }
}

void Entity::setRotation(double newRotation) {
    std::lock_guard lock(m_stateMutex);
    rotation = newRotation;
}

void Entity::setDirection(double newDirection) {
    std::lock_guard lock(m_stateMutex);
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

void Entity::setWidth(double newWidth) {
    std::lock_guard lock(m_stateMutex);
    // 엔티티의 내부 width 값을 업데이트합니다.
    // 이 값은 충돌 감지 등에 사용될 수 있으며, scaleX와 함께 시각적 크기를 결정합니다.
    this->width = newWidth;

    if (pEngineInstance) {
        const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
        if (objInfo && !objInfo->costumes.empty()) {
            const Costume *selectedCostume = nullptr;
            const std::string &currentCostumeId = objInfo->selectedCostumeId;

            for (const auto &costume_ref: objInfo->costumes) {
                if (costume_ref.id == currentCostumeId) {
                    selectedCostume = &costume_ref;
                    break;
                }
            }

            if (selectedCostume) // Check selectedCostume first
            {
                pEngineInstance->EngineStdOut(
                    "DEBUG: setWidth for " + this->id + ", costume '" + selectedCostume->name + "'. imageHandle: " + (
                        selectedCostume->imageHandle ? "VALID_PTR" : "NULL_PTR"), 3);
                if (selectedCostume->imageHandle) // Then check imageHandle
                {
                    float texW = 0, texH = 0;
                    SDL_ClearError(); // Clear previous SDL errors
                    int texture_size_result = SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH);

                    if (texture_size_result == 0) // SDL3: 0 on success
                    {
                        pEngineInstance->EngineStdOut(
                            "DEBUG: SDL_GetTextureSize success for " + selectedCostume->name + ". texW: " +
                            std::to_string(texW) + ", texH: " + std::to_string(texH), 3);
                        if (texW > 0.00001f) {
                            // 0으로 나누기 방지 (매우 작은 값보다 클 때)
                            this->scaleX = newWidth / static_cast<double>(texW);
                        } else {
                            this->scaleX = 1.0; // 원본 텍스처 너비가 0이면 스케일 1로 설정 (오류 상황)
                            pEngineInstance->EngineStdOut(
                                "Warning: Original texture width is 0 for costume '" + selectedCostume->name +
                                "' of entity '" + this->id + "'. Cannot accurately set scaleX.", 1);
                            pEngineInstance->EngineStdOut(
                                "DEBUG: Calculated scaleX: " + std::to_string(this->scaleX) + " (newWidth: " +
                                std::to_string(newWidth) + ", texW: " + std::to_string(texW) + ")", 3);
                        }
                    } else {
                        const char *err = SDL_GetError();
                        std::string sdlErrorString = (err && err[0] != '\0') ? err : "None (or error string was empty)";
                        pEngineInstance->EngineStdOut(
                            "Warning: Could not get texture size for costume '" + selectedCostume->name +
                            "' of entity '" + this->id + "'. SDL_GetTextureSize result: " +
                            std::to_string(texture_size_result) + ", SDL Error: " + sdlErrorString +
                            ". ScaleX not changed.", 1);
                        if (err && err[0] != '\0')
                            SDL_ClearError();
                    }
                } else // selectedCostume->imageHandle is NULL
                {
                    pEngineInstance->EngineStdOut(
                        "Warning: selectedCostume->imageHandle is NULL for costume '" + selectedCostume->name +
                        "' of entity " + this->id + ". ScaleX not changed.", 1);
                }
            } else {
                pEngineInstance->EngineStdOut(
                    "Warning: Selected costume is NULL for entity " + this->id + ". ScaleX not changed.", 1);
            }
        } else {
            // Only show warning if it's not a textBox, as textBoxes don't rely on costumes for scaling this way.
            if (objInfo && objInfo->objectType != "textBox") {
                pEngineInstance->EngineStdOut(
                    "Warning: ObjectInfo or costumes not found for sprite entity " + this->id + ". ScaleX not changed.",
                    1);
            } else if (!objInfo) {
                // ObjectInfo itself is missing, which is a more general issue.
                pEngineInstance->EngineStdOut(
                    "Warning: ObjectInfo not found for entity " + this->id + ". ScaleX not changed.", 1);
            }
        }
    }
}

void Entity::setHeight(double newHeight) {
    std::lock_guard lock(m_stateMutex);
    this->height = newHeight; // 내부 height 값 업데이트

    if (pEngineInstance) {
        // setWidth와 유사한 로직으로 scaleY 업데이트
        const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
        if (objInfo && objInfo->objectType == "textBox") {
            // For textBoxes, scaleY should typically be 1.0 unless explicitly set.
            // Direct height setting shouldn't derive scaleY from costumes.
            return; // Skip costume-based scaling for textBox
        }

        if (objInfo && !objInfo->costumes.empty()) {
            const Costume *selectedCostume = nullptr;
            const std::string &currentCostumeId = objInfo->selectedCostumeId;
            for (const auto &costume_ref: objInfo->costumes) {
                if (costume_ref.id == currentCostumeId) {
                    selectedCostume = &costume_ref;
                    break;
                }
            }
            if (selectedCostume && selectedCostume->imageHandle) {
                float texW = 0, texH = 0;
                if (SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH) == true) {
                    if (texH > 0.00001f) {
                        // 0으로 나누기 방지
                        this->scaleY = newHeight / static_cast<double>(texH);
                    } else {
                        this->scaleY = 1.0;
                        pEngineInstance->EngineStdOut(
                            "Warning: Original texture height is 0 for costume '" + selectedCostume->name +
                            "' of entity '" + this->id + "'. Cannot accurately set scaleY.", 1);
                    }
                } else {
                    const char *err = SDL_GetError();
                    std::string sdlErrorString = (err && err[0] != '\0') ? err : "None";
                    pEngineInstance->EngineStdOut(
                        "Warning: Could not get texture size for costume '" + selectedCostume->name + "' of entity '" +
                        this->id + "'. SDL Error: " + sdlErrorString + ". ScaleY not changed.", 1);
                    if (err && err[0] != '\0')
                        SDL_ClearError();
                }
            } else {
                pEngineInstance->EngineStdOut(
                    "Warning: Selected costume or image handle not found for entity " + this->id +
                    ". ScaleY not changed.", 1);
            }
        } else {
            if (objInfo && objInfo->objectType != "textBox") {
                pEngineInstance->EngineStdOut(
                    "Warning: ObjectInfo or costumes not found for sprite entity " + this->id + ". ScaleY not changed.",
                    1);
            } else if (!objInfo) {
                pEngineInstance->EngineStdOut(
                    "Warning: ObjectInfo not found for entity " + this->id + ". ScaleY not changed.", 1);
            }
        }
    }
}

void Entity::setVisible(bool newVisible) {
    // std::lock_guard lock(m_stateMutex); // Lock removed
    visible.store(newVisible, std::memory_order_relaxed);
}

Entity::RotationMethod Entity::getRotateMethod() const {
    std::lock_guard lock(m_stateMutex);
    return rotateMethod;
}

void Entity::setRotateMethod(RotationMethod method) {
    std::lock_guard lock(m_stateMutex);
    rotateMethod = method;
}

Uint32 get_pixel(SDL_Surface *surface, int x, int y) {
    if (!surface) return 0; // Surface가 null이면 0 반환
    if (x < 0 || x >= surface->w || y < 0 || y >= surface->h) return 0; // 범위 초과 시 0 반환

    int bpp = SDL_BYTESPERPIXEL(surface->format);
    /* Here p is the address to the pixel we want to retrieve */
    Uint8 *p = (Uint8 *) surface->pixels + y * surface->pitch + x * bpp;

    switch (bpp) {
        case 1:
            return *p;
            break;

        case 2:
            return *(Uint16 *) p;
            break;

        case 3:
            if (SDL_ISPIXELFORMAT_ALPHA(surface->format)) {
                // 일반적으로 3bpp는 알파가 없지만, 혹시 모를 경우
                return 0; // 또는 특정 에러 처리
            } else {
                if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                    return p[0] << 16 | p[1] << 8 | p[2];
                } else {
                    return p[0] | p[1] << 8 | p[2] << 16;
                }
            }
            break;

        case 4:
            return *(Uint32 *) p;
            break;

        default:
            return 0;
            break;
    }
}

bool Entity::isPointInside(double pX, double pY) const {
    std::lock_guard lock(m_stateMutex);

    if (!visible.load(std::memory_order_relaxed) || m_effectAlpha < 0.01) {
        return false;
    }

    double localPX = pX - this->x;
    double localPY = pY - this->y;

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);

    if (objInfo && objInfo->objectType == "textBox") {
        // 글상자는 회전을 고려하지 않는 경우가 많으므로, 스케일만 고려한 단순 사각형 판정
        double scaledHalfWidth = (this->width * std::abs(this->scaleX)) / 2.0;
        double scaledHalfHeight = (this->height * std::abs(this->scaleY)) / 2.0;

        // 로컬 좌표를 객체의 스케일 역변환 (회전은 없다고 가정)
        // 글상자의 경우 regX, regY가 (width/2, height/2)와 유사하게 작용할 수 있음
        // 또는 등록점 없이 좌상단 기준 판정 후 로컬 좌표를 그에 맞게 조정
        // 여기서는 간단히 중심 기준 판정
        bool inX = (localPX >= -scaledHalfWidth && localPX <= scaledHalfWidth);
        bool inY = (localPY >= -scaledHalfHeight && localPY <= scaledHalfHeight);
        return inX && inY;
    }

    // 일반 스프라이트
    // 엔트리/스크래치와 유사하게 direction을 주된 회전으로 사용 (0도 위, 90도 오른쪽)
    // this->rotation은 추가적인 시각적 회전으로 간주
    double effectiveRotationDeg = this->direction - 90.0 + this->rotation; // 0도 오른쪽 기준 각도로 변환 후 추가 회전
    double angleRad = -effectiveRotationDeg * (SDL_PI_D / 180.0); // 반시계 방향 회전을 위해 음수 사용

    double rotatedPX = localPX * std::cos(angleRad) - localPY * std::sin(angleRad);
    double rotatedPY = localPX * std::sin(angleRad) + localPY * std::cos(angleRad);

    const double epsilon = 1e-9;
    double unscaledPX = rotatedPX;
    double unscaledPY = rotatedPY;

    if (std::abs(this->scaleX) < epsilon) {
        if (std::abs(this->width) > epsilon) return false;
        if (std::abs(rotatedPX) > epsilon) return false;
    } else {
        unscaledPX /= this->scaleX;
    }

    if (std::abs(this->scaleY) < epsilon) {
        if (std::abs(this->height) > epsilon) return false;
        if (std::abs(rotatedPY) > epsilon) return false;
    } else {
        unscaledPY /= this->scaleY;
    }

    // 텍스처 좌표 계산 (regX, regY는 코스튬의 좌상단 (0,0) 기준 오프셋)
    // this->width, this->height는 원본 코스튬의 크기여야 함.
    // unscaledPX, unscaledPY는 현재 객체의 등록점(regX, regY)을 (0,0)으로 하는 좌표.
    // 텍스처 좌표는 코스튬의 좌상단을 (0,0)으로 함.

    // texClickX: unscaledPX는 등록점 기준 X좌표. regX는 좌상단에서 등록점까지의 X 오프셋.
    // 따라서, 좌상단 기준 클릭 X좌표는 unscaledPX + regX.
    int texClickX = static_cast<int>(std::round(unscaledPX + this->regX));

    int texClickY = static_cast<int>(std::round(this->regY - unscaledPY));

    // this->width와 this->height는 원본 코스튬의 크기여야 합니다.
    if (texClickX < 0 || texClickX >= static_cast<int>(std::round(this->width)) ||
        texClickY < 0 || texClickY >= static_cast<int>(std::round(this->height))) {
        if (pEngineInstance)
            pEngineInstance->EngineStdOut(
                "Entity " + id + " point (" + std::to_string(pX) + "," + std::to_string(pY) +
                ") -> tex (" + std::to_string(texClickX) + "," + std::to_string(texClickY) +
                ") is outside bounds (" + std::to_string(this->width) + "," + std::to_string(this->height) + ")", 3);
        return false;
    }

    if (objInfo && !objInfo->costumes.empty()) {
        const Costume *selectedCostume = nullptr;
        for (const auto &costume: objInfo->costumes) {
            if (costume.id == objInfo->selectedCostumeId) {
                selectedCostume = &costume;
                break;
            }
        }

        if (selectedCostume && selectedCostume->surfaceHandle) {
            // texClickX, texClickY가 selectedCostume->surfaceHandle의 w, h 범위 내에 있는지 추가 확인
            if (texClickX < 0 || texClickX >= selectedCostume->surfaceHandle->w ||
                texClickY < 0 || texClickY >= selectedCostume->surfaceHandle->h) {
                if (pEngineInstance)
                    pEngineInstance->EngineStdOut(
                        "Entity " + id + " tex (" + std::to_string(texClickX) + "," + std::to_string(texClickY) +
                        ") is outside surface bounds (" + std::to_string(selectedCostume->surfaceHandle->w) + "," +
                        std::to_string(selectedCostume->surfaceHandle->h) + ")", 3);
                return false; // 서피스 실제 크기 벗어남
            }

            Uint32 pixel = get_pixel(selectedCostume->surfaceHandle, texClickX, texClickY);
            Uint8 r, g, b, a;
            const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(selectedCostume->surfaceHandle->format);
            SDL_GetRGBA(pixel, details, nullptr, &r, &g, &b, &a);

            const Uint8 ALPHA_THRESHOLD = 10;
            if (a > ALPHA_THRESHOLD) {
                return true;
            }
        } else {
            if (pEngineInstance) {
                std::string reason = "Unknown";
                if (!selectedCostume) reason = "Selected costume is null";
                else if (!selectedCostume->surfaceHandle)
                    reason = "Surface handle is null for costume " + selectedCostume->name;
                pEngineInstance->EngineStdOut(
                    "Warning: Entity " + id + " pixel collision check skipped. Reason: " + reason, 1);
            }
            // 서피스 핸들이 없으면, 바운딩 박스 충돌로 간주 (여기까지 왔다면 true)
            return true;
        }
    } else {
        if (pEngineInstance)
            pEngineInstance->EngineStdOut(
                "Warning: Entity " + id + " has no costume info for pixel collision. Assuming hit based on bounds.", 1);
        return true; // 코스튬 정보 없으면 바운딩 박스 충돌로 간주
    }

    return false;
}

Entity::CollisionSide Entity::getLastCollisionSide() const {
    std::lock_guard lock(m_stateMutex);
    return lastCollisionSide;
}

void Entity::setLastCollisionSide(CollisionSide side) {
    std::lock_guard lock(m_stateMutex);
    lastCollisionSide = side;
}

// processMathematicalBlock, Moving, Calculator 등의 함수 구현도 여기에 유사하게 이동/정의해야 합니다.
// 간결성을 위해 전체 구현은 생략합니다. BlockExecutor.cpp의 내용을 참고하세요.
// -> 이 주석은 이제 유효하지 않습니다. 해당 함수들은 BlockExecutor.cpp에 있습니다.

void Entity::showDialog(const std::string &message, const std::string &dialogType, Uint64 duration) {
    std::lock_guard lock(m_stateMutex);
    m_currentDialog.clear();

    m_currentDialog.text = message.empty() ? "    " : message;
    m_currentDialog.type = dialogType;
    m_currentDialog.isActive = true;
    // m_currentDialog.durationMs = duration; // Old member
    m_currentDialog.totalDurationMs = duration;
    m_currentDialog.remainingDurationMs = static_cast<float>(duration);
    m_currentDialog.startTimeMs = SDL_GetTicks(); // Keep startTime for reference if needed
    m_currentDialog.needsRedraw = true;

    if (pEngineInstance) {
        pEngineInstance->EngineStdOut(
            "Entity " + id + " dialog: '" + m_currentDialog.text + "', type: " + m_currentDialog.type + ", duration: " +
            std::to_string(duration), 3);
    }
}

void Entity::removeDialog() {
    std::lock_guard lock(m_stateMutex);
    if (m_currentDialog.isActive) {
        m_currentDialog.clear();
    }
}

void Entity::updateDialog(float deltaTime) // deltaTime is in seconds
{
    std::lock_guard lock(m_stateMutex);
    if (m_currentDialog.isActive && m_currentDialog.totalDurationMs > 0)
    // Check totalDurationMs to see if it's a timed dialog
    {
        m_currentDialog.remainingDurationMs -= (deltaTime * 1000.0f); // Convert deltaTime to ms and decrement
        if (m_currentDialog.remainingDurationMs <= 0.0f) {
            // removeDialog()는 내부적으로 m_stateMutex를 다시 잠그려고 시도할 수 있습니다.
            // 이미 m_stateMutex가 잠겨있는 상태이므로, clear()를 직접 호출하거나
            // removeDialog()의 내부 로직을 여기에 직접 구현하는 것이 좋습니다.
            // 여기서는 clear()를 직접 호출하는 것으로 변경합니다.
            m_currentDialog.clear(); // Time's up, clear the dialog
        }
    }
}

bool Entity::hasActiveDialog() const {
    std::lock_guard lock(m_stateMutex);
    return m_currentDialog.isActive;
}

std::string Entity::getWaitingBlockId(const std::string &executionThreadId) const {
    std::lock_guard lock(m_stateMutex);
    auto it = scriptThreadStates.find(executionThreadId);
    if (it != scriptThreadStates.end() && it->second.isWaiting) {
        return it->second.blockIdForWait;
    }
    return "";
}

Entity::WaitType Entity::getCurrentWaitType(const std::string &executionThreadId) const {
    std::lock_guard lock(m_stateMutex);
    auto it = scriptThreadStates.find(executionThreadId);
    if (it != scriptThreadStates.end() && it->second.isWaiting) {
        return it->second.currentWaitType;
    }
    return WaitType::NONE;
}

// Effect Getters and Setters
double Entity::getEffectBrightness() const {
    std::lock_guard lock(m_stateMutex);
    return m_effectBrightness;
}

void Entity::setEffectBrightness(double brightness) {
    std::lock_guard lock(m_stateMutex);
    m_effectBrightness = std::clamp(brightness, -100.0, 100.0);
}

double Entity::getEffectAlpha() const {
    std::lock_guard lock(m_stateMutex);
    return m_effectAlpha;
}

void Entity::setEffectAlpha(double alpha) {
    std::lock_guard lock(m_stateMutex);
    m_effectAlpha = std::clamp(alpha, 0.0, 1.0);
}

double Entity::getEffectHue() const {
    std::lock_guard lock(m_stateMutex);
    return m_effectHue;
}

void Entity::setEffectHue(double hue) {
    std::lock_guard lock(m_stateMutex);
    m_effectHue = std::fmod(hue, 360.0);
    if (m_effectHue < 0)
        m_effectHue += 360.0;
}

void Entity::playSound(const std::string &soundId) {
    std::lock_guard lock(m_stateMutex);

    if (!pEngineInstance) {
        // std::cerr << "ERROR: Entity " << id << " has no pEngineInstance to play sound." << std::endl;
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo) {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile: objInfo->sounds) {
        if (soundFile.id == soundId) {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay) {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu) {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        } else {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSound(this->getId(), soundFilePath); // Engine의 public aeHelper 사용
        pEngineInstance->EngineStdOut(
            "Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath +
            ")", 0);
    } else {
        pEngineInstance->EngineStdOut(
            "Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}

void Entity::playSoundWithSeconds(const std::string &soundId, double seconds) {
    // std::unique_lock<std::recursive_mutex> lock(m_stateMutex); // Lock removed for async wait

    if (!pEngineInstance) {
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo) {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile: objInfo->sounds) {
        if (soundFile.id == soundId) {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay) {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu) {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        } else {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSoundForDuration(this->getId(), soundFilePath, seconds);
        // Engine의 public aeHelper 사용
        pEngineInstance->EngineStdOut(
            "Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath +
            ")", 0);
    } else {
        pEngineInstance->EngineStdOut(
            "Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}

void Entity::playSoundWithFromTo(const std::string &soundId, double from, double to) {
    std::lock_guard lock(m_stateMutex);

    if (!pEngineInstance) {
        // std::cerr << "ERROR: Entity " << id << " has no pEngineInstance to play sound." << std::endl;
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo) {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile: objInfo->sounds) {
        if (soundFile.id == soundId) {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay) {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu) {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        } else {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        pEngineInstance->aeHelper.playSoundFromTo(this->getId(), soundFilePath, from, to); // Engine의 public aeHelper 사용
        pEngineInstance->EngineStdOut(
            "Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath +
            ")", 0);
    } else {
        pEngineInstance->EngineStdOut(
            "Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}

// Entity.cpp
void Entity::waitforPlaysound(const std::string &soundId, const std::string &executionThreadId,
                              const std::string &callingBlockId) {
    // m_stateMutex는 이 함수 시작 시점에 잠그지 않고, ScriptThreadState 접근 시에만 잠급니다.
    // 사운드 재생 자체는 비동기일 수 있기 때문입니다.

    if (!pEngineInstance) {
        // pEngineInstance가 null인 경우 즉시 반환 (오류 로깅은 생성자 또는 초기화에서 처리)
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo) {
        pEngineInstance->EngineStdOut("Entity::waitforPlaysound - ObjectInfo not found for entity: " + this->id, 2,
                                      executionThreadId);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile: objInfo->sounds) {
        if (soundFile.id == soundId) {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay) {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu) {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        } else {
            soundFilePath = std::string(BASE_ASSETS) + soundToPlay->fileurl;
        }

        // ScriptThreadState에 접근하기 전에 뮤텍스 잠금
        std::unique_lock<std::recursive_mutex> lock(m_stateMutex);
        auto it_state = scriptThreadStates.find(executionThreadId);
        if (it_state == scriptThreadStates.end()) {
            pEngineInstance->EngineStdOut(
                "Entity " + id + " (Thread: " + executionThreadId +
                ") - ScriptThreadState not found for waitforPlaysound. Cannot set wait.", 2, executionThreadId);
            // 스레드 상태가 없으면 사운드만 재생하고 대기 설정 없이 반환 (또는 오류 처리)
            pEngineInstance->aeHelper.playSound(this->getId(), soundFilePath); // 사운드는 재생
            pEngineInstance->EngineStdOut("Entity " + id + " playing sound (no wait): " + soundToPlay->name, 0,
                                          executionThreadId);
            return;
        }
        ScriptThreadState &threadState = it_state->second;

        // 새로운 promise와 future 생성
        threadState.completionPromise = {}; // 이전 promise가 있다면 리셋 (기본 생성자로 새 promise 할당)
        threadState.completionFuture = threadState.completionPromise.get_future();

        // AudioEngineHelper의 playSound 함수가 이 promise를 알고, 사운드 완료 시 resolve하도록 수정 필요.
        // 예를 들어, playSound 함수가 promise의 참조나 포인터를 받을 수 있습니다.
        // pEngineInstance->aeHelper.playSoundAndNotifyPromise(this->getId(), soundFilePath, threadState.completionPromise);
        // 위와 같은 함수가 AudioEngineHelper에 구현되어야 합니다.
        // 현재는 기존 playSound를 호출합니다. AudioEngineHelper 수정 없이는 future가 자동으로 resolve되지 않습니다.
        pEngineInstance->aeHelper.playSound(this->getId(), soundFilePath);


        pEngineInstance->EngineStdOut(
            "Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath +
            ")", 0, executionThreadId);

        // setScriptWait는 ScriptThreadState를 직접 수정하므로, promise/future 설정 후에 호출
        // SOUND_FINISH 타입으로 대기를 설정합니다.
        // resumeSoundWaitScripts에서 completionFuture를 확인하여 스크립트를 재개합니다.
        // waitEndTime은 SOUND_FINISH 타입에서는 직접 사용되지 않을 수 있지만,
        // 만약 future가 타임아웃되거나 하는 경우를 대비해 사운드 길이를 넣어둘 수 있습니다.
        double soundDuration = pEngineInstance->aeHelper.getSoundFileDuration(soundFilePath);
        Uint64 estimatedEndTime = SDL_GetTicks() + static_cast<Uint64>(soundDuration * 1000.0);

        setScriptWait(executionThreadId, estimatedEndTime, callingBlockId, WaitType::SOUND_FINISH);

        // ScriptThreadState의 completionFuture는 resumeSoundWaitScripts에서 사용됩니다.
        // AudioEngineHelper가 사운드 완료 시 threadState.completionPromise.set_value()를 호출해야 합니다.

        pEngineInstance->EngineStdOut(
            "Entity " + id + " (Thread: " + executionThreadId + ") waiting for sound (future set): " + soundToPlay->name
            + " (Block: " + callingBlockId + ")", 0, executionThreadId);
    } else {
        pEngineInstance->EngineStdOut(
            "Entity::waitforPlaysound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1,
            executionThreadId);
    }
}

void Entity::waitforPlaysoundWithSeconds(const std::string &soundId, double seconds,
                                         const std::string &executionThreadId, const std::string &callingBlockId) {
    if (!pEngineInstance) {
        pEngineInstance->EngineStdOut("Entity " + id + " has no pEngineInstance to play sound.", 2);
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo) {
        pEngineInstance->EngineStdOut("Entity::playSound - ObjectInfo not found for entity: " + this->id, 2);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile: objInfo->sounds) {
        if (soundFile.id == soundId) {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay) {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu) {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        } else {
            soundFilePath = string(BASE_ASSETS) + soundToPlay->fileurl;
        }
        auto it_state = scriptThreadStates.find(executionThreadId);
        if (it_state == scriptThreadStates.end()) {
            pEngineInstance->EngineStdOut(
                "Entity " + id + " (Thread: " + executionThreadId +
                ") - ScriptThreadState not found for waitforPlaysoundWithFromTo. Cannot set wait.", 2,
                executionThreadId);
            // 스레드 상태가 없으면 사운드만 재생하고 대기 설정 없이 반환
            pEngineInstance->aeHelper.playSoundForDuration(this->getId(), soundFilePath, seconds);
            // Engine의 public aeHelper 사용
            pEngineInstance->EngineStdOut("Entity " + id + " playing sound (from-to, no wait): " + soundToPlay->name, 0,
                                          executionThreadId);
            return;
        }
        ScriptThreadState &threadState = it_state->second;
        // 새로운 promise와 future 생성
        threadState.completionPromise = {}; // 이전 promise가 있다면 리셋
        threadState.completionFuture = threadState.completionPromise.get_future();
        // setScriptWait는 ScriptThreadState를 직접 수정하므로, promise/future 설정 후에 호출
        setScriptWait(executionThreadId, seconds, callingBlockId, WaitType::SOUND_FINISH);
        // pEngineInstance->EngineStdOut("Entity " + id + " playing sound: " + soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0);
        pEngineInstance->EngineStdOut(
            "Entity " + id + " playing sound for " + std::to_string(seconds) + "s: " + soundToPlay->name + " (ID: " +
            soundId + ", Path: " + soundFilePath + ")", 0);
    } else {
        pEngineInstance->EngineStdOut(
            "Entity::playSound - Sound ID '" + soundId + "' not found for entity: " + this->id, 1);
    }
}

// Entity.cpp
void Entity::waitforPlaysoundWithFromTo(const std::string &soundId, double from, double to,
                                        const std::string &executionThreadId, const std::string &callingBlockId) {
    // m_stateMutex는 ScriptThreadState 접근 및 수정을 보호하기 위해 사용됩니다.
    std::unique_lock<std::recursive_mutex> lock(m_stateMutex);

    if (!pEngineInstance) {
        // pEngineInstance가 null인 경우 즉시 반환 (오류 로깅은 생성자 또는 초기화에서 처리)
        // EngineStdOut은 pEngineInstance를 사용하므로, 여기서는 직접 로깅하거나 반환합니다.
        // std::cerr << "Error in waitforPlaysoundWithFromTo: pEngineInstance is null for entity " << id << std::endl;
        return;
    }

    const ObjectInfo *objInfo = pEngineInstance->getObjectInfoById(this->id);
    if (!objInfo) {
        pEngineInstance->EngineStdOut(
            "Entity::waitforPlaysoundWithFromTo - ObjectInfo not found for entity: " + this->id, 2, executionThreadId);
        return;
    }

    const SoundFile *soundToPlay = nullptr;
    for (const auto &soundFile: objInfo->sounds) {
        if (soundFile.id == soundId) {
            soundToPlay = &soundFile;
            break;
        }
    }

    if (soundToPlay) {
        std::string soundFilePath = "";
        if (pEngineInstance->IsSysMenu) {
            soundFilePath = "sysmenu/" + soundToPlay->fileurl;
        } else {
            soundFilePath = std::string(BASE_ASSETS) + soundToPlay->fileurl;
        }

        auto it_state = scriptThreadStates.find(executionThreadId);
        if (it_state == scriptThreadStates.end()) {
            pEngineInstance->EngineStdOut(
                "Entity " + id + " (Thread: " + executionThreadId +
                ") - ScriptThreadState not found for waitforPlaysoundWithFromTo. Cannot set wait.", 2,
                executionThreadId);
            // 스레드 상태가 없으면 사운드만 재생하고 대기 설정 없이 반환
            pEngineInstance->aeHelper.playSoundFromTo(this->getId(), soundFilePath, from, to);
            pEngineInstance->EngineStdOut("Entity " + id + " playing sound (from-to, no wait): " + soundToPlay->name, 0,
                                          executionThreadId);
            return;
        }
        ScriptThreadState &threadState = it_state->second;

        // 새로운 promise와 future 생성
        threadState.completionPromise = {}; // 이전 promise가 있다면 리셋
        threadState.completionFuture = threadState.completionPromise.get_future();

        // AudioEngineHelper의 playSoundFromTo 함수가 이 promise를 알고,
        // 사운드 재생 완료 시 resolve하도록 수정되어야 합니다.
        // 예: pEngineInstance->aeHelper.playSoundFromToAndNotifyPromise(this->getId(), soundFilePath, from, to, std::move(threadState.completionPromise));
        // 현재는 AudioEngineHelper 수정 없이 기존 함수를 호출합니다.
        // 이 경우 future는 자동으로 resolve되지 않으며, resumeSoundWaitScripts에서 isSoundPlaying을 폴링하거나 타임아웃에 의존해야 합니다.
        pEngineInstance->aeHelper.playSoundFromTo(this->getId(), soundFilePath, from, to);

        pEngineInstance->EngineStdOut(
            "Entity " + id + " playing sound (from " + std::to_string(from) + "s to " + std::to_string(to) + "s): " +
            soundToPlay->name + " (ID: " + soundId + ", Path: " + soundFilePath + ")", 0, executionThreadId);

        // SOUND_FINISH 타입으로 대기를 설정합니다.
        // waitEndTime은 SOUND_FINISH 타입에서는 future가 주 메커니즘이므로 참고용입니다.
        double durationSeconds = 0.0;
        if (to > from) {
            durationSeconds = to - from;
        } else {
            // 'to'가 'from'보다 작거나 같으면, 재생 시간이 0이거나 매우 짧을 수 있습니다.
            // 또는 miniaudio의 동작에 따라 'from'부터 끝까지 재생될 수도 있습니다.
            // 여기서는 명시적 구간이므로 (to - from)이 음수/0이면 0으로 처리합니다.
            durationSeconds = 0.0;
            if (pEngineInstance) {
                // pEngineInstance 유효성 검사
                pEngineInstance->EngineStdOut(
                    "Warning: 'to' (" + std::to_string(to) + ") is not greater than 'from' (" + std::to_string(from) +
                    ") for sound " + soundId + ". Effective duration for wait might be 0.", 1, executionThreadId);
            }
        }
        Uint64 estimatedEndTime = SDL_GetTicks() + static_cast<Uint64>(durationSeconds * 1000.0);

        // setScriptWait는 ScriptThreadState를 직접 수정하므로, promise/future 설정 후에 호출합니다.
        setScriptWait(executionThreadId, estimatedEndTime, callingBlockId, WaitType::SOUND_FINISH);
        // ScriptThreadState의 completionFuture는 resumeSoundWaitScripts에서 사용됩니다.
        // AudioEngineHelper가 사운드 완료 시 threadState.completionPromise.set_value()를 호출해야 합니다.

        pEngineInstance->EngineStdOut(
            "Entity " + id + " (Thread: " + executionThreadId + ") waiting for sound (from-to, future set): " +
            soundToPlay->name + " (Block: " + callingBlockId + ")", 0, executionThreadId);
    } else {
        pEngineInstance->EngineStdOut(
            "Entity::waitforPlaysoundWithFromTo - Sound ID '" + soundId + "' not found for entity: " + this->id, 1,
            executionThreadId);
    }
}

void Entity::terminateScriptThread(const std::string &threadId) {
    std::lock_guard lock(m_stateMutex);
    auto it = scriptThreadStates.find(threadId);
    if (it != scriptThreadStates.end()) {
        it->second.terminateRequested = true;
        if (pEngineInstance) {
            // Check pEngineInstance before using
            pEngineInstance->EngineStdOut("Entity " + id + " marked script thread " + threadId + " for termination.", 0,
                                          threadId);
        }
    } else {
        if (pEngineInstance) {
            pEngineInstance->EngineStdOut(
                "Entity " + id + " could not find script thread " + threadId + " to mark for termination.", 1,
                threadId);
        }
    }
}

void Entity::terminateAllScriptThread(const std::string &exceptThreadId) {
    std::lock_guard lock(m_stateMutex);
    int markedCount = 0;
    for (auto &pair: scriptThreadStates) {
        if (exceptThreadId.empty() || pair.first != exceptThreadId) {
            if (!pair.second.terminateRequested) {
                // Only mark if not already marked
                pair.second.terminateRequested = true;
                markedCount++;
                if (pEngineInstance) {
                    pEngineInstance->EngineStdOut(
                        "Entity " + id + " marked script thread " + pair.first + " for termination (all/other).", 0,
                        pair.first);
                }
            }
        }
    }
    if (pEngineInstance && markedCount > 0) {
        // Log only if any threads were actually marked now
        pEngineInstance->EngineStdOut(
            "Entity " + id + " marked " + std::to_string(markedCount) + " script threads for termination" + (
                exceptThreadId.empty() ? "" : " (excluding " + exceptThreadId + ")") + ".", 0);
    }
}

void Entity::processInternalContinuations(float deltaTime) {
    if (!pEngineInstance || pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    // Helper struct for task details
    struct ScriptTaskDetails {
        std::string execId;
        const Script* scriptPtr;
        std::string sceneIdForRun;
    };

    // Helper lambda to truncate strings safely, defined once
    auto truncate_string = [](const std::string &str, size_t max_len) {
        if (str.length() > max_len) {
            return str.substr(0, max_len) + "...(truncated)";
        }
        return str;
    };

    std::vector<ScriptTaskDetails> tasksToRunInline;
    {
        std::lock_guard lock(m_stateMutex);
        for (auto it_state = scriptThreadStates.begin(); it_state != scriptThreadStates.end(); /* manual increment */) {
            auto &execId = it_state->first;
            auto &state = it_state->second;

            if (state.isWaiting && state.currentWaitType == WaitType::BLOCK_INTERNAL) {
                const ObjectInfo *objInfoCheck = pEngineInstance->getObjectInfoById(this->getId());
                bool isGlobal = (objInfoCheck && (objInfoCheck->sceneId == "global" || objInfoCheck->sceneId.empty()));
                std::string engineCurrentScene = pEngineInstance->getCurrentSceneId();
                const std::string &scriptSceneContext = state.sceneIdAtDispatchForResume;

                bool canResume = false;

                if (state.waitEndTime > 0 && SDL_GetTicks() < state.waitEndTime) {
                    ++it_state;
                    continue; // 아직 대기 시간이므로 재개하지 않음
                }
                // 대기 시간이 지났거나, 원래 시간 제한이 없는 BLOCK_INTERNAL 대기였다면, waitEndTime을 초기화합니다.
                if (state.waitEndTime > 0) {
                    if (pEngineInstance) {
                        pEngineInstance->EngineStdOut(
                            "Entity " + getId() + " script thread " + execId +
                            " BLOCK_INTERNAL finished inherited wait. Original inner wait on: " + state.originalInnerBlockIdForWait,
                            3, execId);
                    }
                    state.waitEndTime = 0; // 이 특정 시간 제한 대기는 완료되었으므로 초기화
                }

                if (!state.scriptPtrForResume) {
                    // 스크립트 포인터가 유효하지 않으면 재개 불가
                    canResume = false;
                    if (pEngineInstance) { // pEngineInstance null check before use
                        pEngineInstance->EngineStdOut(
                            "WARNING: Entity " + getId() + " script thread " + execId +
                            " is BLOCK_INTERNAL wait but scriptPtrForResume is null. Clearing wait.", 1, execId);
                    }
                } else if (isGlobal) {
                    canResume = true;
                } else { // Not global
                    canResume = objInfoCheck &&
                                objInfoCheck->sceneId == scriptSceneContext &&
                                engineCurrentScene == scriptSceneContext;
                }

                if (canResume) {
                    tasksToRunInline.emplace_back(ScriptTaskDetails{execId, state.scriptPtrForResume, scriptSceneContext});
                    // tasksToRunInline에 추가했으므로, 여기서는 isWaiting을 false로 바꾸지 않습니다.
                    // executeScript가 호출될 때 isWaiting이 false로 설정됩니다.
                    // 만약 executeScript가 BLOCK_INTERNAL을 다시 설정하면 다음 틱에 다시 처리됩니다.
                    ++it_state;
                } else {
                    // 재개할 수 없는 경우, 대기 상태를 해제하여 무한 루프 방지
                    if (state.scriptPtrForResume && pEngineInstance) { // pEngineInstance null check
                        // 로그는 스크립트 포인터가 있을 때만 의미 있음
                        pEngineInstance->EngineStdOut(
                            "Internal continuation for " + getId() + " (Thread: " + execId +
                            ") cancelled. Scene/Context mismatch or invalid script. EntityScene: " + (
                                objInfoCheck ? objInfoCheck->sceneId : "N/A") + ", ScriptDispatchScene: " +
                            scriptSceneContext + ", EngineCurrentScene: " + engineCurrentScene, 1, execId);
                    }
                    state.isWaiting = false;
                    state.currentWaitType = WaitType::NONE;
                    state.scriptPtrForResume = nullptr;
                    state.sceneIdAtDispatchForResume = "";
                    state.waitEndTime = 0; // 여기서도 waitEndTime 초기화
                    state.originalInnerBlockIdForWait = ""; // 관련 정보 초기화
                    state.resumeAtBlockIndex = -1;
                    ++it_state;
                }
            } else {
                ++it_state;
            }
        }
    } // Mutex scope ends

    // 수집된 작업을 현재 스레드에서 직접 실행
    for (const auto &task: tasksToRunInline) {
        // pEngineInstance->EngineStdOut("Executing internal continuation for entity: " + getId() + " (Thread: " + task.execId + ")", 5, task.execId);

        try {
            this->executeScript(task.scriptPtr, task.execId, task.sceneIdForRun, deltaTime);
        } catch (const ScriptBlockExecutionError &sbee) {
            Entity *entitiyInfo = pEngineInstance->getEntityById(sbee.entityId);
            Omocha::BlockTypeEnum blockTypeEnum = Omocha::stringToBlockTypeEnum(sbee.blockType);
            std::string koreanBlockTypeName = Omocha::blockTypeEnumToKoreanString(blockTypeEnum);

            const size_t MAX_ID_LEN = 128;
            const size_t MAX_MSG_LEN = 512;
            const size_t MAX_NAME_LEN = 256;

            std::string entityNameStr = "[정보 없음]";
            if (entitiyInfo) {
                entityNameStr = truncate_string(entitiyInfo->getName(), MAX_NAME_LEN);
            } else {
                entityNameStr = "[객체 ID: " + truncate_string(sbee.entityId, MAX_ID_LEN) + " 찾을 수 없음]";
            }

            std::string detailedErrorMessage = "블럭 을 실행하는데 오류가 발생하였습니다. 블럭ID " + truncate_string(
                                                   sbee.blockId, MAX_ID_LEN) +
                                               " 의 타입 " + truncate_string(koreanBlockTypeName, MAX_ID_LEN) +
                                               (blockTypeEnum == Omocha::BlockTypeEnum::UNKNOWN && !sbee.blockType.
                                                empty()
                                                    ? " (원본: " + truncate_string(sbee.blockType, MAX_ID_LEN) + ")"
                                                    : "") +
                                               " 에서 사용 하는 객체 " + "(" + entityNameStr + ")" +
                                               "\n원본 오류: " + truncate_string(sbee.originalMessage, MAX_MSG_LEN);

            pEngineInstance->EngineStdOut(
                "Script Execution Error (InternalContinuation, Thread " + task.execId + "): " + detailedErrorMessage, 2,
                task.execId);
            if (pEngineInstance->showMessageBox("블럭 처리 오류\n" + detailedErrorMessage,
                                                pEngineInstance->msgBoxIconType.ICON_ERROR)) {
                exit(EXIT_FAILURE);
            }
        }
        catch (const std::exception &e) {
            const size_t MAX_ID_LEN = 128;
            const size_t MAX_MSG_LEN = 512;

            std::string entityIdStr = truncate_string(getId(), MAX_ID_LEN);
            std::string threadIdStr = truncate_string(task.execId, MAX_ID_LEN);
            std::string exceptionWhatStr = truncate_string(e.what(), MAX_MSG_LEN);

            std::string detailedErrorMessage = "Generic exception caught in internal continuation for entity " +
                                               entityIdStr +
                                               " (Thread: " + threadIdStr + "): " + exceptionWhatStr;

            pEngineInstance->EngineStdOut(detailedErrorMessage, 2, task.execId);
        }
        catch (...) {
            pEngineInstance->EngineStdOut(
                "Unknown exception caught in internal continuation for entity " + getId() +
                " (Thread: " + task.execId + ")",
                2, task.execId);
        }
    }
}
// 수정된 함수
void Entity::resumeExplicitWaitScripts(float deltaTime) {
    if (!pEngineInstance || pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    std::vector<ScriptTask> tasksToDispatch; {
        std::lock_guard lock(m_stateMutex);
        // 반복자 증가를 루프 선언부로 옮겨 가독성 및 안정성 향상
        for (auto it = scriptThreadStates.begin(); it != scriptThreadStates.end(); ++it) {
            auto &execId = it->first;
            auto &state = it->second;

            // EXPLICIT_WAIT_SECOND 상태가 아니거나 아직 대기 중이면 다음 스레드로 넘어감
            if (!state.isWaiting || state.currentWaitType != WaitType::EXPLICIT_WAIT_SECOND) {
                continue;
            }
            if (SDL_GetTicks() < state.waitEndTime) {
                continue; // 아직 대기 시간 남음
            }

            // 대기 시간 종료됨
            if (state.scriptPtrForResume && state.resumeAtBlockIndex != -1) {
                // 로그 메시지 구성 시 std::ostringstream 사용으로 효율성 증대
                std::ostringstream oss;
                oss << "Entity " << id << " (Thread: " << execId
                        << ") finished EXPLICIT_WAIT_SECOND for block " << state.blockIdForWait
                        << ". Resuming.";
                pEngineInstance->EngineStdOut(oss.str(), 0, execId);

                tasksToDispatch.emplace_back(ScriptTask{
                    execId, state.scriptPtrForResume, state.sceneIdAtDispatchForResume, state.resumeAtBlockIndex
                });

                // 성공적으로 재개 준비가 된 스크립트의 상태 초기화
                state.isWaiting = false;
                state.waitEndTime = 0;
                state.currentWaitType = WaitType::NONE;
                // blockIdForWait, scriptPtrForResume 등은 디스패치된 태스크에서 사용되거나
                // executeScript에 의해 재설정될 것이므로 여기서는 초기화하지 않음 (원본 로직 유지)
            } else {
                std::ostringstream oss;
                oss << "WARNING: Entity " << id << " script thread " << execId
                        << " EXPLICIT_WAIT_SECOND finished but missing resume context. Clearing wait.";
                pEngineInstance->EngineStdOut(oss.str(), 1, execId);

                // 재개 컨텍스트가 없어 대기를 취소하는 경우, 관련 상태 명시적 초기화
                state.isWaiting = false;
                state.waitEndTime = 0; // 원본 코드에서 누락되었던 부분 추가
                state.currentWaitType = WaitType::NONE;
                state.scriptPtrForResume = nullptr;
                state.sceneIdAtDispatchForResume.clear(); // std::string은 clear() 사용
                state.resumeAtBlockIndex = -1;
                // blockIdForWait는 원본 로직에 따라 여기서 초기화하지 않음
            }
        }
    }

    for (const auto &task: tasksToDispatch) {
        // scheduleScriptExecutionOnPool 호출 시 execId를 전달하여
        // 해당 스레드의 상태(예: resumeAtBlockIndex)가 executeScript 내에서 설정될 것을 기대
        this->scheduleScriptExecutionOnPool(task.script, task.sceneId, deltaTime, task.execId);
    }
}

void Entity::resumeSoundWaitScripts(float deltaTime) {
    if (!pEngineInstance || pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed)) {
        return;
    }

    std::vector<std::tuple<std::string, const Script *, std::string, int> > tasksToDispatch;
    // execId, script, sceneId, resumeIndex

    {
        std::lock_guard lock(m_stateMutex);
        for (auto it = scriptThreadStates.begin(); it != scriptThreadStates.end(); /* no increment here */) {
            auto &execId = it->first;
            auto &state = it->second;

            if (state.isWaiting && state.currentWaitType == WaitType::SOUND_FINISH) {
                // Check if the sound associated with this entity (and potentially this specific wait) has finished.
                // AudioEngineHelper::isSoundPlaying(this->getId()) checks if *any* sound for this entity is playing.
                // For more precise control, you might need to store a specific sound handle/ID in ScriptThreadState.
                if (!pEngineInstance->aeHelper.isSoundPlaying(this->getId())) {
                    if (state.scriptPtrForResume && state.resumeAtBlockIndex != -1) {
                        pEngineInstance->EngineStdOut(
                            "Entity " + id + " (Thread: " + execId + ") finished SOUND_FINISH for block " + state.
                            blockIdForWait + ". Resuming.", 0, execId);
                        tasksToDispatch.emplace_back(execId, state.scriptPtrForResume, state.sceneIdAtDispatchForResume,
                                                     state.resumeAtBlockIndex);

                        state.isWaiting = false;
                        state.currentWaitType = WaitType::NONE;
                        // blockIdForWait, scriptPtrForResume, etc., will be used by the dispatched task or reset by executeScript.
                        ++it;
                    } else {
                        pEngineInstance->EngineStdOut(
                            "WARNING: Entity " + id + " script thread " + execId +
                            " SOUND_FINISH finished but missing resume context. Clearing wait.", 1, execId);
                        state.isWaiting = false;
                        state.currentWaitType = WaitType::NONE;
                        // Clear other relevant state fields
                        ++it;
                    }
                } else {
                    ++it; // Still waiting for sound to finish
                }
            } else {
                ++it;
            }
        }
    }

    for (const auto &task: tasksToDispatch) {
        const std::string &execId = std::get<0>(task);
        // Reschedule the script execution.
        this->scheduleScriptExecutionOnPool(std::get<1>(task), std::get<2>(task), deltaTime, execId);
    }
}

void Entity::scheduleScriptExecutionOnPool(const Script *scriptPtr,
                                           const std::string &sceneIdAtDispatch,
                                           float deltaTime,
                                           const std::string &existingExecutionThreadId) {
    if (!pEngineInstance) {
        // This error can occur if the Entity was not created correctly.
        // In actual production, a more robust logging/error handling mechanism may be needed.
        std::cerr << "CRITICAL ERROR: Entity " << this->id
                << " has no valid pEngineInstance. Cannot schedule script execution." << std::endl; // LCOV_EXCL_LINE
        // In addition to simple console output, it is recommended to use the Engine's logger if available.
        // (However, direct calls are not possible as pEngineInstance is null)
        return;
    }

    if (!scriptPtr) {
        pEngineInstance->EngineStdOut(
            "Entity::scheduleScriptExecutionOnPool - null script pointer for entity: " + this->id, 2,
            existingExecutionThreadId);
        return;
    }

    // 새 스크립트의 경우, 실행할 블록이 있는지 확인합니다.
    // (첫 번째 블록은 보통 이벤트 트리거이므로, 1개 초과여야 실행 가능)
    if (existingExecutionThreadId.empty() && (scriptPtr->blocks.empty() || scriptPtr->blocks.size() <= 1)) {
        pEngineInstance->EngineStdOut(
            "Entity::scheduleScriptExecutionOnPool - Script for entity " + this->id +
            " has no executable blocks. Skipping.", 1, existingExecutionThreadId);
        return;
    }

    std::string execIdToUse;
    bool isResumedScript = !existingExecutionThreadId.empty();

    if (isResumedScript) {
        execIdToUse = existingExecutionThreadId;
    } else {
        // 새 스크립트 실행을 위한 ID 생성
        // 이전 Engine::dispatchScriptForExecution의 ID 생성 로직을 따르되, Engine의 카운터를 추가하여 고유성을 강화합니다.
        // 참고: 이 ID 생성 방식은 호출 스레드의 ID 해시를 사용하므로, 동시에 여러 스크립트가 시작될 경우 완벽한 고유성을 보장하지 않을 수 있습니다.
        // 더 강력한 고유 ID가 필요하다면 UUID 등의 사용을 고려해야 합니다.
        std::thread::id physical_thread_id = std::this_thread::get_id();
        std::stringstream ss_full_hex;
        ss_full_hex << std::hex << std::hash<std::thread::id>{}(physical_thread_id);
        std::string full_hex_str = ss_full_hex.str();
        std::string short_hex_str;

        if (full_hex_str.length() >= 4) {
            short_hex_str = full_hex_str.substr(0, 4);
        } else {
            short_hex_str = std::string(4 - full_hex_str.length(), '0') + full_hex_str;
        }
        // Engine의 카운터를 사용하여 ID의 고유성을 더욱 강화합니다.
        execIdToUse = "script_" + short_hex_str + "_" +
                      std::to_string(pEngineInstance->getNextScriptExecutionCounter());
    }

    // 스레드 풀에 작업을 게시합니다.
    // 람다 함수는 필요한 변수들을 캡처합니다. pEngineInstance는 this를 통해 접근 가능합니다.
    pEngineInstance->submitTask(
        [self = shared_from_this(), scriptPtr, sceneIdAtDispatch, deltaTime, execIdToUse, isResumedScript]() {
            if (self->pEngineInstance->m_isShuttingDown.load(std::memory_order_relaxed)) {
                return;
            }

            // 스크립트 시작 또는 재개 로깅
            if (isResumedScript) {
                self->pEngineInstance->EngineStdOut(
                    "Entity " + self->getId() + " resuming script (Thread: " + execIdToUse + ")", 5, execIdToUse);
            } else {
                self->pEngineInstance->EngineStdOut(
                    "Entity " + self->getId() + " starting new script (Thread: " + execIdToUse + ")", 5, execIdToUse);
            }

            try {
                // 실제 스크립트 실행
                self->executeScript(scriptPtr, execIdToUse, sceneIdAtDispatch, deltaTime);
            } catch (const ScriptBlockExecutionError &sbee) {
                Entity *entity = self->pEngineInstance->getEntityById(sbee.entityId);
                // Handle script block execution errors
                Omocha::BlockTypeEnum blockTypeEnum = Omocha::stringToBlockTypeEnum(sbee.blockType);
                std::string koreanBlockTypeName = Omocha::blockTypeEnumToKoreanString(blockTypeEnum);
                // Configure error message (sbee.entityId could be the entity referenced by the block where the error occurred,
                // so explicitly stating self->id for the entity executing the script might be clearer.)
                std::string detailedErrorMessage = "블록 을 실행하는데 오류가 발생하였습니다.\n(스크립트 소유 객체: " + self->getId() +
                                                   " 블록ID: " + sbee.blockId +
                                                   ") 의 타입 (" + koreanBlockTypeName + ")" +
                                                   (blockTypeEnum == Omocha::BlockTypeEnum::UNKNOWN && !sbee.blockType.
                                                    empty()
                                                        ? " (원본: " + sbee.blockType + ")"
                                                        : "") +
                                                   " 에서 사용 하는 객체 (" + entity->getName() +
                                                   // Object ID directly referenced by the error block
                                                   ")\n원본 오류: " + sbee.originalMessage;

                // EngineStdOut은 이미 상세 메시지를 포함하므로, 여기서는 요약된 메시지 또는 상세 메시지 그대로 사용
                self->pEngineInstance->EngineStdOut(
                    "Script Execution Error (Entity: " + self->getId() + ", Thread " + execIdToUse + "): " +
                    detailedErrorMessage, 2, execIdToUse);
                self->pEngineInstance->showMessageBox(detailedErrorMessage,
                                                      self->pEngineInstance->msgBoxIconType.ICON_ERROR);
                exit(EXIT_FAILURE); // 프로그램 종료
            }
            catch (const std::length_error &le) {
                // Specifically catch std::length_error
                self->pEngineInstance->EngineStdOut(
                    "std::length_error caught in script for entity " + self->getId() +
                    " (Thread: " + execIdToUse + "): " + le.what(),
                    2, execIdToUse);
                // Optionally, show a message box or perform other error handling
                // self->pEngineInstance->showMessageBox("문자열 처리 중 오류가 발생했습니다: " + std::string(le.what()), self->pEngineInstance->msgBoxIconType.ICON_ERROR);
            }
            catch (const std::exception &e) {
                // Handle general C++ exceptions
                self->pEngineInstance->EngineStdOut(
                    "Generic exception caught in script for entity " + self->getId() +
                    " (Thread: " + execIdToUse + "): " + e.what(),
                    2, execIdToUse);
            }
            catch (...) {
                // Handle other unknown exceptions
                self->pEngineInstance->EngineStdOut(
                    "Unknown exception caught in script for entity " + self->getId() +
                    " (Thread: " + execIdToUse + ")",
                    2, execIdToUse);
            }
        });
}

void Entity::setText(const std::string &newText) {
    if (pEngineInstance) {
        std::lock_guard lock(pEngineInstance->m_engineDataMutex);
        // Engine 클래스를 통해 ObjectInfo의 textContent를 업데이트합니다.
        pEngineInstance->updateEntityTextContent(this->id, newText);
    } else {
        // pEngineInstance가 null인 경우 오류 로깅 (이 경우는 거의 없어야 함)
        std::cerr << "Error: Entity " << id << " has no pEngineInstance to set text." << std::endl;
    }
}

void Entity::appendText(const std::string &textToAppend) {
    if (pEngineInstance) {
        std::lock_guard lock(pEngineInstance->m_engineDataMutex);
        ObjectInfo *objInfo = const_cast<ObjectInfo *>(pEngineInstance->getObjectInfoById(this->id));
        if (objInfo) {
            if (objInfo->objectType == "textBox") {
                std::string currentText = objInfo->textContent;
                std::string newText = currentText + textToAppend;
                pEngineInstance->updateEntityTextContent(this->id, newText); // 업데이트된 전체 텍스트로 설정
            } else {
                pEngineInstance->EngineStdOut("Warning: Entity " + id + " is not a textBox. Cannot append text.", 1);
            }
        } else {
            pEngineInstance->EngineStdOut(
                "Warning: ObjectInfo not found for entity " + id + " when trying to append text.", 1);
        }
    } else {
        std::cerr << "Error: Entity " << id << " has no pEngineInstance to append text." << std::endl;
    }
}

void Entity::prependText(const std::string &prependToText) {
    if (pEngineInstance) {
        std::lock_guard lock(pEngineInstance->m_engineDataMutex);
        // 1. 현재 ObjectInfo 가져오기
        ObjectInfo *objInfo = const_cast<ObjectInfo *>(pEngineInstance->getObjectInfoById(this->id));
        if (objInfo) {
            if (objInfo->objectType == "textBox") {
                std::string currentText = objInfo->textContent;
                std::string newText = prependToText + currentText;
                pEngineInstance->updateEntityTextContent(this->id, newText); // 업데이트된 전체 텍스트로 설정
            } else {
                pEngineInstance->EngineStdOut("Warning: Entity " + id + " is not a textBox. Cannot append text.", 1);
            }
        } else {
            pEngineInstance->EngineStdOut(
                "Warning: ObjectInfo not found for entity " + id + " when trying to append text.", 1);
        }
    } else {
        std::cerr << "Error: Entity " << id << " has no pEngineInstance to append text." << std::endl;
    }
}

/**
 * @brief 사이즈 가져오기 (엔트리)
 * @param toFixedSize 자리수
 */
double Entity::getSize(bool toFixedSize) const {
    std::lock_guard lock(m_stateMutex);
    // 스테이지 너비를 기준으로 현재 엔티티의 시각적 너비가 차지하는 비율을 계산합니다.
    // this->width는 엔티티의 원본 이미지 너비입니다.
    // this->getScaleX()는 원본 이미지 너비에 대한 스케일 팩터입니다.
    // 따라서 (this->width * std::abs(this->getScaleX())) 가 현재 시각적 너비입니다.

    double visualWidth = this->width * std::abs(this->getScaleX());
    double stageWidth = Engine::getProjectstageWidth(); // Engine 클래스에서 스테이지 너비 가져오기
    if (pEngineInstance) {
        // 디버깅 로그 추가
        pEngineInstance->EngineStdOut(
            format("Entity::getSize - Debug - visualWidth: {}, stageWidth: {}, this->width: {}, this->getScaleX(): {}",
                   visualWidth, stageWidth, this->width, this->getScaleX()), 3);
    }
    double current_percentage = 0.0;
    current_percentage = std::abs(this->getScaleX()) * 50.0;

    // pEngineInstance가 유효한지 확인 후 로그 출력
    if (pEngineInstance) {
        // pEngineInstance->EngineStdOut(format("Entity::getSize W={} H={} V={}", visualWidth, visualHeight, value), 3); // 이전 로그
        // pEngineInstance->EngineStdOut(format("Entity::getSize OW={} OH={}", this->getWidth(),this->getHeight()), 3); // 이전 로그
        pEngineInstance->EngineStdOut(
            format("Entity::getSize - ScaleX: {}, Calculated Percentage: {}, objectId: {}, name: {}", this->getScaleX(),
                   current_percentage, this->getId(), this->getName()), 3);
    }

    if (toFixedSize) {
        // 엔트리는 크기 값을 소수점 첫째 자리까지 표시합니다. (예: 123.4%)
        return std::round(current_percentage * 10.0) / 10.0;
    }
    return current_percentage;
}

/**
 * @bref 크기 정하기 (엔트리)
 * @param size 크기
 */
void Entity::setSize(double size) {
    std::lock_guard lock(m_stateMutex);

    double stageWidth = Engine::getProjectstageWidth(); // Engine 클래스에서 스테이지 너비 가져오기
    double targetVisualWidth = stageWidth * (size / 50.0);
    double newCalculatedScaleX = 1.0; // 기본값
    newCalculatedScaleX = targetVisualWidth / this->width;
    if (!this->m_isClone) {
        // 복제본 이 아닌경우 변경
        this->setScaleX(newCalculatedScaleX);
        this->setScaleY(newCalculatedScaleX); // 가로세로 비율 유지를 위해 동일한 스케일 적용
    }
    if (pEngineInstance) {
        // pEngineInstance 유효성 검사 추가
        pEngineInstance->EngineStdOut(format("Entity::setSize TargetPercentage: {} NewScaleFactor: {}", size,
                                             newCalculatedScaleX), 3);
    }
}

/**
 * @bref 크기 리셋 (엔트리)
 */
void Entity::resetSize() {
    lock_guard lock(m_stateMutex);
    if (!this->m_isClone) {
        // 복제본이 아닌 경우, 저장된 원본 스케일로 복원합니다.
        // 이 원본 스케일은 엔티티 생성 시 project.json에서 로드된 값입니다.
        this->setScaleX(this->OrigineScaleX);
        this->setScaleY(this->OrigineScaleY);
    }
}

void Entity::clearAllScriptStates() {
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex); // scriptThreadStates 접근 보호
    for (auto& pair : scriptThreadStates) {
        auto& state = pair.second;
        state.isWaiting = false;
        state.waitEndTime = 0;
        state.blockIdForWait.clear();
        state.currentWaitType = WaitType::NONE;
        state.resumeAtBlockIndex = -1;
        state.scriptPtrForResume = nullptr;
        state.sceneIdAtDispatchForResume.clear();
        state.originalInnerBlockIdForWait.clear();
        state.loopCounters.clear();
        state.breakLoopRequested = false;
        state.continueLoopRequested = false;
        // completionPromise와 future는 setScriptWait에서 새로 생성되므로 여기서 특별히 리셋할 필요는 없을 수 있습니다.
        // terminateRequested 플래그는 terminateAllScriptThread에서 이미 설정되었으므로 여기서는 건드리지 않습니다.
    }
    // scriptThreadStates.clear(); // 맵 자체를 비우는 대신 상태만 초기화하는 것이 더 안전할 수 있습니다.
    // 만약 스레드 ID가 재사용되지 않는다면 .clear()도 고려 가능합니다.
    if (pEngineInstance) {
        pEngineInstance->EngineStdOut("Cleared all script states for entity " + id, 0);
    }
}
