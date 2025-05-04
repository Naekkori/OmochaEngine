#pragma once
#include "version_config.h"
#include <string>
#include <map>
#include <vector>
#include <json/value.h>
#include "DxLib.h"
#include "Entity.h"
#include "Block.h"
using namespace std;

const int PROJECT_STAGE_WIDTH = 640;
const int PROJECT_STAGE_HEIGHT = 360;

const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
const double ASSET_ROTATION_CORRECTION_RADIAN = -DX_PI / 2.0;
extern const char* BASE_ASSETS;
extern string PROJECT_NAME;
extern string WINDOW_TITLE;

struct Costume {
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
struct Sound{
    double duration;
    string ext;
    string id;
    string filename;
    string name;
    string fileurl;
};

struct ObjectInfo {
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

class Engine {
private:

    map<string, vector<Script>> objectScripts;
    map<string, vector<const Script*>> scriptsToRunOnClick;
    vector<ObjectInfo> objects_in_order;
    map<string, Entity*> entities;
    string currentSceneId;
    map<string, string> scenes;

    int tempScreenHandle;
    string firstSceneIdInOrder;
    const string ANSI_COLOR_RESET = "\x1b[0m";
    const string ANSI_COLOR_RED = "\x1b[31m";
    const string ANSI_COLOR_YELLOW = "\x1b[33m";
    const string ANSI_COLOR_CYAN = "\x1b[36m";
    const string ANSI_STYLE_BOLD = "\x1b[1m";
    bool createTemporaryScreen();
    void Fontloader(string fontpath);
    void Soundloader(string soundUri);
    void destroyTemporaryScreen();
    void findRunbtnScript();
    long long lastfpstime;
    int framecount;
    float currentFps;
    int totalItemsToLoad;
    int loadedItemCount;  
public:
    Engine();
    ~Engine();

    bool loadProject(const string& projectFilePath);
    bool initGE();
    void terminateGE();

    bool loadImages();

    void drawAllEntities();
    const string& getCurrentSceneId() const;

    void EngineStdOut(string s, int LEVEL = 0);
    void showErrorMessageBox(const string& message);

    void processInput();
    void triggerRunbtnScript();
    void initFps();
    void updateFps();
    void setfps(int fps);
    float getFps() { return currentFps; };
    Entity* getEntityById(const string& id);
    void setTotalItemsToLoad(int count) { totalItemsToLoad =+ count; }
    void incrementLoadedItemCount() { loadedItemCount++; }
    void renderLoadingScreen();
};
