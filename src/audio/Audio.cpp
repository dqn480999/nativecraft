#include "audio/Audio.hpp"

#include <AL/alc.h>
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Log.hpp"

namespace nc {
namespace {

void setError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

}

bool Audio::init(Assets& assets, std::string* error) {
    assets_ = &assets;
    device_ = alcOpenDevice(nullptr);
    if (!device_) {
        setError(error, "OpenAL: cannot open audio device");
        return false;
    }
    context_ = alcCreateContext(device_, nullptr);
    if (!context_ || alcMakeContextCurrent(context_) == ALC_FALSE) {
        setError(error, "OpenAL: cannot create context");
        if (context_) {
            alcDestroyContext(context_);
            context_ = nullptr;
        }
        alcCloseDevice(device_);
        device_ = nullptr;
        return false;
    }

    alDistanceModel(AL_NONE);
    sources_.resize(8);
    alGenSources(static_cast<ALsizei>(sources_.size()), sources_.data());
    logInfo("audio initialized (OpenAL)");
    return true;
}

void Audio::shutdown() {
    if (!context_) {
        return;
    }
    for (ALuint source : sources_) {
        alSourceStop(source);
    }
    if (!sources_.empty()) {
        alDeleteSources(static_cast<ALsizei>(sources_.size()), sources_.data());
        sources_.clear();
    }
    if (musicSource_) {
        alSourceStop(musicSource_);
        alDeleteSources(1, &musicSource_);
        musicSource_ = 0;
    }
    if (waterSource_) {
        alSourceStop(waterSource_);
        alDeleteSources(1, &waterSource_);
        waterSource_ = 0;
    }
    if (birdBuffer_) {
        alDeleteBuffers(1, &birdBuffer_);
        birdBuffer_ = 0;
    }
    for (auto& [name, buffer] : buffers_) {
        (void)name;
        alDeleteBuffers(1, &buffer);
    }
    buffers_.clear();
    currentMusic_.clear();
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(context_);
    context_ = nullptr;
    if (device_) {
        alcCloseDevice(device_);
        device_ = nullptr;
    }
}

ALuint Audio::loadBuffer(const std::string& assetName) {
    const auto cached = buffers_.find(assetName);
    if (cached != buffers_.end()) {
        return cached->second;
    }
    if (!assets_) {
        return 0;
    }
    const auto path = assets_->soundPath(assetName);
    if (!path) {
        logWarn("sound not found in assets: %s", assetName.c_str());
        buffers_[assetName] = 0;
        return 0;
    }

    OggVorbis_File vorbis{};
    if (ov_fopen(path->c_str(), &vorbis) != 0) {
        logWarn("cannot open ogg: %s", path->c_str());
        buffers_[assetName] = 0;
        return 0;
    }

    vorbis_info* info = ov_info(&vorbis, -1);
    if (!info) {
        ov_clear(&vorbis);
        buffers_[assetName] = 0;
        return 0;
    }
    const int channels = info->channels;
    const long sampleRate = info->rate;

    std::vector<char> pcm;
    char chunk[4096];
    int bitstream = 0;
    while (true) {
        const long read = ov_read(&vorbis, chunk, static_cast<int>(sizeof(chunk)), 0, 2, 1,
                                  &bitstream);
        if (read == 0) {
            break;
        }
        if (read < 0) {
            continue;
        }
        pcm.insert(pcm.end(), chunk, chunk + read);
    }
    ov_clear(&vorbis);

    if (pcm.empty()) {
        buffers_[assetName] = 0;
        return 0;
    }

    const ALenum format = channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, format, pcm.data(), static_cast<ALsizei>(pcm.size()),
                 static_cast<ALsizei>(sampleRate));
    buffers_[assetName] = buffer;
    return buffer;
}

void Audio::playMusic(const std::string& assetName, float gain) {
    if (!context_ || currentMusic_ == assetName) {
        return;
    }
    const ALuint buffer = loadBuffer(assetName);
    if (buffer == 0) {
        return;
    }
    if (!musicSource_) {
        alGenSources(1, &musicSource_);
    }
    alSourceStop(musicSource_);
    alSourcei(musicSource_, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(musicSource_, AL_GAIN, gain);
    alSourcei(musicSource_, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(musicSource_, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcei(musicSource_, AL_LOOPING, AL_TRUE);
    alSourcePlay(musicSource_);
    currentMusic_ = assetName;
}

void Audio::setWaterLoop(bool active, float gain) {
    if (!context_) {
        return;
    }
    if (!active) {
        if (waterSource_) {
            alSourceStop(waterSource_);
        }
        return;
    }
    if (!waterSource_) {
        const ALuint buffer = loadBuffer("minecraft/sounds/liquid/water.ogg");
        if (buffer == 0) {
            return;
        }
        alGenSources(1, &waterSource_);
        alSourcei(waterSource_, AL_BUFFER, static_cast<ALint>(buffer));
        alSourcei(waterSource_, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(waterSource_, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcei(waterSource_, AL_LOOPING, AL_TRUE);
        alSourcef(waterSource_, AL_GAIN, gain);
        alSourcePlay(waterSource_);
    } else {
        ALint state = AL_STOPPED;
        alGetSourcei(waterSource_, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcePlay(waterSource_);
        }
        alSourcef(waterSource_, AL_GAIN, gain);
    }
}

void Audio::ensureBirdBuffer() {
    if (birdBuffer_ != 0 || !context_) {
        return;
    }
    constexpr int sampleRate = 44100;
    constexpr int sampleCount = sampleRate / 3;
    constexpr float pi = 3.14159265358979323846f;
    std::vector<std::int16_t> pcm(static_cast<std::size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        const float t = static_cast<float>(i) / sampleRate;
        const float progress = static_cast<float>(i) / sampleCount;
        const float envelope = std::sin(pi * std::min(progress * 3.0f, 1.0f));
        const float frequency = 1900.0f + 700.0f * std::sin(2.0f * pi * 3.2f * t);
        const float sample = (std::sin(2.0f * pi * frequency * t)
                              + 0.35f * std::sin(2.0f * pi * frequency * 2.01f * t))
                           * envelope * 0.22f;
        pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(
            std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
    }
    alGenBuffers(1, &birdBuffer_);
    alBufferData(birdBuffer_, AL_FORMAT_MONO16, pcm.data(),
                 static_cast<ALsizei>(pcm.size() * sizeof(std::int16_t)), sampleRate);
}

void Audio::playBuffer(ALuint buffer, float gain, float pitch) {
    if (!context_ || buffer == 0 || sources_.empty()) {
        return;
    }
    std::size_t index = nextSource_;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        const std::size_t candidate = (nextSource_ + i) % sources_.size();
        ALint state = AL_STOPPED;
        alGetSourcei(sources_[candidate], AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            index = candidate;
            break;
        }
    }
    nextSource_ = (index + 1) % sources_.size();
    const ALuint source = sources_[index];
    alSourceStop(source);
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, gain);
    alSourcef(source, AL_PITCH, pitch);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcei(source, AL_LOOPING, AL_FALSE);
    alSourcePlay(source);
}

void Audio::playBirdChirp(float gain) {
    ensureBirdBuffer();
    if (!context_ || birdBuffer_ == 0) {
        return;
    }
    const float pitch = 0.9f + static_cast<float>(birdPitchIndex_ % 5) * 0.08f;
    ++birdPitchIndex_;
    playBuffer(birdBuffer_, gain, pitch);
}

void Audio::playSound(const std::string& assetName, float gain) {
    if (!context_ || sources_.empty()) {
        return;
    }
    const ALuint buffer = loadBuffer(assetName);
    if (buffer == 0) {
        return;
    }

    std::size_t index = nextSource_;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        const std::size_t candidate = (nextSource_ + i) % sources_.size();
        ALint state = AL_STOPPED;
        alGetSourcei(sources_[candidate], AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            index = candidate;
            break;
        }
    }
    nextSource_ = (index + 1) % sources_.size();

    const ALuint source = sources_[index];
    alSourceStop(source);
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, gain);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcei(source, AL_LOOPING, AL_FALSE);
    alSourcePlay(source);
}

}
