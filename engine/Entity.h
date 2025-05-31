#pragma once

#include <stdexcept> // For std::runtime_error
#include <string>
#include <vector>
#include <mutex>             // For std::mutex
#include "SDL3/SDL_pixels.h" // For SDL_Color (already included)
#include "SDL3/SDL_rect.h"   // For SDL_FRect, SDL_FPoint
#include "SDL3/SDL_render.h" // For SDL_Texture, SDL_Vertex
#include <map>               // For std::map

// Forward declaration
class Engine;
struct Script; // Forward declaration for Script
class Block;   // Forward declaration for Block
// 사용자 정의 예외: 스크립트 블록 실행 중 발생하는 오류를 위한 클래스
class ScriptBlockExecutionError : public std::runtime_error
{
public:
    std::string blockId;
    std::string blockType;
    std::string entityId;
    std::string originalMessage; // 원본 std::exception의 what() 메시지

    // 생성자: 예외 상태만 초기화
    ScriptBlockExecutionError(const std::string &shortErrorDescription, // 사용자에게 표시될 간략한 오류 메시지
                              const std::string &bId,
                              const std::string &bType,
                              const std::string &eId,
                              const std::string &detailedOrigMsg) // 내부/원본 오류 메시지 (runtime_error 기본 생성자용 메시지)
        : std::runtime_error(detailedOrigMsg), // Pass detailedOrigMsg or a combination to base
          blockId(bId), blockType(bType), entityId(eId), originalMessage(detailedOrigMsg)
    {
        // 생성자 내에서 showMessageBox 및 exit 호출 제거
    }
};
struct ScriptWaitState
{
    bool isWaiting = false;
    Uint32 waitEndTime = 0;
    std::string waitingForBlockId = "";
    // std::string waitingOnExecutionThreadId = "";

    ScriptWaitState() : isWaiting(false), waitEndTime(0) {}

    void reset()
    {
        isWaiting = false;
        waitEndTime = 0;
        waitingForBlockId = "";
        // waitingOnExecutionThreadId = "";
    }
};

class Entity
{
public:
    // 스크립트 대기 유형 정의
    enum class WaitType
    {
        NONE,                 // 대기 상태 아님
        EXPLICIT_WAIT_SECOND, // 'wait_second' 블록에 의한 명시적 시간 대기
        BLOCK_INTERNAL,       // 'move_xy_time' 등 시간 소요 블록 내부의 프레임 간 대기
        TEXT_INPUT,           // 'ask_and_wait' 블록에 의한 사용자 입력 대기
        // 필요에 따라 다른 대기 유형 추가 가능
    };

    // 각 스크립트 스레드의 상태를 관리하는 구조체
    struct ScriptThreadState
    {
        // ... 기존 스레드 상태 변수들 (예: currentBlockIndex, scriptStack 등) ...
        bool isWaiting = false;
        Uint32 waitEndTime = 0;
        std::string blockIdForWait = "";           // 어떤 블록에 의해 대기가 시작되었는지 식별
        WaitType currentWaitType = WaitType::NONE; // 현재 대기 유형
        int resumeAtBlockIndex = -1;               // executeBlocksSynchronously 내부에서 대기 발생 시 재개할 블록 인덱스 (필요시)
        const Script* scriptPtrForResume = nullptr;      // BLOCK_INTERNAL 재개를 위한 스크립트 포인터
        // int loopCounter = 0; // Replaced by loopCounters map
        bool terminateRequested  = false;
        std::map<std::string, int> loopCounters; // Key: loop block_id, Value: current_iteration_index
        std::string sceneIdAtDispatchForResume = ""; // BLOCK_INTERNAL 재개를 위한 씬 ID
        bool breakLoopRequested = false; // Flag to signal a 'stop_repeat' or break
        bool continueLoopRequested = false; // Flag to signal a 'continue_repeat'

        ScriptThreadState() : isWaiting(false), waitEndTime(0), currentWaitType(WaitType::NONE), resumeAtBlockIndex(-1) {}
    };
    std::map<std::string, ScriptThreadState> scriptThreadStates;
    enum class RotationMethod
    {
        NONE,       // 회전 없음
        FREE,       // 자유 회전
        VERTICAL,   // 상하 방향으로만 (좌우 반전)
        HORIZONTAL, // 좌우 방향으로만 (상하 반전)
                    // DIAGONAL, // 엔트리에 해당 옵션이 있는지 확인 필요
                    // CIRCLE,   // 엔트리에 해당 옵션이 있는지 확인 필요
        UNKNOWN
    };

    // bounce_wall 블록에서 사용될 충돌 방향 열거형
    enum class CollisionSide
    {
        NONE,
        UP,
        DOWN,
        LEFT,
        RIGHT
    };
    struct TimedRotationState
    {
        bool isActive = false;
        float totalDurationSeconds = 0.0f; // 총 회전 시간 (초)
        float elapsedSeconds = 0.0f;       // 경과 시간 (초)
        double totalAngleToRotate = 0.0;   // 총 회전해야 할 각도
        double totalFrames = 0.0;          // 추가: 총 프레임 수
        double remainingFrames = 0.0;      // 추가: 남은 프레임 수
        double dAngle = 0.0;               // 추가: 프레임당 회전 각도
        std::string blockIdForWait = ""; // 이 블록의 ID 저장
    };
    // move_xy_time 블록과 같이 시간이 걸리는 이동을 위한 상태 저장 구조체
    struct TimedMoveState
    {
        bool isActive = false;               // 현재 이 timed move가 활성 상태인지 여부
        double startX = 0.0, startY = 0.0;   // 시작 위치
        double targetX = 0.0, targetY = 0.0; // 목표 위치
        float totalDurationSeconds = 0.0f;   // 총 이동 시간 (초)
        float elapsedSeconds = 0.0f;         // 경과 시간 (초)
        double totalFrames = 0.0;            // 추가: 총 프레임 수
        double remainingFrames = 0.0;        // 추가: 남은 프레임 수
        std::string blockIdForWait = ""; // 이 블록의 ID 저장
    };
    // locate_object_time 블록을 위한 상태 저장 구조체
    struct TimedMoveToObjectState
    {
        bool isActive = false;
        std::string targetObjectId;          // 이동 목표 Entity ID
        float totalDurationSeconds = 0.0f;   // 총 이동 시간 (초)
        float elapsedSeconds = 0.0f;         // 경과 시간 (초)
        double startX = 0.0, startY = 0.0;   // 이 이동 시작 시점의 현재 엔티티 위치
                                             // 목표 엔티티의 위치는 매 프레임 가져옴
        double totalFrames = 0.0;             // 추가: 총 프레임 수
        double remainingFrames = 0.0;         // 추가: 남은 프레임 수
        std::string blockIdForWait = "";     // 이 블록의 ID 저장
    };

    // 스크립트 대기 설정 함수 (시그니처 변경)
    void setScriptWait(const std::string &executionThreadId, Uint32 endTime, const std::string &blockId, WaitType type);

    // 스크립트 대기 상태 확인 함수 (신규)
    bool isScriptWaiting(const std::string &executionThreadId) const;

    // (선택적) 대기를 유발한 블록 ID를 가져오는 함수
    std::string getWaitingBlockId(const std::string &executionThreadId) const;
    WaitType getCurrentWaitType(const std::string &executionThreadId) const;

    struct DialogState
    {
        bool isActive = false;
        std::string text;
        std::string type; // "speak" or "think"
        SDL_Texture *textTexture = nullptr;
        SDL_FRect textRect;
        SDL_FRect bubbleScreenRect;
        SDL_Vertex tailVertices[3]; // SDL_Vertex 타입의 배열로 수정
        bool needsRedraw = true;

        Uint64 startTimeMs = 0;
        // Uint64 durationMs = 0; // Changed to totalDurationMs and remainingDurationMs
        Uint64 totalDurationMs = 0;    // The original duration set for the dialog
        float remainingDurationMs = 0.0f; // Countdown timer, in milliseconds
        DialogState() = default;

        ~DialogState()
        {
            if (textTexture)
            {
                SDL_DestroyTexture(textTexture);
                textTexture = nullptr;
            }
        }

        void clear();
    };

    struct PenState
    {
        Engine *pEngine = nullptr;
        bool stop = false; // true이면 그리기가 중지된 상태, false이면 활성화. JS의 stop과 동일.
        bool isPenDown = false;
        SDL_FPoint lastStagePosition;
        SDL_Color color;
        // float thickness = 1.0f; // For future

        PenState(Engine *enginePtr);

        void setPenDown(bool down, float currentStageX, float currentStageY);
        void updatePositionAndDraw(float newStageX, float newStageY);
        void setStop(bool shouldStop) { stop = shouldStop; } // isActive 대신 stop 상태를 설정
        bool isStopped() const { return stop; }              // 현재 중지 상태인지 확인
        void setColor(const SDL_Color &c) { color = c; }
        void reset(float currentStageX, float currentStageY); // To set initial position
    };

private:
    std::string id;
    std::string name;
    double x;
    double y;
    double regX;
    double regY;
    double scaleX;
    double scaleY;
    double rotation;
    double direction;
    double width;
    double height;
    bool visible;
    RotationMethod rotateMethod;
    // Effects
    // Brightness: -100 (어둡게) to 0 (원본) to +100 (밝게)
    double m_effectBrightness;
    // Alpha: 0.0 (완전 투명) to 1.0 (완전 불투명)
    double m_effectAlpha; // LCOV_EXCL_LINE
    // Hue: 0-359 degrees offset for color effect
    double m_effectHue;
    // enum class CollisionSide { NONE, UP, DOWN, LEFT, RIGHT }; // 중복 선언 제거, 위로 이동    
    CollisionSide lastCollisionSide = CollisionSide::NONE;
    mutable std::recursive_mutex m_stateMutex; // std::mutex에서 std::recursive_mutex로 변경
    bool m_isClone = false;
    std::string m_originalClonedFromId = "";
public:                      // Made brush and paint public for now for easier access from blocks
    Engine *pEngineInstance; // Store a pointer to the engine instance
    // Explicitly delete copy constructor and copy assignment operator
    // to prevent copying of Entity objects, which contain a non-copyable std::mutex member.
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    // Optionally, define or default move constructor and move assignment operator if needed
    Entity(Entity&&) = default;
    Entity& operator=(Entity&&) = default;
    PenState brush;

    TimedMoveState timedMoveState; // timed move 상태 변수 추가
    TimedMoveToObjectState timedMoveObjState;
    TimedRotationState timedRotationState;
    PenState paint;
    // m_stateMutex에 대한 public 접근자 추가 (주의해서 사용) - 반환 타입도 변경
    std::recursive_mutex& getStateMutex() const { return m_stateMutex; }
    bool getIsClone() const { return m_isClone; }
    void setIsClone(bool isClone, const std::string& originalId=""){
        m_isClone = isClone;
        m_originalClonedFromId = originalId;
    };

public:
    Entity(Engine *engine, const std::string &entityId, const std::string &entityName,
           double initial_x, double initial_y, double initial_regX, double initial_regY,
           double initial_scaleX, double initial_scaleY, double initial_rotation, double initial_direction,
           double initial_width, double initial_height, bool initial_visible, RotationMethod rotationMethod);

    ~Entity();
    DialogState m_currentDialog;
    std::map<std::string, ScriptWaitState> scriptWaitStates;
    CollisionSide getLastCollisionSide() const;
    void performActiveWait(const std::string &executionThreadId, const std::string &waitedBlockId, Uint32 waitEndTime, Engine *pEngine, const std::string &sceneIdAtDispatchForWait);
     void scheduleScriptExecutionOnPool(const Script* scriptPtr,
                                       const std::string& sceneIdAtDispatch,
                                       float deltaTime,
                                       const std::string& existingExecutionThreadId);
public:
    void executeScript(const Script *scriptPtr, const std::string &executionThreadId, const std::string &sceneIdAtDispatch, float deltaTime);
    void setLastCollisionSide(CollisionSide side);
    void showDialog(const std::string &message, const std::string &dialogType, Uint64 duration);
    void removeDialog();
    void update(float deltaTime);
    void updateDialog(float deltaTime); // Changed from Uint64 currentTimeMs
    void resumeInternalBlockScripts(float deltaTime); // 추가: BLOCK_INTERNAL 상태의 스크립트 재개
    bool hasActiveDialog() const;
    bool isPointInside(double pX, double pY) const;
    const std::string &getId() const;
    const std::string &getName() const;
    double getX() const;
    double getY() const;
    double getRegX() const;
    double getRegY() const;
    double getScaleX() const;
    double getScaleY() const;
    double getRotation() const;
    double getDirection() const;
    double getWidth() const;
    double getHeight() const;
    bool isVisible() const;
    SDL_FRect getVisualBounds() const; // 추가
    Entity::RotationMethod getRotateMethod() const;
    void setRotateMethod(RotationMethod method);
    void setX(double newX);
    void setY(double newY);
    void setRegX(double newRegX);
    void setRegY(double newRegY);
    void setScaleX(double newScaleX);
    void setText(const std::string &text);
    void appendText(const std::string& textToAppend); // 텍스트 추가 메서드 선언
    void prependText(const std::string& prependToText);
    void setScaleY(double newScaleY);
    void setRotation(double newRotation);
    void setDirection(double newDirection);
    void setWidth(double newWidth);
    void setHeight(double newHeight);
    void setVisible(bool newVisible);
    void resetScaleSize();
    // Effect getters/setters
    double getEffectBrightness() const;
    void setEffectBrightness(double brightness);
    double getEffectAlpha() const;
    void setEffectAlpha(double alpha);
    std::string getOriginalClonedFromId() const { return m_originalClonedFromId; }
    double getEffectHue() const;
    void setEffectHue(double hue);
    void playSound(const std::string &soundId);
    void playSoundWithSeconds(const std::string &soundId, double seconds);
    void playSoundWithFromTo(const std::string &soundId, double from, double to);
    void waitforPlaysound(const std::string &soundId);
    void waitforPlaysoundWithSeconds(const std::string &soundId, double seconds);
    void waitforPlaysoundWithFromTo(const std::string &soundId, double from, double to);
    void terminateScriptThread(const std::string& threadId);
    void terminateAllScriptThread(const std::string& execeptThreadId);
    // void terminateAllScriptThread(const std::string& exceptThreadId = ""); // Declaration with default argument

    void cleanupTerminatedScriptThreads(Engine* enginePtr);
};

// Declare BlockTypeEnumToString as a free function
std::string BlockTypeEnumToString(Entity::WaitType type);
