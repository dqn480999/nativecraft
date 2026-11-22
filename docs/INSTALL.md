# Installation

## 1. Download

```bash
git clone https://github.com/dqn480999/nativecraft.git
cd nativecraft
```

There are no prebuilt binaries. Build from source with the steps below.

## 2. Install dependencies

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake sdl2 libpng zlib openal libvorbis
```

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libpng-dev zlib1g-dev \
  libopenal-dev libvorbis-dev
```

### Fedora

```bash
sudo dnf install gcc-c++ cmake SDL2-devel libpng-devel zlib-devel \
  openal-soft-devel libvorbis-devel
```

SDL2, OpenGL, libpng, zlib, OpenAL, and Vorbis development headers are required. GLFW, GLM, and JSON libraries are not used.

## 3. Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The executable is `build/nativecraft`.

## 4. Prepare game assets

NativeCraft reads assets from an existing Minecraft 1.12.2 installation. Assets are not downloaded by this project and are not included in the repository.

The Minecraft directory must contain:

```text
.minecraft/
  versions/1.12.2/1.12.2.jar
  assets/indexes/1.12.json
  assets/objects/
```

The official launcher creates this layout after Minecraft 1.12.2 has been started at least once.

If the assets are in a different location, pass it explicitly:

```bash
./build/nativecraft --mc-dir /path/to/.minecraft
```

## 5. Run

```bash
./build/nativecraft --mc-dir "$HOME/.minecraft"
```

Optional arguments:

```text
--width N                 window width, default 1280
--height N                window height, default 720
--seed N                  world seed, default 12345
--render-distance N       chunk render radius, default 6
--grass-distance N        radius with grass meshes, default 3
--threads N               chunk generation threads, default: auto
--time T                  time of day in [0,1], default 0.32
--time-speed S            time advance per second, default 0.0025
--camera X Y Z YAW PITCH  override the camera
--screenshot PATH         render a PNG and exit
--raytrace PATH           software ray tracing test render, 640x360
--frames N                frames to render before the screenshot
--help                    show usage
```

## 6. Offline render check

This does not require mouse or keyboard input:

```bash
./build/nativecraft --mc-dir "$HOME/.minecraft" \
  --screenshot shots/check.png --frames 90
```

The game window is still created because rendering requires an OpenGL context.

## Troubleshooting

### Minecraft 1.12.2 jar not found

Check that `versions/1.12.2/1.12.2.jar` exists under the directory passed with `--mc-dir`.

### OpenGL context creation failed

NativeCraft requires OpenGL 3.3. Verify the driver:

```bash
glxinfo -B
```

The output must report a core OpenGL version of at least 3.3.

### Sounds are missing

OpenAL must find a working audio device. The client continues without audio if OpenAL initialization fails.

### Textures are missing or show a checkerboard

Check that `assets/indexes/1.12.json` and `assets/objects/` are present and complete. Re-download the assets with the official launcher if needed.

### Save files

Modified chunks are written to `saves/<seed>/chunks/<x>_<z>.nc`. Unmodified chunks are regenerated from the seed. Delete the seed directory to reset a world.

### Performance

Shadows use a 4096 near cascade (22 blocks, includes grass) and a 4096 far cascade (130 blocks, covering the render distance) with hardware PCF. The near cascade refreshes ten times per second; the far cascade is cached. Water reflections render the scene a second time at the window resolution. Chunk generation and disk loading run on a worker pool. Mesh construction is limited to two chunks per frame on the render thread. The default render radius of 6 chunks keeps more than 200 chunks loaded and about 400,000 triangles in view. Reduce `--render-distance`, `--grass-distance`, and `--threads` on slower CPUs or GPUs.

### Wayland

SDL2 uses X11 by default. If the window fails to open on a Wayland session, run through XWayland or set:

```bash
SDL_VIDEODRIVER=x11 ./build/nativecraft --mc-dir "$HOME/.minecraft"
```
