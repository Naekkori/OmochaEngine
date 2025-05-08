#pragma once

#include <string>
#include <vector>

class Engine;

class Entity {
private:
    std::string id;
    std::string name;

    double x;
    double y;
    double regX;
    double regY;
    double scaleX;
    double scaleY;
    double rotation;
    double direction;
    double width;
    double height;
    bool visible;

public:
    Entity(const std::string& entityId, const std::string& entityName,
        double initial_x, double initial_y, double initial_regX, double initial_regY,
        double initial_scaleX, double initial_scaleY, double initial_rotation, double initial_direction,
        double initial_width, double initial_height, bool initial_visible);

    ~Entity();
    bool isPointInside(double pX, double pY) const;
    const std::string& getId() const;
    const std::string& getName() const;
    double getX() const;
    double getY() const;
    double getRegX() const;
    double getRegY() const;
    double getScaleX() const;
    double getScaleY() const;
    double getRotation() const;
    double getDirection() const;
    double getWidth() const;
    double getHeight() const;
    bool isVisible() const;

    void setX(double newX);
    void setY(double newY);
    void setRegX(double newRegX);
    void setRegY(double newRegY);
    void setScaleX(double newScaleX);
    void setScaleY(double newScaleY);
    void setRotation(double newRotation);
    void setDirection(double newDirection);
    void setWidth(double newWidth);
    void setHeight(double newHeight);
    void setVisible(bool newVisible);
};
