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
#include <chrono> // std::chrono 타입 및 함수 사용을 위해 필요
#include <format>
#include <nlohmann/json.hpp>
#include "blocks/BlockExecutor.h"
#include "blocks/blockTypes.h"    // Omocha 네임스페이스의 함수 사용을 위해 명시적 포함 (필요시)
#include <future>
#include <resource.h>
using namespace std;

const float Engine::MIN_ZOOM = 1.0f;
const float Engine::MAX_ZOOM = 3.0f;
const char *BASE_ASSETS = "assets/";
const float Engine::LIST_RESIZE_HANDLE_SIZE = 10.0f;
const float Engine::MIN_LIST_WIDTH = 80.0f;
const float Engine::MIN_LIST_HEIGHT = 60.0f;
const float MOUSE_WHEEL_SCROLL_SPEED = 20.0f;
const char *FONT_ASSETS = "font/";
string PROJECT_NAME;
string WINDOW_TITLE;
string LOADING_METHOD_NAME;

static string NlohmannJsonToString(const nlohmann::json &value) {
    return value.dump();
}

// Anonymous namespace for helper functions local to this file
namespace {
    // Helper function to recursively filter 'params' arrays within a JSON structure
    void RecursiveFilterParamsArray(nlohmann::json &paramsArray, Engine &engine, const std::string &parentContext) {
        if (!paramsArray.is_array()) {
            // This function expects an array.
            // If paramsArray was supposed to be an array but isn't, the calling code (ParseBlockDataInternal) handles logging.
            return;
        }

        nlohmann::json filteredArray = nlohmann::json::array();
        for (const auto &item: paramsArray) {
            if (item.is_null()) {
                // Skip null items
                continue;
            } else if (item.is_object() && item.contains("type") && item.contains("params") && item["params"].
                       is_array()) {
                // It's a block-like structure. Make a copy to modify its "params".
                nlohmann::json subBlockCopy = item;
                RecursiveFilterParamsArray(subBlockCopy["params"], engine,
                                           parentContext + " -> sub-param-block (type: " + subBlockCopy.value(
                                               "type", "unknown") + ")"); // Recursively filter its params
                filteredArray.push_back(subBlockCopy); // Add the processed sub-block
            } else {
                // It's a literal or a non-block object, keep it.
                filteredArray.push_back(item);
            }
        }
        paramsArray = filteredArray; // Replace the original array with the filtered one
    }

    Block ParseBlockDataInternal(const nlohmann::json &blockJson, Engine &engine, const std::string &contextForLog) {
        Block newBlock; // Default constructor initializes paramsJson to kNullType

        // Parse ID
        std::string tempId = engine.getSafeStringFromJson(blockJson, "id", contextForLog, "", true, false);
        // Parse Type
        std::string tempType = engine.getSafeStringFromJson(blockJson, "type", contextForLog + " (id: " + tempId + ")",
                                                            "", true, false);

        if (tempId.empty() || tempType.empty()) {
            // Log an error if critical information is missing. getSafeStringFromJson might already log.
            // This additional log ensures we know why the block is considered invalid here.
            engine.EngineStdOut(
                "ERROR: Block " + contextForLog + " is invalid due to missing id ('" + tempId + "') or type ('" +
                tempType + "'). Cannot parse block.", 2);
            return Block(); // Return an empty/invalid block (its id will be empty)
        }
        newBlock.id = tempId;
        newBlock.type = tempType;
        // engine.EngineStdOut("DEBUG: Successfully parsed id='" + newBlock.id + "', type='" + newBlock.type + "' for " + contextForLog, 3, ""); // Optional: debug log for successful basic parse

        // Parse Type
        newBlock.type = engine.getSafeStringFromJson(blockJson, "type", contextForLog + " (id: " + newBlock.id + ")",
                                                     "", true, false);
        if (newBlock.type.empty()) {
            // engine.EngineStdOut("ERROR: Block " + contextForLog + " (id: " + newBlock.id + ") has missing or empty 'type'. Cannot parse block.", 2);
            return Block(); // Return an empty/invalid block
        }

        // Parse Params
        if (blockJson.contains("params")) {
            const nlohmann::json &paramsVal = blockJson["params"];
            if (paramsVal.is_array()) {
                newBlock.paramsJson = paramsVal; // Initial copy
                std::string paramsContext = contextForLog + " (id: " + newBlock.id + ", type: " + newBlock.type +
                                            ") params";
                RecursiveFilterParamsArray(newBlock.paramsJson, engine, paramsContext);
            } else {
                // params가 있지만 배열이 아닌 경우
                engine.EngineStdOut(
                    "WARN: Block " + contextForLog + " (id: " + newBlock.id + ", type: " + newBlock.type +
                    ") has 'params' but it's not an array. Params will be empty. Value: " +
                    NlohmannJsonToString(paramsVal), 1, ""); // Added empty thread ID
                newBlock.paramsJson = nlohmann::json::array(); // Ensure it's an empty array
            }
        } else {
            // 'params' 멤버가 없으면 빈 배열로 초기화합니다.
            newBlock.paramsJson = nlohmann::json::array();
        }

        // Parse Statements (Inner Scripts)
        if (blockJson.contains("statements") && blockJson["statements"].is_array()) {
            const nlohmann::json &statementsArray = blockJson["statements"];
            for (int stmtIdx = 0; stmtIdx < statementsArray.size(); ++stmtIdx) {
                const auto &statementStackJson = statementsArray[stmtIdx];
                if (statementStackJson.is_array()) {
                    Script innerScript;
                    std::string innerScriptContext = contextForLog + " statement " + std::to_string(stmtIdx);
                    for (int innerBlockIdx = 0; innerBlockIdx < statementStackJson.size(); ++innerBlockIdx) {
                        const auto &innerBlockJsonVal = statementStackJson[innerBlockIdx];
                        if (innerBlockJsonVal.is_object()) {
                            Block parsedInnerBlock = ParseBlockDataInternal(
                                innerBlockJsonVal, engine,
                                innerScriptContext + " inner_block " + std::to_string(innerBlockIdx));
                            // Ensure both id and type are valid before adding
                            // Also, ensure that FilterNullsInParamsJsonArray has been called for parsedInnerBlock's paramsJson
                            // This is handled because ParseBlockDataInternal calls FilterNullsInParamsJsonArray before returning.
                            if (!parsedInnerBlock.id.empty() && !parsedInnerBlock.type.empty()) {
                                innerScript.blocks.push_back(std::move(parsedInnerBlock));
                                // Log only if successfully parsed and added
                                // engine.EngineStdOut(
                                //    "    Parsed block: id='" + parsedInnerBlock.id + "', type='" + parsedInnerBlock.type + "'",
                                //    0); // This log might be too verbose if ParseBlockDataInternal already logs.
                            } else {
                                engine.EngineStdOut(
                                    "WARN: Skipping block in " + innerScriptContext + " inner_block " + std::to_string(
                                        innerBlockIdx) +
                                    " due to missing id ('" + parsedInnerBlock.id + "') or type ('" + parsedInnerBlock.
                                    type + "'). Content: " + NlohmannJsonToString(innerBlockJsonVal),
                                    2, ""); // Error level
                            }
                        } else {
                            engine.EngineStdOut(
                                "WARN: Inner block in " + innerScriptContext + " at index " +
                                std::to_string(innerBlockIdx) + " is not an object. Skipping. Content: " +
                                NlohmannJsonToString(innerBlockJsonVal), 1, ""); // Added empty thread ID
                        }
                    }
                    if (!innerScript.blocks.empty()) {
                        newBlock.statementScripts.push_back(std::move(innerScript));
                    } else {
                        engine.EngineStdOut(
                            "DEBUG: Inner script " + innerScriptContext + " is empty or all its blocks were invalid.",
                            3, ""); // Added empty thread ID
                    }
                } else {
                    engine.EngineStdOut(
                        "WARN: Statement entry in " + contextForLog + " at index " + std::to_string(stmtIdx) +
                        " is not an array (not a valid script stack). Skipping. Content: " +
                        NlohmannJsonToString(statementStackJson), 1, ""); // Added empty thread ID
                }
            }
        }
        return newBlock;
    }
} // end anonymous namespace

// Helper function to get font path from font name (similar to drawAllEntities)
namespace {
    // Anonymous namespace for local helper
    std::string getFontPathByName(const std::string &fontName, int fontSize, Engine *engineInstance) {
        // This function is a simplified version. Ideally, FontName enum and getFontNameFromString
        // would be part of Engine or a shared utility.
        std::string fontAsset = std::string(FONT_ASSETS);
        std::string determinedFontPath;

        // Simplified mapping based on common names.
        // This should ideally use the FontName enum and getFontNameFromString logic
        // found in drawAllEntities if that's more comprehensive.
        if (fontName == "D2Coding")
            determinedFontPath = fontAsset + "d2coding.ttf";
        else if (fontName == "NanumGothic")
            determinedFontPath = fontAsset + "nanum_gothic.ttf";
        else if (fontName == "MaruBuri")
            determinedFontPath = fontAsset + "maruburi.ttf";
        else if (fontName == "NanumBarunpen")
            determinedFontPath = fontAsset + "nanum_barunpen.ttf";
        else if (fontName == "NanumPen")
            determinedFontPath = fontAsset + "nanum_pen.ttf";
        else if (fontName == "NanumMyeongjo")
            determinedFontPath = fontAsset + "nanum_myeongjo.ttf";
        else if (fontName == "NanumSquareRound")
            determinedFontPath = fontAsset + "nanum_square_round.ttf";
        else
            determinedFontPath = fontAsset + "nanum_gothic.ttf"; // Default

        return determinedFontPath;
    }
}

Engine::Engine() : window(nullptr), renderer(nullptr),
                   tempScreenTexture(nullptr), totalItemsToLoad(0), loadedItemCount(0),
                   zoomFactor((this->specialConfig.setZoomfactor <= 0.0)
                                  ? 1.0f
                                  : std::clamp(static_cast<float>(this->specialConfig.setZoomfactor), Engine::MIN_ZOOM,
                                               Engine::MAX_ZOOM)),
                   m_isDraggingZoomSlider(false), m_pressedObjectId(""),
                   logger("omocha_engine.log"), // threadPool 멤버 초기화 추가
                   threadPool(std::make_unique<ThreadPool>(
                       *this, (max)(1u, std::thread::hardware_concurrency() > 0
                                            ? std::thread::hardware_concurrency()
                                            : 2))),
                   m_projectTimerValue(0.0), m_projectTimerRunning(false), m_gameplayInputActive(false) {
    EngineStdOut(
        string(OMOCHA_ENGINE_NAME) + " v" + string(OMOCHA_ENGINE_VERSION) + " " + string(OMOCHA_DEVELOPER_NAME), 4);
    EngineStdOut("See Project page " + string(OMOCHA_ENGINE_GITHUB), 4);
    startThreadPool((max)(1u, std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 2));
}

void Engine::workerLoop() {
    while (true) {
        std::function<void()> task; {
            std::unique_lock<std::mutex> lock(m_taskQueueMutex_std);
            m_taskQueueCV_std.wait(lock, [this] {
                return m_isShuttingDown.load(std::memory_order_relaxed) || !m_taskQueue.empty();
            });
            if (m_isShuttingDown.load(std::memory_order_relaxed) && m_taskQueue.empty()) {
                return; // Exit if shutting down and no more tasks
            }
            if (m_taskQueue.empty()) {
                continue;
            }
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }
        if (task) {
            try {
                task();
            } catch (const ScriptBlockExecutionError &sbee) {
                // 이미 EngineStdOut 및 showMessageBox를 호출하는 예외 핸들러가 Entity::executeScript 내에 있으므로,
                // Entity 레벨에서 예외가 발생하여 여기까지 온 경우, 메시지 박스를 표시하고 종료합니다.
                EngineStdOut(
                    "ScriptBlockExecutionError in worker thread: " + std::string(sbee.what()) + " Block: " + sbee.
                    blockId, 2);
                this->showMessageBox(
                    "스크립트 실행 중 오류 발생:\n" + std::string(sbee.what()) + "\n블록 ID: " + sbee.blockId + "\n블록 타입: " + sbee.
                    blockType, msgBoxIconType.ICON_ERROR);
                exit(EXIT_FAILURE);
            }
            catch (const std::exception &e) {
                EngineStdOut("Exception in worker thread task: " + std::string(e.what()), 2);
                this->showMessageBox("작업 스레드에서 예외 발생:\n" + std::string(e.what()), msgBoxIconType.ICON_ERROR);
                exit(EXIT_FAILURE);
            }
            catch (...) {
                EngineStdOut("Unknown exception in worker thread task.", 2);
                this->showMessageBox("알 수 없는 예외가 작업 스레드에서 발생했습니다.", msgBoxIconType.ICON_ERROR);
                exit(EXIT_FAILURE);
            }
        }
    }
}

static void Helper_DrawFilledCircle(SDL_Renderer *renderer, int centerX, int centerY, int radius) {
    if (!renderer || radius <= 0)
        return;
    for (int dy = -radius; dy <= radius; dy++) {
        int dx_limit = static_cast<int>(sqrt(static_cast<float>(radius * radius - dy * dy)));
        SDL_RenderLine(renderer, centerX - dx_limit, centerY + dy, centerX + dx_limit, centerY + dy);
    }
}

// Engine.cpp 상단 또는 유틸리티 함수 영역에 추가
SDL_Color hueToRGB(double H) {
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

    switch (Hi) {
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

static void Helper_RenderFilledRoundedRect(SDL_Renderer *renderer, const SDL_FRect *rect, float radius) {
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

    if (w - 2 * r > 0) {
        SDL_FRect center_h_rect = {x + r, y, w - 2 * r, h};
        SDL_RenderFillRect(renderer, &center_h_rect);
    }
    if (h - 2 * r > 0) {
        SDL_FRect center_v_rect = {x, y + r, w, h - 2 * r};
        SDL_RenderFillRect(renderer, &center_v_rect);
    }

    if (r > 0) {
        Helper_DrawFilledCircle(renderer, static_cast<int>(x + r), static_cast<int>(y + r), static_cast<int>(r));
        Helper_DrawFilledCircle(renderer, static_cast<int>(x + w - r), static_cast<int>(y + r), static_cast<int>(r));
        Helper_DrawFilledCircle(renderer, static_cast<int>(x + r), static_cast<int>(y + h - r), static_cast<int>(r));
        Helper_DrawFilledCircle(renderer, static_cast<int>(x + w - r), static_cast<int>(y + h - r),
                                static_cast<int>(r));
    } else if (r == 0.0f) {
        SDL_RenderFillRect(renderer, rect);
    }
}

Engine::~Engine() {
    EngineStdOut("Engine shutting down...");
    m_isShuttingDown.store(true, std::memory_order_relaxed); // 모든 스레드에 종료 신호

    // 1. BlockExecutor::ThreadPool (engine.threadPool) 명시적 종료
    // 이 풀의 작업자 스레드가 종료 시 로그를 남기므로, 다른 리소스 해제 전에 완료해야 합니다.
    if (threadPool) {
        EngineStdOut("Stopping BlockExecutor::ThreadPool...", 0);
        threadPool.reset(); // unique_ptr의 reset()을 호출하여 ThreadPool 소멸자 실행 및 스레드 join
        EngineStdOut("BlockExecutor::ThreadPool stopped.", 0);
    }

    // 2. Engine 자체의 m_workerThreads 풀 종료 (사용 중인 경우)
    stopThreadPool(); // 이 함수는 m_workerThreads를 중지하고 join합니다.

    // TerminateGE 보다 먼저 폰트 캐시 정리
    for (auto const &[key, val]: m_fontCache) {
        TTF_CloseFont(val);
    }
    m_fontCache.clear();
    EngineStdOut("Font cache cleared.", 0);

    // Entity 객체들 명시적 삭제
    EngineStdOut("Deleting entity objects...", 0);
    entities.clear();
    EngineStdOut("Entity objects deleted.", 0);

    terminateGE();
    objectScripts.clear(); // entities 삭제 후 objectScripts 정리
    EngineStdOut("Object Script Clear"); // 이 로그는 이제 더 안전한 시점에 출력됩니다.
}

std::string Engine::getSafeStringFromJson(const nlohmann::json &parentValue,
                                          const string &fieldName,
                                          const string &contextDescription,
                                          const string &defaultValue,
                                          bool isCritical, // LCOV_EXCL_LINE
                                          bool allowEmpty) // Removed const
{
    if (!parentValue.is_object()) {
        EngineStdOut("Parent for field '" + fieldName + "' in " + contextDescription + " is not an object. Value: " +
                     NlohmannJsonToString(parentValue),
                     2);
        return defaultValue;
    }

    if (!parentValue.contains(fieldName.c_str())) {
        if (isCritical) {
            EngineStdOut(
                "Critical field '" + fieldName + "' missing in " + contextDescription + ". Using default: '" +
                defaultValue + "'.",
                2);
        }

        return defaultValue;
    }

    const nlohmann::json &fieldValue = parentValue[fieldName.c_str()];

    if (!fieldValue.is_string()) {
        if (isCritical || !fieldValue.is_null()) {
            EngineStdOut(
                "Field '" + fieldName + "' in " + contextDescription + " is not a string. Value: [" +
                NlohmannJsonToString(fieldValue) +
                "]. Using default: '" + defaultValue + "'.",
                1);
        }
        return defaultValue;
    }

    string s_val = fieldValue.get<std::string>(); // 문자열 값 가져오기
    if (s_val.empty() && !allowEmpty) {
        if (isCritical) {
            EngineStdOut(
                "Critical field '" + fieldName + "' in " + contextDescription +
                " is an empty string, but empty is not allowed. Using default: '" + defaultValue + "'.",
                2);
        } else {
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
bool Engine::loadProject(const string &projectFilePath) {
    EngineStdOut("Initial Project JSON file parsing...", 0);
    this->m_currentProjectFilePath = projectFilePath;
    nlohmann::json document; {
        lock_guard lock(m_fileMutex);
        ifstream projectFile(projectFilePath);
        if (!projectFile.is_open()) {
            showMessageBox("Failed to open project file: " + projectFilePath, msgBoxIconType.ICON_ERROR);
            EngineStdOut("Failed to open project file: " + projectFilePath, 2);
            return false;
        }

        try {
            document = nlohmann::json::parse(projectFile);
            projectFile.close();
        } catch (const nlohmann::json::parse_error &e) {
            string errorMsg = "Failed to parse project file: " + string(e.what()) +
                              " (byte offset: " + to_string(e.byte) + ")";
            EngineStdOut(errorMsg, 2);
            showMessageBox("Failed to parse project file", msgBoxIconType.ICON_ERROR);
            projectFile.close();
            return false;
        }
    }
    // --- 기존 데이터 초기화 ---
    // 기존에 있던 초기화
    {
        lock_guard lock(m_engineDataMutex);
        objects_in_order.clear();
        entities.clear();
        objectScripts.clear();
        m_mouseClickedScripts.clear();
        m_mouseClickCanceledScripts.clear();
        m_whenObjectClickedScripts.clear();
        m_whenObjectClickCanceledScripts.clear();
        m_messageReceivedScripts.clear();
        m_whenCloneStartScripts.clear();
        m_pressedObjectId = "";
        firstSceneIdInOrder = ""; // currentSceneId는 이 값 또는 파싱된 값으로 설정됩니다.
        m_sceneOrder.clear();
    }

    // 추가된 초기화
    {
        lock_guard lock(m_engineDataMutex);
        m_HUDVariables.clear();
        scenes.clear();
        m_cloneCounters.clear();

        m_scriptExecutionCounter.store(0, std::memory_order_relaxed);
        m_needsTextureRecreation = false;

        resetProjectTimer(); // m_projectTimerValue, m_projectTimerRunning, m_projectTimerStartTime 초기화
        m_gameplayInputActive = false; // 게임플레이 입력 상태 초기화

        // 마우스 상태 초기화
        m_currentStageMouseX = 0.0f;
        m_currentStageMouseY = 0.0f;
        m_isMouseOnStage = false;
        m_stageWasClickedThisFrame.store(false, std::memory_order_relaxed);

        // HUD 변수 드래그 및 UI 상태 초기화
        m_draggedHUDVariableIndex = -1;
        m_currentHUDDragState = HUDDragState::NONE;
        m_draggedHUDVariableMouseOffsetX = 0.0f;
        m_draggedHUDVariableMouseOffsetY = 0.0f;
        m_draggedScrollbarListIndex = -1;
        m_scrollbarDragStartY = 0.0f;
        m_scrollbarDragInitialOffset = 0.0f;
        m_maxVariablesListContentWidth = 180.0f; // 기본값으로 리셋
    }

    // 텍스트 입력 관련 상태 (별도 뮤텍스 필요 시 해당 스코프 내에서 처리)
    // clearTextInput(); // 이 함수를 호출하거나 아래처럼 직접 초기화
    clearTextInput();
    m_needAnswerUpdate.store(false, std::memory_order_relaxed);


    // 키보드 입력 상태 (별도 뮤텍스 필요 시 해당 스코프 내에서 처리)
    {
        std::lock_guard<std::mutex> lock(m_pressedKeysMutex);
        m_pressedKeys.clear();
    }

    // 디버거 스크롤 위치 초기화
    m_debuggerScrollOffsetY = 0.0f;

    // 줌 관련 상태 초기화 (zoomFactor는 specialConfig 파싱 후 설정됨)
    m_isDraggingZoomSlider = false;
    // zoomFactor = 1.0f; // 기본값. specialConfig.setZoomfactor에 의해 덮어쓰여질 것임

    initFps(); // FPS 카운터 관련 변수들 초기화

    // PROJECT_NAME 파싱
    if (document.contains("name") && document["name"].is_string()) {
        PROJECT_NAME = document["name"].get<string>();
    } else {
        PROJECT_NAME = "Omocha Project";
        EngineStdOut("'name' field missing or not a string in project root. Using default: " + PROJECT_NAME, 1);
    }
    if (m_restartRequested.load() == true) {
        SDL_SetWindowTitle(window, string(PROJECT_NAME).c_str());
    }
    WINDOW_TITLE = PROJECT_NAME.empty() ? "Omocha Engine" : PROJECT_NAME;
    EngineStdOut("Project Name: " + (PROJECT_NAME.empty() ? "[Not Set]" : PROJECT_NAME), 0);
    // TARGET_FPS 파싱
    if (document.contains("speed") && document["speed"].is_number()) {
        int speed = document["speed"].get<int>();
        if (speed > 0)
            this->specialConfig.TARGET_FPS = speed;
        else
            EngineStdOut(
                "'speed' field is not a positive integer. Using default TARGET_FPS: " + to_string(
                    this->specialConfig.TARGET_FPS), 1);
        EngineStdOut("Target FPS set from project.json: " + to_string(this->specialConfig.TARGET_FPS), 0);
    } else {
        EngineStdOut(
            "'speed' field missing or not numeric in project.json. Using default TARGET_FPS: " + to_string(
                this->specialConfig.TARGET_FPS), 1);
    }

    // specialConfig 파싱
    if (document.contains("specialConfig")) {
        const nlohmann::json &specialConfigJson = document["specialConfig"];
        if (specialConfigJson.is_object()) {
            // brandName
            if (specialConfigJson.contains("brandName") && specialConfigJson["brandName"].is_string()) {
                this->specialConfig.BRAND_NAME = specialConfigJson["brandName"].get<string>();
            } else {
                this->specialConfig.BRAND_NAME = "";
            }

            EngineStdOut("Brand Name: " + (this->specialConfig.BRAND_NAME.empty()
                                               ? "[Not Set]"
                                               : this->specialConfig.BRAND_NAME), 0);

            // showZoomSliderUI
            if (specialConfigJson.contains("showZoomSliderUI") && specialConfigJson["showZoomSliderUI"].is_boolean()) {
                this->specialConfig.showZoomSlider = specialConfigJson["showZoomSliderUI"].get<bool>();
            } else {
                this->specialConfig.showZoomSlider = false;
                EngineStdOut("'specialConfig.showZoomSliderUI' field missing or not boolean. Using default: false", 1);
            }

            // setZoomfactor
            if (specialConfigJson.contains("setZoomfactor") && specialConfigJson["setZoomfactor"].is_number()) {
                this->specialConfig.setZoomfactor = std::clamp(specialConfigJson["setZoomfactor"].get<double>(),
                                                               (double) Engine::MIN_ZOOM, (double) Engine::MAX_ZOOM);
            } else {
                this->specialConfig.setZoomfactor = 1.0f;
                EngineStdOut("'specialConfig.setZoomfactor' field missing or not numeric. Using default: 1.0", 1);
            }

            // showProjectNameUI
            if (specialConfigJson.contains("showProjectNameUI") && specialConfigJson["showProjectNameUI"].
                is_boolean()) {
                this->specialConfig.SHOW_PROJECT_NAME = specialConfigJson["showProjectNameUI"].get<bool>();
            } else {
                this->specialConfig.SHOW_PROJECT_NAME = false;
                EngineStdOut("'specialConfig.showProjectNameUI' field missing or not boolean. Using default: false", 1);
            }

            // showFPS
            if (specialConfigJson.contains("showFPS") && specialConfigJson["showFPS"].is_boolean()) {
                this->specialConfig.showFPS = specialConfigJson["showFPS"].get<bool>();
            } else {
                this->specialConfig.showFPS = false;
                EngineStdOut("'specialConfig.showFPS' field missing or not boolean. Using default: false", 1);
            }

            // maxEntity
            if (specialConfigJson.contains("maxEntity") && specialConfigJson["maxEntity"].is_number()) {
                int maxEntity = specialConfigJson["maxEntity"].get<int>();
                if (maxEntity > 0) {
                    this->specialConfig.MAX_ENTITY = maxEntity;
                } else {
                    this->specialConfig.MAX_ENTITY = 100;
                    EngineStdOut("'specialConfig.maxEntity' is not a positive integer. Using default: 100", 1);
                }
            } else {
                this->specialConfig.MAX_ENTITY = 100;
            }
        }
    }

    this->zoomFactor = this->specialConfig.setZoomfactor;

    // Helper 람다 함수들
    auto getJsonBool = [&](const nlohmann::json &parentValue, const char *fieldName, bool defaultValue,
                           const std::string &contextForLog) -> bool {
        if (parentValue.contains(fieldName) && parentValue[fieldName].is_boolean()) {
            return parentValue[fieldName].get<bool>();
        }
        this->EngineStdOut(
            "'" + contextForLog + "." + fieldName + "' field missing or not boolean. Using default: " + (
                defaultValue ? "true" : "false"), 1);
        return defaultValue;
    };

    auto getJsonDoubleClamped = [&](const nlohmann::json &parentValue, const char *fieldName, double defaultValue,
                                    double minVal, double maxVal, const std::string &contextForLog) -> double {
        if (parentValue.contains(fieldName) && parentValue[fieldName].is_number()) {
            return clamp(parentValue[fieldName].get<double>(), minVal, maxVal);
        }
        this->EngineStdOut(
            "'" + contextForLog + "." + fieldName + "' field missing or not numeric. Using default: " +
            std::to_string(defaultValue), 1);
        return defaultValue;
    };

    // Variables 파싱
    if (document.contains("variables") && document["variables"].is_array()) {
        const nlohmann::json &variablesJson = document["variables"];
        EngineStdOut("Found " + to_string(variablesJson.size()) + " variables. Processing...", 0);

        for (auto i_var = 0; i_var < variablesJson.size(); ++i_var) {
            const auto &variableJson = variablesJson[i_var];
            if (!variableJson.is_object()) {
                EngineStdOut(
                    "Variable entry at index " + to_string(i_var) + " is not an object. Skipping. Content: " +
                    NlohmannJsonToString(variableJson), 3);
                continue;
            }

            HUDVariableDisplay currentVarDisplay;

            // name 파싱
            if (variableJson.contains("name") && variableJson["name"].is_string()) {
                currentVarDisplay.name = variableJson["name"].get<string>();
            } else {
                EngineStdOut(
                    "Variable name is missing or not a string for variable at index " + to_string(i_var) +
                    ". Skipping variable.", 1);
                continue;
            }

            if (currentVarDisplay.name.empty()) {
                EngineStdOut(
                    "Variable name is empty for variable at index " + to_string(i_var) + ". Skipping variable.", 1);
                continue;
            }

            // id 파싱 (변수 식별에 중요)
            if (variableJson.contains("id") && variableJson["id"].is_string()) {
                currentVarDisplay.id = variableJson["id"].get<string>();
                if (currentVarDisplay.id.empty()) {
                    EngineStdOut(
                        "Variable '" + currentVarDisplay.name + "' has an empty 'id' field. Skipping variable.", 1);
                    continue;
                }
            } else {
                EngineStdOut(
                    "Variable 'id' is missing or not a string for variable '" + currentVarDisplay.name + "' at index " +
                    to_string(i_var) + ". Skipping variable.", 1);
                continue;
            }
            // value 파싱
            if (variableJson.contains("value")) {
                const auto &valNode = variableJson["value"];
            if (valNode.is_string()) {
                std::string raw_value = valNode.get<std::string>();
                std::string sanitized_value;
                for (char c : raw_value) {
                    auto uc = static_cast<unsigned char>(c);

                    // 1. 일반적인 출력 가능 ASCII 문자 (스페이스 포함)
                    // 2. 탭, 줄바꿈, 캐리지 리턴
                    // 3. 0x7F (DEL) 보다 큰 바이트 (멀티바이트 UTF-8 문자의 일부일 가능성이 높음)
                    if ((uc >= 32 && uc <= 126) || // Printable ASCII
                        uc == '\t' || uc == '\n' || uc == '\r' || // Common whitespace
                        uc > 0x7F) // Likely part of a multi-byte UTF-8 character (e.g., Korean, emoji)
                    {
                        sanitized_value += c;
                    }
                    // else: 0-31 범위의 제어 문자 (탭, 줄바꿈, 캐리지리턴 제외) 및 127 (DEL)은 제거됩니다.
                }
                currentVarDisplay.value = sanitized_value;

                // 정제 후 문자열이 비어있고, 리스트 타입이 아니라면 "0"으로 설정
                if (currentVarDisplay.variableType != "list" && currentVarDisplay.value.empty()) {
                    currentVarDisplay.value = "0";
                    // 로그 메시지는 필요에 따라 추가/수정
                    EngineStdOut(
                        "Variable '" + currentVarDisplay.name +
                        "' had an empty or fully sanitized string value. Defaulting to \"0\".", 1);
                }
            } else if (valNode.is_number_integer()) {
                    long long int_val = valNode.get<long long>();
                    currentVarDisplay.value = std::to_string(int_val); // CORRECT
                } else if (valNode.is_number_float()) {
                    double float_val = valNode.get<double>();
                    if (isnan(float_val)) currentVarDisplay.value = "NaN";
                    else if (isinf(float_val)) currentVarDisplay.value = (float_val > 0 ? "Infinity" : "-Infinity");
                    else {
                        std::string s = std::to_string(float_val);
                        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
                        if (!s.empty() && s.back() == '.') {
                            s.pop_back();
                        }
                        currentVarDisplay.value = s; // CORRECT
                    }
                } else if (valNode.is_boolean()) {
                    currentVarDisplay.value = valNode.get<bool>() ? "true" : "false";
                } else if (valNode.is_null()) {
                    currentVarDisplay.value = "0"; // 엔트리는 초기화되지 않은 변수를 0으로 취급하는 경향
                    EngineStdOut(
                        "Variable '" + currentVarDisplay.name +
                        "' has a null value. Interpreting as \"0\".", 1);
                } else {
                    currentVarDisplay.value = "0"; // 예상치 못한 타입도 "0"으로
                    EngineStdOut(
                        "Variable '" + currentVarDisplay.name +
                        "' has an unexpected type for 'value' field. Interpreting as \"0\". Value: " +
                        NlohmannJsonToString(valNode), 1);
                }
            } else {
                currentVarDisplay.value = "0"; // 'value' 필드가 없으면 "0"으로 초기화
                EngineStdOut(
                    "Variable '" + currentVarDisplay.name + "' is missing 'value' field. Interpreting as \"0\".",
                    1);
            }
            if (currentVarDisplay.variableType != "list" && currentVarDisplay.value.empty()) {
                currentVarDisplay.value = "0"; // Default to "0" if empty after parsing
                EngineStdOut(
                    "Variable '" + currentVarDisplay.name +
                    "' had an empty or fully sanitized string value after parsing. Defaulting to \"0\".", 1);
            }
            // ... then push_back under lock ...

            // visible 파싱
            currentVarDisplay.isVisible = false;
            if (variableJson.contains("visible") && variableJson["visible"].is_boolean()) {
                currentVarDisplay.isVisible = variableJson["visible"].get<bool>();
            } else {
                EngineStdOut(
                    "'visible' field missing or not boolean for variable '" + currentVarDisplay.name +
                    "'. Using default: false", 2);
            }

            // variableType 파싱
            if (variableJson.contains("variableType") && variableJson["variableType"].is_string()) {
                currentVarDisplay.variableType = variableJson["variableType"].get<string>();
            } else {
                currentVarDisplay.variableType = "variable";
                EngineStdOut(
                    "Variable '" + currentVarDisplay.name +
                    "' missing 'variableType' or not a string. Using default: 'variable'", 1);
            }

            // object 파싱
            if (variableJson.contains("object") && !variableJson["object"].is_null()) {
                if (variableJson["object"].is_string()) {
                    currentVarDisplay.objectId = variableJson["object"].get<string>();
                } else {
                    currentVarDisplay.objectId = "";
                    EngineStdOut(
                        "Variable '" + currentVarDisplay.name + "' 'object' field is not a string. Using empty.", 1);
                }
            } else {
                currentVarDisplay.objectId = "";
            }

            // x, y 좌표 파싱
            currentVarDisplay.x = variableJson.contains("x") && variableJson["x"].is_number()
                                      ? variableJson["x"].get<float>()
                                      : 0.0f;
            currentVarDisplay.y = variableJson.contains("y") && variableJson["y"].is_number()
                                      ? variableJson["y"].get<float>()
                                      : 0.0f;

            // isCloud 파싱 추가
            if (variableJson.contains("isCloud") && variableJson["isCloud"].is_boolean()) {
                currentVarDisplay.isCloud = variableJson["isCloud"].get<bool>();
            } else {
                currentVarDisplay.isCloud = false;
                // isCloud 필드가 없거나 boolean 타입이 아니면 기본값 false 사용
                EngineStdOut(
                    "Variable '" + currentVarDisplay.name +
                    "' 'isCloud' field missing or not boolean. Defaulting to false.", 1);
            }
            // 리스트 타입 처리
            if (currentVarDisplay.variableType == "list") {
                if (variableJson.contains("width") && variableJson["width"].is_number()) {
                    currentVarDisplay.width = variableJson["width"].get<float>();
                }

                if (variableJson.contains("height") && variableJson["height"].is_number()) {
                    currentVarDisplay.height = variableJson["height"].get<float>();
                }

                if (variableJson.contains("array") && variableJson["array"].is_array()) {
                    const nlohmann::json &arrayJson = variableJson["array"];
                    EngineStdOut(
                        "Found " + to_string(arrayJson.size()) + " items in the list variable '" + currentVarDisplay.
                        name + "'. Processing...", 0);

                    for (int j_item = 0; j_item < arrayJson.size(); ++j_item) {
                        const auto &itemJson = arrayJson[j_item];
                        if (!itemJson.is_object()) {
                            EngineStdOut(
                                "List item entry at index " + to_string(j_item) + " for list '" + currentVarDisplay.name
                                + "' is not an object. Skipping. Content: " + NlohmannJsonToString(itemJson), 1);
                            continue;
                        }

                        ListItem item;
                        if (itemJson.contains("key") && itemJson["key"].is_string()) {
                            item.key = itemJson["key"].get<string>();
                        } else {
                            item.key = "";
                        }

                        if (itemJson.contains("data") && itemJson["data"].is_string()) {
                            item.data = itemJson["data"].get<string>();
                        } else {
                            item.data = "";
                            EngineStdOut(
                                "List item for '" + currentVarDisplay.name + "' at index " + to_string(j_item) +
                                " missing 'data' or not string. Using empty.", 1);
                        }

                        currentVarDisplay.array.push_back(item);
                    }
                } else {
                    EngineStdOut(
                        "'array' field missing or not an array for variable '" + currentVarDisplay.name +
                        "'. Using default: empty array", 1);
                }
            }

            this->m_HUDVariables.push_back(currentVarDisplay);
            EngineStdOut(
                " Parsed variable: " + currentVarDisplay.name + " = " + currentVarDisplay.value + " (Type: " +
                currentVarDisplay.variableType + ")", 3);
        }
    } // "Variables" 파싱 if 문의 닫는 중괄호 추가

    /**
     * @brief 오브젝트 (objects) 및 관련 데이터(모양, 소리, 스크립트) 로드
     */
    if (document.contains("objects") && document["objects"].is_array()) {
        const nlohmann::json &objectsJson = document["objects"];
        EngineStdOut("Found " + to_string(objectsJson.size()) + " objects. Processing...", 0);

        for (auto i_obj = 0; i_obj < objectsJson.size(); ++i_obj) // Renamed loop variable
        {
            const auto &objectJson = objectsJson[i_obj];
            if (!objectJson.is_object()) {
                EngineStdOut(
                    "Object entry at index " + to_string(i_obj) + " is not an object. Skipping. Content: " +
                    NlohmannJsonToString(objectJson), // Use RapidJsonValueToString
                    1);
                continue;
            }
            string objectId;
            if (objectJson.contains("id") && objectJson["id"].is_string()) {
                objectId = objectJson["id"].get<string>();
            } else {
                EngineStdOut(
                    "Object ID is missing or not a string for object at index " + to_string(i_obj) +
                    ". Skipping object.", 2);
                continue;
            }
            if (objectId.empty()) {
                EngineStdOut("Object ID is empty for object at index " + to_string(i_obj) + ". Skipping object.", 2);
                continue;
            }
            ObjectInfo objInfo;
            objInfo.id = objectId;
            if (objectJson.contains("name") && objectJson["name"].is_string()) {
                objInfo.name = objectJson["name"].get<string>();
            } else {
                objInfo.name = "Unnamed Object";
            }
            if (objectJson.contains("objectType") && objectJson["objectType"].is_string()) {
                objInfo.objectType = objectJson["objectType"].get<string>();
            } else {
                objInfo.objectType = "sprite";
            }

            // Store entity JSON object if it exists
            if (objectJson.contains("entity") && objectJson["entity"].is_object()) {
                objInfo.entity = objectJson["entity"];
                EngineStdOut("Stored entity data for object: " + objInfo.name, 3);
            } else {
                // Create empty object if entity section is missing
                objInfo.entity = nlohmann::json::object();
                EngineStdOut("No entity data found for object: " + objInfo.name + ". Using empty object.", 1);
            }
            if (objectJson.contains("scene") && objectJson["scene"].is_string()) {
                objInfo.sceneId = objectJson["scene"].get<string>();
            } else {
                objInfo.sceneId = ""; // Default to global or handle as error
            }

            if (objectJson.contains("sprite") && objectJson["sprite"].is_object() &&
                objectJson["sprite"].contains("pictures") && objectJson["sprite"]["pictures"].is_array()) {
                const nlohmann::json &picturesJson = objectJson["sprite"]["pictures"];
                // LEVEL 0 -> 3 (아래 상세로그와 중복될 수 있음)
                EngineStdOut("Found " + to_string(picturesJson.size()) + " pictures for object " + objInfo.name, 3);
                for (auto j_pic = 0; j_pic < picturesJson.size(); ++j_pic) // Renamed loop variable
                {
                    const auto &pictureJson = picturesJson[j_pic];
                    if (pictureJson.is_object() && pictureJson.contains("id") && pictureJson["id"].is_string() &&
                        pictureJson.contains("filename") && pictureJson["filename"].is_string()) {
                        Costume ctu;
                        ctu.id = pictureJson["id"].get<string>();

                        if (ctu.id.empty()) {
                            EngineStdOut(
                                "Costume ID is empty for object " + objInfo.name + " at picture index " + to_string(
                                    j_pic) +
                                ". Skipping costume.",
                                2);
                            continue;
                        }
                        if (pictureJson.contains("name") && pictureJson["name"].is_string()) {
                            ctu.name = pictureJson["name"].get<string>();
                        } else {
                            ctu.name = "Unnamed Shape";
                        }
                        ctu.filename = pictureJson["filename"].get<string>();

                        if (ctu.filename.empty()) {
                            EngineStdOut(
                                "Costume filename is empty for " + ctu.name + " (ID: " + ctu.id +
                                "). Skipping costume.",
                                2);
                            continue;
                        }
                        if (pictureJson.contains("fileurl") && pictureJson["fileurl"].is_string()) {
                            ctu.fileurl = pictureJson["fileurl"].get<string>();
                        } else {
                            ctu.fileurl = "";
                        }
                        objInfo.costumes.push_back(ctu);
                        EngineStdOut(
                            "  Parsed costume: " + ctu.name + " (ID: " + ctu.id + ", File: " + ctu.filename + ")",
                            3); // LEVEL 0 -> 3
                    } else {
                        EngineStdOut(
                            "Invalid picture structure for object '" + objInfo.name + "' at index " + to_string(j_pic) +
                            ". Skipping.",
                            1);
                    }
                }
            } else {
                EngineStdOut("Object '" + objInfo.name + "' has no 'sprite/pictures' array or it's invalid.", 1);
            }

            if (objectJson.contains("sprite") && objectJson["sprite"].is_object() &&
                objectJson["sprite"].contains("sounds") && objectJson["sprite"]["sounds"].is_array()) {
                const nlohmann::json &soundsJson = objectJson["sprite"]["sounds"]; // LEVEL 0 -> 3 (아래 상세로그와 중복될 수 있음)
                EngineStdOut(
                    "Found " + to_string(soundsJson.size()) + " sounds for object " + objInfo.name + ". Parsing...", 3);

                for (auto j_sound = 0; j_sound < soundsJson.size(); ++j_sound) // Renamed loop variable
                {
                    const auto &soundJson = soundsJson[j_sound];
                    if (soundJson.is_object() && soundJson.contains("id") && soundJson["id"].is_string() && soundJson.
                        contains("filename") && soundJson["filename"].is_string()) {
                        SoundFile sound;
                        sound.id = soundJson["id"].get<string>();

                        if (sound.id.empty()) {
                            EngineStdOut(
                                "Sound ID is empty for object " + objInfo.name + " at sound index " + to_string(j_sound)
                                +
                                ". Skipping sound.",
                                2);
                            continue;
                        }
                        sound.name = getSafeStringFromJson(soundJson, "name", "sound id: " + sound.id, "Unnamed Sound",
                                                           false, true);
                        sound.filename = getSafeStringFromJson(soundJson, "filename", "sound id: " + sound.id, "", true,
                                                               false);
                        // Assuming getSafeStringFromJson is adapted for rapidjson or replaced
                        if (sound.filename.empty()) {
                            EngineStdOut(
                                "Sound filename is empty for " + sound.name + " (ID: " + sound.id +
                                "). Skipping sound.",
                                2);
                            continue;
                        }
                        sound.fileurl = getSafeStringFromJson(soundJson, "fileurl", "sound id: " + sound.id, "", false,
                                                              true);
                        // Assuming getSafeStringFromJson is adapted for rapidjson or replaced
                        if (soundJson.contains("ext") && soundJson["ext"].is_string()) {
                            sound.ext = soundJson["ext"].get<string>();
                        } else {
                            sound.ext = "";
                        }

                        double soundDuration = 0.0;
                        if (soundJson.contains("duration") && soundJson["duration"].is_number()) {
                            soundDuration = soundJson["duration"].get<double>();
                        } else {
                            EngineStdOut(
                                "Sound '" + sound.name + "' (ID: " + sound.id +
                                ") is missing 'duration' or it's not numeric. Using default duration 0.0.",
                                1);
                        }
                        sound.duration = soundDuration;
                        objInfo.sounds.push_back(sound);
                        EngineStdOut(
                            "  Parsed sound: " + sound.name + " (ID: " + sound.id + ", File: " + sound.filename + ")",
                            3); // LEVEL 0 -> 3
                    } else {
                        EngineStdOut(
                            "Invalid sound structure for object '" + objInfo.name + "' at index " + to_string(j_sound) +
                            ". Skipping.",
                            1);
                    }
                }
            } else {
                EngineStdOut("Object '" + objInfo.name + "' has no 'sprite/sounds' array or it's invalid.", 1);
            }

            string tempSelectedCostumeId;
            bool selectedCostumeFound = false;
            if (objectJson.contains("selectedPictureId") && objectJson["selectedPictureId"].is_string()) {
                tempSelectedCostumeId = objectJson["selectedPictureId"].get<string>();
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (!selectedCostumeFound && objectJson.contains("selectedCostume") && objectJson["selectedCostume"].
                is_string()) {
                tempSelectedCostumeId = objectJson["selectedCostume"].get<string>();
                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (!selectedCostumeFound && objectJson.contains("selectedCostume") && objectJson["selectedCostume"].
                is_object() &&
                objectJson["selectedCostume"].contains("id") && objectJson["selectedCostume"]["id"].is_string()) {
                // tempSelectedCostumeId = getSafeStringFromJson(objectJson["selectedCostume"], "id",
                //                                               "object " + objInfo.name + " selectedCostume object", "",
                //                                               false, false); // This call is problematic
                tempSelectedCostumeId = objectJson["selectedCostume"]["id"].get<string>();

                if (!tempSelectedCostumeId.empty())
                    selectedCostumeFound = true;
            }

            if (selectedCostumeFound) {
                objInfo.selectedCostumeId = tempSelectedCostumeId;
                EngineStdOut(
                    "Object '" + objInfo.name + "' (ID: " + objInfo.id + ") selected costume ID: " + objInfo.
                    selectedCostumeId, 3); // LEVEL 0 -> 3
            } else {
                if (!objInfo.costumes.empty()) {
                    objInfo.selectedCostumeId = objInfo.costumes[0].id;
                    EngineStdOut(
                        "Object '" + objInfo.name + "' (ID: " + objInfo.id +
                        ") is missing selectedPictureId/selectedCostume or it's invalid. Using first costume ID: " +
                        objInfo.costumes[0].id,
                        1);
                } else {
                    EngineStdOut(
                        "Object '" + objInfo.name + "' (ID: " + objInfo.id +
                        ") is missing selectedPictureId/selectedCostume and has no costumes.",
                        1);
                    objInfo.selectedCostumeId = "";
                }
            }

            if (objInfo.objectType == "textBox") {
                if (objectJson.contains("entity") && objectJson["entity"].is_object()) {
                    const nlohmann::json &entityJson = objectJson["entity"];

                    if (entityJson.contains("text")) {
                        if (entityJson["text"].is_string()) {
                            // objInfo.textContent = getSafeStringFromJson(entityJson, "text", "textBox " + objInfo.name,
                            //                                             "[DEFAULT TEXT]", false, true); // Problematic call
                            objInfo.textContent = entityJson["text"].get<string>();

                            if (objInfo.textContent == "<OMOCHA_ENGINE_NAME>") {
                                objInfo.textContent = string(OMOCHA_ENGINE_NAME);
                            } else if (objInfo.textContent == "<OMOCHA_DEVELOPER>") {
                                objInfo.textContent = "DEVELOPER: " + string(OMOCHA_DEVELOPER_NAME);
                            } else if (objInfo.textContent == "<OMOCHA_SDL_VERSION>") {
                                objInfo.textContent =
                                        "SDL VERSION: " + to_string(SDL_MAJOR_VERSION) + "." + to_string(
                                            SDL_MINOR_VERSION) + "." + to_string(SDL_MICRO_VERSION);
                            } else if (objInfo.textContent == "<OMOCHA_VERSION>") {
                                objInfo.textContent = "Engine Version: " + string(OMOCHA_ENGINE_VERSION);
                            }
                        } else if (entityJson["text"].is_number()) {
                            objInfo.textContent = to_string(entityJson["text"].get<double>());
                            EngineStdOut(
                                "INFO: textBox '" + objInfo.name + "' 'text' field is numeric. Converted to string: " +
                                objInfo.textContent, 3); // LEVEL 0 -> 3
                        } else {
                            objInfo.textContent = "[INVALID TEXT TYPE]";
                            EngineStdOut(
                                "textBox '" + objInfo.name + "' 'text' field has invalid type: " +
                                NlohmannJsonToString(entityJson["text"]),
                                1);
                        }
                    } else {
                        objInfo.textContent = "[NO TEXT FIELD]";
                        EngineStdOut("textBox '" + objInfo.name + "' is missing 'text' field.", 1);
                    }

                    if (entityJson.contains("colour") && entityJson["colour"].is_string()) {
                        // string hexColor = getSafeStringFromJson(entityJson, "colour", "textBox " + objInfo.name,
                        //                                         "#000000", false, false); // Problematic call
                        string hexColor = entityJson["colour"].get<string>();

                        if (hexColor.length() == 7 && hexColor[0] == '#') {
                            try {
                                unsigned int r = stoul(hexColor.substr(1, 2), nullptr, 16);
                                unsigned int g = stoul(hexColor.substr(3, 2), nullptr, 16);
                                unsigned int b = stoul(hexColor.substr(5, 2), nullptr, 16);
                                objInfo.textColor = {(Uint8) r, (Uint8) g, (Uint8) b, 255};
                                EngineStdOut(
                                    "INFO: textBox '" + objInfo.name + "' text color parsed: R=" + to_string(r) + ", G="
                                    + to_string(g) + ", B=" + to_string(b), 3); // LEVEL 0 -> 3
                            } catch (const exception &e) {
                                EngineStdOut(
                                    "Failed to parse text color '" + hexColor + "' for object '" + objInfo.name + "': "
                                    + e.what() + ". Using default #000000.", 2);
                                objInfo.textColor = {0, 0, 0, 255};
                            }
                        } else {
                            EngineStdOut(
                                "textBox '" + objInfo.name + "' 'colour' field is not a valid HEX string (#RRGGBB): " +
                                hexColor + ". Using default #000000.",
                                1);
                            objInfo.textColor = {0, 0, 0, 255};
                        }
                    } else {
                        EngineStdOut(
                            "textBox '" + objInfo.name +
                            "' is missing 'colour' field or it's not a string. Using default #000000.",
                            1);
                        objInfo.textColor = {0, 0, 0, 255};
                    }

                    // textBoxBackgroundColor 파싱 (기본값: 흰색)
                    if (entityJson.contains("bgColor") && entityJson["bgColor"].is_string()) {
                        string hexBgColor = entityJson["bgColor"].get<string>();
                        if (hexBgColor.length() == 7 && hexBgColor[0] == '#') {
                            try {
                                unsigned int r = stoul(hexBgColor.substr(1, 2), nullptr, 16);
                                unsigned int g = stoul(hexBgColor.substr(3, 2), nullptr, 16);
                                unsigned int b = stoul(hexBgColor.substr(5, 2), nullptr, 16);
                                objInfo.textBoxBackgroundColor = {(Uint8) r, (Uint8) g, (Uint8) b, 255};
                                EngineStdOut(
                                    "INFO: textBox '" + objInfo.name + "' background color parsed: R=" + to_string(r) +
                                    ", G=" + to_string(g) + ", B=" + to_string(b), 0);
                            } catch (const exception &e) {
                                EngineStdOut(
                                    "Failed to parse textBoxBackgroundColor '" + hexBgColor + "' for object '" + objInfo
                                    .name + "': " + e.what() + ". Using default #FFFFFF.", 2);
                                objInfo.textBoxBackgroundColor = {255, 255, 255, 0}; // 투명
                            }
                        } else {
                            EngineStdOut(
                                "textBox '" + objInfo.name +
                                "' 'textBoxBackgroundColor' field is not a valid HEX string (#RRGGBB): " + hexBgColor +
                                ". Using default #FFFFFF.", 1);
                            objInfo.textBoxBackgroundColor = {255, 255, 255, 0}; // 투명
                        }
                    } else {
                        EngineStdOut(
                            "textBox '" + objInfo.name +
                            "' is missing 'textBoxBackgroundColor' field or it's not a string. Using default #FFFFFF.",
                            1);
                        objInfo.textBoxBackgroundColor = {255, 255, 255, 0}; // 투명
                    }

                    if (entityJson.contains("font") && entityJson["font"].is_string()) {
                        objInfo.fontName = entityJson["font"].get<std::string>();
                        std::string remainingFontStr = objInfo.fontName;
                        trim(remainingFontStr); // 앞뒤 공백 제거

                        // 1. "bold" 키워드 확인 및 제거
                        size_t boldPos = remainingFontStr.find("bold");
                        if (boldPos != std::string::npos) {
                            // "bold" 앞이나 뒤에 공백 또는 문자열의 시작/끝인지 확인
                            bool isBoldValid = true;
                            if (boldPos > 0 && !std::isspace(remainingFontStr[boldPos - 1])) {
                                isBoldValid = false; // "somethingbold" 같은 경우
                            }
                            if (boldPos + 4 < remainingFontStr.length() && !
                                std::isspace(remainingFontStr[boldPos + 4])) {
                                isBoldValid = false; // "boldsomething" 같은 경우
                            }

                            if (isBoldValid) {
                                objInfo.Bold = true;
                                remainingFontStr.erase(boldPos, 4); // "bold" 제거
                                trim(remainingFontStr);
                            }
                        }

                        // 2. "italic" 키워드 확인 및 제거 (bold 제거 후)
                        size_t italicPos = remainingFontStr.find("italic");
                        if (italicPos != std::string::npos) {
                            bool isItalicValid = true;
                            if (italicPos > 0 && !std::isspace(remainingFontStr[italicPos - 1])) {
                                isItalicValid = false;
                            }
                            if (italicPos + 6 < remainingFontStr.length() && !std::isspace(
                                    remainingFontStr[italicPos + 6])) {
                                isItalicValid = false;
                            }

                            if (isItalicValid) {
                                objInfo.Italic = true;
                                remainingFontStr.erase(italicPos, 6); // "italic" 제거
                                trim(remainingFontStr);
                            }
                        }

                        // 3. 폰트 크기("px" 단위) 파싱
                        size_t pxPos = remainingFontStr.find("px");
                        std::string sizePart;
                        std::string namePartCandidate = remainingFontStr; // 기본적으로 남은 전체를 이름 후보로

                        if (pxPos != std::string::npos && pxPos > 0) {
                            sizePart = remainingFontStr.substr(0, pxPos);
                            trim(sizePart); // 숫자 부분 앞뒤 공백 제거

                            bool sizeIsNumber = true;
                            for (char c: sizePart) {
                                if (!std::isdigit(c) && c != '.') {
                                    // 소수점 허용
                                    sizeIsNumber = false;
                                    break;
                                }
                            }
                            if (sizeIsNumber && !sizePart.empty()) {
                                try {
                                    objInfo.fontSize = static_cast<int>(std::stod(sizePart));
                                    if (objInfo.fontSize <= 0) objInfo.fontSize = 20; // 유효하지 않은 크기는 기본값으로
                                    // 크기 파싱 성공 시, "px" 뒤의 문자열을 폰트 이름 후보로
                                    namePartCandidate = remainingFontStr.substr(pxPos + 2);
                                } catch (const std::exception &e) {
                                    // 숫자 변환 실패 시 기본값 유지
                                    EngineStdOut("Failed to parse font size from: " + sizePart + ". Error: " + e.what(),
                                                 1);
                                }
                            }
                        }
                        trim(namePartCandidate);
                        if (!namePartCandidate.empty()) {
                            objInfo.fontName = namePartCandidate;
                        }

                        EngineStdOut(
                            "Parsed Font Info for '" + objInfo.name + "': Size=" + std::to_string(objInfo.fontSize) +
                            ", Name='" + objInfo.fontName + "'" +
                            (objInfo.Bold ? ", Bold" : "") +
                            (objInfo.Italic ? ", Italic" : ""), 3);
                    } else {
                        objInfo.fontName = "20px Nanum Gothic"; // 기본값
                        objInfo.Bold = false;
                        objInfo.Italic = false;
                        objInfo.fontSize = 20;
                    }
                    //엔티티 속성에 있는것.
                    if (entityJson.contains("underLine") && entityJson["underLine"].is_boolean()) {
                        objInfo.Underline = entityJson["underLine"].get<bool>();
                    } else if (entityJson.contains("strike") && entityJson["strike"].is_boolean()) {
                        objInfo.Strike = entityJson["strike"].get<bool>();
                    }
                    if (entityJson.contains("textAlign") && entityJson["textAlign"].is_number()) {
                        int parsedAlign = entityJson["textAlign"].get<int>();
                        if (parsedAlign >= 0 && parsedAlign <= 2) {
                            // 0: left, 1: center, 2: right
                            objInfo.textAlign = parsedAlign;
                        } else {
                            objInfo.textAlign = 0; // Default to left align for invalid values
                            EngineStdOut(
                                "textBox '" + objInfo.name + "' 'textAlign' field has an invalid value: " +
                                to_string(parsedAlign) + ". Using default alignment 0 (left).",
                                1);
                        }
                        EngineStdOut(
                            "INFO: textBox '" + objInfo.name + "' text alignment parsed: " + to_string(
                                objInfo.textAlign), 3); // LEVEL 0 -> 3
                    } else {
                        objInfo.textAlign = 0;
                        EngineStdOut(
                            "textBox '" + objInfo.name +
                            "' is missing 'textAlign' field or it's not numeric. Using default alignment 0.",
                            1);
                    }

                    if (entityJson.contains("lineBreak") && entityJson["lineBreak"].is_boolean()) {
                        objInfo.lineBreak = entityJson["lineBreak"].get<bool>();
                    } else {
                        objInfo.lineBreak = false;
                    }
                } else {
                    EngineStdOut(
                        "textBox '" + objInfo.name +
                        "' is missing 'entity' block or it's not an object. Cannot load text box properties.",
                        1);
                    objInfo.textContent = "[NO ENTITY BLOCK]";
                    objInfo.textBoxBackgroundColor = {255, 255, 255, 255}; // 기본 배경색
                    objInfo.textColor = {0, 0, 0, 255};
                    objInfo.fontName = "";
                    objInfo.fontSize = 20;
                    objInfo.textAlign = 0;
                }
            } else {
                objInfo.textContent = "";
                objInfo.textBoxBackgroundColor = {255, 255, 255, 255}; // 기본 배경색
                objInfo.textColor = {0, 0, 0, 255};
                objInfo.fontName = "";
                objInfo.fontSize = 20;
                objInfo.textAlign = 0;
            }

            objects_in_order.push_back(objInfo);

            if (objectJson.contains("entity") && objectJson["entity"].is_object()) {
                const nlohmann::json &entityJson = objectJson["entity"];

                double initial_x = entityJson.contains("x") && entityJson["x"].is_number()
                                       ? entityJson["x"].get<double>()
                                       : 0.0;
                double initial_y = entityJson.contains("y") && entityJson["y"].is_number()
                                       ? entityJson["y"].get<double>()
                                       : 0.0;
                double initial_regX = entityJson.contains("regX") && entityJson["regX"].is_number()
                                          ? entityJson["regX"].get<double>()
                                          : 0.0;
                double initial_regY = entityJson.contains("regY") && entityJson["regY"].is_number()
                                          ? entityJson["regY"].get<double>()
                                          : 0.0;
                double initial_scaleX = entityJson.contains("scaleX") && entityJson["scaleX"].is_number()
                                            ? entityJson["scaleX"].get<double>()
                                            : 1.0;
                double initial_scaleY = entityJson.contains("scaleY") && entityJson["scaleY"].is_number()
                                            ? entityJson["scaleY"].get<double>()
                                            : 1.0;
                double initial_rotation = entityJson.contains("rotation") && entityJson["rotation"].is_number()
                                              ? entityJson["rotation"].get<double>()
                                              : 0.0;
                double initial_direction = entityJson.contains("direction") && entityJson["direction"].is_number()
                                               ? entityJson["direction"].get<double>()
                                               : 90.0;
                int initial_width = entityJson.contains("width") && entityJson["width"].is_number()
                                        ? entityJson["width"].get<int>()
                                        : 100;
                int initial_height = entityJson.contains("height") && entityJson["height"].is_number()
                                         ? entityJson["height"].get<int>()
                                         : 100;

                int entity_constructor_width = initial_width;
                int entity_constructor_height = initial_height;

                if (objInfo.objectType == "textBox") {
                    if (!objInfo.textContent.empty() && objInfo.fontSize > 0) {
                        std::string fontPath = getFontPathByName(objInfo.fontName, objInfo.fontSize, this);
                        TTF_Font *tempFont = getFont(fontPath, objInfo.fontSize); // getFont handles caching

                        if (tempFont) {
                            int measuredW_val, measuredH_val; // 값 자체를 저장할 변수
                            // SDL3에서는 TTF_GetStringSize를 사용합니다.
                            if (TTF_GetStringSize(tempFont, objInfo.textContent.c_str(), 0, &measuredW_val,
                                                  &measuredH_val) == 0) // 주소 전달
                            {
                                entity_constructor_width = static_cast<double>(measuredW_val);
                                entity_constructor_height = static_cast<double>(measuredH_val);
                                EngineStdOut(
                                    "TextBox '" + objInfo.name + "' (ID: " + objectId +
                                    ") calculated dimensions for constructor: " +
                                    std::to_string(measuredW_val) + "x" + std::to_string(measuredH_val),
                                    3);
                            } else {
                                EngineStdOut(
                                    "Warning: TTF_GetStringSize failed for textBox '" + objInfo.name + "' (ID: " +
                                    objectId + "). Using project file dimensions. ", 1);
                            }
                        } else {
                            EngineStdOut(
                                "Warning: Font not found for textBox '" + objInfo.name + "' (ID: " + objectId +
                                "). Using project file dimensions.", 1);
                        }
                    } else {
                        EngineStdOut(
                            "Warning: TextBox '" + objInfo.name + "' (ID: " + objectId +
                            ") has empty text or invalid font size. Using project file dimensions.", 1);
                    }
                }

                bool initial_visible = entityJson.contains("visible") && entityJson["visible"].is_boolean()
                                           ? entityJson["visible"].get<bool>()
                                           : true;
                Entity::RotationMethod currentRotationMethod = Entity::RotationMethod::FREE;
                if (objectJson.contains("rotateMethod") && objectJson["rotateMethod"].is_string()) {
                    // 이정도 있는것으로 예상됨 하지만 발견 못한것 도 있을수 있음
                    // string rotationMethodStr = getSafeStringFromJson(objectJson, "rotationMethod",
                    //                                                  "object " + objInfo.name, "free", false, true); // Problematic
                    std::string rotationMethodStr = objectJson["rotateMethod"].get<string>();
                    if (rotationMethodStr == "free") {
                        currentRotationMethod = Entity::RotationMethod::FREE;
                    } else if (rotationMethodStr == "none") {
                        currentRotationMethod = Entity::RotationMethod::NONE;
                    } else if (rotationMethodStr == "vertical") {
                        currentRotationMethod = Entity::RotationMethod::VERTICAL;
                    } else if (rotationMethodStr == "horizontal") {
                        currentRotationMethod = Entity::RotationMethod::HORIZONTAL;
                    } else {
                        EngineStdOut(
                            "Invalid rotation method '" + rotationMethodStr + "' for object '" + objInfo.name +
                            "'. Using default 'free'.",
                            1);
                        currentRotationMethod = Entity::RotationMethod::FREE;
                    }
                } else {
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
                    // Use potentially overridden dimensions
                    entity_constructor_width, entity_constructor_height,
                    initial_visible, currentRotationMethod);

                // Initialize pen positions
                newEntity->brush.reset(initial_x, initial_y);
                newEntity->paint.reset(initial_x, initial_y);
                std::lock_guard lock(m_engineDataMutex);
                entities[objectId] = std::shared_ptr<Entity>(newEntity);
                // newEntity->startLogicThread(); // This seems to be commented out already
                EngineStdOut("INFO: Created Entity for object ID: " + objectId, 0);
            } else {
                EngineStdOut(
                    "Object '" + objInfo.name + "' (ID: " + objectId +
                    ") is missing 'entity' block or it's not an object. Cannot create Entity.",
                    1);
            }
            /**
             * @brief 스크립트 블럭
             *
             */
            if (objectJson.contains("script") && objectJson["script"].is_string()) {
                vector<Script> scriptsForObject; // 각 object에 대한 scriptsForObject를 여기서 선언
                // string scriptString = getSafeStringFromJson(objectJson, "script", "object " + objInfo.name, "", false,
                //                                             true); // Problematic
                std::string scriptString = objectJson["script"].get<string>();
                if (scriptString.empty()) {
                    EngineStdOut(
                        "INFO: Object '" + objInfo.name +
                        "' has an empty 'script' string. No scripts will be loaded for this object.",
                        0);
                } else {
                    nlohmann::json scriptNlohmannJson; // Use nlohmann::json for script parsing
                    try {
                        scriptNlohmannJson = nlohmann::json::parse(scriptString);
                    } catch (const nlohmann::json::parse_error &e) {
                        EngineStdOut(
                            "ERROR: Failed to parse script JSON string for object '" + objInfo.name + "': " + e.what(),
                            2);
                        showMessageBox(
                            "Failed to parse script JSON string for object '" + objInfo.name +
                            "'. Project loading aborted.", msgBoxIconType.ICON_ERROR);
                        return false;
                    }

                    if (scriptNlohmannJson.is_array()) // Check if root is an array
                    {
                        EngineStdOut("Script JSON string parsed successfully for object: " + objInfo.name, 3);
                        // LEVEL 0 -> 3
                        // vector<Script> scriptsForObject; // 원래 위치에서 이동
                        for (size_t j_script_stack = 0; j_script_stack < scriptNlohmannJson.size(); ++j_script_stack) {
                            // Use size_t
                            const auto &scriptStackJson = scriptNlohmannJson[j_script_stack];
                            if (scriptStackJson.is_array()) {
                                Script currentScript;
                                EngineStdOut(
                                    "  Parsing script stack " + to_string(j_script_stack + 1) + "/" + to_string(
                                        scriptNlohmannJson.size()) + " for object " + objInfo.name, 0);
                                for (size_t k_block = 0; k_block < scriptStackJson.size(); ++k_block) {
                                    // Use size_t
                                    const auto &blockJsonValue = scriptStackJson[k_block]; {
                                        string blockContext =
                                                "block at index " + to_string(k_block) + " in script stack " +
                                                to_string(j_script_stack + 1) + " for object " + objInfo.name;

                                        if (blockJsonValue.is_object()) {
                                            // Use the new helper function to parse the block, including its statements
                                            Block parsedTopLevelBlock = ParseBlockDataInternal(
                                                blockJsonValue, *this, blockContext); // Renamed to avoid confusion
                                            if (!parsedTopLevelBlock.id.empty() && !parsedTopLevelBlock.type.empty()) {
                                                // Check if parsing was successful (id is a good indicator)
                                                currentScript.blocks.push_back(std::move(parsedTopLevelBlock));
                                                // Log for the successfully parsed top-level block
                                                // This log is now potentially redundant if ParseBlockDataInternal itself logs verbosely on success.
                                                // Consider if this specific log is still needed or if the one inside ParseBlockDataInternal is sufficient.
                                                // For now, keeping it to match the original log structure.
                                                EngineStdOut(
                                                    "    Parsed block: id='" + currentScript.blocks.back().id +
                                                    "', type='" + currentScript.blocks.back().type + "'",
                                                    3); // LEVEL 0 -> 3
                                            } else {
                                                EngineStdOut(
                                                    "WARN: Skipping top-level block in " + blockContext + // Corrected
                                                    " due to missing id ('" + parsedTopLevelBlock.id + "') or type ('" +
                                                    parsedTopLevelBlock.type + "'). Content: " + NlohmannJsonToString(
                                                        blockJsonValue),
                                                    2, "");
                                            }
                                        } else // blockJsonValue is not an object
                                        {
                                            EngineStdOut(
                                                "WARN: Invalid block structure (not an object) in " + blockContext +
                                                ". Skipping block. Content: " + NlohmannJsonToString(blockJsonValue),
                                                1);
                                        }
                                    }
                                } // End of k_block loop (blocks within one script stack)

                                // After parsing all blocks for the currentScript:
                                if (!currentScript.blocks.empty()) {
                                    scriptsForObject.push_back(std::move(currentScript));
                                    // Add the parsed script to the object's list
                                } else {
                                    EngineStdOut(
                                        "  WARN: Script stack " + to_string(j_script_stack + 1) + " for object " +
                                        objInfo.name +
                                        " resulted in an empty script (e.g., all blocks were invalid). Skipping this stack.",
                                        1);
                                }
                            } // End of if (scriptStackJson.is_array())
                            else // scriptStackJson is not an array (invalid script stack)
                            {
                                EngineStdOut(
                                    "WARN: Script root for object '" + objInfo.name +
                                    "' (after parsing string) is not an array of script stacks. Skipping script parsing. Content: "
                                    + scriptNlohmannJson.dump(),
                                    1);
                                showMessageBox(
                                    "Failed to parse script JSON string for object '" + objInfo.name +
                                    "'. Project loading aborted.",
                                    msgBoxIconType.ICON_ERROR);
                                return false;
                            }
                        } // End of if (scriptNlohmannJson.is_array())

                        // After parsing all script stacks for the current object, assign to objectScripts
                        if (!scriptsForObject.empty()) {
                            objectScripts[objInfo.id] = std::move(scriptsForObject);
                            EngineStdOut(
                                "  Assigned " + std::to_string(objectScripts[objInfo.id].size()) +
                                " script stacks to object ID: " + objInfo.id, 3);
                        }
                    } else {
                        EngineStdOut(
                            "INFO: Object '" + objInfo.name +
                            "' is missing 'script' field or it's not a string. No scripts will be loaded for this object.",
                            0);
                    }
                }
            } else {
                EngineStdOut("project.json is missing 'objects' array or it's not an array.", 1);
                showMessageBox("project.json is missing 'objects' array or it's not an array.\nBrokenProject.",
                               msgBoxIconType.ICON_ERROR);
                return false;
            }
        } // End of objectsJson loop
    } // End of if (document.contains("objects") && document["objects"].is_array())
    else {
        EngineStdOut("project.json is missing 'objects' array or it's not an array. No objects loaded.", 1);
        // Potentially show a message box or return false if objects are mandatory
    }

    scenes.clear();
    /**
     * @brief 씬 (scenes) 정보 로드
     */
    if (document.contains("scenes") && document["scenes"].is_array()) {
        const nlohmann::json &scenesJson = document["scenes"]; // Correct
        EngineStdOut("Found " + to_string(scenesJson.size()) + " scenes. Parsing...", 0);
        for (auto i_scene = 0; i_scene < scenesJson.size(); ++i_scene) // Renamed loop variable
        {
            const auto &sceneJson = scenesJson[i_scene];

            if (!sceneJson.is_object()) {
                EngineStdOut(
                    "Scene entry at index " + to_string(i_scene) + " is not an object. Skipping. Content: " +
                    NlohmannJsonToString(sceneJson), // Use RapidJsonValueToString
                    1);
                continue;
            }

            if (sceneJson.contains("id") && sceneJson["id"].is_string() && sceneJson.contains("name") && sceneJson[
                    "name"].is_string()) {
                // string sceneId = getSafeStringFromJson(sceneJson, "id", "scene entry " + to_string(i_scene), "", true, false); // Problematic
                std::string sceneId = sceneJson["id"].get<string>();
                if (sceneId.empty()) {
                    EngineStdOut("Scene ID is empty for scene at index " + to_string(i_scene) + ". Skipping scene.", 2);
                    continue;
                }
                // string sceneName = getSafeStringFromJson(sceneJson, "name", "scene id: " + sceneId, "Unnamed Scene",
                //                                          false, true); // Problematic
                std::string sceneName = sceneJson["name"].get<string>();

                scenes[sceneId] = sceneName;
                m_sceneOrder.push_back(sceneId); // LEVEL 0 -> 3
                EngineStdOut("  Parsed scene: " + sceneName + " (ID: " + sceneId + ")", 0);
            } else {
                EngineStdOut(
                    "Invalid scene structure or 'id'/'name' fields missing/not strings for scene at index " +
                    to_string(i_scene) + ". Skipping.",
                    1);
                EngineStdOut("  Scene content: " + NlohmannJsonToString(sceneJson), 1);
            }
        }
    } else {
        EngineStdOut("project.json is missing 'scenes' array or it's not an array. No scenes loaded.", 1);
    }
    string startSceneId = "";

    if (document.contains("startScene") && document["startScene"].is_string()) {
        // startSceneId = getSafeStringFromJson(document, "startScene", "project root for startScene (legacy)", "", false,
        //                                      false); // Problematic
        startSceneId = document["startScene"].get<string>();
        EngineStdOut("'startScene' (legacy) found in project.json: " + startSceneId, 0);
    } else if (document.contains("start") && document["start"].is_object() && document["start"].contains("sceneId") &&
               document["start"]["sceneId"].is_string()) {
        // startSceneId = getSafeStringFromJson(document["start"], "sceneId", "project root start object", "", false,
        //                                      false); // Problematic
        startSceneId = document["start"]["sceneId"].get<string>();

        EngineStdOut("'start/sceneId' found in project.json: " + startSceneId, 0);
    } else {
        EngineStdOut("No explicit 'startScene' or 'start/sceneId' found in project.json.", 1);
    }

    if (!startSceneId.empty() && scenes.count(startSceneId)) {
        currentSceneId = startSceneId;
        EngineStdOut(

            "Initial scene set to explicit start scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")",
            0);
    } else {
        if (!m_sceneOrder.empty() && scenes.count(m_sceneOrder.front())) {
            currentSceneId = m_sceneOrder.front();
            firstSceneIdInOrder = currentSceneId; // Store the determined start scene
            EngineStdOut(
                "Initial scene set to first scene in array order: " + scenes[currentSceneId] + " (ID: " + currentSceneId
                + ")", 0);
        } else {
            EngineStdOut("No valid starting scene found in project.json or no scenes were loaded.", 2);
            firstSceneIdInOrder = ""; // No valid start scene
            currentSceneId = "";
            return false;
        }
    }
    /**
     * @brief 특정 이벤트에 연결될 스크립트 식별 (예: 시작 버튼 클릭, 키 입력, 메시지 수신 등)
     */

    for (auto const &[objectId, scriptsVec]: objectScripts) // Iterate over fully populated objectScripts
    {
        for (const auto &script: scriptsVec) {
            if (!script.blocks.empty()) {
                const Block &firstBlock = script.blocks[0];

                if (firstBlock.type == "when_run_button_click") {
                    if (script.blocks.size() > 1) {
                        startButtonScripts.push_back({objectId, &script});
                        EngineStdOut("  -> Found valid 'Start Button Clicked' script for object ID: " + objectId,
                                     3); // LEVEL 0 -> 3
                    } else {
                        EngineStdOut(
                            "  -> Found 'Start Button Clicked' script for object ID: " + objectId +
                            " but it has no subsequent blocks. Skipping.",
                            1);
                    }
                } else if (firstBlock.type == "when_some_key_pressed") {
                    if (script.blocks.size() > 1) {
                        string keyIdentifierString;
                        bool keyIdentifierFound = false;
                        if (firstBlock.paramsJson.is_array() && firstBlock.paramsJson.size() > 0) {
                            // nlohmann::json
                            if (firstBlock.paramsJson[0].is_string()) {
                                keyIdentifierString = firstBlock.paramsJson[0].get<std::string>();
                                keyIdentifierFound = true;
                            } else if (firstBlock.paramsJson[0].is_null() && firstBlock.paramsJson.size() > 1 &&
                                       // nlohmann::json
                                       firstBlock.paramsJson[1].is_string()) {
                                keyIdentifierString = firstBlock.paramsJson[1].get<std::string>();
                                keyIdentifierFound = true;
                            }

                            if (keyIdentifierFound) {
                                SDL_Scancode keyScancode = this->mapStringToSDLScancode(keyIdentifierString);
                                if (keyScancode != SDL_SCANCODE_UNKNOWN) {
                                    keyPressedScripts[keyScancode].push_back({objectId, &script});
                                }
                            } else {
                                EngineStdOut(
                                    " -> object ID " + objectId +
                                    " 'press key' invalid param or missing message ID. Params JSON: " + firstBlock.
                                    paramsJson.dump() + ".",
                                    1);
                            }
                        }
                    }
                } else if (firstBlock.type == "mouse_clicked") {
                    if (script.blocks.size() > 1) {
                        m_mouseClickedScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'mouse clicked' script.", 3);
                        // LEVEL 0 -> 3
                    }
                } else if (firstBlock.type == "mouse_click_cancled") {
                    if (script.blocks.size() > 1) {
                        m_mouseClickCanceledScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'mouse click canceled' script.", 3);
                        // LEVEL 0 -> 3
                    }
                } else if (firstBlock.type == "when_object_click") {
                    if (script.blocks.size() > 1) {
                        m_whenObjectClickedScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'click an object' script.", 3);
                        // LEVEL 0 -> 3
                    }
                } else if (firstBlock.type == "when_object_click_canceled") {
                    if (script.blocks.size() > 1) {
                        m_whenObjectClickCanceledScripts.push_back({objectId, &script});
                        EngineStdOut(
                            "  -> object ID " + objectId + " found 'When I release the click on an object' script.",
                            3); // LEVEL 0 -> 3
                    }
                } else if (firstBlock.type == "when_message_cast") {
                    if (script.blocks.size() > 1) {
                        // Log details of what's being checked and extracted
                        EngineStdOut(
                            "DEBUG_MSG: Processing when_message_cast for " + objectId + ". First block ID: " +
                            firstBlock.id, 3, "");
                        bool isArr = firstBlock.paramsJson.is_array();
                        int arrSize = isArr ? static_cast<int>(firstBlock.paramsJson.size()) : -1; // SizeType to int
                        bool firstIsStr = (isArr && arrSize >= 1) ? firstBlock.paramsJson[0].is_string() : false;
                        EngineStdOut("DEBUG_MSG:   paramsJson.is_array(): " + std::string(isArr ? "true" : "false") +
                                     ", Size: " + std::to_string(arrSize) +
                                     ", [0].is_string(): " + std::string(firstIsStr ? "true" : "false"),
                                     3, "");

                        string messageIdToReceive;
                        bool messageParamFound = false;

                        // After FilterNullsInParamsJsonArray, the signal ID should be the first element if present.
                        if (firstBlock.paramsJson.is_array() && !firstBlock.paramsJson.empty() && // Use !empty()
                            firstBlock.paramsJson[0].is_string()) {
                            messageIdToReceive = firstBlock.paramsJson[0].get<std::string>();
                            EngineStdOut("DEBUG_MSG:   Extracted messageIdToReceive: '" + messageIdToReceive + "'", 3,
                                         "");
                            messageParamFound = true;
                        }

                        if (messageParamFound && !messageIdToReceive.empty()) {
                            m_messageReceivedScripts[messageIdToReceive].push_back({objectId, &script});
                            EngineStdOut("  -> object ID " + objectId + " " + messageIdToReceive + " message found.",
                                         3); // LEVEL 0 -> 3
                        } else {
                            EngineStdOut(
                                " -> object ID " + objectId +
                                " 'recive signal' invalid param or missing message ID. Params JSON: " + firstBlock.
                                paramsJson.dump() + ".",
                                1);
                        }
                    }
                } else if (firstBlock.type == "when_scene_start") {
                    if (script.blocks.size() > 1) {
                        m_whenStartSceneLoadedScripts.push_back({objectId, &script});
                        EngineStdOut("  -> object ID " + objectId + " found 'when scene start' script.", 3);
                        // LEVEL 0 -> 3
                    }
                } else if (firstBlock.type == "when_clone_start") // 복제본 생성 이벤트
                {
                    if (script.blocks.size() > 1) {
                        m_whenCloneStartScripts.push_back({objectId, &script});
                        EngineStdOut("  -> Found 'when_clone_start' script for object ID: " + objectId, 3);
                    } else {
                        EngineStdOut(
                            "  -> Found 'when_clone_start' script for object ID: " + objectId +
                            " but it has no subsequent blocks. Skipping.", 1);
                    }
                }
            }
        }
    }
    EngineStdOut(
        "Finished identifying event-triggered scripts. Start button scripts found: " + to_string(
            startButtonScripts.size()), 0);
    EngineStdOut("Project JSON file parsed successfully.", 0);
    return true;
}

bool Engine::initGE(bool vsyncEnabled, bool attemptVulkan) {
    EngineStdOut("Initializing SDL...", 0);
    if (SDL_Init(SDL_INIT_VIDEO) < 0) // 오디오 부분은 Audio.h에서 초기화
    {
        // SDL 비디오 서브시스템 초기화
        EngineStdOut("SDL could not initialize! SDL_" + string(SDL_GetError()), 2);
        showMessageBox("Failed to initialize SDL: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    EngineStdOut("SDL video subsystem initialized successfully.", 0);

    if (TTF_Init() == -1) {
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
    if (attemptVulkan) {
        EngineStdOut("Attempting to create Vulkan renderer as requested by command line argument...", 0);
        // Vulkan 렌더러 생성 시도

        int numRenderDrivers = SDL_GetNumRenderDrivers();
        if (numRenderDrivers < 0) {
            EngineStdOut("Failed to get number of render drivers: " + string(SDL_GetError()), 2);
            SDL_DestroyWindow(this->window);
            this->window = nullptr;
            TTF_Quit();
            SDL_Quit();
            return false;
        }
        EngineStdOut("Available render drivers: " + to_string(numRenderDrivers), 0);

        int vulkanDriverIndex = -1;
        for (int i = 0; i < numRenderDrivers; ++i) {
            EngineStdOut("Checking render driver at index: " + to_string(i) + " " + SDL_GetRenderDriver(i), 0);
            const char *driverName = SDL_GetRenderDriver(i);

            if (driverName != nullptr && strcmp(driverName, "vulkan") == 0) {
                vulkanDriverIndex = i;
                EngineStdOut("Vulkan driver found at index: " + to_string(i), 0);
                break;
            }
        }

        if (vulkanDriverIndex != -1) {
            this->renderer = SDL_CreateRenderer(this->window, "vulkan");
            if (this->renderer) {
                EngineStdOut("Successfully created Vulkan renderer.", 0);
                string projectName = PROJECT_NAME;
                string windowTitleWithRenderer = projectName + " (Vulkan)";
                SDL_SetWindowTitle(window, windowTitleWithRenderer.c_str());
            } else {
                EngineStdOut(
                    "Failed to create Vulkan renderer even though driver was found: " + string(SDL_GetError()) +
                    ". Falling back to default.",
                    1);
                showMessageBox(
                    "Failed to create Vulkan renderer: " + string(SDL_GetError()) + ". Falling back to default.",
                    msgBoxIconType.ICON_WARNING);
                this->renderer = SDL_CreateRenderer(this->window, nullptr);
            }
        } else {
            EngineStdOut("Vulkan render driver not found. Using default renderer.", 1);
            showMessageBox("Vulkan render driver not found. Using default renderer.", msgBoxIconType.ICON_WARNING);
            this->renderer = SDL_CreateRenderer(this->window, nullptr);
        }
    } else {
        // 기본 SDL 렌더러 생성
        this->renderer = SDL_CreateRenderer(this->window, nullptr);
    }
    if (this->renderer == nullptr) {
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
                           vsyncEnabled ? SDL_RENDERER_VSYNC_ADAPTIVE : SDL_RENDERER_VSYNC_DISABLED) != 0) {
        // VSync 설정
        EngineStdOut("Failed to set VSync mode. SDL_" + string(SDL_GetError()), 1);
    } else {
        EngineStdOut("VSync mode set to: " + string(vsyncEnabled ? "Adaptive" : "Disabled"), 0);
    }

    string defaultFontPath = "font/nanum_gothic.ttf"; // 기본 폰트 경로
    hudFont = TTF_OpenFont(defaultFontPath.c_str(), 20);
    loadingScreenFont = TTF_OpenFont(defaultFontPath.c_str(), 30);
    percentFont = TTF_OpenFont(defaultFontPath.c_str(), 15);

    if (!hudFont) {
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
    if (!percentFont) {
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

    if (!loadingScreenFont) {
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

    if (!createTemporaryScreen()) {
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
    if (renderer && !m_HUDVariables.empty()) {
        // 렌더러와 HUD 변수가 모두 유효할 때만 실행
        EngineStdOut("Performing initial HUD variable position clamping...", 0);
        int windowW = 0, windowH = 0;
        SDL_GetRenderOutputSize(renderer, &windowW, &windowH);

        float screenCenterX = static_cast<float>(windowW) / 2.0f;
        float screenCenterY = static_cast<float>(windowH) / 2.0f;

        if (windowW > 0 && windowH > 0) {
            float itemHeight_const = 22.0f; // drawHUD 및 processInput과 일치
            float itemPadding_const = 3.0f; // drawHUD 및 processInput과 일치하는 아이템 패딩
            float clampedItemHeight = itemHeight_const + 2 * itemPadding_const;
            // drawHUD/processInput의 minContainerFixedWidth와 일치시키거나 적절한 기본값 사용
            float minContainerFixedWidth_const = 80.0f; // 컨테이너 최소 고정 너비

            for (auto &var: m_HUDVariables) {
                // x, y를 수정해야 하므로 참조로 반복
                if (!var.isVisible)
                    continue;

                float currentItemEstimatedWidth;
                if (var.variableType == "list" && var.width > 0) {
                    // 리스트이고 project.json에 너비가 지정된 경우 해당 너비 사용
                    currentItemEstimatedWidth = max(minContainerFixedWidth_const, var.width);
                } else {
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
        } else {
            EngineStdOut(
                "Window dimensions (W:" + to_string(windowW) + ", H:" + to_string(windowH) +
                ") not valid for initial HUD clamping.",
                1);
        }
    }
    return true;
}

bool Engine::createTemporaryScreen() {
    if (this->renderer == nullptr) {
        EngineStdOut("Renderer not initialized. Cannot create temporary screen texture.", 2); // 렌더러가 초기화되지 않음
        showMessageBox("Internal Renderer not available for offscreen buffer.", msgBoxIconType.ICON_ERROR);
        return false;
    }

    this->tempScreenTexture = SDL_CreateTexture(this->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                                PROJECT_STAGE_WIDTH, PROJECT_STAGE_HEIGHT);
    if (this->tempScreenTexture == nullptr) {
        string errMsg = "Failed to create temporary screen texture! SDL_" + string(SDL_GetError()); // 임시 화면 텍스처 생성 실패
        EngineStdOut(errMsg, 2);
        showMessageBox("Failed to create offscreen buffer: " + string(SDL_GetError()), msgBoxIconType.ICON_ERROR);
        return false;
    }
    EngineStdOut(
        "Temporary screen texture created successfully (" + to_string(PROJECT_STAGE_WIDTH) + "x" + to_string(
            PROJECT_STAGE_HEIGHT) + ").", 0);
    return true;
}

void Engine::destroyTemporaryScreen() {
    if (this->tempScreenTexture != nullptr) // 임시 화면 텍스처 파괴
    {
        SDL_DestroyTexture(this->tempScreenTexture);
        this->tempScreenTexture = nullptr;
        EngineStdOut("Temporary screen texture destroyed.", 0);
    }
}

TTF_Font *Engine::getFont(const std::string &fontPath, int fontSize) {
    std::pair<std::string, int> key = {fontPath, fontSize};
    auto it = m_fontCache.find(key);
    if (it != m_fontCache.end()) {
        return it->second; // 캐시된 폰트 반환
    }

    TTF_Font *font = TTF_OpenFont(fontPath.c_str(), fontSize);
    if (!font) {
        EngineStdOut("Failed to load font: " + fontPath + " at size " + std::to_string(fontSize) + ". SDL_ttf Error",
                     2);
        // Fallback or handle error appropriately
        // For example, try loading a default font if this one fails
        std::string defaultFontPath = std::string(FONT_ASSETS) + "nanum_gothic.ttf";
        if (fontPath != defaultFontPath) {
            // 무한 재귀 방지
            return getFont(defaultFontPath, fontSize); // 기본 폰트 시도
        }
        return nullptr; // 기본 폰트도 실패하면 null 반환
    }

    m_fontCache[key] = font; // 새 폰트를 캐시에 추가
    EngineStdOut("Loaded and cached font: " + fontPath + " at size " + std::to_string(fontSize), 3); // LEVEL 0 -> 3
    return font;
}

void Engine::terminateGE() {
    EngineStdOut("Terminating SDL and engine resources...", 0); // SDL 및 엔진 리소스 종료

    destroyTemporaryScreen();

    // 폰트 캐시에 있는 모든 폰트 닫기
    for (auto const &[key, val]: m_fontCache) {
        TTF_CloseFont(val);
    }
    m_fontCache.clear();
    EngineStdOut("Font cache cleared during terminateGE.", 0);

    if (hudFont) {
        TTF_CloseFont(hudFont);
        hudFont = nullptr;
        EngineStdOut("HUD font closed.", 0);
    }
    if (loadingScreenFont) {
        TTF_CloseFont(loadingScreenFont);
        loadingScreenFont = nullptr;
        EngineStdOut("Loading screen font closed.", 0);
    }
    // Costume 텍스처 해제
    for (auto &objInfo: objects_in_order) {
        for (auto &costume: objInfo.costumes) {
            if (costume.imageHandle) {
                SDL_DestroyTexture(costume.imageHandle);
                costume.imageHandle = nullptr;
            }
        }
    }
    TTF_Quit();
    EngineStdOut("SDL_ttf terminated.", 0);

    EngineStdOut("SDL_image terminated.", 0);

    if (this->renderer != nullptr) {
        SDL_DestroyRenderer(this->renderer);
        this->renderer = nullptr;
        EngineStdOut("SDL Renderer destroyed.", 0);
    }

    if (this->window != nullptr) {
        SDL_DestroyWindow(this->window);
        this->window = nullptr;
        EngineStdOut("SDL Window destroyed.", 0);
    }

    SDL_Quit();
    EngineStdOut("SDL terminated.", 0);
}

void Engine::handleRenderDeviceReset() {
    EngineStdOut("Render device was reset. All GPU resources will be recreated.", 1); // 렌더 장치 리셋됨. GPU 리소스 재생성

    destroyTemporaryScreen();

    for (auto &objInfo: objects_in_order) {
        if (objInfo.objectType == "sprite") {
            for (auto &costume: objInfo.costumes) {
                if (costume.imageHandle) {
                    SDL_DestroyTexture(costume.imageHandle);
                    costume.imageHandle = nullptr;
                }
            }
        }
    }

    m_needsTextureRecreation = true;
}

bool Engine::loadImages() {
    LOADING_METHOD_NAME = "Loading Sprites...";
    chrono::time_point<chrono::steady_clock> startTime = chrono::steady_clock::now();
    EngineStdOut("Starting image loading...", 0);
    totalItemsToLoad = 0;
    loadedItemCount = 0;

    // 기존 텍스처 및 서피스 핸들 해제
    for (auto &objInfo: objects_in_order) {
        if (objInfo.objectType == "sprite") {
            for (auto &costume: objInfo.costumes) {
                if (costume.imageHandle) {
                    SDL_DestroyTexture(costume.imageHandle);
                    costume.imageHandle = nullptr;
                }
                if (costume.surfaceHandle) {
                    // 추가: 서피스 핸들도 해제
                    SDL_DestroySurface(costume.surfaceHandle);
                    costume.surfaceHandle = nullptr;
                }
            }
        }
    }

    for (const auto &objInfo: objects_in_order) {
        if (objInfo.objectType == "sprite") {
            totalItemsToLoad += static_cast<int>(objInfo.costumes.size());
        }
    }
    EngineStdOut("Total image items to load: " + to_string(totalItemsToLoad), 0);

    if (totalItemsToLoad == 0) {
        EngineStdOut("No image items to load.", 0);
        return true;
    }

    int loadedCount = 0;
    int failedCount = 0;
    string imagePath = "";
    for (auto &objInfo: objects_in_order) {
        // objInfo를 참조로 받도록 수정
        if (objInfo.objectType == "sprite") {
            for (auto &costume: objInfo.costumes) {
                // costume을 참조로 받도록 수정
                if (IsSysMenu) {
                    imagePath = "sysmenu/" + costume.fileurl;
                } else {
                    imagePath = string(BASE_ASSETS) + costume.fileurl;
                }

                if (!this->renderer) {
                    EngineStdOut("CRITICAL: Renderer is NULL before image loading for " + imagePath, 2);
                    failedCount++; // 렌더러가 없으면 로드 실패로 간주
                    incrementLoadedItemCount();
                    continue; // 다음 코스튬으로
                }
                SDL_ClearError();

                // 1. IMG_Load를 사용하여 SDL_Surface로 로드
                SDL_Surface *tempSurface = IMG_Load(imagePath.c_str());

                if (tempSurface) {
                    costume.surfaceHandle = tempSurface; // 서피스 핸들 저장

                    // 2. SDL_Surface로부터 SDL_Texture 생성
                    costume.imageHandle = SDL_CreateTextureFromSurface(this->renderer, tempSurface);

                    if (costume.imageHandle) {
                        loadedCount++;
                        EngineStdOut(
                            "  Shape '" + costume.name + "' (" + imagePath +
                            ") loaded successfully. Surface and Texture created.",
                            3);
                        // SDL_Surface는 costume.surfaceHandle에 저장되어 있으므로 여기서 해제하지 않음
                        // 해제는 Costume 소멸 시 또는 이미지 재로드 시 수행
                    } else {
                        failedCount++;
                        EngineStdOut(
                            "SDL_CreateTextureFromSurface failed for '" + objInfo.name + "' shape '" + costume.name +
                            "': " +
                            SDL_GetError(),
                            2);
                        SDL_DestroySurface(tempSurface); // 텍스처 생성 실패 시 서피스 즉시 해제
                        costume.surfaceHandle = nullptr;
                    }
                } else {
                    failedCount++;
                    EngineStdOut(
                        "IMG_Load failed for '" + objInfo.name + "' shape '" + costume.name + "': " +
                        SDL_GetError(),
                        2);
                    costume.surfaceHandle = nullptr; // 로드 실패 시 null로 설정
                }

                incrementLoadedItemCount();

                if (loadedItemCount % 5 == 0 || loadedItemCount == totalItemsToLoad || !costume.imageHandle) {
                    renderLoadingScreen();
                    SDL_Event e;
                    while (SDL_PollEvent(&e)) {
                        if (e.type == SDL_EVENT_QUIT) {
                            EngineStdOut("Image loading cancelled by user.", 1);
                            return false;
                        }
                    }
                }
            }
        }
    }

    EngineStdOut("Image loading finished. Success: " + to_string(loadedCount) + ", Failed: " + to_string(failedCount),
                 0);
    chrono::duration<double> loadingDuration = chrono::duration_cast<chrono::duration<double> >(
        chrono::steady_clock::now() - startTime);
    string greething = "";
    double duration = loadingDuration.count();

    if (duration < 1.0) {
        greething = "WoW Excellent!";
    } else if (duration < 10.0) {
        greething = "Umm ok.";
    } else {
        greething = "You to Slow.";
    }
    EngineStdOut("Time to load entire image " + to_string(loadingDuration.count()) + " seconds " + greething);
    if (failedCount > 0 && loadedCount == 0 && totalItemsToLoad > 0) {
        EngineStdOut("All images failed to load. This may cause issues.", 2);
        showMessageBox("Fatal No images could be loaded. Check asset paths and file integrity.",
                       msgBoxIconType.ICON_ERROR);
    } else if (failedCount > 0) {
        EngineStdOut("Some images failed to load, processing with available resources.", 1);
    }
    return true;
}

bool Engine::loadSounds() {
    LOADING_METHOD_NAME = "Loading Sounds...";
    chrono::time_point<chrono::steady_clock> startTime = chrono::steady_clock::now();
    EngineStdOut("Starting Sound loading...", 0);

    // 1. Calculate total sound items to load for the progress bar
    int numSoundsToAttemptPreload = 0;
    for (const auto &objInfo: objects_in_order) {
        for (const auto &sf: objInfo.sounds) {
            if (!sf.fileurl.empty()) {
                numSoundsToAttemptPreload++;
            }
        }
    }

    this->totalItemsToLoad = numSoundsToAttemptPreload; // Set total items for this loading phase
    this->loadedItemCount = 0; // Reset loaded items count

    EngineStdOut("Total sound items to preload: " + to_string(this->totalItemsToLoad), 0);

    if (this->totalItemsToLoad == 0) {
        EngineStdOut("No sound items to preload.", 0);
        return true;
    }

    int preloadedSuccessfullyCount = 0; // To count actual successful preloads if needed, though aeHelper logs this.
    // For now, 'pl' or 'loadedItemCount' will represent processed items.

    for (const auto &objInfo: objects_in_order) {
        for (const auto &sf: objInfo.sounds) {
            if (!sf.fileurl.empty()) {
                string fullAudioPath = "";
                if (IsSysMenu) {
                    fullAudioPath = "sysmenu/" + sf.fileurl;
                } else {
                    fullAudioPath = string(BASE_ASSETS) + sf.fileurl;
                }
                // In case IsSysMenu affects sound paths, similar logic to loadImages could be added here.
                aeHelper.preloadSound(fullAudioPath);
                preloadedSuccessfullyCount++; // Increment if preload was attempted/successful

                this->loadedItemCount++; // Increment for progress bar

                // Update loading screen periodically and check for quit event
                if (this->loadedItemCount % 5 == 0 || this->loadedItemCount == this->totalItemsToLoad) {
                    renderLoadingScreen();
                    SDL_Event e;
                    while (SDL_PollEvent(&e) != 0) {
                        if (e.type == SDL_EVENT_QUIT) {
                            EngineStdOut("Sound loading cancelled by user.", 1);
                            return false; // Allow cancellation
                        }
                    }
                }
            }
        }
    }

    chrono::duration<double> loadingDuration = chrono::duration_cast<chrono::duration<double> >(
        chrono::steady_clock::now() - startTime);
    EngineStdOut(
        "Finished preloading " + to_string(preloadedSuccessfullyCount) + " sound assets. Time taken: " +
        to_string(loadingDuration.count()) + " seconds.",
        0);
    return true;
}

bool Engine::recreateAssetsIfNeeded() {
    if (!m_needsTextureRecreation) {
        return true;
    }

    EngineStdOut("Recreating GPU assets due to device reset...", 0); // 장치 리셋으로 인한 GPU 에셋 재생성

    if (!createTemporaryScreen()) {
        EngineStdOut("Failed to recreate temporary screen texture after device reset.", 2); // 임시 화면 텍스처 재생성 실패
        return false;
    }

    if (!loadImages()) {
        EngineStdOut("Failed to reload images after device reset.", 2); // 이미지 리로드 실패
        return false;
    }

    m_needsTextureRecreation = false; // 텍스처 재생성 필요 플래그 리셋
    EngineStdOut("GPU assets recreated successfully.", 0);
    return true;
}

void Engine::drawAllEntities() {
    getProjectTimerValue();
    if (!renderer || !tempScreenTexture) {
        EngineStdOut("drawAllEntities: Renderer or temporary screen texture not available.", 1);
        // 렌더러 또는 임시 화면 텍스처 사용 불가
        return;
    }

    // Lock the mutex that protects objects_in_order and entities
    std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex);

    SDL_SetRenderTarget(renderer, tempScreenTexture);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // 배경색 흰색으로 설정
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    for (int i = static_cast<int>(objects_in_order.size()) - 1; i >= 0; --i) {
        const ObjectInfo &objInfo = objects_in_order[i];
        // 현재 씬에 속하거나 전역 오브젝트인 경우에만 그림
        bool isInCurrentScene = (objInfo.sceneId == currentSceneId);
        bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty());

        if (!isInCurrentScene && !isGlobal) {
            continue;
        }

        auto it_entity = entities.find(objInfo.id);
        if (it_entity == entities.end()) {
            // 해당 ID의 엔티티가 없으면 건너뜀

            continue;
        }
        Entity *entityPtr = it_entity->second.get();

        if (!entityPtr->isVisible()) {
            // 엔티티가 보이지 않으면 건너뜀
            continue;
        }

        if (objInfo.objectType == "sprite") {
            // 스프라이트 타입 오브젝트 그리기

            const Costume *selectedCostume = nullptr;

            for (const auto &costume_ref: objInfo.costumes) {
                if (costume_ref.id == objInfo.selectedCostumeId) {
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
                if (!selectedCostume->imageHandle) {
                    EngineStdOut(
                        "Texture handle is null for costume '" + selectedCostume->name + "' of object '" + objInfo.name
                        + "'. Cannot get texture size.", 2); // 텍스처 핸들 null 오류
                    continue;
                }

                if (SDL_GetTextureSize(selectedCostume->imageHandle, &texW, &texH) != true) {
                    const char *sdlErrorChars = SDL_GetError();
                    string errorDetail = "No specific SDL error message available.";
                    if (sdlErrorChars && sdlErrorChars[0] != '\0') {
                        errorDetail = string(sdlErrorChars);
                    }

                    ostringstream oss;
                    oss << selectedCostume->imageHandle;
                    string texturePtrStr = oss.str(); // 텍스처 포인터 주소 로깅
                    EngineStdOut(
                        "Failed to get texture size for costume '" + selectedCostume->name + "' of object '" + objInfo.
                        name + "'. Texture Ptr: " + texturePtrStr + ". SDL_" + errorDetail, 2);
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

                if (abs(hue_effect_dgress) > 0.01) {
                    colorModApplied = true;
                    SDL_Color hue_tint_color = hueToRGB(hue_effect_dgress);

                    r_final_mod = static_cast<Uint8>(clamp(hue_tint_color.r * brightness_factor, 0.0f, 255.0f));
                    g_final_mod = static_cast<Uint8>(clamp(hue_tint_color.g * brightness_factor, 0.0f, 255.0f));
                    b_final_mod = static_cast<Uint8>(clamp(hue_tint_color.b * brightness_factor, 0.0f, 255.0f));
                } else if (abs(brightness_effect) > 0.01) {
                    colorModApplied = true;
                    r_final_mod = static_cast<Uint8>(std::clamp(255.0f * brightness_factor, 0.0f, 255.0f));
                    g_final_mod = static_cast<Uint8>(std::clamp(255.0f * brightness_factor, 0.0f, 255.0f));
                    b_final_mod = static_cast<Uint8>(std::clamp(255.0f * brightness_factor, 0.0f, 255.0f));
                }
                // Engine.cpp - Engine::drawAllEntities() 내 스프라이트 렌더링 로직

                // ... (기존 코드) ...

                if (colorModApplied) {
                    SDL_SetTextureColorMod(selectedCostume->imageHandle, r_final_mod, g_final_mod, b_final_mod);
                }

                double alpha_effect = entityPtr->getEffectAlpha();
                if (abs(alpha_effect - 1.0) > 0.01) {
                    // 알파 값이 1.0 (불투명)이 아닐 때만 적용
                    alphaModApplied = true;
                    Uint8 alpha_sdl_mod = static_cast<Uint8>(std::clamp(alpha_effect * 255.0, 0.0, 255.0));
                    SDL_SetTextureAlphaMod(selectedCostume->imageHandle, alpha_sdl_mod);
                }

                SDL_RenderTextureRotated(renderer, selectedCostume->imageHandle, nullptr, &dstRect, sdlAngle, &center,
                                         SDL_FLIP_NONE);

                // 렌더링 후 원래 상태로 복원
                if (colorModApplied) {
                    SDL_SetTextureColorMod(selectedCostume->imageHandle, 255, 255, 255); // 기본 색상으로 복원
                }
                if (alphaModApplied) {
                    SDL_SetTextureAlphaMod(selectedCostume->imageHandle, 255); // 기본 알파(불투명)로 복원
                }
            }
        } else if (objInfo.objectType == "textBox") {
            // 텍스트 상자 타입 오브젝트 그리기
            if (!objInfo.textContent.empty()) {
                string fontString = objInfo.fontName;

                string determinedFontPath;
                string fontfamily = objInfo.fontName;
                string fontAsset = string(FONT_ASSETS);
                int fontSize = objInfo.fontSize;
                FontName fontLoadEnum = getFontNameFromString(fontfamily);
                TTF_Font *Usefont = nullptr;
                int currentFontSize = objInfo.fontSize;
                switch (fontLoadEnum) {
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
                if (!determinedFontPath.empty()) {
                    Usefont = getFont(determinedFontPath, fontSize); // 캐시된 폰트 사용
                    if (!Usefont) {
                        EngineStdOut(
                            "Failed to load font: " + determinedFontPath + " at size " + to_string(currentFontSize) +
                            " for textBox '" + objInfo.name + "'. Falling back to HUD font.",
                            2);
                        Usefont = hudFont;
                    }
                } else {
                    Usefont = hudFont; // 폰트 로드 실패 시 HUD 기본 폰트 사용
                }
                if (Usefont) {
                    int style = TTF_STYLE_NORMAL;
                    if (objInfo.Bold) {
                        style |= TTF_STYLE_BOLD;
                    }
                    if (objInfo.Italic) {
                        style |= TTF_STYLE_ITALIC;
                    }
                    if (objInfo.Underline) {
                        style |= TTF_STYLE_UNDERLINE;
                    }
                    if (objInfo.Strike) {
                        style |= TTF_STYLE_STRIKETHROUGH;
                    }
                    TTF_SetFontStyle(Usefont, style);
                }
                SDL_Surface *textSurface = nullptr;
                if (objInfo.lineBreak) {
                    // 1. 줄 바꿈 너비 계산 (디자인 시점 너비 사용)
                    double designContainerWidth = PROJECT_STAGE_WIDTH; // 기본값
                    if (objInfo.entity.contains("width") && objInfo.entity["width"].is_number()) {
                        designContainerWidth = objInfo.entity["width"].get<double>();
                    } else {
                        EngineStdOut(
                            "Warning: textBox '" + objInfo.name +
                            "' missing 'entity.width'. Using stage width for wrapping.", 1);
                    }
                    auto wrapLengthPixels = static_cast<int>(round(designContainerWidth));
                    if (wrapLengthPixels == 0) {
                        wrapLengthPixels = 1; // 유효하지 않은 값일 경우 대체
                    }

                    textSurface = TTF_RenderText_Blended_Wrapped(Usefont, objInfo.textContent.c_str(),
                                                                 objInfo.textContent.length(),
                                                                 objInfo.textColor, wrapLengthPixels);
                    entityPtr->setWidth(textSurface->w);
                    entityPtr->setHeight(textSurface->h);
                } else {
                    textSurface = TTF_RenderText_Blended(Usefont, objInfo.textContent.c_str(),
                                                         objInfo.textContent.size(), objInfo.textColor);
                }
                TTF_SetFontStyle(Usefont,TTF_STYLE_NORMAL);
                if (entityPtr) {
                    updateEntityTextContent(entityPtr->getId(), objInfo.textContent);
                }

                if (textSurface) {
                    // 텍스트 표면 렌더링 성공

                    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                    if (textTexture) {
                        double entryX = entityPtr->getX();
                        double entryY = entityPtr->getY();
                        float sdlX = static_cast<float>(entryX + PROJECT_STAGE_WIDTH / 2.0);
                        float sdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 2.0 - entryY);

                        float textWidth = static_cast<float>(textSurface->w);
                        float textHeight = static_cast<float>(textSurface->h);
                        float scaledWidth = textWidth * entityPtr->getScaleX();
                        float scaledHeight = textHeight * entityPtr->getScaleY();
                        SDL_FRect dstRect;

                        // 글상자 배경 그리기
                        SDL_FRect bgRect = {
                            sdlX - scaledWidth / 2.0f, sdlY - scaledHeight / 2.0f, scaledWidth, scaledHeight
                        };
                        if (objInfo.objectType == "textBox") {
                            // 배경색은 글상자 타입에만 적용
                            SDL_SetRenderDrawColor(renderer, objInfo.textBoxBackgroundColor.r,
                                                   objInfo.textBoxBackgroundColor.g, objInfo.textBoxBackgroundColor.b,
                                                   objInfo.textBoxBackgroundColor.a);
                            SDL_RenderFillRect(renderer, &bgRect);
                        }


                        dstRect.w = scaledWidth;
                        dstRect.h = scaledHeight; // 텍스트 정렬 처리
                        // showMessageBox("textAlign:"+to_string(objInfo.textAlign),msgBoxIconType.ICON_INFORMATION);
                        switch (objInfo.textAlign) {
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
                    } else {
                        EngineStdOut(
                            "Failed to create text texture for textBox '" + objInfo.name + "'. SDL_" + SDL_GetError(),
                            2); // 텍스트 텍스처 생성 실패
                    }

                    SDL_DestroySurface(textSurface);
                } else {
                    // 텍스트 표면 렌더링 실패
                    EngineStdOut("Failed to render text surface for textBox '" + objInfo.name, 2);
                }
            }
        }
    }
    // Draw dialogs onto the tempScreenTexture after entities
    // The m_engineDataMutex is already held from the start of drawAllEntities
    drawDialogs();
    SDL_SetRenderTarget(renderer, nullptr);
    // 화면 지우기 (검은색)
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    // 윈도우 렌더링 크기 가져오기
    int windowRenderW = 0, windowRenderH = 0;
    SDL_GetRenderOutputSize(renderer, &windowRenderW, &windowRenderH);

    if (windowRenderW <= 0 || windowRenderH <= 0) {
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

    if (windowAspectRatio >= stageContentAspectRatio) {
        // 윈도우가 스테이지보다 넓거나 같은 비율: 높이 기준, 너비 조정 (레터박스 좌우)
        finalDisplayDstRect.h = static_cast<float>(windowRenderH);
        finalDisplayDstRect.w = finalDisplayDstRect.h * stageContentAspectRatio;
        finalDisplayDstRect.x = (static_cast<float>(windowRenderW) - finalDisplayDstRect.w) / 2.0f;
        finalDisplayDstRect.y = 0.0f;
    } else {
        // 윈도우가 스테이지보다 좁은 비율: 너비 기준, 높이 조정 (레터박스 상하)
        finalDisplayDstRect.w = static_cast<float>(windowRenderW);
        finalDisplayDstRect.h = finalDisplayDstRect.w / stageContentAspectRatio;
        finalDisplayDstRect.x = 0.0f;
        finalDisplayDstRect.y = (static_cast<float>(windowRenderH) - finalDisplayDstRect.h) / 2.0f;
    }

    SDL_RenderTexture(renderer, tempScreenTexture, &currentSrcFRect, &finalDisplayDstRect);
}

void Engine::drawHUD() {
    if (!this->renderer) {
        // 렌더러 사용 불가
        EngineStdOut("drawHUD: Renderer not available.", 1);
        return;
    }

    int windowW = 0, windowH = 0;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH);
    if (this->hudFont && this->specialConfig.showFPS) {
        string fpsText = "FPS: " + to_string(static_cast<int>(currentFps));
        SDL_Color textColor = {255, 150, 0, 255}; // 주황색

        SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, fpsText.c_str(), 0, textColor);
        if (textSurface) {
            // 배경 사각형 설정
            float bgPadding = 5.0f; // 텍스트 주변 여백
            SDL_FRect bgRect = {
                10.0f - bgPadding, // FPS 텍스트 x 위치에서 여백만큼 왼쪽으로
                10.0f - bgPadding, // FPS 텍스트 y 위치에서 여백만큼 위로
                static_cast<float>(textSurface->w) + 2 * bgPadding, // 텍스트 너비 + 양쪽 여백
                static_cast<float>(textSurface->h) + 2 * bgPadding // 텍스트 높이 + 양쪽 여백
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
            if (textTexture) {
                SDL_FRect dstRect = {
                    10.0f, 10.0f, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)
                };
                SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);
                SDL_DestroyTexture(textTexture);
            } else {
                EngineStdOut("Failed to create FPS text texture: " + string(SDL_GetError()), 2); // FPS 텍스트 텍스처 생성 실패
            }
            TTF_SetFontStyle(hudFont, original_style); // 원래 폰트 스타일로 복원
            SDL_DestroySurface(textSurface);
        } else {
            EngineStdOut("Failed to render FPS text surface ", 2); // FPS 텍스트 표면 렌더링 실패
        }
    }
    // 대답 입력
    if (m_textInputActive) {
        // 텍스트 입력 모드가 활성화된 경우
        std::lock_guard<std::mutex> lock(m_textInputMutex);
        // m_textInputQuestionMessage, m_currentTextInputBuffer 접근 보호

        // 화면 하단 등에 질문 메시지와 입력 필드 UI를 그립니다.
        // 예시:
        // 1. 질문 메시지 렌더링 (m_textInputQuestionMessage 사용)
        // 2. 입력 필드 배경 렌더링
        // 3. 현재 입력된 텍스트 렌더링 (m_currentTextInputBuffer 사용)
        // 4. 체크버튼

        if (hudFont) {
            SDL_Color textColor = {0, 0, 0, 255}; // 검정
            SDL_Color bgColor = {255, 255, 255, 255}; // 흰색

            // 질문 메시지
            if (!m_textInputQuestionMessage.empty()) {
                SDL_Surface *questionSurface = TTF_RenderText_Blended_Wrapped(
                    hudFont, m_textInputQuestionMessage.c_str(), m_textInputQuestionMessage.size(), textColor,
                    WINDOW_WIDTH - 40);
                if (questionSurface) {
                    SDL_Texture *questionTexture = SDL_CreateTextureFromSurface(renderer, questionSurface);
                    SDL_FRect questionRect = {
                        20.0f, static_cast<float>(WINDOW_HEIGHT - 100 - questionSurface->h),
                        static_cast<float>(questionSurface->w), static_cast<float>(questionSurface->h)
                    };
                    SDL_RenderTexture(renderer, questionTexture, nullptr, &questionRect);
                    SDL_DestroyTexture(questionTexture);
                    SDL_DestroySurface(questionSurface);
                }
            } // 입력 필드 (더 짧게 수정)
            SDL_FRect inputBgRect = {
                20.0f, static_cast<float>(WINDOW_HEIGHT - 80), static_cast<float>(WINDOW_WIDTH - 120), 40.0f
            };
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
            SDL_RenderFillRect(renderer, &inputBgRect);
            SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255); // 테두리
            SDL_RenderRect(renderer, &inputBgRect);

            std::string displayText = m_currentTextInputBuffer;
            if (SDL_TextInputActive(window)) {
                // IME 사용 중이거나 텍스트 입력 중일 때 커서 표시
                // 간단한 커서 표시 (깜빡임은 추가 구현 필요)
                Uint64 currentTime = SDL_GetTicks();
                if (currentTime > m_cursorBlinkToggleTime + CURSOR_BLINK_INTERVAL_MS) {
                    m_cursorCharVisible = !m_cursorCharVisible;
                    m_cursorBlinkToggleTime = currentTime;
                }
                if (m_cursorCharVisible) {
                    displayText += "|";
                } else {
                    displayText += " ";
                }
            }

            if (!displayText.empty()) {
                SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, displayText.c_str(), displayText.size(),
                                                                  textColor);
                if (textSurface) {
                    SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                    // 입력 필드 내부에 텍스트 위치 조정
                    float textX = inputBgRect.x + 10;
                    float textY = inputBgRect.y + (inputBgRect.h - textSurface->h) / 2;
                    SDL_FRect textRect = {
                        textX, textY, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)
                    };

                    // 텍스트가 입력 필드를 넘어가지 않도록 클리핑
                    if (textRect.x + textRect.w > inputBgRect.x + inputBgRect.w - 10) {
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
            if (checkboxTexture) {
                // 체크박스의 크기와 위치 계산
                int checkboxSize = min(inputFiledRect.h, 40); // 크기 제한

                // 체크박스의 위치 계산 (입력창 우측에 배치)
                SDL_FRect checkboxDestRect;
                checkboxDestRect.x = inputFiledRect.x + inputFiledRect.w + 5; // 입력창과의 간격을 5로 축소
                checkboxDestRect.y = inputFiledRect.y; // 입력창과 동일한 y 좌표 (상단 정렬)
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
    if (this->specialConfig.showZoomSlider) {
        SDL_FRect sliderBgRect = {
            static_cast<float>(SLIDER_X), static_cast<float>(SLIDER_Y), static_cast<float>(SLIDER_WIDTH),
            static_cast<float>(SLIDER_HEIGHT)
        };
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); // 슬라이더 배경색
        SDL_RenderFillRect(renderer, &sliderBgRect);

        float handleX_float = SLIDER_X + ((zoomFactor - MIN_ZOOM) / (MAX_ZOOM - MIN_ZOOM)) * SLIDER_WIDTH;
        float handleWidth_float = 8.0f;

        SDL_FRect sliderHandleRect = {
            handleX_float - handleWidth_float / 2.0f, static_cast<float>(SLIDER_Y - 2), handleWidth_float,
            static_cast<float>(SLIDER_HEIGHT + 4)
        };
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer, &sliderHandleRect); // 슬라이더 핸들

        if (this->hudFont) {
            ostringstream zoomStream;
            zoomStream << fixed << setprecision(2) << zoomFactor;
            string zoomText = "Zoom: " + zoomStream.str();
            SDL_Color textColor = {220, 220, 220, 255}; // 줌 텍스트 색상

            SDL_Surface *textSurface = TTF_RenderText_Blended(hudFont, zoomText.c_str(), 0, textColor);
            if (textSurface) {
                SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                if (textTexture) {
                    SDL_FRect dstRect = {
                        SLIDER_X + SLIDER_WIDTH + 10.0f,
                        SLIDER_Y + (SLIDER_HEIGHT - static_cast<float>(textSurface->h)) / 2.0f,
                        static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)
                    };
                    SDL_RenderTexture(renderer, textTexture, nullptr, &dstRect);
                    SDL_DestroyTexture(textTexture);
                } else {
                    EngineStdOut("Failed to create Zoom text texture: " + string(SDL_GetError()), 2); // 줌 텍스트 텍스처 생성 실패
                }
                SDL_DestroySurface(textSurface);
            } else {
                EngineStdOut("Failed to render Zoom text surface ", 2); // 줌 텍스트 표면 렌더링 실패
            }
        }
    }

    // --- HUD 변수 그리기 (일반 변수 및 리스트) ---
    if (!m_HUDVariables.empty()) {
        lock_guard lock(m_engineDataMutex);
        int window_w, window_h;
        float maxObservedItemWidthThisFrame = 0.0f; // 각 프레임에서 관찰된 가장 넓은 아이템 너비
        int visibleVarsCount = 0; // 보이는 변수 개수
        if (renderer)
            SDL_GetRenderOutputSize(renderer, &window_w, &window_h);

        float screenCenterX = static_cast<float>(window_w) / 2.0f;
        float screenCenterY = static_cast<float>(window_h) / 2.0f;

        // float currentWidgetYPosition = m_variablesListWidgetY; // No longer used for individual items
        // float spacingBetweenBoxes = 2.0f; // No longer used for individual items
        for (auto &var: m_HUDVariables) // Use non-const auto& if var.width might be updated
        {
            if (!var.isVisible) {
                continue; // 변수가 보이지 않으면 건너뜁니다.
            }

            // 엔트리 좌표(var.x, var.y)를 스크린 렌더링 좌표로 변환
            float renderX = screenCenterX + var.x;
            float renderY = screenCenterY - var.y; // var.y는 요소의 상단 Y (엔트리 기준)

            // Colors and fixed dimensions for a single item box
            SDL_Color containerBgColor = {240, 240, 240, 220}; // 컨테이너 배경색 (약간 투명한 밝은 회색)
            SDL_Color containerBorderColor = {100, 100, 100, 255}; // 컨테이너 테두리 색상
            SDL_Color itemLabelTextColor = {0, 0, 0, 255}; // 변수 이름 텍스트 색상 (검정)
            SDL_Color itemValueTextColor = {255, 255, 255, 255}; // 변수 값 텍스트 색상
            float itemHeight = 22.0f; // 각 변수 항목의 높이
            float itemPadding = 3.0f; // 항목 내부 여백
            float containerCornerRadius = 5.0f;
            float containerBorderWidth = 1.0f;

            SDL_Color itemValueBoxBgColor;
            string valueToDisplay;
            var.isAnswerList = false;
            if (var.variableType == "timer") {
                itemValueBoxBgColor = {255, 150, 0, 255}; // 타이머는 주황색 배경
                valueToDisplay = to_string(static_cast<int>(getProjectTimerValue()));
            } else if (var.variableType == "list") {
                // ---------- LIST VARIABLE RENDERING ----------
                if (!hudFont) // HUD 폰트 없으면 리스트 렌더링 불가
                    continue;

                // List specific styling
                SDL_Color listBgColor = {240, 240, 240, 220}; // Dark semi-transparent background for the list
                SDL_Color listBorderColor = {150, 150, 150, 255}; // Light gray border
                SDL_Color listNameTextColor = {0, 0, 0, 255}; // Light text for list name
                SDL_Color listItemBgColor = {0, 120, 255, 255}; // Blue background for item data
                SDL_Color listItemTextColor = {255, 255, 255, 255}; // White text for item data
                SDL_Color listRowNumberColor = {10, 10, 10, 255}; // Light gray for row numbers

                float listCornerRadius = 5.0f;
                float listBorderWidth = 1.0f;
                float headerHeight = 25.0f; // Height for the list name header
                float itemRowHeight = 20.0f; // Height of each row in the list
                float contentPadding = 5.0f; // General padding inside the list container and items
                float rowNumberColumnWidth = 30.0f; // Width allocated for row numbers column (adjust as needed)
                float spacingBetweenRows = 2.0f; // Vertical spacing between list item rows
                float scrollbarWidth = 10.0f; // 스크롤바 너비
                bool needsScrollbar = false;
                var.isAnswerList = true;

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
                    max(0.0f, var.height - (2 * listBorderWidth))
                };
                float innerRadius = max(0.0f, listCornerRadius - listBorderWidth); // 내부 둥근 모서리 반지름
                if (listContainerInnerRect.w > 0 && listContainerInnerRect.h > 0) {
                    SDL_SetRenderDrawColor(renderer, listBgColor.r, listBgColor.g, listBgColor.b, listBgColor.a);
                    Helper_RenderFilledRoundedRect(renderer, &listContainerInnerRect, innerRadius);
                }

                // 3. 리스트 이름 (헤더) 그리기
                string listDisplayName;
                bool foundAssociatedObjectList = false;
                if (!var.objectId.empty()) {
                    const ObjectInfo *objInfoPtrList = getObjectInfoById(var.objectId);
                    if (objInfoPtrList) {
                        listDisplayName = objInfoPtrList->name + " : " + var.name;
                        foundAssociatedObjectList = true;
                    }
                }
                if (!foundAssociatedObjectList) {
                    listDisplayName = var.name;
                }

                SDL_Surface *nameSurfaceList = TTF_RenderText_Blended(hudFont, listDisplayName.c_str(), 0,
                                                                      listNameTextColor);
                if (nameSurfaceList) {
                    SDL_Texture *nameTextureList = SDL_CreateTextureFromSurface(renderer, nameSurfaceList);
                    if (nameTextureList) {
                        SDL_FRect nameDestRectList = {
                            listContainerInnerRect.x + contentPadding,
                            listContainerInnerRect.y + (headerHeight - static_cast<float>(nameSurfaceList->h)) / 2.0f,
                            min(static_cast<float>(nameSurfaceList->w), listContainerInnerRect.w - 2 * contentPadding),
                            static_cast<float>(nameSurfaceList->h)
                        };
                        SDL_RenderTexture(renderer, nameTextureList, nullptr, &nameDestRectList);
                        SDL_DestroyTexture(nameTextureList);
                    }
                    SDL_DestroySurface(nameSurfaceList);
                }

                // 4. 리스트 아이템 그리기
                float itemsAreaStartY = listContainerInnerRect.y + headerHeight;
                float itemsAreaRenderableHeight = listContainerInnerRect.h - headerHeight - contentPadding;
                float currentItemVisualY = itemsAreaStartY + contentPadding;

                // 리스트 아이템 전체 높이 계산
                var.calculatedContentHeight = 0.0f;
                if (!var.array.empty()) {
                    var.calculatedContentHeight =
                            (var.array.size() * (itemRowHeight + spacingBetweenRows)) - spacingBetweenRows + (
                                2 * contentPadding);
                }

                if (var.calculatedContentHeight > itemsAreaRenderableHeight) {
                    needsScrollbar = true;
                }

                // 컬럼 위치 계산 (행 번호 왼쪽, 데이터 오른쪽)
                // 수정: 행 번호 컬럼을 먼저 계산하고 왼쪽에 배치
                float rowNumColumnX = listContainerInnerRect.x + contentPadding;
                // 수정: 데이터 컬럼은 행 번호 컬럼 오른쪽에 위치
                float dataColumnAvailableWidth =
                        listContainerInnerRect.w - (2 * contentPadding) - rowNumberColumnWidth - contentPadding - (
                            needsScrollbar ? scrollbarWidth + contentPadding : 0.0f);
                float dataColumnX = rowNumColumnX + rowNumberColumnWidth + contentPadding;
                float dataColumnWidth = max(0.0f, dataColumnAvailableWidth);
                dataColumnWidth = max(0.0f, dataColumnWidth);

                for (size_t i = 0; i < var.array.size(); ++i) {
                    // 스크롤 오프셋 적용된 아이템 Y 위치
                    float itemRenderY = currentItemVisualY - var.scrollOffset_Y;

                    // 아이템이 보이는 영역 밖에 있으면 그리지 않음
                    if (itemRenderY + itemRowHeight < itemsAreaStartY || itemRenderY > itemsAreaStartY +
                        itemsAreaRenderableHeight) {
                        currentItemVisualY += itemRowHeight + spacingBetweenRows;
                        continue;
                    }

                    const ListItem &listItem = var.array[i];

                    // 컬럼 1: 행 번호 (왼쪽)
                    string rowNumStr = to_string(i + 1);
                    SDL_Surface *rowNumSurface = TTF_RenderText_Blended(hudFont, rowNumStr.c_str(), 0,
                                                                        listRowNumberColor);
                    if (rowNumSurface) {
                        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, rowNumSurface);
                        if (tex) {
                            SDL_FRect r = {
                                rowNumColumnX + (rowNumberColumnWidth - rowNumSurface->w) / 2.0f,
                                itemRenderY + (itemRowHeight - rowNumSurface->h) / 2.0f, // itemRenderY 사용
                                (float) rowNumSurface->w, (float) rowNumSurface->h
                            };
                            SDL_RenderTexture(renderer, tex, nullptr, &r);
                            SDL_DestroyTexture(tex);
                        }
                        SDL_DestroySurface(rowNumSurface);
                    }

                    // 컬럼 2: 아이템 데이터 (오른쪽 - 파란 배경, 흰색 텍스트)
                    SDL_FRect itemDataBgRect = {
                        dataColumnX, itemRenderY, dataColumnWidth, itemRowHeight
                    }; // itemRenderY 사용
                    SDL_SetRenderDrawColor(renderer, listItemBgColor.r, listItemBgColor.g, listItemBgColor.b,
                                           listItemBgColor.a);
                    SDL_RenderFillRect(renderer, &itemDataBgRect);

                    if (dataColumnWidth > contentPadding * 2) {
                        // 텍스트를 그릴 공간이 있을 때만
                        string textToRender = listItem.data;
                        string displayText = textToRender;
                        float availableTextWidthInDataCol = dataColumnWidth - (2 * contentPadding);

                        int fullTextMeasuredWidth;
                        size_t fullTextOriginalLengthInBytes = textToRender.length();
                        size_t fullTextMeasuredLengthInBytes; // max_width=0일 때 fullTextOriginalLengthInBytes와 같아야 함

                        if (TTF_MeasureString(hudFont, textToRender.c_str(), fullTextOriginalLengthInBytes,
                                              0 /* max_width = 0 이면 전체 문자열 측정 */, &fullTextMeasuredWidth,
                                              &fullTextMeasuredLengthInBytes)) {
                            if (static_cast<float>(fullTextMeasuredWidth) > availableTextWidthInDataCol) {
                                // 텍스트가 너무 길면 잘림 처리 (...)
                                // 잘림 처리 필요
                                const string ellipsis = "...";
                                int ellipsisMeasuredWidth;
                                size_t ellipsisOriginalLength = ellipsis.length();
                                size_t ellipsisMeasuredLength; // ellipsis.length()와 같아야 함

                                if (TTF_MeasureString(hudFont, ellipsis.c_str(), ellipsisOriginalLength, 0,
                                                      &ellipsisMeasuredWidth, &ellipsisMeasuredLength)) {
                                    float widthForTextItself =
                                            availableTextWidthInDataCol - static_cast<float>(ellipsisMeasuredWidth);

                                    if (widthForTextItself <= 0) {
                                        // 내용 + "..." 을 위한 공간 없음. "..." 만이라도 표시 가능한지 확인
                                        if (static_cast<float>(ellipsisMeasuredWidth) <= availableTextWidthInDataCol) {
                                            displayText = ellipsis;
                                        } else {
                                            // "..." 조차 표시할 공간 없음
                                            displayText = ""; // 또는 textToRender의 첫 글자 등 (UTF-8 고려 필요)
                                        }
                                    } else {
                                        // "..." 앞의 원본 텍스트가 들어갈 수 있는 부분 측정
                                        int fittingTextPortionWidth;
                                        size_t fittingTextPortionLengthInBytes;
                                        TTF_MeasureString(hudFont, textToRender.c_str(), fullTextOriginalLengthInBytes,
                                                          static_cast<int>(widthForTextItself),
                                                          &fittingTextPortionWidth, &fittingTextPortionLengthInBytes);

                                        if (fittingTextPortionLengthInBytes > 0) {
                                            displayText =
                                                    textToRender.substr(0, fittingTextPortionLengthInBytes) + ellipsis;
                                        } else {
                                            // 텍스트 부분이 전혀 안 들어감. "..." 만이라도 표시 가능한지 확인
                                            if (static_cast<float>(ellipsisMeasuredWidth) <=
                                                availableTextWidthInDataCol) {
                                                displayText = ellipsis;
                                            } else {
                                                displayText = "";
                                            }
                                        }
                                    }
                                } else {
                                    // "..." 측정 실패
                                    EngineStdOut("Failed to measure ellipsis text for HUD list.", 2);
                                    // 간단한 대체 처리
                                    if (textToRender.length() > 2)
                                        displayText = textToRender.substr(0, textToRender.length() - 2) + "..";
                                }
                            }
                            // else: 전체 텍스트가 공간에 맞으므로 displayText = textToRender (초기값) 사용
                        } else {
                            // textToRender 측정 실패
                            EngineStdOut("Failed to measure text: " + textToRender + " for HUD list.", 2);
                            // 오류 처리, displayText는 초기값 textToRender를 유지하거나 비워둘 수 있음
                        }

                        if (!displayText.empty()) {
                            SDL_Surface *itemTextSurface = TTF_RenderText_Blended(
                                hudFont, displayText.c_str(), 0, listItemTextColor);
                            if (itemTextSurface) {
                                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, itemTextSurface);
                                if (tex) {
                                    SDL_FRect r = {
                                        itemDataBgRect.x + contentPadding,
                                        itemDataBgRect.y + (itemDataBgRect.h - itemTextSurface->h) / 2.0f,
                                        (float) itemTextSurface->w, (float) itemTextSurface->h
                                    };
                                    SDL_RenderTexture(renderer, tex, nullptr, &r);
                                    SDL_DestroyTexture(tex);
                                }
                                SDL_DestroySurface(itemTextSurface);
                            }
                        }
                    }

                    currentItemVisualY += itemRowHeight + spacingBetweenRows;
                }

                // 4.5 스크롤바 그리기 (필요한 경우)
                if (needsScrollbar) {
                    SDL_Color scrollbarTrackColor = {200, 200, 200, 150}; // 연한 회색 트랙
                    SDL_Color scrollbarHandleColor = {80, 80, 80, 200}; // 어두운 회색 핸들

                    float scrollbarTrackX = listContainerInnerRect.x + listContainerInnerRect.w - contentPadding -
                                            scrollbarWidth;
                    SDL_FRect scrollbarTrackRect = {
                        scrollbarTrackX,
                        itemsAreaStartY, // 헤더 아래부터 시작
                        scrollbarWidth,
                        itemsAreaRenderableHeight // 아이템 영역 높이만큼
                    };
                    SDL_SetRenderDrawColor(renderer, scrollbarTrackColor.r, scrollbarTrackColor.g,
                                           scrollbarTrackColor.b, scrollbarTrackColor.a);
                    SDL_RenderFillRect(renderer, &scrollbarTrackRect);

                    float handleHeightRatio = itemsAreaRenderableHeight / var.calculatedContentHeight;
                    float scrollbarHandleHeight = max(10.0f, itemsAreaRenderableHeight * handleHeightRatio); // 최소 핸들 높이

                    float scrollPositionRatio =
                            var.scrollOffset_Y / (var.calculatedContentHeight - itemsAreaRenderableHeight);
                    scrollPositionRatio = clamp(scrollPositionRatio, 0.0f, 1.0f);
                    float scrollbarHandleY = itemsAreaStartY + scrollPositionRatio * (
                                                 itemsAreaRenderableHeight - scrollbarHandleHeight);

                    SDL_FRect scrollbarHandleRect = {
                        scrollbarTrackX, scrollbarHandleY, scrollbarWidth, scrollbarHandleHeight
                    };
                    SDL_SetRenderDrawColor(renderer, scrollbarHandleColor.r, scrollbarHandleColor.g,
                                           scrollbarHandleColor.b, scrollbarHandleColor.a);
                    SDL_RenderFillRect(renderer, &scrollbarHandleRect);
                }

                // 5. 리스트 크기 조절 핸들 그리기 (오른쪽 하단)
                if (var.width >= MIN_LIST_WIDTH && var.height >= MIN_LIST_HEIGHT) {
                    // 핸들을 그릴 충분한 공간이 있는지 확인
                    SDL_FRect resizeHandleRect = {
                        renderX + var.width - LIST_RESIZE_HANDLE_SIZE, // renderX 기준
                        renderY + var.height - LIST_RESIZE_HANDLE_SIZE, // renderY 기준
                        LIST_RESIZE_HANDLE_SIZE,
                        LIST_RESIZE_HANDLE_SIZE
                    };
                    SDL_SetRenderDrawColor(renderer, listRowNumberColor.r, listRowNumberColor.g, listRowNumberColor.b,
                                           255); // 핸들 색상
                    SDL_RenderFillRect(renderer, &resizeHandleRect);
                }
                var.transient_render_width = var.width; // 리스트의 경우, 렌더링된 너비는 정의된 너비와 동일
            } else {
                // 일반 변수
                itemValueBoxBgColor = {0, 120, 255, 255}; // 다른 변수는 파란색 배경
                valueToDisplay = var.value;
            }

            // 변수 이름 레이블
            // 변수의 object 키에 오브젝트 ID가 있을 경우 해당 오브젝트 이름을 가져온다.
            // 없을경우 그냥 변수 이름을 사용한다.
            string nameToDisplay; // HUD에 최종적으로 표시될 변수의 이름
            bool foundAssociatedObject = false;

            if (!var.objectId.empty()) {
                const ObjectInfo *objInfoPtr = getObjectInfoById(var.objectId);
                if (objInfoPtr) {
                    nameToDisplay = objInfoPtr->name + " : " + var.name;
                    foundAssociatedObject = true;
                }
            }

            if (!foundAssociatedObject) {
                nameToDisplay = var.name; // 변수 자체의 이름을 표시할 이름으로 사용합니다.
            }

            // 디버그: "메시지3" 변수의 이름과 값을 로그로 출력
            if (var.name == "메시지3") {
                EngineStdOut(
                    "DEBUG: Rendering variable '메시지3'. nameToDisplay: [" + nameToDisplay + "], valueToDisplay: [" +
                    valueToDisplay + "]", 3);
            }

            SDL_Surface *nameSurface = TTF_RenderText_Blended(hudFont, nameToDisplay.c_str(), 0, itemLabelTextColor);
            SDL_Surface *valueSurface = TTF_RenderText_Blended(hudFont, valueToDisplay.c_str(), 0, itemValueTextColor);

            if (!nameSurface || !valueSurface) {
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
            float maxAvailContainerWidth = static_cast<float>(window_w) - var.x - 10.0f; // 창 오른쪽 가장자리에서 10px 여유 확보
            maxAvailContainerWidth = max(minContainerFixedWidth, maxAvailContainerWidth); // 최대 너비는 최소 너비보다 작을 수 없음

            // 이 항목의 최종 컨테이너 너비
            float currentItemContainerWidth = clamp(idealContainerFixedWidth, minContainerFixedWidth,
                                                    maxAvailContainerWidth);
            if (var.variableType == "list" && var.width > 0) {
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
            if (containerBorderWidth > 0.0f) {
                SDL_SetRenderDrawColor(renderer, containerBorderColor.r, containerBorderColor.g, containerBorderColor.b,
                                       containerBorderColor.a);
                Helper_RenderFilledRoundedRect(renderer, &outerContainerRect, containerCornerRadius);
            }
            // 2. 컨테이너 배경 그리기 (테두리 안쪽, currentItemContainerWidth 사용)
            SDL_FRect fillContainerRect = {
                renderX + containerBorderWidth, // 테두리 두께만큼 안쪽으로
                renderY + containerBorderWidth,
                max(0.0f, currentItemContainerWidth - (2 * containerBorderWidth)),
                max(0.0f, singleBoxHeight - (2 * containerBorderWidth))
            };
            float fillRadius = max(0.0f, containerCornerRadius - containerBorderWidth);
            if (fillContainerRect.w > 0 && fillContainerRect.h > 0) {
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

            if (nameTexture && valueTexture) {
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

                if (totalIdealInternalWidth <= spaceForNameTextAndValueBox) {
                    // 이상적인 너비로 둘 다 그릴 충분한 공간이 있음
                    finalNameTextWidth = targetNameTextWidth;
                    finalValueBoxWidth = targetValueBoxWidth;
                } else {
                    // 공간 부족, spaceForNameTextAndValueBox에 맞게 비례적으로 축소
                    if (totalIdealInternalWidth > 0) {
                        float scaleFactor = spaceForNameTextAndValueBox / totalIdealInternalWidth;
                        finalNameTextWidth = targetNameTextWidth * scaleFactor;
                        finalValueBoxWidth = targetValueBoxWidth * scaleFactor;
                    } else {
                        // 두 대상 너비가 모두 0인 경우
                        finalNameTextWidth = 0;
                        finalValueBoxWidth = spaceForNameTextAndValueBox; // Or distribute 0/0 or space/2, space/2
                        if (spaceForNameTextAndValueBox > 0 && targetNameTextWidth == 0 && targetValueBoxWidth == 0) {
                            // 공간은 있지만 내용이 없는 경우
                            finalNameTextWidth = spaceForNameTextAndValueBox / 2.0f; // Arbitrary split
                            finalValueBoxWidth = spaceForNameTextAndValueBox / 2.0f;
                        } else {
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
                    static_cast<float>(nameTexture->h)
                };
                SDL_FRect nameSrcRect = {0, 0, static_cast<int>(finalNameTextWidth), static_cast<int>(nameTexture->h)};
                SDL_RenderTexture(renderer, nameTexture, &nameSrcRect, &nameDestRect);

                // 값 배경 상자 및 값 텍스트 그리기
                if (finalValueBoxWidth > 0) {
                    SDL_FRect valueBgRect = {
                        nameDestRect.x + finalNameTextWidth + itemPadding,
                        contentAreaTopY,
                        finalValueBoxWidth,
                        itemHeight
                    };
                    SDL_SetRenderDrawColor(renderer, itemValueBoxBgColor.r, itemValueBoxBgColor.g,
                                           itemValueBoxBgColor.b, itemValueBoxBgColor.a);
                    SDL_RenderFillRect(renderer, &valueBgRect);

                    // Value text display width is capped by the blue box's inner width
                    float valueTextDisplayWidth = max(
                        0.0f, min(valueTextActualWidth, finalValueBoxWidth - (2 * itemPadding)));
                    if (valueTextDisplayWidth > 0) {
                        SDL_FRect valueDestRect = {
                            valueBgRect.x + itemPadding, // 파란색 상자 내에서 왼쪽 정렬
                            valueBgRect.y + (valueBgRect.h - static_cast<float>(valueTexture->h)) / 2.0f,
                            valueTextDisplayWidth,
                            static_cast<float>(valueTexture->h)
                        };
                        SDL_FRect valueSrcRect = {
                            0, 0, static_cast<int>(valueTextDisplayWidth), static_cast<int>(valueTexture->h)
                        };
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

        if (visibleVarsCount > 0) {
            // maxObservedItemWidthThisFrame은 minContainerFixedWidth(80.0f) 이상이어야 합니다.
            // currentItemContainerWidth가 그렇게 제한(clamp)되기 때문입니다.
            m_maxVariablesListContentWidth = maxObservedItemWidthThisFrame;
        } else {
            m_maxVariablesListContentWidth = 180.0f; // 보이는 항목이 없으면 기본 너비
        }
    } else {
        // 목록이 아예 표시되지 않거나 비어있으면 기본 너비
        m_maxVariablesListContentWidth = 180.0f;
    }

    if (m_showScriptDebugger) {
        drawScriptDebuggerUI();
    }
}

bool Engine::mapWindowToStageCoordinates(int windowMouseX, int windowMouseY, float &stageX, float &stageY) const {
    int windowRenderW = 0, windowRenderH = 0;
    if (this->renderer) {
        SDL_GetRenderOutputSize(this->renderer, &windowRenderW, &windowRenderH);
    } else // 렌더러 사용 불가
    {
        EngineStdOut("mapWindowToStageCoordinates: Renderer not available.", 2);
        return false;
    }

    if (windowRenderW <= 0 || windowRenderH <= 0) {
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

    if (windowAspectRatio >= stageContentAspectRatio) {
        // 윈도우가 스테이지보다 넓거나 같은 비율
        finalDisplayDstRect.h = static_cast<float>(windowRenderH);
        finalDisplayDstRect.w = finalDisplayDstRect.h * stageContentAspectRatio;
        finalDisplayDstRect.x = (static_cast<float>(windowRenderW) - finalDisplayDstRect.w) / 2.0f;
        finalDisplayDstRect.y = 0.0f;
    } else {
        finalDisplayDstRect.w = static_cast<float>(windowRenderW); // 윈도우가 스테이지보다 좁은 비율
        finalDisplayDstRect.h = finalDisplayDstRect.w / stageContentAspectRatio;
        finalDisplayDstRect.x = 0.0f;
        finalDisplayDstRect.y = (static_cast<float>(windowRenderH) - finalDisplayDstRect.h) / 2.0f;
    }

    if (finalDisplayDstRect.w <= 0.0f || finalDisplayDstRect.h <= 0.0f) {
        EngineStdOut("mapWindowToStageCoordinates: Calculated final display rect has zero or negative dimension.",
                     2); // 계산된 표시 영역 크기 오류
        return false;
    }

    if (static_cast<float>(windowMouseX) < finalDisplayDstRect.x || static_cast<float>(windowMouseX) >=
        finalDisplayDstRect.x + finalDisplayDstRect.w ||
        static_cast<float>(windowMouseY) < finalDisplayDstRect.y || static_cast<float>(windowMouseY) >=
        finalDisplayDstRect.y + finalDisplayDstRect.h) {
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

void Engine::processInput(const SDL_Event &event, float deltaTime) {
    // 디버거 열기
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F12) {
        m_showScriptDebugger = !m_showScriptDebugger;
    } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_HOME) {
        if (m_showScriptDebugger) {
            m_debuggerScrollOffsetY = 0.0f;
        }
    } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F5) {
        if (showMessageBox("재시작 하시겠습니까?", msgBoxIconType.ICON_INFORMATION, true) == true) {
            requestProjectRestart();
            performProjectRestart();
        }
    }
    // 마우스 휠 이벤트 처리 (디버그)
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        if (m_showScriptDebugger) {
            float mouse_x_float, mouse_y_float;
            SDL_GetMouseState(&mouse_x_float, &mouse_y_float);


            int windowW_render = 0, windowH_render = 0;
            if (renderer) {
                // 렌더러 유효성 확인
                SDL_GetRenderOutputSize(renderer, &windowW_render, &windowH_render);
            }

            SDL_FRect debuggerPanelRect = {
                10.0f, 50.0f, static_cast<float>(windowW_render) - 20.0f, static_cast<float>(windowH_render) - 60.0f
            };

            // 마우스가 디버거 패널 위에 있을 때만 스크롤 작동
            if (mouse_x_float >= debuggerPanelRect.x && mouse_x_float <= debuggerPanelRect.x + debuggerPanelRect.w &&
                mouse_y_float >= debuggerPanelRect.y && mouse_y_float <= debuggerPanelRect.y + debuggerPanelRect.h) {
                const float scrollSpeed = 50.0f; // 스크롤 속도 (조정 가능)
                m_debuggerScrollOffsetY -= static_cast<float>(event.wheel.y) * scrollSpeed; // event.wheel.y 사용
                // m_debuggerScrollOffsetY의 최소값은 0으로 제한 (최대값은 drawScriptDebuggerUI에서 계산 후 제한)
                if (m_debuggerScrollOffsetY < 0.0f) {
                    m_debuggerScrollOffsetY = 0.0f;
                }
            }
        }
    }
    // 키보드 텍스트 입력 처리
    if (m_textInputActive) {
        // 텍스트 입력 모드가 활성화된 경우
        if (event.type == SDL_EVENT_TEXT_INPUT) {
            std::lock_guard<std::mutex> lock(m_textInputMutex);
            if (event.text.text != nullptr) {
                m_currentTextInputBuffer += event.text.text;
            }
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            std::lock_guard<std::mutex> lock(m_textInputMutex);
            if (event.key.scancode == SDL_SCANCODE_BACKSPACE && !m_currentTextInputBuffer.empty()) {
                m_currentTextInputBuffer.pop_back();
            } else if (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER) {
                // 현재 입력된 텍스트를 대답으로 저장 (비어있어도 저장)
                m_lastAnswer = m_currentTextInputBuffer;
                m_currentTextInputBuffer.clear();
                m_textInputActive = false;

                // 질문 다이얼로그 제거
                Entity *entity = getEntityById_nolock(m_textInputRequesterObjectId);
                if (entity) {
                    entity->removeDialog();
                }

                // 즉시 대답 변수 업데이트
                updateAnswerVariable();

                m_textInputCv.notify_all(); // 대기 중인 스크립트 스레드 깨우기
                EngineStdOut("Enter pressed. Input complete. Answer: " + m_lastAnswer, 0);
            }
            return; // 키 입력만 리턴하고 마우스 이벤트는 계속 처리
        }
    }
    // --- General Key State Update (Happens regardless of m_gameplayInputActive for isKeyPressed) ---
    if (event.type == SDL_EVENT_KEY_DOWN) {
        std::lock_guard<std::mutex> lock(m_pressedKeysMutex);
        m_pressedKeys.insert(event.key.scancode);
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        // 마우스 버튼 누름 이벤트
        // 엔트리는 마우스 버튼 구별하지않음
        if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
            bool uiClicked = false; // UI 요소 클릭 여부
            int mouseX = event.button.x;
            int mouseY = event.button.y;
            if (this->specialConfig.showZoomSlider && // 줌 슬라이더 클릭 확인
                mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH &&
                mouseY >= SLIDER_Y - 5 && mouseY <= SLIDER_Y + SLIDER_HEIGHT + 5) {
                float ratio = static_cast<float>(mouseX - SLIDER_X) / SLIDER_WIDTH;
                this->zoomFactor = MIN_ZOOM + ratio * (MAX_ZOOM - MIN_ZOOM);
                this->zoomFactor = max(MIN_ZOOM, min(MAX_ZOOM, this->zoomFactor));
                this->m_isDraggingZoomSlider = true;
                uiClicked = true;
            } // 체크 버튼 클릭 확인 (텍스트 입력 활성화 상태일 때만)
            EngineStdOut("텍스트 입력 상태: " + std::to_string(m_textInputActive), 3);
            bool isCheckboxClick = false;
            if (!uiClicked && m_textInputActive) {
                EngineStdOut("체크박스 클릭 체크 시작", 0);
                // 실제 윈도우 렌더링 크기 가져오기
                int windowRenderW = 0, windowRenderH = 0;
                SDL_GetRenderOutputSize(renderer, &windowRenderW, &windowRenderH);

                // drawHUD에서 정의된 입력 필드 및 체크박스 크기/위치와 일치해야 함
                SDL_FRect inputBgRect = {
                    20.0f, static_cast<float>(windowRenderH - 80), static_cast<float>(windowRenderW - 120), 40.0f
                };
                int checkboxSize = min(static_cast<int>(inputBgRect.h), 40);
                SDL_FRect checkboxDestRect;
                checkboxDestRect.x = inputBgRect.x + inputBgRect.w + 5;
                checkboxDestRect.y = inputBgRect.y;
                checkboxDestRect.w = static_cast<float>(checkboxSize);
                checkboxDestRect.h = static_cast<float>(checkboxSize); // 디버깅을 위한 좌표 정보 출력
                EngineStdOut("Mouse click at: (" + std::to_string(mouseX) + ", " + std::to_string(mouseY) + ")", 3);
                EngineStdOut("Checkbox area: x=" + std::to_string(checkboxDestRect.x) +
                             ", y=" + std::to_string(checkboxDestRect.y) +
                             ", w=" + std::to_string(checkboxDestRect.w) +
                             ", h=" + std::to_string(checkboxDestRect.h),
                             3);
                EngineStdOut("텍스트 입력 박스: x=" + std::to_string(inputBgRect.x) +
                             ", y=" + std::to_string(inputBgRect.y) +
                             ", w=" + std::to_string(inputBgRect.w) +
                             ", h=" + std::to_string(inputBgRect.h),
                             3);
                bool isInCheckbox = static_cast<float>(mouseX) >= checkboxDestRect.x &&
                                    static_cast<float>(mouseX) <= checkboxDestRect.x + checkboxDestRect.w &&
                                    static_cast<float>(mouseY) >= checkboxDestRect.y &&
                                    static_cast<float>(mouseY) <= checkboxDestRect.y + checkboxDestRect.h;

                EngineStdOut("체크박스 클릭 검사: " + std::to_string(isInCheckbox), 0);
                if (isInCheckbox) {
                    std::lock_guard<std::mutex> lock(m_textInputMutex);
                    m_lastAnswer = m_currentTextInputBuffer; // 현재 입력된 텍스트를 대답으로 저장
                    updateAnswerVariable(); // 대답 변수 업데이트
                    m_currentTextInputBuffer.clear(); // 버퍼 초기화
                    m_textInputActive = false; // 입력 완료, 플래그 해제

                    Entity *entity = getEntityById_nolock(m_textInputRequesterObjectId);
                    if (entity) {
                        entity->removeDialog();
                    }
                    m_textInputCv.notify_all(); // 대기 중인 스크립트 스레드 깨우기
                    EngineStdOut("Checkbox clicked. Input complete. Answer: " + m_lastAnswer, 0);
                    uiClicked = true; // UI 요소 클릭으로 처리
                }
            }
            // 개별 HUD 변수 드래그 확인
            if (!uiClicked && !m_HUDVariables.empty()) {
                float itemHeight = 22.0f;
                float itemPadding = 3.0f;
                float singleBoxHeight = itemHeight + 2 * itemPadding;
                float containerBorderWidth = 1.0f; // drawHUD와 일치
                float minContainerFixedWidth = 80.0f; // Matches drawHUD
                int windowW_render = 0, windowH_render = 0;
                if (renderer) {
                    SDL_GetRenderOutputSize(renderer, &windowW_render, &windowH_render);
                }
                float screenCenterX = static_cast<float>(windowW_render) / 2.0f;
                float screenCenterY = static_cast<float>(windowH_render) / 2.0f;

                for (int i = 0; i < m_HUDVariables.size(); ++i) {
                    const auto &var_item = m_HUDVariables[i];
                    if (!var_item.isVisible)
                        continue;

                    SDL_FRect varRect;
                    float itemActualWidthForHitTest;
                    float itemActualHeightForHitTest; // 충돌 검사를 위한 실제 아이템 너비/높이

                    // 엔트리 좌표를 스크린 좌표로 변환
                    float itemScreenX = screenCenterX + var_item.x;
                    float itemScreenY = screenCenterY - var_item.y;

                    if (var_item.variableType == "list" && var_item.width > 0) {
                        itemActualWidthForHitTest = var_item.width;
                        itemActualHeightForHitTest = var_item.height;
                        varRect = {itemScreenX, itemScreenY, itemActualWidthForHitTest, itemActualHeightForHitTest};

                        // 리스트의 리사이즈 핸들 클릭 확인
                        SDL_FRect resizeHandleRect = {
                            itemScreenX + var_item.width - LIST_RESIZE_HANDLE_SIZE,
                            itemScreenY + var_item.height - LIST_RESIZE_HANDLE_SIZE,
                            LIST_RESIZE_HANDLE_SIZE,
                            LIST_RESIZE_HANDLE_SIZE
                        };

                        if (static_cast<float>(mouseX) >= resizeHandleRect.x && static_cast<float>(mouseX) <=
                            resizeHandleRect.x + resizeHandleRect.w &&
                            static_cast<float>(mouseY) >= resizeHandleRect.y && static_cast<float>(mouseY) <=
                            resizeHandleRect.y + resizeHandleRect.h) {
                            m_draggedHUDVariableIndex = i;
                            m_currentHUDDragState = HUDDragState::RESIZING;
                            // 오프셋: 마우스 스크린 위치 - (아이템 스크린 우하단 모서리)
                            m_draggedHUDVariableMouseOffsetX =
                                    static_cast<float>(mouseX) - (itemScreenX + var_item.width);
                            m_draggedHUDVariableMouseOffsetY =
                                    static_cast<float>(mouseY) - (itemScreenY + var_item.height);
                            uiClicked = true;
                            EngineStdOut("Started resizing HUD list: " + var_item.name, 3); // LEVEL 0 -> 3
                            break;
                        }
                    } else {
                        // For other types, calculate width similar to drawHUD
                        // For non-list items, use transient_render_width if available and seems reasonable.
                        itemActualWidthForHitTest = var_item.transient_render_width;
                        if (itemActualWidthForHitTest <= 0) {
                            // 아직 렌더링되지 않았다면 대체값 사용
                            itemActualWidthForHitTest = minContainerFixedWidth;
                        }
                        itemActualHeightForHitTest = singleBoxHeight;
                        varRect = {itemScreenX, itemScreenY, itemActualWidthForHitTest, itemActualHeightForHitTest};
                    }

                    // 일반 이동을 위한 클릭 확인 (리사이즈 핸들이 아닐 경우)
                    if (!uiClicked && // 리사이즈 핸들이 이미 클릭되지 않았는지 확인
                        static_cast<float>(mouseX) >= varRect.x && static_cast<float>(mouseX) <= varRect.x + varRect.w
                        &&
                        static_cast<float>(mouseY) >= varRect.y && static_cast<float>(mouseY) <= varRect.y + varRect.
                        h) {
                        m_draggedHUDVariableIndex = i;
                        m_currentHUDDragState = HUDDragState::MOVING;
                        m_draggedHUDVariableMouseOffsetX = static_cast<float>(mouseX) - itemScreenX;
                        // 마우스 스크린 위치 - 아이템 스크린 좌상단 X
                        m_draggedHUDVariableMouseOffsetY = static_cast<float>(mouseY) - itemScreenY;
                        // 마우스 스크린 위치 - 아이템 스크린 좌상단 Y
                        uiClicked = true;
                        EngineStdOut(
                            "Started dragging HUD variable: " + var_item.name + " (Type: " + var_item.variableType +
                            ")", 3); // LEVEL 0 -> 3
                        break;
                    }
                }
            }
            if (!uiClicked && m_gameplayInputActive) {
                // UI 클릭이 아니고 게임플레이 입력이 활성화된 경우
                this->setStageClickedThisFrame(true);
                float stageMouseX = 0.0f, stageMouseY = 0.0f;
                if (mapWindowToStageCoordinates(mouseX, mouseY, stageMouseX, stageMouseY)) // 윈도우 좌표를 스테이지 좌표로 변환
                {
                    EngineStdOut("Click at stage coordinates: (" + std::to_string(stageMouseX) + ", " +
                                 std::to_string(stageMouseY) + ")",
                                 3);

                    // objects_in_order[0]이 가장 위에 그려지므로, 0번 인덱스부터 순회하여 가장 위에 있는 엔티티를 먼저 확인합니다.
                    for (size_t i = 0; i < objects_in_order.size(); ++i) {
                        const ObjectInfo &objInfo = objects_in_order[i];
                        const string &objectId = objInfo.id;

                        bool isInCurrentScene = (objInfo.sceneId == currentSceneId); // 현재 씬에 있는지 확인
                        bool isGlobal = (objInfo.sceneId == "global" || objInfo.sceneId.empty());
                        if (!isInCurrentScene && !isGlobal) {
                            continue;
                        }

                        Entity *entity = getEntityById(objectId);
                        // 엔티티 유효성, 가시성, 투명도 체크 강화
                        if (!entity || !entity->isVisible()) {
                            continue;
                        }
                        EngineStdOut("Checking click for entity: " + objectId + " at stage pos (" +
                                     std::to_string(stageMouseX) + ", " + std::to_string(stageMouseY) + ")",
                                     3);
                        if (entity->isPointInside(stageMouseX, stageMouseY)) {
                            EngineStdOut("Hit detected on entity: " + objectId, 3);
                            // 마우스가 엔티티 내부에 있으면
                            m_pressedObjectId = objectId;

                            // 클릭된 오브젝트의 모든 관련 스크립트 실행                            // 클릭된 엔티티에 대한 모든 스크립트를 비동기적으로 실행
                            std::vector<std::pair<std::string, const Script *> > scriptsToRun;

                            // "when clicked" 스크립트 수집
                            for (const auto &clickScriptPair: m_whenObjectClickedScripts) {
                                if (clickScriptPair.first == objectId) {
                                    scriptsToRun.emplace_back(objectId, clickScriptPair.second);
                                }
                            }

                            // "mouse clicked" 스크립트 수집
                            for (const auto &scriptPair: m_mouseClickedScripts) {
                                if (scriptPair.first == objectId) {
                                    scriptsToRun.emplace_back(objectId, scriptPair.second);
                                }
                            }

                            // 수집된 모든 스크립트를 비동기적으로 실행
                            std::string currentScene = getCurrentSceneId();
                            for (const auto &scriptPair: scriptsToRun) {
                                this->dispatchScriptForExecution(
                                    scriptPair.first,
                                    scriptPair.second,
                                    currentScene,
                                    deltaTime);
                            }

                            EngineStdOut("Click handled by entity: " + objectId, 0);
                            return; // 클릭 처리 완료 후 다음 엔티티 처리 방지
                        }
                    }
                } else {
                    EngineStdOut("Warning: Could not map window to stage coordinates for object click.", 1);
                    // 오브젝트 클릭을 위한 좌표 변환 실패
                }
            } else if (uiClicked) {
                m_pressedObjectId = "";
            }
            // 스크롤바 핸들 드래그 시작 확인 (다른 HUD 요소 드래그 확인 후, uiClicked가 false일 때)
            if (!uiClicked && m_currentHUDDragState == HUDDragState::NONE) {
                // uiClicked가 false이고, 다른 드래그 상태가 아닐 때만
                // mouseX, mouseY는 이미 위에서 event.button.x/y로 설정됨
                int windowW_render = 0, windowH_render = 0;
                if (renderer)
                    SDL_GetRenderOutputSize(renderer, &windowW_render, &windowH_render);
                float screenCenterX = static_cast<float>(windowW_render) / 2.0f;
                float screenCenterY = static_cast<float>(windowH_render) / 2.0f;

                for (int i = static_cast<int>(m_HUDVariables.size()) - 1; i >= 0; --i) {
                    // 역순으로 순회 (위에 있는 UI 우선)
                    HUDVariableDisplay &var = m_HUDVariables[i]; // 참조로 가져와야 scrollOffset_Y 등 수정 가능
                    if (var.isVisible && var.variableType == "list") {
                        // 엔트리 좌표를 스크린 좌표로 변환
                        float itemScreenX = screenCenterX + var.x;
                        float itemScreenY = screenCenterY - var.y;

                        float headerHeight = 30.0f; // drawHUD와 일치
                        float contentPadding = 5.0f; // drawHUD와 일치
                        float scrollbarWidth = 10.0f; // drawHUD와 일치

                        // listContainerInnerRect 계산 (스크린 좌표 기준)
                        SDL_FRect listContainerInnerRect = {
                            itemScreenX + contentPadding, itemScreenY + contentPadding,
                            var.width - (2 * contentPadding), var.height - (2 * contentPadding)
                        };
                        float itemsAreaStartY = listContainerInnerRect.y + headerHeight;
                        float itemsAreaRenderableHeight = listContainerInnerRect.h - headerHeight - contentPadding;

                        if (var.calculatedContentHeight > itemsAreaRenderableHeight) {
                            // 스크롤바가 있는 경우
                            float scrollbarTrackX =
                                    listContainerInnerRect.x + listContainerInnerRect.w - contentPadding -
                                    scrollbarWidth;
                            float handleHeightRatio = itemsAreaRenderableHeight / var.calculatedContentHeight;
                            float scrollbarHandleHeight = max(10.0f, itemsAreaRenderableHeight * handleHeightRatio);
                            float scrollPositionRatio = (var.calculatedContentHeight - itemsAreaRenderableHeight == 0)
                                                            ? 0.0f
                                                            : (var.scrollOffset_Y / (
                                                                   var.calculatedContentHeight -
                                                                   itemsAreaRenderableHeight));
                            scrollPositionRatio = std::clamp(scrollPositionRatio, 0.0f, 1.0f);
                            float scrollbarHandleY =
                                    itemsAreaStartY + scrollPositionRatio * (
                                        itemsAreaRenderableHeight - scrollbarHandleHeight);
                            SDL_FRect scrollbarHandleRect = {
                                scrollbarTrackX, scrollbarHandleY, scrollbarWidth, scrollbarHandleHeight
                            };

                            if (static_cast<float>(mouseX) >= scrollbarHandleRect.x && static_cast<float>(mouseX) <=
                                scrollbarHandleRect.x + scrollbarHandleRect.w &&
                                static_cast<float>(mouseY) >= scrollbarHandleRect.y && static_cast<float>(mouseY) <=
                                scrollbarHandleRect.y + scrollbarHandleRect.h) {
                                m_currentHUDDragState = HUDDragState::SCROLLING_LIST_HANDLE;
                                m_draggedScrollbarListIndex = i;
                                m_scrollbarDragStartY = static_cast<float>(mouseY);
                                m_scrollbarDragInitialOffset = var.scrollOffset_Y;
                                uiClicked = true; // UI 요소 클릭으로 처리
                                EngineStdOut("Started dragging list scrollbar: " + var.name, 3);
                                break;
                            }
                        }
                    }
                }
            }
        }
    } else if (event.type == SDL_EVENT_KEY_DOWN) {
        // This part is for triggering "when_some_key_pressed" scripts.
        // The actual state of m_pressedKeys is updated above, outside the m_gameplayInputActive check.
        // std::lock_guard<std::mutex> lock(m_pressedKeysMutex); // Lock for m_pressedKeys
        // m_pressedKeys.insert(event.key.scancode);

        if (m_gameplayInputActive) // 키 누름 이벤트
        {
            SDL_Scancode scancode = event.key.scancode;
            auto it = keyPressedScripts.find(scancode);
            if (it != keyPressedScripts.end()) {
                const auto &scriptsToRun = it->second;
                for (const auto &scriptPair: scriptsToRun) {
                    const string &objectId = scriptPair.first;
                    const Script *scriptPtr = scriptPair.second;
                    EngineStdOut(" -> Dispatching 'Key Pressed' script for object: " + objectId + " (Key: " +
                                 SDL_GetScancodeName(scancode) + ")",
                                 3); // LEVEL 0 -> 3
                    this->dispatchScriptForExecution(objectId, scriptPtr, getCurrentSceneId(), deltaTime);
                }
            }
        }
    } else if (event.type == SDL_EVENT_KEY_UP) {
        std::lock_guard<std::mutex> lock(m_pressedKeysMutex);
        m_pressedKeys.erase(event.key.scancode);
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (this->m_isDraggingZoomSlider && (event.motion.state & SDL_BUTTON_LMASK)) // 줌 슬라이더 드래그 중
        {
            int mouseX = event.motion.x;
            // 슬라이더 범위 내에서 줌 계수 업데이트
            if (mouseX >= SLIDER_X && mouseX <= SLIDER_X + SLIDER_WIDTH) {
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
            if (renderer) {
                SDL_GetRenderOutputSize(renderer, &windowW, &windowH);
            }
            float screenCenterX = static_cast<float>(windowW) / 2.0f;
            float screenCenterY = static_cast<float>(windowH) / 2.0f;

            if (m_currentHUDDragState == HUDDragState::MOVING) {
                // 이동 상태
                // 새 스크린 좌표 계산
                float newScreenX = static_cast<float>(mouseX) - m_draggedHUDVariableMouseOffsetX;
                float newScreenY = static_cast<float>(mouseY) - m_draggedHUDVariableMouseOffsetY;

                // 아이템 크기 가져오기 (클램핑용)
                float draggedItemWidth = 0.0f;
                float draggedItemHeight = 0.0f;

                if (draggedVar.variableType == "list") {
                    draggedItemWidth = draggedVar.width; // 리스트 너비
                    draggedItemHeight = draggedVar.height; // 리스트 높이
                } else {
                    draggedItemWidth = (draggedVar.transient_render_width > 0)
                                           ? draggedVar.transient_render_width
                                           : 180.0f; // 렌더링된 너비 또는 기본값
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
                if (windowW > 0 && draggedItemWidth > 0) {
                    // 창 경계 내로 위치 제한
                    clampedNewScreenX = clamp(newScreenX, 0.0f, static_cast<float>(windowW) - draggedItemWidth);
                }
                if (windowH > 0 && draggedItemHeight > 0) {
                    clampedNewScreenY = clamp(newScreenY, 0.0f, static_cast<float>(windowH) - draggedItemHeight);
                }

                // 클램핑된 스크린 좌표를 엔트리 좌표로 변환하여 저장
                draggedVar.x = clampedNewScreenX - screenCenterX;
                draggedVar.y = screenCenterY - clampedNewScreenY;
            } else if (m_currentHUDDragState == HUDDragState::RESIZING && draggedVar.variableType == "list") {
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
                if (windowW > 0) {
                    draggedVar.width = min(draggedVar.width, static_cast<float>(windowW) - itemScreenX);
                }
                // 높이는 (창 높이 - 아이템의 스크린 Y 시작 위치)를 넘을 수 없음
                if (windowH > 0) {
                    draggedVar.height = min(draggedVar.height, static_cast<float>(windowH) - itemScreenY);
                }
            } else if (m_currentHUDDragState == HUDDragState::SCROLLING_LIST_HANDLE && m_draggedScrollbarListIndex != -
                       1) {
                HUDVariableDisplay &var = m_HUDVariables[m_draggedScrollbarListIndex];
                float dy = static_cast<float>(mouseY) - m_scrollbarDragStartY;

                // 필요한 값들 (drawHUD와 일치)
                float headerHeight = 30.0f;
                float contentPadding = 5.0f;
                // scrollbarWidth는 여기서는 직접 사용되지 않음

                // var.x, var.y는 엔트리 좌표. 스크린 좌표계의 listContainerInnerRect 계산 필요 없음.
                // var.width, var.height는 HUDVariableDisplay에 저장된 리스트의 크기.
                float itemsAreaRenderableHeight = var.height - (2 * contentPadding) - headerHeight - contentPadding;

                if (var.calculatedContentHeight <= itemsAreaRenderableHeight) {
                    // 스크롤 불필요
                    var.scrollOffset_Y = 0.0f;
                } else {
                    float totalScrollableContentHeight = var.calculatedContentHeight - itemsAreaRenderableHeight;
                    if (totalScrollableContentHeight <= 0.0f) {
                        // 이론상 발생 안 함
                        var.scrollOffset_Y = 0.0f;
                    } else {
                        float scrollbarTrackHeight = itemsAreaRenderableHeight; // 스크롤바 트랙의 실제 높이
                        float handleHeightRatio = itemsAreaRenderableHeight / var.calculatedContentHeight;
                        float scrollbarHandleHeight = max(10.0f, scrollbarTrackHeight * handleHeightRatio);
                        float scrollbarEffectiveTrackHeight = scrollbarTrackHeight - scrollbarHandleHeight;

                        if (scrollbarEffectiveTrackHeight <= 0.0f) {
                            // 핸들이 트랙보다 크거나 같음
                            var.scrollOffset_Y = 0.0f;
                        } else {
                            // 마우스 이동량(dy)을 스크롤 가능한 트랙 높이 비율로 변환하고,
                            // 이를 전체 스크롤 가능한 콘텐츠 높이에 곱하여 실제 스크롤 오프셋 변경량 계산
                            float scrollOffsetChange =
                                    (dy / scrollbarEffectiveTrackHeight) * totalScrollableContentHeight;
                            var.scrollOffset_Y = m_scrollbarDragInitialOffset + scrollOffsetChange;
                            // 스크롤 오프셋을 0과 최대 스크롤 가능 범위 사이로 제한
                            var.scrollOffset_Y = std::clamp(var.scrollOffset_Y, 0.0f, totalScrollableContentHeight);
                        }
                    }
                }
            }
        }
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        //엔트리 는 마우스 좌/우 클릭 구별 안함
        if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) // 마우스 왼쪽 버튼 뗌 이벤트
        {
            bool uiInteractionReleased = false; // UI 상호작용 (드래그/리사이즈) 해제 여부
            if (this->m_isDraggingZoomSlider) {
                this->m_isDraggingZoomSlider = false;
                uiInteractionReleased = true;
            }
            if (m_draggedHUDVariableIndex != -1) {
                if (m_currentHUDDragState == HUDDragState::MOVING) {
                    EngineStdOut("Stopped dragging HUD variable: " + m_HUDVariables[m_draggedHUDVariableIndex].name,
                                 3); // LEVEL 0 -> 3
                } else if (m_currentHUDDragState == HUDDragState::RESIZING) {
                    EngineStdOut("Stopped resizing HUD list: " + m_HUDVariables[m_draggedHUDVariableIndex].name,
                                 3); // LEVEL 0 -> 3
                }
                m_draggedHUDVariableIndex = -1;
                m_currentHUDDragState = HUDDragState::NONE;
                uiInteractionReleased = true;
            } else if (m_currentHUDDragState == HUDDragState::SCROLLING_LIST_HANDLE) // 스크롤바 핸들 드래그 종료
            {
                m_currentHUDDragState = HUDDragState::NONE;
                m_draggedScrollbarListIndex = -1;
                uiInteractionReleased = true; // UI 상호작용으로 처리
            }
            if (m_gameplayInputActive) {
                this->setStageClickedThisFrame(false);
            }
            if (m_gameplayInputActive && !uiInteractionReleased) {
                // 게임플레이 입력 활성화 상태이고 UI 상호작용이 해제되지 않은 경우

                if (!m_mouseClickCanceledScripts.empty()) {
                    for (const auto &scriptPair: m_mouseClickCanceledScripts) {
                        const string &objectId = scriptPair.first;
                        const Script *scriptPtr = scriptPair.second;
                        Entity *currentEntity = getEntityById(objectId);
                        if (currentEntity && currentEntity->isVisible() == true) {
                            // 엔티티가 존재하고 보이는 경우
                            // 현재 씬에 속하거나 전역 오브젝트인지 확인
                            const ObjectInfo *objInfoPtr = getObjectInfoById(objectId); // ObjectInfo 가져오기
                            if (objInfoPtr) {
                                std::string sceneContext = getCurrentSceneId(); // 현재 씬 컨텍스트 가져오기
                                bool isInCurrentScene = (objInfoPtr->sceneId == currentSceneId);
                                bool isGlobal = (objInfoPtr->sceneId == "global" || objInfoPtr->sceneId.empty());
                                if (isInCurrentScene || isGlobal) {
                                    this->dispatchScriptForExecution(objectId, scriptPtr, sceneContext, deltaTime);
                                }
                            } else {
                                EngineStdOut(
                                    "Warning: ObjectInfo not found for entity ID '" + objectId +
                                    "' during mouse_click_canceled event processing. Script not run.",
                                    1);
                            }
                        } else if (!currentEntity) {
                            // currentEntity가 null이면 아무것도 할 수 없음. 이 경우는 로직 오류일 수 있음.
                            EngineStdOut(
                                "Warning: mouse_click_canceled event for null entity ID '" + objectId +
                                "'. Script not run.",
                                1);
                        }
                    }
                }
                // 눌렸던 오브젝트에 대한 클릭 취소 스크립트 실행
                if (!m_pressedObjectId.empty()) {
                    const string &canceledObjectId = m_pressedObjectId;
                    for (const auto &scriptPair: m_whenObjectClickCanceledScripts) {
                        std::string sceneContext = getCurrentSceneId(); // 현재 씬 컨텍스트 가져오기
                        if (scriptPair.first == canceledObjectId) {
                            EngineStdOut("Dispatching 'when_object_click_canceled' for object: " + canceledObjectId,
                                         3); // LEVEL 0 -> 3
                            this->dispatchScriptForExecution(canceledObjectId, scriptPair.second, sceneContext,
                                                             deltaTime);
                        }
                    }
                }
                m_pressedObjectId = ""; // 눌린 오브젝트 ID 초기화
            }
        }
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        // 마우스 휠 이벤트는 m_gameplayInputActive와 관계없이 처리 (HUD 스크롤용)
        float mouseX_wheel, mouseY_wheel;
        SDL_GetMouseState(&mouseX_wheel, &mouseY_wheel); // 현재 마우스 위치 가져오기

        // 화면 중앙 기준 좌표계 변환 (drawHUD와 일치시키기 위함)
        int windowW_render = 0, windowH_render = 0;
        if (renderer)
            SDL_GetRenderOutputSize(renderer, &windowW_render, &windowH_render);
        float screenCenterX = static_cast<float>(windowW_render) / 2.0f;
        float screenCenterY = static_cast<float>(windowH_render) / 2.0f;

        for (size_t i = 0; i < m_HUDVariables.size(); ++i) {
            HUDVariableDisplay &var = m_HUDVariables[i]; // 값 변경을 위해 참조 사용
            if (var.variableType == "list" && var.isVisible) {
                // HUD 변수의 화면상 사각형 계산 (엔트리 좌표를 스크린 좌표로 변환)
                float varScreenX = screenCenterX + var.x;
                float varScreenY = screenCenterY - var.y; // Y축 반전
                SDL_FRect listRect = {varScreenX, varScreenY, var.width, var.height};

                // 마우스가 해당 리스트 위에 있는지 확인
                if (static_cast<float>(mouseX_wheel) >= listRect.x && static_cast<float>(mouseX_wheel) <= listRect.x +
                    listRect.w &&
                    static_cast<float>(mouseY_wheel) >= listRect.y && static_cast<float>(mouseY_wheel) <= listRect.y +
                    listRect.h) {
                    // 스크롤 가능한 높이 계산 (drawHUD와 동일한 로직)
                    float headerHeight = 30.0f;
                    float contentPadding = 5.0f;
                    float itemsAreaRenderableHeight = var.height - (2 * contentPadding) - headerHeight - contentPadding;

                    if (var.calculatedContentHeight > itemsAreaRenderableHeight) {
                        // 스크롤이 필요한 경우
                        float maxScrollOffset = var.calculatedContentHeight - itemsAreaRenderableHeight;
                        // event.wheel.y > 0 이면 위로 스크롤 (내용이 아래로), < 0 이면 아래로 스크롤 (내용이 위로)
                        // scrollOffset_Y는 내용이 위로 올라간 정도를 나타내므로, 위로 스크롤 시 감소해야 함.
                        var.scrollOffset_Y -= static_cast<float>(event.wheel.y) * MOUSE_WHEEL_SCROLL_SPEED;
                        var.scrollOffset_Y = std::clamp(var.scrollOffset_Y, 0.0f, maxScrollOffset);
                        break; // 가장 위에 있는 리스트만 스크롤
                    }
                }
            }
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
double Engine::getAngle(double x1, double y1, double x2, double y2) const {
    // PI_VALUE -> SDL_PI_D
    double deltaX = x2 - x1;
    double deltaY = y2 - y1; // Y축이 위로 향하는 좌표계이므로 그대로 사용

    double angleRad = atan2(deltaY, deltaX); // 표준 수학 각도 (라디안, 0도는 오른쪽)
    double angleDegMath = angleRad * 180.0 / SDL_PI_D; // 표준 수학 각도 (도)

    // EntryJS/Scratch 각도로 변환 (0도는 위쪽, 90도는 오른쪽)
    double angleDegEntry = 90.0 - angleDegMath;

    // 0-360 범위로 정규화
    angleDegEntry = fmod(angleDegEntry, 360.0);
    if (angleDegEntry < 0) {
        angleDegEntry += 360.0;
    }
    return angleDegEntry;
}

const ObjectInfo *Engine::getObjectInfoById(const string &id) const {
    // Ensure thread-safe access if objects_in_order can be modified concurrently.
    // If only read during typical gameplay after loading, mutex might not be strictly needed here
    // but consider if project reloading or dynamic object addition/removal happens.
    // For simplicity and safety, if there's any doubt, use a mutex.
    // std::lock_guard<std::mutex> lock(m_engineDataMutex); // If m_engineDataMutex protects objects_in_order
    for (const auto &objInfo: objects_in_order) {
        if (objInfo.id == id) {
            return &objInfo;
        }
    }
    return nullptr; // Not found
}

/**
 * @brief Calculates the angle in degrees from a given point (entityX, entityY) to the current stage mouse position.
 * The angle is compatible with EntryJS/Scratch coordinate system (0 degrees is up, 90 degrees is right).
 * @param entityX The X coordinate of the entity (origin point).
 * @param entityY The Y coordinate of the entity (origin point).
 * @return The angle in degrees (0-360). Returns 0 if the mouse is not on stage.
 */
double Engine::getCurrentStageMouseAngle(double entityX, double entityY) const {
    if (!m_isMouseOnStage) {
        // Or return a specific value indicating mouse is not on stage,
        // but current usage in BlockExecutor already checks m_isMouseOnStage.
        // Returning 0 or current entity rotation might be alternatives if needed elsewhere.
        return 0.0; // Default if mouse is not on stage
    }

    double deltaX = m_currentStageMouseX - entityX;
    double deltaY = m_currentStageMouseY - entityY; // Y-up system, so this is correct

    double angleRad = atan2(deltaY, deltaX); // Correct
    double angleDegMath = angleRad * 180.0 / SDL_PI_D; // 0 is right, 90 is up
    double angleDegEntry = 90.0 - angleDegMath; // Convert to 0 is up, 90 is right

    // Normalize to 0-360 range
    angleDegEntry = fmod(angleDegEntry, 360.0);
    if (angleDegEntry < 0) {
        angleDegEntry += 360.0;
    }
    return angleDegEntry;
}

void Engine::setVisibleHUDVariables(const vector<HUDVariableDisplay> &variables) {
    m_HUDVariables = variables;
    // EngineStdOut("HUD variables updated. Count: " + to_string(m_visibleHUDVariables.size()), 0);
}

SDL_Scancode Engine::mapStringToSDLScancode(const string &keyIdentifier) const {
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
        {"222", SDL_SCANCODE_APOSTROPHE}
    };

    auto it = jsKeyCodeMap.find(keyIdentifier);
    if (it != jsKeyCodeMap.end()) {
        return it->second;
    }

    SDL_Scancode sc = SDL_GetScancodeFromName(keyIdentifier.c_str()); // 이름으로 Scancode 검색
    if (sc != SDL_SCANCODE_UNKNOWN) {
        return sc;
    }
    // 단일 소문자 알파벳인 경우 대문자로 변환하여 다시 시도
    if (keyIdentifier.length() == 1) {
        char c = keyIdentifier[0];
        if (c >= 'a' && c <= 'z') {
            char upper_c = static_cast<char>(toupper(c));
            string upper_s(1, upper_c);
            return SDL_GetScancodeFromName(upper_s.c_str());
        }
    }

    return SDL_SCANCODE_UNKNOWN;
}

void Engine::runStartButtonScripts() {
    if (startButtonScripts.empty()) // 시작 버튼 클릭 스크립트가 없으면
    {
        IsScriptStart = false;
        EngineStdOut("No 'Start Button Clicked' scripts found to run.", 1);
        return;
    } else {
        IsScriptStart = true;
        EngineStdOut("Running 'Start Button Clicked' scripts...", 0);
    }

    for (const auto &scriptPair: startButtonScripts) {
        const string &objectId = scriptPair.first;
        const Script *scriptPtr = scriptPair.second;
        EngineStdOut(" -> Running script for object: " + objectId, 3);
        std::string sceneContext = getCurrentSceneId(); // 현재 씬 컨텍스트 가져오기
        this->dispatchScriptForExecution(objectId, scriptPtr, sceneContext, 0.0);
        m_gameplayInputActive = true;
    }
    EngineStdOut("Finished running 'Start Button Clicked' scripts.", 0);
}

void Engine::initFps() {
    lastfpstime = SDL_GetTicks(); // 마지막 FPS 측정 시간
    framecount = 0;
    currentFps = 0.0f;
    EngineStdOut("FPS counter initialized.", 0);
}

void Engine::setfps(int fps) {
    if (fps > 0) {
        // 유효한 FPS 값인 경우에만 설정
        this->specialConfig.TARGET_FPS = fps;
        EngineStdOut("Target FPS set to: " + to_string(this->specialConfig.TARGET_FPS), 0);
    } else {
        EngineStdOut(
            "Attempted to set invalid Target FPS: " + to_string(fps) + ". Keeping current TARGET_FPS: " + to_string(
                this->specialConfig.TARGET_FPS), 1);
    }
}

void Engine::updateFps() {
    framecount++;
    Uint64 now = SDL_GetTicks(); // 현재 시간
    Uint64 delta = now - lastfpstime;

    if (delta >= 1000) {
        // 1초 이상 경과 시 FPS 업데이트
        currentFps = static_cast<float>(framecount * 1000.0) / delta;
        lastfpstime = now;
        framecount = 0;
    }
}

void Engine::startProjectTimer() {
    if (!m_projectTimerRunning) {
        m_projectTimerRunning = true;
        // 타이머 재개 시, 이전에 누적된 시간을 제외한 시점부터 시작하도록 Ticks 설정
        m_projectTimerStartTime = SDL_GetTicks() - static_cast<Uint64>(m_projectTimerValue * 1000.0);
    }
}

void Engine::stopProjectTimer() {
    if (!m_projectTimerRunning) {
        m_projectTimerRunning = false;
        m_projectTimerValue = (SDL_GetTicks() - m_projectTimerStartTime) / 1000.0;
    }
}

void Engine::resetProjectTimer() {
    m_projectTimerValue = 0.0; // 프로젝트 타이머 리셋
    m_projectTimerStartTime = 0;
}

void Engine::showProjectTimer(bool show) {
    for (auto &var: m_HUDVariables) // HUD 변수 중 타이머 타입의 가시성 설정
    {
        if (var.variableType == "timer") {
            var.isVisible = show;
            EngineStdOut(
                string("Project timer ('") + var.name + "') visibility set to: " + (show ? "Visible" : "Hidden"), 3);
            return; // Assuming only one timer variable for now
        }
    }
    EngineStdOut("showProjectTimer: No timer variable found in HUDVariables to set visibility.", 1);
}

void Engine::showAnswerValue(bool show) {
    for (auto &var: m_HUDVariables) {
        if (var.variableType == "answer") {
            var.isVisible = show;
            EngineStdOut(
                string("Project Answer ('") + var.name + "') visibility set to: " + (show ? "Visible" : "Hidden"), 3);
            return; // Assuming only one timer variable for now
        }
    }
}

double Engine::getProjectTimerValue() const {
    if (m_projectTimerRunning) // 타이머가 실행 중이면 현재 경과 시간 반환
    {
        Uint64 now = SDL_GetTicks();
        Uint64 delta = now - lastfpstime;
        return static_cast<double>(now - m_projectTimerStartTime) / 1000.0;
    }
    return m_projectTimerValue;
}

Entity *Engine::getEntityById(const string &id) {
    auto it = entities.find(id); // ID로 엔티티 검색
    if (it != entities.end()) {
        return it->second.get();
    }

    return nullptr;
}

// Private method, assumes m_engineDataMutex is already locked by the caller.
Entity *Engine::getEntityById_nolock(const std::string &id) {
    auto it = entities.find(id);
    if (it != entities.end()) {
        return it->second.get();
    }
    return nullptr;
}

// Shared pointer version of getEntityById
std::shared_ptr<Entity> Engine::getEntityByIdShared(const std::string &id) {
    std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // Protects entities map
    auto it = entities.find(id);
    if (it != entities.end()) {
        return it->second; // entities map stores std::shared_ptr<Entity>
    }
    return nullptr;
}

// Shared pointer version of getEntityById_nolock
std::shared_ptr<Entity> Engine::getEntityByIdNolockShared(const std::string &id) {
    // Assumes m_engineDataMutex is already locked by the caller or not needed.
    auto it = entities.find(id);
    if (it != entities.end()) {
        return it->second;
    }
    return nullptr;
}

void Engine::renderLoadingScreen() {
    if (!this->renderer) {
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
        static_cast<float>(barX), static_cast<float>(barY), static_cast<float>(barWidth), static_cast<float>(barHeight)
    };
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255); // 로딩 바 배경
    SDL_RenderFillRect(renderer, &bgRect);

    SDL_FRect innerBgRect = {
        static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(barWidth - 4),
        static_cast<float>(barHeight - 4)
    };
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); // 로딩 바 내부 배경
    SDL_RenderFillRect(renderer, &innerBgRect);

    float progressPercent = 0.0f;
    if (totalItemsToLoad > 0) {
        progressPercent = static_cast<float>(loadedItemCount) / totalItemsToLoad; // 진행률 계산
    }
    progressPercent = min(1.0f, max(0.0f, progressPercent));

    int progressWidth = static_cast<int>((barWidth - 4) * progressPercent);
    SDL_FRect progressRect = {
        static_cast<float>(barX + 2), static_cast<float>(barY + 2), static_cast<float>(progressWidth),
        static_cast<float>(barHeight - 4)
    };
    SDL_SetRenderDrawColor(renderer, 255, 165, 0, 255);
    SDL_RenderFillRect(renderer, &progressRect);

    if (loadingScreenFont) {
        SDL_Color textColor = {255, 255, 255, 255}; // 텍스트 색상
        ostringstream percentStream;
        percentStream << fixed << setprecision(0) << (progressPercent * 100.0f) << "%";
        string percentText = percentStream.str();
        // 진행률 텍스트 렌더링
        SDL_Surface *surfPercent = TTF_RenderText_Blended(percentFont, percentText.c_str(), percentText.size(),
                                                          textColor);
        if (surfPercent) {
            SDL_Texture *texPercent = SDL_CreateTextureFromSurface(renderer, surfPercent);
            if (texPercent) {
                SDL_FRect dstRect = {
                    barX + (barWidth - static_cast<float>(surfPercent->w)) / 2.0f,
                    barY + (barHeight - static_cast<float>(surfPercent->h)) / 2.0f,
                    static_cast<float>(surfPercent->w),
                    static_cast<float>(surfPercent->h)
                };
                SDL_RenderTexture(renderer, texPercent, nullptr, &dstRect);
                SDL_DestroyTexture(texPercent);
            } else {
                EngineStdOut("Failed to create loading percent text texture: " + string(SDL_GetError()), 2);
            }
            SDL_DestroySurface(surfPercent);
        } else {
            EngineStdOut("Failed to render loading percent text surface ", 2);
        }
        // 브랜드 이름 렌더링 (설정된 경우)
        if (!specialConfig.BRAND_NAME.empty()) {
            SDL_Surface *surfBrand = TTF_RenderText_Blended(loadingScreenFont, specialConfig.BRAND_NAME.c_str(),
                                                            specialConfig.BRAND_NAME.size(), textColor);
            if (surfBrand) {
                SDL_Texture *texBrand = SDL_CreateTextureFromSurface(renderer, surfBrand);
                if (texBrand) {
                    SDL_FRect dstRect = {
                        (windowW - static_cast<float>(surfBrand->w)) / 2.0f,
                        barY - static_cast<float>(surfBrand->h) - 10.0f, static_cast<float>(surfBrand->w),
                        static_cast<float>(surfBrand->h)
                    };
                    SDL_RenderTexture(renderer, texBrand, nullptr, &dstRect);
                    SDL_DestroyTexture(texBrand);
                } else {
                    EngineStdOut("Failed to create brand name text texture: " + string(SDL_GetError()), 2);
                }
                SDL_DestroySurface(surfBrand);
            } else {
                EngineStdOut("Failed to render brand name text surface ", 2);
            }
        } else {
            // 아니면 무엇이 로드하는지 보여줌.
            SDL_Surface *surfBrand = TTF_RenderText_Blended(loadingScreenFont, LOADING_METHOD_NAME.c_str(),
                                                            specialConfig.BRAND_NAME.size(), textColor);
            if (surfBrand) {
                SDL_Texture *texBrand = SDL_CreateTextureFromSurface(renderer, surfBrand);
                if (texBrand) {
                    SDL_FRect dstRect = {
                        (windowW - static_cast<float>(surfBrand->w)) / 2.0f,
                        barY - static_cast<float>(surfBrand->h) - 10.0f, static_cast<float>(surfBrand->w),
                        static_cast<float>(surfBrand->h)
                    };
                    SDL_RenderTexture(renderer, texBrand, nullptr, &dstRect);
                    SDL_DestroyTexture(texBrand);
                } else {
                    EngineStdOut("Failed to create brand name text texture: " + string(SDL_GetError()), 2);
                }
                SDL_DestroySurface(surfBrand);
            } else {
                EngineStdOut("Failed to render brand name text surface ", 2);
            }
        }
        // 프로젝트 이름 렌더링 (설정된 경우)
        if (specialConfig.SHOW_PROJECT_NAME && !PROJECT_NAME.empty()) {
            SDL_Surface *surfProject = TTF_RenderText_Blended(loadingScreenFont, PROJECT_NAME.c_str(),
                                                              PROJECT_NAME.size(), textColor);
            if (surfProject) {
                SDL_Texture *texProject = SDL_CreateTextureFromSurface(renderer, surfProject);
                if (texProject) {
                    // 프로젝트 이름은 상단으로
                    SDL_FRect dstRect = {
                        (windowW - static_cast<float>(surfProject->w)) / 2.0f,
                        barY + static_cast<float>(surfProject->h) - 130.5f, static_cast<float>(surfProject->w),
                        static_cast<float>(surfProject->h)
                    };
                    SDL_RenderTexture(renderer, texProject, nullptr, &dstRect);
                    SDL_DestroyTexture(texProject);
                } else {
                    EngineStdOut("Failed to create project name text texture: " + string(SDL_GetError()), 2);
                }
                SDL_DestroySurface(surfProject);
            } else {
                EngineStdOut("Failed to render project name text surface ", 2);
            }
        }
    } else {
        EngineStdOut("renderLoadingScreen: Loading screen font not available. Cannot draw text.", 1); // 로딩 화면 폰트 사용 불가
    }

    SDL_RenderPresent(this->renderer); // 화면에 렌더링된 내용 표시
}

// 현재 씬 ID 반환
const string &Engine::getCurrentSceneId() const {
    return currentSceneId;
}

/**
 * @brief 메시지 박스 표시
 *
 * @param message 메시지 내용
 * @param IconType 아이콘 종류 (SDL_MESSAGEBOX_ERROR, SDL_MESSAGEBOX_WARNING, SDL_MESSAGEBOX_INFORMATION)
 * @param showYesNo 예 / 아니오
 */
bool Engine::showMessageBox(const string &message, int IconType, bool showYesNo) const {
    Uint32 flags = 0;
    const char *title = OMOCHA_ENGINE_NAME;

    switch (IconType) {
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
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "예"}, // "Yes" 버튼
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "아니오"}
    }; // "No" 버튼
    const SDL_MessageBoxData messageboxData{
        flags,
        window,
        title,
        message.c_str(),
        SDL_arraysize(buttons),
        buttons,
        nullptr
    };
    if (showYesNo) {
        // 예/아니오 버튼 표시
        int buttonid_press = -1;
        if (SDL_ShowMessageBox(&messageboxData, &buttonid_press) < 0) {
            EngineStdOut("Can't Showing MessageBox");
            return false; // Add return statement for the error case
        } else {
            if (buttonid_press == 0) {
                // "예" 버튼 클릭
                return true;
            } else {
                return false;
            }
        }
    } else {
        SDL_ShowSimpleMessageBox(flags, title, message.c_str(), this->window); // 단순 메시지 박스 (OK 버튼만)
        /*if (flags == SDL_MESSAGEBOX_ERROR)
        {
           quick_exit(EXIT_FAILURE); //오류면 종료함
        }*/

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
                               const std::string &executionThreadId) {
    EngineStdOut("Activating text input for object " + requesterObjectId + " with question: \"" + question + "\"", 0,
                 executionThreadId); {
        std::unique_lock<std::mutex> lock(m_textInputMutex);
        if (m_textInputActive) {
            EngineStdOut("Text input already active. Waiting for it to complete...", 1, executionThreadId);
            m_textInputCv.wait(lock, [this] { return !m_textInputActive || m_isShuttingDown; });
        }

        // 이전 입력 상태 정리 및 새 입력 상태 설정
        m_currentTextInputBuffer.clear();
        m_textInputQuestionMessage = question;
        m_textInputRequesterObjectId = requesterObjectId;
        m_textInputActive = true;
    }

    m_gameplayInputActive = false; // 텍스트 입력 중에는 일반 게임플레이 키 입력 비활성화

    // SDL 텍스트 입력 시작 (IME 등 활성화)
    SDL_StartTextInput(window); // Entity에 질문 다이얼로그 표시 요청
    {
        std::lock_guard<std::recursive_mutex> guard(m_engineDataMutex);
        std::shared_ptr<Entity> entity = getEntityByIdShared(requesterObjectId); // Use shared_ptr version
        if (entity) {
            entity->showDialog(question, "ask", 0); // 0 duration means it stays until explicitly removed
            EngineStdOut("Successfully showed dialog for entity " + requesterObjectId, 3, executionThreadId);
        } else {
            EngineStdOut("Warning: Entity " + requesterObjectId + " not found when trying to show 'ask' dialog.", 1,
                         executionThreadId);
        }
    } {
        std::unique_lock<std::mutex> inputLock(m_textInputMutex);
        EngineStdOut("Script thread " + executionThreadId + " waiting for text input...", 0, executionThreadId);

        // 입력이 완료되거나 엔진이 종료될 때까지 대기
        m_textInputCv.wait(inputLock, [this] { return !m_textInputActive || m_isShuttingDown; });

        // 엔진이 종료되는 경우 텍스트 입력 상태를 정리
        if (m_isShuttingDown) {
            clearTextInput();
            deactivateTextInput();
        }

        // 입력 완료 또는 엔진 종료로 대기 상태 해제
        SDL_StopTextInput(window);
        m_gameplayInputActive = true; // 게임플레이 입력 다시 활성화

        if (m_isShuttingDown) {
            EngineStdOut("Text input cancelled due to engine shutdown for " + requesterObjectId, 1, executionThreadId);
        } else {
            EngineStdOut("Text input received for " + requesterObjectId + ". Last answer: \"" + m_lastAnswer + "\"", 0,
                         executionThreadId);
        }
    }
}

std::string Engine::getLastAnswer() const {
    std::lock_guard<std::mutex> lock(m_textInputMutex); // Protect access to m_lastAnswer
    return m_lastAnswer;
}

void Engine::updateAnswerVariable() {
    std::string currentAnswer; {
        std::unique_lock<std::mutex> inputLock(m_textInputMutex, std::try_to_lock);
        if (!inputLock.owns_lock()) {
            requestAnswerUpdate(); // 락을 얻지 못했다면 나중에 다시 시도
            return;
        }
        currentAnswer = m_lastAnswer;
    }

    std::unique_lock<std::recursive_mutex> dataLock(m_engineDataMutex, std::try_to_lock);
    if (!dataLock.owns_lock()) {
        requestAnswerUpdate(); // 락을 얻지 못했다면 나중에 다시 시도
        return;
    }
    for (auto &var: m_HUDVariables) {
        if (var.variableType == "answer" || (var.variableType == "list" && var.isAnswerList)) {
            var.value = currentAnswer;
            EngineStdOut("Updated " + var.variableType + " variable '" + var.name + "' value to: " + currentAnswer, 3);
            // answer 타입일 경우 첫 번째 변수만 업데이트하고 종료
            if (var.variableType == "answer") {
                break;
            }
        }
    }
}

/**
 * @brief 엔진 로그출력
 *
 * @param s 출력할내용
 * @param LEVEL 수준 예) 0:정보, 1:경고, 2:오류, 3:디버그, 4:특수, 5:TREAD
 * @param ThreadID 쓰레드 ID
 */
void Engine::EngineStdOut(string s, int LEVEL, string TREADID) const {
    std::lock_guard<std::mutex> lock(m_logMutex);
    string prefix;

    string color_code = ANSI_COLOR_RESET;

    switch (LEVEL) {
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

void Engine::updateCurrentMouseStageCoordinates(int windowMouseX, int windowMouseY) {
    float stageX_calc, stageY_calc;
    if (mapWindowToStageCoordinates(windowMouseX, windowMouseY, stageX_calc, stageY_calc)) {
        // 윈도우 좌표를 스테이지 좌표로 변환 성공 시
        this->m_currentStageMouseX = stageX_calc;
        this->m_currentStageMouseY = stageY_calc;
        this->m_isMouseOnStage = true;
    } else {
        this->m_isMouseOnStage = false;
        // 마우스가 스테이지 밖에 있을 때의 처리
        // 엔트리는 마지막 유효 좌표를 유지하거나 0을 반환할 수 있습니다.
        // 현재는 m_isMouseOnStage 플래그로 구분하고, BlockExecutor에서 이 플래그를 확인하여 처리합니다.
        // 필요하다면 여기서 m_currentStageMouseX/Y를 특정 값(예: 0)으로 리셋할 수 있습니다.
        // this->m_currentStageMouseX = 0.0f;
        // this->m_currentStageMouseY = 0.0f;
    }
}

void Engine::goToScene(const string &sceneId) {
    if (scenes.count(sceneId)) // 요청된 씬 ID가 존재하는 경우
    {
        if (currentSceneId == sceneId) {
            EngineStdOut("Already in scene: " + scenes[sceneId] + " (ID: " + sceneId + "). No change.", 0); // 이미 해당 씬임
            return;
        }

        const std::string oldSceneId = currentSceneId; {
            std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex);

            // 1. 모든 엔티티의 스크립트 상태를 확인하고 필요한 작업 수행
            for (const auto &[entityId, entityPtr]: entities) {
                const ObjectInfo *objInfo = getObjectInfoById(entityId);
                if (objInfo) {
                    bool isGlobal = (objInfo->sceneId == "global" || objInfo->sceneId.empty());
                    // 글로벌이 아니고 현재 씬에 속한 엔티티의 스크립트 종료
                    if (!isGlobal && objInfo->sceneId == oldSceneId) {
                        entityPtr->terminateAllScriptThread("");
                        EngineStdOut("Terminated all scripts for entity " + entityId + " during scene change", 0);
                    }
                }
            }

            // 2. 클론 엔티티 수집 및 제거
            std::vector<std::string> entitiesToDelete;
            for (const auto &objInfo: objects_in_order) {
                if (!objInfo.sceneId.empty() && objInfo.sceneId != "global") {
                    if (objInfo.sceneId == oldSceneId) {
                        auto entityIt = entities.find(objInfo.id);
                        if (entityIt != entities.end() && entityIt->second->getIsClone()) {
                            entitiesToDelete.push_back(objInfo.id);
                        }
                    }
                }
            }

            // 수집된 클론 엔티티들을 제거
            for (const auto &entityId: entitiesToDelete) {
                auto entityIt = entities.find(entityId);
                if (entityIt != entities.end()) {
                    entities.erase(entityIt);
                    EngineStdOut("Removed clone entity " + entityId + " during scene change", 0);
                }
            }
        }
        // 2.5 모든 엔티티의 활성 다이얼로그 제거
        EngineStdOut("Clearing active dialogs for scene change...", 0);
        for (const auto &[entityId, entityPtr]: entities) {
            if (entityPtr) {
                entityPtr->removeDialog();
            }
        }

        // 3. 새로운 씬으로 전환하기 전에 엔티티들을 초기 위치로 리셋
        {
            std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // 초기 위치 정보를 ObjectInfo의 entity 필드에서 수집
            std::map<std::string, std::pair<double, double> > initialPositions;
            for (const auto &obj: objects_in_order) {
                if (obj.sceneId == sceneId || obj.sceneId == "global" || obj.sceneId.empty()) {
                    // entity 필드에서 x, y 좌표 가져오기
                    double x = 0.0, y = 0.0;
                    if (obj.entity.contains("x") && obj.entity["x"].is_number()) {
                        x = obj.entity["x"].get<double>();
                    }
                    if (obj.entity.contains("y") && obj.entity["y"].is_number()) {
                        y = obj.entity["y"].get<double>();
                    }
                    initialPositions[obj.id] = std::make_pair(x, y);
                }
            }

            // 엔티티들의 위치와 상태를 초기화
            for (const auto &[entityId, entityPtr]: entities) {
                const ObjectInfo *objInfo = getObjectInfoById(entityId);
                if (objInfo && (objInfo->sceneId == sceneId || objInfo->sceneId == "global" || objInfo->sceneId.
                                empty())) {
                    Entity *entity = entityPtr.get();

                    // 초기 위치 설정
                    auto posIt = initialPositions.find(entityId);
                    if (posIt != initialPositions.end()) {
                        entity->setX(posIt->second.first);
                        entity->setY(posIt->second.second);
                    }

                    EngineStdOut("Reset entity " + entityId + " to initial position for new scene", 0);
                }
            }
        }

        // 4. 씬 전환 완료
        currentSceneId = sceneId;
        EngineStdOut("Changed scene to: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")", 0);
        triggerWhenSceneStartScripts();
    } else {
        EngineStdOut("Error: Scene with ID '" + sceneId + "' not found. Cannot switch scene.", 2); // 씬 ID 찾을 수 없음
    }
}

void Engine::goToNextScene() {
    if (m_sceneOrder.empty()) {
        EngineStdOut("Cannot go to next scene: Scene order is not defined or no scenes loaded.", 1); // 씬 순서 정의 안됨
        return;
    }
    if (currentSceneId.empty()) {
        EngineStdOut("Cannot go to next scene: Current scene ID is empty.", 1); // 현재 씬 ID 없음
        return;
    }

    auto it = find(m_sceneOrder.begin(), m_sceneOrder.end(), currentSceneId);
    if (it != m_sceneOrder.end()) {
        size_t currentIndex = distance(m_sceneOrder.begin(), it);
        if (currentIndex + 1 < m_sceneOrder.size()) {
            // 다음 씬으로 이동
            goToScene(m_sceneOrder[currentIndex + 1]);
        } else {
            EngineStdOut("Already at the last scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")",
                         0); // 이미 마지막 씬임
        }
    } else {
        // 현재 씬 ID를 씬 순서에서 찾을 수 없음
        EngineStdOut(
            "Error: Current scene ID '" + currentSceneId +
            "' not found in defined scene order. Cannot determine next scene.",
            2);
    }
}

void Engine::goToPreviousScene() {
    if (m_sceneOrder.empty()) {
        EngineStdOut("Cannot go to previous scene: Scene order is not defined or no scenes loaded.", 1); // 씬 순서 정의 안됨
        return;
    }
    if (currentSceneId.empty()) {
        EngineStdOut("Cannot go to previous scene: Current scene ID is empty.", 1); // 현재 씬 ID 없음
        return;
    }

    auto it = find(m_sceneOrder.begin(), m_sceneOrder.end(), currentSceneId);
    if (it != m_sceneOrder.end()) {
        size_t currentIndex = distance(m_sceneOrder.begin(), it);
        if (currentIndex > 0) {
            // 이전 씬으로 이동
            goToScene(m_sceneOrder[currentIndex - 1]);
        } else {
            EngineStdOut("Already at the first scene: " + scenes[currentSceneId] + " (ID: " + currentSceneId + ")",
                         0); // 이미 첫 번째 씬임
        }
    } else {
        // 현재 씬 ID를 씬 순서에서 찾을 수 없음
        EngineStdOut(
            "Error: Current scene ID '" + currentSceneId +
            "' not found in defined scene order. Cannot determine previous scene.",
            2);
    }
}

void Engine::triggerWhenSceneStartScripts() {
    if (currentSceneId.empty()) {
        EngineStdOut("Cannot trigger 'when_scene_start' scripts: Current scene ID is empty.", 1);
        return;
    }
    EngineStdOut("Triggering 'when_scene_start' scripts for scene: " + currentSceneId, 0);
    for (const auto &scriptPair: m_whenStartSceneLoadedScripts) {
        const string &objectId = scriptPair.first;
        const Script *scriptPtr = scriptPair.second;

        bool executeForScene = false;
        for (const auto &objInfo: objects_in_order) {
            if (objInfo.id == objectId && (objInfo.sceneId == currentSceneId || objInfo.sceneId == "global" || objInfo.
                                           sceneId.empty())) {
                executeForScene = true;
                break;
            }
        }

        if (executeForScene) {
            EngineStdOut(
                "  -> Running 'when_scene_start' script for object ID: " + objectId + " in scene " + currentSceneId, 3);
            this->dispatchScriptForExecution(objectId, scriptPtr, currentSceneId, 0.0);
        }
    }
}

int Engine::getBlockCountForObject(const std::string &objectId) const {
    auto it = objectScripts.find(objectId);
    if (it == objectScripts.end()) {
        return 0;
    }
    int count = 0;
    for (const auto &script: it->second) {
        // 첫 번째 블록은 이벤트 트리거이므로 제외
        if (script.blocks.size() > 1) {
            // 실행 가능한 블록 수 계산
            count += static_cast<int>(script.blocks.size() - 1);
        }
    }
    return count;
}

int Engine::getBlockCountForScene(const std::string &sceneId) const {
    int totalCount = 0;
    for (const auto &objInfo: objects_in_order) {
        if (objInfo.sceneId == sceneId) {
            // 지정된 씬의 각 오브젝트에 대해 블록 수 가져오기
            totalCount += getBlockCountForObject(objInfo.id);
        }
    }
    return totalCount;
}

void Engine::changeObjectIndex(const std::string &entityId, Omocha::ObjectIndexChangeType changeType) {
    std::lock_guard<std::recursive_mutex> lock(this->m_engineDataMutex); // Protect access to objects_in_order

    auto it = std::find_if(objects_in_order.begin(), objects_in_order.end(),
                           [&entityId](const ObjectInfo &objInfo) {
                               return objInfo.id == entityId;
                           });

    if (it == objects_in_order.end()) {
        EngineStdOut("changeObjectIndex: Entity ID '" + entityId + "' not found in objects_in_order.", 1);
        return;
    }

    ObjectInfo objectToMove = *it; // Make a copy of the ObjectInfo
    int currentIndex = static_cast<int>(std::distance(objects_in_order.begin(), it));
    int targetIndex = currentIndex;
    int numObjects = static_cast<int>(objects_in_order.size());

    if (numObjects <= 0) {
        // Should not happen if an item was found, but good for safety
        EngineStdOut("changeObjectIndex: objects_in_order is empty or in an invalid state.", 2);
        return;
    }
    int maxPossibleIndex = numObjects - 1;

    switch (changeType) {
        case Omocha::ObjectIndexChangeType::BRING_TO_FRONT: // Move to index 0 (topmost)
            targetIndex = 0;
            break;
        case Omocha::ObjectIndexChangeType::SEND_TO_BACK: // Move to the last index (bottommost)
            targetIndex = maxPossibleIndex;
            break;
        case Omocha::ObjectIndexChangeType::BRING_FORWARD: // Move one step towards top (index decreases)
            if (currentIndex > 0) {
                targetIndex = currentIndex - 1;
            } else {
                EngineStdOut("Object " + entityId + " is already at the front. No change in Z-order.", 0);
                return; // Already at the front
            }
            break;
        case Omocha::ObjectIndexChangeType::SEND_BACKWARD: // Move one step towards bottom (index increases)
            if (currentIndex < maxPossibleIndex) {
                targetIndex = currentIndex + 1;
            } else {
                EngineStdOut("Object " + entityId + " is already at the back. No change in Z-order.", 0);
                return; // Already at the back
            }
            break;
        case Omocha::ObjectIndexChangeType::UNKNOWN:
        default:
            EngineStdOut("changeObjectIndex: Unknown or unsupported Z-order change type for entity " + entityId, 1);
            return;
    }

    if (targetIndex == currentIndex && changeType != Omocha::ObjectIndexChangeType::BRING_TO_FRONT && changeType !=
        Omocha::ObjectIndexChangeType::SEND_TO_BACK) {
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
                                   float thickness) {
    // p1_stage_entry is {lastX_entry, lastY_entry}
    // p2_stage_entry_modified_y is {currentX_entry, currentY_entry * -1.0f}
    // 두 점의 구성 요소는 엔트리 스테이지 좌표계 기준 (중앙 0,0, Y축 위쪽은 .x 및 원래 .y)

    if (!renderer || !tempScreenTexture) {
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

int Engine::getTotalBlockCount() const {
    int totalCount = 0;
    for (const auto &pair: objectScripts) {
        // 또는, 스크립트가 없는 오브젝트(개수 0)를 포함하려면 모든 objects_in_order를 반복하고 getBlockCountForObject를 호출합니다.
        // 현재 방식은 실제로 스크립트 항목이 있는 오브젝트의 개수를 합산합니다.
        totalCount += getBlockCountForObject(pair.first);
    }
    return totalCount;
}

TTF_Font *Engine::getDialogFont() {
    // 우선 hudFont를 재사용합니다. 필요시 별도 폰트 로드 로직 추가 가능.
    if (!hudFont) {
        EngineStdOut("Dialog font (hudFont) is not loaded!", 2);
        // 여기서 기본 폰트를 로드하거나, hudFont가 로드되도록 보장해야 합니다.
    }
    return hudFont;
}

void Engine::drawDialogs() {
    if (!renderer || !tempScreenTexture)
        return;

    TTF_Font *font = getDialogFont();
    if (!font)
        return;

    for (auto &pair: entities) {
        // Entity의 DialogState를 수정할 수 있으므로 non-const 반복
        std::lock_guard<std::recursive_mutex> entity_lock(pair.second->getStateMutex());
        // Lock the entity's state mutex
        Entity *entity = pair.second.get();
        if (entity && entity->hasActiveDialog()) {
            Entity::DialogState &dialog = entity->m_currentDialog; // 수정 가능한 참조 가져오기

            // 1. 텍스트 텍스처 생성/업데이트 (필요한 경우)
            if (dialog.needsRedraw || !dialog.textTexture) {
                if (dialog.textTexture)
                    SDL_DestroyTexture(dialog.textTexture);
                SDL_Color textColor = {0, 0, 0, 255}; // 검은색 텍스트
                // 텍스트 자동 줄 바꿈을 위해 _Wrapped 함수 사용 (예: 150px 너비에서 줄 바꿈)
                SDL_Surface *surface = TTF_RenderText_Blended_Wrapped(font, dialog.text.c_str(), dialog.text.size(),
                                                                      textColor, 150);
                if (surface) {
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
            float entitySdlY = static_cast<float>(PROJECT_STAGE_HEIGHT / 3.0 - entity->getY());

            // 엔티티의 시각적 너비/높이
            float entityVisualWidth = static_cast<float>(entity->getWidth() * std::abs(entity->getScaleX()));
            float entityVisualHeight = static_cast<float>(entity->getHeight() * std::abs(entity->getScaleY()));

            float padding = 8.0f;
            float bubbleWidth = dialog.textRect.w + 2 * padding;
            float bubbleHeight = dialog.textRect.h + 2 * padding;

            // 꼬리가 연결될 오브젝트 위의 지점 (오브젝트 중앙 상단)
            float tailConnectToEntityX = entitySdlX;
            float tailConnectToEntityY = entitySdlY - entityVisualHeight / 5.0f;

            // 말풍선 기본 위치 설정 (오브젝트 위쪽)
            float desiredGapAboveEntity = 10.0f; // 오브젝트 상단과 말풍선 하단 사이의 간격

            // 말풍선 하단 Y좌표 계산
            float bubbleBottomEdgeY = tailConnectToEntityY - desiredGapAboveEntity;

            // 말풍선 최종 Y좌표 (상단 기준)
            dialog.bubbleScreenRect.y = bubbleBottomEdgeY - bubbleHeight;

            // 말풍선 X좌표 (오브젝트 중앙에 맞춤)
            dialog.bubbleScreenRect.x = entitySdlX - bubbleWidth / 2.0f;

            // 말풍선 너비 및 높이 설정
            dialog.bubbleScreenRect.w = bubbleWidth;
            dialog.bubbleScreenRect.h = bubbleHeight;

            // 화면 경계 내로 클램핑
            if (dialog.bubbleScreenRect.x < 0)
                dialog.bubbleScreenRect.x = 0;
            if (dialog.bubbleScreenRect.y < 0)
                dialog.bubbleScreenRect.y = 0;
            if (dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w > PROJECT_STAGE_WIDTH) {
                dialog.bubbleScreenRect.x = PROJECT_STAGE_WIDTH - dialog.bubbleScreenRect.w;
            }
            if (dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h > PROJECT_STAGE_HEIGHT) {
                dialog.bubbleScreenRect.y = PROJECT_STAGE_HEIGHT - dialog.bubbleScreenRect.h;
            }

            // 3. 말풍선 배경 렌더링
            SDL_FColor bubbleBgColor = {255, 255, 255, 255}; // 흰색
            SDL_FColor bubbleBorderColor = {79, 128, 255, 255}; // 엔트리 블루 테두리
            float cornerRadius = 8.0f;

            if (dialog.type == "think") {
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
                // 꼬리가 말풍선 본체에서 anchorPoint 방향으로 이어지도록 조정
                float thinkBubbleCenterX = dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w / 2.0f;
                float thinkBubbleBottomY = dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h; // 말풍선 하단 중앙

                // 꼬리가 향할 지점 (오브젝트 중앙 상단)
                float dx_think_tail = tailConnectToEntityX - thinkBubbleCenterX;
                float dy_think_tail = tailConnectToEntityY - thinkBubbleBottomY;
                float dist_think_tail = sqrt(dx_think_tail * dx_think_tail + dy_think_tail * dy_think_tail);

                if (dist_think_tail > 0) {
                    // 거리가 있을 때만 꼬리 그리기
                    float norm_dx_think = dx_think_tail / dist_think_tail;
                    float norm_dy_think = dy_think_tail / dist_think_tail;

                    Helper_DrawFilledCircle(
                        renderer, static_cast<int>(thinkBubbleBottomY + norm_dx_think * (dist_think_tail * 0.3f)),
                        static_cast<int>(thinkBubbleBottomY + norm_dy_think * (dist_think_tail * 0.3f)), 5);
                    Helper_DrawFilledCircle(
                        renderer, static_cast<int>(thinkBubbleBottomY + norm_dx_think * (dist_think_tail * 0.6f)),
                        static_cast<int>(thinkBubbleBottomY + norm_dy_think * (dist_think_tail * 0.6f)), 4);
                }
            } else {
                // "speak"
                // 꼬리 밑변 너비의 절반
                float tailBaseWidth = 8.0f;

                // 꼬리 꼭짓점 계산
                // 꼬리 끝점 (오브젝트 중앙 상단)
                dialog.tailVertices[2].position = {tailConnectToEntityX, tailConnectToEntityY};
                // 꼬리 시작점 (말풍선 본체 아래쪽, 앵커 포인트를 향하도록 약간 조정)
                // 말풍선 하단 중앙을 기준으로 좌우로 tailBaseWidth 만큼 떨어진 두 점
                float bubbleBottomCenterX = dialog.bubbleScreenRect.x + dialog.bubbleScreenRect.w / 2.0f;
                float bubbleBottomY = dialog.bubbleScreenRect.y + dialog.bubbleScreenRect.h;

                dialog.tailVertices[0].position = {bubbleBottomCenterX - tailBaseWidth, bubbleBottomY};
                dialog.tailVertices[1].position = {bubbleBottomCenterX + tailBaseWidth, bubbleBottomY};

                // 꼬리 채우기를 위해 각 꼭짓점의 색상을 말풍선 배경색으로 설정
                for (int k = 0; k < 3; ++k) {
                    dialog.tailVertices[k].color = bubbleBgColor;
                }

                // 꼬리 채우기 (말풍선 배경색과 동일하게)
                // SDL_RenderGeometry는 texture가 NULL일 때 정점 색상을 사용합니다.
                SDL_SetRenderDrawColor(renderer, bubbleBgColor.r, bubbleBgColor.g, bubbleBgColor.b, bubbleBgColor.a);
                SDL_RenderGeometry(renderer, nullptr, dialog.tailVertices, 3, nullptr, 0);

                // 말풍선 본체 테두리 그리기 (테두리 색상으로 채워진 둥근 사각형)
                SDL_SetRenderDrawColor(renderer, bubbleBorderColor.r, bubbleBorderColor.g, bubbleBorderColor.b,
                                       bubbleBorderColor.a);
                Helper_RenderFilledRoundedRect(renderer, &dialog.bubbleScreenRect, cornerRadius);

                // 꼬리 테두리 그리기
                SDL_RenderLine(renderer, static_cast<int>(dialog.tailVertices[0].position.x),
                               static_cast<int>(dialog.tailVertices[0].position.y),
                               static_cast<int>(dialog.tailVertices[2].position.x),
                               static_cast<int>(dialog.tailVertices[2].position.y));
                SDL_RenderLine(renderer, static_cast<int>(dialog.tailVertices[1].position.x),
                               static_cast<int>(dialog.tailVertices[1].position.y),
                               static_cast<int>(dialog.tailVertices[2].position.x),
                               static_cast<int>(dialog.tailVertices[2].position.y));
                // 말풍선 본체와 꼬리 밑변이 만나는 부분은 둥근 사각형 테두리에 의해 이미 그려짐

                // 내부 배경 그리기 (테두리보다 약간 작게, 꼬리 부분은 이미 채워짐)
                SDL_FRect innerBgRect = {
                    dialog.bubbleScreenRect.x + 1.0f, dialog.bubbleScreenRect.y + 1.0f,
                    dialog.bubbleScreenRect.w - 2.0f, dialog.bubbleScreenRect.h - 2.0f
                };
                SDL_SetRenderDrawColor(renderer, bubbleBgColor.r, bubbleBgColor.g, bubbleBgColor.b, bubbleBgColor.a);
                Helper_RenderFilledRoundedRect(renderer, &innerBgRect, cornerRadius - 1.0f);
            }

            // 5. 텍스트 렌더링
            SDL_FRect textDestRect = {
                dialog.bubbleScreenRect.x + padding,
                dialog.bubbleScreenRect.y + padding,
                dialog.textRect.w,
                dialog.textRect.h
            };
            SDL_RenderTexture(renderer, dialog.textTexture, nullptr, &textDestRect);
        }
    }
}

SDL_Texture *Engine::LoadTextureFromSvgResource(SDL_Renderer *renderer, int resourceID) {
    HINSTANCE hInstance = GetModuleHandle(NULL); // 현재 실행 파일의 인스턴스 핸들

    // 1. 리소스 정보 찾기
    HRSRC hResInfo = FindResource(hInstance, MAKEINTRESOURCE(resourceID), RT_RCDATA);
    if (hResInfo == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FindResource failed for ID %d: %lu", resourceID, GetLastError());
        return NULL;
    }

    // 2. 리소스 로드
    HGLOBAL hResData = LoadResource(hInstance, hResInfo);
    if (hResData == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LoadResource failed for ID %d: %lu", resourceID, GetLastError());
        return NULL;
    }

    // 3. 리소스 데이터 포인터 및 크기 얻기
    void *pSvgData = LockResource(hResData);
    DWORD dwSize = SizeofResource(hInstance, hResInfo);

    if (pSvgData == NULL || dwSize == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LockResource or SizeofResource failed for ID %d. Error: %lu",
                     resourceID, GetLastError());
        // FreeResource는 LoadResource로 얻은 핸들에 대해 호출 (Vista 이상에서는 no-op)
        // LockResource 실패 시에는 호출하지 않는 것이 일반적
        return NULL;
    }

    // 4. 메모리상의 SVG 데이터를 위한 SDL_RWops 생성
    SDL_IOStream *rw = SDL_IOFromConstMem(pSvgData, dwSize);
    if (rw == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_RWFromConstMem failed: %s", SDL_GetError());
        // pSvgData는 Windows 리소스이므로 여기서 해제하지 않음
        return NULL;
    }

    // 5. SDL_image를 사용하여 SVG 데이터로부터 Surface 로드
    // IMG_LoadTyped_RW의 세 번째 인자 '1'은 작업 후 SDL_image가 rwops를 자동으로 닫도록(SDL_RWclose) 함.
    SDL_Surface *surface = IMG_LoadSVG_IO(rw); // "SVG" 타입을 명시
    if (surface == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IMG_LoadTyped_RW (SVG) failed");
        // rw는 IMG_LoadTyped_RW에 의해 이미 처리되었으므로 여기서 SDL_RWclose를 호출할 필요 없음
        return NULL;
    }

    // 6. Surface로부터 Texture 생성
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
    }

    // 7. 원본 Surface 해제
    SDL_DestroySurface(surface);

    // LockResource로 얻은 포인터(pSvgData)는 UnlockResource가 필요 없고,
    // HGLOBAL 핸들(hResData)도 FreeResource를 명시적으로 호출할 필요가 없습니다 (특히 Vista 이상).
    // 리소스는 모듈이 언로드될 때 자동으로 해제됩니다.

    return texture;
}

bool Engine::setEntitySelectedCostume(const std::string &entityId, const std::string &costumeId) {
    for (auto &objInfo: objects_in_order) {
        if (objInfo.id == entityId) {
            // Check if the costumeId exists in objInfo.costumes
            bool costumeExists = false;
            for (const auto &costume: objInfo.costumes) {
                if (costume.id == costumeId) {
                    costumeExists = true;
                    break;
                }
            }
            if (costumeExists) {
                objInfo.selectedCostumeId = costumeId;
                // If you have an Entity* map, you might want to inform the Entity object too,
                // or the Entity might fetch its ObjectInfo when needed.
                // For now, just updating ObjectInfo which is used in drawAllEntities.
                return true;
            } else {
                EngineStdOut(
                    "Costume ID '" + costumeId + "' not found in the costume list for object '" + entityId + "'.", 1);
                return false;
            }
        }
    }
    EngineStdOut("Entity ID '" + entityId + "' not found in objects_in_order when trying to set costume.", 1);
    return false;
}

bool Engine::setEntitychangeToNextCostume(const string &entityId, const string &asOption) {
    for (auto &objInfo: objects_in_order) {
        if (objInfo.id == entityId) {
            if (objInfo.costumes.size() <= 1) {
                // 모양이 없거나 1개만 있으면 다음/이전으로 변경 불가
                EngineStdOut(
                    "Entity '" + entityId + "' has " + to_string(objInfo.costumes.size()) +
                    " costume(s). Cannot change to next/previous.",
                    1);
                return false;
            }

            int currentCostumeIndex = -1;
            for (size_t i = 0; i < objInfo.costumes.size(); ++i) {
                if (objInfo.costumes[i].id == objInfo.selectedCostumeId) {
                    currentCostumeIndex = static_cast<int>(i);
                    break;
                }
            }

            if (currentCostumeIndex == -1) {
                // 현재 선택된 모양 ID를 목록에서 찾을 수 없는 경우 (데이터 불일치)
                // 안전하게 첫 번째 모양으로 설정하거나 오류 처리
                EngineStdOut(
                    "Error: Selected costume ID '" + objInfo.selectedCostumeId +
                    "' not found in costume list for entity '" + entityId +
                    "'. Defaulting to first costume if available.",
                    2);
                if (!objInfo.costumes.empty()) {
                    objInfo.selectedCostumeId = objInfo.costumes[0].id;
                }
                return false; // 또는 true를 반환하고 첫 번째 모양으로 설정
            }

            int totalCostumes = static_cast<int>(objInfo.costumes.size());
            int nextCostumeIndex = currentCostumeIndex;

            if (asOption == "prev") {
                nextCostumeIndex = (currentCostumeIndex - 1 + totalCostumes) % totalCostumes;
            } else // "next" 또는 다른 알 수 없는 옵션은 다음으로 처리
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
                          const std::string &executionThreadId) {
    EngineStdOut(
        "Message '" + messageId + "' raised by object " + senderObjectId + " (Thread: " + executionThreadId + ")", 0,
        executionThreadId);
    auto it = m_messageReceivedScripts.find(messageId);
    if (it != m_messageReceivedScripts.end()) {
        const auto &scriptsToRun = it->second;
        EngineStdOut(
            "Found " + std::to_string(scriptsToRun.size()) + " script(s) listening for message '" + messageId + "'", 0,
            executionThreadId);
        for (const auto &scriptPair: scriptsToRun) {
            const std::string &listeningObjectId = scriptPair.first;
            const Script *scriptPtr = scriptPair.second;

            // Optional: Prevent an object from triggering its own message-received script if that's desired behavior.
            // if (listeningObjectId == senderObjectId) {
            //     EngineStdOut("Object " + listeningObjectId + " is trying to trigger its own message '" + messageId + "'. Skipping.", 1, executionThreadId);
            //     continue;
            // }

            Entity *listeningEntity = getEntityById(listeningObjectId);
            if (listeningEntity) {
                // Check if the listening entity is in the current scene or is global
                const ObjectInfo *objInfoPtr = getObjectInfoById(listeningObjectId);
                if (objInfoPtr) {
                    bool isInCurrentScene = (objInfoPtr->sceneId == currentSceneId);
                    bool isGlobal = (objInfoPtr->sceneId == "global" || objInfoPtr->sceneId.empty());
                    if (isInCurrentScene || isGlobal) {
                        EngineStdOut(
                            "Dispatching message-received script for object: " + listeningObjectId + " (Message: '" +
                            messageId + "')", 3, executionThreadId); // LEVEL 0 -> 3
                        // 메시지 수신 스크립트는 항상 새 스레드로 시작 (existingExecutionThreadId를 비워둠)
                        // 또한, 메시지를 받은 시점의 씬 컨텍스트(currentSceneId)를 사용합니다.
                        // 기존 코드에서 sceneIdAtDispatch가 항상 currentSceneId로 전달되었으므로,
                        // 이 부분은 변경 없이 유지하거나 명시적으로 currentSceneId를 전달할 수 있습니다.
                        // 여기서는 명확성을 위해 currentSceneId를 전달합니다.
                        // deltaTime은 이벤트 발생 시점이므로 0.0f가 적절합니다.
                        this->dispatchScriptForExecution(listeningObjectId, scriptPtr, currentSceneId, 0.0f, "");
                    } else {
                        EngineStdOut(
                            "Script for message '" + messageId + "' on object " + listeningObjectId +
                            " not run because object is not in current scene (" + currentSceneId + ") and not global.",
                            1, executionThreadId);
                    }
                } else {
                    EngineStdOut(
                        "ObjectInfo not found for entity ID '" + listeningObjectId + "' during message '" + messageId +
                        "' processing. Script not run.",
                        1, executionThreadId);
                }
            } else {
                EngineStdOut(
                    "Entity " + listeningObjectId +
                    " not found when trying to execute message-received script for message '" + messageId + "'.",
                    1,
                    executionThreadId);
            }
        }
    } else {
        EngineStdOut("No scripts found listening for message '" + messageId + "'", 0, executionThreadId);
    }
}

void Engine::dispatchScriptForExecution(const std::string &entityId, const Script *scriptPtr,
                                        const std::string &sceneIdAtDispatch, float deltaTime,
                                        const std::string &existingExecutionThreadId) {
    if (!scriptPtr) {
        // 스크립트 포인터가 null이면 오류 로깅 후 반환
        EngineStdOut("dispatchScriptForExecution called with null script pointer for object: " + entityId, 2);
        return;
    }

    // Entity 객체를 찾습니다.
    Entity *entity = nullptr; {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // entities 맵 접근 보호
        entity = getEntityById_nolock(entityId);
    }

    if (!entity) {
        // 엔티티를 찾을 수 없으면 오류 로깅 후 반환
        EngineStdOut("dispatchScriptForExecution: Entity " + entityId + " not found. Cannot schedule script.", 2,
                     existingExecutionThreadId);
        return;
    }

    // Entity가 자신의 스크립트 실행을 스레드 풀에 직접 스케줄링하도록 요청합니다.
    // 이 변경은 Entity 클래스에 scheduleScriptExecutionOnPool (가칭)과 같은 새 메소드 구현을 필요로 합니다.
    // 해당 메소드는 이전에 dispatchScriptForExecution의 람다가 수행하던 로직 (스레드 ID 생성, 로깅, executeScript 호출, 오류 처리)을 포함해야 합니다.
    entity->scheduleScriptExecutionOnPool(scriptPtr, sceneIdAtDispatch, deltaTime, existingExecutionThreadId);
}

/**
 * @brief Saves cloud variables to a JSON file based on m_projectId.
 * @return True if saving was successful, false otherwise.
 */
bool Engine::saveCloudVariablesToJson() {
    if (PROJECT_NAME.empty()) {
        EngineStdOut("Project ID is not set. Cannot save cloud variables.", 2);
        return false;
    }
    // 파일 경로를 m_projectId를 사용하여 생성
    std::string directoryPath = "cloud_saves"; // 클라우드 저장 파일들을 모아둘 디렉토리
    std::string filePath = directoryPath + "/" + PROJECT_NAME + ".cloud.json";

    // 디렉토리 생성 (존재하지 않는 경우)
    try {
        if (!std::filesystem::exists(directoryPath)) {
            if (std::filesystem::create_directories(directoryPath)) {
                EngineStdOut("Created directory for cloud saves: " + directoryPath, 0);
            } else {
                EngineStdOut("Failed to create directory for cloud saves (unknown error): " + directoryPath, 2);
                return false; // 디렉토리 생성 실패 시 저장 중단
            }
        }
    } catch (const std::filesystem::filesystem_error &e) {
        EngineStdOut("Error creating directory for cloud saves '" + directoryPath + "': " + std::string(e.what()), 2);
        return false; // 예외 발생 시 저장 중단
    }

    EngineStdOut("Saving cloud variables to: " + filePath, 3);
    std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // Protect m_HUDVariables

    nlohmann::json doc = nlohmann::json::array();

    for (const auto &hudVar: m_HUDVariables) {
        if (hudVar.isCloud) {
            nlohmann::json varJson = nlohmann::json::object();

            varJson["name"] = hudVar.name;
            varJson["value"] = hudVar.value;
            varJson["objectId"] = hudVar.objectId;
            varJson["variableType"] = hudVar.variableType;

            if (hudVar.variableType == "list") {
                nlohmann::json arrayJson = nlohmann::json::array();
                for (const auto &item: hudVar.array) {
                    nlohmann::json itemJson = nlohmann::json::object();
                    itemJson["key"] = item.key;
                    itemJson["data"] = item.data;
                    arrayJson.push_back(itemJson);
                }
                varJson["array"] = arrayJson;
            }
            doc.push_back(varJson);
        }
    }

    std::ofstream ofs(filePath);
    if (!ofs.is_open()) {
        EngineStdOut("Failed to open file for saving cloud variables: " + filePath, 2);
        return false;
    }

    ofs << doc.dump(4); // 4는 들여쓰기 칸 수 (pretty print)
    ofs.close();

    EngineStdOut("Cloud variables saved successfully to: " + filePath, 0);
    return true;
}

/**
 * @brief Loads cloud variables from a JSON file based on m_projectId, updating existing ones.
 * @return True if loading was successful or file didn't exist, false on parse error.
 */
bool Engine::loadCloudVariablesFromJson() {
    if (PROJECT_NAME.empty()) {
        EngineStdOut("Project ID is not set. Cannot load cloud variables.", 1);
        return true; // 프로젝트 ID가 없으면 로드할 파일도 없다고 간주 (오류는 아님)
    }
    // 파일 경로를 m_projectId를 사용하여 생성
    std::string directoryPath = "cloud_saves";
    std::string filePath = directoryPath + "/" + PROJECT_NAME + ".cloud.json";

    EngineStdOut("Attempting to load cloud variables from: " + filePath, 3);

    if (!std::filesystem::exists(filePath)) {
        EngineStdOut("Cloud variable save file not found: " + filePath + ". No variables loaded.", 1);
        return true; // 저장 파일이 아직 없는 것은 정상적인 상황
    }

    std::ifstream ifs(filePath); // Correct
    nlohmann::json doc; // Correct
    if (!ifs.is_open()) {
        EngineStdOut("Failed to open cloud variable file: " + filePath, 2);
        return false;
    }

    try {
        ifs >> doc; // 또는 doc = nlohmann::json::parse(ifs);
        ifs.close();
    } catch (const nlohmann::json::parse_error &e) {
        EngineStdOut("Failed to parse cloud variable file: " + std::string(e.what()) +
                     " at byte " + std::to_string(e.byte),
                     2);
        return false;
    }

    if (!doc.is_array()) {
        EngineStdOut("Cloud variable file content is not a JSON array: " + filePath, 2);
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // Protect m_HUDVariables

    int updatedCount = 0;
    int notFoundCount = 0;

    for (const auto &savedVarJson: doc) // Correct
    {
        if (!savedVarJson.is_object()) {
            EngineStdOut("Skipping non-object entry in cloud variable file.", 1);
            continue;
        }
        std::string name, value, objectId, variableType;

        if (savedVarJson.contains("name") && savedVarJson["name"].is_string()) {
            name = savedVarJson["name"].get<string>();
        } else {
            /* handle error or skip */
            continue;
        }

        if (savedVarJson.contains("value") && savedVarJson["value"].is_string()) {
            // Assuming value is always stored as string
            value = savedVarJson["value"].get<string>();
        } else {
            /* handle error or default */
            value = "";
        }

        if (savedVarJson.contains("objectId") && savedVarJson["objectId"].is_string()) {
            objectId = savedVarJson["objectId"].get<string>();
        } else {
            objectId = ""; /* Default for global */
        }

        if (savedVarJson.contains("variableType") && savedVarJson["variableType"].is_string()) {
            variableType = savedVarJson["variableType"].get<string>();
        } else {
            /* handle error or skip */
            continue;
        }

        if (name.empty() || variableType.empty()) {
            EngineStdOut("Skipping cloud variable entry with empty name or variableType.", 1);
            continue;
        }

        bool foundAndUpdated = false;
        for (auto &hudVar: m_HUDVariables) {
            if (hudVar.isCloud && hudVar.name == name && hudVar.objectId == objectId && hudVar.variableType ==
                variableType) {
                hudVar.value = value;
                EngineStdOut(
                    "Cloud variable '" + name + "' (Object: '" + (objectId.empty() ? "global" : objectId) +
                    "') updated to value: '" + value + "'", 3);

                if (variableType == "list") {
                    hudVar.array.clear();
                    if (savedVarJson.contains("array") && savedVarJson["array"].is_array()) {
                        const nlohmann::json &arrayJson = savedVarJson["array"];
                        for (const auto &itemJson: arrayJson) // Corrected: Iterate over nlohmann::json array
                        {
                            if (itemJson.is_object()) {
                                ListItem listItem;
                                if (itemJson.contains("key") && itemJson["key"].is_string()) {
                                    listItem.key = itemJson["key"].get<string>();
                                } else {
                                    listItem.key = "";
                                }
                                if (itemJson.contains("data") && itemJson["data"].is_string()) {
                                    listItem.data = itemJson["data"].get<string>();
                                } else {
                                    listItem.data = "";
                                }
                                hudVar.array.push_back(listItem);
                            }
                        }
                        EngineStdOut(
                            "  List '" + name + "' updated with " + std::to_string(hudVar.array.size()) + " items.", 0);
                    }
                }
                foundAndUpdated = true;
                updatedCount++;
                break;
            }
        }

        if (!foundAndUpdated) {
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

void Engine::requestProjectRestart() {
    EngineStdOut("Project restart requested. Flag set.", 0);
    m_restartRequested.store(true, std::memory_order_relaxed);
}

void Engine::performProjectRestart() {
    SDL_SetWindowTitle(window, "재시작 하는중...");
    aeHelper.stopAllSounds(); //실행중 사운드 전부 종료
    EngineStdOut("Project restart sequence initiated...", 0);
    m_isShuttingDown.store(true, std::memory_order_relaxed); // 모든 시스템에 종료 신호

    // 1. 모든 엔티티의 스크립트에 종료 요청
    EngineStdOut("Requesting termination of all entity scripts...", 0);
    std::vector<std::shared_ptr<Entity> > currentEntitiesSnapshot; {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // entities 맵 접근 보호
        for (auto const &[id, entity_ptr]: entities) {
            currentEntitiesSnapshot.push_back(entity_ptr);
        }
    }

    for (auto &entity_ptr: currentEntitiesSnapshot) {
        if (entity_ptr) {
            // 각 엔티티의 스레드 상태 뮤텍스는 terminateAllScriptThread 내부에서 처리됨
            entity_ptr->terminateAllScriptThread("");
        }
    }

    // 2. Engine의 메인 작업 스레드 풀 중지 및 조인
    EngineStdOut("Stopping main worker thread pool (m_workerThreads)...", 0);
    stopThreadPool(); // m_workerThreads 중지 및 조인

    // 3. BlockExecutor의 스레드 풀 중지 및 조인 (unique_ptr의 reset()이 소멸자 호출)
    if (threadPool) {
        // threadPool은 BlockExecutor의 ThreadPool 인스턴스를 가리키는 unique_ptr
        EngineStdOut("Stopping BlockExecutor::ThreadPool (engine.threadPool)...", 0);
        threadPool.reset(); // ThreadPool 소멸자가 호출되어 내부 작업자 스레드들을 join합니다.
        EngineStdOut("BlockExecutor::ThreadPool stopped.", 0);
    }

    // 4. 스레드가 모두 중지된 후 엔티티별 상태 정리 (스크립트 상태, 다이얼로그 등)
    EngineStdOut("Clearing script states and dialogs from entities...", 0);
    for (auto &entity_ptr: currentEntitiesSnapshot) {
        if (entity_ptr) {
            std::lock_guard<std::recursive_mutex> entity_lock(entity_ptr->getStateMutex()); // 개별 엔티티 상태 보호
            entity_ptr->scriptThreadStates.clear();
            entity_ptr->removeDialog();
            // 필요하다면 다른 Entity 내부 상태 초기화 (예: timedMoveState 등)
            entity_ptr->timedMoveState = {};
            entity_ptr->timedMoveObjState = {};
            entity_ptr->timedRotationState = {};
        }
    }

    // 5. 엔진의 핵심 컬렉션 정리 및 엔티티 소멸
    EngineStdOut("Clearing engine collections and deleting entities...", 0); {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // 모든 컬렉션 접근 보호
        entities.clear(); // shared_ptr 참조 카운트가 0이 되면 Entity 소멸자 호출
        objects_in_order.clear();
        objectScripts.clear();

        // 이벤트 스크립트 목록 초기화
        startButtonScripts.clear();
        keyPressedScripts.clear();
        m_mouseClickedScripts.clear();
        m_mouseClickCanceledScripts.clear();
        m_whenObjectClickedScripts.clear();
        m_whenObjectClickCanceledScripts.clear();
        m_messageReceivedScripts.clear();
        m_whenStartSceneLoadedScripts.clear();
        m_whenCloneStartScripts.clear();

        m_HUDVariables.clear();
        scenes.clear();
        m_sceneOrder.clear(); // 씬 순서도 초기화
        m_cloneCounters.clear(); // 복제본 카운터 초기화
    }

    // 6. 엔진 상태 변수들 리셋
    EngineStdOut("Resetting engine state...", 0);
    m_isShuttingDown.store(false, std::memory_order_relaxed); // 재실행을 위해 종료 플래그 리셋
    resetProjectTimer();
    m_gameplayInputActive = false;
    m_pressedObjectId = "";
    m_stageWasClickedThisFrame.store(false, std::memory_order_relaxed);
    currentSceneId = ""; // 현재 씬 ID 초기화
    firstSceneIdInOrder = ""; // 첫 씬 ID 초기화
    m_debuggerScrollOffsetY = 0.0f; // 디버거 스크롤 위치 초기화


    // 7. 스레드 풀들 재 생성
    EngineStdOut("Re-creating thread pools...", 0);
    startThreadPool((max)(2u, std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 2u));
    if (!threadPool) {
        // BlockExecutor의 스레드 풀이 reset()으로 해제되었으므로 다시 생성
        threadPool = std::make_unique<ThreadPool>(
            *this, (max)(1u, std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 2));
    }

    // 8. 프로젝트 데이터 리로드
    EngineStdOut("Reloading project data...", 0);
    if (m_currentProjectFilePath.empty()) {
        EngineStdOut("Error: Current project file path is empty. Cannot restart project.", 2);
        m_restartRequested.store(false, std::memory_order_relaxed);
        return;
    }
    if (!loadProject(m_currentProjectFilePath)) {
        EngineStdOut("Error: Failed to reload project during restart. Restart aborted.", 2);
        showMessageBox("프로젝트를 다시 시작하는 중 오류가 발생했습니다: 프로젝트 파일을 다시 로드할 수 없습니다.", msgBoxIconType.ICON_ERROR);
        m_restartRequested.store(false, std::memory_order_relaxed);
        // 심각한 오류이므로, 여기서 프로그램을 안전하게 종료하는 것을 고려할 수 있습니다.
        // exit(EXIT_FAILURE);
        return;
    }

    // 9. 에셋(이미지, 사운드) 리로드
    EngineStdOut("Reloading assets...", 0);
    if (!loadImages()) {
        // loadImages는 내부적으로 기존 텍스처를 해제해야 합니다.
        EngineStdOut("Warning: Failed to reload images during restart.", 1);
    }
    if (!loadSounds()) {
        // loadSounds도 필요시 기존 사운드 데이터 정리 로직 포함해야 합니다.
        EngineStdOut("Warning: Failed to reload sounds during restart.", 1);
    }

    // 10. 프로젝트 실행 시작
    EngineStdOut("Restarting project execution...", 0);
    // currentSceneId는 loadProject에 의해 firstSceneIdInOrder를 통해 설정되어야 합니다.
    if (scenes.count(firstSceneIdInOrder)) {
        currentSceneId = firstSceneIdInOrder;
    } else if (!m_sceneOrder.empty() && scenes.count(m_sceneOrder.front())) {
        currentSceneId = m_sceneOrder.front();
        EngineStdOut(
            "Warning: firstSceneIdInOrder was not set or invalid, falling back to the first scene in m_sceneOrder for restart.",
            1);
    } else {
        EngineStdOut("Error: No valid start scene found after reloading project. Cannot start execution.", 2);
        showMessageBox("프로젝트 재시작 오류: 유효한 시작 장면을 찾을 수 없습니다.", msgBoxIconType.ICON_ERROR);
        m_restartRequested.store(false, std::memory_order_relaxed);
        // exit(EXIT_FAILURE);
        return;
    }

    triggerWhenSceneStartScripts(); // 새 씬의 시작 스크립트 트리거
    runStartButtonScripts(); // 시작 버튼 스크립트 실행 (이 함수 내부에서 m_gameplayInputActive = true 설정)

    m_restartRequested.store(false, std::memory_order_relaxed); // 재시작 요청 플래그 리셋
    EngineStdOut("Project restart sequence complete.", 0);
}

void Engine::requestStopObject(const std::string &callingEntityId, const std::string &callingThreadId,
                               const std::string &targetOption) {
    EngineStdOut(
        "Engine::requestStopObject called by Entity: " + callingEntityId + ", Thread: " + callingThreadId + ", Option: "
        + targetOption, 0, callingThreadId);

    if (targetOption == "all") {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // Protects entities map
        for (auto &pair: entities) {
            if (pair.second) {
                pair.second->terminateAllScriptThread(""); // Terminate all threads for this entity
            }
        }
        EngineStdOut("Requested to stop ALL scripts for ALL objects.", 0, callingThreadId);
    } else if (targetOption == "thisOnly" || targetOption == "this_object") {
        // "this_object" is from Entry.js, seems to mean the current Entity
        Entity *currentEntity = getEntityById(callingEntityId);
        // Mutex handled by getEntityById if needed, or use _nolock if outer lock exists
        if (currentEntity) {
            currentEntity->terminateAllScriptThread(""); // Terminate all threads for the calling entity
            EngineStdOut("Requested to stop ALL scripts for THIS object: " + callingEntityId, 0, callingThreadId);
        }
    } else if (targetOption == "thisThread") {
        Entity *currentEntity = getEntityById(callingEntityId);
        if (currentEntity) {
            currentEntity->terminateScriptThread(callingThreadId); // Terminate only the current thread
            EngineStdOut("Requested to stop THIS script thread: " + callingThreadId + " for object: " + callingEntityId,
                         0, callingThreadId);
        }
    } else if (targetOption == "otherThread") {
        Entity *currentEntity = getEntityById(callingEntityId);
        if (currentEntity) {
            currentEntity->terminateAllScriptThread(callingThreadId);
            // Terminate all threads for this entity EXCEPT the current one
            EngineStdOut(
                "Requested to stop OTHER script threads for object: " + callingEntityId + " (excluding " +
                callingThreadId + ")", 0, callingThreadId);
        }
    } else if (targetOption == "other_objects") {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // Protects entities map
        for (auto &pair: entities) {
            if (pair.first != callingEntityId && pair.second) {
                // If it's not the calling entity
                pair.second->terminateAllScriptThread(""); // Terminate all threads for this other entity
            }
        }
        EngineStdOut("Requested to stop ALL scripts for OTHER objects (excluding " + callingEntityId + ")", 0,
                     callingThreadId);
    } else {
        EngineStdOut("Unknown targetOption for stop_object: " + targetOption, 1, callingThreadId);
    }
}

void Engine::startThreadPool(size_t numThreads) {
    m_isShuttingDown.store(false, std::memory_order_relaxed);
    for (size_t i = 0; i < numThreads; ++i) {
        m_workerThreads.emplace_back(&Engine::workerLoop, this);
    }
    EngineStdOut("thread pool started with " + std::to_string(numThreads) + " threads.", 0);
}

void Engine::stopThreadPool() {
    EngineStdOut("Stopping thread pool...", 0);
    // m_isShuttingDown is typically set by the caller (destructor or restart logic)
    // If not, it should be set here:

    m_taskQueueCV_std.notify_all(); // Wake up all workers
    for (std::thread &worker: m_workerThreads) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workerThreads.clear();
    // Clear any remaining tasks in the queue
    std::queue<std::function<void()> > empty;
    std::swap(m_taskQueue, empty);
    EngineStdOut("pool stopped and joined.", 0);
}

int Engine::getNextCloneIdSuffix(const std::string &originalId) {
    // std::lock_guard<std::mutex> lock(m_engineDataMutex); // m_cloneCounters 접근 보호
    return ++m_cloneCounters[originalId];
}

std::shared_ptr<Entity> Engine::createCloneOfEntity(const std::string &originalEntityId,
                                                    const std::string &sceneIdForScripts) {
    EngineStdOut("Attempting to create clone of entity: " + originalEntityId, 0);

    const ObjectInfo *originalObjInfo = nullptr;
    Entity *originalEntity = nullptr; {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex);
        if (entities.size() >= static_cast<size_t>(specialConfig.MAX_ENTITY)) {
            EngineStdOut(
                "Cannot create clone: Maximum entity limit (" + std::to_string(specialConfig.MAX_ENTITY) + ") reached.",
                1);
            return nullptr;
        }
        originalObjInfo = getObjectInfoById(originalEntityId);
        auto it_orig_entity = entities.find(originalEntityId);
        if (it_orig_entity != entities.end()) {
            originalEntity = it_orig_entity->second.get();
        }
    }

    if (!originalObjInfo || !originalEntity) {
        EngineStdOut("Cannot create clone: Original entity or ObjectInfo not found for ID: " + originalEntityId, 2);
        return nullptr;
    }

    // 1. Generate unique ID for the clone
    std::string cloneId = originalEntityId + "_clone_" + std::to_string(getNextCloneIdSuffix(originalEntityId));

    // 2. Create new ObjectInfo for the clone
    ObjectInfo cloneObjInfo = *originalObjInfo; // Copy original ObjectInfo
    cloneObjInfo.id = cloneId;
    cloneObjInfo.name = originalObjInfo->name + " (복제본)"; // Append to name
    // sceneId, objectType, costumes, sounds, textContent, etc., are copied.
    // Importantly, SDL_Texture* in costumes should point to the *same* shared textures.
    // The ObjectInfo copy is a shallow copy for SDL_Texture*, which is correct.

    // 3. Create new Entity for the clone
    Entity *cloneEntity = new Entity(
        this,
        cloneId,
        cloneObjInfo.name,
        originalEntity->getX(), originalEntity->getY(), // Clones start at original's current position
        originalEntity->getRegX(), originalEntity->getRegY(),
        originalEntity->getScaleX(), originalEntity->getScaleY(),
        originalEntity->getRotation(), originalEntity->getDirection(),
        originalEntity->getWidth(), originalEntity->getHeight(),
        originalEntity->isVisible(), // Clones are visible by default if original is, or follow original's visibility
        originalEntity->getRotateMethod());

    cloneEntity->setIsClone(true, originalEntityId);

    // Copy effects
    cloneEntity->setEffectBrightness(originalEntity->getEffectBrightness());
    cloneEntity->setEffectAlpha(originalEntity->getEffectAlpha());
    cloneEntity->setEffectHue(originalEntity->getEffectHue());

    // Pen state: Clones typically start with a clean pen state (pen up, default color)
    // The Entity constructor already initializes PenState (brush, paint) to default.
    // If specific pen properties need to be inherited, copy them here.
    // For now, default initialization is fine.
    // cloneEntity->brush = originalEntity->brush; // If deep copy of pen state is needed
    // cloneEntity->paint = originalEntity->paint;

    // Timed states (move, rotation) should be default/inactive for a new clone.
    // Dialog state should be default/inactive.

    // 4. Add clone to engine collections
    {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex);
        objects_in_order.push_back(cloneObjInfo); // Add to rendering order (usually on top initially)
        // Consider Z-order: clones often appear on top of the original.
        // The default push_back adds to the end, which is rendered first (bottom).
        // To put on top (rendered last):
        // objects_in_order.insert(objects_in_order.begin(), cloneObjInfo);
        // For now, let's add to the end and it can be reordered by blocks.

        // cloneEntity는 Entity* 이므로 std::shared_ptr로 감싸서 저장
        entities[cloneId] = std::shared_ptr<Entity>(cloneEntity);

        // Copy scripts from the original object type to the clone's entry in objectScripts
        // This ensures the clone can respond to events if its original type had scripts.
        auto originalScriptsIt = objectScripts.find(originalEntityId);
        if (originalScriptsIt != objectScripts.end()) {
            objectScripts[cloneId] = originalScriptsIt->second; // Copy the vector of Scripts
        }
    }

    EngineStdOut("Successfully created clone: " + cloneId + " from " + originalEntityId, 0);

    // 5. Trigger "when_clone_start" scripts for the new clone
    // These scripts are associated with the *original object's ID* in m_whenCloneStartScripts
    bool foundCloneStartScript = false;
    for (const auto &scriptPair: m_whenCloneStartScripts) {
        if (scriptPair.first == originalEntityId) {
            // Does the original object type have a "when_clone_start"?
            const Script *scriptToRunOnClone = scriptPair.second;
            if (scriptToRunOnClone && !scriptToRunOnClone->blocks.empty()) {
                EngineStdOut("Dispatching 'when_clone_start' for new clone: " + cloneEntity->getId() +
                             " (original type: " + originalEntityId + ")",
                             0);
                // Dispatch the script for the *new clone's ID*
                // The sceneIdForScripts is the scene where the create_clone block was executed.
                this->dispatchScriptForExecution(cloneEntity->getId(), scriptToRunOnClone, sceneIdForScripts, 0.0f);
                foundCloneStartScript = true;
            } else {
                EngineStdOut(
                    "Found 'when_clone_start' for original type " + originalEntityId + " but script is empty. Clone: " +
                    cloneEntity->getId(), 1);
            }
        }
    }
    if (!foundCloneStartScript) {
        EngineStdOut(
            "No 'when_clone_start' script found for original type: " + originalEntityId + " for clone " + cloneEntity->
            getId(), 0);
    }

    // If the original was part of a specific scene, the clone should also be.
    // This is handled by copying ObjectInfo which includes sceneId.
    // If the original was global, the clone is also global.

    // 반환 타입이 std::shared_ptr<Entity>이므로, 맵에서 가져오거나 새로 생성한 shared_ptr 반환
    return entities[cloneId];
}

void Engine::deleteEntity(const std::string &entityIdToDelete) {
    auto safe_log_id = [](const std::string &id, size_t max_len = 256) {
        if (id.length() > max_len) {
            return id.substr(0, max_len) + "...";
        }
        return id;
    };

    EngineStdOut(std::format("Attempting to delete entity: {}.", safe_log_id(entityIdToDelete)), 0);

    Entity *entityPtr = nullptr;
    // First, mark scripts for termination and get the pointer

    {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // Lock for entities map access
        auto it = entities.find(entityIdToDelete);
        if (it == entities.end()) {
            EngineStdOut("Cannot delete entity: Entity ID '" + entityIdToDelete + "' not found in 'entities' map.", 1);
            return;
        }
        entityPtr = it->second.get();

        if (entityPtr) {
            EngineStdOut("Marking all scripts for entity " + entityIdToDelete + " for termination.", 0);
            entityPtr->terminateAllScriptThread(""); // Signal all its scripts to stop
        }
    }

    // Now, perform removals from collections and delete the object.
    // This assumes that script threads will see the termination flag and exit cleanly
    // before any major issues arise from the entity being removed from collections.
    // A more robust system might defer the actual deletion of 'entityPtr' and removal
    // from collections to the end of the game loop's update cycle.
    if (entityPtr) {
        // Check if entityPtr was successfully retrieved
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // Lock for all collection modifications

        // Remove from entities map
        auto it_map = entities.find(entityIdToDelete);
        if (it_map != entities.end()) {
            entities.erase(it_map);
        }

        // Remove from objects_in_order
        std::erase_if(objects_in_order,
                      [&entityIdToDelete](const ObjectInfo &oi) { return oi.id == entityIdToDelete; });
        EngineStdOut(std::format("Removed ObjectInfo for {} from rendering order.", safe_log_id(entityIdToDelete)), 0);

        // Remove from objectScripts
        auto scriptIt = objectScripts.find(entityIdToDelete);
        if (scriptIt != objectScripts.end()) {
            objectScripts.erase(scriptIt);
        }

        // 로그 메시지 생성 전 ID 길이 제한
        std::string truncatedId = entityIdToDelete;
        const size_t maxLogIdLength = 256; // 로그에 표시할 ID 최대 길이 (예시)
        if (truncatedId.length() > maxLogIdLength) {
            truncatedId = truncatedId.substr(0, maxLogIdLength) + "...(truncated)";
        }
        EngineStdOut(std::format("Removed script entries for {}.", truncatedId), 0);
        EngineStdOut(std::format("Entity object {} deleted from memory.", truncatedId), 0);
    }
    std::string finalLogId = entityIdToDelete;
    if (finalLogId.length() > 256) {
        // 예시: ID가 256자 초과 시 일부만 표시
        finalLogId = finalLogId.substr(0, 256) + "...(truncated)";
    }
    EngineStdOut(std::format("Entity deletion process for {} completed.", finalLogId), 0);
}

void Engine::submitTask(std::function<void()> task) {
    if (m_isShuttingDown.load(std::memory_order_relaxed)) {
        EngineStdOut("Engine is shutting down. Task not submitted.", 1);
        // Optionally, log the task details if possible, or just ignore.
        return;
    } {
        std::lock_guard<std::mutex> lock(m_taskQueueMutex_std);
        m_taskQueue.push(std::move(task));
    }
    m_taskQueueCV_std.notify_one();
}

void Engine::deleteAllClonesOf(const std::string &originalEntityId) {
    EngineStdOut("Attempting to delete all clones of entity: " + originalEntityId, 0);
    std::vector<std::string> cloneIdsToDelete;

    // 1. Collect IDs of all clones originating from originalEntityId
    // Lock entities for reading, but don't modify it yet to avoid iterator invalidation.
    {
        std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex);
        for (const auto &pair: entities) {
            // pair.second는 std::shared_ptr<Entity> 타입입니다. 원시 포인터를 얻으려면 .get()을 사용해야 합니다.
            Entity *entity = pair.second.get();
            if (entity && entity->getIsClone() && entity->getOriginalClonedFromId() == originalEntityId) {
                cloneIdsToDelete.push_back(pair.first);
            }
        }
    }

    if (cloneIdsToDelete.empty()) {
        EngineStdOut("No clones found for original entity ID: " + originalEntityId, 0);
        return;
    }

    EngineStdOut("Found " + std::to_string(cloneIdsToDelete.size()) + " clones of " + originalEntityId + " to delete.",
                 0);
    for (const std::string &cloneId: cloneIdsToDelete) {
        deleteEntity(cloneId); // This will handle script termination and removal from collections
    }
    EngineStdOut("Finished deleting all clones of entity: " + originalEntityId, 0);
}

bool Engine::getStageWasClickedThisFrame() const {
    return m_stageWasClickedThisFrame.load(std::memory_order_relaxed);
}

void Engine::setStageClickedThisFrame(bool clicked) {
    m_stageWasClickedThisFrame.store(clicked, std::memory_order_relaxed);
}

std::string Engine::getPressedObjectId() const {
    std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // m_pressedObjectId 접근 보호
    return m_pressedObjectId;
}

bool Engine::isKeyPressed(SDL_Scancode scancode) const {
    std::lock_guard<std::mutex> lock(m_pressedKeysMutex);
    return m_pressedKeys.count(scancode) > 0;
}

std::string Engine::getDeviceType() const {
    // TODO: 실제 장치 유형 감지 로직 구현 (예: SDL_GetPlatform 또는 OS별 API 사용)
    // 현재는 데스크톱으로 가정합니다.
    // 안드로이드 빌드 시에는 __ANDROID__ 매크로 등을 사용하여 "mobile" 또는 "tablet" 반환
#if defined(__ANDROID__)
    // 안드로이드 플랫폼에서 화면 크기나 DPI 등을 기준으로 tablet/mobile 구분 필요
    // 여기서는 단순화를 위해 mobile로 가정
    return "mobile";
#else
    return "desktop";
#endif
}

bool Engine::isTouchSupported() const {
    /*if (SDL_InitSubSystem(SDL_INIT_EVENTS) < 0) // SDL_InitSubSystem은 실패 시 음수 반환
    {
        EngineStdOut("SDL_INIT_EVENTS could not be initialized",2);
        return false; // bool 함수이므로 false 반환
    }

    int num_touch_devices = SDL_GetTouchDevices();
    if (num_touch_devices < 0) { // SDL_GetNumTouchDevices는 오류 시 음수 반환 가능성 있음 (문서 확인 필요)
        EngineStdOut("Failed to get number of touch devices: " + string(SDL_GetError()), 2);
        return false;
    }
    return num_touch_devices > 0;*/
    return false;
}

void Engine::updateEntityTextContent(const std::string &entityId, const std::string &newText) {
    bool found = false;
    for (auto &objInfo: objects_in_order) {
        // objects_in_order를 순회하며 ID로 ObjectInfo를 찾습니다.
        if (objInfo.id == entityId) {
            if (objInfo.objectType == "textBox") {
                // 글상자 타입인지 확인
                objInfo.textContent = newText;
                found = true;
                //EngineStdOut("TextBox " + entityId + " text content updated to: \"" + newText + "\"", 3);

                // 글상자의 텍스트가 변경되었으므로, 해당 Entity의 다이얼로그(또는 텍스트 렌더링 캐시)를
                // 업데이트해야 할 수 있습니다. Entity 객체를 찾아 관련 플래그를 설정합니다.
                Entity *entity = getEntityById_nolock(entityId); // m_engineDataMutex가 이미 잠겨 있으므로 _nolock 사용
                if (entity) {
                    // 글상자가 다이얼로그 시스템을 사용하여 텍스트를 표시하거나,
                    // 자체적으로 텍스트 텍스처를 캐시하는 경우, 해당 부분을 다시 그려야 함을 표시합니다.
                    // 예를 들어, DialogState를 사용한다면:
                    // entity->m_currentDialog.text = newText; // DialogState의 텍스트도 동기화 (필요하다면)
                    // entity->m_currentDialog.needsRedraw = true;
                    // 또는 글상자 전용 렌더링 로직이 있다면 해당 플래그 설정
                    // 중요: Entity의 내부 너비/높이도 업데이트해야 합니다.
                    // 예시: entity->updateDimensionsForText(newText, objInfo.fontName, objInfo.fontSize);
                    // 아래는 Engine 레벨에서 직접 계산하여 Entity의 setter를 호출하는 예시입니다.
                    // 실제로는 Entity 클래스 내부에 이 로직이 있는 것이 더 좋습니다.
                    if (!newText.empty() && objInfo.fontSize > 0) {
                        std::string fontPath = getFontPathByName(objInfo.fontName, objInfo.fontSize, this);
                        TTF_Font *tempFont = getFont(fontPath, objInfo.fontSize);
                        if (tempFont) {
                            int measuredW;
                            size_t measuredLengthInBytes; // Correct type for TTF_MeasureString's last param
                            if (TTF_MeasureString(tempFont, newText.c_str(), newText.length(), 0, &measuredW,
                                                  &measuredLengthInBytes)) {
                                int fontHeight = TTF_GetFontHeight(tempFont);
                                entity->setWidth(static_cast<double>(measuredW));
                                if (fontHeight > 0) {
                                    entity->setHeight(static_cast<double>(fontHeight));
                                } else {
                                    EngineStdOut("Can't Get FontSize: ", 2);
                                }
                                //EngineStdOut("TextBox " + entityId + " dimensions potentially updated after text change.", 3);
                            } else {
                                EngineStdOut("Warning: TTF_SizeUTF8 failed during text update for " + entityId, 1);
                            }
                        } else {
                            EngineStdOut("Warning: Font not found during text update for " + entityId, 1);
                        }
                    }
                }
            } else {
                EngineStdOut(
                    "Warning: Attempted to set text for entity " + entityId + " which is not a textBox (type: " +
                    objInfo.objectType + ")", 1);
            }
            break;
        }
    }

    if (!found) {
        EngineStdOut("Warning: ObjectInfo not found for entity " + entityId + " when trying to update text content.",
                     1);
    }
}

void Engine::updateEntityTextColor(const std::string &entityId, const SDL_Color &newColor) {
    std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex);
    bool found = false;
    for (auto &objInfo: objects_in_order) {
        if (objInfo.id == entityId) {
            if (objInfo.objectType == "textBox") {
                objInfo.textColor = newColor;
                found = true;
                EngineStdOut("TextBox " + entityId + " text color updated.", 3);
                // Entity의 DialogState 등 텍스트 렌더링 캐시가 있다면 needsRedraw = true 설정 필요
            } else {
                EngineStdOut("Warning: Attempted to set text color for entity " + entityId + " which is not a textBox.",
                             1);
            }
            break;
        }
    }
    if (!found) {
        EngineStdOut("Warning: ObjectInfo not found for entity " + entityId + " when trying to update text color.", 1);
    }
}

void Engine::updateEntityTextBoxBackgroundColor(const std::string &entityId, const SDL_Color &newColor) {
    std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex);
    bool found = false;
    for (auto &objInfo: objects_in_order) {
        if (objInfo.id == entityId) {
            if (objInfo.objectType == "textBox") {
                objInfo.textBoxBackgroundColor = newColor;
                found = true;
                EngineStdOut("TextBox " + entityId + " background color updated.", 3);
                // Entity의 DialogState 등 텍스트 렌더링 캐시가 있다면 needsRedraw = true 설정 필요
            } else {
                EngineStdOut(
                    "Warning: Attempted to set background color for entity " + entityId + " which is not a textBox.",
                    1);
            }
            break;
        }
    }
    if (!found) {
        EngineStdOut(
            "Warning: ObjectInfo not found for entity " + entityId + " when trying to update background color.", 1);
    }
}

void Engine::drawScriptDebuggerUI() {
    auto truncate_str_len = [](const std::string &str, size_t max_len) {
        if (str.length() > max_len) {
            return str.substr(0, max_len) + "...";
        }
        return str;
    };

    if (!m_showScriptDebugger || !renderer || !hudFont) {
        return;
    }

    int windowW, windowH;
    SDL_GetRenderOutputSize(renderer, &windowW, &windowH);

    SDL_FRect debuggerPanelRect = {
        10.0f, 50.0f, static_cast<float>(windowW) - 20.0f, static_cast<float>(windowH) - 60.0f
    };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 20, 20, 40, 200);
    SDL_RenderFillRect(renderer, &debuggerPanelRect);

    SDL_Color textColor = {220, 220, 220, 255};
    float lineSpacing = 18.0f;
    float indent = 20.0f;

    // contentLayoutY는 스크롤 없이 모든 콘텐츠가 그려질 때의 Y 위치를 추적합니다.
    float contentLayoutY = debuggerPanelRect.y + 10.0f;
    float initialContentLayoutY = contentLayoutY; // 총 높이 계산을 위한 시작점

    // 제목 렌더링
    SDL_Surface *surfTitle = TTF_RenderText_Blended(hudFont, "Script Debugger", 0, textColor);
    if (surfTitle) {
        SDL_FRect titleRect = {
            debuggerPanelRect.x + 10.0f, 0 /*임시*/, static_cast<float>(surfTitle->w), static_cast<float>(surfTitle->h)
        };
        float renderY = contentLayoutY - m_debuggerScrollOffsetY;

        SDL_Texture *texTitleCorrect = SDL_CreateTextureFromSurface(renderer, surfTitle);
        if (texTitleCorrect) {
            if (renderY + titleRect.h > debuggerPanelRect.y && renderY < debuggerPanelRect.y + debuggerPanelRect.h) {
                SDL_FRect actualTitleRect = {titleRect.x, renderY, titleRect.w, titleRect.h};
                SDL_RenderTexture(renderer, texTitleCorrect, nullptr, &actualTitleRect);
            }
            SDL_DestroyTexture(texTitleCorrect);
        }
        SDL_DestroySurface(surfTitle);
        contentLayoutY += titleRect.h + lineSpacing;
    }

    std::lock_guard<std::recursive_mutex> engine_lock(m_engineDataMutex);

    for (const auto &entityPair: entities) {
        if (!entityPair.second) continue;
        Entity *entity = entityPair.second.get();
        std::string truncatedEName = truncate_str_len(entity->getName(), 20);
        std::string entityDisplayName = "Entity: " + entity->getId() + " (" + truncatedEName + ")";

        SDL_Surface *surfEntityName = TTF_RenderText_Blended(hudFont, entityDisplayName.c_str(), 0, textColor);
        if (surfEntityName) {
            SDL_FRect entityNameRect = {
                debuggerPanelRect.x + 10.0f, 0, static_cast<float>(surfEntityName->w),
                static_cast<float>(surfEntityName->h)
            };
            float renderY = contentLayoutY - m_debuggerScrollOffsetY;
            SDL_Texture *texEntityName = SDL_CreateTextureFromSurface(renderer, surfEntityName);
            if (texEntityName) {
                if (renderY + entityNameRect.h > debuggerPanelRect.y && renderY < debuggerPanelRect.y +
                    debuggerPanelRect.h) {
                    SDL_FRect actualEntityNameRect = {entityNameRect.x, renderY, entityNameRect.w, entityNameRect.h};
                    SDL_RenderTexture(renderer, texEntityName, nullptr, &actualEntityNameRect);
                }
                SDL_DestroyTexture(texEntityName);
            }
            SDL_DestroySurface(surfEntityName);
            contentLayoutY += entityNameRect.h + 5.0f;
        }

        std::lock_guard<std::recursive_mutex> entity_state_lock(entity->getStateMutex());
        if (entity->scriptThreadStates.empty()) {
            SDL_Surface *surfNoThreads = TTF_RenderText_Blended(hudFont, "  (No active script threads)", 0, textColor);
            if (surfNoThreads) {
                SDL_FRect noThreadsRect = {
                    debuggerPanelRect.x + indent, 0, static_cast<float>(surfNoThreads->w),
                    static_cast<float>(surfNoThreads->h)
                };
                float renderY = contentLayoutY - m_debuggerScrollOffsetY;
                SDL_Texture *texNoThreads = SDL_CreateTextureFromSurface(renderer, surfNoThreads);
                if (texNoThreads) {
                    if (renderY + noThreadsRect.h > debuggerPanelRect.y && renderY < debuggerPanelRect.y +
                        debuggerPanelRect.h) {
                        SDL_FRect actualNoThreadsRect = {noThreadsRect.x, renderY, noThreadsRect.w, noThreadsRect.h};
                        SDL_RenderTexture(renderer, texNoThreads, nullptr, &actualNoThreadsRect);
                    }
                    SDL_DestroyTexture(texNoThreads);
                }
                SDL_DestroySurface(surfNoThreads);
                contentLayoutY += noThreadsRect.h + lineSpacing;
            }
        }

        for (const auto &threadPair: entity->scriptThreadStates) {
            const std::string &threadId = threadPair.first;
            const Entity::ScriptThreadState &state = threadPair.second;
            std::string info = "  Thread: " + truncate_str_len(threadId, 25); // 스레드 ID 길이 제한
            info += " | Waiting: " + std::string(state.isWaiting ? "Yes" : "No");
            info += " | Type: " + BlockTypeEnumToString(state.currentWaitType);

            if (state.isWaiting && !state.blockIdForWait.empty()) {
                info += " | Block: " + truncate_str_len(state.blockIdForWait, 15);
            }
            // ResumingAt 정보 추가 (유효성 검사 강화)
            if (state.resumeAtBlockIndex != -1 && state.scriptPtrForResume) {
                // scriptPtrForResume 유효성 검사
                if (state.resumeAtBlockIndex >= 0 && static_cast<size_t>(state.resumeAtBlockIndex) < state.
                    scriptPtrForResume->blocks.size()) {
                    info += " | ResumingAt: " + truncate_str_len(
                                state.scriptPtrForResume->blocks[state.resumeAtBlockIndex].id, 15) +
                            " (idx " + std::to_string(state.resumeAtBlockIndex) + ")";
                } else {
                    info += " | ResumingAt: Invalid Index (" + std::to_string(state.resumeAtBlockIndex) + ")";
                }
            } else if (state.resumeAtBlockIndex != -1) {
                // scriptPtrForResume이 null인 경우
                info += " | ResumingAt: (null script ptr, idx " + std::to_string(state.resumeAtBlockIndex) + ")";
                // EngineStdOut 로그는 const 함수 내에서 직접 호출 불가, 필요시 멤버 함수로 분리 또는 const 제거
            }

            if (!state.terminateRequested) {
                SDL_Surface *surf = TTF_RenderText_Blended_Wrapped(hudFont, info.c_str(), 0, textColor,
                                                                   static_cast<Uint32>(
                                                                       debuggerPanelRect.w - indent - 20.0f));
                if (surf) {
                    SDL_FRect dstRect = {
                        debuggerPanelRect.x + indent, 0, static_cast<float>(surf->w), static_cast<float>(surf->h)
                    };
                    float renderY = contentLayoutY - m_debuggerScrollOffsetY;
                    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                    if (tex) {
                        if (renderY + dstRect.h > debuggerPanelRect.y && renderY < debuggerPanelRect.y +
                            debuggerPanelRect.h) {
                            SDL_FRect actualDstRect = {dstRect.x, renderY, dstRect.w, dstRect.h};
                            SDL_RenderTexture(renderer, tex, nullptr, &actualDstRect);
                        }
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(surf);
                    contentLayoutY += dstRect.h + 2.0f;
                }

                if (!state.loopCounters.empty()) {
                    std::string loopInfoStr = "    Loop Counters: ";
                    int counter = 0;
                    for (const auto &loopPair: state.loopCounters) {
                        if (counter++ > 3) {
                            // 너무 많은 루프 카운터 표시 방지 (예: 최대 4개)
                            loopInfoStr += "...";
                            break;
                        }
                        loopInfoStr += "[" + truncate_str_len(loopPair.first, 10) + ":" + std::to_string(
                            loopPair.second) + "] ";
                    }
                    SDL_Surface *surfLoop = TTF_RenderText_Blended_Wrapped(
                        hudFont, loopInfoStr.c_str(), 0, textColor,
                        static_cast<Uint32>(debuggerPanelRect.w - indent - 30.0f));
                    if (surfLoop) {
                        SDL_FRect loopRect = {
                            debuggerPanelRect.x + indent + 10.0f, 0, static_cast<float>(surfLoop->w),
                            static_cast<float>(surfLoop->h)
                        };
                        float renderY = contentLayoutY - m_debuggerScrollOffsetY;
                        SDL_Texture *texLoop = SDL_CreateTextureFromSurface(renderer, surfLoop);
                        if (texLoop) {
                            if (renderY + loopRect.h > debuggerPanelRect.y && renderY < debuggerPanelRect.y +
                                debuggerPanelRect.h) {
                                SDL_FRect actualLoopRect = {loopRect.x, renderY, loopRect.w, loopRect.h};
                                SDL_RenderTexture(renderer, texLoop, nullptr, &actualLoopRect);
                            }
                            SDL_DestroyTexture(texLoop);
                        }
                        SDL_DestroySurface(surfLoop);
                        contentLayoutY += loopRect.h + 2.0f;
                    }
                }
            }
        }
        contentLayoutY += lineSpacing / 2.0f;
    }

    float totalCalculatedContentHeight = contentLayoutY - initialContentLayoutY + 10.0f; // 마지막 패딩 추가
    float visibleHeight = debuggerPanelRect.h;

    // 스크롤 오프셋 제한
    if (totalCalculatedContentHeight <= visibleHeight) {
        m_debuggerScrollOffsetY = 0.0f;
    } else {
        if (m_debuggerScrollOffsetY > totalCalculatedContentHeight - visibleHeight) {
            m_debuggerScrollOffsetY = totalCalculatedContentHeight - visibleHeight;
        }
        // m_debuggerScrollOffsetY < 0 은 processInput에서 이미 처리됨
    }

    // 스크롤바 그리기
    if (totalCalculatedContentHeight > visibleHeight) {
        float scrollbarWidth = 10.0f;
        float scrollbarPadding = 2.0f;

        SDL_FRect scrollbarTrackRect = {
            debuggerPanelRect.x + debuggerPanelRect.w - scrollbarWidth - scrollbarPadding,
            debuggerPanelRect.y + scrollbarPadding,
            scrollbarWidth,
            visibleHeight - 2 * scrollbarPadding
        };
        SDL_SetRenderDrawColor(renderer, 70, 70, 90, 200); // 트랙 색상
        SDL_RenderFillRect(renderer, &scrollbarTrackRect);

        float handleHeightRatio = visibleHeight / totalCalculatedContentHeight;
        float scrollbarHandleHeight = max(20.0f, scrollbarTrackRect.h * handleHeightRatio);

        float scrollableContentRange = totalCalculatedContentHeight - visibleHeight;
        float scrollbarTrackScrollableRange = scrollbarTrackRect.h - scrollbarHandleHeight;

        float handleYOffset = 0.0f;
        if (scrollableContentRange > 0.001f) {
            // 0으로 나누기 방지
            handleYOffset = (m_debuggerScrollOffsetY / scrollableContentRange) * scrollbarTrackScrollableRange;
        }

        SDL_FRect scrollbarHandleRect = {
            scrollbarTrackRect.x,
            scrollbarTrackRect.y + handleYOffset,
            scrollbarWidth,
            scrollbarHandleHeight
        };
        SDL_SetRenderDrawColor(renderer, 160, 160, 180, 220); // 핸들 색상
        SDL_RenderFillRect(renderer, &scrollbarHandleRect);
    }
}

void Engine::updateEntityTextEffect(const std::string &entityId, const std::string &effect, bool setOn) {
    std::lock_guard<std::recursive_mutex> lock(m_engineDataMutex); // ObjectInfo 접근 보호
    for (auto &objInfo: objects_in_order) {
        if (objInfo.id == entityId && objInfo.objectType == "textBox") {
            if (effect == "strike") {
                objInfo.Strike = setOn;
            } else if (effect == "underLine") {
                objInfo.Underline = setOn;
            } else if (effect == "fontItalic") {
                objInfo.Italic = setOn;
            } else if (effect == "fontBold") {
                objInfo.Bold = setOn;
            } else {
                EngineStdOut("Unknown text effect: " + effect + " for entity " + entityId, 1);
                return;
            }
            EngineStdOut("TextBox " + entityId + " effect '" + effect + "' set to " + (setOn ? "ON" : "OFF"), 3);
            return;
        }
    }
    EngineStdOut("TextBox entity " + entityId + " not found for text_change_effect.", 1);
}
