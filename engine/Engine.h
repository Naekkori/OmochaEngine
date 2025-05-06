#pragma once
#include "version_config.h"
#include <string>
#include <map>
#include <vector>
#include <json/value.h>
#include "DxLib.h"
#include "Entity.h"
#include "blocks/Block.h"
#include "util/fontName.h"
#include "../util/Logger.h"
using namespace std;

const int PROJECT_STAGE_WIDTH = 640;
const int PROJECT_STAGE_HEIGHT = 360;

const int WINDOW_WIDTH = 640 * 2;
const int WINDOW_HEIGHT = 360 * 2;
const double ASSET_ROTATION_CORRECTION_RADIAN = -DX_PI / 2.0;
extern const char *BASE_ASSETS;
extern string PROJECT_NAME;
extern string WINDOW_TITLE;

struct Costume
{
    string id;
    string name;
    string assetId;
    int imageHandle;
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
struct Sound
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
    vector<Sound> sounds;
    string textContent;
    unsigned int textColor;
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
        bool SHOW_PROJECT_NAME = false; // 로딩 화면 등에 프로젝트 이름 표시 여부 (구현 필요)
        bool showZoomSlider = false;    // 줌 슬라이더 UI 표시 여부
        bool showFPS = false;
        int TARGET_FPS = 60;
        //-- Experimal
        bool useSqlite = true;
    };
    SPECIAL_ENGINE_CONFIG specialConfig; // 엔진의 특별 설정을 저장하는 멤버 변수
    map<string, vector<Script>> objectScripts;
    vector<pair<string, const Script *>> startButtonScripts; // <objectId, Script*> 시작 버튼 클릭 시 실행할 스크립트 목록
    vector<ObjectInfo> objects_in_order;
    map<string, Entity *> entities;
    string currentSceneId;
    map<string, string> scenes;
    SimpleLogger logger;
    int tempScreenHandle;
    string firstSceneIdInOrder;
    const string ANSI_COLOR_RESET = "\x1b[0m";
    const string ANSI_COLOR_RED = "\x1b[31m";
    const string ANSI_COLOR_YELLOW = "\x1b[33m";
    const string ANSI_COLOR_CYAN = "\x1b[36m";
    const string ANSI_STYLE_BOLD = "\x1b[1m";
    bool createTemporaryScreen();
    int Fontloader(string fontpath);
    int Soundloader(string soundUri);
    void destroyTemporaryScreen();
    void findRunbtnScript();
    long long lastfpstime;
    int framecount;
    float currentFps;
    int totalItemsToLoad;
    int loadedItemCount;
    float zoomFactor; // 현재 줌 배율 저장
    int loadingFontHandle=-1;
    // --- UI Constants ---
    static const int SLIDER_X = 10;
    static const int SLIDER_Y = WINDOW_HEIGHT - 40;
    static const int SLIDER_WIDTH = 200;
    static const int SLIDER_HEIGHT = 20;
    static const float MIN_ZOOM;
    static const float MAX_ZOOM;
    int mapEntryKeyToDxLibKey(const string& entryKey);
public:
    struct MsgBoxIconType
    {
        const int ICON_ERROR = MB_ICONERROR;
        const int ICON_WARNING = MB_ICONWARNING;
        const int ICON_INFORMATION = MB_ICONINFORMATION;
        const int ICON_QUESTION = MB_ICONQUESTION;
    };
    Engine();
    ~Engine();
    MsgBoxIconType msgBoxIconType;
    bool loadProject(const string &projectFilePath);
    bool initGE();
    void terminateGE();
    bool loadImages();
    void drawAllEntities();
    const string &getCurrentSceneId() const;
    void showMessageBox(const string &message, int IconType);
    void EngineStdOut(string s, int LEVEL = 0);
    map<int,vector<pair<string,const Script*>>> sceneScripts;
    void processInput();
    void runStartButtonScripts(); // 시작 버튼 스크립트 실행 메서드
    void initFps();
    void updateFps();
    void setfps(int fps);
    float getFps() { return currentFps; };
    int getTargetFps() const { return specialConfig.TARGET_FPS; } // 목표 FPS getter 추가
    Entity *getEntityById(const string &id);
    void setTotalItemsToLoad(int count) { totalItemsToLoad = count; } // += 가 아니라 = 로 수정 (또는 아래처럼 초기화 로직 추가)
    void incrementLoadedItemCount() { loadedItemCount++; }
    void renderLoadingScreen();
    void drawHUD(); // HUD 그리기 메서드 추가
};
