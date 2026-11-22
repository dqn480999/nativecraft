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
src/render/Environment.*      sun direction, sky colors, ambient light, day factor
src/render/SkyRenderer.*      sky gradient, sun, moon, stars, moving clouds
src/render/ReflectionTarget.* offscreen color and depth target for water reflections
src/render/WorldRenderer.*    chunk meshes, shadow map, world shader, water shader
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

The grass block uses a composited side texture built from `grass_side` and a green-tinted `grass_side_overlay`. Tall grass uses procedurally generated blade quads instead of the tallgrass texture. Animated textures such as water and lava use the first frame of the vertical strip.

## Rendering passes

Each frame runs these passes:

1. **Shadow map.** Opaque chunks are rendered from the sun direction into a 4096x4096 depth texture. The light matrix uses an orthographic projection that covers the world. The map is cached and only re-rendered when the sun moves or a chunk changes. Sampling uses hardware PCF with a normal-offset bias.
2. **Reflection.** The scene is rendered from a camera mirrored across the water plane into an offscreen color/depth target. The sky and opaque geometry are included. Geometry below the water plane is clipped.
3. **Sky.** A fullscreen triangle computes sky gradient, sun disk, moon, stars, and moving clouds from the inverse view-projection matrix.
4. **Opaque world.** Block textures are lit by sun color, ambient sky/ground, and the shadow map. Glass and ice use a separate transparent pass.
5. **Grass.** Procedural tapering blade quads are displaced by wind and pushed away from the player. The grass shader samples the shadow map and applies alpha-free blade shading.
6. **Water.** Water vertices are displaced by large waves and small ripples. The fragment shader blends deep water color with the planar reflection, applies Fresnel, blurred reflections, sun path, specular highlights, and fog.
7. **HUD.** Crosshair, hotbar icons, bitmap text, and status lines.

All world passes use frustum culling against cached chunk bounds.

## Lighting model

- Sun direction and color are derived from `timeOfDay` in `render/Environment.cpp`.
- Sky zenith, horizon, fog, and ambient colors interpolate between day and night values.
- The world shader uses `ambient = mix(ambientGround, ambientSky, normal.y * 0.5 + 0.5)` and `light = ambient + sunColor * max(dot(normal, sunDir), 0) * shadow`.
- Shadows use a 3x3 PCF kernel with a slope-scaled bias.
- Time advances at `--time-speed` per second and can be paused with `T`.

## World generation

- Chunks are 16x128x16. The default world is 8x8 chunks (128x128x128).
- Height uses fBm value noise with two frequency bands.
- Sea level is 56. Columns below sea level are filled with water and topped with sand.
- A 14-block ocean margin forces the world boundary below sea level, so the visible edge is ocean instead of a cliff.
- Stone, dirt, grass, coal ore, iron ore, and gravel are placed by depth and hash.
- Trees are placed deterministically from a position hash on grass above sea level.
- Tall grass is placed on one third of the grass columns. Each tuft generates 9-13 tapered blades.
- The world is generated once at startup and kept in memory.

## Threading

The client is single-threaded. World generation, meshing, shadow rendering, reflection rendering, input, and the main render pass all run on the main thread.

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

Screenshot mode creates a real window, renders a fixed number of frames, writes a PNG, and exits.

## Current state

Verified on:

- NVIDIA GeForce GTX 970
- NVIDIA 580.196.07
- OpenGL 3.3.0
- 1280x720
- About 60 FPS
- 64 chunks
- 241392 triangles
- 79 blocks
- 101 textures
- 103 texture-array layers

## Next work

1. Anvil and NBT support to open existing 1.12.2 saves.
2. Full block model geometry: `elements`, `multipart`, rotations, `uvlock`, cross models, slabs, stairs, fences, panes.
3. Inventory, item stacks, block drops, and item pickup.
4. Skylight and blocklight propagation.
5. Entities and mobs.
6. Networking and the 1.12.2 protocol.
7. Biomes, structures, and chunk streaming.
8. Weather, cloud shadows, volumetric light shafts, and water depth effects.
9. Grass collision, targeting, LOD, and wind animation for tree leaves.
10. Positional audio and music.
11. Native launcher with version JSON, asset validation, and authentication.

## Conventions

- No Java source or JVM dependencies.
- No Minecraft assets in the repository.
- Keep source free of comments. Names and structure should be self-explanatory.
- Keep documentation factual and concise.
- Do not commit build artifacts, generated screenshots, or local Minecraft data.
