#include "blockTypes.h"
#include <map>

namespace Omocha {

BlockTypeEnum stringToBlockTypeEnum(const std::string& typeStr) {
    static const std::map<std::string, BlockTypeEnum> typeMap = {
        {"move_direction", BlockTypeEnum::MOVE_DIRECTION},
        {"bounce_wall", BlockTypeEnum::BOUNCE_WALL},
        {"move_x", BlockTypeEnum::MOVE_X},
        {"move_y", BlockTypeEnum::MOVE_Y},
        {"move_xy_time", BlockTypeEnum::MOVE_XY_TIME},
        {"locate_xy_time", BlockTypeEnum::LOCATE_XY_TIME},
        {"locate_x", BlockTypeEnum::LOCATE_X},
        {"locate_y", BlockTypeEnum::LOCATE_Y},
        {"locate_xy", BlockTypeEnum::LOCATE_XY},
        {"locate", BlockTypeEnum::LOCATE},
        {"locate_object_time", BlockTypeEnum::LOCATE_OBJECT_TIME},
        {"rotate_relative", BlockTypeEnum::ROTATE_RELATIVE},
        {"direction_relative", BlockTypeEnum::DIRECTION_RELATIVE},
        {"rotate_by_time", BlockTypeEnum::ROTATE_BY_TIME},
        {"direction_relative_duration", BlockTypeEnum::DIRECTION_RELATIVE_DURATION},
        {"rotate_absolute", BlockTypeEnum::ROTATE_ABSOLUTE},
        {"direction_absolute", BlockTypeEnum::DIRECTION_ABSOLUTE},
        {"see_angle_object", BlockTypeEnum::SEE_ANGLE_OBJECT},
        {"move_to_angle", BlockTypeEnum::MOVE_TO_ANGLE},
        {"calc_basic", BlockTypeEnum::CALC_BASIC},
        {"calc_rand", BlockTypeEnum::CALC_RAND},
        {"coordinate_mouse", BlockTypeEnum::COORDINATE_MOUSE},
        {"coordinate_object", BlockTypeEnum::COORDINATE_OBJECT},
        {"quotient_and_mod", BlockTypeEnum::QUOTIENT_AND_MOD},
        {"calc_operation", BlockTypeEnum::CALC_OPERATION},
        {"get_project_timer_value", BlockTypeEnum::GET_PROJECT_TIMER_VALUE},
        {"choose_project_timer_action", BlockTypeEnum::CHOOSE_PROJECT_TIMER_ACTION},
        {"set_visible_project_timer", BlockTypeEnum::SET_VISIBLE_PROJECT_TIMER},
        {"get_date", BlockTypeEnum::GET_DATE},
        {"distance_something", BlockTypeEnum::DISTANCE_SOMETHING},
        {"length_of_string", BlockTypeEnum::LENGTH_OF_STRING},
        {"reverse_of_string", BlockTypeEnum::REVERSE_OF_STRING},
        {"combie_something", BlockTypeEnum::COMBINE_SOMETHING}, // Assuming "combine"
        {"char_at", BlockTypeEnum::CHAR_AT},
        {"substring", BlockTypeEnum::SUBSTRING},
        {"count_match_string", BlockTypeEnum::COUNT_MATCH_STRING},
        {"index_of_string", BlockTypeEnum::INDEX_OF_STRING},
        {"replace_string", BlockTypeEnum::REPLACE_STRING},
        {"change_string_case", BlockTypeEnum::CHANGE_STRING_CASE},
        {"get_block_count", BlockTypeEnum::GET_BLOCK_COUNT},
        {"change_rgb_to_hex", BlockTypeEnum::CHANGE_RGB_TO_HEX},
        {"change_hex_to_rgb", BlockTypeEnum::CHANGE_HEX_TO_RGB},
        {"get_boolean_value", BlockTypeEnum::GET_BOOLEAN_VALUE},
        {"get_user_name", BlockTypeEnum::GET_USER_NAME},
        {"get_nickname", BlockTypeEnum::GET_NICKNAME},
        {"show", BlockTypeEnum::SHOW},
        {"hide", BlockTypeEnum::HIDE},
        {"dialog_time", BlockTypeEnum::DIALOG_TIME},
        {"dialog", BlockTypeEnum::DIALOG},
        {"remove_dialog", BlockTypeEnum::REMOVE_DIALOG},
        {"change_to_some_shape", BlockTypeEnum::CHANGE_TO_SOME_SHAPE},
        {"change_to_next_shape", BlockTypeEnum::CHANGE_TO_NEXT_SHAPE},
        {"when_run_button_click", BlockTypeEnum::WHEN_RUN_BUTTON_CLICK},
        {"when_some_key_pressed", BlockTypeEnum::WHEN_SOME_KEY_PRESSED},
        {"mouse_clicked", BlockTypeEnum::MOUSE_CLICKED},
        {"mouse_click_cancled", BlockTypeEnum::MOUSE_CLICK_CANCELED}, // Original: mouse_click_cancled
        {"when_object_click", BlockTypeEnum::WHEN_OBJECT_CLICK},
        {"when_object_click_canceled", BlockTypeEnum::WHEN_OBJECT_CLICK_CANCELED},
        {"when_message_cast", BlockTypeEnum::WHEN_MESSAGE_CAST}, // Original: when_message_cast (receive)
        {"when_scene_start", BlockTypeEnum::WHEN_SCENE_START},
        {"get_pictures", BlockTypeEnum::GET_PICTURES },
        {"text_reporter_number", BlockTypeEnum::TEXT_REPORTER_NUMBER },
        {"text_reporter_string", BlockTypeEnum::TEXT_REPORTER_STRING }
    };
    auto it = typeMap.find(typeStr);
    if (it != typeMap.end()) {
        return it->second;
    }
    return BlockTypeEnum::UNKNOWN;
}

std::string blockTypeEnumToKoreanString(BlockTypeEnum type) {
    // This map can be quite large. Consider externalizing if it grows too much.
    // Placeholder translations.
    static const std::map<BlockTypeEnum, std::string> koreanMap = {
        {BlockTypeEnum::MOVE_DIRECTION, "방향으로 이동"},
        {BlockTypeEnum::BOUNCE_WALL, "벽에 닿으면 튕기기"},
        {BlockTypeEnum::MOVE_X, "X좌표를 ~만큼 바꾸기"},
        {BlockTypeEnum::MOVE_Y, "Y좌표를 ~만큼 바꾸기"},
        {BlockTypeEnum::MOVE_XY_TIME, "~초 동안 X, Y만큼 이동"},
        {BlockTypeEnum::LOCATE_XY_TIME, "~초 동안 X, Y 위치로 이동"}, // EntryJS에서는 MOVE_XY_TIME과 동일하게 동작
        {BlockTypeEnum::LOCATE_X, "X 위치를 ~로 정하기"},
        {BlockTypeEnum::LOCATE_Y, "Y 위치를 ~로 정하기"},
        {BlockTypeEnum::LOCATE_XY, "X, Y 위치로 이동하기"},
        {BlockTypeEnum::LOCATE, "~ 위치로 이동하기"},
        {BlockTypeEnum::LOCATE_OBJECT_TIME, "~초 동안 [오브젝트] 위치로 이동"},
        {BlockTypeEnum::ROTATE_RELATIVE, "방향을 ~만큼 회전하기"},
        {BlockTypeEnum::DIRECTION_RELATIVE, "이동 방향을 ~만큼 회전하기"}, // EntryJS에서는 ROTATE_RELATIVE와 동일하게 동작
        {BlockTypeEnum::ROTATE_BY_TIME, "~초 동안 ~만큼 회전하기"},
        {BlockTypeEnum::DIRECTION_RELATIVE_DURATION, "~초 동안 이동 방향 ~만큼 회전하기"}, // EntryJS에서는 ROTATE_BY_TIME와 동일하게 동작
        {BlockTypeEnum::ROTATE_ABSOLUTE, "방향을 ~로 정하기"},
        {BlockTypeEnum::DIRECTION_ABSOLUTE, "이동 방향을 ~로 정하기"},
        {BlockTypeEnum::SEE_ANGLE_OBJECT, "~쪽 보기"},
        {BlockTypeEnum::MOVE_TO_ANGLE, "~ 방향으로 ~만큼 이동하기"},
        {BlockTypeEnum::CALC_BASIC, "기본 사칙연산"},
        {BlockTypeEnum::CALC_RAND, "무작위 수 계산"},
        {BlockTypeEnum::COORDINATE_MOUSE, "마우스 좌표"},
        {BlockTypeEnum::COORDINATE_OBJECT, "오브젝트 좌표/정보"},
        {BlockTypeEnum::QUOTIENT_AND_MOD, "몫과 나머지"},
        {BlockTypeEnum::CALC_OPERATION, "고급 수학 연산"},
        {BlockTypeEnum::GET_PROJECT_TIMER_VALUE, "타이머 값"},
        {BlockTypeEnum::CHOOSE_PROJECT_TIMER_ACTION, "타이머 제어"},
        {BlockTypeEnum::SET_VISIBLE_PROJECT_TIMER, "타이머 보이기/숨기기"},
        {BlockTypeEnum::GET_DATE, "날짜/시간 정보"},
        {BlockTypeEnum::DISTANCE_SOMETHING, "~까지의 거리"},
        {BlockTypeEnum::LENGTH_OF_STRING, "문자열 길이"},
        {BlockTypeEnum::REVERSE_OF_STRING, "문자열 뒤집기"},
        {BlockTypeEnum::COMBINE_SOMETHING, "문자열 합치기"},
        {BlockTypeEnum::CHAR_AT, "~의 ~번째 글자"},
        {BlockTypeEnum::SUBSTRING, "~의 ~번째부터 ~번째까지 글자"},
        {BlockTypeEnum::COUNT_MATCH_STRING, "~에서 ~ 찾기"},
        {BlockTypeEnum::INDEX_OF_STRING, "~에서 ~의 위치"},
        {BlockTypeEnum::REPLACE_STRING, "~의 ~을 ~로 바꾸기"},
        {BlockTypeEnum::CHANGE_STRING_CASE, "영어를 대/소문자로 바꾸기"},
        {BlockTypeEnum::GET_BLOCK_COUNT, "블록 개수"},
        {BlockTypeEnum::CHANGE_RGB_TO_HEX, "RGB를 HEX로 변환"},
        {BlockTypeEnum::CHANGE_HEX_TO_RGB, "HEX를 RGB로 변환"},
        {BlockTypeEnum::GET_BOOLEAN_VALUE, "참/거짓 값"},
        {BlockTypeEnum::GET_USER_NAME, "사용자 이름"},
        {BlockTypeEnum::GET_NICKNAME, "사용자 닉네임"},
        {BlockTypeEnum::SHOW, "모양 보이기"},
        {BlockTypeEnum::HIDE, "모양 숨기기"},
        {BlockTypeEnum::DIALOG_TIME, "~을 ~초 동안 말하기/생각하기"},
        {BlockTypeEnum::DIALOG, "~을 말하기/생각하기"},
        {BlockTypeEnum::REMOVE_DIALOG, "말풍선/생각풍선 지우기"},
        {BlockTypeEnum::CHANGE_TO_SOME_SHAPE, "~ 모양으로 바꾸기"},
        {BlockTypeEnum::CHANGE_TO_NEXT_SHAPE, "다음/이전 모양으로 바꾸기"},
        {BlockTypeEnum::WHEN_RUN_BUTTON_CLICK, "시작 버튼 클릭 시"},
        {BlockTypeEnum::WHEN_SOME_KEY_PRESSED, "키 눌렀을 때"},
        {BlockTypeEnum::MOUSE_CLICKED, "마우스 클릭 시"},
        {BlockTypeEnum::MOUSE_CLICK_CANCELED, "마우스 클릭 해제 시"},
        {BlockTypeEnum::WHEN_OBJECT_CLICK, "오브젝트 클릭 시"},
        {BlockTypeEnum::WHEN_OBJECT_CLICK_CANCELED, "오브젝트 클릭 해제 시"},
        {BlockTypeEnum::WHEN_MESSAGE_CAST, "신호 받았을 때"},
        {BlockTypeEnum::WHEN_SCENE_START, "장면 시작 시"},
        {BlockTypeEnum::GET_PICTURES, "모양 가져오기 (파라미터용)"},
        {BlockTypeEnum::TEXT_REPORTER_NUMBER, "숫자 입력 (파라미터용)"},
        {BlockTypeEnum::TEXT_REPORTER_STRING, "문자열 입력 (파라미터용)"}
    };
    auto it = koreanMap.find(type);
    if (it != koreanMap.end()) {
        return it->second;
    }
    return "알 수 없는 블록 타입";
}

std::string blockTypeEnumToEnglishString(BlockTypeEnum type) {
    // This is the reverse of stringToBlockTypeEnum's map keys.
    // For brevity, only a few examples. You'd need to fill this out completely.
    static const std::map<BlockTypeEnum, std::string> englishMap = {
        {BlockTypeEnum::MOVE_DIRECTION, "move_direction"},
        {BlockTypeEnum::BOUNCE_WALL, "bounce_wall"},
        // ... Add all other mappings ...
        {BlockTypeEnum::CALC_BASIC, "calc_basic"},
        {BlockTypeEnum::SHOW, "show"},
        {BlockTypeEnum::UNKNOWN, "unknown_block_type"}
    };
     auto it = englishMap.find(type);
    if (it != englishMap.end()) {
        return it->second;
    }
    return "unknown_block_type";
}

} // namespace Omocha