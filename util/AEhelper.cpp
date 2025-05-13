#include "AEhelper.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdexcept>

AudioEngineHelper::AudioEngineHelper() : logger("hibiki.log") {
    aeStdOut("Audio Engine Helper initializing...");
    ma_device_config deviceConfig;
    ma_context_config contextConfig;
    ma_result result;

    // Initialize the context
    contextConfig = ma_context_config_init();
    // 특정 백엔드를 사용하지 않을 경우 nullptr과 0을 전달
    result = ma_context_init(nullptr, 0, &contextConfig, &m_context);
    if (result != MA_SUCCESS) {
        throw std::runtime_error("Failed to initialize audio context");
    }
    m_contextInitialized = true;
    aeStdOut("Audio context initialized successfully");

    // Initialize the device
    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 2;           
    deviceConfig.sampleRate = 44100;           

    result = ma_device_init(&m_context, &deviceConfig, &m_device);
    if (result != MA_SUCCESS) {
        if (m_contextInitialized) ma_context_uninit(&m_context);
        throw std::runtime_error("Failed to initialize audio device");
    }
    m_deviceInitialized = true;
    aeStdOut("Audio device initialized successfully");

    // Initialize the engine
    ma_engine_config engineConfig;
    engineConfig = ma_engine_config_init();
    engineConfig.pDevice = &m_device; // 엔진에 디바이스 연결

    result = ma_engine_init(&engineConfig, &m_engine);
    if (result != MA_SUCCESS) {
        if (m_deviceInitialized) ma_device_uninit(&m_device);
        if (m_contextInitialized) ma_context_uninit(&m_context);
        throw std::runtime_error("Failed to initialize audio engine");
    }
    m_engineInitialized = true;
    aeStdOut("Audio engine initialized successfully");
}

AudioEngineHelper::~AudioEngineHelper() {
    aeStdOut("Audio Engine Helper uninitializing...");
    stopAllSounds(); // 모든 활성 사운드 정리
    if (m_backgroundMusicInitialized) {
        aeStdOut("Stopping and uninitializing background music.");
        uninitializeSound(&m_backgroundMusic); // 배경음악도 해제
        m_backgroundMusicInitialized = false;
    }

    if (m_engineInitialized) {
        ma_engine_uninit(&m_engine);
        aeStdOut("Audio engine uninitialized");
    }
    if (m_deviceInitialized) {
        ma_device_uninit(&m_device);
        aeStdOut("Audio device uninitialized");
    }
    if (m_contextInitialized) {
        ma_context_uninit(&m_context);
        aeStdOut("Audio context initialized successfully");
    }
}

void AudioEngineHelper::uninitializeSound(ma_sound* pSound) {
    if (pSound) { // 초기화되었는지 확인
        ma_sound_stop(pSound);
        ma_sound_uninit(pSound);
    }
}

void AudioEngineHelper::playSound(const std::string& objectId, const std::string& filePath, bool loop, float initialVolume) {
    if (!m_engineInitialized) {
        aeStdOut("Engine not initialized. Cannot play sound for object: " + objectId);
        return;
    }

    // 해당 objectId로 이미 재생 중인 사운드가 있다면 정지하고 해제
    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end()) {
        aeStdOut("Stopping and uninitializing existing sound for object: " + objectId);
        uninitializeSound(&(it->second));
        // map에서 제거하지 않고, 새 사운드로 덮어쓸 것이므로 ma_sound 객체만 새로 초기화
    }

    ma_sound soundInstance; // 새 ma_sound 인스턴스 또는 기존 맵 위치에 새로 초기화
    ma_result result;
    // MA_SOUND_FLAG_DECODE: 미리 디코딩하여 메모리에 로드 (짧은 효과음에 적합)
    // 스트리밍이 필요하면 플래그를 조정하거나 ma_sound_init_from_file_w 스트리밍 버전 사용
    result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &soundInstance);

    if (result != MA_SUCCESS) {
        aeStdOut("Failed to load sound file: " + filePath + " for object: " + objectId + ". Error: " + ma_result_description(result));
        return;
    }

    ma_sound_set_looping(&soundInstance, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&soundInstance, initialVolume);
    ma_sound_start(&soundInstance);

    m_activeSounds[objectId] = soundInstance; // 맵에 저장 (새로 추가 또는 덮어쓰기)
    aeStdOut("Sound started: " + filePath + " for object: " + objectId);
}

void AudioEngineHelper::stopSound(const std::string& objectId) {
    if (!m_engineInitialized) return;

    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end()) {
        aeStdOut("Stopping sound for object: " + objectId);
        uninitializeSound(&(it->second));
        m_activeSounds.erase(it); // 맵에서 제거
    } else {
        aeStdOut("No sound found to stop for object: " + objectId);
    }
}

void AudioEngineHelper::stopAllSounds() {
    if (!m_engineInitialized) return;
    aeStdOut("Stopping all sounds...");
    for (auto& pair : m_activeSounds) {
        uninitializeSound(&(pair.second));
    }
    // 배경음악도 중지
    if (m_backgroundMusicInitialized) {
        uninitializeSound(&m_backgroundMusic);
        m_backgroundMusicInitialized = false;
    }
    m_activeSounds.clear();
}

void AudioEngineHelper::stopAllSoundsExcept(const std::string& objectIdToKeepPlaying) {
    if (!m_engineInitialized) return;
    aeStdOut("Stopping all sounds except for object: " + objectIdToKeepPlaying);
    
    // 직접 반복 중 제거는 복잡하므로, 제거할 ID 목록을 만들거나 새 맵을 만듭니다.
    // 여기서는 간단하게 반복하며 조건에 맞지 않는 것을 정지/해제하고, 나중에 맵에서 제거합니다.
    std::vector<std::string> idsToRemove;
    for (auto& pair : m_activeSounds) {
        if (pair.first != objectIdToKeepPlaying) {
            uninitializeSound(&(pair.second));
            idsToRemove.push_back(pair.first);
        }
    }
    for (const auto& id : idsToRemove) {
        m_activeSounds.erase(id);
    }
}

unsigned int AudioEngineHelper::getPlayingSoundCount() const {
    if (!m_engineInitialized) return 0;

    unsigned int playingCount = 0;
    for (const auto& pair : m_activeSounds) {
        // ma_sound_is_playing은 const ma_sound*를 받으므로 const_cast가 필요할 수 있으나,
        // miniaudio 최신 버전에서는 const를 지원할 수 있습니다.
        // 여기서는 pair.second가 ma_sound 객체 그 자체이므로 &pair.second로 주소를 넘깁니다.
        if (ma_sound_is_playing(const_cast<ma_sound*>(&pair.second))) {
            playingCount++;
        }
    }
    return playingCount;
}

void AudioEngineHelper::setSoundVolume(const std::string& objectId, float volume) {
    if (!m_engineInitialized) return;
    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end()) {
        ma_sound_set_volume(&(it->second), volume);
    } else {
        aeStdOut("Cannot set volume: No sound found for object: " + objectId);
    }
}

float AudioEngineHelper::getSoundVolume(const std::string& objectId) const {
    if (!m_engineInitialized) return 0.0f;
    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end()) {
        // ma_sound_get_volume은 const ma_sound*를 받습니다.
        return ma_sound_get_volume(const_cast<ma_sound*>(&it->second));
    }
    aeStdOut("Cannot get volume: No sound found for object: " + objectId);
    return 0.0f; // 또는 오류 값 (예: -1.0f)
}

void AudioEngineHelper::setMasterVolume(float volume) {
    if (!m_engineInitialized) return;
    ma_engine_set_volume(&m_engine, volume);
    aeStdOut("Master volume set to: " + std::to_string(volume));
}

void AudioEngineHelper::playBackgroundMusic(const std::string& filePath, bool loop, float initialVolume) {
    if (!m_engineInitialized) {
        aeStdOut("Engine not initialized. Cannot play background music.");
        return;
    }

    // 기존 배경음악이 있다면 정지 및 해제
    if (m_backgroundMusicInitialized) {
        aeStdOut("Stopping and uninitializing existing background music.");
        uninitializeSound(&m_backgroundMusic);
        m_backgroundMusicInitialized = false;
    }

    ma_result result;
    // 배경음악은 스트리밍으로 로드하는 것이 일반적입니다 (MA_SOUND_FLAG_STREAM)
    // 필요에 따라 MA_SOUND_FLAG_NO_PITCH, MA_SOUND_FLAG_NO_SPATIALIZATION 플래그 사용 가능
    ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION;
    result = ma_sound_init_from_file(&m_engine, filePath.c_str(), flags, nullptr, nullptr, &m_backgroundMusic);

    if (result != MA_SUCCESS) {
        aeStdOut("Failed to load background music: " + filePath + ". Error: " + ma_result_description(result));
        return;
    }

    ma_sound_set_looping(&m_backgroundMusic, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&m_backgroundMusic, initialVolume);
    ma_sound_start(&m_backgroundMusic);

    m_backgroundMusicInitialized = true;
    aeStdOut("Background music started: " + filePath);
}

void AudioEngineHelper::stopBackgroundMusic() {
    if (!m_engineInitialized || !m_backgroundMusicInitialized) return;

    aeStdOut("Stopping background music.");
    uninitializeSound(&m_backgroundMusic);
    m_backgroundMusicInitialized = false;
}

void AudioEngineHelper::setBackgroundMusicVolume(float volume) {
    if (!m_engineInitialized || !m_backgroundMusicInitialized) return;
    ma_sound_set_volume(&m_backgroundMusic, volume);
}

float AudioEngineHelper::getBackgroundMusicVolume() const {
    if (!m_engineInitialized || !m_backgroundMusicInitialized) return 0.0f;
    return ma_sound_get_volume(const_cast<ma_sound*>(&m_backgroundMusic));
}

bool AudioEngineHelper::isBackgroundMusicPlaying() const {
    if (!m_engineInitialized || !m_backgroundMusicInitialized) return false;
    return ma_sound_is_playing(const_cast<ma_sound*>(&m_backgroundMusic)) == MA_TRUE;
}


void AudioEngineHelper::aeStdOut(const std::string &message) const {
    std::string TAG = "[Hibiki] "; // std::string으로 변경
    std::string lm = TAG + message; // std::string으로 변경
    printf(lm.c_str());
    printf("\n"); // printf는 자동 줄바꿈 안 함
    logger.log(lm);
}