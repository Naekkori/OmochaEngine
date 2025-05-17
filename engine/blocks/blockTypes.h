#pragma once

#include <string>
#include <vector>
#include <map>

namespace Omocha {

enum class BlockTypeEnum {
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
    // Looks
    SHOW,
    HIDE,
    DIALOG_TIME,
    DIALOG,
    REMOVE_DIALOG,
    CHANGE_TO_SOME_SHAPE,
    CHANGE_TO_NEXT_SHAPE,
    // Event Triggers (might not need Korean names in error messages if they are not "executed" in the same way)
    WHEN_RUN_BUTTON_CLICK,
    WHEN_SOME_KEY_PRESSED,
    MOUSE_CLICKED,
    MOUSE_CLICK_CANCELED, // Original: mouse_click_cancled
    WHEN_OBJECT_CLICK,
    WHEN_OBJECT_CLICK_CANCELED,
    WHEN_MESSAGE_CAST, // Original: when_message_cast (receive)
    WHEN_SCENE_START,
    // Params (usually not top-level executable blocks, but types within params)
    GET_PICTURES, // from getOperandValue
    TEXT_REPORTER_NUMBER, // from getOperandValue
    TEXT_REPORTER_STRING  // from getOperandValue
};

/**
 * @brief Converts a block type string to its corresponding BlockTypeEnum.
 * @param typeStr The string representation of the block type.
 * @return The BlockTypeEnum value.
 */
BlockTypeEnum stringToBlockTypeEnum(const std::string& typeStr);

/**
 * @brief Converts a BlockTypeEnum to its Korean string representation.
 * @param type The BlockTypeEnum value.
 * @return The Korean string for the block type.
 */
std::string blockTypeEnumToKoreanString(BlockTypeEnum type);
}