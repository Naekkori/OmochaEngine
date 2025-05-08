#include "Engine.h" // Engine 클래스 선언을 포함합니다.
#include "Entity.h" // Entity 클래스 선언을 포함합니다.

#include <fstream>    // 파일 스트림 입출력을 위해 포함합니다.
#include <filesystem> // 파일 시스템 관련 작업을 위해 포함합니다 (C++17).
#include <sstream>    // 문자열 스트림 작업을 위해 포함합니다.
#include <iostream>   // 표준 입출력 스트림을 위해 포함합니다.
#include <stdexcept>  // 표준 예외 클래스를 위해 포함합니다.
#include <vector>     // std::vector를 사용하기 위해 포함합니다.
#include <string>     // std::string을 사용하기 위해 포함합니다.
#include <cmath>      // 수학 함수를 사용하기 위해 포함합니다.
#include <cstdio>     // C 스타일 입출력 함수를 위해 포함합니다.
#include <algorithm>  // std::min, std::max 등을 사용하기 위해 포함합니다.
#include <memory>     // 스마트 포인터 등을 사용하기 위해 포함합니다.
#include <format>     // C++20 형식화 라이브러리를 사용하기 위해 포함합니다.

#include "rapidjson/istreamwrapper.h" // For parsing ifstream
#include "rapidjson/error/en.h"       // For human-readable error messages
#include "rapidjson/stringbuffer.h"   // For serializing Value to string
#include "rapidjson/writer.h"         // For serializing Value to string
#include "blocks/BlockExecutor.h"
using namespace std; // std 네임스페이스 사용

// Engine Class Static Constants Definition
const float Engine::MIN_ZOOM = 1.0f;
const float Engine::MAX_ZOOM = 3.0f;

// Global variable definitions
const char *BASE_ASSETS = "assets/";
const char *FONT_ASSETS = "font/";
string PROJECT_NAME;
string WINDOW_TITLE;

// Helper to convert rapidjson::Value to string for logging
static std::string RapidJsonValueToString(const rapidjson::Value &value)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

// 생성자: 엔진 초기 상태 설정
Engine::Engine() : window(nullptr), renderer(nullptr), tempScreenTexture(nullptr), totalItemsToLoad(0), loadedItemCount(0), zoomFactor(1.26f), m_isDraggingZoomSlider(false), logger("omocha_engine.log")
{
    // 엔진 정보 출력 (EngineStdOut은 Engine.h에 선언되어야 합니다)
    EngineStdOut(string(OMOCHA_ENGINE_NAME) + " v" + string(OMOCHA_ENGINE_VERSION) + " " + string(OMOCHA_DEVELOPER_NAME), 4);
    EngineStdOut("See Project page " + string(OMOCHA_ENGINE_GITHUB), 4);
}

// 소멸자: SDL 리소스 및 동적 할당된 메모리 해제
Engine::~Engine()
{
    terminateGE(); // SDL 리소스 해제 보장

    // 엔티티 객체들 해제
    for (auto const &pair : entities)
    {
        delete pair.second; // 동적으로 할당된 Entity 객체 해제
    }
    entities.clear(); // 맵 비우기

    // destroyTemporaryScreen(); // terminateGE에서 호출됨
}

// Helper function to safely get a string from a Json::Value
// JSON 값에서 안전하게 문자열 필드를 읽어오는 헬퍼 함수
std::string Engine::getSafeStringFromJson(const rapidjson::Value &parentValue,
                                          const std::string &fieldName,
                                          const std::string &contextDescription,
                                          const std::string &defaultValue,
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
        // Optionally log non-critical missing fields if needed for debugging
        // else {
        //     EngineStdOut("DEBUG: Optional field '" + fieldName + "' missing in " + contextDescription + ". Using default: '" + defaultValue + "'.", 0);
        // }
        return defaultValue;
    }

    const rapidjson::Value &fieldValue = parentValue[fieldName.c_str()];

    if (!fieldValue.IsString())
    {
        // isString()이 false이고 null이 아닌 경우에만 경고
        if (isCritical || !fieldValue.IsNull())
        {
            EngineStdOut("Field '" + fieldName + "' in " + contextDescription + " is not a string. Value: [" + RapidJsonValueToString(fieldValue) +
                             "]. Using default: '" + defaultValue + "'.",
                         1);
        }
        return defaultValue;
    }

    std::string s_val = fieldValue.GetString();
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

// 프로젝트 파일 (JSON) 로드 및 파싱
bool Engine::loadProject(const string &projectFilePath)
{
    // Reset the allocator document for block params for the new project
    m_blockParamsAllocatorDoc = rapidjson::Document();

    EngineStdOut("Initial Project JSON file parsing...", 0);

    ifstream projectFile(projectFilePath);
    if (!projectFile.is_open())
    {
        // showMessageBox 함수는 Engine.h에 선언되어 있어야 합니다.
        showMessageBox("Failed to open project file: " + projectFilePath, this->msgBoxIconType.ICON_ERROR);
        EngineStdOut(" Failed to open project file: " + projectFilePath, 2);
        return false;
    }

    rapidjson::IStreamWrapper isw(projectFile);
    rapidjson::Document document; // Use 'document' consistently
    document.ParseStream(isw);
    projectFile.close(); // 파일 스트림 닫기

    if (document.HasParseError())
    {
        std::string errorMsg = std::string("Failed to parse project file: ") + rapidjson::GetParseError_En(document.GetParseError()) + // Already using 'document' here
                               " (Offset: " + std::to_string(document.GetErrorOffset()) + ")";
        EngineStdOut(errorMsg, 2);
        showMessageBox(errorMsg, msgBoxIconType.ICON_ERROR);
        return false;
    }

    objects_in_order.clear();
    entities.clear();
    objectScripts.clear(); // 스크립트 맵도 초기화

    PROJECT_NAME = getSafeStringFromJson(document, "name", "project root", "Omocha Project", false, false);
    WINDOW_TITLE = PROJECT_NAME.empty() ? "Omocha Engine" : PROJECT_NAME; // PROJECT_NAME이 비어있을 경우 대비
    EngineStdOut("Project Name: " + (PROJECT_NAME.empty() ? "[Not Set]" : PROJECT_NAME), 0);

    // project.json에서 speed 값 읽어옴
    if (document.HasMember("speed") && document["speed"].IsNumber())
    {
        this->specialConfig.TARGET_FPS = document["speed"].GetInt();
        EngineStdOut("Target FPS set from project.json: " + std::to_string(this->specialConfig.TARGET_FPS), 0);
    }
    else
    {
        EngineStdOut("'speed' field missing or not numeric in project.json. Using default TARGET_FPS: " + std::to_string(this->specialConfig.TARGET_FPS), 1);
    }
    /**
     * @brief 특수 설정
     * 브랜드 이름 (로딩중)
     * 프로젝트 이름 표시 (로딩중)
     */
    if (document.HasMember("specialConfig"))
    {
        const rapidjson::Value &specialConfigJson = document["specialConfig"]; // Use const&
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
                this->specialConfig.showZoomSlider = false; // 기본값
                EngineStdOut("'specialConfig.showZoomSliderUI' field missing or not boolean. Using default: false", 1);
            }

            if (specialConfigJson.HasMember("showProjectNameUI") && specialConfigJson["showProjectNameUI"].IsBool())
            {
                this->specialConfig.SHOW_PROJECT_NAME = specialConfigJson["showProjectNameUI"].GetBool();
            }
            else
            {
                this->specialConfig.SHOW_PROJECT_NAME = false; // 기본값
                EngineStdOut("'specialConfig.showProjectNameUI' field missing or not boolean. Using default: false", 1);
            }
            if (specialConfigJson.HasMember("showFPS") && specialConfigJson["showFPS"].IsBool())
            {
                this->specialConfig.showFPS = specialConfigJson["showFPS"].GetBool();
            }
            else
            {
                this->specialConfig.showFPS = false; // 기본값
                EngineStdOut("'specialConfig.showFPS' field missing or not boolean. Using default: false", 1);
            }
        }
        else
        {
            EngineStdOut("'specialConfig' field is not an object. Skipping special config.", 1);
        }
    }
    else
    {
        EngineStdOut("'specialConfig' field missing in project.json. Using default special config.", 1);
    }

    // 오브젝트 파싱
    if (document.HasMember("objects") && document["objects"].IsArray())
    {
        const rapidjson::Value &objectsJson = document["objects"];
        EngineStdOut("Found " + to_string(objectsJson.Size()) + " objects. Processing...", 0);

        for (rapidjson::SizeType i = 0; i < objectsJson.Size(); ++i) // Use rapidjson::SizeType
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

            ObjectInfo objInfo; // ObjectInfo 구조체는 Engine.h에 정의되어 있어야 합니다.
            objInfo.id = objectId;
            objInfo.name = getSafeStringFromJson(objectJson, "name", "object id: " + objectId, "Unnamed Object", false, true);
            objInfo.objectType = getSafeStringFromJson(objectJson, "objectType", "object id: " + objectId, "sprite", false, false);
            objInfo.sceneId = getSafeStringFromJson(objectJson, "scene", "object id: " + objectId, "", false, true); // sceneId는 비어있을 수 있음 (global)

            // 스프라이트 (코스튬) 로드
            if (objectJson.HasMember("sprite") && objectJson["sprite"].IsObject() &&
                objectJson["sprite"].HasMember("pictures") && objectJson["sprite"]["pictures"].IsArray())
            {
                const rapidjson::Value &picturesJson = objectJson["sprite"]["pictures"];
                EngineStdOut("Found " + to_string(picturesJson.Size()) + " pictures for object " + objInfo.name, 0);
                for (rapidjson::SizeType j = 0; j < picturesJson.Size(); ++j) // 인덱스 j 사용
                {
                    const auto &pictureJson = picturesJson[j];
                    if (pictureJson.IsObject() && pictureJson.HasMember("id") && pictureJson["id"].IsString() &&
                        pictureJson.HasMember("filename") && pictureJson["filename"].IsString())
                    {
                        Costume ctu; // Costume 구조체는 Engine.h에 정의되어 있어야 합니다.
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
                        // ctu.imageHandle is already nullptr by default. It will be loaded in loadImages.

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

            // 사운드 로드
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
                        SoundFile sound; // SoundFile 구조체는 Engine.h에 정의되어 있어야 합니다.
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

            // 선택된 코스튬 ID 찾기 (여러 필드 호환성)
            string tempSelectedCostumeId;
            bool selectedCostumeFound = false;
            if (objectJson.HasMember("selectedPictureId") && objectJson["selectedPictureId"].IsString())
            {
                tempSelectedCostumeId = getSafeStringFromJson(objectJson, "selectedPictureId", "object " + objInfo.name, "", false, false);
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }
            // selectedCostume (string) 호환성
            if (!selectedCostumeFound && objectJson.HasMember("selectedCostume") && objectJson["selectedCostume"].IsString())
            {
                tempSelectedCostumeId = getSafeStringFromJson(objectJson, "selectedCostume", "object " + objInfo.name, "", false, false);
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }
            // selectedCostume (object with id) 호환성
            if (!selectedCostumeFound && objectJson.HasMember("selectedCostume") && objectJson["selectedCostume"].IsObject() &&
                objectJson["selectedCostume"].HasMember("id") && objectJson["selectedCostume"]["id"].IsString()) // Should be IsString
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
                    objInfo.selectedCostumeId = ""; // 코스튬이 없으면 ID를 비워둡니다.
                }
            }

            // 텍스트 상자 속성 로드
            if (objInfo.objectType == "textBox")
            {
                if (objectJson.HasMember("entity") && objectJson["entity"].IsObject())
                {
                    const rapidjson::Value &entityJson = objectJson["entity"];

                    // 텍스트 내용
                    if (entityJson.HasMember("text"))
                    {
                        if (entityJson["text"].IsString())
                        {
                            objInfo.textContent = getSafeStringFromJson(entityJson, "text", "textBox " + objInfo.name, "[DEFAULT TEXT]", false, true);
                        }
                        else if (entityJson["text"].IsNumber())
                        {
                            // 숫자를 문자열로 변환
                            objInfo.textContent = std::to_string(entityJson["text"].GetDouble());
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

                    // 텍스트 색상 (HEX 문자열 #RRGGBB)
                    if (entityJson.HasMember("colour") && entityJson["colour"].IsString())
                    {
                        string hexColor = getSafeStringFromJson(entityJson, "colour", "textBox " + objInfo.name, "#000000", false, false);
                        if (hexColor.length() == 7 && hexColor[0] == '#')
                        {
                            try
                            {
                                // 16진수 문자열을 정수로 변환
                                unsigned int r = stoul(hexColor.substr(1, 2), nullptr, 16);
                                unsigned int g = stoul(hexColor.substr(3, 2), nullptr, 16);
                                unsigned int b = stoul(hexColor.substr(5, 2), nullptr, 16);
                                objInfo.textColor = {(Uint8)r, (Uint8)g, (Uint8)b, 255}; // SDL_Color는 Engine.h에 정의되어 있어야 합니다.
                                EngineStdOut("INFO: textBox '" + objInfo.name + "' text color parsed: R=" + std::to_string(r) + ", G=" + std::to_string(g) + ", B=" + std::to_string(b), 0);
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

                    // 폰트 정보 (예: "12px NanumBarunpen")
                    if (entityJson.HasMember("font") && entityJson["font"].IsString())
                    {
                        string fontString = getSafeStringFromJson(entityJson, "font", "textBox " + objInfo.name, "20px NanumBarunpen", false, true); // 기본값 변경
                        size_t pxPos = fontString.find("px");
                        if (pxPos != string::npos)
                        {
                            try
                            {
                                objInfo.fontSize = stoi(fontString.substr(0, pxPos));
                                EngineStdOut("INFO: textBox '" + objInfo.name + "' font size parsed: " + std::to_string(objInfo.fontSize), 0);
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
                                // "px" 뒤에 바로 글꼴 이름이 오는 경우 (공백 없음)
                                objInfo.fontName = fontString.substr(pxPos + 2);
                                // 앞뒤 공백 제거
                                objInfo.fontName.erase(0, objInfo.fontName.find_first_not_of(" "));
                                objInfo.fontName.erase(objInfo.fontName.find_last_not_of(" ") + 1);
                            }
                            EngineStdOut("INFO: textBox '" + objInfo.name + "' font name parsed: '" + objInfo.fontName + "'", 0);
                        }
                        else
                        {
                            objInfo.fontSize = 20;         // "px" 형식이 아니면 기본 크기 사용
                            objInfo.fontName = fontString; // 전체 문자열을 이름으로 사용
                            EngineStdOut("textBox '" + objInfo.name + "' 'font' field is not in 'size px Name' format: '" + fontString + "'. Using default size 20 and '" + objInfo.fontName + "' as name.", 1);
                        }
                    }
                    else
                    {
                        EngineStdOut("textBox '" + objInfo.name + "' is missing 'font' field or it's not a string. Using default size 20 and empty font name.", 1);
                        objInfo.fontSize = 20;
                        objInfo.fontName = ""; // 기본 폰트 이름 (예: 나눔바른펜)
                    }

                    // 텍스트 정렬 (숫자 값)
                    if (entityJson.HasMember("textAlign") && entityJson["textAlign"].IsNumber())
                    {
                        objInfo.textAlign = entityJson["textAlign"].GetInt();
                        EngineStdOut("INFO: textBox '" + objInfo.name + "' text alignment parsed: " + std::to_string(objInfo.textAlign), 0);
                    }
                    else
                    {
                        objInfo.textAlign = 0; // 기본값 (예: 좌측 정렬)
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
                // 텍스트 상자가 아닌 경우 텍스트 관련 필드는 비워둡니다.
                objInfo.textContent = "";
                objInfo.textColor = {0, 0, 0, 255};
                objInfo.fontName = "";
                objInfo.fontSize = 20;
                objInfo.textAlign = 0;
            }

            // 오브젝트 순서에 추가
            objects_in_order.push_back(objInfo);

            // Entity 객체 생성
            if (objectJson.HasMember("entity") && objectJson["entity"].IsObject())
            {
                const rapidjson::Value &entityJson = objectJson["entity"];

                // 각 속성에 대해 HasMember와 타입 체크를 수행하고 기본값을 제공합니다.
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

                // Entity 객체 동적 할당
                Entity *newEntity = new Entity(
                    objectId,
                    objInfo.name, // ObjectInfo에서 이름 가져오기
                    initial_x, initial_y, initial_regX, initial_regY,
                    initial_scaleX, initial_scaleY, initial_rotation, initial_direction,
                    initial_width, initial_height, initial_visible);

                entities[objectId] = newEntity; // 맵에 추가
                EngineStdOut("INFO: Created Entity for object ID: " + objectId, 0);
            }
            else
            {
                EngineStdOut("Object '" + objInfo.name + "' (ID: " + objectId + ") is missing 'entity' block or it's not an object. Cannot create Entity.", 1);
                // Entity가 없는 경우 해당 오브젝트는 렌더링되지 않거나 기본값으로 처리될 수 있습니다.
            }

            // 스크립트 파싱
            if (objectJson.HasMember("script") && objectJson["script"].IsString())
            {
                string scriptString = getSafeStringFromJson(objectJson, "script", "object " + objInfo.name, "", false, true);
                rapidjson::Document scriptDocument;
                scriptDocument.Parse(scriptString.c_str());

                if (!scriptDocument.HasParseError())
                {
                    EngineStdOut("Script JSON parsed successfully for object: " + objInfo.name, 0);
                    if (scriptDocument.IsArray())
                    {
                        vector<Script> scriptsForObject; // Script 구조체는 Engine.h에 정의되어 있어야 합니다.
                        for (rapidjson::SizeType j = 0; j < scriptDocument.Size(); ++j)
                        {
                            const auto &scriptListJson = scriptDocument[j];
                            if (scriptListJson.IsArray())
                            {
                                Script currentScript;
                                for (rapidjson::SizeType k = 0; k < scriptListJson.Size(); ++k)
                                {
                                    const auto &blockJson = scriptListJson[k];
                                    if (blockJson.IsObject() &&
                                        blockJson.HasMember("id") && blockJson["id"].IsString() &&
                                        blockJson.HasMember("type") && blockJson["type"].IsString())
                                    {
                                        Block block; // Block 구조체는 Engine.h에 정의되어 있어야 합니다.
                                        block.id = getSafeStringFromJson(blockJson, "id", "script block index " + to_string(k) + " in script " + to_string(j) + " for " + objInfo.name, "", true, false);
                                        if (block.id.empty())
                                        {
                                            EngineStdOut("Script block ID is empty for object " + objInfo.name + " at script index " + to_string(j) + ", block index " + to_string(k) + ". Skipping block.", 2);
                                            continue;
                                        }
                                        block.type = getSafeStringFromJson(blockJson, "type", "script block " + block.id + " in " + objInfo.name, "", true, false);
                                        if (block.type.empty())
                                        {
                                            EngineStdOut("Script block type is empty for block " + block.id + " in object " + objInfo.name + ". Skipping block.", 2);
                                            continue;
                                        }
                                        if (blockJson.HasMember("params") && blockJson["params"].IsArray())
                                        {
                                            block.paramsJson.CopyFrom(blockJson["params"], m_blockParamsAllocatorDoc.GetAllocator()); // Json::Value 자체를 저장
                                        }
                                        else
                                        {
                                            EngineStdOut("Script block " + block.id + " in object " + objInfo.name + " is missing 'params' array or it's invalid.", 1);
                                        }
                                        // TODO: statements 파싱 로직 추가 (재귀적으로) - 이 부분은 Engine.h의 Block 구조체 정의 및 파싱 함수에 따라 달라집니다.
                                        currentScript.blocks.push_back(block);
                                    }
                                    else
                                    { // Invalid block structure
                                        EngineStdOut("Invalid block structure in script " + to_string(j) + " for object '" + objInfo.name + "' at block index " + to_string(k) + ". Skipping block. Content: " + RapidJsonValueToString(blockJson), 1);
                                    }
                                } // End of inner loop for blocks
                                scriptsForObject.push_back(currentScript); // Add populated script to the list
                            }
                            else // scriptListJson is not an array
                            {
                                EngineStdOut("Script entry at index " + to_string(j) + " for object '" + objInfo.name + "' is not an array of blocks. Skipping this script.", 1);
                            }
                        }
                        objectScripts[objectId] = scriptsForObject; // Add all parsed scripts for this object
                        EngineStdOut("INFO: Parsed " + std::to_string(scriptsForObject.size()) + " scripts for object ID: " + objectId, 0);
                    }
                    else // scriptDocument is not an array (root of script string)
                    {
                        EngineStdOut("Script root for object '" + objInfo.name + "' is not an array of scripts. Skipping script parsing.", 1);
                    }
                }
                else // Failed to parse the entire script string
                {
                    std::string scriptErrorMsg = std::string("Failed to parse script JSON string for object '") + objInfo.name + "': " +
                                                 rapidjson::GetParseError_En(scriptDocument.GetParseError()) + " (Offset: " + std::to_string(scriptDocument.GetErrorOffset()) + ")";
                    EngineStdOut(scriptErrorMsg, 1);
                }
            }
            else // Object does not have a "script" field or it's not a string
            {
                EngineStdOut("Object '" + objInfo.name + "' is missing 'script' field or it's not a string. No scripts loaded for this object.", 1);
            }
        } // End of main loop for objectsJson
    } // End of if (document.HasMember("objects") ...
    else
    {
        EngineStdOut("project.json is missing 'objects' array or it's not an array.", 1);
    }

    // 씬 파싱
    scenes.clear();                  // 씬 맵 초기화
    string firstSceneIdInOrder = ""; // 첫 번째 씬 ID를 저장할 변수

    if (document.HasMember("scenes") && document["scenes"].IsArray())
    {
        const rapidjson::Value &scenesJson = document["scenes"];
        EngineStdOut("Found " + to_string(scenesJson.Size()) + " scenes. Parsing...", 0);
        for (rapidjson::SizeType i = 0; i < scenesJson.Size(); ++i) // Use rapidjson::SizeType
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
                if (i == 0)
                {
                    firstSceneIdInOrder = sceneId; // 배열의 첫 번째 씬 ID 저장
                    EngineStdOut("Identified first scene in array order: " + sceneName + " (ID: " + firstSceneIdInOrder + ")", 0);
                }
                scenes[sceneId] = sceneName; // 맵에 추가
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

    // 시작 씬 설정
    string startSceneId = "";
    // 엔트리 구버전 호환: "startScene" 필드 확인
    if (document.HasMember("startScene") && document["startScene"].IsString())
    {
        startSceneId = getSafeStringFromJson(document, "startScene", "project root for startScene (legacy)", "", false, false);
        EngineStdOut("'startScene' (legacy) found in project.json: " + startSceneId, 0);
    }
    // 최신 버전: "start" 객체 내 "sceneId" 필드 확인
    else if (document.HasMember("start") && document["start"].IsObject() && document["start"].HasMember("sceneId") && document["start"]["sceneId"].IsString())
    {
        startSceneId = getSafeStringFromJson(document["start"], "sceneId", "project root start object", "", false, false);
        EngineStdOut("'start/sceneId' found in project.json: " + startSceneId, 0);
    }
    else
    {
        EngineStdOut("No explicit 'startScene' or 'start/sceneId' found in project.json.", 1);
    }

    // 유효한 시작 씬 ID가 있고, 해당 ID가 로드된 씬 맵에 존재하는지 확인
    if (!startSceneId.empty() && scenes.count(startSceneId))
    {
        currentSceneId = startSceneId;
        EngineStdOut("Initial scene set to explicit start scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
    }
    else
    {
        // 명시된 시작 씬이 없거나 유효하지 않으면, 배열의 첫 번째 씬을 사용
        if (!firstSceneIdInOrder.empty() && scenes.count(firstSceneIdInOrder))
        {
            currentSceneId = firstSceneIdInOrder;
            EngineStdOut("Initial scene set to first scene in array order: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        }
        else
        {
            EngineStdOut("No valid starting scene found in project.json or no scenes were loaded.", 2);
            currentSceneId = ""; // 시작 씬 없음 상태
            return false;        // 시작 씬 없으면 로드 실패
        }
    }

    EngineStdOut("Identifying 'Start Button Clicked' scripts...", 0);
    startButtonScripts.clear();                              // 시작 버튼 스크립트 목록 초기화
    for (auto const &[objectId, scriptsVec] : objectScripts) // objectScripts 맵 순회
    {
        for (const auto &script : scriptsVec) // 각 오브젝트의 스크립트 벡터 순회
        {
            if (!script.blocks.empty())
            {
                const Block &firstBlock = script.blocks[0];
                // 첫 번째 블록 타입이 "when_run_button_click"인지 확인
                if (firstBlock.type == "when_run_button_click")
                {
                    // 시작 블록 외에 다른 블록이 연결되어 있는지 확인 (실제 실행할 내용이 있는지)
                    if (script.blocks.size() > 1)
                    {
                        // 시작 스크립트 목록에 추가 (오브젝트 ID와 스크립트 포인터 저장)
                        startButtonScripts.push_back({objectId, &script});
                        EngineStdOut("  -> Found valid 'Start Button Clicked' script for object ID: " + objectId, 0);
                    }
                    else
                    {
                        EngineStdOut("  -> Found 'Start Button Clicked' script for object ID: " + objectId + " but it has no subsequent blocks. Skipping.", 1);
                    }
                }
                // "when_some_key_pressed" 블록 처리
                else if (firstBlock.type == "when_some_key_pressed")
                {
                    if (script.blocks.size() > 1) // 실제 실행할 내용이 있는지 확인
                    {
                        string keyIdentifierString;
                        bool keyIdentifierFound = false;
                        if (firstBlock.paramsJson.IsArray() && firstBlock.paramsJson.Size() > 0)
                        {
                            if(firstBlock.paramsJson[0].IsString())
                            {
                                keyIdentifierString = firstBlock.paramsJson[0].GetString();
                                keyIdentifierFound = true;
                            }else if(firstBlock.paramsJson[0].IsNull() && firstBlock.paramsJson.Size() > 1 && firstBlock.paramsJson[1].IsString())
                            {
                                keyIdentifierString = firstBlock.paramsJson[1].GetString();
                                keyIdentifierFound = true;
                            }

                            if (keyIdentifierFound)
                            {
                                SDL_Scancode keyScancode = SDL_GetScancodeFromName(keyIdentifierString.c_str());
                                if (keyScancode != SDL_SCANCODE_UNKNOWN)
                                {
                                    keyPressedScripts[keyScancode].push_back({objectId, &script});
                                }
                            }else{
                                rapidjson::StringBuffer buffer;
                                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                                firstBlock.paramsJson.Accept(writer);
                                EngineStdOut("  -> 'when_some_key_pressed' script for object ID: " + objectId + " has invalid or missing key parameter. Params JSON: " + string(buffer.GetString()) + ". Skipping.", 1);
                            }
                            
                        } else {
                            EngineStdOut("  -> Found 'Key Pressed' script for object ID: " + objectId + " but it has no subsequent blocks. Skipping.", 1);
                        }
                        
                    }
                }
            }
        }
    }
    EngineStdOut("Finished identifying 'Start Button Clicked' scripts. Found: " + std::to_string(startButtonScripts.size()), 0);

    EngineStdOut("Project JSON file parsed successfully.", 0);
    return true; // 프로젝트 로드 성공
}

// SDL 및 그래픽 엔진 초기화
bool Engine::initGE(bool vsyncEnabled, bool attemptVulkan) // Vulkan 사용 여부 인자 추가
{
    EngineStdOut("Initializing SDL...", 0);
    // SDL 비디오 및 오디오 서브시스템 초기화
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        string errMsg = "SDL could not initialize! SDL_" + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to initialize SDL: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    EngineStdOut("SDL initialized successfully (Video and Audio).", 0);

    // SDL_ttf 초기화
    if (TTF_Init() == -1)
    {
        string errMsg = "SDL_ttf could not initialize!"; // TTF_GetError() 사용
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to initialize", msgBoxIconType.ICON_ERROR);

        SDL_Quit(); // SDL 기본 초기화 해제
        return false;
    }
    EngineStdOut("SDL_ttf initialized successfully.", 0);

    // 윈도우 생성 (WINDOW_TITLE은 loadProject에서 설정됨)
    this->window = SDL_CreateWindow(WINDOW_TITLE.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (this->window == nullptr)
    {
        string errMsg = "Window could not be created! SDL_" + string(SDL_GetError());
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create window: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        TTF_Quit(); // TTF 초기화 해제

        SDL_Quit(); // SDL 기본 초기화 해제
        return false;
    }
    EngineStdOut("SDL Window created successfully.", 0);
    if (attemptVulkan)
    {
        EngineStdOut("Attempting to create Vulkan renderer as requested by command line argument...", 0);

        // 사용 가능한 렌더링 드라이버 수 가져오기
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
        EngineStdOut("Available render drivers: " + std::to_string(numRenderDrivers), 0);

        int vulkanDriverIndex = -1;
        for (int i = 0; i < numRenderDrivers; ++i)
        {
            EngineStdOut("Checking render driver at index: " + std::to_string(i) + " " + SDL_GetRenderDriver(i), 0);
            const char *driverName = SDL_GetRenderDriver(i);
            // strcmp를 사용하여 문자열 내용을 비교합니다.
            if (driverName != nullptr && strcmp(driverName, "vulkan") == 0)
            {
                vulkanDriverIndex = i;
                EngineStdOut("Vulkan driver found at index: " + std::to_string(i), 0);
                break; // Vulkan 드라이버를 찾았으므로 루프를 중단합니다.
            }
        }

        if (vulkanDriverIndex != -1)
        {
            // SDL_CreateRenderer는 드라이버 이름을 직접 받을 수 있습니다.
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
                this->renderer = SDL_CreateRenderer(this->window, nullptr); // 기본 렌더러로 폴백
            }
        }
        else
        {
            EngineStdOut("Vulkan render driver not found. Using default renderer.", 1);
            showMessageBox("Vulkan render driver not found. Using default renderer.", msgBoxIconType.ICON_WARNING);
            this->renderer = SDL_CreateRenderer(this->window, nullptr); // 기본 렌더러 사용
        }
    }
    else
    {
        // 선택하지 않았을때
        this->renderer = SDL_CreateRenderer(this->window, nullptr); // 기본 렌더러 사용
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

    // VSync 설정 (SDL3 방식)
    // vsyncEnabled 값에 따라 SDL_VSYNC_ADAPTIVE 또는 SDL_VSYNC_DISABLED 설정
    if (SDL_SetRenderVSync(this->renderer, vsyncEnabled ? SDL_RENDERER_VSYNC_ADAPTIVE : SDL_RENDERER_VSYNC_DISABLED) != 0)
    {
        EngineStdOut("Failed to set VSync mode. SDL_" + string(SDL_GetError()), 1);
        // VSync 설정 실패가 치명적이지는 않으므로 경고만 출력하고 진행합니다.
    }
    else
    {
        EngineStdOut("VSync mode set to: " + string(vsyncEnabled ? "Adaptive" : "Disabled"), 0);
    }

    // HUD 및 로딩 화면용 폰트 로드
    string defaultFontPath = "font/nanum_gothic.ttf"; // 폰트 파일 경로 확인 필요
    hudFont = TTF_OpenFont(defaultFontPath.c_str(), 20);
    loadingScreenFont = TTF_OpenFont(defaultFontPath.c_str(), 30);

    if (!hudFont)
    {
        string errMsg = "Failed to load HUD font! Font path: " + defaultFontPath;
        EngineStdOut(errMsg, 2);
        // HUD 폰트 로드 실패는 치명적일 수 있으므로 오류로 처리하고 메시지 박스 표시
        showMessageBox(errMsg, msgBoxIconType.ICON_ERROR);
        // 폰트 로드 실패 시에도 나머지 초기화 해제
        if (loadingScreenFont)
            TTF_CloseFont(loadingScreenFont);
        TTF_Quit();
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        SDL_DestroyWindow(this->window);
        this->window = nullptr;

        SDL_Quit();
        return false; // 초기화 실패
    }
    EngineStdOut("HUD font loaded successfully.", 0);

    if (!loadingScreenFont)
    {
        string errMsg = "Failed to load loading screen font! Font path: " + defaultFontPath;
        EngineStdOut(errMsg, 2);
        // 로딩 화면 폰트 로드 실패는 치명적일 수 있으므로 오류로 처리
        showMessageBox(errMsg, msgBoxIconType.ICON_ERROR);
        if (hudFont)
            TTF_CloseFont(hudFont);
        TTF_Quit();
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        SDL_DestroyWindow(this->window);
        this->window = nullptr;

        SDL_Quit();
        return false; // 초기화 실패
    }
    EngineStdOut("Loading screen font loaded successfully.", 0);

    SDL_SetRenderDrawColor(this->renderer, 255, 255, 255, 255); // 기본 배경색 (흰색)

    initFps(); // FPS 카운터 초기화 (SDL_GetTicks 사용하도록 수정됨)

    // 임시 화면 텍스처 생성 (렌더링 버퍼로 사용)
    if (!createTemporaryScreen())
    {
        EngineStdOut("Failed to create temporary screen texture during initGE.", 2);
        // 임시 화면 생성 실패 시 모든 초기화 해제
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
        return false; // 초기화 실패
    }
    EngineStdOut("Engine graphics initialization complete.", 0);
    return true; // 초기화 성공
}

// 임시 화면 텍스처 생성 (오프스크린 렌더링 버퍼)
bool Engine::createTemporaryScreen()
{
    if (this->renderer == nullptr)
    {
        EngineStdOut("Renderer not initialized. Cannot create temporary screen texture.", 2);
        showMessageBox("Internal Renderer not available for offscreen buffer.", msgBoxIconType.ICON_ERROR);
        return false;
    }
    // SDL_TEXTUREACCESS_TARGET 플래그로 텍스처를 렌더링 대상으로 사용할 수 있도록 생성
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

/*int Engine::Soundloader(string soundUri)
{
    // 이 함수는 SDL_mixer를 사용하여 구현되어야 합니다.
    EngineStdOut("Soundloader: Needs implementation with SDL_mixer. Sound URI: " + soundUri, 1);
    // SDL_mixer 초기화, 사운드 파일 로드 등의 로직이 필요합니다.
    return -1; // Placeholder
}*/

// 임시 화면 텍스처 해제
void Engine::destroyTemporaryScreen()
{
    if (this->tempScreenTexture != nullptr)
    {
        SDL_DestroyTexture(this->tempScreenTexture);
        this->tempScreenTexture = nullptr;
        EngineStdOut("Temporary screen texture destroyed.", 0);
    }
}

// SDL 및 엔진 리소스 종료
void Engine::terminateGE()
{
    EngineStdOut("Terminating SDL and engine resources...", 0);

    // 임시 화면 텍스처 해제
    destroyTemporaryScreen();

    // 폰트 해제
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
    TTF_Quit(); // SDL_ttf 종료
    EngineStdOut("SDL_ttf terminated.", 0);

    // SDL_image 종료
    EngineStdOut("SDL_image terminated.", 0);

    // 렌더러 해제
    if (this->renderer != nullptr)
    {
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        EngineStdOut("SDL Renderer destroyed.", 0);
    }
    // 윈도우 해제
    if (this->window != nullptr)
    {
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
        EngineStdOut("SDL Window destroyed.", 0);
    }

    SDL_Quit(); // SDL 기본 종료
    EngineStdOut("SDL terminated.", 0);
}

// Called when SDL_EVENT_RENDER_DEVICE_RESET or SDL_EVENT_RENDER_TARGETS_RESET occurs
void Engine::handleRenderDeviceReset()
{
    EngineStdOut("Render device was reset. All GPU resources will be recreated.", 1);

    // Destroy existing temporary screen texture
    destroyTemporaryScreen();

    // Destroy all existing costume textures
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
    // TODO: Destroy other GPU resources if any (e.g., textures for textboxes if cached)

    m_needsTextureRecreation = true;
}

// 이미지 (코스튬 텍스처) 로드
bool Engine::loadImages()
{
    EngineStdOut("Starting image loading...", 0);
    totalItemsToLoad = 0;
    loadedItemCount = 0;

    // If called for recreation, ensure old textures are cleared first
    // This is also handled in handleRenderDeviceReset, but good for standalone calls too.
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
    // 로드할 총 이미지 개수 계산
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
        return true; // 로드할 이미지가 없으면 성공으로 간주
    }

    int loadedCount = 0;
    int failedCount = 0;

    // objects_in_order는 const 참조가 아니므로 직접 수정 가능합니다.
    for (auto &objInfo : objects_in_order)
    {
        if (objInfo.objectType == "sprite")
        {
            // objInfo.costumes는 vector<Costume> 이므로 직접 순회하며 imageHandle 설정
            for (auto &costume : objInfo.costumes)
            {
                string imagePath = string(BASE_ASSETS) + costume.fileurl;
                if (!this->renderer)
                {
                    EngineStdOut("CRITICAL Renderer is NULL before IMG_LoadTexture for " + imagePath, 2);
                }
                SDL_ClearError();
                // SDL_image를 사용하여 이미지 파일을 SDL_Surface로 로드
                costume.imageHandle = IMG_LoadTexture(this->renderer, imagePath.c_str());
                if (!costume.imageHandle)
                {
                    EngineStdOut("Renderer pointer value at IMG_LoadTexture failure: " + std::to_string(reinterpret_cast<uintptr_t>(this->renderer)), 3);
                }
                if (costume.imageHandle)
                {
                    loadedCount++;
                    EngineStdOut("  Shape '" + costume.name + "' (" + imagePath + ") image loaded successfully as SDL_Texture.", 0);
                }
                else
                {
                    failedCount++;
                    EngineStdOut("IMG_LoadTexture failed for '" + objInfo.name + "' shape '" + costume.name + "' from path: " + imagePath + ". SDL_" + SDL_GetError(), 2); // IMG_LoadTexture 실패 시 SDL_GetError() 사용
                }

                // 로딩 진행률 업데이트 및 화면 갱신
                incrementLoadedItemCount(); // loadedItemCount는 Engine 클래스의 멤버 변수여야 합니다.
                // 일정 간격 또는 마지막 항목 로드 시 로딩 화면 갱신
                if (loadedItemCount % 5 == 0 || loadedItemCount == totalItemsToLoad || costume.imageHandle == nullptr) // 5개 로드마다 또는 실패 시 갱신
                {
                    renderLoadingScreen();
                    // 로딩 중 사용자 입력 처리 (창 닫기 등)
                    SDL_Event e;
                    while (SDL_PollEvent(&e) != 0)
                    {
                        if (e.type == SDL_EVENT_QUIT)
                        {
                            EngineStdOut("Image loading cancelled by user.", 1);
                            return false; // 로딩 중단 시 false 반환
                        }
                        // TODO: 다른 이벤트 처리 (예: 창 크기 변경 시 로딩 화면 위치 조정)
                    }
                }
            }
        }
    }

    EngineStdOut("Image loading finished. Success: " + to_string(loadedCount) + ", Failed: " + to_string(failedCount), 0);

    // 모든 이미지가 로드 실패하고 로드할 이미지가 0보다 많으면 치명적인 오류
    if (failedCount > 0 && loadedCount == 0 && totalItemsToLoad > 0)
    {
        EngineStdOut("All images failed to load. Cannot continue.", 2);
        showMessageBox("Fatal No images could be loaded. Check asset paths and file integrity.", msgBoxIconType.ICON_ERROR);
        return false; // 모든 이미지 로드 실패 시 중단
    }
    else if (failedCount > 0)
    {
        EngineStdOut("Some images failed to load, processing with available resources.", 1);
    }
    return true; // 로드 완료 (일부 실패 포함 가능)
}

// Recreates assets if m_needsTextureRecreation is true
bool Engine::recreateAssetsIfNeeded()
{
    if (!m_needsTextureRecreation)
    {
        return true; // No recreation needed
    }

    EngineStdOut("Recreating GPU assets due to device reset...", 0);

    if (!createTemporaryScreen())
    {
        EngineStdOut("Failed to recreate temporary screen texture after device reset.", 2);
        return false;
    }

    if (!loadImages())
    { // loadImages should now correctly clear old textures and load new ones
        EngineStdOut("Failed to reload images after device reset.", 2);
        return false;
    }
    // TODO: Reload other GPU-dependent assets like fonts if they were turned into textures that got lost.
    // TTF_Font itself is not a GPU resource, but textures created from it are.
    m_needsTextureRecreation = false;
    EngineStdOut("GPU assets recreated successfully.", 0);
    return true;
}
// 모든 엔티티를 임시 화면 텍스처에 그립니다.
void Engine::drawAllEntities()
{
    // 렌더러 또는 임시 화면 텍스처가 유효한지 확인
    if (!renderer || !tempScreenTexture)
    {
        EngineStdOut("drawAllEntities: Renderer or temporary screen texture not available.", 1);
        return;
    }

    // 렌더링 대상을 임시 화면 텍스처로 설정
    SDL_SetRenderTarget(renderer, tempScreenTexture);
    // 임시 화면을 흰색으로 클리어
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    // 오브젝트 순서의 역순으로 그립니다 (뒷 오브젝트부터 그려서 겹쳐 보이도록)
    for (int i = static_cast<int>(objects_in_order.size()) - 1; i >= 0; --i)
    {
        const ObjectInfo &objInfo = objects_in_order[i];

        // 현재 씬 또는 'global' 씬에 속하는 오브젝트만 그립니다.
        bool isInCurrentScene = (objInfo.sceneId == currentSceneId);
        bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty()); // sceneId가 비어있으면 global로 간주

        if (!isInCurrentScene && !isGlobal)
        {
            continue; // 현재 씬이나 global 씬이 아니면 건너뜁니다.
        }

        // 해당 오브젝트의 Entity 객체를 찾습니다.
        auto it_entity = entities.find(objInfo.id);
        if (it_entity == entities.end())
        {
            // EngineStdOut("Entity not found for object ID: " + objInfo.id + ". Cannot draw.", 1); // 너무 많은 로그 방지
            continue; // Entity가 없으면 그릴 수 없습니다.
        }
        const Entity *entityPtr = it_entity->second; // Entity 포인터

        // 엔티티가 보이지 않으면 그리지 않습니다.
        if (!entityPtr->isVisible())
        {
            continue;
        }

        // 오브젝트 타입에 따라 그리기 로직 분기
        if (objInfo.objectType == "sprite")
        {
            // 현재 선택된 코스튬 찾기
            const Costume *selectedCostume = nullptr;
            // objInfo.costumes는 const ObjectInfo&의 멤버이므로 const vector<Costume>입니다.
            // 따라서 const auto&를 사용하여 순회해야 합니다.
            for (const auto &costume_ref : objInfo.costumes)
            {
                if (costume_ref.id == objInfo.selectedCostumeId)
                {
                    selectedCostume = &costume_ref; // const Costume* 포인터 저장
                    break;
                }
            }

            // 선택된 코스튬이 있고 이미지 핸들(SDL_Texture)이 유효하면 그립니다.
            if (selectedCostume && selectedCostume->imageHandle != nullptr)
            {
                double entryX = entityPtr->getX();
                double entryY = entityPtr->getY();

                // SDL 좌표계로 변환 (Entry: 중심 (0,0), Y축 위가 +, SDL: 좌상단 (0,0), Y축 아래가 +)
                // Entry X는 그대로 사용 (좌우 동일)
                // Entry Y를 SDL Y로 변환: Stage 높이의 절반에서 Entry Y를
                float sdlX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                float sdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                // 텍스처 원본 크기 가져오기
                float texW = 0, texH = 0; // int 타입으로 선언
                if (!selectedCostume->imageHandle)
                { // imageHandle 유효성 검사 추가
                    EngineStdOut("Texture handle is null for costume '" + selectedCostume->name + "' of object '" + objInfo.name + "'. Cannot get texture size.", 2);
                    continue; // 다음 오브젝트 또는 코스튬으로 넘어갑니다.
                }

                if (SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH) != true)
                {
                    const char *sdlErrorChars = SDL_GetError(); // 오류 발생 직후 SDL_GetError() 호출
                    std::string errorDetail = "No specific SDL error message available.";
                    if (sdlErrorChars && sdlErrorChars[0] != '\0')
                    { // 오류 메시지가 null이 아니고 비어있지 않은지 확인
                        errorDetail = std::string(sdlErrorChars);
                    }
                    // 텍스처 핸들 주소 로깅 (디버깅용)
                    std::ostringstream oss;
                    oss << selectedCostume->imageHandle;
                    std::string texturePtrStr = oss.str();
                    EngineStdOut("Failed to get texture size for costume '" + selectedCostume->name + "' of object '" + objInfo.name + "'. Texture Ptr: " + texturePtrStr + ". SDL_" + errorDetail, 2);
                    SDL_ClearError(); // 다음 SDL 호출에 영향을 주지 않도록 오류 상태를 클리어 (선택 사항)
                    continue;         // 텍스처 크기를 가져올 수 없으면 그리지 않습니다.
                }

                // 렌더링될 사각형 (목적지 사각형) 계산
                SDL_FRect dstRect;
                // 스케일 적용된 너비/높이
                dstRect.w = static_cast<float>(texW * entityPtr->getScaleX());
                dstRect.h = static_cast<float>(texH * entityPtr->getScaleY());
                SDL_FPoint center; // 회전 및 스케일링 중심점 (텍스처 로컬 좌표)
                center.x = static_cast<float>(entityPtr->getRegX());
                center.y = static_cast<float>(entityPtr->getRegY());
                dstRect.x = sdlX - static_cast<float>(entityPtr->getRegX() * entityPtr->getScaleX()); // 스케일 적용된 regX 만큼 좌측으로 이동
                dstRect.y = sdlY - static_cast<float>(entityPtr->getRegY() * entityPtr->getScaleY()); // 스케일 적용된 regY 만큼 상단으로 이동

                // Entry 회전(시계방향이 +) -> SDL 회전(시계방향이 +)
                // Entry 방향(0도 오른쪽, 90도 위쪽) -> SDL 각도 (0도 오른쪽, 90도 아래쪽)
                // 에셋 자체가 90도 회전되어 있을 수 있음 (ASSET_ROTATION_CORRECTION_RADIAN)
                // 여기서는 entityPtr->getRotation() 값을 그대로 사용합니다. (Entry의 rotation은 시계방향 +)
                // SDL_RenderTextureRotated의 angle은 시계방향 각도입니다.
                double sdlAngle = entityPtr->getRotation();

                // SDL_RenderTextureRotated 함수 호출하여 텍스처 그리기
                SDL_RenderTextureRotated(renderer, selectedCostume->imageHandle, nullptr, &dstRect, sdlAngle, &center, SDL_FLIP_NONE);
            }
        }
        else if (objInfo.objectType == "textBox")
        {
            // TODO: SDL_ttf를 사용하여 글상자 텍스트 렌더링 (drawHUD와 유사하게)
            // 폰트 이름(objInfo.fontName), 크기(objInfo.fontSize), 색상(objInfo.textColor), 내용(objInfo.textContent) 사용
            // 위치는 entityPtr->getX(), getY() 사용 (좌표계 변환 필요)
            // 텍스트 정렬(objInfo.textAlign) 구현 필요

            // 텍스트 내용이 비어있지 않았는지 확인.
            if (!objInfo.textContent.empty())
            {
                // 텍스트 Surface 생성
                // TTF_RenderText_Blended 또는 TTF_RenderText_Solid 사용
                // 폰트 사이즈 와 종류는 이렇게 로드한다.
                // "font": "bold 20px NotoSans"
                string fontString = objInfo.fontName;
                // 폰트 설정
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
                default: // 정의된 폰트 없으면 기본값.
                    determinedFontPath = fontAsset + "nanum_gothic.ttf";
                    break;
                }
                if (!determinedFontPath.empty())
                {
                    Usefont = TTF_OpenFont(determinedFontPath.c_str(), fontSize);
                    if (!Usefont) {
                        EngineStdOut("Failed to load font: " + determinedFontPath + " at size " + std::to_string(currentFontSize) + " for textBox '" + objInfo.name + "'. Falling back to HUD font.", 2);
                        Usefont = hudFont; // Fallback to the globally loaded HUD font
                    }
                    
                }else{
                    Usefont = hudFont; // Fallback to the globally loaded HUD font
                }
                
                SDL_Surface *textSurface = TTF_RenderText_Blended(Usefont, objInfo.textContent.c_str(), objInfo.textContent.size(), objInfo.textColor); // Blended가 더 부드러움
                if (textSurface)
                {
                    // Surface에서 텍스처 생성
                    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                    if (textTexture)
                    {
                        // 텍스트 위치 계산 (Entry 좌표계 -> SDL 좌표계 변환 필요)
                        double entryX = entityPtr->getX();
                        double entryY = entityPtr->getY();
                        float sdlX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                        float sdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                        // 텍스트의 너비와 높이
                        float textWidth = static_cast<float>(textSurface->w);
                        float textHeight = static_cast<float>(textSurface->h);

                        SDL_FRect dstRect = {sdlX, sdlY, textWidth, textHeight};

                        // textAlign 값에 따른 정렬 로직 구현 (예: 0=좌측, 1=중앙, 2=우측)
                        // sdlX를 기준으로 텍스트의 시작점을 조정합니다.
                        switch (objInfo.textAlign)
                        {
                        case 0: // 좌측 정렬: sdlX가 텍스트의 왼쪽 시작점
                            // dstRect.x는 이미 sdlX로 설정되어 있으므로 변경 없음
                            break;
                        case 1: // 중앙 정렬: sdlX가 텍스트의 중앙점
                            dstRect.x = sdlX - textWidth / 2.0f;
                            break;
                        case 2: // 우측 정렬: sdlX가 텍스트의 오른쪽 끝점
                            dstRect.x = sdlX - textWidth;
                            break;
                        default:
                            break;
                        }
                        // 텍스처 렌더링
                        SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);

                        // 텍스처 해제
                        SDL_DestroyTexture(textTexture);
                    }
                    else
                    {
                        EngineStdOut("Failed to create text texture for textBox '" + objInfo.name + "'. SDL_" + SDL_GetError(), 2);
                    }
                    // Surface 해제
                    SDL_DestroySurface(textSurface);
                }
                else
                {
                    EngineStdOut("Failed to render text surface for textBox '" + objInfo.name, 2);
                }
            }
        }
        // TODO: 다른 오브젝트 타입 (예: line, circle 등) 그리기 로직 추가
    }

    // 렌더링 대상을 다시 기본 윈도우 렌더러로 설정
    SDL_SetRenderTarget(renderer, nullptr);
    // 윈도우 배경을 검은색으로 클리어
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // 임시 화면 텍스처를 윈도우에 그립니다 (줌 적용)
    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH); // 현재 렌더러의 출력 크기 사용

    // 줌 팩터에 따라 임시 화면 텍스처의 어떤 부분을 가져올지 결정 (원본 스테이지 크기 기준)
    int srcWidth = static_cast<int>(PROJECT_STAGE_WIDTH / zoomFactor);
    int srcHeight = static_cast<int>(PROJECT_STAGE_HEIGHT / zoomFactor);

    // 가져올 원본 사각형 (임시 화면 텍스처 기준)
    SDL_Rect srcRect;
    srcRect.x = (PROJECT_STAGE_WIDTH - srcWidth) / 2;   // 중앙 정렬
    srcRect.y = (PROJECT_STAGE_HEIGHT - srcHeight) / 2; // 중앙 정렬
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    // 윈도우 전체에 그릴 목적지 사각형
    SDL_FRect dstFRect = {0.0f, 0.0f, static_cast<float>(windowW), static_cast<float>(windowH)}; // 윈도우 크기 사용

    // 원본 사각형도 SDL_FRect로 변환 (부동 소수점 정밀도 유지)
    SDL_FRect srcFRect = {static_cast<float>(srcRect.x), static_cast<float>(srcRect.y), static_cast<float>(srcRect.w), static_cast<float>(srcRect.h)};

    // 임시 화면 텍스처를 윈도우에 복사 (줌 적용)
    SDL_RenderTexture(renderer, tempScreenTexture, &srcFRect, &dstFRect);
}

// HUD (Head-Up Display) 요소 그리기 (FPS, 줌 슬라이더 등)
void Engine::drawHUD()
{
    // 렌더러가 유효한지 확인
    if (!this->renderer)
    {
        EngineStdOut("drawHUD: Renderer not available.", 1);
        return;
    }

    // 현재 윈도우 크기 가져오기 (HUD 요소 위치 계산에 사용)
    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH);

    // --- FPS 카운터 그리기 ---
    if (this->hudFont && this->specialConfig.showFPS)
    {
        // currentFps는 updateFps에서 계산됩니다.
        string fpsText = "FPS: " + to_string(static_cast<int>(currentFps));
        SDL_Color textColor = {255, 150, 0, 255}; // 주황색

        // 텍스트 Surface 생성 (SDL_ttf 사용)
        SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, fpsText.c_str(), fpsText.size(), textColor); // Blended 사용
        if (textSurface)
        {
            // Surface에서 텍스처 생성
            SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            if (textTexture)
            {
                // 화면 좌상단에 그릴 위치 및 크기 설정
                SDL_FRect dstRect = {10.0f, 10.0f, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};
                // 텍스처 렌더링
                SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);
                // 텍스처 해제
                SDL_DestroyTexture(textTexture);
            }
            else
            {
                EngineStdOut("Failed to create FPS text texture: " + string(SDL_GetError()), 2);
            }
            // Surface 해제
            SDL_DestroySurface(textSurface);
        }
        else
        {
            EngineStdOut("Failed to render FPS text surface ", 2); // TTF_GetError() 사용
        }
    }
    // --- 줌 슬라이더 UI 그리기 (설정에서 활성화된 경우) ---
    if (this->specialConfig.showZoomSlider)
    {
        // 슬라이더 배경 그리기
        // SLIDER_X, SLIDER_Y 등은 Engine.h에 정의되어 있어야 합니다.
        SDL_FRect sliderBgRect = {static_cast<float>(SLIDER_X), static_cast<float>(SLIDER_Y), static_cast<float>(SLIDER_WIDTH), static_cast<float>(SLIDER_HEIGHT)};
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); // 어두운 회색 배경
        SDL_RenderFillRect(renderer, &sliderBgRect);

        // 슬라이더 핸들 위치 계산 (현재 줌 팩터에 따라)
        float handleX_float = SLIDER_X + ((zoomFactor - MIN_ZOOM) / (MAX_ZOOM - MIN_ZOOM)) * SLIDER_WIDTH;
        float handleWidth_float = 8.0f; // 핸들 너비
        // 슬라이더 핸들 사각형
        SDL_FRect sliderHandleRect = {handleX_float - handleWidth_float / 2.0f, static_cast<float>(SLIDER_Y - 2), handleWidth_float, static_cast<float>(SLIDER_HEIGHT + 4)}; // 배경보다 약간 크게
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);                                                                                                                // 밝은 회색 핸들
        SDL_RenderFillRect(renderer, &sliderHandleRect);

        // 줌 팩터 값 텍스트 그리기
        if (this->hudFont)
        {
            std::ostringstream zoomStream;
            zoomStream << std::fixed << std::setprecision(2) << zoomFactor; // 소수점 둘째 자리까지 표시
            string zoomText = "Zoom: " + zoomStream.str();
            SDL_Color textColor = {220, 220, 220, 255}; // 밝은 회색 텍스트

            SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, zoomText.c_str(), zoomText.size(), textColor); // Blended 사용
            if (textSurface)
            {
                SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                if (textTexture)
                {
                    // 슬라이더 우측에 텍스트 위치
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

// 사용자 입력 처리 (마우스, 키보드 등)
void Engine::processInput(const SDL_Event& event)
{
    // 마우스 입력 처리 (줌 슬라이더)
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            int mouseX = event.button.x;
            int mouseY = event.button.y;
            if (this->specialConfig.showZoomSlider &&
                mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH &&
                mouseY >= SLIDER_Y - 5 && mouseY <= SLIDER_Y + SLIDER_HEIGHT + 5) {
                float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
                this->zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
                this->zoomFactor = std::max(MIN_ZOOM, std::min(MAX_ZOOM, this->zoomFactor));
                this->m_isDraggingZoomSlider = true; // 드래그 시작
            }

            //엔트리
            if (m_gameplayInputActive)
            {
                //오브젝트 클릭했을때 처리
            }
            
        }
    }
    else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (m_gameplayInputActive)
        {
            SDL_Scancode scancode = event.key.scancode;
            auto it = keyPressedScripts.find(scancode);
            if (it != keyPressedScripts.end()) {
                const auto& scriptsToRun = it->second;
                for (const auto& scriptPair : scriptsToRun) {
                    const string& objectId = scriptPair.first;
                    const Script* scriptPtr = scriptPair.second;
                    EngineStdOut(" -> Executing 'Key Pressed' script for object: " + objectId + " (Key: " + SDL_GetScancodeName(scancode) + ")", 0);
                    executeScript(*this, objectId, scriptPtr);
                }
            }
        }
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (this->m_isDraggingZoomSlider && (event.motion.state & SDL_BUTTON_LMASK)) { // 왼쪽 버튼이 눌린 상태로 움직일 때
            int mouseX = event.motion.x;
            // 슬라이더 범위 내에서만 업데이트
            if (mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH) {
                 float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
                 this->zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
                 this->zoomFactor = std::max(MIN_ZOOM, std::min(MAX_ZOOM, this->zoomFactor));
            }
        }
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            this->m_isDraggingZoomSlider = false; // 드래그 종료
        }
    }
    // TODO: 다른 이벤트 처리 (예: SDL_EVENT_WINDOW_RESIZED)
}

// Entry/Scratch 스타일 키 이름을 SDL_Scancode로 변환
SDL_Scancode Engine::mapStringToSDLScancode(const std::string& keyIdentifier) const
{
    // JavaScript 스타일 숫자 문자열 키 코드를 SDL_Scancode로 매핑하기 위한 정적 맵
    static const std::map<std::string, SDL_Scancode> jsKeyCodeMap = {
        {"8", SDL_SCANCODE_BACKSPACE},
        {"9", SDL_SCANCODE_TAB},
        {"13", SDL_SCANCODE_RETURN},
        // Shift, Ctrl, Alt는 JS에서 좌우 구분이 없으므로 기본적으로 왼쪽 키로 매핑합니다.
        {"16", SDL_SCANCODE_LSHIFT}, 
        {"17", SDL_SCANCODE_LCTRL},
        {"18", SDL_SCANCODE_LALT},
        {"27", SDL_SCANCODE_ESCAPE},
        {"32", SDL_SCANCODE_SPACE},
        {"37", SDL_SCANCODE_LEFT},
        {"38", SDL_SCANCODE_UP},
        {"39", SDL_SCANCODE_RIGHT},
        {"40", SDL_SCANCODE_DOWN},
        // 숫자 키 (JS keycode 48-57 for '0'-'9')
        {"48", SDL_SCANCODE_0}, {"49", SDL_SCANCODE_1}, {"50", SDL_SCANCODE_2},
        {"51", SDL_SCANCODE_3}, {"52", SDL_SCANCODE_4}, {"53", SDL_SCANCODE_5},
        {"54", SDL_SCANCODE_6}, {"55", SDL_SCANCODE_7}, {"56", SDL_SCANCODE_8},
        {"57", SDL_SCANCODE_9},
        // 알파벳 키 (JS keycode 65-90 for 'A'-'Z')
        {"65", SDL_SCANCODE_A}, {"66", SDL_SCANCODE_B}, {"67", SDL_SCANCODE_C},
        {"68", SDL_SCANCODE_D}, {"69", SDL_SCANCODE_E}, {"70", SDL_SCANCODE_F},
        {"71", SDL_SCANCODE_G}, {"72", SDL_SCANCODE_H}, {"73", SDL_SCANCODE_I},
        {"74", SDL_SCANCODE_J}, {"75", SDL_SCANCODE_K}, {"76", SDL_SCANCODE_L},
        {"77", SDL_SCANCODE_M}, {"78", SDL_SCANCODE_N}, {"79", SDL_SCANCODE_O},
        {"80", SDL_SCANCODE_P}, {"81", SDL_SCANCODE_Q}, {"82", SDL_SCANCODE_R},
        {"83", SDL_SCANCODE_S}, {"84", SDL_SCANCODE_T}, {"85", SDL_SCANCODE_U},
        {"86", SDL_SCANCODE_V}, {"87", SDL_SCANCODE_W}, {"88", SDL_SCANCODE_X},
        {"89", SDL_SCANCODE_Y}, {"90", SDL_SCANCODE_Z},
        // 특수 문자 키
        {"186", SDL_SCANCODE_SEMICOLON},      // ;
        {"187", SDL_SCANCODE_EQUALS},         // =
        {"188", SDL_SCANCODE_COMMA},          // ,
        {"189", SDL_SCANCODE_MINUS},          // -
        {"190", SDL_SCANCODE_PERIOD},         // .
        {"191", SDL_SCANCODE_SLASH},          // /
        {"192", SDL_SCANCODE_GRAVE},          // ` (Grave Accent)
        {"219", SDL_SCANCODE_LEFTBRACKET},   // [
        {"220", SDL_SCANCODE_BACKSLASH},      // \
        {"221", SDL_SCANCODE_RIGHTBRACKET},  // ]
        {"222", SDL_SCANCODE_APOSTROPHE}     // ' (or SDL_SCANCODE_QUOTE)
    };

    auto it = jsKeyCodeMap.find(keyIdentifier);
    if (it != jsKeyCodeMap.end()) {
        return it->second;
    }

    // 숫자 문자열 키 코드가 아닌 경우, SDL 표준 키 이름으로 시도
    SDL_Scancode sc = SDL_GetScancodeFromName(keyIdentifier.c_str());
    if (sc != SDL_SCANCODE_UNKNOWN) {
        return sc;
    }

    // 마지막으로, 단일 소문자 알파벳인 경우 대문자로 변환하여 다시 시도 (예: "a" -> "A")
    if (keyIdentifier.length() == 1) {
        char c = keyIdentifier[0];
        if (c >= 'a' && c <= 'z') {
            char upper_c = static_cast<char>(toupper(c));
            std::string upper_s(1, upper_c);
            return SDL_GetScancodeFromName(upper_s.c_str());
        }
    }

    return SDL_SCANCODE_UNKNOWN; // 매핑되는 키가 없는 경우
}

// '시작하기' 버튼 클릭 시 실행되는 스크립트들을 실행합니다.
void Engine::runStartButtonScripts()
{
    if (startButtonScripts.empty())
    {
        EngineStdOut("No 'Start Button Clicked' scripts found to run.", 1);
        return;
    }
    EngineStdOut("Running 'Start Button Clicked' scripts...", 0);
    // startButtonScripts 목록을 순회하며 각 스크립트 실행
    for (const auto &scriptPair : startButtonScripts)
    {
        const string &objectId = scriptPair.first;   // 스크립트가 속한 오브젝트 ID
        const Script *scriptPtr = scriptPair.second; // 실행할 스크립트 포인터

        EngineStdOut(" -> Running script for object: " + objectId, 3);
        // BlockExecutor.h/cpp에 정의된 executeScript 함수 호출
        // executeScript 함수는 Engine 인스턴스, 오브젝트 ID, 스크립트 포인터를 받아 스크립트를 실행합니다.
        // 이 함수는 스크립트 블록들을 순차적으로 처리하는 로직을 포함해야 합니다.
        executeScript(*this, objectId, scriptPtr);
        m_gameplayInputActive=true;
        EngineStdOut(" -> executeScript call is commented out. Script for object " + objectId + " was not executed.", 1);
    }
    EngineStdOut("Finished running 'Start Button Clicked' scripts.", 0);
}

// FPS 카운터 초기화
void Engine::initFps()
{
    lastfpstime = SDL_GetTicks(); // SDL_GetTicks()는 SDL 초기화 후 호출 가능
    framecount = 0;
    currentFps = 0.0f;
    EngineStdOut("FPS counter initialized.", 0);
}

// 목표 FPS 설정
void Engine::setfps(int fps)
{
    if (fps > 0)
    {
        this->specialConfig.TARGET_FPS = fps;
        EngineStdOut("Target FPS set to: " + std::to_string(this->specialConfig.TARGET_FPS), 0);
    }
    else
    {
        EngineStdOut("Attempted to set invalid Target FPS: " + std::to_string(fps) + ". Keeping current TARGET_FPS: " + std::to_string(this->specialConfig.TARGET_FPS), 1);
    }
}

// FPS 업데이트 (매 프레임 호출)
void Engine::updateFps()
{
    framecount++;
    Uint64 now = SDL_GetTicks();      // Uint64로 받음
    Uint64 delta = now - lastfpstime; // Uint64 간의 연산

    if (delta >= 1000)
    {
        currentFps = static_cast<float>(framecount * 1000.0) / delta;
        lastfpstime = now;
        framecount = 0;
    }
}

// ID로 Entity 객체 찾기
Entity *Engine::getEntityById(const string &id)
{
    auto it = entities.find(id); // entities 맵에서 ID로 검색
    if (it != entities.end())
    {
        return it->second; // 찾았으면 Entity 포인터 반환
    }
    // EngineStdOut("Entity with ID '" + id + "' not found.", 1); // 너무 많은 로그 방지
    return nullptr; // 찾지 못했으면 nullptr 반환
}

// 로딩 화면 렌더링
void Engine::renderLoadingScreen()
{
    // 렌더러가 유효한지 확인
    if (!this->renderer)
    {
        EngineStdOut("renderLoadingScreen: Renderer not available.", 1);
        return;
    }

    // 배경색 설정 및 클리어
    SDL_SetRenderDrawColor(this->renderer, 30, 30, 30, 255); // 어두운 회색 배경
    SDL_RenderClear(this->renderer);

    // 현재 윈도우 크기 가져오기 (UI 요소 위치 계산에 사용)
    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH); // 현재 렌더러의 출력 크기 사용

    // 로딩 바 크기 및 위치 설정
    int barWidth = 400;
    int barHeight = 30;
    int barX = (windowW - barWidth) / 2;  // 화면 중앙에 위치
    int barY = (windowH - barHeight) / 2; // 화면 중앙에 위치

    // 로딩 바 배경 (외곽선 역할) 그리기
    SDL_FRect bgRect = {static_cast<float>(barX), static_cast<float>(barY), static_cast<float>(barWidth), static_cast<float>(barHeight)};
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255); // 회색 외곽선
    SDL_RenderFillRect(renderer, &bgRect);

    // 로딩 바 내부 배경 그리기
    SDL_FRect innerBgRect = {static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(barWidth - 4), static_cast<float>(barHeight - 4)};
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); // 어두운 회색 내부
    SDL_RenderFillRect(renderer, &innerBgRect);

    // 로딩 진행률 계산 (0.0 ~ 1.0)
    float progressPercent = 0.0f;
    if (totalItemsToLoad > 0)
    {
        progressPercent = static_cast<float>(loadedItemCount) / totalItemsToLoad;
    }
    progressPercent = std::min(1.0f, std::max(0.0f, progressPercent)); // 0% ~ 100% 범위 제한

    // 로딩 진행 바 그리기
    int progressWidth = static_cast<int>((barWidth - 4) * progressPercent);
    SDL_FRect progressRect = {static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(progressWidth), static_cast<float>(barHeight - 4)};
    SDL_SetRenderDrawColor(renderer, 255, 165, 0, 255); // 주황색 진행 바
    SDL_RenderFillRect(renderer, &progressRect);

    // 텍스트 (퍼센트, 브랜드 이름, 프로젝트 이름) 그리기
    if (loadingScreenFont)
    {                                               // 로딩 화면 폰트가 로드되었는지 확인
        SDL_Color textColor = {220, 220, 220, 255}; // 밝은 회색 텍스트 색상

        // 퍼센트 텍스트 그리기 (예: "50%")
        std::ostringstream percentStream;
        percentStream << std::fixed << std::setprecision(0) << (progressPercent * 100.0f) << "%"; // 소수점 없이 정수 퍼센트 표시
        string percentText = percentStream.str();

        SDL_Surface *surfPercent = TTF_RenderText_Blended(loadingScreenFont, percentText.c_str(), percentText.size(), textColor); // Blended 사용
        if (surfPercent)
        {
            SDL_Texture *texPercent = SDL_CreateTextureFromSurface(renderer, surfPercent);
            if (texPercent)
            {
                // 로딩 바 우측에 퍼센트 텍스트 위치
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

        // 브랜드 이름 그리기 (설정에서 비어있지 않으면)
        if (!specialConfig.BRAND_NAME.empty())
        {
            SDL_Surface *surfBrand = TTF_RenderText_Blended(loadingScreenFont, specialConfig.BRAND_NAME.c_str(), specialConfig.BRAND_NAME.size(), textColor); // Blended 사용
            if (surfBrand)
            {
                SDL_Texture *texBrand = SDL_CreateTextureFromSurface(renderer, surfBrand);
                if (texBrand)
                {
                    // 로딩 바 상단 중앙에 브랜드 이름 위치
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

        // 프로젝트 이름 그리기 (설정 활성화 및 이름 비어있지 않으면)
        if (specialConfig.SHOW_PROJECT_NAME && !PROJECT_NAME.empty())
        {
            SDL_Surface *surfProject = TTF_RenderText_Blended(loadingScreenFont, PROJECT_NAME.c_str(), PROJECT_NAME.size(), textColor); // Blended 사용
            if (surfProject)
            {
                SDL_Texture *texProject = SDL_CreateTextureFromSurface(renderer, surfProject);
                if (texProject)
                {
                    // 로딩 바 하단 중앙에 프로젝트 이름 위치
                    SDL_FRect dstRect = {(windowW - static_cast<float>(surfProject->w)) / 2.0f, barY + barHeight + 10.0f, static_cast<float>(surfProject->w), static_cast<float>(surfProject->h)};
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

    // 렌더링된 내용을 화면에 표시
    SDL_RenderPresent(this->renderer);
}

// 현재 씬 ID 반환
const string &Engine::getCurrentSceneId() const
{
    return currentSceneId; // currentSceneId는 Engine 클래스의 멤버 변수여야 합니다.
}

/**
 * @brief 메시지 박스 표시
 *
 * @param message 메시지 내용
 * @param IconType 아이콘 종류
 * @param showYesNo 예 / 아니오
 */
bool Engine::showMessageBox(const string &message, int IconType, bool showYesNo)
{
    Uint32 flags = 0;
    const char *title = OMOCHA_ENGINE_NAME;

    // IconType에 따라 메시지 박스 플래그 및 제목 설정
    switch (IconType)
    {
    case SDL_MESSAGEBOX_ERROR:
        flags = SDL_MESSAGEBOX_ERROR;
        title = "Omocha is Broken"; // 오류 메시지 박스 제목
        break;
    case SDL_MESSAGEBOX_WARNING:
        flags = SDL_MESSAGEBOX_WARNING;
        title = PROJECT_NAME.c_str(); // 경고 메시지 박스 제목
        break;
    case SDL_MESSAGEBOX_INFORMATION:
        flags = SDL_MESSAGEBOX_INFORMATION;
        title = PROJECT_NAME.c_str(); // 정보 메시지 박스 제목
        break;
    default:
        // 알 수 없는 IconType 처리, 정보 메시지 또는 오류 로그로 기본 설정
        EngineStdOut("Unknown IconType passed to showMessageBox: " + std::to_string(IconType) + ". Using default INFORMATION.", 1);
        flags = SDL_MESSAGEBOX_INFORMATION; // 정보 메시지로 기본 설정
        title = "Message";                  // 기본 제목
        break;
    }
    // SDL_ShowSimpleMessageBox 함수 호출
    // this->window는 Engine 클래스의 멤버 변수 SDL_Window* 입니다.
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
void Engine::EngineStdOut(string s, int LEVEL)
{
    string prefix;
    // ANSI 컬러 코드는 Engine.h 또는 다른 공통 헤더에 정의되어 있어야 합니다.
    string color_code = ANSI_COLOR_RESET; // 기본값

    // 로그 레벨에 따른 접두사 및 색상 코드 설정
    switch (LEVEL)
    {
    case 0: // INFO (정보)
        prefix = "[INFO]";
        color_code = ANSI_COLOR_CYAN; // 시안색
        break;
    case 1: // WARN (경고)
        prefix = "[WARN]";
        color_code = ANSI_COLOR_YELLOW; // 노란색
        break;
    case 2: // ERROR (오류)
        prefix = "[ERROR]";
        color_code = ANSI_COLOR_RED; // 빨간색
        break;
    case 3: // DEBUG (디버그)
        prefix = "[DEBUG]";
        color_code = ANSI_STYLE_BOLD; // 색상 없이 굵게만
        break;
    case 4:
        prefix = "[SAYHELLO]";                            // 환영
        color_code = ANSI_COLOR_YELLOW + ANSI_STYLE_BOLD; // 노란색 + 굵게
        break;
    default: // 기본 로그 레벨
        prefix = "[LOG]";
        break;
    }

    // 콘솔 출력 (ANSI 컬러 코드 적용)
    // printf 함수를 사용하여 포맷팅된 문자열 출력
    printf("%s%s %s%s\n", color_code.c_str(), prefix.c_str(), s.c_str(), ANSI_COLOR_RESET.c_str());

    // 파일 로그 (색상 코드 없이)
    // logger 객체는 Engine 클래스의 멤버 변수이며, 로그 파일 관리를 담당해야 합니다.
    // logger.log 함수는 문자열을 받아 로그 파일에 기록하는 기능을 수행해야 합니다.
    string logMessage = format("{} {}", prefix, s); // C++20 format 사용
    logger.log(logMessage);                         // Logger 클래스의 log 함수 호출
}