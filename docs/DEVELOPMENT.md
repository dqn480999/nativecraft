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
src/game/Game.*               event loop, streaming updates, HUD, screenshots, sound
src/game/Player.*             camera, movement, collision, raycast, spawn search
src/world/World.*             chunk map, per-chunk generation, streaming, save files
src/world/Blocks.*            block registry, model resolution, texture layers, grass types
src/render/Environment.*      sun direction, sky colors, ambient light, day factor
src/render/SkyRenderer.*      sky gradient, sun, moon, stars, moving clouds
src/render/ReflectionTarget.* offscreen color and depth target for water reflections
src/render/WorldRenderer.*    chunk meshes, shadow map, world shader, grass shader, water shader
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

## Chunk streaming and persistence

- `World` stores chunks in an `unordered_map` keyed by signed chunk coordinates.
- `updateStreaming` generates chunks inside `renderRadius + 1` and unloads chunks beyond `renderRadius + 2`.
- Chunk generation is deterministic from the world seed and chunk coordinates.
- Modified chunks are written to `saves/<seed>/chunks/<x>_<z>.nc` when unloaded or when the world is saved.
- The chunk file format is a small binary header (magic, version, chunk coordinates, volume) followed by the raw block array.
- Unmodified chunks are regenerated from the seed. Meshes are always rebuilt from chunk data and are not stored on disk.
- `WorldRenderer::syncMeshes` keeps a mesh map keyed by chunk coordinates. It builds new meshes, removes unloaded chunks, and rebuilds meshes when the grass LOD status changes.

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

The grass block uses a composited side texture built from `grass_side` and a green-tinted `grass_side_overlay`. Tall grass, meadow grass, dry grass, and fern use procedural blade geometry instead of a texture. Animated textures such as water and lava use the first frame of the vertical strip.

## World generation

- Chunks are 16x128x16.
- Terrain height uses two fBm value noise bands. Sea level is 56.
- Columns below sea level are filled with water and topped with sand. Higher ground is grass or dirt.
- Stone, dirt, grass, coal ore, iron ore, and gravel are placed by depth and position hash.
- Trees are placed deterministically from a position hash and are restricted to the chunk interior so they never cross chunk boundaries.
- Grass types are chosen from a low-frequency biome noise plus a per-column hash. The grass density is two thirds of eligible grass columns.
- Tall grass has 10-14 blades, meadow grass 14-20, dry grass 9-13, and fern 12-17. Each type has its own color and height range.

## Rendering passes

Each frame runs these passes:

1. **Shadow map.** Opaque chunks are rendered from the sun direction into a 4096x4096 depth texture. The light matrix follows the player with a 90-block radius. The map is cached and re-rendered only when the sun or player focus moves enough. Sampling uses hardware PCF with a normal-offset bias.
2. **Reflection.** The scene is rendered from a camera mirrored across the water plane into an offscreen color/depth target. The sky and opaque geometry are included. Geometry below the water plane is clipped.
3. **Sky.** A fullscreen triangle computes sky gradient, sun, moon, stars, and moving clouds from the inverse view-projection matrix.
4. **Opaque world.** Block textures are lit by sun color, ambient sky/ground, and the shadow map.
5. **Grass.** Procedural tapering blade quads are displaced by wind and pushed away from the player. The grass shader samples the shadow map and applies blade shading.
6. **Transparent blocks.** Glass and ice use a sorted transparent pass.
7. **Water.** Water vertices are displaced by waves and ripples. The fragment shader blends deep water color with the planar reflection, applies Fresnel, blurred reflections, sun path, and specular highlights.
8. **HUD.** Crosshair, hotbar icons, bitmap text, and status lines.

All world passes use frustum culling against cached chunk bounds.

## Lighting model

- Sun direction and color are derived from `timeOfDay` in `render/Environment.cpp`.
- Sky zenith, horizon, fog, and ambient colors interpolate between day and night values.
- The world shader uses `ambient = mix(ambientGround, ambientSky, normal.y * 0.5 + 0.5)` and `light = ambient + sunColor * max(dot(normal, sunDir), 0) * shadow`.
- Shadows use a 3x3 PCF kernel with a slope-scaled bias and normal offset.
- Time advances at `--time-speed` per second and can be paused with `T`.
- Wind direction and strength are computed in `Game::update` and passed to the grass shader.

## Threading

The client is single-threaded. Chunk generation, meshing, shadow rendering, reflection rendering, input, and the main render pass all run on the main thread. Fast movement can cause short generation hitches.

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
- Default render radius 6 chunks
- 169 meshed chunks and more than 225 loaded chunks
- About 400,000 triangles in the default view
- 82 blocks, 101 textures, 103 texture-array layers

## Next work

1. Asynchronous chunk generation and meshing to remove movement hitches.
2. Anvil and NBT support for importing existing 1.12.2 saves.
3. Full block model geometry: `elements`, `multipart`, rotations, `uvlock`, cross models, slabs, stairs, fences, panes.
4. Inventory, item stacks, block drops, and item pickup.
5. Skylight and blocklight propagation.
6. Entities and mobs.
7. Networking and the 1.12.2 protocol.
8. Biomes, structures, cave generation, and larger vertical range.
9. Weather, cloud shadows, volumetric light shafts, and water depth effects.
10. Positional audio and music.
11. Native launcher with version JSON, asset validation, and authentication.

## Conventions

- No Java source or JVM dependencies.
- No Minecraft assets in the repository.
- Keep source free of comments. Names and structure should be self-explanatory.
- Keep documentation factual and concise.
- Do not commit build artifacts, generated screenshots, save files, or local Minecraft data.
