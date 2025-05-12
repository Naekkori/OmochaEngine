#include <string>
using namespace std;
#include "Logger.h"
class AudioEngineHelper
{
private:
SimpleLogger logger;
void aeStdOut(const std::string &message);
public:
    AudioEngineHelper();
    ~AudioEngineHelper();
    void playSoundEffect(const string& filePath);
    unsigned int getPlayingsoundEntity() const;
    void stopSoundEffect();
    void playBackgroundMusic(const string& filePath);
    void stopBackgroundMusic();
    void setBackgroundVolume(float volume);
    float getBackgroundVolume() const;
};