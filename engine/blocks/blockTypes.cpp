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
        {"combine_something", BlockTypeEnum::COMBINE_SOMETHING},
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
        {"get_sound_volume", BlockTypeEnum::GET_SOUND_VOLUME},
        {"get_sound_speed", BlockTypeEnum::GET_SOUND_SPEED},
        {"get_sound_duration", BlockTypeEnum::GET_SOUND_DURATION},
        {"get_canvas_input_value", BlockTypeEnum::GET_CANVAS_INPUT_VALUE},
        {"length_of_list", BlockTypeEnum::LENGTH_OF_LIST},
        {"is_included_in_list", BlockTypeEnum::IS_INCLUDED_IN_LIST},
        {"show", BlockTypeEnum::SHOW},
        {"hide", BlockTypeEnum::HIDE},
        {"dialog_time", BlockTypeEnum::DIALOG_TIME},
        {"dialog", BlockTypeEnum::DIALOG},
        {"remove_dialog", BlockTypeEnum::REMOVE_DIALOG},
        {"change_to_some_shape", BlockTypeEnum::CHANGE_TO_SOME_SHAPE},
        {"change_to_next_shape", BlockTypeEnum::CHANGE_TO_NEXT_SHAPE},
        {"add_effect_amount", BlockTypeEnum::ADD_EFFECT_AMOUNT},
        {"change_effect_amount", BlockTypeEnum::CHANGE_EFFECT_AMOUNT},
        {"erase_all_effects", BlockTypeEnum::ERASE_ALL_EFFECTS},
        {"change_scale_size", BlockTypeEnum::CHANGE_SCALE_SIZE},
        {"set_scale_size", BlockTypeEnum::SET_SCALE_SIZE},
        {"stretch_scale_size", BlockTypeEnum::STRETCH_SCALE_SIZE},
        {"reset_scale_size", BlockTypeEnum::RESET_SCALE_SIZE},
        {"flip_x", BlockTypeEnum::FLIP_X},
        {"flip_y", BlockTypeEnum::FLIP_Y},
        {"change_object_index", BlockTypeEnum::CHANGE_OBJECT_INDEX},
        {"sound_something_with_block", BlockTypeEnum::SOUND_SOMETHING_WITH_BLOCK},
        {"sound_something_second_with_block", BlockTypeEnum::SOUND_SOMETHING_SECOND_WITH_BLOCK},
        {"sound_from_to", BlockTypeEnum::SOUND_FROM_TO},
        {"sound_something_wait_with_block", BlockTypeEnum::SOUND_SOMETHING_WAIT_WITH_BLOCK},
        {"sound_something_second_wait_with_block", BlockTypeEnum::SOUND_SOMETHING_SECOND_WAIT_WITH_BLOCK},
        {"sound_from_to_and_wait", BlockTypeEnum::SOUND_FROM_TO_AND_WAIT},
        {"sound_volume_change", BlockTypeEnum::SOUND_VOLUME_CHANGE},
        {"sound_volume_set", BlockTypeEnum::SOUND_VOLUME_SET},
        {"sound_speed_change", BlockTypeEnum::SOUND_SPEED_CHANGE},
        {"sound_speed_set", BlockTypeEnum::SOUND_SPEED_SET},
        {"sound_silent_all", BlockTypeEnum::SOUND_SILENT_ALL},
        {"play_bgm", BlockTypeEnum::PLAY_BGM},
        {"when_run_button_click", BlockTypeEnum::WHEN_RUN_BUTTON_CLICK},
        {"when_some_key_pressed", BlockTypeEnum::WHEN_SOME_KEY_PRESSED},
        {"mouse_clicked", BlockTypeEnum::MOUSE_CLICKED},
        {"mouse_click_cancled", BlockTypeEnum::MOUSE_CLICK_CANCELED}, // Original: mouse_click_cancled
        {"when_object_click", BlockTypeEnum::WHEN_OBJECT_CLICK},
        {"when_object_click_canceled", BlockTypeEnum::WHEN_OBJECT_CLICK_CANCELED},
        {"when_message_cast", BlockTypeEnum::WHEN_MESSAGE_CAST},     // Original: when_message_cast (receive)
        {"message_cast", BlockTypeEnum::MESSAGE_CAST_ACTION},        // For sending
        {"when_scene_start", BlockTypeEnum::WHEN_SCENE_START},
        {"start_scene", BlockTypeEnum::START_SCENE},
        {"start_neighbor_scene", BlockTypeEnum::START_NEIGHBOR_SCENE},
        {"get_pictures", BlockTypeEnum::GET_PICTURES },
        {"text_reporter_number", BlockTypeEnum::TEXT_REPORTER_NUMBER },
        {"text_reporter_string", BlockTypeEnum::TEXT_REPORTER_STRING },
        {"get_variable", BlockTypeEnum::GET_VARIABLE},
        {"value_of_index_from_list", BlockTypeEnum::VALUE_OF_INDEX_FROM_LIST},
        {"set_visible_answer", BlockTypeEnum::SET_VISIBLE_ANSWER},
        {"ask_and_wait", BlockTypeEnum::ASK_AND_WAIT},
        {"change_variable", BlockTypeEnum::CHANGE_VARIABLE},
        {"set_variable", BlockTypeEnum::SET_VARIABLE},
        {"show_variable", BlockTypeEnum::SHOW_VARIABLE},
        {"hide_variable", BlockTypeEnum::HIDE_VARIABLE},
        {"add_value_to_list", BlockTypeEnum::ADD_VALUE_TO_LIST},
        {"remove_value_from_list", BlockTypeEnum::REMOVE_VALUE_FROM_LIST},
        {"insert_value_to_list", BlockTypeEnum::INSERT_VALUE_TO_LIST},
        {"change_value_list_index", BlockTypeEnum::CHANGE_VALUE_LIST_INDEX},
        {"show_list", BlockTypeEnum::SHOW_LIST},
        {"hide_list", BlockTypeEnum::HIDE_LIST},
        // Flow
        {"wait_second", BlockTypeEnum::WAIT_SECOND},
        {"repeat_basic", BlockTypeEnum::REPEAT_BASIC},
        {"repeat_inf", BlockTypeEnum::REPEAT_INF},
        {"repeat_while_true", BlockTypeEnum::REPEAT_WHILE_TRUE},
        {"stop_repeat", BlockTypeEnum::STOP_REPEAT},
        {"continue_repeat", BlockTypeEnum::CONTINUE_REPEAT},
        {"_if", BlockTypeEnum::_IF},
        {"if_else", BlockTypeEnum::IF_ELSE},
        {"wait_until_true", BlockTypeEnum::WAIT_UNTIL_TRUE},
        {"stop_object", BlockTypeEnum::STOP_OBJECT}, // Added
        {"restart_project", BlockTypeEnum::RESTART_PROJECT}, // Added
        {"when_clone_start", BlockTypeEnum::WHEN_CLONE_START}, // Added
        {"is_clicked", BlockTypeEnum::IS_CLICKED}, // 일반 마우스 클릭 판단
        {"is_object_clicked", BlockTypeEnum::IS_OBJECT_CLICKED_JUDGE}, // 특정 오브젝트 클릭 판단
        {"is_press_some_key", BlockTypeEnum::IS_KEY_PRESSED_JUDGE},     // 특정 키 눌림 판단
        {"reach_something", BlockTypeEnum::REACH_SOMETHING}, // ~에 닿았는가?
        {"is_type", BlockTypeEnum::IS_TYPE}, // ~ 타입인가?
        {"boolean_basic_operator", BlockTypeEnum::BOOLEAN_BASIC_OPERATOR} // 두 값의 관계 비교
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
        {BlockTypeEnum::GET_SOUND_VOLUME, "소리 크기 값"},
        {BlockTypeEnum::GET_SOUND_SPEED, "소리 재생 속도 값"},
        {BlockTypeEnum::GET_SOUND_DURATION, "소리 길이 값"},
        {BlockTypeEnum::GET_CANVAS_INPUT_VALUE, "대답 값"},
        {BlockTypeEnum::LENGTH_OF_LIST, "리스트의 항목 수"},
        {BlockTypeEnum::IS_INCLUDED_IN_LIST, "리스트에 항목이 포함되어 있는가"},
        {BlockTypeEnum::SHOW, "모양 보이기"},
        {BlockTypeEnum::HIDE, "모양 숨기기"},
        {BlockTypeEnum::DIALOG_TIME, "~을 ~초 동안 말하기/생각하기"},
        {BlockTypeEnum::DIALOG, "~을 말하기/생각하기"},
        {BlockTypeEnum::REMOVE_DIALOG, "말풍선/생각풍선 지우기"},
        {BlockTypeEnum::CHANGE_TO_SOME_SHAPE, "~ 모양으로 바꾸기"},
        {BlockTypeEnum::CHANGE_TO_NEXT_SHAPE, "다음/이전 모양으로 바꾸기"},
        {BlockTypeEnum::ADD_EFFECT_AMOUNT, "효과를 ~만큼 주기"},
        {BlockTypeEnum::CHANGE_EFFECT_AMOUNT, "효과를 ~로 정하기"},
        {BlockTypeEnum::ERASE_ALL_EFFECTS, "모든 효과 지우기"},
        {BlockTypeEnum::CHANGE_SCALE_SIZE, "크기를 ~만큼 바꾸기"},
        {BlockTypeEnum::SET_SCALE_SIZE, "크기를 ~로 정하기"},
        {BlockTypeEnum::STRETCH_SCALE_SIZE, "가로/세로 크기를 ~로 정하기"},
        {BlockTypeEnum::RESET_SCALE_SIZE, "원래 크기로"},
        {BlockTypeEnum::FLIP_X, "좌우 뒤집기"},
        {BlockTypeEnum::FLIP_Y, "상하 뒤집기"},
        {BlockTypeEnum::CHANGE_OBJECT_INDEX, "순서 바꾸기"},
        {BlockTypeEnum::SOUND_SOMETHING_WITH_BLOCK, "소리 재생하기"},
        {BlockTypeEnum::SOUND_SOMETHING_SECOND_WITH_BLOCK, "소리를 ~초 재생하기"},
        {BlockTypeEnum::SOUND_FROM_TO, "소리를 ~부터 ~까지 재생하기"},
        {BlockTypeEnum::SOUND_SOMETHING_WAIT_WITH_BLOCK, "소리 재생하고 기다리기"},
        {BlockTypeEnum::SOUND_SOMETHING_SECOND_WAIT_WITH_BLOCK, "소리를 ~초 재생하고 기다리기"},
        {BlockTypeEnum::SOUND_FROM_TO_AND_WAIT, "소리를 ~부터 ~까지 재생하고 기다리기"},
        {BlockTypeEnum::SOUND_VOLUME_CHANGE, "소리 크기를 ~만큼 바꾸기"},
        {BlockTypeEnum::SOUND_VOLUME_SET, "소리 크기를 ~로 정하기"},
        {BlockTypeEnum::SOUND_SPEED_CHANGE, "소리 재생 속도를 ~만큼 바꾸기"},
        {BlockTypeEnum::SOUND_SPEED_SET, "소리 재생 속도를 ~로 정하기"},
        {BlockTypeEnum::SOUND_SILENT_ALL, "모든 소리 끄기"},
        {BlockTypeEnum::PLAY_BGM, "배경음악 재생하기"},
        {BlockTypeEnum::WHEN_RUN_BUTTON_CLICK, "시작 버튼 클릭 시"},
        {BlockTypeEnum::WHEN_SOME_KEY_PRESSED, "키 눌렀을 때"},
        {BlockTypeEnum::MOUSE_CLICKED, "마우스 클릭 시"},
        {BlockTypeEnum::MOUSE_CLICK_CANCELED, "마우스 클릭 해제 시"},
        {BlockTypeEnum::WHEN_OBJECT_CLICK, "오브젝트 클릭 시"},
        {BlockTypeEnum::WHEN_OBJECT_CLICK_CANCELED, "오브젝트 클릭 해제 시"},
        {BlockTypeEnum::WHEN_MESSAGE_CAST, "신호 받았을 때"}, // Receiving
        {BlockTypeEnum::MESSAGE_CAST_ACTION, "신호 보내기"}, // Sending
        {BlockTypeEnum::WHEN_SCENE_START, "장면 시작 시"},
        {BlockTypeEnum::START_SCENE, "장면 시작하기"},
        {BlockTypeEnum::START_NEIGHBOR_SCENE, "다음/이전 장면 시작하기"},
        {BlockTypeEnum::GET_PICTURES, "모양 가져오기 (파라미터용)"},
        {BlockTypeEnum::TEXT_REPORTER_NUMBER, "숫자 입력 (파라미터용)"},
        {BlockTypeEnum::TEXT_REPORTER_STRING, "문자열 입력 (파라미터용)"},
    {BlockTypeEnum::GET_VARIABLE, "변수 값 가져오기"},
    {BlockTypeEnum::VALUE_OF_INDEX_FROM_LIST, "리스트의 ~번째 항목 값"},
        // Flow
        {BlockTypeEnum::WAIT_SECOND, "~초 기다리기"},
        {BlockTypeEnum::REPEAT_BASIC, "~번 반복하기"},
        {BlockTypeEnum::REPEAT_INF, "계속 반복하기"},
        {BlockTypeEnum::REPEAT_WHILE_TRUE, "~가 참일 때까지 반복하기"},
        {BlockTypeEnum::STOP_REPEAT, "반복 중단하기"},
        {BlockTypeEnum::CONTINUE_REPEAT, "반복 처음으로 돌아가기"},
        {BlockTypeEnum::CONTINUE_REPEAT, "반복 처음으로 돌아가기"},
        // Variable/List Actions
        {BlockTypeEnum::SET_VISIBLE_ANSWER, "대답 보이기/숨기기"},
        {BlockTypeEnum::ASK_AND_WAIT, "묻고 기다리기"},
        {BlockTypeEnum::CHANGE_VARIABLE, "변수 값 바꾸기"},
        {BlockTypeEnum::SET_VARIABLE, "변수 값 정하기"},
        {BlockTypeEnum::SHOW_VARIABLE, "변수 보이기"},
        {BlockTypeEnum::HIDE_VARIABLE, "변수 숨기기"},
        {BlockTypeEnum::ADD_VALUE_TO_LIST, "리스트에 항목 추가하기"},
        {BlockTypeEnum::REMOVE_VALUE_FROM_LIST, "리스트에서 항목 삭제하기"},
        {BlockTypeEnum::INSERT_VALUE_TO_LIST, "리스트의 특정 위치에 항목 추가하기"},
        {BlockTypeEnum::CHANGE_VALUE_LIST_INDEX, "리스트의 특정 위치 항목 바꾸기"},
        {BlockTypeEnum::SHOW_LIST, "리스트 보이기"},
        {BlockTypeEnum::HIDE_LIST, "리스트 숨기기"},
        {BlockTypeEnum::_IF, "만약 ~이라면"},
        {BlockTypeEnum::IF_ELSE, "만약 ~이라면, 아니면"},
        {BlockTypeEnum::WAIT_UNTIL_TRUE, "~가 될 때까지 기다리기"},
        {BlockTypeEnum::STOP_OBJECT, "멈추기"}, // Added
        {BlockTypeEnum::RESTART_PROJECT, "다시 시작하기"}, // Added
        {BlockTypeEnum::WHEN_CLONE_START, "복제되었을 때"}, // Added
        {BlockTypeEnum::IS_CLICKED, "마우스를 클릭했는가?"}, // 일반 마우스 클릭
        {BlockTypeEnum::IS_OBJECT_CLICKED_JUDGE, "오브젝트를 클릭했는가?"}, // 특정 오브젝트 클릭
        {BlockTypeEnum::IS_KEY_PRESSED_JUDGE, "키가 눌려있는가?"},      // 특정 키 눌림 판단
        {BlockTypeEnum::REACH_SOMETHING, "~에 닿았는가?"},
        {BlockTypeEnum::IS_TYPE, "~ 타입인가?"},
        {BlockTypeEnum::BOOLEAN_BASIC_OPERATOR, "두 값 비교"}
    };
    auto it = koreanMap.find(type);
    if (it != koreanMap.end()) {
        return it->second;
    }
    return "알 수 없는 블록 타입";
}
ObjectIndexChangeType stringToObjectIndexChangeType(const std::string& typeStr) {
    if (typeStr == "FRONT") return ObjectIndexChangeType::BRING_TO_FRONT;
    if (typeStr == "FORWARD") return ObjectIndexChangeType::BRING_FORWARD;
    if (typeStr == "BACKWARD") return ObjectIndexChangeType::SEND_BACKWARD;
    if (typeStr == "BACK") return ObjectIndexChangeType::SEND_TO_BACK;
    return ObjectIndexChangeType::UNKNOWN;
}

} // namespace Omocha
