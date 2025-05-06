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

Engine::Engine() : tempScreenHandle(-1), totalItemsToLoad(0), loadedItemCount(0), zoomFactor(1.26f), logger("omocha_engine.log") // zoomFactor 초기화 추가
{
    EngineStdOut(string(OMOCHA_ENGINE_NAME) + " v" + string(OMOCHA_ENGINE_VERSION) + " " + string(OMOCHA_DEVELOPER_NAME), 4);
    EngineStdOut("See Project page " + string(OMOCHA_ENGINE_GITHUB), 4);
}

Engine::~Engine()
{
    for (auto const &pair : entities)
    {
        delete pair.second;
    }
    entities.clear();
    // 로딩 폰트 핸들 해제
    if (loadingFontHandle != -1)
    {
        DeleteFontToHandle(loadingFontHandle);
        loadingFontHandle = -1;
    }

    destroyTemporaryScreen();
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
            this->specialConfig.showZoomSlider = specialConfigJson["showZoomSliderUI"].asBool();
            if (this->specialConfig.showZoomSlider)
            {
                this->specialConfig.showZoomSlider = true;
            }
            else
            {
                this->specialConfig.showZoomSlider = false;
            }
            this->specialConfig.SHOW_PROJECT_NAME = specialConfigJson["showProjectNameUI"].asBool();
            if (this->specialConfig.SHOW_PROJECT_NAME)
            {
                this->specialConfig.SHOW_PROJECT_NAME = true;
            }
            else
            {
                this->specialConfig.SHOW_PROJECT_NAME = false;
            }
        }

        // 예시: project.json에서 showZoomSlider 설정을 읽어오는 로직 (선택 사항)
        // if(root["specialConfig"].isMember("showZoomSliderUI") && root["specialConfig"]["showZoomSliderUI"].isBool())
        // {
        //     this->specialConfig.showZoomSlider = root["specialConfig"]["showZoomSliderUI"].asBool();
        // }
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
                            ctu.imageHandle = -1;

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
                            Sound sound;
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
                    EngineStdOut("WARN: Failed to parse script JSON string for Sound object ", 1);
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
                                    objInfo.textColor = GetColor(r, g, b);
                                }
                                catch (const exception &e)
                                {
                                    EngineStdOut("Failed to parse text color '" + hexColor + "' for object '" + objInfo.name + "': " + e.what(), 1);
                                    objInfo.textColor = GetColor(0, 0, 0);
                                }
                            }
                            else
                            {
                                objInfo.textColor = GetColor(0, 0, 0);
                            }
                        }
                        else
                        {
                            objInfo.textColor = GetColor(0, 0, 0);
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
                                    objInfo.fontName = fontString.substr(pxPos + 2);
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
                            objInfo.fontName = "";
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
                        objInfo.textColor = GetColor(0, 0, 0);
                        objInfo.fontName = "";
                        objInfo.fontSize = 20;
                        objInfo.textAlign = 0;
                    }
                }
                else
                {
                    objInfo.textContent = "";
                    objInfo.textColor = GetColor(0, 0, 0);
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

                    // 스크립트 문자열을 다시 JSON으로 파싱
                    Json::Reader scriptReader;
                    Json::Value scriptRoot; // 스크립트 JSON의 루트 (보통 배열)

                    // 문자열에서 파싱하기 위해 Json::StringStream 사용
                    Json::IStringStream is(scriptString);

                    // 파싱 실행
                    if (scriptReader.parse(is, scriptRoot))
                    {
                        // 스크립트 JSON 파싱 성공
                        EngineStdOut("Script JSON parsed successfully for object: " + objInfo.name, 0);

                        // project.json의 script 형식은 보통 [[...], [...], ...] 형태입니다.
                        // 각 내부 배열이 하나의 스크립트 목록(예: "시작" 탭의 스크립트)입니다.
                        if (scriptRoot.isArray())
                        {
                            // 모든 스크립트 목록을 순회합니다.
                            for (const auto &scriptListJson : scriptRoot)
                            {
                                if (scriptListJson.isArray())
                                {
                                    Script currentScript; // Block.h에 정의된 Script 구조체

                                    // 스크립트 목록 안의 블록들을 순회합니다.
                                    for (const auto &blockJson : scriptListJson)
                                    {
                                        if (blockJson.isObject() && blockJson.isMember("id") && blockJson["id"].isString() &&
                                            blockJson.isMember("type") && blockJson["type"].isString())
                                        {

                                            Block block; // Block.h에 정의된 Block 구조체
                                            block.id = blockJson["id"].asString();
                                            block.type = blockJson["type"].asString();

                                            // 파라미터 파싱 (params 필드)
                                            if (blockJson.isMember("params") && blockJson["params"].isArray())
                                            {
                                                // paramsJson에 Json::Value 통째로 저장
                                                block.paramsJson = blockJson["params"];
                                            }

                                            // 내부 스크립트 파싱 (statements 필드)
                                            if (blockJson.isMember("statements") && blockJson["statements"].isArray())
                                            {
                                                // statements는 [[블록들], [블록들], ...] 형태일 수 있습니다.
                                                // statementsScripts 벡터에 Script 구조체들을 저장합니다.
                                                for (const auto &statementScriptListJson : blockJson["statements"])
                                                {
                                                    if (statementScriptListJson.isArray())
                                                    {
                                                        Script statementScript;
                                                        // statementsScriptListJson 안의 블록들을 파싱하여 statementScript.blocks에 추가
                                                        // (이 부분은 재귀 함수 등으로 처리할 수 있습니다)
                                                        // 예시: parseBlocks(statementScriptListJson, statementScript.blocks);
                                                        // 현재는 간단히 비워둡니다. 실제 구현 시 파싱 로직 추가 필요.
                                                        block.statementScripts.push_back(statementScript);
                                                    }
                                                }
                                            }

                                            // 파싱된 블록을 현재 스크립트 목록에 추가
                                            currentScript.blocks.push_back(block);
                                        }
                                        else
                                        {
                                            EngineStdOut("WARN: Invalid block structure in script for object: " + objInfo.name, 1);
                                        }
                                    }
                                }
                            } // 스크립트 목록 순회 끝
                        } // scriptRoot.isArray() 확인 끝
                    }
                    else
                    {
                        EngineStdOut("WARN: Failed to parse script JSON string for object '" + objInfo.name + "': " + scriptReader.getFormattedErrorMessages(), 1);
                    }
                } // --- 스크립트 파싱 끝 ---
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

        // --- JSON 배열 순서대로 순회 ---
        for (Json::Value::ArrayIndex i = 0; i < scenesJson.size(); ++i)
        {
            const auto &sceneJson = scenesJson[i];

            if (sceneJson.isObject() && sceneJson.isMember("id") && sceneJson["id"].isString() && sceneJson.isMember("name") && sceneJson["name"].isString())
            {
                string sceneId = sceneJson["id"].asString();
                string sceneName = sceneJson["name"].asString();

                // --- 첫 번째 씬 ID 저장 ---
                if (i == 0)
                { // 배열의 첫 번째 요소일 경우
                    firstSceneIdInOrder = sceneId;
                    EngineStdOut("Identified first scene in array order: " + sceneName + " (ID: " + firstSceneIdInOrder + ")", 0);
                }
                // -------------------------

                // scenes 맵에 저장 (ID로 찾기 위함)
                scenes[sceneId] = sceneName;

                EngineStdOut("  Parsed scene: " + sceneName + " (ID: " + sceneId + ")", 0);
            }
            else
            {
                EngineStdOut("WARN: Invalid scene structure encountered.", 1);
            }
        } // --- JSON 배열 순회 루프 끝 ---
    }
    else
    {
        EngineStdOut("WARN: project.json is missing 'scenes' array or it's not an array.", 1);
    }

    // 엔트리 의 게시물은 항상 첫번째 씬부터 보여준다.
    // 이걸 이용해서 썸네일 을 보여준다.
    /*if (!scenes.empty()) {
        currentSceneId = scenes.begin()->first;
        EngineStdOut("Initial scene set to: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
    }
    else {
            EngineStdOut(" No scenes found in project.json.", 2);
            return false;
    }*/

    // --- 시작 씬 설정 로직 수정 ---
    string startSceneId = "";
    if (root.isMember("start") && root["start"].isObject() && root["start"].isMember("sceneId") && root["start"]["sceneId"].isString())
    {
        startSceneId = root["start"]["sceneId"].asString();
        EngineStdOut("'start/sceneId' found in project.json: " + startSceneId, 0);
    }

    // project.json에 start/sceneId가 명시되어 있거나
    // start/sceneId는 없지만 scenes 맵에 해당 ID가 있는 경우 (안전장치)
    if (!startSceneId.empty() && scenes.count(startSceneId))
    {
        currentSceneId = startSceneId;
        EngineStdOut("Initial scene set to explicit start scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
    }
    else
    {
        // start/sceneId가 없거나 유효하지 않은 경우,
        // project.json 배열 순서의 첫 번째 씬을 사용합니다.
        if (!firstSceneIdInOrder.empty() && scenes.count(firstSceneIdInOrder))
        { // 첫 번째 씬 ID가 유효한지 확인
            currentSceneId = firstSceneIdInOrder;
            EngineStdOut("Initial scene set to first scene in array order: " + scenes[currentSceneId] + " (ID: " + currentSceneId); // INFO 대신 WARN 레벨 사용
        }
        else
        {
            // 첫 번째 씬도 없는 경우 (씬이 아예 없거나 파싱 실패)
            EngineStdOut("ERROR: No valid starting scene found in project.json (neither explicit nor first in array).", 2);
            return false; // 시작 씬 없으면 로드 실패
        }
    }

    // --- 모든 스크립트 파싱 후 시작 버튼 스크립트 식별 ---
    EngineStdOut("Identifying 'Start Button Clicked' scripts...", 0);
    startButtonScripts.clear(); // 기존 목록 초기화
    for (auto const &[objectId, scripts] : objectScripts)
    {
        for (const auto &script : scripts)
        {
            // 스크립트가 비어있지 않고, 시작 블록 외에 다른 블록이 연결되어 있는지 확인
            if (!script.blocks.empty() && script.blocks.size() > 1)
            {
                if (script.blocks[0].type == "when_run_button_click")
                {
                    startButtonScripts.push_back({objectId, &script});
                    EngineStdOut("  -> Found valid 'Start Button Clicked' script for object ID: " + objectId, 0);
                }
            }
        }
    }
    // -------------------------------------------------

    EngineStdOut("Project JSON file parsed successfully.", 0);
    return true;
}

bool Engine::initGE()
{
    if (ChangeWindowMode(TRUE) == -1)
    {
        showMessageBox("Failed to set window mode.", msgBoxIconType.ICON_ERROR);
        EngineStdOut("DxLib initialization failed: Failed to set window mode.", 2);
        return false;
    }
    EngineStdOut("DxLib initialize", 0);
    if (DxLib_Init() == -1)
    {
        showMessageBox("Failed to initialize DxLib.", msgBoxIconType.ICON_ERROR);
        EngineStdOut("DxLib initialization failed: DxLib_Init() returned -1.", 2);
        return false;
    }
    InitFontToHandle(); // 폰트를 초기화 안해서 오류떴을수 있음.

    // 로딩 화면용 폰트 미리 로드
    loadingFontHandle = Fontloader("font/NanumBarunpenR.ttf");
    if (loadingFontHandle == -1)
    {
        EngineStdOut("Failed to load essential loading screen font. Loading screen text might not display.", 1);
        // 필요하다면 여기서 오류 처리 또는 메시지 박스 표시
    }

    initFps();
    SetUseCharSet(DX_CHARCODEFORMAT_UTF8);
    SetBackgroundColor(255, 255, 255);
    string projectName = PROJECT_NAME;
    SetWindowTextDX(projectName.c_str());

    if (SetGraphMode(WINDOW_WIDTH, WINDOW_HEIGHT, 32, this->specialConfig.TARGET_FPS) == -1)
    {
        showMessageBox("Failed to set graphics mode to window size.", msgBoxIconType.ICON_ERROR);
        EngineStdOut("DxLib initialization failed: Failed to set graphics mode.", 2);
        return false;
    }

    if (!createTemporaryScreen())
    {
        showMessageBox("Failed to create temporary screen buffer.", msgBoxIconType.ICON_ERROR);
        EngineStdOut("DxLib initialization failed: Failed to create temporary screen buffer.", 2);
        return false;
    }
    EngineStdOut("Created temporary screen handle: " + to_string(tempScreenHandle), 0);
    SetDrawScreen(DX_SCREEN_BACK);
    SetFontSize(20);
    SetMouseDispFlag(true);
    EngineStdOut("DxLib initialized", 0);
    return true;
}

bool Engine::createTemporaryScreen()
{
    tempScreenHandle = MakeScreen(PROJECT_STAGE_WIDTH, PROJECT_STAGE_HEIGHT, TRUE);
    if (tempScreenHandle == -1)
    {
        return false;
    }
    return true;
}
int Engine::Fontloader(string fontpath)
{
    if (filesystem::exists(fontpath.c_str()) == false)
    {
        EngineStdOut("font is not found.", 2);
        return -1;
    }
    // 폰트로더
    int fontHandle = LoadFontDataToHandle(fontpath.c_str());
    if (fontHandle == -1)
    {
        EngineStdOut("Failed to load font: " + fontpath, 2);
    }
    else
    {
        EngineStdOut("Loaded font: " + fontpath, 0);
    }
    return fontHandle;
}
/**
 * @brief 소리를 로드합니다.
 *
 * @param soundName
 */
int Engine::Soundloader(string soundUri)
{
    // 소리로더
    int soundHandle = LoadSoundMem(soundUri.c_str());
    if (soundHandle == -1)
    {
        EngineStdOut("Failed to load sound: " + soundUri, 2);
    }
    else
    {
        EngineStdOut("Loaded sound: " + soundUri, 0);
    }
    return soundHandle;
}
void Engine::destroyTemporaryScreen()
{
    if (tempScreenHandle != -1)
    {
        DeleteGraph(tempScreenHandle);
        tempScreenHandle = -1;
    }
}

void Engine::findRunbtnScript()
{
    Engine::EngineStdOut("find run Button Block...");
}

void Engine::terminateGE()
{
    destroyTemporaryScreen();
    DxLib_End();
    EngineStdOut("DxLib terminated", 0);
}

bool Engine::loadImages()
{
    EngineStdOut("Starting image loading...", 0);

    // 1. 로드할 총 아이템 수 계산 (루프 시작 전)
    totalItemsToLoad = 0;
    loadedItemCount = 0; // 카운터 초기화
    for (const auto &objInfo : objects_in_order)
    {
        if (objInfo.objectType == "sprite")
        {
            totalItemsToLoad += objInfo.costumes.size();
        }
        // 필요하다면 다른 타입의 로딩 아이템 (사운드 등) 개수도 여기서 합산
    }
    EngineStdOut("Total items to load: " + to_string(totalItemsToLoad), 0);

    int loadedCount = 0;
    int failedCount = 0;

    // 2. 이미지 로딩 및 로딩 화면 업데이트
    for (auto &objInfo : objects_in_order) // ObjectInfo 참조를 사용하도록 수정
    {
        if (objInfo.objectType == "sprite")
        {
            for (auto &costume : objInfo.costumes)
            {
                string imagePath = "";
                // filename 이 아닌 fileurl 사용
                imagePath = string(BASE_ASSETS) + costume.fileurl;
                incrementLoadedItemCount();
                int handle = -1;
                string fileExtension;
                size_t dotPos = imagePath.rfind('.');
                if (dotPos != string::npos)
                {
                    fileExtension = imagePath.substr(dotPos);
                }

                if (fileExtension == ".svg" || fileExtension == ".webp")
                {
                    EngineStdOut("  Shape '" + costume.name + "' (" + imagePath + "): " + fileExtension + " file, DXLib LoadGraph failed (expected).", 2);
                    failedCount++;
                    continue;
                }

                handle = LoadGraph(imagePath.c_str());

                if (handle != -1)
                {
                    costume.imageHandle = handle;
                    loadedCount++;
                    EngineStdOut("  Shape '" + costume.name + "' (" + imagePath + ") image loaded successfully, handle: " + to_string(handle), 0);
                }
                else
                {
                    failedCount++;
                    EngineStdOut("ERROR: Image load failed for '" + objInfo.name + "' shape '" + costume.name + "' from path: " + imagePath, 2);
                }

                // 3. 로딩 화면 그리기 및 메시지 처리 (각 이미지 로드 후)
                renderLoadingScreen();
                if (ProcessMessage() == -1)
                    return false; // 창이 닫히면 로딩 중단
            }
        }
    }

    EngineStdOut("Image loading finished. Success: " + to_string(loadedCount) + ", Failed: " + to_string(failedCount), 0);

    if (failedCount > 0)
    {
        EngineStdOut("WARN: Some image failed to load, processing with available resources.", 1);
        // 실패해도 계속 진행할지 여부에 따라 return false; 또는 true; 결정
    }
    return true;
}
void Engine::drawAllEntities()
{
    SetDrawScreen(tempScreenHandle);
    ClearDrawScreen();
    for (int i = static_cast<int>(objects_in_order.size()) - 1; i >= 0; --i)
    { // vector 순회 (순서 유지)
        const ObjectInfo &objInfo = objects_in_order[i];
        // 현재 씬에 속하는지 확인 (ObjectInfo에서 sceneId 확인)
        bool isInCurrentScene = (objInfo.sceneId == currentSceneId);

        if (!isInCurrentScene && objInfo.sceneId != "global")
        {
            continue;
        }

        // entities 맵에서 해당 ObjectInfo의 Entity*를 찾습니다.
        auto it_entity = entities.find(objInfo.id);
        if (it_entity == entities.end())
        {
            continue;
        }
        const Entity *entityPtr = it_entity->second;

        if (!entityPtr->isVisible())
        {
            continue; // 보이지 않으면 건너뜁니다.
        }
        // 스프라이트
        if (objInfo.objectType == "sprite")
        {

            Costume *selectedCostume = nullptr;
            for (const auto &costume : objInfo.costumes)
            {
                if (costume.id == objInfo.selectedCostumeId)
                {
                    selectedCostume = (Costume *)&costume;
                    break;
                }
            }

            if (selectedCostume && selectedCostume->imageHandle != -1)
            {
                double entryX = entityPtr->getX();
                double entryY = entityPtr->getY();
                double regX = entityPtr->getRegX();
                double regY = entityPtr->getRegY();
                double scaleX = entityPtr->getScaleX();
                double scaleY = entityPtr->getScaleY();
                double rotation = entityPtr->getRotation();

                float dxlibX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                float dxlibY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                double entryAngleDegrees = rotation;
                double dxlibBaseAngleDegrees = 90.0 - entryAngleDegrees;
                double assetCorrectionDegrees = ASSET_ROTATION_CORRECTION_RADIAN * 180.0 / DX_PI;
                double finalDxlibAngleDegrees = dxlibBaseAngleDegrees + assetCorrectionDegrees;
                double dxlibAngleRadians = finalDxlibAngleDegrees * DX_PI / 180.0;

                DrawRotaGraph3F(
                    dxlibX, dxlibY,
                    static_cast<float>(regX), static_cast<float>(regY),
                    scaleX, // Use original scale
                    scaleY, // Use original scale
                    dxlibAngleRadians,
                    selectedCostume->imageHandle,
                    TRUE);
            }
        }
        // 글상자
        if (objInfo.objectType == "textBox")
        {
            string textToDraw = objInfo.textContent;
            unsigned int textColor = objInfo.textColor;
            int fontSize = objInfo.fontSize;

            if (!textToDraw.empty())
            {
                double entryX = entityPtr->getX();
                double entryY = entityPtr->getY();

                float dxlibX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                float dxlibY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);
                ChangeFontType(DX_FONTTYPE_ANTIALIASING);
                SetFontSize(fontSize);
                // 폰트 변경
                switch (getFontNameFromString(objInfo.fontName))
                {
                case FontName::D2Coding:

                    break;
                case FontName::MaruBuri:
                    break;
                case FontName::NanumBarunPen:
                    break;
                case FontName::NanumGothic:
                    break;
                case FontName::NanumMyeongjo:
                    break;
                case FontName::NanumSquareRound:
                    break;
                default:
                    break;
                }
                DrawStringF(
                    dxlibX, dxlibY,
                    textToDraw.c_str(),
                    textColor);

                // TODO: 텍스트 정렬 (objInfo.textAlign) 로직 구현 필요
            }
        }
    }

    // EngineStdOut("DrawExtendGraph - Target: (0, 0) to (" + to_string(WINDOW_WIDTH) + ", " + to_string(WINDOW_HEIGHT) + "), Source Handle: " + to_string(tempScreenHandle), 0);
    SetDrawMode(DX_DRAWMODE_BILINEAR);
    SetDrawScreen(DX_SCREEN_BACK);
    ClearDrawScreen();

    // 확대 비율에 따라 원본(tempScreenHandle)에서 가져올 영역의 크기 계산
    int srcWidth = static_cast<int>(PROJECT_STAGE_WIDTH / zoomFactor);
    int srcHeight = static_cast<int>(PROJECT_STAGE_HEIGHT / zoomFactor);

    // 가져올 영역이 중앙에 위치하도록 좌상단 좌표 계산
    int srcX = (PROJECT_STAGE_WIDTH - srcWidth) / 2;
    int srcY = (PROJECT_STAGE_HEIGHT - srcHeight) / 2;

    // 계산된 소스 영역(srcX, srcY, srcWidth, srcHeight)을 사용하여
    // tempScreenHandle의 중앙 부분을 전체 창(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT)에 확대하여 그립니다.
    DrawRectExtendGraph(
        0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, // 대상 영역 (창 전체)
        srcX, srcY, srcWidth, srcHeight,   // 소스 영역 (tempScreenHandle의 중앙 부분)
        tempScreenHandle,                  // 소스 이미지 핸들
        TRUE                               // 투명도 처리 활성화
    );

    // --- Draw Zoom Slider UI (if enabled in config) ---
    if (this->specialConfig.showZoomSlider)
    {
        // Slider Background
        DrawBox(SLIDER_X, SLIDER_Y, SLIDER_X + SLIDER_WIDTH, SLIDER_Y + SLIDER_HEIGHT, GetColor(50, 50, 50), TRUE);
        // Slider Handle
        int handleX = SLIDER_X + static_cast<int>(((zoomFactor - MIN_ZOOM) / (MAX_ZOOM - MIN_ZOOM)) * SLIDER_WIDTH);
        int handleWidth = 8;
        DrawBox(handleX - handleWidth / 2, SLIDER_Y - 2, handleX + handleWidth / 2, SLIDER_Y + SLIDER_HEIGHT + 2, GetColor(200, 200, 200), TRUE);
        // Zoom Factor Text
        string zoomText = "Zoom: " + to_string(zoomFactor);
        int textWidth = GetDrawStringWidth(zoomText.c_str(), zoomText.length());
        DrawString(SLIDER_X + SLIDER_WIDTH + 10, SLIDER_Y + (SLIDER_HEIGHT / 2) - (GetFontSize() / 2), zoomText.c_str(), GetColor(255, 255, 255));
    }
    // --- End UI Drawing ---
}

void Engine::drawHUD()
{
    // 그리기 대상을 백 버퍼로 설정 (drawAllEntities 이후에 호출되므로 이미 설정되어 있을 수 있지만 명시)
    SetDrawScreen(DX_SCREEN_BACK);

    // --- FPS 카운터 그리기 ---
    unsigned int fpsColor = GetColor(255, 150, 0); // FPS 텍스트 색상
    string FPS_STRING = "FPS: " + to_string(static_cast<int>(currentFps));
    SetFontSize(16);
    DrawStringF(
        10, 10, // FPS 표시 위치
        FPS_STRING.c_str(),
        fpsColor);

    // --- 줌 슬라이더 UI 그리기 (설정에서 활성화된 경우) ---
    if (this->specialConfig.showZoomSlider)
    {
        // Slider Background
        DrawBox(SLIDER_X, SLIDER_Y, SLIDER_X + SLIDER_WIDTH, SLIDER_Y + SLIDER_HEIGHT, GetColor(50, 50, 50), TRUE);
        // Slider Handle
        int handleX = SLIDER_X + static_cast<int>(((zoomFactor - MIN_ZOOM) / (MAX_ZOOM - MIN_ZOOM)) * SLIDER_WIDTH);
        int handleWidth = 8;
        DrawBox(handleX - handleWidth / 2, SLIDER_Y - 2, handleX + handleWidth / 2, SLIDER_Y + SLIDER_HEIGHT + 2, GetColor(200, 200, 200), TRUE);
        // Zoom Factor Text
        string zoomText = "Zoom: " + to_string(zoomFactor);
        DrawString(SLIDER_X + SLIDER_WIDTH + 10, SLIDER_Y + (SLIDER_HEIGHT / 2) - (GetFontSize() / 2), zoomText.c_str(), GetColor(255, 255, 255));
    }
}

void Engine::processInput()
{
    int mouseX, mouseY;
    GetMousePoint(&mouseX, &mouseY);

    // --- Process Slider Input (if enabled and mouse interacts) ---
    if (this->specialConfig.showZoomSlider &&                             // Check if slider is enabled
        (GetMouseInput() & MOUSE_INPUT_LEFT) != 0 &&                      // Check if left button is pressed
        mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH &&        // Check horizontal bounds
        mouseY >= SLIDER_Y - 5 && mouseY <= SLIDER_Y + SLIDER_HEIGHT + 5) // Check vertical bounds (with tolerance)
    {
        // Calculate and update zoomFactor based on mouse position
        float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
        zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
        zoomFactor = max(MIN_ZOOM, min(MAX_ZOOM, zoomFactor)); // Clamp value
    }

    // 여기에 엔트리 키입력 을 구현해야겠지?
}
// 시작버튼
void Engine::runStartButtonScripts()
{
    if (startButtonScripts.empty())
    {
        EngineStdOut("No 'Start Button Clicked' scripts found.", 1);
        return;
    }
    for (const auto &scriptPair : startButtonScripts)
    {
        const string &objectId = scriptPair.first;
        const Script *scriptPtr = scriptPair.second;
        EngineStdOut(" -> Running script for object: " + objectId, 0);
        executeScript(*this, objectId, scriptPtr);
    }
}
void Engine::initFps()
{
    lastfpstime = GetNowCount();
    framecount = 0;
    currentFps = 0.0f;
}
void Engine::updateFps()
{
    framecount++;
    long long currentTime = GetNowCount();
    long elapseTime = currentTime - lastfpstime;

    if (elapseTime >= 500 || framecount >= 60)
    {
        currentFps = (float)framecount / (elapseTime / 1000.0f);
        lastfpstime = currentTime;
        framecount = 0;
    }
}
void Engine::setfps(int fps)
{
    this->specialConfig.TARGET_FPS = fps;
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
    SetDrawScreen(DX_SCREEN_BACK);
    ClearDrawScreen();
    // int fontHandle = Fontloader("font/NanumBarunpenR.ttf"); // <-- 매번 로드하는 코드 제거
    // --- 로딩 바의 위치 및 크기 설정 ---
    int barWidth = 400; // 로딩 바의 전체 폭
    int barHeight = 30; // 로딩 바의 전체 높이
    int barX = (WINDOW_WIDTH - barWidth) / 2;
    int barY = (WINDOW_HEIGHT - barHeight) / 2;

    // --- 로딩 바 배경 그리기 ---
    unsigned int borderColor = GetColor(100, 100, 100); // 테두리 색상 (회색)
    unsigned int bgColor = GetColor(0, 0, 0);           // 배경 안쪽 색상 (검정)

    DrawBox(barX, barY, barX + barWidth, barY + barHeight, borderColor, FALSE);
    DrawBox(barX + 1, barY + 1, barX + barWidth - 1, barY + barHeight - 1, bgColor, TRUE);

    float progressPercent = 0.0f;
    if (totalItemsToLoad > 0)
    {
        progressPercent = static_cast<float>(loadedItemCount) / totalItemsToLoad;
    }

    unsigned int progressColor = GetColor(255, 165, 0); // 진행 바 색상 (주황색)

    int progressWidth = static_cast<int>((barWidth - 2) * progressPercent);

    DrawBox(barX + 1, barY + 1, barX + 1 + progressWidth, barY + barHeight - 1, progressColor, TRUE);
    // --- 퍼센트 텍스트 그리기 ---
    ChangeFontType(DX_FONTTYPE_ANTIALIASING);
    SetFontSize(16); // 폰트 크기 조정
    if (loadingFontHandle != -1)
    { // 로드된 핸들이 유효한 경우에만 폰트 변경
        ChangeFontFromHandle(loadingFontHandle);
    }
    unsigned int textColor = GetColor(255, 255, 255); // 텍스트 색상 (흰색)
    string percentText = to_string(static_cast<int>(progressPercent * 100)) + "%";
    int textWidth = GetDrawStringWidth(percentText.c_str(), percentText.length());
    int textHeight = GetFontSize(); // 현재 폰트 크기 얻기
    DrawString(barX + (barWidth - textWidth) / 2, barY + (barHeight - textHeight) / 2, percentText.c_str(), textColor);

    // --- 브랜드 (원하는 문구 입력) ---
    SetFontSize(20);
    if (loadingFontHandle != -1)
    { // 로드된 핸들이 유효한 경우에만 폰트 변경
        ChangeFontFromHandle(loadingFontHandle);
    }
    unsigned int brandColor = GetColor(0, 0, 0);
    string brandText = this->specialConfig.BRAND_NAME;
    int brandWidth = GetDrawStringWidth(brandText.c_str(), brandText.length());
    int brandHeight = GetFontSize();
    DrawString(barX + (barWidth - brandWidth) / 2, barY + barHeight + -60, brandText.c_str(), brandColor);

    // --- 프로젝트 이름 ---
    SetFontSize(50);
    if (this->specialConfig.SHOW_PROJECT_NAME)
    {
        if (loadingFontHandle != -1)
        { // 로드된 핸들이 유효한 경우에만 폰트 변경
            ChangeFontFromHandle(loadingFontHandle);
        }
        unsigned int projectNameColor = GetColor(0, 0, 0);
        string projectName = PROJECT_NAME;
        int projectNameWidth = GetDrawStringWidth(projectName.c_str(), projectName.length());
        int projectNameHeight = GetFontSize();
        DrawString(barX + (barWidth - projectNameWidth) / 2, barY + barHeight + -250 + projectNameHeight, projectName.c_str(), projectNameColor);
    }

    ScreenFlip();
}

const string &Engine::getCurrentSceneId() const
{
    return currentSceneId;
}

void Engine::showMessageBox(const string &message, int IconType)
{
    MessageBox(NULL, message.c_str(), string(OMOCHA_ENGINE_NAME).c_str(), MB_OK | IconType);
}
/*
 @brief 엔진에 로그를 출력합니다.
 @param s 내용
 @param LEVEL 레벨
 예)
 0 -> 정보
 1 -> 경고
 2 -> 오류
 3 -> 쓰레드
*/
void Engine::EngineStdOut(string s, int LEVEL)
{
    string INFO = "[INFO]";
    string WARN = "[WARN]";
    string ERR = "[ERROR]";
    string THR = "[TREAD]";
    string prefix;
    string color_code;

    switch (LEVEL)
    {
    case 0:
        prefix = INFO;
        color_code = ANSI_COLOR_CYAN;
        break;
    case 1:
        prefix = WARN;
        color_code = ANSI_COLOR_YELLOW;
        break;
    case 2:
        prefix = ERR;
        color_code = ANSI_COLOR_RED;
        break;
    case 3:
        prefix = THR;
        color_code = ANSI_STYLE_BOLD;
    }

    printf("%s%s %s\x1b[0m\n", color_code.c_str(), prefix.c_str(), s.c_str());
    std::string logMessage = std::format("{} {}", prefix, s);
    logger.log(logMessage);
}
