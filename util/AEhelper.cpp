#include "AEhelper.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdexcept>
#include <chrono>
#include <iostream> // 프로그레스 바 출력을 위해 추가
#include <condition_variable>
AudioEngineHelper::AudioEngineHelper() : logger("hibiki.log"), m_globalPlaybackSpeed(1.0f)
{
    aeStdOut("Audio Engine Helper initializing...");
    ma_result result;
    result = ma_engine_init(NULL, &m_engine);
    if (result != MA_SUCCESS)
    {
        throw std::runtime_error("Failed to initialize audio engine (high-level): " + std::string(ma_result_description(result)));
    }
    m_engineInitialized = true;
    aeStdOut("Audio engine initialized successfully (high-level)");

    // It's good practice to ensure the engine is started, though ma_engine_init often handles this.
    result = ma_engine_start(&m_engine);
    if (result != MA_SUCCESS) {
        ma_engine_uninit(&m_engine); // Clean up if start fails
        m_engineInitialized = false;
        throw std::runtime_error("Failed to start audio engine: " + std::string(ma_result_description(result)));
    }
    aeStdOut("Audio engine started successfully");
    setGlobalVolume(1.0f);
}

AudioEngineHelper::~AudioEngineHelper()
{
    aeStdOut("Audio Engine Helper uninitializing...");
    // 엔진이 중지된 후 사운드 리소스를 정리합니다.
    stopAllSounds(); // 모든 활성 사운드 정리 (내부적으로 배경음악도 처리 시도)
    if (m_backgroundMusicInitialized)
    { // stopAllSounds에서 처리되지 않았을 경우를 대비
        aeStdOut("Uninitializing background music.");
        uninitializeSound(&m_backgroundMusic);
        m_backgroundMusicInitialized = false;
    }
    clearPreloadedSounds(); // 미리 로딩된 사운드 해제 (프로그레스 바 포함)

    // 엔진 및 디바이스, 컨텍스트 해제 순서 중요
    // 엔진 해제
    if (m_engineInitialized)
    {
        ma_engine_uninit(&m_engine);
        aeStdOut("Audio engine uninitialized");
        m_engineInitialized = false;
    }
}

void AudioEngineHelper::uninitializeSound(ma_sound *pSound)
{
    if (pSound)
    { // 초기화되었는지 확인
        ma_sound_stop(pSound);
        ma_sound_uninit(pSound);
    }
}

void AudioEngineHelper::preloadSound(const std::string &filePath)
{
    if (!m_engineInitialized)
    {
        aeStdOut("Engine not initialized. Cannot preload sound: " + filePath);
        return;
    }
    if (m_decodedSoundsCache.count(filePath))
    {
        aeStdOut("Sound already preloaded: " + filePath);
        return;
    }

    // ma_sound 객체를 힙에 할당합니다.
    ma_sound *pSoundToCache = new (std::nothrow) ma_sound();
    if (!pSoundToCache)
    {
        aeStdOut("Failed to allocate memory for sound: " + filePath);
        return;
    }

    ma_result result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, pSoundToCache);

    if (result != MA_SUCCESS)
    {
        aeStdOut("Failed to preload sound file: " + filePath + ". Error: " + ma_result_description(result));
        delete pSoundToCache; // 할당된 메모리 정리
    }
    else
    {
        m_decodedSoundsCache[filePath] = pSoundToCache; // 맵에 포인터 저장
        aeStdOut("Sound preloaded successfully: " + filePath);
    }
}

void AudioEngineHelper::clearPreloadedSounds()
{
    if (!m_engineInitialized)
        return;
    if (m_decodedSoundsCache.empty())
        return;

    size_t totalSounds = m_decodedSoundsCache.size();
    aeStdOut("Clearing all preloaded sounds (" + std::to_string(totalSounds) + " sounds)...");
    size_t soundsCleared = 0;

    for (auto &pair : m_decodedSoundsCache)
    {
        // pair.second는 이제 ma_sound* 입니다.
        ma_sound_uninit(pair.second);
        aeStdOut("Deleted audio in Memory : "+pair.first);
        delete pair.second; // 힙에 할당된 ma_sound 객체 해제
        soundsCleared++;
    }
    aeStdOut(std::to_string(soundsCleared)+" preloaded sounds cleared.");
}
void AudioEngineHelper::playSound(const std::string &objectId, const std::string &filePath, bool loop, float initialVolume)
{
    if (!m_engineInitialized)
    {
        aeStdOut("Engine not initialized. Cannot play sound for object: " + objectId);
        return;
    }

    // 해당 objectId로 이미 재생 중인 사운드가 있다면 정지하고 해제
    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end())
    {
        ma_sound* pOldSound = it->second;
        // Removed problematic path logging. The objectId is sufficient for identification.
        aeStdOut("Found existing sound for object: " + objectId + ". Attempting to stop it.");

        if (ma_sound_is_playing(pOldSound) == MA_TRUE) {
            aeStdOut("Old sound for " + objectId + " IS PLAYING before stop command.");
        } else {
            aeStdOut("Old sound for " + objectId + " is NOT PLAYING before stop command. At end: " + std::string(ma_sound_at_end(pOldSound) ? "true" : "false"));
        }

        // ma_sound_stop(pOldSound); // uninitializeSound 내부에서 이미 호출됨
        // if (stop_result != MA_SUCCESS) {
        //     aeStdOut("WARNING: ma_sound_stop() failed for " + objectId + " with error: " + ma_result_description(stop_result));
        // }

        // if (ma_sound_is_playing(pOldSound) == MA_TRUE) { // Check immediately after stop
        //     aeStdOut("WARNING: Old sound for " + objectId + " STILL reported as PLAYING after ma_sound_stop(). At end: " + std::string(ma_sound_at_end(pOldSound) ? "true" : "false"));
        // } else {
        //     aeStdOut("Old sound for " + objectId + " successfully stopped (is_playing is false after stop command).");
        // }
        
        aeStdOut("Uninitializing and deleting old sound for object: " + objectId);
        uninitializeSound(pOldSound); // Calls ma_sound_stop and ma_sound_uninit
        delete pOldSound;             // Then delete the ma_sound structure
        m_activeSounds.erase(it);     // Finally, remove from map
        aeStdOut("Old sound for " + objectId + " fully cleaned up from m_activeSounds.");
    }
    ma_sound *pSoundInstance = new (std::nothrow) ma_sound();
    if (!pSoundInstance)
    {
        aeStdOut("Failed to allocate memory for sound instance: " + objectId);
        return;
    }

    ma_result result = MA_ERROR; // Initialize to a non-success state

    auto cache_it = m_decodedSoundsCache.find(filePath);
    if (cache_it != m_decodedSoundsCache.end()) // Check if found in cache
    {
        // 캐시에서 발견됨, 복사하여 사용
        // cache_it->second는 ma_sound*
        result = ma_sound_init_copy(&m_engine, cache_it->second, 0, nullptr, pSoundInstance);
        if (result == MA_SUCCESS)
        {
            aeStdOut("Playing sound from cache: " + filePath + " for object: " + objectId);
        }
        else
        {
            aeStdOut("Failed to copy sound from cache: " + filePath + ". Error: " + ma_result_description(result) + ". Loading from file instead.");
            // result is already non-MA_SUCCESS, so it will fall through to load from file below
        }
    }

    // If not found in cache (result is still MA_ERROR) or if cache copy failed (result is non-MA_SUCCESS)
    if (result != MA_SUCCESS)
    { // 캐시에 없거나 복사 실패 시 파일에서 직접 로드
        // MA_SOUND_FLAG_DECODE: 미리 디코딩하여 메모리에 로드 (짧은 효과음에 적합)
        result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, pSoundInstance);
        if (result != MA_SUCCESS)
        {
            aeStdOut("Failed to load sound file: " + filePath + " for object: " + objectId + ". Error: " + ma_result_description(result));
            delete pSoundInstance; // 할당된 메모리 정리
            return;
        }
        aeStdOut("Playing sound loaded from file (not cached or cache copy failed): " + filePath + " for object: " + objectId);
    }

    ma_sound_set_looping(pSoundInstance, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(pSoundInstance, initialVolume);
    ma_sound_set_pitch(pSoundInstance, m_globalPlaybackSpeed); // 전역 재생 속도 적용

    float current_engine_volume_debug = ma_engine_get_volume(&m_engine);
    aeStdOut("Sound '" + filePath + "' for object '" + objectId + "' PRE-START details: EngineVol=" + std::to_string(current_engine_volume_debug) +
             ", InstanceVol=" + std::to_string(initialVolume) + // initialVolume is what was set by ma_sound_set_volume
             ", InstancePitch=" + std::to_string(m_globalPlaybackSpeed)); // m_globalPlaybackSpeed is what was set by ma_sound_set_pitch

    ma_sound_start(pSoundInstance);
    // Add a post-start check
    if (ma_sound_is_playing(pSoundInstance) == MA_FALSE) {
        aeStdOut("WARNING: Sound '" + filePath + "' for object '" + objectId + "' reported as NOT PLAYING immediately after ma_sound_start. At end: " + (ma_sound_at_end(pSoundInstance) ? "true" : "false"));
    }

    m_activeSounds[objectId] = pSoundInstance; // 맵에 포인터 저장
    aeStdOut("Sound started: " + filePath + " for object: " + objectId);
}
void AudioEngineHelper::playSoundForDuration(const std::string &objectId, const std::string &filePath, double durationSeconds, bool loop, float initialVolume)
{
    if (!m_engineInitialized)
    {
        aeStdOut("Engine not initialized. Cannot play sound for object: " + objectId);
        return;
    }

    // 해당 objectId로 이미 재생 중인 사운드가 있다면 정지하고 해제
    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end())
    {
        aeStdOut("Stopping and uninitializing existing sound for object: " + objectId);
        uninitializeSound(it->second);
        delete it->second;
        m_activeSounds.erase(it);
    }

    ma_sound *pSoundInstance = new (std::nothrow) ma_sound();
    if (!pSoundInstance)
    {
        aeStdOut("Failed to allocate memory for sound instance (for duration): " + objectId);
        return;
    }

    ma_result result = MA_ERROR; // Initialize to a non-success state

    auto cache_it = m_decodedSoundsCache.find(filePath);
    if (cache_it != m_decodedSoundsCache.end()) // Check if found in cache
    {
        result = ma_sound_init_copy(&m_engine, cache_it->second, 0, nullptr, pSoundInstance);
        if (result == MA_SUCCESS)
        {
            aeStdOut("Playing sound (for duration) from cache: " + filePath + " for object: " + objectId);
        }
        else
        {
            aeStdOut("Failed to copy sound (for duration) from cache: " + filePath + ". Error: " + ma_result_description(result) + ". Loading from file.");
        }
    }

    // If not found in cache (result is still MA_ERROR) or if cache copy failed (result is non-MA_SUCCESS)
    if (result != MA_SUCCESS) 
    {
        result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, pSoundInstance);
        if (result != MA_SUCCESS)
        {
            aeStdOut("Failed to load sound file (for duration): " + filePath + " for object: " + objectId + ". Error: " + ma_result_description(result));
            delete pSoundInstance;
            return;
        }
        aeStdOut("Playing sound (for duration) loaded from file: " + filePath + " for object: " + objectId);
    }

    if (durationSeconds > 0.0)
    {
        ma_uint64 stopTimeMs = static_cast<ma_uint64>(durationSeconds * 1000.0);
        ma_sound_set_stop_time_in_milliseconds(pSoundInstance, stopTimeMs);
        aeStdOut("Sound '" + filePath + "' for object '" + objectId + "' will play for " + std::to_string(durationSeconds) + "s (stop time: " + std::to_string(stopTimeMs) + "ms)");
    }

    ma_sound_set_looping(pSoundInstance, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_pitch(pSoundInstance, m_globalPlaybackSpeed);
    ma_sound_set_volume(pSoundInstance, initialVolume);
    ma_sound_start(pSoundInstance);

    m_activeSounds[objectId] = pSoundInstance;
    aeStdOut("Sound started: " + filePath + " for object: " + objectId);
}
void AudioEngineHelper::playSoundFromTo(const std::string &objectId, const std::string &filePath, double startTimeSeconds, double endTimeSeconds, bool loop, float initialVolume)
{
    if (!m_engineInitialized)
    {
        aeStdOut("Engine not initialized. Cannot play sound for object: " + objectId);
        return;
    }

    // 해당 objectId로 이미 재생 중인 사운드가 있다면 정지하고 해제
    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end())
    {
        aeStdOut("Stopping and uninitializing existing sound for object: " + objectId);
        uninitializeSound(it->second);
        delete it->second;
        m_activeSounds.erase(it);
    }

    ma_sound *pSoundInstance = new (std::nothrow) ma_sound();
    if (!pSoundInstance)
    {
        aeStdOut("Failed to allocate memory for sound instance (from-to): " + objectId);
        return;
    }

    ma_result result = MA_ERROR; // Initialize to a non-success state

    auto cache_it = m_decodedSoundsCache.find(filePath);
    if (cache_it != m_decodedSoundsCache.end()) // Check if found in cache
    {
        result = ma_sound_init_copy(&m_engine, cache_it->second, 0, nullptr, pSoundInstance);
        if (result == MA_SUCCESS)
        {
            aeStdOut("Playing sound (from-to) from cache: " + filePath + " for object: " + objectId);
        }
        else
        {
            aeStdOut("Failed to copy sound (from-to) from cache: " + filePath + ". Error: " + ma_result_description(result) + ". Loading from file.");
        }
    }

    // If not found in cache (result is still MA_ERROR) or if cache copy failed (result is non-MA_SUCCESS)
    if (result != MA_SUCCESS) 
    {
        result = ma_sound_init_from_file(&m_engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, pSoundInstance);
        if (result != MA_SUCCESS)
        {
            aeStdOut("Failed to load sound file (from-to): " + filePath + " for object: " + objectId + ". Error: " + ma_result_description(result));
            delete pSoundInstance;
            return;
        }
        aeStdOut("Playing sound (from-to) loaded from file: " + filePath + " for object: " + objectId);
    }

    if (startTimeSeconds > 0.0)
    {
        ma_sound_seek_to_second(pSoundInstance, static_cast<float>(startTimeSeconds));
        aeStdOut("Sound '" + filePath + "' seeked to " + std::to_string(startTimeSeconds) + "s for playFromTo.");
    }

    if (endTimeSeconds > startTimeSeconds)
    {
        ma_uint64 stopTimeMs = static_cast<ma_uint64>(endTimeSeconds * 1000.0);
        ma_sound_set_stop_time_in_milliseconds(pSoundInstance, stopTimeMs);
        aeStdOut("Sound '" + filePath + "' stop time set to " + std::to_string(stopTimeMs) + "ms for playFromTo.");
    }

    ma_sound_set_looping(pSoundInstance, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(pSoundInstance, initialVolume);
    ma_sound_set_pitch(pSoundInstance, m_globalPlaybackSpeed);
    ma_sound_start(pSoundInstance);

    m_activeSounds[objectId] = pSoundInstance;
    aeStdOut("Sound started: " + filePath + " for object: " + objectId);
}
bool AudioEngineHelper::isSoundPlaying(const std::string &objectId) const
{
    if (!m_engineInitialized)
        return false;

    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end())
    {
        return ma_sound_is_playing(it->second) == MA_TRUE; // it->second는 ma_sound*
    }
    return false;
}
double AudioEngineHelper::getSoundFileDuration(const std::string &filePath) const
{
    if (!m_engineInitialized)
    {
        aeStdOut("Engine not initalized. Cannot get Sound Duration for:" + filePath);
        return 0.0;
    }
    ma_decoder dec;
    ma_decoder_config conf = ma_decoder_config_init_default();

    ma_result result = ma_decoder_init_file(filePath.c_str(), &conf, &dec);
    if (result != MA_SUCCESS)
    {
        aeStdOut("Failed to initialize decoder for duration check: " + filePath + ". Error: " + ma_result_description(result));
        return 0.0; // 디코더 초기화 실패 시 0.0 반환
    }

    ma_uint64 frame = 0;
    ma_uint32 sampleRate = dec.outputSampleRate;
    double sec = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &frame);

    if (sampleRate > 0)
    {
        sec = static_cast<double>(frame) / sampleRate;
    }

    ma_decoder_uninit(&dec); // 사용한 디코더 해제
    return sec;
}
void AudioEngineHelper::stopSound(const std::string &objectId)
{
    if (!m_engineInitialized)
        return;

    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end())
    {
        aeStdOut("Stopping and uninitializing sound for object: " + objectId);
        uninitializeSound(it->second); // it->second는 ma_sound*
        delete it->second;             // 힙 메모리 해제
        m_activeSounds.erase(it);      // 맵에서 제거
    }
    else
    {
        aeStdOut("No sound found to stop for object: " + objectId);
    }
}

void AudioEngineHelper::stopAllSounds()
{
    if (!m_engineInitialized)
        return;
    aeStdOut("Stopping all sounds...");
    for (auto &pair : m_activeSounds)
    {
        uninitializeSound(pair.second); // pair.second는 ma_sound*
        delete pair.second;             // 힙 메모리 해제
    }
    // 배경음악도 중지
    if (m_backgroundMusicInitialized)
    {
        uninitializeSound(&m_backgroundMusic);
        m_backgroundMusicInitialized = false;
    }
    m_activeSounds.clear();
}

void AudioEngineHelper::stopAllSoundsExcept(const std::string &objectIdToKeepPlaying)
{
    if (!m_engineInitialized)
        return;
    aeStdOut("Stopping all sounds except for object: " + objectIdToKeepPlaying);

    // 직접 반복 중 제거는 복잡하므로, 제거할 ID 목록을 만들거나 새 맵을 만듭니다.
    // 여기서는 간단하게 반복하며 조건에 맞지 않는 것을 정지/해제하고, 나중에 맵에서 제거합니다.
    std::vector<std::string> idsToRemove;
    for (auto &pair : m_activeSounds)
    {
        if (pair.first != objectIdToKeepPlaying)
        {
            uninitializeSound(pair.second); // pair.second는 ma_sound*
            delete pair.second;             // 힙 메모리 해제
            idsToRemove.push_back(pair.first);
        }
    }
    for (const auto &id : idsToRemove)
    {
        m_activeSounds.erase(id);
    }
}

unsigned int AudioEngineHelper::getPlayingSoundCount() const
{
    if (!m_engineInitialized)
        return 0;

    unsigned int playingCount = 0;
    for (const auto &pair : m_activeSounds)
    {
        if (ma_sound_is_playing(pair.second)) // pair.second는 ma_sound*
        {
            playingCount++;
        }
    }
    return playingCount;
}

void AudioEngineHelper::setSoundVolume(const std::string &objectId, float volume)
{
    if (!m_engineInitialized)
        return;
    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end())
    {
        ma_sound_set_volume(it->second, volume); // it->second는 ma_sound*
    }
    else
    {
        aeStdOut("Cannot set volume: No sound found for object: " + objectId);
    }
}
float AudioEngineHelper::getGlobalVolume() {
    if (!m_engineInitialized) {
        aeStdOut("Audio engine not initialized. Cannot get global volume.");
        return 0.0f; // 엔진 미초기화 시 기본값 0.0f 반환
    }
    return ma_engine_get_volume(&m_engine); // ma_engine_get_volume이 반환하는 실제 볼륨 값을 반환
}

void AudioEngineHelper::setGlobalVolume(float volume)
{
    if (!m_engineInitialized)
        return;
    ma_engine_set_volume(&m_engine, volume);
    
}
float AudioEngineHelper::getSoundVolume(const std::string &objectId) const
{
    if (!m_engineInitialized)
        return 0.0f;
    auto it = m_activeSounds.find(objectId);
    if (it != m_activeSounds.end())
    {
        return ma_sound_get_volume(it->second); // it->second는 ma_sound*
    }
    aeStdOut("Cannot get volume: No sound found for object: " + objectId);
    return 0.0f; // 또는 오류 값 (예: -1.0f)
}

void AudioEngineHelper::setMasterVolume(float volume)
{
    if (!m_engineInitialized)
        return;
    ma_engine_set_volume(&m_engine, volume);
    aeStdOut("Master volume set to: " + std::to_string(volume));
}

void AudioEngineHelper::playBackgroundMusic(const std::string &filePath, bool loop, float initialVolume)
{
    if (!m_engineInitialized)
    {
        aeStdOut("Engine not initialized. Cannot play background music.");
        return;
    }

    // 기존 배경음악이 있다면 정지 및 해제
    if (m_backgroundMusicInitialized)
    {
        aeStdOut("Stopping and uninitializing existing background music.");
        uninitializeSound(&m_backgroundMusic);
        m_backgroundMusicInitialized = false;
    }

    ma_result result;
    // 배경음악은 스트리밍으로 로드하는 것이 일반적입니다 (MA_SOUND_FLAG_STREAM)
    // 필요에 따라 MA_SOUND_FLAG_NO_PITCH, MA_SOUND_FLAG_NO_SPATIALIZATION 플래그 사용 가능
    ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION;
    result = ma_sound_init_from_file(&m_engine, filePath.c_str(), flags, nullptr, nullptr, &m_backgroundMusic);

    if (result != MA_SUCCESS)
    {
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

void AudioEngineHelper::stopBackgroundMusic()
{
    if (!m_engineInitialized || !m_backgroundMusicInitialized)
        return;

    aeStdOut("Stopping background music.");
    uninitializeSound(&m_backgroundMusic);
    m_backgroundMusicInitialized = false;
}

void AudioEngineHelper::setBackgroundMusicVolume(float volume)
{
    if (!m_engineInitialized || !m_backgroundMusicInitialized)
        return;
    ma_sound_set_volume(&m_backgroundMusic, volume);
}

float AudioEngineHelper::getBackgroundMusicVolume() const
{
    if (!m_engineInitialized || !m_backgroundMusicInitialized)
        return 0.0f;
    return ma_sound_get_volume(const_cast<ma_sound *>(&m_backgroundMusic));
}

bool AudioEngineHelper::isBackgroundMusicPlaying() const
{
    if (!m_engineInitialized || !m_backgroundMusicInitialized)
        return false;
    return ma_sound_is_playing(const_cast<ma_sound *>(&m_backgroundMusic)) == MA_TRUE;
}

void AudioEngineHelper::setGlobalPlaybackSpeed(float speed)
{
    if (!m_engineInitialized)
        return;
    if (speed <= 0.0f)
    { // 속도는 0보다 커야 합니다.
        aeStdOut("Invalid playback speed: " + std::to_string(speed) + ". Speed must be positive.");
        return;
    }
    m_globalPlaybackSpeed = speed;
    aeStdOut("Global playback speed set to: " + std::to_string(m_globalPlaybackSpeed));

    // 모든 활성 효과음에 적용
    for (auto &pair : m_activeSounds)
    {
        ma_sound_set_pitch(pair.second, m_globalPlaybackSpeed); // pair.second는 ma_sound*
    }
    // 배경음악에 적용
    if (m_backgroundMusicInitialized)
    {
        ma_sound_set_pitch(&m_backgroundMusic, m_globalPlaybackSpeed);
    }
}

float AudioEngineHelper::getGlobalPlaybackSpeed() const
{
    return m_globalPlaybackSpeed;
}
void AudioEngineHelper::aeStdOut(const std::string &message) const
{
    std::string TAG = "[Hibiki] ";
    std::string lm = TAG + message;
    printf(lm.c_str());
    printf("\n");
    logger.log(lm);
}
