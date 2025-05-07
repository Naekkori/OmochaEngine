// OmochaEngine.h: 표준 시스템 포함 파일
// 또는 프로젝트 특정 포함 파일이 들어 있는 포함 파일입니다.

#pragma once
#include <iostream>
#include <filesystem>
// TODO: 여기서 프로그램에 필요한 추가 헤더를 참조합니다.
class MainProgram
{
public:
    struct MainProgramValue
    {
        bool setVsync = true;
        const bool getVsync() const { return setVsync; }
    };
    MainProgramValue mainProgramValue;
};
