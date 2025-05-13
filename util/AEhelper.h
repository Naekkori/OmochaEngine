#pragma once // 헤더 가드 추가

#include <string>
#include <vector> // getPlayingObjectIDs를 위해 추가 (선택 사항)
#include <map>    // m_activeSounds를 위해 추가
#include "Logger.h"
#include "miniaudio.h" // miniaudio.h 포함

class AudioEngineHelper
{
private:
    SimpleLogger logger;
    void aeStdOut(const std::string &message) const; // const 멤버 함수로 변경 가능

    ma_engine m_engine;
    ma_context m_context;
    ma_device m_device;

    std::map<std::string, ma_sound> m_activeSounds; // 오브젝트 ID와 ma_sound 매핑

    bool m_contextInitialized = false;
    bool m_deviceInitialized = false;
    bool m_engineInitialized = false;

    ma_sound m_backgroundMusic;          // 배경음악을 위한 ma_sound 인스턴스
    bool m_backgroundMusicInitialized = false; // 배경음악 초기화 상태

    // 내부적으로 사운드를 안전하게 해제하는 헬퍼 함수
    void uninitializeSound(ma_sound* pSound);

public:
    AudioEngineHelper();
    ~AudioEngineHelper();

    // 효과음 관련 메서드
    void playSound(const std::string& objectId, const std::string& filePath, bool loop = false, float initialVolume = 1.0f);
    void stopSound(const std::string& objectId);
    void stopAllSounds();
    void stopAllSoundsExcept(const std::string& objectIdToKeepPlaying);
    void setSoundVolume(const std::string& objectId, float volume);
    float getSoundVolume(const std::string& objectId) const;
    unsigned int getPlayingSoundCount() const;

    // 배경음악 관련 메서드
    void playBackgroundMusic(const std::string& filePath, bool loop = true, float initialVolume = 1.0f);
    void stopBackgroundMusic();
    void setBackgroundMusicVolume(float volume);
    float getBackgroundMusicVolume() const;
    bool isBackgroundMusicPlaying() const;

    // std::vector<std::string> getPlayingObjectIDs() const; // 필요하다면 활성화
    void setMasterVolume(float volume);
    // float getMasterVolume() const; // ma_engine_get_volume은 직접 제공되지 않음
};