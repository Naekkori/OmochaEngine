#pragma once

#include <string>
#include <variant> // C++17, for holding different command types if preferred
#include "SDL3/SDL_pixels.h"
#include "SDL3/SDL_rect.h"

// 기본 커맨드 타입 열거형
enum class CommandType {
    SET_POSITION,
    SET_VISIBILITY,
    SHOW_DIALOG,
    REMOVE_DIALOG,
    PEN_DRAW_LINE,
    PEN_SET_STATE, // For penDown, penUp, setColor etc.
    CHANGE_COSTUME,
    // ... 기타 필요한 커맨드 타입들
};

// 모든 커맨드의 기본이 될 수 있는 구조체 (선택 사항, 또는 std::variant 사용)
struct BaseCommand {
    CommandType type;
    std::string entityId; // 대부분의 커맨드는 대상 엔티티 ID를 가짐

    BaseCommand(CommandType t, std::string id) : type(t), entityId(std::move(id)) {}
    virtual ~BaseCommand() = default; // 다형적 삭제를 위해 가상 소멸자
};

struct SetPositionCommand : public BaseCommand {
    double x, y;
    SetPositionCommand(std::string id, double newX, double newY)
        : BaseCommand(CommandType::SET_POSITION, std::move(id)), x(newX), y(newY) {}
};

struct SetVisibilityCommand : public BaseCommand {
    bool visible;
    SetVisibilityCommand(std::string id, bool v)
        : BaseCommand(CommandType::SET_VISIBILITY, std::move(id)), visible(v) {}
};

struct ShowDialogCommand : public BaseCommand {
    std::string message;
    std::string dialogType; // "speak" or "think"
    Uint64 durationMs;
    ShowDialogCommand(std::string id, std::string msg, std::string dtype, Uint64 dur)
        : BaseCommand(CommandType::SHOW_DIALOG, std::move(id)), message(std::move(msg)), dialogType(std::move(dtype)), durationMs(dur) {}
};

struct RemoveDialogCommand : public BaseCommand {
    RemoveDialogCommand(std::string id)
        : BaseCommand(CommandType::REMOVE_DIALOG, std::move(id)) {}
};

struct PenDrawLineCommand : public BaseCommand {
    // Entity ID 대신 Pen ID를 사용할 수도 있음 (brush, paint 구분)
    SDL_FPoint p1_stage_entry;
    SDL_FPoint p2_stage_entry_modified_y;
    SDL_Color color;
    float thickness;
    // 생성자에서 entityId를 penOwnerId 등으로 명명하는 것이 더 명확할 수 있음
    PenDrawLineCommand(std::string ownerId, SDL_FPoint pt1, SDL_FPoint pt2, SDL_Color c, float thick)
        : BaseCommand(CommandType::PEN_DRAW_LINE, std::move(ownerId)), p1_stage_entry(pt1), p2_stage_entry_modified_y(pt2), color(c), thickness(thick) {}
};

struct ChangeCostumeCommand : public BaseCommand {
    std::string costumeId;
    bool next; // true면 다음, false면 이전 (또는 특정 ID)
    bool specificId; // costumeId가 특정 ID인지, 아니면 next/prev인지 구분

    ChangeCostumeCommand(std::string eid, std::string cid, bool specId = true, bool nxt = false)
        : BaseCommand(CommandType::CHANGE_COSTUME, std::move(eid)), costumeId(std::move(cid)), next(nxt), specificId(specId) {}
};

// 앞으로 더 많은 커맨드 구조체들이 추가될 것입니다.
// 예: SetRotationCommand, SetScaleCommand, PlaySoundCommand 등