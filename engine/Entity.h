#pragma once

#include <string>
#include <vector>

// Forward declaration
class Engine;
class Entity
{
public:
    enum class RotationMethod
    {
        NONE,       // 회전 없음
        FREE,       // 자유 회전
        VERTICAL,   // 상하 방향으로만 (좌우 반전)
        HORIZONTAL, // 좌우 방향으로만 (상하 반전)
     // DIAGONAL, // 엔트리에 해당 옵션이 있는지 확인 필요
     // CIRCLE,   // 엔트리에 해당 옵션이 있는지 확인 필요
        UNKNOWN
    };

    // bounce_wall 블록에서 사용될 충돌 방향 열거형
    enum class CollisionSide {
        NONE,
        UP,
        DOWN,
        LEFT,
        RIGHT
    };

    struct PenState {
        Engine* pEngine = nullptr; 
        bool stop = false; // true이면 그리기가 중지된 상태, false이면 활성화. JS의 stop과 동일.
        bool isPenDown = false;    
        SDL_FPoint lastStagePosition = {0.0f, 0.0f}; 
        SDL_Color color = {0, 0, 0, 255}; 
        // float thickness = 1.0f; // For future

        PenState(Engine* enginePtr);

        void setPenDown(bool down, float currentStageX, float currentStageY);
        void updatePositionAndDraw(float newStageX, float newStageY);
        void setStop(bool shouldStop) { stop = shouldStop; } // isActive 대신 stop 상태를 설정
        bool isStopped() const { return stop; }              // 현재 중지 상태인지 확인
        void setColor(const SDL_Color& c) { color = c; }
        void reset(float currentStageX, float currentStageY); // To set initial position
    };
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
    RotationMethod rotateMethod;
    // enum class CollisionSide { NONE, UP, DOWN, LEFT, RIGHT }; // 중복 선언 제거, 위로 이동
    CollisionSide lastCollisionSide = CollisionSide::NONE;
public: // Made brush and paint public for now for easier access from blocks
    PenState brush;
    PenState paint;

public:
    Entity(Engine* engine, const std::string &entityId, const std::string &entityName,
           double initial_x, double initial_y, double initial_regX, double initial_regY,
           double initial_scaleX, double initial_scaleY, double initial_rotation, double initial_direction,
           double initial_width, double initial_height, bool initial_visible, RotationMethod rotationMethod);

    ~Entity();
    CollisionSide getLastCollisionSide() const;
    void setLastCollisionSide(CollisionSide side);
    bool isPointInside(double pX, double pY) const;
    const std::string &getId() const;
    const std::string &getName() const;
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
    Entity::RotationMethod getRotateMethod() const;
    void setRotateMethod(RotationMethod method);
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
