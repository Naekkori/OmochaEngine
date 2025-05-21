#include "AEhelper.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdexcept>

AudioEngineHelper::AudioEngineHelper() : logger("hibiki.log"), m_globalPlaybackSpeed(1.0f) {
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

    if (m_engineInitialized) {
        ma_engine_uninit(&m_engine); // 엔진을 먼저 해제하여 오디오 스레드를 중지합니다.
        aeStdOut("Audio engine uninitialized");
        m_engineInitialized = false; // 플래그 업데이트
    }

    // 엔진이 중지된 후 사운드 리소스를 정리합니다.
    stopAllSounds(); // 모든 활성 사운드 정리 (내부적으로 배경음악도 처리 시도)
    if (m_backgroundMusicInitialized) { // stopAllSounds에서 처리되지 않았을 경우를 대비
        aeStdOut("Uninitializing background music.");
        uninitializeSound(&m_backgroundMusic);
        m_backgroundMusicInitialized = false;
    }
    clearPreloadedSounds(); // 미리 로딩된 사운드 해제

    if (m_deviceInitialized) {
        ma_device_uninit(&m_device);
        aeStdOut("Audio device uninitialized");
        m_deviceInitialized = false; // 플래그 업데이트
    }
    if (m_contextInitialized) {
        ma_context_uninit(&m_context);
        aeStdOut("Audio context uninitialized");
        m_contextInitialized = false; // 플래그 업데이트
    }
}

void AudioEngineHelper::uninitializeSound(ma_sound* pSound) {
    if (pSound) { // 초기화되었는지 확인
        ma_sound_stop(pSound);
        ma_sound_uninit(pSound);
    }
}

void AudioEngineHelper::preloadSound(const std::string& filePath) {
    if (!m_engineInitialized) {
        aeStdOut("Engine not initialized. Cannot preload sound: " + filePath);
        return;
    }
    if (m_decodedSoundsCache.count(filePath)) {
        aeStdOut("Sound already preloaded: " + filePath);
        return;
    }

    ma_sound soundToCache;
    ma_result result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &soundToCache);

    if (result != MA_SUCCESS) {
        aeStdOut("Failed to preload sound file: " + filePath + ". Error: " + ma_result_description(result));
    } else {
        m_decodedSoundsCache[filePath] = soundToCache;
        aeStdOut("Sound preloaded successfully: " + filePath);
    }
}

void AudioEngineHelper::clearPreloadedSounds() {
    if (!m_engineInitialized) return;
    if (m_decodedSoundsCache.empty()) return;

    aeStdOut("Clearing all preloaded sounds (" + std::to_string(m_decodedSoundsCache.size()) + " sounds)...");
    for (auto& pair : m_decodedSoundsCache) {
        // ma_sound_uninit은 ma_sound*를 받으므로 주소를 전달합니다.
        ma_sound_uninit(&(pair.second));
    }
    m_decodedSoundsCache.clear();
    aeStdOut("All preloaded sounds cleared.");
}

void AudioEngineHelper::playSound(const std::string& objectId, const std::string& filePath, bool loop=false, float initialVolume=1.0f) {
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

    ma_sound soundInstance; 
    ma_result result;

    auto cache_it = m_decodedSoundsCache.find(filePath);
    if (cache_it != m_decodedSoundsCache.end()) {
        // 캐시에서 발견됨, 복사하여 사용
        // 복사 시 플래그는 0 또는 필요한 최소 플래그 (원본이 DECODE | SHARE로 로드됨)
        result = ma_sound_init_copy(&m_engine, &(cache_it->second), 0, nullptr, &soundInstance);
        if (result == MA_SUCCESS) {
            aeStdOut("Playing sound from cache: " + filePath + " for object: " + objectId);
        } else {
            aeStdOut("Failed to copy sound from cache: " + filePath + ". Error: " + ma_result_description(result) + ". Loading from file instead.");
            // 복사 실패 시 파일에서 직접 로드 (아래 로직으로 이어짐)
        }
    }
    
    if (result != MA_SUCCESS) { // 캐시에 없거나 복사 실패 시 파일에서 직접 로드
        // MA_SOUND_FLAG_DECODE: 미리 디코딩하여 메모리에 로드 (짧은 효과음에 적합)
        result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &soundInstance);
        if (result != MA_SUCCESS) {
            aeStdOut("Failed to load sound file: " + filePath + " for object: " + objectId + ". Error: " + ma_result_description(result));
            return;
        }
        aeStdOut("Playing sound loaded from file (not cached or cache copy failed): " + filePath + " for object: " + objectId);
    }

    ma_sound_set_looping(&soundInstance, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&soundInstance, initialVolume);
    ma_sound_set_pitch(&soundInstance, m_globalPlaybackSpeed); // 전역 재생 속도 적용
    ma_sound_start(&soundInstance);

    m_activeSounds[objectId] = soundInstance; // 맵에 저장 (새로 추가 또는 덮어쓰기)
    aeStdOut("Sound started: " + filePath + " for object: " + objectId);
}
void AudioEngineHelper::playSoundForDuration(const std::string& objectId, const std::string& filePath, double durationSeconds, bool loop = false, float initialVolume = 1.0f){
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

    ma_sound soundInstance;
    ma_result result;

    auto cache_it = m_decodedSoundsCache.find(filePath);
    if (cache_it != m_decodedSoundsCache.end()) {
        result = ma_sound_init_copy(&m_engine, &(cache_it->second), 0, nullptr, &soundInstance);
        if (result == MA_SUCCESS) {
            aeStdOut("Playing sound (for duration) from cache: " + filePath + " for object: " + objectId);
        } else {
            aeStdOut("Failed to copy sound (for duration) from cache: " + filePath + ". Error: " + ma_result_description(result) + ". Loading from file.");
        }
    }

    if (result != MA_SUCCESS) {
        result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &soundInstance);
        if (result != MA_SUCCESS) {
            aeStdOut("Failed to load sound file (for duration): " + filePath + " for object: " + objectId + ". Error: " + ma_result_description(result));
            return;
        }
        aeStdOut("Playing sound (for duration) loaded from file: " + filePath + " for object: " + objectId);
    }

    // durationSeconds 만큼 재생 후 멈추도록 설정
    if (durationSeconds > 0.0) {
        ma_uint64 stopTimeMs = static_cast<ma_uint64>(durationSeconds * 1000.0);
        ma_sound_set_stop_time_in_milliseconds(&soundInstance, stopTimeMs);
        aeStdOut("Sound '" + filePath + "' for object '" + objectId + "' will play for " + std::to_string(durationSeconds) + "s (stop time: " + std::to_string(stopTimeMs) + "ms)");
    }

    ma_sound_set_looping(&soundInstance, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_pitch(&soundInstance, m_globalPlaybackSpeed); // 전역 재생 속도 적용
    ma_sound_set_volume(&soundInstance, initialVolume);
    ma_sound_start(&soundInstance);

    m_activeSounds[objectId] = soundInstance; // 맵에 저장 (새로 추가 또는 덮어쓰기)
    aeStdOut("Sound started: " + filePath + " for object: " + objectId);
}
void AudioEngineHelper::playSoundFromTo(const std::string& objectId, const std::string& filePath, double startTimeSeconds, double endTimeSeconds, bool loop = false, float initialVolume = 1.0f){
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

    ma_sound soundInstance;
    ma_result result;

    auto cache_it = m_decodedSoundsCache.find(filePath);
    if (cache_it != m_decodedSoundsCache.end()) {
        result = ma_sound_init_copy(&m_engine, &(cache_it->second), 0, nullptr, &soundInstance);
        if (result == MA_SUCCESS) {
            aeStdOut("Playing sound (from-to) from cache: " + filePath + " for object: " + objectId);
        } else {
            aeStdOut("Failed to copy sound (from-to) from cache: " + filePath + ". Error: " + ma_result_description(result) + ". Loading from file.");
        }
    }

    if (result != MA_SUCCESS) {
        result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &soundInstance);
        if (result != MA_SUCCESS) {
            aeStdOut("Failed to load sound file (from-to): " + filePath + " for object: " + objectId + ". Error: " + ma_result_description(result));
            return;
        }
        aeStdOut("Playing sound (from-to) loaded from file: " + filePath + " for object: " + objectId);
    }

    // 1. 재생 시작 시간으로 이동
    if (startTimeSeconds > 0.0) {
        ma_sound_seek_to_second(&soundInstance, static_cast<float>(startTimeSeconds));
        aeStdOut("Sound '" + filePath + "' seeked to " + std::to_string(startTimeSeconds) + "s for playFromTo.");
    }

    // 2. 재생 종료 시간 설정 (startTimeSeconds 이후부터 endTimeSeconds까지 재생)
    if (endTimeSeconds > startTimeSeconds) {
        ma_uint64 stopTimeMs = static_cast<ma_uint64>(endTimeSeconds * 1000.0);
        ma_sound_set_stop_time_in_milliseconds(&soundInstance, stopTimeMs);
        aeStdOut("Sound '" + filePath + "' stop time set to " + std::to_string(stopTimeMs) + "ms for playFromTo.");
    } // endTimeSeconds <= startTimeSeconds 이면, startTimeSeconds부터 끝까지 재생됩니다.

    ma_sound_set_looping(&soundInstance, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&soundInstance, initialVolume);
    ma_sound_set_pitch(&soundInstance, m_globalPlaybackSpeed); // 전역 재생 속도 적용
    ma_sound_start(&soundInstance);

    m_activeSounds[objectId] = soundInstance; // 맵에 저장 (새로 추가 또는 덮어쓰기)
    aeStdOut("Sound started: " + filePath + " for object: " + objectId);
}
bool AudioEngineHelper::isSoundPlaying(const std::string& objectId) const {
    if (!m_engineInitialized) return false;

    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end())
    {
        return ma_sound_is_playing(const_cast<ma_sound*>(&it->second)) == MA_TRUE;
    }
    return false;
}
double AudioEngineHelper::getSoundFileDuration(const std::string& filePath)const{
    if (!m_engineInitialized)
    {
        aeStdOut("Engine not initalized. Cannot get Sound Duration for:"+filePath);
        return 0.0;
    }
    ma_decoder dec;
    ma_decoder_config conf = ma_decoder_config_init_default();

    ma_result result = ma_decoder_init_file(filePath.c_str(),&conf,&dec);
    if (result != MA_SUCCESS) {
        aeStdOut("Failed to initialize decoder for duration check: " + filePath + ". Error: " + ma_result_description(result));
        return 0.0; // 디코더 초기화 실패 시 0.0 반환
    }

    ma_uint64 frame = 0;
    ma_uint32 sampleRate = dec.outputSampleRate;
    double sec =0;
    ma_decoder_get_length_in_pcm_frames(&dec,&frame);

    if (sampleRate > 0)
    {
        sec = static_cast<double>(frame)/sampleRate;
    }

    ma_decoder_uninit(&dec); // 사용한 디코더 해제
    return sec;
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
    ma_sound_set_pitch(&m_backgroundMusic, m_globalPlaybackSpeed); // 전역 재생 속도 적용
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

void AudioEngineHelper::setGlobalPlaybackSpeed(float speed) {
    if (!m_engineInitialized) return;
    if (speed <= 0.0f) { // 속도는 0보다 커야 합니다.
        aeStdOut("Invalid playback speed: " + std::to_string(speed) + ". Speed must be positive.");
        return;
    }
    m_globalPlaybackSpeed = speed;
    aeStdOut("Global playback speed set to: " + std::to_string(m_globalPlaybackSpeed));

    // 모든 활성 효과음에 적용
    for (auto& pair : m_activeSounds) {
        ma_sound_set_pitch(&(pair.second), m_globalPlaybackSpeed);
    }
    // 배경음악에 적용
    if (m_backgroundMusicInitialized) {
        ma_sound_set_pitch(&m_backgroundMusic, m_globalPlaybackSpeed);
    }
}

float AudioEngineHelper::getGlobalPlaybackSpeed() const {
    return m_globalPlaybackSpeed;
}
void AudioEngineHelper::aeStdOut(const std::string &message) const {
    std::string TAG = "[Hibiki] ";
    std::string lm = TAG + message;
    printf(lm.c_str());
    printf("\n");
    logger.log(lm);
}