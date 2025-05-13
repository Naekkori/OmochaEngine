#pragma once
#include "version_config.h"
#include <string>
#include <map>
#include <vector>
#include <rapidjson/rapidjson.h> // Added for RapidJSON forward declaration if needed, or full include
#include <rapidjson/document.h>
#include "Entity.h"
#include "SDL3/SDL.h"             // SDL 코어
#include "SDL3_image/SDL_image.h" //SDL 이미지
#include "SDL3_ttf/SDL_ttf.h"     //SDL TTF
#include "SDL3/SDL_audio.h"       // SDL 오디오
#include "SDL3/SDL_stdinc.h"      // For SDL_PI_D
#include "SDL3/SDL_render.h"      // SDL 렌더링
#include "SDL3/SDL_scancode.h"    // For SDL_Scancode
#include "blocks/Block.h"
#include "util/fontName.h"
#include "../util/Logger.h"
using namespace std;

const int WINDOW_WIDTH = 480 * 3;
const int WINDOW_HEIGHT = 270 * 3;
static const int PROJECT_STAGE_WIDTH = 480;  // 실제 프로젝트의 가로 크기에 맞춤
static const int PROJECT_STAGE_HEIGHT = 270; // 실제 프로젝트의 세로 크기(16:9 비율)에 맞춤

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
struct Costume
{
    string id;
    string name;
    string assetId;
    SDL_Texture *imageHandle = nullptr; // Initialize to nullptr
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
    string sceneId;
    string selectedCostumeId;
    vector<Costume> costumes;
    vector<SoundFile> sounds;
    string textContent;
    SDL_Color textColor;
    string fontName;
    int fontSize;
    int textAlign;
};
struct ListItem{
    string key=""; // 리스트 항목의 키
    string data; // 리스트 항목의 데이터
};
// HUD에 표시될 일반 변수의 정보를 담는 구조체
struct HUDVariableDisplay {
    string name;  // 변수 이름
    string value; // 변수 값 (문자열로 표시)
    string objectId; // 변수를 표시할 오브젝트 ID 가 null 이면 public 변수
    bool isVisible;    // HUD에 표시 여부
    float x;    // HUD에서의 X 좌표
    float y;    // HUD에서의 Y 좌표
    string variableType; // 변수 유형 ("variable", "timer", "answer" 등)
    float width=0; // HUD에서의 너비 리스트전용
    float height=0; // HUD에서의 높이 리스트전용
    float transient_render_width = 0.0f; // 드래그 클램핑을 위해 마지막으로 계산된 렌더링 너비
    vector<ListItem> array; // 리스트 항목 (리스트 전용)
};
class Engine
{
private:
    // --- Engine Special Configuration ---
    struct SPECIAL_ENGINE_CONFIG
    {
        string BRAND_NAME = "";
        bool SHOW_PROJECT_NAME = false; // 로딩 화면 등에 프로젝트 이름 표시 여부
        bool showZoomSlider = false;    // 줌 슬라이더 UI 표시 여부
        bool showFPS = false;           // FPS 표시 여부
        int TARGET_FPS = 60;
        //-- Experimal
        bool useSqlite = false; // 클라우드 변수를 sqlite 에 저장 (네이버 서버에 연결할수 없으니 로컬 db에 저장 false 면 project.json 에 키 저장)
        float setZoomfactor = 1.0f;
    };
    SPECIAL_ENGINE_CONFIG specialConfig; // 엔진의 특별 설정을 저장하는 멤버 변수
    map<string, vector<Script>> objectScripts;
    vector<pair<string, const Script *>> startButtonScripts;                   // <objectId, Script*> 시작 버튼 클릭 시 실행할 스크립트 목록
    map<SDL_Scancode, vector<pair<string, const Script *>>> keyPressedScripts; // <Scancode, vector<objectId, Script*>> 키 눌림 시 실행할 스크립트 목록
    vector<ObjectInfo> objects_in_order;
    map<string, Entity *> entities;
    vector<string> m_sceneOrder; // Stores scene IDs in the order they are defined
    SDL_Window *window;               // SDL Window
    SDL_Renderer *renderer;           // SDL Renderer
    string currentSceneId;
    TTF_Font *hudFont = nullptr;           // HUD용 폰트
    TTF_Font *loadingScreenFont = nullptr; // 로딩 화면용 폰트
    map<string, string> scenes;
    SDL_Texture *tempScreenTexture;
    string m_pressedObjectId; // ID of the object currently being pressed by the mouse
    vector<pair<string, const Script *>> m_mouseClickedScripts;
    vector<pair<string, const Script *>> m_mouseClickCanceledScripts;
    vector<pair<string, const Script *>> m_whenObjectClickedScripts;
    vector<pair<string, const Script *>> m_whenObjectClickCanceledScripts;
    vector<pair<string, const Script *>> m_whenStartSceneLoadedScripts;
    map<string, vector<pair<string, const Script *>>> m_messageReceivedScripts; // Key: 메시지 ID/이름

    string firstSceneIdInOrder;
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
    bool m_gameplayInputActive = false;    // Flag to indicate if gameplay-related key input is active
    Uint64 m_projectTimerStartTime = 0; // Start time of the project timer (Uint64로 변경)
    // int Soundloader(const string& soundUri);
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
    enum class HUDDragState { NONE, MOVING, RESIZING };
    vector<HUDVariableDisplay> m_HUDVariables;      // HUD에 표시될 변수 목록
    int m_draggedHUDVariableIndex = -1;             // 드래그 중인 HUD 변수의 인덱스, 없으면 -1
    HUDDragState m_currentHUDDragState = HUDDragState::NONE; // 현재 HUD 드래그 상태
    float m_draggedHUDVariableMouseOffsetX = 0.0f;  // 드래그 중인 변수의 마우스 오프셋 X
    float m_draggedHUDVariableMouseOffsetY = 0.0f;  // 드래그 중인 변수의 마우스 오프셋 Y
    // 리사이즈 관련 변수 (리사이즈 시작 시점의 마우스 위치와 HUD 크기를 저장할 수도 있으나,
    // 여기서는 MOVING과 RESIZING 상태를 구분하고, 오프셋은 공유하되 계산 방식을 달리합니다.)
    // float m_resizeStartMouseX = 0.0f;
    // float m_resizeStartMouseY = 0.0f;
    // float m_resizeStartHUDWidth = 0.0f;
    // float m_resizeStartHUDHeight = 0.0f;
    float m_maxVariablesListContentWidth = 180.0f; // 변수 목록의 실제 내용물 최대 너비

    void destroyTemporaryScreen();
    Uint64 lastfpstime;                  // SDL_GetTicks64() 또는 SDL_GetTicks() (SDL3에서 Uint64 반환) 와 호환되도록 Uint64로 변경
    bool m_isDraggingZoomSlider = false; // 줌 슬라이더 드래그 상태
    int framecount;
    float currentFps;
    int totalItemsToLoad;
    int loadedItemCount; // 로드된 아이템 수
    float zoomFactor;    // 현재 줌 배율 저장
    static const float MIN_ZOOM;
    static const float MAX_ZOOM;
    static const float LIST_RESIZE_HANDLE_SIZE; // 리스트 리사이즈 핸들 크기
    static const float MIN_LIST_WIDTH;          // 리스트 최소 너비
    static const float MIN_LIST_HEIGHT;         // 리스트 최소 높이
    int mapEntryKeyToDxLibKey(const string &entryKey);
    string getSafeStringFromJson(const rapidjson::Value &parentValue,
                                 const string &fieldName,
                                 const string &contextDescription,
                                 const string &defaultValue,
                                 bool isCritical,
                                 bool allowEmpty);

    SDL_Scancode mapStringToSDLScancode(const string &keyName) const;

    void setVisibleHUDVariables(const vector<HUDVariableDisplay> &variables);

public:
    struct MsgBoxIconType
    {
        // Note: SDL_MESSAGEBOX_ERROR etc. are already integers, so this struct might be redundant if only used for these constants.
        const int ICON_ERROR = SDL_MESSAGEBOX_ERROR;
        const int ICON_WARNING = SDL_MESSAGEBOX_WARNING;
        const int ICON_INFORMATION = SDL_MESSAGEBOX_INFORMATION;
    };
    bool mapWindowToStageCoordinates(int windowMouseX, int windowMouseY, float &stageX, float &stageY) const;
    MsgBoxIconType msgBoxIconType;
    Engine();
    ~Engine();
    bool IsSysMenu = false;
    bool loadProject(const string &projectFilePath);
    bool initGE(bool vsyncEnabled, bool attemptVulkan); // VSync 및 Vulkan 사용 여부 인자 추가
    void terminateGE();
    bool loadImages();
    void drawAllEntities();
    const string &getCurrentSceneId() const;
    bool showMessageBox(const string &message, int IconType, bool showYesNo = false) const;
    void showProjectTimer(bool show); // 프로젝트 타이머 표시 여부 설정
    void EngineStdOut(string s, int LEVEL = 0) const;
    void processInput(const SDL_Event &event);
    void runStartButtonScripts(); // 시작 버튼 스크립트 실행 메서드
    void initFps();
    void updateFps();
    void setfps(int fps);
    SDL_Renderer *getRenderer()
    {
        return this->renderer;
    }
    float getFps() { return currentFps; };
    int getTargetFps() const { return specialConfig.TARGET_FPS; } // 목표 FPS getter 추가
    Entity *getEntityById(const string &id);
    void setTotalItemsToLoad(int count) { totalItemsToLoad = count; }
    void incrementLoadedItemCount() { loadedItemCount++; }
    void renderLoadingScreen();
    void handleRenderDeviceReset();
    bool recreateAssetsIfNeeded();
    void drawHUD(); // HUD 그리기 메서드 추가
    void goToScene(const string &sceneId);
    void goToNextScene();
    void goToPreviousScene();
    void triggerWhenSceneStartScripts();
    void startProjectTimer();
    void stopProjectTimer();
    void resetProjectTimer();
    void setProjectTimerVisibility(bool show); // 이름 변경 및 기능 수정
    double getProjectTimerValue() const;
    void updateCurrentMouseStageCoordinates(int windowMouseX, int windowMouseY); // 스테이지 마우스 좌표 업데이트 메서드
    // --- Mouse State Getters ---
    float getCurrentStageMouseX() const { return m_currentStageMouseX; }
    float getCurrentStageMouseY() const { return m_currentStageMouseY; }
    const ObjectInfo* getObjectInfoById(const string& id) const;
    bool isMouseCurrentlyOnStage() const { return m_isMouseOnStage; }
    // HUD에 표시할 변수 목록을 설정하는 메서드
    void loadHUDVariablesFromJson(const rapidjson::Value& variablesArrayJson); // JSON에서 직접 로드하도록 변경
    vector<HUDVariableDisplay>& getHUDVariables_Editable() { return m_HUDVariables; } // 블록에서 접근하기 위함
    SimpleLogger logger; // 로거 인스턴스
    rapidjson::Document m_blockParamsAllocatorDoc; // Allocator for Block::paramsJson data - public으로 이동
};
