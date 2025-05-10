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
#include <algorithm>
#include <memory>
#include <format>
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "blocks/BlockExecutor.h"
using namespace std;

const float Engine::MIN_ZOOM = 1.0f;
const float Engine::MAX_ZOOM = 3.0f;

const char *BASE_ASSETS = "assets/";
const char *FONT_ASSETS = "font/";
string PROJECT_NAME;
string WINDOW_TITLE;
const double PI_VALUE = acos(-1.0);
static string RapidJsonValueToString(const rapidjson::Value &value)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

Engine::Engine() : window(nullptr), renderer(nullptr), tempScreenTexture(nullptr), totalItemsToLoad(0), loadedItemCount(0), zoomFactor(this->specialConfig.setZoomfactor), m_isDraggingZoomSlider(false), m_pressedObjectId(""), logger("omocha_engine.log")
{

    EngineStdOut(string(OMOCHA_ENGINE_NAME) + " v" + string(OMOCHA_ENGINE_VERSION) + " " + string(OMOCHA_DEVELOPER_NAME), 4);
    EngineStdOut("See Project page " + string(OMOCHA_ENGINE_GITHUB), 4);
}

Engine::~Engine()
{
    terminateGE();

    for (auto const &pair : entities)
    {
        delete pair.second;
    }
    entities.clear();

    objects_in_order.clear();
    objectScripts.clear();
}

string Engine::getSafeStringFromJson(const rapidjson::Value &parentValue,
                                     const string &fieldName,
                                     const string &contextDescription,
                                     const string &defaultValue,
                                     bool isCritical,
                                     bool allowEmpty)
{
    if (!parentValue.IsObject())
    {
        EngineStdOut("Parent for field '" + fieldName + "' in " + contextDescription + " is not an object. Value: " + RapidJsonValueToString(parentValue), 2);
        return defaultValue;
    }

    if (!parentValue.HasMember(fieldName.c_str()))
    {
        if (isCritical)
        {
            EngineStdOut("Critical field '" + fieldName + "' missing in " + contextDescription + ". Using default: '" + defaultValue + "'.", 2);
        }

        return defaultValue;
    }

    const rapidjson::Value &fieldValue = parentValue[fieldName.c_str()];

    if (!fieldValue.IsString())
    {
        if (isCritical || !fieldValue.IsNull())
        {
            EngineStdOut("Field '" + fieldName + "' in " + contextDescription + " is not a string. Value: [" + RapidJsonValueToString(fieldValue) +
                             "]. Using default: '" + defaultValue + "'.",
                         1);
        }
        return defaultValue;
    }

    string s_val = fieldValue.GetString();
    if (s_val.empty() && !allowEmpty)
    {
        if (isCritical)
        {
            EngineStdOut("Critical field '" + fieldName + "' in " + contextDescription + " is an empty string, but empty is not allowed. Using default: '" + defaultValue + "'.", 2);
        }
        else
        {
            EngineStdOut("Field '" + fieldName + "' in " + contextDescription + " is an empty string, but empty is not allowed. Using default: '" + defaultValue + "'.", 1);
        }
        return defaultValue;
    }

    return s_val;
}
/**
 * @brief 프로젝트 로드
 *
 * @param projectFilePath 프로젝트 경로
 * @return true 성공하면
 * @return false 실패하면
 */
bool Engine::loadProject(const string &projectFilePath)
{
    m_blockParamsAllocatorDoc = rapidjson::Document();

    EngineStdOut("Initial Project JSON file parsing...", 0);

    ifstream projectFile(projectFilePath);
    if (!projectFile.is_open())
    {
        showMessageBox("Failed to open project file: " + projectFilePath, msgBoxIconType.ICON_ERROR);
        EngineStdOut(" Failed to open project file: " + projectFilePath, 2);
        return false;
    }

    rapidjson::IStreamWrapper isw(projectFile);
    rapidjson::Document document;
    document.ParseStream(isw);
    projectFile.close();

    if (document.HasParseError())
    {
        string errorMsg = string("Failed to parse project file: ") + rapidjson::GetParseError_En(document.GetParseError()) +
                          " (Offset: " + to_string(document.GetErrorOffset()) + ")";
        EngineStdOut(errorMsg, 2);
        showMessageBox("Failed to parse project file", msgBoxIconType.ICON_ERROR);
        return false;
    }

    objects_in_order.clear();
    entities.clear();
    objectScripts.clear();
    m_mouseClickedScripts.clear();
    m_mouseClickCanceledScripts.clear();
    m_whenObjectClickedScripts.clear();
    m_whenObjectClickCanceledScripts.clear();
    m_messageReceivedScripts.clear();
    m_pressedObjectId = "";
    m_sceneOrder.clear();
    PROJECT_NAME = getSafeStringFromJson(document, "name", "project root", "Omocha Project", false, false);
    WINDOW_TITLE = PROJECT_NAME.empty() ? "Omocha Engine" : PROJECT_NAME;
    EngineStdOut("Project Name: " + (PROJECT_NAME.empty() ? "[Not Set]" : PROJECT_NAME), 0);

    if (document.HasMember("speed") && document["speed"].IsNumber())
    {
        this->specialConfig.TARGET_FPS = document["speed"].GetInt();
        EngineStdOut("Target FPS set from project.json: " + to_string(this->specialConfig.TARGET_FPS), 0);
    }
    else
    {
        EngineStdOut("'speed' field missing or not numeric in project.json. Using default TARGET_FPS: " + to_string(this->specialConfig.TARGET_FPS), 1);
    }
    /**
     * @brief 특수 설정
     * 브랜드 이름 (로딩중)
     * 프로젝트 이름 표시 (로딩중)
     */
    if (document.HasMember("specialConfig"))
    {
        const rapidjson::Value &specialConfigJson = document["specialConfig"];
        if (specialConfigJson.IsObject())
        {
            this->specialConfig.BRAND_NAME = getSafeStringFromJson(specialConfigJson, "brandName", "specialConfig", "", false, true);
            EngineStdOut("Brand Name: " + (this->specialConfig.BRAND_NAME.empty() ? "[Not Set]" : this->specialConfig.BRAND_NAME), 0);

            if (specialConfigJson.HasMember("showZoomSliderUI") && specialConfigJson["showZoomSliderUI"].IsBool())
            {
                this->specialConfig.showZoomSlider = specialConfigJson["showZoomSliderUI"].GetBool();
            }
            else
            {
                this->specialConfig.showZoomSlider = false;
                EngineStdOut("'specialConfig.showZoomSliderUI' field missing or not boolean. Using default: false", 1);
            }
            if (specialConfigJson.HasMember("setZoomfactor") && specialConfigJson["setZoomfactor"].IsNumber())
            {
                this->specialConfig.setZoomfactor = std::clamp(specialConfigJson["setZoomfactor"].GetDouble(), (double)Engine::MIN_ZOOM, (double)Engine::MAX_ZOOM);
            }
            else
            {
                this->specialConfig.setZoomfactor = 1.26f;
                EngineStdOut("'specialConfig.setZoomfactor' field missing or not numeric. Using default: 1.26", 1);
            }
            if (specialConfigJson.HasMember("showProjectNameUI") && specialConfigJson["showProjectNameUI"].IsBool())
            {
                this->specialConfig.SHOW_PROJECT_NAME = specialConfigJson["showProjectNameUI"].GetBool();
            }
            else
            {
                this->specialConfig.SHOW_PROJECT_NAME = false;
                EngineStdOut("'specialConfig.showProjectNameUI' field missing or not boolean. Using default: false", 1);
            }
            if (specialConfigJson.HasMember("showFPS") && specialConfigJson["showFPS"].IsBool())
            {
                this->specialConfig.showFPS = specialConfigJson["showFPS"].GetBool();
            }
            else
            {
                this->specialConfig.showFPS = false;
                EngineStdOut("'specialConfig.showFPS' field missing or not boolean. Using default: false", 1);
            }
        }
        this->zoomFactor = this->specialConfig.setZoomfactor;
    }

    /**
     * @brief 오브젝트 블럭
     *
     */
    if (document.HasMember("objects") && document["objects"].IsArray())
    {
        const rapidjson::Value &objectsJson = document["objects"];
        EngineStdOut("Found " + to_string(objectsJson.Size()) + " objects. Processing...", 0);

        for (rapidjson::SizeType i = 0; i < objectsJson.Size(); ++i)
        {
            const auto &objectJson = objectsJson[i];
            if (!objectJson.IsObject())
            {
                EngineStdOut("Object entry at index " + to_string(i) + " is not an object. Skipping. Content: " + RapidJsonValueToString(objectJson), 1);
                continue;
            }

            string objectId = getSafeStringFromJson(objectJson, "id", "object entry " + to_string(i), "", true, false);
            if (objectId.empty())
            {
                EngineStdOut("Object ID is empty for object at index " + to_string(i) + ". Skipping object.", 2);
                continue;
            }

            ObjectInfo objInfo;
            objInfo.id = objectId;
            objInfo.name = getSafeStringFromJson(objectJson, "name", "object id: " + objectId, "Unnamed Object", false, true);
            objInfo.objectType = getSafeStringFromJson(objectJson, "objectType", "object id: " + objectId, "sprite", false, false);
            objInfo.sceneId = getSafeStringFromJson(objectJson, "scene", "object id: " + objectId, "", false, true);

            if (objectJson.HasMember("sprite") && objectJson["sprite"].IsObject() &&
                objectJson["sprite"].HasMember("pictures") && objectJson["sprite"]["pictures"].IsArray())
            {
                const rapidjson::Value &picturesJson = objectJson["sprite"]["pictures"];
                EngineStdOut("Found " + to_string(picturesJson.Size()) + " pictures for object " + objInfo.name, 0);
                for (rapidjson::SizeType j = 0; j < picturesJson.Size(); ++j)
                {
                    const auto &pictureJson = picturesJson[j];
                    if (pictureJson.IsObject() && pictureJson.HasMember("id") && pictureJson["id"].IsString() &&
                        pictureJson.HasMember("filename") && pictureJson["filename"].IsString())
                    {
                        Costume ctu;
                        ctu.id = getSafeStringFromJson(pictureJson, "id", "costume entry " + to_string(j) + " for " + objInfo.name, "", true, false);
                        if (ctu.id.empty())
                        {
                            EngineStdOut("Costume ID is empty for object " + objInfo.name + " at picture index " + to_string(j) + ". Skipping costume.", 2);
                            continue;
                        }
                        ctu.name = getSafeStringFromJson(pictureJson, "name", "costume id: " + ctu.id, "Unnamed Shape", false, true);
                        ctu.filename = getSafeStringFromJson(pictureJson, "filename", "costume id: " + ctu.id, "", true, false);
                        if (ctu.filename.empty())
                        {
                            EngineStdOut("Costume filename is empty for " + ctu.name + " (ID: " + ctu.id + "). Skipping costume.", 2);
                            continue;
                        }
                        ctu.fileurl = getSafeStringFromJson(pictureJson, "fileurl", "costume id: " + ctu.id, "", false, true);

                        objInfo.costumes.push_back(ctu);
                        EngineStdOut("  Parsed costume: " + ctu.name + " (ID: " + ctu.id + ", File: " + ctu.filename + ")", 0);
                    }
                    else
                    {
                        EngineStdOut("Invalid picture structure for object '" + objInfo.name + "' at index " + to_string(j) + ". Skipping.", 1);
                    }
                }
            }
            else
            {
                EngineStdOut("Object '" + objInfo.name + "' has no 'sprite/pictures' array or it's invalid.", 1);
            }

            if (objectJson.HasMember("sprite") && objectJson["sprite"].IsObject() &&
                objectJson["sprite"].HasMember("sounds") && objectJson["sprite"]["sounds"].IsArray())
            {
                const rapidjson::Value &soundsJson = objectJson["sprite"]["sounds"];
                EngineStdOut("Found " + to_string(soundsJson.Size()) + " sounds for object " + objInfo.name + ". Parsing...", 0);

                for (rapidjson::SizeType j = 0; j < soundsJson.Size(); ++j)
                {
                    const auto &soundJson = soundsJson[j];
                    if (soundJson.IsObject() && soundJson.HasMember("id") && soundJson["id"].IsString() && soundJson.HasMember("filename") && soundJson["filename"].IsString())
                    {
                        SoundFile sound;
                        string soundId = getSafeStringFromJson(soundJson, "id", "sound entry " + to_string(j) + " for " + objInfo.name, "", true, false);
                        sound.id = soundId;
                        if (sound.id.empty())
                        {
                            EngineStdOut("Sound ID is empty for object " + objInfo.name + " at sound index " + to_string(j) + ". Skipping sound.", 2);
                            continue;
                        }
                        sound.name = getSafeStringFromJson(soundJson, "name", "sound id: " + sound.id, "Unnamed Sound", false, true);
                        sound.filename = getSafeStringFromJson(soundJson, "filename", "sound id: " + sound.id, "", true, false);
                        if (sound.filename.empty())
                        {
                            EngineStdOut("Sound filename is empty for " + sound.name + " (ID: " + sound.id + "). Skipping sound.", 2);
                            continue;
                        }
                        sound.fileurl = getSafeStringFromJson(soundJson, "fileurl", "sound id: " + sound.id, "", false, true);
                        sound.ext = getSafeStringFromJson(soundJson, "ext", "sound id: " + sound.id, "", false, true);

                        double soundDuration = 0.0;
                        if (soundJson.HasMember("duration") && soundJson["duration"].IsNumber())
                        {
                            soundDuration = soundJson["duration"].GetDouble();
                        }
                        else
                        {
                            EngineStdOut("Sound '" + sound.name + "' (ID: " + sound.id + ") is missing 'duration' or it's not numeric. Using default duration 0.0.", 1);
                        }
                        sound.duration = soundDuration;
                        objInfo.sounds.push_back(sound);
                        EngineStdOut("  Parsed sound: " + sound.name + " (ID: " + sound.id + ", File: " + sound.filename + ")", 0);
                    }
                    else
                    {
                        EngineStdOut("Invalid sound structure for object '" + objInfo.name + "' at index " + to_string(j) + ". Skipping.", 1);
                    }
                }
            }
            else
            {
                EngineStdOut("Object '" + objInfo.name + "' has no 'sprite/sounds' array or it's invalid.", 1);
            }

            string tempSelectedCostumeId;
            bool selectedCostumeFound = false;
            if (objectJson.HasMember("selectedPictureId") && objectJson["selectedPictureId"].IsString())
            {
                tempSelectedCostumeId = getSafeStringFromJson(objectJson, "selectedPictureId", "object " + objInfo.name, "", false, false);
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (!selectedCostumeFound && objectJson.HasMember("selectedCostume") && objectJson["selectedCostume"].IsString())
            {
                tempSelectedCostumeId = getSafeStringFromJson(objectJson, "selectedCostume", "object " + objInfo.name, "", false, false);
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (!selectedCostumeFound && objectJson.HasMember("selectedCostume") && objectJson["selectedCostume"].IsObject() &&
                objectJson["selectedCostume"].HasMember("id") && objectJson["selectedCostume"]["id"].IsString())
            {
                tempSelectedCostumeId = getSafeStringFromJson(objectJson["selectedCostume"], "id", "object " + objInfo.name + " selectedCostume object", "", false, false);
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (selectedCostumeFound)
            {
                objInfo.selectedCostumeId = tempSelectedCostumeId;
                EngineStdOut("Object '" + objInfo.name + "' (ID: " + objInfo.id + ") selected costume ID: " + objInfo.selectedCostumeId, 0);
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
                    objInfo.selectedCostumeId = "";
                }
            }

            if (objInfo.objectType == "textBox")
            {
                if (objectJson.HasMember("entity") && objectJson["entity"].IsObject())
                {
                    const rapidjson::Value &entityJson = objectJson["entity"];

                    if (entityJson.HasMember("text"))
                    {
                        if (entityJson["text"].IsString())
                        {
                            objInfo.textContent = getSafeStringFromJson(entityJson, "text", "textBox " + objInfo.name, "[DEFAULT TEXT]", false, true);
                        }
                        else if (entityJson["text"].IsNumber())
                        {

                            objInfo.textContent = to_string(entityJson["text"].GetDouble());
                            EngineStdOut("INFO: textBox '" + objInfo.name + "' 'text' field is numeric. Converted to string: " + objInfo.textContent, 0);
                        }
                        else
                        {
                            objInfo.textContent = "[INVALID TEXT TYPE]";
                            EngineStdOut("textBox '" + objInfo.name + "' 'text' field has invalid type: " + RapidJsonValueToString(entityJson["text"]), 1);
                        }
                    }
                    else
                    {
                        objInfo.textContent = "[NO TEXT FIELD]";
                        EngineStdOut("textBox '" + objInfo.name + "' is missing 'text' field.", 1);
                    }

                    if (entityJson.HasMember("colour") && entityJson["colour"].IsString())
                    {
                        string hexColor = getSafeStringFromJson(entityJson, "colour", "textBox " + objInfo.name, "#000000", false, false);
                        if (hexColor.length() == 7 && hexColor[0] == '#')
                        {
                            try
                            {

                                unsigned int r = stoul(hexColor.substr(1, 2), nullptr, 16);
                                unsigned int g = stoul(hexColor.substr(3, 2), nullptr, 16);
                                unsigned int b = stoul(hexColor.substr(5, 2), nullptr, 16);
                                objInfo.textColor = {(Uint8)r, (Uint8)g, (Uint8)b, 255};
                                EngineStdOut("INFO: textBox '" + objInfo.name + "' text color parsed: R=" + to_string(r) + ", G=" + to_string(g) + ", B=" + to_string(b), 0);
                            }
                            catch (const exception &e)
                            {
                                EngineStdOut("Failed to parse text color '" + hexColor + "' for object '" + objInfo.name + "': " + e.what() + ". Using default #000000.", 2);
                                objInfo.textColor = {0, 0, 0, 255};
                            }
                        }
                        else
                        {
                            EngineStdOut("textBox '" + objInfo.name + "' 'colour' field is not a valid HEX string (#RRGGBB): " + hexColor + ". Using default #000000.", 1);
                            objInfo.textColor = {0, 0, 0, 255};
                        }
                    }
                    else
                    {
                        EngineStdOut("textBox '" + objInfo.name + "' is missing 'colour' field or it's not a string. Using default #000000.", 1);
                        objInfo.textColor = {0, 0, 0, 255};
                    }

                    if (entityJson.HasMember("font") && entityJson["font"].IsString())
                    {
                        string fontString = getSafeStringFromJson(entityJson, "font", "textBox " + objInfo.name, "20px NanumBarunpen", false, true);
                        size_t pxPos = fontString.find("px");
                        if (pxPos != string::npos)
                        {
                            try
                            {
                                objInfo.fontSize = stoi(fontString.substr(0, pxPos));
                                EngineStdOut("INFO: textBox '" + objInfo.name + "' font size parsed: " + to_string(objInfo.fontSize), 0);
                            }
                            catch (...)
                            {
                                objInfo.fontSize = 20;
                                EngineStdOut("Failed to parse font size from '" + fontString + "' for textBox '" + objInfo.name + "'. Using default size 20.", 1);
                            }
                            size_t spaceAfterPx = fontString.find(' ', pxPos + 2);
                            if (spaceAfterPx != string::npos)
                            {
                                objInfo.fontName = fontString.substr(spaceAfterPx + 1);
                            }
                            else
                            {

                                objInfo.fontName = fontString.substr(pxPos + 2);

                                objInfo.fontName.erase(0, objInfo.fontName.find_first_not_of(" "));
                                objInfo.fontName.erase(objInfo.fontName.find_last_not_of(" ") + 1);
                            }
                            EngineStdOut("INFO: textBox '" + objInfo.name + "' font name parsed: '" + objInfo.fontName + "'", 0);
                        }
                        else
                        {
                            objInfo.fontSize = 20;
                            objInfo.fontName = fontString;
                            EngineStdOut("textBox '" + objInfo.name + "' 'font' field is not in 'size px Name' format: '" + fontString + "'. Using default size 20 and '" + objInfo.fontName + "' as name.", 1);
                        }
                    }
                    else
                    {
                        EngineStdOut("textBox '" + objInfo.name + "' is missing 'font' field or it's not a string. Using default size 20 and empty font name.", 1);
                        objInfo.fontSize = 20;
                        objInfo.fontName = "";
                    }

                    if (entityJson.HasMember("textAlign") && entityJson["textAlign"].IsNumber())
                    {
                        int parsedAlign = entityJson["textAlign"].GetInt();
                        if (parsedAlign >= 0 && parsedAlign <= 2)
                        { // 0: left, 1: center, 2: right
                            objInfo.textAlign = parsedAlign;
                        }
                        else
                        {
                            objInfo.textAlign = 0; // Default to left align for invalid values
                            EngineStdOut("textBox '" + objInfo.name + "' 'textAlign' field has an invalid value: " + std::to_string(parsedAlign) + ". Using default alignment 0 (left).", 1);
                        }
                        EngineStdOut("INFO: textBox '" + objInfo.name + "' text alignment parsed: " + to_string(objInfo.textAlign), 0);
                    }
                    else
                    {
                        objInfo.textAlign = 0;
                        EngineStdOut("textBox '" + objInfo.name + "' is missing 'textAlign' field or it's not numeric. Using default alignment 0.", 1);
                    }
                }
                else
                {
                    EngineStdOut("textBox '" + objInfo.name + "' is missing 'entity' block or it's not an object. Cannot load text box properties.", 1);
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

            if (objectJson.HasMember("entity") && objectJson["entity"].IsObject())
            {
                const rapidjson::Value &entityJson = objectJson["entity"];

                double initial_x = entityJson.HasMember("x") && entityJson["x"].IsNumber() ? entityJson["x"].GetDouble() : 0.0;
                double initial_y = entityJson.HasMember("y") && entityJson["y"].IsNumber() ? entityJson["y"].GetDouble() : 0.0;
                double initial_regX = entityJson.HasMember("regX") && entityJson["regX"].IsNumber() ? entityJson["regX"].GetDouble() : 0.0;
                double initial_regY = entityJson.HasMember("regY") && entityJson["regY"].IsNumber() ? entityJson["regY"].GetDouble() : 0.0;
                double initial_scaleX = entityJson.HasMember("scaleX") && entityJson["scaleX"].IsNumber() ? entityJson["scaleX"].GetDouble() : 1.0;
                double initial_scaleY = entityJson.HasMember("scaleY") && entityJson["scaleY"].IsNumber() ? entityJson["scaleY"].GetDouble() : 1.0;
                double initial_rotation = entityJson.HasMember("rotation") && entityJson["rotation"].IsNumber() ? entityJson["rotation"].GetDouble() : 0.0;
                double initial_direction = entityJson.HasMember("direction") && entityJson["direction"].IsNumber() ? entityJson["direction"].GetDouble() : 90.0;
                double initial_width = entityJson.HasMember("width") && entityJson["width"].IsNumber() ? entityJson["width"].GetDouble() : 100.0;
                double initial_height = entityJson.HasMember("height") && entityJson["height"].IsNumber() ? entityJson["height"].GetDouble() : 100.0;
                bool initial_visible = entityJson.HasMember("visible") && entityJson["visible"].IsBool() ? entityJson["visible"].GetBool() : true;

                Entity *newEntity = new Entity(
                    objectId,
                    objInfo.name,
                    initial_x, initial_y, initial_regX, initial_regY,
                    initial_scaleX, initial_scaleY, initial_rotation, initial_direction,
                    initial_width, initial_height, initial_visible);

                entities[objectId] = newEntity;
                EngineStdOut("INFO: Created Entity for object ID: " + objectId, 0);
            }
            else
            {
                EngineStdOut("Object '" + objInfo.name + "' (ID: " + objectId + ") is missing 'entity' block or it's not an object. Cannot create Entity.", 1);
            }
            /**
             * @brief 스크립트 블럭
             *
             */
            if (objectJson.HasMember("script") && objectJson["script"].IsString())
            {
                string scriptString = getSafeStringFromJson(objectJson, "script", "object " + objInfo.name, "", false, true);
                if (scriptString.empty())
                {
                    EngineStdOut("INFO: Object '" + objInfo.name + "' has an empty 'script' string. No scripts will be loaded for this object.", 0);
                }
                else
                {
                    rapidjson::Document scriptDocument;
                    scriptDocument.Parse(scriptString.c_str());

                    if (!scriptDocument.HasParseError())
                    {

                        EngineStdOut("Script JSON string parsed successfully for object: " + objInfo.name, 0);

                        if (scriptDocument.IsArray())
                        {
                            vector<Script> scriptsForObject;
                            for (rapidjson::SizeType j = 0; j < scriptDocument.Size(); ++j)
                            {
                                const auto &scriptStackJson = scriptDocument[j];
                                if (scriptStackJson.IsArray())
                                {
                                    Script currentScript;
                                    EngineStdOut("  Parsing script stack " + to_string(j + 1) + "/" + to_string(scriptDocument.Size()) + " for object " + objInfo.name, 0);

                                    for (rapidjson::SizeType k = 0; k < scriptStackJson.Size(); ++k)
                                    {
                                        const auto &blockJson = scriptStackJson[k];
                                        string blockContext = "block at index " + to_string(k) + " in script stack " + to_string(j + 1) + " for object " + objInfo.name;

                                        if (blockJson.IsObject())
                                        {

                                            string blockId = getSafeStringFromJson(blockJson, "id", blockContext, "", true, false);
                                            if (blockId.empty())
                                            {
                                                EngineStdOut("ERROR: Script " + blockContext + " has missing or empty 'id'. Skipping block.", 2);
                                                continue;
                                            }

                                            string blockType = getSafeStringFromJson(blockJson, "type", blockContext + " (id: " + blockId + ")", "", true, false);
                                            if (blockType.empty())
                                            {
                                                EngineStdOut("ERROR: Script " + blockContext + " (id: " + blockId + ") has missing or empty 'type'. Skipping block.", 2);
                                                continue;
                                            }

                                            Block block;
                                            block.id = blockId;
                                            block.type = blockType;

                                            if (blockJson.HasMember("params") && blockJson["params"].IsArray())
                                            {
                                                block.paramsJson.CopyFrom(blockJson["params"], m_blockParamsAllocatorDoc.GetAllocator());
                                            }
                                            else if (blockJson.HasMember("params"))
                                            {
                                                EngineStdOut("WARN: Script " + blockContext + " (id: " + blockId + ", type: " + blockType + ") has 'params' but it's not an array. Params will be empty. Value: " + RapidJsonValueToString(blockJson["params"]), 1);
                                            }

                                            currentScript.blocks.push_back(block);
                                            EngineStdOut("    Parsed block: id='" + block.id + "', type='" + block.type + "'", 0);
                                        }
                                        else
                                        {
                                            EngineStdOut("WARN: Invalid block structure (not an object) in " + blockContext + ". Skipping block. Content: " + RapidJsonValueToString(blockJson), 1);
                                        }
                                    }
                                    if (!currentScript.blocks.empty())
                                    {
                                        scriptsForObject.push_back(currentScript);
                                    }
                                    else
                                    {
                                        EngineStdOut("  WARN: Script stack " + to_string(j + 1) + " for object " + objInfo.name + " resulted in an empty script (e.g., all blocks were invalid). Skipping this stack.", 1);
                                    }
                                }
                                else
                                {
                                    EngineStdOut("WARN: Script entry at index " + to_string(j) + " for object '" + objInfo.name + "' is not an array of blocks (not a valid script stack). Skipping this script stack. Content: " + RapidJsonValueToString(scriptStackJson), 1);
                                }
                            }
                            objectScripts[objectId] = scriptsForObject;
                            EngineStdOut("INFO: Parsed " + to_string(scriptsForObject.size()) + " script stacks for object ID: " + objectId, 0);
                        }
                        else
                        {
                            EngineStdOut("WARN: Script root for object '" + objInfo.name + "' (after parsing string) is not an array of script stacks. Skipping script parsing. Content: " + RapidJsonValueToString(scriptDocument), 1);
                        }
                    }
                    else
                    {
                        string scriptErrorMsg = string("ERROR: Failed to parse script JSON string for object '") + objInfo.name + "': " +
                                                rapidjson::GetParseError_En(scriptDocument.GetParseError()) + " (Offset: " + to_string(scriptDocument.GetErrorOffset()) + ")";
                        EngineStdOut(scriptErrorMsg, 2);
                        showMessageBox("Failed to parse script JSON string for object '" + objInfo.name + "'. Project loading aborted.", msgBoxIconType.ICON_ERROR);
                        return false;
                    }
                }
            }
            else
            {
                EngineStdOut("INFO: Object '" + objInfo.name + "' is missing 'script' field or it's not a string. No scripts will be loaded for this object.", 0);
            }
        }
    }
    else
    {
        EngineStdOut("project.json is missing 'objects' array or it's not an array.", 1);
        showMessageBox("project.json is missing 'objects' array or it's not an array.\nBrokenProject.", msgBoxIconType.ICON_ERROR);
        return false;
    }

    scenes.clear();

    /**
     * @brief 엔트리 씬
     *
     */
    if (document.HasMember("scenes") && document["scenes"].IsArray())
    {
        const rapidjson::Value &scenesJson = document["scenes"];
        EngineStdOut("Found " + to_string(scenesJson.Size()) + " scenes. Parsing...", 0);
        for (rapidjson::SizeType i = 0; i < scenesJson.Size(); ++i)
        {
            const auto &sceneJson = scenesJson[i];

            if (!sceneJson.IsObject())
            {
                EngineStdOut("Scene entry at index " + to_string(i) + " is not an object. Skipping. Content: " + RapidJsonValueToString(sceneJson), 1);
                continue;
            }

            if (sceneJson.HasMember("id") && sceneJson["id"].IsString() && sceneJson.HasMember("name") && sceneJson["name"].IsString())
            {
                string sceneId = getSafeStringFromJson(sceneJson, "id", "scene entry " + to_string(i), "", true, false);
                if (sceneId.empty())
                {
                    EngineStdOut("Scene ID is empty for scene at index " + to_string(i) + ". Skipping scene.", 2);
                    continue;
                }
                string sceneName = getSafeStringFromJson(sceneJson, "name", "scene id: " + sceneId, "Unnamed Scene", false, true);
                scenes[sceneId] = sceneName;
                m_sceneOrder.push_back(sceneId);
                EngineStdOut("  Parsed scene: " + sceneName + " (ID: " + sceneId + ")", 0);
            }
            else
            {
                EngineStdOut("Invalid scene structure or 'id'/'name' fields missing/not strings for scene at index " + to_string(i) + ". Skipping.", 1);
                EngineStdOut("  Scene content: " + RapidJsonValueToString(sceneJson), 1);
            }
        }
    }
    else
    {
        EngineStdOut("project.json is missing 'scenes' array or it's not an array. No scenes loaded.", 1);
    }

    string startSceneId = "";

    if (document.HasMember("startScene") && document["startScene"].IsString())
    {
        startSceneId = getSafeStringFromJson(document, "startScene", "project root for startScene (legacy)", "", false, false);
        EngineStdOut("'startScene' (legacy) found in project.json: " + startSceneId, 0);
    }

    else if (document.HasMember("start") && document["start"].IsObject() && document["start"].HasMember("sceneId") && document["start"]["sceneId"].IsString())
    {
        startSceneId = getSafeStringFromJson(document["start"], "sceneId", "project root start object", "", false, false);
        EngineStdOut("'start/sceneId' found in project.json: " + startSceneId, 0);
    }
    else
    {
        EngineStdOut("No explicit 'startScene' or 'start/sceneId' found in project.json.", 1);
    }

    if (!startSceneId.empty() && scenes.count(startSceneId))
    {
        currentSceneId = startSceneId;
        EngineStdOut("Initial scene set to explicit start scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
    }
    else
    {

        if (!m_sceneOrder.empty() && scenes.count(m_sceneOrder.front()))
        {
            currentSceneId = m_sceneOrder.front();
            EngineStdOut("Initial scene set to first scene in array order: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        }
        else
        {
            EngineStdOut("No valid starting scene found in project.json or no scenes were loaded.", 2);
            currentSceneId = "";
            return false;
        }
    }

    if (!currentSceneId.empty())
    {
        EngineStdOut("Triggering 'when_scene_start' scripts for initial scene: " + currentSceneId, 0);
        triggerWhenSceneStartScripts();
    }
    else
    {
    }

    /**
     * @brief 이벤트 블럭들
     *
     */
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

                    if (script.blocks.size() > 1)
                    {

                        startButtonScripts.push_back({objectId, &script});
                        EngineStdOut("  -> Found valid 'Start Button Clicked' script for object ID: " + objectId, 0);
                    }
                    else
                    {
                        EngineStdOut("  -> Found 'Start Button Clicked' script for object ID: " + objectId + " but it has no subsequent blocks. Skipping.", 1);
                    }
                }

                else if (firstBlock.type == "when_some_key_pressed")
                {
                    if (script.blocks.size() > 1)
                    {
                        string keyIdentifierString;
                        bool keyIdentifierFound = false;
                        if (firstBlock.paramsJson.IsArray() && firstBlock.paramsJson.Size() > 0)
                        {
                            if (firstBlock.paramsJson[0].IsString())
                            {
                                keyIdentifierString = firstBlock.paramsJson[0].GetString();
                                keyIdentifierFound = true;
                            }
                            else if (firstBlock.paramsJson[0].IsNull() && firstBlock.paramsJson.Size() > 1 && firstBlock.paramsJson[1].IsString())
                            {
                                keyIdentifierString = firstBlock.paramsJson[1].GetString();
                                keyIdentifierFound = true;
                            }

                            if (keyIdentifierFound)
                            {

                                SDL_Scancode keyScancode = this->mapStringToSDLScancode(keyIdentifierString);
                                if (keyScancode != SDL_SCANCODE_UNKNOWN)
                                {
                                    keyPressedScripts[keyScancode].push_back({objectId, &script});
                                }
                            }
                            else
                            {
                                rapidjson::StringBuffer buffer;
                                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                                firstBlock.paramsJson.Accept(writer);
                                EngineStdOut(" -> object ID " + objectId + " 'press key' invalid param or missing message ID. Params JSON: " + string(buffer.GetString()) + ".", 1);
                            }
                        }
                    }
                }

                else if (firstBlock.type == "mouse_clicked")
                {
                    if (script.blocks.size() > 1)
                    {
                        m_mouseClickedScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'mouse clicked' script.", 0);
                    }
                }

                else if (firstBlock.type == "mouse_click_cancled")
                {
                    if (script.blocks.size() > 1)
                    {
                        m_mouseClickCanceledScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'mouse click canceled' script.", 0);
                    }
                }

                else if (firstBlock.type == "when_object_click")
                {
                    if (script.blocks.size() > 1)
                    {
                        m_whenObjectClickedScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'click an object' script.", 0);
                    }
                }

                else if (firstBlock.type == "when_object_click_canceled")
                {
                    if (script.blocks.size() > 1)
                    {
                        m_whenObjectClickCanceledScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'When I release the click on an object' script.", 0);
                    }
                }

                else if (firstBlock.type == "when_message_cast")
                {
                    if (script.blocks.size() > 1)
                    {
                        string messageIdToReceive;
                        bool messageParamFound = false;

                        if (firstBlock.paramsJson.IsArray() && firstBlock.paramsJson.Size() > 1 &&
                            firstBlock.paramsJson[1].IsString())
                        {
                            messageIdToReceive = firstBlock.paramsJson[1].GetString();
                            messageParamFound = true;
                        }

                        if (messageParamFound && !messageIdToReceive.empty())
                        {
                            m_messageReceivedScripts[messageIdToReceive].push_back({objectId, &script});
                            EngineStdOut("  -> object ID " + objectId + " " + messageIdToReceive + " message found.", 0);
                        }
                        else
                        {
                            rapidjson::StringBuffer buffer;
                            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                            firstBlock.paramsJson.Accept(writer);
                            EngineStdOut(" -> object ID " + objectId + " 'recive signal' invalid param or missing message ID. Params JSON: " + string(buffer.GetString()) + ".", 1);
                        }
                    }
                }

                else if (firstBlock.type == "when_scene_start")
                {
                    if (script.blocks.size() > 1)
                    {
                        m_whenStartSceneLoadedScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'when scene start' script.", 0);
                    }
                }
            }
        }
    }
    EngineStdOut("Finished identifying 'Start Button Clicked' scripts. Found: " + to_string(startButtonScripts.size()), 0);

    EngineStdOut("Project JSON file parsed successfully.", 0);
    return true;
}
const ObjectInfo* Engine::getObjectInfoById(const std::string& id) const {
    // objects_in_order is a vector, so we need to iterate.
    // This could be optimized if access becomes frequent by creating a map during loadProject.
    for (const auto& objInfo : objects_in_order) {
        if (objInfo.id == id) {
            return &objInfo;
        }
    }
    return nullptr;
}
bool Engine::initGE(bool vsyncEnabled, bool attemptVulkan)
{
    EngineStdOut("Initializing SDL...", 0);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        string errMsg = "SDL could not initialize! SDL_" + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to initialize SDL: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    EngineStdOut("SDL initialized successfully (Video and Audio).", 0);

    if (TTF_Init() == -1)
    {
        string errMsg = "SDL_ttf could not initialize!";
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to initialize", msgBoxIconType.ICON_ERROR);

        SDL_Quit();
        return false;
    }
    EngineStdOut("SDL_ttf initialized successfully.", 0);

    this->window = SDL_CreateWindow(WINDOW_TITLE.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (this->window == nullptr)
    {
        string errMsg = "Window could not be created! SDL_" + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create window: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        TTF_Quit();

        SDL_Quit();
        return false;
    }
    EngineStdOut("SDL Window created successfully.", 0);
    if (attemptVulkan)
    {
        EngineStdOut("Attempting to create Vulkan renderer as requested by command line argument...", 0);

        int numRenderDrivers = SDL_GetNumRenderDrivers();
        if (numRenderDrivers < 0)
        {
            EngineStdOut("Failed to get number of render drivers: " + string(SDL_GetError()), 2);
            SDL_DestroyWindow(this->window);
            this->window = nullptr;
            TTF_Quit();
            SDL_Quit();
            return false;
        }
        EngineStdOut("Available render drivers: " + to_string(numRenderDrivers), 0);

        int vulkanDriverIndex = -1;
        for (int i = 0; i < numRenderDrivers; ++i)
        {
            EngineStdOut("Checking render driver at index: " + to_string(i) + " " + SDL_GetRenderDriver(i), 0);
            const char *driverName = SDL_GetRenderDriver(i);

            if (driverName != nullptr && strcmp(driverName, "vulkan") == 0)
            {
                vulkanDriverIndex = i;
                EngineStdOut("Vulkan driver found at index: " + to_string(i), 0);
                break;
            }
        }

        if (vulkanDriverIndex != -1)
        {

            this->renderer = SDL_CreateRenderer(this->window, "vulkan");
            if (this->renderer)
            {
                EngineStdOut("Successfully created Vulkan renderer.", 0);
                string projectName = PROJECT_NAME;
                string windowTitleWithRenderer = projectName + " (Vulkan)";
                SDL_SetWindowTitle(window, windowTitleWithRenderer.c_str());
            }
            else
            {
                EngineStdOut("Failed to create Vulkan renderer even though driver was found: " + string(SDL_GetError()) + ". Falling back to default.", 1);
                this->renderer = SDL_CreateRenderer(this->window, nullptr);
            }
        }
        else
        {
            EngineStdOut("Vulkan render driver not found. Using default renderer.", 1);
            showMessageBox("Vulkan render driver not found. Using default renderer.", msgBoxIconType.ICON_WARNING);
            this->renderer = SDL_CreateRenderer(this->window, nullptr);
        }
    }
    else
    {

        this->renderer = SDL_CreateRenderer(this->window, nullptr);
    }
    if (this->renderer == nullptr)
    {
        string errMsg = "Renderer could not be created! SDL_" + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create renderer: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    EngineStdOut("SDL Renderer created successfully.", 0);
    if (SDL_SetRenderVSync(this->renderer, vsyncEnabled ? SDL_RENDERER_VSYNC_ADAPTIVE : SDL_RENDERER_VSYNC_DISABLED) != 0)
    {
        EngineStdOut("Failed to set VSync mode. SDL_" + string(SDL_GetError()), 1);
    }
    else
    {
        EngineStdOut("VSync mode set to: " + string(vsyncEnabled ? "Adaptive" : "Disabled"), 0);
    }

    string defaultFontPath = "font/nanum_barun_bold.ttf";
    hudFont = TTF_OpenFont(defaultFontPath.c_str(), 20);
    loadingScreenFont = TTF_OpenFont(defaultFontPath.c_str(), 30);

    if (!hudFont)
    {
        string errMsg = "Failed to load HUD font! Font path: " + defaultFontPath;
        EngineStdOut(errMsg, 2);

        showMessageBox(errMsg, msgBoxIconType.ICON_ERROR);

        if (loadingScreenFont)
            TTF_CloseFont(loadingScreenFont);
        TTF_Quit();
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        SDL_DestroyWindow(this->window);
        this->window = nullptr;

        SDL_Quit();
        return false;
    }
    EngineStdOut("HUD font loaded successfully.", 0);

    if (!loadingScreenFont)
    {
        string errMsg = "Failed to load loading screen font! Font path: " + defaultFontPath;
        EngineStdOut(errMsg, 2);

        showMessageBox(errMsg, msgBoxIconType.ICON_ERROR);
        if (hudFont)
            TTF_CloseFont(hudFont);
        TTF_Quit();
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        SDL_DestroyWindow(this->window);
        this->window = nullptr;

        SDL_Quit();
        return false;
    }
    EngineStdOut("Loading screen font loaded successfully.", 0);

    SDL_SetRenderDrawColor(this->renderer, 255, 255, 255, 255);

    initFps();

    if (!createTemporaryScreen())
    {
        EngineStdOut("Failed to create temporary screen texture during initGE.", 2);

        if (hudFont)
            TTF_CloseFont(hudFont);
        if (loadingScreenFont)
            TTF_CloseFont(loadingScreenFont);
        TTF_Quit();
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        SDL_DestroyWindow(this->window);
        this->window = nullptr;

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
        showMessageBox("Internal Renderer not available for offscreen buffer.", msgBoxIconType.ICON_ERROR);
        return false;
    }

    this->tempScreenTexture = SDL_CreateTexture(this->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, PROJECT_STAGE_WIDTH, PROJECT_STAGE_HEIGHT);
    if (this->tempScreenTexture == nullptr)
    {
        string errMsg = "Failed to create temporary screen texture! SDL_" + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create offscreen buffer: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    EngineStdOut("Temporary screen texture created successfully (" + to_string(PROJECT_STAGE_WIDTH) + "x" + to_string(PROJECT_STAGE_HEIGHT) + ").", 0);
    return true;
}

void Engine::destroyTemporaryScreen()
{
    if (this->tempScreenTexture != nullptr)
    {
        SDL_DestroyTexture(this->tempScreenTexture);
        this->tempScreenTexture = nullptr;
        EngineStdOut("Temporary screen texture destroyed.", 0);
    }
}

void Engine::terminateGE()
{
    EngineStdOut("Terminating SDL and engine resources...", 0);

    destroyTemporaryScreen();

    if (hudFont)
    {
        TTF_CloseFont(hudFont);
        hudFont = nullptr;
        EngineStdOut("HUD font closed.", 0);
    }
    if (loadingScreenFont)
    {
        TTF_CloseFont(loadingScreenFont);
        loadingScreenFont = nullptr;
        EngineStdOut("Loading screen font closed.", 0);
    }
    TTF_Quit();
    EngineStdOut("SDL_ttf terminated.", 0);

    EngineStdOut("SDL_image terminated.", 0);

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

    SDL_Quit();
    EngineStdOut("SDL terminated.", 0);
}

void Engine::handleRenderDeviceReset()
{
    EngineStdOut("Render device was reset. All GPU resources will be recreated.", 1);

    destroyTemporaryScreen();

    for (auto &objInfo : objects_in_order)
    {
        if (objInfo.objectType == "sprite")
        {
            for (auto &costume : objInfo.costumes)
            {
                if (costume.imageHandle)
                {
                    SDL_DestroyTexture(costume.imageHandle);
                    costume.imageHandle = nullptr;
                }
            }
        }
    }

    m_needsTextureRecreation = true;
}

bool Engine::loadImages()
{
    EngineStdOut("Starting image loading...", 0);
    totalItemsToLoad = 0;
    loadedItemCount = 0;

    for (auto &objInfo : objects_in_order)
    {
        if (objInfo.objectType == "sprite")
        {
            for (auto &costume : objInfo.costumes)
            {
                if (costume.imageHandle)
                {
                    SDL_DestroyTexture(costume.imageHandle);
                    costume.imageHandle = nullptr;
                }
            }
        }
    }

    for (const auto &objInfo : objects_in_order)
    {
        if (objInfo.objectType == "sprite")
        {
            totalItemsToLoad += static_cast<int>(objInfo.costumes.size());
        }
    }
    EngineStdOut("Total image items to load: " + to_string(totalItemsToLoad), 0);

    if (totalItemsToLoad == 0)
    {
        EngineStdOut("No image items to load.", 0);
        return true;
    }

    int loadedCount = 0;
    int failedCount = 0;
    string imagePath = "";
    for (auto &objInfo : objects_in_order)
    {
        if (objInfo.objectType == "sprite")
        {

            for (auto &costume : objInfo.costumes)
            {
                if (IsSysMenu)
                {
                    imagePath = "sysmenu/" + costume.fileurl;
                }
                else
                {
                    imagePath = string(BASE_ASSETS) + costume.fileurl;
                }

                if (!this->renderer)
                {
                    EngineStdOut("CRITICAL Renderer is NULL before IMG_LoadTexture for " + imagePath, 2);
                }
                SDL_ClearError();

                costume.imageHandle = IMG_LoadTexture(this->renderer, imagePath.c_str());
                if (!costume.imageHandle)
                {
                    EngineStdOut("Renderer pointer value at IMG_LoadTexture failure: " + to_string(reinterpret_cast<uintptr_t>(this->renderer)), 3);
                }
                if (costume.imageHandle)
                {
                    loadedCount++;
                    EngineStdOut("  Shape '" + costume.name + "' (" + imagePath + ") image loaded successfully as SDL_Texture.", 0);
                }
                else
                {
                    failedCount++;
                    EngineStdOut("IMG_LoadTexture failed for '" + objInfo.name + "' shape '" + costume.name + "' from path: " + imagePath + ". SDL_" + SDL_GetError(), 2);
                }

                incrementLoadedItemCount();

                if (loadedItemCount % 5 == 0 || loadedItemCount == totalItemsToLoad || costume.imageHandle == nullptr)
                {
                    renderLoadingScreen();

                    SDL_Event e;
                    while (SDL_PollEvent(&e) != 0)
                    {
                        if (e.type == SDL_EVENT_QUIT)
                        {
                            EngineStdOut("Image loading cancelled by user.", 1);
                            return false;
                        }
                    }
                }
            }
        }
    }

    EngineStdOut("Image loading finished. Success: " + to_string(loadedCount) + ", Failed: " + to_string(failedCount), 0);

    if (failedCount > 0 && loadedCount == 0 && totalItemsToLoad > 0)
    {
        EngineStdOut("All images failed to load. Cannot continue.", 2);
        showMessageBox("Fatal No images could be loaded. Check asset paths and file integrity.", msgBoxIconType.ICON_ERROR);
        return false;
    }
    else if (failedCount > 0)
    {
        EngineStdOut("Some images failed to load, processing with available resources.", 1);
    }
    return true;
}

bool Engine::recreateAssetsIfNeeded()
{
    if (!m_needsTextureRecreation)
    {
        return true;
    }

    EngineStdOut("Recreating GPU assets due to device reset...", 0);

    if (!createTemporaryScreen())
    {
        EngineStdOut("Failed to recreate temporary screen texture after device reset.", 2);
        return false;
    }

    if (!loadImages())
    {
        EngineStdOut("Failed to reload images after device reset.", 2);
        return false;
    }

    m_needsTextureRecreation = false;
    EngineStdOut("GPU assets recreated successfully.", 0);
    return true;
}

void Engine::drawAllEntities()
{

    if (!renderer || !tempScreenTexture)
    {
        EngineStdOut("drawAllEntities: Renderer or temporary screen texture not available.", 1);
        return;
    }

    SDL_SetRenderTarget(renderer, tempScreenTexture);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    for (int i = static_cast<int>(objects_in_order.size()) - 1; i >= 0; --i)
    {
        const ObjectInfo &objInfo = objects_in_order[i];

        bool isInCurrentScene = (objInfo.sceneId == currentSceneId);
        bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty());

        if (!isInCurrentScene && !isGlobal)
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

            const Costume *selectedCostume = nullptr;

            for (const auto &costume_ref : objInfo.costumes)
            {
                if (costume_ref.id == objInfo.selectedCostumeId)
                {
                    selectedCostume = &costume_ref;
                    break;
                }
            }

            if (selectedCostume && selectedCostume->imageHandle != nullptr)
            {
                double entryX = entityPtr->getX();
                double entryY = entityPtr->getY();

                float sdlX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                float sdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                float texW = 0, texH = 0;
                if (!selectedCostume->imageHandle)
                {
                    EngineStdOut("Texture handle is null for costume '" + selectedCostume->name + "' of object '" + objInfo.name + "'. Cannot get texture size.", 2);
                    continue;
                }

                if (SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH) != true)
                {
                    const char *sdlErrorChars = SDL_GetError();
                    string errorDetail = "No specific SDL error message available.";
                    if (sdlErrorChars && sdlErrorChars[0] != '\0')
                    {
                        errorDetail = string(sdlErrorChars);
                    }

                    ostringstream oss;
                    oss << selectedCostume->imageHandle;
                    string texturePtrStr = oss.str();
                    EngineStdOut("Failed to get texture size for costume '" + selectedCostume->name + "' of object '" + objInfo.name + "'. Texture Ptr: " + texturePtrStr + ". SDL_" + errorDetail, 2);
                    SDL_ClearError();
                    continue;
                }

                SDL_FRect dstRect;

                dstRect.w = static_cast<float>(texW * entityPtr->getScaleX());
                dstRect.h = static_cast<float>(texH * entityPtr->getScaleY());
                SDL_FPoint center;
                center.x = static_cast<float>(entityPtr->getRegX());
                center.y = static_cast<float>(entityPtr->getRegY());
                dstRect.x = sdlX - static_cast<float>(entityPtr->getRegX() * entityPtr->getScaleX());
                dstRect.y = sdlY - static_cast<float>(entityPtr->getRegY() * entityPtr->getScaleY());

                double sdlAngle = entityPtr->getRotation() + (entityPtr->getDirection() - 90.0);

                SDL_RenderTextureRotated(renderer, selectedCostume->imageHandle, nullptr, &dstRect, sdlAngle, &center, SDL_FLIP_NONE);
            }
        }
        else if (objInfo.objectType == "textBox")
        {

            if (!objInfo.textContent.empty())
            {

                string fontString = objInfo.fontName;

                string determinedFontPath;
                string fontfamily = objInfo.fontName;
                string fontAsset = string(FONT_ASSETS);
                int fontSize = objInfo.fontSize;
                SDL_Color textColor = objInfo.textColor;
                FontName fontLoadEnum = getFontNameFromString(fontfamily);
                TTF_Font *Usefont = nullptr;
                int currentFontSize = objInfo.fontSize;
                switch (fontLoadEnum)
                {
                case FontName::D2Coding:
                    determinedFontPath = fontAsset + "d2coding.ttf";
                    break;
                case FontName::NanumGothic:
                    determinedFontPath = fontAsset + "nanum_gothic.ttf";
                    break;
                case FontName::MaruBuri:
                    determinedFontPath = fontAsset + "maruburi.ttf";
                    break;
                case FontName::NanumBarunPen:
                    determinedFontPath = fontAsset + "nanum_barunpen.ttf";
                    break;
                case FontName::NanumPen:
                    determinedFontPath = fontAsset + "nanum_pen.ttf";
                    break;
                case FontName::NanumMyeongjo:
                    determinedFontPath = fontAsset + "nanum_myeongjo.ttf";
                    break;
                case FontName::NanumSquareRound:
                    determinedFontPath = fontAsset + "nanum_square_round.ttf";
                    break;
                default:
                    determinedFontPath = fontAsset + "nanum_gothic.ttf";
                    break;
                }
                if (!determinedFontPath.empty())
                {
                    Usefont = TTF_OpenFont(determinedFontPath.c_str(), fontSize);
                    if (!Usefont)
                    {
                        EngineStdOut("Failed to load font: " + determinedFontPath + " at size " + to_string(currentFontSize) + " for textBox '" + objInfo.name + "'. Falling back to HUD font.", 2);
                        Usefont = hudFont;
                    }
                }
                else
                {
                    Usefont = hudFont;
                }

                SDL_Surface *textSurface = TTF_RenderText_Blended(Usefont, objInfo.textContent.c_str(), objInfo.textContent.size(), objInfo.textColor);
                if (textSurface)
                {

                    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                    if (textTexture)
                    {

                        double entryX = entityPtr->getX();
                        double entryY = entityPtr->getY();
                        float sdlX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                        float sdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                        float textWidth = static_cast<float>(textSurface->w);
                        float textHeight = static_cast<float>(textSurface->h);
                        float scaledWidth = textWidth * entityPtr->getScaleX();
                        float scaledHeight = textHeight * entityPtr->getScaleY();
                        SDL_FRect dstRect;
                        dstRect.w = scaledWidth;
                        dstRect.h = scaledHeight;
                        // showMessageBox("textAlign:"+to_string(objInfo.textAlign),msgBoxIconType.ICON_INFORMATION);
                        switch (objInfo.textAlign)
                        {
                        case 0: // 가운데 정렬 (EntryJS 기준)
                            dstRect.x = sdlX - scaledWidth / 2.0f;
                            break;
                        case 1: // 왼쪽 정렬 (EntryJS 기준)
                            dstRect.x = sdlX;
                            break;
                        case 2: // 오른쪽 정렬 (EntryJS 기준)
                            dstRect.x = sdlX - scaledWidth;
                            break;
                        default: // 기본값: 왼쪽 정렬 (또는 EntryJS의 기본값에 맞춰 수정)
                            dstRect.x = sdlX;
                            break;
                        }
                        dstRect.y = sdlY - scaledHeight / 2.0f;

                        SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);

                        SDL_DestroyTexture(textTexture);
                    }
                    else
                    {
                        EngineStdOut("Failed to create text texture for textBox '" + objInfo.name + "'. SDL_" + SDL_GetError(), 2);
                    }

                    SDL_DestroySurface(textSurface);
                }
                else
                {
                    EngineStdOut("Failed to render text surface for textBox '" + objInfo.name, 2);
                }
            }
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    int windowRenderW = 0, windowRenderH = 0;
    SDL_GetRenderOutputSize(renderer, &windowRenderW, &windowRenderH);

    if (windowRenderW <= 0 || windowRenderH <= 0)
    {
        EngineStdOut("drawAllEntities: Window render dimensions are zero or negative.", 1);
        return;
    }

    float srcViewWidth = static_cast<float>(PROJECT_STAGE_WIDTH) / zoomFactor;
    float srcViewHeight = static_cast<float>(PROJECT_STAGE_HEIGHT) / zoomFactor;
    float srcViewX = (static_cast<float>(PROJECT_STAGE_WIDTH) - srcViewWidth) / 2.0f;
    float srcViewY = (static_cast<float>(PROJECT_STAGE_HEIGHT) - srcViewHeight) / 2.0f;
    SDL_FRect currentSrcFRect = {srcViewX, srcViewY, srcViewWidth, srcViewHeight};

    float stageContentAspectRatio = static_cast<float>(PROJECT_STAGE_WIDTH) / static_cast<float>(PROJECT_STAGE_HEIGHT);

    SDL_FRect finalDisplayDstRect;
    float windowAspectRatio = static_cast<float>(windowRenderW) / static_cast<float>(windowRenderH);

    if (windowAspectRatio >= stageContentAspectRatio)
    {

        finalDisplayDstRect.h = static_cast<float>(windowRenderH);
        finalDisplayDstRect.w = finalDisplayDstRect.h * stageContentAspectRatio;
        finalDisplayDstRect.x = (static_cast<float>(windowRenderW) - finalDisplayDstRect.w) / 2.0f;
        finalDisplayDstRect.y = 0.0f;
    }
    else
    {

        finalDisplayDstRect.w = static_cast<float>(windowRenderW);
        finalDisplayDstRect.h = finalDisplayDstRect.w / stageContentAspectRatio;
        finalDisplayDstRect.x = 0.0f;
        finalDisplayDstRect.y = (static_cast<float>(windowRenderH) - finalDisplayDstRect.h) / 2.0f;
    }

    SDL_RenderTexture(renderer, tempScreenTexture, &currentSrcFRect, &finalDisplayDstRect);
}

void Engine::drawHUD()
{

    if (!this->renderer)
    {
        EngineStdOut("drawHUD: Renderer not available.", 1);
        return;
    }

    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH);

    if (this->hudFont && this->specialConfig.showFPS)
    {

        string fpsText = "FPS: " + to_string(static_cast<int>(currentFps));
        SDL_Color textColor = {255, 150, 0, 255};

        SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, fpsText.c_str(), fpsText.size(), textColor);
        if (textSurface)
        {

            SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            if (textTexture)
            {

                SDL_FRect dstRect = {10.0f, 10.0f, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};

                SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);

                SDL_DestroyTexture(textTexture);
            }
            else
            {
                EngineStdOut("Failed to create FPS text texture: " + string(SDL_GetError()), 2);
            }

            SDL_DestroySurface(textSurface);
        }
        else
        {
            EngineStdOut("Failed to render FPS text surface ", 2);
        }
    }

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

        if (this->hudFont)
        {
            ostringstream zoomStream;
            zoomStream << fixed << setprecision(2) << zoomFactor;
            string zoomText = "Zoom: " + zoomStream.str();
            SDL_Color textColor = {220, 220, 220, 255};

            SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, zoomText.c_str(), zoomText.size(), textColor);
            if (textSurface)
            {
                SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                if (textTexture)
                {

                    SDL_FRect dstRect = {SLIDER_X + SLIDER_WIDTH + 10.0f, SLIDER_Y + (SLIDER_HEIGHT - static_cast<float>(textSurface->h)) / 2.0f, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};
                    SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);
                    SDL_DestroyTexture(textTexture);
                }
                else
                {
                    EngineStdOut("Failed to create Zoom text texture: " + string(SDL_GetError()), 2);
                }
                SDL_DestroySurface(textSurface);
            }
            else
            {
                EngineStdOut("Failed to render Zoom text surface ", 2);
            }
        }
    }
}

bool Engine::mapWindowToStageCoordinates(int windowMouseX, int windowMouseY, float &stageX, float &stageY) const
{
    int windowRenderW = 0, windowRenderH = 0;
    if (this->renderer)
    {
        SDL_GetRenderOutputSize(this->renderer, &windowRenderW, &windowRenderH);
    }
    else
    {
        EngineStdOut("mapWindowToStageCoordinates: Renderer not available.", 2);
        return false;
    }

    if (windowRenderW <= 0 || windowRenderH <= 0)
    {
        EngineStdOut("mapWindowToStageCoordinates: Render dimensions are zero or negative.", 2);
        return false;
    }

    float fullStageWidthTex = static_cast<float>(PROJECT_STAGE_WIDTH);
    float fullStageHeightTex = static_cast<float>(PROJECT_STAGE_HEIGHT);

    float currentSrcViewWidth = fullStageWidthTex / this->zoomFactor;
    float currentSrcViewHeight = fullStageHeightTex / this->zoomFactor;
    float currentSrcViewX = (fullStageWidthTex - currentSrcViewWidth) / 2.0f;
    float currentSrcViewY = (fullStageHeightTex - currentSrcViewHeight) / 2.0f;

    float stageContentAspectRatio = fullStageWidthTex / fullStageHeightTex;
    SDL_FRect finalDisplayDstRect;
    float windowAspectRatio = static_cast<float>(windowRenderW) / static_cast<float>(windowRenderH);

    if (windowAspectRatio >= stageContentAspectRatio)
    {
        finalDisplayDstRect.h = static_cast<float>(windowRenderH);
        finalDisplayDstRect.w = finalDisplayDstRect.h * stageContentAspectRatio;
        finalDisplayDstRect.x = (static_cast<float>(windowRenderW) - finalDisplayDstRect.w) / 2.0f;
        finalDisplayDstRect.y = 0.0f;
    }
    else
    {
        finalDisplayDstRect.w = static_cast<float>(windowRenderW);
        finalDisplayDstRect.h = finalDisplayDstRect.w / stageContentAspectRatio;
        finalDisplayDstRect.x = 0.0f;
        finalDisplayDstRect.y = (static_cast<float>(windowRenderH) - finalDisplayDstRect.h) / 2.0f;
    }

    if (finalDisplayDstRect.w <= 0.0f || finalDisplayDstRect.h <= 0.0f)
    {
        EngineStdOut("mapWindowToStageCoordinates: Calculated final display rect has zero or negative dimension.", 2);
        return false;
    }

    if (static_cast<float>(windowMouseX) < finalDisplayDstRect.x || static_cast<float>(windowMouseX) >= finalDisplayDstRect.x + finalDisplayDstRect.w ||
        static_cast<float>(windowMouseY) < finalDisplayDstRect.y || static_cast<float>(windowMouseY) >= finalDisplayDstRect.y + finalDisplayDstRect.h)
    {
        return false;
    }
    float normX_on_displayed_stage = (static_cast<float>(windowMouseX) - finalDisplayDstRect.x) / finalDisplayDstRect.w;
    float normY_on_displayed_stage = (static_cast<float>(windowMouseY) - finalDisplayDstRect.y) / finalDisplayDstRect.h;

    float texture_coord_x_abs = currentSrcViewX + (normX_on_displayed_stage * currentSrcViewWidth);
    float texture_coord_y_abs = currentSrcViewY + (normY_on_displayed_stage * currentSrcViewHeight);

    stageX = texture_coord_x_abs - (fullStageWidthTex / 2.0f);
    stageY = (fullStageHeightTex / 2.0f) - texture_coord_y_abs;

    return true;
}

void Engine::processInput(const SDL_Event &event)
{

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            bool uiClicked = false;
            int mouseX = event.button.x;
            int mouseY = event.button.y;
            if (this->specialConfig.showZoomSlider &&
                mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH &&
                mouseY >= SLIDER_Y - 5 && mouseY <= SLIDER_Y + SLIDER_HEIGHT + 5)
            {
                float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
                this->zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
                this->zoomFactor = max(MIN_ZOOM, min(MAX_ZOOM, this->zoomFactor));
                this->m_isDraggingZoomSlider = true;
                uiClicked = true;
            }

            if (!uiClicked && m_gameplayInputActive)
            {

                if (!m_mouseClickedScripts.empty())
                {
                    for (const auto &scriptPair : m_mouseClickedScripts)
                    {
                        const string &objectId = scriptPair.first;
                        const Script *scriptPtr = scriptPair.second;
                        Entity *currentEntity = getEntityById(objectId);

                        if (currentEntity && currentEntity->isVisible())
                        {
                            bool executeForScene = false;
                            for (const auto &objInfo : objects_in_order)
                            {
                                if (objInfo.id == objectId)
                                {
                                    bool isInCurrentScene = (objInfo.sceneId == currentSceneId);
                                    bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty());
                                    if (isInCurrentScene || isGlobal)
                                    {
                                        executeForScene = true;
                                    }
                                    break;
                                }
                            }
                            if (executeForScene)
                            {
                                executeScript(*this, objectId, scriptPtr);
                            }
                        }
                        else if (!currentEntity)
                        {
                            EngineStdOut("Warning: Entity with ID '" + objectId + "' not found for mouse_clicked event. Script not run.", 1);
                        }
                    }
                }

                float stageMouseX = 0.0f, stageMouseY = 0.0f;
                if (mapWindowToStageCoordinates(mouseX, mouseY, stageMouseX, stageMouseY))
                {

                    for (int i = static_cast<int>(objects_in_order.size()) - 1; i >= 0; --i)
                    {
                        const ObjectInfo &objInfo = objects_in_order[i];
                        const string &objectId = objInfo.id;

                        bool isInCurrentScene = (objInfo.sceneId == currentSceneId);
                        bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty());
                        if (!isInCurrentScene && !isGlobal)
                        {
                            continue;
                        }

                        Entity *entity = getEntityById(objectId);
                        if (!entity || !entity->isVisible())
                        {
                            continue;
                        }

                        if (entity->isPointInside(stageMouseX, stageMouseY))
                        {
                            m_pressedObjectId = objectId;

                            for (const auto &clickScriptPair : m_whenObjectClickedScripts)
                            {
                                if (clickScriptPair.first == objectId)
                                {
                                    const Script *scriptPtr = clickScriptPair.second;
                                    EngineStdOut("Executing 'when_object_click' for object: " + objectId, 0);
                                    executeScript(*this, objectId, scriptPtr);
                                }
                            }
                            break;
                        }
                    }
                }
                else
                {
                    EngineStdOut("Warning: Could not map window to stage coordinates for object click.", 1);
                }
            }
            else if (uiClicked)
            {
                m_pressedObjectId = "";
            }
        }
    }
    else if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (m_gameplayInputActive)
        {
            SDL_Scancode scancode = event.key.scancode;
            auto it = keyPressedScripts.find(scancode);
            if (it != keyPressedScripts.end())
            {
                const auto &scriptsToRun = it->second;
                for (const auto &scriptPair : scriptsToRun)
                {
                    const string &objectId = scriptPair.first;
                    const Script *scriptPtr = scriptPair.second;
                    EngineStdOut(" -> Executing 'Key Pressed' script for object: " + objectId + " (Key: " + SDL_GetScancodeName(scancode) + ")", 0);
                    executeScript(*this, objectId, scriptPtr);
                }
            }
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        if (this->m_isDraggingZoomSlider && (event.motion.state & SDL_BUTTON_LMASK))
        {
            int mouseX = event.motion.x;

            if (mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH)
            {
                float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
                this->zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
                this->zoomFactor = max(MIN_ZOOM, min(MAX_ZOOM, this->zoomFactor));
            }
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            this->m_isDraggingZoomSlider = false;

            if (m_gameplayInputActive)
            {

                if (!m_mouseClickCanceledScripts.empty())
                {
                    for (const auto &scriptPair : m_mouseClickCanceledScripts)
                    {
                        const string &objectId = scriptPair.first;
                        const Script *scriptPtr = scriptPair.second;
                        Entity *currentEntity = getEntityById(objectId);
                        if (currentEntity && currentEntity->isVisible())
                        {
                            bool executeForScene = false;
                            for (const auto &objInfo : objects_in_order)
                            {
                                if (objInfo.id == objectId)
                                {
                                    bool isInCurrentScene = (objInfo.sceneId == currentSceneId);
                                    bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty());
                                    if (isInCurrentScene || isGlobal)
                                    {
                                        executeForScene = true;
                                    }
                                    break;
                                }
                            }
                            if (executeForScene)
                            {
                                executeScript(*this, objectId, scriptPtr);
                            }
                        }
                        else if (!currentEntity)
                        {
                            EngineStdOut("Warning: Entity with ID '" + objectId + "' not found for mouse_click_canceled event. Script not run.", 1);
                        }
                    }
                }

                if (!m_pressedObjectId.empty())
                {
                    const string &canceledObjectId = m_pressedObjectId;
                    m_pressedObjectId = "";

                    for (const auto &scriptPair : m_whenObjectClickCanceledScripts)
                    {
                        if (scriptPair.first == canceledObjectId)
                        {
                            const Script *scriptPtr = scriptPair.second;
                            Entity *entity = getEntityById(canceledObjectId);

                            EngineStdOut("Executing 'when_object_click_canceled' for object: " + canceledObjectId, 0);
                            executeScript(*this, canceledObjectId, scriptPtr);
                        }
                    }
                }
            }
        }
    }
}

SDL_Scancode Engine::mapStringToSDLScancode(const string &keyIdentifier) const
{

    static const map<string, SDL_Scancode> jsKeyCodeMap = {
        {"8", SDL_SCANCODE_BACKSPACE},
        {"9", SDL_SCANCODE_TAB},
        {"13", SDL_SCANCODE_RETURN},

        {"16", SDL_SCANCODE_LSHIFT},
        {"17", SDL_SCANCODE_LCTRL},
        {"18", SDL_SCANCODE_LALT},
        {"27", SDL_SCANCODE_ESCAPE},
        {"32", SDL_SCANCODE_SPACE},
        {"37", SDL_SCANCODE_LEFT},
        {"38", SDL_SCANCODE_UP},
        {"39", SDL_SCANCODE_RIGHT},
        {"40", SDL_SCANCODE_DOWN},

        {"48", SDL_SCANCODE_0},
        {"49", SDL_SCANCODE_1},
        {"50", SDL_SCANCODE_2},
        {"51", SDL_SCANCODE_3},
        {"52", SDL_SCANCODE_4},
        {"53", SDL_SCANCODE_5},
        {"54", SDL_SCANCODE_6},
        {"55", SDL_SCANCODE_7},
        {"56", SDL_SCANCODE_8},
        {"57", SDL_SCANCODE_9},

        {"65", SDL_SCANCODE_A},
        {"66", SDL_SCANCODE_B},
        {"67", SDL_SCANCODE_C},
        {"68", SDL_SCANCODE_D},
        {"69", SDL_SCANCODE_E},
        {"70", SDL_SCANCODE_F},
        {"71", SDL_SCANCODE_G},
        {"72", SDL_SCANCODE_H},
        {"73", SDL_SCANCODE_I},
        {"74", SDL_SCANCODE_J},
        {"75", SDL_SCANCODE_K},
        {"76", SDL_SCANCODE_L},
        {"77", SDL_SCANCODE_M},
        {"78", SDL_SCANCODE_N},
        {"79", SDL_SCANCODE_O},
        {"80", SDL_SCANCODE_P},
        {"81", SDL_SCANCODE_Q},
        {"82", SDL_SCANCODE_R},
        {"83", SDL_SCANCODE_S},
        {"84", SDL_SCANCODE_T},
        {"85", SDL_SCANCODE_U},
        {"86", SDL_SCANCODE_V},
        {"87", SDL_SCANCODE_W},
        {"88", SDL_SCANCODE_X},
        {"89", SDL_SCANCODE_Y},
        {"90", SDL_SCANCODE_Z},

        {"186", SDL_SCANCODE_SEMICOLON},
        {"187", SDL_SCANCODE_EQUALS},
        {"188", SDL_SCANCODE_COMMA},
        {"189", SDL_SCANCODE_MINUS},
        {"190", SDL_SCANCODE_PERIOD},
        {"191", SDL_SCANCODE_SLASH},
        {"192", SDL_SCANCODE_GRAVE},
        {"219", SDL_SCANCODE_LEFTBRACKET},
        {"220", SDL_SCANCODE_BACKSLASH},
        {"221", SDL_SCANCODE_RIGHTBRACKET},
        {"222", SDL_SCANCODE_APOSTROPHE}};

    auto it = jsKeyCodeMap.find(keyIdentifier);
    if (it != jsKeyCodeMap.end())
    {
        return it->second;
    }

    SDL_Scancode sc = SDL_GetScancodeFromName(keyIdentifier.c_str());
    if (sc != SDL_SCANCODE_UNKNOWN)
    {
        return sc;
    }

    if (keyIdentifier.length() == 1)
    {
        char c = keyIdentifier[0];
        if (c >= 'a' && c <= 'z')
        {
            char upper_c = static_cast<char>(toupper(c));
            string upper_s(1, upper_c);
            return SDL_GetScancodeFromName(upper_s.c_str());
        }
    }

    return SDL_SCANCODE_UNKNOWN;
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

        EngineStdOut(" -> Running script for object: " + objectId, 3);

        executeScript(*this, objectId, scriptPtr);
        m_gameplayInputActive = true;
        EngineStdOut(" -> executeScript call is commented out. Script for object " + objectId + " was not executed.", 1);
    }
    EngineStdOut("Finished running 'Start Button Clicked' scripts.", 0);
}

void Engine::initFps()
{
    lastfpstime = SDL_GetTicks();
    framecount = 0;
    currentFps = 0.0f;
    EngineStdOut("FPS counter initialized.", 0);
}

void Engine::setfps(int fps)
{
    if (fps > 0)
    {
        this->specialConfig.TARGET_FPS = fps;
        EngineStdOut("Target FPS set to: " + to_string(this->specialConfig.TARGET_FPS), 0);
    }
    else
    {
        EngineStdOut("Attempted to set invalid Target FPS: " + to_string(fps) + ". Keeping current TARGET_FPS: " + to_string(this->specialConfig.TARGET_FPS), 1);
    }
}

void Engine::updateFps()
{
    framecount++;
    Uint64 now = SDL_GetTicks();
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

    if (!this->renderer)
    {
        EngineStdOut("renderLoadingScreen: Renderer not available.", 1);
        return;
    }

    SDL_SetRenderDrawColor(this->renderer, 30, 30, 30, 255);
    SDL_RenderClear(this->renderer);

    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH);

    int barWidth = 400;
    int barHeight = 30;
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
    progressPercent = min(1.0f, max(0.0f, progressPercent));

    int progressWidth = static_cast<int>((barWidth - 4) * progressPercent);
    SDL_FRect progressRect = {static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(progressWidth), static_cast<float>(barHeight - 4)};
    SDL_SetRenderDrawColor(renderer, 255, 165, 0, 255);
    SDL_RenderFillRect(renderer, &progressRect);

    if (loadingScreenFont)
    {
        SDL_Color textColor = {220, 220, 220, 255};

        ostringstream percentStream;
        percentStream << fixed << setprecision(0) << (progressPercent * 100.0f) << "%";
        string percentText = percentStream.str();

        SDL_Surface *surfPercent = TTF_RenderText_Blended(loadingScreenFont, percentText.c_str(), percentText.size(), textColor);
        if (surfPercent)
        {
            SDL_Texture *texPercent = SDL_CreateTextureFromSurface(renderer, surfPercent);
            if (texPercent)
            {

                SDL_FRect dstRect = {barX + barWidth + 10.0f, barY + (barHeight - static_cast<float>(surfPercent->h)) / 2.0f, static_cast<float>(surfPercent->w), static_cast<float>(surfPercent->h)};
                SDL_RenderTexture(renderer, texPercent, nullptr, &dstRect);
                SDL_DestroyTexture(texPercent);
            }
            else
            {
                EngineStdOut("Failed to create loading percent text texture: " + string(SDL_GetError()), 2);
            }
            SDL_DestroySurface(surfPercent);
        }
        else
        {
            EngineStdOut("Failed to render loading percent text surface ", 2);
        }

        if (!specialConfig.BRAND_NAME.empty())
        {
            SDL_Surface *surfBrand = TTF_RenderText_Blended(loadingScreenFont, specialConfig.BRAND_NAME.c_str(), specialConfig.BRAND_NAME.size(), textColor);
            if (surfBrand)
            {
                SDL_Texture *texBrand = SDL_CreateTextureFromSurface(renderer, surfBrand);
                if (texBrand)
                {

                    SDL_FRect dstRect = {(windowW - static_cast<float>(surfBrand->w)) / 2.0f, barY - static_cast<float>(surfBrand->h) - 10.0f, static_cast<float>(surfBrand->w), static_cast<float>(surfBrand->h)};
                    SDL_RenderTexture(renderer, texBrand, nullptr, &dstRect);
                    SDL_DestroyTexture(texBrand);
                }
                else
                {
                    EngineStdOut("Failed to create brand name text texture: " + string(SDL_GetError()), 2);
                }
                SDL_DestroySurface(surfBrand);
            }
            else
            {
                EngineStdOut("Failed to render brand name text surface ", 2);
            }
        }

        if (specialConfig.SHOW_PROJECT_NAME && !PROJECT_NAME.empty())
        {
            SDL_Surface *surfProject = TTF_RenderText_Blended(loadingScreenFont, PROJECT_NAME.c_str(), PROJECT_NAME.size(), textColor);
            if (surfProject)
            {
                SDL_Texture *texProject = SDL_CreateTextureFromSurface(renderer, surfProject);
                if (texProject)
                {
                    SDL_FRect dstRect = {(windowW - static_cast<float>(surfProject->w)) / 2.0f, barY + static_cast<float>(surfProject->h) + 20.0f, static_cast<float>(surfProject->w), static_cast<float>(surfProject->h)};
                    SDL_RenderTexture(renderer, texProject, nullptr, &dstRect);
                    SDL_DestroyTexture(texProject);
                }
                else
                {
                    EngineStdOut("Failed to create project name text texture: " + string(SDL_GetError()), 2);
                }
                SDL_DestroySurface(surfProject);
            }
            else
            {
                EngineStdOut("Failed to render project name text surface ", 2);
            }
        }
    }
    else
    {
        EngineStdOut("renderLoadingScreen: Loading screen font not available. Cannot draw text.", 1);
    }

    SDL_RenderPresent(this->renderer);
}

const string &Engine::getCurrentSceneId() const
{
    return currentSceneId;
}

/**
 * @brief 메시지 박스 표시
 *
 * @param message 메시지 내용
 * @param IconType 아이콘 종류
 * @param showYesNo 예 / 아니오
 */
bool Engine::showMessageBox(const string &message, int IconType, bool showYesNo) const
{
    Uint32 flags = 0;
    const char *title = OMOCHA_ENGINE_NAME;

    switch (IconType)
    {
    case SDL_MESSAGEBOX_ERROR:
        flags = SDL_MESSAGEBOX_ERROR;
        title = "Omocha is Broken";
        break;
    case SDL_MESSAGEBOX_WARNING:
        flags = SDL_MESSAGEBOX_WARNING;
        title = PROJECT_NAME.c_str();
        break;
    case SDL_MESSAGEBOX_INFORMATION:
        flags = SDL_MESSAGEBOX_INFORMATION;
        title = PROJECT_NAME.c_str();
        break;
    default:

        EngineStdOut("Unknown IconType passed to showMessageBox: " + to_string(IconType) + ". Using default INFORMATION.", 1);
        flags = SDL_MESSAGEBOX_INFORMATION;
        title = "Message";
        break;
    }

    const SDL_MessageBoxButtonData buttons[]{
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Yes"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "No"}};
    const SDL_MessageBoxData messageboxData{
        flags,
        window,
        title,
        message.c_str(),
        SDL_arraysize(buttons),
        buttons,
        nullptr};
    if (showYesNo)
    {
        int buttonid_press = -1;
        if (SDL_ShowMessageBox(&messageboxData, &buttonid_press) < 0)
        {
            EngineStdOut("Can't Showing MessageBox");
        }
        else
        {
            if (buttonid_press == 0)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
    }
    else
    {
        SDL_ShowSimpleMessageBox(flags, title, message.c_str(), this->window);
        return true;
    }
}

/**
 * @brief 엔진 로그출력
 *
 * @param s 출력할내용
 * @param LEVEL 수준 예) 0->정보 1->경고 2->오류 3->디버그 4->특수
 */
void Engine::EngineStdOut(string s, int LEVEL) const
{
    string prefix;

    string color_code = ANSI_COLOR_RESET;

    switch (LEVEL)
    {
    case 0:
        prefix = "[INFO]";
        color_code = ANSI_COLOR_CYAN;
        break;
    case 1:
        prefix = "[WARN]";
        color_code = ANSI_COLOR_YELLOW;
        break;
    case 2:
        prefix = "[ERROR]";
        color_code = ANSI_COLOR_RED;
        break;
    case 3:
        prefix = "[DEBUG]";
        color_code = ANSI_STYLE_BOLD;
        break;
    case 4:
        prefix = "[SAYHELLO]";
        color_code = ANSI_COLOR_YELLOW + ANSI_STYLE_BOLD;
        break;
    default:
        prefix = "[LOG]";
        break;
    }

    printf("%s%s %s%s\n", color_code.c_str(), prefix.c_str(), s.c_str(), ANSI_COLOR_RESET.c_str());

    string logMessage = format("{} {}", prefix, s);
    logger.log(logMessage);
}

void Engine::updateCurrentMouseStageCoordinates(int windowMouseX, int windowMouseY) {
    float stageX_calc, stageY_calc; 
    if (mapWindowToStageCoordinates(windowMouseX, windowMouseY, stageX_calc, stageY_calc)) {
        this->m_currentStageMouseX = stageX_calc;
        this->m_currentStageMouseY = stageY_calc;
        this->m_isMouseOnStage = true;
    } else {
        this->m_isMouseOnStage = false;
        // 마우스가 스테이지 밖에 있을 때의 처리:
        // 엔트리는 마지막 유효 좌표를 유지하거나 0을 반환할 수 있습니다.
        // 현재는 m_isMouseOnStage 플래그로 구분하고, BlockExecutor에서 이 플래그를 확인하여 처리합니다.
        // 필요하다면 여기서 m_currentStageMouseX/Y를 특정 값(예: 0)으로 리셋할 수 있습니다.
        // this->m_currentStageMouseX = 0.0f;
        // this->m_currentStageMouseY = 0.0f;
    }
}
void Engine::goToScene(const std::string &sceneId)
{
    if (scenes.count(sceneId))
    {
        if (currentSceneId == sceneId)
        {
            EngineStdOut("Already in scene: " + scenes[sceneId] + " (ID: " + sceneId + "). No change.", 0);

            return;
        }
        currentSceneId = sceneId;
        EngineStdOut("Changed scene to: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        triggerWhenSceneStartScripts();
    }
    else
    {
        EngineStdOut("Error: Scene with ID '" + sceneId + "' not found. Cannot switch scene.", 2);
    }
}

void Engine::goToNextScene()
{
    if (m_sceneOrder.empty())
    {
        EngineStdOut("Cannot go to next scene: Scene order is not defined or no scenes loaded.", 1);
        return;
    }
    if (currentSceneId.empty())
    {
        EngineStdOut("Cannot go to next scene: Current scene ID is empty.", 1);
        return;
    }

    auto it = std::find(m_sceneOrder.begin(), m_sceneOrder.end(), currentSceneId);
    if (it != m_sceneOrder.end())
    {
        size_t currentIndex = std::distance(m_sceneOrder.begin(), it);
        if (currentIndex + 1 < m_sceneOrder.size())
        {
            goToScene(m_sceneOrder[currentIndex + 1]);
        }
        else
        {
            EngineStdOut("Already at the last scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        }
    }
    else
    {
        EngineStdOut("Error: Current scene ID '" + currentSceneId + "' not found in defined scene order. Cannot determine next scene.", 2);
    }
}

void Engine::goToPreviousScene()
{
    if (m_sceneOrder.empty())
    {
        EngineStdOut("Cannot go to previous scene: Scene order is not defined or no scenes loaded.", 1);
        return;
    }
    if (currentSceneId.empty())
    {
        EngineStdOut("Cannot go to previous scene: Current scene ID is empty.", 1);
        return;
    }

    auto it = std::find(m_sceneOrder.begin(), m_sceneOrder.end(), currentSceneId);
    if (it != m_sceneOrder.end())
    {
        size_t currentIndex = std::distance(m_sceneOrder.begin(), it);
        if (currentIndex > 0)
        {
            goToScene(m_sceneOrder[currentIndex - 1]);
        }
        else
        {
            EngineStdOut("Already at the first scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        }
    }
    else
    {
        EngineStdOut("Error: Current scene ID '" + currentSceneId + "' not found in defined scene order. Cannot determine previous scene.", 2);
    }
}

void Engine::triggerWhenSceneStartScripts()
{
    if (currentSceneId.empty())
    {
        EngineStdOut("Cannot trigger 'when_scene_start' scripts: Current scene ID is empty.", 1);
        return;
    }
    EngineStdOut("Triggering 'when_scene_start' scripts for scene: " + currentSceneId, 0);
    for (const auto &scriptPair : m_whenStartSceneLoadedScripts)
    {
        const std::string &objectId = scriptPair.first;
        const Script *scriptPtr = scriptPair.second;

        bool executeForScene = false;
        for (const auto &objInfo : objects_in_order)
        {
            if (objInfo.id == objectId && (objInfo.sceneId == currentSceneId || objInfo.sceneId == "global" || objInfo.sceneId.empty()))
            {
                executeForScene = true;
                break;
            }
        }

        if (executeForScene)
        {
            EngineStdOut("  -> Running 'when_scene_start' script for object ID: " + objectId + " in scene " + currentSceneId, 0);
            executeScript(*this, objectId, scriptPtr);
        }
    }
}