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
#include <memory>

#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>

using namespace std;

const char *BASE_ASSETS = "assets/";
string PROJECT_NAME = "";
string WINDOW_TITLE = "";
int TARGET_FPS = 60;
Engine::Engine() : tempScreenHandle(-1)
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

    destroyTemporaryScreen();
}

bool Engine::loadProject(const string &projectFilePath)
{
    EngineStdOut("Initial Project JSON file parsing...", 0);

    ifstream projectFile(projectFilePath);
    if (!projectFile.is_open())
    {
        showErrorMessageBox("Failed to open project file: " + projectFilePath);
        EngineStdOut(" Failed to open project file: " + projectFilePath, 2);
        return false;
    }

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(projectFile, root))
    {
        showErrorMessageBox("Failed to parse project file: " + reader.getFormattedErrorMessages());
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

    if (root.isMember("objects") && root["objects"].isArray())
    {
        const Json::Value &objectsJson = root["objects"];
        EngineStdOut("Found " + std::to_string(objectsJson.size()) + " objects. Processing...", 0);

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
                    EngineStdOut("Found " + std::to_string(soundsJson.size()) + " sounds. Parsing...", 0);

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
                            if (soundJson.isMember("ext") && soundJson["ext"].isString()) {
                                soundfileExt = soundJson["ext"].asString();
                            }
                            double soundDuration = 0.0;
                            if (soundJson.isMember("duration") && soundJson["duration"].isNumeric()) {
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
                    std::string scriptString = objectJson["script"].asString();

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
                                    } // 블록 순회 끝

                                    // 파싱된 스크립트 목록을 해당 오브젝트와 연결하여 저장
                                    // (예: Engine 클래스의 멤버 변수에 맵 형태로 저장 - objectId를 키로 사용)
                                    // 예: objectScriptsMap[objectId].push_back(currentScript);
                                    // 현재는 파싱만 하고 저장 로직은 생략합니다.
                                    EngineStdOut("Parsed a script list with " + std::to_string(currentScript.blocks.size()) + " blocks for object: " + objInfo.name, 0);
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
        EngineStdOut("Found " + std::to_string(scenesJson.size()) + " scenes. Parsing...", 0);

        // --- JSON 배열 순서대로 순회 ---
        for (Json::Value::ArrayIndex i = 0; i < scenesJson.size(); ++i)
        {
            const auto &sceneJson = scenesJson[i];

            if (sceneJson.isObject() && sceneJson.isMember("id") && sceneJson["id"].isString() && sceneJson.isMember("name") && sceneJson["name"].isString())
            {
                std::string sceneId = sceneJson["id"].asString();
                std::string sceneName = sceneJson["name"].asString();

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
    std::string startSceneId = "";
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

    EngineStdOut("Project JSON file parsed successfully.", 0);
    return true;
}

bool Engine::initGE()
{
    EngineStdOut("DxLib initialize", 0);
    if (DxLib_Init() == -1)
    {
        showErrorMessageBox("Failed to initialize DxLib.");
        EngineStdOut("DxLib initialization failed: DxLib_Init() returned -1.", 2);
        return false;
    }
    initFps();
    SetUseCharSet(DX_CHARCODEFORMAT_UTF8);
    SetBackgroundColor(255, 255, 255);
    if (ChangeWindowMode(TRUE) == -1)
    {
        showErrorMessageBox("Failed to set window mode.");
        EngineStdOut("DxLib initialization failed: Failed to set window mode.", 2);
        return false;
    }
    string projectName = PROJECT_NAME;
    SetWindowTextDX(projectName.c_str());

    if (SetGraphMode(WINDOW_WIDTH, WINDOW_HEIGHT, 32, TARGET_FPS) == -1)
    {
        showErrorMessageBox("Failed to set graphics mode to window size.");
        EngineStdOut("DxLib initialization failed: Failed to set graphics mode.", 2);
        return false;
    }

    if (!createTemporaryScreen())
    {
        showErrorMessageBox("Failed to create temporary screen buffer.");
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
void Engine::Fontloader(string fontpath)
{
    // 폰트로더
    int fontHandle = LoadFontDataToHandle(fontpath.c_str());
    if (fontHandle == -1)
    {
        EngineStdOut("Failed to load font: " + fontpath, 2);
    }
}
/**
 * @brief 소리를 로드합니다.
 *
 * @param soundName
 */
void Engine::Soundloader(string soundUri)
{
    // 소리로더
    int soundHandle = LoadSoundMem(soundUri.c_str());
    if (soundHandle == -1)
    {
        EngineStdOut("Failed to load sound: " + soundUri, 2);
    }
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

    int loadedCount = 0;
    int failedCount = 0;
    for (auto &pair : objects_in_order)
    {
        ObjectInfo &objInfo = pair;
        setTotalItemsToLoad(objInfo.costumes.size());
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
            }
        }
    }

    EngineStdOut("Image loading finished. Success: " + to_string(loadedCount) + ", Failed: " + to_string(failedCount), 0);

    if (failedCount > 0)
    {
        EngineStdOut("WARN: Some image failed to load, processing with available resources.", 1);
        return false;
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
                    static_cast<double>(scaleX), static_cast<double>(scaleY),
                    dxlibAngleRadians,
                    selectedCostume->imageHandle,
                    TRUE);
            }
        }
        else if (objInfo.objectType == "textBox")
        {
            std::string textToDraw = objInfo.textContent;
            unsigned int textColor = objInfo.textColor;
            int fontSize = objInfo.fontSize;

            if (!textToDraw.empty())
            {
                double entryX = entityPtr->getX();
                double entryY = entityPtr->getY();

                float dxlibX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                float dxlibY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                SetFontSize(fontSize);

                DrawStringF(
                    dxlibX, dxlibY,
                    textToDraw.c_str(),
                    textColor);

                // TODO: 텍스트 정렬 (objInfo.textAlign) 로직 구현 필요
            }
        }
    }

    // EngineStdOut("DrawExtendGraph - Target: (0, 0) to (" + std::to_string(WINDOW_WIDTH) + ", " + std::to_string(WINDOW_HEIGHT) + "), Source Handle: " + std::to_string(tempScreenHandle), 0);
    SetDrawMode(DX_DRAWMODE_BILINEAR);
    SetDrawScreen(DX_SCREEN_BACK);
    ClearDrawScreen();
    DrawExtendGraph(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, tempScreenHandle, TRUE);
}
void Engine::processInput() {}
void Engine::triggerRunbtnScript()
{
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
    TARGET_FPS = fps;
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

    // SetFontSize(20); // 적절한 폰트 크기 설정
    // unsigned int textColor = GetColor(255, 255, 255); // 텍스트 색상 (흰색)
    // std::string percentText = std::to_string(static_cast<int>(progressPercent * 100)) + "%";
    // DrawString(barX + barWidth / 2 - GetDrawStringWidth(percentText.c_str(), percentText.length()) / 2,
    //            barY + barHeight / 2 - 10, // Y 위치 조정 (폰트 크기에 따라)
    //            percentText.c_str(), textColor);

    ScreenFlip();
}

const string &Engine::getCurrentSceneId() const
{
    return currentSceneId;
}

void Engine::showErrorMessageBox(const string &message)
{
    MessageBox(NULL, message.c_str(), string(OMOCHA_ENGINE_NAME).c_str(), MB_OK | MB_ICONERROR);
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
}
