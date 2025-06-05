#include "BlockExecutor.h"
#include "util/AEhelper.h"
#include "../Engine.h"
#include "../Entity.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <regex>  // regex 헤더 추가
#include <random> // For std::mt19937 and std::uniform_real_distribution
#include <future>
#include <algorithm> // For std::clamp
#include <format>    // For std::format

// ...existing code...

AudioEngineHelper aeHelper; // 전역 AudioEngineHelper 인스턴스

// ThreadPool 클래스 정의는 BlockExecutor.h로 이동

// Helper function to check if a string can be parsed as a number
bool is_number(const std::string &s)
{
    std::string str = s;
    trim(str);
    if (str.empty())
        return false;
    try
    {
        size_t pos;
        std::stod(str, &pos); // Use trimmed string
        // Check if the entire string was consumed by stod and it's not just a prefix
        return pos == str.length();
    }
    catch (const std::invalid_argument &)
    {
        return false;
    }
    catch (const std::out_of_range &)
    {
        return false;
    }
}

// ThreadPool 생성자 구현
ThreadPool::ThreadPool(Engine &eng, size_t threads, size_t maxQueueSize)
    : engine(eng), stop(false), max_queue_size(maxQueueSize)
{
    engine.EngineStdOut("ThreadPool initalize.", 3);

    for (size_t i = 0; i < threads; ++i)
    {
        workers.emplace_back([this, i]
                             {
            engine.EngineStdOut(std::format("{} WorkerThread Started.", i), 3); // 이 로그는 EngineStdOut의 스레드 안전성에 따라 주의

            while(true) {
                std::function<void()> task_fn; // 변수 이름 변경 (task -> task_fn)
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });

                    if(this->stop && this->tasks.empty()) {
                        engine.EngineStdOut(std::format("{} Ended.", i), 3);
                        return;
                    }

                    task_fn = std::move(this->tasks.top().second);
                    this->tasks.pop();
                    this->queue_space_available.notify_one();
                }

                try {
                    task_fn();
                } catch (const std::exception& e) {
                    engine.EngineStdOut(std::format("{} WorkerThread Exception: {}", i, e.what()), 3);
                }
            } });
        engine.EngineStdOut(std::format("{}WorkerThread Created.", i), 3);
    }
    engine.EngineStdOut("ThreadPool initalized.", 0);
}

// ...rest of the file...

// OperandValue 생성자 및 멤버 함수 구현 (BlockExecutor.h에 선언됨)
OperandValue::OperandValue() : type(Type::EMPTY), boolean_val(false), number_val(0.0)
{
}

OperandValue::OperandValue(double val) : type(Type::NUMBER), number_val(val), boolean_val(false)
{
}

OperandValue::OperandValue(const string &val) : type(Type::STRING), string_val(val), boolean_val(false),
                                                number_val(0.0)
{
}

OperandValue::OperandValue(bool val) : type(Type::BOOLEAN), boolean_val(val), number_val(0.0)
{
}

PublicVariable publicVariable; // 전역 PublicVariable 인스턴스
double OperandValue::asNumber() const
{
    if (type == Type::NUMBER)
        return number_val;
    if (type == Type::STRING)
    {
        const std::regex integer_regex("^[-+]?\\d+$");
        const std::regex double_regex(R"(^[-+]?(\d*\.?\d+|\d+\.?)\s*([eE][-+]?\d+)?$)");

        std::string temp_str = string_val; // Create a mutable copy
        trim(temp_str);                    // Trim whitespace
        if (temp_str.empty())
            return 0.0; // Empty string after trim is 0.0

        std::size_t pos = 0;
        try
        {
            if (std::regex_match(temp_str, double_regex))
            {
                double double_val = std::stod(temp_str, &pos);
                if (pos == temp_str.length())
                {
                    return double_val;
                }
                else
                {
                    throw std::invalid_argument("String contains non-numeric characters after number.");
                }
            }
        }
        catch (const std::invalid_argument &)
        {
            // double 변환 실패 시 int로 변환 시도
            try
            {
                if (std::regex_match(temp_str, integer_regex))
                {
                    std::size_t int_pos = 0;
                    int int_val = std::stoi(temp_str, &int_pos);

                    if (int_pos == temp_str.length())
                    {
                        return static_cast<double>(int_val);
                    }
                    else
                    {
                        throw std::invalid_argument("String cannot be converted to a valid integer.");
                    }
                }
            }
            catch (const std::invalid_argument &)
            {
                // int 변환도 실패
                throw std::runtime_error("Invalid number format.");
            }
            catch (const std::out_of_range &)
            {
                // int 범위 초과
                throw std::runtime_error("Number out of int range.");
            }
        }
        catch (const std::out_of_range &)
        {
            // double 범위 초과
            throw std::runtime_error("Number out of double range.");
        }
    }
    return 0.0; // Default for non-convertible types or errors
}

string OperandValue::asString() const
{
    if (type == Type::STRING)
        return string_val;
    if (type == Type::NUMBER)
    {
        if (isnan(number_val))
            return "NaN";
        if (isinf(number_val))
            return (number_val > 0 ? "Infinity" : "-Infinity");

        string s = to_string(number_val);
        // Remove trailing zeros and decimal point if it's the last char
        s.erase(s.find_last_not_of('0') + 1, string::npos);
        if (!s.empty() && s.back() == '.')
        {
            s.pop_back();
        }
        return s;
    }
    if (type == Type::BOOLEAN)
    {
        return boolean_val ? "true" : "false";
    }
    return ""; // Default for EMPTY or unhandled types
}

bool OperandValue::asBool() const
{
    if (type == Type::BOOLEAN)
        return boolean_val;
    if (type == Type::NUMBER)
        return number_val != 0.0; // 0 is false, non-zero is true
    if (type == Type::STRING)
    {
        // Mimic JavaScript-like truthiness:
        // Empty string is false.
        // "false" (case-insensitive) is false.
        // "0" is false.
        // Otherwise, true.
        string lower_str = string_val;
        ranges::transform(lower_str, lower_str.begin(), ::tolower);
        return !string_val.empty() && lower_str != "false" && string_val != "0";
    }
    // EMPTY or unhandled types are false
    return false;
}

// processVariableBlock 선언이 누락된 것 같아 추가 (필요하다면)
// OperandValue processVariableBlock(Engine &engine, const string &objectId, const Block &block);

OperandValue getOperandValue(Engine &engine, const string &objectId, const nlohmann::json &paramField, const string &executionThreadId)
{
    // paramField 내용을 로깅하여 문제 파악
    string paramField_summary = "Unknown Type";
    if (paramField.is_null())
    {
        paramField_summary = "Null";
    }
    else if (paramField.is_string())
    {
        string str_val = paramField.get<string>();
        if (str_val.length() > 50)
        { // 너무 긴 문자열은 자르기
            paramField_summary = "String (len=" + to_string(str_val.length()) + "): \"" + str_val.substr(0, 47) + "...\"";
        }
        else
        {
            paramField_summary = "String: \"" + str_val + "\"";
        }
    }
    else if (paramField.is_number())
    {
        paramField_summary = "Number: " + to_string(paramField.get<double>());
    }
    else if (paramField.is_boolean())
    {
        paramField_summary = "Boolean: " + string(paramField.get<bool>() ? "true" : "false");
    }
    else if (paramField.is_object())
    {
        if (paramField.contains("type") && paramField["type"].is_string())
        {
            paramField_summary = "Object (block type: " + paramField["type"].get<string>() + ")";
        }
        else
        {
            paramField_summary = "Object (structure unknown or missing 'type' field)";
        }
    }
    else if (paramField.is_array())
    {
        paramField_summary = "Array (size: " + to_string(paramField.size()) + ")";
    }
    // 상세 로깅으로 인한 성능 저하 가능성이 있으므로, 디버그 시에만 활성화하거나 로그 레벨 조정
    // 예: if (engine.specialConfig.enableVerboseLogging) engine.EngineStdOut(...);
    // engine.EngineStdOut("getOperandValue (obj: " + objectId + ", thread: " + executionThreadId + ") processing paramField - Summary: " + paramField_summary + ", RawType: " + paramField.type_name(), 3, executionThreadId);

    // 가장 먼저 paramField가 Null인지 확인합니다. Null이면 대부분의 is_object(), is_string() 등에서 문제를 일으킬 수 있습니다.
    if (paramField.is_null())
    {
        engine.EngineStdOut("getOperandValue received a Null paramField for object " + objectId + ". Returning empty OperandValue.", 1, executionThreadId);
        return {}; // Null 값은 빈 OperandValue로 처리
    }

    if (paramField.is_string())
    {
        string str_val_for_op = paramField.get<string>();
        return OperandValue({str_val_for_op});
    }
    else if (paramField.is_object()) // paramField가 Null이 아님을 위에서 확인했으므로 is_object() 호출이 좀 더 안전해집니다.
    {
        // 추가 디버깅: 객체의 모든 키가 문자열인지 확인
        // nlohmann::json object keys are always strings, so this check is less critical
        // but can be kept for robustness if paramField might not be a valid JSON object structure
        // from an external source before parsing.
        // For nlohmann::json, iterating items():
        // for (auto& [key, value] : paramField.items()) { /* key is string */ }

        if (!paramField.contains("type") || !paramField["type"].is_string())
        {
            engine.EngineStdOut(std::format("Parameter field is object but missing 'type' for object {}", objectId), 2);
            return nan("");
        }
        string fieldType = paramField["type"].get<string>();

        if (fieldType == "number" || fieldType == "text_reporter_number")
        {
            if (paramField.contains("params") && paramField["params"].is_array() &&
                !paramField["params"].empty())
            {
                const auto& param_zero = paramField["params"][0];
                if (param_zero.is_string())
                {
                    std::string num_str_param = param_zero.get<std::string>();
                    OperandValue temp_op_val(num_str_param); // 문자열로부터 OperandValue 생성
                    double numeric_value = temp_op_val.asNumber(); // OperandValue::asNumber()는 내부에 숫자 변환 로직을 포함하며, 실패 시 0.0 반환
                    return OperandValue({numeric_value});
                }
                else if (param_zero.is_number())
                {
                    // 파라미터가 직접 숫자인 경우 해당 값을 사용
                    return OperandValue({param_zero.get<double>()});
                }
            }
            engine.EngineStdOut("Invalid 'number' or 'text_reporter_number' block structure in parameter field for " + objectId + ". Expected params[0] to be a string or number.", 1, executionThreadId);
            return {0.0};
        }
        else if (fieldType == "text" || fieldType == "text_reporter_string")
        {
            if (paramField.contains("params") && paramField["params"].is_array() &&
                !paramField["params"].empty() && paramField["params"][0].is_string())
            {
                return OperandValue({paramField["params"][0].get<string>()});
            }
            engine.EngineStdOut("Invalid 'text' or 'text_reporter_string' block structure in parameter field for " + objectId + ". Expected params[0] to be a string.", 1, executionThreadId);
            return {""}; // 문자열 타입이므로 빈 문자열 반환
        }
        else if (fieldType == "calc_basic" || fieldType == "calc_rand" || fieldType == "quotient_and_mod" || fieldType == "calc_operation" ||
                 fieldType == "distance_something" || fieldType == "length_of_string" || fieldType == "reverse_of_string" ||
                 fieldType == "combine_something" || fieldType == "char_at" || fieldType == "substring" ||
                 fieldType == "count_match_string" || fieldType == "index_of_string" || fieldType == "replace_string" ||
                 fieldType == "change_string_case" || fieldType == "get_block_count" || fieldType == "change_rgb_to_hex" ||
                 fieldType == "change_hex_to_rgb" || fieldType == "get_boolean_value" || fieldType == "get_project_timer_value" ||
                 fieldType == "get_date" || fieldType == "get_user_name" || fieldType == "get_nickname" ||
                 fieldType == "get_sound_volume" || fieldType == "get_sound_speed" || fieldType == "get_sound_duration" ||
                 fieldType == "get_canvas_input_value" || fieldType == "length_of_list" || fieldType == "is_included_in_list" ||
                 fieldType == "coordinate_mouse" || fieldType == "coordinate_object" || fieldType == "get_variable" ||
                 fieldType == "reach_something" ||
                 fieldType == "is_type" ||
                 fieldType == "boolean_basic_operator" ||
                 fieldType == "boolean_and_or" ||         // 추가
                 fieldType == "boolean_not" ||            // 추가
                 fieldType == "is_boost_mode" ||          // 추가
                 fieldType == "is_current_device_type" || // 추가: 사용자가 보고한 문제 해결
                 fieldType == "is_touch_supported" ||     // 추가
                 fieldType == "text_read" ||              // 추가
                 fieldType == "is_clicked" || fieldType == "is_object_clicked" || fieldType == "is_press_some_key" ||
                 fieldType == "value_of_index_from_list")
        {
            Block subBlock;
            subBlock.type = fieldType;
            if (paramField.contains("id") && paramField["id"].is_string())
                subBlock.id = paramField["id"].get<string>();
            // paramsJson은 Document 타입이므로 CopyFrom 사용
            // engine.m_blockParamsAllocatorDoc.GetAllocator()를 사용하여 할당

            // Ensure "params" exists and is an array before attempting to copy
            if (paramField.contains("params"))
            {
                const auto &paramsMember = paramField["params"];
                if (paramsMember.is_array())
                {
                    // subBlock.paramsJson의 자체 할당자를 사용하도록 변경
                    subBlock.paramsJson = paramsMember;      // Direct assignment for nlohmann::json
                    subBlock.FilterNullsInParamsJsonArray(); // subBlock의 paramsJson에 대해서도 null 제거
                }
                else
                {
                    engine.EngineStdOut("Error: 'params' member in paramField for block type " + fieldType + " (object " + objectId + ") is not an array. Actual type: " + paramsMember.type_name(), 2, executionThreadId);
                    // Handle error: return an empty/error OperandValue or throw
                    return nan("");
                }
            }
            // Route to the main Calculator function, passing an empty string for executionThreadId as it's not available here.
            return Calculator(fieldType, engine, objectId, subBlock, executionThreadId);
        }
        else if (fieldType == "text_color") // Added handler for text_color
        {
            // text_color 블록은 params[0]에 있는 HEX 색상 문자열을 반환합니다.
            if (paramField.contains("params") && paramField["params"].is_array() &&
                !paramField["params"].empty() && paramField["params"][0].is_string())
            {
                return OperandValue(paramField["params"][0].get<std::string>());
            }
            engine.EngineStdOut(
                "Invalid 'text_color' block structure in parameter field for " + objectId +
                    ". Expected params[0] to be a HEX string.",
                1, executionThreadId);
            return OperandValue("#000000"); // 기본값 또는 오류 처리
        }
        else if (fieldType == "get_pictures")
        {
            // get_pictures 블록의 params[0]은 실제 모양 ID 문자열입니다.
            if (paramField.contains("params") && paramField["params"].is_array() &&
                !paramField["params"].empty() && paramField["params"][0].is_string())
            {
                string temp_image_id = paramField["params"][0].get<string>();
                // temp_image_id로 안전하게 복사된 문자열을 사용하여 OperandValue 직접 반환
                return OperandValue(temp_image_id);
            }
            engine.EngineStdOut(
                "Invalid 'get_pictures' block structure in parameter field for " + objectId +
                    ". Expected params[0] to be a string (costume ID).",
                1);
            return OperandValue(""); // 또는 오류를 나타내는 빈 OperandValue
        }
        else if (fieldType == "get_sounds") // Handle get_sounds block type
        {
            // Similar to get_pictures, params[0] should be the sound ID string
            if (paramField.contains("params") && paramField["params"].is_array() &&
                !paramField["params"].empty() && paramField["params"][0].is_string())
            {
                string tem_sound_id = paramField["params"][0].get<string>();
                return OperandValue(tem_sound_id);
            }
            engine.EngineStdOut(
                "Invalid 'get_sounds' block structure in parameter field for " + objectId +
                    ". Expected params[0] to be a string (sound ID).",
                1);
            return OperandValue(""); // Return empty string or handle error appropriately
        }

        engine.EngineStdOut("Unsupported block type in parameter: " + fieldType + " for " + objectId, 1);
        return OperandValue();
    }
    // NEW: Check for other literal types before the final default
    else if (paramField.is_number()) // If the param is a direct number literal
    {
        double getNumber = paramField.get<double>();
        return OperandValue(getNumber); // Returns NUMBER
    }
    else if (paramField.is_boolean()) // If the param is a direct boolean literal
    {
        bool val = paramField.get<bool>();
        engine.EngineStdOut(
            "Parameter field for " + objectId + " is a direct boolean literal: " + (val ? "true" : "false") +
                ". This might be unexpected if a block (e.g., get_pictures) was intended.",
            1);
        return OperandValue(val); // Returns BOOLEAN
    }
    else if (paramField.is_null()) // If the param is a direct null literal
    {
        engine.EngineStdOut("Parameter field is null for " + objectId, 1);
        return OperandValue(); // Returns EMPTY
    }

    // Fallback if not string, object, number, bool, or null
    engine.EngineStdOut("Parameter field is not a string, object, number, boolean, or null for " + objectId + ". Actual type: " + paramField.type_name(), 1);
    return OperandValue();
}

/* 여기에 있던 excuteBlock 함수는 Entity.cpp 로 이동*/
/**
 * @brief 움직이기 블록
 *
 */
void Moving(string BlockType, Engine &engine, const string &objectId, const Block &block,
            const string &executionThreadId, float deltaTime) // sceneIdAtDispatch는 이 함수 레벨에서는 직접 사용되지 않음
{
    auto entity = engine.getEntityByIdShared(objectId);
    if (!entity)
    {
        // Moving 함수 내 어떤 블록도 이 objectId에 대해 실행될 수 없으므로 여기서 공통 오류 처리 후 반환합니다.                engine.EngineStdOut(std::format("Moving block execution failed: Entity {} not found.", objectId), 2);
        return;
    }

    if (BlockType == "move_direction")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 2)
        {
            engine.EngineStdOut(
                std::format("move_direction block for object {} has invalid params structure. Expected 2 params.", objectId), 2,
                executionThreadId);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId); // paramField 전달
        OperandValue direction = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        double dist = distance.asNumber();
        double dir = direction.asNumber();
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newX = entity->getX() + dist * cos(dir * SDL_PI_D / 180.0);
        double newY = entity->getY() - dist * sin(dir * SDL_PI_D / 180.0);
        // engine.EngineStdOut("move_direction objectId: " + objectId + " direction: " + to_string(newX) + ", " + to_string(newY), 0, executionThreadId);
        entity->setX(newX);
        entity->setY(newY);
    }
    else if (BlockType == "bounce_wall")
    {
        // entity는 함수 시작 시 이미 검증되었습니다.
        // 이전의 if (!entity) 체크는 불필요합니다.

        // Javascript의 threshold와 유사한 값 (픽셀 충돌이 아니므로 의미가 다를 수 있음)
        // const float collisionThreshold = 0.0f; // 경계 상자 충돌에서는 크게 의미 없을 수 있음

        Entity::RotationMethod method = entity->getRotateMethod();
        double currentRotation = entity->getRotation();
        double currentDirection = entity->getDirection();
        double angle = 0;

        if (method == Entity::RotationMethod::FREE)
        {
            angle = fmod(currentRotation + currentDirection, 360.0);
            if (angle < 0)
                angle += 360.0; // fmod 결과가 음수일 수 있으므로 양수로 조정
        }
        else
        {
            angle = fmod(currentDirection, 360.0);
            if (angle < 0)
                angle += 360.0;
        }

        // 엔티티의 현재 위치와 크기
        double entityX = entity->getX();
        double entityY = entity->getY();
        // 엔티티의 실제 화면상 경계를 계산해야 함 (회전 및 등록점 고려)
        // 여기서는 단순화를 위해 엔티티의 x, y를 중심으로 판단
        // 실제로는 entity->getWidth(), entity->getHeight(), entity->getScaleX/Y() 등을 사용한
        // 정확한 경계 상자 계산이 필요합니다.

        // 스테이지 경계 (엔트리 좌표계 기준)
        const double stageLeft = -PROJECT_STAGE_WIDTH / 2.0;
        const double stageRight = PROJECT_STAGE_WIDTH / 2.0;
        const double stageTop = PROJECT_STAGE_HEIGHT / 2.0; // 엔트리는 Y가 위로 갈수록 증가
        const double stageBottom = -PROJECT_STAGE_HEIGHT / 2.0;

        Entity::CollisionSide lastCollision = entity->getLastCollisionSide();
        bool collidedThisFrame = false;

        // 상하 벽 충돌 (Y축)
        // 엔티티가 위쪽으로 움직이는 경향 (angle: 0~90 또는 270~360)
        if ((angle < 90.0 && angle >= 0.0) || (angle < 360.0 && angle >= 270.0))
        {
            // 위쪽 벽 충돌 검사
            if (entityY + (entity->getHeight() * entity->getScaleY() / 2.0) > stageTop && lastCollision !=
                                                                                              Entity::CollisionSide::UP)
            {
                // 단순화된 경계
                if (method == Entity::RotationMethod::FREE)
                {
                    entity->setRotation(fmod(-currentRotation - currentDirection * 2.0 + 180.0, 360.0));
                }
                else
                {
                    entity->setDirection(fmod(-currentDirection + 180.0, 360.0));
                }
                entity->setLastCollisionSide(Entity::CollisionSide::UP);
                collidedThisFrame = true;
                // 엔티티를 벽 안으로 밀어넣는 로직 추가 가능
                // entity->setY(stageTop - (entity->getHeight() * entity->getScaleY() / 2.0));
            }
            // 아래쪽 벽 충돌 검사 (위로 가려다 아래로 튕길 수도 있으므로)
            else if (!collidedThisFrame && entityY - (entity->getHeight() * entity->getScaleY() / 2.0) < stageBottom &&
                     lastCollision != Entity::CollisionSide::DOWN)
            {
                if (method == Entity::RotationMethod::FREE)
                {
                    entity->setRotation(fmod(-currentRotation - currentDirection * 2.0 + 180.0, 360.0));
                }
                else
                {
                    entity->setDirection(fmod(-currentDirection + 180.0, 360.0));
                }
                entity->setLastCollisionSide(Entity::CollisionSide::DOWN);
                collidedThisFrame = true;
            }
        }
        // 엔티티가 아래쪽으로 움직이는 경향 (angle: 90~270)
        else if (angle < 270.0 && angle >= 90.0)
        {
            // 아래쪽 벽 충돌 검사
            if (entityY - (entity->getHeight() * entity->getScaleY() / 2.0) < stageBottom && lastCollision !=
                                                                                                 Entity::CollisionSide::DOWN)
            {
                if (method == Entity::RotationMethod::FREE)
                {
                    entity->setRotation(fmod(-currentRotation - currentDirection * 2.0 + 180.0, 360.0));
                }
                else
                {
                    entity->setDirection(fmod(-currentDirection + 180.0, 360.0));
                }
                entity->setLastCollisionSide(Entity::CollisionSide::DOWN);
                collidedThisFrame = true;
            }
            // 위쪽 벽 충돌 검사
            else if (!collidedThisFrame && entityY + (entity->getHeight() * entity->getScaleY() / 2.0) > stageTop &&
                     lastCollision != Entity::CollisionSide::UP)
            {
                if (method == Entity::RotationMethod::FREE)
                {
                    entity->setRotation(fmod(-currentRotation - currentDirection * 2.0 + 180.0, 360.0));
                }
                else
                {
                    entity->setDirection(fmod(-currentDirection + 180.0, 360.0));
                }
                entity->setLastCollisionSide(Entity::CollisionSide::UP);
                collidedThisFrame = true;
            }
        }

        // 좌우 벽 충돌 (X축) - 상하 벽 충돌이 없었을 경우에만 검사 (또는 동시에 검사 후 우선순위 결정)
        if (!collidedThisFrame)
        {
            // 엔티티가 왼쪽으로 움직이는 경향 (angle: 180~360)
            if (angle < 360.0 && angle >= 180.0)
            {
                // 왼쪽 벽 충돌 검사
                if (entityX - (entity->getWidth() * entity->getScaleX() / 2.0) < stageLeft && lastCollision !=
                                                                                                  Entity::CollisionSide::LEFT)
                {
                    if (method == Entity::RotationMethod::FREE)
                    {
                        entity->setRotation(fmod(-currentRotation - currentDirection * 2.0, 360.0));
                    }
                    else
                    {
                        entity->setDirection(fmod(-currentDirection + 360.0, 360.0)); // JS는 +360, fmod로 처리
                    }
                    entity->setLastCollisionSide(Entity::CollisionSide::LEFT);
                    collidedThisFrame = true;
                }
                // 오른쪽 벽 충돌 검사
                else if (!collidedThisFrame && entityX + (entity->getWidth() * entity->getScaleX() / 2.0) > stageRight && lastCollision != Entity::CollisionSide::RIGHT)
                {
                    if (method == Entity::RotationMethod::FREE)
                    {
                        entity->setRotation(fmod(-currentRotation - currentDirection * 2.0, 360.0));
                    }
                    else
                    {
                        entity->setDirection(fmod(-currentDirection + 360.0, 360.0));
                    }
                    entity->setLastCollisionSide(Entity::CollisionSide::RIGHT);
                    collidedThisFrame = true;
                }
            }
            // 엔티티가 오른쪽으로 움직이는 경향 (angle: 0~180)
            else if (angle < 180.0 && angle >= 0.0)
            {
                // 오른쪽 벽 충돌 검사
                if (entityX + (entity->getWidth() * entity->getScaleX() / 2.0) > stageRight && lastCollision !=
                                                                                                   Entity::CollisionSide::RIGHT)
                {
                    if (method == Entity::RotationMethod::FREE)
                    {
                        entity->setRotation(fmod(-currentRotation - currentDirection * 2.0, 360.0));
                    }
                    else
                    {
                        entity->setDirection(fmod(-currentDirection + 360.0, 360.0));
                    }
                    entity->setLastCollisionSide(Entity::CollisionSide::RIGHT);
                    collidedThisFrame = true;
                }
                // 왼쪽 벽 충돌 검사
                else if (!collidedThisFrame && entityX - (entity->getWidth() * entity->getScaleX() / 2.0) < stageLeft &&
                         lastCollision != Entity::CollisionSide::LEFT)
                {
                    if (method == Entity::RotationMethod::FREE)
                    {
                        entity->setRotation(fmod(-currentRotation - currentDirection * 2.0, 360.0));
                    }
                    else
                    {
                        entity->setDirection(fmod(-currentDirection + 360.0, 360.0));
                    }
                    entity->setLastCollisionSide(Entity::CollisionSide::LEFT);
                    collidedThisFrame = true;
                }
            }
        }

        if (!collidedThisFrame)
        {
            // 이번 프레임에 어떤 벽과도 충돌하지 않았다면, 이전 충돌 상태를 리셋
            entity->setLastCollisionSide(Entity::CollisionSide::NONE);
        }
        // 각도 정규화 (0~360)
        if (entity->getRotation() < 0)
            entity->setRotation(fmod(entity->getRotation(), 360.0) + 360.0);
        else
            entity->setRotation(fmod(entity->getRotation(), 360.0));

        if (entity->getDirection() < 0)
            entity->setDirection(fmod(entity->getDirection(), 360.0) + 360.0);
        else
            entity->setDirection(fmod(entity->getDirection(), 360.0));
    }
    else if (BlockType == "move_x")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 1) // 파라미터 개수 확인 수정 (2개 -> 1개)
        {
            engine.EngineStdOut(
                std::format("move_x block for object {} has invalid params structure. Expected 1 param after filtering.", objectId), 2,
                executionThreadId);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        double dist = distance.asNumber();
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newX = entity->getX() + dist;
        // engine.EngineStdOut("move_x objId: " + objectId + " newX: " + to_string(newX), 0, executionThreadId);
        entity->setX(newX);
    }
    else if (BlockType == "move_y")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 1) // 파라미터 개수 확인 수정 (2개 -> 1개)
        {
            engine.EngineStdOut(
                std::format("move_y block for object {} has invalid params structure. Expected 1 param after filtering.", objectId), 2,
                executionThreadId);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        double dist = distance.asNumber();
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newY = entity->getY() + dist;
        // engine.EngineStdOut("move_y objId: " + objectId + " newY: " + to_string(newY), 0, executionThreadId);
        entity->setY(newY);
    }
    else if (BlockType == "move_xy_time" || BlockType == "locate_xy_time") // 이둘 똑같이 동작하는걸 확인함 왜 이걸 따로뒀는지 이해안감
    {
        // entity는 함수 시작 시 이미 검증되었습니다.
        // 이전의 if (!entity) 체크는 불필요합니다.

        Entity::TimedMoveState &state = entity->timedMoveState;

        if (!state.isActive)
        {
            // 블록 처음 실행 시 초기화
            if (!block.paramsJson.is_array() || block.paramsJson.size() < 3)
            {
                engine.EngineStdOut(
                    std::format("move_xy_time block for {} is missing parameters. Expected TIME, X, Y.", objectId), 2,
                    executionThreadId);
                state.isActive = false; // Ensure it's not accidentally active
                return;
            }

            OperandValue timeOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
            OperandValue xOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
            OperandValue yOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);
            double timeValue = timeOp.asNumber();
            state.targetX = xOp.asNumber();
            state.targetY = yOp.asNumber();

            const double fps = static_cast<double>(engine.getTargetFps()); // 실제 FPS 사용
            state.totalFrames = max(1.0, floor(timeValue * fps));
            state.remainingFrames = state.totalFrames;
            state.isActive = true;

            // 1 프레임 이동이면 즉시 처리
            if (state.remainingFrames <= 1.0 && state.totalFrames <= 1.0)
            {
                // totalFrames도 확인
                entity->setX(state.targetX);
                entity->setY(state.targetY);
                entity->paint.updatePositionAndDraw(entity->getX(), entity->getY());
                entity->brush.updatePositionAndDraw(entity->getX(), entity->getY());
                // engine.EngineStdOut("move_xy_time: " + objectId + " completed in single step. Pos: (" +
                //                         to_string(entity->getX()) + ", " + to_string(entity->getY()) + ")",
                //                     3, executionThreadId);
                state.isActive = false; // 완료
                return;                 // 이 블록 실행 완료
            }
        }

        // 매 프레임 실행 로직 (state.isActive가 true일 때)
        if (state.isActive && state.remainingFrames > 0)
        {
            double currentX = entity->getX();
            double currentY = entity->getY();

            double dX_total = state.targetX - currentX;
            double dY_total = state.targetY - currentY;

            double dX_step = dX_total / state.remainingFrames;
            double dY_step = dY_total / state.remainingFrames;

            entity->setX(currentX + dX_step);
            entity->setY(currentY + dY_step);

            entity->paint.updatePositionAndDraw(entity->getX(), entity->getY());
            entity->brush.updatePositionAndDraw(entity->getX(), entity->getY());

            state.remainingFrames--;

            if (state.remainingFrames <= 0)
            {
                entity->setX(state.targetX); // 최종 위치로 정확히 이동
                entity->setY(state.targetY);
                // engine.EngineStdOut("move_xy_time: " + objectId + " Pos: (" +
                //                         to_string(entity->getX()) + ", " + to_string(entity->getY()) + ")",
                //                     3, executionThreadId);
                state.isActive = false; // 이동 완료
            }
            else
            {
                // 아직 프레임이 남았으므로 BLOCK_INTERNAL 대기 설정
                entity->setScriptWait(executionThreadId, 0, block.id, Entity::WaitType::BLOCK_INTERNAL);
            }
        }
    }
    else if (BlockType == "locate_x")
    {
        OperandValue valueX = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        double x = valueX.asNumber();
        // entity는 함수 시작 시 이미 검증되었습니다.
        // engine.EngineStdOut("locate_x objId: " + objectId + " newX: " + to_string(x), 3, executionThreadId);
        entity->setX(x);
    }
    else if (BlockType == "locate_y")
    {
        OperandValue valueY = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        // entity는 함수 시작 시 이미 검증되었습니다.
        double y = valueY.asNumber();
        // engine.EngineStdOut("locate_y objId: " + objectId + " newX: " + to_string(y), 3, executionThreadId);
        entity->setY(y);
    }
    else if (BlockType == "locate_xy")
    {
        OperandValue valueXOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue valueYOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);

        // Use asNumber() for type coercion. It returns 0.0 for strings that can't be converted.
        double x = valueXOp.asNumber();
        double y = valueYOp.asNumber();

        // The original strict type check is removed.
        // engine.EngineStdOut("locate_xy block for object " + objectId + " is not a number.", 2, executionThreadId);
        // No explicit error here, as 0.0 is a valid coordinate if conversion fails, matching typical Scratch-like behavior.

        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setX(x);
        entity->setY(y);
        // engine.EngineStdOut("locate_xy objId: " + objectId + " newX: " + to_string(x) + " newY: " + to_string(y), 3, executionThreadId);
    }
    else if (BlockType == "locate")
    {
        // 이것은 마우스커서 나 오브젝트를 따라갑니다.
        OperandValue target = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (target.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(std::format("locate block for object {} is not a string.", objectId), 2, executionThreadId);
            return;
        }
        if (target.string_val == "mouse")
        {
            if (engine.isMouseCurrentlyOnStage())
            {
                // entity는 함수 시작 시 이미 검증되었습니다.
                entity->setX(static_cast<double>(engine.getCurrentStageMouseX()));
                entity->setY(static_cast<double>(engine.getCurrentStageMouseY()));
            }
        }
        else
        {
            Entity *targetEntity = engine.getEntityById(target.string_val);
            // entity (현재 객체)는 함수 시작 시 이미 검증되었습니다.
            if (targetEntity)
            {
                entity->setX(targetEntity->getX());
                entity->setY(targetEntity->getY());
            }
            else
            {
                engine.EngineStdOut(
                    "locate block for object " + objectId + ": target entity '" + target.string_val + "' not found.", 2,
                    executionThreadId);
            }
        }
    }
    else if (BlockType == "locate_object_time")
    {
        // locate_object_time 구현
        // entity는 함수 시작 시 이미 검증되었습니다.
        // 이전의 if (!entity) 체크는 불필요합니다.

        Entity::TimedMoveToObjectState &state = entity->timedMoveObjState;

        if (!state.isActive)
        {
            // 블록 처음 실행 시 초기화
            if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
            {
                // time, target 필요                engine.EngineStdOut(
                engine.EngineStdOut(format("locate_object_time block for {} is missing parameters. Expected TIME, TARGET_OBJECT_ID.", objectId),
                                    2, executionThreadId);
                // state.isActive는 false로 유지됩니다.
                return;
            }

            OperandValue timeOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
            OperandValue targetOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);

            if (timeOp.type != OperandValue::Type::NUMBER || targetOp.type != OperandValue::Type::STRING)
            {
                engine.EngineStdOut(
                    std::format("locate_object_time block for {} has invalid parameters. Time should be number, target should be string.", objectId),
                    2, executionThreadId);
                // state.isActive는 false로 유지됩니다.
                return;
            }

            double timeValue = timeOp.asNumber();
            state.targetObjectId = targetOp.asString();
            // 목표 엔티티 유효성 검사는 매 프레임 진행

            const double fps = static_cast<double>(engine.getTargetFps());
            state.totalFrames = max(1.0, floor(timeValue * fps));
            state.remainingFrames = state.totalFrames;
            state.isActive = true;

            // 시간이 0 또는 1프레임 이동이면 즉시 이동하고 완료합니다.
            if (state.totalFrames <= 1.0)
            {
                Entity *targetEntity = engine.getEntityById(state.targetObjectId);
                if (targetEntity)
                {
                    entity->setX(targetEntity->getX());
                    entity->setY(targetEntity->getY());
                    if (entity->paint.isPenDown)
                        entity->paint.updatePositionAndDraw(entity->getX(), entity->getY());
                    if (entity->brush.isPenDown)
                        entity->brush.updatePositionAndDraw(entity->getX(), entity->getY());
                }
                else
                {
                    engine.EngineStdOut(std::format("locate_object_time: target object {} not found for {}.", state.targetObjectId, objectId),
                                        2, executionThreadId);
                }
                state.isActive = false; // 이동 완료, 이 틱에서 블록 실행 완료.
                return;
            }
        }

        // state.isActive가 true (이동 진행 중)이고 남은 프레임이 있을 경우 매 프레임 실행됩니다.
        if (state.isActive && state.remainingFrames > 0)
        {
            Entity *targetEntity = engine.getEntityById(state.targetObjectId);
            if (!targetEntity)
            {
                engine.EngineStdOut(
                    std::format("locate_object_time: target object {} disappeared mid-move for {}.", state.targetObjectId, objectId),
                    2, executionThreadId);
                state.isActive = false;
                return;
            }

            // 목표 위치를 매 프레임 얻어옵니다.
            double targetX = targetEntity->getX();
            double targetY = targetEntity->getY();

            double currentX = entity->getX();
            double currentY = entity->getY();

            // 남은 프레임 동안의 이동 거리를 균등하게 분배하여 이동합니다.
            double dX = (targetX - currentX) / state.remainingFrames;
            double dY = (targetY - currentY) / state.remainingFrames;

            double newX = currentX + dX;
            double newY = currentY + dY;

            entity->setX(newX);
            entity->setY(newY);
            // engine.EngineStdOut("locate_object_time: " + objectId + " moving to" + state.targetObjectId + ". Pos: (" + to_string(newX) + ", " + to_string(newY) + ")", 3, executionThreadId);
            if (entity->paint.isPenDown)
                entity->paint.updatePositionAndDraw(entity->getX(), entity->getY());
            if (entity->brush.isPenDown)
                entity->brush.updatePositionAndDraw(entity->getX(), entity->getY());

            state.remainingFrames--;

            if (state.remainingFrames <= 0)
            {
                // 목표 위치로 정확히 이동시키고, 이동을 완료합니다.
                entity->setX(targetX);
                entity->setY(targetY);
                if (entity->paint.isPenDown)
                    entity->paint.updatePositionAndDraw(entity->getX(), entity->getY());
                if (entity->brush.isPenDown)
                    entity->brush.updatePositionAndDraw(entity->getX(), entity->getY());
                state.isActive = false;
            }
            else
            {
                // 아직 프레임이 남았으므로 BLOCK_INTERNAL 대기 설정
                entity->setScriptWait(executionThreadId, 0, block.id, Entity::WaitType::BLOCK_INTERNAL);
            }
        }
    }
    else if (BlockType == "rotate_relative" || BlockType == "direction_relative")
    {
        OperandValue value = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (value.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("rotate_relative block for object " + objectId + " is not a number.", 2,
                                executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setDirection(value.asNumber() + entity->getDirection());
    }
    else if (BlockType == "rotate_by_time" || BlockType == "direction_relative_duration")
    {
        OperandValue timeValue = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue angleValue = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (timeValue.type != OperandValue::Type::NUMBER || angleValue.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(std::format("rotate_by_time block for object {} has non-number parameters.", objectId), 2,
                                executionThreadId);
            return;
        }
        double time = timeValue.asNumber();
        double angle = angleValue.asNumber();

        // entity는 함수 시작 시 이미 검증되었습니다.
        // 이전의 if (!entity) 체크는 불필요합니다.
        Entity::TimedRotationState &state = entity->timedRotationState;

        if (!state.isActive)
        {
            const double fps = static_cast<double>(engine.getTargetFps());
            state.totalFrames = max(1.0, floor(time * fps));
            state.remainingFrames = state.totalFrames;
            state.dAngle = angle / state.totalFrames;
            state.isActive = true;
        }

        if (state.isActive && state.remainingFrames > 0)
        {
            entity->setRotation(entity->getRotation() + state.dAngle);
            state.remainingFrames--;

            if (state.remainingFrames <= 0)
            {
                state.isActive = false;
            }
            else
            {
                // 아직 프레임이 남았으므로 BLOCK_INTERNAL 대기 설정
                entity->setScriptWait(executionThreadId, 0, block.id, Entity::WaitType::BLOCK_INTERNAL);
            }
        }
    }
    else if (BlockType == "rotate_absolute")
    {
        OperandValue angle = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (angle.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(std::format("rotate_absolute block for object {} is not a number.", objectId), 2,
                                executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setRotation(angle.asNumber());
    }
    else if (BlockType == "direction_absolute")
    {
        OperandValue angle = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (angle.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(std::format("direction_absolute block for object {}is not a number.", objectId), 2,
                                executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setDirection(angle.asNumber());
    }
    else if (BlockType == "see_angle_object")
    {
        OperandValue hasmouse = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (hasmouse.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(std::format("see_angle_object block for object {}is not a string.", objectId), 2,
                                executionThreadId);
            return;
        }
        if (hasmouse.string_val == "mouse")
        {
            if (engine.isMouseCurrentlyOnStage())
            {
                // entity는 함수 시작 시 이미 검증되었습니다.
                entity->setRotation(engine.getCurrentStageMouseAngle(entity->getX(), entity->getY()));
            }
        }
        else
        {
            Entity *targetEntity = engine.getEntityById(hasmouse.string_val);
            // entity (현재 객체)는 함수 시작 시 이미 검증되었습니다.
            if (targetEntity)
            {
                entity->setRotation(engine.getAngle(entity->getX(), entity->getY(), targetEntity->getX(),
                                                    targetEntity->getY()));
            }
            else
            {
                engine.EngineStdOut(std::format("see_angle_object block for object {}: target entity '{}' not found.",
                                                objectId, hasmouse.string_val),
                                    2, executionThreadId);
            }
        }
    }
    else if (BlockType == "move_to_angle")
    {
        OperandValue setAngle = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue setDesnitance = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (setAngle.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_to_angle block for object " + objectId + "is not a number.", 2,
                                executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        double angle = setAngle.asNumber();
        double distance = setDesnitance.asNumber();
        entity->setX(entity->getX() + distance * cos(angle * SDL_PI_D / 180.0));
        entity->setY(entity->getY() + distance * sin(angle * SDL_PI_D / 180.0));
    }
}

/**
 * @brief 계산 블록
 *
 */
OperandValue Calculator(string BlockType, Engine &engine, const string &objectId, const Block &block,
                        const string &executionThreadId)
{
    if (BlockType == "calc_basic")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 3)
        {
            engine.EngineStdOut(
                std::format("calc_basic block for object {} has invalid params structure. Expected 3 params.", objectId), 2,
                executionThreadId);
            return OperandValue();
        }

        OperandValue leftOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue opVal = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue rightOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);

        string anOperator = opVal.asString();

        // EntryJS-like behavior: PLUS can be string concatenation or numeric addition
        if (anOperator == "PLUS")
        {
            // If both can be strictly interpreted as numbers, add them. Otherwise, concatenate as strings.
            // This mimics Scratch/EntryJS behavior where "1" + "2" is 3, but "1" + "a" is "1a".
            bool leftIsNumeric = (leftOp.type == OperandValue::Type::NUMBER || (leftOp.type == OperandValue::Type::STRING && !leftOp.string_val.empty() && is_number(leftOp.string_val)));
            bool rightIsNumeric = (rightOp.type == OperandValue::Type::NUMBER || (rightOp.type == OperandValue::Type::STRING && !rightOp.string_val.empty() && is_number(rightOp.string_val)));

            if (leftIsNumeric && rightIsNumeric)
            {
                return OperandValue(leftOp.asNumber() + rightOp.asNumber());
            }
            else
            {
                return OperandValue(leftOp.asString() + rightOp.asString());
            }
        }

        double numLeft = leftOp.asNumber();
        double numRight = rightOp.asNumber();

        if (anOperator == "MINUS")
            return OperandValue(numLeft - numRight);
        if (anOperator == "MULTI")
            return OperandValue(numLeft * numRight);
        if (anOperator == "DIVIDE")
        {
            if (numRight == 0.0)
            {
                string errMsg = std::format("Division by zero in calc_basic for {}", objectId);
                engine.EngineStdOut(errMsg, 2, executionThreadId);
                throw ScriptBlockExecutionError("0으로 나눌 수 없습니다.", block.id, BlockType, objectId, "Division by zero.");
            }
            return OperandValue(numLeft / numRight);
        }
        engine.EngineStdOut(std::format("Unknown operator in calc_basic: {} for {}", anOperator, objectId), 2, executionThreadId);
        return OperandValue();
    }
    else if (BlockType == "calc_rand")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 2)
        {
            engine.EngineStdOut(
                "calc_rand block for object " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue minVal = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue maxVal = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (minVal.type != OperandValue::Type::NUMBER || maxVal.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("calc_rand block for object " + objectId + " has non-numeric params.", 2,
                                executionThreadId);
            return OperandValue();
        }
        // thread_local을 사용하여 각 스레드가 자신만의 난수 생성기를 갖도록 합니다.
        // std::random_device{}()를 사용하여 각 스레드마다 다른 시드로 초기화합니다.
        thread_local std::mt19937 generator(std::random_device{}());

        double min_val = minVal.asNumber();
        double max_val = maxVal.asNumber();

        if (min_val < max_val)
        {
            std::uniform_real_distribution<double> distribution(min_val, max_val);
            return OperandValue(distribution(generator));
        }
        else if (min_val == max_val)
        {
            return OperandValue(min_val); // 최소값과 최대값이 같으면 해당 값 반환
        }
        else
        { // min_val > max_val
            // EntryJS는 이 경우 min_val을 반환하는 것으로 보입니다.
            // 또는 오류를 발생시키거나, 범위를 교정할 수 있습니다.
            // 여기서는 EntryJS 동작을 따라 min_val을 반환합니다.
            engine.EngineStdOut("calc_rand block for object " + objectId + ": min_val (" + std::to_string(min_val) + ") is greater than max_val (" + std::to_string(max_val) + "). Returning min_val.", 1, executionThreadId);
            return OperandValue(min_val);
        }
    }
    else if (BlockType == "coordinate_mouse")
    {
        // paramsKeyMap: { VALUE: 1 }
        // 드롭다운 값 ("x" 또는 "y")은 null 필터링 후 paramsJson[0]에 있습니다.
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            // 인덱스 0에 접근하려면 크기가 최소 1이어야 함
            engine.EngineStdOut(
                "coordinate_mouse block for " + objectId +
                    " has invalid params structure. Expected param at index 0 for VALUE.",
                2, executionThreadId);
            return OperandValue(0.0);
        }

        OperandValue coordTypeOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        string coord_type_str;

        if (coordTypeOp.type == OperandValue::Type::STRING)
        {
            coord_type_str = coordTypeOp.asString();
        }
        else
        {
            engine.EngineStdOut(
                "coordinate_mouse block for " + objectId + ": VALUE parameter (index 0) is not a string.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }

        if (coord_type_str.empty())
        {
            // getOperandValue가 빈 문자열을 반환했거나, 원래 문자열이 비어있는 경우
            engine.EngineStdOut(
                "coordinate_mouse block for object " + objectId +
                    " has an empty coordinate type string for VALUE parameter.",
                2, executionThreadId);
            return OperandValue(0.0);
        }
        if (engine.isMouseCurrentlyOnStage())
        {
            // 실제 마우스 좌표 가져오기 (이미 스테이지 좌표계로 변환된 값)
            int mouseX = engine.getCurrentStageMouseX();
            int mouseY = engine.getCurrentStageMouseY();

            // 마우스 좌표는 이미 스테이지 좌표계(-240~240, -180~180)에 있음
            double entryX = static_cast<double>(mouseX);
            double entryY = static_cast<double>(mouseY);

            if (coord_type_str == "x" || coord_type_str == "mouseX")
            {
                return OperandValue(entryX);
            }
            else if (coord_type_str == "y" || coord_type_str == "mouseY")
            {
                return OperandValue(entryY);
            }
            else
            {
                engine.EngineStdOut(
                    "coordinate_mouse block for object " + objectId + " has unknown coordinate type: " + coord_type_str,
                    2, executionThreadId);
                return OperandValue(0.0);
            }
        }
        else
        {
            engine.EngineStdOut(
                "Info: coordinate_mouse block - mouse is not on stage. Returning 0.0 for " + coord_type_str +
                    " for object " + objectId,
                0, executionThreadId);
            return OperandValue(0.0);
        }
    }
    else if (BlockType == "coordinate_object")
    {
        // FilterNullsInParamsJsonArray가 null을 제거하므로, 유효한 파라미터는 2개여야 합니다.
        // (원래 params: [null, TARGET_OBJECT_ID, null, COORDINATE_TYPE])
        // 필터링 후: [TARGET_OBJECT_ID_VALUE, COORDINATE_TYPE_VALUE]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            engine.EngineStdOut(
                "coordinate_object block for object " + objectId +
                    " has invalid params structure. Expected at least 2 params after filtering.",
                2, executionThreadId);
            return OperandValue(0.0);
        }

        // 필터링 후 첫 번째 파라미터 (원래 인덱스 1)가 대상 객체 ID입니다.
        OperandValue targetIdOpVal = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);

        if (targetIdOpVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "coordinate_object block for " + objectId + ": target object ID parameter (original index 1) did not resolve to a string. Value: " + targetIdOpVal.asString(), 2,
                executionThreadId);
            return OperandValue(0.0);
        }
        string targetObjectIdStr = targetIdOpVal.asString();

        // 필터링 후 두 번째 파라미터 (원래 인덱스 3)가 좌표 유형입니다.
        OperandValue coordinateTypeOpVal = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (coordinateTypeOpVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "coordinate_object block for " + objectId + ": coordinate type parameter (original index 3) did not resolve to a string. Value: " + coordinateTypeOpVal.asString(), 2, executionThreadId);
            return OperandValue(0.0);
        }
        string coordinateTypeStr = coordinateTypeOpVal.asString();

        Entity *targetEntity = nullptr;
        if (targetObjectIdStr == "self" || targetObjectIdStr.empty())
        {
            targetEntity = engine.getEntityById(objectId);
        }
        else
        {
            targetEntity = engine.getEntityById(targetObjectIdStr);
        }

        if (!targetEntity)
        {
            engine.EngineStdOut(
                "coordinate_object block for " + objectId + ": target entity '" + targetObjectIdStr + "' not found.", 2,
                executionThreadId);
            if (coordinateTypeStr == "picture_name")
                return OperandValue("");
            return OperandValue(0.0);
        }

        if (coordinateTypeStr == "x")
        {
            return OperandValue(targetEntity->getX());
        }
        else if (coordinateTypeStr == "y")
        {
            return OperandValue(targetEntity->getY());
        }
        else if (coordinateTypeStr == "rotation")
        {
            return OperandValue(targetEntity->getRotation());
        }
        else if (coordinateTypeStr == "direction")
        {
            return OperandValue(targetEntity->getDirection());
        }
        else if (coordinateTypeStr == "size")
        {
            // Match EntryJS func: Number(targetEntity.getSize().toFixed(1))
            double sizeVal = round((targetEntity->getScaleX() * 100.0) * 10.0) / 10.0;
            return OperandValue(sizeVal);
        }
        else if (coordinateTypeStr == "picture_index" || coordinateTypeStr == "picture_name")
        {
            const ObjectInfo *targetObjInfo = engine.getObjectInfoById(targetEntity->getId());

            if (!targetObjInfo)
            {
                engine.EngineStdOut(
                    "coordinate_object block for " + objectId + ": could not find ObjectInfo for target entity '" +
                        targetEntity->getId() + "'.",
                    2, executionThreadId);
                if (coordinateTypeStr == "picture_name")
                    return OperandValue("");
                return OperandValue(0.0); // Align with indexOf behavior (0 if not found)
            }

            if (targetObjInfo->costumes.empty())
            {
                engine.EngineStdOut(
                    "coordinate_object block for " + objectId + ": target entity '" + targetEntity->getId() +
                        "' has no costumes.",
                    1, executionThreadId);
                if (coordinateTypeStr == "picture_name")
                    return OperandValue("");
                return OperandValue(0.0);
            }

            const string &selectedCostumeId = targetObjInfo->selectedCostumeId;

            bool found = false;
            size_t found_idx = 0;
            for (size_t i = 0; i < targetObjInfo->costumes.size(); ++i)
            {
                if (targetObjInfo->costumes[i].id == selectedCostumeId)
                {
                    found_idx = i;
                    found = true;
                    break;
                }
            }

            if (found)
            {
                if (coordinateTypeStr == "picture_index")
                {
                    return OperandValue(static_cast<double>(found_idx + 1));
                }
                else // picture_name
                {
                    return OperandValue(targetObjInfo->costumes[found_idx].name);
                }
            }
            else
            {
                engine.EngineStdOut(
                    "coordinate_object - Selected costume ID '" + selectedCostumeId +
                        "' not found in costume list for entity '" + targetEntity->getId() + "'.",
                    1, executionThreadId);
                if (coordinateTypeStr == "picture_index")
                    return OperandValue(0.0); // Not found, return 0 for index
                else
                    return OperandValue(""); // Not found, return empty for name
            }
        }
        else
        {
            engine.EngineStdOut(
                "coordinate_object block for " + objectId + ": unknown coordinate type '" + coordinateTypeStr + "'.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }
    }
    else if (BlockType == "quotient_and_mod")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 3)
        {
            engine.EngineStdOut("quotient_and_mod block for " + objectId + " parameter is invalid. Expected 3 params.",
                                2, executionThreadId);
            throw ScriptBlockExecutionError(
                "몫과 나머지 블록의 파라미터 개수가 올바르지 않습니다.", block.id, BlockType, objectId,
                "Invalid parameter count for quotient_and_mod block. Expected 3 params.");
        }

        OperandValue left_op = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue operator_op = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue right_op = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);
        string anOperator = operator_op.asString();

        double left_val = left_op.asNumber();
        double right_val = right_op.asNumber();

        if (anOperator == "QUOTIENT")
        {
            if (right_val == 0.0)
            {
                engine.EngineStdOut(
                    "Division by zero in quotient_and_mod (QUOTIENT) for " + objectId + ". Returning NaN.", 2,
                    executionThreadId);
                // throw "0으로 나누기 (몫) (은)는 불가능합니다.";
                return OperandValue(nan("")); // NaN 반환
            }
            return OperandValue(floor(left_val / right_val));
        }
        else
        {
            if (right_val == 0.0)
            {
                engine.EngineStdOut("Division by zero in quotient_and_mod (MOD) for " + objectId + ". Returning NaN.",
                                    2, executionThreadId);
                // throw "0으로 나누기 (나머지) (은)는 불가능합니다.";
                return OperandValue(nan("")); // NaN 반환
            }
            return OperandValue(left_val - right_val * floor(left_val / right_val));
        }
    }
    else if (BlockType == "calc_operation")
    {
        // EntryJS: get_value_of_operator
        // switch 문에서 사용할 수학 연산 열거형
        enum class MathOperationType
        {
            ABS,
            FLOOR,
            CEIL,
            ROUND,
            SQRT,
            SIN,
            COS,
            TAN,
            ASIN,
            ACOS,
            ATAN,
            LOG,
            LN, // LOG는 밑이 10인 로그, LN은 자연로그
            UNKNOWN
        };
        // 연산자 문자열을 열거형으로 변환하는 헬퍼 함수
        auto stringToMathOperation = [](const string &opStr) -> MathOperationType
        {
            if (opStr == "abs")
                return MathOperationType::ABS;
            if (opStr == "floor")
                return MathOperationType::FLOOR;
            if (opStr == "ceil")
                return MathOperationType::CEIL;
            if (opStr == "round")
                return MathOperationType::ROUND;
            if (opStr == "sqrt")
                return MathOperationType::SQRT;
            if (opStr == "sin")
                return MathOperationType::SIN;
            if (opStr == "cos")
                return MathOperationType::COS;
            if (opStr == "tan")
                return MathOperationType::TAN;
            if (opStr == "asin")
                return MathOperationType::ASIN;
            if (opStr == "acos")
                return MathOperationType::ACOS;
            if (opStr == "atan")
                return MathOperationType::ATAN;
            if (opStr == "log")
                return MathOperationType::LOG;
            if (opStr == "ln")
                return MathOperationType::LN;
            return MathOperationType::UNKNOWN;
        };

        if (!block.paramsJson.is_array() || block.paramsJson.size() != 2)
        {
            engine.EngineStdOut(
                "calc_operation block for " + objectId + " has invalid params. Expected 2 params (LEFTHAND, OPERATOR).",
                2, executionThreadId);
            return OperandValue(nan(""));
        }

        OperandValue leftOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue opVal = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);

        if (leftOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("calc_operation block for " + objectId + " has non-numeric left operand.", 2,
                                executionThreadId);
            return OperandValue(nan(""));
        }
        if (opVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("calc_operation block for " + objectId + " has non-string operator.", 2,
                                executionThreadId);
            return OperandValue(nan(""));
        }

        double leftNum = leftOp.asNumber();
        string originalOperator = opVal.asString();
        string anOperator = originalOperator;

        // JavaScript 코드: if (operator.indexOf('_')) { operator = operator.split('_')[0]; }
        // C++ 대응 코드:
        // 이 조건은 indexOf('_') 결과가 0이 아닐 때 (즉, > 0 또는 -1일 때) 참입니다.
        // indexOf('_') 결과가 0이면 거짓입니다.
        size_t underscore_pos = anOperator.find('_');
        if (underscore_pos != 0)
        {
            // 찾지 못했거나(npos) 0보다 큰 인덱스에서 찾았을 경우 참
            if (underscore_pos != string::npos)
            {
                // 찾았고, 0번 위치가 아닐 경우
                anOperator = anOperator.substr(0, underscore_pos);
            }
            // 찾지 못했을 경우 (underscore_pos == string::npos), anOperator는 originalOperator로 유지됩니다.
        }
        const double PI_CONST = SDL_PI_D; // SDL PI 사용

        bool inputWasDegrees = false;
        if (originalOperator.length() > 7 && originalOperator.substr(originalOperator.length() - 7) == "_degree")
        {
            if (anOperator == "sin" || anOperator == "cos" || anOperator == "tan")
            {
                leftNum = leftNum * PI_CONST / 180.0;
                inputWasDegrees = true; // 입력이 변환되었음을 표시
            }
        }

        double result = 0.0;
        bool errorOccurred = false;
        string errorMsg = "";

        MathOperationType opType = stringToMathOperation(anOperator);

        switch (opType)
        {
        case MathOperationType::ABS:
            result = fabs(leftNum);
            break;
        case MathOperationType::FLOOR:
            result = floor(leftNum);
            break;
        case MathOperationType::CEIL:
            result = ceil(leftNum);
            break;
        case MathOperationType::ROUND:
            result = round(leftNum);
            break;
        case MathOperationType::SQRT:
            if (leftNum < 0)
            {
                errorOccurred = true;
                errorMsg = "sqrt of negative number";
                result = nan("");
            }
            else
            {
                result = sqrt(leftNum);
            }
            break;
        case MathOperationType::SIN:
            result = sin(leftNum); // leftNum이 라디안이라고 가정
            break;
        case MathOperationType::COS:
            result = cos(leftNum); // leftNum이 라디안이라고 가정
            break;
        case MathOperationType::TAN:
            result = tan(leftNum); // leftNum이 라디안이라고 가정
            break;
        case MathOperationType::ASIN:
            if (leftNum < -1.0 || leftNum > 1.0)
            {
                errorOccurred = true;
                errorMsg = "asin input out of range [-1, 1]";
                result = nan("");
            }
            else
            {
                result = asin(leftNum); // 라디안 값 반환
            }
            break;
        case MathOperationType::ACOS:
            if (leftNum < -1.0 || leftNum > 1.0)
            {
                errorOccurred = true;
                errorMsg = "acos input out of range [-1, 1]";
                result = nan("");
            }
            else
            {
                result = acos(leftNum); // 라디안 값 반환
            }
            break;
        case MathOperationType::ATAN:
            result = atan(leftNum); // 라디안 값 반환
            break;
        case MathOperationType::LOG: // 밑이 10인 로그
            if (leftNum <= 0)
            {
                errorOccurred = true;
                errorMsg = "log of non-positive number";
                result = nan("");
            }
            else
            {
                result = log10(leftNum);
            }
            break;
        case MathOperationType::LN: // 자연로그
            if (leftNum <= 0)
            {
                errorOccurred = true;
                errorMsg = "ln of non-positive number";
                result = nan("");
            }
            else
            {
                result = log(leftNum);
            }
            break;
        case MathOperationType::UNKNOWN:
        default:
            errorOccurred = true;
            errorMsg = "Unknown operator in calc_operation: " + originalOperator;
            result = nan("");
            break;
        }

        if (errorOccurred)
        {
            engine.EngineStdOut("calc_operation block for " + objectId + ": " + errorMsg, 2, executionThreadId);
            return OperandValue(result); // NaN 반환
        }

        // 원본 연산자가 결과가 각도여야 함을 나타내는 경우 (예: "asin_degree")
        if (originalOperator.length() > 7 && originalOperator.substr(originalOperator.length() - 7) == "_degree")
        {
            if (anOperator == "asin" || anOperator == "acos" || anOperator == "atan")
            {
                result = result * 180.0 / PI_CONST;
            }
        }
        return OperandValue(result);
    }
    else if (BlockType == "get_project_timer_value")
    {
        // 파라미터 없음, 엔진에서 직접 타이머 값을 가져옴
        return OperandValue(engine.getProjectTimerValue());
    } // choose_project_timer_action은 Behavior 함수로 이동했으므로 여기서 제거합니다.
    else if (BlockType == "get_date")
    {
        time_t now = time(nullptr);
        // paramsKeyMap: { VALUE: 0 }
        // 드롭다운 값은 block.paramsJson[0]에 문자열로 저장되어 있을 것으로 예상합니다.
        if (!block.paramsJson.is_array() || block.paramsJson.empty() || !block.paramsJson[0].is_string())
        {
            engine.EngineStdOut("get_date block for " + objectId + " has invalid or missing action parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }

        // 이 블록의 파라미터는 항상 단순 문자열 드롭다운 값이므로 직접 접근합니다.
        OperandValue actionOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        string action = actionOp.asString(); // OperandValue에서 문자열 가져오기
        struct tm timeinfo_s;                // localtime_s 및 localtime_r을 위한 구조체
        struct tm *timeinfo_ptr = nullptr;
#ifdef _WIN32
        localtime_s(&timeinfo_s, &now);
        timeinfo_ptr = &timeinfo_s;
#elif defined(__linux__) || defined(__APPLE__)
        localtime_r(&now, &timeinfo_s);
        timeinfo_ptr = &timeinfo_s;
#endif
        if (action == "YEAR")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_year + 1900)); // 연도는 숫자 그대로 반환
        }
        else if (action == "MONTH")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_mon + 1)); // 월은 숫자 그대로 반환
        }
        else if (action == "DAY")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_mday)); // 일은 숫자 그대로 반환
        }
        else if (action == "HOUR")
        {
            return OperandValue(std::format("{:02d}", timeinfo_ptr->tm_hour)); // 시를 두 자리 문자열로 포맷팅
        }
        else if (action == "MINUTE")
        {
            return OperandValue(std::format("{:02d}", timeinfo_ptr->tm_min)); // 분을 두 자리 문자열로 포맷팅
        }
        else if (action == "SECOND")
        {
            return OperandValue(std::format("{:02d}", timeinfo_ptr->tm_sec)); // 초를 두 자리 문자열로 포맷팅
        }
        else
        {
            engine.EngineStdOut("get_date block for " + objectId + " has unknown action: " + action, 1,
                                executionThreadId);
            return OperandValue();
        }
    }
    else if (BlockType == "distance_something")
    {
        OperandValue targetIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (targetIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("distance_something block for " + objectId + ": target parameter is not a string. Value: " + targetIdOp.asString(), 2, executionThreadId);
            return OperandValue(0.0);
        }
        string targetId = targetIdOp.asString();
        if (targetId == "mouse")
        {
            if (engine.isMouseCurrentlyOnStage())
            {
                double mouseX = engine.getCurrentStageMouseX();
                double mouseY = engine.getCurrentStageMouseY();
                return OperandValue(sqrt(mouseX * mouseX + mouseY * mouseY));
            }
            else
            {
                engine.EngineStdOut("distance_something block for " + objectId + ": mouse is not on stage.", 1,
                                    executionThreadId);
                return OperandValue(0.0);
            }
        }
        else
        {
            Entity *targetEntity = engine.getEntityById(targetId);
            if (!targetEntity)
            {
                engine.EngineStdOut(
                    "distance_something block for " + objectId + ": target entity '" + targetId + "' not found.", 2,
                    executionThreadId);
                return OperandValue(0.0);
            }
            double dx = targetEntity->getX() - engine.getCurrentStageMouseX();
            double dy = targetEntity->getY() - engine.getCurrentStageMouseY();
            return OperandValue(sqrt(dx * dx + dy * dy));
        }
    }
    else if (BlockType == "length_of_string")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 1)
        {
            engine.EngineStdOut(
                "length_of_string block for " + objectId + " has invalid params structure. Expected 1 param.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (strOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("length_of_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        return OperandValue(static_cast<double>(strOp.string_val.length()));
    }
    else if (BlockType == "reverse_of_string")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 1)
        {
            engine.EngineStdOut(
                "reverse_of_string block for " + objectId + " has invalid params structure. Expected 1 param.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (strOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("reverse_of_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        string reversedStr = strOp.asString(); // Ensure it's a string
        reverse(reversedStr.begin(), reversedStr.end());
        return OperandValue(reversedStr);
    }
    else if (BlockType == "combine_something") // Corrected typo
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 2)
        {
            engine.EngineStdOut(
                "combie_something block for " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp1 = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue strOp2 = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        // Always concatenate as strings, as per typical block-based language behavior
        string combinedStr = strOp1.asString() + strOp2.asString();
        return OperandValue(combinedStr);
    }
    else if (BlockType == "char_at")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 2)
        {
            engine.EngineStdOut("char_at block for " + objectId + " has invalid params structure. Expected 2 params.",
                                2, executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (strOp.type != OperandValue::Type::STRING || indexOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("char_at block for " + objectId + " has non-string or non-number parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        int index = static_cast<int>(indexOp.number_val);
        if (index < 0 || index >= static_cast<int>(strOp.string_val.length()))
        {
            engine.EngineStdOut("char_at block for " + objectId + " has index out of range.", 2, executionThreadId);
            return OperandValue();
        }
        return OperandValue(string(1, strOp.string_val[index]));
    }
    else if (BlockType == "substring")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 3)
        {
            engine.EngineStdOut("substring block for " + objectId + " has invalid params structure. Expected 3 params.",
                                2, executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue startOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue endOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);
        if (strOp.type != OperandValue::Type::STRING || startOp.type != OperandValue::Type::NUMBER || endOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("substring block for " + objectId + " has non-string or non-number parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        int startIndex = static_cast<int>(startOp.number_val);
        int endIndex = static_cast<int>(endOp.number_val);
        if (startIndex < 0 || endIndex > static_cast<int>(strOp.string_val.length()) || startIndex > endIndex)
        {
            engine.EngineStdOut("substring block for " + objectId + " has index out of range.", 2, executionThreadId);
            return OperandValue();
        }
        return OperandValue(strOp.string_val.substr(startIndex, endIndex - startIndex));
    }
    else if (BlockType == "count_match_string")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 2)
        {
            engine.EngineStdOut(
                "count_match_string block for " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue subStrOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (strOp.type != OperandValue::Type::STRING || subStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("count_match_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        string str = strOp.string_val;
        string subStr = subStrOp.string_val;
        size_t count = 0;
        size_t pos = str.find(subStr);
        while (pos != string::npos)
        {
            count++;
            pos = str.find(subStr, pos + subStr.length());
        }
        return OperandValue(static_cast<double>(count));
    }
    else if (BlockType == "index_of_string")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 2)
        {
            engine.EngineStdOut(
                "index_of_string block for " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue subStrOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (strOp.type != OperandValue::Type::STRING || subStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("index_of_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        string str = strOp.string_val;
        string subStr = subStrOp.string_val;
        size_t pos = str.find(subStr);
        if (pos != string::npos)
        {
            return OperandValue(static_cast<double>(pos));
        }
        else
        {
            return OperandValue(-1.0); // Not found
        }
    }
    else if (BlockType == "replace_string")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 3)
        {
            engine.EngineStdOut(
                "replace_string block for " + objectId + " has invalid params structure. Expected 3 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue oldStrOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue newStrOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);
        if (strOp.type != OperandValue::Type::STRING || oldStrOp.type != OperandValue::Type::STRING || newStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("replace_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        string str = strOp.string_val;
        string oldStr = oldStrOp.string_val;
        string newStr = newStrOp.string_val;
        size_t pos = str.find(oldStr);
        if (pos != string::npos)
        {
            str.replace(pos, oldStr.length(), newStr);
            return OperandValue(str);
        }
        else
        {
            return OperandValue(str); // Not found, return original string
        }
    }
    else if (BlockType == "change_string_case")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 2)
        {
            engine.EngineStdOut(
                "change_string_case block for " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue caseOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (strOp.type != OperandValue::Type::STRING || caseOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_string_case block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        string str = strOp.string_val;
        string caseType = caseOp.string_val;
        if (caseType == "upper")
        {
            transform(str.begin(), str.end(), str.begin(), ::toupper);
        }
        else if (caseType == "lower")
        {
            transform(str.begin(), str.end(), str.begin(), ::tolower);
        }
        else
        {
            engine.EngineStdOut("change_string_case block for " + objectId + " has unknown case type: " + caseType, 2,
                                executionThreadId);
            return OperandValue();
        }
        return OperandValue(str);
    }
    else if (BlockType == "get_block_count")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 1)
        {
            engine.EngineStdOut(
                "get_block_count block for " + objectId + " has invalid params structure. Expected 1 param (OBJECT).",
                2, executionThreadId);
            return OperandValue(0.0);
        }
        OperandValue objectKeyOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (objectKeyOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "get_block_count block for " + objectId + " OBJECT parameter did not resolve to a string.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }
        string objectKey = objectKeyOp.string_val;

        if (objectKey.empty())
        {
            engine.EngineStdOut("get_block_count block for " + objectId + " received an empty OBJECT key.", 1,
                                executionThreadId);
            return OperandValue(0.0);
        }

        int count = 0;
        if (objectKey.rfind("scene-", 0) == 0)
        {
            // Starts with "scene-"
            string sceneIdToQuery = objectKey.substr(6);
            count = engine.getBlockCountForScene(sceneIdToQuery);
        }
        else if (objectKey == "all")
        {
            count = engine.getTotalBlockCount();
        }
        else if (objectKey == "self")
        {
            // 'objectId' is the ID of the object whose script is currently executing
            count = engine.getBlockCountForObject(objectId);
        }
        else if (objectKey.rfind("object-", 0) == 0)
        {
            // Starts with "object-"
            string targetObjId = objectKey.substr(7);
            count = engine.getBlockCountForObject(targetObjId);
        }
        else
        {
            engine.EngineStdOut("get_block_count block for " + objectId + " has unknown OBJECT key: " + objectKey, 1,
                                executionThreadId);
            return OperandValue(0.0); // Default to 0 for unknown keys
        }
        return OperandValue(static_cast<double>(count));
    }
    else if (BlockType == "change_rgb_to_hex")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 3)
        {
            engine.EngineStdOut(
                "change_rgb_to_hex block for " + objectId + " has invalid params structure. Expected 3 params.", 2,
                executionThreadId);
            return OperandValue("#000000");
        }
        OperandValue redOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue greenOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue blueOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);
        // asNumber()를 사용하여 숫자형이 아닌 입력도 0으로 처리하고, 결과를 0-255 사이로 클램핑합니다.
        // EntryJS는 보통 정수가 아닌 값을 버림(floor/truncate) 처리 후 클램핑합니다. round() 후 clamp()가 좀 더 일반적일 수 있습니다.
        double r_double = redOp.asNumber();
        double g_double = greenOp.asNumber();
        double b_double = blueOp.asNumber();

        // 입력이 엄밀한 숫자가 아니었을 경우 디버깅을 위해 로그를 남길 수 있습니다.
        if (redOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(std::format("change_rgb_to_hex for {}: R param (type {}, val '{}') coereced to {}", objectId, (int)redOp.type, redOp.asString(), r_double), 1, executionThreadId);
        }
        if (greenOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(std::format("change_rgb_to_hex for {}: G param (type {}, val '{}') coereced to {}", objectId, (int)greenOp.type, greenOp.asString(), g_double), 1, executionThreadId);
        }
        if (blueOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(std::format("change_rgb_to_hex for {}: B param (type {}, val '{}') coereced to {}", objectId, (int)blueOp.type, blueOp.asString(), b_double), 1, executionThreadId);
        }

        int red = static_cast<int>(std::clamp(round(r_double), 0.0, 255.0));
        int green = static_cast<int>(std::clamp(round(g_double), 0.0, 255.0));
        int blue = static_cast<int>(std::clamp(round(b_double), 0.0, 255.0));

        std::stringstream hexStream; // #include <sstream>, <iomanip> 필요
        hexStream << "#" << std::setfill('0')
                  << std::setw(2) << std::hex << red
                  << std::setw(2) << std::hex << green
                  << std::setw(2) << std::hex << blue;
        return OperandValue(hexStream.str());
    }
    else if (BlockType == "change_hex_to_rgb")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 1)
        {
            engine.EngineStdOut(
                "change_hex_to_rgb block for " + objectId + " has invalid params structure. Expected 1 param.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }
        OperandValue hexOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (hexOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_hex_to_rgb block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue(0.0);
        }
        string hexStr = hexOp.string_val;
        if (hexStr.length() != 7 || hexStr[0] != '#')
        {
            engine.EngineStdOut("change_hex_to_rgb block for " + objectId + " has invalid hex string: " + hexStr, 2,
                                executionThreadId);
            return OperandValue(0.0);
        }
        try
        {
            int red = stoi(hexStr.substr(1, 2), nullptr, 16);
            int green = stoi(hexStr.substr(3, 2), nullptr, 16);
            int blue = stoi(hexStr.substr(5, 2), nullptr, 16);
            // EntryJS는 RGB 값을 반환할 때 특정 숫자 형식을 사용하지 않고,
            // 각 R, G, B 값을 개별적으로 사용하거나 문자열로 합쳐서 보여주는 경우가 많습니다.
            // 여기서는 단순히 R값을 반환하도록 수정하였으나,
            // 실제 EntryJS 블록의 반환 값 형식에 맞춰 조정해야 합니다.
            // 예를 들어, 문자열 "R,G,B" 또는 JSON 객체 형태로 반환할 수 있습니다.
            // 현재로서는 가장 첫 번째 값인 R을 반환하도록 단순화합니다.
            // 필요하다면 이 부분을 수정하여 원하는 형식으로 반환하세요.
            return OperandValue(static_cast<double>(red)); // 예시: R 값만 반환
        }
        catch (const invalid_argument &ia)
        {
            throw ScriptBlockExecutionError("HEX 문자열을 RGB로 변환 중 유효하지 않은 문자가 포함되어 있습니다.", block.id, BlockType, objectId, "Invalid character in HEX string: " + hexStr + ". Error: " + ia.what());
        }
        catch (const out_of_range &oor)
        {
            throw ScriptBlockExecutionError("HEX 문자열을 RGB로 변환 중 숫자 범위 초과가 발생했습니다.", block.id, BlockType, objectId, "HEX string value out of range: " + hexStr + ". Error: " + oor.what());
        }
    }
    else if (BlockType == "get_boolean_value")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() != 1)
        {
            engine.EngineStdOut(
                "get_boolean_value block for " + objectId + " has invalid params structure. Expected 1 param.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue boolOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (boolOp.type != OperandValue::Type::BOOLEAN)
        {
            engine.EngineStdOut("get_boolean_value block for " + objectId + " has non-boolean parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        return OperandValue(boolOp.boolean_val);
    }
    else if (BlockType == "is_clicked")
    {
        // 이 블록은 현재 프레임에서 스테이지가 클릭되었는지 여부를 반환합니다.
        // block.paramsJson에서 별도의 파라미터를 사용하지 않습니다.
        return OperandValue(engine.getStageWasClickedThisFrame());
    }
    else if (BlockType == "is_object_clicked")
    {
        // 이 블록은 현재 스크립트를 실행 중인 오브젝트(objectId)가
        // 엔진에 마지막으로 눌린 오브젝트 ID와 일치하는지 확인합니다.
        // block.paramsJson에서 별도의 파라미터를 사용하지 않습니다.
        return OperandValue(engine.getPressedObjectId() == objectId);
    }
    else if (BlockType == "is_press_some_key")
    {
        // 파라미터: [KEY_IDENTIFIER_STRING (키 식별자 문자열), null]
        // paramsKeyMap: { VALUE: 0 }
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut("is_press_some_key block for " + objectId + " has invalid or missing params array. Expected key identifier at index 0.", 2, executionThreadId);
            return OperandValue(false); // 오류 시 false 반환
        }

        // 첫 번째 파라미터 (키 식별자 문자열)를 가져옵니다.
        // 이 파라미터는 직접 문자열 값이거나, 문자열을 반환하는 다른 블록일 수 있습니다.
        const nlohmann::json &keyParamValue = block.paramsJson[0];
        OperandValue keyIdentifierOp = getOperandValue(engine, objectId, keyParamValue, executionThreadId);

        if (keyIdentifierOp.type != OperandValue::Type::STRING || keyIdentifierOp.asString().empty())
        {
            engine.EngineStdOut("is_press_some_key block for " + objectId +
                                    ": key identifier parameter did not resolve to a non-empty string. Value: " +
                                    keyIdentifierOp.asString(),
                                2, executionThreadId);
            return OperandValue(false);
        }
        string keyIdentifierStr = keyIdentifierOp.asString();

        SDL_Scancode scancode = engine.mapStringToSDLScancode(keyIdentifierStr);
        return OperandValue(engine.isKeyPressed(scancode));
    }
    else if (BlockType == "choose_project_timer_action")
    {
        // paramsKeyMap: { ACTION: 0 }
        // 드롭다운 값은 block.paramsJson[0]에 문자열로 저장되어 있을 것으로 예상합니다.
        // Block.h에서 paramsJson이 rapidjson::Value 타입이고,
        // 이 값은 loadProject 시점에 engine.m_blockParamsAllocatorDoc를 사용하여 할당됩니다.
        if (!block.paramsJson.is_array() || block.paramsJson.empty() || !block.paramsJson[0].is_string())
        {
            engine.EngineStdOut(
                "choose_project_timer_action block for " + objectId + " has invalid or missing action parameter.", 2,
                executionThreadId);
            return OperandValue();
        }

        // 이 블록의 파라미터는 항상 단순 문자열 드롭다운 값이므로 직접 접근합니다.
        OperandValue actionOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        string action = actionOp.asString();

        if (action == "START")
        {
            engine.EngineStdOut("Project timer STARTED by object " + objectId, 0, executionThreadId);
            engine.startProjectTimer();
        }
        else if (action == "STOP")
        {
            engine.EngineStdOut("Project timer STOPPED by object " + objectId, 0, executionThreadId);
            engine.stopProjectTimer();
        }
        else if (action == "RESET")
        {
            engine.EngineStdOut("Project timer RESET by object " + objectId, 0, executionThreadId);
            engine.resetProjectTimer(); // resetProjectTimer는 값만 0으로 설정합니다.
        }
        return OperandValue();
    }
    else if (BlockType == "set_visible_project_timer")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut(
                "set_visible_project_timer block for " + objectId +
                    " has missing or invalid params array. Defaulting to HIDE.",
                2, executionThreadId);
            engine.showProjectTimer(false); // Default action
            return OperandValue();
        }
        OperandValue actionValue = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);

        if (actionValue.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "set_visible_project_timer parameter for object " + objectId +
                    " did not resolve to a string. Interpreted as: '" + actionValue.string_val + "'",
                1, executionThreadId);
        }

        if (actionValue.string_val == "SHOW")
        {
            engine.showProjectTimer(true);
        }
        else if (actionValue.string_val == "HIDE")
        {
            engine.showProjectTimer(false);
        }
        else
        {
            engine.EngineStdOut(
                "set_visible_project_timer block for " + objectId + " has unknown or non-string action value: '" +
                    actionValue.string_val + "'. Defaulting to HIDE.",
                1, executionThreadId);
            // 기본적으로 숨김 처리 또는 아무것도 안 함
        }
        return OperandValue();
    }
    else if (BlockType == "get_user_name")
    {
        // 네이버 클라우드 플랫폼에서 제공하는 사용자 이름을 가져오는 블록
        // 엔트리쪽 에서 API 를 제공하지 못하기에 플레이스 홀더 로 사용
        return OperandValue(publicVariable.user_id);
    }
    else if (BlockType == "get_nickname")
    {
        // 네이버 클라우드 플랫폼에서 제공하는 사용자 ID를 가져오는 블록
        // 엔트리쪽 에서 API 를 제공하지 못하기에 플레이스 홀더 로 사용
        return OperandValue(publicVariable.user_name);
    }
    else if (BlockType == "get_sound_volume")
    {
        // engine.aeHelper.getGlobalVolume()이 0.0f ~ 1.0f 범위의 float 값을 반환한다고 가정
        double volume = static_cast<double>(engine.aeHelper.getGlobalVolume()) * 100.0; // 백분율로 변환
        return OperandValue(volume);
    }
    else if (BlockType == "get_sound_speed")
    {
        float speed = engine.aeHelper.getGlobalPlaybackSpeed(); // 전역 재생 속도 가져오기 (0.0f ~ N.Nf)
        return OperandValue(static_cast<double>(speed));
    }
    else if (BlockType == "get_sound_duration")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut(
                "get_sound_duration block for " + objectId + " has insufficient parameters. Expected sound ID.", 2,
                executionThreadId);
            return OperandValue(0.0); // 오류 시 기본값 반환
        }
        OperandValue soundIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (soundIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "get_sound_duration block for " + objectId + ": sound ID parameter is not a string. Value: " + soundIdOp.asString(), 2, executionThreadId);
            return OperandValue(0.0);
        }
        string targetSoundId = soundIdOp.asString();

        if (targetSoundId.empty())
        {
            engine.EngineStdOut("get_sound_duration block for " + objectId + ": received an empty sound ID.", 2,
                                executionThreadId);
            return OperandValue(0.0);
        }

        const ObjectInfo *objInfo = engine.getObjectInfoById(objectId);
        if (!objInfo)
        {
            engine.EngineStdOut("get_sound_duration - ObjectInfo not found for entity: " + objectId, 2,
                                executionThreadId);
            return OperandValue(0.0);
        }

        const vector<SoundFile> &soundsVector = objInfo->sounds; // 참조로 받기

        for (const auto &sound : soundsVector)
        {
            if (sound.id == targetSoundId)
            {
                return OperandValue(sound.duration);
                // SoundFile 구조체에 duration이 double로 저장되어 있음 아마도 이거사용하는게 좋을뜻 (엔트리가 캐시해둔 사운드의 길이인듯)
            }
        }

        engine.EngineStdOut(
            "get_sound_duration - Sound ID '" + targetSoundId + "' not found in sound list for entity: " + objectId, 1,
            executionThreadId);
        return OperandValue(0.0); // 해당 ID의 사운드를 찾지 못한 경우
    }
    else if (BlockType == "get_canvas_input_value")
    {
        // 이 블록은 OperandValue를 반환해야 하므로, Variable 함수가 아닌 Calculator 함수에서 처리합니다.
        // Variable 함수는 void 반환형을 가집니다.
        // engine.getLastAnswer()는 가장 최근에 ask_and_wait을 통해 입력된 값을 반환해야 합니다.
        return OperandValue(engine.getLastAnswer());
    }
    else if (BlockType == "get_variable")
    {
        // EntryJS: get_variable
        // params: [VARIABLE_ID_STRING, null, null] (VARIABLE_ID_STRING 는 드롭다운 메뉴 항목이다.)
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut(
                "get_variable block for " + objectId + " has insufficient parameters. Expected VARIABLE_ID.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }

        OperandValue variableIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (variableIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "get_variable block for " + objectId + ": VARIABLE_ID parameter did not resolve to a string. Value: " +
                    variableIdOp.asString(),
                2, executionThreadId);
            return OperandValue(0.0);
        }

        string variableIdToFind = variableIdOp.asString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("get_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return OperandValue(0.0);
        }

        HUDVariableDisplay *targetListPtr = nullptr;
        // 1. Search for a local variable (associated with the current objectId)
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            // Compare with hudVar.id instead of hudVar.id
            if (hudVar.id == variableIdToFind && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break; // Found local, no need to search further
            }
        }

        // 2. If not found locally, search for a global variable
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                // Compare with hudVar.id instead of hudVar.id
                if (hudVar.id == variableIdToFind && hudVar.objectId.empty())
                {
                    targetListPtr = &hudVar;
                    break; // Found global
                }
            }
        }

        if (!targetListPtr)
        {
            engine.EngineStdOut("get_variable block for " + objectId + ": Variable '" + variableIdToFind + "' not found.",
                                1, executionThreadId);
            return OperandValue(0.0);
        }
        else
        {
            if (targetListPtr->isCloud)
            {
                engine.loadCloudVariablesFromJson();
            }
            return OperandValue(targetListPtr->value);
        }
    }
    else if (BlockType == "value_of_index_from_list")
    {
        // params: [LIST_ID_STRING, INDEX_VALUE_OR_BLOCK, null, null]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            engine.EngineStdOut(
                "value_of_index_from_list block for " + objectId +
                    " has insufficient parameters. Expected LIST_ID and INDEX.",
                2, executionThreadId);
            return OperandValue(""); // 데이터는 문자열이므로 오류 시 빈 문자열 반환
        }

        // 1. 리스트 ID 가져오기 (항상 드롭다운 메뉴의 문자열)
        OperandValue listIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (listIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("value_of_index_from_list block for " + objectId + ": LIST_ID parameter is not a string. Value: " + listIdOp.asString(), 2, executionThreadId);
            return OperandValue("");
        }
        string listIdToFind = block.paramsJson[0].get<string>();
        if (listIdToFind.empty())
        {
            engine.EngineStdOut("value_of_index_from_list block for " + objectId + ": received an empty LIST_ID.", 2,
                                executionThreadId);
            return OperandValue("");
        }

        // 2. 리스트 찾기 (로컬 리스트 우선, 없으면 전역 리스트)
        HUDVariableDisplay *targetListPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listIdToFind && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listIdToFind && hudVar.objectId.empty())
                {
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }

        if (!targetListPtr)
        {
            engine.EngineStdOut(
                "value_of_index_from_list block for " + objectId + ": List '" + listIdToFind + "' not found.", 1,
                executionThreadId);
            return OperandValue("");
        }
        else
        {
            if (targetListPtr->isCloud)
            {
                engine.loadCloudVariablesFromJson();
            }
        }

        vector<ListItem> &listArray = targetListPtr->array;
        if (listArray.empty())
        {
            engine.EngineStdOut(
                "value_of_index_from_list block for " + objectId + ": List '" + listIdToFind + "' is empty.", 1,
                executionThreadId);
            return OperandValue("");
        }

        // 3. 인덱스 값 가져오기 및 처리
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        double resolvedIndex_1based = 0.0; // 처리 후 1기반 인덱스

        if (indexOp.type == OperandValue::Type::STRING)
        {
            string indexStr = indexOp.asString();
            if (indexStr == "last")
            {
                resolvedIndex_1based = static_cast<double>(listArray.size());
            }
            else if (indexStr == "random")
            {
                if (listArray.empty())
                {
                    engine.EngineStdOut(
                        "value_of_index_from_list: Cannot get random index from empty list '" + listIdToFind + "'.", 1,
                        executionThreadId);
                    return OperandValue("");
                }
                resolvedIndex_1based = static_cast<double>(1 + (rand() % listArray.size()));
            }
            else
            {
                try
                {
                    size_t idx = 0;
                    resolvedIndex_1based = stod(indexStr, &idx);
                    if (idx != indexStr.length() || !isfinite(resolvedIndex_1based))
                    {
                        engine.EngineStdOut(
                            "value_of_index_from_list: INDEX string '" + indexStr + "' is not a valid number for list '" + listIdToFind + "'.", 1, executionThreadId);
                        return OperandValue("");
                    }
                }
                catch (const exception &)
                {
                    engine.EngineStdOut(
                        "value_of_index_from_list: Could not convert INDEX string '" + indexStr +
                            "' to number for list '" + listIdToFind + "'.",
                        1, executionThreadId);
                    return OperandValue("");
                }
            }
        }
        else if (indexOp.type == OperandValue::Type::NUMBER)
        {
            resolvedIndex_1based = indexOp.asNumber();
            if (!isfinite(resolvedIndex_1based))
            {
                engine.EngineStdOut(
                    "value_of_index_from_list: INDEX is not a finite number for list '" + listIdToFind + "'.", 1,
                    executionThreadId);
                return OperandValue("");
            }
        }
        else
        {
            engine.EngineStdOut(
                "value_of_index_from_list: INDEX parameter for list '" + listIdToFind +
                    "' is not a recognizable type (string or number).",
                1, executionThreadId);
            return OperandValue("");
        }

        // 4. 1기반 인덱스 유효성 검사 및 0기반으로 변환
        if (floor(resolvedIndex_1based) != resolvedIndex_1based)
        {
            // 정수인지 확인
            engine.EngineStdOut(
                "value_of_index_from_list: INDEX '" + to_string(resolvedIndex_1based) +
                    "' is not an integer for list '" + listIdToFind + "'.",
                1, executionThreadId);
            return OperandValue("");
        }

        long long finalIndex_1based = static_cast<long long>(resolvedIndex_1based);

        if (finalIndex_1based < 1 || finalIndex_1based > static_cast<long long>(listArray.size()))
        {
            engine.EngineStdOut(
                "value_of_index_from_list: INDEX " + to_string(finalIndex_1based) + " is out of bounds for list '" + listIdToFind + "' (size: " + to_string(listArray.size()) + ").", 1, executionThreadId);
            return OperandValue("");
        }

        size_t finalIndex_0based = static_cast<size_t>(finalIndex_1based - 1);

        // 5. 데이터 반환
        return OperandValue(listArray[finalIndex_0based].data);
    }
    else if (BlockType == "length_of_list")
    {
        // 리스트의 길이를 반환
        OperandValue listId = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        // params: [LIST_ID_STRING, INDEX_VALUE_OR_BLOCK, null, null]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut(
                "value_of_index_from_list block for " + objectId +
                    " has insufficient parameters. Expected LIST_ID and INDEX.",
                2, executionThreadId);
            return OperandValue(0.0);
        }

        if (listId.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "value_of_index_from_list block for " + objectId + ": LIST_ID parameter is not a string.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }

        HUDVariableDisplay *targetListPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listId.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listId.asString() && hudVar.objectId.empty())
                {
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }
        double itemCount = targetListPtr->array.size();
        return OperandValue(itemCount);
    }
    else if (BlockType == "is_included_in_list")
    {
        // 리스트에 해당 항목이 들어있는지 확인
        OperandValue listId = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue dataOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (listId.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "is_included_in_list block for " + objectId + ": LIST_ID  parameter is not a string.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            engine.EngineStdOut(
                "is_included_in_list block for " + objectId +
                    " has insufficient parameters. Expected LIST_ID and DataOp.",
                2, executionThreadId);
            return OperandValue(false);
        }
        HUDVariableDisplay *targetListPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listId.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listId.asString() && hudVar.objectId.empty())
                {
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }
        if (!targetListPtr)
        {
            engine.EngineStdOut(
                "is_included_in_list block for " + objectId + ": List '" + listId.asString() + "' not found.", 1,
                executionThreadId);
            return OperandValue(false);
        }

        if (targetListPtr->variableType != "list")
        {
            engine.EngineStdOut(
                "is_included_in_list block for " + objectId + ": Variable '" + listId.asString() + "' is not a list.", 2,
                executionThreadId);
            return OperandValue(false);
        }

        if (targetListPtr->isCloud)
        {
            engine.loadCloudVariablesFromJson(); // 클라우드 변수인 경우 최신 데이터 로드
        }

        bool finded = false;
        for (auto &item : targetListPtr->array)
        {
            if (item.data == dataOp.asString())
            {
                finded = true;
                break; // 항목을 찾으면 루프 종료
            }
        }
        return OperandValue(finded);
    }
    else if (BlockType == "reach_something")
    { // ~에 닿았는가?
        Entity *self = engine.getEntityById(objectId);
        if (!self)
        {
            engine.EngineStdOut("reach_something: Self entity " + objectId + " not found.", 2, executionThreadId);
            return OperandValue(false);
        } // self->isVisible() 로 변경하거나, 엔트리 로직에 맞춰 이 조건 제거
        if (!self->isVisible())
        { // 엔트리는 보이지 않아도 충돌 판정함. 이 조건은 주석 처리하거나 로직에 맞게 조정.
          // engine.EngineStdOut("reach_something: Self entity " + objectId + " is not visible.", 0, executionThreadId);
          // return OperandValue(false); // 엔트리 동작과 맞추려면 이 줄 주석 처리
        }

        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut("reach_something block for " + objectId + " has invalid or missing params array. Expected target ID at index 0.", 2, executionThreadId);
            return OperandValue(false);
        }

        OperandValue targetIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (targetIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("reach_something block for " + objectId + ": target ID parameter is not a string. Value: " + targetIdOp.asString(), 2, executionThreadId);
            return OperandValue(false);
        }
        string targetId = targetIdOp.asString();

        if (targetId.empty())
        {
            engine.EngineStdOut("reach_something block for " + objectId + ": target ID is empty.", 2, executionThreadId);
            return OperandValue(false);
        }

        // Wall collision
        if (targetId == "wall" || targetId == "wall_up" || targetId == "wall_down" || targetId == "wall_left" || targetId == "wall_right")
        {
            // Engine에 벽 충돌 확인 로직 필요 (engine.checkCollisionWithWall(self, targetId))
            // 여기서는 Entity의 경계와 스테이지 경계를 비교하는 단순화된 로직을 사용합니다.
            float selfX = self->getX();
            float selfY = self->getY();
            float scaledWidth = self->getWidth() * self->getScaleX();
            float scaledHeight = self->getHeight() * self->getScaleY();

            float selfTop = selfY + scaledHeight / 2.0f;
            float selfBottom = selfY - scaledHeight / 2.0f;
            float selfRight = selfX + scaledWidth / 2.0f;
            float selfLeft = selfX - scaledWidth / 2.0f;

            const float stageTop = PROJECT_STAGE_HEIGHT / 2.0f;
            const float stageBottom = -PROJECT_STAGE_HEIGHT / 2.0f;
            const float stageRight = PROJECT_STAGE_WIDTH / 2.0f;
            const float stageLeft = -PROJECT_STAGE_WIDTH / 2.0f;

            bool collided = false;
            if ((targetId == "wall" || targetId == "wall_up") && selfTop > stageTop)
                collided = true;
            if (!collided && (targetId == "wall" || targetId == "wall_down") && selfBottom < stageBottom)
                collided = true;
            if (!collided && (targetId == "wall" || targetId == "wall_right") && selfRight > stageRight)
                collided = true;
            if (!collided && (targetId == "wall" || targetId == "wall_left") && selfLeft < stageLeft)
                collided = true;

            return OperandValue(collided);
        }

        // Mouse collision
        if (targetId == "mouse")
        {
            if (!engine.isMouseCurrentlyOnStage())
                return OperandValue(false);

            SDL_FPoint mousePos = {static_cast<float>(engine.getCurrentStageMouseX()), static_cast<float>(engine.getCurrentStageMouseY())};

            // Entity의 getVisualBounds()가 전역 좌표계의 SDL_FRect를 반환한다고 가정
            SDL_FRect selfBounds = self->getVisualBounds(); // Engine에 getGlobalBounds(self) 같은 헬퍼가 더 적합할 수 있음

            return OperandValue(SDL_PointInRectFloat(&mousePos, &selfBounds));
        }

        // Sprite collision
        vector<Entity *> entitiesToTest;
        Entity *mainTargetSprite = engine.getEntityById(targetId);

        if (mainTargetSprite)
        {
            if (mainTargetSprite->isVisible() /*&& !mainTargetSprite->isStamp() 엔트리는 스탬프도 충돌대상*/)
            {
                entitiesToTest.push_back(mainTargetSprite);
            }
            // 클론 가져오기 (Engine에 getClones(targetId) 또는 Entity에 getMyClones() 필요)
            // vector<Entity*> clones = engine.getClones(targetId);
            // for (Entity* clone : clones) {
            //    if (clone->getVisible() /*&& !clone->isStamp()*/) {
            //        entitiesToTest.push_back(clone);
            //    }
            // }
            // 임시: 클론 로직은 Engine에 구현 필요. 여기서는 주석 처리.
        }
        else
        {
            // ID로 못찾으면 이름으로 찾아 시도 (엔트리는 ID 기반)
            // 현재 구현에서는 ID로만 찾음
            engine.EngineStdOut("reach_something: Target sprite ID '" + targetId + "' not found.", 1, executionThreadId);
            return OperandValue(false);
        }

        if (entitiesToTest.empty() && !mainTargetSprite)
        { // mainTargetSprite도 없고 entitiesToTest도 비었으면 대상 없음
            engine.EngineStdOut("reach_something: No target entities to test against for ID '" + targetId + "'.", 1, executionThreadId);
            return OperandValue(false);
        } // mainTargetSprite->isVisible() 로 변경
        if (entitiesToTest.empty() && mainTargetSprite && !mainTargetSprite->isVisible())
        { // 대상은 있으나 보이지 않음
          // 엔트리는 보이지 않는 대상과도 충돌 판정하므로 이 케이스는 실제로는 발생하면 안됨 (위의 getVisible 체크 때문)
          // 만약 getVisible 체크를 제거한다면 이 로그가 유용할 수 있음
          // engine.EngineStdOut("reach_something: Target sprite '" + targetId + "' is not visible and has no visible clones.", 0, executionThreadId);
          // return OperandValue(false); // 엔트리 동작과 맞추려면 이 부분도 수정 필요
        }

        SDL_FRect selfBounds = self->getVisualBounds();

        for (Entity *testEntity : entitiesToTest)
        {
            if (!testEntity)
                continue; // 혹시 모를 null 체크
            SDL_FRect targetBounds = testEntity->getVisualBounds();
            if (SDL_HasRectIntersectionFloat(&selfBounds, &targetBounds))
            { // 함수 이름 변경
                // 엔트리는 텍스트 박스 간, 텍스트 박스와 다른 오브젝트 간 충돌은 항상 사각 충돌 사용
                // 일반 오브젝트 간에는 픽셀 충돌 (여기서는 경계 상자로 단순화) - SDL_HasRectIntersectionFloat 사용
                // if (self->isTextbox() || testEntity->isTextbox()) {
                //    return OperandValue(true); // 사각 충돌로 충분
                // } else {
                //    // 픽셀 충돌 로직 (여기서는 경계 상자로 대체됨)
                //    return OperandValue(true);
                // }
                return OperandValue(true); // 단순화된 경계 상자 충돌
            } // SDL_HasIntersectionF 대신 SDL_HasRectIntersectionFloat 사용
        }
        return OperandValue(false);
    }
    else if (BlockType == "is_type")
    {
        // ~ 타입인가?
        // params: [VALUE_TO_CHECK (any type), TYPE_STRING_DROPDOWN (string: "number", "en", "ko")]
        // paramsKeyMap: { VALUE: 0, TYPE: 1 } (가정)

        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            engine.EngineStdOut("is_type block for " + objectId + " has insufficient parameters. Expected VALUE and TYPE.", 2, executionThreadId);
            return OperandValue(false);
        }

        OperandValue valueOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue typeOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);

        string valueStr = valueOp.asString(); // 검사를 위해 입력값을 문자열로 변환

        if (typeOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("is_type block for " + objectId + ": TYPE parameter is not a string. Value: " + typeOp.asString(), 2, executionThreadId);
            return OperandValue(false);
        }
        string typeStr = typeOp.asString();

        if (typeStr == "number")
        {
            return OperandValue(is_number(valueStr));
        }
        else if (typeStr == "en")
        {
            try
            {
                std::regex en_pat("^[a-zA-Z]+$");
                return OperandValue(std::regex_match(valueStr, en_pat));
            }
            catch (const exception &e)
            {
                engine.EngineStdOut("Regex error for 'en' type check: " + string(e.what()), 1);
                return OperandValue(false);
            }
        }
        else if (typeStr == "ko")
        {
            try
            {
                std::regex ko_pat("^[ㄱ-ㅎㅏ-ㅣ가-힣]+$");
                return OperandValue(std::regex_match(valueStr, ko_pat));
            }
            catch (const exception &e)
            {
                engine.EngineStdOut("Regex error for 'ko' type check: " + string(e.what()), 1);
                return OperandValue(false);
            }
        }
        else
        {
            engine.EngineStdOut("is_type block for " + objectId + ": Unsupported type: " + typeStr, 1);
            return OperandValue(false);
        }
    }
    else if (BlockType == "boolean_basic_operator")
    {
        // 두 값의 관계 비교
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 3)
        {
            engine.EngineStdOut("boolean_basic_operator block for " + objectId + " has insufficient parameters. Expected LEFTHAND, OPERATOR, RIGHTHAND.", 2, executionThreadId);
            return OperandValue(false);
        }

        OperandValue leftOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue operatorOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue rightOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);

        if (operatorOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("boolean_basic_operator block for " + objectId + ": OPERATOR parameter is not a string. Value: " + operatorOp.asString(), 2, executionThreadId);
            return OperandValue(false);
        }
        string opStr = operatorOp.asString();
        // 1. 먼저 양쪽 모두 엄격한 숫자인지 확인
        bool leftIsNumber = (leftOp.type == OperandValue::Type::NUMBER) ||
                            (leftOp.type == OperandValue::Type::STRING && is_number(leftOp.string_val));
        bool rightIsNumber = (rightOp.type == OperandValue::Type::NUMBER) ||
                             (rightOp.type == OperandValue::Type::STRING && is_number(rightOp.string_val));

        engine.EngineStdOut(
            std::format("boolean_basic_operator ({}): leftIsNumber={}, rightIsNumber={}",
                        objectId, leftIsNumber, rightIsNumber),
            3, executionThreadId);

        // 2. 둘 다 숫자로 처리 가능한 경우
        if (leftIsNumber && rightIsNumber)
        {
            double leftNum = leftOp.asNumber();
            double rightNum = rightOp.asNumber();
            engine.EngineStdOut(std::format("boolean_basic_operator ({}): Numeric compare: {} {} {}", objectId, leftNum, opStr, rightNum), 3, executionThreadId);
            if (opStr == "EQUAL")
                return OperandValue(leftNum == rightNum);
            else if (opStr == "NOT_EQUAL")
                return OperandValue(leftNum != rightNum);
            else if (opStr == "GREATER")
                return OperandValue(leftNum > rightNum);
            else if (opStr == "LESS")
                return OperandValue(leftNum < rightNum);
            else if (opStr == "GREATER_OR_EQUAL")
                return OperandValue(leftNum >= rightNum);
            else if (opStr == "LESS_OR_EQUAL")
                return OperandValue(leftNum <= rightNum);
        }
        // 3. 숫자가 아닌 경우 문자열로 비교
        else
        {
            string leftStr = leftOp.asString();
            string rightStr = rightOp.asString();
            engine.EngineStdOut(std::format("boolean_basic_operator ({}): String compare: \"{}\" {} \"{}\"", objectId, leftStr, opStr, rightStr), 3, executionThreadId);
            if (opStr == "EQUAL")
                return OperandValue(leftStr == rightStr);
            if (opStr == "NOT_EQUAL")
                return OperandValue(leftStr != rightStr);

            // 숫자가 아닌 값들에 대한 비교는 문자열을 0으로 취급
            double leftVal = leftIsNumber ? leftOp.asNumber() : 0.0;
            double rightVal = rightIsNumber ? rightOp.asNumber() : 0.0;

            if (opStr == "GREATER")
                return OperandValue(leftVal > rightVal);
            if (opStr == "LESS")
                return OperandValue(leftVal < rightVal);
            if (opStr == "GREATER_OR_EQUAL")
                return OperandValue(leftVal >= rightVal);
            if (opStr == "LESS_OR_EQUAL")
                return OperandValue(leftVal <= rightVal);
        }

        engine.EngineStdOut("boolean_basic_operator block for " + objectId + ": Unknown operator '" + opStr + "'.", 2, executionThreadId);
        return OperandValue(false);
    }
    else if (BlockType == "boolean_not")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut("boolean_not block for " + objectId + " has insufficient parameters. Expected VALUE.", 2, executionThreadId);
            return OperandValue(false);
        }
        OperandValue ValueOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        return OperandValue(!ValueOp.asBool());
    }
    else if (BlockType == "is_boost_mode")
    {
        // C++ 기반 엔진 은 SDL 을 사용합니다 (하드코딩)
        return OperandValue(true);
    }
    else if (BlockType == "is_current_device_type")
    {
        // params: [DEVICE_TYPE_DROPDOWN (string: "desktop", "tablet", "mobile")]
        // paramsKeyMap: { DEVICE: 0 }
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut("is_current_device_type block for " + objectId + " has insufficient parameters. Expected DEVICE type.", 2, executionThreadId);
            return OperandValue(false); // 오류 시 false 반환
        }

        OperandValue deviceParamOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (deviceParamOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("is_current_device_type block for " + objectId + ": DEVICE parameter is not a string. Value: " + deviceParamOp.asString(), 2, executionThreadId);
            return OperandValue(false);
        }
        string selectedDeviceType = deviceParamOp.asString(); // 드롭다운에서 선택된 값 ("desktop", "tablet", "mobile")

        string actualDeviceType = engine.getDeviceType(); // Engine에서 실제 장치 유형 가져오기

        if (selectedDeviceType != "desktop")
        {
            return OperandValue(actualDeviceType == selectedDeviceType);
        }
        else // selectedDeviceType이 "desktop"인 경우
        {
            return OperandValue(actualDeviceType != "mobile" && actualDeviceType != "tablet");
        }
    }
    else if (BlockType == "is_touch_supported")
    {
        // 미지원
        return OperandValue(engine.isTouchSupported());
    }
    else if (BlockType == "text_read")
    {
        // paramsKeyMap: { VALUE: 0 }
        // 파라미터는 글상자 ID 또는 "self"를 가리키는 드롭다운입니다.
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut("text_read block for " + objectId + " has invalid or missing params. Expected target textBox ID.", 2, executionThreadId);
            return OperandValue(""); // 오류 시 빈 문자열 반환
        }

        OperandValue targetIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (targetIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("text_read block for " + objectId + ": target ID parameter is not a string. Value: " + targetIdOp.asString(), 2, executionThreadId);
            return OperandValue("");
        }
        string targetIdStr = targetIdOp.asString();

        const ObjectInfo *targetObjInfo = nullptr;
        if (targetIdStr == "self")
        {
            targetObjInfo = engine.getObjectInfoById(objectId); // 현재 스크립트를 실행하는 오브젝트
            if (targetObjInfo && targetObjInfo->objectType != "textBox")
            {
                engine.EngineStdOut("text_read: 'self' (object ID: " + objectId + ") is not a textBox.", 2, executionThreadId);
                return OperandValue(""); // 'self'가 textBox가 아니면 빈 문자열 반환
            }
        }
        else
        {
            targetObjInfo = engine.getObjectInfoById(targetIdStr);
            if (targetObjInfo && targetObjInfo->objectType != "textBox")
            {
                engine.EngineStdOut("text_read: Target object '" + targetIdStr + "' is not a textBox.", 1, executionThreadId);
                return OperandValue(""); // 대상이 textBox가 아니면 빈 문자열 반환
            }
        }

        if (!targetObjInfo)
        {
            engine.EngineStdOut("text_read: Target textBox '" + targetIdStr + "' not found.", 1, executionThreadId);
            return OperandValue(""); // 대상을 찾을 수 없으면 빈 문자열 반환
        }

        string textValue = targetObjInfo->textContent;
        // JavaScript의 value.replace(/\n/gim, ' ')와 동일하게 처리
        size_t pos = 0;
        while ((pos = textValue.find('\n', pos)) != string::npos)
        {
            textValue.replace(pos, 1, " ");
            pos += 1;
        }
        return OperandValue(textValue);
    }
    return OperandValue();
}

/**
 * @brief 모양새 블록
 *
 */
void Looks(string BlockType, Engine &engine, const string &objectId, const Block &block,
           const string &executionThreadId)
{
    auto entity = engine.getEntityByIdShared(objectId);
    if (!entity)
    {
        engine.EngineStdOut("Looks block: Entity " + objectId + " not found for block type " + BlockType, 2);
        return;
    }

    if (BlockType == "show")
    {
        entity->setVisible(true);
    }
    else if (BlockType == "hide")
    {
        entity->setVisible(false);
    }
    else if (BlockType == "dialog_time")
    {
        // params: VALUE (message), SECOND, OPTION (speak/think)
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 3)
        {
            // 인디케이터 포함하면 4개일 수 있음
            engine.EngineStdOut(
                "dialog_time block for " + objectId + " has insufficient parameters. Expected message, time, option.",
                2, executionThreadId);
            return;
        }
        OperandValue messageOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue timeOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue optionOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId); // Dropdown value

        if (timeOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "dialog_time block for " + objectId + ": SECOND parameter is not a number. Value: " + timeOp.asString(),
                2, executionThreadId);
            return;
        }
        if (optionOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "dialog_time block for " + objectId + ": OPTION parameter is not a string. Value: " + optionOp.asString(), 2, executionThreadId);
            return;
        }

        string message = messageOp.asString();
        Uint64 durationMs = static_cast<Uint64>(timeOp.asNumber() * 1000.0);
        string dialogType = optionOp.asString();
        entity->showDialog(message, dialogType, durationMs);
    }
    else if (BlockType == "dialog")
    {
        // params: VALUE (message), OPTION (speak/think)
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            // 인디케이터 포함하면 3개일 수 있음
            engine.EngineStdOut(
                "dialog block for " + objectId + " has insufficient parameters. Expected message, option.", 2,
                executionThreadId);
            return;
        }
        OperandValue messageOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue optionOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId); // Dropdown value

        if (optionOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "dialog block for " + objectId + ": OPTION parameter is not a string. Value: " + optionOp.asString(), 2,
                executionThreadId);
            return;
        }

        string message = messageOp.asString();
        string dialogType = optionOp.asString();
        entity->showDialog(message, dialogType, 0);
    }
    else if (BlockType == "remove_dialog")
    {
        entity->removeDialog();
    }
    else if (BlockType == "change_to_some_shape")
    {
        // 이미지 url 묶음에서 해당 모양의 ID를 (사용자 는 모양의 이름이 정의된 드롭다운이 나온다) 선택 한 것으로 바꾼다.
        // --- DEBUG START ---
        if (block.paramsJson.is_array() && !block.paramsJson.empty())
        {
            string params_json_dump = "null";
            if (!block.paramsJson[0].is_null())
            {
                params_json_dump = block.paramsJson[0].dump();
            }
            engine.EngineStdOut(
                "change_to_some_shape for " + objectId + ": Raw paramField[0] before getOperandValue: " +
                    params_json_dump,
                3, executionThreadId);
        }
        else
        {
            engine.EngineStdOut(
                "change_to_some_shape for " + objectId + ": paramsJson is not an array or is empty.", 3, executionThreadId);
        }
        // --- DEBUG END ---

        OperandValue imageDropdown = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        // getOperandValue는 get_pictures 블록의 params[0] (모양 ID 문자열)을 반환해야 합니다.
        // getOperandValue 내부에서 get_pictures 타입 처리가 필요합니다.

        if (imageDropdown.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "change_to_some_shape block for " + objectId +
                    " parameter did not resolve to a string (expected costume ID). Actual type: " +
                    to_string(static_cast<int>(imageDropdown.type)),
                2, executionThreadId);
            // --- DEBUG START ---
            engine.EngineStdOut(
                "change_to_some_shape for " + objectId + ": imageDropdown.asString() returned: '" + imageDropdown.asString() + "'", 3, executionThreadId);
            // --- DEBUG END ---
            return;
        }
        string costumeIdToSet = imageDropdown.asString();
        if (costumeIdToSet.empty())
        {
            engine.EngineStdOut("change_to_some_shape block for " + objectId + " received an empty costume ID.", 2,
                                executionThreadId);
            return;
        }
        // --- DEBUG START ---
        engine.EngineStdOut(
            "change_to_some_shape for " + objectId + ": Attempting to set costume to ID: '" + costumeIdToSet + "'", 3, executionThreadId);
        // --- DEBUG END ---

        // Engine에서 ObjectInfo를 가져와서 selectedCostumeId를 업데이트합니다.
        if (!engine.setEntitySelectedCostume(objectId, costumeIdToSet))
        {
            engine.EngineStdOut(
                "change_to_some_shape block for " + objectId + ": Failed to set costume to ID '" + costumeIdToSet +
                    "'. It might not exist for this object.",
                1, executionThreadId);
        }
        else
        {
            engine.EngineStdOut("Entity " + objectId + " changed shape to: " + costumeIdToSet, 0, executionThreadId);
        }
    }
    else if (BlockType == "change_to_next_shape")
    {
        OperandValue nextorprev = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (nextorprev.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "change_to_some_shape block for " + objectId +
                    " parameter did not resolve to a string (expected costume ID). Actual type: " +
                    to_string(static_cast<int>(nextorprev.type)),
                2, executionThreadId);
            return;
        }
        string direction = nextorprev.asString();
        if (direction == "next")
        {
            engine.setEntitychangeToNextCostume(objectId, "next");
        }
        else
        {
            engine.setEntitychangeToNextCostume(objectId, "prev");
        }
    }
    else if (BlockType == "add_effect_amount")
    {
        // params: EFFECT (dropdown: "color", "brightness", "transparency"), VALUE (number)
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            // 인디케이터 포함 시 3개일 수 있음
            engine.EngineStdOut(
                "add_effect_amount block for " + objectId + " has insufficient parameters. Expected EFFECT, VALUE.", 2,
                executionThreadId);
            return;
        }
        OperandValue effectTypeOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);  // EFFECT dropdown
        OperandValue effectValueOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId); // VALUE number

        if (effectTypeOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "add_effect_amount block for " + objectId + ": EFFECT parameter is not a string. Value: " + effectTypeOp.asString(), 2, executionThreadId);
            return;
        }
        if (effectValueOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "add_effect_amount block for " + objectId + ": VALUE parameter is not a number. Value: " + effectValueOp.asString(), 2, executionThreadId);
            return;
        }

        string effectName = effectTypeOp.asString();
        double value = effectValueOp.asNumber();

        if (effectName == "color")
        {
            // JavaScript의 'hsv'에 해당, 여기서는 색조(hue)로 처리
            entity->setEffectHue(entity->getEffectHue() + value);
            // engine.EngineStdOut("Entity " + objectId + " effect 'color' (hue) changed by " + to_string(value) + ", new value: " + to_string(entity->getEffectHue()), 0);
        }
        else if (effectName == "brightness")
        {
            entity->setEffectBrightness(entity->getEffectBrightness() + value);
            // engine.EngineStdOut("Entity " + objectId + " effect 'brightness' changed by " + to_string(value) + ", new value: " + to_string(entity->getEffectBrightness()), 0);
        }
        else if (effectName == "transparency")
        {
            // JavaScript: sprite.effect.alpha = sprite.effect.alpha - effectValue / 100;
            // 여기서 effectValue는 0-100 범위의 값으로, 투명도를 증가시킵니다 (알파 값을 감소시킴).
            // Entity의 m_effectAlpha는 0.0(투명) ~ 1.0(불투명) 범위입니다.
            entity->setEffectAlpha(entity->getEffectAlpha() - (value / 100.0));
            // engine.EngineStdOut("Entity " + objectId + " effect 'transparency' (alpha) changed by " + to_string(value) + "%, new value: " + to_string(entity->getEffectAlpha()), 0);
        }
        else
        {
            engine.EngineStdOut(
                "add_effect_amount block for " + objectId + ": Unknown effect type '" + effectName + "'.", 1,
                executionThreadId);
        }
    }
    else if (BlockType == "change_effect_amount")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            engine.EngineStdOut(
                "change_effect_amount block for " + objectId + " has insufficient parameters. Expected EFFECT, VALUE.",
                2, executionThreadId);
            return;
        }
        OperandValue effectTypeOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);  // EFFECT dropdown
        OperandValue effectValueOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId); // VALUE number

        if (effectTypeOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "add_effect_amount block for " + objectId + ": EFFECT parameter is not a string. Value: " + effectTypeOp.asString(), 2, executionThreadId);
            return;
        }
        if (effectValueOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "add_effect_amount block for " + objectId + ": VALUE parameter is not a number. Value: " + effectValueOp.asString(), 2, executionThreadId);
            return;
        }

        string effectName = effectTypeOp.asString();
        double value = effectValueOp.asNumber();
        if (effectName == "color")
        {
            entity->setEffectHue(value);
            engine.EngineStdOut("Entity " + objectId + " effect 'color' (hue) changed to " + to_string(value), 3,
                                executionThreadId);
        }
        else if (effectName == "brightness")
        {
            entity->setEffectBrightness(value);
            engine.EngineStdOut("Entity " + objectId + " effect 'brightness' changed to " + to_string(value), 3,
                                executionThreadId);
        }
        else if (effectName == "transparency")
        {
            entity->setEffectAlpha(1 - (value / 100.0));
            engine.EngineStdOut(
                "Entity " + objectId + " effect 'transparency' (alpha) changed to " + to_string(value), 3);
        }
    }
    else if (BlockType == "erase_all_effects")
    {
        entity->setEffectBrightness(0.0); // 밝기 효과 초기화 (0.0이 기본값)
        entity->setEffectAlpha(1.0);      // 투명도 효과 초기화 (1.0이 기본값, 완전 불투명)
        entity->setEffectHue(0.0);        // 색깔 효과 (색조) 초기화 (0.0이 기본값)
        engine.EngineStdOut("Entity " + objectId + " all graphic effects erased.", 0, executionThreadId);
    }
    else if (BlockType == "change_scale_size")
    {
        OperandValue size = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        double changePercent = size.asNumber();

        // 현재 스케일 팩터를 퍼센트로 변환, 변경량을 더한 후 다시 팩터로 변환하여 적용
        double currentScaleXFactor = entity->getScaleX();
        double newPercentX = (currentScaleXFactor * 100.0) + changePercent;
        entity->setScaleX(newPercentX / 100.0);

        double currentScaleYFactor = entity->getScaleY();
        double newPercentY = (currentScaleYFactor * 100.0) + changePercent;
        entity->setScaleY(newPercentY / 100.0);

        //debug
        engine.EngineStdOut("SCALE: " + to_string(entity->getScaleX()) + ", " + to_string(entity->getScaleY()), 3);
    }
    else if (BlockType == "set_scale_size")
    {
        OperandValue setSize = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        double percent = setSize.asNumber();
        // 입력된 퍼센트 값을 스케일 팩터로 변환하여 적용
        entity->setScaleX(percent / 100.0);
        entity->setScaleY(percent / 100.0);
        //debug
        engine.EngineStdOut("SCALE: " + to_string(entity->getScaleX()) + ", " + to_string(entity->getScaleY()), 3);
    }
    else if (BlockType == "stretch_scale_size")
    {
        OperandValue setWidth = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue setHeight = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        // 입력된 x, y 퍼센트 값을 각각 스케일 팩터로 변환하여 적용
        entity->setScaleX(setWidth.asNumber() / 100.0);
        entity->setScaleY(setHeight.asNumber() / 100.0);
        //debug
        engine.EngineStdOut("STRETCH SCALE_FACTORS: " + to_string(entity->getScaleX()) + ", " + to_string(entity->getScaleY()), 3);
    }
    else if (BlockType == "reset_scale_size")
    {
        entity->setScaleX(1.0); // 100% 크기
        entity->setScaleY(1.0); // 100% 크기
    }
    else if (BlockType == "flip_x")
    {
        entity->setScaleX(-1 * entity->getScaleX());
    }
    else if (BlockType == "flip_y")
    {
        entity->setScaleY(-1 * entity->getScaleY());
    }
    else if (BlockType == "change_object_index")
    {
        // 이 엔진은 역순으로 스프라이트를 렌더링 하고있음
        OperandValue zindexEnumDropdown = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (zindexEnumDropdown.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_object_index object is not String", 2, executionThreadId);
            return;
        }
        string zindexEnumStr = zindexEnumDropdown.asString();
        Omocha::ObjectIndexChangeType changeType = Omocha::stringToObjectIndexChangeType(zindexEnumStr);
        engine.changeObjectIndex(objectId, changeType);
    }
}

/**
 * @brief 사운드 블록
 *
 */
void Sound(string BlockType, Engine &engine, const string &objectId, const Block &block,
           const string &executionThreadId)
{
    auto entity = engine.getEntityByIdShared(objectId);
    if (BlockType == "sound_something_with_block")
    {
        OperandValue soundType = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);

        if (soundType.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "sound_something_with_block for object " + objectId + ": sound ID parameter is not a string. Value: " +
                    soundType.asString(),
                2, executionThreadId);
            return;
        }
        string soundIdToPlay = soundType.asString();

        if (soundIdToPlay.empty())
        {
            engine.EngineStdOut("sound_something_with_block for object " + objectId + ": received an empty sound ID.",
                                2, executionThreadId);
            return;
        }

        entity->playSound(soundIdToPlay);
    }
    else if (BlockType == "sound_something_second_with_block")
    {
        OperandValue soundType = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue soundTime = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (soundType.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "sound_something_second_with_block for object " + objectId +
                    ": sound ID parameter is not a string. Value: " + soundType.asString(),
                2, executionThreadId);
            return;
        }
        if (soundTime.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "sound_something_second_with_block for object " + objectId + ": sound time parameter is not a number.",
                2, executionThreadId);
            return;
        }

        string soundIdToPlay = soundType.asString();

        if (soundIdToPlay.empty())
        {
            engine.EngineStdOut("sound_something_with_block for object " + objectId + ": received an empty sound ID.",
                                2, executionThreadId);
            return;
        }

        entity->playSoundWithSeconds(soundIdToPlay, soundTime.asNumber());
    }
    else if (BlockType == "sound_from_to")
    {
        OperandValue soundId = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue from = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue to = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);
        if (soundId.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "sound_something_with_block for object " + objectId + ": sound ID parameter is not a string. Value: " +
                    soundId.asString(),
                2, executionThreadId);
            return;
        }
        if (from.type != OperandValue::Type::NUMBER && to.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "sound_something_with_block for object " + objectId + ": sound time parameter is not a number.", 2,
                executionThreadId);
            return;
        }

        string soundIdToPlay = soundId.asString();
        double fromTime = from.asNumber();
        double toTime = to.asNumber();
        if (soundIdToPlay.empty())
        {
            engine.EngineStdOut("sound_from_to for object " + objectId + ": received an empty sound ID", 2,
                                executionThreadId);
            return;
        }
        entity->playSoundWithFromTo(soundIdToPlay, fromTime, toTime);
    }
    else if (BlockType == "sound_something_wait_with_block")
    {
        // 소리 를 재생하고 기다리기. (재생이 끝날때까지 기다리는것)
        OperandValue soundId = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (soundId.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "sound_something_wait_with_block for object" + objectId + ": received an empty sound ID", 2,
                executionThreadId);
            return;
        }
        string soundIdToPlay = soundId.asString();

        // 1. Play the sound (non-blocking)
        entity->playSound(soundIdToPlay); // Assuming playSound is non-blocking

        // 2. Set script wait state for SOUND_FINISH
        // Need to store scriptPtr and next block index for resume
        Entity::ScriptThreadState *pThreadState = nullptr;
        {
            lock_guard<recursive_mutex> lock(entity->getStateMutex());
            pThreadState = &entity->scriptThreadStates[executionThreadId];
        }
        // resumeAtBlockIndex will be set by Entity::executeScript when it sees this wait state.
        // It should point to the block *after* this sound_something_wait_with_block.
        entity->setScriptWait(executionThreadId, 0, block.id, Entity::WaitType::SOUND_FINISH);
    }
    else if (BlockType == "sound_something_second_wait_with_block")
    {
        OperandValue soundId = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue soundTime = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (soundId.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "sound_something_second_wait_with_block for object" + objectId + ": received an empty sound ID", 2,
                executionThreadId);
            return;
        }
        if (soundTime.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "sound_something_second_wait_with_block for object" + objectId + ": received an empty time", 2,
                executionThreadId);
            return;
        }
        string soundIdToPlay = soundId.asString();
        double soundTimeValue = soundTime.asNumber();

        entity->playSoundWithSeconds(soundIdToPlay, soundTimeValue);

        Entity::ScriptThreadState *pThreadState = nullptr;
        {
            lock_guard<recursive_mutex> lock(entity->getStateMutex());
            pThreadState = &entity->scriptThreadStates[executionThreadId];
        }
        // Similar to above, set wait state. Entity::executeScript will handle pausing
        // and setting resumeAtBlockIndex.
        entity->setScriptWait(executionThreadId, 0, block.id, Entity::WaitType::SOUND_FINISH);
    }
    else if (BlockType == "sound_from_to_and_wait")
    {
        OperandValue soundId = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue from = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue to = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);
        if (soundId.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("sound_from_to_and_wait for object" + objectId + ": received an empty sound ID", 2,
                                executionThreadId);
            return;
        }
        if (from.type != OperandValue::Type::NUMBER && to.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("sound_from_to_and_wait for object" + objectId + ": received an empty from and to time",
                                2, executionThreadId);
            return;
        }
        string soundIdToPlay = soundId.asString();
        double fromTime = from.asNumber();
        double toTime = to.asNumber();

        entity->playSoundWithFromTo(soundIdToPlay, fromTime, toTime);

        Entity::ScriptThreadState *pThreadState = nullptr;
        {
            lock_guard<recursive_mutex> lock(entity->getStateMutex());
            pThreadState = &entity->scriptThreadStates[executionThreadId];
        }
        // Set wait state. Entity::executeScript will handle pausing
        // and setting resumeAtBlockIndex.
        entity->setScriptWait(executionThreadId, 0, block.id, Entity::WaitType::SOUND_FINISH);
    }
    else if (BlockType == "sound_volume_change")
    {
        // 파라미터는 하나 (VALUE) - 볼륨 변경량 (예: 10, -20)
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            // VALUE 파라미터 확인
            engine.EngineStdOut(
                "sound_volume_change block for object " + objectId + " has insufficient parameters. Expected VALUE.", 2,
                executionThreadId);
            return;
        }
        OperandValue volumeChangeOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (volumeChangeOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "sound_volume_change for object " + objectId + ": VALUE parameter is not a number. Value: " +
                    volumeChangeOp.asString(),
                2, executionThreadId);
            return;
        }

        double volumeChangePercentage = volumeChangeOp.asNumber();                     // 사용자가 입력한 값 (예: 10, -20)
        float volumeChangeRatio = static_cast<float>(volumeChangePercentage) / 100.0f; // 비율로 변환 (예: 0.1, -0.2)

        // Engine의 AudioEngineHelper를 통해 전역 볼륨을 가져와서 변경하고 다시 설정
        // aeHelper에 getGlobalVolume() 및 setGlobalVolume() 함수가 구현되어 있다고 가정합니다.
        float currentGlobalVolume = engine.aeHelper.getGlobalVolume();
        float newGlobalVolume = currentGlobalVolume + volumeChangeRatio;
        newGlobalVolume = clamp(newGlobalVolume, 0.0f, 1.0f); // 0.0 ~ 1.0 범위로 제한

        engine.aeHelper.setGlobalVolume(newGlobalVolume);
        engine.EngineStdOut(
            "Global volume changed by " + to_string(volumeChangePercentage) + "%. New global volume: " +
                to_string(newGlobalVolume) + " (triggered by object " + objectId + ")",
            0, executionThreadId);
    }
    else if (BlockType == "sound_volume_set")
    {
        // 파라미터는 하나 (VALUE) - 볼륨 변경량 (예: 10, -20)
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            // VALUE 파라미터 확인
            engine.EngineStdOut(
                "sound_volume_change block for object " + objectId + " has insufficient parameters. Expected VALUE.", 2,
                executionThreadId);
            return;
        }
        OperandValue volumeChangeOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (volumeChangeOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "sound_volume_change for object " + objectId + ": VALUE parameter is not a number. Value: " +
                    volumeChangeOp.asString(),
                2, executionThreadId);
            return;
        }

        double volumeChangePercentage = volumeChangeOp.asNumber();                     // 사용자가 입력한 값 (예: 10, -20)
        float volumeChangeRatio = static_cast<float>(volumeChangePercentage) / 100.0f; // 비율로 변환 (예: 0.1, -0.2)
        engine.aeHelper.setGlobalVolume(volumeChangeRatio);
    }
    else if (BlockType == "sound_speed_change")
    {
        OperandValue speed = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (speed.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "sound_speed_change for object " + objectId + ": VALUE parameter is not a number. Value: " + speed.asString(), 2, executionThreadId);
            return;
        }
        double valueFromBlock = speed.asNumber();                            // 사용자가 블록에 입력한 값 (예: 10, -20)
        float currentSpeedFactor = engine.aeHelper.getGlobalPlaybackSpeed(); // 현재 재생 속도 (예: 1.0이 기본)

        // 입력값을 100으로 나누어 실제 속도 변경량으로 변환 (예: 10 -> 0.1)
        float speedChangeAmount = static_cast<float>(valueFromBlock) / 100.0f;
        float newSpeedFactor = currentSpeedFactor + speedChangeAmount;

        double clampedSpeed = clamp(static_cast<double>(newSpeedFactor), 0.5, 2.0); // 엔트리와 동일하게 0.5 ~ 2.0 범위로 제한
        engine.aeHelper.setGlobalPlaybackSpeed(static_cast<float>(clampedSpeed));
    }
    else if (BlockType == "sound_speed_set")
    {
        OperandValue speed = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (speed.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "sound_speed_change for object " + objectId + ": VALUE parameter is not a number. Value: " + speed.asString(), 2, executionThreadId);
            return;
        }
        double valueFromBlock = speed.asNumber(); // 사용자가 블록에 입력한 값 (예: 10, -20)
        // 입력값을 100으로 나누어 실제 속도 변경량으로 변환 (예: 10 -> 0.1)
        float speedChangeAmount = static_cast<float>(valueFromBlock) / 100.0f;
        engine.aeHelper.setGlobalPlaybackSpeed(static_cast<float>(speedChangeAmount));
    }
    else if (BlockType == "sound_silent_all")
    {
        // 파라미터는 하나 (TARGET) - "all", "thisOnly", "other_objects"
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut(
                "sound_silent_all for object " + objectId + ": TARGET parameter is missing.", 2,
                executionThreadId);
            return;
        }
        OperandValue targetOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (targetOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("sound_silent_all for object " + objectId + ": TARGET parameter did not resolve to a string. Value: " + targetOp.asString(), 2, executionThreadId);
            return;
        }
        string target = targetOp.asString();

        if (target == "all")
        {
            engine.aeHelper.stopAllSounds();
            // engine.EngineStdOut("All sounds stopped (triggered by object " + objectId + ")", 0);
        }
        else if (target == "thisOnly")
        {
            engine.aeHelper.stopSound(objectId); // 해당 objectId의 모든 소리 중지
            // engine.EngineStdOut("Sounds for object " + objectId + " stopped.", 0);
        }
        else if (target == "other_objects")
        {
            engine.aeHelper.stopAllSoundsExcept(objectId);
            // engine.EngineStdOut("Sounds for all other objects (except " + objectId + ") stopped.", 0);
        }
        else
        {
            engine.EngineStdOut(
                "sound_silent_all for object " + objectId + ": Unknown TARGET parameter value: " + target, 2,
                executionThreadId);
        }
    }
    else if (BlockType == "play_bgm")
    {
        // EntryJS에서는 'VALUE' 필드 하나만 사용하며, 이것이 get_sounds 블록을 통해 사운드 ID를 가져옵니다.
        // block.paramsJson[0]이 get_sounds 블록일 것으로 예상합니다.
        OperandValue soundIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (soundIdOp.type != OperandValue::Type::STRING) // getOperandValue가 get_sounds를 처리하여 문자열 ID를 반환해야 함
        {
            engine.EngineStdOut(
                "play_bgm for object " + objectId + ": sound ID parameter is not a string. Value: " + soundIdOp.asString(), 2, executionThreadId);
            throw ScriptBlockExecutionError(
                "play_bgm 블록의 사운드 ID 파라미터가 문자열이 아닙니다.",
                block.id, BlockType, objectId, "Sound ID parameter is not a string. Value: " + soundIdOp.asString());
        }
        string soundIdToPlay = soundIdOp.asString(); // 실제 사운드 ID
        if (soundIdToPlay.empty())
        {
            engine.EngineStdOut("play_bgm for object " + objectId + ": received an empty sound ID.", 2,
                                executionThreadId);
            throw ScriptBlockExecutionError(
                "play_bgm 블록에 빈 사운드 ID가 전달되었습니다. (get_sounds 블록에서 ID를 가져오지 못했을 수 있습니다)",
                block.id, BlockType, objectId, "Received an empty sound ID.");
        }

        // 엔티티(오브젝트) 정보를 가져와서 해당 오브젝트에 등록된 소리인지 확인합니다.
        // 배경음악 자체는 전역적이지만, 어떤 소리를 배경음악으로 사용할지는 특정 오브젝트의 소리 목록에서 가져옵니다.
        const ObjectInfo *objInfo = engine.getObjectInfoById(objectId);
        if (!objInfo)
        {
            engine.EngineStdOut("play_bgm - ObjectInfo not found for entity: " + objectId, 2, executionThreadId);
            return;
        }
        const SoundFile *soundToUseAsBgm = nullptr;
        for (const auto &sound : objInfo->sounds)
        {
            if (sound.id == soundIdToPlay)
            {
                soundToUseAsBgm = &sound;
                break;
            }
        }
        if (soundToUseAsBgm)
        {
            string soundFilePath = "";
            if (engine.IsSysMenu)
            {
                soundFilePath = "sysmenu/" + soundToUseAsBgm->fileurl;
            }
            else
            {
                soundFilePath = string(BASE_ASSETS) + soundToUseAsBgm->fileurl;
            }
            // EntryJS의 forceStopBGM()과 유사하게, 기존 BGM을 확실히 중지합니다.
            // AudioEngineHelper에 BGM 상태를 추적하고, 필요시 이전 BGM을 중지하는 로직이 있다면
            // stopBackgroundMusic() 호출이 충분할 수 있습니다.
            engine.aeHelper.stopBackgroundMusic();                             // 기존 BGM 중지
            engine.aeHelper.playBackgroundMusic(soundFilePath.c_str(), false); // 배경음악을 재생하지만 무한반복하는 옵션은 엔트리에 없음.
        }
        else
        {
            engine.EngineStdOut(
                "play_bgm - Sound ID '" + soundIdToPlay + "' not found in sound list for entity: " + objectId, 1,
                executionThreadId);
        }
    }
}

/**
 * @brief 변수 블록
 *
 */
void Variable(string BlockType, Engine &engine, const string &objectId, const Block &block,
              const string &executionThreadId)
{
    if (BlockType == "set_visible_answer")
    {
        OperandValue visibleDropdown = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (visibleDropdown.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "set_visible_answer for object " + objectId + ": visible parameter is not a string. Value: " +
                    visibleDropdown.asString(),
                2, executionThreadId);
            return;
        }
        string visible = visibleDropdown.asString();
        if (visible == "HIDE")
        {
            engine.showAnswerValue(false);
        }
        else
        {
            engine.showAnswerValue(true);
        }
    }
    else if (BlockType == "ask_and_wait")
    {
        // params: [VALUE (question_string_block), null (indicator)]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut(
                "ask_and_wait block for " + objectId + " has insufficient parameters. Expected question.", 2,
                executionThreadId);
            throw ScriptBlockExecutionError("질문 파라미터가 부족합니다.", block.id, BlockType, objectId,
                                            "Insufficient parameters for ask_and_wait.");
        }

        OperandValue questionOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        string questionMessage = questionOp.asString();

        if (questionMessage.empty())
        {
            // EntryJS는 빈 메시지를 허용하지 않는 것으로 보임 (오류)
            engine.EngineStdOut(
                "ask_and_wait block for " + objectId + ": question message is empty. Proceeding with empty question.",
                2, executionThreadId);
            throw ScriptBlockExecutionError("질문 내용이 비어있습니다.", block.id, BlockType, objectId,
                                            "Question message cannot be empty.");
        }

        auto entity = engine.getEntityByIdShared(objectId);
        if (!entity)
        {
            engine.EngineStdOut("ask_and_wait: Entity " + objectId + " not found.", 2, executionThreadId);
            throw ScriptBlockExecutionError("질문 대상 객체를 찾을 수 없습니다.", block.id, BlockType, objectId,
                                            "Entity not found for ask_and_wait.");
        }

        // EntryJS는 Dialog를 먼저 띄웁니다.
        // Engine의 activateTextInput 내부에서 Dialog를 띄우거나, 여기서 직접 호출할 수 있습니다.
        // entity->showDialog(questionMessage, "ask", 0); // Engine에서 처리하도록 변경

        // Engine에 텍스트 입력을 요청하고, 사용자 입력이 완료될 때까지 이 스크립트 스레드는 대기합니다.
        // engine.activateTextInput은 내부적으로 m_lastAnswer를 설정해야 합니다.
        engine.activateTextInput(objectId, questionMessage, executionThreadId);
    }
    else if (BlockType == "change_variable")
    {
        // params: [VARIABLE_ID_STRING, VALUE_TO_ADD_OR_CONCAT, null, null]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            engine.EngineStdOut(
                "change_variable block for " + objectId +
                    " has insufficient parameters. Expected VARIABLE_ID and VALUE.",
                2, executionThreadId);
            return;
        }

        // 1. 변수 ID 가져오기 (항상 문자열 드롭다운)
        OperandValue variableIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (variableIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": VARIABLE_ID parameter is not a string. Value: " + variableIdOp.asString(), 2, executionThreadId);
            return;
        }
        string variableIdToFind = variableIdOp.asString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return;
        }

        // 2. 더하거나 이어붙일 값 가져오기
        OperandValue valueToAddOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);

        // 3. 변수 찾기 (로컬 우선, 없으면 전역)
        HUDVariableDisplay *targetVarPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.id == variableIdToFind && hudVar.objectId == objectId)
            {
                targetVarPtr = &hudVar;
                break;
            }
        }
        if (!targetVarPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.id == variableIdToFind && hudVar.objectId.empty())
                {
                    targetVarPtr = &hudVar;
                    break;
                }
            }
        }

        if (!targetVarPtr)
        {
            engine.EngineStdOut(
                "change_variable block for " + objectId + ": Variable '" + variableIdToFind + "' not found.", 1,
                executionThreadId);
            return;
        }

        // 4. 연산 수행
        // 현재 변수 값(문자열)을 숫자로 변환 시도
        double currentVarNumericValue = 0.0;
        bool currentVarIsNumeric = false;
        try
        {
            size_t idx = 0;
            currentVarNumericValue = stod(targetVarPtr->value, &idx);
            if (idx == targetVarPtr->value.length() && isfinite(currentVarNumericValue))
            {
                // 전체 문자열이 파싱되었고 유한한 숫자인지 확인
                currentVarIsNumeric = true;
            }
        }
        catch (const exception &)
        {
            // 파싱 실패 시 currentVarIsNumeric는 false로 유지
        }

        // 더할 값도 숫자인지 확인
        // bool valueToAddIsNumeric = (valueToAddOp.type == OperandValue::Type::NUMBER && isfinite(valueToAddOp.asNumber()));
        // Let's make this more robust:
        double valueToAddNumVal = 0.0;
        bool valueToAddIsActuallyNumeric = false;
        if (valueToAddOp.type == OperandValue::Type::NUMBER)
        {
            valueToAddNumVal = valueToAddOp.number_val;
            valueToAddIsActuallyNumeric = isfinite(valueToAddNumVal);
        }
        else if (valueToAddOp.type == OperandValue::Type::STRING)
        {
            if (!valueToAddOp.string_val.empty())
            { // Avoid stod on empty string
                try
                {
                    size_t add_idx = 0;
                    valueToAddNumVal = stod(valueToAddOp.string_val, &add_idx);
                    if (add_idx == valueToAddOp.string_val.length() && isfinite(valueToAddNumVal))
                    {
                        valueToAddIsActuallyNumeric = true;
                    }
                }
                catch (const exception &)
                {
                    // valueToAddIsActuallyNumeric remains false
                }
            }
        }

        if (currentVarIsNumeric && valueToAddIsActuallyNumeric)
        {
            // 둘 다 숫자면 덧셈
            double sumValue = currentVarNumericValue + valueToAddNumVal;

            // EntryJS의 toFixed와 유사한 효과를 내기 위해 to_string 사용 후 후처리
            string resultStr = to_string(sumValue);
            resultStr.erase(resultStr.find_last_not_of('0') + 1, string::npos);
            if (!resultStr.empty() && resultStr.back() == '.')
            {
                resultStr.pop_back();
            }
            targetVarPtr->value = resultStr;
            engine.EngineStdOut(
                "Variable '" + variableIdToFind + "' (numeric) changed by " + valueToAddOp.asString() + " to " +
                    targetVarPtr->value,
                3, executionThreadId);
        }
        else
        {
            // 하나라도 숫자가 아니면 문자열 이어붙이기
            targetVarPtr->value = targetVarPtr->value + valueToAddOp.asString();
            engine.EngineStdOut(
                "Variable '" + variableIdToFind + "' (string) concatenated with " + valueToAddOp.asString() + " to " +
                    targetVarPtr->value,
                3, executionThreadId);
        }
        if (targetVarPtr->isCloud)
        {
            engine.saveCloudVariablesToJson();
        }
    }
    else if (BlockType == "set_variable")
    {
        // params: [VARIABLE_ID_STRING, VALUE_TO_ADD_OR_CONCAT, null, null]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            engine.EngineStdOut(
                "change_variable block for " + objectId +
                    " has insufficient parameters. Expected VARIABLE_ID and VALUE.",
                2, executionThreadId);
            return;
        } // 1. 변수 ID 가져오기 (항상 문자열 드롭다운)
        OperandValue variableIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (variableIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("set_variable block for " + objectId + ": VARIABLE_ID parameter (index 0) did not resolve to a string. Value: " + variableIdOp.asString(), 2,
                                executionThreadId);
            return;
        }
        string variableIdToFind = variableIdOp.asString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("set_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return;
        }

        // 2. 설정 할 값 가져오기
        OperandValue valueToSet = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);

        // 3. 변수 찾기 (로컬 우선, 없으면 전역)
        HUDVariableDisplay *targetVarPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.id == variableIdToFind && hudVar.objectId == objectId)
            {
                targetVarPtr = &hudVar;
                break;
            }
        }
        if (!targetVarPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.id == variableIdToFind && hudVar.objectId.empty())
                {
                    targetVarPtr = &hudVar;
                    break;
                }
            }
        }

        if (!targetVarPtr)
        {
            engine.EngineStdOut(
                "set_variable block for " + objectId + ": Variable '" + variableIdToFind + "' not found.", 1,
                executionThreadId);
            return;
        }

        if (valueToSet.type == OperandValue::Type::STRING)
        {
            targetVarPtr->value = valueToSet.asString();
        }
        else
        {
            targetVarPtr->value = valueToSet.asNumber();
        }

        if (targetVarPtr->isCloud)
        {
            engine.saveCloudVariablesToJson();
        }
    }
    else if (BlockType == "show_variable")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut(
                "show_variable block for " + objectId +
                    " has insufficient parameters. Expected VARIABLE_ID and VALUE.",
                2, executionThreadId);
            return;
        }

        OperandValue variableIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (variableIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("show_variable block for " + objectId + ": VARIABLE_ID parameter (index 0) did not resolve to a string. Value: " + variableIdOp.asString(), 2,
                                executionThreadId);
            return;
        }
        string variableIdToFind = variableIdOp.asString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("show_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return;
        }

        HUDVariableDisplay *targetVarPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.id == variableIdToFind && hudVar.objectId == objectId)
            {
                targetVarPtr = &hudVar;
                break;
            }
        }
        if (!targetVarPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.id == variableIdToFind && hudVar.objectId.empty())
                {
                    targetVarPtr = &hudVar;
                    break;
                }
            }
        }
        targetVarPtr->isVisible = true;
    }
    else if (BlockType == "hide_variable")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut(
                "hide_variable block for " + objectId +
                    " has insufficient parameters. Expected VARIABLE_ID and VALUE.",
                2, executionThreadId);
            return;
        }

        OperandValue variableIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (variableIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("hide_variable block for " + objectId + ": VARIABLE_ID parameter (index 0) did not resolve to a string. Value: " + variableIdOp.asString(), 2,
                                executionThreadId);
            return;
        }
        string variableIdToFind = variableIdOp.asString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("hide_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return;
        }

        HUDVariableDisplay *targetVarPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.id == variableIdToFind && hudVar.objectId == objectId)
            {
                targetVarPtr = &hudVar;
                break;
            }
        }
        if (!targetVarPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.id == variableIdToFind && hudVar.objectId.empty())
                {
                    targetVarPtr = &hudVar;
                    break;
                }
            }
        }
        targetVarPtr->isVisible = false;
    }
    else if (BlockType == "add_value_to_list")
    {
        // 리스트에 항목을 추가합니다.
        // 파라미터: [LIST_ID_STRING (드롭다운), VALUE_TO_ADD (모든 타입 가능)]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            // LIST_ID와 VALUE, 총 2개의 파라미터 필요
            engine.EngineStdOut(
                "add_value_to_list block for " + objectId + " has insufficient parameters. Expected LIST_ID and VALUE.",
                2, executionThreadId);
            return;
        } // 1. 리스트 ID 가져오기 (항상 드롭다운 메뉴의 문자열)
        OperandValue listIdOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId); // 실제 리스트 ID는 paramsJson[1]에서 가져옴
        if (listIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "add_value_to_list block for " + objectId + ": LIST_ID parameter (index 1) did not resolve to a string. Value: " + listIdOp.asString(), 2,
                executionThreadId);
            return;
        }
        string listIdToFind = listIdOp.asString();
        if (listIdToFind.empty())
        {
            engine.EngineStdOut("add_value_to_list block for " + objectId + ": received an empty LIST_ID.", 2,
                                executionThreadId);
            return;
        } // 2. 리스트에 추가할 값 가져오기
        OperandValue valueOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId); // 실제 추가할 값은 paramsJson[0]에서 가져옴
        string valueToAdd = valueOp.asString();                                                           // 모든 Operand 타입을 문자열로 변환하여 리스트에 저장

        // 빈 문자열 체크
        if (valueToAdd.empty() && valueOp.type == OperandValue::Type::STRING)
        {
            engine.EngineStdOut("add_value_to_list block for " + objectId + ": Cannot add empty value to list.", 1,
                                executionThreadId);
            return;
        }

        // 3. 대상 리스트 찾기 (지역 리스트 우선, 없으면 전역 리스트)
        HUDVariableDisplay *targetListPtr = nullptr;

        // 지역 리스트 검색 (현재 오브젝트에 속한 리스트)
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listIdToFind && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }

        // 지역 리스트가 없으면 전역 리스트 검색
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listIdToFind && hudVar.objectId.empty())
                {
                    // 전역 리스트 조건 확인
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }

        // 4. 리스트를 찾았는지 확인 후 값 추가
        if (targetListPtr)
        {
            // 혹시라도 타입이 list가 아닌 경우를 대비한 안전장치 (정상적이라면 발생하지 않음)
            if (targetListPtr->variableType != "list")
            {
                engine.EngineStdOut(
                    "add_value_to_list block for " + objectId + ": Variable '" + listIdToFind +
                        "' found but is not a list.",
                    2, executionThreadId);
                return;
            }

            targetListPtr->array.push_back({valueToAdd}); // 새로운 ListItem으로 추가
            if (targetListPtr->isCloud)                   // 클라우드 저장 흉내
            {
                engine.saveCloudVariablesToJson();
            }
            engine.EngineStdOut("DEBUG: add_value_to_list - block.paramsJson: " + block.paramsJson.dump(), 3, executionThreadId);
        }
        else
        {
            engine.EngineStdOut("add_value_to_list block for " + objectId + ": List '" + listIdToFind + "' not found.",
                                1, executionThreadId);
        }
    }
    else if (BlockType == "remove_value_from_list")
    {
        // 리스트에서 특정 인덱스의 항목을 삭제합니다.
        // 파라미터: [LIST_ID_STRING (드롭다운), INDEX_TO_REMOVE (숫자 또는 숫자 반환 블록)]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 2)
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId +
                    " has insufficient parameters. Expected LIST_ID and INDEX.",
                2, executionThreadId);
            return;
        }

        // 1. 리스트 ID 가져오기 (항상 드롭다운 메뉴의 문자열)
        OperandValue listIdOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        if (listIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": LIST_ID parameter (index 0) did not resolve to a string. Value: " + listIdOp.asString(), 2,
                executionThreadId);
            return;
        }
        string listIdToFind = listIdOp.asString();

        if (listIdToFind.empty())
        {
            engine.EngineStdOut("remove_value_from_list block for " + objectId + ": received an empty LIST_ID.", 2,
                                executionThreadId);
            return;
        }

        // 2. 삭제할 인덱스 값 가져오기
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (indexOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": INDEX parameter is not a number. Value: " + indexOp.asString(), 2, executionThreadId);
            return;
        }
        double index_1_based_double = indexOp.asNumber();

        // 인덱스가 유한한 정수인지 확인
        if (!isfinite(index_1_based_double) || floor(index_1_based_double) != index_1_based_double)
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": INDEX '" + indexOp.asString() +
                    "' is not a valid integer.",
                2, executionThreadId);
            return;
        }
        auto index_1_based = static_cast<long long>(index_1_based_double);

        // 3. 대상 리스트 찾기 (지역 리스트 우선, 없으면 전역 리스트)
        HUDVariableDisplay *targetListPtr = nullptr;
        // 지역 리스트 검색 (현재 오브젝트에 속한 리스트)
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listIdToFind && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        // 지역 리스트가 없으면 전역 리스트 검색
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listIdToFind && hudVar.objectId.empty())
                {
                    // 전역 리스트 조건 확인
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }

        if (!targetListPtr)
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": List '" + listIdToFind + "' not found.", 1,
                executionThreadId);
            return;
        }

        // 혹시라도 타입이 list가 아닌 경우를 대비한 안전장치
        if (targetListPtr->variableType != "list")
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": Variable '" + listIdToFind +
                    "' found but is not a list.",
                2, executionThreadId);
            return;
        }

        vector<ListItem> &listArray = targetListPtr->array;

        // 4. 리스트가 비어있는지 확인
        if (listArray.empty())
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": List '" + listIdToFind +
                    "' is empty. Cannot remove item.",
                1, executionThreadId);
            return;
        }

        // 5. 1기반 인덱스 유효성 검사
        // 유효한 1기반 인덱스 범위: 1 <= index_1_based <= listArray.size()
        if (index_1_based < 1 || index_1_based > static_cast<long long>(listArray.size()))
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": Index " + to_string(index_1_based) +
                    " is out of bounds for list '" + listIdToFind + "' (size: " + to_string(listArray.size()) + ").",
                1, executionThreadId);
            return;
        }

        // 6. 0기반 인덱스로 변환하여 항목 삭제
        size_t index_0_based = static_cast<size_t>(index_1_based - 1);

        string removedItemData = listArray[index_0_based].data; // 로깅을 위해 삭제될 데이터 저장
        listArray.erase(listArray.begin() + index_0_based);

        engine.EngineStdOut(
            "Removed item at index " + to_string(index_1_based) + " (value: '" + removedItemData + "') from list '" + listIdToFind + "' for object " + objectId, 0, executionThreadId);
        if (targetListPtr->isCloud)
        {
            engine.saveCloudVariablesToJson();
        }
    }
    else if (BlockType == "insert_value_to_list")
    {
        // 특정 인덱스에 항목 삽입
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 3)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": insufficient parameters", 2);
            return;
        }
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue listIdToFindOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue valueOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);

        // 빈 문자열 체크
        if (valueOp.asString().empty() && valueOp.type == OperandValue::Type::STRING)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": Cannot insert empty value to list.", 1,
                                executionThreadId);
            return;
        }

        double index_1_based_double = indexOp.asNumber();
        if (listIdToFindOp.type != OperandValue::Type::STRING || valueOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("listId or valueOp is not a string objId:" + objectId, 2);
            return;
        }
        if (indexOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("indexOp is not a number objId:" + objectId, 2);
            return;
        }
        auto index_1_based = static_cast<long long>(index_1_based_double);
        HUDVariableDisplay *targetListPtr = nullptr;
        // 지역 리스트 검색 (현재 오브젝트에 속한 리스트)
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listIdToFindOp.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        // 지역 리스트가 없으면 전역 리스트 검색
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listIdToFindOp.asString() && hudVar.objectId.empty())
                {
                    // 전역 리스트 조건 확인
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }

        // 혹시라도 타입이 list가 아닌 경우를 대비한 안전장치
        if (targetListPtr->variableType != "list")
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": Variable '" + listIdToFindOp.asString() +
                    "' found but is not a list.",
                2, executionThreadId);
            return;
        }
        vector<ListItem> &listArray = targetListPtr->array;
        if (index_1_based < 1 || index_1_based > static_cast<long long>(listArray.size()))
        {
            engine.EngineStdOut("insert_value_to_list block for" + objectId + ": index " + to_string(index_1_based_double) + " is out of bounds for list '" + listIdToFindOp.asString() +
                                    "' (size: " + to_string(listArray.size()) + ").",
                                1, executionThreadId);
            return;
        }
        size_t index_0_based = static_cast<size_t>(index_1_based - 1);
        listArray.insert(listArray.begin() + index_0_based, {valueOp.asString()});
        if (targetListPtr->isCloud)
        {
            engine.saveCloudVariablesToJson();
        }
        engine.EngineStdOut("Inserted value '" + valueOp.asString() + "' at index " + to_string(index_1_based) + " (value: '" + valueOp.asString() + "') to list '");
    }
    else if (BlockType == "change_value_list_index")
    {
        // 특정 인덱스에 항목 변경
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 3)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": insufficient parameters", 2);
            return;
        }
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1], executionThreadId);
        OperandValue listIdToFindOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        OperandValue valueOp = getOperandValue(engine, objectId, block.paramsJson[2], executionThreadId);

        // 빈 문자열 체크
        if (valueOp.asString().empty() && valueOp.type == OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_value_list_index block for " + objectId + ": Cannot set empty value to list.", 1,
                                executionThreadId);
            return;
        }
        double index_1_based_double = indexOp.asNumber();
        if (listIdToFindOp.type != OperandValue::Type::STRING || valueOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("listId or valueOp is not a string objId:" + objectId, 2);
            return;
        }
        if (indexOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("indexOp is not a number objId:" + objectId, 2);
            return;
        }
        auto index_1_based = static_cast<long long>(index_1_based_double);
        HUDVariableDisplay *targetListPtr = nullptr;
        // 지역 리스트 검색 (현재 오브젝트에 속한 리스트)
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listIdToFindOp.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        // 지역 리스트가 없으면 전역 리스트 검색
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listIdToFindOp.asString() && hudVar.objectId.empty())
                {
                    // 전역 리스트 조건 확인
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }

        // 혹시라도 타입이 list가 아닌 경우를 대비한 안전장치
        if (targetListPtr->variableType != "list")
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": Variable '" + listIdToFindOp.asString() +
                    "' found but is not a list.",
                2, executionThreadId);
            return;
        }
        vector<ListItem> &listArray = targetListPtr->array;
        if (index_1_based < 1 || index_1_based > static_cast<long long>(listArray.size()))
        {
            engine.EngineStdOut("insert_value_to_list block for" + objectId + ": index " + to_string(index_1_based_double) + " is out of bounds for list '" + listIdToFindOp.asString() +
                                    "' (size: " + to_string(listArray.size()) + ").",
                                1, executionThreadId);
            return;
        }
        size_t index_0_based = static_cast<size_t>(index_1_based - 1);
        listArray[index_0_based].data = valueOp.asString();
    }
    else if (BlockType == "show_list")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": insufficient parameters", 2);
            return;
        }
        OperandValue listIdtofindOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (listIdtofindOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("listId is not a string objId:" + objectId, 2);
            return;
        }
        HUDVariableDisplay *targetListPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listIdtofindOp.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listIdtofindOp.asString() && hudVar.objectId.empty())
                {
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }
        targetListPtr->isVisible = true;
    }
    else if (BlockType == "hide_list")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": insufficient parameters", 2);
            return;
        }
        OperandValue listIdtofindOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (listIdtofindOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("listId is not a string objId:" + objectId, 2);
            return;
        }
        HUDVariableDisplay *targetListPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.id == listIdtofindOp.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.id == listIdtofindOp.asString() && hudVar.objectId.empty())
                {
                    targetListPtr = &hudVar;
                    break;
                }
            }
        }
        targetListPtr->isVisible = false;
    }
}

/**
 * @brief 흐름 블록
 *
 */
void Flow(string BlockType, Engine &engine, const string &objectId, const Block &block,
          const string &executionThreadId, const string &sceneIdAtDispatch, float deltaTime)
{
    auto entity = engine.getEntityByIdShared(objectId);
    if (!entity)
    {
        engine.EngineStdOut("Flow 'wait_second': Entity " + objectId + " not found.", 2, executionThreadId);
        return;
    }
    if (BlockType == "wait_second")
    {

        // Flow 함수는 Entity의 setScriptWait를 호출하여 대기 상태 설정을 요청합니다.
        // 실제 대기(SDL_Delay)는 Entity::executeScript의 메인 루프에서 처리됩니다.
        if (!block.paramsJson.is_array() || block.paramsJson.empty() || block.paramsJson[0].is_null())
        {
            engine.EngineStdOut("Flow 'wait_second' for " + objectId + ": Missing or invalid time parameter.", 2, executionThreadId);
            // engine.terminateScriptExecution(executionThreadId);
            return;
        }

        OperandValue secondsOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (secondsOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("Flow 'wait_second' for " + objectId + ": Time parameter is not a number. Value: " + secondsOp.asString(), 2, executionThreadId);
            // engine.terminateScriptExecution(executionThreadId);
            return;
        }

        double secondsToWait = secondsOp.asNumber();
        if (secondsToWait < 0)
        {
            secondsToWait = 0;
        }

        Uint64 waitEndTime = SDL_GetTicks() + static_cast<Uint64>(secondsToWait * 1000.0);
        // Set the wait state *before* performing the active wait
        entity->setScriptWait(executionThreadId, waitEndTime, block.id, Entity::WaitType::EXPLICIT_WAIT_SECOND);

        engine.EngineStdOut("Flow 'wait_second': " + objectId + " (Thread: " + executionThreadId + ") setting wait for " + to_string(secondsToWait) + "s. Block ID: " + block.id + ". Script will pause.", 3, executionThreadId);
        // The Entity::executeScript method will see the EXPLICIT_WAIT_SECOND
        // and pause the script. Entity::resumeExplicitWaitScripts will resume it.
        // No active waiting (performActiveWait) is done here anymore for wait_second.
    }
    else if (BlockType == "repeat_basic")
    {
        // Entity 유효성 검사 추가
        if (!entity)
        {
            engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + ": Entity is null.", 2, executionThreadId);
            return;
        }

        // 기본 반복 블록 구현
        // 반복 횟수 파라미터 가져오기
        if (!block.paramsJson.is_array() || block.paramsJson.empty() || block.paramsJson[0].is_null())
        {
            engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + ": Missing or invalid iteration count parameter.", 2, executionThreadId);
            throw ScriptBlockExecutionError("반복 횟수 파라미터가 부족하거나 유효하지 않습니다.", block.id, BlockType, objectId, "Missing or invalid iteration count parameter.");
        }
        OperandValue iterOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (iterOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + ": Iteration count parameter is not a number. Value: " + iterOp.asString(), 2, executionThreadId);
            throw ScriptBlockExecutionError("반복 횟수 파라미터가 숫자가 아닙니다.", block.id, BlockType, objectId, "Iteration count parameter is not a number.");
        }
        double iterNumber = iterOp.asNumber();
        if (iterNumber < 0)
        {
            engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + ": Iteration count cannot be negative. Value: " + to_string(iterNumber), 2, executionThreadId);
            throw ScriptBlockExecutionError("반복 횟수는 음수일 수 없습니다.", block.id, BlockType, objectId, "Iteration count cannot be negative.");
        }
        int iterCount = static_cast<int>(floor(iterNumber));

        // DO 스테이트먼트 스크립트 가져오기
        if (block.statementScripts.empty())
        {
            engine.EngineStdOut("repeat_basic block " + block.id + " for " + objectId + " has no DO statement.", 1, executionThreadId);
            return; // 반복할 내용이 없으면 바로 종료
        }

        const Script &doScript = block.statementScripts[0]; // 첫 번째 statementScript가 DO 블록이라고 가정

        // Debugging: Print the content of doScript
        // if (!doScript.blocks.empty())
        // {
        //     engine.EngineStdOut("doScript for block " + block.id + " (object " + objectId + ") contains " + to_string(doScript.blocks.size()) + " inner blocks:", 3, executionThreadId);
        //     for (size_t j = 0; j < doScript.blocks.size(); ++j)
        //     {
        //         const Block &innerBlock = doScript.blocks[j];
        //         string inner_params_dump = "null";
        //         if (!innerBlock.paramsJson.is_null()) {
        //            inner_params_dump = innerBlock.paramsJson.dump();
        //         }
        //         engine.EngineStdOut("  Inner Block [" + to_string(j) + "]: ID=" + innerBlock.id + ", Type=" + innerBlock.type + ", ParamsJSON=" + inner_params_dump, 3, executionThreadId);
        //     }
        // }
        // else
        // {
        //     engine.EngineStdOut("doScript for block " + block.id + " (object " + objectId + ") is empty (no inner blocks).", 3, executionThreadId);
        // }
        Entity::ScriptThreadState *pThreadState = nullptr;
        if (entity)
        {                                                              // entity 포인터 유효성 검사
            lock_guard<recursive_mutex> lock(entity->getStateMutex()); // public getter 사용
            auto it = entity->scriptThreadStates.find(executionThreadId);
            if (it != entity->scriptThreadStates.end())
            {
                pThreadState = &it->second;
            }
        }

        if (!pThreadState)
        { // pThreadState가 여전히 nullptr이면 오류 처리
            engine.EngineStdOut("Critical: ScriptThreadState not found for " + executionThreadId + " in repeat_basic, or entity is null.", 2, executionThreadId);
            return; // 또는 예외 발생
        }
        int startIteration = 0;
        // Check if we are resuming this specific repeat_basic block
        if (pThreadState->loopCounters.count(block.id))
        {
            startIteration = pThreadState->loopCounters[block.id];
            engine.EngineStdOut("repeat_basic: " + objectId + " resuming from iteration " + to_string(startIteration) + "/" + to_string(iterCount) + ". Block ID: " + block.id, 3, executionThreadId);
        }
        else
        {
            engine.EngineStdOut("repeat_basic: " + objectId + " starting new loop with " + to_string(iterCount) + " iterations. Block ID: " + block.id, 3, executionThreadId);
        }
        // 반복 횟수만큼 내부 블록들을 동기적으로 실행
        for (int i = startIteration; i < iterCount; ++i)
        {
            // 매 반복 시작 시 엔진 종료 또는 씬 변경 확인
            if (engine.m_isShuttingDown.load(memory_order_relaxed))
            {
                engine.EngineStdOut("repeat_basic for " + objectId + " cancelled due to engine shutdown.", 1, executionThreadId);
                if (pThreadState)
                    pThreadState->loopCounters.erase(block.id);
                break;
            }

            string currentEngineSceneId = engine.getCurrentSceneId();
            const ObjectInfo *objInfo = engine.getObjectInfoById(objectId);
            bool isGlobalEntity = (objInfo && (objInfo->sceneId == "global" || objInfo->sceneId.empty()));

            if (!isGlobalEntity && objInfo && objInfo->sceneId != currentEngineSceneId)
            {
                engine.EngineStdOut("repeat_basic for " + objectId + " halted. Entity no longer in current scene " + currentEngineSceneId + ".", 1, executionThreadId);
                if (pThreadState)
                    pThreadState->loopCounters.erase(block.id);
                break;
            }

            engine.EngineStdOut("repeat_basic: " + objectId + " starting iteration " + to_string(i) + "/" + to_string(iterCount) + ". Block ID: " + block.id, 3, executionThreadId);
            // 내부 블록 실행 전에 현재 반복 카운터를 저장
            {
                lock_guard<recursive_mutex> lock(entity->getStateMutex());
                int nextIteration = i + 1;
                pThreadState->loopCounters[block.id] = nextIteration;
                engine.EngineStdOut("repeat_basic: Saved next iteration counter " + to_string(nextIteration) + " for block " + block.id, 3, executionThreadId);
            }

            // 내부 블록 실행
            engine.EngineStdOut("repeat_basic: Executing iteration " + to_string(i) + " for block " + block.id, 3, executionThreadId);
            executeBlocksSynchronously(engine, objectId, doScript.blocks, executionThreadId, sceneIdAtDispatch, deltaTime); // 내부 블록 실행 후 대기 상태 확인
            if (entity)
            {
                bool shouldBreakLoop = false;
                // Check and reset breakLoopRequested under lock
                {
                    lock_guard<recursive_mutex> lock(entity->getStateMutex());
                    auto it_state = entity->scriptThreadStates.find(executionThreadId);
                    if (it_state != entity->scriptThreadStates.end() && it_state->second.breakLoopRequested)
                    {
                        shouldBreakLoop = true;
                        it_state->second.breakLoopRequested = false; // Reset flag
                    }
                }
                if (shouldBreakLoop)
                {
                    engine.EngineStdOut("repeat_basic for " + objectId + " breaking loop due to stop_repeat. Iteration " + to_string(i) + ". Block ID: " + block.id, 0, executionThreadId);
                    if (pThreadState)
                        pThreadState->loopCounters.erase(block.id); // Reset loop counter as the loop is terminated
                    break;                                          // Break from the C++ for loop
                }

                bool shouldContinue = false;
                // Check and reset continueLoopRequested under lock
                {
                    lock_guard<recursive_mutex> lock(entity->getStateMutex());
                    auto it_state = entity->scriptThreadStates.find(executionThreadId);
                    if (it_state != entity->scriptThreadStates.end() && it_state->second.continueLoopRequested)
                    {
                        shouldContinue = true;
                        it_state->second.continueLoopRequested = false; // Reset flag
                    }
                }
                if (shouldContinue)
                {
                    engine.EngineStdOut("repeat_basic for " + objectId + " continuing to next iteration. Iteration " + to_string(i) + ". Block ID: " + block.id, 0, executionThreadId);
                    continue; // Skip to the next iteration of the C++ for loop
                }

                // 내부 블록이 대기를 설정하지 않은 경우 프레임 동기화를 위한 대기 추가
                bool innerBlockIsWaiting = false;
                Entity::WaitType innerWaitType = Entity::WaitType::NONE;
                string innerWaitingBlockId;
                {
                    lock_guard<recursive_mutex> lock(entity->getStateMutex());
                    auto it_check = entity->scriptThreadStates.find(executionThreadId);
                    if (it_check != entity->scriptThreadStates.end() && it_check->second.isWaiting)
                    {
                        innerBlockIsWaiting = true;
                        innerWaitType = it_check->second.currentWaitType;
                        innerWaitingBlockId = it_check->second.blockIdForWait;
                    }
                }
                if (innerBlockIsWaiting)
                {
                    // 내부 블록이 대기 중일 때
                    engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + " pausing at iteration " + to_string(i) +
                                            " because inner block " + innerWaitingBlockId + " set wait type " + BlockTypeEnumToString(innerWaitType) +
                                            ". Will resume here.",
                                        3, executionThreadId);
                    return;
                }
                else
                {
                    // 내부 블록이 대기하지 않는 경우 프레임 동기화를 위한 대기 추가
                    double idealFrameTime = 1000.0 / max(1, engine.specialConfig.TARGET_FPS);
                    Uint32 frameDelay = static_cast<Uint32>(std::clamp(idealFrameTime, static_cast<double>(MIN_LOOP_WAIT_MS), 33.0)); // 최소 1ms, 최대 33ms
                    entity->setScriptWait(executionThreadId, frameDelay, block.id, Entity::WaitType::BLOCK_INTERNAL);
                    engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + " iteration " + to_string(i) +
                                            " completed, adding frame sync wait (" + to_string(frameDelay) + "ms)",
                                        3, executionThreadId);
                    return;
                }
            }
        }
        // Loop finished all iterations normally
        if (pThreadState)
            pThreadState->loopCounters.erase(block.id); // Reset for the next time this repeat_basic block might run.
        engine.EngineStdOut("repeat_basic: " + objectId + " loop completed. Block ID: " + block.id, 0, executionThreadId);
    }
    else if (BlockType == "repeat_inf")
    {
        // 무한반복 이게 맞는듯
        if (block.statementScripts.empty())
        {
            // DO 블록에 해당하는 스크립트 자체가 없는 경우
            engine.EngineStdOut("DO statement script missing in repeat_inf block. objectId:" + objectId, 1, executionThreadId);
            return;
        }

        Entity::ScriptThreadState *pThreadState = nullptr;
        if (entity)
        { // entity 포인터 유효성 검사
            lock_guard<recursive_mutex> lock(entity->getStateMutex());
            auto it = entity->scriptThreadStates.find(executionThreadId);
            if (it != entity->scriptThreadStates.end())
            {
                pThreadState = &it->second;
            }
        }

        if (!pThreadState)
        { // pThreadState가 여전히 nullptr이면 오류 처리
            engine.EngineStdOut("Critical: ScriptThreadState not found for " + executionThreadId + " in repeat_inf, or entity is null.", 2, executionThreadId);
            return;
        }

        const Script &doScript = block.statementScripts[0]; // 첫 번째 statementScript가 DO 블록이라고 가정

        // Debugging: Print the content of doScript
        if (!doScript.blocks.empty())
        {
            engine.EngineStdOut("doScript for block " + block.id + " (object " + objectId + ") contains " + to_string(doScript.blocks.size()) + " inner blocks:", 3, executionThreadId);
            for (size_t j = 0; j < doScript.blocks.size(); ++j)
            {
                const Block &innerBlock = doScript.blocks[j];
                string inner_params_dump = "null";
                if (!innerBlock.paramsJson.is_null())
                {
                    inner_params_dump = innerBlock.paramsJson.dump();
                }
                engine.EngineStdOut("  Inner Block [" + to_string(j) + "]: ID=" + innerBlock.id + ", Type=" + innerBlock.type + ", ParamsJSON=" + inner_params_dump, 3, executionThreadId);
            }
        }
        else
        {
            engine.EngineStdOut("doScript for block " + block.id + " (object " + objectId + ") is empty (no inner blocks).", 3, executionThreadId);
        }

        if (doScript.blocks.empty())
        {
            engine.EngineStdOut("Warning: repeat_inf for " + objectId + " has an empty DO statement. This will be a very tight loop if not handled carefully.", 1, executionThreadId);
        }

        while (true)
        {
            // 매 반복 시작 시 엔진 종료 또는 씬 변경 확인
            if (engine.m_isShuttingDown.load(memory_order_relaxed))
            {
                engine.EngineStdOut("repeat_inf for " + objectId + " cancelled due to engine shutdown.", 1, executionThreadId);
                break; // 루프 종료
            }

            string currentEngineSceneId = engine.getCurrentSceneId();
            const ObjectInfo *objInfo = engine.getObjectInfoById(objectId);
            bool isGlobalEntity = (objInfo && (objInfo->sceneId == "global" || objInfo->sceneId.empty()));

            if (!isGlobalEntity && objInfo && objInfo->sceneId != currentEngineSceneId)
            {
                engine.EngineStdOut("repeat_inf for " + objectId + " halted. Entity no longer in current scene " + currentEngineSceneId + ".", 1, executionThreadId);
                break; // 루프 종료
            }

            executeBlocksSynchronously(engine, objectId, doScript.blocks, executionThreadId, sceneIdAtDispatch, deltaTime); // Pass deltaTime

            if (entity)
            {
                if (entity->isScriptWaiting(executionThreadId))
                {
                    // 내부 블록이 대기를 설정했음 (예: move_xy_time). Flow 함수 종료.
                    bool shouldBreakLoop = false;
                    { // Scope for lock
                        lock_guard<recursive_mutex> lock(entity->getStateMutex());
                        auto it_check_break = entity->scriptThreadStates.find(executionThreadId);
                        if (it_check_break != entity->scriptThreadStates.end())
                        {
                            if (it_check_break->second.breakLoopRequested)
                            {
                                shouldBreakLoop = true;
                                it_check_break->second.breakLoopRequested = false; // Reset flag
                            }
                        }
                    }
                    if (shouldBreakLoop)
                    {
                        engine.EngineStdOut("repeat_inf for " + objectId + " breaking loop due to stop_repeat. Block ID: " + block.id, 0, executionThreadId);
                        break; // Break from the C++ while(true) loop
                    }

                    bool shouldContinue = false;
                    { // Scope for lock
                        lock_guard<recursive_mutex> lock(entity->getStateMutex());
                        auto it_check_continue = entity->scriptThreadStates.find(executionThreadId);
                        if (it_check_continue != entity->scriptThreadStates.end() && it_check_continue->second.continueLoopRequested)
                        {
                            shouldContinue = true;
                            it_check_continue->second.continueLoopRequested = false; // Reset flag
                        }
                    }
                    if (shouldContinue)
                    {
                        engine.EngineStdOut("repeat_inf for " + objectId + " continuing to next iteration. Block ID: " + block.id, 3, executionThreadId);
                        continue; // Skip to the next iteration of the C++ while(true) loop
                    }
                    engine.EngineStdOut("Flow 'repeat_inf' for " + objectId + " pausing because an inner block set a wait state.", 3, executionThreadId);
                    return;
                }
                else
                {
                    // 내부 블록이 대기를 설정하지 않음 (모두 즉시 실행됨).
                    // 매 프레임마다 적절한 대기를 주어 다른 엔티티에게 실행 기회를 제공
                    if (pThreadState->loopCounters.count(block.id) == 0)
                    {
                        pThreadState->loopCounters[block.id] = 0;
                    }
                    pThreadState->loopCounters[block.id]++;
                    double idealFrameTime = 1000.0 / max(1, engine.specialConfig.TARGET_FPS);
                    Uint32 frameDelay = static_cast<Uint32>(std::clamp(idealFrameTime, static_cast<double>(MIN_LOOP_WAIT_MS), 33.0)); // 최소 1ms, 최대 33ms
                    entity->setScriptWait(executionThreadId, frameDelay, block.id, Entity::WaitType::BLOCK_INTERNAL);
                    engine.EngineStdOut("Flow 'repeat_inf' for " + objectId + " forcing frame sync wait (" + to_string(engine.specialConfig.TARGET_FPS) +
                                            "FPS, " + to_string(frameDelay) + "ms). Iteration: " +
                                            to_string(pThreadState->loopCounters[block.id]) + ", Block ID: " + block.id,
                                        3, executionThreadId);
                    return;
                }
            }
            else
            {
                // Entity가 null이 된 경우, 루프를 안전하게 종료합니다.
                break;
            }
            // executeBlocksSynchronously 내부에서도 종료/씬 변경을 확인하므로,
            // 만약 해당 함수가 return으로 중단되었다면 이 while 루프도 다음 반복에서 위의 break 조건에 걸릴 것입니다.
        }
    }
    else if (BlockType == "repeat_while_true")
    {
        // 될때까지 반복
        // params: [CONDITION_BLOCK]
        // statements: [DO_SCRIPT]

        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut("Flow 'repeat_while_true' for " + objectId + ": Missing condition parameter.", 2, executionThreadId);
            throw ScriptBlockExecutionError("조건 파라미터가 부족합니다.", block.id, BlockType, objectId, "Missing condition parameter.");
        }
        if (block.statementScripts.empty())
        {
            engine.EngineStdOut("Flow 'repeat_while_true' for " + objectId + ": Missing DO statement script.", 1, executionThreadId);
            return; // 반복할 내용이 없으면 바로 종료
        }

        const nlohmann::json &conditionJson = block.paramsJson[0]; // Condition block JSON
        const Script &doScript = block.statementScripts[0];        // First statementScript is the DO block

        engine.EngineStdOut("repeat_while_true: " + objectId + " starting loop. Block ID: " + block.id, 0, executionThreadId);

        while (true)
        {
            // Check for shutdown/scene change before evaluating condition or executing blocks
            if (engine.m_isShuttingDown.load(memory_order_relaxed))
            {
                engine.EngineStdOut("repeat_while_true for " + objectId + " cancelled due to engine shutdown.", 1, executionThreadId);
                break; // Exit loop
            }
            string currentEngineSceneId = engine.getCurrentSceneId();
            const ObjectInfo *objInfo = engine.getObjectInfoById(objectId);
            bool isGlobalEntity = (objInfo && (objInfo->sceneId == "global" || objInfo->sceneId.empty()));
            if (!isGlobalEntity && objInfo && objInfo->sceneId != currentEngineSceneId)
            {
                engine.EngineStdOut("repeat_while_true for " + objectId + " halted. Entity no longer in current scene " + currentEngineSceneId + ".", 1, executionThreadId);
                break; // Exit loop
            }

            // Evaluate the condition
            OperandValue conditionResult = getOperandValue(engine, objectId, conditionJson, executionThreadId);

            // The loop continues AS LONG AS the condition is TRUE.
            // So, if the condition is FALSE, we break the loop.
            // The condition is evaluated *before* executing the inner blocks.
            if (conditionResult.type != OperandValue::Type::BOOLEAN || !conditionResult.boolean_val)
            {
                engine.EngineStdOut("repeat_while_true for " + objectId + ": Condition is false or not boolean. Exiting loop. Block ID: " + block.id, 0, executionThreadId);
                break; // Condition is false, exit the loop
            }

            // If condition is true, execute inner blocks
            executeBlocksSynchronously(engine, objectId, doScript.blocks, executionThreadId, sceneIdAtDispatch, deltaTime); // Pass deltaTime

            // repeat_inf와 유사한 대기 처리 로직
            if (entity)
            {
                bool shouldBreakLoop = false;
                { // Scope for lock
                    lock_guard<recursive_mutex> lock(entity->getStateMutex());
                    auto it_check_break = entity->scriptThreadStates.find(executionThreadId);
                    if (it_check_break != entity->scriptThreadStates.end())
                    {
                        if (it_check_break->second.breakLoopRequested)
                        {
                            shouldBreakLoop = true;
                            it_check_break->second.breakLoopRequested = false; // Reset flag
                        }
                    }
                }
                if (shouldBreakLoop)
                {
                    engine.EngineStdOut("repeat_while_true for " + objectId + " breaking loop due to stop_repeat. Block ID: " + block.id, 0, executionThreadId);
                    break; // Break from the C++ while(true) loop
                }

                bool shouldContinue = false;
                { // Scope for lock
                    lock_guard<recursive_mutex> lock(entity->getStateMutex());
                    auto it_check_continue = entity->scriptThreadStates.find(executionThreadId);
                    if (it_check_continue != entity->scriptThreadStates.end() && it_check_continue->second.continueLoopRequested)
                    {
                        shouldContinue = true;
                        it_check_continue->second.continueLoopRequested = false; // Reset flag
                    }
                }
                if (shouldContinue)
                {
                    engine.EngineStdOut("repeat_while_true for " + objectId + " continuing to next iteration. Block ID: " + block.id, 0, executionThreadId);
                    continue; // Skip to the next iteration of the C++ while(true) loop
                }
                if (entity->isScriptWaiting(executionThreadId))
                {
                    engine.EngineStdOut("Flow 'repeat_while_true' for " + objectId + " pausing because an inner block set a wait state.", 3, executionThreadId); // WARN -> DEBUG
                    return;
                }
                else
                { // 조건이 계속 참이고 내부 블록이 즉시 실행되면 프레임 동기화를 위해 강제 대기
                    double idealFrameTime = 1000.0 / max(1, engine.specialConfig.TARGET_FPS);
                    Uint32 frameDelay = static_cast<Uint32>(std::clamp(idealFrameTime, static_cast<double>(MIN_LOOP_WAIT_MS), 33.0)); // 최소 1ms, 최대 33ms
                    entity->setScriptWait(executionThreadId, frameDelay, block.id, Entity::WaitType::BLOCK_INTERNAL);
                    engine.EngineStdOut("Flow 'repeat_while_true' for " + objectId + " forcing frame sync wait (" + to_string(engine.specialConfig.TARGET_FPS) +
                                            "FPS, " + to_string(frameDelay) + "ms). Block ID: " + block.id,
                                        3, executionThreadId);
                    return;
                }
            }
            else
            {
                break; // Entity가 null이 된 경우
            }
        }
    }
    else if (BlockType == "stop_repeat")
    {
        // This block signals that the current innermost loop should terminate.
        Entity::ScriptThreadState *pThreadState = nullptr;
        if (entity)
        { // entity pointer 유효성 검사
            lock_guard<recursive_mutex> lock(entity->getStateMutex());
            auto it = entity->scriptThreadStates.find(executionThreadId);
            if (it != entity->scriptThreadStates.end())
            {
                pThreadState = &it->second;
            }
        }

        if (pThreadState)
        {
            pThreadState->breakLoopRequested = true;
            engine.EngineStdOut("Flow 'stop_repeat': Break loop requested for " + objectId + " (Thread: " + executionThreadId + ")", 0, executionThreadId);
        }
        else
        {
            engine.EngineStdOut("Flow 'stop_repeat': ScriptThreadState not found for " + executionThreadId + " or entity is null. Cannot request break.", 2, executionThreadId);
        }
        // No wait is set here. The flag is checked by the loop constructs.
        // If this block is executed, and it's not inside a loop that checks the flag,
        // the flag will be set but have no immediate effect, which is acceptable.
    }
    else if (BlockType == "continue_repeat")
    {
        Entity::ScriptThreadState *pThreadState = nullptr;
        if (entity)
        { // entity 포인터 유효성 검사
            lock_guard<recursive_mutex> lock(entity->getStateMutex());
            auto it = entity->scriptThreadStates.find(executionThreadId);
            if (it != entity->scriptThreadStates.end())
            {
                pThreadState = &it->second;
            }
        }

        if (pThreadState)
        {
            pThreadState->continueLoopRequested = true;
            engine.EngineStdOut("Flow 'continue_repeat': Continue loop requested for " + objectId + " (Thread: " + executionThreadId + ")", 0, executionThreadId);
        }
        else
        {
            engine.EngineStdOut("Flow 'continue_repeat': ScriptThreadState not found for " + executionThreadId + " or entity is null. Cannot request continue.", 2, executionThreadId);
        }
        // 이 블록은 대기를 설정하지 않습니다. 플래그는 루프 구문에서 확인합니다.
    }
    else if (BlockType == "_if")
    {
        engine.EngineStdOut(
            std::format("Flow (_if): Evaluating _if block (ID: {}) for object '{}'. Condition param JSON: {}",
                        block.id, objectId, block.paramsJson[0].dump()),
            3, executionThreadId);
        // params: [CONDITION_BLOCK (BOOL), Indicator]
        // statements: [DO_SCRIPT (STACK)]
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut("Flow '_if' for " + objectId + ": Missing condition parameter (BOOL). Block ID: " + block.id, 2, executionThreadId);
            throw ScriptBlockExecutionError("조건 파라미터가 누락되었습니다.", block.id, BlockType, objectId, "Missing condition parameter.");
        }

        OperandValue conditionResult = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);

        bool conditionIsTrue = false;
        std::string conditionResultString = "N/A"; // For logging
        if (conditionResult.type == OperandValue::Type::BOOLEAN)
        {
            conditionIsTrue = conditionResult.asBool();
            conditionResultString = conditionResult.asBool() ? "true (boolean)" : "false (boolean)";
        }
        else if (conditionResult.type == OperandValue::Type::NUMBER)
        {
            conditionIsTrue = (conditionResult.asNumber() != 0); // 0이 아니면 참
        }
        else if (conditionResult.type == OperandValue::Type::STRING)
        {
            // 엔트리JS는 빈 문자열, "0", "false"를 거짓으로 간주. 그 외는 참.
            conditionIsTrue = !conditionResult.asString().empty() &&
                              conditionResult.asString() != "0" &&
                              conditionResult.asString() != "false";
            conditionResultString = "\"" + conditionResult.string_val + "\" (string) -> " + (conditionIsTrue ? "true" : "false");
        }
        else
        {
            engine.EngineStdOut(
                std::format("Flow (_if) for object '{}': Condition (block ID '{}') evaluated to {} (type: {}, string_val: '{}', number_val: {}, boolean_val: {}).",
                            objectId, block.id, (conditionIsTrue ? "true" : "false"),
                            static_cast<int>(conditionResult.type), conditionResult.string_val, conditionResult.number_val, conditionResult.boolean_val),
                3, executionThreadId);
            conditionResultString = "type " + std::to_string(static_cast<int>(conditionResult.type)) + " -> false";
        }

        engine.EngineStdOut(std::format("Flow (_if) for object '{}', block ID '{}': Condition evaluated to {}. (Raw condition result: {})",
                                        objectId, block.id, (conditionIsTrue ? "TRUE" : "FALSE"), conditionResultString),
                            3, executionThreadId);

        if (conditionIsTrue)
        {
            if (block.statementScripts.empty())
            {
                engine.EngineStdOut("Flow '_if' for " + objectId + ": No STACK (statement) found to execute even though condition was true. Block ID: " + block.id, 1, executionThreadId);
                return;
            }
            const Script &doScript = block.statementScripts[0]; // STACK 스크립트 (statementsKeyMap: { STACK: 0 })

            if (doScript.blocks.empty())
            {
                engine.EngineStdOut("Flow '_if' for " + objectId + ": STACK (statement) is empty. Block ID: " + block.id, 3, executionThreadId);
                // STACK이 비어있는 것은 유효한 상황일 수 있으므로, 여기서 return하지 않습니다.
            }
            else
            {
                engine.EngineStdOut("Flow '_if' for " + objectId + ": Condition true, executing STACK (statement). Block ID: " + block.id, 3, executionThreadId);
                executeBlocksSynchronously(engine, objectId, doScript.blocks, executionThreadId, sceneIdAtDispatch, deltaTime);

                // 내부 블록 실행 후 대기 상태 확인
                if (entity && entity->isScriptWaiting(executionThreadId))
                {
                    engine.EngineStdOut("Flow '_if' for " + objectId + " pausing because an inner block in STACK set a wait state. Block ID: " + block.id, 3, executionThreadId); // WARN -> DEBUG
                    return;                                                                                                                                                       // Flow 함수 종료, Entity::executeScript가 대기 처리
                }
            }
        }
        // 조건이 거짓이거나, 참이었지만 내부 블록 실행이 (대기 없이) 완료된 경우, 이 블록의 실행은 완료.
    }
    else if (BlockType == "if_else")
    {
        // params: [CONDITION_BLOCK (BOOL), Indicator, LineBreak]
        // statements: [DO_SCRIPT_IF (STACK_IF), DO_SCRIPT_ELSE (STACK_ELSE)]
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut("Flow 'if_else' for " + objectId + ": Missing condition parameter (BOOL). Block ID: " + block.id, 2, executionThreadId);
            throw ScriptBlockExecutionError("조건 파라미터가 누락되었습니다.", block.id, BlockType, objectId, "Missing condition parameter.");
        }

        OperandValue conditionResult = getOperandValue(engine, objectId,block.paramsJson[0], executionThreadId);

        bool conditionIsTrue = false;
        if (conditionResult.type == OperandValue::Type::BOOLEAN)
        {
            conditionIsTrue = conditionResult.asBool();
        }
        else if (conditionResult.type == OperandValue::Type::NUMBER)
        {
            conditionIsTrue = (conditionResult.asNumber() != 0);
        }
        else if (conditionResult.type == OperandValue::Type::STRING)
        {
            conditionIsTrue = !conditionResult.asString().empty() &&
                              conditionResult.asString() != "0" &&
                              conditionResult.asString() != "false";
        }
        else
        {
            engine.EngineStdOut("Flow 'if_else' for " + objectId + ": Condition evaluated to " + (conditionIsTrue ? "true" : "false") + ". Block ID: " + block.id, 3, executionThreadId);
        }
        const Script *scriptToExecute = nullptr;
        string stackToExecuteName;

        if (conditionIsTrue)
        {
            if (block.statementScripts.size() > 0) // STACK_IF (index 0)
            {
                scriptToExecute = &block.statementScripts[0];
                stackToExecuteName = "STACK_IF";
            }
            else
            {
                engine.EngineStdOut("Flow 'if_else' for " + objectId + ": STACK_IF (statement 0) is missing. Block ID: " + block.id, 1, executionThreadId);
            }
        }
        else // Condition is false
        {
            if (block.statementScripts.size() > 1) // STACK_ELSE (index 1)
            {
                scriptToExecute = &block.statementScripts[1];
                stackToExecuteName = "STACK_ELSE";
            }
            else
            {
                engine.EngineStdOut("Flow 'if_else' for " + objectId + ": STACK_ELSE (statement 1) is missing. Block ID: " + block.id, 1, executionThreadId);
            }
        }

        if (scriptToExecute)
        {
            if (scriptToExecute->blocks.empty())
            {
                engine.EngineStdOut("Flow 'if_else' for " + objectId + ": " + stackToExecuteName + " is empty. Block ID: " + block.id, 1, executionThreadId);
            }
            else
            {
                engine.EngineStdOut("Flow 'if_else' for " + objectId + ": Executing " + stackToExecuteName + ". Block ID: " + block.id, 3, executionThreadId);
                executeBlocksSynchronously(engine, objectId, scriptToExecute->blocks, executionThreadId, sceneIdAtDispatch, deltaTime);

                if (entity && entity->isScriptWaiting(executionThreadId))
                {
                    engine.EngineStdOut("Flow 'if_else' for " + objectId + " pausing because an inner block in " + stackToExecuteName + " set a wait state. Block ID: " + block.id, 3, executionThreadId); // WARN -> DEBUG
                    return;
                }
            }
        }
        // 조건에 따라 해당 스택이 없거나, 있었지만 내부 블록 실행이 (대기 없이) 완료된 경우, 이 블록의 실행은 완료.
    }
    else if (BlockType == "wait_until_true")
    {
        // params: [CONDITION_BLOCK (BOOL), Indicator]
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut("Flow 'wait_until_true' for " + objectId + ": Missing condition parameter (BOOL). Block ID: " + block.id, 2, executionThreadId);
            throw ScriptBlockExecutionError("조건 파라미터가 누락되었습니다.", block.id, BlockType, objectId, "Missing condition parameter.");
        }

        const nlohmann::json &conditionParamJson = block.paramsJson[0]; // BOOL 파라미터 (paramsKeyMap: { BOOL: 0 })
        OperandValue conditionResult = getOperandValue(engine, objectId, conditionParamJson, executionThreadId);

        bool conditionIsTrue = false;
        if (conditionResult.type == OperandValue::Type::BOOLEAN)
        {
            conditionIsTrue = conditionResult.boolean_val;
        }
        else if (conditionResult.type == OperandValue::Type::NUMBER)
        {
            conditionIsTrue = (conditionResult.number_val != 0.0); // 0이 아니면 참
        }
        else if (conditionResult.type == OperandValue::Type::STRING)
        {
            // 엔트리JS는 빈 문자열, "0", "false"를 거짓으로 간주. 그 외는 참.
            conditionIsTrue = !conditionResult.string_val.empty() &&
                              conditionResult.string_val != "0" &&
                              conditionResult.string_val != "false";
        }
        // 그 외 타입(EMPTY 등)은 거짓으로 처리됩니다.

        if (conditionIsTrue)
        {
            engine.EngineStdOut("Flow 'wait_until_true' for " + objectId + ": Condition is true. Proceeding. Block ID: " + block.id, 3, executionThreadId);
            // 조건이 참이므로, 이 블록은 완료되고 다음 블록으로 넘어갑니다.
            // 특별한 대기 설정 없이 Flow 함수를 반환합니다.
        }
        else
        { // 조건이 거짓이므로, 다음 프레임에 다시 평가하기 위해 BLOCK_INTERNAL 대기를 설정합니다.
            engine.EngineStdOut("Flow 'wait_until_true' for " + objectId + ": Condition is false. Waiting. Block ID: " + block.id, 3, executionThreadId);
            double idealFrameTime = 1000.0 / max(1, engine.specialConfig.TARGET_FPS);
            Uint32 frameDelay = static_cast<Uint32>(std::clamp(idealFrameTime, static_cast<double>(MIN_LOOP_WAIT_MS), 33.0)); // 최소 1ms, 최대 33ms
            entity->setScriptWait(executionThreadId, frameDelay, block.id, Entity::WaitType::BLOCK_INTERNAL);
            // Flow 함수를 반환하여 Entity::executeScript가 이 블록에서 대기하도록 합니다.
        }
    }
    else if (BlockType == "stop_object")
    {
        // params: [TARGET_DROPDOWN (string), Indicator]
        // paramsKeyMap: { TARGET: 0 }
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        { // Check for array and emptiness first
            engine.EngineStdOut("Flow 'stop_object' for " + objectId + ": Missing or invalid TARGET parameter. Block ID: " + block.id, 2, executionThreadId);
            throw ScriptBlockExecutionError("TARGET 파라미터가 누락되었거나 유효하지 않습니다.", block.id, BlockType, objectId, "Missing or invalid TARGET parameter.");
        }
        OperandValue targetOptionOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (targetOptionOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("Flow 'stop_object' for " + objectId + ": TARGET parameter is not a string. Value: " + targetOptionOp.asString() + ". Block ID: " + block.id, 2, executionThreadId);
            throw ScriptBlockExecutionError("TARGET 파라미터가 문자열이 아닙니다.", block.id, BlockType, objectId, "TARGET parameter is not a string.");
        }
        string targetOption = targetOptionOp.asString();
        engine.EngineStdOut("Flow 'stop_object' for " + objectId + ": Option='" + targetOption + "'. Block ID: " + block.id, 0, executionThreadId);

        engine.requestStopObject(objectId, executionThreadId, targetOption);
        // The engine.requestStopObject will set terminateRequested flags on the appropriate ScriptThreadStates.
        // The Entity::executeScript loop will check this flag after this Flow function returns and handle the actual termination of the thread(s).
        // No explicit "return this.die()" equivalent is needed here in Flow; the flag mechanism handles it.
    }
    else if (BlockType == "restart_project")
    {
        engine.EngineStdOut("Flow 'restart_project': Requesting project restart for " + objectId, 0, executionThreadId);
        engine.requestProjectRestart();
        // The Entity::executeScript loop will check this flag after this Flow function returns and handle the actual termination of the thread(s).
        // No explicit "return this.die()" equivalent is needed here in Flow; the flag mechanism handles it.
    }
    else if (BlockType == "create_clone")
    {
        // params: [VALUE (DropdownDynamic with menuName 'clone'), Indicator]
        // VALUE will be the ID of the object to clone, or "self"
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut("Flow 'create_clone' for " + objectId + ": Missing target parameter. Block ID: " + block.id, 2, executionThreadId);
            throw ScriptBlockExecutionError("복제할 대상 파라미터가 없습니다.", block.id, BlockType, objectId, "Missing target parameter.");
        }

        OperandValue targetOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (targetOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("Flow 'create_clone' for " + objectId + ": Target parameter is not a string. Value: " + targetOp.asString() + ". Block ID: " + block.id, 2, executionThreadId);
            throw ScriptBlockExecutionError("복제 대상 파라미터가 문자열이 아닙니다.", block.id, BlockType, objectId, "Target parameter is not a string.");
        }

        string targetToCloneId = targetOp.asString();
        string actualOriginalId;

        if (targetToCloneId == "self" || targetToCloneId.empty())
        {                                // "self" 또는 드롭다운이 self에 대해 빈 값을 반환하는 경우
            actualOriginalId = objectId; // 이 스크립트를 실행하는 엔티티가 복제 대상
        }
        else
        {
            actualOriginalId = targetToCloneId; // 다른 엔티티가 복제 대상으로 지정됨
        }

        engine.EngineStdOut("Flow 'create_clone': Object " + objectId + " requests clone of " + actualOriginalId + ". Block ID: " + block.id, 0, executionThreadId);
        // sceneIdAtDispatch는 복제본의 "when_clone_start" 스크립트에 대한 씬 컨텍스트입니다.
        engine.createCloneOfEntity(actualOriginalId, sceneIdAtDispatch);
    }
    else if (BlockType == "delete_clone")
    {
        // 이 블록은 자신(복제본)을 삭제합니다. 파라미터는 보통 없습니다.
        auto entity = engine.getEntityByIdShared(objectId); // objectId는 이 스크립트를 실행하는 엔티티의 ID입니다.
        if (!entity)
        {
            engine.EngineStdOut("Flow 'delete_clone' for " + objectId + ": Entity not found.", 2, executionThreadId);
            throw ScriptBlockExecutionError("삭제할 복제본 객체를 찾을 수 없습니다.", block.id, BlockType, objectId, "Entity not found for delete_clone.");
        }

        if (!entity->getIsClone())
        {
            engine.EngineStdOut("Flow 'delete_clone' for " + objectId + ": Target is not a clone. Block has no effect.", 1, executionThreadId);
            // 엔트리에서는 원본 객체도 이 블록으로 삭제될 수 있는 경우가 있지만 (마지막 남은 객체일 때 등),
            // "복제본 삭제" 블록은 이름 그대로 복제본에만 작용하는 것이 더 명확할 수 있습니다.
            // 여기서는 복제본이 아니면 아무 작업도 하지 않도록 합니다.
            return;
        }

        engine.EngineStdOut("Flow 'delete_clone': Clone " + objectId + " is requesting self-deletion. Block ID: " + block.id, 0, executionThreadId);
        // Engine의 deleteEntity 메소드는 해당 엔티티의 스크립트 종료 처리도 담당해야 합니다.
        // 이 블록이 실행된 후, 현재 스크립트 스레드는 Entity::executeScript 루프 내에서 종료 플래그를 확인하고 중단됩니다.
        engine.deleteEntity(objectId);
        // 이 블록 이후에 오는 블록은 실행되지 않아야 합니다 (해당 스크립트 스레드가 종료되므로).
    }
    else if (BlockType == "remove_all_clones")
    {
        // 이 블록은 현재 오브젝트(objectId)가 생성한 모든 복제본을 삭제합니다.
        // 파라미터는 보통 없습니다 (인디케이터만 있을 수 있음).
        engine.EngineStdOut("Flow 'remove_all_clones': Object " + objectId + " is requesting deletion of all its clones. Block ID: " + block.id, 0, executionThreadId);
        engine.deleteAllClonesOf(objectId);
        // 이 블록은 실행 흐름을 중단시키지 않고, 다음 블록으로 계속 진행됩니다.
    }
}

void TextBox(string BlockType, Engine &engine, const string &objectId, const Block &block,
             const string &executionThreadId)
{
    auto entity = engine.getEntityByIdShared(objectId);
    if (!entity)
    {
        engine.EngineStdOut("TextBox block: Entity " + objectId + " not found for block type " + BlockType, 2, executionThreadId);
        return;
    }

    const ObjectInfo *objInfo = engine.getObjectInfoById(objectId); // getObjectInfoById는 내부적으로 뮤텍스 처리 가정
    if (!objInfo || objInfo->objectType != "textBox")
    {
        // 텍스트박스가 아닐경우 무시함
        return;
    }

    if (BlockType == "text_write")
    {
        // paramsKeyMap: { VALUE: 0 }
        // 파라미터는 쓰여질 텍스트 값입니다.
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut(
                "text_write block for " + objectId + " has invalid or missing params. Expected text VALUE.",
                2, executionThreadId);
            return;
        }

        OperandValue textValueOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        string textToWrite = textValueOp.asString(); // 모든 타입을 문자열로 변환

        entity->setText(textToWrite); // 새로 추가된 Entity::setText 메소드 호출
        engine.EngineStdOut("TextBox " + objectId + " executed text_write with: \"" + textToWrite + "\"", 3, executionThreadId);
    }
    else if (BlockType == "text_append")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut(
                "text_append block for " + objectId + " has invalid or missing params. Expected text VALUE.",
                2, executionThreadId);
            return;
        }
        OperandValue textValue = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        string textToAppend = textValue.asString(); // 모든 타입을 문자열로 변환
        entity->appendText(textToAppend);
    }
    else if (BlockType == "text_prepend")
    {
        if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut(
                "text_append block for " + objectId + " has invalid or missing params. Expected text VALUE.",
                2, executionThreadId);
            return;
        }
        OperandValue textValue = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        string textToAppend = textValue.asString(); // 모든 타입을 문자열로 변환
        entity->prependText(textToAppend);
    }
    else if (BlockType == "text_change_font_color")
    {
        // params: [TARGET_TEXTBOX_ID_STRING, COLOR_HEX_STRING]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut("text_set_font_color block for " + objectId + " has insufficient parameters. Expected COLOR_HEX.", 2, executionThreadId);
            return;
        }

        OperandValue colorHexOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);

        if (colorHexOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("text_set_font_color block for " + objectId + ": COLOR_HEX parameter is not a string.", 2, executionThreadId);
            return;
        }

        string hexColor = colorHexOp.asString();
        if (hexColor.length() == 7 && hexColor[0] == '#')
        {
            try
            {
                unsigned int r = stoul(hexColor.substr(1, 2), nullptr, 16);
                unsigned int g = stoul(hexColor.substr(3, 2), nullptr, 16);
                unsigned int b = stoul(hexColor.substr(5, 2), nullptr, 16);
                engine.updateEntityTextColor(entity->getId(), {(Uint8)r, (Uint8)g, (Uint8)b, 255});
                engine.EngineStdOut("TextBox " + objectId + " fontcolor changed to " + hexColor, 3, executionThreadId);
            }
            catch (const std::exception &e)
            {
                engine.EngineStdOut("Error parsing HEX color '" + hexColor + "' for text_set_font_color: " + e.what(), 2, executionThreadId);
            }
        }
        else
        {
            engine.EngineStdOut("Invalid HEX color format '" + hexColor + "' for text_set_font_color. Expected #RRGGBB.", 2, executionThreadId);
        }
    }
    else if (BlockType == "text_change_bg_color")
    {
        // params: [TARGET_TEXTBOX_ID_STRING, COLOR_HEX_STRING]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1) // JavaScript 코드에서는 파라미터가 하나 (색상 값)
        {
            engine.EngineStdOut("text_change_bg_color block for " + objectId + " has insufficient parameters. Expected COLOR_HEX.", 2, executionThreadId);
            return;
        }

        OperandValue colorHexOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId); // 색상 값은 첫 번째 파라미터

        if (colorHexOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("text_change_bg_color block for " + objectId + ": COLOR_HEX parameter is not a string.", 2, executionThreadId);
            return;
        }

        string hexColor = colorHexOp.asString();
        // JavaScript 코드처럼 '#'이 없으면 추가
        if (hexColor.rfind('#', 0) != 0)
        { // starts_with('#')와 유사
            hexColor = "#" + hexColor;
        }

        if (hexColor.length() == 7 && hexColor[0] == '#') // 유효한 #RRGGBB 형식인지 확인
        {
            try
            {
                unsigned int r = stoul(hexColor.substr(1, 2), nullptr, 16);
                unsigned int g = stoul(hexColor.substr(3, 2), nullptr, 16);
                unsigned int b = stoul(hexColor.substr(5, 2), nullptr, 16);
                engine.updateEntityTextBoxBackgroundColor(entity->getId(), {(Uint8)r, (Uint8)g, (Uint8)b, 255}); // 현재 엔티티의 배경색 변경
                engine.EngineStdOut("TextBox " + objectId + " background color changed to " + hexColor, 3, executionThreadId);
            }
            catch (const std::exception &e)
            {
                engine.EngineStdOut("Error parsing HEX color '" + hexColor + "' for text_change_bg_color: " + e.what(), 2, executionThreadId);
            }
        }
        else
        {
            engine.EngineStdOut("Invalid HEX color format '" + hexColor + "' for text_change_bg_color. Expected #RRGGBB or RRGGBB.", 2, executionThreadId);
        }
    }
}
/**
 * @brief 함수 블록
 *
 */
void Function(string BlockType, Engine &engine, const string &objectId, const Block &block,
              const string &executionThreadId)
{
}

/**
 * @brief 이벤트 (기타 제어) 블록
 *
 */
void Event(string BlockType, Engine &engine, const string &objectId, const Block &block,
           const string &executionThreadId)
{
    if (BlockType == "message_cast")
    {
        // params: [MESSAGE_ID_INPUT_OR_BLOCK, null, null]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut("message_cast block for object " + objectId + " has insufficient parameters. Expected message ID.", 2, executionThreadId);
            throw ScriptBlockExecutionError("메시지 ID 파라미터가 부족합니다.", block.id, BlockType, objectId, "Insufficient parameters for message_cast.");
        }

        OperandValue messageIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        // messageIdOp.asString() will handle conversion if messageIdOp is a number or boolean.
        string messageId = messageIdOp.asString();
        engine.EngineStdOut("DEBUG_MSG: Object " + objectId + " (Thread " + executionThreadId + ") is RAISING message: '" + messageId + "'", 3, executionThreadId);

        if (messageId.empty() || messageId == "null")
        {
            string errMsg = "메시지 ID가 비어있거나 null입니다. 메시지를 발송할 수 없습니다.";
            engine.EngineStdOut("message_cast block for object " + objectId + ": " + errMsg, 2, executionThreadId);
            throw ScriptBlockExecutionError(errMsg, block.id, BlockType, objectId, "Message ID is null or empty.");
        }

        // Optional: Validate if messageId exists in a list of defined messages if such a list is maintained by the Engine.
        // For now, we proceed to raise the message.

        engine.EngineStdOut("Object " + objectId + " is casting message: '" + messageId + "'", 0, executionThreadId);
        engine.raiseMessage(messageId, objectId, executionThreadId); // Pass sender info (objectId and current threadId)
    }
    else if (BlockType == "start_scene")
    {
        // params: [scene_id_string, null, null]
        if (!block.paramsJson.is_array() || block.paramsJson.size() < 1)
        {
            engine.EngineStdOut(
                "start_scene block for object " + objectId + " has invalid or missing scene ID parameter.", 2,
                executionThreadId);
            throw ScriptBlockExecutionError(
                "장면 ID 파라미터가 유효하지 않습니다.",
                block.id, BlockType, objectId, "Invalid or missing scene ID parameter.");
        }
        OperandValue sceneIdOp = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (sceneIdOp.type != OperandValue::Type::STRING || sceneIdOp.asString().empty() || sceneIdOp.asString() == "null")
        {
            string errMsg = "장면 ID가 유효한 문자열이 아니거나 비어있거나 null입니다. 장면을 전환할 수 없습니다. 값: " + sceneIdOp.asString();
            engine.EngineStdOut("start_scene block for object " + objectId + ": " + errMsg, 2, executionThreadId);
            throw ScriptBlockExecutionError(errMsg, block.id, BlockType, objectId,
                                            "Scene ID is not a valid string, is empty, or null.");
        }

        string sceneId = sceneIdOp.asString();
        engine.EngineStdOut("Object " + objectId + " is requesting to start scene: '" + sceneId + "'", 3,
                            executionThreadId);
        engine.goToScene(sceneId); // goToScene 내부에서 scene 존재 여부 확인 및 when_scene_start 이벤트 트리거
    }
    else if (BlockType == "start_neighbor_scene")
    {
        // 1. paramsJson 자체가 null인지 명시적으로 확인
        if (block.paramsJson.is_null())
        {
            engine.EngineStdOut(
                "start_neighbor_scene block for object " + objectId + " has null paramsJson. Expected one parameter (next/prev).", 2,
                executionThreadId);
            throw ScriptBlockExecutionError(
                "다음/이전 장면 시작하기 블록의 파라미터가 존재하지 않습니다 (null).",
                block.id, BlockType, objectId, "paramsJson is null for start_neighbor_scene block.");
        }
        // 2. null이 아니라면, 배열이 아니거나 비어있는지 확인
        else if (!block.paramsJson.is_array() || block.paramsJson.empty())
        {
            engine.EngineStdOut(
                "start_neighbor_scene block for object " + objectId + " has invalid (not an array or empty) paramsJson. Expected one parameter (next/prev). Type: " + block.paramsJson.type_name(), 2,
                executionThreadId);
            throw ScriptBlockExecutionError(
                "다음/이전 장면 시작하기 블록의 파라미터가 유효하지 않습니다.",
                block.id, BlockType, objectId, "Invalid or non-array/empty paramsJson for start_neighbor_scene block.");
        }

        OperandValue o = getOperandValue(engine, objectId, block.paramsJson[0], executionThreadId);
        if (o.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("start_neighbor_scene block for object " + objectId + ": parameter is not a string. Value: " + o.asString(), 2, executionThreadId);
            return;
        }
        if (o.string_val == "next")
        {
            engine.goToNextScene();
        }
        else
        {
            engine.goToPreviousScene();
        }
    }
}
// 이 함수는 주어진 블록 목록을 순차적으로 실행하며, wait_second를 만나면 해당 스레드를 블록합니다.
// 실제 게임 엔진에서는 비동기 실행 및 스레드 관리가 더 복잡하게 이루어집니다.
void executeBlocksSynchronously(Engine &engine, const string &objectId, const vector<Block> &blocks,
                                // Entity::ScriptThreadState& currentThreadState, // This might be needed for more complex state management
                                const string &executionThreadId, const string &sceneIdAtDispatch, float deltaTime)
{
    engine.EngineStdOut("Enter executeBlocksSynchronously for " + objectId + ". blocks.size() = " + to_string(blocks.size()) + ". Thread: " + executionThreadId, 3, executionThreadId);
    Entity *entity = engine.getEntityById_nolock(objectId);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const Block &block = blocks[i];
        // 엔진 종료 신호 확인
        if (engine.m_isShuttingDown.load(memory_order_relaxed))
        {
            engine.EngineStdOut("Synchronous block execution cancelled due to engine shutdown for entity: " + objectId, 1, executionThreadId);
            engine.EngineStdOut("executeBlocksSynchronously: Shutting down. Exiting loop for " + objectId, 1, executionThreadId);
            return; // Stop execution
        }
        // 씬 변경 확인
        string currentEngineSceneId = engine.getCurrentSceneId();
        const ObjectInfo *objInfo = engine.getObjectInfoById(objectId);
        bool isGlobalEntity = false;
        if (objInfo)
        {
            isGlobalEntity = (objInfo->sceneId == "global" || objInfo->sceneId.empty());
        }
        // 스크립트가 디스패치된 시점의 씬과 현재 엔진의 씬이 다르면서, 이 엔티티가 전역 엔티티가 아니라면 실행 중단
        if (currentEngineSceneId != sceneIdAtDispatch && !isGlobalEntity)
        {
            engine.EngineStdOut("Synchronous block execution for entity " + objectId + " (Block: " + block.type + ") halted. Entity no longer in current scene " + currentEngineSceneId + ".", 1, executionThreadId);
            engine.EngineStdOut("executeBlocksSynchronously: Scene mismatch. Exiting loop for " + objectId + ". Entity scene: " + (objInfo ? objInfo->sceneId : "N/A") + ", Engine scene: " + currentEngineSceneId, 1, executionThreadId);
            return;
        }

        // 다른 블록 타입 실행
        // 이 헬퍼 함수는 내부 블록 실행만을 담당하므로, 여기서 다시 제어 블록(repeat, if 등)을 만나면
        // 해당 블록의 로직에 따라 재귀적으로 이 함수를 호출하거나 다른 처리를 해야 합니다.
        // 여기서는 간단히 카테고리 함수만 호출합니다. 실제로는 더 복잡한 실행기 로직이 필요합니다.
        try
        {
            Moving(block.type, engine, objectId, block, executionThreadId, deltaTime);
            Calculator(block.type, engine, objectId, block, executionThreadId);
            Looks(block.type, engine, objectId, block, executionThreadId);
            Sound(block.type, engine, objectId, block, executionThreadId);
            Variable(block.type, engine, objectId, block, executionThreadId);
            Function(block.type, engine, objectId, block, executionThreadId);
            TextBox(block.type, engine, objectId, block, executionThreadId);
            Event(block.type, engine, objectId, block, executionThreadId);
            Flow(block.type, engine, objectId, block, executionThreadId, sceneIdAtDispatch, deltaTime);
        }
        catch (const ScriptBlockExecutionError &sbee)
        {
            // 내부 블록 실행 중 오류 발생 시 상위로 전파
            throw;
        }
        catch (const exception &e)
        {
            // 다른 예외 발생 시 ScriptBlockExecutionError로 래핑하여 전파
            throw ScriptBlockExecutionError(
                "Error during synchronous nested block execution.",
                block.id, block.type, objectId, e.what());
        }

        // 방금 실행한 블록이 대기 상태를 설정했는지 확인합니다.
        // Entity 포인터가 유효한지 먼저 확인합니다.
        if (entity && entity->isScriptWaiting(executionThreadId))
        {
            Entity::WaitType typeOfWait = entity->getCurrentWaitType(executionThreadId);
            string idOfWaitingBlock = entity->getWaitingBlockId(executionThreadId);

            // 현재 실행된 블록(block.id)이 스스로 대기를 설정한 경우
            if (idOfWaitingBlock == block.id)
            {
                if (typeOfWait == Entity::WaitType::BLOCK_INTERNAL ||
                    typeOfWait == Entity::WaitType::EXPLICIT_WAIT_SECOND ||
                    typeOfWait == Entity::WaitType::TEXT_INPUT)
                {
                    engine.EngineStdOut("executeBlocksSynchronously: Block " + block.id + " (Type: " + block.type + ") set wait (" + BlockTypeEnumToString(typeOfWait) + "). Pausing synchronous execution chain for " + objectId + ".", 3, executionThreadId);
                    return; // 동기적 블록 목록 실행을 여기서 중단합니다.
                }
            }
        }
    }
    engine.EngineStdOut("Exit executeBlocksSynchronously for " + objectId + ". Loop " + (blocks.empty() ? "was not entered (empty blocks)." : "completed or exited."), 3, executionThreadId);
}