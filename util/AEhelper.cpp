#include "AEhelper.h"
#include "miniaudio.h"
#include <stdexcept>
ma_engine engine;
ma_context context;
ma_device device;
ma_sound soundEffect;
ma_sound backgroundMusic;
AudioEngineHelper::AudioEngineHelper():logger("hibiki.log")
{
    aeStdOut("Audio Engine Helper started");
    ma_backend backend;
    ma_uint32 backendCount;
    ma_device_config deviceConfig;
    ma_context_config contextConfig;
    ma_result result;
    // Initialize the context
    contextConfig = ma_context_config_init();
    result = ma_context_init(&backend,backendCount,&contextConfig, &context);
    if (result != MA_SUCCESS)
    {
        throw std::runtime_error("Failed to initialize audio context");
    }else{
        aeStdOut("Audio context initialized successfully");
    }

    // Initialize the device
    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate = 44100;
    deviceConfig.dataCallback = nullptr; // Set your callback function here
    deviceConfig.pUserData = nullptr;    // Set your user data here

    result = ma_device_init(&context, &deviceConfig, &device);
    if (result != MA_SUCCESS)
    {
        ma_context_uninit(&context);
        throw std::runtime_error("Failed to initialize audio device");
    }else{
        aeStdOut("Audio device initialized successfully");
    }
    ma_engine_config engineConfig;
    engineConfig = ma_engine_config_init();
    ma_result initEngineResult = ma_engine_init(&engineConfig, &engine);
    if (initEngineResult != MA_SUCCESS)
    {
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        throw std::runtime_error("Failed to initialize audio engine");
    }
    else
    {
        aeStdOut("Audio engine initialized successfully");
    }
    ma_engine_start(&engine);
}
AudioEngineHelper::~AudioEngineHelper()
{
    // Destructor implementation
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    aeStdOut("Audio device and context uninitialized");
    ma_engine_stop(&engine);
    ma_engine_uninit(&engine);
    aeStdOut("Audio engine uninitialized");
}
void AudioEngineHelper::playSoundEffect(const std::string &filePath)
{
    // Load and play sound effect
    ma_result result;
    result = ma_sound_init_from_file(&engine, filePath.c_str(), 0, nullptr, nullptr, &soundEffect);
    if (result != MA_SUCCESS)
    {
        aeStdOut("Failed to load sound effect");
        return;
    }
    ma_sound_start(&soundEffect);
}
void AudioEngineHelper::stopSoundEffect()
{
    // Stop sound effect
    ma_sound_stop(&soundEffect);
}
unsigned int AudioEngineHelper::getPlayingsoundEntity() const
{
    // 'engine'은 AEhelper.cpp에 선언된 전역 ma_engine 인스턴스입니다.
    if (engine.pResourceManager == nullptr) {
        throw std::runtime_error("Resource manager is not initialized");
        return 0;
    }
    ma_resource_manager* pRM = engine.pResourceManager;
    unsigned int playingCount = 0;
    if (ma_sound_is_playing(&soundEffect)) {
        playingCount++;
    }else{
        if (playingCount>0) // Prevent negative count
        {
            playingCount--;
        } 
    }
    if (ma_sound_is_playing(&backgroundMusic)) {
        playingCount++;
    }else{
        if (playingCount>0)
        {
            playingCount--;
        } 
    }
    
    return playingCount;
}
void AudioEngineHelper::aeStdOut(const std::string &message)
{
    string TAG = "[HIBIKI] ";
    string lm = TAG + message;
    printf(lm.c_str());
    logger.log(lm);
}
void AudioEngineHelper::setVolume(float volume)
{
    // Set volume for sound effect
    ma_sound_set_volume(&soundEffect, volume);
    // Set volume for background music
    ma_sound_set_volume(&backgroundMusic, volume);
}
float AudioEngineHelper::getVolume() const
{
    // Get volume for sound effect
    float soundEffectVolume = ma_sound_get_volume(&soundEffect);
    // Get volume for background music
    float backgroundMusicVolume = ma_sound_get_volume(&backgroundMusic);
    return (soundEffectVolume + backgroundMusicVolume) / 2.0f; // Return average volume
}