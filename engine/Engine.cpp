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
#include <boost/asio/thread_pool.hpp> // 추가
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/writer.h"
#include "blocks/BlockExecutor.h"
#include "blocks/blockTypes.h" // Omocha 네임스페이스의 함수 사용을 위해 명시적 포함 (필요시)
#include <future>
#include <resource.h>
using namespace std;

const float Engine::MIN_ZOOM = 1.0f;
const float Engine::MAX_ZOOM = 3.0f;
const char *BASE_ASSETS = "assets/";
const float Engine::LIST_RESIZE_HANDLE_SIZE = 10.0f;
const float Engine::MIN_LIST_WIDTH = 80.0f;
const float Engine::MIN_LIST_HEIGHT = 60.0f;
const char *FONT_ASSETS = "font/";
string PROJECT_NAME;
string WINDOW_TITLE;
string LOADING_METHOD_NAME;
const double PI_VALUE = acos(-1.0);

static string RapidJsonValueToString(const rapidjson::Value &value)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

// Anonymous namespace for helper functions local to this file
namespace
{

    // Forward declaration for recursive use if ParseBlockDataInternal calls itself for nested statements.
    Block ParseBlockDataInternal(const rapidjson::Value &blockJson, Engine &engine, const std::string &contextForLog);

    Block ParseBlockDataInternal(const rapidjson::Value &blockJson, Engine &engine, const std::string &contextForLog)
    {
        Block newBlock; // Default constructor initializes paramsJson to kNullType

        // Parse ID
        newBlock.id = engine.getSafeStringFromJson(blockJson, "id", contextForLog, "", true, false);
        if (newBlock.id.empty())
        {
            // engine.EngineStdOut("ERROR: Block " + contextForLog + " has missing or empty 'id'. Cannot parse block.", 2); // getSafeStringFromJson logs critical
            return Block(); // Return an empty/invalid block (its id will be empty)
        }

        // Parse Type
        newBlock.type = engine.getSafeStringFromJson(blockJson, "type", contextForLog + " (id: " + newBlock.id + ")", "", true, false);
        if (newBlock.type.empty())
        {
            // engine.EngineStdOut("ERROR: Block " + contextForLog + " (id: " + newBlock.id + ") has missing or empty 'type'. Cannot parse block.", 2);
            return Block(); // Return an empty/invalid block
        }

        // Parse Params
        if (blockJson.HasMember("params"))
        {
            const rapidjson::Value &paramsVal = blockJson["params"];
            if (paramsVal.IsArray())
            { // paramsJson은 Document이므로 자체 Allocator 사용
                newBlock.paramsJson.CopyFrom(paramsVal, newBlock.paramsJson.GetAllocator());
            }
            else
            {                                                                                                                                                                                                                                 // params가 있지만 배열이 아닌 경우
                engine.EngineStdOut("WARN: Block " + contextForLog + " (id: " + newBlock.id + ", type: " + newBlock.type + ") has 'params' but it's not an array. Params will be empty. Value: " + RapidJsonValueToString(paramsVal), 1, ""); // Added empty thread ID
                newBlock.paramsJson.SetArray();
            }
        }
        else
        {
            newBlock.paramsJson.SetArray();
        } // params 필드가 없으면 빈 배열로 초기화
        newBlock.FilterNullsInParamsJsonArray();

        // Parse Statements (Inner Scripts)
        if (blockJson.HasMember("statements") && blockJson["statements"].IsArray())
        {
            const rapidjson::Value &statementsArray = blockJson["statements"];
            for (rapidjson::SizeType stmtIdx = 0; stmtIdx < statementsArray.Size(); ++stmtIdx)
            {
                const auto &statementStackJson = statementsArray[stmtIdx];
                if (statementStackJson.IsArray())
                {
                    Script innerScript;
                    std::string innerScriptContext = contextForLog + " statement " + std::to_string(stmtIdx);
                    for (rapidjson::SizeType innerBlockIdx = 0; innerBlockIdx < statementStackJson.Size(); ++innerBlockIdx)
                    {
                        const auto &innerBlockJsonVal = statementStackJson[innerBlockIdx];
                        if (innerBlockJsonVal.IsObject())
                        {
                            Block parsedInnerBlock = ParseBlockDataInternal(innerBlockJsonVal, engine, innerScriptContext + " inner_block " + std::to_string(innerBlockIdx));
                            if (!parsedInnerBlock.id.empty())
                            {
                                innerScript.blocks.push_back(std::move(parsedInnerBlock));
                            }
                        }
                        else
                        {
                            engine.EngineStdOut("WARN: Inner block in " + innerScriptContext + " at index " + std::to_string(innerBlockIdx) + " is not an object. Skipping. Content: " + RapidJsonValueToString(innerBlockJsonVal), 1, ""); // Added empty thread ID
                        }
                    }
                    if (!innerScript.blocks.empty())
                    {
                        newBlock.statementScripts.push_back(std::move(innerScript));
                    }
                    else
                    {
                        engine.EngineStdOut("DEBUG: Inner script " + innerScriptContext + " is empty or all its blocks were invalid.", 3, ""); // Added empty thread ID
                    }
                }
                else
                {
                    engine.EngineStdOut("WARN: Statement entry in " + contextForLog + " at index " + std::to_string(stmtIdx) + " is not an array (not a valid script stack). Skipping. Content: " + RapidJsonValueToString(statementStackJson), 1, ""); // Added empty thread ID
                }
            }
        }
        return newBlock;
    }

} // end anonymous namespace

Engine::Engine() : window(nullptr), renderer(nullptr),
                   tempScreenTexture(nullptr), totalItemsToLoad(0), loadedItemCount(0),
                   zoomFactor((this->specialConfig.setZoomfactor <= 0.0)
                                  ? 1.0f
                                  : std::clamp(static_cast<float>(this->specialConfig.setZoomfactor), Engine::MIN_ZOOM,
                                               Engine::MAX_ZOOM)),
                   m_isDraggingZoomSlider(false), m_pressedObjectId(""),
                   logger("omocha_engine.log"),
                   m_projectTimerValue(0.0), m_projectTimerRunning(false), m_gameplayInputActive(false),
                   m_scriptThreadPool(max(1u, std::thread::hardware_concurrency() > 0
                                                  ? std::thread::hardware_concurrency()
                                                  : 2)) // 스레드 풀 초기화 (최소 1개, 가능하면 CPU 코어 수만큼)
{
    EngineStdOut(
        string(OMOCHA_ENGINE_NAME) + " v" + string(OMOCHA_ENGINE_VERSION) + " " + string(OMOCHA_DEVELOPER_NAME), 4);
    EngineStdOut("See Project page " + string(OMOCHA_ENGINE_GITHUB), 4);
}

static void Helper_DrawFilledCircle(SDL_Renderer *renderer, int centerX, int centerY, int radius)
{
    if (!renderer || radius <= 0)
        return;
    for (int dy = -radius; dy <= radius; dy++)
    {
        int dx_limit = static_cast<int>(sqrt(static_cast<float>(radius * radius - dy * dy)));
        SDL_RenderLine(renderer, centerX - dx_limit, centerY + dy, centerX + dx_limit, centerY + dy);
    }
}

// Engine.cpp 상단 또는 유틸리티 함수 영역에 추가
SDL_Color hueToRGB(double H)
{
    // H는 0-359 범위의 색조(hue) 값
    H = std::fmod(H, 360.0);
    if (H < 0)
        H += 360.0;

    double S = 1.0; // 채도 (Saturation) - 여기서는 최대로 설정
    double V = 1.0; // 명도 (Value/Brightness) - 여기서는 최대로 설정

    int Hi = static_cast<int>(std::floor(H / 60.0)) % 6;
    double f = H / 60.0 - std::floor(H / 60.0);

    double p = V * (1.0 - S);
    double q = V * (1.0 - f * S);
    double t = V * (1.0 - (1.0 - f) * S);

    double r_norm = 0, g_norm = 0, b_norm = 0;

    switch (Hi)
    {
    case 0:
        r_norm = V;
        g_norm = t;
        b_norm = p;
        break;
    case 1:
        r_norm = q;
        g_norm = V;
        b_norm = p;
        break;
    case 2:
        r_norm = p;
        g_norm = V;
        b_norm = t;
        break;
    case 3:
        r_norm = p;
        g_norm = q;
        b_norm = V;
        break;
    case 4:
        r_norm = t;
        g_norm = p;
        b_norm = V;
        break;
    case 5:
        r_norm = V;
        g_norm = p;
        b_norm = q;
        break;
    }
    return {
        static_cast<Uint8>(std::clamp(r_norm * 255.0, 0.0, 255.0)),
        static_cast<Uint8>(std::clamp(g_norm * 255.0, 0.0, 255.0)),
        static_cast<Uint8>(std::clamp(b_norm * 255.0, 0.0, 255.0)),
        255 // Alpha는 여기서는 불투명으로 고정
    };
}

static void Helper_RenderFilledRoundedRect(SDL_Renderer *renderer, const SDL_FRect *rect, float radius)
{
    if (!renderer || !rect)
        return;

    float r = (radius < 0.0f) ? 0.0f : radius;

    float x = rect->x;
    float y = rect->y;
    float w = rect->w;
    float h = rect->h;

    if (r > w / 2.0f)
        r = w / 2.0f;
    if (r > h / 2.0f)
        r = h / 2.0f;

    if (w - 2 * r > 0)
    {
        SDL_FRect center_h_rect = {x + r, y, w - 2 * r, h};
        SDL_RenderFillRect(renderer, &center_h_rect);
    }
    if (h - 2 * r > 0)
    {
        SDL_FRect center_v_rect = {x, y + r, w, h - 2 * r};
        SDL_RenderFillRect(renderer, &center_v_rect);
    }

    if (r > 0)
    {
        Helper_DrawFilledCircle(renderer, static_cast<int>(x + r), static_cast<int>(y + r), static_cast<int>(r));
        Helper_DrawFilledCircle(renderer, static_cast<int>(x + w - r), static_cast<int>(y + r), static_cast<int>(r));
        Helper_DrawFilledCircle(renderer, static_cast<int>(x + r), static_cast<int>(y + h - r), static_cast<int>(r));
        Helper_DrawFilledCircle(renderer, static_cast<int>(x + w - r), static_cast<int>(y + h - r),
                                static_cast<int>(r));
    }
    else if (r == 0.0f)
    {
        SDL_RenderFillRect(renderer, rect);
    }
}

Engine::~Engine()
{
    EngineStdOut("Engine shutting down...");
    m_isShuttingDown.store(true, std::memory_order_relaxed); // 스레드들에게 종료 신호
    EngineStdOut("Shutting down script thread pool...");
    m_scriptThreadPool.stop();
    std::future<void> join_future = std::async(std::launch::async, [&]()
                                               { m_scriptThreadPool.join(); });
    EngineStdOut("Waiting for script threads to join...", 0);
    std::chrono::seconds timeout_duration(3);
    if (join_future.wait_for(timeout_duration) == std::future_status::timeout)
    {
        EngineStdOut("Timeout waiting for script threads to join. Some scripts might be stuck.", 2);
        showMessageBox("엔진 종료 중 스크립트 스레드 대기 시간이 초과되었습니다.\n일부 스크립트가 응답하지 않는 것 같습니다.", msgBoxIconType.ICON_WARNING);
        quick_exit(EXIT_FAILURE);
    }
    else
    {
        EngineStdOut("Script threads joined successfully.", 0);
    }
    terminateGE();
    EngineStdOut("Deleting entity objects...");
    entities.clear();
    objects_in_order.clear();
    EngineStdOut("Entity objects deleted.");
    objectScripts.clear();
    EngineStdOut("Object Script Clear");
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
        EngineStdOut(
            "Parent for field '" + fieldName + "' in " + contextDescription + " is not an object. Value: " +
                RapidJsonValueToString(parentValue),
            2);
        return defaultValue;
    }

    if (!parentValue.HasMember(fieldName.c_str()))
    {
        if (isCritical)
        {
            EngineStdOut(
                "Critical field '" + fieldName + "' missing in " + contextDescription + ". Using default: '" +
                    defaultValue + "'.",
                2);
        }

        return defaultValue;
    }

    const rapidjson::Value &fieldValue = parentValue[fieldName.c_str()];

    if (!fieldValue.IsString())
    {
        if (isCritical || !fieldValue.IsNull())
        {
            EngineStdOut(
                "Field '" + fieldName + "' in " + contextDescription + " is not a string. Value: [" +
                    RapidJsonValueToString(fieldValue) +
                    "]. Using default: '" + defaultValue + "'.",
                1);
        }
        return defaultValue;
    }

    string s_val = fieldValue.GetString(); // 문자열 값 가져오기
    if (s_val.empty() && !allowEmpty)
    {
        if (isCritical)
        {
            EngineStdOut(
                "Critical field '" + fieldName + "' in " + contextDescription +
                    " is an empty string, but empty is not allowed. Using default: '" + defaultValue + "'.",
                2);
        }
        else
        {
            EngineStdOut(
                "Field '" + fieldName + "' in " + contextDescription +
                    " is an empty string, but empty is not allowed. Using default: '" + defaultValue + "'.",
                1);
        }
        return defaultValue;
    }

    return s_val;
}

/**
 * @brief 프로젝트 로드
 * @param projectFilePath 프로젝트 파일 경로
 * @return true 성공 시
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
        EngineStdOut(
            "'speed' field missing or not numeric in project.json. Using default TARGET_FPS: " + to_string(
                                                                                                     this->specialConfig.TARGET_FPS),
            1);
    }
    /**
     * @brief 특수 설정 (specialConfig) 로드
     */
    if (document.HasMember("specialConfig"))
    {
        const rapidjson::Value &specialConfigJson = document["specialConfig"];
        if (specialConfigJson.IsObject())
        {
            this->specialConfig.BRAND_NAME = getSafeStringFromJson(specialConfigJson, "brandName", "specialConfig", "",
                                                                   false, true);
            EngineStdOut("Brand Name: " + (this->specialConfig.BRAND_NAME.empty()
                                               ? "[Not Set]"
                                               : this->specialConfig.BRAND_NAME),
                         0);

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
                this->specialConfig.setZoomfactor = clamp(specialConfigJson["setZoomfactor"].GetDouble(),
                                                          (double)Engine::MIN_ZOOM, (double)Engine::MAX_ZOOM);
            }
            else
            {
                this->specialConfig.setZoomfactor = 1.0f; // 원래배율
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
            if (specialConfigJson.HasMember("maxEntity") && specialConfigJson["maxEntity"].IsNumber())
            {
                this->specialConfig.MAX_ENTITY = specialConfigJson["maxEntity"].GetInt();
            }
            else
            {
                this->specialConfig.MAX_ENTITY = 100;
            }
        }
        this->zoomFactor = this->specialConfig.setZoomfactor;
    }
    /**
     * @brief Helper to get a boolean value from JSON or return a default
     */
    auto getJsonBool = [&](const rapidjson::Value &parentValue, const char *fieldName, bool defaultValue,
                           const std::string &contextForLog)
    {
        if (parentValue.HasMember(fieldName) && parentValue[fieldName].IsBool())
        {
            return parentValue[fieldName].GetBool();
        }
        this->EngineStdOut(
            "'" + contextForLog + "." + fieldName + "' field missing or not boolean. Using default: " + (defaultValue ? "true" : "false"), 1);
        return defaultValue;
    };
    /**
     * @brief Helper to get a double value from JSON, clamp it, or return a default
     */
    auto getJsonDoubleClamped = [&](const rapidjson::Value &parentValue, const char *fieldName, double defaultValue,
                                    double minVal, double maxVal, const std::string &contextForLog)
    {
        if (parentValue.HasMember(fieldName) && parentValue[fieldName].IsNumber())
        {
            return std::clamp(parentValue[fieldName].GetDouble(), minVal, maxVal);
        }
        this->EngineStdOut(
            "'" + contextForLog + "." + fieldName + "' field missing or not numeric. Using default: " +
                std::to_string(defaultValue),
            1);
        return defaultValue;
    };
    /**
     * @brief 전역 변수 (variables) 로드
     */
    if (document.HasMember("variables") && document["variables"].IsArray())
    {
        const rapidjson::Value &variablesJson = document["variables"];
        EngineStdOut("Found " + to_string(variablesJson.Size()) + " variables. Processing...", 0);

        for (rapidjson::SizeType i = 0; i < variablesJson.Size(); ++i)
        {
            const auto &variableJson = variablesJson[i];
            if (!variableJson.IsObject())
            {
                EngineStdOut(
                    "Variable entry at index " + to_string(i) + " is not an object. Skipping. Content: " +
                        RapidJsonValueToString(variableJson),
                    3);
                continue;
            }
            HUDVariableDisplay currentVarDisplay; // Create instance upfront

            currentVarDisplay.name = getSafeStringFromJson(variableJson, "name", "variable entry " + to_string(i), "",
                                                           true, false);
            if (currentVarDisplay.name.empty())
            {
                EngineStdOut("Variable name is empty for variable at index " + to_string(i) + ". Skipping variable.",
                             1);
                continue;
            }
            // 'value' 파싱을 위한 새 로직
            if (variableJson.HasMember("value"))
            {
                const auto &valNode = variableJson["value"];
                if (valNode.IsString())
                {
                    currentVarDisplay.value = valNode.GetString();
                }
                else if (valNode.IsNumber())
                {
                    currentVarDisplay.value = RapidJsonValueToString(valNode); // 숫자를 문자열로 변환
                }
                else if (valNode.IsBool())
                {
                    currentVarDisplay.value = valNode.GetBool() ? "true" : "false";
                }
                else if (valNode.IsNull())
                {
                    currentVarDisplay.value = ""; // null일 경우 기본값
                    EngineStdOut(
                        "Variable '" + currentVarDisplay.name +
                            "' has a null value. Interpreting as empty string for 'value' field.",
                        1);
                }
                else
                {
                    currentVarDisplay.value = ""; // 예상치 못한 다른 타입일 경우 기본값
                    EngineStdOut(
                        "Variable '" + currentVarDisplay.name +
                            "' has an unexpected type for 'value' field. Interpreting as empty string. Value: " +
                            RapidJsonValueToString(valNode),
                        1);
                }
            }
            else
            {
                currentVarDisplay.value = ""; // "value" 필드가 없을 경우 기본값
                EngineStdOut(
                    "Variable '" + currentVarDisplay.name + "' is missing 'value' field. Interpreting as empty string.",
                    1);
            }

            // If 'value' (now correctly stringified for numbers) is empty, skip the variable.
            // This fixes numeric 0 being skipped, as "0" is not empty.
            // It maintains skipping for actual empty strings, nulls, or missing values.
            if (currentVarDisplay.value.empty())
            {
                EngineStdOut(
                    "Variable value is effectively empty for variable '" + currentVarDisplay.name + "' at index " +
                        to_string(i) + ". Skipping variable.",
                    1);
                continue;
            }
            // 위에서 currentVarDisplay.value가 이미 올바르게 설정되었으므로, 아래 중복 코드는 제거합니다.
            currentVarDisplay.isVisible = false;
            if (variableJson.HasMember("visible") && variableJson["visible"].IsBool())
            {
                currentVarDisplay.isVisible = variableJson["visible"].GetBool();
            }
            else
            {
                EngineStdOut(
                    "'visible' field missing or not boolean for variable '" + currentVarDisplay.name +
                        "'. Using default: false",
                    2);
            }

            currentVarDisplay.variableType = getSafeStringFromJson(variableJson, "variableType",
                                                                   "variable entry " + to_string(i), "variable", false,
                                                                   true);

            /*
            TODO: variableType도 project.json에서 읽어오도록 수정해야 합니다. 현재는 기본값으로 "variable"을 사용합니다.
            Json 분석결과
            {
                "name": "variableName",
                "value": "variableValue",
                "visible": true,
                "object": "objectId" // objectId가 없으면 null로 처리됨. 이를 활용해 public/private 구분 가능
            }
            TODO: x,y 좌표도 추가
            */
            string variableType = getSafeStringFromJson(variableJson, "variableType", "variable entry " + to_string(i),
                                                        "variable", false, true);
            string objectId;
            if (variableJson.HasMember("object") && !variableJson["object"].IsNull())
            {
                currentVarDisplay.objectId = getSafeStringFromJson(variableJson, "object",
                                                                   "variable entry " + to_string(i), "", false, false);
            }
            else
            {
                currentVarDisplay.objectId = ""; // null이거나 없으면 빈 문자열로 기본값 설정
            }
            currentVarDisplay.x = variableJson.HasMember("x") && variableJson["x"].IsNumber()
                                      ? variableJson["x"].GetFloat()
                                      : 0.0f;
            currentVarDisplay.y = variableJson.HasMember("y") && variableJson["y"].IsNumber()
                                      ? variableJson["y"].GetFloat()
                                      : 0.0f;

            /*
            리스트 타입이 감지되면 해당데이터를 넣습니다.
            {
                "name": "variableName",
                "value": "variableValue",
                "visible": true,
                "object": "objectId", // objectId가 없으면 null로 처리됨. 이를 활용해 public/private 구분 가능
                "width": 0,
                "height": 0,
                "array": [
                    {
                        "key": "itemKey",
                        "data": "itemData"
                    }
                ]
            }
            */
            if (currentVarDisplay.variableType == "list")
            {
                // 너비와 높이 모두 float 타입
                if (variableJson.HasMember("width") && variableJson["width"].IsNumber())
                {
                    currentVarDisplay.width = variableJson["width"].GetFloat();
                }
                if (variableJson.HasMember("height") && variableJson["height"].IsNumber())
                {
                    currentVarDisplay.height = variableJson["height"].GetFloat();
                }
                if (variableJson.HasMember("array") && variableJson["array"].IsArray())
                {
                    const rapidjson::Value &arrayJson = variableJson["array"];
                    EngineStdOut(
                        "Found " + to_string(arrayJson.Size()) + " items in the list variable '" + currentVarDisplay.name + "'. Processing...", 0);
                    for (rapidjson::SizeType j = 0; j < arrayJson.Size(); ++j)
                    {
                        const auto &itemJson = arrayJson[j];
                        if (!itemJson.IsObject())
                        {
                            EngineStdOut(
                                "List item entry at index " + to_string(j) + " for list '" + currentVarDisplay.name +
                                    "' is not an object. Skipping. Content: " + RapidJsonValueToString(itemJson),
                                1);
                            continue;
                        }
                        ListItem item;
                        item.key = getSafeStringFromJson(itemJson, "key",
                                                         "list item entry " + to_string(j) + " for " + currentVarDisplay.name, "", false, true); // key는 빈 문자열 허용
                        item.data = getSafeStringFromJson(itemJson, "data",
                                                          "list item entry " + to_string(j) + " for " +
                                                              currentVarDisplay.name,
                                                          "", true,
                                                          false); // data는 중요하며, 이 호출에서 빈 문자열 비허용
                        currentVarDisplay.array.push_back(item);
                    }
                }
                else
                {
                    EngineStdOut(
                        "'array' field missing or not an array for variable '" + currentVarDisplay.name +
                            "'. Using default: empty array",
                        1);
                }
            }

            this->m_HUDVariables.push_back(currentVarDisplay); // 완전히 채워진 표시 객체 추가
            EngineStdOut(
                string("  Parsed variable: ") + currentVarDisplay.name + " = " + currentVarDisplay.value + " (Type: " +
                currentVarDisplay.variableType + ")");
        }
    }
    /**
     * @brief 오브젝트 (objects) 및 관련 데이터(모양, 소리, 스크립트) 로드
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
                EngineStdOut(
                    "Object entry at index " + to_string(i) + " is not an object. Skipping. Content: " +
                        RapidJsonValueToString(objectJson),
                    1);
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
            objInfo.name = getSafeStringFromJson(objectJson, "name", "object id: " + objectId, "Unnamed Object", false,
                                                 true);
            objInfo.objectType = getSafeStringFromJson(objectJson, "objectType", "object id: " + objectId, "sprite",
                                                       false, false);
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
                        ctu.id = getSafeStringFromJson(pictureJson, "id",
                                                       "costume entry " + to_string(j) + " for " + objInfo.name, "",
                                                       true, false);
                        if (ctu.id.empty())
                        {
                            EngineStdOut(
                                "Costume ID is empty for object " + objInfo.name + " at picture index " + to_string(j) +
                                    ". Skipping costume.",
                                2);
                            continue;
                        }
                        ctu.name = getSafeStringFromJson(pictureJson, "name", "costume id: " + ctu.id, "Unnamed Shape",
                                                         false, true);
                        ctu.filename = getSafeStringFromJson(pictureJson, "filename", "costume id: " + ctu.id, "", true,
                                                             false);
                        if (ctu.filename.empty())
                        {
                            EngineStdOut(
                                "Costume filename is empty for " + ctu.name + " (ID: " + ctu.id +
                                    "). Skipping costume.",
                                2);
                            continue;
                        }
                        ctu.fileurl = getSafeStringFromJson(pictureJson, "fileurl", "costume id: " + ctu.id, "", false,
                                                            true);

                        objInfo.costumes.push_back(ctu);
                        EngineStdOut(
                            "  Parsed costume: " + ctu.name + " (ID: " + ctu.id + ", File: " + ctu.filename + ")", 0);
                    }
                    else
                    {
                        EngineStdOut(
                            "Invalid picture structure for object '" + objInfo.name + "' at index " + to_string(j) +
                                ". Skipping.",
                            1);
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
                EngineStdOut(
                    "Found " + to_string(soundsJson.Size()) + " sounds for object " + objInfo.name + ". Parsing...", 0);

                for (rapidjson::SizeType j = 0; j < soundsJson.Size(); ++j)
                {
                    const auto &soundJson = soundsJson[j];
                    if (soundJson.IsObject() && soundJson.HasMember("id") && soundJson["id"].IsString() && soundJson.HasMember("filename") && soundJson["filename"].IsString())
                    {
                        SoundFile sound;
                        string soundId = getSafeStringFromJson(soundJson, "id",
                                                               "sound entry " + to_string(j) + " for " + objInfo.name,
                                                               "", true, false);
                        sound.id = soundId;
                        if (sound.id.empty())
                        {
                            EngineStdOut(
                                "Sound ID is empty for object " + objInfo.name + " at sound index " + to_string(j) +
                                    ". Skipping sound.",
                                2);
                            continue;
                        }
                        sound.name = getSafeStringFromJson(soundJson, "name", "sound id: " + sound.id, "Unnamed Sound",
                                                           false, true);
                        sound.filename = getSafeStringFromJson(soundJson, "filename", "sound id: " + sound.id, "", true,
                                                               false);
                        if (sound.filename.empty())
                        {
                            EngineStdOut(
                                "Sound filename is empty for " + sound.name + " (ID: " + sound.id +
                                    "). Skipping sound.",
                                2);
                            continue;
                        }
                        sound.fileurl = getSafeStringFromJson(soundJson, "fileurl", "sound id: " + sound.id, "", false,
                                                              true);
                        sound.ext = getSafeStringFromJson(soundJson, "ext", "sound id: " + sound.id, "", false, true);

                        double soundDuration = 0.0;
                        if (soundJson.HasMember("duration") && soundJson["duration"].IsNumber())
                        {
                            soundDuration = soundJson["duration"].GetDouble();
                        }
                        else
                        {
                            EngineStdOut(
                                "Sound '" + sound.name + "' (ID: " + sound.id +
                                    ") is missing 'duration' or it's not numeric. Using default duration 0.0.",
                                1);
                        }
                        sound.duration = soundDuration;
                        objInfo.sounds.push_back(sound);
                        EngineStdOut(
                            "  Parsed sound: " + sound.name + " (ID: " + sound.id + ", File: " + sound.filename + ")",
                            0);
                    }
                    else
                    {
                        EngineStdOut(
                            "Invalid sound structure for object '" + objInfo.name + "' at index " + to_string(j) +
                                ". Skipping.",
                            1);
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
                tempSelectedCostumeId = getSafeStringFromJson(objectJson, "selectedPictureId", "object " + objInfo.name,
                                                              "", false, false);
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (!selectedCostumeFound && objectJson.HasMember("selectedCostume") && objectJson["selectedCostume"].IsString())
            {
                tempSelectedCostumeId = getSafeStringFromJson(objectJson, "selectedCostume", "object " + objInfo.name,
                                                              "", false, false);
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (!selectedCostumeFound && objectJson.HasMember("selectedCostume") && objectJson["selectedCostume"].IsObject() &&
                objectJson["selectedCostume"].HasMember("id") && objectJson["selectedCostume"]["id"].IsString())
            {
                tempSelectedCostumeId = getSafeStringFromJson(objectJson["selectedCostume"], "id",
                                                              "object " + objInfo.name + " selectedCostume object", "",
                                                              false, false);
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (selectedCostumeFound)
            {
                objInfo.selectedCostumeId = tempSelectedCostumeId;
                EngineStdOut(
                    "Object '" + objInfo.name + "' (ID: " + objInfo.id + ") selected costume ID: " + objInfo.selectedCostumeId, 0);
            }
            else
            {
                if (!objInfo.costumes.empty())
                {
                    objInfo.selectedCostumeId = objInfo.costumes[0].id;
                    EngineStdOut(
                        "Object '" + objInfo.name + "' (ID: " + objInfo.id +
                            ") is missing selectedPictureId/selectedCostume or it's invalid. Using first costume ID: " +
                            objInfo.costumes[0].id,
                        1);
                }
                else
                {
                    EngineStdOut(
                        "Object '" + objInfo.name + "' (ID: " + objInfo.id +
                            ") is missing selectedPictureId/selectedCostume and has no costumes.",
                        1);
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
                            objInfo.textContent = getSafeStringFromJson(entityJson, "text", "textBox " + objInfo.name,
                                                                        "[DEFAULT TEXT]", false, true);
                            if (objInfo.textContent == "<OMOCHA_ENGINE_NAME>")
                            {
                                objInfo.textContent = string(OMOCHA_ENGINE_NAME);
                            }else if(objInfo.textContent == "<OMOCHA_DEVELOPER>"){
                                objInfo.textContent = "DEVELOPER: "+string(OMOCHA_DEVELOPER_NAME);
                            }else if(objInfo.textContent == "<OMOCHA_SDL_VERSION>"){
                                objInfo.textContent = "SDL VERSION: "+to_string(SDL_MAJOR_VERSION)+"."+to_string(SDL_MINOR_VERSION)+"."+to_string(SDL_MICRO_VERSION);
                            }else if(objInfo.textContent == "<OMOCHA_VERSION>"){
                                objInfo.textContent = "Engine Version: "+string(OMOCHA_ENGINE_VERSION);
                            }
                        }
                        else if (entityJson["text"].IsNumber())
                        {
                            objInfo.textContent = to_string(entityJson["text"].GetDouble());
                            EngineStdOut(
                                "INFO: textBox '" + objInfo.name + "' 'text' field is numeric. Converted to string: " +
                                    objInfo.textContent,
                                0);
                        }
                        else
                        {
                            objInfo.textContent = "[INVALID TEXT TYPE]";
                            EngineStdOut(
                                "textBox '" + objInfo.name + "' 'text' field has invalid type: " +
                                    RapidJsonValueToString(entityJson["text"]),
                                1);
                        }
                    }
                    else
                    {
                        objInfo.textContent = "[NO TEXT FIELD]";
                        EngineStdOut("textBox '" + objInfo.name + "' is missing 'text' field.", 1);
                    }

                    if (entityJson.HasMember("colour") && entityJson["colour"].IsString())
                    {
                        string hexColor = getSafeStringFromJson(entityJson, "colour", "textBox " + objInfo.name,
                                                                "#000000", false, false);
                        if (hexColor.length() == 7 && hexColor[0] == '#')
                        {
                            try
                            {
                                unsigned int r = stoul(hexColor.substr(1, 2), nullptr, 16);
                                unsigned int g = stoul(hexColor.substr(3, 2), nullptr, 16);
                                unsigned int b = stoul(hexColor.substr(5, 2), nullptr, 16);
                                objInfo.textColor = {(Uint8)r, (Uint8)g, (Uint8)b, 255};
                                EngineStdOut(
                                    "INFO: textBox '" + objInfo.name + "' text color parsed: R=" + to_string(r) + ", G=" + to_string(g) + ", B=" + to_string(b), 0);
                            }
                            catch (const exception &e)
                            {
                                EngineStdOut(
                                    "Failed to parse text color '" + hexColor + "' for object '" + objInfo.name + "': " + e.what() + ". Using default #000000.", 2);
                                objInfo.textColor = {0, 0, 0, 255};
                            }
                        }
                        else
                        {
                            EngineStdOut(
                                "textBox '" + objInfo.name + "' 'colour' field is not a valid HEX string (#RRGGBB): " +
                                    hexColor + ". Using default #000000.",
                                1);
                            objInfo.textColor = {0, 0, 0, 255};
                        }
                    }
                    else
                    {
                        EngineStdOut(
                            "textBox '" + objInfo.name +
                                "' is missing 'colour' field or it's not a string. Using default #000000.",
                            1);
                        objInfo.textColor = {0, 0, 0, 255};
                    }

                    if (entityJson.HasMember("font") && entityJson["font"].IsString())
                    {
                        string fontString = getSafeStringFromJson(entityJson, "font", "textBox " + objInfo.name,
                                                                  "20px NanumBarunpen", false, true);
                        // "bold 20px FontName" 또는 "20px FontName" 형식 처리
                        size_t firstSpace = fontString.find(' ');
                        std::string sizePart;
                        std::string namePart;

                        if (firstSpace != std::string::npos && fontString.substr(0, firstSpace) == "bold")
                        {
                            // "bold " 접두사 제거
                            std::string restOfString = fontString.substr(firstSpace + 1);
                            size_t secondSpace = restOfString.find(' ');
                            if (secondSpace != std::string::npos)
                            {
                                sizePart = restOfString.substr(0, secondSpace);
                                namePart = restOfString.substr(secondSpace + 1);
                            }
                            else
                            {                            // "bold 20pxFontName" (공백 없음) 또는 "bold FontName" (크기 없음)
                                sizePart = restOfString; // 일단 전체를 sizePart로
                            }
                        }
                        else
                        {
                            // "bold" 접두사 없음
                            size_t spacePos = fontString.find(' ');
                            if (spacePos != std::string::npos)
                            {
                                sizePart = fontString.substr(0, spacePos);
                                namePart = fontString.substr(spacePos + 1);
                            }
                            else
                            {                          // "20pxFontName" 또는 "FontName"
                                sizePart = fontString; // 일단 전체를 sizePart로
                            }
                        }

                        size_t pxPos = sizePart.find("px");
                        if (pxPos != std::string::npos)
                        {
                            try
                            {
                                objInfo.fontSize = std::stoi(sizePart.substr(0, pxPos));
                            }
                            catch (const std::exception &e)
                            {
                                objInfo.fontSize = 20;
                                EngineStdOut(
                                    "Failed to parse font size from '" + fontString + "' for textBox '" + objInfo.name +
                                        "'. Using default size 20.",
                                    1);
                            }
                            objInfo.fontName = namePart.empty() ? sizePart.substr(pxPos + 2) : namePart;
                        }
                        else
                        {
                            objInfo.fontSize = 20;
                            objInfo.fontName = fontString;
                            EngineStdOut(
                                "textBox '" + objInfo.name + "' 'font' field is not in 'size px Name' format: '" +
                                    fontString + "'. Using default size 20 and '" + objInfo.fontName + "' as name.",
                                1);
                        }
                        // 폰트 이름 앞뒤 공백 제거
                        objInfo.fontName.erase(0, objInfo.fontName.find_first_not_of(" \t\n\r\f\v"));
                        objInfo.fontName.erase(objInfo.fontName.find_last_not_of(" \t\n\r\f\v") + 1);
                        EngineStdOut("INFO: textBox '" + objInfo.name + "' parsed font size: " + std::to_string(objInfo.fontSize) + ", name: '" + objInfo.fontName + "'", 0);
                    }
                    else
                    {
                        EngineStdOut(
                            "textBox '" + objInfo.name +
                                "' is missing 'font' field or it's not a string. Using default size 20 and empty font name.",
                            1);
                        objInfo.fontSize = 20;
                        objInfo.fontName = "";
                    }

                    if (entityJson.HasMember("textAlign") && entityJson["textAlign"].IsNumber())
                    {
                        int parsedAlign = entityJson["textAlign"].GetInt();
                        if (parsedAlign >= 0 && parsedAlign <= 2)
                        {
                            // 0: left, 1: center, 2: right
                            objInfo.textAlign = parsedAlign;
                        }
                        else
                        {
                            objInfo.textAlign = 0; // Default to left align for invalid values
                            EngineStdOut(
                                "textBox '" + objInfo.name + "' 'textAlign' field has an invalid value: " +
                                    to_string(parsedAlign) + ". Using default alignment 0 (left).",
                                1);
                        }
                        EngineStdOut(
                            "INFO: textBox '" + objInfo.name + "' text alignment parsed: " + to_string(objInfo.textAlign), 0);
                    }
                    else
                    {
                        objInfo.textAlign = 0;
                        EngineStdOut(
                            "textBox '" + objInfo.name +
                                "' is missing 'textAlign' field or it's not numeric. Using default alignment 0.",
                            1);
                    }
                }
                else
                {
                    EngineStdOut(
                        "textBox '" + objInfo.name +
                            "' is missing 'entity' block or it's not an object. Cannot load text box properties.",
                        1);
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

                double initial_x = entityJson.HasMember("x") && entityJson["x"].IsNumber()
                                       ? entityJson["x"].GetDouble()
                                       : 0.0;
                double initial_y = entityJson.HasMember("y") && entityJson["y"].IsNumber()
                                       ? entityJson["y"].GetDouble()
                                       : 0.0;
                double initial_regX = entityJson.HasMember("regX") && entityJson["regX"].IsNumber()
                                          ? entityJson["regX"].GetDouble()
                                          : 0.0;
                double initial_regY = entityJson.HasMember("regY") && entityJson["regY"].IsNumber()
                                          ? entityJson["regY"].GetDouble()
                                          : 0.0;
                double initial_scaleX = entityJson.HasMember("scaleX") && entityJson["scaleX"].IsNumber()
                                            ? entityJson["scaleX"].GetDouble()
                                            : 1.0;
                double initial_scaleY = entityJson.HasMember("scaleY") && entityJson["scaleY"].IsNumber()
                                            ? entityJson["scaleY"].GetDouble()
                                            : 1.0;
                double initial_rotation = entityJson.HasMember("rotation") && entityJson["rotation"].IsNumber()
                                              ? entityJson["rotation"].GetDouble()
                                              : 0.0;
                double initial_direction = entityJson.HasMember("direction") && entityJson["direction"].IsNumber()
                                               ? entityJson["direction"].GetDouble()
                                               : 90.0;
                double initial_width = entityJson.HasMember("width") && entityJson["width"].IsNumber()
                                           ? entityJson["width"].GetDouble()
                                           : 100.0;
                double initial_height = entityJson.HasMember("height") && entityJson["height"].IsNumber()
                                            ? entityJson["height"].GetDouble()
                                            : 100.0;
                bool initial_visible = entityJson.HasMember("visible") && entityJson["visible"].IsBool()
                                           ? entityJson["visible"].GetBool()
                                           : true;
                Entity::RotationMethod currentRotationMethod = Entity::RotationMethod::FREE;
                if (objectJson.HasMember("rotationMethod") && objectJson["rotationMethod"].IsString())
                {
                    // 이정도 있는것으로 예상됨 하지만 발견 못한것 도 있을수 있음
                    string rotationMethodStr = getSafeStringFromJson(objectJson, "rotationMethod",
                                                                     "object " + objInfo.name, "free", false, true);
                    if (rotationMethodStr == "free")
                    {
                        currentRotationMethod = Entity::RotationMethod::FREE;
                    }
                    else if (rotationMethodStr == "none")
                    {
                        currentRotationMethod = Entity::RotationMethod::NONE;
                    }
                    else if (rotationMethodStr == "vertical")
                    {
                        currentRotationMethod = Entity::RotationMethod::VERTICAL;
                    }
                    else if (rotationMethodStr == "horizontal")
                    {
                        currentRotationMethod = Entity::RotationMethod::HORIZONTAL;
                    }
                    else
                    {
                        EngineStdOut(
                            "Invalid rotation method '" + rotationMethodStr + "' for object '" + objInfo.name +
                                "'. Using default 'free'.",
                            1);
                        currentRotationMethod = Entity::RotationMethod::FREE;
                    }
                }
                else
                {
                    EngineStdOut(
                        "Missing or invalid 'rotationMethod' for object '" + objInfo.name + "'. Using default 'free'.",
                        1);
                }

                Entity *newEntity = new Entity(
                    this, // Pass the engine instance
                    objectId,
                    objInfo.name,
                    initial_x, initial_y, initial_regX, initial_regY,
                    initial_scaleX, initial_scaleY, initial_rotation, initial_direction,
                    initial_width, initial_height, initial_visible, currentRotationMethod);

                // Initialize pen positions
                newEntity->brush.reset(initial_x, initial_y);
                newEntity->paint.reset(initial_x, initial_y);
                lock_guard<mutex> lock(m_engineDataMutex);
                entities[objectId] = newEntity;
                // newEntity->startLogicThread();
                EngineStdOut("INFO: Created Entity for object ID: " + objectId, 0);
            }
            else
            {
                EngineStdOut(
                    "Object '" + objInfo.name + "' (ID: " + objectId +
                        ") is missing 'entity' block or it's not an object. Cannot create Entity.",
                    1);
            }
            /**
             * @brief 스크립트 블럭
             *
             */
            if (objectJson.HasMember("script") && objectJson["script"].IsString())
            {
                string scriptString = getSafeStringFromJson(objectJson, "script", "object " + objInfo.name, "", false,
                                                            true);
                if (scriptString.empty())
                {
                    EngineStdOut(
                        "INFO: Object '" + objInfo.name +
                            "' has an empty 'script' string. No scripts will be loaded for this object.",
                        0);
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
                                    EngineStdOut(
                                        "  Parsing script stack " + to_string(j + 1) + "/" + to_string(scriptDocument.Size()) + " for object " + objInfo.name, 0);

                                    for (rapidjson::SizeType k = 0; k < scriptStackJson.Size(); ++k)
                                    {
                                        const auto &blockJsonValue = scriptStackJson[k]; // Renamed to avoid conflict
                                        string blockContext =
                                            "block at index " + to_string(k) + " in script stack " +
                                            to_string(j + 1) + " for object " + objInfo.name;

                                        if (blockJsonValue.IsObject())
                                        {
                                            // Use the new helper function to parse the block, including its statements
                                            Block parsedBlock = ParseBlockDataInternal(blockJsonValue, *this, blockContext);
                                            if (!parsedBlock.id.empty())
                                            { // Check if parsing was successful (id is a good indicator)
                                                currentScript.blocks.push_back(std::move(parsedBlock));
                                                // Log for the successfully parsed top-level block
                                                EngineStdOut(
                                                    "    Parsed block: id='" + parsedBlock.id + "', type='" + parsedBlock.type + "'",
                                                    0);
                                            }
                                        }
                                        else
                                        {
                                            EngineStdOut(
                                                "WARN: Invalid block structure (not an object) in " + blockContext +
                                                    ". Skipping block. Content: " + RapidJsonValueToString(blockJsonValue),
                                                1);
                                        }
                                    }
                                    if (!currentScript.blocks.empty())
                                    {
                                        scriptsForObject.push_back(currentScript);
                                    }
                                    else
                                    {
                                        EngineStdOut(
                                            "  WARN: Script stack " + to_string(j + 1) + " for object " + objInfo.name +
                                                " resulted in an empty script (e.g., all blocks were invalid). Skipping this stack.",
                                            1);
                                    }
                                }
                                else
                                {
                                    EngineStdOut(
                                        "WARN: Script entry at index " + to_string(j) + " for object '" + objInfo.name +
                                            "' is not an array of blocks (not a valid script stack). Skipping this script stack. Content: " + RapidJsonValueToString(scriptStackJson),
                                        1);
                                }
                            }
                            objectScripts[objectId] = scriptsForObject;
                            EngineStdOut(
                                "INFO: Parsed " + to_string(scriptsForObject.size()) + " script stacks for object ID: " + objectId, 0);
                        }
                        else
                        {
                            EngineStdOut(
                                "WARN: Script root for object '" + objInfo.name +
                                    "' (after parsing string) is not an array of script stacks. Skipping script parsing. Content: " + RapidJsonValueToString(scriptDocument),
                                1);
                        }
                    }
                    else
                    {
                        string scriptErrorMsg = string("ERROR: Failed to parse script JSON string for object '") +
                                                objInfo.name + "': " +
                                                rapidjson::GetParseError_En(scriptDocument.GetParseError()) +
                                                " (Offset: " + to_string(scriptDocument.GetErrorOffset()) + ")";
                        EngineStdOut(scriptErrorMsg, 2);
                        showMessageBox(
                            "Failed to parse script JSON string for object '" + objInfo.name +
                                "'. Project loading aborted.",
                            msgBoxIconType.ICON_ERROR);
                        return false;
                    }
                }
            }
            else
            {
                EngineStdOut(
                    "INFO: Object '" + objInfo.name +
                        "' is missing 'script' field or it's not a string. No scripts will be loaded for this object.",
                    0);
            }
        }
    }
    else
    {
        EngineStdOut("project.json is missing 'objects' array or it's not an array.", 1);
        showMessageBox("project.json is missing 'objects' array or it's not an array.\nBrokenProject.",
                       msgBoxIconType.ICON_ERROR);
        return false;
    }

    scenes.clear();

    /**
     * @brief 씬 (scenes) 정보 로드
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
                EngineStdOut(
                    "Scene entry at index " + to_string(i) + " is not an object. Skipping. Content: " +
                        RapidJsonValueToString(sceneJson),
                    1);
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
                string sceneName = getSafeStringFromJson(sceneJson, "name", "scene id: " + sceneId, "Unnamed Scene",
                                                         false, true);
                scenes[sceneId] = sceneName;
                m_sceneOrder.push_back(sceneId);
                EngineStdOut("  Parsed scene: " + sceneName + " (ID: " + sceneId + ")", 0);
            }
            else
            {
                EngineStdOut(
                    "Invalid scene structure or 'id'/'name' fields missing/not strings for scene at index " +
                        to_string(i) + ". Skipping.",
                    1);
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
        startSceneId = getSafeStringFromJson(document, "startScene", "project root for startScene (legacy)", "", false,
                                             false);
        EngineStdOut("'startScene' (legacy) found in project.json: " + startSceneId, 0);
    }
    else if (document.HasMember("start") && document["start"].IsObject() && document["start"].HasMember("sceneId") &&
             document["start"]["sceneId"].IsString())
    {
        startSceneId = getSafeStringFromJson(document["start"], "sceneId", "project root start object", "", false,
                                             false);
        EngineStdOut("'start/sceneId' found in project.json: " + startSceneId, 0);
    }
    else
    {
        EngineStdOut("No explicit 'startScene' or 'start/sceneId' found in project.json.", 1);
    }

    if (!startSceneId.empty() && scenes.count(startSceneId))
    {
        currentSceneId = startSceneId;
        EngineStdOut(
            "Initial scene set to explicit start scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")",
            0);
    }
    else
    {
        if (!m_sceneOrder.empty() && scenes.count(m_sceneOrder.front()))
        {
            currentSceneId = m_sceneOrder.front();
            EngineStdOut(
                "Initial scene set to first scene in array order: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        }
        else
        {
            EngineStdOut("No valid starting scene found in project.json or no scenes were loaded.", 2);
            currentSceneId = "";
            return false;
        }
    }
    /**
     * @brief 특정 이벤트에 연결될 스크립트 식별 (예: 시작 버튼 클릭, 키 입력, 메시지 수신 등)
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
                        EngineStdOut(
                            "  -> Found 'Start Button Clicked' script for object ID: " + objectId +
                                " but it has no subsequent blocks. Skipping.",
                            1);
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
                            else if (firstBlock.paramsJson[0].IsNull() && firstBlock.paramsJson.Size() > 1 &&
                                     firstBlock.paramsJson[1].IsString())
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
                                EngineStdOut(
                                    " -> object ID " + objectId +
                                        " 'press key' invalid param or missing message ID. Params JSON: " + string(buffer.GetString()) + ".",
                                    1);
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
                        EngineStdOut(
                            "  -> object ID " + objectId + " found 'When I release the click on an object' script.", 0);
                    }
                }
                else if (firstBlock.type == "when_message_cast")
                {
                    if (script.blocks.size() > 1)
                    {
                        // Log details of what's being checked and extracted
                        EngineStdOut("DEBUG_MSG: Processing when_message_cast for " + objectId + ". First block ID: " + firstBlock.id, 3, "");
                        bool isArr = firstBlock.paramsJson.IsArray();
                        int arrSize = isArr ? static_cast<int>(firstBlock.paramsJson.Size()) : -1; // SizeType to int
                        bool firstIsStr = (isArr && arrSize >= 1) ? firstBlock.paramsJson[0].IsString() : false;
                        EngineStdOut("DEBUG_MSG:   paramsJson.IsArray(): " + std::string(isArr ? "true" : "false") +
                                         ", Size: " + std::to_string(arrSize) +
                                         ", [0].IsString(): " + std::string(firstIsStr ? "true" : "false"),
                                     3, "");

                        string messageIdToReceive;
                        bool messageParamFound = false;

                        // After FilterNullsInParamsJsonArray, the signal ID should be the first element if present.
                        if (firstBlock.paramsJson.IsArray() && firstBlock.paramsJson.Size() >= 1 &&
                            firstBlock.paramsJson[0].IsString()) // Check size >= 1 and access index 0
                        {
                            messageIdToReceive = firstBlock.paramsJson[0].GetString(); // Get signal ID from index 0
                            EngineStdOut("DEBUG_MSG:   Extracted messageIdToReceive: '" + messageIdToReceive + "'", 3, "");
                            messageParamFound = true;
                        }

                        if (messageParamFound && !messageIdToReceive.empty())
                        {
                            m_messageReceivedScripts[messageIdToReceive].push_back({objectId, &script});
                            EngineStdOut("  -> object ID " + objectId + " " + messageIdToReceive + " message found.",
                                         0);
                        }
                        else
                        {
                            rapidjson::StringBuffer buffer;
                            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                            firstBlock.paramsJson.Accept(writer);
                            EngineStdOut(
                                " -> object ID " + objectId +
                                    " 'recive signal' invalid param or missing message ID. Params JSON: " + string(buffer.GetString()) + ".",
                                1);
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
    EngineStdOut("Finished identifying 'Start Button Clicked' scripts. Found: " + to_string(startButtonScripts.size()),
                 0);

    EngineStdOut("Project JSON file parsed successfully.", 0);
    return true;
}

const ObjectInfo *Engine::getObjectInfoById(const string &id) const
{
    // objects_in_order is a vector, so we need to iterate.
    // This could be optimized if access becomes frequent by creating a map during loadProject.
    for (const auto &objInfo : objects_in_order)
    {
        if (objInfo.id == id)
        {
            return &objInfo;
        }
    }
    return nullptr;
}

bool Engine::initGE(bool vsyncEnabled, bool attemptVulkan)
{
    EngineStdOut("Initializing SDL...", 0);
    if (SDL_Init(SDL_INIT_VIDEO) < 0) // 오디오 부분은 Audio.h에서 초기화
    {
        // SDL 비디오 서브시스템 초기화
        EngineStdOut("SDL could not initialize! SDL_" + string(SDL_GetError()), 2);
        showMessageBox("Failed to initialize SDL: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    EngineStdOut("SDL video subsystem initialized successfully.", 0);

    if (TTF_Init() == -1)
    {
        // SDL_ttf (폰트 렌더링 라이브러리) 초기화
        string errMsg = "SDL_ttf could not initialize!";
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to initialize", msgBoxIconType.ICON_ERROR);

        SDL_Quit();
        return false;
    }
    EngineStdOut("SDL_ttf initialized successfully.", 0);

    this->window = SDL_CreateWindow(WINDOW_TITLE.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (this->window == nullptr) // SDL 윈도우 생성
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
        // Vulkan 렌더러 생성 시도

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
                EngineStdOut(
                    "Failed to create Vulkan renderer even though driver was found: " + string(SDL_GetError()) +
                        ". Falling back to default.",
                    1);
                showMessageBox(
                    "Failed to create Vulkan renderer: " + string(SDL_GetError()) + ". Falling back to default.",
                    msgBoxIconType.ICON_WARNING);
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
        // 기본 SDL 렌더러 생성
        this->renderer = SDL_CreateRenderer(this->window, nullptr);
    }
    if (this->renderer == nullptr)
    {
        string errMsg = "Renderer could not be created! SDL_" + string(SDL_GetError()); // 렌더러 생성 실패
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create renderer: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    EngineStdOut("SDL Renderer created successfully.", 0);
    if (SDL_SetRenderVSync(this->renderer,
                           vsyncEnabled ? SDL_RENDERER_VSYNC_ADAPTIVE : SDL_RENDERER_VSYNC_DISABLED) != 0)
    {
        // VSync 설정
        EngineStdOut("Failed to set VSync mode. SDL_" + string(SDL_GetError()), 1);
    }
    else
    {
        EngineStdOut("VSync mode set to: " + string(vsyncEnabled ? "Adaptive" : "Disabled"), 0);
    }

    string defaultFontPath = "font/nanum_gothic.ttf"; // 기본 폰트 경로
    hudFont = TTF_OpenFont(defaultFontPath.c_str(), 20);
    loadingScreenFont = TTF_OpenFont(defaultFontPath.c_str(), 30);
    percentFont = TTF_OpenFont(defaultFontPath.c_str(), 15);

    if (!hudFont)
    {
        // HUD 폰트 로드 실패
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
    if (!percentFont)
    {
        string errMsg = "Failed to load percent font! Font path: " + defaultFontPath;
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
    EngineStdOut("HUD font loaded successfully.", 0);

    if (!loadingScreenFont)
    {
        // 로딩 화면 폰트 로드 실패
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

    initFps(); // FPS 카운터 초기화

    if (!createTemporaryScreen())
    {
        // 임시 화면 텍스처 생성 실패
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

    // --- Initial HUD Variable Position Clamping ---
    // project.json에서 로드된 x, y 좌표가 엔트리 좌표계 기준일 수 있으므로,
    // SDL 창 내에 있도록 초기 위치를 제한(clamp)합니다.
    if (renderer && !m_HUDVariables.empty())
    {
        // 렌더러와 HUD 변수가 모두 유효할 때만 실행
        EngineStdOut("Performing initial HUD variable position clamping...", 0);
        int windowW = 0, windowH = 0;
        SDL_GetRenderOutputSize(renderer, &windowW, &windowH);

        float screenCenterX = static_cast<float>(windowW) / 2.0f;
        float screenCenterY = static_cast<float>(windowH) / 2.0f;

        if (windowW > 0 && windowH > 0)
        {
            float itemHeight_const = 22.0f; // drawHUD 및 processInput과 일치
            float itemPadding_const = 3.0f; // drawHUD 및 processInput과 일치하는 아이템 패딩
            float clampedItemHeight = itemHeight_const + 2 * itemPadding_const;
            // drawHUD/processInput의 minContainerFixedWidth와 일치시키거나 적절한 기본값 사용
            float minContainerFixedWidth_const = 80.0f; // 컨테이너 최소 고정 너비

            for (auto &var : m_HUDVariables)
            {
                // x, y를 수정해야 하므로 참조로 반복
                if (!var.isVisible)
                    continue;

                float currentItemEstimatedWidth;
                if (var.variableType == "list" && var.width > 0)
                {
                    // 리스트이고 project.json에 너비가 지정된 경우 해당 너비 사용
                    currentItemEstimatedWidth = max(minContainerFixedWidth_const, var.width);
                }
                else
                {
                    // 일반 변수 또는 너비가 지정되지 않은 리스트의 경우, m_maxVariablesListContentWidth의 기본값을 사용
                    // m_maxVariablesListContentWidth는 drawHUD에서 계산되지만, 초기에는 기본값(180.0f)을 가짐
                    currentItemEstimatedWidth = m_maxVariablesListContentWidth;
                }

                // 추정된 너비가 창 너비보다 크지 않도록 하고, 최소 너비는 보장합니다.
                currentItemEstimatedWidth = min(currentItemEstimatedWidth, static_cast<float>(windowW) - 10.0f);
                // 창 오른쪽 10px 여유
                currentItemEstimatedWidth = max(minContainerFixedWidth_const, currentItemEstimatedWidth);

                // 엔트리 좌표를 스크린 좌표로 변환
                float currentItemScreenX = screenCenterX + var.x;
                float currentItemScreenY = screenCenterY - var.y; // var.y는 요소의 상단 Y좌표 (엔트리 기준)

                // 스크린 좌표에서 클램핑
                float clampedScreenX = clamp(currentItemScreenX, 0.0f,
                                             static_cast<float>(windowW) - currentItemEstimatedWidth);
                float clampedScreenY = clamp(currentItemScreenY, 0.0f, static_cast<float>(windowH) - clampedItemHeight);
                // 클램핑된 스크린 좌표를 다시 엔트리 좌표로 변환하여 저장
                var.x = clampedScreenX - screenCenterX;
                var.y = screenCenterY - clampedScreenY;
                EngineStdOut(
                    "Clamped initial Entry pos for '" + var.name + "' to X=" + to_string(var.x) + ", Y=" +
                        to_string(var.y) + " (Screen TL: " + to_string(clampedScreenX) + "," + to_string(clampedScreenY) +
                        ", Est. W=" + to_string(currentItemEstimatedWidth) + ", H=" + to_string(clampedItemHeight) + ")",
                    3);
            }
        }
        else
        {
            EngineStdOut(
                "Window dimensions (W:" + to_string(windowW) + ", H:" + to_string(windowH) +
                    ") not valid for initial HUD clamping.",
                1);
        }
    }
    return true;
}

bool Engine::createTemporaryScreen()
{
    if (this->renderer == nullptr)
    {
        EngineStdOut("Renderer not initialized. Cannot create temporary screen texture.", 2); // 렌더러가 초기화되지 않음
        showMessageBox("Internal Renderer not available for offscreen buffer.", msgBoxIconType.ICON_ERROR);
        return false;
    }

    this->tempScreenTexture = SDL_CreateTexture(this->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                                PROJECT_STAGE_WIDTH, PROJECT_STAGE_HEIGHT);
    if (this->tempScreenTexture == nullptr)
    {
        string errMsg = "Failed to create temporary screen texture! SDL_" + string(SDL_GetError()); // 임시 화면 텍스처 생성 실패
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create offscreen buffer: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    EngineStdOut(
        "Temporary screen texture created successfully (" + to_string(PROJECT_STAGE_WIDTH) + "x" + to_string(PROJECT_STAGE_HEIGHT) + ").", 0);
    return true;
}

void Engine::destroyTemporaryScreen()
{
    if (this->tempScreenTexture != nullptr) // 임시 화면 텍스처 파괴
    {
        SDL_DestroyTexture(this->tempScreenTexture);
        this->tempScreenTexture = nullptr;
        EngineStdOut("Temporary screen texture destroyed.", 0);
    }
}

void Engine::terminateGE()
{
    EngineStdOut("Terminating SDL and engine resources...", 0); // SDL 및 엔진 리소스 종료

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
    EngineStdOut("Render device was reset. All GPU resources will be recreated.", 1); // 렌더 장치 리셋됨. GPU 리소스 재생성

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
    LOADING_METHOD_NAME = "Loading Sprites...";
    chrono::time_point<chrono::steady_clock> startTime = chrono::steady_clock::now(); // 이미지 로딩 시작 시간
    EngineStdOut("Starting image loading...", 0);                                     // 이미지 로딩 시작
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
        EngineStdOut("No image items to load.", 0); // 로드할 이미지 항목 없음
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
                    EngineStdOut("CRITICAL: Renderer is NULL before IMG_LoadTexture for " + imagePath, 2);
                }
                SDL_ClearError();

                costume.imageHandle = IMG_LoadTexture(this->renderer, imagePath.c_str());
                if (!costume.imageHandle) // 텍스처 로드 실패 시 렌더러 포인터 값 로깅
                {
                    EngineStdOut(
                        "Renderer pointer value at IMG_LoadTexture failure: " + to_string(
                                                                                    reinterpret_cast<uintptr_t>(this->renderer)),
                        3);
                }
                if (costume.imageHandle)
                {
                    loadedCount++;
                    EngineStdOut(
                        "  Shape '" + costume.name + "' (" + imagePath + ") image loaded successfully as SDL_Texture.",
                        0);
                }
                else
                {
                    failedCount++;
                    EngineStdOut(
                        "IMG_LoadTexture failed for '" + objInfo.name + "' shape '" + costume.name + "' from path: " +
                            imagePath + ". SDL_" + SDL_GetError(),
                        2);
                }

                incrementLoadedItemCount();

                if (loadedItemCount % 5 == 0 || loadedItemCount == totalItemsToLoad || costume.imageHandle == nullptr)
                {
                    renderLoadingScreen();

                    SDL_Event e; // 이벤트 폴링은 메인 루프에서 처리하는 것이 더 일반적입니다.
                                 // 로딩 중 UI 업데이트를 위해 최소한의 이벤트 처리는 필요할 수 있습니다.
                    while (SDL_PollEvent(&e))
                    { // SDL_PollEvent의 반환 값을 확인합니다.
                        if (e.type == SDL_EVENT_QUIT)
                        {
                            EngineStdOut("Image loading cancelled by user.", 1); // 사용자에 의해 이미지 로딩 취소됨
                            return false;
                        }
                    }
                }
            }
        }
    }

    EngineStdOut("Image loading finished. Success: " + to_string(loadedCount) + ", Failed: " + to_string(failedCount),
                 0);
    chrono::duration<double> loadingDuration = chrono::duration_cast<chrono::duration<double>>(
        chrono::steady_clock::now() - startTime);
    string greething = "";
    double duration = loadingDuration.count();

    if (duration < 1.0)
    {
        greething = "WoW Excellent!";
    }
    else if (duration < 10.0)
    {
        greething = "Umm ok.";
    }
    else
    {
        greething = "You to Slow.";
    }
    EngineStdOut("Time to load entire image " + to_string(loadingDuration.count()) + " seconds " + greething);
    if (failedCount > 0 && loadedCount == 0 && totalItemsToLoad > 0)
    {
        EngineStdOut("All images failed to load. This may cause issues.", 2); // 모든 이미지 로드 실패
        showMessageBox("Fatal No images could be loaded. Check asset paths and file integrity.",
                       msgBoxIconType.ICON_ERROR);
        // return false; // 여기서 프로그램을 종료하는 대신, 경고 후 계속 진행하도록 변경
    }
    else if (failedCount > 0)
    {
        // 일부 이미지 로드 실패
        EngineStdOut("Some images failed to load, processing with available resources.", 1);
    }
    return true;
}

bool Engine::loadSounds()
{
    LOADING_METHOD_NAME = "Loading Sounds...";
    chrono::time_point<chrono::steady_clock> startTime = chrono::steady_clock::now();
    EngineStdOut("Starting Sound loading...", 0);

    // 1. Calculate total sound items to load for the progress bar
    int numSoundsToAttemptPreload = 0;
    for (const auto &objInfo : objects_in_order)
    {
        for (const auto &sf : objInfo.sounds)
        {
            if (!sf.fileurl.empty())
            {
                numSoundsToAttemptPreload++;
            }
        }
    }

    this->totalItemsToLoad = numSoundsToAttemptPreload; // Set total items for this loading phase
    this->loadedItemCount = 0;                          // Reset loaded items count

    EngineStdOut("Total sound items to preload: " + to_string(this->totalItemsToLoad), 0);

    if (this->totalItemsToLoad == 0)
    {
        EngineStdOut("No sound items to preload.", 0);
        return true;
    }

    int preloadedSuccessfullyCount = 0; // To count actual successful preloads if needed, though aeHelper logs this.
    // For now, 'pl' or 'loadedItemCount' will represent processed items.

    for (const auto &objInfo : objects_in_order)
    {
        for (const auto &sf : objInfo.sounds)
        {
            if (!sf.fileurl.empty())
            {
                string fullAudioPath = "";
                if (IsSysMenu)
                {
                    fullAudioPath = "sysmenu/" + sf.fileurl;
                }
                else
                {
                    fullAudioPath = string(BASE_ASSETS) + sf.fileurl;
                }
                // In case IsSysMenu affects sound paths, similar logic to loadImages could be added here.
                aeHelper.preloadSound(fullAudioPath);
                preloadedSuccessfullyCount++; // Increment if preload was attempted/successful

                this->loadedItemCount++; // Increment for progress bar

                // Update loading screen periodically and check for quit event
                if (this->loadedItemCount % 5 == 0 || this->loadedItemCount == this->totalItemsToLoad)
                {
                    renderLoadingScreen();
                    SDL_Event e;
                    while (SDL_PollEvent(&e) != 0)
                    {
                        if (e.type == SDL_EVENT_QUIT)
                        {
                            EngineStdOut("Sound loading cancelled by user.", 1);
                            return false; // Allow cancellation
                        }
                    }
                }
            }
        }
    }

    chrono::duration<double> loadingDuration = chrono::duration_cast<chrono::duration<double>>(
        chrono::steady_clock::now() - startTime);
    EngineStdOut(
        "Finished preloading " + to_string(preloadedSuccessfullyCount) + " sound assets. Time taken: " +
            to_string(loadingDuration.count()) + " seconds.",
        0);
    return true;
}

bool Engine::recreateAssetsIfNeeded()
{
    if (!m_needsTextureRecreation)
    {
        return true;
    }

    EngineStdOut("Recreating GPU assets due to device reset...", 0); // 장치 리셋으로 인한 GPU 에셋 재생성

    if (!createTemporaryScreen())
    {
        EngineStdOut("Failed to recreate temporary screen texture after device reset.", 2); // 임시 화면 텍스처 재생성 실패
        return false;
    }

    if (!loadImages())
    {
        EngineStdOut("Failed to reload images after device reset.", 2); // 이미지 리로드 실패
        return false;
    }

    m_needsTextureRecreation = false; // 텍스처 재생성 필요 플래그 리셋
    EngineStdOut("GPU assets recreated successfully.", 0);
    return true;
}

void Engine::drawAllEntities()
{
    getProjectTimerValue();
    if (!renderer || !tempScreenTexture)
    {
        EngineStdOut("drawAllEntities: Renderer or temporary screen texture not available.", 1);
        // 렌더러 또는 임시 화면 텍스처 사용 불가
        return;
    }

    SDL_SetRenderTarget(renderer, tempScreenTexture);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // 배경색 흰색으로 설정
    SDL_RenderClear(renderer);

    for (int i = static_cast<int>(objects_in_order.size()) - 1; i >= 0; --i)
    {
        const ObjectInfo &objInfo = objects_in_order[i];
        // 현재 씬에 속하거나 전역 오브젝트인 경우에만 그림
        bool isInCurrentScene = (objInfo.sceneId == currentSceneId);
        bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty());

        if (!isInCurrentScene && !isGlobal)
        {
            continue;
        }

        auto it_entity = entities.find(objInfo.id);
        if (it_entity == entities.end())
        {
            // 해당 ID의 엔티티가 없으면 건너뜀

            continue;
        }
        const Entity *entityPtr = it_entity->second;

        if (!entityPtr->isVisible())
        {
            // 엔티티가 보이지 않으면 건너뜀
            continue;
        }

        if (objInfo.objectType == "sprite")
        {
            // 스프라이트 타입 오브젝트 그리기

            const Costume *selectedCostume = nullptr;

            for (const auto &costume_ref : objInfo.costumes)
            {
                if (costume_ref.id == objInfo.selectedCostumeId)
                {
                    selectedCostume = &costume_ref;
                    break;
                }
            }

            if (selectedCostume && selectedCostume->imageHandle != nullptr) // 선택된 모양이 있고 이미지 핸들이 유효한 경우
            {
                double entryX = entityPtr->getX();
                double entryY = entityPtr->getY();

                float sdlX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                float sdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                float texW = 0, texH = 0;
                if (!selectedCostume->imageHandle)
                {
                    EngineStdOut(
                        "Texture handle is null for costume '" + selectedCostume->name + "' of object '" + objInfo.name + "'. Cannot get texture size.", 2); // 텍스처 핸들 null 오류
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
                    string texturePtrStr = oss.str(); // 텍스처 포인터 주소 로깅
                    EngineStdOut(
                        "Failed to get texture size for costume '" + selectedCostume->name + "' of object '" + objInfo.name + "'. Texture Ptr: " + texturePtrStr + ". SDL_" + errorDetail, 2);
                    SDL_ClearError();
                    continue;
                }

                SDL_FRect dstRect;

                dstRect.w = static_cast<float>(texW * entityPtr->getScaleX());
                dstRect.h = static_cast<float>(texH * entityPtr->getScaleY());
                SDL_FPoint center; // 회전 중심점
                center.x = static_cast<float>(entityPtr->getRegX());
                center.y = static_cast<float>(entityPtr->getRegY());
                dstRect.x = sdlX - static_cast<float>(entityPtr->getRegX() * entityPtr->getScaleX());
                dstRect.y = sdlY - static_cast<float>(entityPtr->getRegY() * entityPtr->getScaleY());

                double sdlAngle = entityPtr->getRotation() + (entityPtr->getDirection() - 90.0); // SDL 렌더링 각도 계산
                bool colorModApplied = false, alphaModApplied = false;
                double brightness_effect = entityPtr->getEffectBrightness();
                double hue_effect_dgress = entityPtr->getEffectHue();

                Uint8 r_final_mod = 255, g_final_mod = 255, b_final_mod = 255;

                float brightness_factor = (100.0f + static_cast<float>(brightness_effect)) / 100.0f;
                brightness_factor = clamp(brightness_factor, 0.0f, 2.0f);

                if (abs(hue_effect_dgress) > 0.01)
                {
                    colorModApplied = true;
                    SDL_Color hue_tint_color = hueToRGB(hue_effect_dgress);

                    r_final_mod = static_cast<Uint8>(clamp(hue_tint_color.r * brightness_factor, 0.0f, 255.0f));
                    g_final_mod = static_cast<Uint8>(clamp(hue_tint_color.g * brightness_factor, 0.0f, 255.0f));
                    b_final_mod = static_cast<Uint8>(clamp(hue_tint_color.b * brightness_factor, 0.0f, 255.0f));
                }
                else if (abs(brightness_effect) > 0.01)
                {
                    colorModApplied = true;
                    r_final_mod = static_cast<Uint8>(std::clamp(255.0f * brightness_factor, 0.0f, 255.0f));
                    g_final_mod = static_cast<Uint8>(std::clamp(255.0f * brightness_factor, 0.0f, 255.0f));
                    b_final_mod = static_cast<Uint8>(std::clamp(255.0f * brightness_factor, 0.0f, 255.0f));
                }
                if (colorModApplied)
                {
                    SDL_SetTextureColorMod(selectedCostume->imageHandle, r_final_mod, g_final_mod, b_final_mod);
                }
                double alpha_effect = entityPtr->getEffectAlpha();
                if (abs(alpha_effect - 1.0) > 0.01)
                {
                    alphaModApplied = true;
                    Uint8 alpha_sdl_mod = static_cast<Uint8>(std::clamp(alpha_effect * 255.0, 0.0, 255.0));
                    SDL_SetTextureAlphaMod(selectedCostume->imageHandle, alpha_sdl_mod);
                }

                if (colorModApplied)
                {
                    SDL_SetTextureColorMod(selectedCostume->imageHandle, 255, 255, 255);
                }
                if (alphaModApplied)
                {
                    SDL_SetTextureAlphaMod(selectedCostume->imageHandle, 255);
                }
                SDL_RenderTextureRotated(renderer, selectedCostume->imageHandle, nullptr, &dstRect, sdlAngle, &center,
                                         SDL_FLIP_NONE);
            }
        }
        else if (objInfo.objectType == "textBox")
        {
            // 텍스트 상자 타입 오브젝트 그리기

            if (!objInfo.textContent.empty())
            {
                string fontString = objInfo.fontName;

                string determinedFontPath;
                string fontfamily = objInfo.fontName;
                string fontAsset = string(FONT_ASSETS);
                int fontSize = objInfo.fontSize;
                SDL_Color textColor = objInfo.textColor; // 텍스트 색상
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
                        EngineStdOut(
                            "Failed to load font: " + determinedFontPath + " at size " + to_string(currentFontSize) +
                                " for textBox '" + objInfo.name + "'. Falling back to HUD font.",
                            2);
                        Usefont = hudFont;
                    }
                }
                else
                {
                    Usefont = hudFont; // 폰트 로드 실패 시 HUD 기본 폰트 사용
                }

                SDL_Surface *textSurface = TTF_RenderText_Blended(Usefont, objInfo.textContent.c_str(),
                                                                  objInfo.textContent.size(), objInfo.textColor);
                if (textSurface)
                {
                    // 텍스트 표면 렌더링 성공

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
                        dstRect.h = scaledHeight; // 텍스트 정렬 처리
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
                        EngineStdOut(
                            "Failed to create text texture for textBox '" + objInfo.name + "'. SDL_" + SDL_GetError(),
                            2); // 텍스트 텍스처 생성 실패
                    }

                    SDL_DestroySurface(textSurface);
                }
                else
                {
                    // 텍스트 표면 렌더링 실패
                    EngineStdOut("Failed to render text surface for textBox '" + objInfo.name, 2);
                }
            }
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
    // 화면 지우기 (검은색)
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    // 윈도우 렌더링 크기 가져오기
    int windowRenderW = 0, windowRenderH = 0;
    SDL_GetRenderOutputSize(renderer, &windowRenderW, &windowRenderH);

    if (windowRenderW <= 0 || windowRenderH <= 0)
    {
        EngineStdOut("drawAllEntities: Window render dimensions are zero or negative.", 1);
        return;
    }
    // 줌 계수 적용된 소스 뷰 영역 계산
    float srcViewWidth = static_cast<float>(PROJECT_STAGE_WIDTH) / zoomFactor;
    float srcViewHeight = static_cast<float>(PROJECT_STAGE_HEIGHT) / zoomFactor;
    float srcViewX = (static_cast<float>(PROJECT_STAGE_WIDTH) - srcViewWidth) / 2.0f;
    float srcViewY = (static_cast<float>(PROJECT_STAGE_HEIGHT) - srcViewHeight) / 2.0f;
    SDL_FRect currentSrcFRect = {srcViewX, srcViewY, srcViewWidth, srcViewHeight};

    float stageContentAspectRatio = static_cast<float>(PROJECT_STAGE_WIDTH) / static_cast<float>(PROJECT_STAGE_HEIGHT);
    // 최종 화면에 표시될 목적지 사각형 계산 (화면 비율 유지)
    SDL_FRect finalDisplayDstRect;
    float windowAspectRatio = static_cast<float>(windowRenderW) / static_cast<float>(windowRenderH);

    if (windowAspectRatio >= stageContentAspectRatio)
    {
        // 윈도우가 스테이지보다 넓거나 같은 비율: 높이 기준, 너비 조정 (레터박스 좌우)
        finalDisplayDstRect.h = static_cast<float>(windowRenderH);
        finalDisplayDstRect.w = finalDisplayDstRect.h * stageContentAspectRatio;
        finalDisplayDstRect.x = (static_cast<float>(windowRenderW) - finalDisplayDstRect.w) / 2.0f;
        finalDisplayDstRect.y = 0.0f;
    }
    else
    {
        // 윈도우가 스테이지보다 좁은 비율: 너비 기준, 높이 조정 (레터박스 상하)
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
        // 렌더러 사용 불가
        EngineStdOut("drawHUD: Renderer not available.", 1);
        return;
    }

    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH);
    if (this->hudFont && this->specialConfig.showFPS)
    {
        string fpsText = "FPS: " + to_string(static_cast<int>(currentFps));
        SDL_Color textColor = {255, 150, 0, 255}; // 주황색

        SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, fpsText.c_str(), 0, textColor);
        if (textSurface)
        {
            // 배경 사각형 설정
            float bgPadding = 5.0f; // 텍스트 주변 여백
            SDL_FRect bgRect = {
                10.0f - bgPadding,                                  // FPS 텍스트 x 위치에서 여백만큼 왼쪽으로
                10.0f - bgPadding,                                  // FPS 텍스트 y 위치에서 여백만큼 위로
                static_cast<float>(textSurface->w) + 2 * bgPadding, // 텍스트 너비 + 양쪽 여백
                static_cast<float>(textSurface->h) + 2 * bgPadding  // 텍스트 높이 + 양쪽 여백
            };

            // 반투명한 어두운 배경색 설정
            SDL_Color bgColor = {30, 30, 30, 150}; // 어두운 회색, 약 70% 불투명도

            // 현재 블렌드 모드 저장 및 블렌딩 활성화
            SDL_BlendMode originalBlendMode;
            SDL_GetRenderDrawBlendMode(renderer, &originalBlendMode);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

            SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
            Helper_RenderFilledRoundedRect(renderer, &bgRect, 5.0f); // 둥근 모서리 배경 그리기

            // 원래 블렌드 모드로 복원 (다른 HUD 요소에 영향 주지 않도록)
            SDL_SetRenderDrawBlendMode(renderer, originalBlendMode);

            // FPS 텍스트 렌더링 (배경 위에)
            TTF_FontStyleFlags original_style = TTF_GetFontStyle(hudFont);
            TTF_SetFontStyle(hudFont, TTF_STYLE_BOLD);

            SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            if (textTexture)
            {
                SDL_FRect dstRect = {
                    10.0f, 10.0f, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};
                SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);
                SDL_DestroyTexture(textTexture);
            }
            else
            {
                EngineStdOut("Failed to create FPS text texture: " + string(SDL_GetError()), 2); // FPS 텍스트 텍스처 생성 실패
            }
            TTF_SetFontStyle(hudFont, original_style); // 원래 폰트 스타일로 복원
            SDL_DestroySurface(textSurface);
        }
        else
        {
            EngineStdOut("Failed to render FPS text surface ", 2); // FPS 텍스트 표면 렌더링 실패
        }
    }
    // 대답 입력
    if (m_textInputActive)
    {
        // 텍스트 입력 모드가 활성화된 경우
        std::lock_guard<std::mutex> lock(m_textInputMutex);
        // m_textInputQuestionMessage, m_currentTextInputBuffer 접근 보호

        // 화면 하단 등에 질문 메시지와 입력 필드 UI를 그립니다.
        // 예시:
        // 1. 질문 메시지 렌더링 (m_textInputQuestionMessage 사용)
        // 2. 입력 필드 배경 렌더링
        // 3. 현재 입력된 텍스트 렌더링 (m_currentTextInputBuffer 사용)
        // 4. 체크버튼

        if (hudFont)
        {
            SDL_Color textColor = {0, 0, 0, 255};     // 검정
            SDL_Color bgColor = {255, 255, 255, 255}; // 흰색

            // 질문 메시지
            if (!m_textInputQuestionMessage.empty())
            {
                SDL_Surface *questionSurface = TTF_RenderText_Blended_Wrapped(
                    hudFont, m_textInputQuestionMessage.c_str(), m_textInputQuestionMessage.size(), textColor,
                    WINDOW_WIDTH - 40);
                if (questionSurface)
                {
                    SDL_Texture *questionTexture = SDL_CreateTextureFromSurface(renderer, questionSurface);
                    SDL_FRect questionRect = {
                        20.0f, static_cast<float>(WINDOW_HEIGHT - 100 - questionSurface->h),
                        static_cast<float>(questionSurface->w), static_cast<float>(questionSurface->h)};
                    SDL_RenderTexture(renderer, questionTexture, nullptr, &questionRect);
                    SDL_DestroyTexture(questionTexture);
                    SDL_DestroySurface(questionSurface);
                }
            }

            // 입력 필드
            SDL_FRect inputBgRect = {
                20.0f, static_cast<float>(WINDOW_HEIGHT - 80), static_cast<float>(WINDOW_WIDTH - 40), 40.0f};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
            SDL_RenderFillRect(renderer, &inputBgRect);
            SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255); // 테두리
            SDL_RenderRect(renderer, &inputBgRect);

            std::string displayText = m_currentTextInputBuffer;
            if (SDL_TextInputActive(window))
            {
                // IME 사용 중이거나 텍스트 입력 중일 때 커서 표시
                // 간단한 커서 표시 (깜빡임은 추가 구현 필요)
                Uint64 currentTime = SDL_GetTicks();
                if (currentTime > m_cursorBlinkToggleTime + CURSOR_BLINK_INTERVAL_MS)
                {
                    m_cursorCharVisible = !m_cursorCharVisible;
                    m_cursorBlinkToggleTime = currentTime;
                }
                if (m_cursorCharVisible)
                {
                    displayText += "|";
                }
                else
                {
                    displayText += " ";
                }
            }

            if (!displayText.empty())
            {
                SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, displayText.c_str(), displayText.size(),
                                                                  textColor);
                if (textSurface)
                {
                    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                    // 입력 필드 내부에 텍스트 위치 조정
                    float textX = inputBgRect.x + 10;
                    float textY = inputBgRect.y + (inputBgRect.h - textSurface->h) / 2;
                    SDL_FRect textRect = {
                        textX, textY, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};

                    // 텍스트가 입력 필드를 넘어가지 않도록 클리핑
                    if (textRect.x + textRect.w > inputBgRect.x + inputBgRect.w - 10)
                    {
                        textRect.w = inputBgRect.x + inputBgRect.w - 10 - textRect.x;
                    }

                    SDL_RenderTexture(renderer, textTexture, nullptr, &textRect);
                    SDL_DestroyTexture(textTexture);
                    SDL_DestroySurface(textSurface);
                }
            }
            // 체크버튼 누르면 엔터 친거랑 동일한 효과
            SDL_Texture *checkboxTexture = LoadTextureFromSvgResource(renderer, IDI_CHBOX);
            SDL_Rect inputFiledRect = {inputBgRect.x, inputBgRect.y, inputBgRect.w, inputBgRect.h};
            if (checkboxTexture)
            {
                // 1. 체크박스의 크기를 입력창의 높이에 맞춘 정사각형으로 설정
                int checkboxSize = inputFiledRect.h;

                // 2. 체크박스의 위치 계산
                SDL_FRect checkboxDestRect;
                checkboxDestRect.x = inputFiledRect.x + inputFiledRect.w + 10; // 입력창 우측 + 간격
                checkboxDestRect.y = inputFiledRect.y;                         // 입력창과 동일한 y 좌표 (상단 정렬)
                // 만약 입력창과 수직 중앙 정렬을 원한다면:
                // checkboxDestRect.y = inputFieldRect.y + (inputFieldRect.h - checkboxSize) / 2;
                // 이 경우 checkboxSize가 inputFieldRect.h와 같으므로 결과는 동일합니다.

                checkboxDestRect.w = checkboxSize;
                checkboxDestRect.h = checkboxSize;

                // 3. 체크박스 렌더링
                // SDL_RenderCopy는 checkboxTexture의 전체 내용을 checkboxDestRect에 맞춰 렌더링합니다.
                // SVG는 벡터이므로 checkboxDestRect 크기에 맞게 품질 저하 없이 스케일링됩니다.
                SDL_RenderTexture(renderer, checkboxTexture, NULL, &checkboxDestRect);
            }
        }
    }
    // 줌 슬라이더 UI 표시
    if (this->specialConfig.showZoomSlider)
    {
        SDL_FRect sliderBgRect = {
            static_cast<float>(SLIDER_X), static_cast<float>(SLIDER_Y), static_cast<float>(SLIDER_WIDTH),
            static_cast<float>(SLIDER_HEIGHT)};
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); // 슬라이더 배경색
        SDL_RenderFillRect(renderer, &sliderBgRect);

        float handleX_float = SLIDER_X + ((zoomFactor - MIN_ZOOM) / (MAX_ZOOM - MIN_ZOOM)) * SLIDER_WIDTH;
        float handleWidth_float = 8.0f;

        SDL_FRect sliderHandleRect = {
            handleX_float - handleWidth_float / 2.0f, static_cast<float>(SLIDER_Y - 2), handleWidth_float,
            static_cast<float>(SLIDER_HEIGHT + 4)};
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer, &sliderHandleRect); // 슬라이더 핸들

        if (this->hudFont)
        {
            ostringstream zoomStream;
            zoomStream << fixed << setprecision(2) << zoomFactor;
            string zoomText = "Zoom: " + zoomStream.str();
            SDL_Color textColor = {220, 220, 220, 255}; // 줌 텍스트 색상

            SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, zoomText.c_str(), 0, textColor);
            if (textSurface)
            {
                SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                if (textTexture)
                {
                    SDL_FRect dstRect = {
                        SLIDER_X + SLIDER_WIDTH + 10.0f,
                        SLIDER_Y + (SLIDER_HEIGHT - static_cast<float>(textSurface->h)) / 2.0f,
                        static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};
                    SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);
                    SDL_DestroyTexture(textTexture);
                }
                else
                {
                    EngineStdOut("Failed to create Zoom text texture: " + string(SDL_GetError()), 2); // 줌 텍스트 텍스처 생성 실패
                }
                SDL_DestroySurface(textSurface);
            }
            else
            {
                EngineStdOut("Failed to render Zoom text surface ", 2); // 줌 텍스트 표면 렌더링 실패
            }
        }
    }

    // --- HUD 변수 그리기 (일반 변수 및 리스트) ---
    if (!m_HUDVariables.empty())
    {
        int windowW = 0;
        float maxObservedItemWidthThisFrame = 0.0f; // 각 프레임에서 관찰된 가장 넓은 아이템 너비
        int visibleVarsCount = 0;                   // 보이는 변수 개수
        if (renderer)
            SDL_GetRenderOutputSize(renderer, &windowW, nullptr);

        float screenCenterX = static_cast<float>(windowW) / 2.0f;
        float screenCenterY = static_cast<float>(windowH) / 2.0f;

        // float currentWidgetYPosition = m_variablesListWidgetY; // No longer used for individual items
        // float spacingBetweenBoxes = 2.0f; // No longer used for individual items
        for (auto &var : m_HUDVariables) // Use non-const auto& if var.width might be updated
        {
            if (!var.isVisible)
            {
                continue; // 변수가 보이지 않으면 건너뜁니다.
            }

            // 엔트리 좌표(var.x, var.y)를 스크린 렌더링 좌표로 변환
            float renderX = screenCenterX + var.x;
            float renderY = screenCenterY - var.y; // var.y는 요소의 상단 Y (엔트리 기준)

            // Colors and fixed dimensions for a single item box
            SDL_Color containerBgColor = {240, 240, 240, 220};     // 컨테이너 배경색 (약간 투명한 밝은 회색)
            SDL_Color containerBorderColor = {100, 100, 100, 255}; // 컨테이너 테두리 색상
            SDL_Color itemLabelTextColor = {0, 0, 0, 255};         // 변수 이름 텍스트 색상 (검정)
            SDL_Color itemValueTextColor = {255, 255, 255, 255};   // 변수 값 텍스트 색상
            float itemHeight = 22.0f;                              // 각 변수 항목의 높이
            float itemPadding = 3.0f;                              // 항목 내부 여백
            float containerCornerRadius = 5.0f;
            float containerBorderWidth = 1.0f;

            SDL_Color itemValueBoxBgColor;
            string valueToDisplay;

            if (var.variableType == "timer")
            {
                itemValueBoxBgColor = {255, 150, 0, 255}; // 타이머는 주황색 배경
                valueToDisplay = to_string(static_cast<int>(getProjectTimerValue()));
            }
            else if (var.variableType == "list")
            {
                // ---------- LIST VARIABLE RENDERING ----------
                if (!hudFont) // HUD 폰트 없으면 리스트 렌더링 불가
                    continue;

                // List specific styling
                SDL_Color listBgColor = {240, 240, 240, 220};        // Dark semi-transparent background for the list
                SDL_Color listBorderColor = {150, 150, 150, 255};    // Light gray border
                SDL_Color listNameTextColor = {255, 255, 255, 255};  // Light text for list name
                SDL_Color listItemBgColor = {0, 120, 255, 255};      // Blue background for item data
                SDL_Color listItemTextColor = {255, 255, 255, 255};  // White text for item data
                SDL_Color listRowNumberColor = {200, 200, 200, 255}; // Light gray for row numbers

                float listCornerRadius = 5.0f;
                float listBorderWidth = 1.0f;
                float headerHeight = 25.0f;         // Height for the list name header
                float itemRowHeight = 20.0f;        // Height of each row in the list
                float contentPadding = 5.0f;        // General padding inside the list container and items
                float rowNumberColumnWidth = 30.0f; // Width allocated for row numbers column (adjust as needed)
                float spacingBetweenRows = 2.0f;    // Vertical spacing between list item rows
                // 1. 리스트 컨테이너 테두리 그리기
                // 1. Draw List Container Border
                SDL_FRect listContainerOuterRect = {renderX, renderY, var.width, var.height};
                if (listBorderWidth > 0.0f) // 테두리 두께가 0보다 클 때만 그림
                {
                    SDL_SetRenderDrawColor(renderer, listBorderColor.r, listBorderColor.g, listBorderColor.b,
                                           listBorderColor.a);
                    Helper_RenderFilledRoundedRect(renderer, &listContainerOuterRect, listCornerRadius);
                }

                // 2. Draw List Container Background (inside the border)
                SDL_FRect listContainerInnerRect = {
                    renderX + listBorderWidth,
                    renderY + listBorderWidth,
                    max(0.0f, var.width - (2 * listBorderWidth)),
                    max(0.0f, var.height - (2 * listBorderWidth))};
                float innerRadius = max(0.0f, listCornerRadius - listBorderWidth); // 내부 둥근 모서리 반지름
                if (listContainerInnerRect.w > 0 && listContainerInnerRect.h > 0)
                {
                    SDL_SetRenderDrawColor(renderer, listBgColor.r, listBgColor.g, listBgColor.b, listBgColor.a);
                    Helper_RenderFilledRoundedRect(renderer, &listContainerInnerRect, innerRadius);
                }

                // 3. 리스트 이름 (헤더) 그리기
                string listDisplayName;
                bool foundAssociatedObjectList = false;
                if (!var.objectId.empty())
                {
                    const ObjectInfo *objInfoPtrList = getObjectInfoById(var.objectId);
                    if (objInfoPtrList)
                    {
                        listDisplayName = objInfoPtrList->name + " : " + var.name;
                        foundAssociatedObjectList = true;
                    }
                }
                if (!foundAssociatedObjectList)
                {
                    listDisplayName = var.name;
                }

                SDL_Surface *nameSurfaceList = TTF_RenderText_Blended(hudFont, listDisplayName.c_str(), 0,
                                                                      listNameTextColor);
                if (nameSurfaceList)
                {
                    SDL_Texture *nameTextureList = SDL_CreateTextureFromSurface(renderer, nameSurfaceList);
                    if (nameTextureList)
                    {
                        SDL_FRect nameDestRectList = {
                            listContainerInnerRect.x + contentPadding,
                            listContainerInnerRect.y + (headerHeight - static_cast<float>(nameSurfaceList->h)) / 2.0f,
                            min(static_cast<float>(nameSurfaceList->w), listContainerInnerRect.w - 2 * contentPadding),
                            static_cast<float>(nameSurfaceList->h)};
                        SDL_RenderTexture(renderer, nameTextureList, nullptr, &nameDestRectList);
                        SDL_DestroyTexture(nameTextureList);
                    }
                    SDL_DestroySurface(nameSurfaceList);
                }

                // 4. 리스트 아이템 그리기
                float itemsAreaStartY = listContainerInnerRect.y + headerHeight;
                float itemsAreaRenderableHeight = listContainerInnerRect.h - headerHeight - contentPadding;
                float currentItemVisualY = itemsAreaStartY + contentPadding;
                // 컬럼 위치 계산 (행 번호 왼쪽, 데이터 오른쪽)
                // 수정: 행 번호 컬럼을 먼저 계산하고 왼쪽에 배치
                float rowNumColumnX = listContainerInnerRect.x + contentPadding;
                // 수정: 데이터 컬럼은 행 번호 컬럼 오른쪽에 위치
                float dataColumnX = rowNumColumnX + rowNumberColumnWidth + contentPadding;
                float dataColumnWidth = listContainerInnerRect.w - (2 * contentPadding) - rowNumberColumnWidth -
                                        contentPadding; // 기존 계산 유지, 위치만 변경
                dataColumnWidth = max(0.0f, dataColumnWidth);

                for (size_t i = 0; i < var.array.size(); ++i)
                {
                    if (currentItemVisualY + itemRowHeight > itemsAreaStartY + itemsAreaRenderableHeight)
                    {
                        // 그릴 공간이 없으면 중단
                        break;
                    }
                    const ListItem &listItem = var.array[i];

                    // 컬럼 1: 행 번호 (왼쪽)
                    string rowNumStr = to_string(i + 1);
                    SDL_Surface *rowNumSurface = TTF_RenderText_Blended(hudFont, rowNumStr.c_str(), 0,
                                                                        listRowNumberColor);
                    if (rowNumSurface)
                    {
                        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, rowNumSurface);
                        if (tex)
                        {
                            SDL_FRect r = {
                                rowNumColumnX + (rowNumberColumnWidth - rowNumSurface->w) / 2.0f,
                                currentItemVisualY + (itemRowHeight - rowNumSurface->h) / 2.0f,
                                (float)rowNumSurface->w, (float)rowNumSurface->h};
                            SDL_RenderTexture(renderer, tex, nullptr, &r);
                            SDL_DestroyTexture(tex);
                        }
                        SDL_DestroySurface(rowNumSurface);
                    }

                    // 컬럼 2: 아이템 데이터 (오른쪽 - 파란 배경, 흰색 텍스트)
                    SDL_FRect itemDataBgRect = {dataColumnX, currentItemVisualY, dataColumnWidth, itemRowHeight};
                    SDL_SetRenderDrawColor(renderer, listItemBgColor.r, listItemBgColor.g, listItemBgColor.b,
                                           listItemBgColor.a);
                    SDL_RenderFillRect(renderer, &itemDataBgRect);

                    if (dataColumnWidth > contentPadding * 2)
                    {
                        // 텍스트를 그릴 공간이 있을 때만
                        string textToRender = listItem.data;
                        string displayText = textToRender;
                        float availableTextWidthInDataCol = dataColumnWidth - (2 * contentPadding);

                        int fullTextMeasuredWidth;
                        size_t fullTextOriginalLengthInBytes = textToRender.length();
                        size_t fullTextMeasuredLengthInBytes; // max_width=0일 때 fullTextOriginalLengthInBytes와 같아야 함

                        if (TTF_MeasureString(hudFont, textToRender.c_str(), fullTextOriginalLengthInBytes,
                                              0 /* max_width = 0 이면 전체 문자열 측정 */, &fullTextMeasuredWidth,
                                              &fullTextMeasuredLengthInBytes))
                        {
                            if (static_cast<float>(fullTextMeasuredWidth) > availableTextWidthInDataCol)
                            {
                                // 텍스트가 너무 길면 잘림 처리 (...)
                                // 잘림 처리 필요
                                const string ellipsis = "...";
                                int ellipsisMeasuredWidth;
                                size_t ellipsisOriginalLength = ellipsis.length();
                                size_t ellipsisMeasuredLength; // ellipsis.length()와 같아야 함

                                if (TTF_MeasureString(hudFont, ellipsis.c_str(), ellipsisOriginalLength, 0,
                                                      &ellipsisMeasuredWidth, &ellipsisMeasuredLength))
                                {
                                    float widthForTextItself =
                                        availableTextWidthInDataCol - static_cast<float>(ellipsisMeasuredWidth);

                                    if (widthForTextItself <= 0)
                                    {
                                        // 내용 + "..." 을 위한 공간 없음. "..." 만이라도 표시 가능한지 확인
                                        if (static_cast<float>(ellipsisMeasuredWidth) <= availableTextWidthInDataCol)
                                        {
                                            displayText = ellipsis;
                                        }
                                        else
                                        {
                                            // "..." 조차 표시할 공간 없음
                                            displayText = ""; // 또는 textToRender의 첫 글자 등 (UTF-8 고려 필요)
                                        }
                                    }
                                    else
                                    {
                                        // "..." 앞의 원본 텍스트가 들어갈 수 있는 부분 측정
                                        int fittingTextPortionWidth;
                                        size_t fittingTextPortionLengthInBytes;
                                        TTF_MeasureString(hudFont, textToRender.c_str(), fullTextOriginalLengthInBytes,
                                                          static_cast<int>(widthForTextItself),
                                                          &fittingTextPortionWidth, &fittingTextPortionLengthInBytes);

                                        if (fittingTextPortionLengthInBytes > 0)
                                        {
                                            displayText =
                                                textToRender.substr(0, fittingTextPortionLengthInBytes) + ellipsis;
                                        }
                                        else
                                        {
                                            // 텍스트 부분이 전혀 안 들어감. "..." 만이라도 표시 가능한지 확인
                                            if (static_cast<float>(ellipsisMeasuredWidth) <=
                                                availableTextWidthInDataCol)
                                            {
                                                displayText = ellipsis;
                                            }
                                            else
                                            {
                                                displayText = "";
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    // "..." 측정 실패
                                    EngineStdOut("Failed to measure ellipsis text for HUD list.", 2);
                                    // 간단한 대체 처리
                                    if (textToRender.length() > 2)
                                        displayText = textToRender.substr(0, textToRender.length() - 2) + "..";
                                }
                            }
                            // else: 전체 텍스트가 공간에 맞으므로 displayText = textToRender (초기값) 사용
                        }
                        else
                        {
                            // textToRender 측정 실패
                            EngineStdOut("Failed to measure text: " + textToRender + " for HUD list.", 2);
                            // 오류 처리, displayText는 초기값 textToRender를 유지하거나 비워둘 수 있음
                        }

                        if (!displayText.empty())
                        {
                            SDL_Surface *itemTextSurface = TTF_RenderText_Blended(
                                hudFont, displayText.c_str(), 0, listItemTextColor);
                            if (itemTextSurface)
                            {
                                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, itemTextSurface);
                                if (tex)
                                {
                                    SDL_FRect r = {
                                        itemDataBgRect.x + contentPadding,
                                        itemDataBgRect.y + (itemDataBgRect.h - itemTextSurface->h) / 2.0f,
                                        (float)itemTextSurface->w, (float)itemTextSurface->h};
                                    SDL_RenderTexture(renderer, tex, nullptr, &r);
                                    SDL_DestroyTexture(tex);
                                }
                                SDL_DestroySurface(itemTextSurface);
                            }
                        }
                    }

                    currentItemVisualY += itemRowHeight + spacingBetweenRows;
                }

                // 5. 리스트 크기 조절 핸들 그리기 (오른쪽 하단)
                if (var.width >= MIN_LIST_WIDTH && var.height >= MIN_LIST_HEIGHT)
                {
                    // 핸들을 그릴 충분한 공간이 있는지 확인
                    SDL_FRect resizeHandleRect = {
                        renderX + var.width - LIST_RESIZE_HANDLE_SIZE,  // renderX 기준
                        renderY + var.height - LIST_RESIZE_HANDLE_SIZE, // renderY 기준
                        LIST_RESIZE_HANDLE_SIZE,
                        LIST_RESIZE_HANDLE_SIZE};
                    SDL_SetRenderDrawColor(renderer, listRowNumberColor.r, listRowNumberColor.g, listRowNumberColor.b,
                                           255); // 핸들 색상
                    SDL_RenderFillRect(renderer, &resizeHandleRect);
                }
                var.transient_render_width = var.width; // 리스트의 경우, 렌더링된 너비는 정의된 너비와 동일
            }
            else
            {
                // 일반 변수
                itemValueBoxBgColor = {0, 120, 255, 255}; // 다른 변수는 파란색 배경
                valueToDisplay = var.value;
            }

            // 변수 이름 레이블
            // 변수의 object 키에 오브젝트 ID가 있을 경우 해당 오브젝트 이름을 가져온다.
            // 없을경우 그냥 변수 이름을 사용한다.
            string nameToDisplay; // HUD에 최종적으로 표시될 변수의 이름
            bool foundAssociatedObject = false;

            if (!var.objectId.empty())
            {
                const ObjectInfo *objInfoPtr = getObjectInfoById(var.objectId);
                if (objInfoPtr)
                {
                    nameToDisplay = objInfoPtr->name + " : " + var.name;
                    foundAssociatedObject = true;
                }
            }

            if (!foundAssociatedObject)
            {
                nameToDisplay = var.name; // 변수 자체의 이름을 표시할 이름으로 사용합니다.
            }

            SDL_Surface *nameSurface = TTF_RenderText_Blended(hudFont, nameToDisplay.c_str(), 0, itemLabelTextColor);
            SDL_Surface *valueSurface = TTF_RenderText_Blended(hudFont, valueToDisplay.c_str(), 0, itemValueTextColor);

            if (!nameSurface || !valueSurface)
            {
                if (nameSurface)
                    SDL_DestroySurface(nameSurface);
                if (valueSurface)
                    SDL_DestroySurface(valueSurface);
                EngineStdOut("Failed to render name or value surface for variable: " + var.name, 2);
                continue; // 이름 또는 값 표면 렌더링 실패
            }

            float nameTextActualWidth = static_cast<float>(nameSurface->w);
            float valueTextActualWidth = static_cast<float>(valueSurface->w);

            // 값 텍스트와 내부 여백(좌우 itemPadding)을 포함한 이상적인 파란색 값 배경 상자 너비
            float idealValueBgWidth = valueTextActualWidth + (2 * itemPadding);

            // 이름, 값 배경, 그리고 그 사이 및 양옆의 내부 여백을 모두 포함하는 이상적인 컨테이너 내용물 영역의 너비
            // (왼쪽패딩 + 이름너비 + 중간패딩 + 값배경너비 + 오른쪽패딩)
            float idealFillWidth = itemPadding + nameTextActualWidth + itemPadding + idealValueBgWidth + itemPadding;
            idealFillWidth = nameTextActualWidth + idealValueBgWidth + (3 * itemPadding); // Simplified

            // 이상적인 전체 컨테이너 너비 (테두리 포함)
            float idealContainerFixedWidth = idealFillWidth + (2 * containerBorderWidth);

            // 컨테이너 최소/최대 너비 정의
            float minContainerFixedWidth = 80.0f; // 최소 너비
            // 최대 사용 가능 너비는 현재 변수의 x 위치를 기준으로 계산해야 합니다.
            float maxAvailContainerWidth = static_cast<float>(windowW) - var.x - 10.0f;   // 창 오른쪽 가장자리에서 10px 여유 확보
            maxAvailContainerWidth = max(minContainerFixedWidth, maxAvailContainerWidth); // 최대 너비는 최소 너비보다 작을 수 없음

            // 이 항목의 최종 컨테이너 너비
            float currentItemContainerWidth = clamp(idealContainerFixedWidth, minContainerFixedWidth,
                                                    maxAvailContainerWidth);
            if (var.variableType == "list" && var.width > 0)
            {
                // 리스트 타입이고 명시적 너비가 있다면 해당 너비 사용
                // 참고: 리스트의 경우 var.width는 사용자가 project.json에서 명시적으로 설정한 너비를 의미할 수 있습니다.
                currentItemContainerWidth = clamp(var.width, minContainerFixedWidth, maxAvailContainerWidth);
            }

            maxObservedItemWidthThisFrame = max(maxObservedItemWidthThisFrame, currentItemContainerWidth);
            visibleVarsCount++; // Count visible variables to update m_maxVariablesListContentWidth later

            // 이 변수 항목 상자의 높이
            var.transient_render_width = currentItemContainerWidth; // 마지막으로 렌더링된 너비 저장
            float singleBoxHeight = itemHeight + 2 * itemPadding;

            // 1. 컨테이너 테두리 그리기 (currentItemContainerWidth 사용)
            // float containerX = m_variablesListWidgetX; // 이제 var.x를 사용
            SDL_FRect outerContainerRect = {renderX, renderY, currentItemContainerWidth, singleBoxHeight};
            if (containerBorderWidth > 0.0f)
            {
                SDL_SetRenderDrawColor(renderer, containerBorderColor.r, containerBorderColor.g, containerBorderColor.b,
                                       containerBorderColor.a);
                Helper_RenderFilledRoundedRect(renderer, &outerContainerRect, containerCornerRadius);
            }
            // 2. 컨테이너 배경 그리기 (테두리 안쪽, currentItemContainerWidth 사용)
            SDL_FRect fillContainerRect = {
                renderX + containerBorderWidth, // 테두리 두께만큼 안쪽으로
                renderY + containerBorderWidth,
                max(0.0f, currentItemContainerWidth - (2 * containerBorderWidth)),
                max(0.0f, singleBoxHeight - (2 * containerBorderWidth))};
            float fillRadius = max(0.0f, containerCornerRadius - containerBorderWidth);
            if (fillContainerRect.w > 0 && fillContainerRect.h > 0)
            {
                SDL_SetRenderDrawColor(renderer, containerBgColor.r, containerBgColor.g, containerBgColor.b,
                                       containerBgColor.a);
                Helper_RenderFilledRoundedRect(renderer, &fillContainerRect, fillRadius);
            }

            // 3. 각 변수 항목 그리기 (이름과 값)
            float contentAreaTopY = fillContainerRect.y + itemPadding;

            SDL_Texture *nameTexture = SDL_CreateTextureFromSurface(renderer, nameSurface);
            SDL_Texture *valueTexture = SDL_CreateTextureFromSurface(renderer, valueSurface);
            SDL_DestroySurface(nameSurface);
            SDL_DestroySurface(valueSurface);

            if (nameTexture && valueTexture)
            {
                // Determine available width for (NameText + ValueBox) within fillContainerRect,
                // accounting for 3 itemPaddings (left, middle, right).
                float spaceForNameTextAndValueBox = max(0.0f, fillContainerRect.w - (3 * itemPadding));
                // 이름 텍스트와 값 상자를 위한 공간

                // Ideal widths for name text and the blue value background box
                float targetNameTextWidth = nameTextActualWidth;
                float targetValueBoxWidth = idealValueBgWidth;
                // Already calculated: valueTextActualWidth + (2 * itemPadding)

                float totalIdealInternalWidth = targetNameTextWidth + targetValueBoxWidth;

                float finalNameTextWidth;
                float finalValueBoxWidth;

                if (totalIdealInternalWidth <= spaceForNameTextAndValueBox)
                {
                    // 이상적인 너비로 둘 다 그릴 충분한 공간이 있음
                    finalNameTextWidth = targetNameTextWidth;
                    finalValueBoxWidth = targetValueBoxWidth;
                }
                else
                {
                    // 공간 부족, spaceForNameTextAndValueBox에 맞게 비례적으로 축소
                    if (totalIdealInternalWidth > 0)
                    {
                        float scaleFactor = spaceForNameTextAndValueBox / totalIdealInternalWidth;
                        finalNameTextWidth = targetNameTextWidth * scaleFactor;
                        finalValueBoxWidth = targetValueBoxWidth * scaleFactor;
                    }
                    else
                    {
                        // 두 대상 너비가 모두 0인 경우
                        finalNameTextWidth = 0;
                        finalValueBoxWidth = spaceForNameTextAndValueBox; // Or distribute 0/0 or space/2, space/2
                        if (spaceForNameTextAndValueBox > 0 && targetNameTextWidth == 0 && targetValueBoxWidth == 0)
                        {
                            // 공간은 있지만 내용이 없는 경우
                            finalNameTextWidth = spaceForNameTextAndValueBox / 2.0f; // Arbitrary split
                            finalValueBoxWidth = spaceForNameTextAndValueBox / 2.0f;
                        }
                        else
                        {
                            finalValueBoxWidth = 0;
                        }
                    }
                }
                finalNameTextWidth = max(0.0f, finalNameTextWidth);
                finalValueBoxWidth = max(0.0f, finalValueBoxWidth);

                // 이름 그리기
                SDL_FRect nameDestRect = {
                    fillContainerRect.x + itemPadding,
                    contentAreaTopY + (itemHeight - static_cast<float>(nameTexture->h)) / 2.0f,
                    finalNameTextWidth,
                    static_cast<float>(nameTexture->h)};
                SDL_FRect nameSrcRect = {0, 0, static_cast<int>(finalNameTextWidth), static_cast<int>(nameTexture->h)};
                SDL_RenderTexture(renderer, nameTexture, &nameSrcRect, &nameDestRect);

                // 값 배경 상자 및 값 텍스트 그리기
                if (finalValueBoxWidth > 0)
                {
                    SDL_FRect valueBgRect = {
                        nameDestRect.x + finalNameTextWidth + itemPadding,
                        contentAreaTopY,
                        finalValueBoxWidth,
                        itemHeight};
                    SDL_SetRenderDrawColor(renderer, itemValueBoxBgColor.r, itemValueBoxBgColor.g,
                                           itemValueBoxBgColor.b, itemValueBoxBgColor.a);
                    SDL_RenderFillRect(renderer, &valueBgRect);

                    // Value text display width is capped by the blue box's inner width
                    float valueTextDisplayWidth = max(
                        0.0f, min(valueTextActualWidth, finalValueBoxWidth - (2 * itemPadding)));
                    if (valueTextDisplayWidth > 0)
                    {
                        SDL_FRect valueDestRect = {
                            valueBgRect.x + itemPadding, // 파란색 상자 내에서 왼쪽 정렬
                            valueBgRect.y + (valueBgRect.h - static_cast<float>(valueTexture->h)) / 2.0f,
                            valueTextDisplayWidth,
                            static_cast<float>(valueTexture->h)};
                        SDL_FRect valueSrcRect = {
                            0, 0, static_cast<int>(valueTextDisplayWidth), static_cast<int>(valueTexture->h)};
                        SDL_RenderTexture(renderer, valueTexture, &valueSrcRect, &valueDestRect);
                    }
                }
            }
            if (nameTexture)
                SDL_DestroyTexture(nameTexture);
            if (valueTexture)
                SDL_DestroyTexture(valueTexture);

            // currentWidgetYPosition += singleBoxHeight + spacingBetweenBoxes; // 개별 위치를 사용하므로 이 줄은 제거됩니다.
        }

        if (visibleVarsCount > 0)
        {
            // maxObservedItemWidthThisFrame은 minContainerFixedWidth(80.0f) 이상이어야 합니다.
            // currentItemContainerWidth가 그렇게 제한(clamp)되기 때문입니다.
            m_maxVariablesListContentWidth = maxObservedItemWidthThisFrame;
        }
        else
        {
            m_maxVariablesListContentWidth = 180.0f; // 보이는 항목이 없으면 기본 너비
        }
    }
    else
    {
        // 목록이 아예 표시되지 않거나 비어있으면 기본 너비
        m_maxVariablesListContentWidth = 180.0f;
    }
}

bool Engine::mapWindowToStageCoordinates(int windowMouseX, int windowMouseY, float &stageX, float &stageY) const
{
    int windowRenderW = 0, windowRenderH = 0;
    if (this->renderer)
    {
        SDL_GetRenderOutputSize(this->renderer, &windowRenderW, &windowRenderH);
    }
    else // 렌더러 사용 불가
    {
        EngineStdOut("mapWindowToStageCoordinates: Renderer not available.", 2);
        return false;
    }

    if (windowRenderW <= 0 || windowRenderH <= 0)
    {
        // 렌더링 크기 유효하지 않음
        EngineStdOut("mapWindowToStageCoordinates: Render dimensions are zero or negative.", 2);
        return false;
    }

    float fullStageWidthTex = static_cast<float>(PROJECT_STAGE_WIDTH);
    float fullStageHeightTex = static_cast<float>(PROJECT_STAGE_HEIGHT);

    float currentSrcViewWidth = fullStageWidthTex / this->zoomFactor; // 현재 줌 계수 적용된 소스 뷰 너비
    float currentSrcViewHeight = fullStageHeightTex / this->zoomFactor;
    float currentSrcViewX = (fullStageWidthTex - currentSrcViewWidth) / 2.0f;
    float currentSrcViewY = (fullStageHeightTex - currentSrcViewHeight) / 2.0f;

    float stageContentAspectRatio = fullStageWidthTex / fullStageHeightTex;
    SDL_FRect finalDisplayDstRect; // 최종 화면 표시 영역
    float windowAspectRatio = static_cast<float>(windowRenderW) / static_cast<float>(windowRenderH);

    if (windowAspectRatio >= stageContentAspectRatio)
    {
        // 윈도우가 스테이지보다 넓거나 같은 비율
        finalDisplayDstRect.h = static_cast<float>(windowRenderH);
        finalDisplayDstRect.w = finalDisplayDstRect.h * stageContentAspectRatio;
        finalDisplayDstRect.x = (static_cast<float>(windowRenderW) - finalDisplayDstRect.w) / 2.0f;
        finalDisplayDstRect.y = 0.0f;
    }
    else
    {
        finalDisplayDstRect.w = static_cast<float>(windowRenderW); // 윈도우가 스테이지보다 좁은 비율
        finalDisplayDstRect.h = finalDisplayDstRect.w / stageContentAspectRatio;
        finalDisplayDstRect.x = 0.0f;
        finalDisplayDstRect.y = (static_cast<float>(windowRenderH) - finalDisplayDstRect.h) / 2.0f;
    }

    if (finalDisplayDstRect.w <= 0.0f || finalDisplayDstRect.h <= 0.0f)
    {
        EngineStdOut("mapWindowToStageCoordinates: Calculated final display rect has zero or negative dimension.",
                     2); // 계산된 표시 영역 크기 오류
        return false;
    }

    if (static_cast<float>(windowMouseX) < finalDisplayDstRect.x || static_cast<float>(windowMouseX) >= finalDisplayDstRect.x + finalDisplayDstRect.w ||
        static_cast<float>(windowMouseY) < finalDisplayDstRect.y || static_cast<float>(windowMouseY) >= finalDisplayDstRect.y + finalDisplayDstRect.h)
    {
        // 마우스가 스테이지 표시 영역 밖에 있음
        return false;
    }
    float normX_on_displayed_stage = (static_cast<float>(windowMouseX) - finalDisplayDstRect.x) / finalDisplayDstRect.w;
    // 표시된 스테이지 내 정규화된 X 좌표
    float normY_on_displayed_stage = (static_cast<float>(windowMouseY) - finalDisplayDstRect.y) / finalDisplayDstRect.h;

    float texture_coord_x_abs = currentSrcViewX + (normX_on_displayed_stage * currentSrcViewWidth); // 텍스처 내 절대 X 좌표
    float texture_coord_y_abs = currentSrcViewY + (normY_on_displayed_stage * currentSrcViewHeight);
    // 스테이지 좌표계로 변환 (중앙 0,0, Y 위쪽)
    stageX = texture_coord_x_abs - (fullStageWidthTex / 2.0f);
    stageY = (fullStageHeightTex / 2.0f) - texture_coord_y_abs;

    return true;
}

void Engine::processInput(const SDL_Event &event, float deltaTime)
{
    if (m_textInputActive)
    {
        // 텍스트 입력 모드가 활성화된 경우
        if (event.type == SDL_EVENT_TEXT_INPUT)
        {
            std::lock_guard<std::mutex> lock(m_textInputMutex); // m_currentTextInputBuffer 접근 보호
            m_currentTextInputBuffer += event.text.text;
            // EngineStdOut("Text input: " + m_currentTextInputBuffer, 3);
        }
        else if (event.type == SDL_EVENT_KEY_DOWN)
        {
            std::lock_guard<std::mutex> lock(m_textInputMutex);
            if (event.key.scancode == SDLK_BACKSPACE && !m_currentTextInputBuffer.empty())
            {
                m_currentTextInputBuffer.pop_back();
                // EngineStdOut("Backspace. Buffer: " + m_currentTextInputBuffer, 3);
            }
            else if (event.key.scancode == SDLK_RETURN || event.key.scancode == SDLK_KP_ENTER)
            {
                m_lastAnswer = m_currentTextInputBuffer;
                m_textInputActive = false; // 입력 완료, 플래그 해제

                // 질문 다이얼로그 제거
                Entity *entity = getEntityById_nolock(m_textInputRequesterObjectId);
                if (entity)
                {
                    entity->removeDialog();
                }

                m_textInputCv.notify_all(); // 대기 중인 스크립트 스레드 깨우기
                EngineStdOut("Enter pressed. Input complete. Answer: " + m_lastAnswer, 0);
            }
        }
        // 텍스트 입력 중에는 다른 키 입력(예: keyPressedScripts)을 무시할 수 있도록 return 또는 플래그 사용
        return; // 텍스트 입력 중에는 다른 게임플레이 입력 처리 방지
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        // 마우스 버튼 누름 이벤트
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            bool uiClicked = false; // UI 요소 클릭 여부
            int mouseX = event.button.x;
            int mouseY = event.button.y;
            if (this->specialConfig.showZoomSlider && // 줌 슬라이더 클릭 확인
                mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH &&
                mouseY >= SLIDER_Y - 5 && mouseY <= SLIDER_Y + SLIDER_HEIGHT + 5)
            {
                float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
                this->zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
                this->zoomFactor = max(MIN_ZOOM, min(MAX_ZOOM, this->zoomFactor));
                this->m_isDraggingZoomSlider = true;
                uiClicked = true;
            }
            // 개별 HUD 변수 드래그 확인
            if (!uiClicked && !m_HUDVariables.empty())
            {
                float itemHeight = 22.0f;
                float itemPadding = 3.0f;
                float singleBoxHeight = itemHeight + 2 * itemPadding;
                float containerBorderWidth = 1.0f;    // drawHUD와 일치
                float minContainerFixedWidth = 80.0f; // Matches drawHUD
                int windowW_render = 0, windowH_render = 0;
                if (renderer)
                {
                    SDL_GetRenderOutputSize(renderer, &windowW_render, &windowH_render);
                }
                float screenCenterX = static_cast<float>(windowW_render) / 2.0f;
                float screenCenterY = static_cast<float>(windowH_render) / 2.0f;

                for (int i = 0; i < m_HUDVariables.size(); ++i)
                {
                    const auto &var_item = m_HUDVariables[i];
                    if (!var_item.isVisible)
                        continue;

                    SDL_FRect varRect;
                    float itemActualWidthForHitTest;
                    float itemActualHeightForHitTest; // 충돌 검사를 위한 실제 아이템 너비/높이

                    // 엔트리 좌표를 스크린 좌표로 변환
                    float itemScreenX = screenCenterX + var_item.x;
                    float itemScreenY = screenCenterY - var_item.y;

                    if (var_item.variableType == "list" && var_item.width > 0)
                    {
                        itemActualWidthForHitTest = var_item.width;
                        itemActualHeightForHitTest = var_item.height;
                        varRect = {itemScreenX, itemScreenY, itemActualWidthForHitTest, itemActualHeightForHitTest};

                        // 리스트의 리사이즈 핸들 클릭 확인
                        SDL_FRect resizeHandleRect = {
                            itemScreenX + var_item.width - LIST_RESIZE_HANDLE_SIZE,
                            itemScreenY + var_item.height - LIST_RESIZE_HANDLE_SIZE,
                            LIST_RESIZE_HANDLE_SIZE,
                            LIST_RESIZE_HANDLE_SIZE};

                        if (static_cast<float>(mouseX) >= resizeHandleRect.x && static_cast<float>(mouseX) <= resizeHandleRect.x + resizeHandleRect.w &&
                            static_cast<float>(mouseY) >= resizeHandleRect.y && static_cast<float>(mouseY) <= resizeHandleRect.y + resizeHandleRect.h)
                        {
                            m_draggedHUDVariableIndex = i;
                            m_currentHUDDragState = HUDDragState::RESIZING;
                            // 오프셋: 마우스 스크린 위치 - (아이템 스크린 우하단 모서리)
                            m_draggedHUDVariableMouseOffsetX =
                                static_cast<float>(mouseX) - (itemScreenX + var_item.width);
                            m_draggedHUDVariableMouseOffsetY =
                                static_cast<float>(mouseY) - (itemScreenY + var_item.height);
                            uiClicked = true;
                            EngineStdOut("Started resizing HUD list: " + var_item.name, 0); // HUD 리스트 크기 조절 시작
                            break;
                        }
                    }
                    else
                    {
                        // For other types, calculate width similar to drawHUD
                        // For non-list items, use transient_render_width if available and seems reasonable.
                        itemActualWidthForHitTest = var_item.transient_render_width;
                        if (itemActualWidthForHitTest <= 0)
                        {
                            // 아직 렌더링되지 않았다면 대체값 사용
                            itemActualWidthForHitTest = minContainerFixedWidth;
                        }
                        itemActualHeightForHitTest = singleBoxHeight;
                        varRect = {itemScreenX, itemScreenY, itemActualWidthForHitTest, itemActualHeightForHitTest};
                    }

                    // 일반 이동을 위한 클릭 확인 (리사이즈 핸들이 아닐 경우)
                    if (!uiClicked && // 리사이즈 핸들이 이미 클릭되지 않았는지 확인
                        static_cast<float>(mouseX) >= varRect.x && static_cast<float>(mouseX) <= varRect.x + varRect.w &&
                        static_cast<float>(mouseY) >= varRect.y && static_cast<float>(mouseY) <= varRect.y + varRect.h)
                    {
                        m_draggedHUDVariableIndex = i;
                        m_currentHUDDragState = HUDDragState::MOVING;
                        m_draggedHUDVariableMouseOffsetX = static_cast<float>(mouseX) - itemScreenX;
                        // 마우스 스크린 위치 - 아이템 스크린 좌상단 X
                        m_draggedHUDVariableMouseOffsetY = static_cast<float>(mouseY) - itemScreenY;
                        // 마우스 스크린 위치 - 아이템 스크린 좌상단 Y
                        uiClicked = true;
                        EngineStdOut(
                            "Started dragging HUD variable: " + var_item.name + " (Type: " + var_item.variableType +
                                ")",
                            0); // HUD 변수 드래그 시작
                        break;
                    }
                }
            }
            if (!uiClicked && m_gameplayInputActive)
            {
                // UI 클릭이 아니고 게임플레이 입력이 활성화된 경우

                if (!m_mouseClickedScripts.empty())
                {
                    for (const auto &scriptPair : m_mouseClickedScripts)
                    {
                        const string &objectId = scriptPair.first;
                        const Script *scriptPtr = scriptPair.second;
                        Entity *currentEntity = getEntityById(objectId);
                        if (currentEntity)
                        {
                            if (currentEntity->isVisible()) // 엔티티가 보이는지 확인
                            {
                                // 현재 씬에 속하거나 전역 오브젝트인지 확인
                                const ObjectInfo *objInfoPtr = getObjectInfoById(objectId); // ObjectInfo 가져오기
                                if (objInfoPtr)
                                {
                                    bool isInCurrentScene = (objInfoPtr->sceneId == currentSceneId);
                                    bool isGlobal = (objInfoPtr->sceneId == "global" || objInfoPtr->sceneId.empty());
                                    if (isInCurrentScene || isGlobal)
                                    {
                                        this->dispatchScriptForExecution(objectId, scriptPtr, getCurrentSceneId(), 0.0f);
                                    }
                                }
                                else
                                {
                                    EngineStdOut(
                                        "Warning: ObjectInfo not found for entity ID '" + objectId +
                                            "' during mouse_clicked event processing. Script not run.",
                                        1);
                                }
                            }
                        }
                    }
                }

                float stageMouseX = 0.0f, stageMouseY = 0.0f;
                if (mapWindowToStageCoordinates(mouseX, mouseY, stageMouseX, stageMouseY)) // 윈도우 좌표를 스테이지 좌표로 변환
                {
                    for (int i = static_cast<int>(objects_in_order.size()) - 1; i >= 0; --i)
                    {
                        const ObjectInfo &objInfo = objects_in_order[i];
                        const string &objectId = objInfo.id;

                        bool isInCurrentScene = (objInfo.sceneId == currentSceneId); // 현재 씬에 있는지 확인
                        bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty());
                        if (!isInCurrentScene && !isGlobal)
                        {
                            continue;
                        }

                        Entity *entity = getEntityById(objectId);
                        if (!entity || !entity->isVisible()) // 엔티티가 없거나 보이지 않으면 건너뜀
                        {
                            continue;
                        }

                        if (entity->isPointInside(stageMouseX, stageMouseY))
                        {
                            // 마우스가 엔티티 내부에 있으면
                            m_pressedObjectId = objectId;

                            for (const auto &clickScriptPair : m_whenObjectClickedScripts)
                            {
                                if (clickScriptPair.first == objectId)
                                {
                                    EngineStdOut("Dispatching 'when_object_click' for object: " + entity->getId(), 0);
                                    this->dispatchScriptForExecution(objectId, clickScriptPair.second,
                                                                     getCurrentSceneId(), deltaTime);
                                }
                            }
                            break;
                        }
                    }
                }
                else
                {
                    EngineStdOut("Warning: Could not map window to stage coordinates for object click.", 1);
                    // 오브젝트 클릭을 위한 좌표 변환 실패
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
        if (m_gameplayInputActive) // 키 누름 이벤트
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
                    EngineStdOut(
                        " -> Dispatching 'Key Pressed' script for object: " + objectId + " (Key: " +
                            SDL_GetScancodeName(scancode) + ")",
                        0);
                    this->dispatchScriptForExecution(objectId, scriptPtr, getCurrentSceneId(), deltaTime);
                }
            }
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        if (this->m_isDraggingZoomSlider && (event.motion.state & SDL_BUTTON_LMASK)) // 줌 슬라이더 드래그 중
        {
            int mouseX = event.motion.x;
            // 슬라이더 범위 내에서 줌 계수 업데이트
            if (mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH)
            {
                float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
                this->zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
                this->zoomFactor = max(MIN_ZOOM, min(MAX_ZOOM, this->zoomFactor));
            }
        }
        // HUD 변수 드래그 중 마우스 이동 처리
        else if (m_draggedHUDVariableIndex != -1 && (event.motion.state & SDL_BUTTON_LMASK)) // HUD 변수 드래그 중
        {
            HUDVariableDisplay &draggedVar = m_HUDVariables[m_draggedHUDVariableIndex];

            int mouseX = event.motion.x;
            int mouseY = event.motion.y;
            int windowW = 0, windowH = 0;
            if (renderer)
            {
                SDL_GetRenderOutputSize(renderer, &windowW, &windowH);
            }
            float screenCenterX = static_cast<float>(windowW) / 2.0f;
            float screenCenterY = static_cast<float>(windowH) / 2.0f;

            if (m_currentHUDDragState == HUDDragState::MOVING)
            {
                // 이동 상태
                // 새 스크린 좌표 계산
                float newScreenX = static_cast<float>(mouseX) - m_draggedHUDVariableMouseOffsetX;
                float newScreenY = static_cast<float>(mouseY) - m_draggedHUDVariableMouseOffsetY;

                // 아이템 크기 가져오기 (클램핑용)
                float draggedItemWidth = 0.0f;
                float draggedItemHeight = 0.0f;

                if (draggedVar.variableType == "list")
                {
                    draggedItemWidth = draggedVar.width;   // 리스트 너비
                    draggedItemHeight = draggedVar.height; // 리스트 높이
                }
                else
                {
                    draggedItemWidth = (draggedVar.transient_render_width > 0)
                                           ? draggedVar.transient_render_width
                                           : 180.0f;       // 렌더링된 너비 또는 기본값
                    float itemHeight_const_motion = 22.0f; // 아이템 높이 상수
                    float itemPadding_const_motion = 3.0f;
                    draggedItemHeight = itemHeight_const_motion + 2 * itemPadding_const_motion;
                }

                if (draggedItemWidth <= 0)
                    draggedItemWidth = 180.0f;
                if (draggedItemHeight <= 0)
                    draggedItemHeight = 28.0f;

                // 스크린 좌표계에서 클램핑
                float clampedNewScreenX = newScreenX;
                float clampedNewScreenY = newScreenY;
                if (windowW > 0 && draggedItemWidth > 0)
                {
                    // 창 경계 내로 위치 제한
                    clampedNewScreenX = clamp(newScreenX, 0.0f, static_cast<float>(windowW) - draggedItemWidth);
                }
                if (windowH > 0 && draggedItemHeight > 0)
                {
                    clampedNewScreenY = clamp(newScreenY, 0.0f, static_cast<float>(windowH) - draggedItemHeight);
                }

                // 클램핑된 스크린 좌표를 엔트리 좌표로 변환하여 저장
                draggedVar.x = clampedNewScreenX - screenCenterX;
                draggedVar.y = screenCenterY - clampedNewScreenY;
            }
            else if (m_currentHUDDragState == HUDDragState::RESIZING && draggedVar.variableType == "list")
            {
                // 크기 조절 상태 (리스트만 해당)
                // draggedVar.x, draggedVar.y는 엔트리 좌표. 이를 스크린 좌상단 좌표로 변환.
                float itemScreenX = screenCenterX + draggedVar.x;
                float itemScreenY = screenCenterY - draggedVar.y;

                // m_draggedHUDVariableMouseOffset은 (마우스 스크린 위치 - 아이템 스크린 우하단 모서리)의 오프셋
                // 새 너비/높이는 (현재 마우스 스크린 위치 - 아이템 스크린 좌상단 위치 - 오프셋)
                float newWidth = static_cast<float>(mouseX) - itemScreenX - m_draggedHUDVariableMouseOffsetX;
                float newHeight = static_cast<float>(mouseY) - itemScreenY - m_draggedHUDVariableMouseOffsetY;

                draggedVar.width = max(MIN_LIST_WIDTH, newWidth);
                draggedVar.height = max(MIN_LIST_HEIGHT, newHeight);

                // 창 경계를 넘어가지 않도록 추가 클램핑
                // 너비는 (창 너비 - 아이템의 스크린 X 시작 위치)를 넘을 수 없음
                if (windowW > 0)
                {
                    draggedVar.width = min(draggedVar.width, static_cast<float>(windowW) - itemScreenX);
                }
                // 높이는 (창 높이 - 아이템의 스크린 Y 시작 위치)를 넘을 수 없음
                if (windowH > 0)
                {
                    draggedVar.height = min(draggedVar.height, static_cast<float>(windowH) - itemScreenY);
                }
            }
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT) // 마우스 왼쪽 버튼 뗌 이벤트
        {
            bool uiInteractionReleased = false; // UI 상호작용 (드래그/리사이즈) 해제 여부
            if (this->m_isDraggingZoomSlider)
            {
                this->m_isDraggingZoomSlider = false;
                uiInteractionReleased = true;
            }
            if (m_draggedHUDVariableIndex != -1)
            {
                if (m_currentHUDDragState == HUDDragState::MOVING)
                {
                    EngineStdOut("Stopped dragging HUD variable: " + m_HUDVariables[m_draggedHUDVariableIndex].name,
                                 0); // HUD 변수 드래그 중지
                }
                else if (m_currentHUDDragState == HUDDragState::RESIZING)
                {
                    EngineStdOut("Stopped resizing HUD list: " + m_HUDVariables[m_draggedHUDVariableIndex].name, 0);
                    // HUD 리스트 크기 조절 중지
                }
                m_draggedHUDVariableIndex = -1;
                m_currentHUDDragState = HUDDragState::NONE;
                uiInteractionReleased = true;
            }

            if (m_gameplayInputActive && !uiInteractionReleased)
            {
                // 게임플레이 입력 활성화 상태이고 UI 상호작용이 해제되지 않은 경우

                if (!m_mouseClickCanceledScripts.empty())
                {
                    for (const auto &scriptPair : m_mouseClickCanceledScripts)
                    {
                        const string &objectId = scriptPair.first;
                        const Script *scriptPtr = scriptPair.second;
                        Entity *currentEntity = getEntityById(objectId);
                        if (currentEntity && currentEntity->isVisible())
                        {
                            // 엔티티가 존재하고 보이는 경우
                            // 현재 씬에 속하거나 전역 오브젝트인지 확인
                            const ObjectInfo *objInfoPtr = getObjectInfoById(objectId); // ObjectInfo 가져오기
                            if (objInfoPtr)
                            {
                                std::string sceneContext = getCurrentSceneId(); // 현재 씬 컨텍스트 가져오기
                                bool isInCurrentScene = (objInfoPtr->sceneId == currentSceneId);
                                bool isGlobal = (objInfoPtr->sceneId == "global" || objInfoPtr->sceneId.empty());
                                if (isInCurrentScene || isGlobal)
                                {
                                    this->dispatchScriptForExecution(objectId, scriptPtr, sceneContext, deltaTime);
                                }
                            }
                            else
                            {
                                EngineStdOut(
                                    "Warning: ObjectInfo not found for entity ID '" + objectId +
                                        "' during mouse_click_canceled event processing. Script not run.",
                                    1);
                            }
                        }
                        else if (!currentEntity)
                        {
                            // currentEntity가 null이면 아무것도 할 수 없음. 이 경우는 로직 오류일 수 있음.
                            EngineStdOut(
                                "Warning: mouse_click_canceled event for null entity ID '" + objectId +
                                    "'. Script not run.",
                                1);
                        }
                    }
                }
                // 눌렸던 오브젝트에 대한 클릭 취소 스크립트 실행
                if (!m_pressedObjectId.empty())
                {
                    const string &canceledObjectId = m_pressedObjectId;
                    for (const auto &scriptPair : m_whenObjectClickCanceledScripts)
                    {
                        std::string sceneContext = getCurrentSceneId(); // 현재 씬 컨텍스트 가져오기
                        if (scriptPair.first == canceledObjectId)
                        {
                            EngineStdOut("Dispatching 'when_object_click_canceled' for object: " + canceledObjectId, 0);
                            this->dispatchScriptForExecution(canceledObjectId, scriptPair.second, sceneContext, deltaTime);
                        }
                    }
                }
            }
            m_pressedObjectId = ""; // 눌린 오브젝트 ID 초기화
        }
    }
}

/**
 * @brief 두 점 (x1, y1)에서 (x2, y2)를 바라보는 각도를 계산합니다.
 * 각도는 EntryJS/Scratch 좌표계 (0도는 위, 90도는 오른쪽)를 따릅니다.
 * @param x1 시작점의 X 좌표
 * @param y1 시작점의 Y 좌표
 * @param x2 목표점의 X 좌표
 * @param y2 목표점의 Y 좌표
 * @return 0-360 범위의 각도 (도)
 */
double Engine::getAngle(double x1, double y1, double x2, double y2) const
{
    double deltaX = x2 - x1;
    double deltaY = y2 - y1; // Y축이 위로 향하는 좌표계이므로 그대로 사용

    double angleRad = atan2(deltaY, deltaX);           // 표준 수학 각도 (라디안, 0도는 오른쪽)
    double angleDegMath = angleRad * 180.0 / PI_VALUE; // 표준 수학 각도 (도)

    // EntryJS/Scratch 각도로 변환 (0도는 위쪽, 90도는 오른쪽)
    double angleDegEntry = 90.0 - angleDegMath;

    // 0-360 범위로 정규화
    angleDegEntry = fmod(angleDegEntry, 360.0);
    if (angleDegEntry < 0)
    {
        angleDegEntry += 360.0;
    }
    return angleDegEntry;
}

/**
 * @brief Calculates the angle in degrees from a given point (entityX, entityY) to the current stage mouse position.
 * The angle is compatible with EntryJS/Scratch coordinate system (0 degrees is up, 90 degrees is right).
 * @param entityX The X coordinate of the entity (origin point).
 * @param entityY The Y coordinate of the entity (origin point).
 * @return The angle in degrees (0-360). Returns 0 if the mouse is not on stage.
 */
double Engine::getCurrentStageMouseAngle(double entityX, double entityY) const
{
    if (!m_isMouseOnStage)
    {
        // Or return a specific value indicating mouse is not on stage,
        // but current usage in BlockExecutor already checks m_isMouseOnStage.
        // Returning 0 or current entity rotation might be alternatives if needed elsewhere.
        return 0.0; // Default if mouse is not on stage
    }

    double deltaX = m_currentStageMouseX - entityX;
    double deltaY = m_currentStageMouseY - entityY; // Y-up system, so this is correct

    double angleRad = atan2(deltaY, deltaX);
    double angleDegMath = angleRad * 180.0 / PI_VALUE; // 0 is right, 90 is up
    double angleDegEntry = 90.0 - angleDegMath;        // Convert to 0 is up, 90 is right

    // Normalize to 0-360 range
    angleDegEntry = fmod(angleDegEntry, 360.0);
    if (angleDegEntry < 0)
    {
        angleDegEntry += 360.0;
    }
    return angleDegEntry;
}

void Engine::setVisibleHUDVariables(const vector<HUDVariableDisplay> &variables)
{
    m_HUDVariables = variables;
    // EngineStdOut("HUD variables updated. Count: " + to_string(m_visibleHUDVariables.size()), 0);
}

SDL_Scancode Engine::mapStringToSDLScancode(const string &keyIdentifier) const
{
    static const map<string, SDL_Scancode> jsKeyCodeMap = {
        // JavaScript 키 코드와 SDL_Scancode 매핑
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

    SDL_Scancode sc = SDL_GetScancodeFromName(keyIdentifier.c_str()); // 이름으로 Scancode 검색
    if (sc != SDL_SCANCODE_UNKNOWN)
    {
        return sc;
    }
    // 단일 소문자 알파벳인 경우 대문자로 변환하여 다시 시도
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
    if (startButtonScripts.empty()) // 시작 버튼 클릭 스크립트가 없으면
    {
        IsScriptStart = false;
        EngineStdOut("No 'Start Button Clicked' scripts found to run.", 1);
        return;
    }
    else
    {
        IsScriptStart = true;
        EngineStdOut("Running 'Start Button Clicked' scripts...", 0);
    }

    for (const auto &scriptPair : startButtonScripts)
    {
        const string &objectId = scriptPair.first;
        const Script *scriptPtr = scriptPair.second;
        EngineStdOut(" -> Running script for object: " + objectId, 3);
        std::string sceneContext = getCurrentSceneId(); // 현재 씬 컨텍스트 가져오기
        this->dispatchScriptForExecution(objectId, scriptPtr, sceneContext, 0.0);
        m_gameplayInputActive = true;
    }
    EngineStdOut("Finished running 'Start Button Clicked' scripts.", 0);
}

void Engine::initFps()
{
    lastfpstime = SDL_GetTicks(); // 마지막 FPS 측정 시간
    framecount = 0;
    currentFps = 0.0f;
    EngineStdOut("FPS counter initialized.", 0);
}

void Engine::setfps(int fps)
{
    if (fps > 0)
    {
        // 유효한 FPS 값인 경우에만 설정
        this->specialConfig.TARGET_FPS = fps;
        EngineStdOut("Target FPS set to: " + to_string(this->specialConfig.TARGET_FPS), 0);
    }
    else
    {
        EngineStdOut(
            "Attempted to set invalid Target FPS: " + to_string(fps) + ". Keeping current TARGET_FPS: " + to_string(this->specialConfig.TARGET_FPS), 1);
    }
}

void Engine::updateFps()
{
    framecount++;
    Uint64 now = SDL_GetTicks(); // 현재 시간
    Uint64 delta = now - lastfpstime;

    if (delta >= 1000)
    {
        // 1초 이상 경과 시 FPS 업데이트
        currentFps = static_cast<float>(framecount * 1000.0) / delta;
        lastfpstime = now;
        framecount = 0;
    }
}

void Engine::startProjectTimer()
{
    m_projectTimerRunning = true; // 프로젝트 타이머 시작
    EngineStdOut("Project timer started.", 0);
}

void Engine::stopProjectTimer()
{
    m_projectTimerRunning = false; // 프로젝트 타이머 중지
    EngineStdOut("Project timer stopped.", 0);
}

void Engine::resetProjectTimer()
{
    m_projectTimerValue = 0.0; // 프로젝트 타이머 리셋
    EngineStdOut("Project timer reset.", 0);
}

void Engine::showProjectTimer(bool show)
{
    for (auto &var : m_HUDVariables) // HUD 변수 중 타이머 타입의 가시성 설정
    {
        if (var.variableType == "timer")
        {
            var.isVisible = show;
            EngineStdOut(
                string("Project timer ('") + var.name + "') visibility set to: " + (show ? "Visible" : "Hidden"), 3);
            return; // Assuming only one timer variable for now
        }
    }
    EngineStdOut("showProjectTimer: No timer variable found in HUDVariables to set visibility.", 1);
}

void Engine::showAnswerValue(bool show)
{
    for (auto &var : m_HUDVariables)
    {
        if (var.variableType == "answer")
        {
            var.isVisible = show;
            EngineStdOut(
                string("Project Answer ('") + var.name + "') visibility set to: " + (show ? "Visible" : "Hidden"), 3);
            return; // Assuming only one timer variable for now
        }
    }
}

double Engine::getProjectTimerValue() const
{
    if (m_projectTimerRunning) // 타이머가 실행 중이면 현재 경과 시간 반환
    {
        Uint64 now = SDL_GetTicks();
        Uint64 delta = now - lastfpstime;
        return static_cast<double>(now - m_projectTimerStartTime) / 1000.0;
    }
    return m_projectTimerValue;
}

Entity *Engine::getEntityById(const string &id)
{
    auto it = entities.find(id); // ID로 엔티티 검색
    if (it != entities.end())
    {
        return it->second;
    }

    return nullptr;
}

// Private method, assumes m_engineDataMutex is already locked by the caller.
Entity *Engine::getEntityById_nolock(const std::string &id)
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
        EngineStdOut("renderLoadingScreen: Renderer not available.", 1); // 렌더러 사용 불가
        return;
    }

    SDL_SetRenderDrawColor(this->renderer, 30, 30, 30, 255);
    SDL_RenderClear(this->renderer); // 배경색 어두운 회색으로 지우기

    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH);
    // 로딩 바 위치 및 크기
    int barWidth = 400;
    int barHeight = 30;
    int barX = (windowW - barWidth) / 2;
    int barY = (windowH - barHeight) / 2;

    SDL_FRect bgRect = {
        static_cast<float>(barX), static_cast<float>(barY), static_cast<float>(barWidth), static_cast<float>(barHeight)};
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255); // 로딩 바 배경
    SDL_RenderFillRect(renderer, &bgRect);

    SDL_FRect innerBgRect = {
        static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(barWidth - 4),
        static_cast<float>(barHeight - 4)};
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); // 로딩 바 내부 배경
    SDL_RenderFillRect(renderer, &innerBgRect);

    float progressPercent = 0.0f;
    if (totalItemsToLoad > 0)
    {
        progressPercent = static_cast<float>(loadedItemCount) / totalItemsToLoad; // 진행률 계산
    }
    progressPercent = min(1.0f, max(0.0f, progressPercent));

    int progressWidth = static_cast<int>((barWidth - 4) * progressPercent);
    SDL_FRect progressRect = {
        static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(progressWidth),
        static_cast<float>(barHeight - 4)};
    SDL_SetRenderDrawColor(renderer, 255, 165, 0, 255);
    SDL_RenderFillRect(renderer, &progressRect);

    if (loadingScreenFont)
    {
        SDL_Color textColor = {255, 255, 255, 255}; // 텍스트 색상
        ostringstream percentStream;
        percentStream << fixed << setprecision(0) << (progressPercent * 100.0f) << "%";
        string percentText = percentStream.str();
        // 진행률 텍스트 렌더링
        SDL_Surface *surfPercent = TTF_RenderText_Blended(percentFont, percentText.c_str(), percentText.size(),
                                                          textColor);
        if (surfPercent)
        {
            SDL_Texture *texPercent = SDL_CreateTextureFromSurface(renderer, surfPercent);
            if (texPercent)
            {
                SDL_FRect dstRect = {
                    barX + (barWidth - static_cast<float>(surfPercent->w)) / 2.0f,
                    barY + (barHeight - static_cast<float>(surfPercent->h)) / 2.0f,
                    static_cast<float>(surfPercent->w),
                    static_cast<float>(surfPercent->h)};
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
        // 브랜드 이름 렌더링 (설정된 경우)
        if (!specialConfig.BRAND_NAME.empty())
        {
            SDL_Surface *surfBrand = TTF_RenderText_Blended(loadingScreenFont, specialConfig.BRAND_NAME.c_str(),
                                                            specialConfig.BRAND_NAME.size(), textColor);
            if (surfBrand)
            {
                SDL_Texture *texBrand = SDL_CreateTextureFromSurface(renderer, surfBrand);
                if (texBrand)
                {
                    SDL_FRect dstRect = {
                        (windowW - static_cast<float>(surfBrand->w)) / 2.0f,
                        barY - static_cast<float>(surfBrand->h) - 10.0f, static_cast<float>(surfBrand->w),
                        static_cast<float>(surfBrand->h)};
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
        else
        {
            // 아니면 무엇이 로드하는지 보여줌.
            SDL_Surface *surfBrand = TTF_RenderText_Blended(loadingScreenFont, LOADING_METHOD_NAME.c_str(),
                                                            specialConfig.BRAND_NAME.size(), textColor);
            if (surfBrand)
            {
                SDL_Texture *texBrand = SDL_CreateTextureFromSurface(renderer, surfBrand);
                if (texBrand)
                {
                    SDL_FRect dstRect = {
                        (windowW - static_cast<float>(surfBrand->w)) / 2.0f,
                        barY - static_cast<float>(surfBrand->h) - 10.0f, static_cast<float>(surfBrand->w),
                        static_cast<float>(surfBrand->h)};
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
        // 프로젝트 이름 렌더링 (설정된 경우)
        if (specialConfig.SHOW_PROJECT_NAME && !PROJECT_NAME.empty())
        {
            SDL_Surface *surfProject = TTF_RenderText_Blended(loadingScreenFont, PROJECT_NAME.c_str(),
                                                              PROJECT_NAME.size(), textColor);
            if (surfProject)
            {
                SDL_Texture *texProject = SDL_CreateTextureFromSurface(renderer, surfProject);
                if (texProject)
                {
                    // 프로젝트 이름은 상단으로
                    SDL_FRect dstRect = {
                        (windowW - static_cast<float>(surfProject->w)) / 2.0f,
                        barY + static_cast<float>(surfProject->h) - 130.5f, static_cast<float>(surfProject->w),
                        static_cast<float>(surfProject->h)};
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
        EngineStdOut("renderLoadingScreen: Loading screen font not available. Cannot draw text.", 1); // 로딩 화면 폰트 사용 불가
    }

    SDL_RenderPresent(this->renderer); // 화면에 렌더링된 내용 표시
}

// 현재 씬 ID 반환
const string &Engine::getCurrentSceneId() const
{
    return currentSceneId;
}

/**
 * @brief 메시지 박스 표시
 *
 * @param message 메시지 내용
 * @param IconType 아이콘 종류 (SDL_MESSAGEBOX_ERROR, SDL_MESSAGEBOX_WARNING, SDL_MESSAGEBOX_INFORMATION)
 * @param showYesNo 예 / 아니오
 */
bool Engine::showMessageBox(const string &message, int IconType, bool showYesNo) const
{
    Uint32 flags = 0;
    const char *title = OMOCHA_ENGINE_NAME;

    switch (IconType)
    {
    case SDL_MESSAGEBOX_ERROR:
#ifdef _WIN32
        MessageBeep(MB_ICONERROR);
#endif
        flags = SDL_MESSAGEBOX_ERROR; // 오류 아이콘
        title = "Omocha is Broken";
        break;
    case SDL_MESSAGEBOX_WARNING:
#ifdef _WIN32
        MessageBeep(MB_ICONWARNING);
#endif
        flags = SDL_MESSAGEBOX_WARNING; // 경고 아이콘
        title = PROJECT_NAME.c_str();
        break;
    case SDL_MESSAGEBOX_INFORMATION:
#ifdef _WIN32
        MessageBeep(MB_ICONINFORMATION);
#endif
        flags = SDL_MESSAGEBOX_INFORMATION; // 정보 아이콘
        title = PROJECT_NAME.c_str();
        break;
    default:
        // 알 수 없는 아이콘 타입일 경우 기본 정보 아이콘 사용
        EngineStdOut(
            "Unknown IconType passed to showMessageBox: " + to_string(IconType) + ". Using default INFORMATION.",
            1);
        flags = SDL_MESSAGEBOX_INFORMATION;
        title = "Message";
        break;
    }

    const SDL_MessageBoxButtonData buttons[]{
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "예"},      // "Yes" 버튼
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "아니오"}}; // "No" 버튼
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
        // 예/아니오 버튼 표시
        int buttonid_press = -1;
        if (SDL_ShowMessageBox(&messageboxData, &buttonid_press) < 0)
        {
            EngineStdOut("Can't Showing MessageBox");
            return false; // Add return statement for the error case
        }
        else
        {
            if (buttonid_press == 0)
            {
                // "예" 버튼 클릭
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
        SDL_ShowSimpleMessageBox(flags, title, message.c_str(), this->window); // 단순 메시지 박스 (OK 버튼만)
        return true;
    }
    // If showYesNo was true and SDL_ShowMessageBox failed, this path might be reached.
    // Adding a default return false here to satisfy all control paths,
    // though the logic above should ideally cover it.
    // However, the primary fix is inside the if (SDL_ShowMessageBox < 0) block.
    // To be absolutely safe and ensure all paths return, especially if the above logic
    // might be refactored, a fallback return here is defensive.
    return false;
}

void Engine::activateTextInput(const std::string &requesterObjectId, const std::string &question,
                               const std::string &executionThreadId)
{
    EngineStdOut("Activating text input for object " + requesterObjectId + " with question: \"" + question + "\"", 0,
                 executionThreadId);

    std::unique_lock<std::mutex> lock(m_textInputMutex);

    // 이전 입력 상태 정리 및 새 입력 상태 설정
    m_currentTextInputBuffer.clear();
    m_textInputQuestionMessage = question;
    m_textInputRequesterObjectId = requesterObjectId;
    m_textInputActive = true;
    m_gameplayInputActive = false; // 텍스트 입력 중에는 일반 게임플레이 키 입력 비활성화

    // SDL 텍스트 입력 시작 (IME 등 활성화)
    SDL_StartTextInput(window);

    // Entity에 질문 다이얼로그 표시 요청 (Engine이 Entity를 직접 제어)
    Entity *entity = getEntityById_nolock(requesterObjectId); // m_engineDataMutex는 여기서 잠그지 않음 (m_textInputMutex와 별개)
    // 만약 getEntityById가 m_engineDataMutex를 사용한다면,
    // activateTextInput 호출 전에 해당 뮤텍스를 잠그거나,
    // getEntityById_nolock 같은 내부용 함수를 사용해야 함.
    // 여기서는 getEntityById_nolock이 m_engineDataMutex를 잠그지 않는다고 가정.
    // 또는, Engine의 entities 맵 접근 시 m_engineDataMutex를 사용하도록 수정.
    if (entity)
    {
        // 다이얼로그 표시는 메인 스레드에서 처리하는 것이 더 안전할 수 있으나,
        // Entity의 showDialog가 스레드 안전하다면 여기서 호출 가능.
        // 여기서는 Entity의 showDialog가 상태만 변경하고 실제 그리기는 메인 스레드에서 한다고 가정.
        entity->showDialog(question, "ask", 0); // 0 duration means it stays until explicitly removed
    }
    else
    {
        EngineStdOut("Warning: Entity " + requesterObjectId + " not found when trying to show 'ask' dialog.", 1,
                     executionThreadId);
    }

    EngineStdOut("Script thread " + executionThreadId + " waiting for text input...", 0, executionThreadId);
    m_textInputCv.wait(lock, [this]
                       { return !m_textInputActive || m_isShuttingDown; });

    // 입력 완료 또는 엔진 종료로 대기 상태 해제
    SDL_StopTextInput(window);    // SDL 텍스트 입력 종료
    m_gameplayInputActive = true; // 게임플레이 입력 다시 활성화

    if (m_isShuttingDown)
    {
        EngineStdOut("Text input cancelled due to engine shutdown for " + requesterObjectId, 1, executionThreadId);
    }
    else
    {
        EngineStdOut("Text input received for " + requesterObjectId + ". Last answer: \"" + m_lastAnswer + "\"", 0,
                     executionThreadId);
    }
}

std::string Engine::getLastAnswer() const
{
    std::lock_guard<std::mutex> lock(m_textInputMutex); // Protect access to m_lastAnswer
    return m_lastAnswer;
}

/**
 * @brief 엔진 로그출력
 *
 * @param s 출력할내용
 * @param LEVEL 수준 예) 0:정보, 1:경고, 2:오류, 3:디버그, 4:특수, 5:TREAD
 * @param ThreadID 쓰레드 ID
 */
void Engine::EngineStdOut(string s, int LEVEL, string TREADID) const
{
    std::lock_guard<std::mutex> lock(m_logMutex);
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
    case 5:
        prefix = "[EntryCPP THREAD " + TREADID + "]";
        color_code = ANSI_COLOR_CYAN + ANSI_STYLE_BOLD;
        break;
    default:
        prefix = "[LOG]";
        break;
    }

    printf("%s%s %s%s\n", color_code.c_str(), prefix.c_str(), s.c_str(), ANSI_COLOR_RESET.c_str()); // 콘솔에 색상 적용하여 출력

    string logMessage = format("{} {}", prefix, s); // 파일 로그용 메시지
    logger.log(logMessage);
}

void Engine::updateCurrentMouseStageCoordinates(int windowMouseX, int windowMouseY)
{
    float stageX_calc, stageY_calc;
    if (mapWindowToStageCoordinates(windowMouseX, windowMouseY, stageX_calc, stageY_calc))
    {
        // 윈도우 좌표를 스테이지 좌표로 변환 성공 시
        this->m_currentStageMouseX = stageX_calc;
        this->m_currentStageMouseY = stageY_calc;
        this->m_isMouseOnStage = true;
    }
    else
    {
        this->m_isMouseOnStage = false;
        // 마우스가 스테이지 밖에 있을 때의 처리
        // 엔트리는 마지막 유효 좌표를 유지하거나 0을 반환할 수 있습니다.
        // 현재는 m_isMouseOnStage 플래그로 구분하고, BlockExecutor에서 이 플래그를 확인하여 처리합니다.
        // 필요하다면 여기서 m_currentStageMouseX/Y를 특정 값(예: 0)으로 리셋할 수 있습니다.
        // this->m_currentStageMouseX = 0.0f;
        // this->m_currentStageMouseY = 0.0f;
    }
}

void Engine::goToScene(const string &sceneId)
{
    if (scenes.count(sceneId)) // 요청된 씬 ID가 존재하는 경우
    {
        if (currentSceneId == sceneId)
        {
            EngineStdOut("Already in scene: " + scenes[sceneId] + " (ID: " + sceneId + "). No change.", 0); // 이미 해당 씬임

            return;
        }
        currentSceneId = sceneId;
        EngineStdOut("Changed scene to: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        triggerWhenSceneStartScripts();
    }
    else
    {
        EngineStdOut("Error: Scene with ID '" + sceneId + "' not found. Cannot switch scene.", 2); // 씬 ID 찾을 수 없음
    }
}

void Engine::goToNextScene()
{
    if (m_sceneOrder.empty())
    {
        EngineStdOut("Cannot go to next scene: Scene order is not defined or no scenes loaded.", 1); // 씬 순서 정의 안됨
        return;
    }
    if (currentSceneId.empty())
    {
        EngineStdOut("Cannot go to next scene: Current scene ID is empty.", 1); // 현재 씬 ID 없음
        return;
    }

    auto it = find(m_sceneOrder.begin(), m_sceneOrder.end(), currentSceneId);
    if (it != m_sceneOrder.end())
    {
        size_t currentIndex = distance(m_sceneOrder.begin(), it);
        if (currentIndex + 1 < m_sceneOrder.size())
        {
            // 다음 씬으로 이동
            goToScene(m_sceneOrder[currentIndex + 1]);
        }
        else
        {
            EngineStdOut("Already at the last scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")",
                         0); // 이미 마지막 씬임
        }
    }
    else
    {
        // 현재 씬 ID를 씬 순서에서 찾을 수 없음
        EngineStdOut(
            "Error: Current scene ID '" + currentSceneId +
                "' not found in defined scene order. Cannot determine next scene.",
            2);
    }
}

void Engine::goToPreviousScene()
{
    if (m_sceneOrder.empty())
    {
        EngineStdOut("Cannot go to previous scene: Scene order is not defined or no scenes loaded.", 1); // 씬 순서 정의 안됨
        return;
    }
    if (currentSceneId.empty())
    {
        EngineStdOut("Cannot go to previous scene: Current scene ID is empty.", 1); // 현재 씬 ID 없음
        return;
    }

    auto it = find(m_sceneOrder.begin(), m_sceneOrder.end(), currentSceneId);
    if (it != m_sceneOrder.end())
    {
        size_t currentIndex = distance(m_sceneOrder.begin(), it);
        if (currentIndex > 0)
        {
            // 이전 씬으로 이동
            goToScene(m_sceneOrder[currentIndex - 1]);
        }
        else
        {
            EngineStdOut("Already at the first scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")",
                         0); // 이미 첫 번째 씬임
        }
    }
    else
    {
        // 현재 씬 ID를 씬 순서에서 찾을 수 없음
        EngineStdOut(
            "Error: Current scene ID '" + currentSceneId +
                "' not found in defined scene order. Cannot determine previous scene.",
            2);
    }
}

void Engine::triggerWhenSceneStartScripts()
{
    if (currentSceneId.empty())
    {
        EngineStdOut("Cannot trigger 'when_scene_start' scripts: Current scene ID is empty.", 1); // 현재 씬 ID 없음
        return;
    }
    EngineStdOut("Triggering 'when_scene_start' scripts for scene: " + currentSceneId, 0);
    for (const auto &scriptPair : m_whenStartSceneLoadedScripts)
    {
        // 씬 시작 시 실행될 스크립트들 순회
        const string &objectId = scriptPair.first;
        const Script *scriptPtr = scriptPair.second;

        bool executeForScene = false; // 해당 오브젝트가 현재 씬에서 실행되어야 하는지 확인
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
            EngineStdOut(
                "  -> Running 'when_scene_start' script for object ID: " + objectId + " in scene " + currentSceneId, 0);
            this->dispatchScriptForExecution(objectId, scriptPtr, currentSceneId, 0.0);
        }
    }
}

int Engine::getBlockCountForObject(const std::string &objectId) const
{
    auto it = objectScripts.find(objectId);
    if (it == objectScripts.end())
    {
        return 0;
    }
    int count = 0;
    for (const auto &script : it->second)
    {
        // 첫 번째 블록은 이벤트 트리거이므로 제외
        if (script.blocks.size() > 1)
        {
            // 실행 가능한 블록 수 계산
            count += static_cast<int>(script.blocks.size() - 1);
        }
    }
    return count;
}

int Engine::getBlockCountForScene(const std::string &sceneId) const
{
    int totalCount = 0;
    for (const auto &objInfo : objects_in_order)
    {
        if (objInfo.sceneId == sceneId)
        {
            // 지정된 씬의 각 오브젝트에 대해 블록 수 가져오기
            totalCount += getBlockCountForObject(objInfo.id);
        }
    }
    return totalCount;
}

void Engine::changeObjectIndex(const std::string &entityId, Omocha::ObjectIndexChangeType changeType)
{
    std::lock_guard<std::mutex> lock(this->m_engineDataMutex); // Protect access to objects_in_order

    auto it = std::find_if(objects_in_order.begin(), objects_in_order.end(),
                           [&entityId](const ObjectInfo &objInfo)
                           {
                               return objInfo.id == entityId;
                           });

    if (it == objects_in_order.end())
    {
        EngineStdOut("changeObjectIndex: Entity ID '" + entityId + "' not found in objects_in_order.", 1);
        return;
    }

    ObjectInfo objectToMove = *it; // Make a copy of the ObjectInfo
    int currentIndex = static_cast<int>(std::distance(objects_in_order.begin(), it));
    int targetIndex = currentIndex;
    int numObjects = static_cast<int>(objects_in_order.size());

    if (numObjects <= 0)
    {
        // Should not happen if an item was found, but good for safety
        EngineStdOut("changeObjectIndex: objects_in_order is empty or in an invalid state.", 2);
        return;
    }
    int maxPossibleIndex = numObjects - 1;

    switch (changeType)
    {
    case Omocha::ObjectIndexChangeType::BRING_TO_FRONT: // Move to index 0 (topmost)
        targetIndex = 0;
        break;
    case Omocha::ObjectIndexChangeType::SEND_TO_BACK: // Move to the last index (bottommost)
        targetIndex = maxPossibleIndex;
        break;
    case Omocha::ObjectIndexChangeType::BRING_FORWARD: // Move one step towards top (index decreases)
        if (currentIndex > 0)
        {
            targetIndex = currentIndex - 1;
        }
        else
        {
            EngineStdOut("Object " + entityId + " is already at the front. No change in Z-order.", 0);
            return; // Already at the front
        }
        break;
    case Omocha::ObjectIndexChangeType::SEND_BACKWARD: // Move one step towards bottom (index increases)
        if (currentIndex < maxPossibleIndex)
        {
            targetIndex = currentIndex + 1;
        }
        else
        {
            EngineStdOut("Object " + entityId + " is already at the back. No change in Z-order.", 0);
            return; // Already at the back
        }
        break;
    case Omocha::ObjectIndexChangeType::UNKNOWN:
    default:
        EngineStdOut("changeObjectIndex: Unknown or unsupported Z-order change type for entity " + entityId, 1);
        return;
    }

    if (targetIndex == currentIndex && changeType != Omocha::ObjectIndexChangeType::BRING_TO_FRONT && changeType != Omocha::ObjectIndexChangeType::SEND_TO_BACK)
    {
        // No actual move needed unless it's to absolute front/back
        return;
    }

    objects_in_order.erase(it);
    objects_in_order.insert(objects_in_order.begin() + targetIndex, objectToMove);

    EngineStdOut(
        "Object " + entityId + " Z-order changed. From original index " + std::to_string(currentIndex) +
            " to new index " + std::to_string(targetIndex) + " (0 is topmost).",
        0);
}

void Engine::engineDrawLineOnStage(SDL_FPoint p1_stage_entry, SDL_FPoint p2_stage_entry_modified_y, SDL_Color color,
                                   float thickness)
{
    // p1_stage_entry is {lastX_entry, lastY_entry}
    // p2_stage_entry_modified_y is {currentX_entry, currentY_entry * -1.0f}
    // 두 점의 구성 요소는 엔트리 스테이지 좌표계 기준 (중앙 0,0, Y축 위쪽은 .x 및 원래 .y)

    if (!renderer || !tempScreenTexture)
    {
        EngineStdOut("engineDrawLineOnStage: Renderer or tempScreenTexture not available.", 2);
        return;
    }

    SDL_Texture *prevRenderTarget = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, tempScreenTexture); // 스테이지 텍스처에 그리도록 설정

    // p1 (표준 엔트리 스테이지 좌표)을 SDL 텍스처 좌표 (좌상단 0,0, Y축 아래쪽)로 변환
    float sdlP1X = p1_stage_entry.x + PROJECT_STAGE_WIDTH / 2.0f;
    float sdlP1Y = PROJECT_STAGE_HEIGHT / 2.0f - p1_stage_entry.y;

    // p2 (p2.y가 currentY_entry * -1.0f로 미리 수정된 상태)를 SDL 텍스처 좌표로 변환
    float sdlP2X = p2_stage_entry_modified_y.x + PROJECT_STAGE_WIDTH / 2.0f;
    float sdlP2Y = PROJECT_STAGE_HEIGHT / 2.0f - p2_stage_entry_modified_y.y;
    // 예시: 원래 엔티티 Y가 20이면, p2_stage_entry_modified_y.y는 -20.
    // sdlP2Y = (270/2) - (-20) = 135 + 20 = 155. This correctly maps the inverted Y.

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    // 참고: SDL_RenderLine은 int 좌표를 사용합니다. float 좌표 및 두께를 위해서는 SDL_RenderLineFloat (SDL3) 또는 사용자 정의 선 그리기가 필요합니다.
    SDL_RenderLine(renderer, static_cast<int>(sdlP1X), static_cast<int>(sdlP1Y), static_cast<int>(sdlP2X),
                   static_cast<int>(sdlP2Y));

    SDL_SetRenderTarget(renderer, prevRenderTarget); // 이전 렌더 타겟 복원
}

int Engine::getTotalBlockCount() const
{
    int totalCount = 0;
    for (const auto &pair : objectScripts)
    {
        // 또는, 스크립트가 없는 오브젝트(개수 0)를 포함하려면 모든 objects_in_order를 반복하고 getBlockCountForObject를 호출합니다.
        // 현재 방식은 실제로 스크립트 항목이 있는 오브젝트의 개수를 합산합니다.
        totalCount += getBlockCountForObject(pair.first);
    }
    return totalCount;
}

TTF_Font *Engine::getDialogFont()
{
    // 우선 hudFont를 재사용합니다. 필요시 별도 폰트 로드 로직 추가 가능.
    if (!hudFont)
    {
        EngineStdOut("Dialog font (hudFont) is not loaded!", 2);
        // 여기서 기본 폰트를 로드하거나, hudFont가 로드되도록 보장해야 합니다.
    }
    return hudFont;
}

void Engine::drawDialogs()
{
    if (!renderer || !tempScreenTexture)
        return;

    SDL_SetRenderTarget(renderer, tempScreenTexture); // 스테이지 텍스처에 그립니다.

    TTF_Font *font = getDialogFont();
    if (!font)
        return;

    for (auto &pair : entities)
    {
        // Entity의 DialogState를 수정할 수 있으므로 non-const 반복
        Entity *entity = pair.second;
        if (entity && entity->hasActiveDialog())
        {
            Entity::DialogState &dialog = entity->m_currentDialog; // 수정 가능한 참조 가져오기

            // 1. 텍스트 텍스처 생성/업데이트 (필요한 경우)
            if (dialog.needsRedraw || !dialog.textTexture)
            {
                if (dialog.textTexture)
                    SDL_DestroyTexture(dialog.textTexture);
                SDL_Color textColor = {0, 0, 0, 255}; // 검은색 텍스트
                // 텍스트 자동 줄 바꿈을 위해 _Wrapped 함수 사용 (예: 150px 너비에서 줄 바꿈)
                SDL_Surface *surface = TTF_RenderText_Blended_Wrapped(font, dialog.text.c_str(), dialog.text.size(),
                                                                      textColor, 150);
                if (surface)
                {
                    dialog.textTexture = SDL_CreateTextureFromSurface(renderer, surface);
                    dialog.textRect.w = static_cast<float>(surface->w);
                    dialog.textRect.h = static_cast<float>(surface->h);
                    SDL_DestroySurface(surface);
                }
                dialog.needsRedraw = false;
            }

            if (!dialog.textTexture)
                continue; // 텍스처 생성 실패 시 건너뛰기

            // 2. 말풍선 위치 및 크기 계산
            float entitySdlX = static_cast<float>(entity->getX() + PROJECT_STAGE_WIDTH / 2.0);
            float entitySdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entity->getY());

            // 엔티티의 시각적 너비/높이 추정 (단순화된 방식)
            float entityVisualWidth = static_cast<float>(entity->getWidth() * std::abs(entity->getScaleX()));
            float entityVisualHeight = static_cast<float>(entity->getHeight() * std::abs(entity->getScaleY()));

            // 말풍선 기준점 (엔티티 등록점 기준 우측 상단 근처)
            // 좀 더 정확한 위치는 엔티티의 실제 화면상 경계 상자를 사용해야 합니다.
            float anchorX = entitySdlX + entityVisualWidth * 0.25f;
            float anchorY = entitySdlY - entityVisualHeight * 0.25f;

            float padding = 8.0f;
            float bubbleWidth = dialog.textRect.w + 2 * padding;
            float bubbleHeight = dialog.textRect.h + 2 * padding;
            float tailHeight = 10.0f;
            float tailWidth = 15.0f;

            // 말풍선 위치 (말풍선 내용 영역의 좌상단)
            // 기준점 위에 말풍선이 위치하도록 조정
            dialog.bubbleScreenRect.x = anchorX;
            dialog.bubbleScreenRect.y = anchorY - bubbleHeight - tailHeight;
            dialog.bubbleScreenRect.w = bubbleWidth;
            dialog.bubbleScreenRect.h = bubbleHeight;

            // 화면 경계 내로 클램핑
            if (dialog.bubbleScreenRect.x < 0)
                dialog.bubbleScreenRect.x = 0;
            if (dialog.bubbleScreenRect.y < 0)
                dialog.bubbleScreenRect.y = 0;
            if (dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w > PROJECT_STAGE_WIDTH)
            {
                dialog.bubbleScreenRect.x = PROJECT_STAGE_WIDTH - dialog.bubbleScreenRect.w;
            }
            if (dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h > PROJECT_STAGE_HEIGHT)
            {
                dialog.bubbleScreenRect.y = PROJECT_STAGE_HEIGHT - dialog.bubbleScreenRect.h;
            }

            // 3. 말풍선 배경 렌더링
            SDL_Color bubbleBgColor = {255, 255, 255, 255};    // 흰색
            SDL_Color bubbleBorderColor = {79, 128, 255, 255}; // 엔트리 블루 테두리
            float cornerRadius = 8.0f;

            if (dialog.type == "think")
            {
                // "생각" 풍선: 여러 개의 원으로 구름 모양 표현 (간단 버전)
                SDL_SetRenderDrawColor(renderer, bubbleBgColor.r, bubbleBgColor.g, bubbleBgColor.b, bubbleBgColor.a);
                Helper_DrawFilledCircle(
                    renderer, static_cast<int>(dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w * 0.3f),
                    static_cast<int>(dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h * 0.4f),
                    static_cast<int>(dialog.bubbleScreenRect.h * 0.4f));
                Helper_DrawFilledCircle(
                    renderer, static_cast<int>(dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w * 0.7f),
                    static_cast<int>(dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h * 0.35f),
                    static_cast<int>(dialog.bubbleScreenRect.h * 0.35f));
                Helper_DrawFilledCircle(
                    renderer, static_cast<int>(dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w * 0.5f),
                    static_cast<int>(dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h * 0.6f),
                    static_cast<int>(dialog.bubbleScreenRect.h * 0.5f));
                // "생각" 풍선 꼬리 (작은 원들)
                Helper_DrawFilledCircle(renderer, static_cast<int>(anchorX - 10), static_cast<int>(anchorY - 5), 5);
                Helper_DrawFilledCircle(renderer, static_cast<int>(anchorX - 5), static_cast<int>(anchorY - 10), 4);
            }
            else
            {
                // "speak"
                // 테두리 먼저 그리기
                SDL_SetRenderDrawColor(renderer, bubbleBorderColor.r, bubbleBorderColor.g, bubbleBorderColor.b,
                                       bubbleBorderColor.a);
                Helper_RenderFilledRoundedRect(renderer, &dialog.bubbleScreenRect, cornerRadius);
                // 내부 배경 그리기 (테두리보다 약간 작게)
                SDL_FRect innerBgRect = {
                    dialog.bubbleScreenRect.x + 1.0f, dialog.bubbleScreenRect.y + 1.0f,
                    dialog.bubbleScreenRect.w - 2.0f, dialog.bubbleScreenRect.h - 2.0f};
                SDL_SetRenderDrawColor(renderer, bubbleBgColor.r, bubbleBgColor.g, bubbleBgColor.b, bubbleBgColor.a);
                Helper_RenderFilledRoundedRect(renderer, &innerBgRect, cornerRadius - 1.0f);

                // 4. "말하기" 풍선 꼬리 렌더링
                dialog.tailVertices[0] = {
                    dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w * 0.3f,
                    dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h};
                dialog.tailVertices[1] = {
                    dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w * 0.4f,
                    dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h};
                dialog.tailVertices[2] = {anchorX, anchorY};

                SDL_SetRenderDrawColor(renderer, bubbleBorderColor.r, bubbleBorderColor.g, bubbleBorderColor.b,
                                       bubbleBorderColor.a);
                SDL_RenderGeometry(renderer, nullptr, dialog.tailVertices, 3, nullptr, 0); // 테두리 색으로 채우기
            }

            // 5. 텍스트 렌더링
            SDL_FRect textDestRect = {
                dialog.bubbleScreenRect.x + padding,
                dialog.bubbleScreenRect.y + padding,
                dialog.textRect.w,
                dialog.textRect.h};
            SDL_RenderTexture(renderer, dialog.textTexture, nullptr, &textDestRect);
        }
    }
    SDL_SetRenderTarget(renderer, nullptr); // 렌더 타겟 리셋
}

SDL_Texture *Engine::LoadTextureFromSvgResource(SDL_Renderer *renderer, int resourceID)
{
    HINSTANCE hInstance = GetModuleHandle(NULL); // 현재 실행 파일의 인스턴스 핸들

    // 1. 리소스 정보 찾기
    HRSRC hResInfo = FindResource(hInstance, MAKEINTRESOURCE(resourceID), RT_RCDATA);
    if (hResInfo == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FindResource failed for ID %d: %lu", resourceID, GetLastError());
        return NULL;
    }

    // 2. 리소스 로드
    HGLOBAL hResData = LoadResource(hInstance, hResInfo);
    if (hResData == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadResource failed for ID %d: %lu", resourceID, GetLastError());
        return NULL;
    }

    // 3. 리소스 데이터 포인터 및 크기 얻기
    void *pSvgData = LockResource(hResData);
    DWORD dwSize = SizeofResource(hInstance, hResInfo);

    if (pSvgData == NULL || dwSize == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LockResource or SizeofResource failed for ID %d. Error: %lu",
                     resourceID, GetLastError());
        // FreeResource는 LoadResource로 얻은 핸들에 대해 호출 (Vista 이상에서는 no-op)
        // LockResource 실패 시에는 호출하지 않는 것이 일반적
        return NULL;
    }

    // 4. 메모리상의 SVG 데이터를 위한 SDL_RWops 생성
    SDL_IOStream *rw = SDL_IOFromConstMem(pSvgData, dwSize);
    if (rw == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RWFromConstMem failed: %s", SDL_GetError());
        // pSvgData는 Windows 리소스이므로 여기서 해제하지 않음
        return NULL;
    }

    // 5. SDL_image를 사용하여 SVG 데이터로부터 Surface 로드
    // IMG_LoadTyped_RW의 세 번째 인자 '1'은 작업 후 SDL_image가 rwops를 자동으로 닫도록(SDL_RWclose) 함.
    SDL_Surface *surface = IMG_LoadSVG_IO(rw); // "SVG" 타입을 명시
    if (surface == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IMG_LoadTyped_RW (SVG) failed");
        // rw는 IMG_LoadTyped_RW에 의해 이미 처리되었으므로 여기서 SDL_RWclose를 호출할 필요 없음
        return NULL;
    }

    // 6. Surface로부터 Texture 생성
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
    }

    // 7. 원본 Surface 해제
    SDL_DestroySurface(surface);

    // LockResource로 얻은 포인터(pSvgData)는 UnlockResource가 필요 없고,
    // HGLOBAL 핸들(hResData)도 FreeResource를 명시적으로 호출할 필요가 없습니다 (특히 Vista 이상).
    // 리소스는 모듈이 언로드될 때 자동으로 해제됩니다.

    return texture;
}

bool Engine::setEntitySelectedCostume(const std::string &entityId, const std::string &costumeId)
{
    for (auto &objInfo : objects_in_order)
    {
        if (objInfo.id == entityId)
        {
            // Check if the costumeId exists in objInfo.costumes
            bool costumeExists = false;
            for (const auto &costume : objInfo.costumes)
            {
                if (costume.id == costumeId)
                {
                    costumeExists = true;
                    break;
                }
            }
            if (costumeExists)
            {
                objInfo.selectedCostumeId = costumeId;
                // If you have an Entity* map, you might want to inform the Entity object too,
                // or the Entity might fetch its ObjectInfo when needed.
                // For now, just updating ObjectInfo which is used in drawAllEntities.
                return true;
            }
            else
            {
                EngineStdOut(
                    "Costume ID '" + costumeId + "' not found in the costume list for object '" + entityId + "'.", 1);
                return false;
            }
        }
    }
    EngineStdOut("Entity ID '" + entityId + "' not found in objects_in_order when trying to set costume.", 1);
    return false;
}

bool Engine::setEntitychangeToNextCostume(const string &entityId, const string &asOption)
{
    for (auto &objInfo : objects_in_order)
    {
        if (objInfo.id == entityId)
        {
            if (objInfo.costumes.size() <= 1)
            {
                // 모양이 없거나 1개만 있으면 다음/이전으로 변경 불가
                EngineStdOut(
                    "Entity '" + entityId + "' has " + to_string(objInfo.costumes.size()) +
                        " costume(s). Cannot change to next/previous.",
                    1);
                return false;
            }

            int currentCostumeIndex = -1;
            for (size_t i = 0; i < objInfo.costumes.size(); ++i)
            {
                if (objInfo.costumes[i].id == objInfo.selectedCostumeId)
                {
                    currentCostumeIndex = static_cast<int>(i);
                    break;
                }
            }

            if (currentCostumeIndex == -1)
            {
                // 현재 선택된 모양 ID를 목록에서 찾을 수 없는 경우 (데이터 불일치)
                // 안전하게 첫 번째 모양으로 설정하거나 오류 처리
                EngineStdOut(
                    "Error: Selected costume ID '" + objInfo.selectedCostumeId +
                        "' not found in costume list for entity '" + entityId +
                        "'. Defaulting to first costume if available.",
                    2);
                if (!objInfo.costumes.empty())
                {
                    objInfo.selectedCostumeId = objInfo.costumes[0].id;
                }
                return false; // 또는 true를 반환하고 첫 번째 모양으로 설정
            }

            int totalCostumes = static_cast<int>(objInfo.costumes.size());
            int nextCostumeIndex = currentCostumeIndex;

            if (asOption == "prev")
            {
                nextCostumeIndex = (currentCostumeIndex - 1 + totalCostumes) % totalCostumes;
            }
            else // "next" 또는 다른 알 수 없는 옵션은 다음으로 처리
            {
                nextCostumeIndex = (currentCostumeIndex + 1) % totalCostumes;
            }

            objInfo.selectedCostumeId = objInfo.costumes[nextCostumeIndex].id;
            EngineStdOut(
                "Entity '" + entityId + "' changed costume to '" + objInfo.costumes[nextCostumeIndex].name + "' (ID: " +
                    objInfo.selectedCostumeId + ")",
                0);
            return true;
        }
    }
    EngineStdOut("Entity ID '" + entityId + "' not found in objects_in_order for changing costume.", 1);
    return false; // entityId를 찾지 못한 경우
}

void Engine::raiseMessage(const std::string &messageId, const std::string &senderObjectId,
                          const std::string &executionThreadId)
{
    EngineStdOut(
        "Message '" + messageId + "' raised by object " + senderObjectId + " (Thread: " + executionThreadId + ")", 0,
        executionThreadId);
    auto it = m_messageReceivedScripts.find(messageId);
    if (it != m_messageReceivedScripts.end())
    {
        const auto &scriptsToRun = it->second;
        EngineStdOut(
            "Found " + std::to_string(scriptsToRun.size()) + " script(s) listening for message '" + messageId + "'", 0,
            executionThreadId);
        for (const auto &scriptPair : scriptsToRun)
        {
            const std::string &listeningObjectId = scriptPair.first;
            const Script *scriptPtr = scriptPair.second;

            // Optional: Prevent an object from triggering its own message-received script if that's desired behavior.
            // if (listeningObjectId == senderObjectId) {
            //     EngineStdOut("Object " + listeningObjectId + " is trying to trigger its own message '" + messageId + "'. Skipping.", 1, executionThreadId);
            //     continue;
            // }

            Entity *listeningEntity = getEntityById(listeningObjectId);
            if (listeningEntity)
            {
                // Check if the listening entity is in the current scene or is global
                const ObjectInfo *objInfoPtr = getObjectInfoById(listeningObjectId);
                if (objInfoPtr)
                {
                    bool isInCurrentScene = (objInfoPtr->sceneId == currentSceneId);
                    bool isGlobal = (objInfoPtr->sceneId == "global" || objInfoPtr->sceneId.empty());
                    if (isInCurrentScene || isGlobal)
                    {
                        EngineStdOut(
                            "Dispatching message-received script for object: " + listeningObjectId + " (Message: '" + messageId + "')", 0, executionThreadId);
                        this->dispatchScriptForExecution(listeningObjectId, scriptPtr, currentSceneId, 0.0);
                    }
                    else
                    {
                        EngineStdOut(
                            "Script for message '" + messageId + "' on object " + listeningObjectId +
                                " not run because object is not in current scene (" + currentSceneId + ") and not global.",
                            1, executionThreadId);
                    }
                }
                else
                {
                    EngineStdOut(
                        "ObjectInfo not found for entity ID '" + listeningObjectId + "' during message '" + messageId +
                            "' processing. Script not run.",
                        1, executionThreadId);
                }
            }
            else
            {
                EngineStdOut(
                    "Entity " + listeningObjectId +
                        " not found when trying to execute message-received script for message '" + messageId + "'.",
                    1,
                    executionThreadId);
            }
        }
    }
    else
    {
        EngineStdOut("No scripts found listening for message '" + messageId + "'", 0, executionThreadId);
    }
}

void Engine::dispatchScriptForExecution(const std::string &entityId, const Script *scriptPtr,
                                        const std::string &sceneIdAtDispatch, float deltaTime,
                                        const std::string &existingExecutionThreadId)
{
    if (!scriptPtr)
    {
        EngineStdOut("dispatchScriptForExecution called with null script pointer for object: " + entityId, 2);
        return;
    }
    // For new scripts, check block size. For resumed scripts, this check might not be appropriate if resumeAtBlockIndex is valid.
    if (existingExecutionThreadId.empty() && (scriptPtr->blocks.empty() || scriptPtr->blocks.size() <= 1))
    {
        // 첫번째 블록은 이벤트 트리거
        EngineStdOut(
            "dispatchScriptForExecution: Script for object " + entityId + " has no executable blocks. Skipping.", 1);
        return;
    }

    boost::asio::post(m_scriptThreadPool, [this, entityId, scriptPtr, sceneIdAtDispatch, deltaTime, existingExecutionThreadId]()
                      {
        std::string thread_id_str;
        if (!existingExecutionThreadId.empty()) {
            thread_id_str = existingExecutionThreadId;
            EngineStdOut("Worker thread resuming script for entity: " + entityId, 5, thread_id_str);
        } else {
            // 새 스크립트 실행을 위한 ID 생성 (더 견고한 ID 생성 방식 고려 필요)
            std::thread::id physical_thread_id = std::this_thread::get_id();
            std::stringstream ss;
            ss << std::hash<std::thread::id>{}(physical_thread_id);
            thread_id_str = "script_" + ss.str() + "_" + std::to_string(SDL_GetTicks()); // 고유성 향상
            EngineStdOut("Worker thread starting new script for entity: " + entityId, 5, thread_id_str);
        }

        try {

            Entity *entity = nullptr; {
                std::lock_guard<std::mutex> lock(m_engineDataMutex); // entities 맵 접근 보호
                entity = getEntityById_nolock(entityId);
            }

            if (entity) {
                entity->executeScript(scriptPtr, thread_id_str, sceneIdAtDispatch, deltaTime);
            } else {
                EngineStdOut(
                    "Entity " + entityId + " not found when trying to execute script in worker thread " + thread_id_str,
                    1, thread_id_str);
            }
        } catch (const ScriptBlockExecutionError &sbee) {
            // ScriptBlockExecutionError를 여기서 처리하여 상세 한글 오류 메시지 생성
            Omocha::BlockTypeEnum blockTypeEnum = Omocha::stringToBlockTypeEnum(sbee.blockType);
            std::string koreanBlockTypeName = Omocha::blockTypeEnumToKoreanString(blockTypeEnum);

            std::string detailedErrorMessage = "블럭 을 실행하는데 오류가 발생하였습니다. 블럭ID " + sbee.blockId +
                                               " 의 타입 " + koreanBlockTypeName +
                                               (blockTypeEnum == Omocha::BlockTypeEnum::UNKNOWN && !sbee.blockType.
                                                empty()
                                                    ? " (원본: " + sbee.blockType + ")"
                                                    : "") +
                                               " 에서 사용 하는 객체 " + sbee.entityId +
                                               "\n원본 오류: " + sbee.originalMessage;

            EngineStdOut("Script Execution Error (Thread " + thread_id_str + "): " + detailedErrorMessage, 2,
                         thread_id_str);
            this->showMessageBox("오류가 발생했습니다!\n" + detailedErrorMessage,SDL_MESSAGEBOX_ERROR);
        } catch (const std::exception &e) {
            EngineStdOut(
                "Generic exception caught in worker thread " + thread_id_str + " executing script for entity " +
                entityId + ": " + e.what(), 2, thread_id_str);
        } catch (...) {
            EngineStdOut(
                "Unknown exception caught in worker thread " + thread_id_str + " executing script for entity " +
                entityId, 2, thread_id_str);
        } });
}

/**
 * @brief Saves cloud variables to a JSON file based on m_projectId.
 * @return True if saving was successful, false otherwise.
 */
bool Engine::saveCloudVariablesToJson()
{
    if (PROJECT_NAME.empty())
    {
        EngineStdOut("Project ID is not set. Cannot save cloud variables.", 2);
        return false;
    }
    // 파일 경로를 m_projectId를 사용하여 생성
    std::string directoryPath = "cloud_saves"; // 클라우드 저장 파일들을 모아둘 디렉토리
    std::string filePath = directoryPath + "/" + PROJECT_NAME + ".cloud.json";

    // 디렉토리 생성 (존재하지 않는 경우)
    try
    {
        if (!std::filesystem::exists(directoryPath))
        {
            if (std::filesystem::create_directories(directoryPath))
            {
                EngineStdOut("Created directory for cloud saves: " + directoryPath, 0);
            }
            else
            {
                EngineStdOut("Failed to create directory for cloud saves (unknown error): " + directoryPath, 2);
                return false; // 디렉토리 생성 실패 시 저장 중단
            }
        }
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        EngineStdOut("Error creating directory for cloud saves '" + directoryPath + "': " + std::string(e.what()), 2);
        return false; // 예외 발생 시 저장 중단
    }

    EngineStdOut("Saving cloud variables to: " + filePath, 0);
    std::lock_guard<std::mutex> lock(m_engineDataMutex); // Protect m_HUDVariables

    rapidjson::Document doc;
    doc.SetArray();
    rapidjson::Document::AllocatorType &allocator = doc.GetAllocator();

    for (const auto &hudVar : m_HUDVariables)
    {
        if (hudVar.isCloud)
        {
            rapidjson::Value varJson(rapidjson::kObjectType);

            varJson.AddMember("name", rapidjson::Value(hudVar.name.c_str(), allocator).Move(), allocator);
            varJson.AddMember("value", rapidjson::Value(hudVar.value.c_str(), allocator).Move(), allocator);
            varJson.AddMember("objectId", rapidjson::Value(hudVar.objectId.c_str(), allocator).Move(), allocator);
            varJson.AddMember("variableType", rapidjson::Value(hudVar.variableType.c_str(), allocator).Move(), allocator);

            if (hudVar.variableType == "list")
            {
                rapidjson::Value arrayJson(rapidjson::kArrayType);
                for (const auto &item : hudVar.array)
                {
                    rapidjson::Value itemJson(rapidjson::kObjectType);
                    itemJson.AddMember("key", rapidjson::Value(item.key.c_str(), allocator).Move(), allocator);
                    itemJson.AddMember("data", rapidjson::Value(item.data.c_str(), allocator).Move(), allocator);
                    arrayJson.PushBack(itemJson, allocator);
                }
                varJson.AddMember("array", arrayJson, allocator);
            }
            doc.PushBack(varJson, allocator);
        }
    }

    std::ofstream ofs(filePath);
    if (!ofs.is_open())
    {
        EngineStdOut("Failed to open file for saving cloud variables: " + filePath, 2);
        return false;
    }

    rapidjson::OStreamWrapper osw(ofs);
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
    doc.Accept(writer);
    ofs.close();

    EngineStdOut("Cloud variables saved successfully to: " + filePath, 0);
    return true;
}

/**
 * @brief Loads cloud variables from a JSON file based on m_projectId, updating existing ones.
 * @return True if loading was successful or file didn't exist, false on parse error.
 */
bool Engine::loadCloudVariablesFromJson()
{
    if (PROJECT_NAME.empty())
    {
        EngineStdOut("Project ID is not set. Cannot load cloud variables.", 1);
        return true; // 프로젝트 ID가 없으면 로드할 파일도 없다고 간주 (오류는 아님)
    }
    // 파일 경로를 m_projectId를 사용하여 생성
    std::string directoryPath = "cloud_saves";
    std::string filePath = directoryPath + "/" + PROJECT_NAME + ".cloud.json";

    EngineStdOut("Attempting to load cloud variables from: " + filePath, 0);

    if (!std::filesystem::exists(filePath))
    {
        EngineStdOut("Cloud variable save file not found: " + filePath + ". No variables loaded.", 0);
        return true; // 저장 파일이 아직 없는 것은 정상적인 상황
    }

    std::ifstream ifs(filePath);
    if (!ifs.is_open())
    {
        EngineStdOut("Failed to open cloud variable file: " + filePath, 2);
        return false;
    }

    rapidjson::IStreamWrapper isw(ifs);
    rapidjson::Document doc;
    doc.ParseStream(isw);
    ifs.close();

    if (doc.HasParseError())
    {
        EngineStdOut("Failed to parse cloud variable file: " + std::string(rapidjson::GetParseError_En(doc.GetParseError())) +
                         " (Offset: " + std::to_string(doc.GetErrorOffset()) + ")",
                     2);
        return false;
    }

    if (!doc.IsArray())
    {
        EngineStdOut("Cloud variable file content is not a JSON array: " + filePath, 2);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_engineDataMutex); // Protect m_HUDVariables

    int updatedCount = 0;
    int notFoundCount = 0;

    for (const auto &savedVarJson : doc.GetArray())
    {
        if (!savedVarJson.IsObject())
        {
            EngineStdOut("Skipping non-object entry in cloud variable file.", 1);
            continue;
        }

        std::string name = getSafeStringFromJson(savedVarJson, "name", "cloud variable entry", "", true, false);
        std::string value = getSafeStringFromJson(savedVarJson, "value", "cloud variable entry for " + name, "", false, true);
        std::string objectId = getSafeStringFromJson(savedVarJson, "objectId", "cloud variable entry for " + name, "", false, true);
        std::string variableType = getSafeStringFromJson(savedVarJson, "variableType", "cloud variable entry for " + name, "variable", true, false);

        if (name.empty() || variableType.empty())
        {
            EngineStdOut("Skipping cloud variable entry with empty name or variableType.", 1);
            continue;
        }

        bool foundAndUpdated = false;
        for (auto &hudVar : m_HUDVariables)
        {
            if (hudVar.isCloud && hudVar.name == name && hudVar.objectId == objectId && hudVar.variableType == variableType)
            {
                hudVar.value = value;
                EngineStdOut("Cloud variable '" + name + "' (Object: '" + (objectId.empty() ? "global" : objectId) + "') updated to value: '" + value + "'", 0);

                if (variableType == "list")
                {
                    hudVar.array.clear();
                    if (savedVarJson.HasMember("array") && savedVarJson["array"].IsArray())
                    {
                        const rapidjson::Value &arrayJson = savedVarJson["array"];
                        for (const auto &itemJson : arrayJson.GetArray())
                        {
                            if (itemJson.IsObject())
                            {
                                ListItem listItem;
                                listItem.key = getSafeStringFromJson(itemJson, "key", "list item in " + name, "", false, true);
                                listItem.data = getSafeStringFromJson(itemJson, "data", "list item in " + name, "", false, true);
                                hudVar.array.push_back(listItem);
                            }
                        }
                        EngineStdOut("  List '" + name + "' updated with " + std::to_string(hudVar.array.size()) + " items.", 0);
                    }
                }
                foundAndUpdated = true;
                updatedCount++;
                break;
            }
        }

        if (!foundAndUpdated)
        {
            EngineStdOut("Cloud variable '" + name + "' (Object: '" + (objectId.empty() ? "global" : objectId) +
                             "') from save file not found or not marked as cloud in current project configuration. Value not loaded.",
                         1);
            notFoundCount++;
        }
    }

    EngineStdOut("Cloud variables loading finished. Updated: " + std::to_string(updatedCount) +
                     ", Not found/applicable: " + std::to_string(notFoundCount),
                 0);
    return true;
}
