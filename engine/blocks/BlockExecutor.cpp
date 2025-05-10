#include "BlockExecutor.h"
#include "../Engine.h"
#include "../Entity.h"
#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <limits>

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

        Behavior(block.type, engine, objectId, block);
        Mathematical(block.type, engine, objectId, block);
        Shape(block.type, engine, objectId, block);
        Sound(block.type, engine, objectId, block);
        Variable(block.type, engine, objectId, block);
        Function(block.type, engine, objectId, block);
    }
}
/**
 * @brief 행동블럭
 *
 */
void Behavior(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
{
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

        std::string coord_type_str;

        if (block.paramsJson.IsArray() && block.paramsJson.Size() > 0)
        {
            if (block.paramsJson[0].IsString())
            {
                coord_type_str = block.paramsJson[0].GetString();
            }
            else if (block.paramsJson[0].IsNull() && block.paramsJson.Size() > 1 && block.paramsJson[1].IsString())
            {

                coord_type_str = block.paramsJson[1].GetString();
            }
        }

        if (coord_type_str.empty())
        {
            engine.EngineStdOut("coordinate_mouse block for object " + objectId + " is missing or has invalid coordinate type parameter (expected 'x' or 'y').", 2);
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
            double sizeVal = targetEntity->getScaleX() * 100.0;
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
                return OperandValue(1.0);
            }

            if (targetObjInfo->costumes.empty())
            {
                engine.EngineStdOut("coordinate_object block for " + objectId + ": target entity '" + targetEntity->getId() + "' has no costumes.", 1);
                if (coordinateTypeStr == "picture_name")
                    return OperandValue("");
                return OperandValue(0.0);
            }

            const std::string &selectedCostumeId = targetObjInfo->selectedCostumeId;

            for (size_t i = 0; i < targetObjInfo->costumes.size(); ++i)
            {
                if (targetObjInfo->costumes[i].id == selectedCostumeId)
                {
                    if (coordinateTypeStr == "picture_index")
                    {
                        return OperandValue(static_cast<double>(i + 1));
                    }
                    else
                    {
                        return OperandValue(targetObjInfo->costumes[i].name);
                    }
                }
            }

            engine.EngineStdOut("coordinate_object - Selected costume ID '" + selectedCostumeId + "' not found in costume list for entity '" + targetEntity->getId() + "'. Using first costume as fallback.", 2);
            if (!targetObjInfo->costumes.empty())
            {
                if (coordinateTypeStr == "picture_index")
                    return OperandValue(1.0);
                return OperandValue(targetObjInfo->costumes[0].name);
            }
            if (coordinateTypeStr == "picture_name")
                return OperandValue("");
            return OperandValue(0.0);
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
            return OperandValue();
        }

        OperandValue left_op = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue operator_op = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue right_op = getOperandValue(engine, objectId, block.paramsJson[2]);

        if (operator_op.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("quotient_and_mod block for " + objectId + " has non-string operator parameter.", 2);
            return OperandValue();
        }
        std::string anOperator = operator_op.string_val;

        double left_val = left_op.asNumber();
        double right_val = right_op.asNumber();

        if (anOperator == "QUOTIENT")
        {
            if (right_val == 0.0)
            {
                engine.EngineStdOut("Division by zero in quotient_and_mod (QUOTIENT) for " + objectId, 1);

                return OperandValue(std::floor(left_val / right_val));
            }
            return OperandValue(std::floor(left_val / right_val));
        }
        else
        {
            if (right_val == 0.0)
            {
                engine.EngineStdOut("Division by zero in quotient_and_mod (MOD) for " + objectId, 1);

                return OperandValue(left_val - right_val * std::floor(left_val / right_val));
            }
            return OperandValue(left_val - right_val * std::floor(left_val / right_val));
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