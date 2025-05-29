#pragma once

#include <string>
#include <vector>
#include <map>

namespace Omocha
{
    enum class ObjectIndexChangeType
    {
        UNKNOWN,
        BRING_TO_FRONT, // 맨 앞으로 가져오기 ('FRONT')
        BRING_FORWARD,  // 앞으로 가져오기 ('FORWARD')
        SEND_BACKWARD,  // 뒤로 보내기 ('BACKWARD')
        SEND_TO_BACK    // 맨 뒤로 보내기 ('BACK')
    };

    enum class BlockTypeEnum
    {
        UNKNOWN,
        // Moving
        MOVE_DIRECTION,
        BOUNCE_WALL,
        MOVE_X,
        MOVE_Y,
        MOVE_XY_TIME,
        LOCATE_XY_TIME, // Often same as MOVE_XY_TIME
        LOCATE_X,
        LOCATE_Y,
        LOCATE_XY,
        LOCATE,
        LOCATE_OBJECT_TIME,
        ROTATE_RELATIVE,
        DIRECTION_RELATIVE, // Often same as ROTATE_RELATIVE
        ROTATE_BY_TIME,
        DIRECTION_RELATIVE_DURATION, // Often same as ROTATE_BY_TIME
        ROTATE_ABSOLUTE,
        DIRECTION_ABSOLUTE,
        SEE_ANGLE_OBJECT,
        MOVE_TO_ANGLE,
        // Calculator
        CALC_BASIC,
        CALC_RAND,
        COORDINATE_MOUSE,
        COORDINATE_OBJECT,
        QUOTIENT_AND_MOD,
        CALC_OPERATION,
        GET_PROJECT_TIMER_VALUE,
        CHOOSE_PROJECT_TIMER_ACTION,
        SET_VISIBLE_PROJECT_TIMER,
        GET_DATE,
        DISTANCE_SOMETHING,
        LENGTH_OF_STRING,
        REVERSE_OF_STRING,
        COMBINE_SOMETHING, // Note: "combie" in original, assuming "combine"
        CHAR_AT,
        SUBSTRING,
        COUNT_MATCH_STRING,
        INDEX_OF_STRING,
        REPLACE_STRING,
        CHANGE_STRING_CASE,
        GET_BLOCK_COUNT,
        CHANGE_RGB_TO_HEX,
        CHANGE_HEX_TO_RGB,
        GET_BOOLEAN_VALUE,
        GET_USER_NAME,
        GET_NICKNAME,        
        GET_SOUND_VOLUME,        // New
        GET_SOUND_SPEED,         // New
        GET_SOUND_DURATION,      // New
        GET_CANVAS_INPUT_VALUE,  // New
        LENGTH_OF_LIST,          // New
        IS_INCLUDED_IN_LIST,     // New
        // Looks
        SHOW,
        HIDE,
        DIALOG_TIME,
        DIALOG,
        REMOVE_DIALOG,
        CHANGE_TO_SOME_SHAPE,
        CHANGE_TO_NEXT_SHAPE,
        ADD_EFFECT_AMOUNT,       // New
        CHANGE_EFFECT_AMOUNT,    // New
        ERASE_ALL_EFFECTS,       // New
        CHANGE_SCALE_SIZE,       // New
        SET_SCALE_SIZE,          // New
        STRETCH_SCALE_SIZE,      // New
        RESET_SCALE_SIZE,        // New
        FLIP_X,                  // New
        FLIP_Y,                  // New
        CHANGE_OBJECT_INDEX,     // New
        // Sound
        SOUND_SOMETHING_WITH_BLOCK,             // New
        SOUND_SOMETHING_SECOND_WITH_BLOCK,      // New
        SOUND_FROM_TO,                          // New
        SOUND_SOMETHING_WAIT_WITH_BLOCK,        // New
        SOUND_SOMETHING_SECOND_WAIT_WITH_BLOCK, // New
        SOUND_FROM_TO_AND_WAIT,                 // New
        SOUND_VOLUME_CHANGE,                    // New
        SOUND_VOLUME_SET,                       // New
        SOUND_SPEED_CHANGE,                     // New
        SOUND_SPEED_SET,                        // New
        SOUND_SILENT_ALL,                       // New
        PLAY_BGM,                               // New
        // Event Triggers (might not need Korean names in error messages if they are not "executed" in the same way)
        WHEN_RUN_BUTTON_CLICK,
        WHEN_SOME_KEY_PRESSED,
        MOUSE_CLICKED,
        MOUSE_CLICK_CANCELED, // Original: mouse_click_cancled
        WHEN_OBJECT_CLICK,
        WHEN_OBJECT_CLICK_CANCELED,
        WHEN_MESSAGE_CAST, // Original: when_message_cast (receive)
        MESSAGE_CAST_ACTION, // New for sending message
        WHEN_SCENE_START,
        START_SCENE,             // New
        START_NEIGHBOR_SCENE,    // New
        // Params (usually not top-level executable blocks, but types within params)
        GET_PICTURES,         // from getOperandValue
        TEXT_REPORTER_NUMBER, // from getOperandValue
        TEXT_REPORTER_STRING, // from getOperandValue
        GET_VARIABLE,             // To retrieve a variable's value
        VALUE_OF_INDEX_FROM_LIST, // 리스트의 특정 인덱스 값을 가져오는 블록
        // Variable/List specific actions (not value reporters)
        SET_VISIBLE_ANSWER,       // New
        ASK_AND_WAIT,             // New
        CHANGE_VARIABLE,          // New
        SET_VARIABLE,             // New
        SHOW_VARIABLE,            // New
        HIDE_VARIABLE,            // New
        ADD_VALUE_TO_LIST,        // New
        REMOVE_VALUE_FROM_LIST,   // New
        INSERT_VALUE_TO_LIST,     // New
        CHANGE_VALUE_LIST_INDEX,  // New
        SHOW_LIST,                // New
        HIDE_LIST,                // New
        // Flow
        WAIT_SECOND,
        REPEAT_BASIC,
        REPEAT_INF,               // New
        REPEAT_WHILE_TRUE,        // New
        STOP_REPEAT,              // New: To break loops
        CONTINUE_REPEAT,          // New: To continue to next iteration of a loop
        _IF,                      // New: Conditional execution
        IF_ELSE,                  // New: Conditional execution with else
        WAIT_UNTIL_TRUE,          // New: Wait until a condition is true
        STOP_OBJECT,              // New: Stop script execution (from user request)
        RESTART_PROJECT,          // New: Restart the entire project
        WHEN_CLONE_START          // New: Event when a clone is created
    };

    /**
     * @brief Converts a block type string to its corresponding BlockTypeEnum.
     * @param typeStr The string representation of the block type.
     * @return The BlockTypeEnum value.
     */
    BlockTypeEnum stringToBlockTypeEnum(const std::string &typeStr);

    /**
     * @brief Converts a BlockTypeEnum to its Korean string representation.
     * @param type The BlockTypeEnum value.
     * @return The Korean string for the block type.
     */
    std::string blockTypeEnumToKoreanString(BlockTypeEnum type);

    ObjectIndexChangeType stringToObjectIndexChangeType(const std::string& typeStr);
}