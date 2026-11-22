# NativeCraft

A native C++20 Minecraft-style client with an infinite procedural world. It does not use Java or the JVM. Rendering, multithreaded terrain generation, chunk streaming, persistence, and audio are implemented from scratch.

Textures, block models, blockstates, the bitmap font, language files, and sounds are read from a local Minecraft 1.12.2 installation. The repository does not contain Minecraft assets.

## Screenshots

![World](docs/screenshots/world.png)

![Wind and player reactive grass](docs/screenshots/grass.png)

![Water reflections and waves](docs/screenshots/water.png)

![Sun](docs/screenshots/sun.png)

![Night](docs/screenshots/night.png)

![World overview](docs/screenshots/overview.png)

![Software ray tracing test](docs/screenshots/raytrace.png)

## Requirements

- C++20 compiler (GCC 16 tested)
- CMake 3.20 or newer
- SDL2
- OpenGL 3.3
- libpng
- zlib
- OpenAL
- libvorbis and libvorbisfile

Installation instructions for Arch, Debian/Ubuntu, and Fedora are in [docs/INSTALL.md](docs/INSTALL.md).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

```bash
./build/nativecraft --mc-dir "$HOME/.minecraft"
```

The directory must contain:

```text
versions/1.12.2/1.12.2.jar
assets/indexes/1.12.json
assets/objects/
```

Worlds are generated from a seed. Modified chunk data is stored under `saves/<seed>/chunks/` and reloaded on demand.

Useful options:

```text
--seed N                world seed
--render-distance N     chunk render radius, default 6
--grass-distance N      radius with grass meshes, default 3
--threads N             chunk generation threads, default: auto
--time T                time of day in [0,1]
--time-speed S          day cycle speed
--camera X Y Z YAW PITCH
--screenshot PATH
--raytrace PATH         software ray tracing test render, 1280x720
--frames N
```

## Controls

| Input | Action |
|---|---|
| Mouse | Look |
| W/A/S/D | Move |
| Space | Jump or fly up |
| Ctrl | Fly down |
| Shift | Sprint |
| F | Toggle flight |
| M | Toggle mouse capture |
| T | Pause or resume the day cycle |
| 1-9, mouse wheel | Select hotbar slot |
| Left click | Break block |
| Right click | Place block |
| F2 | Save screenshot to `screenshots/` |
| Esc | Exit |

## Rendering and world features

- Infinite chunk streaming around the player with deterministic seed-based generation.
- Chunk generation and disk loading run on a worker pool; mesh construction is budgeted to two chunks per frame.
- Chunk unloading and binary persistence for modified chunks.
- Multiple procedural grass types: tall grass, meadow grass, dry grass, and fern with distinct colors, blade counts, and heights.
- Grass reacts to changing wind direction, gust strength, and the player's position.
- Two-cascade directional shadows: 4096 near cascade for sharp grass and terrain shadows plus a 4096 far cascade covering the render distance, both with a 16-tap rotated Poisson PCF kernel for soft shadow edges, and cached updates.
- Separate near/far normal offsets and bias values reduce sunlight leaks in dark areas.
- Breakable grass with a raycast hitbox: aim and left-click to remove it, or right-click to replace it with a block.
- Procedural moving clouds, sun, moon, and stars.
- Planar water reflections, animated waves, ripples, Fresnel reflections, and specular highlights.
- Frustum culling for shadow, reflection, opaque, grass, transparent, and water passes.
- Grass LOD: detailed grass meshes are limited to a configurable radius.

## Current scale

- 82 blocks, 101 unique block textures, 103 texture-array layers.
- Default render radius: 6 chunks (169 meshed chunks, 225+ loaded).
- About 400,000 triangles in the default view.
- About 60 FPS at 1280x720 on a GTX 970.

## Limitations

- No inventory, item entities, crafting, mobs, or networking.
- Only cube-based block models are supported.
- Animated textures use the first frame.
- No weather, volumetric lighting, or cloud shadows.
- Tall grass has no collision. Its shadows are limited to the near shadow cascade (22 blocks).
- Chunk generation and disk loading are multithreaded; mesh building and OpenGL uploads remain on the render thread and are budgeted per frame.
- The software ray tracing test samples the original block textures at 1280x720 with nearest filtering. It still uses voxel geometry and does not render individual grass blades.
- Grass LOD changes can pop when chunks cross the grass radius.
- Meshes are rebuilt from chunk data and are not stored on disk.

## Documentation

- [Installation](docs/INSTALL.md)
- [Development notes](docs/DEVELOPMENT.md)

## Legal

This project does not include or redistribute Minecraft code or assets. It requires a legally obtained local Minecraft installation and only reads files from that installation. Minecraft is a trademark of Mojang Studios.
