#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "assets/Assets.hpp"
#include "audio/Audio.hpp"
#include "game/Player.hpp"
#include "render/Environment.hpp"
#include "render/Hud.hpp"
#include "render/ReflectionTarget.hpp"
#include "render/SkyRenderer.hpp"
#include "render/TextureArray.hpp"
#include "render/WorldRenderer.hpp"
#include "world/Blocks.hpp"
#include "world/World.hpp"

namespace nc {

struct GameOptions {
    std::string minecraftDir;
    int width = 1280;
    int height = 720;
    std::uint64_t seed = 12345;
    std::string screenshotPath;
    std::string raytracePath;
    int screenshotFrames = 0;
    float timeOfDay = 0.32f;
    float timeSpeed = 0.0025f;
    int renderDistance = 6;
    int grassDistance = 3;
    int workerThreads = 0;
    bool hasCamera = false;
    Vec3 cameraPosition{};
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
};

class Game {
public:
    bool init(const GameOptions& options, std::string* error = nullptr);
    int run();
    void shutdown();

private:
    bool createWindow(std::string* error);
    void processEvents(bool& running);
    void update(float deltaTime);
    void render();
    void drawHud();
    void handleBreak();
    void handlePlace();
    void takeScreenshot(const std::string& path);
    void renderRaytrace(const std::string& path);
    int selectedBlockId() const;
    std::string selectedBlockName() const;

    GameOptions options_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;

    Assets assets_;
    BlockRegistry blocks_;
    TextureArray textures_;
    std::unique_ptr<World> world_;
    WorldRenderer worldRenderer_;
    SkyRenderer sky_;
    ReflectionTarget reflection_;
    Hud hud_;
    Audio audio_;
    Player player_;
    Environment environment_;

    float timeOfDay_ = 0.32f;
    float timeSpeed_ = 0.0025f;
    float elapsedTime_ = 0.0f;
    float waterLevel_ = 56.0f;
    bool timePaused_ = false;
    Vec3 windDirection_{0.8f, 0.0f, 0.6f};
    float windStrength_ = 0.24f;
    bool underwater_ = false;
    bool underwaterLava_ = false;
    double birdTimer_ = 7.0;
    ChunkPos lastStreamCenter_{};
    bool hasStreamCenter_ = false;

    std::vector<int> hotbar_;
    int selectedSlot_ = 0;
    bool mouseCaptured_ = false;
    bool screenshotMode_ = false;
    bool screenshotRequested_ = false;
    std::string screenshotRequestPath_;
    int screenshotFrameCounter_ = 0;
    int screenshotCounter_ = 0;

    std::uint64_t lastCounter_ = 0;
    int soundCounter_ = 0;
    int stepSoundCounter_ = 0;
    double stepAccumulator_ = 0.0;
    float fps_ = 0.0f;
    double fpsAccumulator_ = 0.0;
    int fpsFrames_ = 0;

};

}
