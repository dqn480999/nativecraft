#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "assets/Assets.hpp"
#include "audio/Audio.hpp"
#include "game/Player.hpp"
#include "render/Environment.hpp"
#include "render/Hud.hpp"
#include "render/Particles.hpp"
#include "render/PlayerRenderer.hpp"
#include "render/ReflectionTarget.hpp"
#include "render/SkyRenderer.hpp"
#include "render/TextureArray.hpp"
#include "render/WorldRenderer.hpp"
#include "world/Blocks.hpp"
#include "world/World.hpp"

namespace nc {

struct ItemStack {
    int blockId = 0;
    int count = 0;
};

struct PrimedTnt {
    BlockPos block{};
    float fuse = 2.5f;
    float emitTimer = 0.0f;
};

struct CraftingRecipe {
    int outputId = 0;
    int outputCount = 1;
    std::vector<std::pair<int, int>> inputs;
};

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
    bool thirdPerson = false;
    bool flashlight = false;
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
    PlayerRenderState buildPlayerRenderState() const;
    void primeTnt(const BlockPos& block);
    void updateTnt(float deltaTime);
    void explodeTnt(const BlockPos& block);
    void updateParticles(float deltaTime);
    void updateSurvival(float deltaTime);
    void applyDamage(float amount);
    void respawn();
    bool addItem(int blockId, int count);
    bool removeItems(int blockId, int count);
    int itemCount(int blockId) const;
    bool craftRecipe(int index);
    std::array<float, 4> inventoryRecipeRect(int index, int width, int height) const;
    int inventoryRecipeAt(int mouseX, int mouseY, int width, int height) const;
    float random01();
    void toggleInventory();
    void closeInventory();
    void handleInventoryClick(int mouseX, int mouseY, int button);
    void drawInventory(int width, int height);
    std::array<float, 4> inventorySlotRect(int index, int width, int height) const;
    int inventorySlotAt(int mouseX, int mouseY, int width, int height) const;
    ItemStack* inventoryStackAt(int index);
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
    PlayerRenderer playerRenderer_;
    ParticleRenderer particleRenderer_;
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
    bool thirdPerson_ = false;
    bool flashlightOn_ = false;
    float walkPhase_ = 0.0f;
    float walkAmount_ = 0.0f;
    float attackSwing_ = 0.0f;
    double birdTimer_ = 7.0;
    std::mt19937 rng_{0x5EED1234};
    std::vector<Particle> particles_;
    std::vector<PrimedTnt> primedTnt_;
    Vec3 explosionFlashPosition_{};
    float explosionFlashTime_ = 0.0f;
    float health_ = 20.0f;
    float hunger_ = 20.0f;
    float oxygen_ = 10.0f;
    bool dead_ = false;
    float damageFlashTime_ = 0.0f;
    bool wasOnGround_ = true;
    float fallStartY_ = 0.0f;
    Vec3 spawnPosition_{};
    std::vector<CraftingRecipe> recipes_;
    ChunkPos lastStreamCenter_{};
    bool hasStreamCenter_ = false;

    std::vector<ItemStack> hotbar_;
    std::vector<ItemStack> storage_;
    ItemStack heldStack_{};
    bool inventoryOpen_ = false;
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
