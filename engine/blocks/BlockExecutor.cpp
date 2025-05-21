#include "BlockExecutor.h"
#include "util/AEhelper.h"
#include "../Engine.h"
#include "../Entity.h"
#include <string>
#include <vector>
#include <thread>
#include <cmath>
#include <limits>
#include <ctime>
#include "blockTypes.h"
AudioEngineHelper aeHelper; // 전역 AudioEngineHelper 인스턴스

// OperandValue 생성자 및 멤버 함수 구현 (BlockExecutor.h에 선언됨)
OperandValue::OperandValue() : type(Type::EMPTY), boolean_val(false), number_val(0.0) {}
OperandValue::OperandValue(double val) : type(Type::NUMBER), number_val(val), boolean_val(false) {}
OperandValue::OperandValue(const std::string &val) : type(Type::STRING), string_val(val), boolean_val(false), number_val(0.0) {}
OperandValue::OperandValue(bool val) : type(Type::BOOLEAN), boolean_val(val), number_val(0.0) {}
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
        else if (fieldType == "get_pictures")
        {
            // get_pictures 블록의 params[0]은 실제 모양 ID 문자열입니다.
            if (paramField.HasMember("params") && paramField["params"].IsArray() &&
                paramField["params"].Size() > 0 && paramField["params"][0].IsString())
            {
                return OperandValue(paramField["params"][0].GetString());
            }
            engine.EngineStdOut("Invalid 'get_pictures' block structure in parameter field for " + objectId + ". Expected params[0] to be a string (costume ID).", 1);
            return OperandValue(""); // 또는 오류를 나타내는 빈 OperandValue
        }

        engine.EngineStdOut("Unsupported block type in parameter: " + fieldType + " for " + objectId, 1);
        return OperandValue();
    }
    engine.EngineStdOut("Parameter field is not a string or object for " + objectId, 1);
    return OperandValue();
}
/* 여기에 있던 excuteBlock 함수는 Entity.cpp 로 이동*/
/**
 * @brief 움직이기 블럭
 *
 */
void Moving(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
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
            engine.EngineStdOut("move_direction block for object " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue direction = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (distance.type != OperandValue::Type::NUMBER || direction.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_direction block for object " + objectId + " has non-numeric params.", 2);
            return;
        }
        double dist = distance.number_val;
        double dir = direction.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newX = entity->getX() + dist * std::cos(dir * SDL_PI_D / 180.0);
        double newY = entity->getY() - dist * std::sin(dir * SDL_PI_D / 180.0);
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
            if (entityY + (entity->getHeight() * entity->getScaleY() / 2.0) > stageTop && lastCollision != Entity::CollisionSide::UP)
            { // 단순화된 경계
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
            else if (!collidedThisFrame && entityY - (entity->getHeight() * entity->getScaleY() / 2.0) < stageBottom && lastCollision != Entity::CollisionSide::DOWN)
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
            if (entityY - (entity->getHeight() * entity->getScaleY() / 2.0) < stageBottom && lastCollision != Entity::CollisionSide::DOWN)
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
            else if (!collidedThisFrame && entityY + (entity->getHeight() * entity->getScaleY() / 2.0) > stageTop && lastCollision != Entity::CollisionSide::UP)
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
                if (entityX - (entity->getWidth() * entity->getScaleX() / 2.0) < stageLeft && lastCollision != Entity::CollisionSide::LEFT)
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
                if (entityX + (entity->getWidth() * entity->getScaleX() / 2.0) > stageRight && lastCollision != Entity::CollisionSide::RIGHT)
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
                else if (!collidedThisFrame && entityX - (entity->getWidth() * entity->getScaleX() / 2.0) < stageLeft && lastCollision != Entity::CollisionSide::LEFT)
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
        { // 이번 프레임에 어떤 벽과도 충돌하지 않았다면, 이전 충돌 상태를 리셋
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut("move_x block for object " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (distance.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_x block for object " + objectId + " has non-numeric params.", 2);
            return;
        }
        double dist = distance.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newX = entity->getX() + dist;
        entity->setX(newX);
    }
    else if (BlockType == "move_y")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut("move_y block for object " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return;
        }
        OperandValue distance = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (distance.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_y block for object " + objectId + " has non-numeric params.", 2);
            return;
        }
        double dist = distance.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        double newY = entity->getY() + dist;
        entity->setY(newY);
    }
    else if (BlockType == "move_xy_time" || BlockType == "locate_xy_time") // 이둘 똑같이 동작하는걸 확인함 왜 이걸 따로뒀는지 이해안감
    {
        // entity는 함수 시작 시 이미 검증되었습니다.
        // 이전의 if (!entity) 체크는 불필요합니다.

        Entity::TimedMoveState &state = entity->timedMoveState;

        if (!state.isActive)
        { // 블록 처음 실행 시 초기화
            if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 3)
            {
                engine.EngineStdOut("move_xy_time block for " + objectId + " is missing parameters. Expected TIME, X, Y.", 2);
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
                engine.EngineStdOut("move_xy_time block for " + objectId + " has non-number parameters. Time: " + timeOp.asString() + ", X: " + xOp.asString() + ", Y: " + yOp.asString(), 2);
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
            { // totalFrames도 확인
                entity->setX(state.targetX);
                entity->setY(state.targetY);
                entity->paint.updatePositionAndDraw(entity->getX(), entity->getY());
                entity->brush.updatePositionAndDraw(entity->getX(), entity->getY());
                engine.EngineStdOut("move_xy_time: " + objectId + " completed in single step. Pos: (" +
                                        std::to_string(entity->getX()) + ", " + std::to_string(entity->getY()) + ")",
                                    0);
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
                state.isActive = false; // 이동 완료
            }
        }
    }
    else if (BlockType == "locate_x")
    {
        OperandValue valueX = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (valueX.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("locate_x block for object " + objectId + " is not a number.", 2);
            return;
        }
        double x = valueX.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setX(x);
    }
    else if (BlockType == "locate_y")
    {
        OperandValue valueY = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (valueY.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("locate_y block for object " + objectId + " is not a number.", 2);
            return;
        }
        // entity는 함수 시작 시 이미 검증되었습니다.
        double y = valueY.number_val;
        entity->setY(y);
    }
    else if (BlockType == "locate_xy")
    {
        OperandValue valueX = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue valueY = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (valueX.type != OperandValue::Type::NUMBER || valueY.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("locate_xy block for object " + objectId + " is not a number.", 2);
            return;
        }
        double x = valueX.number_val;
        double y = valueY.number_val;
        // entity는 함수 시작 시 이미 검증되었습니다.
        entity->setX(x);
        entity->setY(y);
    }
    else if (BlockType == "locate")
    {
        // 이것은 마우스커서 나 오브젝트를 따라갑니다.
        OperandValue target = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (target.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("locate block for object " + objectId + " is not a string.", 2);
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
                engine.EngineStdOut("locate block for object " + objectId + ": target entity '" + target.string_val + "' not found.", 2);
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
        { // 블록 처음 실행 시 초기화
            if (!block.paramsJson.IsArray() || block.paramsJson.Size() < 2)
            { // time, target 필요
                engine.EngineStdOut("locate_object_time block for " + objectId + " is missing parameters. Expected TIME, TARGET_OBJECT_ID.", 2);
                // state.isActive는 false로 유지됩니다.
                return;
            }

            OperandValue timeOp = getOperandValue(engine, objectId, block.paramsJson[0]);
            OperandValue targetOp = getOperandValue(engine, objectId, block.paramsJson[1]);

            if (timeOp.type != OperandValue::Type::NUMBER || targetOp.type != OperandValue::Type::STRING)
            {
                engine.EngineStdOut("locate_object_time block for " + objectId + " has invalid parameters. Time should be number, target should be string.", 2);
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
                    engine.EngineStdOut("locate_object_time: target object " + state.targetObjectId + " not found for " + objectId + ".", 2);
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
                engine.EngineStdOut("locate_object_time: target object " + state.targetObjectId + " disappeared mid-move for " + objectId + ".", 2);
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
            engine.EngineStdOut("rotate_relative block for object " + objectId + " is not a number.", 2);
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
            engine.EngineStdOut("rotate_by_time block for object " + objectId + " has non-number parameters.", 2);
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
            engine.EngineStdOut("rotate_absolute block for object " + objectId + " is not a number.", 2);
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
            engine.EngineStdOut("direction_absolute block for object " + objectId + "is not a number.", 2);
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
            engine.EngineStdOut("see_angle_object block for object " + objectId + "is not a string.", 2);
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
                entity->setRotation(engine.getAngle(entity->getX(), entity->getY(), targetEntity->getX(), targetEntity->getY()));
            }
            else
            {
                engine.EngineStdOut("see_angle_object block for object " + objectId + ": target entity '" + hasmouse.string_val + "' not found.", 2);
            }
        }
    }
    else if (BlockType == "move_to_angle")
    {
        OperandValue setAngle = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue setDesnitance = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (setAngle.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("move_to_angle block for object " + objectId + "is not a number.", 2);
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
OperandValue Calculator(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
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
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() <= 1)
        { // 인덱스 1은 크기가 최소 2여야 함
            engine.EngineStdOut("coordinate_mouse block for " + objectId + " has invalid params structure. Expected param at index 1 for VALUE.", 2);
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
            engine.EngineStdOut("coordinate_mouse block for " + objectId + ": VALUE parameter (index 1) is not a string.", 2);
            return OperandValue(0.0);
        }

        if (coord_type_str.empty())
        { // getOperandValue가 빈 문자열을 반환했거나, 원래 문자열이 비어있는 경우
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
                engine.EngineStdOut("coordinate_object - Selected costume ID '" + selectedCostumeId + "' not found in costume list for entity '" + targetEntity->getId() + "'.", 1);
                if (coordinateTypeStr == "picture_index")
                    return OperandValue(0.0); // Not found, return 0 for index
                else
                    return OperandValue(""); // Not found, return empty for name
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
            throw "알수없는 파라미터 크기";
        }

        OperandValue left_op = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue operator_op = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue right_op = getOperandValue(engine, objectId, block.paramsJson[2]);

        if (operator_op.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("quotient_and_mod block for " + objectId + " has non-string operator parameter.", 2);
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
                engine.EngineStdOut("Division by zero in quotient_and_mod (QUOTIENT) for " + objectId + ". Returning NaN.", 2);
                // throw "0으로 나누기 (몫) (은)는 불가능합니다.";
                return OperandValue(std::nan("")); // NaN 반환
            }
            return OperandValue(std::floor(left_val / right_val));
        }
        else
        {
            if (right_val == 0.0)
            {
                engine.EngineStdOut("Division by zero in quotient_and_mod (MOD) for " + objectId + ". Returning NaN.", 2);
                // throw "0으로 나누기 (나머지) (은)는 불가능합니다.";
                return OperandValue(std::nan("")); // NaN 반환
            }
            return OperandValue(left_val - right_val * std::floor(left_val / right_val));
        }
    }
    else if (BlockType == "calc_operation")
    { // EntryJS: get_value_of_operator
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
            engine.EngineStdOut("calc_operation block for " + objectId + " has invalid params. Expected 2 params (LEFTHAND, OPERATOR).", 2);
            return OperandValue(std::nan(""));
        }

        OperandValue leftOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue opVal = getOperandValue(engine, objectId, block.paramsJson[1]);

        if (leftOp.type != OperandValue::Type::NUMBER)
        {
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
        if (underscore_pos != 0)
        { // 찾지 못했거나(npos) 0보다 큰 인덱스에서 찾았을 경우 참
            if (underscore_pos != std::string::npos)
            { // 찾았고, 0번 위치가 아닐 경우
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
            engine.EngineStdOut("calc_operation block for " + objectId + ": " + errorMsg, 2);
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
            engine.EngineStdOut("get_date block for " + objectId + " has invalid or missing action parameter.", 2);
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
            engine.EngineStdOut("get_date block for " + objectId + " has unknown action: " + action, 1);
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
                engine.EngineStdOut("distance_something block for " + objectId + ": mouse is not on stage.", 1);
                return OperandValue(0.0);
            }
        }
        else
        {
            Entity *targetEntity = engine.getEntityById(targetId);
            if (!targetEntity)
            {
                engine.EngineStdOut("distance_something block for " + objectId + ": target entity '" + targetId + "' not found.", 2);
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
            engine.EngineStdOut("length_of_string block for " + objectId + " has invalid params structure. Expected 1 param.", 2);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (strOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("length_of_string block for " + objectId + " has non-string parameter.", 2);
            return OperandValue();
        }
        return OperandValue(static_cast<double>(strOp.string_val.length()));
    }
    else if (BlockType == "reverse_of_string")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1)
        {
            engine.EngineStdOut("reverse_of_string block for " + objectId + " has invalid params structure. Expected 1 param.", 2);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (strOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("reverse_of_string block for " + objectId + " has non-string parameter.", 2);
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
            engine.EngineStdOut("combie_something block for " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return OperandValue();
        }
        OperandValue strOp1 = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue strOp2 = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp1.type != OperandValue::Type::STRING || strOp2.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("combie_something block for " + objectId + " has non-string parameter.", 2);
            return OperandValue();
        }
        std::string combinedStr = strOp1.string_val + strOp2.string_val;
        return OperandValue(combinedStr);
    }
    else if (BlockType == "char_at")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut("char_at block for " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue indexOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp.type != OperandValue::Type::STRING || indexOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("char_at block for " + objectId + " has non-string or non-number parameter.", 2);
            return OperandValue();
        }
        int index = static_cast<int>(indexOp.number_val);
        if (index < 0 || index >= static_cast<int>(strOp.string_val.length()))
        {
            engine.EngineStdOut("char_at block for " + objectId + " has index out of range.", 2);
            return OperandValue();
        }
        return OperandValue(std::string(1, strOp.string_val[index]));
    }
    else if (BlockType == "substring")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut("substring block for " + objectId + " has invalid params structure. Expected 3 params.", 2);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue startOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue endOp = getOperandValue(engine, objectId, block.paramsJson[2]);
        if (strOp.type != OperandValue::Type::STRING || startOp.type != OperandValue::Type::NUMBER || endOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("substring block for " + objectId + " has non-string or non-number parameter.", 2);
            return OperandValue();
        }
        int startIndex = static_cast<int>(startOp.number_val);
        int endIndex = static_cast<int>(endOp.number_val);
        if (startIndex < 0 || endIndex > static_cast<int>(strOp.string_val.length()) || startIndex > endIndex)
        {
            engine.EngineStdOut("substring block for " + objectId + " has index out of range.", 2);
            return OperandValue();
        }
        return OperandValue(strOp.string_val.substr(startIndex, endIndex - startIndex));
    }
    else if (BlockType == "count_match_string")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 2)
        {
            engine.EngineStdOut("count_match_string block for " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue subStrOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp.type != OperandValue::Type::STRING || subStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("count_match_string block for " + objectId + " has non-string parameter.", 2);
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
            engine.EngineStdOut("index_of_string block for " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue subStrOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp.type != OperandValue::Type::STRING || subStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("index_of_string block for " + objectId + " has non-string parameter.", 2);
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
            engine.EngineStdOut("replace_string block for " + objectId + " has invalid params structure. Expected 3 params.", 2);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue oldStrOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue newStrOp = getOperandValue(engine, objectId, block.paramsJson[2]);
        if (strOp.type != OperandValue::Type::STRING || oldStrOp.type != OperandValue::Type::STRING || newStrOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("replace_string block for " + objectId + " has non-string parameter.", 2);
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
            engine.EngineStdOut("change_string_case block for " + objectId + " has invalid params structure. Expected 2 params.", 2);
            return OperandValue();
        }
        OperandValue strOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue caseOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        if (strOp.type != OperandValue::Type::STRING || caseOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_string_case block for " + objectId + " has non-string parameter.", 2);
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
            engine.EngineStdOut("change_string_case block for " + objectId + " has unknown case type: " + caseType, 2);
            return OperandValue();
        }
        return OperandValue(str);
    }
    else if (BlockType == "get_block_count")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1)
        {
            engine.EngineStdOut("get_block_count block for " + objectId + " has invalid params structure. Expected 1 param (OBJECT).", 2);
            return OperandValue(0.0);
        }
        OperandValue objectKeyOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (objectKeyOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("get_block_count block for " + objectId + " OBJECT parameter did not resolve to a string.", 2);
            return OperandValue(0.0);
        }
        std::string objectKey = objectKeyOp.string_val;

        if (objectKey.empty())
        {
            engine.EngineStdOut("get_block_count block for " + objectId + " received an empty OBJECT key.", 1);
            return OperandValue(0.0);
        }

        int count = 0;
        if (objectKey.rfind("scene-", 0) == 0)
        { // Starts with "scene-"
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
        { // Starts with "object-"
            std::string targetObjId = objectKey.substr(7);
            count = engine.getBlockCountForObject(targetObjId);
        }
        else
        {
            engine.EngineStdOut("get_block_count block for " + objectId + " has unknown OBJECT key: " + objectKey, 1);
            return OperandValue(0.0); // Default to 0 for unknown keys
        }
        return OperandValue(static_cast<double>(count));
    }
    else if (BlockType == "change_rgb_to_hex")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 3)
        {
            engine.EngineStdOut("change_rgb_to_hex block for " + objectId + " has invalid params structure. Expected 3 params.", 2);
            return OperandValue();
        }
        OperandValue redOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue greenOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue blueOp = getOperandValue(engine, objectId, block.paramsJson[2]);
        if (redOp.type != OperandValue::Type::NUMBER || greenOp.type != OperandValue::Type::NUMBER || blueOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("change_rgb_to_hex block for " + objectId + " has non-number parameter.", 2);
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
            engine.EngineStdOut("change_hex_to_rgb block for " + objectId + " has invalid params structure. Expected 1 param.", 2);
            return OperandValue();
        }
        OperandValue hexOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (hexOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_hex_to_rgb block for " + objectId + " has non-string parameter.", 2);
            return OperandValue();
        }
        std::string hexStr = hexOp.string_val;
        if (hexStr.length() != 7 || hexStr[0] != '#')
        {
            engine.EngineStdOut("change_hex_to_rgb block for " + objectId + " has invalid hex string: " + hexStr, 2);
            return OperandValue();
        }
        int red = std::stoi(hexStr.substr(1, 2), nullptr, 16);
        int green = std::stoi(hexStr.substr(3, 2), nullptr, 16);
        int blue = std::stoi(hexStr.substr(5, 2), nullptr, 16);
        return OperandValue(static_cast<double>(red) + static_cast<double>(green) / 256.0 + static_cast<double>(blue) / (256.0 * 256.0));
    }
    else if (BlockType == "get_boolean_value")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() != 1)
        {
            engine.EngineStdOut("get_boolean_value block for " + objectId + " has invalid params structure. Expected 1 param.", 2);
            return OperandValue();
        }
        OperandValue boolOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (boolOp.type != OperandValue::Type::BOOLEAN)
        {
            engine.EngineStdOut("get_boolean_value block for " + objectId + " has non-boolean parameter.", 2);
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
            engine.EngineStdOut("choose_project_timer_action block for " + objectId + " has invalid or missing action parameter.", 2);
            return OperandValue();
        }

        // 이 블록의 파라미터는 항상 단순 문자열 드롭다운 값이므로 직접 접근합니다.
        // getOperandValue를 사용할 수도 있지만, 여기서는 직접 사용합니다.
        std::string action = block.paramsJson[0].GetString();

        if (action == "START")
        {
            engine.EngineStdOut("Project timer STARTED by object " + objectId, 0);
            engine.startProjectTimer();
        }
        else if (action == "STOP")
        {
            engine.EngineStdOut("Project timer STOPPED by object " + objectId, 0);
            engine.stopProjectTimer();
        }
        else if (action == "RESET")
        {
            engine.EngineStdOut("Project timer RESET by object " + objectId, 0);
            engine.resetProjectTimer(); // resetProjectTimer는 값만 0으로 설정합니다.
        }
        else
        {
            engine.EngineStdOut("choose_project_timer_action block for " + objectId + " has unknown action: " + action, 1);
        }
        return OperandValue();
    }
    else if (BlockType == "set_visible_project_timer")
    {
        if (!block.paramsJson.IsArray() || block.paramsJson.Size() == 0)
        {
            engine.EngineStdOut("set_visible_project_timer block for " + objectId + " has missing or invalid params array. Defaulting to HIDE.", 2);
            engine.showProjectTimer(false); // Default action
            return OperandValue();
        }
        OperandValue actionValue = getOperandValue(engine, objectId, block.paramsJson[0]);

        if (actionValue.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("set_visible_project_timer parameter for object " + objectId + " did not resolve to a string. Interpreted as: '" + actionValue.string_val + "'", 1);
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
            engine.EngineStdOut("set_visible_project_timer block for " + objectId + " has unknown or non-string action value: '" + actionValue.string_val + "'. Defaulting to HIDE.", 1);
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
                engine.EngineStdOut("Division by zero", 2);
                return OperandValue(std::nan(""));
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
 * @brief 모양새
 *
 */
void Looks(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
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
        { // 인디케이터 포함하면 4개일 수 있음
            engine.EngineStdOut("dialog_time block for " + objectId + " has insufficient parameters. Expected message, time, option.", 2);
            return;
        }
        OperandValue messageOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue timeOp = getOperandValue(engine, objectId, block.paramsJson[1]);
        OperandValue optionOp = getOperandValue(engine, objectId, block.paramsJson[2]); // Dropdown value

        if (timeOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("dialog_time block for " + objectId + ": SECOND parameter is not a number. Value: " + timeOp.asString(), 2);
            return;
        }
        if (optionOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("dialog_time block for " + objectId + ": OPTION parameter is not a string. Value: " + optionOp.asString(), 2);
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
        { // 인디케이터 포함하면 3개일 수 있음
            engine.EngineStdOut("dialog block for " + objectId + " has insufficient parameters. Expected message, option.", 2);
            return;
        }
        OperandValue messageOp = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue optionOp = getOperandValue(engine, objectId, block.paramsJson[1]); // Dropdown value

        if (optionOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("dialog block for " + objectId + ": OPTION parameter is not a string. Value: " + optionOp.asString(), 2);
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
        OperandValue imageDropdown = getOperandValue(engine, objectId, block.paramsJson[0]);
        // getOperandValue는 get_pictures 블록의 params[0] (모양 ID 문자열)을 반환해야 합니다.
        // getOperandValue 내부에서 get_pictures 타입 처리가 필요합니다.
        if (imageDropdown.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_to_some_shape block for " + objectId + " parameter did not resolve to a string (expected costume ID). Actual type: " + std::to_string(static_cast<int>(imageDropdown.type)), 2);
            return;
        }
        std::string costumeIdToSet = imageDropdown.asString();
        if (costumeIdToSet.empty())
        {
            engine.EngineStdOut("change_to_some_shape block for " + objectId + " received an empty costume ID.", 2);
            return;
        }

        // Engine에서 ObjectInfo를 가져와서 selectedCostumeId를 업데이트합니다.
        if (!engine.setEntitySelectedCostume(objectId, costumeIdToSet))
        {
            engine.EngineStdOut("change_to_some_shape block for " + objectId + ": Failed to set costume to ID '" + costumeIdToSet + "'. It might not exist for this object.", 1);
        }
        else
        {
            engine.EngineStdOut("Entity " + objectId + " changed shape to: " + costumeIdToSet, 0);
        }
    }
    else if (BlockType == "change_to_next_shape")
    {
        OperandValue nextorprev = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (nextorprev.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_to_some_shape block for " + objectId + " parameter did not resolve to a string (expected costume ID). Actual type: " + std::to_string(static_cast<int>(nextorprev.type)), 2);
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
        { // 인디케이터 포함 시 3개일 수 있음
            engine.EngineStdOut("add_effect_amount block for " + objectId + " has insufficient parameters. Expected EFFECT, VALUE.", 2);
            return;
        }
        OperandValue effectTypeOp = getOperandValue(engine, objectId, block.paramsJson[0]);  // EFFECT dropdown
        OperandValue effectValueOp = getOperandValue(engine, objectId, block.paramsJson[1]); // VALUE number

        if (effectTypeOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("add_effect_amount block for " + objectId + ": EFFECT parameter is not a string. Value: " + effectTypeOp.asString(), 2);
            return;
        }
        if (effectValueOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("add_effect_amount block for " + objectId + ": VALUE parameter is not a number. Value: " + effectValueOp.asString(), 2);
            return;
        }

        std::string effectName = effectTypeOp.asString();
        double value = effectValueOp.asNumber();

        if (effectName == "color")
        { // JavaScript의 'hsv'에 해당, 여기서는 색조(hue)로 처리
            entity->setEffectHue(entity->getEffectHue() + value);
            engine.EngineStdOut("Entity " + objectId + " effect 'color' (hue) changed by " + std::to_string(value) + ", new value: " + std::to_string(entity->getEffectHue()), 0);
        }
        else if (effectName == "brightness")
        {
            entity->setEffectBrightness(entity->getEffectBrightness() + value);
            engine.EngineStdOut("Entity " + objectId + " effect 'brightness' changed by " + std::to_string(value) + ", new value: " + std::to_string(entity->getEffectBrightness()), 0);
        }
        else if (effectName == "transparency")
        {
            // JavaScript: sprite.effect.alpha = sprite.effect.alpha - effectValue / 100;
            // 여기서 effectValue는 0-100 범위의 값으로, 투명도를 증가시킵니다 (알파 값을 감소시킴).
            // Entity의 m_effectAlpha는 0.0(투명) ~ 1.0(불투명) 범위입니다.
            entity->setEffectAlpha(entity->getEffectAlpha() - (value / 100.0));
            engine.EngineStdOut("Entity " + objectId + " effect 'transparency' (alpha) changed by " + std::to_string(value) + "%, new value: " + std::to_string(entity->getEffectAlpha()), 0);
        }
        else
        {
            engine.EngineStdOut("add_effect_amount block for " + objectId + ": Unknown effect type '" + effectName + "'.", 1);
        }
    }
    else if (BlockType == "change_effect_amount")
    {
        if (!block.paramsJson.Empty() || block.paramsJson.Size() < 2)
        {
            engine.EngineStdOut("change_effect_amount block for " + objectId + " has insufficient parameters. Expected EFFECT, VALUE.", 2);
            return;
        }
        OperandValue effectTypeOp = getOperandValue(engine, objectId, block.paramsJson[0]);  // EFFECT dropdown
        OperandValue effectValueOp = getOperandValue(engine, objectId, block.paramsJson[1]); // VALUE number

        if (effectTypeOp.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("add_effect_amount block for " + objectId + ": EFFECT parameter is not a string. Value: " + effectTypeOp.asString(), 2);
            return;
        }
        if (effectValueOp.type != OperandValue::Type::NUMBER)
        {
            engine.EngineStdOut("add_effect_amount block for " + objectId + ": VALUE parameter is not a number. Value: " + effectValueOp.asString(), 2);
            return;
        }

        std::string effectName = effectTypeOp.asString();
        double value = effectValueOp.asNumber();
        if (effectName == "color")
        {
            entity->setEffectHue(value);
            engine.EngineStdOut("Entity " + objectId + " effect 'color' (hue) changed to " + std::to_string(value), 0);
        }
        else if (effectName == "brightness")
        {
            entity->setEffectBrightness(value);
            engine.EngineStdOut("Entity " + objectId + " effect 'brightness' changed to " + std::to_string(value), 0);
        }
        else if (effectName == "transparency")
        {
            entity->setEffectAlpha(1 - (value / 100.0));
            engine.EngineStdOut("Entity " + objectId + " effect 'transparency' (alpha) changed to " + std::to_string(value), 0);
        }
    }
    else if (BlockType == "erase_all_effects")
    {
        entity->setEffectBrightness(0.0); // 밝기 효과 초기화 (0.0이 기본값)
        entity->setEffectAlpha(1.0);      // 투명도 효과 초기화 (1.0이 기본값, 완전 불투명)
        entity->setEffectHue(0.0);        // 색깔 효과 (색조) 초기화 (0.0이 기본값)
        engine.EngineStdOut("Entity " + objectId + " all graphic effects erased.", 0);
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
    }else if (BlockType == "stretch_scale_size"){
        OperandValue setWidth = getOperandValue(engine, objectId, block.paramsJson[0]);
        OperandValue setHeight = getOperandValue(engine, objectId, block.paramsJson[1]);
        entity->setWidth(setWidth.asNumber());
        entity->setHeight(setHeight.asNumber());
    }else if (BlockType == "reset_scale_size"){
        entity->resetScaleSize();
    }else if (BlockType == "flip_x"){
        entity->setScaleX(-1 * entity->getScaleX());
    }else if (BlockType == "flip_y"){
        entity->setScaleY(-1 * entity->getScaleY());
    }else if (BlockType == "change_object_index"){
        //이 엔진은 역순으로 스프라이트를 렌더링 하고있음
        OperandValue zindexEnumDropdown = getOperandValue(engine, objectId, block.paramsJson[0]);
        if (zindexEnumDropdown.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("change_object_index object is not String", 2);
            return;
        }
        std::string zindexEnumStr = zindexEnumDropdown.asString();
        Omocha::ObjectIndexChangeType changeType = Omocha::stringToObjectIndexChangeType(zindexEnumStr);
        engine.changeObjectIndex(objectId, changeType);
    }else if (BlockType == "sound_something_with_block")
    {
        OperandValue soundType = getOperandValue(engine, objectId, block.paramsJson[0]);

        if (soundType.type != OperandValue::Type::STRING)
        {
            engine.EngineStdOut("sound_something_with_block for object " + objectId + ": sound ID parameter is not a string. Value: " + soundType.asString(), 2);
            return;
        }
        std::string soundIdToPlay = soundType.asString();

        if (soundIdToPlay.empty()) {
            engine.EngineStdOut("sound_something_with_block for object " + objectId + ": received an empty sound ID.", 2);
            return;
        }

        entity->playSound(soundIdToPlay);
    }
    
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
 * @brief 흐름 
 * 
 */
void Flow(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block){
}
/**
 * @brief 함수블럭
 *
 */
void Function(std::string BlockType, Engine &engine, const std::string &objectId, const Block &block)
{
}