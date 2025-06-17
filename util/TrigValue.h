#pragma once
#include <string>
#include <map>
#include <cmath> // For M_SQRT2, M_SQRT3 (may need to define if not available) or std::sqrt
#include <limits> // For std::numeric_limits<double>::infinity()
/**
 * @brief 엔트리 의 이상한 각도시스템에 맞춘 삼각함수 테이블(?)
 *
 */
// M_SQRT3와 같은 상수가 정의되어 있지 않다면 직접 정의합니다.
#ifndef M_SQRT3
#define M_SQRT3 1.73205081
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356
#endif


#ifndef TrigValue_H
#define TrigValueTable_H



class TrigValue {
public:
   inline static const std::map<std::string, std::map<int, double>> TABLE = {
        {"sin", {
            {0, 0.0}, {30, 0.5}, {45, M_SQRT2 / 2.0}, {60, M_SQRT3 / 2.0}, {90, 1.0},
            {120, M_SQRT3 / 2.0}, {135, M_SQRT2 / 2.0}, {150, 0.5}, {180, 0.0},
            {210, -0.5}, {225, -M_SQRT2 / 2.0}, {240, -M_SQRT3 / 2.0}, {270, -1.0},
            {300, -M_SQRT3 / 2.0}, {315, -M_SQRT2 / 2.0}, {330, -0.5}, {360, 0.0}
        }},
        {"cos", {
            {0, 1.0}, {30, M_SQRT3 / 2.0}, {45, M_SQRT2 / 2.0}, {60, 0.5}, {90, 0.0},
            {120, -0.5}, {135, -M_SQRT2 / 2.0}, {150, -M_SQRT3 / 2.0}, {180, -1.0},
            {210, -M_SQRT3 / 2.0}, {225, -M_SQRT2 / 2.0}, {240, -0.5}, {270, 0.0},
            {300, 0.5}, {315, M_SQRT2 / 2.0}, {330, M_SQRT3 / 2.0}, {360, 1.0}
        }},
        {"tan", {
            {0, 0.0}, {30, M_SQRT3 / 3.0}, {45, 1.0}, {60, M_SQRT3}, {90, std::numeric_limits<double>::infinity()},
            {120, -M_SQRT3}, {135, -1.0}, {150, -M_SQRT3 / 3.0}, {180, 0.0},
            {210, M_SQRT3 / 3.0}, {225, 1.0}, {240, M_SQRT3}, {270, -std::numeric_limits<double>::infinity()},
            {300, -M_SQRT3}, {315, -1.0}, {330, -M_SQRT3 / 3.0}, {360, 0.0}
        }}
    };

    static double toRadians(double degrees) {
        return degrees * M_PI / 180.0;
    };

    static double TRIG_VALUE(std::string Operator,double degrees) {
        int angle_int = static_cast<int>(round(degrees));
        int normalized_angle_int = angle_int % 360;
        if (normalized_angle_int < 0) {
            normalized_angle_int += 360;
        }
        auto it_op = TABLE.find(Operator);
        if (it_op != TABLE.end()) {
            const auto& angle_map = it_op->second;
            auto it_angle = angle_map.find(normalized_angle_int);
            if (it_angle != angle_map.end()) {
                return it_angle->second;
            }
            // 테이블에 없는 경우, 표준 라이브러리 함수 사용
            // op 문자열을 직접 비교하여 분기합니다.
            double radians = toRadians(degrees); // 표준 함수는 라디안 값을 사용
            if (Operator == "sin") {
                return std::sin(radians);
            }
            if (Operator == "cos") {
                return std::cos(radians);
            }
            if (Operator == "tan") {
                return std::tan(radians);
            }
            // 알려지지 않은 연산자 처리 (예: 예외 발생 또는 기본값 반환)
            // 이 지점에 도달하면 TABLE.find(op)는 성공했지만, op가 sin/cos/tan이 아닌 경우입니다.
            // (현재 TABLE 구조상 이럴 일은 없지만 방어적으로 코딩)
            // 예를 들어, throw std::invalid_argument("Unknown trigonometric operator: " + op);
            return std::nan(""); // 또는 NaN 반환
        }
    };
};



#endif //ENTRYTRIANGLEFUNTIONTABLE_H
