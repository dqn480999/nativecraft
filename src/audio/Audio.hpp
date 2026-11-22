#pragma once

#include <AL/al.h>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "assets/Assets.hpp"

struct ALCdevice;
struct ALCcontext;

namespace nc {

class Audio {
public:
    bool init(Assets& assets, std::string* error = nullptr);
    void shutdown();

    bool available() const { return context_ != nullptr; }
    void playSound(const std::string& assetName, float gain = 1.0f);
    void playMusic(const std::string& assetName, float gain = 0.12f);
    void setWaterLoop(bool active, float gain = 0.35f);
    void playBirdChirp(float gain = 0.2f);

private:
    ALuint loadBuffer(const std::string& assetName);

    Assets* assets_ = nullptr;
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    void playBuffer(ALuint buffer, float gain, float pitch);
    void ensureBirdBuffer();

    std::map<std::string, ALuint> buffers_;
    std::vector<ALuint> sources_;
    std::size_t nextSource_ = 0;
    ALuint musicSource_ = 0;
    ALuint waterSource_ = 0;
    ALuint birdBuffer_ = 0;
    std::string currentMusic_;
    int birdPitchIndex_ = 0;
};

}
