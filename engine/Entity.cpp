#include "Entity.h"

#include <iostream>
#include <string>
#include <cmath>
#include <stdexcept>

Entity::Entity(const std::string &entityId, const std::string &entityName,
               double initial_x, double initial_y, double initial_regX, double initial_regY,
               double initial_scaleX, double initial_scaleY, double initial_rotation, double initial_direction,
               double initial_width, double initial_height, bool initial_visible)
    : id(entityId), name(entityName),
      x(initial_x), y(initial_y), regX(initial_regX), regY(initial_regY),
      scaleX(initial_scaleX), scaleY(initial_scaleY), rotation(initial_rotation), direction(initial_direction),
      width(initial_width), height(initial_height), visible(initial_visible)
{
}

Entity::~Entity()
{
}

const std::string &Entity::getId() const { return id; }
const std::string &Entity::getName() const { return name; }
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

bool Entity::isPointInside(double pX, double pY) const
{

    double localPX = pX - this->x;
    double localPY = pY - this->y;

    const double PI = std::acos(-1.0);
    double angleRad = -this->rotation * (PI / 180.0);

    double rotatedPX = localPX * std::cos(angleRad) - localPY * std::sin(angleRad);
    double rotatedPY = localPX * std::sin(angleRad) + localPY * std::cos(angleRad);

    double localMinX = -this->regX * this->scaleX;
    double localMaxX = (-this->regX + this->width) * this->scaleX;

    double localMinY = (this->regY - this->height) * this->scaleY;
    double localMaxY = this->regY * this->scaleY;

    if (rotatedPX >= localMinX && rotatedPX <= localMaxX &&
        rotatedPY >= localMinY && rotatedPY <= localMaxY)
    {
        return true;
    }

    return false;
}
