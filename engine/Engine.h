#pragma once
#include "version_config.h"
#include <string>
#include <map>
#include <Windows.h>
#include <vector>
#include "Entity.h"
#include "SDL3/SDL.h"             // SDL 코어
#include "SDL3/SDL_touch.h"
#include "SDL3_image/SDL_image.h" //SDL 이미지
#include "SDL3_ttf/SDL_ttf.h"     //SDL TTF
#include "util/AEhelper.h"
#include "SDL3/SDL_stdinc.h"   // For SDL_PI_D
#include "SDL3/SDL_render.h"   // SDL 렌더링
#include "TextInput.h" // 텍스트 입력 관련 인터페이스
#include "SDL3/SDL_scancode.h" // For SDL_Scancode
#include "blocks/Block.h"
#include <nlohmann/json.hpp>
#include "blocks/blockTypes.h"
#include "util/fontName.h"
#include "../util/Logger.h"
#include <mutex>
#include <queue>
#include <condition_variable>
#include <functional>               // For std::function
#include <memory>                     // For std::unique_ptr
#include <regex>
using namespace std;
#include <set>      // For std::set
#include <atomic> // For std::atomic

constexpr int WINDOW_WIDTH = 480 * 3;
constexpr int WINDOW_HEIGHT = 270 * 3;
static constexpr int PROJECT_STAGE_WIDTH = 480;  // 실제 프로젝트의 가로 크기에 맞춤
static constexpr int PROJECT_STAGE_HEIGHT = 270; // 실제 프로젝트의 세로 크기(16:9 비율)에 맞춤
static constexpr int INTER_RENDER_WIDTH = PROJECT_STAGE_WIDTH * 3;
static constexpr int INTER_RENDER_HEIGHT = PROJECT_STAGE_HEIGHT *3;

// HUD 상수 정의
static const int SLIDER_X = 10;
static const int SLIDER_Y = WINDOW_HEIGHT - 40;
static const int SLIDER_WIDTH = 200;
static const int SLIDER_HEIGHT = 20;
const double ASSET_ROTATION_CORRECTION_RADIAN = -SDL_PI_D / 2.0; // Using SDL's PI constant
extern const char *BASE_ASSETS;                                  // Declaration only
extern const char *FONT_ASSETS;                                  // Declaration only
extern string PROJECT_NAME;                                      // Declaration only
extern string WINDOW_TITLE;                                      // Declaration only

// Forward declaration for ThreadPool
class ThreadPool;
struct Costume
{
    string id;
    string name;
    string assetId;
    SDL_Texture *imageHandle = nullptr; // Initialize to nullptr
    SDL_Surface *surfaceHandle = nullptr;
    string filename;
    string fileurl;
};
/*
"sounds": [
 {
    "duration": 2.9,
    "ext": ".mp3",
    "id": "ev4n",
    "filename": "1080771fm9ytn3030006a2c9431u3tqd",
    "name": "Wii Startup Sound Effect.mp3.mp3",
    "fileurl": "temp/10/80/sound/1080771fm9ytn3030006a2c9431u3tqd.mp3"
}
]
*/
struct SoundFile
{
    double duration;
    string ext;
    string id;
    string filename;
    string name;
    string fileurl;
};
struct ObjectInfo
{
    string id;
    string name;
    string objectType;
    std::string sceneId; // Changed to std::string for consistency
    string selectedCostumeId;
    vector<Costume> costumes;
    vector<SoundFile> sounds;
    SDL_Color textBoxBackgroundColor; // 글상자 배경색 추가
    string textContent;
    SDL_Color textColor;
    string fontName;
    bool Underline=false;
    bool Strike=false;
    bool Bold=false;
    bool Italic=false;
    bool lineBreak;
    int fontSize;
    int textAlign;
    nlohmann::json entity; // Store the raw entity JSON object
};
struct ListItem
{
    string data;     // 리스트 항목의 데이터 (첫 번째 멤버로 변경)
    string key = ""; // 리스트 항목의 키 (두 번째 멤버로 변경)
};
// HUD에 표시될 일반 변수의 정보를 담는 구조체
struct HUDVariableDisplay
{
    string id;                           // 변수 ID
    string name;                         // 변수 이름
    string value;                        // 변수 값 (문자열로 표시)
    string objectId;                     // 변수를 표시할 오브젝트 ID 가 null 이면 public 변수
    bool isVisible;                      // HUD에 표시 여부
    float x;                             // HUD에서의 X 좌표
    float y;                             // HUD에서의 Y 좌표
    string variableType;                 // 변수 유형 ("variable", "timer", "answer" 등)
    bool isCloud;                        // 클라우드 변수 여부 (json 세이브)
    bool isAnswerList;
    float width = 0;                     // HUD에서의 너비 리스트전용
    float height = 0;                    // HUD에서의 높이 리스트전용
    float transient_render_width = 0.0f; // 드래그 클램핑을 위해 마지막으로 계산된 렌더링 너비
    float scrollOffset_Y = 0.0f; // 리스트 스크롤 오프셋
    float calculatedContentHeight = 0.0f; // 리스트 내용 전체 높이
    vector<ListItem> array;              // 리스트 항목 (리스트 전용)
};
class Engine : public TextInputInterface
{
    map<string, vector<Script>> objectScripts;
    vector<pair<string, const Script *>> startButtonScripts;                   // <objectId, Script*> 시작 버튼 클릭 시 실행할 스크립트 목록
    map<SDL_Scancode, vector<pair<string, const Script *>>> keyPressedScripts; // <Scancode, vector<objectId, Script*>> 키 눌림 시 실행할 스크립트 목록
    vector<ObjectInfo> objects_in_order; // This stores info, not live entities.
    map<string, std::shared_ptr<Entity>> entities; // Changed to shared_ptr
    vector<string> m_sceneOrder; // Stores scene IDs in the order they are defined
    SDL_Window *window;          // SDL Window
    SDL_Renderer *renderer;      // SDL Renderer
    string currentSceneId;
    TTF_Font *hudFont = nullptr; // HUD용 폰트
    TTF_Font *percentFont = nullptr;
    TTF_Font *loadingScreenFont = nullptr; // 로딩 화면용 폰트
    map<string, string> scenes;
    SDL_Texture *tempScreenTexture;
    mutable mutex m_logMutex;
    mutable mutex m_fileMutex;
    string m_pressedObjectId; // ID of the object currently being pressed by the mouse
    vector<pair<string, const Script *>> m_mouseClickedScripts;
    vector<pair<string, const Script *>> m_mouseClickCanceledScripts;
    vector<pair<string, const Script *>> m_whenObjectClickedScripts;
    vector<pair<string, const Script *>> m_whenObjectClickCanceledScripts;
    vector<pair<string, const Script *>> m_whenStartSceneLoadedScripts;
    vector<pair<string, const Script *>> m_whenCloneStartScripts;               // 복제본 생성 시 실행될 스크립트
    map<string, vector<pair<string, const Script *>>> m_messageReceivedScripts; // Key: 메시지 ID/이름
    std::map<std::string, std::string> m_messageIdToNameMap;
    bool m_showScriptDebugger = false;                                         // 스크립트 디버거 표시 여부
    float m_debuggerScrollOffsetY = 0.0f;                                      // 스크립트 디버거 스크롤 오프셋
    // --- Text Input Members (for ask_and_wait) ---
    std::string m_currentTextInputBuffer;     // 현재 입력 중인 텍스트 버퍼
    bool m_textInputActive = false;           // 텍스트 입력 모드 활성화 여부
    std::string m_textInputRequesterObjectId; // 텍스트 입력을 요청한 오브젝트 ID
    std::string m_textInputQuestionMessage;   // 텍스트 입력 시 표시될 질문 메시지
    std::string m_lastAnswer;                 // 마지막으로 입력된 답변 (ask_and_wait 블록용)
    mutable std::mutex m_textInputMutex;
    std::condition_variable m_textInputCv;    
    string firstSceneIdInOrder;
    std::string m_currentProjectFilePath; // 현재 로드된 프로젝트 파일 경로
    SDL_Texture *LoadTextureFromSvgResource(SDL_Renderer *renderer, int resourceID);
    const string ANSI_COLOR_RESET = "\x1b[0m";
    const string ANSI_COLOR_RED = "\x1b[31m";
    const string ANSI_COLOR_YELLOW = "\x1b[33m";
    const string ANSI_COLOR_CYAN = "\x1b[36m";
    const string ANSI_STYLE_BOLD = "\x1b[1m";
    bool createTemporaryScreen();
    bool m_needsTextureRecreation = false; // Flag to indicate if textures need to be recreated
    // --- Project Timer Members ---
    double m_projectTimerValue = 0.0;
    bool m_projectTimerRunning = false;
    // bool m_projectTimerVisible = false; // 이제 HUDVariableDisplay의 isVisible로 관리
    bool m_gameplayInputActive = false; // Flag to indicate if gameplay-related key input is active
    Uint64 m_projectTimerStartTime = 0; // Start time of the project timer (Uint64로 변경)
    // --- Mouse State ---
    float m_currentStageMouseX = 0.0f;
    float m_currentStageMouseY = 0.0f;
    bool m_isMouseOnStage = false; // True if the mouse is currently over the stage display area
    // --- Project Timer UI ---
    // float m_projectTimerWidgetX = 10.0f; // 이제 HUDVariableDisplay의 x, y로 관리
    // float m_projectTimerWidgetY = 70.0f; // 이제 HUDVariableDisplay의 x, y로 관리
    // bool m_isDraggingProjectTimer = false; // 드래그 로직은 변수 목록 전체 드래그 제거 시 함께 제거됨
    // float m_projectTimerDragOffsetX = 0.0f;
    // float m_projectTimerDragOffsetY = 0.0f;
    // --- General Variables Display UI ---
    enum class HUDDragState
    {
        NONE,
        MOVING,
        RESIZING,
        SCROLLING_LIST_HANDLE // 리스트 스크롤바 핸들 드래그 상태 추가
    };
    vector<HUDVariableDisplay> m_HUDVariables;               // HUD에 표시될 변수 목록
    int m_draggedHUDVariableIndex = -1;                      // 드래그 중인 HUD 변수의 인덱스, 없으면 -1
    HUDDragState m_currentHUDDragState = HUDDragState::NONE; // 현재 HUD 드래그 상태
    float m_draggedHUDVariableMouseOffsetX = 0.0f;           // 드래그 중인 변수의 마우스 오프셋 X
    float m_draggedHUDVariableMouseOffsetY = 0.0f;           // 드래그 중인 변수의 마우스 오프셋 Y
    // 리사이즈 관련 변수 (리사이즈 시작 시점의 마우스 위치와 HUD 크기를 저장할 수도 있으나,
    // 스크롤바 드래그 관련 변수
    int m_draggedScrollbarListIndex = -1;     // 드래그 중인 리스트 스크롤바의 HUD 변수 인덱스
    float m_scrollbarDragStartY = 0.0f;       // 스크롤바 드래그 시작 시 마우스 Y 좌표
    float m_scrollbarDragInitialOffset = 0.0f; // 스크롤바 드래그 시작 시 리스트의 scrollOffset_Y
    // --- Cursor Blink Members ---
    Uint64 m_cursorBlinkToggleTime = 0;                 // 마지막으로 커서 상태가 변경된 시간
    bool m_cursorCharVisible = true;                    // 현재 커서 문자('|')가 보여야 하는지 여부
    static const Uint32 CURSOR_BLINK_INTERVAL_MS = 500; // 커서 깜빡임 간격 (밀리초)
    // 여기서는 MOVING과 RESIZING 상태를 구분하고, 오프셋은 공유하되 계산 방식을 달리합니다.)
    // float m_resizeStartMouseX = 0.0f;
    // float m_resizeStartMouseY = 0.0f;
    // float m_resizeStartHUDWidth = 0.0f;
    std::map<std::pair<std::string, int>, TTF_Font *> m_fontCache; // 폰트 캐시
    // float m_resizeStartHUDHeight = 0.0f;
    float m_maxVariablesListContentWidth = 180.0f; // 변수 목록의 실제 내용물 최대 너비

    void destroyTemporaryScreen();
    std::mutex m_commandQueueMutex;
    std::condition_variable m_commandQueueCV; // 엔티티 스레드가 커맨드를 추가했음을 알리기 위함 (선택적)

    // Standard C++ Thread Pool members
    std::vector<std::thread> m_workerThreads;
    std::queue<std::function<void()>> m_taskQueue;
    std::mutex m_taskQueueMutex_std; // Renamed to avoid conflict if another m_taskQueueMutex exists
    std::condition_variable m_taskQueueCV_std;
    void workerLoop();
    void processCommands();                   // 메인 루프에서 커맨드를 처리하는 함수
    string getOEparam(string s) const {
        //OmochaEngine 파라미터 캡쳐
        regex OEpat("<OE:(.+?)>");
        smatch OEmatch;
        if (regex_search(s, OEmatch, OEpat)) {
            return OEmatch[1].str();
        }
    }
    Uint64 lastfpstime;                  // SDL_GetTicks64() 또는 SDL_GetTicks() (SDL3에서 Uint64 반환) 와 호환되도록 Uint64로 변경
    bool m_isDraggingZoomSlider = false; // 줌 슬라이더 드래그 상태
    int framecount;
    float currentFps;
    map<string, int> m_cloneCounters;
    int totalItemsToLoad;
    int loadedItemCount; // 로드된 아이템 수
    float zoomFactor;    // 현재 줌 배율 저장
    static const float MIN_ZOOM;
    static const float MAX_ZOOM;
    static const float LIST_RESIZE_HANDLE_SIZE; // 리스트 리사이즈 핸들 크기
    // --- Keyboard State ---
    std::set<SDL_Scancode> m_pressedKeys; // Set of currently pressed keys
    mutable std::mutex m_pressedKeysMutex;  // Mutex for m_pressedKeys
    std::atomic<bool> m_stageWasClickedThisFrame{false};
    void setVisibleHUDVariables(const vector<HUDVariableDisplay> &variables);
    bool m_treeCollapseTargetState = true; // 초기값: 기본적으로 펼침 (true) 또는 접힘 (false)
    bool m_applyGlobalTreeState = false;   // 프레임 단위로 전역 상태 적용 여부 플래그
    std::unique_ptr<ThreadPool> threadPool;  // ThreadPool 멤버 추가
    std::atomic<uint64_t> m_scriptExecutionCounter{0}; // 스크립트 실행 ID 고유성 확보를 위한 카운터
public:
    static int getProjectstageWidth(){return PROJECT_STAGE_WIDTH;}
    static int getProjectstageHeight(){return PROJECT_STAGE_HEIGHT;}
    // --- Engine Special Configuration ---
    static const float MIN_LIST_WIDTH;  // 리스트 최소 너비
    static const float MIN_LIST_HEIGHT; // 리스트 최소 높이
    string getSafeStringFromJson(const nlohmann::json &parentValue,
                                 const string &fieldName,
                                 const string &contextDescription,
                                 const string &defaultValue,
                                 bool isCritical, // LCOV_EXCL_LINE
                                 bool allowEmpty); // Removed const
    struct SPECIAL_ENGINE_CONFIG
    {
        string BRAND_NAME = "";
        bool SHOW_PROJECT_NAME = false; // 로딩 화면 등에 프로젝트 이름 표시 여부
        bool showZoomSlider = false;    // 줌 슬라이더 UI 표시 여부
        bool showFPS = false;           // FPS 표시 여부
        int TARGET_FPS = 60;
        int MAX_ENTITY = 100;
        float setZoomfactor = 1.0f;
    };
    SPECIAL_ENGINE_CONFIG specialConfig; // 엔진의 특별 설정을 저장하는 멤버 변수
    struct MsgBoxIconType
    {
        // Note: SDL_MESSAGEBOX_ERROR etc. are already integers, so this struct might be redundant if only used for these constants.
        const int ICON_ERROR = SDL_MESSAGEBOX_ERROR;
        const int ICON_WARNING = SDL_MESSAGEBOX_WARNING;
        const int ICON_INFORMATION = SDL_MESSAGEBOX_INFORMATION;
    };
    bool mapWindowToStageCoordinates(int windowMouseX, int windowMouseY, float &stageX, float &stageY) const;
    void resumeSuspendedScriptsInCurrentScene();
    MsgBoxIconType msgBoxIconType;
    Engine();
    // Explicitly delete copy constructor and copy assignment operator
    // to prevent copying of Engine objects, which contain non-copyable std::mutex members.
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    // Optionally, define or default move constructor and move assignment operator if needed
    Engine(Engine&&) = default;
    Engine& operator=(Engine&&) = default;
    ~Engine() override;
    bool IsSysMenu = false;
    bool IsScriptStart = false; // 스크립트 시작 여부
    bool loadProject(const string &projectFilePath);

    string OFD() const;

    bool initGE(bool vsyncEnabled, bool attemptVulkan); // VSync 및 Vulkan 사용 여부 인자 추가
    bool initImGui();

    void terminateGE();
    TTF_Font *getFont(const std::string &fontPath, int fontSize); // 폰트 가져오기 (캐시 사용)
    bool loadImages(); // LCOV_EXCL_LINE
    bool loadSounds();
    // --- Cloud Variable Persistence ---
    bool saveCloudVariablesToJson();
    bool loadCloudVariablesFromJson();
    void drawAllEntities();
    static int GET_WINDOW_WIDTH() {
        return WINDOW_WIDTH; // LCOV_EXCL_LINE
    };
    static int GET_WINDOW_HEIGHT() {
        return WINDOW_HEIGHT;
    };
    const string &getCurrentSceneId() const;
    SDL_Scancode mapStringToSDLScancode(const string &keyName) const;
    bool showMessageBox(const string &message, int IconType, bool showYesNo = false) const;
    void showProjectTimer(bool show); 
    void EngineStdOut(string s, int LEVEL = 0, string TREADID = "") const; 
    void processInput(const SDL_Event &event, float deltaTime);
    void drawScriptDebuggerUI();

    void updateEntityTextEffect(const std::string &entityId, const std::string &effect, bool setOn);

    void runStartButtonScripts(); // 시작 버튼 스크립트 실행 메서드
    void initFps();
    void updateFps();
    void setfps(int fps);
    SDL_Renderer *getRenderer() const // Added const
    { // Added const
        return this->renderer;
    } // LCOV_EXCL_LINE
    float getFps() { return currentFps; };
    int getTargetFps() const { return specialConfig.TARGET_FPS; } // 목표 FPS getter 추가
    Entity *getEntityById(const string &id);
    void setTotalItemsToLoad(int count) { totalItemsToLoad = count; }
    void incrementLoadedItemCount() { loadedItemCount++; }
    Entity *getEntityById_nolock(const std::string &id);
    std::shared_ptr<Entity> getEntityByIdShared(const std::string &id); // New method
    std::shared_ptr<Entity> getEntityByIdNolockShared(const std::string &id); // New method
    void renderLoadingScreen();
    void handleRenderDeviceReset();
    bool recreateAssetsIfNeeded();    void drawHUD(); // HUD 그리기 메서드 추가
    void drawImGui();

    void goToScene(const string &sceneId);
    void goToNextScene();
    void goToPreviousScene(); // LCOV_EXCL_LINE
    void triggerWhenSceneStartScripts();
    void updateAnswerVariable(); // Update the answer variable with the latest text input
    void startProjectTimer();
    void stopProjectTimer();
    void activateTextInput(const std::string &requesterObjectId, const std::string &question, const std::string &executionThreadId);
    std::string getLastAnswer() const;
    void resetProjectTimer();
    double getProjectTimerValue() const; // LCOV_EXCL_LINE
    void showAnswerValue(bool show); // Declaration
    void updateCurrentMouseStageCoordinates(int windowMouseX, int windowMouseY); // 스테이지 마우스 좌표 업데이트 메서드
    // --- Mouse State Getters ---
    float getCurrentStageMouseX() const { return m_currentStageMouseX; }
    float getCurrentStageMouseY() const { return m_currentStageMouseY; }
    std::string getPressedObjectId() const;
    double getAngle(double x1, double y1, double x2, double y2) const;      // 두 점 사이의 각도 계산
    double getCurrentStageMouseAngle(double entityX, double entityY) const; // Angle to mouse from entity
    const ObjectInfo *getObjectInfoById(const string &id) const;
    bool isMouseCurrentlyOnStage() const { return m_isMouseOnStage; }
    bool getStageWasClickedThisFrame() const;
    bool isKeyPressed(SDL_Scancode scancode) const; // New: Check if a key is pressed // LCOV_EXCL_LINE
    std::string getDeviceType() const;
    bool isTouchSupported() const;
    void updateEntityTextContent(const std::string &entityId, const std::string &newText);
    void setStageClickedThisFrame(bool clicked);
    void updateEntityTextColor(const std::string& entityId, const SDL_Color& newColor);
    void updateEntityTextBoxBackgroundColor(const std::string& entityId, const SDL_Color& newColor);
    // HUD에 표시할 변수 목록을 설정하는 메서드    
    map<string, std::shared_ptr<Entity>> &getEntities_Modifiable() { return entities; } // Changed to shared_ptr
    vector<HUDVariableDisplay> &getHUDVariables_Editable() { return m_HUDVariables; } // 블록에서 접근하기 위함
    // --- Pen Drawing ---
    void engineDrawLineOnStage(SDL_FPoint p1_stage_entry, SDL_FPoint p2_stage_entry_modified_y, SDL_Color color, float thickness);

    AudioEngineHelper aeHelper; // public으로 이동 // LCOV_EXCL_LINE
    int getBlockCountForObject(const std::string &objectId) const;
    int getBlockCountForScene(const std::string &sceneId) const; // 이 함수는 getBlockCountForObject를 호출하므로 간접적으로 objectScripts에 접근
    int getTotalBlockCount() const;
    TTF_Font *getDialogFont();
    void drawDialogs();
    bool setEntitySelectedCostume(const std::string &entityId, const std::string &costumeId);
    bool setEntitychangeToNextCostume(const std::string &entityId, const std::string &asOption);
    void dispatchScriptForExecution(const std::string &entityId, const Script *scriptPtr, const std::string &sceneIdAtDispatch, float deltaTime, const std::string &existingExecutionThreadId = "");
    void raiseMessage(const std::string &messageId, const std::string &senderObjectId, const std::string &executionThreadId);
    std::string getMessageNameById(const std::string& messageId) const;
    std::shared_ptr<Entity> createCloneOfEntity(const std::string &originalEntityId, const std::string &sceneIdForScripts); // Return shared_ptr
    int getNextCloneIdSuffix(const std::string &originalId);
    void deleteEntity(const std::string& entityIdToDelete);
    void deleteAllClonesOf(const std::string& originalEntityId);
    void changeObjectIndex(const std::string &entityId, Omocha::ObjectIndexChangeType changeType);
    void requestStopObject(const std::string &callingEntityId, const std::string &callingThreadId, const std::string &targetOption);
    void requestProjectRestart(); 
    void performProjectRestart(); // LCOV_EXCL_LINE
    mutable SimpleLogger logger;                 // 로거 인스턴스를 mutable로 변경
    // Thread pool management
    void startThreadPool(size_t numThreads); // LCOV_EXCL_LINE
    void stopThreadPool();
    std::atomic<bool> m_isShuttingDown{false};   // 엔진 종료 상태 플래그
    std::atomic<bool> m_restartRequested{false}; // 프로젝트 다시 시작 요청 플래그
    mutable std::recursive_mutex m_engineDataMutex; // 엔진 데이터 보호용 뮤텍스 (entities, objectScripts 등 접근 시)
    uint64_t getNextScriptExecutionCounter() { return m_scriptExecutionCounter++; } // 카운터 값 증가 및 반환
    void submitTask(std::function<void()> task); // Task submission method

    std::atomic<bool> m_needAnswerUpdate{false};
    void requestAnswerUpdate() { m_needAnswerUpdate = true; }
    bool checkAndClearAnswerUpdateFlag() {
        bool expected = true;
        return m_needAnswerUpdate.compare_exchange_strong(expected, false);
    }

    // TextInputInterface 구현
    bool isTextInputActive() const override { return m_textInputActive; }
    void clearTextInput() override {
        m_currentTextInputBuffer.clear();
        m_lastAnswer.clear();
        m_textInputQuestionMessage.clear();
    }
    void deactivateTextInput() override {
        if (m_textInputActive) {
            m_textInputActive = false;
            if (window) SDL_StopTextInput(window);
            m_textInputCv.notify_all();
            
            Entity* entity = getEntityById_nolock(m_textInputRequesterObjectId);
            if (entity) {
                entity->removeDialog();
            }
            m_gameplayInputActive = true;
        }
    }
    std::mutex& getTextInputMutex() override { return m_textInputMutex; }
};
