#include "game/Game.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_set>
#include <thread>
#include <vector>

#include "core/Log.hpp"
#include "render/GL.hpp"
#include "util/Image.hpp"

namespace nc {
namespace {

std::string blockMaterial(const std::string& name) {
    if (name.find("slab") != std::string::npos) {
        if (name.find("oak") != std::string::npos || name.find("wood") != std::string::npos) {
            return "wood";
        }
        return "stone";
    }
    if (name.find("grass") != std::string::npos || name == "dirt" || name == "coarse_dirt"
        || name == "grass_path" || name.find("leaves") != std::string::npos) {
        return "grass";
    }
    if (name.find("sand") != std::string::npos || name == "gravel") {
        return "sand";
    }
    if (name.find("planks") != std::string::npos || name.find("log") != std::string::npos
        || name == "bookshelf" || name == "crafting_table" || name == "oak_door") {
        return "wood";
    }
    if (name.find("wool") != std::string::npos) {
        return "cloth";
    }
    return "stone";
}

std::string digSound(const std::string& material, int index) {
    return "minecraft/sounds/dig/" + material + std::to_string(1 + index % 4) + ".ogg";
}

std::string stepSound(const std::string& material, int index) {
    return "minecraft/sounds/step/" + material + std::to_string(1 + index % 4) + ".ogg";
}

}


struct VoxelRayHit {
    bool hit = false;
    int x = 0;
    int y = 0;
    int z = 0;
    int face = 2;
    float distance = 0.0f;
};

Vec3 faceNormal(int face) {
    switch (face) {
    case 0: return {1.0f, 0.0f, 0.0f};
    case 1: return {-1.0f, 0.0f, 0.0f};
    case 2: return {0.0f, 1.0f, 0.0f};
    case 3: return {0.0f, -1.0f, 0.0f};
    case 4: return {0.0f, 0.0f, 1.0f};
    default: return {0.0f, 0.0f, -1.0f};
    }
}

VoxelRayHit traceVoxelRay(const World& world, const BlockRegistry& blocks,
                          const Vec3& origin, const Vec3& direction, float maxDistance,
                          bool includeVegetation) {
    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    const int stepX = direction.x > 0.0f ? 1 : -1;
    const int stepY = direction.y > 0.0f ? 1 : -1;
    const int stepZ = direction.z > 0.0f ? 1 : -1;

    constexpr float infinity = std::numeric_limits<float>::infinity();
    const float deltaX = direction.x == 0.0f ? infinity : std::abs(1.0f / direction.x);
    const float deltaY = direction.y == 0.0f ? infinity : std::abs(1.0f / direction.y);
    const float deltaZ = direction.z == 0.0f ? infinity : std::abs(1.0f / direction.z);

    float maxX = direction.x == 0.0f ? infinity
                                     : ((direction.x > 0.0f ? (x + 1 - origin.x) : (x - origin.x))
                                        / direction.x);
    float maxY = direction.y == 0.0f ? infinity
                                     : ((direction.y > 0.0f ? (y + 1 - origin.y) : (y - origin.y))
                                        / direction.y);
    float maxZ = direction.z == 0.0f ? infinity
                                     : ((direction.z > 0.0f ? (z + 1 - origin.z) : (z - origin.z))
                                        / direction.z);

    int lastAxis = -1;
    float distance = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        const int block = world.get(x, y, z);
        if (block > 0) {
            const BlockDef& def = blocks.def(block);
            if (!blocks.isLiquid(block)
                && (blocks.isSolid(block) || (includeVegetation && def.cross))) {
                int face = static_cast<int>(Face::Up);
                if (lastAxis == 0) {
                    face = stepX > 0 ? static_cast<int>(Face::West)
                                     : static_cast<int>(Face::East);
                } else if (lastAxis == 1) {
                    face = stepY > 0 ? static_cast<int>(Face::Down)
                                     : static_cast<int>(Face::Up);
                } else if (lastAxis == 2) {
                    face = stepZ > 0 ? static_cast<int>(Face::North)
                                     : static_cast<int>(Face::South);
                }
                VoxelRayHit hit;
                hit.hit = true;
                hit.x = x;
                hit.y = y;
                hit.z = z;
                hit.face = face;
                hit.distance = distance;
                return hit;
            }
        }

        if (maxX < maxY && maxX < maxZ) {
            distance = maxX;
            x += stepX;
            maxX += deltaX;
            lastAxis = 0;
        } else if (maxY < maxZ) {
            distance = maxY;
            y += stepY;
            maxY += deltaY;
            lastAxis = 1;
        } else {
            distance = maxZ;
            z += stepZ;
            maxZ += deltaZ;
            lastAxis = 2;
        }

        if (distance > maxDistance) {
            break;
        }
    }
    return {};
}

Vec3 raySkyColor(const Vec3& direction, const Environment& environment) {
    const float up = clampf(direction.y, 0.0f, 1.0f);
    Vec3 sky = environment.skyHorizon
             + (environment.skyZenith - environment.skyHorizon) * std::pow(up, 0.45f);
    const float sunAmount = dot(direction, environment.sunDirection);
    sky += environment.sunColor
         * (std::pow(std::max(sunAmount, 0.0f), 400.0f) * 0.5f
            + std::pow(std::max(sunAmount, 0.0f), 16.0f) * 0.05f);
    return sky;
}

bool Game::init(const GameOptions& options, std::string* error) {
    options_ = options;
    if (options_.screenshotPath.empty()) {
        screenshotMode_ = false;
    } else {
        screenshotMode_ = true;
        if (options_.screenshotFrames <= 0) {
            options_.screenshotFrames = 90;
        }
    }

    if (!createWindow(error)) {
        return false;
    }

    if (!assets_.init(options_.minecraftDir, error)) {
        return false;
    }

    textures_.initialize(16, 512);
    if (!blocks_.load(assets_, textures_, error)) {
        return false;
    }
    if (!textures_.upload(error)) {
        return false;
    }
    if (!worldRenderer_.init(textures_, error)) {
        return false;
    }
    if (!sky_.init(error)) {
        return false;
    }
    if (!hud_.init(assets_, textures_, error)) {
        return false;
    }
    if (!playerRenderer_.init(assets_, error)) {
        return false;
    }
    if (!particleRenderer_.init(error)) {
        return false;
    }

    thirdPerson_ = options_.thirdPerson;
    flashlightOn_ = options_.flashlight;
    timeOfDay_ = options_.timeOfDay;
    timeSpeed_ = options_.timeSpeed;
    waterLevel_ = static_cast<float>(World::kSeaLevel);
    environment_ = computeEnvironment(timeOfDay_);
    if (!reflection_.resize(std::max(1, options_.width), std::max(1, options_.height),
                            error)) {
        return false;
    }

    std::string audioError;
    if (!audio_.init(assets_, &audioError)) {
        logWarn("audio disabled: %s", audioError.c_str());
    } else if (!screenshotMode_ && options_.raytracePath.empty()) {
        audio_.playMusic("minecraft/sounds/music/game/calm1.ogg", 0.10f);
    }

    const std::string saveDirectory = "saves/" + std::to_string(options_.seed);
    world_ = std::make_unique<World>(options_.seed, saveDirectory, options_.workerThreads);
    {
        std::vector<int> emissiveBlocks;
        const int glowstone = blocks_.id("glowstone");
        const int lava = blocks_.id("lava");
        if (glowstone > 0) {
            emissiveBlocks.push_back(glowstone);
        }
        if (lava > 0) {
            emissiveBlocks.push_back(lava);
        }
        world_->setEmissiveBlocks(emissiveBlocks);
    }

    const auto waitForStreaming = [this](int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(timeoutMs);
        while (!world_->isStreamingIdle()
               && std::chrono::steady_clock::now() < deadline) {
            world_->pumpCompleted();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };

    world_->updateStreaming({0.0f, 64.0f, 0.0f}, options_.renderDistance, blocks_);
    waitForStreaming(15000);
    player_.spawn(*world_, blocks_);
    player_.yaw = 35.0f;
    player_.pitch = -18.0f;
    if (options_.hasCamera) {
        player_.position = options_.cameraPosition;
        player_.yaw = options_.cameraYaw;
        player_.pitch = options_.cameraPitch;
        player_.flying = true;
        player_.velocity = {};
    }
    world_->updateStreaming(player_.position, options_.renderDistance, blocks_);
    waitForStreaming(15000);
    lastStreamCenter_ = World::chunkOf(static_cast<int>(std::floor(player_.position.x)),
                                       static_cast<int>(std::floor(player_.position.z)));
    hasStreamCenter_ = true;
    worldRenderer_.syncMeshes(*world_, blocks_, lastStreamCenter_, options_.renderDistance,
                              options_.grassDistance);

    if (screenshotMode_) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while ((!world_->isStreamingIdle()
                || !worldRenderer_.isSyncedFor(*world_, lastStreamCenter_,
                                               options_.renderDistance,
                                               options_.grassDistance))
               && std::chrono::steady_clock::now() < deadline) {
            world_->pumpCompleted();
            world_->updateStreaming(player_.position, options_.renderDistance, blocks_);
            worldRenderer_.syncMeshes(*world_, blocks_, lastStreamCenter_,
                                      options_.renderDistance, options_.grassDistance);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    logInfo("world seed=%llu renderDistance=%d grassDistance=%d",
            static_cast<unsigned long long>(options_.seed), options_.renderDistance,
            options_.grassDistance);

    hotbar_.clear();
    storage_.clear();
    heldStack_ = {};
    const auto addHotbarItem = [this](const char* name, int count) {
        const int blockId = blocks_.id(name);
        if (blockId > 0 && hotbar_.size() < 9) {
            hotbar_.push_back(ItemStack{blockId, count});
        }
    };
    addHotbarItem("oak_planks", 16);
    addHotbarItem("cobblestone", 16);
    addHotbarItem("stone", 8);
    addHotbarItem("dirt", 16);
    addHotbarItem("oak_log", 4);
    addHotbarItem("sand", 8);
    addHotbarItem("stone_slab", 8);
    addHotbarItem("glass", 4);
    addHotbarItem("tnt", 1);
    while (hotbar_.size() < 9) {
        hotbar_.push_back(ItemStack{});
    }
    while (storage_.size() < 27) {
        storage_.push_back(ItemStack{});
    }
    const int glowstoneId = blocks_.id("glowstone");
    if (glowstoneId > 0) {
        addItem(glowstoneId, 16);
    }

    const auto addRecipe = [this](const char* outputName, int outputCount,
                                  std::initializer_list<std::pair<const char*, int>> inputs) {
        const int output = blocks_.id(outputName);
        if (output <= 0) {
            return;
        }
        CraftingRecipe recipe;
        recipe.outputId = output;
        recipe.outputCount = outputCount;
        for (const auto& [name, count] : inputs) {
            const int blockId = blocks_.id(name);
            if (blockId <= 0) {
                return;
            }
            recipe.inputs.emplace_back(blockId, count);
        }
        recipes_.push_back(std::move(recipe));
    };
    addRecipe("oak_planks", 4, {{"oak_log", 1}});
    addRecipe("crafting_table", 1, {{"oak_planks", 4}});
    addRecipe("oak_slab", 4, {{"oak_planks", 4}});
    addRecipe("stone_slab", 4, {{"stone", 4}});
    addRecipe("cobblestone_slab", 4, {{"cobblestone", 4}});
    addRecipe("stonebrick", 1, {{"cobblestone", 4}});
    addRecipe("sandstone", 1, {{"sand", 4}});
    addRecipe("sandstone_slab", 4, {{"sandstone", 4}});
    addRecipe("tnt", 1, {{"sand", 4}, {"redstone_ore", 1}});

    spawnPosition_ = player_.position;
    health_ = 20.0f;
    hunger_ = 20.0f;
    oxygen_ = 10.0f;
    dead_ = false;
    wasOnGround_ = player_.onGround;
    fallStartY_ = player_.position.y;
    if (!screenshotMode_) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        mouseCaptured_ = true;
    } else {
        SDL_ShowCursor(SDL_DISABLE);
    }

    lastCounter_ = SDL_GetPerformanceCounter();
    logInfo("player spawned at %.2f %.2f %.2f", player_.position.x, player_.position.y,
            player_.position.z);
    logInfo("hotbar slots: %zu", hotbar_.size());
    return true;
}

bool Game::createWindow(std::string* error) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        if (error) {
            *error = std::string("SDL_Init failed: ") + SDL_GetError();
        }
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    window_ = SDL_CreateWindow("NativeCraft", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               options_.width, options_.height,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        if (error) {
            *error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        }
        return false;
    }

    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        if (error) {
            *error = std::string("SDL_GL_CreateContext failed: ") + SDL_GetError();
        }
        return false;
    }
    if (SDL_GL_MakeCurrent(window_, context_) != 0) {
        if (error) {
            *error = std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError();
        }
        return false;
    }

    SDL_GL_SetSwapInterval(screenshotMode_ ? 0 : 1);
    if (!gl::loadFunctions(error)) {
        return false;
    }

    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    logInfo("OpenGL %s | %s", version ? reinterpret_cast<const char*>(version) : "?",
            renderer ? reinterpret_cast<const char*>(renderer) : "?");
    return true;
}

void Game::processEvents(bool& running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            running = false;
            break;
        case SDL_KEYDOWN:
            if (event.key.repeat) {
                break;
            }
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                if (inventoryOpen_) {
                    closeInventory();
                } else {
                    running = false;
                }
                break;
            case SDLK_e:
                toggleInventory();
                break;
            case SDLK_f:
                player_.flying = !player_.flying;
                player_.velocity = {};
                logInfo("fly mode %s", player_.flying ? "enabled" : "disabled");
                break;
            case SDLK_m:
                if (!inventoryOpen_) {
                    mouseCaptured_ = !mouseCaptured_;
                    SDL_SetRelativeMouseMode(mouseCaptured_ ? SDL_TRUE : SDL_FALSE);
                }
                break;
            case SDLK_F5:
                thirdPerson_ = !thirdPerson_;
                break;
            case SDLK_l:
                flashlightOn_ = !flashlightOn_;
                logInfo("flashlight %s", flashlightOn_ ? "enabled" : "disabled");
                break;
            case SDLK_g: {
                const RayHit hit = player_.raycast(*world_, blocks_, 6.0f);
                if (hit.hit) {
                    primeTnt(BlockPos{hit.x, hit.y, hit.z});
                }
                break;
            }
            case SDLK_r:
                if (dead_) {
                    respawn();
                }
                break;
            case SDLK_t:
                timePaused_ = !timePaused_;
                logInfo("time %s", timePaused_ ? "paused" : "running");
                break;
            case SDLK_F2: {
                screenshotRequested_ = true;
                screenshotRequestPath_ =
                    "screenshots/shot_" + std::to_string(++screenshotCounter_) + ".png";
                break;
            }
            default:
                if (event.key.keysym.sym >= SDLK_1 && event.key.keysym.sym <= SDLK_9) {
                    const int slot = event.key.keysym.sym - SDLK_1;
                    if (slot >= 0 && static_cast<std::size_t>(slot) < hotbar_.size()) {
                        selectedSlot_ = slot;
                    }
                }
                break;
            }
            break;
        case SDL_MOUSEMOTION:
            if (mouseCaptured_ && !screenshotMode_) {
                player_.yaw += static_cast<float>(event.motion.xrel) * 0.12f;
                player_.pitch -= static_cast<float>(event.motion.yrel) * 0.12f;
                player_.pitch = clampf(player_.pitch, -89.0f, 89.0f);
                player_.yaw = std::fmod(player_.yaw, 360.0f);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (inventoryOpen_) {
                int mouseX = 0;
                int mouseY = 0;
                SDL_GetMouseState(&mouseX, &mouseY);
                handleInventoryClick(mouseX, mouseY, event.button.button);
            } else if (mouseCaptured_ && !screenshotMode_) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    handleBreak();
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    handlePlace();
                }
            }
            break;
        case SDL_MOUSEWHEEL:
            if (!inventoryOpen_ && !hotbar_.empty() && event.wheel.y != 0) {
                const int direction = event.wheel.y > 0 ? 1 : -1;
                selectedSlot_ = (selectedSlot_ + direction
                                 + static_cast<int>(hotbar_.size()))
                              % static_cast<int>(hotbar_.size());
            }
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                options_.width = event.window.data1;
                options_.height = event.window.data2;
            }
            break;
        default:
            break;
        }
    }
}

void Game::update(float deltaTime) {
    fpsAccumulator_ += deltaTime;
    ++fpsFrames_;
    if (fpsAccumulator_ >= 0.5) {
        fps_ = static_cast<float>(fpsFrames_) / static_cast<float>(fpsAccumulator_);
        fpsAccumulator_ = 0.0;
        fpsFrames_ = 0;
    }

    elapsedTime_ += deltaTime;
    if (!timePaused_) {
        timeOfDay_ += deltaTime * timeSpeed_;
        timeOfDay_ -= std::floor(timeOfDay_);
    }
    environment_ = computeEnvironment(timeOfDay_);

    const float windTime = elapsedTime_;
    const float windAngle = 0.9f + 0.7f * std::sin(windTime * 0.05f)
                          + 0.25f * std::sin(windTime * 0.17f);
    windDirection_ = {std::cos(windAngle), 0.0f, std::sin(windAngle)};
    windStrength_ = clampf(0.18f + 0.10f * (0.5f + 0.5f * std::sin(windTime * 0.37f))
                               + 0.06f * std::sin(windTime * 1.7f + 0.4f),
                           0.05f, 0.45f);

    world_->pumpCompleted();
    ChunkPos center = World::chunkOf(static_cast<int>(std::floor(player_.position.x)),
                                     static_cast<int>(std::floor(player_.position.z)));
    if (!hasStreamCenter_ || center.x != lastStreamCenter_.x
        || center.z != lastStreamCenter_.z) {
        world_->updateStreaming(player_.position, options_.renderDistance, blocks_);
        lastStreamCenter_ = center;
        hasStreamCenter_ = true;
    }
    worldRenderer_.syncMeshes(*world_, blocks_, center, options_.renderDistance,
                              options_.grassDistance);

    const std::vector<ChunkPos> fluidChanges = world_->updateFluids(blocks_, 64);
    for (const ChunkPos& pos : fluidChanges) {
        worldRenderer_.rebuildChunkAt(*world_, blocks_, pos);
    }

    updateTnt(deltaTime);
    updateParticles(deltaTime);
    explosionFlashTime_ = std::max(0.0f, explosionFlashTime_ - deltaTime);

    if (screenshotMode_) {
        return;
    }

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    PlayerInput input;
    input.forward = keys[SDL_SCANCODE_W] != 0;
    input.back = keys[SDL_SCANCODE_S] != 0;
    input.left = keys[SDL_SCANCODE_A] != 0;
    input.right = keys[SDL_SCANCODE_D] != 0;
    input.jump = keys[SDL_SCANCODE_SPACE] != 0;
    input.descend = keys[SDL_SCANCODE_LCTRL] != 0 || keys[SDL_SCANCODE_RCTRL] != 0;
    input.boost = keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;
    if (inventoryOpen_ || dead_) {
        input = {};
    }

    const bool wasOnGround = player_.onGround;
    if (!dead_) {
        player_.update(*world_, blocks_, input, deltaTime);
    } else {
        player_.velocity = {};
    }
    updateSurvival(deltaTime);

    if (!player_.flying && !dead_) {
        if (!player_.onGround) {
            fallStartY_ = std::max(fallStartY_, player_.position.y);
        } else {
            if (!wasOnGround && !player_.inWater) {
                const float fallDistance = fallStartY_ - player_.position.y;
                if (fallDistance > 3.0f) {
                    applyDamage((fallDistance - 3.0f) * 2.0f);
                }
            }
            fallStartY_ = player_.position.y;
        }
        wasOnGround_ = player_.onGround;
    } else {
        fallStartY_ = player_.position.y;
    }

    const float horizontalSpeed = std::sqrt(player_.velocity.x * player_.velocity.x
                                             + player_.velocity.z * player_.velocity.z);
    const float targetWalkAmount = clampf(horizontalSpeed / 4.3f, 0.0f, 1.0f);
    walkAmount_ += (targetWalkAmount - walkAmount_)
                 * std::min(1.0f, deltaTime * 10.0f);
    walkPhase_ += deltaTime * (3.5f + horizontalSpeed * 1.6f) * (0.35f + walkAmount_);
    attackSwing_ = std::max(0.0f, attackSwing_ - deltaTime * 4.5f);

    const ChunkPos movedCenter =
        World::chunkOf(static_cast<int>(std::floor(player_.position.x)),
                       static_cast<int>(std::floor(player_.position.z)));
    if (movedCenter.x != lastStreamCenter_.x || movedCenter.z != lastStreamCenter_.z) {
        world_->updateStreaming(player_.position, options_.renderDistance, blocks_);
        worldRenderer_.syncMeshes(*world_, blocks_, movedCenter, options_.renderDistance,
                                  options_.grassDistance);
        lastStreamCenter_ = movedCenter;
    }

    audio_.setWaterLoop(player_.inWater, 0.42f);

    birdTimer_ -= deltaTime;
    if (birdTimer_ <= 0.0) {
        birdTimer_ = 9.0 + 6.0 * (0.5 + 0.5 * std::sin(elapsedTime_ * 0.7));
        if (!underwater_ && environment_.dayFactor > 0.3f) {
            audio_.playBirdChirp(0.16f);
        }
    }

    if (!player_.flying && player_.onGround) {
        const float horizontalSpeed =
            std::sqrt(player_.velocity.x * player_.velocity.x
                      + player_.velocity.z * player_.velocity.z);
        stepAccumulator_ += static_cast<double>(horizontalSpeed * deltaTime);
        if (stepAccumulator_ >= 1.8) {
            stepAccumulator_ = 0.0;
            const int x = static_cast<int>(std::floor(player_.position.x));
            const int y = static_cast<int>(std::floor(player_.position.y - 0.1f));
            const int z = static_cast<int>(std::floor(player_.position.z));
            const int below = world_->get(x, y, z);
            if (below > 0) {
                audio_.playSound(stepSound(blockMaterial(blocks_.def(below).name),
                                           stepSoundCounter_++),
                                 0.22f);
            }
        }
    } else {
        stepAccumulator_ = 0.0;
    }
}

float Game::random01() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_);
}

void Game::primeTnt(const BlockPos& block) {
    const int tntId = blocks_.id("tnt");
    if (tntId <= 0 || world_->get(block.x, block.y, block.z) != tntId) {
        return;
    }
    for (const PrimedTnt& primed : primedTnt_) {
        if (primed.block == block) {
            return;
        }
    }
    primedTnt_.push_back(PrimedTnt{block, 2.4f, 0.0f});
    audio_.playSound("minecraft/sounds/random/fuse.ogg", 0.75f);
    logInfo("TNT primed at %d,%d,%d", block.x, block.y, block.z);
}

void Game::updateParticles(float deltaTime) {
    for (Particle& particle : particles_) {
        particle.life -= deltaTime;
        particle.velocity.y -= particle.gravity * deltaTime;
        const float damping = std::exp(-particle.drag * deltaTime);
        particle.velocity.x *= damping;
        particle.velocity.y *= damping;
        particle.velocity.z *= damping;
        particle.position += particle.velocity * deltaTime;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& particle) {
                                        return particle.life <= 0.0f;
                                    }),
                     particles_.end());
}

void Game::updateTnt(float deltaTime) {
    const int tntId = blocks_.id("tnt");
    for (std::size_t i = 0; i < primedTnt_.size();) {
        PrimedTnt& primed = primedTnt_[i];
        if (tntId <= 0 || world_->get(primed.block.x, primed.block.y, primed.block.z) != tntId) {
            primedTnt_.erase(primedTnt_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        primed.fuse -= deltaTime;
        primed.emitTimer -= deltaTime;
        if (primed.emitTimer <= 0.0f) {
            primed.emitTimer = 0.14f;
            Particle smoke;
            smoke.position = {static_cast<float>(primed.block.x) + 0.5f + (random01() - 0.5f) * 0.5f,
                              static_cast<float>(primed.block.y) + 1.0f,
                              static_cast<float>(primed.block.z) + 0.5f + (random01() - 0.5f) * 0.5f};
            smoke.velocity = {(random01() - 0.5f) * 0.4f, 0.8f + random01(), (random01() - 0.5f) * 0.4f};
            smoke.startColor = {0.45f, 0.42f, 0.38f};
            smoke.endColor = {0.12f, 0.12f, 0.12f};
            smoke.life = 0.7f;
            smoke.maxLife = 0.7f;
            smoke.startSize = 0.20f;
            smoke.endSize = 0.55f;
            smoke.gravity = 0.4f;
            smoke.drag = 1.0f;
            particles_.push_back(smoke);
        }
        if (primed.fuse <= 0.0f) {
            const BlockPos block = primed.block;
            primedTnt_.erase(primedTnt_.begin() + static_cast<std::ptrdiff_t>(i));
            explodeTnt(block);
            continue;
        }
        ++i;
    }
}

void Game::explodeTnt(const BlockPos& block) {
    const int tntId = blocks_.id("tnt");
    const int bedrockId = blocks_.id("bedrock");
    const Vec3 center{static_cast<float>(block.x) + 0.5f,
                      static_cast<float>(block.y) + 0.5f,
                      static_cast<float>(block.z) + 0.5f};
    const float radius = 4.2f;
    std::unordered_set<ChunkPos, ChunkPosHash> changedChunks;

    if (world_->get(block.x, block.y, block.z) == tntId) {
        world_->set(block.x, block.y, block.z, 0);
    }
    changedChunks.insert(World::chunkOf(block.x, block.z));

    for (int dy = -4; dy <= 4; ++dy) {
        for (int dz = -4; dz <= 4; ++dz) {
            for (int dx = -4; dx <= 4; ++dx) {
                if (static_cast<float>(dx * dx + dy * dy + dz * dz) > radius * radius) {
                    continue;
                }
                const int x = block.x + dx;
                const int y = block.y + dy;
                const int z = block.z + dz;
                const int blockId = world_->get(x, y, z);
                if (blockId == 0 || blockId == bedrockId || blocks_.isLiquid(blockId)) {
                    continue;
                }
                const BlockDef& def = blocks_.def(blockId);
                if (def.name == "tnt") {
                    bool alreadyPrimed = false;
                    for (const PrimedTnt& primed : primedTnt_) {
                        if (primed.block == BlockPos{x, y, z}) {
                            alreadyPrimed = true;
                            break;
                        }
                    }
                    if (!alreadyPrimed) {
                        primedTnt_.push_back(PrimedTnt{BlockPos{x, y, z}, 0.35f, 0.0f});
                    }
                    continue;
                }

                world_->set(x, y, z, 0);
                changedChunks.insert(World::chunkOf(x, z));
                const int up = static_cast<int>(Face::Up);
                const Vec3 base = textures_.averageColor(def.layers[up]);
                const Vec3 tint{def.tints[up][0], def.tints[up][1], def.tints[up][2]};
                for (int particleIndex = 0; particleIndex < 2; ++particleIndex) {
                    Particle debris;
                    debris.position = {static_cast<float>(x) + random01(),
                                       static_cast<float>(y) + random01(),
                                       static_cast<float>(z) + random01()};
                    debris.velocity = {(random01() - 0.5f) * 7.0f,
                                       1.0f + random01() * 5.0f,
                                       (random01() - 0.5f) * 7.0f};
                    debris.startColor = {base.x * tint.x, base.y * tint.y, base.z * tint.z};
                    debris.endColor = debris.startColor * 0.45f;
                    debris.life = 0.6f + random01() * 0.8f;
                    debris.maxLife = debris.life;
                    debris.startSize = 0.12f;
                    debris.endSize = 0.06f;
                    debris.gravity = 9.0f;
                    debris.drag = 0.4f;
                    particles_.push_back(debris);
                }
            }
        }
    }

    for (const ChunkPos& chunk : changedChunks) {
        worldRenderer_.rebuildChunkAt(*world_, blocks_, chunk);
    }

    for (int i = 0; i < 80; ++i) {
        Particle smoke;
        smoke.position = center + Vec3{(random01() - 0.5f) * 2.0f,
                                       0.5f + random01() * 2.0f,
                                       (random01() - 0.5f) * 2.0f};
        smoke.velocity = {(random01() - 0.5f) * 7.0f,
                          1.5f + random01() * 5.0f,
                          (random01() - 0.5f) * 7.0f};
        smoke.startColor = {0.38f, 0.35f, 0.32f};
        smoke.endColor = {0.05f, 0.05f, 0.05f};
        smoke.life = 1.2f + random01() * 1.8f;
        smoke.maxLife = smoke.life;
        smoke.startSize = 0.35f + random01() * 0.4f;
        smoke.endSize = 1.2f + random01() * 1.4f;
        smoke.gravity = 0.2f;
        smoke.drag = 1.4f;
        particles_.push_back(smoke);
    }

    for (int i = 0; i < 50; ++i) {
        Particle fire;
        fire.position = center + Vec3{(random01() - 0.5f) * 1.5f,
                                      0.2f + random01() * 1.5f,
                                      (random01() - 0.5f) * 1.5f};
        fire.velocity = {(random01() - 0.5f) * 8.0f,
                         2.0f + random01() * 6.0f,
                         (random01() - 0.5f) * 8.0f};
        fire.startColor = {1.0f, 0.85f, 0.25f};
        fire.endColor = {1.0f, 0.16f, 0.02f};
        fire.life = 0.3f + random01() * 0.55f;
        fire.maxLife = fire.life;
        fire.startSize = 0.18f;
        fire.endSize = 0.5f;
        fire.gravity = -1.2f;
        fire.drag = 1.8f;
        particles_.push_back(fire);
    }

    const Vec3 toPlayer = player_.position - center;
    const float distance = length(toPlayer);
    if (distance < 9.0f) {
        const float strength = (9.0f - distance) / 9.0f;
        player_.velocity += normalize(toPlayer) * (8.0f * strength)
                          + Vec3{0.0f, 4.0f * strength, 0.0f};
        applyDamage(10.0f * strength);
    }

    explosionFlashPosition_ = center;
    explosionFlashTime_ = 0.45f;
    audio_.playSound("minecraft/sounds/random/explode"
                     + std::to_string(1 + static_cast<int>(random01() * 4.0f)) + ".ogg",
                     1.0f);
    logInfo("TNT exploded at %d,%d,%d", block.x, block.y, block.z);
}

PlayerRenderState Game::buildPlayerRenderState() const {
    PlayerRenderState state;
    state.position = player_.position;
    state.bodyYaw = player_.yaw;
    state.headPitch = player_.pitch;
    state.walkPhase = walkPhase_;
    state.walkAmount = walkAmount_;
    state.attackSwing = attackSwing_;
    state.verticalVelocity = player_.velocity.y;
    state.time = elapsedTime_;
    state.onGround = player_.onGround;
    state.inWater = player_.inWater;
    return state;
}

void Game::render() {
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    const float viewDistance = static_cast<float>(options_.renderDistance * World::kChunkSize);
    const float reflectionFogStart = viewDistance * 0.42f;
    const float reflectionFogEnd = viewDistance * 0.92f;

    Vec3 viewEye = player_.eye();
    Vec3 viewForward = player_.forward();
    Mat4 view = player_.viewMatrix();
    if (thirdPerson_) {
        viewEye = player_.eye() - viewForward * 3.2f + Vec3{0.0f, 0.45f, 0.0f};
        view = Mat4::lookAt(viewEye, viewEye + viewForward, {0.0f, 1.0f, 0.0f});
    }

    const int eyeX = static_cast<int>(std::floor(viewEye.x));
    const int eyeY = static_cast<int>(std::floor(viewEye.y));
    const int eyeZ = static_cast<int>(std::floor(viewEye.z));
    const int eyeBlock = world_->get(eyeX, eyeY, eyeZ);
    underwater_ = blocks_.isLiquid(eyeBlock);
    underwaterLava_ = underwater_ && blocks_.def(eyeBlock).name == "lava";

    Environment renderEnvironment = environment_;
    float fogStart = reflectionFogStart;
    float fogEnd = reflectionFogEnd;
    if (underwater_) {
        if (underwaterLava_) {
            renderEnvironment.fogColor = {0.30f, 0.08f, 0.02f};
            renderEnvironment.skyHorizon = {0.24f, 0.06f, 0.01f};
            renderEnvironment.skyZenith = {0.10f, 0.02f, 0.01f};
            renderEnvironment.ambientSky = {0.30f, 0.10f, 0.03f};
            renderEnvironment.ambientGround = {0.08f, 0.02f, 0.01f};
            renderEnvironment.sunColor = environment_.sunColor * 0.2f;
            fogStart = 1.0f;
            fogEnd = 10.0f;
        } else {
            renderEnvironment.fogColor = {0.035f, 0.22f, 0.32f};
            renderEnvironment.skyHorizon = {0.05f, 0.30f, 0.45f};
            renderEnvironment.skyZenith = {0.02f, 0.14f, 0.25f};
            renderEnvironment.ambientSky = {0.12f, 0.32f, 0.42f};
            renderEnvironment.ambientGround = {0.02f, 0.08f, 0.10f};
            renderEnvironment.sunColor = environment_.sunColor * 0.55f;
            fogStart = 2.0f;
            fogEnd = 42.0f;
        }
    }
    std::vector<PointLight> pointLights;
    for (const BlockPos& position : world_->lightSources()) {
        const int blockId = world_->get(position.x, position.y, position.z);
        const BlockDef& block = blocks_.def(blockId);
        if (block.lightEmission <= 0.0f) {
            continue;
        }
        const float dx = static_cast<float>(position.x) + 0.5f - player_.position.x;
        const float dy = static_cast<float>(position.y) + 0.5f - player_.position.y;
        const float dz = static_cast<float>(position.z) + 0.5f - player_.position.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared > 80.0f * 80.0f) {
            continue;
        }
        Vec3 color{1.5f, 1.05f, 0.50f};
        float range = 18.0f;
        if (block.name == "lava") {
            color = {1.4f, 0.45f, 0.10f};
            range = 13.0f;
        }
        pointLights.push_back(PointLight{{static_cast<float>(position.x) + 0.5f,
                                         static_cast<float>(position.y) + 0.5f,
                                         static_cast<float>(position.z) + 0.5f},
                                        color, range});
    }
    std::sort(pointLights.begin(), pointLights.end(),
              [this](const PointLight& a, const PointLight& b) {
                  return lengthSquared(a.position - player_.position)
                       < lengthSquared(b.position - player_.position);
              });
    if (pointLights.size() > 16) {
        pointLights.resize(16);
    }
    if (explosionFlashTime_ > 0.0f) {
        const float intensity = clampf(explosionFlashTime_ / 0.45f, 0.0f, 1.0f);
        pointLights.push_back(PointLight{explosionFlashPosition_,
                                         {5.0f * intensity, 2.2f * intensity,
                                          0.4f * intensity},
                                         30.0f});
    }
    worldRenderer_.setDynamicLights(pointLights);
    worldRenderer_.setFlashlight(flashlightOn_, player_.eye(), player_.forward());
    worldRenderer_.setWaterState(waterLevel_, elapsedTime_, underwater_);
    const PlayerRenderState playerState = buildPlayerRenderState();
    worldRenderer_.setPlayerRenderer(&playerRenderer_, playerState);

    const int reflectionWidth = std::max(1, width);
    const int reflectionHeight = std::max(1, height);
    std::string reflectionError;
    if (!reflection_.resize(reflectionWidth, reflectionHeight, &reflectionError)) {
        static bool logged = false;
        if (!logged) {
            logError("reflection disabled: %s", reflectionError.c_str());
            logged = true;
        }
    }

    worldRenderer_.renderShadowMap(player_.position, environment_.sunDirection, elapsedTime_,
                                  windDirection_, windStrength_, player_.position);

    const Vec3 reflectedEye{viewEye.x, 2.0f * waterLevel_ - viewEye.y, viewEye.z};
    const Vec3 reflectedForward{viewForward.x, -viewForward.y, viewForward.z};
    const Vec3 up = std::abs(reflectedForward.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f}
                                                         : Vec3{0.0f, 1.0f, 0.0f};
    const Mat4 reflectionView = Mat4::lookAt(reflectedEye, reflectedEye + reflectedForward, up);
    const float reflectionAspect =
        static_cast<float>(reflectionWidth) / static_cast<float>(reflectionHeight);
    const Mat4 reflectionProjection =
        Mat4::perspective(radians(70.0f), reflectionAspect, 0.1f, 300.0f);
    const Mat4 reflectionViewProjection = reflectionProjection * reflectionView;

    if (reflection_.valid()) {
        reflection_.bind();
        glClearColor(environment_.fogColor.x, environment_.fogColor.y, environment_.fogColor.z,
                     1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        sky_.render(reflectionViewProjection.inverse(), reflectedEye, environment_,
                    elapsedTime_);
        worldRenderer_.drawOpaque(reflectionViewProjection, reflectedEye, environment_,
                                  reflectionFogStart, reflectionFogEnd, true, waterLevel_, true);
        if (thirdPerson_) {
            playerRenderer_.render(reflectionViewProjection, reflectedEye, environment_,
                                   reflectionFogStart, reflectionFogEnd, playerState);
        }
        reflection_.unbind();
    }

    glViewport(0, 0, width, height);
    glClearColor(renderEnvironment.fogColor.x, renderEnvironment.fogColor.y,
                 renderEnvironment.fogColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Mat4 projection = Mat4::perspective(radians(70.0f), aspect, 0.1f, 300.0f);
    const Mat4 viewProjection = projection * view;

    sky_.render(viewProjection.inverse(), viewEye, renderEnvironment, elapsedTime_);
    worldRenderer_.drawOpaque(viewProjection, viewEye, renderEnvironment, fogStart, fogEnd,
                              false, 0.0f, false);
    worldRenderer_.drawGrass(viewProjection, viewEye, renderEnvironment, fogStart, fogEnd,
                             elapsedTime_, player_.position, windDirection_, windStrength_);
    if (thirdPerson_) {
        playerRenderer_.render(viewProjection, viewEye, renderEnvironment, fogStart, fogEnd,
                               playerState);
    }
    worldRenderer_.drawTransparent(viewProjection, viewEye, renderEnvironment, fogStart, fogEnd);
    worldRenderer_.drawWater(viewProjection, viewEye, renderEnvironment, fogStart, fogEnd,
                             reflection_.colorTexture(), reflectionViewProjection,
                             elapsedTime_, waterLevel_);
    particleRenderer_.render(viewProjection, viewEye, renderEnvironment, fogStart, fogEnd,
                             height, particles_);
    drawHud();
}

void Game::drawHud() {
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window_, &width, &height);

    hud_.begin(width, height);
    if (dead_) {
        hud_.drawRect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                      {0.18f, 0.01f, 0.01f}, 0.62f);
        const std::string title = "You died";
        const std::string hint = "Press R to respawn";
        const float titleWidth = hud_.textWidth(title, 3.0f);
        const float hintWidth = hud_.textWidth(hint, 1.6f);
        hud_.drawText((static_cast<float>(width) - titleWidth) * 0.5f,
                      static_cast<float>(height) * 0.42f, title, 3.0f, {1.0f, 0.9f, 0.9f});
        hud_.drawText((static_cast<float>(width) - hintWidth) * 0.5f,
                      static_cast<float>(height) * 0.42f + 54.0f, hint, 1.6f,
                      {1.0f, 0.85f, 0.85f});
        hud_.end();
        return;
    }
    if (inventoryOpen_) {
        drawInventory(width, height);
        hud_.end();
        return;
    }
    if (damageFlashTime_ > 0.0f) {
        hud_.drawRect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                      {0.7f, 0.02f, 0.02f},
                      clampf(damageFlashTime_ * 0.7f, 0.0f, 0.28f));
    }
    if (underwater_) {
        const Vec3 tint = underwaterLava_ ? Vec3{0.55f, 0.20f, 0.05f}
                                          : Vec3{0.02f, 0.20f, 0.30f};
        hud_.drawRect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), tint,
                      underwaterLava_ ? 0.28f : 0.12f);
    }
    if (explosionFlashTime_ > 0.0f) {
        hud_.drawRect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                      {1.0f, 0.62f, 0.18f},
                      clampf(explosionFlashTime_ * 0.9f, 0.0f, 0.45f));
    }
    hud_.drawCrosshair();

    const auto shadowText = [this](float x, float y, const std::string& text, float scale,
                                   const Vec3& color) {
        hud_.drawText(x + 1.5f, y + 1.5f, text, scale, {0.0f, 0.0f, 0.0f}, 0.7f);
        hud_.drawText(x, y, text, scale, color, 1.0f);
    };

    char line[256];
    std::snprintf(line, sizeof(line), "NativeCraft  |  C++ native  |  %.0f FPS",
                  static_cast<double>(fps_));
    shadowText(12.0f, 10.0f, line, 1.8f, {1.0f, 1.0f, 1.0f});

    const ChunkPos playerChunk =
        World::chunkOf(static_cast<int>(std::floor(player_.position.x)),
                       static_cast<int>(std::floor(player_.position.z)));
    std::snprintf(line, sizeof(line),
                  "XYZ %.1f  %.1f  %.1f  %s  seed %llu  chunk %d,%d",
                  static_cast<double>(player_.position.x),
                  static_cast<double>(player_.position.y),
                  static_cast<double>(player_.position.z), player_.flying ? "FLY" : "WALK",
                  static_cast<unsigned long long>(options_.seed), playerChunk.x,
                  playerChunk.z);
    shadowText(12.0f, 32.0f, line, 1.5f, {0.94f, 0.94f, 0.94f});

    const RayHit hit = player_.raycast(*world_, blocks_, 6.0f);
    const std::string looking =
        hit.hit ? blocks_.def(world_->get(hit.x, hit.y, hit.z)).displayName : "-";
    std::snprintf(line, sizeof(line), "Block: %s    Looking at: %s    Tris: %d",
                  selectedBlockName().c_str(), looking.c_str(),
                  worldRenderer_.triangleCount());
    shadowText(12.0f, 52.0f, line, 1.5f, {0.94f, 0.94f, 0.94f});

    std::snprintf(line, sizeof(line), "Time %.2f  Sun %.2f  Wind %.2f  %s  %s",
                  static_cast<double>(timeOfDay_),
                  static_cast<double>(environment_.sunDirection.y),
                  static_cast<double>(windStrength_),
                  timePaused_ ? "PAUSED" : "RUNNING",
                  flashlightOn_ ? "FLASHLIGHT" : "");
    shadowText(12.0f, 72.0f, line, 1.4f, {0.86f, 0.92f, 1.0f});

    const float statusY = static_cast<float>(height) - 112.0f;
    hud_.drawRect(12.0f, statusY - 4.0f, 128.0f, 18.0f, {0.0f, 0.0f, 0.0f}, 0.35f);
    hud_.drawRect(static_cast<float>(width) - 140.0f, statusY - 4.0f, 128.0f, 18.0f,
                  {0.0f, 0.0f, 0.0f}, 0.35f);
    for (int i = 0; i < 10; ++i) {
        const bool full = health_ >= static_cast<float>((i + 1) * 2);
        hud_.drawRect(16.0f + static_cast<float>(i) * 12.0f, statusY, 10.0f, 10.0f,
                      full ? Vec3{0.85f, 0.08f, 0.08f} : Vec3{0.25f, 0.05f, 0.05f}, 0.95f);
        const bool fed = hunger_ >= static_cast<float>((i + 1) * 2);
        hud_.drawRect(static_cast<float>(width) - 136.0f + static_cast<float>(i) * 12.0f,
                      statusY, 10.0f, 10.0f,
                      fed ? Vec3{0.75f, 0.48f, 0.12f} : Vec3{0.22f, 0.14f, 0.05f}, 0.95f);
    }
    if (oxygen_ < 10.0f) {
        const float oxygenWidth = 10.0f * 12.0f;
        const float oxygenX = (static_cast<float>(width) - oxygenWidth) * 0.5f;
        for (int i = 0; i < 10; ++i) {
            const bool full = oxygen_ >= static_cast<float>(i + 1);
            hud_.drawRect(oxygenX + static_cast<float>(i) * 12.0f, statusY - 14.0f, 10.0f,
                          8.0f, full ? Vec3{0.2f, 0.6f, 0.95f} : Vec3{0.05f, 0.12f, 0.2f},
                          0.9f);
        }
    }

    const int slots = static_cast<int>(hotbar_.size());
    if (slots > 0) {
        const float slotSize = 40.0f;
        const float gap = 4.0f;
        const float totalWidth = slots * slotSize + (slots - 1) * gap;
        const float baseX = (static_cast<float>(width) - totalWidth) * 0.5f;
        const float baseY = static_cast<float>(height) - slotSize - 16.0f;

        hud_.drawRect(baseX - 6.0f, baseY - 6.0f, totalWidth + 12.0f, slotSize + 12.0f,
                      {0.0f, 0.0f, 0.0f}, 0.45f);

        for (int i = 0; i < slots; ++i) {
            const float sx = baseX + static_cast<float>(i) * (slotSize + gap);
            const bool selected = i == selectedSlot_;
            if (selected) {
                hud_.drawRect(sx - 2.0f, baseY - 2.0f, slotSize + 4.0f, slotSize + 4.0f,
                              {1.0f, 1.0f, 1.0f}, 0.95f);
            }
            hud_.drawRect(sx, baseY, slotSize, slotSize,
                          selected ? Vec3{0.28f, 0.28f, 0.28f} : Vec3{0.12f, 0.12f, 0.12f},
                          0.9f);
            const ItemStack& stack = hotbar_[static_cast<std::size_t>(i)];
            if (stack.blockId > 0 && stack.count > 0) {
                const BlockDef& block = blocks_.def(stack.blockId);
                const int up = static_cast<int>(Face::Up);
                const Vec3 tint{block.tints[up][0], block.tints[up][1], block.tints[up][2]};
                hud_.drawBlockIcon(sx + 4.0f, baseY + 4.0f, slotSize - 8.0f, block.layers[up],
                                   tint, 0.95f);
                if (stack.count > 1) {
                    const std::string countText = std::to_string(stack.count);
                    const float countScale = 1.1f;
                    const float countWidth = hud_.textWidth(countText, countScale);
                    shadowText(sx + slotSize - 3.0f - countWidth, baseY + slotSize - 15.0f,
                               countText, countScale, {1.0f, 1.0f, 1.0f});
                }
            }
            shadowText(sx + 4.0f, baseY + 3.0f, std::to_string(i + 1), 1.4f,
                       {0.92f, 0.92f, 0.92f});
        }

        const std::string name = selectedBlockName();
        const float nameScale = 1.6f;
        const float textWidth = hud_.textWidth(name, nameScale);
        shadowText((static_cast<float>(width) - textWidth) * 0.5f, baseY - 26.0f, name,
                   nameScale, {1.0f, 1.0f, 1.0f});
    }

    hud_.end();
}

int Game::itemCount(int blockId) const {
    int total = 0;
    for (const ItemStack& stack : hotbar_) {
        if (stack.blockId == blockId && stack.count > 0) {
            total += stack.count;
        }
    }
    for (const ItemStack& stack : storage_) {
        if (stack.blockId == blockId && stack.count > 0) {
            total += stack.count;
        }
    }
    return total;
}

bool Game::addItem(int blockId, int count) {
    if (blockId <= 0 || count <= 0) {
        return false;
    }
    int remaining = count;
    const auto fill = [&](std::vector<ItemStack>& slots) {
        for (ItemStack& stack : slots) {
            if (remaining <= 0) {
                return;
            }
            if (stack.blockId == blockId && stack.count > 0 && stack.count < 64) {
                const int added = std::min(64 - stack.count, remaining);
                stack.count += added;
                remaining -= added;
            }
        }
        for (ItemStack& stack : slots) {
            if (remaining <= 0) {
                return;
            }
            if (stack.blockId == 0 || stack.count <= 0) {
                stack.blockId = blockId;
                stack.count = std::min(64, remaining);
                remaining -= stack.count;
            }
        }
    };
    fill(hotbar_);
    fill(storage_);
    return remaining == 0;
}

bool Game::removeItems(int blockId, int count) {
    if (count <= 0) {
        return true;
    }
    if (itemCount(blockId) < count) {
        return false;
    }
    int remaining = count;
    const auto remove = [&](std::vector<ItemStack>& slots) {
        for (ItemStack& stack : slots) {
            if (remaining <= 0) {
                return;
            }
            if (stack.blockId == blockId && stack.count > 0) {
                const int taken = std::min(stack.count, remaining);
                stack.count -= taken;
                remaining -= taken;
                if (stack.count <= 0) {
                    stack = {};
                }
            }
        }
    };
    remove(hotbar_);
    remove(storage_);
    return remaining == 0;
}

bool Game::craftRecipe(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= recipes_.size()) {
        return false;
    }
    const CraftingRecipe& recipe = recipes_[static_cast<std::size_t>(index)];
    for (const auto& [blockId, count] : recipe.inputs) {
        if (itemCount(blockId) < count) {
            return false;
        }
    }
    for (const auto& [blockId, count] : recipe.inputs) {
        removeItems(blockId, count);
    }
    if (!addItem(recipe.outputId, recipe.outputCount)) {
        return false;
    }
    audio_.playSound("minecraft/sounds/random/click.ogg", 0.35f);
    logInfo("crafted %s x%d", blocks_.def(recipe.outputId).name.c_str(), recipe.outputCount);
    return true;
}

std::array<float, 4> Game::inventoryRecipeRect(int index, int width, int height) const {
    constexpr float slotSize = 40.0f;
    constexpr float gap = 4.0f;
    constexpr float gridWidth = 9 * slotSize + 8 * gap;
    constexpr float gridHeight = 4 * slotSize + 3 * gap;
    constexpr float recipeWidth = 300.0f;
    constexpr float panelGap = 20.0f;
    constexpr float rowHeight = 34.0f;
    const float contentWidth = gridWidth + panelGap + recipeWidth;
    const float originX = (static_cast<float>(width) - contentWidth) * 0.5f;
    const float originY = (static_cast<float>(height) - gridHeight) * 0.5f;
    return {originX + gridWidth + panelGap, originY + static_cast<float>(index) * rowHeight,
            recipeWidth, rowHeight};
}

int Game::inventoryRecipeAt(int mouseX, int mouseY, int width, int height) const {
    for (std::size_t i = 0; i < recipes_.size(); ++i) {
        const auto rect = inventoryRecipeRect(static_cast<int>(i), width, height);
        if (static_cast<float>(mouseX) >= rect[0] && static_cast<float>(mouseX) < rect[0] + rect[2]
            && static_cast<float>(mouseY) >= rect[1]
            && static_cast<float>(mouseY) < rect[1] + rect[3]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Game::applyDamage(float amount) {
    if (dead_ || amount <= 0.0f) {
        return;
    }
    health_ = std::max(0.0f, health_ - amount);
    damageFlashTime_ = 0.35f;
    if (health_ <= 0.0f) {
        dead_ = true;
        player_.velocity = {};
        logInfo("player died");
    }
}

void Game::respawn() {
    player_.position = spawnPosition_;
    player_.velocity = {};
    player_.flying = false;
    health_ = 20.0f;
    hunger_ = 20.0f;
    oxygen_ = 10.0f;
    dead_ = false;
    damageFlashTime_ = 0.0f;
    fallStartY_ = player_.position.y;
    wasOnGround_ = player_.onGround;
}

void Game::updateSurvival(float deltaTime) {
    if (dead_) {
        return;
    }
    const Vec3 eye = player_.eye();
    const int eyeX = static_cast<int>(std::floor(eye.x));
    const int eyeY = static_cast<int>(std::floor(eye.y));
    const int eyeZ = static_cast<int>(std::floor(eye.z));
    const int eyeBlock = world_->get(eyeX, eyeY, eyeZ);
    const bool headInLiquid = blocks_.isLiquid(eyeBlock);
    const bool inLava = headInLiquid && blocks_.def(eyeBlock).name == "lava";

    if (inLava) {
        applyDamage(4.0f * deltaTime);
        oxygen_ = 10.0f;
    } else if (headInLiquid) {
        oxygen_ = std::max(0.0f, oxygen_ - deltaTime);
        if (oxygen_ <= 0.0f) {
            applyDamage(2.0f * deltaTime);
        }
    } else {
        oxygen_ = 10.0f;
    }

    hunger_ = std::max(0.0f, hunger_ - deltaTime * 0.015f);
    if (hunger_ <= 0.0f) {
        applyDamage(0.5f * deltaTime);
    } else if (health_ < 20.0f && hunger_ > 15.0f) {
        health_ = std::min(20.0f, health_ + 0.5f * deltaTime);
        hunger_ = std::max(0.0f, hunger_ - deltaTime * 0.05f);
    }
    damageFlashTime_ = std::max(0.0f, damageFlashTime_ - deltaTime);
}

std::array<float, 4> Game::inventorySlotRect(int index, int width, int height) const {
    constexpr float slotSize = 40.0f;
    constexpr float gap = 4.0f;
    constexpr int columns = 9;
    constexpr int rows = 4;
    constexpr float recipeWidth = 300.0f;
    constexpr float panelGap = 20.0f;
    const float totalWidth = columns * slotSize + (columns - 1) * gap;
    const float totalHeight = rows * slotSize + (rows - 1) * gap;
    const float contentWidth = totalWidth + panelGap + recipeWidth;
    const float originX = (static_cast<float>(width) - contentWidth) * 0.5f;
    const float originY = (static_cast<float>(height) - totalHeight) * 0.5f;
    const int row = index / columns;
    const int column = index % columns;
    return {originX + column * (slotSize + gap), originY + row * (slotSize + gap), slotSize,
            slotSize};
}

int Game::inventorySlotAt(int mouseX, int mouseY, int width, int height) const {
    for (int index = 0; index < 36; ++index) {
        const auto rect = inventorySlotRect(index, width, height);
        if (static_cast<float>(mouseX) >= rect[0] && static_cast<float>(mouseX) < rect[0] + rect[2]
            && static_cast<float>(mouseY) >= rect[1]
            && static_cast<float>(mouseY) < rect[1] + rect[3]) {
            return index;
        }
    }
    return -1;
}

ItemStack* Game::inventoryStackAt(int index) {
    if (index < 0 || index >= 36) {
        return nullptr;
    }
    if (index < 27) {
        if (static_cast<std::size_t>(index) >= storage_.size()) {
            return nullptr;
        }
        return &storage_[static_cast<std::size_t>(index)];
    }
    const int hotbarIndex = index - 27;
    if (hotbarIndex < 0 || static_cast<std::size_t>(hotbarIndex) >= hotbar_.size()) {
        return nullptr;
    }
    return &hotbar_[static_cast<std::size_t>(hotbarIndex)];
}

void Game::toggleInventory() {
    if (inventoryOpen_) {
        closeInventory();
        return;
    }
    inventoryOpen_ = true;
    mouseCaptured_ = false;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
}

void Game::closeInventory() {
    if (heldStack_.blockId > 0 && heldStack_.count > 0) {
        for (ItemStack& stack : storage_) {
            if (stack.blockId == 0 || stack.count <= 0) {
                stack = heldStack_;
                heldStack_ = {};
                break;
            }
        }
        if (heldStack_.blockId > 0) {
            for (ItemStack& stack : hotbar_) {
                if (stack.blockId == 0 || stack.count <= 0) {
                    stack = heldStack_;
                    heldStack_ = {};
                    break;
                }
            }
        }
        heldStack_ = {};
    }
    inventoryOpen_ = false;
    mouseCaptured_ = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
}

void Game::handleInventoryClick(int mouseX, int mouseY, int button) {
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window_, &width, &height);
    const int recipeIndex = inventoryRecipeAt(mouseX, mouseY, width, height);
    if (recipeIndex >= 0) {
        craftRecipe(recipeIndex);
        return;
    }
    const int slotIndex = inventorySlotAt(mouseX, mouseY, width, height);
    if (slotIndex < 0) {
        return;
    }
    ItemStack* target = inventoryStackAt(slotIndex);
    if (!target) {
        return;
    }

    if (button == SDL_BUTTON_LEFT) {
        const bool heldEmpty = heldStack_.blockId == 0 || heldStack_.count <= 0;
        const bool targetEmpty = target->blockId == 0 || target->count <= 0;
        if (heldEmpty) {
            heldStack_ = *target;
            *target = {};
        } else if (targetEmpty) {
            *target = heldStack_;
            heldStack_ = {};
        } else {
            std::swap(*target, heldStack_);
        }
    } else if (button == SDL_BUTTON_RIGHT && slotIndex >= 27) {
        selectedSlot_ = slotIndex - 27;
    }
}

void Game::drawInventory(int width, int height) {
    constexpr float slotSize = 40.0f;
    constexpr float gap = 4.0f;
    constexpr int columns = 9;
    constexpr int rows = 4;
    constexpr float recipeWidth = 300.0f;
    constexpr float recipeRowHeight = 34.0f;
    constexpr float panelGap = 20.0f;
    const float totalWidth = columns * slotSize + (columns - 1) * gap;
    const float totalHeight = rows * slotSize + (rows - 1) * gap;
    const float contentWidth = totalWidth + panelGap + recipeWidth;
    const float originX = (static_cast<float>(width) - contentWidth) * 0.5f;
    const float originY = (static_cast<float>(height) - totalHeight) * 0.5f;
    const float recipeHeight =
        std::max(totalHeight, recipeRowHeight * static_cast<float>(recipes_.size())) + 6.0f;

    hud_.drawRect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                  {0.0f, 0.0f, 0.0f}, 0.55f);
    hud_.drawRect(originX - 14.0f, originY - 48.0f, totalWidth + 28.0f, totalHeight + 64.0f,
                  {0.08f, 0.08f, 0.10f}, 0.94f);
    hud_.drawRect(originX + totalWidth + panelGap - 12.0f, originY - 48.0f,
                  recipeWidth + 24.0f, recipeHeight + 62.0f, {0.08f, 0.08f, 0.10f}, 0.94f);
    hud_.drawText(originX, originY - 38.0f, "Inventory", 2.0f, {1.0f, 1.0f, 1.0f});
    hud_.drawText(originX + totalWidth + panelGap, originY - 38.0f, "Crafting", 2.0f,
                  {1.0f, 1.0f, 1.0f});

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    const int hovered = inventorySlotAt(mouseX, mouseY, width, height);
    const int hoveredRecipe = inventoryRecipeAt(mouseX, mouseY, width, height);

    for (int index = 0; index < 36; ++index) {
        const auto rect = inventorySlotRect(index, width, height);
        const bool hotbarSlot = index >= 27;
        const bool selected = hotbarSlot && index - 27 == selectedSlot_;
        if (selected) {
            hud_.drawRect(rect[0] - 2.0f, rect[1] - 2.0f, rect[2] + 4.0f, rect[3] + 4.0f,
                          {1.0f, 1.0f, 1.0f}, 0.95f);
        }
        hud_.drawRect(rect[0], rect[1], rect[2], rect[3],
                      hovered == index ? Vec3{0.30f, 0.30f, 0.30f}
                                       : Vec3{0.14f, 0.14f, 0.18f},
                      0.96f);
        ItemStack* stack = inventoryStackAt(index);
        if (stack && stack->blockId > 0 && stack->count > 0) {
            const BlockDef& block = blocks_.def(stack->blockId);
            const int up = static_cast<int>(Face::Up);
            const Vec3 tint{block.tints[up][0], block.tints[up][1], block.tints[up][2]};
            hud_.drawBlockIcon(rect[0] + 4.0f, rect[1] + 4.0f, rect[2] - 8.0f,
                               block.layers[up], tint, 0.98f);
            if (stack->count > 1) {
                const std::string countText = std::to_string(stack->count);
                const float countScale = 1.1f;
                const float countWidth = hud_.textWidth(countText, countScale);
                hud_.drawText(rect[0] + rect[2] - 3.0f - countWidth, rect[1] + rect[3] - 15.0f,
                              countText, countScale, {1.0f, 1.0f, 1.0f});
            }
        }
        if (hotbarSlot) {
            hud_.drawText(rect[0] + 3.0f, rect[1] + 2.0f, std::to_string(index - 26), 1.1f,
                          {0.75f, 0.75f, 0.75f});
        }
    }

    for (std::size_t index = 0; index < recipes_.size(); ++index) {
        const auto rect = inventoryRecipeRect(static_cast<int>(index), width, height);
        bool craftable = true;
        std::string description =
            std::to_string(recipes_[index].outputCount) + "x "
            + blocks_.def(recipes_[index].outputId).name + "  <-  ";
        for (const auto& [blockId, count] : recipes_[index].inputs) {
            if (itemCount(blockId) < count) {
                craftable = false;
            }
            description += std::to_string(count) + "x " + blocks_.def(blockId).name + "  ";
        }
        hud_.drawRect(rect[0], rect[1], rect[2], rect[3],
                      hoveredRecipe == static_cast<int>(index) ? Vec3{0.30f, 0.30f, 0.30f}
                                                              : Vec3{0.14f, 0.14f, 0.18f},
                      0.96f);
        const BlockDef& output = blocks_.def(recipes_[index].outputId);
        const int up = static_cast<int>(Face::Up);
        const Vec3 tint{output.tints[up][0], output.tints[up][1], output.tints[up][2]};
        hud_.drawBlockIcon(rect[0] + 4.0f, rect[1] + 4.0f, 24.0f, output.layers[up], tint,
                           0.98f);
        hud_.drawText(rect[0] + 34.0f, rect[1] + 9.0f, description, 1.0f,
                      craftable ? Vec3{0.92f, 0.92f, 0.92f} : Vec3{0.45f, 0.45f, 0.45f});
    }

    if (heldStack_.blockId > 0 && heldStack_.count > 0) {
        const BlockDef& block = blocks_.def(heldStack_.blockId);
        const int up = static_cast<int>(Face::Up);
        const Vec3 tint{block.tints[up][0], block.tints[up][1], block.tints[up][2]};
        hud_.drawBlockIcon(static_cast<float>(mouseX) - 20.0f, static_cast<float>(mouseY) - 20.0f,
                           40.0f, block.layers[up], tint, 1.0f);
        if (heldStack_.count > 1) {
            hud_.drawText(static_cast<float>(mouseX) + 4.0f, static_cast<float>(mouseY) + 4.0f,
                          std::to_string(heldStack_.count), 1.2f, {1.0f, 1.0f, 1.0f});
        }
    }

    hud_.drawText(originX, originY + std::max(totalHeight, recipeHeight) + 8.0f,
                  "Left click: move/craft    Right click hotbar: select    E: close", 1.3f,
                  {0.85f, 0.85f, 0.85f});
}

int Game::selectedBlockId() const {
    if (hotbar_.empty()) {
        return 0;
    }
    const std::size_t index =
        static_cast<std::size_t>(selectedSlot_ < 0 ? 0 : selectedSlot_) % hotbar_.size();
    const ItemStack& stack = hotbar_[index];
    return stack.count > 0 ? stack.blockId : 0;
}

std::string Game::selectedBlockName() const {
    const int blockId = selectedBlockId();
    if (blockId <= 0) {
        return "Air";
    }
    const BlockDef& block = blocks_.def(blockId);
    return block.displayName.empty() ? block.name : block.displayName;
}

void Game::handleBreak() {
    if (!world_) {
        return;
    }
    const RayHit hit = player_.raycast(*world_, blocks_, 6.0f);
    if (!hit.hit) {
        return;
    }
    const int blockId = world_->get(hit.x, hit.y, hit.z);
    if (blockId == 0) {
        return;
    }
    attackSwing_ = 1.0f;
    world_->set(hit.x, hit.y, hit.z, 0);
    const int tallGrass = blocks_.id("tall_grass");
    if (tallGrass > 0 && world_->get(hit.x, hit.y + 1, hit.z) == tallGrass) {
        world_->set(hit.x, hit.y + 1, hit.z, 0);
    }
    const BlockDef& brokenBlock = blocks_.def(blockId);
    if (brokenBlock.name != "bedrock" && !brokenBlock.cross
        && !addItem(blockId, 1)) {
        logWarn("inventory full, lost %s", brokenBlock.name.c_str());
    }
    worldRenderer_.rebuildAround(*world_, blocks_, hit.x, hit.z);
    audio_.playSound(digSound(blockMaterial(brokenBlock.name), soundCounter_++), 0.7f);
    logInfo("broke %s at %d %d %d", brokenBlock.name.c_str(), hit.x, hit.y, hit.z);
}

void Game::handlePlace() {
    if (!world_) {
        return;
    }
    const RayHit hit = player_.raycast(*world_, blocks_, 6.0f);
    if (!hit.hit) {
        return;
    }
    const int blockId = selectedBlockId();
    if (blockId <= 0) {
        return;
    }

    int px = hit.previousX;
    int py = hit.previousY;
    int pz = hit.previousZ;
    const int hitBlockId = world_->get(hit.x, hit.y, hit.z);
    if (blocks_.def(hitBlockId).cross) {
        px = hit.x;
        py = hit.y;
        pz = hit.z;
    }
    if (py < 0 || py >= World::kHeight) {
        return;
    }
    const int existing = world_->get(px, py, pz);
    if (existing != 0 && blocks_.isSolid(existing)) {
        return;
    }

    const Aabb target{{static_cast<float>(px), static_cast<float>(py),
                       static_cast<float>(pz)},
                      {static_cast<float>(px + 1), static_cast<float>(py + 1),
                       static_cast<float>(pz + 1)}};
    if (player_.boxAt(player_.position).intersects(target)) {
        return;
    }

    attackSwing_ = 1.0f;
    world_->set(px, py, pz, static_cast<std::uint8_t>(blockId));
    worldRenderer_.rebuildAround(*world_, blocks_, px, pz);
    audio_.playSound(digSound(blockMaterial(blocks_.def(blockId).name), soundCounter_++), 0.6f);
    if (!hotbar_.empty()) {
        ItemStack& stack =
            hotbar_[static_cast<std::size_t>(selectedSlot_) % hotbar_.size()];
        if (stack.count > 0) {
            --stack.count;
            if (stack.count <= 0) {
                stack = {};
            }
        }
    }
    logInfo("placed %s at %d %d %d", blocks_.def(blockId).name.c_str(), px, py, pz);
}

void Game::takeScreenshot(const std::string& path) {
    if (path.empty()) {
        return;
    }
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glFinish();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    std::vector<std::uint8_t> flipped(pixels.size());
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(flipped.data() + static_cast<std::size_t>(y) * rowBytes,
                    pixels.data() + static_cast<std::size_t>(height - 1 - y) * rowBytes,
                    rowBytes);
    }

    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::error_code errorCode;
        std::filesystem::create_directories(target.parent_path(), errorCode);
    }

    std::string error;
    if (!writePng(path, width, height, flipped.data(), &error)) {
        logError("screenshot failed: %s", error.c_str());
    } else {
        logInfo("screenshot saved: %s (%dx%d)", path.c_str(), width, height);
    }
}

void Game::renderRaytrace(const std::string& path) {
    constexpr int width = 1280;
    constexpr int height = 720;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);

    const Vec3 eye = player_.eye();
    const Vec3 forward = player_.forward();
    Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    if (lengthSquared(right) < 1e-6f) {
        right = {1.0f, 0.0f, 0.0f};
    }
    const Vec3 up = normalize(cross(right, forward));
    const float tangent = std::tan(radians(70.0f) * 0.5f);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Environment environment = environment_;

    std::atomic<int> nextRow{0};
    unsigned int threadCount = std::thread::hardware_concurrency();
    threadCount = std::max(1u, std::min(16u, threadCount));

    const auto worker = [&]() {
        while (true) {
            const int row = nextRow.fetch_add(1);
            if (row >= height) {
                return;
            }
            for (int column = 0; column < width; ++column) {
                const float ndcX = (2.0f * (static_cast<float>(column) + 0.5f) / width - 1.0f)
                                 * tangent * aspect;
                const float ndcY = (1.0f - 2.0f * (static_cast<float>(row) + 0.5f) / height)
                                 * tangent;
                const Vec3 direction = normalize(forward + right * ndcX + up * ndcY);

                const VoxelRayHit hit =
                    traceVoxelRay(*world_, blocks_, eye, direction, 110.0f, false);

                Vec3 color;
                if (!hit.hit) {
                    color = raySkyColor(direction, environment);
                } else {
                    const int blockId = world_->get(hit.x, hit.y, hit.z);
                    const BlockDef& block = blocks_.def(blockId);
                    const int faceIndex = hit.face;
                    const int layer = block.layers[static_cast<std::size_t>(faceIndex)];
                    const Vec3 point = eye + direction * hit.distance;
                    const Vec3 local{point.x - static_cast<float>(hit.x),
                                     point.y - static_cast<float>(hit.y),
                                     point.z - static_cast<float>(hit.z)};
                    float u = 0.0f;
                    float v = 0.0f;
                    switch (faceIndex) {
                    case 0:
                        u = 1.0f - local.z;
                        v = 1.0f - local.y;
                        break;
                    case 1:
                        u = local.z;
                        v = 1.0f - local.y;
                        break;
                    case 2:
                        u = local.x;
                        v = 1.0f - local.z;
                        break;
                    case 3:
                        u = local.x;
                        v = local.z;
                        break;
                    case 4:
                        u = local.x;
                        v = 1.0f - local.y;
                        break;
                    default:
                        u = 1.0f - local.x;
                        v = 1.0f - local.y;
                        break;
                    }
                    const Vec3 base = textures_.sampleNearest(layer, u, v);
                    Vec3 albedo{
                        base.x * block.tints[static_cast<std::size_t>(faceIndex)][0],
                        base.y * block.tints[static_cast<std::size_t>(faceIndex)][1],
                        base.z * block.tints[static_cast<std::size_t>(faceIndex)][2],
                    };

                    const Vec3 normal = faceNormal(faceIndex);
                    const float sun = std::max(dot(normal, environment.sunDirection), 0.0f);
                    bool shadowed = false;
                    if (sun > 0.0f && environment.sunDirection.y > 0.0f) {
                        const Vec3 point = eye + direction * hit.distance;
                        const Vec3 shadowOrigin{
                            point.x + normal.x * 0.51f,
                            point.y + normal.y * 0.51f,
                            point.z + normal.z * 0.51f,
                        };
                        shadowed = traceVoxelRay(*world_, blocks_, shadowOrigin,
                                                 environment.sunDirection, 120.0f, false)
                                       .hit;
                    }

                    const Vec3 ambient = environment.ambientGround
                                       + (environment.ambientSky - environment.ambientGround)
                                             * (normal.y * 0.5f + 0.5f);
                    const Vec3 light = ambient
                                     + environment.sunColor * (sun * (shadowed ? 0.0f : 1.0f));
                    color = {albedo.x * light.x, albedo.y * light.y, albedo.z * light.z};

                    const float fog = clampf((hit.distance - 45.0f) / (140.0f - 45.0f), 0.0f,
                                             1.0f);
                    color = color + (environment.fogColor - color) * fog;
                }

                std::uint8_t* pixel =
                    pixels.data() + (static_cast<std::size_t>(row) * width + column) * 4;
                pixel[0] = static_cast<std::uint8_t>(
                    clampf(color.x, 0.0f, 1.0f) * 255.0f);
                pixel[1] = static_cast<std::uint8_t>(
                    clampf(color.y, 0.0f, 1.0f) * 255.0f);
                pixel[2] = static_cast<std::uint8_t>(
                    clampf(color.z, 0.0f, 1.0f) * 255.0f);
                pixel[3] = 255;
            }
        }
    };

    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i) {
        workers.emplace_back(worker);
    }
    for (std::thread& thread : workers) {
        thread.join();
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start);

    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::error_code errorCode;
        std::filesystem::create_directories(target.parent_path(), errorCode);
    }

    std::string error;
    if (!writePng(path, width, height, pixels.data(), &error)) {
        logError("raytrace failed: %s", error.c_str());
    } else {
        logInfo("raytrace test saved: %s (%dx%d, %.2fs, %u threads)", path.c_str(), width,
                height, elapsed.count(), threadCount);
    }
}

int Game::run() {
    if (!options_.raytracePath.empty()) {
        renderRaytrace(options_.raytracePath);
        return 0;
    }

    bool running = true;
    lastCounter_ = SDL_GetPerformanceCounter();

    while (running) {
        processEvents(running);

        const std::uint64_t now = SDL_GetPerformanceCounter();
        float deltaTime = static_cast<float>(now - lastCounter_)
                        / static_cast<float>(SDL_GetPerformanceFrequency());
        lastCounter_ = now;
        if (screenshotMode_) {
            deltaTime = 1.0f / 60.0f;
        }

        update(deltaTime);
        render();

        if (screenshotRequested_) {
            screenshotRequested_ = false;
            takeScreenshot(screenshotRequestPath_);
        }

        if (screenshotMode_) {
            ++screenshotFrameCounter_;
            if (screenshotFrameCounter_ >= options_.screenshotFrames) {
                takeScreenshot(options_.screenshotPath);
                running = false;
            }
        }

        SDL_GL_SwapWindow(window_);
    }

    return 0;
}

void Game::shutdown() {
    if (window_) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
    if (world_) {
        world_->saveAll();
    }
    audio_.shutdown();
    hud_.destroy();
    playerRenderer_.destroy();
    particleRenderer_.destroy();
    sky_.destroy();
    reflection_.destroy();
    worldRenderer_.destroy();
    textures_.destroy();
    world_.reset();
    if (context_) {
        SDL_GL_DeleteContext(context_);
        context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

}
