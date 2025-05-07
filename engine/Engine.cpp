#include "Engine.h"
#include "Entity.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <algorithm> // For min
#include <memory>
#include <format>
#include "blocks/BlockExecutor.h"
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
using namespace std;

const char *BASE_ASSETS = "assets/";
string PROJECT_NAME = "";
string WINDOW_TITLE = "";

// --- Engine Class Static Constants Definition ---
const float Engine::MIN_ZOOM = 1.0f;
const float Engine::MAX_ZOOM = 3.0f;

Engine::Engine() : window(nullptr), renderer(nullptr), tempScreenTexture(nullptr), totalItemsToLoad(0), loadedItemCount(0), zoomFactor(1.26f), logger("omocha_engine.log")
{
    EngineStdOut(string(OMOCHA_ENGINE_NAME) + " v" + string(OMOCHA_ENGINE_VERSION) + " " + string(OMOCHA_DEVELOPER_NAME), 4);
    EngineStdOut("See Project page " + string(OMOCHA_ENGINE_GITHUB), 4);
}

Engine::~Engine()
{
    terminateGE(); // SDL 리소스 해제 보장
    for (auto const &pair : entities)
    {
        delete pair.second;
    }
    entities.clear();
    // destroyTemporaryScreen(); // terminateGE에서 호출됨
}

bool Engine::loadProject(const string &projectFilePath)
{
    EngineStdOut("Initial Project JSON file parsing...", 0);

    ifstream projectFile(projectFilePath);
    if (!projectFile.is_open())
    {
        showMessageBox("Failed to open project file: " + projectFilePath, this->msgBoxIconType.ICON_ERROR);
        EngineStdOut(" Failed to open project file: " + projectFilePath, 2);
        return false;
    }

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(projectFile, root))
    {
        showMessageBox("Failed to parse project file: " + reader.getFormattedErrorMessages(), msgBoxIconType.ICON_ERROR);
        EngineStdOut(" Failed to parse project file: " + reader.getFormattedErrorMessages(), 2);
        return false;
    }
    projectFile.close();

    objects_in_order.clear();
    entities.clear();
    if (root.isMember("name") && root["name"].isString())
    {
        PROJECT_NAME = root["name"].asString();
        WINDOW_TITLE = PROJECT_NAME + " - " + OMOCHA_ENGINE_NAME + " v" + OMOCHA_ENGINE_VERSION;
        EngineStdOut("Project Name: " + PROJECT_NAME, 0);
    }
    // project.json에서 speed 값 읽어옴
    if (root.isMember("speed") && root["speed"].isNumeric())
    {
        this->specialConfig.TARGET_FPS = root["speed"].asInt();
        EngineStdOut("Target FPS set from project.json: " + std::to_string(this->specialConfig.TARGET_FPS), 0);
    }
    /**
     * @brief 특수 설정
     * 브랜드 이름 (로딩중)
     * 프로젝트 이름 표시 (로딩중)
     */
    if (root.isMember("specialConfig"))
    {
        Json::Value specialConfigJson = root["specialConfig"];
        if (specialConfigJson.isObject())
        {
            if (specialConfigJson.isMember("brandName") && specialConfigJson["brandName"].isString())
            {
                this->specialConfig.BRAND_NAME = specialConfigJson["brandName"].asString();
                EngineStdOut("Brand Name: " + this->specialConfig.BRAND_NAME, 0);
            }
            if (specialConfigJson.isMember("showZoomSliderUI") && specialConfigJson["showZoomSliderUI"].isBool()){
                 this->specialConfig.showZoomSlider = specialConfigJson["showZoomSliderUI"].asBool();
            } else {
                this->specialConfig.showZoomSlider = false; // 기본값
            }

            if (specialConfigJson.isMember("showProjectNameUI") && specialConfigJson["showProjectNameUI"].isBool()){
                this->specialConfig.SHOW_PROJECT_NAME = specialConfigJson["showProjectNameUI"].asBool();
            } else {
                this->specialConfig.SHOW_PROJECT_NAME = false; // 기본값
            }
            if (specialConfigJson.isMember("showFPS") && specialConfigJson["showFPS"].isBool()){
                this->specialConfig.showFPS = specialConfigJson["showFPS"].asBool();
            } else {
                this->specialConfig.showFPS = false; // 기본값
            }
        }
    }
    if (root.isMember("objects") && root["objects"].isArray())
    {
        const Json::Value &objectsJson = root["objects"];
        EngineStdOut("Found " + to_string(objectsJson.size()) + " objects. Processing...", 0);

        for (Json::Value::ArrayIndex i = 0; i < objectsJson.size(); ++i)
        {
            const auto &objectJson = objectsJson[i];
            if (objectJson.isObject() && objectJson.isMember("id") && objectJson["id"].isString())
            {
                string objectId = objectJson["id"].asString();

                ObjectInfo objInfo;
                objInfo.id = objectId;
                objInfo.name = objectJson.isMember("name") && objectJson["name"].isString() ? objectJson["name"].asString() : "Unnamed Object";
                objInfo.objectType = objectJson.isMember("objectType") && objectJson["objectType"].isString() ? objectJson["objectType"].asString() : "sprite";
                objInfo.sceneId = objectJson.isMember("scene") && objectJson["scene"].isString() ? objectJson["scene"].asString() : "";

                if (objectJson.isMember("sprite") && objectJson["sprite"].isObject() &&
                    objectJson["sprite"].isMember("pictures") && objectJson["sprite"]["pictures"].isArray())
                {

                    const Json::Value &picturesJson = objectJson["sprite"]["pictures"];
                    EngineStdOut("Found " + to_string(picturesJson.size()) + " pictures for object " + objInfo.name, 0);
                    for (const auto &pictureJson : picturesJson)
                    {
                        if (pictureJson.isObject() && pictureJson.isMember("id") && pictureJson["id"].isString() &&
                            pictureJson.isMember("filename") && pictureJson["filename"].isString())
                        { // 엔트리 스프라이트 배열에는 assetId 가 없음.

                            Costume ctu;
                            ctu.id = pictureJson["id"].asString();
                            ctu.name = pictureJson.isMember("name") && pictureJson["name"].isString() ? pictureJson["name"].asString() : "Unamed Shape";
                            ctu.filename = pictureJson["filename"].asString();
                            ctu.fileurl = pictureJson.isMember("fileurl") && pictureJson["fileurl"].isString() ? pictureJson["fileurl"].asString() : "";
                            // ctu.imageHandle is already nullptr by default. It will be loaded in loadImages.

                            objInfo.costumes.push_back(ctu);
                        }
                        else
                        {
                            EngineStdOut(objInfo.name + " object has Bad Picture structure", 1);
                        }
                    }
                }

                // --- 사운드 로드 ---
                if (objectJson.isMember("sprite") && objectJson["sprite"].isObject() &&
                    objectJson["sprite"].isMember("sounds") && objectJson["sprite"]["sounds"].isArray())
                {
                    const Json::Value &soundsJson = objectJson["sprite"]["sounds"];
                    EngineStdOut("Found " + to_string(soundsJson.size()) + " sounds. Parsing...", 0);

                    for (Json::Value::ArrayIndex i = 0; i < soundsJson.size(); ++i)
                    {
                        const auto &soundJson = soundsJson[i];
                        if (soundJson.isObject() && soundJson.isMember("id") && soundJson["id"].isString() && soundJson.isMember("filename") && soundJson["filename"].isString())
                        {
                            SoundFile sound;
                            string soundId = soundJson["id"].asString();
                            string soundName = soundJson["name"].asString();
                            string soundFilename = soundJson["filename"].asString();
                            string soundFileurl = soundJson["fileurl"].asString();
                            string soundfileExt = "";
                            if (soundJson.isMember("ext") && soundJson["ext"].isString())
                            {
                                soundfileExt = soundJson["ext"].asString();
                            }
                            double soundDuration = 0.0;
                            if (soundJson.isMember("duration") && soundJson["duration"].isNumeric())
                            {
                                soundDuration = soundJson["duration"].asDouble();
                            }
                            sound.id = soundId;
                            sound.name = soundName;
                            sound.filename = soundFilename;
                            sound.fileurl = soundFileurl;
                            sound.ext = soundfileExt;
                            sound.duration = soundDuration;
                            objInfo.sounds.push_back(sound);
                            EngineStdOut("  Parsed sound: " + soundName + " (ID: " + soundId + ")", 0);
                        }
                        else
                        {
                            EngineStdOut("Object '" + objInfo.name + "' has no 'sprite/sounds' array or it's invalid.", 1);
                        }
                    }
                }
                else
                {
                    // EngineStdOut("WARN: Failed to parse script JSON string for Sound object ", 1); // 이 로그는 불필요해 보임
                }

                if (objectJson.isMember("selectedPictureId") && objectJson["selectedPictureId"].isString())
                {
                    objInfo.selectedCostumeId = objectJson["selectedPictureId"].asString();
                }
                else if (objectJson.isMember("selectedCostume") && objectJson["selectedCostume"].isString())
                {
                    objInfo.selectedCostumeId = objectJson["selectedCostume"].asString();
                }
                else if (objectJson.isMember("selectedCostume") && objectJson["selectedCostume"].isObject() && objectJson["selectedCostume"].isMember("id") && objectJson["selectedCostume"]["id"].isString())
                {
                    objInfo.selectedCostumeId = objectJson["selectedCostume"]["id"].asString();
                }
                else
                {
                    if (!objInfo.costumes.empty())
                    {
                        objInfo.selectedCostumeId = objInfo.costumes[0].id;
                        EngineStdOut("Object '" + objInfo.name + "' (ID: " + objInfo.id + ") is missing selectedPictureId/selectedCostume or it's invalid. Using first costume ID: " + objInfo.costumes[0].id, 1);
                    }
                    else
                    {
                        EngineStdOut("Object '" + objInfo.name + "' (ID: " + objInfo.id + ") is missing selectedPictureId/selectedCostume and has no costumes.", 1);
                    }
                }

                if (objInfo.objectType == "textBox")
                {
                    if (objectJson.isMember("entity") && objectJson["entity"].isObject())
                    {
                        const Json::Value &entityJson = objectJson["entity"];
                        if (entityJson.isMember("text"))
                        {
                            if (entityJson["text"].isString())
                            {
                                objInfo.textContent = entityJson["text"].asString();
                            }
                            else if (entityJson["text"].isNumeric())
                            {
                                objInfo.textContent = to_string(entityJson["text"].asDouble());
                            }
                            else
                            {
                                objInfo.textContent = "[INVALID TEXT TYPE]";
                            }
                        }
                        else
                        {
                            objInfo.textContent = "[NO TEXT FIELD]";
                        }
                        if (entityJson.isMember("colour") && entityJson["colour"].isString())
                        {
                            string hexColor = entityJson["colour"].asString();
                            if (hexColor.length() == 7 && hexColor[0] == '#')
                            {
                                try
                                {
                                    unsigned int r = stoul(hexColor.substr(1, 2), nullptr, 16);
                                    unsigned int g = stoul(hexColor.substr(3, 2), nullptr, 16);
                                    unsigned int b = stoul(hexColor.substr(5, 2), nullptr, 16);
                                    objInfo.textColor = {(Uint8)r, (Uint8)g, (Uint8)b, 255};
                                }
                                catch (const exception &e)
                                {
                                    EngineStdOut("Failed to parse text color '" + hexColor + "' for object '" + objInfo.name + "': " + e.what(), 1);
                                    objInfo.textColor = {0, 0, 0, 255};
                                }
                            }
                            else
                            {
                                objInfo.textColor = {0, 0, 0, 255};
                            }
                        }
                        else
                        {
                            objInfo.textColor = {0, 0, 0, 255};
                        }
                        if (entityJson.isMember("font") && entityJson["font"].isString())
                        {
                            string fontString = entityJson["font"].asString();
                            size_t pxPos = fontString.find("px");
                            if (pxPos != string::npos)
                            {
                                try
                                {
                                    objInfo.fontSize = stoi(fontString.substr(0, pxPos));
                                }
                                catch (...)
                                {
                                    objInfo.fontSize = 20;
                                }
                                size_t spaceAfterPx = fontString.find(' ', pxPos + 2);
                                if (spaceAfterPx != string::npos)
                                {
                                    objInfo.fontName = fontString.substr(spaceAfterPx + 1);
                                }
                                else
                                {
                                    // "px" 뒤에 바로 글꼴 이름이 오는 경우 (공백 없음)
                                    objInfo.fontName = fontString.substr(pxPos + 2);
                                    // 앞뒤 공백 제거
                                    objInfo.fontName.erase(0, objInfo.fontName.find_first_not_of(" "));
                                    objInfo.fontName.erase(objInfo.fontName.find_last_not_of(" ") + 1);
                                }
                            }
                            else
                            {
                                objInfo.fontSize = 20;
                                objInfo.fontName = fontString;
                            }
                        }
                        else
                        {
                            objInfo.fontSize = 20;
                            objInfo.fontName = ""; // 기본 폰트 이름 (예: 나눔바른펜)
                        }
                        if (entityJson.isMember("textAlign") && entityJson["textAlign"].isNumeric())
                        {
                            objInfo.textAlign = entityJson["textAlign"].asInt();
                        }
                        else
                        {
                            objInfo.textAlign = 0;
                        }
                    }
                    else
                    {
                        objInfo.textContent = "[NO ENTITY BLOCK]";
                        objInfo.textColor = {0, 0, 0, 255};
                        objInfo.fontName = "";
                        objInfo.fontSize = 20;
                        objInfo.textAlign = 0;
                    }
                }
                else
                {
                    objInfo.textContent = "";
                    objInfo.textColor = {0, 0, 0, 255};
                    objInfo.fontName = "";
                    objInfo.fontSize = 20;
                    objInfo.textAlign = 0;
                }

                objects_in_order.push_back(objInfo);

                if (objectJson.isMember("entity") && objectJson["entity"].isObject())
                {
                    const Json::Value &entityJson = objectJson["entity"];

                    double initial_x = entityJson.isMember("x") && entityJson["x"].isDouble() ? entityJson["x"].asDouble() : 0.0;
                    double initial_y = entityJson.isMember("y") && entityJson["y"].isDouble() ? entityJson["y"].asDouble() : 0.0;
                    double initial_regX = entityJson.isMember("regX") && entityJson["regX"].isDouble() ? entityJson["regX"].asDouble() : 0.0;
                    double initial_regY = entityJson.isMember("regY") && entityJson["regY"].isDouble() ? entityJson["regY"].asDouble() : 0.0;
                    double initial_scaleX = entityJson.isMember("scaleX") && entityJson["scaleX"].isDouble() ? entityJson["scaleX"].asDouble() : 1.0;
                    double initial_scaleY = entityJson.isMember("scaleY") && entityJson["scaleY"].isDouble() ? entityJson["scaleY"].asDouble() : 1.0;
                    double initial_rotation = entityJson.isMember("rotation") && entityJson["rotation"].isDouble() ? entityJson["rotation"].asDouble() : 0.0;
                    double initial_direction = entityJson.isMember("direction") && entityJson["direction"].isDouble() ? entityJson["direction"].asDouble() : 90.0;
                    double initial_width = entityJson.isMember("width") && entityJson["width"].isDouble() ? entityJson["width"].asDouble() : 100.0;
                    double initial_height = entityJson.isMember("height") && entityJson["height"].isDouble() ? entityJson["height"].asDouble() : 100.0;
                    bool initial_visible = entityJson.isMember("visible") && entityJson["visible"].isBool() ? entityJson["visible"].asBool() : true;

                    Entity *newEntity = new Entity(
                        objectId,
                        objInfo.name,
                        initial_x, initial_y, initial_regX, initial_regY,
                        initial_scaleX, initial_scaleY, initial_rotation, initial_direction,
                        initial_width, initial_height, initial_visible);

                    entities[objectId] = newEntity;
                }
                else
                {
                    EngineStdOut("Object '" + objInfo.name + "' (ID: " + objectId + ") is missing 'entity' block or it's not an object. Cannot create Entity.", 1);
                }
                if (objectJson.isMember("script") && objectJson["script"].isString())
                {
                    string scriptString = objectJson["script"].asString();
                    Json::Reader scriptReader;
                    Json::Value scriptRoot;
                    Json::IStringStream is(scriptString);

                    if (scriptReader.parse(is, scriptRoot))
                    {
                        EngineStdOut("Script JSON parsed successfully for object: " + objInfo.name, 0);
                        if (scriptRoot.isArray())
                        {
                            vector<Script> scriptsForObject;
                            for (const auto &scriptListJson : scriptRoot)
                            {
                                if (scriptListJson.isArray())
                                {
                                    Script currentScript;
                                    for (const auto &blockJson : scriptListJson)
                                    {
                                        if (blockJson.isObject() && blockJson.isMember("id") && blockJson["id"].isString() &&
                                            blockJson.isMember("type") && blockJson["type"].isString())
                                        {
                                            Block block;
                                            block.id = blockJson["id"].asString();
                                            block.type = blockJson["type"].asString();
                                            if (blockJson.isMember("params") && blockJson["params"].isArray())
                                            {
                                                block.paramsJson = blockJson["params"];
                                            }
                                            // TODO: statements 파싱 로직 추가 (재귀적으로)
                                            currentScript.blocks.push_back(block);
                                        }
                                        else
                                        {
                                            EngineStdOut("WARN: Invalid block structure in script for object: " + objInfo.name, 1);
                                        }
                                    }
                                    scriptsForObject.push_back(currentScript);
                                }
                            }
                             objectScripts[objectId] = scriptsForObject;
                        }
                    }
                    else
                    {
                        EngineStdOut("WARN: Failed to parse script JSON string for object '" + objInfo.name + "': " + scriptReader.getFormattedErrorMessages(), 1);
                    }
                }
            }
            else
            {
                EngineStdOut("Invalid object structure encountered.", 1);
            }
        }
    }
    else
    {
        EngineStdOut("project.json is missing 'objects' array or it's not an array.", 1);
    }

    if (root.isMember("scenes") && root["scenes"].isArray())
    {
        const Json::Value &scenesJson = root["scenes"];
        EngineStdOut("Found " + to_string(scenesJson.size()) + " scenes. Parsing...", 0);
        for (Json::Value::ArrayIndex i = 0; i < scenesJson.size(); ++i)
        {
            const auto &sceneJson = scenesJson[i];
            if (sceneJson.isObject() && sceneJson.isMember("id") && sceneJson["id"].isString() && sceneJson.isMember("name") && sceneJson["name"].isString())
            {
                string sceneId = sceneJson["id"].asString();
                string sceneName = sceneJson["name"].asString();
                if (i == 0)
                {
                    firstSceneIdInOrder = sceneId;
                    EngineStdOut("Identified first scene in array order: " + sceneName + " (ID: " + firstSceneIdInOrder + ")", 0);
                }
                scenes[sceneId] = sceneName;
                EngineStdOut("  Parsed scene: " + sceneName + " (ID: " + sceneId + ")", 0);
            }
            else
            {
                EngineStdOut("WARN: Invalid scene structure encountered.", 1);
            }
        }
    }
    else
    {
        EngineStdOut("WARN: project.json is missing 'scenes' array or it's not an array.", 1);
    }

    string startSceneId = "";
    if (root.isMember("startScene") && root["startScene"].isString()) // 엔트리 구버전 호환
    {
        startSceneId = root["startScene"].asString();
        EngineStdOut("'startScene' (legacy) found in project.json: " + startSceneId, 0);
    }
    else if (root.isMember("start") && root["start"].isObject() && root["start"].isMember("sceneId") && root["start"]["sceneId"].isString())
    {
        startSceneId = root["start"]["sceneId"].asString();
        EngineStdOut("'start/sceneId' found in project.json: " + startSceneId, 0);
    }


    if (!startSceneId.empty() && scenes.count(startSceneId))
    {
        currentSceneId = startSceneId;
        EngineStdOut("Initial scene set to explicit start scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
    }
    else
    {
        if (!firstSceneIdInOrder.empty() && scenes.count(firstSceneIdInOrder))
        {
            currentSceneId = firstSceneIdInOrder;
            EngineStdOut("Initial scene set to first scene in array order: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        }
        else
        {
            EngineStdOut("ERROR: No valid starting scene found in project.json.", 2);
            return false;
        }
    }

    EngineStdOut("Identifying 'Start Button Clicked' scripts...", 0);
    startButtonScripts.clear();
    for (auto const &[objectId, scriptsVec] : objectScripts)
    {
        for (const auto &script : scriptsVec)
        {
            if (!script.blocks.empty())
            {
                const Block &firstBlock = script.blocks[0];
                if (firstBlock.type == "when_run_button_click")
                {
                     // 시작 블록 외에 다른 블록이 연결되어 있는지 확인 (실제 실행할 내용이 있는지)
                    if (script.blocks.size() > 1) {
                        startButtonScripts.push_back({objectId, &script});
                        EngineStdOut("  -> Found valid 'Start Button Clicked' script for object ID: " + objectId, 0);
                    } else {
                        EngineStdOut("  -> Found 'Start Button Clicked' script for object ID: " + objectId + " but it has no subsequent blocks. Skipping.", 1);
                    }
                }
            }
        }
    }

    EngineStdOut("Project JSON file parsed successfully.", 0);
    return true;
}

bool Engine::initGE(bool vsyncEnabled)
{
    EngineStdOut("Initializing SDL...", 0);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        string errMsg = "SDL could not initialize! SDL_Error: " + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to initialize SDL: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    
    EngineStdOut("SDL initialized successfully (Video and Audio).", 0);

    // 윈도우 생성
    // WINDOW_TITLE은 loadProject에서 설정됨
    this->window = SDL_CreateWindow(WINDOW_TITLE.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (this->window == nullptr)
    {
        string errMsg = "Window could not be created! SDL_Error: " + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create window: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        SDL_Quit();
        return false;
    }
    EngineStdOut("SDL Window created successfully.", 0);

    // 렌더러 생성
    Uint32 rendererFlags = SDL_RENDERER_VSYNC_ADAPTIVE; // 기본적으로 하드웨어 가속 사용
    if (vsyncEnabled)
    {
        rendererFlags |= SDL_RENDERER_VSYNC_ADAPTIVE; // SDL3에서는 SDL_RENDERER_VSYNC_ADAPTIVE 가 맞음
        EngineStdOut("VSync enabled for renderer.", 0);
    }
    else
    {
        EngineStdOut("VSync disabled for renderer.", 0);
    }
    this->renderer = SDL_CreateRenderer(this->window, nullptr); //Renderer flag 없음
    if (this->renderer == nullptr)
    {
        string errMsg = "Renderer could not be created! SDL_Error: " + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create renderer: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_SetRenderVSync(renderer,rendererFlags);
    EngineStdOut("SDL Renderer created successfully.", 0);

    // SDL_ttf 초기화
    if (TTF_Init() == -1) {
        string errMsg = "SDL_ttf could not initialize!";
        EngineStdOut(errMsg , 2);
        showMessageBox("Failed to initialize", msgBoxIconType.ICON_ERROR);
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
        SDL_Quit();
        return false;
    }
    EngineStdOut("SDL_ttf initialized successfully.", 0);

    // HUD 및 로딩 화면용 폰트 로드
    string defaultFontPath = "font/nanum_barunpen.ttf";
    hudFont = TTF_OpenFont(defaultFontPath.c_str(), 16);
    loadingScreenFont = TTF_OpenFont(defaultFontPath.c_str(), 20);

    if (!hudFont) { // hudFont만 체크해도 loadingScreenFont도 같은 파일이므로 유사한 문제일 가능성 높음
        string errMsg = "Failed to load default font! Font path: " + defaultFontPath;
        EngineStdOut(errMsg, 2);
        showMessageBox(errMsg, msgBoxIconType.ICON_WARNING); // 경고로 처리하고 일단 진행
    }
    if (!loadingScreenFont && hudFont) { // hudFont는 성공했는데 loadingScreenFont만 실패한 경우 (다른 크기 등)
         string errMsg = "Failed to load loading screen font (size 20)! Font path: " + defaultFontPath;
        EngineStdOut(errMsg, 1); // 경고
    }


    SDL_SetRenderDrawColor(this->renderer, 255, 255, 255, 255); // 기본 배경색 (흰색)

    initFps(); // FPS 카운터 초기화 (SDL_GetTicks 사용하도록 수정됨)

    if (!createTemporaryScreen())
    {
        EngineStdOut("Failed to create temporary screen texture during initGE.", 2);
        if (hudFont) TTF_CloseFont(hudFont);
        if (loadingScreenFont) TTF_CloseFont(loadingScreenFont);
        TTF_Quit();
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
        //IMG_Quit(); 이것도 SDL3 에는 없음
        SDL_Quit();
        return false;
    }
    EngineStdOut("Engine graphics initialization complete.", 0);
    return true;
}

bool Engine::createTemporaryScreen()
{
    if (this->renderer == nullptr)
    {
        EngineStdOut("Renderer not initialized. Cannot create temporary screen texture.", 2);
        showMessageBox("Internal Error: Renderer not available for offscreen buffer.", msgBoxIconType.ICON_ERROR);
        return false;
    }
    this->tempScreenTexture = SDL_CreateTexture(this->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, PROJECT_STAGE_WIDTH, PROJECT_STAGE_HEIGHT);
    if (this->tempScreenTexture == nullptr)
    {
        string errMsg = "Failed to create temporary screen texture! SDL_Error: " + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create offscreen buffer: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    EngineStdOut("Temporary screen texture created successfully (" + to_string(PROJECT_STAGE_WIDTH) + "x" + to_string(PROJECT_STAGE_HEIGHT) + ").", 0);
    return true;
}

/*int Engine::Soundloader(string soundUri)
{
    EngineStdOut("Soundloader: Needs implementation with SDL_mixer. Sound URI: " + soundUri, 1);
    return -1; // Placeholder
}*/
void Engine::destroyTemporaryScreen()
{
    if (this->tempScreenTexture != nullptr)
    {
        SDL_DestroyTexture(this->tempScreenTexture);
        this->tempScreenTexture = nullptr;
        EngineStdOut("Temporary screen texture destroyed.", 0);
    }
}

void Engine::findRunbtnScript()
{
    Engine::EngineStdOut("findRunbtnScript: This function seems to be a duplicate or placeholder. 'Start Button Clicked' scripts are identified in loadProject.", 1);
}

void Engine::terminateGE()
{
    EngineStdOut("Terminating SDL and engine resources...", 0);
    destroyTemporaryScreen();

    if (hudFont) {
        TTF_CloseFont(hudFont);
        hudFont = nullptr;
        EngineStdOut("HUD font closed.",0);
    }
    if (loadingScreenFont) {
        TTF_CloseFont(loadingScreenFont);
        loadingScreenFont = nullptr;
        EngineStdOut("Loading screen font closed.",0);
    }
    TTF_Quit();
    EngineStdOut("SDL_ttf terminated.", 0);

    if (this->renderer != nullptr)
    {
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        EngineStdOut("SDL Renderer destroyed.", 0);
    }
    if (this->window != nullptr)
    {
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
        EngineStdOut("SDL Window destroyed.", 0);
    }
    //IMG_Quit();
    EngineStdOut("SDL_image terminated.", 0);
    SDL_Quit();
    EngineStdOut("SDL terminated.", 0);
}

bool Engine::loadImages()
{
    EngineStdOut("Starting image loading...", 0);
    totalItemsToLoad = 0;
    loadedItemCount = 0;
    for (const auto &objInfo : objects_in_order)
    {
        if (objInfo.objectType == "sprite")
        {
            totalItemsToLoad += static_cast<int>(objInfo.costumes.size());
        }
    }
    EngineStdOut("Total image items to load: " + to_string(totalItemsToLoad), 0);

    int loadedCount = 0;
    int failedCount = 0;

    for (auto &objInfo : objects_in_order)
    {
        if (objInfo.objectType == "sprite")
        {
            for (auto &costume : objInfo.costumes)
            {
                string imagePath = string(BASE_ASSETS) + costume.fileurl;
                string fileExtension;
                size_t dotPos = imagePath.rfind('.');
                if (dotPos != string::npos)
                {
                    fileExtension = imagePath.substr(dotPos);
                }
                    SDL_Surface* surface = IMG_Load(imagePath.c_str());
                    if (surface != NULL) {
                        costume.imageHandle = SDL_CreateTextureFromSurface(this->renderer, surface);
                        SDL_DestroySurface(surface);

                        if (costume.imageHandle) {
                            loadedCount++;
                            EngineStdOut("  Shape '" + costume.name + "' (" + imagePath + ") image loaded successfully as SDL_Texture.", 0);
                        } else {
                            failedCount++;
                            EngineStdOut("ERROR: Failed to create texture from surface for '" + objInfo.name + "' shape '" + costume.name + "' from path: " + imagePath + ". SDL_Error: " + SDL_GetError(), 2);
                        }
                    } else {
                        failedCount++; // IMG_Load 실패도 실패 카운트에 포함
                        EngineStdOut("ERROR: IMG_Load failed for '" + objInfo.name + "' shape '" + costume.name + "' from path: " + imagePath, 2);
                    }
                incrementLoadedItemCount();
                if (loadedItemCount % 3 == 0 || costume.imageHandle == nullptr || loadedItemCount == totalItemsToLoad)
                {
                    renderLoadingScreen();
                    SDL_Event e; // 간단한 이벤트 처리 추가 (창 닫힘 등)
                    while (SDL_PollEvent(&e) != 0) {
                        if (e.type == SDL_EVENT_QUIT) {
                             EngineStdOut("Image loading cancelled by user.", 1);
                             return false; // 로딩 중단
                        }
                    }
                }
            }
        }
    }

    EngineStdOut("Image loading finished. Success: " + to_string(loadedCount) + ", Failed: " + to_string(failedCount), 0);

    if (failedCount > 0 && loadedCount == 0 && totalItemsToLoad > 0) {
        EngineStdOut("ERROR: All images failed to load. Cannot continue.", 2);
        showMessageBox("Fatal Error: No images could be loaded. Check asset paths and file integrity.", msgBoxIconType.ICON_ERROR);
        return false; // 모든 이미지 로드 실패 시 중단
    } else if (failedCount > 0) {
        EngineStdOut("WARN: Some images failed to load, processing with available resources.", 1);
    }
    return true;
}
void Engine::drawAllEntities()
{
    if (!renderer || !tempScreenTexture)
        return;

    SDL_SetRenderTarget(renderer, tempScreenTexture);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    for (int i = static_cast<int>(objects_in_order.size()) - 1; i >= 0; --i)
    {
        const ObjectInfo &objInfo = objects_in_order[i];
        bool isInCurrentScene = (objInfo.sceneId == currentSceneId);

        if (!isInCurrentScene && objInfo.sceneId != "global")
        {
            continue;
        }

        auto it_entity = entities.find(objInfo.id);
        if (it_entity == entities.end())
        {
            continue;
        }
        const Entity *entityPtr = it_entity->second;

        if (!entityPtr->isVisible())
        {
            continue;
        }
        if (objInfo.objectType == "sprite")
        {
            Costume *selectedCostume = nullptr;
            // selectedCostumeId를 사용하여 현재 의상 찾기 (objInfo.costumes는 const가 아니어야 함)
            for (auto &costume_ref : const_cast<ObjectInfo&>(objInfo).costumes) { // 임시 const_cast, ObjectInfo의 costumes가 const가 아니도록 수정 필요
                if (costume_ref.id == objInfo.selectedCostumeId) {
                    selectedCostume = &costume_ref;
                    break;
                }
            }


            if (selectedCostume && selectedCostume->imageHandle != nullptr)
            {
                double entryX = entityPtr->getX();
                double entryY = entityPtr->getY();
                // regX, regY는 SDL_RenderTexture의 center 파라미터에 사용됨
                // scaleX, scaleY는 dstRect의 w, h에 적용됨
                // rotation은 SDL_RenderTexture의 angle 파라미터에 사용됨

                // SDL 좌표계로 변환 (스테이지 중심 (0,0) -> 좌상단 (0,0))
                // Entry Y축은 위가 +, SDL Y축은 아래가 +
                float sdlX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                float sdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                float texW, texH;
                // SDL_QueryTexture(selectedCostume->imageHandle, nullptr, nullptr, &texW, &texH); // SDL2 방식
                if (SDL_GetTextureSize(selectedCostume->imageHandle,&texW, &texH) != 0) {
                    EngineStdOut("ERROR: Failed to get texture info for costume '" + selectedCostume->name + "' of object '" + objInfo.name + "'. SDL_Error: " + SDL_GetError(), 2);
                    // 오류 발생 시 해당 엔티티 그리기를 건너뛸 수 있습니다. 
                    // 또는 기본 크기를 사용하거나, 오류를 기록하고 계속 진행할 수 있습니다.
                    texW = 0; // 예시: 오류 시 크기를 0으로 설정하여 그리지 않도록 함
                    texH = 0;
                }

                SDL_FRect dstRect;
                dstRect.w = static_cast<float>(texW * entityPtr->getScaleX());
                dstRect.h = static_cast<float>(texH * entityPtr->getScaleY());
                // 위치는 중심점을 기준으로 설정
                dstRect.x = sdlX - dstRect.w / 2.0f; // 회전 중심점을 고려하여 x,y 조정 필요
                dstRect.y = sdlY - dstRect.h / 2.0f; // 회전 중심점을 고려하여 x,y 조정 필요


                // Entry 회전(시계방향이 +) -> SDL 회전(시계방향이 +)
                // Entry 방향(0도 오른쪽, 90도 위쪽) -> SDL 각도 (0도 오른쪽, 90도 아래쪽)
                // 에셋 자체가 90도 회전되어 있을 수 있음 (ASSET_ROTATION_CORRECTION_RADIAN)
                double sdlAngle = entityPtr->getRotation(); // Entry의 rotation 값을 그대로 사용 (SDL도 시계방향이 +)
                                                            // 에셋 보정 각도는 텍스처 자체에 적용되어야 하거나, 로딩 시점에 처리.
                                                            // 여기서는 entityPtr->getRotation()을 직접 사용.

                SDL_FPoint center; // 회전 및 스케일링 중심점 (텍스처 로컬 좌표)
                // Entry의 regX, regY는 이미지의 좌상단 기준 offset. SDL의 center는 너비/높이의 비율 또는 실제 픽셀값.
                // 여기서는 이미지의 중앙을 기준으로 가정. 필요시 objInfo에서 regX/Y 픽셀값을 읽어와서 사용.
                center.x = dstRect.w / 2.0f; // 기본값: 이미지 중앙
                center.y = dstRect.h / 2.0f; // 기본값: 이미지 중앙
                // TODO: objInfo.regX, objInfo.regY (픽셀 단위)를 사용하여 center 값 설정 필요

                SDL_RenderTextureRotated(renderer, selectedCostume->imageHandle, nullptr, &dstRect, sdlAngle, &center, SDL_FLIP_NONE);
            }
        }
        if (objInfo.objectType == "textBox")
        {
            // TODO: SDL_ttf를 사용하여 글상자 텍스트 렌더링 (drawHUD와 유사하게)
            // 폰트 이름(objInfo.fontName), 크기(objInfo.fontSize), 색상(objInfo.textColor), 내용(objInfo.textContent) 사용
            // 위치는 entityPtr->getX(), getY() 사용 (좌표계 변환 필요)
            // 텍스트 정렬(objInfo.textAlign) 구현 필요
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    int srcWidth = static_cast<int>(PROJECT_STAGE_WIDTH / zoomFactor);
    int srcHeight = static_cast<int>(PROJECT_STAGE_HEIGHT / zoomFactor);
    SDL_Rect srcRect;
    srcRect.x = (PROJECT_STAGE_WIDTH - srcWidth) / 2;
    srcRect.y = (PROJECT_STAGE_HEIGHT - srcHeight) / 2;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_FRect dstFRect = {0.0f, 0.0f, static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT)};
    SDL_FRect srcFRect = {static_cast<float>(srcRect.x), static_cast<float>(srcRect.y), static_cast<float>(srcRect.w), static_cast<float>(srcRect.h)};

    SDL_RenderTexture(renderer, tempScreenTexture, &srcFRect, &dstFRect);
}

void Engine::drawHUD()
{
    if (!this->renderer) return;

    // --- FPS 카운터 그리기 ---
    if (this->hudFont && this->specialConfig.showFPS) {
        string fpsText = "FPS: " + to_string(static_cast<int>(currentFps));
        SDL_Color textColor = {255, 150, 0, 255}; // 주황색

        SDL_Surface* textSurface = TTF_RenderText_Solid(hudFont, fpsText.c_str(),fpsText.size(), textColor);//텍스트 길이 (size_t) 필수
        if (textSurface) {
            SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            if (textTexture) {
                SDL_FRect dstRect = {10.0f, 10.0f, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};
                SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);
                SDL_DestroyTexture(textTexture);
            } else {
                EngineStdOut("Failed to create FPS text texture: " + string(SDL_GetError()), 2);
            }
            SDL_DestroySurface(textSurface);
        } else {
            EngineStdOut("Failed to render FPS text surface", 2);
        }
    }

    // ---HUD 에 프로젝트 이름 표시 (사용안함) ---
    /*if (this->hudFont && this->specialConfig.SHOW_PROJECT_NAME && !PROJECT_NAME.empty()) {
        SDL_Color textColor = {200, 200, 200, 255}; // 밝은 회색
        SDL_Surface* nameSurface = TTF_RenderText_Solid(hudFont, PROJECT_NAME.c_str(),PROJECT_NAME.size(), textColor);
        if (nameSurface) {
            SDL_Texture* nameTexture = SDL_CreateTextureFromSurface(renderer, nameSurface);
            if (nameTexture) {
                // 화면 너비 가져오기 (WINDOW_WIDTH는 전역 변수 또는 멤버 변수여야 함)
                int windowW = 0;
                SDL_GetRenderOutputSize(renderer, &windowW, nullptr); // 현재 렌더러의 출력 크기 사용
                SDL_FRect dstRect = { (windowW - static_cast<float>(nameSurface->w)) / 2.0f, 10.0f, static_cast<float>(nameSurface->w), static_cast<float>(nameSurface->h) };
                SDL_RenderTexture(renderer, nameTexture, nullptr, &dstRect);
                SDL_DestroyTexture(nameTexture);
            }
            SDL_DestroySurface(nameSurface);
        }
    }*/


    // --- 줌 슬라이더 UI 그리기 (설정에서 활성화된 경우) ---
    if (this->specialConfig.showZoomSlider)
    {
        SDL_FRect sliderBgRect = {static_cast<float>(SLIDER_X), static_cast<float>(SLIDER_Y), static_cast<float>(SLIDER_WIDTH), static_cast<float>(SLIDER_HEIGHT)};
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(renderer, &sliderBgRect);

        float handleX_float = SLIDER_X + ((zoomFactor - MIN_ZOOM) / (MAX_ZOOM - MIN_ZOOM)) * SLIDER_WIDTH;
        float handleWidth_float = 8.0f;
        SDL_FRect sliderHandleRect = {handleX_float - handleWidth_float / 2.0f, static_cast<float>(SLIDER_Y - 2), handleWidth_float, static_cast<float>(SLIDER_HEIGHT + 4)};
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer, &sliderHandleRect);

        if (this->hudFont) {
            std::ostringstream zoomStream;
            zoomStream << std::fixed << std::setprecision(2) << zoomFactor;
            string zoomText = "Zoom: " + zoomStream.str();
            SDL_Color textColor = {220, 220, 220, 255};

            SDL_Surface* textSurface = TTF_RenderText_Solid(hudFont, zoomText.c_str(),zoomText.size(), textColor);
            if (textSurface) {
                SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                if (textTexture) {
                    SDL_FRect dstRect = {SLIDER_X + SLIDER_WIDTH + 10.0f, SLIDER_Y + (SLIDER_HEIGHT - static_cast<float>(textSurface->h)) / 2.0f, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};
                    SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);
                    SDL_DestroyTexture(textTexture);
                }
                SDL_DestroySurface(textSurface);
            }
        }
    }
}

void Engine::processInput()
{
    // SDL 이벤트 루프에서 마우스 입력 처리 (메인 루프에서 호출)
    // 예시:
    // SDL_Event e;
    // while (SDL_PollEvent(&e) != 0) {
    //     if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    //         if (e.button.button == SDL_BUTTON_LEFT) {
    //             int mouseX = e.button.x;
    //             int mouseY = e.button.y;
    //             if (this->specialConfig.showZoomSlider &&
    //                 mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH &&
    //                 mouseY >= SLIDER_Y - 5 && mouseY <= SLIDER_Y + SLIDER_HEIGHT + 5) {
    //                 float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
    //                 zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
    //                 zoomFactor = std::max(MIN_ZOOM, std::min(MAX_ZOOM, zoomFactor));
    //             }
    //         }
    //     }
    // }
}
int Engine::mapEntryKeyToDxLibKey(const string &entryKey)
{
    // 이 함수는 DxLib 의존성을 제거하면서 SDL_Keycode 등으로 대체되어야 합니다.
    // 현재는 사용되지 않으므로 경고만 출력하거나 내용을 비워둘 수 있습니다.
    EngineStdOut("mapEntryKeyToDxLibKey is deprecated and needs replacement with SDL key handling.", 1);
    return -1;
}
void Engine::runStartButtonScripts()
{
    if (startButtonScripts.empty())
    {
        EngineStdOut("No 'Start Button Clicked' scripts found to run.", 1);
        return;
    }
    EngineStdOut("Running 'Start Button Clicked' scripts...", 0);
    for (const auto &scriptPair : startButtonScripts)
    {
        const string &objectId = scriptPair.first;
        const Script *scriptPtr = scriptPair.second;
        EngineStdOut(" -> Running script for object: " + objectId, 0);
        executeScript(*this, objectId, scriptPtr); // BlockExecutor.h/cpp에 정의된 함수
    }
}
void Engine::initFps()
{
    lastfpstime = SDL_GetTicks();
    framecount = 0;
    currentFps = 0.0f;
}
void Engine::setfps(int fps)
{
    this->specialConfig.TARGET_FPS = fps;
}
void Engine::updateFps()
{
    framecount++;
    Uint64 now = SDL_GetTicks(); // SDL_GetTicks()는 Uint32를 반환하지만, 경과 시간 계산을 위해 Uint64 사용 가능
    Uint64 delta = now - lastfpstime;

    if (delta >= 1000)
    {
        currentFps = static_cast<float>(framecount * 1000.0) / delta;
        lastfpstime = now;
        framecount = 0;
    }
}
Entity *Engine::getEntityById(const string &id)
{
    auto it = entities.find(id);
    if (it != entities.end())
    {
        return it->second;
    }
    return nullptr;
}

void Engine::renderLoadingScreen()
{
    if (!this->renderer) {
        EngineStdOut("renderLoadingScreen: Renderer not available.", 1);
        return;
    }

    SDL_SetRenderDrawColor(this->renderer, 30, 30, 30, 255);
    SDL_RenderClear(this->renderer);

    int barWidth = 400;
    int barHeight = 30;
    // 화면 너비 가져오기
    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH); // 현재 렌더러의 출력 크기 사용

    int barX = (windowW - barWidth) / 2;
    int barY = (windowH - barHeight) / 2;

    SDL_FRect bgRect = {static_cast<float>(barX), static_cast<float>(barY), static_cast<float>(barWidth), static_cast<float>(barHeight)};
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderFillRect(renderer, &bgRect);

    SDL_FRect innerBgRect = {static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(barWidth - 4), static_cast<float>(barHeight - 4)};
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderFillRect(renderer, &innerBgRect);

    float progressPercent = 0.0f;
    if (totalItemsToLoad > 0)
    {
        progressPercent = static_cast<float>(loadedItemCount) / totalItemsToLoad;
    }

    int progressWidth = static_cast<int>((barWidth - 4) * progressPercent);
    SDL_FRect progressRect = {static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(progressWidth), static_cast<float>(barHeight - 4)};
    SDL_SetRenderDrawColor(renderer, 255, 165, 0, 255);
    SDL_RenderFillRect(renderer, &progressRect);

    if (loadingScreenFont) {
        SDL_Color textColor = {220, 220, 220, 255};

        // 퍼센트 텍스트
        std::ostringstream percentStream;
        percentStream << std::fixed << std::setprecision(0) << (progressPercent * 100.0f) << "%";
        string percentText = percentStream.str();

        SDL_Surface* surfPercent = TTF_RenderText_Solid(loadingScreenFont, percentText.c_str(),percentText.size(), textColor);
        if (surfPercent) {
            SDL_Texture* texPercent = SDL_CreateTextureFromSurface(renderer, surfPercent);
            if (texPercent) {
                SDL_FRect dstRect = {barX + barWidth + 10.0f, barY + (barHeight - static_cast<float>(surfPercent->h)) / 2.0f, static_cast<float>(surfPercent->w), static_cast<float>(surfPercent->h)};
                SDL_RenderTexture(renderer, texPercent, nullptr, &dstRect);
                SDL_DestroyTexture(texPercent);
            }
            SDL_DestroySurface(surfPercent);
        }

        // 브랜드 이름
        if (!specialConfig.BRAND_NAME.empty()) {
            SDL_Surface* surfBrand = TTF_RenderText_Solid(loadingScreenFont, specialConfig.BRAND_NAME.c_str(),percentText.size(), textColor);
            if (surfBrand) {
                SDL_Texture* texBrand = SDL_CreateTextureFromSurface(renderer, surfBrand);
                if (texBrand) {
                    SDL_FRect dstRect = {(windowW - static_cast<float>(surfBrand->w)) / 2.0f, barY - static_cast<float>(surfBrand->h) - 10.0f, static_cast<float>(surfBrand->w), static_cast<float>(surfBrand->h)};
                    SDL_RenderTexture(renderer, texBrand, nullptr, &dstRect);
                    SDL_DestroyTexture(texBrand);
                }
                SDL_DestroySurface(surfBrand);
            }
        }
         // 프로젝트 이름
        if (specialConfig.SHOW_PROJECT_NAME && !PROJECT_NAME.empty()) {
            SDL_Surface* surfProject = TTF_RenderText_Solid(loadingScreenFont, PROJECT_NAME.c_str(),PROJECT_NAME.size(), textColor);
            if (surfProject) {
                SDL_Texture* texProject = SDL_CreateTextureFromSurface(renderer, surfProject);
                if (texProject) {
                    SDL_FRect dstRect = {(windowW - static_cast<float>(surfProject->w)) / 2.0f, barY + barHeight + 10.0f, static_cast<float>(surfProject->w), static_cast<float>(surfProject->h)};
                    SDL_RenderTexture(renderer, texProject, nullptr, &dstRect);
                    SDL_DestroyTexture(texProject);
                }
                SDL_DestroySurface(surfProject);
            }
        }
    }
    SDL_RenderPresent(this->renderer);
}

const string &Engine::getCurrentSceneId() const
{
    return currentSceneId;
}

void Engine::showMessageBox(const string &message, int IconType)
{
    Uint32 flags = 0;
    const char* title = OMOCHA_ENGINE_NAME;
    switch (IconType)
    {
    case SDL_MESSAGEBOX_ERROR:
        flags = SDL_MESSAGEBOX_ERROR;
        title = "Omocha is Broken";
        break;
    case SDL_MESSAGEBOX_WARNING:
        flags = SDL_MESSAGEBOX_WARNING;
        title = OMOCHA_ENGINE_NAME;
        break;
    case SDL_MESSAGEBOX_INFORMATION:
        flags = SDL_MESSAGEBOX_INFORMATION;
        title = OMOCHA_ENGINE_NAME;
        break;
    default:
        // 알 수 없는 IconType 처리, 정보 메시지 또는 오류 로그로 기본 설정
        EngineStdOut("Unknown IconType passed to showMessageBox: " + std::to_string(IconType), 1);
        flags = SDL_MESSAGEBOX_INFORMATION; // 정보 메시지로 기본 설정
        title = "Message";
        break;
    }
    SDL_ShowSimpleMessageBox(flags, title, message.c_str(), this->window);
}

void Engine::EngineStdOut(string s, int LEVEL)
{
    string prefix;
    string color_code = ANSI_COLOR_RESET; // 기본값

    switch (LEVEL)
    {
    case 0: // INFO
        prefix = "[INFO]";
        color_code = ANSI_COLOR_CYAN;
        break;
    case 1: // WARN
        prefix = "[WARN]";
        color_code = ANSI_COLOR_YELLOW;
        break;
    case 2: // ERROR
        prefix = "[ERROR]";
        color_code = ANSI_COLOR_RED;
        break;
    case 3: // THREAD (또는 DEBUG)
        prefix = "[DEBUG]"; // THR 대신 DEBUG로 변경 또는 유지
        color_code = ANSI_STYLE_BOLD; // 색상 없이 굵게만
        break;
    case 4: // OMOCHA_ENGINE_INFO (특별 레벨)
        prefix = "[" + string(OMOCHA_ENGINE_NAME) + "]";
        color_code = ANSI_COLOR_YELLOW + ANSI_STYLE_BOLD; // 예시: 노란색 + 굵게
        break;
    default:
        prefix = "[LOG]";
        break;
    }

    // 콘솔 출력
    // MinGW나 일부 터미널에서는 ANSI 이스케이프 코드가 기본적으로 지원되지 않을 수 있음
    // #ifdef _WIN32 // Windows 특정 코드 (예: SetConsoleTextAttribute)를 사용하거나, ANSI 지원 터미널 사용 가정
    // #endif
    printf("%s%s %s%s\n", color_code.c_str(), prefix.c_str(), s.c_str(), ANSI_COLOR_RESET.c_str());

    // 파일 로그
    string logMessage = format("{} {}", prefix, s); // C++20 format 사용
    logger.log(logMessage);
}
