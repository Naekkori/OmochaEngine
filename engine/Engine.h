#pragma once
#include "version_config.h"
#include <string>
#include <map>
#include <vector>
#include <json/value.h>
#include "DxLib.h"
#include "Entity.h"
#include "Block.h"


const int PROJECT_STAGE_WIDTH = 640;
const int PROJECT_STAGE_HEIGHT = 360;

const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
const double ASSET_ROTATION_CORRECTION_RADIAN = -DX_PI / 2.0;
extern const char* BASE_ASSETS;
extern std::string PROJECT_NAME;
extern std::string WINDOW_TITLE;


struct Costume {
    std::string id;
    std::string name;
    std::string assetId;
    int imageHandle;
    std::string filename;
    std::string fileurl;
};


struct ObjectInfo {
    std::string id;
    std::string name;
    std::string objectType;
    std::string sceneId;
    std::string selectedCostumeId;
    std::vector<Costume> costumes;
    std::string textContent;
    unsigned int textColor;
    std::string fontName;
    int fontSize;
    int textAlign;
};


class Engine {
private:

    std::map<std::string, std::vector<Script>> objectScripts;
    std::map<std::string, std::vector<const Script*>> scriptsToRunOnClick;
    std::vector<ObjectInfo> objects_in_order;
    std::map<std::string, Entity*> entities;
    std::string currentSceneId;
    std::map<std::string, std::string> scenes;

    int tempScreenHandle;
    std::string firstSceneIdInOrder;
    const std::string ANSI_COLOR_RESET = "\x1b[0m";
    const std::string ANSI_COLOR_RED = "\x1b[31m";
    const std::string ANSI_COLOR_YELLOW = "\x1b[33m";
    const std::string ANSI_COLOR_CYAN = "\x1b[36m";
    const std::string ANSI_STYLE_BOLD = "\x1b[1m";
    bool createTemporaryScreen();
    void Fontloader(std::string fontName);
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

    bool loadProject(const std::string& projectFilePath, const std::string& projectDirectory);
    bool initGE();
    void terminateGE();

    bool loadImages();

    void drawAllEntities();
    const std::string& getCurrentSceneId() const;

    void EngineStdOut(std::string s, int LEVEL = 0);
    void showErrorMessageBox(const std::string& message);

    void processInput();
    void triggerRunbtnScript();
    void initFps();
    void updateFps();
    void setfps(int fps);
    float getFps() { return currentFps; };
    Entity* getEntityById(const std::string& id);
    void setTotalItemsToLoad(int count) { totalItemsToLoad = count; }
    void incrementLoadedItemCount() { loadedItemCount++; }
    void renderLoadingScreen();
};
