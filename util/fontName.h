#pragma once
#include <string>
#include <ranges>
#include <algorithm>
// 폰트 패밀리 이름을 나타내는 열거형
enum class FontName {
    D2Coding,
    MaruBuri,
    NanumBarunPen,
    NanumGothic,
    NanumMyeongjo,
    NanumPen,
    NanumSquareRound,
    // 필요하다면 다른 폰트 추가
    Default // 기본값 또는 알 수 없는 폰트
};

// FontName 열거형 값을 실제 폰트 패밀리 이름 문자열로 변환하는 함수
inline const char* getFontFamilyName(FontName font) {
    switch (font) {
        case FontName::D2Coding:         return "d2coding";
        case FontName::MaruBuri:         return "maru buri"; // 실제 폰트 이름 확인 및 소문자 변환 필요
        case FontName::NanumBarunPen:    return "nanum barun pen";
        case FontName::NanumGothic:      return "nanumgothic"; // 또는 "나눔고딕" (한글 이름은 그대로)
        case FontName::NanumMyeongjo:    return "nanummyeongjo"; // 또는 "나눔명조" (한글 이름은 그대로)
        case FontName::NanumPen:         return "nanum pen script"; // 실제 폰트 이름 확인 및 소문자 변환 필요
        case FontName::NanumSquareRound: return "nanumsquareround";
        default:                         return "gulim"; // 또는 "굴림" (기본 폰트)
    }
}

inline FontName getFontNameFromString(const std::string& familyName) {
    std::string lowerFamilyName = familyName;
    // 입력 문자열을 소문자로 변환 (선택 사항, 비교 일관성 위해)
    std::transform(lowerFamilyName.begin(), lowerFamilyName.end(), lowerFamilyName.begin(), ::tolower);

    if (lowerFamilyName == "d2coding") return FontName::D2Coding;
    if (lowerFamilyName == "maru buri") return FontName::MaruBuri;
    if (lowerFamilyName == "nanum barun pen") return FontName::NanumBarunPen;
    if (lowerFamilyName == "nanum gothic" || lowerFamilyName == "나눔고딕") return FontName::NanumGothic;
    if (lowerFamilyName == "nanumm yeongjo" || lowerFamilyName == "나눔명조") return FontName::NanumMyeongjo;
    if (lowerFamilyName == "nanum pen script") return FontName::NanumPen;
    if (lowerFamilyName == "nanum squareround") return FontName::NanumSquareRound;
    // 필요한 다른 폰트 이름 비교 추가
    return FontName::Default; // 일치하는 이름이 없으면 기본값 반환
}
// struct FontInfo {
//     std::string filePath;
//     FontName fontName;
//     int handle = -1;
// };
// std::vector<FontInfo> loadedFonts;
