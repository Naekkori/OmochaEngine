#include "BlockExecutor.h"
#include "util/AEhelper.h"
#include "../Engine.h"
#include "../Entity.h"
#include <string>
#include <vector>
#include <thread>
#include <cmath>
#include <algorithm> // std::clamp를 위해 추가
#include <limits>
#include <ctime>
#include "blockTypes.h"
AudioEngineHelper aeHelper; // 전역 AudioEngineHelper 인스턴스

// OperandValue 생성자 및 멤버 함수 구현 (BlockExecutor.h에 선언됨)
OperandValue::OperandValue() : type(Type::EMPTY), boolean_val(false), number_val(0.0)
{
}

OperandValue::OperandValue(double val) : type(Type::NUMBER), number_val(val), boolean_val(false)
{
}

OperandValue::OperandValue(const std::string &val) : type(Type::STRING), string_val(val), boolean_val(false),
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
        try
        {
            size_t idx = 0;
            double val = std::stod(string_val, &idx);
            if (idx == string_val.length()) // Ensure the whole string was parsed
            {
                return val;
            }
        }
        catch (const std::invalid_argument &)
        {
            // Consider logging or a more specific error handling
            // For now, re-throwing a generic message or returning 0.0
            // throw "Invalid number format"; // Or handle more gracefully
        }
        catch (const std::out_of_range &)
        {
            // throw "Number out of range"; // Or handle more gracefully
        }
    }
    return 0.0; // Default for non-convertible types or errors
}

std::string OperandValue::asString() const
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
        // Remove trailing zeros and decimal point if it's the last char
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
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

// processVariableBlock 선언이 누락된 것 같아 추가 (필요하다면)
// OperandValue processVariableBlock(Engine &engine, const std::string &objectId, const Block &block);

OperandValue getOperandValue(Engine &engine, const std::string &objectId, const rapidjson::Value &paramField)
{
    if (paramField.IsString())
    {
        std::string str_val_for_op = paramField.GetString();
        OperandValue temp_op_val(str_val_for_op);
        return temp_op_val;
    }
    else if (paramField.IsObject())
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
                    engine.EngineStdOut(
                        "Error converting number param: " + std::string(paramField["params"][0].GetString()) + " for " +
                            objectId + " - " + e.what(),
                        2);
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
            return processMathematicalBlock(engine, objectId, subBlock, "");
            // executionThreadId not available here, or pass if possible
        }
        else if (fieldType == "get_pictures")
        {
            // get_pictures 블록의 params[0]은 실제 모양 ID 문자열입니다.
            if (paramField.HasMember("params") && paramField["params"].IsArray() &&
                paramField["params"].Size() > 0 && paramField["params"][0].IsString())
            {
                std::string temp_image_id = paramField["params"][0].GetString();
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
            if (paramField.HasMember("params") && paramField["params"].IsArray() &&
                paramField["params"].Size() > 0 && paramField["params"][0].IsString())
            {
                std::string tem_sound_id = paramField["params"][0].GetString();
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
    else if (paramField.IsNumber()) // If the param is a direct number literal
    {
        return OperandValue(paramField.GetDouble()); // Returns NUMBER
    }
    else if (paramField.IsBool()) // If the param is a direct boolean literal
    {
        bool val = paramField.GetBool();
        engine.EngineStdOut(
            "Parameter field for " + objectId + " is a direct boolean literal: " + (val ? "true" : "false") +
                ". This might be unexpected if a block (e.g., get_pictures) was intended.",
            1);
        return OperandValue(val); // Returns BOOLEAN
    }
    else if (paramField.IsNull()) // If the param is a direct null literal
    {
        engine.EngineStdOut("Parameter field is null for " + objectId, 1);
        return OperandValue(); // Returns EMPTY
    }

    // Fallback if not string, object, number, bool, or null
    engine.EngineStdOut("Parameter field is not a string, object, number, boolean, or null for " + objectId + ". Actual type: " + std::to_string(paramField.GetType()), 1);
    return OperandValue();
}

/* 여기에 있던 excuteBlock 함수는 Entity.cpp 로 이동*/
/**
 * @brief 움직이기 블럭
 *
 */
void Moving(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block,
            const std::string &executionThreadId)
{
    Entity *entity = engine.getEntityById(objectId);
    if (!entity)
    {
        // Moving 함수 내 어떤 블록도 이 objectId에 대해 실행될 수 없으므로 여기서 공통 오류 처리 후 반환합니다.
        engine.EngineStdOut("Moving block execution failed: Entity " + objectId + " not found.", 2);
        return;
    }

    if (BlockType == "move_direction")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut(
                "move_direction block for object " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue direction = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (distance.type != OperandValue::Type::NUMBER || direction.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_direction block for object " + objectId + " has non-numeric params.", 2,
                                executionThreadId);
            return;
        }
        double dist = distance.number_val;
        double dir = direction.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newX = entity->getX() + dist * std::cos(dir * SDL_PI_D / 180.0);
        double newY = entity->getY() - dist * std::sin(dir * SDL_PI_D / 180.0);
        engine.EngineStdOut("move_direction objectId: " + objectId + " direction: " + to_string(newX) + ", " + to_string(newY), 0, executionThreadId);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1) // 파라미터 개수 확인 수정 (2개 -> 1개)
        {
            engine.EngineStdOut(
                "move_x block for object " + objectId + " has invalid params structure. Expected 1 param after filtering.", 2,
                executionThreadId);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (distance.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_x block for object " + objectId + " has non-numeric params.", 2,
                                executionThreadId);
            return;
        }
        double dist = distance.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newX = entity->getX() + dist;
        engine.EngineStdOut("move_x objId: " + objectId + " newX: " + to_string(newX), 0, executionThreadId);
        entity->setX(newX);
    }
    else if (BlockType == "move_y")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1) // 파라미터 개수 확인 수정 (2개 -> 1개)
        {
            engine.EngineStdOut(
                "move_y block for object " + objectId + " has invalid params structure. Expected 1 param after filtering.", 2,
                executionThreadId);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (distance.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_y block for object " + objectId + " has non-numeric params.", 2,
                                executionThreadId);
            return;
        }
        double dist = distance.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newY = entity->getY() + dist;
        engine.EngineStdOut("move_y objId: " + objectId + " newY: " + to_string(newY), 0, executionThreadId);
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
            if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 3)
            {
                engine.EngineStdOut(
                    "move_xy_time block for " + objectId + " is missing parameters. Expected TIME, X, Y.", 2,
                    executionThreadId);
                state.isActive = false; // Ensure it's not accidentally active
                return;
            }

            OperandValue timeOp = getOperandValue(engine, objectId, block.paramsJson[0]);
            OperandValue xOp = getOperandValue(engine, objectId, block.paramsJson[1]);
            OperandValue yOp = getOperandValue(engine, objectId, block.paramsJson[2]);

            if (timeOp.type != OperandValue::Type::NUMBER ||
                xOp.type != OperandValue::Type::NUMBER ||
                yOp.type != OperandValue::Type::NUMBER)
            {
                engine.EngineStdOut(
                    "move_xy_time block for " + objectId + " has non-number parameters. Time: " + timeOp.asString() +
                        ", X: " + xOp.asString() + ", Y: " + yOp.asString(),
                    2, executionThreadId);
                state.isActive = false;
                return;
            }

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
                engine.EngineStdOut("move_xy_time: " + objectId + " completed in single step. Pos: (" +
                                        std::to_string(entity->getX()) + ", " + std::to_string(entity->getY()) + ")",
                                    3, executionThreadId);
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
                engine.EngineStdOut("move_xy_time: " + objectId + " Pos: (" +
                                        std::to_string(entity->getX()) + ", " + std::to_string(entity->getY()) + ")",
                                    3, executionThreadId);
                state.isActive = false; // 이동 완료
            }
        }
    }
    else if (BlockType == "locate_x")
    {
        OperandValue valueX = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (valueX.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("locate_x block for object " + objectId + " is not a number.", 2, executionThreadId);
            return;
        }
        double x = valueX.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        engine.EngineStdOut("locate_x objId: " + objectId + " newX: " + to_string(x), 3, executionThreadId);
        entity->setX(x);
    }
    else if (BlockType == "locate_y")
    {
        OperandValue valueY = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (valueY.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("locate_y block for object " + objectId + " is not a number.", 2, executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        double y = valueY.number_val;
        engine.EngineStdOut("locate_y objId: " + objectId + " newX: " + to_string(y), 3, executionThreadId);
        entity->setY(y);
    }
    else if (BlockType == "locate_xy")
    {
        OperandValue valueXOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue valueYOp = getOperandValue(engine, objectId, block.paramsJson[1]);

        // Use asNumber() for type coercion. It returns 0.0 for strings that can't be converted.
        double x = valueXOp.asNumber();
        double y = valueYOp.asNumber();

        // The original strict type check is removed.
        // engine.EngineStdOut("locate_xy block for object " + objectId + " is not a number.", 2, executionThreadId);
        // No explicit error here, as 0.0 is a valid coordinate if conversion fails, matching typical Scratch-like behavior.

        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setX(x);
        entity->setY(y);
        engine.EngineStdOut("locate_xy objId: " + objectId + " newX: " + to_string(x) + " newY: " + to_string(y), 3, executionThreadId);
    }
    else if (BlockType == "locate")
    {
        // 이것은 마우스커서 나 오브젝트를 따라갑니다.
        OperandValue target = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (target.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("locate block for object " + objectId + " is not a string.", 2, executionThreadId);
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
            if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
            {
                // time, target 필요
                engine.EngineStdOut(
                    "locate_object_time block for " + objectId +
                        " is missing parameters. Expected TIME, TARGET_OBJECT_ID.",
                    2, executionThreadId);
                // state.isActive는 false로 유지됩니다.
                return;
            }

            OperandValue timeOp = getOperandValue(engine, objectId, block.paramsJson[0]);
            OperandValue targetOp = getOperandValue(engine, objectId, block.paramsJson[1]);

            if (timeOp.type != OperandValue::Type::NUMBER || targetOp.type != OperandValue::Type::STRING)
            {
                engine.EngineStdOut(
                    "locate_object_time block for " + objectId +
                        " has invalid parameters. Time should be number, target should be string.",
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
                    engine.EngineStdOut(
                        "locate_object_time: target object " + state.targetObjectId + " not found for " + objectId +
                            ".",
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
                    "locate_object_time: target object " + state.targetObjectId + " disappeared mid-move for " +
                        objectId + ".",
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
            engine.EngineStdOut("locate_object_time: " + objectId + " moving to" + state.targetObjectId + ". Pos: (" + to_string(newX) + ", " + to_string(newY) + ")", 3, executionThreadId);
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
        }
    }
    else if (BlockType == "rotate_relative" || BlockType == "direction_relative")
    {
        OperandValue value = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (value.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("rotate_relative block for object " + objectId + " is not a number.", 2,
                                executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setDirection(value.number_val + entity->getDirection());
    }
    else if (BlockType == "rotate_by_time" || BlockType == "direction_relative_duration")
    {
        OperandValue timeValue = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue angleValue = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (timeValue.type != OperandValue::Type::NUMBER || angleValue.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("rotate_by_time block for object " + objectId + " has non-number parameters.", 2,
                                executionThreadId);
            return;
        }
        double time = timeValue.number_val;
        double angle = angleValue.number_val;

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
        }
    }
    else if (BlockType == "rotate_absolute")
    {
        OperandValue angle = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (angle.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("rotate_absolute block for object " + objectId + " is not a number.", 2,
                                executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setRotation(angle.number_val);
    }
    else if (BlockType == "direction_absolute")
    {
        OperandValue angle = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (angle.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("direction_absolute block for object " + objectId + "is not a number.", 2,
                                executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setDirection(angle.number_val);
    }
    else if (BlockType == "see_angle_object")
    {
        OperandValue hasmouse = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (hasmouse.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("see_angle_object block for object " + objectId + "is not a string.", 2,
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
                engine.EngineStdOut(
                    "see_angle_object block for object " + objectId + ": target entity '" + hasmouse.string_val +
                        "' not found.",
                    2, executionThreadId);
            }
        }
    }
    else if (BlockType == "move_to_angle")
    {
        OperandValue setAngle = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue setDesnitance = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (setAngle.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_to_angle block for object " + objectId + "is not a number.", 2,
                                executionThreadId);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        double angle = setAngle.number_val;
        double distance = setDesnitance.number_val;
        entity->setX(entity->getX() + distance * cos(angle * SDL_PI_D / 180.0));
        entity->setY(entity->getY() + distance * sin(angle * SDL_PI_D / 180.0));
    }
}

/**
 * @brief 계산 블록
 *
 */
OperandValue Calculator(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block,
                        const std::string &executionThreadId)
{
    if (BlockType == "calc_basic")
    {
        return processMathematicalBlock(engine, objectId, block, executionThreadId);
    }
    else if (BlockType == "calc_rand")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut(
                "calc_rand block for object " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue minVal = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue maxVal = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (minVal.type != OperandValue::Type::NUMBER || maxVal.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("calc_rand block for object " + objectId + " has non-numeric params.", 2,
                                executionThreadId);
            return OperandValue();
        }
        double min = minVal.number_val;
        double max = maxVal.number_val;
        if (min >= max)
        {
            engine.EngineStdOut("calc_rand block for object " + objectId + " has invalid range.", 2, executionThreadId);
            return OperandValue();
        }
        double randVal = min + static_cast<double>(rand()) / (static_cast<double>(RAND_MAX / (max - min)));
        return OperandValue(randVal);
    }
    else if (BlockType == "coordinate_mouse")
    {
        // paramsKeyMap: { VALUE: 1 }
        // 드롭다운 값 ("x" 또는 "y")은 paramsJson[1]에 있습니다.
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() <= 1)
        {
            // 인덱스 1은 크기가 최소 2여야 함
            engine.EngineStdOut(
                "coordinate_mouse block for " + objectId +
                    " has invalid params structure. Expected param at index 1 for VALUE.",
                2, executionThreadId);
            return OperandValue(0.0);
        }

        OperandValue coordTypeOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        std::string coord_type_str;

        if (coordTypeOp.type == OperandValue::Type::STRING)
        {
            coord_type_str = coordTypeOp.asString();
        }
        else
        {
            engine.EngineStdOut(
                "coordinate_mouse block for " + objectId + ": VALUE parameter (index 1) is not a string.", 2,
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 4)
        {
            engine.EngineStdOut(
                "coordinate_object block for object " + objectId +
                    " has invalid params structure. Expected at least 4 params.",
                2, executionThreadId);
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
            engine.EngineStdOut(
                "coordinate_object block for " + objectId + ": target object ID parameter (VALUE) is not a string.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }
        std::string targetObjectIdStr = targetIdOpVal.asString();

        OperandValue coordinateTypeOpVal = getOperandValue(engine, objectId, block.paramsJson[3]);
        if (coordinateTypeOpVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "coordinate_object block for " + objectId + ": coordinate type parameter (COORDINATE) is not a string.",
                2, executionThreadId);
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
            double sizeVal = std::round((targetEntity->getScaleX() * 100.0) * 10.0) / 10.0;
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut("quotient_and_mod block for " + objectId + " parameter is invalid. Expected 3 params.",
                                2, executionThreadId);
            throw "알수없는 파라미터 크기";
        }

        OperandValue left_op = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue operator_op = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue right_op = getOperandValue(engine, objectId, block.paramsJson[2]);

        if (operator_op.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("quotient_and_mod block for " + objectId + " has non-string operator parameter.", 2,
                                executionThreadId);
            throw "알수없는 피연산자 타입";
            return OperandValue();
        }
        std::string anOperator = operator_op.string_val;

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
                return OperandValue(std::nan("")); // NaN 반환
            }
            return OperandValue(std::floor(left_val / right_val));
        }
        else
        {
            if (right_val == 0.0)
            {
                engine.EngineStdOut("Division by zero in quotient_and_mod (MOD) for " + objectId + ". Returning NaN.",
                                    2, executionThreadId);
                // throw "0으로 나누기 (나머지) (은)는 불가능합니다.";
                return OperandValue(std::nan("")); // NaN 반환
            }
            return OperandValue(left_val - right_val * std::floor(left_val / right_val));
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
        auto stringToMathOperation = [](const std::string &opStr) -> MathOperationType
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

        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut(
                "calc_operation block for " + objectId + " has invalid params. Expected 2 params (LEFTHAND, OPERATOR).",
                2, executionThreadId);
            return OperandValue(std::nan(""));
        }

        OperandValue leftOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue opVal = getOperandValue(engine, objectId, block.paramsJson[1]);

        if (leftOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("calc_operation block for " + objectId + " has non-numeric left operand.", 2,
                                executionThreadId);
            return OperandValue(std::nan(""));
        }
        if (opVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("calc_operation block for " + objectId + " has non-string operator.", 2,
                                executionThreadId);
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
        if (underscore_pos != 0)
        {
            // 찾지 못했거나(npos) 0보다 큰 인덱스에서 찾았을 경우 참
            if (underscore_pos != std::string::npos)
            {
                // 찾았고, 0번 위치가 아닐 경우
                anOperator = anOperator.substr(0, underscore_pos);
            }
            // 찾지 못했을 경우 (underscore_pos == std::string::npos), anOperator는 originalOperator로 유지됩니다.
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
        std::string errorMsg = "";

        MathOperationType opType = stringToMathOperation(anOperator);

        switch (opType)
        {
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
            if (leftNum < 0)
            {
                errorOccurred = true;
                errorMsg = "sqrt of negative number";
                result = std::nan("");
            }
            else
            {
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
            if (leftNum < -1.0 || leftNum > 1.0)
            {
                errorOccurred = true;
                errorMsg = "asin input out of range [-1, 1]";
                result = std::nan("");
            }
            else
            {
                result = std::asin(leftNum); // 라디안 값 반환
            }
            break;
        case MathOperationType::ACOS:
            if (leftNum < -1.0 || leftNum > 1.0)
            {
                errorOccurred = true;
                errorMsg = "acos input out of range [-1, 1]";
                result = std::nan("");
            }
            else
            {
                result = std::acos(leftNum); // 라디안 값 반환
            }
            break;
        case MathOperationType::ATAN:
            result = std::atan(leftNum); // 라디안 값 반환
            break;
        case MathOperationType::LOG: // 밑이 10인 로그
            if (leftNum <= 0)
            {
                errorOccurred = true;
                errorMsg = "log of non-positive number";
                result = std::nan("");
            }
            else
            {
                result = std::log10(leftNum);
            }
            break;
        case MathOperationType::LN: // 자연로그
            if (leftNum <= 0)
            {
                errorOccurred = true;
                errorMsg = "ln of non-positive number";
                result = std::nan("");
            }
            else
            {
                result = std::log(leftNum);
            }
            break;
        case MathOperationType::UNKNOWN:
        default:
            errorOccurred = true;
            errorMsg = "Unknown operator in calc_operation: " + originalOperator;
            result = std::nan("");
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() == 0 || !block.paramsJson[0].IsString())
        {
            engine.EngineStdOut("get_date block for " + objectId + " has invalid or missing action parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }

        // 이 블록의 파라미터는 항상 단순 문자열 드롭다운 값이므로 직접 접근합니다.
        std::string action = block.paramsJson[0].GetString();
        struct tm timeinfo_s; // localtime_s 및 localtime_r을 위한 구조체
        struct tm *timeinfo_ptr = nullptr;
#ifdef _WIN32
        localtime_s(&timeinfo_s, &now);
        timeinfo_ptr = &timeinfo_s;
#elif defined(__linux__) || defined(__APPLE__)
        localtime_r(&now, &timeinfo_s);
        timeinfo_ptr = &timeinfo_s;
#endif
        if (action == "year")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_year + 1900));
        }
        else if (action == "month")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_mon + 1));
        }
        else if (action == "day")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_mday));
        }
        else if (action == "hour")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_hour));
        }
        else if (action == "minute")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_min));
        }
        else if (action == "second")
        {
            return OperandValue(static_cast<double>(timeinfo_ptr->tm_sec));
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
        string targetId = block.paramsJson[0].GetString();
        if (targetId == "mouse")
        {
            if (engine.isMouseCurrentlyOnStage())
            {
                double mouseX = engine.getCurrentStageMouseX();
                double mouseY = engine.getCurrentStageMouseY();
                return OperandValue(std::sqrt(mouseX * mouseX + mouseY * mouseY));
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
            return OperandValue(std::sqrt(dx * dx + dy * dy));
        }
    }
    else if (BlockType == "length_of_string")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1)
        {
            engine.EngineStdOut(
                "length_of_string block for " + objectId + " has invalid params structure. Expected 1 param.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1)
        {
            engine.EngineStdOut(
                "reverse_of_string block for " + objectId + " has invalid params structure. Expected 1 param.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (strOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("reverse_of_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        std::string reversedStr = strOp.string_val;
        std::reverse(reversedStr.begin(), reversedStr.end());
        return OperandValue(reversedStr);
    }
    else if (BlockType == "combie_something")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut(
                "combie_something block for " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp1 = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue strOp2 = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp1.type != OperandValue::Type::STRING || strOp2.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("combie_something block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        std::string combinedStr = strOp1.string_val + strOp2.string_val;
        return OperandValue(combinedStr);
    }
    else if (BlockType == "char_at")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut("char_at block for " + objectId + " has invalid params structure. Expected 2 params.",
                                2, executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1]);
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
        return OperandValue(std::string(1, strOp.string_val[index]));
    }
    else if (BlockType == "substring")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut("substring block for " + objectId + " has invalid params structure. Expected 3 params.",
                                2, executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue startOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue endOp = getOperandValue(engine, objectId, block.paramsJson[2]);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut(
                "count_match_string block for " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue subStrOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp.type != OperandValue::Type::STRING || subStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("count_match_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        std::string str = strOp.string_val;
        std::string subStr = subStrOp.string_val;
        size_t count = 0;
        size_t pos = str.find(subStr);
        while (pos != std::string::npos)
        {
            count++;
            pos = str.find(subStr, pos + subStr.length());
        }
        return OperandValue(static_cast<double>(count));
    }
    else if (BlockType == "index_of_string")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut(
                "index_of_string block for " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue subStrOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp.type != OperandValue::Type::STRING || subStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("index_of_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        std::string str = strOp.string_val;
        std::string subStr = subStrOp.string_val;
        size_t pos = str.find(subStr);
        if (pos != std::string::npos)
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut(
                "replace_string block for " + objectId + " has invalid params structure. Expected 3 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue oldStrOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue newStrOp = getOperandValue(engine, objectId, block.paramsJson[2]);
        if (strOp.type != OperandValue::Type::STRING || oldStrOp.type != OperandValue::Type::STRING || newStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("replace_string block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        std::string str = strOp.string_val;
        std::string oldStr = oldStrOp.string_val;
        std::string newStr = newStrOp.string_val;
        size_t pos = str.find(oldStr);
        if (pos != std::string::npos)
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut(
                "change_string_case block for " + objectId + " has invalid params structure. Expected 2 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue caseOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp.type != OperandValue::Type::STRING || caseOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_string_case block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        std::string str = strOp.string_val;
        std::string caseType = caseOp.string_val;
        if (caseType == "upper")
        {
            std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        }
        else if (caseType == "lower")
        {
            std::transform(str.begin(), str.end(), str.begin(), ::tolower);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1)
        {
            engine.EngineStdOut(
                "get_block_count block for " + objectId + " has invalid params structure. Expected 1 param (OBJECT).",
                2, executionThreadId);
            return OperandValue(0.0);
        }
        OperandValue objectKeyOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (objectKeyOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "get_block_count block for " + objectId + " OBJECT parameter did not resolve to a string.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }
        std::string objectKey = objectKeyOp.string_val;

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
            std::string sceneIdToQuery = objectKey.substr(6);
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
            std::string targetObjId = objectKey.substr(7);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut(
                "change_rgb_to_hex block for " + objectId + " has invalid params structure. Expected 3 params.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue redOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue greenOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue blueOp = getOperandValue(engine, objectId, block.paramsJson[2]);
        if (redOp.type != OperandValue::Type::NUMBER || greenOp.type != OperandValue::Type::NUMBER || blueOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("change_rgb_to_hex block for " + objectId + " has non-number parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        int red = static_cast<int>(redOp.number_val);
        int green = static_cast<int>(greenOp.number_val);
        int blue = static_cast<int>(blueOp.number_val);
        std::stringstream hexStream;
        hexStream << std::hex << std::setw(2) << std::setfill('0') << red
                  << std::setw(2) << std::setfill('0') << green
                  << std::setw(2) << std::setfill('0') << blue;
        return OperandValue("#" + hexStream.str());
    }
    else if (BlockType == "change_hex_to_rgb")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1)
        {
            engine.EngineStdOut(
                "change_hex_to_rgb block for " + objectId + " has invalid params structure. Expected 1 param.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue hexOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (hexOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_hex_to_rgb block for " + objectId + " has non-string parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        std::string hexStr = hexOp.string_val;
        if (hexStr.length() != 7 || hexStr[0] != '#')
        {
            engine.EngineStdOut("change_hex_to_rgb block for " + objectId + " has invalid hex string: " + hexStr, 2,
                                executionThreadId);
            return OperandValue();
        }
        int red = std::stoi(hexStr.substr(1, 2), nullptr, 16);
        int green = std::stoi(hexStr.substr(3, 2), nullptr, 16);
        int blue = std::stoi(hexStr.substr(5, 2), nullptr, 16);
        return OperandValue(
            static_cast<double>(red) + static_cast<double>(green) / 256.0 + static_cast<double>(blue) / (256.0 * 256.0));
    }
    else if (BlockType == "get_boolean_value")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1)
        {
            engine.EngineStdOut(
                "get_boolean_value block for " + objectId + " has invalid params structure. Expected 1 param.", 2,
                executionThreadId);
            return OperandValue();
        }
        OperandValue boolOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (boolOp.type != OperandValue::Type::BOOLEAN)
        {
            engine.EngineStdOut("get_boolean_value block for " + objectId + " has non-boolean parameter.", 2,
                                executionThreadId);
            return OperandValue();
        }
        return OperandValue(boolOp.boolean_val);
    }
    else if (BlockType == "choose_project_timer_action")
    {
        // paramsKeyMap: { ACTION: 0 }
        // 드롭다운 값은 block.paramsJson[0]에 문자열로 저장되어 있을 것으로 예상합니다.
        // Block.h에서 paramsJson이 rapidjson::Value 타입이고,
        // 이 값은 loadProject 시점에 engine.m_blockParamsAllocatorDoc를 사용하여 할당됩니다.
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() == 0 || !block.paramsJson[0].IsString())
        {
            engine.EngineStdOut(
                "choose_project_timer_action block for " + objectId + " has invalid or missing action parameter.", 2,
                executionThreadId);
            return OperandValue();
        }

        // 이 블록의 파라미터는 항상 단순 문자열 드롭다운 값이므로 직접 접근합니다.
        // getOperandValue를 사용할 수도 있지만, 여기서는 직접 사용합니다.
        std::string action = block.paramsJson[0].GetString();

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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() == 0)
        {
            engine.EngineStdOut(
                "set_visible_project_timer block for " + objectId +
                    " has missing or invalid params array. Defaulting to HIDE.",
                2, executionThreadId);
            engine.showProjectTimer(false); // Default action
            return OperandValue();
        }
        OperandValue actionValue = getOperandValue(engine, objectId, block.paramsJson[0]);

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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            engine.EngineStdOut(
                "get_sound_duration block for " + objectId + " has insufficient parameters. Expected sound ID.", 2,
                executionThreadId);
            return OperandValue(0.0); // 오류 시 기본값 반환
        }
        OperandValue soundIdOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (soundIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "get_sound_duration block for " + objectId + ": sound ID parameter is not a string. Value: " + soundIdOp.asString(), 2, executionThreadId);
            return OperandValue(0.0);
        }
        std::string targetSoundId = soundIdOp.asString();

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

        const std::vector<SoundFile> &soundsVector = objInfo->sounds; // 참조로 받기

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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            engine.EngineStdOut(
                "get_variable block for " + objectId + " has insufficient parameters. Expected VARIABLE_ID.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }

        OperandValue variableIdOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (variableIdOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "get_variable block for " + objectId + ": VARIABLE_ID parameter did not resolve to a string. Value: " +
                    variableIdOp.asString(),
                2, executionThreadId);
            return OperandValue(0.0);
        }

        std::string variableIdToFind = variableIdOp.asString();
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
            if (hudVar.name == variableIdToFind && hudVar.objectId == objectId)
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
                if (hudVar.name == variableIdToFind && hudVar.objectId.empty())
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
        {
            engine.EngineStdOut(
                "value_of_index_from_list block for " + objectId +
                    " has insufficient parameters. Expected LIST_ID and INDEX.",
                2, executionThreadId);
            return OperandValue(""); // 데이터는 문자열이므로 오류 시 빈 문자열 반환
        }

        // 1. 리스트 ID 가져오기 (항상 드롭다운 메뉴의 문자열)
        if (!block.paramsJson[0].IsString())
        {
            engine.EngineStdOut(
                "value_of_index_from_list block for " + objectId + ": LIST_ID parameter is not a string.", 2,
                executionThreadId);
            return OperandValue("");
        }
        std::string listIdToFind = block.paramsJson[0].GetString();
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
            if (hudVar.variableType == "list" && hudVar.name == listIdToFind && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.name == listIdToFind && hudVar.objectId.empty())
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

        std::vector<ListItem> &listArray = targetListPtr->array;
        if (listArray.empty())
        {
            engine.EngineStdOut(
                "value_of_index_from_list block for " + objectId + ": List '" + listIdToFind + "' is empty.", 1,
                executionThreadId);
            return OperandValue("");
        }

        // 3. 인덱스 값 가져오기 및 처리
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        double resolvedIndex_1based = 0.0; // 처리 후 1기반 인덱스

        if (indexOp.type == OperandValue::Type::STRING)
        {
            std::string indexStr = indexOp.asString();
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
                    resolvedIndex_1based = std::stod(indexStr, &idx);
                    if (idx != indexStr.length() || !std::isfinite(resolvedIndex_1based))
                    {
                        engine.EngineStdOut(
                            "value_of_index_from_list: INDEX string '" + indexStr + "' is not a valid number for list '" + listIdToFind + "'.", 1, executionThreadId);
                        return OperandValue("");
                    }
                }
                catch (const std::exception &)
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
            if (!std::isfinite(resolvedIndex_1based))
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
        if (std::floor(resolvedIndex_1based) != resolvedIndex_1based)
        {
            // 정수인지 확인
            engine.EngineStdOut(
                "value_of_index_from_list: INDEX '" + std::to_string(resolvedIndex_1based) +
                    "' is not an integer for list '" + listIdToFind + "'.",
                1, executionThreadId);
            return OperandValue("");
        }

        long long finalIndex_1based = static_cast<long long>(resolvedIndex_1based);

        if (finalIndex_1based < 1 || finalIndex_1based > static_cast<long long>(listArray.size()))
        {
            engine.EngineStdOut(
                "value_of_index_from_list: INDEX " + std::to_string(finalIndex_1based) + " is out of bounds for list '" + listIdToFind + "' (size: " + std::to_string(listArray.size()) + ").", 1, executionThreadId);
            return OperandValue("");
        }

        size_t finalIndex_0based = static_cast<size_t>(finalIndex_1based - 1);

        // 5. 데이터 반환
        return OperandValue(listArray[finalIndex_0based].data);
    }
    else if (BlockType == "length_of_list")
    {
        // 리스트의 길이를 반환
        OperandValue listId = getOperandValue(engine, objectId, block.paramsJson[0]);
        // params: [LIST_ID_STRING, INDEX_VALUE_OR_BLOCK, null, null]
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
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
            if (hudVar.variableType == "list" && hudVar.name == listId.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.name == listId.asString() && hudVar.objectId.empty())
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
        OperandValue listId = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue dataOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (listId.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "is_included_in_list block for " + objectId + ": LIST_ID  parameter is not a string.", 2,
                executionThreadId);
            return OperandValue(0.0);
        }
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
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
            if (hudVar.variableType == "list" && hudVar.name == listId.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.name == listId.asString() && hudVar.objectId.empty())
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

    return OperandValue();
}

OperandValue processMathematicalBlock(Engine &engine, const std::string &objectId, const Block &block,
                                      const std::string &executionThreadId)
{
    if (block.type == "calc_basic")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut(
                "calc_basic block for object " + objectId + " has invalid params structure. Expected 3 params.", 2,
                executionThreadId);
            return OperandValue();
        }

        OperandValue leftOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue opVal = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue rightOp = getOperandValue(engine, objectId, block.paramsJson[2]);

        if (opVal.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("calc_basic operator is not a string for " + objectId, 2, executionThreadId);
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
                engine.EngineStdOut("Division by zero", 2, executionThreadId);
                return OperandValue(std::nan(""));
            }
            return OperandValue(numLeft / numRight);
        }
        else
        {
            engine.EngineStdOut("Unknown operator in calc_basic: " + anOperator + " for " + objectId, 2,
                                executionThreadId);
            return OperandValue();
        }
    }
    engine.EngineStdOut("  Mathematical block type '" + block.type + "' not handled in processMathematicalBlock.", 1,
                        executionThreadId);
    return OperandValue();
}

/**
 * @brief 모양새
 *
 */
void Looks(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block,
           const std::string &executionThreadId)
{
    Entity *entity = engine.getEntityById(objectId);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 3)
        {
            // 인디케이터 포함하면 4개일 수 있음
            engine.EngineStdOut(
                "dialog_time block for " + objectId + " has insufficient parameters. Expected message, time, option.",
                2, executionThreadId);
            return;
        }
        OperandValue messageOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue timeOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue optionOp = getOperandValue(engine, objectId, block.paramsJson[2]); // Dropdown value

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

        std::string message = messageOp.asString();
        Uint64 durationMs = static_cast<Uint64>(timeOp.asNumber() * 1000.0);
        std::string dialogType = optionOp.asString();
        entity->showDialog(message, dialogType, durationMs);
    }
    else if (BlockType == "dialog")
    {
        // params: VALUE (message), OPTION (speak/think)
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
        {
            // 인디케이터 포함하면 3개일 수 있음
            engine.EngineStdOut(
                "dialog block for " + objectId + " has insufficient parameters. Expected message, option.", 2,
                executionThreadId);
            return;
        }
        OperandValue messageOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue optionOp = getOperandValue(engine, objectId, block.paramsJson[1]); // Dropdown value

        if (optionOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "dialog block for " + objectId + ": OPTION parameter is not a string. Value: " + optionOp.asString(), 2,
                executionThreadId);
            return;
        }

        std::string message = messageOp.asString();
        std::string dialogType = optionOp.asString();
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
        if (block.paramsJson.IsArray() && !block.paramsJson.Empty())
        {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            block.paramsJson[0].Accept(writer);
            engine.EngineStdOut(
                "DEBUG: change_to_some_shape for " + objectId + ": Raw paramField[0] before getOperandValue: " +
                    std::string(buffer.GetString()),
                3, executionThreadId);
        }
        else
        {
            engine.EngineStdOut(
                "DEBUG: change_to_some_shape for " + objectId + ": paramsJson is not an array or is empty.", 3, executionThreadId);
        }
        // --- DEBUG END ---

        OperandValue imageDropdown = getOperandValue(engine, objectId, block.paramsJson[0]);
        // getOperandValue는 get_pictures 블록의 params[0] (모양 ID 문자열)을 반환해야 합니다.
        // getOperandValue 내부에서 get_pictures 타입 처리가 필요합니다.

        if (imageDropdown.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "change_to_some_shape block for " + objectId +
                    " parameter did not resolve to a string (expected costume ID). Actual type: " +
                    std::to_string(static_cast<int>(imageDropdown.type)),
                2, executionThreadId);
            // --- DEBUG START ---
            engine.EngineStdOut(
                "DEBUG: change_to_some_shape for " + objectId + ": imageDropdown.asString() returned: '" + imageDropdown.asString() + "'", 3, executionThreadId);
            // --- DEBUG END ---
            return;
        }
        std::string costumeIdToSet = imageDropdown.asString();
        if (costumeIdToSet.empty())
        {
            engine.EngineStdOut("change_to_some_shape block for " + objectId + " received an empty costume ID.", 2,
                                executionThreadId);
            return;
        }
        // --- DEBUG START ---
        engine.EngineStdOut(
            "DEBUG: change_to_some_shape for " + objectId + ": Attempting to set costume to ID: '" + costumeIdToSet + "'", 3, executionThreadId);
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
        OperandValue nextorprev = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (nextorprev.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "change_to_some_shape block for " + objectId +
                    " parameter did not resolve to a string (expected costume ID). Actual type: " +
                    std::to_string(static_cast<int>(nextorprev.type)),
                2, executionThreadId);
            return;
        }
        std::string direction = nextorprev.asString();
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
        {
            // 인디케이터 포함 시 3개일 수 있음
            engine.EngineStdOut(
                "add_effect_amount block for " + objectId + " has insufficient parameters. Expected EFFECT, VALUE.", 2,
                executionThreadId);
            return;
        }
        OperandValue effectTypeOp = getOperandValue(engine, objectId, block.paramsJson[0]);  // EFFECT dropdown
        OperandValue effectValueOp = getOperandValue(engine, objectId, block.paramsJson[1]); // VALUE number

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

        std::string effectName = effectTypeOp.asString();
        double value = effectValueOp.asNumber();

        if (effectName == "color")
        {
            // JavaScript의 'hsv'에 해당, 여기서는 색조(hue)로 처리
            entity->setEffectHue(entity->getEffectHue() + value);
            // engine.EngineStdOut("Entity " + objectId + " effect 'color' (hue) changed by " + std::to_string(value) + ", new value: " + std::to_string(entity->getEffectHue()), 0);
        }
        else if (effectName == "brightness")
        {
            entity->setEffectBrightness(entity->getEffectBrightness() + value);
            // engine.EngineStdOut("Entity " + objectId + " effect 'brightness' changed by " + std::to_string(value) + ", new value: " + std::to_string(entity->getEffectBrightness()), 0);
        }
        else if (effectName == "transparency")
        {
            // JavaScript: sprite.effect.alpha = sprite.effect.alpha - effectValue / 100;
            // 여기서 effectValue는 0-100 범위의 값으로, 투명도를 증가시킵니다 (알파 값을 감소시킴).
            // Entity의 m_effectAlpha는 0.0(투명) ~ 1.0(불투명) 범위입니다.
            entity->setEffectAlpha(entity->getEffectAlpha() - (value / 100.0));
            // engine.EngineStdOut("Entity " + objectId + " effect 'transparency' (alpha) changed by " + std::to_string(value) + "%, new value: " + std::to_string(entity->getEffectAlpha()), 0);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
        {
            engine.EngineStdOut(
                "change_effect_amount block for " + objectId + " has insufficient parameters. Expected EFFECT, VALUE.",
                2, executionThreadId);
            return;
        }
        OperandValue effectTypeOp = getOperandValue(engine, objectId, block.paramsJson[0]);  // EFFECT dropdown
        OperandValue effectValueOp = getOperandValue(engine, objectId, block.paramsJson[1]); // VALUE number

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

        std::string effectName = effectTypeOp.asString();
        double value = effectValueOp.asNumber();
        if (effectName == "color")
        {
            entity->setEffectHue(value);
            engine.EngineStdOut("Entity " + objectId + " effect 'color' (hue) changed to " + std::to_string(value), 0,
                                executionThreadId);
        }
        else if (effectName == "brightness")
        {
            entity->setEffectBrightness(value);
            engine.EngineStdOut("Entity " + objectId + " effect 'brightness' changed to " + std::to_string(value), 0,
                                executionThreadId);
        }
        else if (effectName == "transparency")
        {
            entity->setEffectAlpha(1 - (value / 100.0));
            engine.EngineStdOut(
                "Entity " + objectId + " effect 'transparency' (alpha) changed to " + std::to_string(value), 0);
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
        OperandValue size = getOperandValue(engine, objectId, block.paramsJson[0]);
        entity->setScaleX(entity->getScaleX() + size.asNumber());
        entity->setScaleY(entity->getScaleY() + size.asNumber());
    }
    else if (BlockType == "set_scale_size")
    {
        OperandValue setSize = getOperandValue(engine, objectId, block.paramsJson[0]);
        entity->setScaleX(setSize.asNumber());
        entity->setScaleY(setSize.asNumber());
    }
    else if (BlockType == "stretch_scale_size")
    {
        OperandValue setWidth = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue setHeight = getOperandValue(engine, objectId, block.paramsJson[1]);
        entity->setWidth(setWidth.asNumber());
        entity->setHeight(setHeight.asNumber());
    }
    else if (BlockType == "reset_scale_size")
    {
        entity->resetScaleSize();
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
        OperandValue zindexEnumDropdown = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (zindexEnumDropdown.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_object_index object is not String", 2, executionThreadId);
            return;
        }
        std::string zindexEnumStr = zindexEnumDropdown.asString();
        Omocha::ObjectIndexChangeType changeType = Omocha::stringToObjectIndexChangeType(zindexEnumStr);
        engine.changeObjectIndex(objectId, changeType);
    }
}

/**
 * @brief 사운드블럭
 *
 */
void Sound(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block,
           const std::string &executionThreadId)
{
    Entity *entity = engine.getEntityById(objectId);
    if (BlockType == "sound_something_with_block")
    {
        OperandValue soundType = getOperandValue(engine, objectId, block.paramsJson[0]);

        if (soundType.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "sound_something_with_block for object " + objectId + ": sound ID parameter is not a string. Value: " +
                    soundType.asString(),
                2, executionThreadId);
            return;
        }
        std::string soundIdToPlay = soundType.asString();

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
        OperandValue soundType = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue soundTime = getOperandValue(engine, objectId, block.paramsJson[1]);
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

        std::string soundIdToPlay = soundType.asString();

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
        OperandValue soundId = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue from = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue to = getOperandValue(engine, objectId, block.paramsJson[2]);
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

        std::string soundIdToPlay = soundId.asString();
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
        OperandValue soundId = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (soundId.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "sound_something_wait_with_block for object" + objectId + ": received an empty sound ID", 2,
                executionThreadId);
            return;
        }
        std::string soundIdToPlay = soundId.asString();
        entity->waitforPlaysound(soundIdToPlay);
    }
    else if (BlockType == "sound_something_second_wait_with_block")
    {
        OperandValue soundId = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue soundTime = getOperandValue(engine, objectId, block.paramsJson[1]);
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
        std::string soundIdToPlay = soundId.asString();
        double soundTimeValue = soundTime.asNumber();

        entity->waitforPlaysoundWithSeconds(soundIdToPlay, soundTimeValue);
    }
    else if (BlockType == "sound_from_to_and_wait")
    {
        OperandValue soundId = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue from = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue to = getOperandValue(engine, objectId, block.paramsJson[2]);
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
        std::string soundIdToPlay = soundId.asString();
        double fromTime = from.asNumber();
        double toTime = to.asNumber();
        entity->waitforPlaysoundWithFromTo(soundIdToPlay, fromTime, toTime);
    }
    else if (BlockType == "sound_volume_change")
    {
        // 파라미터는 하나 (VALUE) - 볼륨 변경량 (예: 10, -20)
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            // VALUE 파라미터 확인
            engine.EngineStdOut(
                "sound_volume_change block for object " + objectId + " has insufficient parameters. Expected VALUE.", 2,
                executionThreadId);
            return;
        }
        OperandValue volumeChangeOp = getOperandValue(engine, objectId, block.paramsJson[0]);
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
        newGlobalVolume = std::clamp(newGlobalVolume, 0.0f, 1.0f); // 0.0 ~ 1.0 범위로 제한

        engine.aeHelper.setGlobalVolume(newGlobalVolume);
        engine.EngineStdOut(
            "Global volume changed by " + std::to_string(volumeChangePercentage) + "%. New global volume: " +
                std::to_string(newGlobalVolume) + " (triggered by object " + objectId + ")",
            0, executionThreadId);
    }
    else if (BlockType == "sound_volume_set")
    {
        // 파라미터는 하나 (VALUE) - 볼륨 변경량 (예: 10, -20)
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            // VALUE 파라미터 확인
            engine.EngineStdOut(
                "sound_volume_change block for object " + objectId + " has insufficient parameters. Expected VALUE.", 2,
                executionThreadId);
            return;
        }
        OperandValue volumeChangeOp = getOperandValue(engine, objectId, block.paramsJson[0]);
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
        OperandValue speed = getOperandValue(engine, objectId, block.paramsJson[0]);
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

        double clampedSpeed = std::clamp(static_cast<double>(newSpeedFactor), 0.5, 2.0); // 엔트리와 동일하게 0.5 ~ 2.0 범위로 제한
        engine.aeHelper.setGlobalPlaybackSpeed(static_cast<float>(clampedSpeed));
    }
    else if (BlockType == "sound_speed_set")
    {
        OperandValue speed = getOperandValue(engine, objectId, block.paramsJson[0]);
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
        if (block.paramsJson.Empty() || !block.paramsJson[0].IsString())
        {
            engine.EngineStdOut(
                "sound_silent_all for object " + objectId + ": TARGET parameter is missing or not a string.", 2,
                executionThreadId);
            return;
        }
        std::string target = block.paramsJson[0].GetString();

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
        OperandValue soundIdOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (soundIdOp.type != OperandValue::Type::STRING) // getOperandValue가 get_sounds를 처리하여 문자열 ID를 반환해야 함
        {
            engine.EngineStdOut(
                "play_bgm for object " + objectId + ": sound ID parameter is not a string. Value: " + soundIdOp.asString(), 2, executionThreadId);
            throw ScriptBlockExecutionError(
                "play_bgm 블록의 사운드 ID 파라미터가 문자열이 아닙니다.",
                block.id, BlockType, objectId, "Sound ID parameter is not a string. Value: " + soundIdOp.asString());
        }
        std::string soundIdToPlay = soundIdOp.asString(); // 실제 사운드 ID
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
            engine.aeHelper.stopBackgroundMusic(); // 기존 BGM 중지
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
 * @brief 변수블럭
 *
 */
void Variable(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block,
              const std::string &executionThreadId)
{
    if (BlockType == "set_visible_answer")
    {
        OperandValue visibleDropdown = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (visibleDropdown.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut(
                "set_visible_answer for object " + objectId + ": visible parameter is not a string. Value: " +
                    visibleDropdown.asString(),
                2, executionThreadId);
            return;
        }
        std::string visible = visibleDropdown.asString();
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            engine.EngineStdOut(
                "ask_and_wait block for " + objectId + " has insufficient parameters. Expected question.", 2,
                executionThreadId);
            throw ScriptBlockExecutionError("질문 파라미터가 부족합니다.", block.id, BlockType, objectId,
                                            "Insufficient parameters for ask_and_wait.");
        }

        OperandValue questionOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        std::string questionMessage = questionOp.asString();

        if (questionMessage.empty())
        {
            // EntryJS는 빈 메시지를 허용하지 않는 것으로 보임 (오류)
            engine.EngineStdOut(
                "ask_and_wait block for " + objectId + ": question message is empty. Proceeding with empty question.",
                2, executionThreadId);
            throw ScriptBlockExecutionError("질문 내용이 비어있습니다.", block.id, BlockType, objectId,
                                            "Question message cannot be empty.");
        }

        Entity *entity = engine.getEntityById(objectId);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
        {
            engine.EngineStdOut(
                "change_variable block for " + objectId +
                    " has insufficient parameters. Expected VARIABLE_ID and VALUE.",
                2, executionThreadId);
            return;
        }

        // 1. 변수 ID 가져오기 (항상 문자열 드롭다운)
        if (!block.paramsJson[0].IsString())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": VARIABLE_ID parameter is not a string.", 2,
                                executionThreadId);
            return;
        }
        std::string variableIdToFind = block.paramsJson[0].GetString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return;
        }

        // 2. 더하거나 이어붙일 값 가져오기
        OperandValue valueToAddOp = getOperandValue(engine, objectId, block.paramsJson[1]);

        // 3. 변수 찾기 (로컬 우선, 없으면 전역)
        HUDVariableDisplay *targetVarPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.name == variableIdToFind && hudVar.objectId == objectId)
            {
                targetVarPtr = &hudVar;
                break;
            }
        }
        if (!targetVarPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.name == variableIdToFind && hudVar.objectId.empty())
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
            currentVarNumericValue = std::stod(targetVarPtr->value, &idx);
            if (idx == targetVarPtr->value.length() && std::isfinite(currentVarNumericValue))
            {
                // 전체 문자열이 파싱되었고 유한한 숫자인지 확인
                currentVarIsNumeric = true;
            }
        }
        catch (const std::exception &)
        {
            // 파싱 실패 시 currentVarIsNumeric는 false로 유지
        }

        // 더할 값도 숫자인지 확인
        bool valueToAddIsNumeric = (valueToAddOp.type == OperandValue::Type::NUMBER && std::isfinite(
                                                                                           valueToAddOp.asNumber()));

        if (currentVarIsNumeric && valueToAddIsNumeric)
        {
            // 둘 다 숫자면 덧셈
            double sumValue = currentVarNumericValue + valueToAddOp.asNumber();

            // EntryJS의 toFixed와 유사한 효과를 내기 위해 std::to_string 사용 후 후처리
            std::string resultStr = std::to_string(sumValue);
            resultStr.erase(resultStr.find_last_not_of('0') + 1, std::string::npos);
            if (!resultStr.empty() && resultStr.back() == '.')
            {
                resultStr.pop_back();
            }
            targetVarPtr->value = resultStr;
            engine.EngineStdOut(
                "Variable '" + variableIdToFind + "' (numeric) changed by " + valueToAddOp.asString() + " to " +
                    targetVarPtr->value,
                0, executionThreadId);
        }
        else
        {
            // 하나라도 숫자가 아니면 문자열 이어붙이기
            targetVarPtr->value = targetVarPtr->value + valueToAddOp.asString();
            engine.EngineStdOut(
                "Variable '" + variableIdToFind + "' (string) concatenated with " + valueToAddOp.asString() + " to " +
                    targetVarPtr->value,
                0, executionThreadId);
        }
        if (targetVarPtr->isCloud)
        {
            engine.saveCloudVariablesToJson();
        }
    }
    else if (BlockType == "set_variable")
    {
        // params: [VARIABLE_ID_STRING, VALUE_TO_ADD_OR_CONCAT, null, null]
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
        {
            engine.EngineStdOut(
                "change_variable block for " + objectId +
                    " has insufficient parameters. Expected VARIABLE_ID and VALUE.",
                2, executionThreadId);
            return;
        }

        // 1. 변수 ID 가져오기 (항상 문자열 드롭다운)
        if (!block.paramsJson[0].IsString())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": VARIABLE_ID parameter is not a string.", 2,
                                executionThreadId);
            return;
        }
        std::string variableIdToFind = block.paramsJson[0].GetString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return;
        }

        // 2. 설정 할 값 가져오기
        OperandValue valueToSet = getOperandValue(engine, objectId, block.paramsJson[1]);

        // 3. 변수 찾기 (로컬 우선, 없으면 전역)
        HUDVariableDisplay *targetVarPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.name == variableIdToFind && hudVar.objectId == objectId)
            {
                targetVarPtr = &hudVar;
                break;
            }
        }
        if (!targetVarPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.name == variableIdToFind && hudVar.objectId.empty())
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            engine.EngineStdOut(
                "change_variable block for " + objectId +
                    " has insufficient parameters. Expected VARIABLE_ID and VALUE.",
                2, executionThreadId);
            return;
        }

        if (!block.paramsJson[0].IsString())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": VARIABLE_ID parameter is not a string.", 2,
                                executionThreadId);
            return;
        }
        std::string variableIdToFind = block.paramsJson[0].GetString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return;
        }

        HUDVariableDisplay *targetVarPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.name == variableIdToFind && hudVar.objectId == objectId)
            {
                targetVarPtr = &hudVar;
                break;
            }
        }
        if (!targetVarPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.name == variableIdToFind && hudVar.objectId.empty())
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            engine.EngineStdOut(
                "change_variable block for " + objectId +
                    " has insufficient parameters. Expected VARIABLE_ID and VALUE.",
                2, executionThreadId);
            return;
        }

        if (!block.paramsJson[0].IsString())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": VARIABLE_ID parameter is not a string.", 2,
                                executionThreadId);
            return;
        }
        std::string variableIdToFind = block.paramsJson[0].GetString();
        if (variableIdToFind.empty())
        {
            engine.EngineStdOut("change_variable block for " + objectId + ": received an empty VARIABLE_ID.", 2,
                                executionThreadId);
            return;
        }

        HUDVariableDisplay *targetVarPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.name == variableIdToFind && hudVar.objectId == objectId)
            {
                targetVarPtr = &hudVar;
                break;
            }
        }
        if (!targetVarPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.name == variableIdToFind && hudVar.objectId.empty())
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
        {
            // LIST_ID와 VALUE, 총 2개의 파라미터 필요
            engine.EngineStdOut(
                "add_value_to_list block for " + objectId + " has insufficient parameters. Expected LIST_ID and VALUE.",
                2, executionThreadId);
            return;
        }

        // 1. 리스트 ID 가져오기 (항상 드롭다운 메뉴의 문자열)
        if (!block.paramsJson[0].IsString())
        {
            engine.EngineStdOut(
                "add_value_to_list block for " + objectId + ": LIST_ID parameter (index 0) is not a string.", 2,
                executionThreadId);
            return;
        }
        std::string listIdToFind = block.paramsJson[0].GetString();
        if (listIdToFind.empty())
        {
            engine.EngineStdOut("add_value_to_list block for " + objectId + ": received an empty LIST_ID.", 2,
                                executionThreadId);
            return;
        }

        // 2. 리스트에 추가할 값 가져오기
        OperandValue valueOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        std::string valueToAdd = valueOp.asString(); // 모든 Operand 타입을 문자열로 변환하여 리스트에 저장

        // 3. 대상 리스트 찾기 (지역 리스트 우선, 없으면 전역 리스트)
        HUDVariableDisplay *targetListPtr = nullptr;

        // 지역 리스트 검색 (현재 오브젝트에 속한 리스트)
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.name == listIdToFind && hudVar.objectId == objectId)
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
                if (hudVar.variableType == "list" && hudVar.name == listIdToFind && hudVar.objectId.empty())
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
            engine.EngineStdOut(
                "Added value '" + valueToAdd + "' to list '" + listIdToFind + "' for object " + objectId, 0,
                executionThreadId);
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId +
                    " has insufficient parameters. Expected LIST_ID and INDEX.",
                2, executionThreadId);
            return;
        }

        // 1. 리스트 ID 가져오기 (항상 드롭다운 메뉴의 문자열)
        if (!block.paramsJson[0].IsString())
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": LIST_ID parameter (index 0) is not a string.", 2,
                executionThreadId);
            return;
        }
        std::string listIdToFind = block.paramsJson[0].GetString();
        if (listIdToFind.empty())
        {
            engine.EngineStdOut("remove_value_from_list block for " + objectId + ": received an empty LIST_ID.", 2,
                                executionThreadId);
            return;
        }

        // 2. 삭제할 인덱스 값 가져오기
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (indexOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut(
                "remove_value_from_list block for " + objectId + ": INDEX parameter is not a number. Value: " + indexOp.asString(), 2, executionThreadId);
            return;
        }
        double index_1_based_double = indexOp.asNumber();

        // 인덱스가 유한한 정수인지 확인
        if (!std::isfinite(index_1_based_double) || std::floor(index_1_based_double) != index_1_based_double)
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
            if (hudVar.variableType == "list" && hudVar.name == listIdToFind && hudVar.objectId == objectId)
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
                if (hudVar.variableType == "list" && hudVar.name == listIdToFind && hudVar.objectId.empty())
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

        std::vector<ListItem> &listArray = targetListPtr->array;

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
                "remove_value_from_list block for " + objectId + ": Index " + std::to_string(index_1_based) +
                    " is out of bounds for list '" + listIdToFind + "' (size: " + std::to_string(listArray.size()) + ").",
                1, executionThreadId);
            return;
        }

        // 6. 0기반 인덱스로 변환하여 항목 삭제
        size_t index_0_based = static_cast<size_t>(index_1_based - 1);

        std::string removedItemData = listArray[index_0_based].data; // 로깅을 위해 삭제될 데이터 저장
        listArray.erase(listArray.begin() + index_0_based);

        engine.EngineStdOut(
            "Removed item at index " + std::to_string(index_1_based) + " (value: '" + removedItemData + "') from list '" + listIdToFind + "' for object " + objectId, 0, executionThreadId);
        if (targetListPtr->isCloud)
        {
            engine.saveCloudVariablesToJson();
        }
    }
    else if (BlockType == "insert_value_to_list")
    {
        // 특정 인덱스에 항목 삽입
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 3)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": insufficient parameters", 2);
            return;
        }
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue listIdToFindOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue valueOp = getOperandValue(engine, objectId, block.paramsJson[2]);
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
            if (hudVar.variableType == "list" && hudVar.name == listIdToFindOp.asString() && hudVar.objectId == objectId)
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
                if (hudVar.variableType == "list" && hudVar.name == listIdToFindOp.asString() && hudVar.objectId.empty())
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
        std::vector<ListItem> &listArray = targetListPtr->array;
        if (index_1_based < 1 || index_1_based > static_cast<long long>(listArray.size()))
        {
            engine.EngineStdOut("insert_value_to_list block for" + objectId + ": index " + to_string(index_1_based_double) + " is out of bounds for list '" + listIdToFindOp.asString() +
                                    "' (size: " + std::to_string(listArray.size()) + ").",
                                1, executionThreadId);
            return;
        }
        size_t index_0_based = static_cast<size_t>(index_1_based - 1);
        listArray.insert(listArray.begin() + index_0_based, {valueOp.asString()});
        if (targetListPtr->isCloud)
        {
            engine.saveCloudVariablesToJson();
        }
        engine.EngineStdOut("Inserted value '" + valueOp.asString() + "' at index " + std::to_string(index_1_based) + " (value: '" + valueOp.asString() + "') to list '");
    }
    else if (BlockType == "change_value_list_index")
    {
        // 특정 인덱스에 항목 변경
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 3)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": insufficient parameters", 2);
            return;
        }
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue listIdToFindOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue valueOp = getOperandValue(engine, objectId, block.paramsJson[2]);
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
            if (hudVar.variableType == "list" && hudVar.name == listIdToFindOp.asString() && hudVar.objectId == objectId)
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
                if (hudVar.variableType == "list" && hudVar.name == listIdToFindOp.asString() && hudVar.objectId.empty())
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
        std::vector<ListItem> &listArray = targetListPtr->array;
        if (index_1_based < 1 || index_1_based > static_cast<long long>(listArray.size()))
        {
            engine.EngineStdOut("insert_value_to_list block for" + objectId + ": index " + to_string(index_1_based_double) + " is out of bounds for list '" + listIdToFindOp.asString() +
                                    "' (size: " + std::to_string(listArray.size()) + ").",
                                1, executionThreadId);
            return;
        }
        size_t index_0_based = static_cast<size_t>(index_1_based - 1);
        listArray[index_0_based].data = valueOp.asString();
    }
    else if (BlockType == "show_list")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": insufficient parameters", 2);
            return;
        }
        OperandValue listIdtofindOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (listIdtofindOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("listId is not a string objId:" + objectId, 2);
            return;
        }
        HUDVariableDisplay *targetListPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.name == listIdtofindOp.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.name == listIdtofindOp.asString() && hudVar.objectId.empty())
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            engine.EngineStdOut("insert_value_to_list block for " + objectId + ": insufficient parameters", 2);
            return;
        }
        OperandValue listIdtofindOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (listIdtofindOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("listId is not a string objId:" + objectId, 2);
            return;
        }
        HUDVariableDisplay *targetListPtr = nullptr;
        for (auto &hudVar : engine.getHUDVariables_Editable())
        {
            if (hudVar.variableType == "list" && hudVar.name == listIdtofindOp.asString() && hudVar.objectId == objectId)
            {
                targetListPtr = &hudVar;
                break;
            }
        }
        if (!targetListPtr)
        {
            for (auto &hudVar : engine.getHUDVariables_Editable())
            {
                if (hudVar.variableType == "list" && hudVar.name == listIdtofindOp.asString() && hudVar.objectId.empty())
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
 * @brief 흐름
 *
 */
void Flow(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block,
          const std::string &executionThreadId)
{
    Entity *entity = engine.getEntityById(objectId);
    if (!entity)
    {
        engine.EngineStdOut("Flow 'wait_second': Entity " + objectId + " not found.", 2, executionThreadId);
        return;
    }
    if (BlockType == "wait_second")
    {

        // Flow 함수는 Entity의 setScriptWait를 호출하여 대기 상태 설정을 요청합니다.
        // 실제 대기(SDL_Delay)는 Entity::executeScript의 메인 루프에서 처리됩니다.
            if (!block.paramsJson.IsArray() || block.paramsJson.Empty() || block.paramsJson[0].IsNull())
            {
                engine.EngineStdOut("Flow 'wait_second' for " + objectId + ": Missing or invalid time parameter.", 2, executionThreadId);
                // engine.terminateScriptExecution(executionThreadId);
                return;
            }

            OperandValue secondsOp = getOperandValue(engine, objectId, block.paramsJson[0]);
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

            Uint32 waitEndTime = SDL_GetTicks() + static_cast<Uint32>(secondsToWait * 1000.0);
            entity->setScriptWait(executionThreadId, waitEndTime, block.id); // 엔티티에 대기 상태 설정 요청

            engine.EngineStdOut("Flow 'wait_second': " + objectId + " (Thread: " + executionThreadId + ") requested wait for " + std::to_string(secondsToWait) + "s. Block ID: " + block.id, 0, executionThreadId);
    }
    else if (BlockType == "repeat_basic")
    {
        // 기본 반복 블록 구현
        // 반복 횟수 파라미터 가져오기
        if (!block.paramsJson.IsArray() || block.paramsJson.Empty() || block.paramsJson[0].IsNull())
        {
            engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + ": Missing or invalid iteration count parameter.", 2, executionThreadId);
            throw ScriptBlockExecutionError("반복 횟수 파라미터가 부족하거나 유효하지 않습니다.", block.id, BlockType, objectId, "Missing or invalid iteration count parameter.");
        }
        OperandValue iterOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (iterOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + ": Iteration count parameter is not a number. Value: " + iterOp.asString(), 2, executionThreadId);
            throw ScriptBlockExecutionError("반복 횟수 파라미터가 숫자가 아닙니다.", block.id, BlockType, objectId, "Iteration count parameter is not a number.");
        }
        double iterNumber = iterOp.asNumber();
        if (iterNumber < 0)
        {
            engine.EngineStdOut("Flow 'repeat_basic' for " + objectId + ": Iteration count cannot be negative. Value: " + std::to_string(iterNumber), 2, executionThreadId);
            throw ScriptBlockExecutionError("반복 횟수는 음수일 수 없습니다.", block.id, BlockType, objectId, "Iteration count cannot be negative.");
        }
        int iterCount = static_cast<int>(std::floor(iterNumber));

        // DO 스테이트먼트 스크립트 가져오기
        if (block.statementScripts.empty())
        {
            engine.EngineStdOut("repeat_basic block " + block.id + " for " + objectId + " has no DO statement.", 1, executionThreadId);
            return; // 반복할 내용이 없으면 바로 종료
        }

        const Script &doScript = block.statementScripts[0]; // 첫 번째 statementScript가 DO 블록이라고 가정

        // Debugging: Print the content of doScript
        if (!doScript.blocks.empty()) {
            engine.EngineStdOut("DEBUG: doScript for block " + block.id + " (object " + objectId + ") contains " + std::to_string(doScript.blocks.size()) + " inner blocks:", 3, executionThreadId);
            for (size_t j = 0; j < doScript.blocks.size(); ++j) {
                const Block& innerBlock = doScript.blocks[j];
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                // paramsJson이 Null이 아닐 경우에만 Accept 호출
                if (!innerBlock.paramsJson.IsNull()) {
                    innerBlock.paramsJson.Accept(writer);
                }
                engine.EngineStdOut("  Inner Block [" + std::to_string(j) + "]: ID=" + innerBlock.id + ", Type=" + innerBlock.type + ", ParamsJSON=" + (innerBlock.paramsJson.IsNull() ? "null" : buffer.GetString()), 3, executionThreadId);
            }
        } else {
            engine.EngineStdOut("DEBUG: doScript for block " + block.id + " (object " + objectId + ") is empty (no inner blocks).", 3, executionThreadId);
        }
        engine.EngineStdOut("repeat_basic: " + objectId + " starting synchronous loop for " + std::to_string(iterCount) + " iterations. Block ID: " + block.id, 0, executionThreadId);

        // 반복 횟수만큼 내부 블록들을 동기적으로 실행
        for (int i = 0; i < iterCount; ++i)
        {
            executeBlocksSynchronously(engine, objectId, doScript.blocks, executionThreadId);
        }
    }
    else if (BlockType == "repeat_inf")
    {
        // 무한반복 이게 맞는듯
        if (block.statementScripts.empty())
        {
            // DO 블록에 해당하는 스크립트 자체가 없는 경우
            engine.EngineStdOut("DO statement script missing in repeat_inf block. objectId:"+objectId, 1, executionThreadId);
            return;
        }

        const Script &doScript = block.statementScripts[0]; // 첫 번째 statementScript가 DO 블록이라고 가정

        // Debugging: Print the content of doScript
        if (!doScript.blocks.empty()) {
            engine.EngineStdOut("DEBUG: doScript for block " + block.id + " (object " + objectId + ") contains " + std::to_string(doScript.blocks.size()) + " inner blocks:", 3, executionThreadId);
            for (size_t j = 0; j < doScript.blocks.size(); ++j) {
                const Block& innerBlock = doScript.blocks[j];
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                // paramsJson이 Null이 아닐 경우에만 Accept 호출
                if (!innerBlock.paramsJson.IsNull()) {
                    innerBlock.paramsJson.Accept(writer);
                }
                engine.EngineStdOut("  Inner Block [" + std::to_string(j) + "]: ID=" + innerBlock.id + ", Type=" + innerBlock.type + ", ParamsJSON=" + (innerBlock.paramsJson.IsNull() ? "null" : buffer.GetString()), 3, executionThreadId);
            }
        } else {
            engine.EngineStdOut("DEBUG: doScript for block " + block.id + " (object " + objectId + ") is empty (no inner blocks).", 3, executionThreadId);
        }

        if (doScript.blocks.empty())
        {
            engine.EngineStdOut("Warning: repeat_inf for " + objectId + " has an empty DO statement. This will be a very tight loop if not handled carefully.", 1, executionThreadId);
        }

        while (true)
        {
            // 매 반복 시작 시 엔진 종료 또는 씬 변경 확인
            if (engine.m_isShuttingDown.load(std::memory_order_relaxed))
            {
                engine.EngineStdOut("repeat_inf for " + objectId + " cancelled due to engine shutdown.", 1, executionThreadId);
                break; // 루프 종료
            }

            std::string currentEngineSceneId = engine.getCurrentSceneId();
            const ObjectInfo *objInfo = engine.getObjectInfoById(objectId);
            bool isGlobalEntity = (objInfo && (objInfo->sceneId == "global" || objInfo->sceneId.empty()));

            if (!isGlobalEntity && objInfo && objInfo->sceneId != currentEngineSceneId)
            {
                engine.EngineStdOut("repeat_inf for " + objectId + " halted. Entity no longer in current scene " + currentEngineSceneId + ".", 1, executionThreadId);
                break; // 루프 종료
            }

            executeBlocksSynchronously(engine, objectId, doScript.blocks, executionThreadId);
            // executeBlocksSynchronously 내부에서도 종료/씬 변경을 확인하므로,
            // 만약 해당 함수가 return으로 중단되었다면 이 while 루프도 다음 반복에서 위의 break 조건에 걸릴 것입니다.
        }
    }
    else if (BlockType == "repeat_while_true")
    {
        // 될때까지 반복
    }
}

/**
 * @brief 함수블럭
 *
 */
void Function(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block,
              const std::string &executionThreadId)
{
}

/**
 * @brief 이벤트 (기타 제어)
 *
 */
void Event(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block,
           const std::string &executionThreadId)
{
    if (BlockType == "message_cast")
    {
        // params: [message_id_string, null, null]
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1 || !block.paramsJson[0].IsString())
        {
            engine.EngineStdOut(
                "message_cast block for object " + objectId + " has invalid or missing message ID parameter.", 2,
                executionThreadId);
            throw ScriptBlockExecutionError(
                "메시지 ID 파라미터가 유효하지 않습니다.",
                block.id, BlockType, objectId, "Invalid or missing message ID parameter.");
        }

        std::string messageId = block.paramsJson[0].GetString();
        engine.EngineStdOut("DEBUG_MSG: Object " + objectId + " (Thread " + executionThreadId + ") is RAISING message: '" + messageId + "'", 3, executionThreadId);

        if (messageId.empty() || messageId == "null")
        {
            std::string errMsg = "메시지 ID가 비어있거나 null입니다. 메시지를 발송할 수 없습니다.";
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 1)
        {
            engine.EngineStdOut(
                "start_scene block for object " + objectId + " has invalid or missing scene ID parameter.", 2,
                executionThreadId);
            throw ScriptBlockExecutionError(
                "장면 ID 파라미터가 유효하지 않습니다.",
                block.id, BlockType, objectId, "Invalid or missing scene ID parameter.");
        }
        OperandValue sceneIdOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (sceneIdOp.type != OperandValue::Type::STRING || sceneIdOp.asString().empty() || sceneIdOp.asString() == "null")
        {
            std::string errMsg = "장면 ID가 유효한 문자열이 아니거나 비어있거나 null입니다. 장면을 전환할 수 없습니다. 값: " + sceneIdOp.asString();
            engine.EngineStdOut("start_scene block for object " + objectId + ": " + errMsg, 2, executionThreadId);
            throw ScriptBlockExecutionError(errMsg, block.id, BlockType, objectId,
                                            "Scene ID is not a valid string, is empty, or null.");
        }

        std::string sceneId = sceneIdOp.asString();
        engine.EngineStdOut("Object " + objectId + " is requesting to start scene: '" + sceneId + "'", 0,
                            executionThreadId);
        engine.goToScene(sceneId); // goToScene 내부에서 scene 존재 여부 확인 및 when_scene_start 이벤트 트리거
    }
    else if (BlockType == "start_neighbor_scene")
    {
        OperandValue o = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (o.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("start_neighbor_scene block for object ");
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
void executeBlocksSynchronously(Engine &engine, const std::string &objectId, const std::vector<Block> &blocks, const std::string &executionThreadId)
{
    Entity *entity = engine.getEntityById_nolock(objectId); // Assuming entity exists and mutex is handled by caller if needed
    engine.EngineStdOut("Enter executeBlocksSynchronously for " + objectId + ". blocks.size() = " + std::to_string(blocks.size()), 3, executionThreadId);

    for (size_t i = 0; i < blocks.size(); ++i)
    {
        engine.EngineStdOut("executeBlocksSynchronously: Loop iteration " + std::to_string(i) + " for " + objectId, 3, executionThreadId);
        const Block &block = blocks[i];
        // 엔진 종료 신호 확인
        if (engine.m_isShuttingDown.load(std::memory_order_relaxed))
        {
            engine.EngineStdOut("Synchronous block execution cancelled due to engine shutdown for entity: " + objectId, 1, executionThreadId);
            engine.EngineStdOut("executeBlocksSynchronously: Shutting down. Exiting loop for " + objectId, 1, executionThreadId);
            return; // Stop execution
        }
        // 씬 변경 확인 (동기 실행 중 씬 변경 시 중단)
        // 이 헬퍼 함수는 호출 시점의 씬 컨텍스트를 알지 못하므로, 정확한 씬 변경 감지는 상위 실행기에서 해야 합니다.
        // 여기서는 간단히 현재 엔진의 씬 ID와 오브젝트의 씬 ID를 비교합니다.
        // 더 정확한 구현을 위해서는 executeScript 함수에서 씬 ID를 인자로 받아와야 합니다.
        std::string currentEngineSceneId = engine.getCurrentSceneId();
        const ObjectInfo *objInfo = engine.getObjectInfoById(objectId);
        bool isGlobalEntity = false;
        if (objInfo)
        {
            isGlobalEntity = (objInfo->sceneId == "global" || objInfo->sceneId.empty());
        }
        // 오브젝트가 현재 씬에 속하지 않으면 중단 (전역 오브젝트는 제외)
        if (!isGlobalEntity && objInfo && objInfo->sceneId != currentEngineSceneId)
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
            engine.EngineStdOut("   Recursive Block "+block.type,3);
            Moving(block.type, engine, objectId, block, executionThreadId);
            Calculator(block.type, engine, objectId, block, executionThreadId);
            Looks(block.type, engine, objectId, block, executionThreadId);
            Sound(block.type, engine, objectId, block, executionThreadId);
            Variable(block.type, engine, objectId, block, executionThreadId);
            Function(block.type, engine, objectId, block, executionThreadId);
            Event(block.type, engine, objectId, block, executionThreadId);
            Flow(block.type, engine, objectId, block, executionThreadId);
        }
        catch (const ScriptBlockExecutionError &sbee)
        {
            // 내부 블록 실행 중 오류 발생 시 상위로 전파
            throw;
        }
        catch (const std::exception &e)
        {
            // 다른 예외 발생 시 ScriptBlockExecutionError로 래핑하여 전파
            throw ScriptBlockExecutionError(
                "Error during synchronous nested block execution.",
                block.id, block.type, objectId, e.what());
        }
    }
    engine.EngineStdOut("Exit executeBlocksSynchronously for " + objectId + ". Loop " + (blocks.empty() ? "was not entered (empty blocks)." : "completed or exited."), 3, executionThreadId);
}
