# Development notes

## Design

NativeCraft is a standalone C++20 client. It does not load the Minecraft JVM, Forge, or any Java code. The only input from Minecraft is the local 1.12.2 asset set:

- `versions/1.12.2/1.12.2.jar` for textures, blockstates, models, language files, and the bitmap font.
- `assets/indexes/1.12.json` and `assets/objects/` for sounds.

Assets are never copied into the repository.

## Stack

- SDL2 for window, input, and OpenGL context creation.
- OpenGL 3.3 core.
- libpng and zlib for PNG and ZIP data.
- OpenAL and libvorbisfile for sound.
- Custom JSON parser, ZIP reader, math library, and GL function loader.

## Source layout

```text
src/main.cpp                  CLI parsing and startup
src/game/Game.*               event loop, input, HUD, screenshots, sound triggers
src/game/Player.*             camera, movement, collision, raycast
src/world/World.*             chunk storage and world generation
src/world/Blocks.*            block registry, model resolution, texture layers, display names
src/render/WorldRenderer.*    chunk meshes, shaders, transparent pass
src/render/Hud.*              crosshair, hotbar, icons, bitmap text
src/render/GL.*               OpenGL function loading
src/render/Shader.*           shader compilation and uniforms
src/render/TextureArray.*     2D array texture for block textures
src/util/Zip.*                ZIP central-directory reader with zlib inflate
src/util/Json.*               recursive-descent JSON parser
src/util/Image.*              PNG load/write, crop, resize, overlay composition
src/assets/Assets.*           resource lookup and caching
src/audio/Audio.*             OpenAL source pool and Vorbis decoding
src/core/Math.hpp             vectors, matrices, AABB
src/core/Log.hpp              logging
```

## Asset pipeline

Block loading:

1. Read `assets/minecraft/blockstates/<block>.json`.
2. Select a variant. Preference order is `normal`, `axis=y`, `inventory`, `facing=north`, then the first variant.
3. Read the model and its `parent` chain from `assets/minecraft/models/`.
4. Merge parent textures, then child textures.
5. Resolve `#texture` references.
6. Select a texture per face using face keys (`east`, `west`, `up`, `down`, `south`, `north`) with fallbacks (`side`, `top`, `bottom`, `end`, `all`, `particle`).
7. Load the PNG from the jar and append it to the block texture array.
8. Resolve the display name from `lang/en_us.lang`.

Grass uses a composited side texture built from `grass_side` and a green-tinted `grass_side_overlay`. Animated textures such as water and lava use the first frame of the vertical strip.

## Rendering

- One `GL_TEXTURE_2D_ARRAY` holds 16x16 block textures. Filtering is nearest, mipmaps are disabled.
- Each chunk owns one VAO, VBO, and EBO. Opaque and transparent indices share the buffer; opaque indices are drawn first, then transparent indices.
- Faces are culled when the neighbor is opaque. Faces between two identical non-opaque blocks are skipped.
- Per-face shading is constant: top 1.0, bottom 0.5, north/south 0.86, east/west 0.72.
- Transparent chunks are sorted back to front. Blending uses `GL_SRC_ALPHA` and `GL_ONE_MINUS_SRC_ALPHA` with depth writes disabled.
- Fog is linear between 80 and 230 blocks.

## World generation

- Chunks are 16x128x16. The default world is 6x6 chunks.
- Height uses fBm value noise with two frequency bands.
- Sea level is 56. Columns below sea level are filled with water and topped with sand.
- Stone, dirt, grass, coal ore, iron ore, and gravel are placed by depth and hash.
- Trees are placed deterministically from a position hash on grass above sea level.
- The world is generated once at startup and kept in memory.

## Threading

The client is single-threaded. World generation, meshing, input, and rendering all run on the main thread.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Render smoke test:

```bash
./build/nativecraft --mc-dir "$HOME/.minecraft" \
  --screenshot shots/check.png --frames 90 --width 1280 --height 720
```

The screenshot mode creates a real window, renders a fixed number of frames, writes a PNG, and exits.

## Current state

Verified on:

- NVIDIA GeForce GTX 970
- NVIDIA 580.196.07
- OpenGL 3.3.0
- 1280x720
- About 60 FPS
- 36 chunks
- 83454 triangles
- 78 blocks
- 101 textures
- 103 texture-array layers

## Next work

1. Anvil and NBT support to open existing 1.12.2 saves.
2. Full block model geometry: `elements`, `multipart`, rotations, `uvlock`, cross models, slabs, stairs, fences, panes.
3. Inventory, item stacks, block drops, and item pickup.
4. Skylight and blocklight propagation plus ambient occlusion.
5. Entities and mobs.
6. Networking and the 1.12.2 protocol.
7. Biomes, structures, chunk streaming, and frustum culling.
8. Positional audio and music.
9. Native launcher with version JSON, asset validation, and authentication.

## Conventions

- No Java source or JVM dependencies.
- No Minecraft assets in the repository.
- Keep source free of comments. Names and structure should be self-explanatory.
- Keep documentation factual and concise.
- Do not commit build artifacts, generated screenshots, or local Minecraft data.
