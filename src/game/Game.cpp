#include "game/Game.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <thread>
#include <vector>

#include "core/Log.hpp"
#include "render/GL.hpp"
#include "util/Image.hpp"

namespace nc {
namespace {

std::string blockMaterial(const std::string& name) {
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
    }

    const std::string saveDirectory = "saves/" + std::to_string(options_.seed);
    world_ = std::make_unique<World>(options_.seed, saveDirectory, options_.workerThreads);

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
    for (const std::string& name : blocks_.hotbarNames()) {
        const int blockId = blocks_.id(name);
        if (blockId > 0) {
            hotbar_.push_back(blockId);
        }
    }
    if (hotbar_.empty()) {
        hotbar_.push_back(blocks_.id("stone") > 0 ? blocks_.id("stone") : 1);
    }

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
                running = false;
                break;
            case SDLK_f:
                player_.flying = !player_.flying;
                player_.velocity = {};
                logInfo("fly mode %s", player_.flying ? "enabled" : "disabled");
                break;
            case SDLK_m:
                mouseCaptured_ = !mouseCaptured_;
                SDL_SetRelativeMouseMode(mouseCaptured_ ? SDL_TRUE : SDL_FALSE);
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
            if (mouseCaptured_ && !screenshotMode_) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    handleBreak();
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    handlePlace();
                }
            }
            break;
        case SDL_MOUSEWHEEL:
            if (!hotbar_.empty() && event.wheel.y != 0) {
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

    player_.update(*world_, blocks_, input, deltaTime);

    const ChunkPos movedCenter =
        World::chunkOf(static_cast<int>(std::floor(player_.position.x)),
                       static_cast<int>(std::floor(player_.position.z)));
    if (movedCenter.x != lastStreamCenter_.x || movedCenter.z != lastStreamCenter_.z) {
        world_->updateStreaming(player_.position, options_.renderDistance, blocks_);
        worldRenderer_.syncMeshes(*world_, blocks_, movedCenter, options_.renderDistance,
                                  options_.grassDistance);
        lastStreamCenter_ = movedCenter;
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

void Game::render() {
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    const float viewDistance = static_cast<float>(options_.renderDistance * World::kChunkSize);
    const float fogStart = viewDistance * 0.42f;
    const float fogEnd = viewDistance * 0.92f;

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

    const Vec3 eye = player_.eye();
    const Vec3 reflectedEye{eye.x, 2.0f * waterLevel_ - eye.y, eye.z};
    const Vec3 forward = player_.forward();
    const Vec3 reflectedForward{forward.x, -forward.y, forward.z};
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
        worldRenderer_.drawOpaque(reflectionViewProjection, reflectedEye, environment_, fogStart,
                                  fogEnd, true, waterLevel_, true);
        reflection_.unbind();
    }

    glViewport(0, 0, width, height);
    glClearColor(environment_.fogColor.x, environment_.fogColor.y, environment_.fogColor.z,
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Mat4 projection = Mat4::perspective(radians(70.0f), aspect, 0.1f, 300.0f);
    const Mat4 view = player_.viewMatrix();
    const Mat4 viewProjection = projection * view;

    sky_.render(viewProjection.inverse(), eye, environment_, elapsedTime_);
    worldRenderer_.drawOpaque(viewProjection, eye, environment_, fogStart, fogEnd, false, 0.0f,
                              false);
    worldRenderer_.drawGrass(viewProjection, eye, environment_, fogStart, fogEnd, elapsedTime_,
                             player_.position, windDirection_, windStrength_);
    worldRenderer_.drawTransparent(viewProjection, eye, environment_, fogStart, fogEnd);
    worldRenderer_.drawWater(viewProjection, eye, environment_, fogStart, fogEnd,
                             reflection_.colorTexture(), reflectionViewProjection,
                             elapsedTime_, waterLevel_);
    drawHud();
}

void Game::drawHud() {
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window_, &width, &height);

    hud_.begin(width, height);
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

    std::snprintf(line, sizeof(line), "Time %.2f  Sun %.2f  Wind %.2f  %s",
                  static_cast<double>(timeOfDay_),
                  static_cast<double>(environment_.sunDirection.y),
                  static_cast<double>(windStrength_),
                  timePaused_ ? "PAUSED" : "RUNNING");
    shadowText(12.0f, 72.0f, line, 1.4f, {0.86f, 0.92f, 1.0f});

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
            const int blockId = hotbar_[static_cast<std::size_t>(i)];
            if (blockId > 0) {
                const BlockDef& block = blocks_.def(blockId);
                const int up = static_cast<int>(Face::Up);
                const Vec3 tint{block.tints[up][0], block.tints[up][1], block.tints[up][2]};
                hud_.drawBlockIcon(sx + 4.0f, baseY + 4.0f, slotSize - 8.0f, block.layers[up],
                                   tint, 0.95f);
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

int Game::selectedBlockId() const {
    if (hotbar_.empty()) {
        return 0;
    }
    const std::size_t index =
        static_cast<std::size_t>(selectedSlot_ < 0 ? 0 : selectedSlot_) % hotbar_.size();
    return hotbar_[index];
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
    world_->set(hit.x, hit.y, hit.z, 0);
    const int tallGrass = blocks_.id("tall_grass");
    if (tallGrass > 0 && world_->get(hit.x, hit.y + 1, hit.z) == tallGrass) {
        world_->set(hit.x, hit.y + 1, hit.z, 0);
    }
    worldRenderer_.rebuildAround(*world_, blocks_, hit.x, hit.z);
    audio_.playSound(digSound(blockMaterial(blocks_.def(blockId).name), soundCounter_++), 0.7f);
    logInfo("broke %s at %d %d %d", blocks_.def(blockId).name.c_str(), hit.x, hit.y, hit.z);
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

    world_->set(px, py, pz, static_cast<std::uint8_t>(blockId));
    worldRenderer_.rebuildAround(*world_, blocks_, px, pz);
    audio_.playSound(digSound(blockMaterial(blocks_.def(blockId).name), soundCounter_++), 0.6f);
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
    constexpr int width = 640;
    constexpr int height = 360;
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
                    const Vec3 base = textures_.averageColor(layer);
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
