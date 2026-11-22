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

private:
    ALuint loadBuffer(const std::string& assetName);

    Assets* assets_ = nullptr;
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    std::map<std::string, ALuint> buffers_;
    std::vector<ALuint> sources_;
    std::size_t nextSource_ = 0;
};

}
