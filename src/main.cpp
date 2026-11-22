#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/Log.hpp"
#include "game/Game.hpp"

namespace {

void printUsage() {
    std::fprintf(stderr,
                 "NativeCraft — native C++ client using Minecraft 1.12.2 assets\n"
                 "Usage: nativecraft [options]\n"
                 "  --mc-dir PATH      Minecraft directory (default: $HOME/.minecraft)\n"
                 "  --width N          window width (default 1280)\n"
                 "  --height N         window height (default 720)\n"
                 "  --seed N           world seed (default 12345)\n"
                 "  --screenshot PATH  render to PATH and exit\n"
                 "  --raytrace PATH    software ray tracing test render and exit\n"
                 "  --frames N         frames before screenshot (default 90)\n"
                 "  --time T           time of day in [0,1] (default 0.32)\n"
                 "  --time-speed S     time advance per second (default 0.0025)\n"
                 "  --render-distance N  chunk render radius (default 6)\n"
                 "  --grass-distance N   grass mesh radius (default 3)\n"
                 "  --threads N          chunk generation threads (default: auto)\n"
                 "  --third-person       start in third-person view\n"
                 "  --camera X Y Z YAW PITCH  override camera (debug/screenshots)\n"
                 "  --help             show this help\n");
}

bool parseU64(const char* text, std::uint64_t& out) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
}

bool parseFloat(const char* text, float& out) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (!end || *end != '\0') {
        return false;
    }
    out = value;
    return true;
}

bool parseInt(const char* text, int& out, int minimum, int maximum) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < minimum || value > maximum) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

}

int main(int argc, char** argv) {
    nc::GameOptions options;
    if (const char* home = std::getenv("HOME")) {
        options.minecraftDir = std::string(home) + "/.minecraft";
    } else {
        options.minecraftDir = ".minecraft";
    }

    for (int i = 1; i < argc; ++i) {
        const char* argument = argv[i];
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                nc::logError("missing value for %s", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (std::strcmp(argument, "--help") == 0 || std::strcmp(argument, "-h") == 0) {
            printUsage();
            return 0;
        } else if (std::strcmp(argument, "--mc-dir") == 0) {
            const char* value = requireValue("--mc-dir");
            if (!value) {
                return 1;
            }
            options.minecraftDir = value;
        } else if (std::strcmp(argument, "--width") == 0) {
            const char* value = requireValue("--width");
            if (!value || !parseInt(value, options.width, 320, 7680)) {
                nc::logError("invalid --width");
                return 1;
            }
        } else if (std::strcmp(argument, "--height") == 0) {
            const char* value = requireValue("--height");
            if (!value || !parseInt(value, options.height, 240, 4320)) {
                nc::logError("invalid --height");
                return 1;
            }
        } else if (std::strcmp(argument, "--seed") == 0) {
            const char* value = requireValue("--seed");
            if (!value || !parseU64(value, options.seed)) {
                nc::logError("invalid --seed");
                return 1;
            }
        } else if (std::strcmp(argument, "--screenshot") == 0) {
            const char* value = requireValue("--screenshot");
            if (!value) {
                return 1;
            }
            options.screenshotPath = value;
        } else if (std::strcmp(argument, "--raytrace") == 0) {
            const char* value = requireValue("--raytrace");
            if (!value) {
                return 1;
            }
            options.raytracePath = value;
        } else if (std::strcmp(argument, "--frames") == 0) {
            const char* value = requireValue("--frames");
            if (!value || !parseInt(value, options.screenshotFrames, 1, 100000)) {
                nc::logError("invalid --frames");
                return 1;
            }
        } else if (std::strcmp(argument, "--time") == 0) {
            const char* value = requireValue("--time");
            if (!value || !parseFloat(value, options.timeOfDay)) {
                nc::logError("invalid --time");
                return 1;
            }
        } else if (std::strcmp(argument, "--time-speed") == 0) {
            const char* value = requireValue("--time-speed");
            if (!value || !parseFloat(value, options.timeSpeed)) {
                nc::logError("invalid --time-speed");
                return 1;
            }
        } else if (std::strcmp(argument, "--render-distance") == 0) {
            const char* value = requireValue("--render-distance");
            if (!value || !parseInt(value, options.renderDistance, 1, 16)) {
                nc::logError("invalid --render-distance");
                return 1;
            }
        } else if (std::strcmp(argument, "--grass-distance") == 0) {
            const char* value = requireValue("--grass-distance");
            if (!value || !parseInt(value, options.grassDistance, 0, 16)) {
                nc::logError("invalid --grass-distance");
                return 1;
            }
        } else if (std::strcmp(argument, "--threads") == 0) {
            const char* value = requireValue("--threads");
            if (!value || !parseInt(value, options.workerThreads, 0, 16)) {
                nc::logError("invalid --threads");
                return 1;
            }
        } else if (std::strcmp(argument, "--third-person") == 0) {
            options.thirdPerson = true;
        } else if (std::strcmp(argument, "--camera") == 0) {
            float values[5] = {};
            for (float& target : values) {
                const char* value = requireValue("--camera");
                if (!value || !parseFloat(value, target)) {
                    nc::logError("invalid --camera (expected X Y Z YAW PITCH)");
                    return 1;
                }
            }
            options.hasCamera = true;
            options.cameraPosition = {values[0], values[1], values[2]};
            options.cameraYaw = values[3];
            options.cameraPitch = values[4];
        } else {
            nc::logError("unknown argument: %s", argument);
            printUsage();
            return 1;
        }
    }

    if (!options.screenshotPath.empty() && options.screenshotFrames <= 0) {
        options.screenshotFrames = 90;
    }

    nc::Game game;
    std::string error;
    if (!game.init(options, &error)) {
        nc::logError("%s", error.c_str());
        game.shutdown();
        return 1;
    }

    const int result = game.run();
    game.shutdown();
    return result;
}
