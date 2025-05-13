#include "Entity.h"
#include <iostream>
#include <string>
#include <cmath>
#include <stdexcept>
#include "Engine.h"

Entity::PenState::PenState(Engine* enginePtr) : pEngine(enginePtr) {
    stop = false; // 기본적으로 그리기가 중지되지 않은 상태 (활성화)
    isPenDown = false; // Default to pen up
    lastStagePosition = {0.0f, 0.0f}; // Initial position
    color = {0, 0, 0, 255}; // Default color (black)
}

void Entity::PenState::setPenDown(bool down, float currentStageX, float currentStageY) {
    isPenDown = down;
    if (isPenDown) {
        lastStagePosition = {currentStageX, currentStageY};
    }
}

void Entity::PenState::updatePositionAndDraw(float newStageX, float newStageY) {
    if (!stop && isPenDown && pEngine) { // 그리기 조건: 중지되지 않았고(!stop) 펜이 내려져 있을 때
        // Target Y for lineTo is inverted from current stage Y, as per JS: sprite.getY() * -1
        SDL_FPoint targetStagePosJSStyle = {newStageX, newStageY * -1.0f};
        pEngine->engineDrawLineOnStage(lastStagePosition, targetStagePosJSStyle, color, 1.0f);
    }
    // Always update last position for the next potential draw segment
    lastStagePosition = {newStageX, newStageY};
}

void Entity::PenState::reset(float currentStageX, float currentStageY) {
    lastStagePosition = {currentStageX, currentStageY};
    // isPenDown and stop 상태는 그대로 유지, reset은 새 선 그리기를 위한 위치 추적만 재설정합니다.
}

Entity::Entity(Engine* engine, const std::string& entityId, const std::string& entityName,
    double initial_x, double initial_y, double initial_regX, double initial_regY,
    double initial_scaleX, double initial_scaleY, double initial_rotation, double initial_direction,
    double initial_width, double initial_height, bool initial_visible, Entity::RotationMethod initial_rotationMethod)
    : id(entityId), name(entityName),
    x(initial_x), y(initial_y), regX(initial_regX), regY(initial_regY),
    scaleX(initial_scaleX), scaleY(initial_scaleY), rotation(initial_rotation), direction(initial_direction),
    width(initial_width), height(initial_height), visible(initial_visible), rotateMethod(initial_rotationMethod),
    brush(engine), paint(engine) // Initialize PenState members
{
}

Entity::~Entity() {
}


const std::string& Entity::getId() const { return id; }
const std::string& Entity::getName() const { return name; }
double Entity::getX() const { return x; }
double Entity::getY() const { return y; }
double Entity::getRegX() const { return regX; }
double Entity::getRegY() const { return regY; }
double Entity::getScaleX() const { return scaleX; }
double Entity::getScaleY() const { return scaleY; }
double Entity::getRotation() const { return rotation; }
double Entity::getDirection() const { return direction; }
double Entity::getWidth() const { return width; }
double Entity::getHeight() const { return height; }
bool Entity::isVisible() const { return visible; }


void Entity::setX(double newX) { x = newX; }
void Entity::setY(double newY) { y = newY; }
void Entity::setRegX(double newRegX) { regX = newRegX; }
void Entity::setRegY(double newRegY) { regY = newRegY; }
void Entity::setScaleX(double newScaleX) { scaleX = newScaleX; }
void Entity::setScaleY(double newScaleY) { scaleY = newScaleY; }
void Entity::setRotation(double newRotation) { rotation = newRotation; }
void Entity::setDirection(double newDirection) { direction = newDirection; }
void Entity::setWidth(double newWidth) { width = newWidth; }
void Entity::setHeight(double newHeight) { height = newHeight; }
void Entity::setVisible(bool newVisible) { visible = newVisible; }
Entity::RotationMethod Entity::getRotateMethod() const {
    return rotateMethod;
}

void Entity::setRotateMethod(RotationMethod method) {
    rotateMethod = method;
}
bool Entity::isPointInside(double pX, double pY) const {
    // pX, pY는 스테이지 좌표 (중앙 (0,0), Y축 위쪽)
    // this->x, this->y는 엔티티의 등록점의 스테이지 좌표

    // 1. 점을 엔티티의 로컬 좌표계로 변환 (등록점을 원점으로)
    double localPX = pX - this->x;
    double localPY = pY - this->y;

    // 2. 엔티티의 회전만큼 점을 반대로 회전
    // SDL 각도는 시계 방향이므로, 점을 객체 프레임으로 가져오려면 반시계 방향(-rotation)으로 회전
    const double PI = std::acos(-1.0);
    double angleRad = -this->rotation * (PI / 180.0); // 도 -> 라디안, 반대 방향

    double rotatedPX = localPX * std::cos(angleRad) - localPY * std::sin(angleRad);
    double rotatedPY = localPX * std::sin(angleRad) + localPY * std::cos(angleRad);

    // 3. 이제 rotatedPX, rotatedPY는 엔티티의 비회전 로컬 좌표계에 있음 (등록점이 원점)
    //    이 좌표계에서 엔티티의 경계 상자를 확인합니다.
    //    regX, regY는 비스케일 이미지의 좌상단으로부터 등록점까지의 오프셋입니다.
    //    로컬 좌표계 (Y 위쪽)에서:
    //    - 좌측 X: -regX * scaleX
    //    - 우측 X: (-regX + width) * scaleX
    //    - 상단 Y: regY * scaleY
    //    - 하단 Y: (regY - height) * scaleY

    double localMinX = -this->regX * this->scaleX;
    double localMaxX = (-this->regX + this->width) * this->scaleX;

    // Y축이 위를 향하므로, regY가 클수록 위쪽, (regY - height)가 아래쪽
    double localMinY = (this->regY - this->height) * this->scaleY;
    double localMaxY = this->regY * this->scaleY;


    if (rotatedPX >= localMinX && rotatedPX <= localMaxX &&
        rotatedPY >= localMinY && rotatedPY <= localMaxY) {
        return true;
    }

    return false;
}
