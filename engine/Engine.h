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
#include "SDL3/SDL_render.h"      // SDL 렌더링
#include "SDL3/SDL_scancode.h"    // For SDL_Scancode
#include "blocks/Block.h"
#include "util/fontName.h"
#include "../util/Logger.h"
using namespace std;

const int WINDOW_WIDTH = 640 * 2;
const int WINDOW_HEIGHT = 360 * 2;
static const int PROJECT_STAGE_WIDTH = 480;
static const int PROJECT_STAGE_HEIGHT = 360;

// HUD 상수 정의
static const int SLIDER_X = 10;
static const int SLIDER_Y = WINDOW_HEIGHT - 40;
static const int SLIDER_WIDTH = 200;
static const int SLIDER_HEIGHT = 20;
const double ASSET_ROTATION_CORRECTION_RADIAN = -3.14159265358979323846 / 2.0;
extern const char *BASE_ASSETS; // Declaration only
extern const char *FONT_ASSETS; // Declaration only
extern string PROJECT_NAME;     // Declaration only
extern string WINDOW_TITLE;     // Declaration only
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
        bool useSqlite = true;
    };
    SPECIAL_ENGINE_CONFIG specialConfig; // 엔진의 특별 설정을 저장하는 멤버 변수
    map<string, vector<Script>> objectScripts;
    vector<pair<string, const Script *>> startButtonScripts;                   // <objectId, Script*> 시작 버튼 클릭 시 실행할 스크립트 목록
    map<SDL_Scancode, vector<pair<string, const Script *>>> keyPressedScripts; // <Scancode, vector<objectId, Script*>> 키 눌림 시 실행할 스크립트 목록
    vector<ObjectInfo> objects_in_order;
    map<string, Entity *> entities;
    SDL_Window *window;     // SDL Window
    SDL_Renderer *renderer; // SDL Renderer
    string currentSceneId;
    TTF_Font *hudFont = nullptr;           // HUD용 폰트
    TTF_Font *loadingScreenFont = nullptr; // 로딩 화면용 폰트
    map<string, string> scenes;
    SimpleLogger logger;
    SDL_Texture *tempScreenTexture;
    rapidjson::Document m_blockParamsAllocatorDoc; // Allocator for Block::paramsJson data
    std::string m_pressedObjectId; // ID of the object currently being pressed by the mouse
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
    bool m_gameplayInputActive = false;    // Flag to indicate if gameplay-related key input is active
    // int Soundloader(const string& soundUri);
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
    int mapEntryKeyToDxLibKey(const string &entryKey);
    string getSafeStringFromJson(const rapidjson::Value &parentValue,
                                      const string &fieldName,
                                      const string &contextDescription,
                                      const string &defaultValue,
                                      bool isCritical,
                                      bool allowEmpty);
    bool mapWindowToStageCoordinates(int windowMouseX, int windowMouseY, float& stageX, float& stageY) const;
    SDL_Scancode mapStringToSDLScancode(const string &keyName) const;

public:
    struct MsgBoxIconType
    {
        const int ICON_ERROR = SDL_MESSAGEBOX_ERROR;
        const int ICON_WARNING = SDL_MESSAGEBOX_WARNING;
        const int ICON_INFORMATION = SDL_MESSAGEBOX_INFORMATION;
    };
    MsgBoxIconType msgBoxIconType;
    Engine();
    ~Engine();
    bool mapWindowToStageCoordinates(int mouseX, int mouseY, float &stageX, float &stageY);
    bool loadProject(const string &projectFilePath);
    bool initGE(bool vsyncEnabled, bool attemptVulkan); // VSync 및 Vulkan 사용 여부 인자 추가
    void terminateGE();
    bool loadImages();
    void drawAllEntities();
    const string &getCurrentSceneId() const;
    bool showMessageBox(const string &message, int IconType, bool showYesNo = false);
    void EngineStdOut(string s, int LEVEL = 0);
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
    void drawHUD();                 // HUD 그리기 메서드 추가
};
