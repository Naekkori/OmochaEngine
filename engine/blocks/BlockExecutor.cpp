#include "BlockExecutor.h"
#include "util/AEhelper.h"
#include "../Engine.h"
#include "../Entity.h"
#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <ctime>
AudioEngineHelper aeHelper; // 전역 AudioEngineHelper 인스턴스
struct OperandValue
{
    enum class Type
    {
        EMPTY,
        NUMBER,
        STRING
    };
    Type type = Type::EMPTY;
    double number_val = 0.0;
    std::string string_val = "";

    OperandValue() = default;
    OperandValue(double val) : type(Type::NUMBER), number_val(val) {}
    OperandValue(const std::string &val) : type(Type::STRING), string_val(val) {}

    double asNumber() const
    {
        if (type == Type::NUMBER)
            return number_val;
        if (type == Type::STRING)
        {
            try
            {
                size_t idx = 0;
                double val = std::stod(string_val, &idx);
                if (idx == string_val.length())
                {
                    return val;
                }
            }
            catch (const std::invalid_argument &)
            {

                throw "Invalid number format";
            }
            catch (const std::out_of_range &)
            {

                throw "Number out of range";
            }
        }
        return 0.0;
    }

    std::string asString() const
    {
        if (type == Type::STRING)
            return string_val;
        if (type == Type::NUMBER)
        {
            if (std::isnan(number_val))
                return "NaN";
            if (std::isinf(number_val))
                return (number_val > 0 ? "Infinity" : "-Infinity");

            std::string s = std::to_string(number_val);
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            if (s.back() == '.')
            {
                s.pop_back();
            }
            return s;
        }
        return "";
    }
};

OperandValue processMathematicalBlock(Engine &engine, const std::string &objectId, const Block &block);
OperandValue processVariableBlock(Engine &engine, const std::string &objectId, const Block &block);

OperandValue getOperandValue(Engine &engine, const std::string &objectId, const rapidjson::Value &paramField)
{
    if (paramField.IsString())
    {
        return OperandValue(paramField.GetString());
    }

    if (paramField.IsObject())
    {
        if (!paramField.HasMember("type") || !paramField["type"].IsString())
        {
            engine.EngineStdOut("Parameter field is object but missing 'type' for object " + objectId, 2);
            return OperandValue();
        }
        std::string fieldType = paramField["type"].GetString();

        if (fieldType == "number" || fieldType == "text_reporter_number")
        {
            if (paramField.HasMember("params") && paramField["params"].IsArray() &&
                paramField["params"].Size() > 0 && paramField["params"][0].IsString())
            {
                try
                {
                    return OperandValue(std::stod(paramField["params"][0].GetString()));
                }
                catch (const std::exception &e)
                {
                    engine.EngineStdOut("Error converting number param: " + std::string(paramField["params"][0].GetString()) + " for " + objectId + " - " + e.what(), 2);
                    return OperandValue(0.0);
                }
            }
            engine.EngineStdOut("Invalid 'number' block structure in parameter field for " + objectId, 1);
            return OperandValue(0.0);
        }
        else if (fieldType == "text" || fieldType == "text_reporter_string")
        {
            if (paramField.HasMember("params") && paramField["params"].IsArray() &&
                paramField["params"].Size() > 0 && paramField["params"][0].IsString())
            {
                return OperandValue(paramField["params"][0].GetString());
            }
            engine.EngineStdOut("Invalid 'text' block structure in parameter field for " + objectId, 1);
            return OperandValue("");
        }
        else if (fieldType == "calc_basic")
        {
            Block subBlock;
            subBlock.type = fieldType;
            if (paramField.HasMember("id") && paramField["id"].IsString())
                subBlock.id = paramField["id"].GetString();

            if (paramField.HasMember("params") && paramField["params"].IsArray())
            {
                subBlock.paramsJson.CopyFrom(paramField["params"], engine.m_blockParamsAllocatorDoc.GetAllocator());
            }
            return processMathematicalBlock(engine, objectId, subBlock);
        }

        engine.EngineStdOut("Unsupported block type in parameter: " + fieldType + " for " + objectId, 1);
        return OperandValue();
    }
    engine.EngineStdOut("Parameter field is not a string or object for " + objectId, 1);
    return OperandValue();
}

void Behavior(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
OperandValue Mathematical(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
void Shape(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
void Sound(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
void Variable(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);
void Function(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block);

void executeScript(Engine &engine, const std::string &objectId, const Script *scriptPtr)
{
    if (!scriptPtr)
    {
        engine.EngineStdOut("executeScript called with null script pointer for object: " + objectId, 2);
        return;
    }

    engine.EngineStdOut("Executing script for object: " + objectId, 0);
    for (size_t i = 1; i < scriptPtr->blocks.size(); ++i)
    {
        const Block &block = scriptPtr->blocks[i];
        engine.EngineStdOut("  Executing Block ID: " + block.id + ", Type: " + block.type + " for object: " + objectId, 0);
        try
        {
            Behavior(block.type, engine, objectId, block);
            Mathematical(block.type, engine, objectId, block);
            Shape(block.type, engine, objectId, block);
            Sound(block.type, engine, objectId, block);
            Variable(block.type, engine, objectId, block);
            Function(block.type, engine, objectId, block);
        }
        catch(const std::exception& e)
        {
            engine.showMessageBox("Error executing block: " + block.id + " of type: " + block.type + " for object: " + objectId + "\n" + e.what(), engine.msgBoxIconType.ICON_ERROR);
            SDL_Quit();
        }
    }
}
/**
 * @brief 행동블럭
 *
 */
void Behavior(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
{
    if (BlockType == "choose_project_timer_action")
    {
        // paramsKeyMap: { ACTION: 0 }
        // 드롭다운 값은 block.paramsJson[0]에 문자열로 저장되어 있을 것으로 예상합니다.
        // Block.h에서 paramsJson이 rapidjson::Value 타입이고,
        // 이 값은 loadProject 시점에 engine.m_blockParamsAllocatorDoc를 사용하여 할당됩니다.
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() == 0 || !block.paramsJson[0].IsString()) {
            engine.EngineStdOut("choose_project_timer_action block for " + objectId + " has invalid or missing action parameter.", 2);
            return;
        }

        // 이 블록의 파라미터는 항상 단순 문자열 드롭다운 값이므로 직접 접근합니다.
        // getOperandValue를 사용할 수도 있지만, 여기서는 직접 사용합니다.
        std::string action = block.paramsJson[0].GetString();

        if (action == "START") {
            engine.EngineStdOut("Project timer STARTED by object " + objectId, 0);
            engine.startProjectTimer();
        } else if (action == "STOP") {
            engine.EngineStdOut("Project timer STOPPED by object " + objectId, 0);
            engine.stopProjectTimer();
        } else if (action == "RESET") {
            engine.EngineStdOut("Project timer RESET by object " + objectId, 0);
            engine.resetProjectTimer(); // resetProjectTimer는 값만 0으로 설정합니다.
        } else {
            engine.EngineStdOut("choose_project_timer_action block for " + objectId + " has unknown action: " + action, 1);
        }
        return; // Behavior 블록은 값을 반환하지 않음
    }else if (BlockType == "set_visible_project_timer"){
        // paramsKeyMap: { ACTION: 0 } (가정)
        // 드롭다운 값 ("SHOW" 또는 "HIDE")은 block.paramsJson[0]에 문자열로 저장되어 있을 것으로 예상합니다.
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() == 0 || !block.paramsJson[0].IsString()) {
            engine.EngineStdOut("set_visible_project_timer block for " + objectId + " has invalid or missing action parameter.", 2);
            return;
        }

        std::string actionValue = block.paramsJson[0].GetString();

        if (actionValue == "SHOW") {
            engine.EngineStdOut("Project timer set to VISIBLE by object " + objectId, 0);
            engine.showProjectTimer(true);
        } else if (actionValue == "HIDE") { // 엔트리에서는 "HIDE" 또는 다른 값을 사용할 수 있습니다. "SHOW"가 아니면 숨김으로 처리.
            engine.EngineStdOut("Project timer set to HIDDEN by object " + objectId, 0);
            engine.showProjectTimer(false);
        }else{
            engine.EngineStdOut("set_visible_project_timer block for " + objectId + " has unknown action value: " + actionValue, 1);
            // 기본적으로 숨김 처리 또는 아무것도 안 함
            // engine.showProjectTimer(false); 
        }
        return;
    }
    // 여기에 다른 Behavior 블록 타입에 대한 처리를 추가할 수 있습니다.
    // 예: if (BlockType == "move_direction") { ... }
}
/**
 * @brief 수학블럭
 *
 */
OperandValue Mathematical(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
{
    if (BlockType == "calc_basic")
    {
        return processMathematicalBlock(engine, objectId, block);
    }
    else if (BlockType == "calc_rand")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut("calc_rand block for object " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return OperandValue();
        }
        OperandValue minVal = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue maxVal = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (minVal.type != OperandValue::Type::NUMBER || maxVal.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("calc_rand block for object " + objectId + " has non-numeric params.", 2);
            return OperandValue();
        }
        double min = minVal.number_val;
        double max = maxVal.number_val;
        if (min >= max)
        {
            engine.EngineStdOut("calc_rand block for object " + objectId + " has invalid range.", 2);
            return OperandValue();
        }
        double randVal = min + static_cast<double>(rand()) / (static_cast<double>(RAND_MAX / (max - min)));
        return OperandValue(randVal);
    }
    else if (BlockType == "coordinate_mouse")
    {
        // paramsKeyMap: { VALUE: 1 }
        // 드롭다운 값 ("x" 또는 "y")은 paramsJson[1]에 있습니다.
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() <= 1) { // 인덱스 1은 크기가 최소 2여야 함
            engine.EngineStdOut("coordinate_mouse block for " + objectId + " has invalid params structure. Expected param at index 1 for VALUE.", 2);
            return OperandValue(0.0);
        }

        OperandValue coordTypeOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        std::string coord_type_str;

        if (coordTypeOp.type == OperandValue::Type::STRING) {
            coord_type_str = coordTypeOp.asString();
        } else {
            engine.EngineStdOut("coordinate_mouse block for " + objectId + ": VALUE parameter (index 1) is not a string.", 2);
            return OperandValue(0.0);
        }

        if (coord_type_str.empty()) { // getOperandValue가 빈 문자열을 반환했거나, 원래 문자열이 비어있는 경우
            engine.EngineStdOut("coordinate_mouse block for object " + objectId + " has an empty coordinate type string for VALUE parameter.", 2);
            return OperandValue(0.0);
        }

        if (engine.isMouseCurrentlyOnStage())
        {
            if (coord_type_str == "x" || coord_type_str == "mouseX")
            {
                return OperandValue(static_cast<double>(engine.getCurrentStageMouseX()));
            }
            else if (coord_type_str == "y" || coord_type_str == "mouseY")
            {
                return OperandValue(static_cast<double>(engine.getCurrentStageMouseY()));
            }
            else
            {
                engine.EngineStdOut("coordinate_mouse block for object " + objectId + " has unknown coordinate type: " + coord_type_str, 2);
                return OperandValue(0.0);
            }
        }
        else
        {

            engine.EngineStdOut("Info: coordinate_mouse block - mouse is not on stage. Returning 0.0 for " + coord_type_str + " for object " + objectId, 0);
            return OperandValue(0.0);
        }
    }
    else if (BlockType == "coordinate_object")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 4)
        {
            engine.EngineStdOut("coordinate_object block for object " + objectId + " has invalid params structure. Expected at least 4 params.", 2);
            return OperandValue(0.0);
        }

        OperandValue targetIdOpVal;
        if (block.paramsJson[1].IsNull())
        {
            targetIdOpVal = OperandValue("self");
        }
        else
        {
            targetIdOpVal = getOperandValue(engine, objectId, block.paramsJson[1]);
        }

        if (targetIdOpVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("coordinate_object block for " + objectId + ": target object ID parameter (VALUE) is not a string.", 2);
            return OperandValue(0.0);
        }
        std::string targetObjectIdStr = targetIdOpVal.asString();

        OperandValue coordinateTypeOpVal = getOperandValue(engine, objectId, block.paramsJson[3]);
        if (coordinateTypeOpVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("coordinate_object block for " + objectId + ": coordinate type parameter (COORDINATE) is not a string.", 2);
            return OperandValue(0.0);
        }
        std::string coordinateTypeStr = coordinateTypeOpVal.asString();

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
            engine.EngineStdOut("coordinate_object block for " + objectId + ": target entity '" + targetObjectIdStr + "' not found.", 2);
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
            double sizeVal = std::round((targetEntity->getScaleX() * 100.0) * 10.0) / 10.0;
            return OperandValue(sizeVal);
        }
        else if (coordinateTypeStr == "picture_index" || coordinateTypeStr == "picture_name")
        {
            const ObjectInfo *targetObjInfo = engine.getObjectInfoById(targetEntity->getId());

            if (!targetObjInfo)
            {
                engine.EngineStdOut("coordinate_object block for " + objectId + ": could not find ObjectInfo for target entity '" + targetEntity->getId() + "'.", 2);
                if (coordinateTypeStr == "picture_name")
                    return OperandValue("");
                return OperandValue(0.0); // Align with indexOf behavior (0 if not found)
            }

            if (targetObjInfo->costumes.empty())
            {
                engine.EngineStdOut("coordinate_object block for " + objectId + ": target entity '" + targetEntity->getId() + "' has no costumes.", 1);
                if (coordinateTypeStr == "picture_name")
                    return OperandValue("");
                return OperandValue(0.0);
            }

            const std::string &selectedCostumeId = targetObjInfo->selectedCostumeId;

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

            if (found) {
                if (coordinateTypeStr == "picture_index")
                {
                    return OperandValue(static_cast<double>(found_idx + 1));
                }
                else // picture_name
                {
                    return OperandValue(targetObjInfo->costumes[found_idx].name);
                }
            } else {
                engine.EngineStdOut("coordinate_object - Selected costume ID '" + selectedCostumeId + "' not found in costume list for entity '" + targetEntity->getId() + "'.", 1);
                if (coordinateTypeStr == "picture_index") return OperandValue(0.0); // Not found, return 0 for index
                else return OperandValue(""); // Not found, return empty for name
            }
        }
        else
        {
            engine.EngineStdOut("coordinate_object block for " + objectId + ": unknown coordinate type '" + coordinateTypeStr + "'.", 2);
            return OperandValue(0.0);
        }
    }
    else if (BlockType == "quotient_and_mod")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut("quotient_and_mod block for " + objectId + " parameter is invalid. Expected 3 params.", 2);
            throw "Invalid params for quotient_and_mod block";
            return OperandValue();
        }

        OperandValue left_op = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue operator_op = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue right_op = getOperandValue(engine, objectId, block.paramsJson[2]);

        if (operator_op.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("quotient_and_mod block for " + objectId + " has non-string operator parameter.", 2);
            throw "Invalid operator type in quotient_and_mod block";
            return OperandValue();
        }
        std::string anOperator = operator_op.string_val;

        double left_val = left_op.asNumber();
        double right_val = right_op.asNumber();

        if (anOperator == "QUOTIENT")
        {
            if (right_val == 0.0)
            {
                engine.EngineStdOut("Division by zero in quotient_and_mod (QUOTIENT) for " + objectId, 2);
                throw "Division by zero in quotient_and_mod (QUOTIENT)";
                return OperandValue();
            }
            return OperandValue(std::floor(left_val / right_val));
        }
        else
        {
            if (right_val == 0.0)
            {
                engine.EngineStdOut("Division by zero in quotient_and_mod (MOD) for " + objectId, 2);
                throw "Division by zero in quotient_and_mod (MOD)";
                return OperandValue();
            }
            return OperandValue(left_val - right_val * std::floor(left_val / right_val));
        }
    }else if (BlockType == "calc_operation")
    { // EntryJS: get_value_of_operator
        // switch 문에서 사용할 수학 연산 열거형
        enum class MathOperationType {
            ABS, FLOOR, CEIL, ROUND, SQRT,
            SIN, COS, TAN,
            ASIN, ACOS, ATAN,
            LOG, LN, // LOG는 밑이 10인 로그, LN은 자연로그
            UNKNOWN
        };
        // 연산자 문자열을 열거형으로 변환하는 헬퍼 함수
        auto stringToMathOperation = [](const std::string& opStr) -> MathOperationType {
            if (opStr == "abs") return MathOperationType::ABS;
            if (opStr == "floor") return MathOperationType::FLOOR;
            if (opStr == "ceil") return MathOperationType::CEIL;
            if (opStr == "round") return MathOperationType::ROUND;
            if (opStr == "sqrt") return MathOperationType::SQRT;
            if (opStr == "sin") return MathOperationType::SIN;
            if (opStr == "cos") return MathOperationType::COS;
            if (opStr == "tan") return MathOperationType::TAN;
            if (opStr == "asin") return MathOperationType::ASIN;
            if (opStr == "acos") return MathOperationType::ACOS;
            if (opStr == "atan") return MathOperationType::ATAN;
            if (opStr == "log") return MathOperationType::LOG;
            if (opStr == "ln") return MathOperationType::LN;
            return MathOperationType::UNKNOWN;
        };

        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2) {
            engine.EngineStdOut("calc_operation block for " + objectId + " has invalid params. Expected 2 params (LEFTHAND, OPERATOR).", 2);
            return OperandValue(std::nan(""));
        }

        OperandValue leftOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue opVal = getOperandValue(engine, objectId, block.paramsJson[1]);

        if (leftOp.type != OperandValue::Type::NUMBER) {
            engine.EngineStdOut("calc_operation block for " + objectId + " has non-numeric left operand.", 2);
            return OperandValue(std::nan(""));
        }
        if (opVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("calc_operation block for " + objectId + " has non-string operator.", 2);
            return OperandValue(std::nan(""));
        }

        double leftNum = leftOp.asNumber();
        std::string originalOperator = opVal.asString();
        std::string anOperator = originalOperator;

        // JavaScript 코드: if (operator.indexOf('_')) { operator = operator.split('_')[0]; }
        // C++ 대응 코드:
        // 이 조건은 indexOf('_') 결과가 0이 아닐 때 (즉, > 0 또는 -1일 때) 참입니다.
        // indexOf('_') 결과가 0이면 거짓입니다.
        size_t underscore_pos = anOperator.find('_');
        if (underscore_pos != 0) { // 찾지 못했거나(npos) 0보다 큰 인덱스에서 찾았을 경우 참
            if (underscore_pos != std::string::npos) { // 찾았고, 0번 위치가 아닐 경우
                anOperator = anOperator.substr(0, underscore_pos);
            }
            // 찾지 못했을 경우 (underscore_pos == std::string::npos), anOperator는 originalOperator로 유지됩니다.
        }
        // underscore_pos == 0 인 경우, anOperator는 originalOperator로 유지됩니다.

        // 입력이 각도(degree)인 특정 원본 연산자에 대해 각도-라디안 변환
        // M_PI가 <cmath>에 정의되어 있지 않다면 정의 (예: MSVC에서 _USE_MATH_DEFINES 사용)
        #ifndef M_PI
            #define M_PI 3.14159265358979323846
        #endif
        const double PI_CONST = M_PI;

        bool inputWasDegrees = false;
        if (originalOperator.length() > 7 && originalOperator.substr(originalOperator.length() - 7) == "_degree") {
            if (anOperator == "sin" || anOperator == "cos" || anOperator == "tan") {
                leftNum = leftNum * PI_CONST / 180.0;
                inputWasDegrees = true; // 입력이 변환되었음을 표시
            }
        }

        double result = 0.0;
        bool errorOccurred = false;
        std::string errorMsg = "";

        MathOperationType opType = stringToMathOperation(anOperator);

        switch (opType) {
            case MathOperationType::ABS:
                result = std::fabs(leftNum);
                break;
            case MathOperationType::FLOOR:
                result = std::floor(leftNum);
                break;
            case MathOperationType::CEIL:
                result = std::ceil(leftNum);
                break;
            case MathOperationType::ROUND:
                result = std::round(leftNum);
                break;
            case MathOperationType::SQRT:
                if (leftNum < 0) {
                    errorOccurred = true; errorMsg = "sqrt of negative number"; result = std::nan("");
                } else {
                    result = std::sqrt(leftNum);
                }
                break;
            case MathOperationType::SIN:
                result = std::sin(leftNum); // leftNum이 라디안이라고 가정
                break;
            case MathOperationType::COS:
                result = std::cos(leftNum); // leftNum이 라디안이라고 가정
                break;
            case MathOperationType::TAN:
                result = std::tan(leftNum); // leftNum이 라디안이라고 가정
                break;
            case MathOperationType::ASIN:
                if (leftNum < -1.0 || leftNum > 1.0) {
                    errorOccurred = true; errorMsg = "asin input out of range [-1, 1]"; result = std::nan("");
                } else {
                    result = std::asin(leftNum); // 라디안 값 반환
                }
                break;
            case MathOperationType::ACOS:
                if (leftNum < -1.0 || leftNum > 1.0) {
                    errorOccurred = true; errorMsg = "acos input out of range [-1, 1]"; result = std::nan("");
                } else {
                    result = std::acos(leftNum); // 라디안 값 반환
                }
                break;
            case MathOperationType::ATAN:
                result = std::atan(leftNum); // 라디안 값 반환
                break;
            case MathOperationType::LOG: // 밑이 10인 로그
                if (leftNum <= 0) {
                    errorOccurred = true; errorMsg = "log of non-positive number"; result = std::nan("");
                } else {
                    result = std::log10(leftNum);
                }
                break;
            case MathOperationType::LN: // 자연로그
                if (leftNum <= 0) {
                    errorOccurred = true; errorMsg = "ln of non-positive number"; result = std::nan("");
                } else {
                    result = std::log(leftNum);
                }
                break;
            case MathOperationType::UNKNOWN:
            default:
                errorOccurred = true; errorMsg = "Unknown operator in calc_operation: " + originalOperator; result = std::nan("");
                break;
        }

        if (errorOccurred) {
            engine.EngineStdOut("calc_operation block for " + objectId + ": " + errorMsg, 2);
            return OperandValue(result); // NaN 반환
        }

        // 원본 연산자가 결과가 각도여야 함을 나타내는 경우 (예: "asin_degree")
        if (originalOperator.length() > 7 && originalOperator.substr(originalOperator.length() - 7) == "_degree") {
            if (anOperator == "asin" || anOperator == "acos" || anOperator == "atan") {
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
    else if (BlockType == "get_date"){
        time_t now = time(nullptr);
        // paramsKeyMap: { VALUE: 0 }
        // 드롭다운 값은 block.paramsJson[0]에 문자열로 저장되어 있을 것으로 예상합니다.
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() == 0 || !block.paramsJson[0].IsString()) {
            engine.EngineStdOut("get_date block for " + objectId + " has invalid or missing action parameter.", 2);
            return OperandValue();
        }
        
        // 이 블록의 파라미터는 항상 단순 문자열 드롭다운 값이므로 직접 접근합니다.
        std::string action = block.paramsJson[0].GetString();
        tm *now_tm = localtime(&now);
        if (action == "year") {
            return OperandValue(static_cast<double>(now_tm->tm_year + 1900));
        } else if (action == "month") {
            return OperandValue(static_cast<double>(now_tm->tm_mon + 1));
        } else if (action == "day") {
            return OperandValue(static_cast<double>(now_tm->tm_mday));
        } else if (action == "hour") {
            return OperandValue(static_cast<double>(now_tm->tm_hour));
        } else if (action == "minute") {
            return OperandValue(static_cast<double>(now_tm->tm_min));
        } else if (action == "second") {
            return OperandValue(static_cast<double>(now_tm->tm_sec));
        } else {
            engine.EngineStdOut("get_date block for " + objectId + " has unknown action: " + action, 1);
            return OperandValue();
        }
    }
    return OperandValue();
}

OperandValue processMathematicalBlock(Engine &engine, const std::string &objectId, const Block &block)
{
    if (block.type == "calc_basic")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut("calc_basic block for object " + objectId + " has invalid params structure. Expected 3 params.", 2);
            return OperandValue();
        }

        OperandValue leftOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue opVal = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue rightOp = getOperandValue(engine, objectId, block.paramsJson[2]);

        if (opVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("calc_basic operator is not a string for " + objectId, 2);
            return OperandValue();
        }
        std::string anOperator = opVal.string_val;

        if (anOperator == "PLUS")
        {

            double numLeft = 0.0, numRight = 0.0;
            bool leftIsStrictlyNumeric = false;
            bool rightIsStrictlyNumeric = false;

            if (leftOp.type == OperandValue::Type::NUMBER)
            {
                numLeft = leftOp.number_val;
                leftIsStrictlyNumeric = std::isfinite(numLeft);
            }
            else if (leftOp.type == OperandValue::Type::STRING)
            {
                try
                {
                    size_t idx;
                    numLeft = std::stod(leftOp.string_val, &idx);
                    if (idx == leftOp.string_val.length() && std::isfinite(numLeft))
                    {
                        leftIsStrictlyNumeric = true;
                    }
                }
                catch (...)
                {
                }
            }

            if (rightOp.type == OperandValue::Type::NUMBER)
            {
                numRight = rightOp.number_val;
                rightIsStrictlyNumeric = std::isfinite(numRight);
            }
            else if (rightOp.type == OperandValue::Type::STRING)
            {
                try
                {
                    size_t idx;
                    numRight = std::stod(rightOp.string_val, &idx);
                    if (idx == rightOp.string_val.length() && std::isfinite(numRight))
                    {
                        rightIsStrictlyNumeric = true;
                    }
                }
                catch (...)
                {
                }
            }

            if (leftIsStrictlyNumeric && rightIsStrictlyNumeric)
            {
                return OperandValue(numLeft + numRight);
            }
            else
            {
                return OperandValue(leftOp.asString() + rightOp.asString());
            }
        }

        double numLeft = leftOp.asNumber();
        double numRight = rightOp.asNumber();

        if (anOperator == "MINUS")
        {
            return OperandValue(numLeft - numRight);
        }
        else if (anOperator == "MULTI")
        {
            return OperandValue(numLeft * numRight);
        }
        else if (anOperator == "DIVIDE")
        {
            if (numRight == 0.0)
            {
                engine.EngineStdOut("Division by zero in calc_basic for " + objectId, 1);

                return OperandValue(numLeft / numRight);
            }
            return OperandValue(numLeft / numRight);
        }
        else
        {
            engine.EngineStdOut("Unknown operator in calc_basic: " + anOperator + " for " + objectId, 2);
            return OperandValue();
        }
    }
    engine.EngineStdOut("  Mathematical block type '" + block.type + "' not handled in processMathematicalBlock.", 1);
    return OperandValue();
}
/**
 * @brief 모양블럭
 *
 */
void Shape(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
{
}
/**
 * @brief 사운드블럭
 *
 */
void Sound(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
{
}
/**
 * @brief 변수블럭
 *
 */
void Variable(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
{
}
/**
 * @brief 함수블럭
 *
 */
void Function(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
{
}