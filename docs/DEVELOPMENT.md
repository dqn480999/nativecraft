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
src/core/ThreadPool.hpp       worker pool for asynchronous chunk jobs
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
- `updateStreaming` submits generation and disk-load jobs for chunks inside `renderRadius + 1` and unloads chunks beyond `renderRadius + 2`.
- Chunk generation and disk loading run on a bounded `ThreadPool`. The main thread polls completed jobs with `pumpCompleted`.
- Chunk generation is deterministic from the world seed and chunk coordinates.
- Modified chunks are written to `saves/<seed>/chunks/<x>_<z>.nc` when unloaded or when the world is saved.
- The chunk file format is a small binary header (magic, version, chunk coordinates, volume) followed by the raw block array.
- Unmodified chunks are regenerated from the seed. Meshes are always rebuilt from chunk data and are not stored on disk.
- `WorldRenderer::syncMeshes` keeps a mesh map keyed by chunk coordinates. It removes unloaded chunks, waits for a 3x3 chunk neighborhood, then builds at most two meshes per frame in nearest-first order.
- Meshes are rebuilt on the render thread because OpenGL buffer uploads require the main context.

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
- Terrain height uses two fBm value noise bands, a continental band, and a ridged mountain term. Sea level is 56.
- Low columns are deepened into basins, then filled with water and topped with sand. Higher ground is grass or dirt.
- Peaks above y=82 are stone; peaks above y=94 get snow tops.
- Stone, dirt, grass, coal ore, iron ore, and gravel are placed by depth and position hash.
- Trees are placed deterministically from a position hash and are restricted to the chunk interior so they never cross chunk boundaries.
- Grass types are chosen from a low-frequency biome noise plus a per-column hash. The grass density is two thirds of eligible grass columns.
- Tall grass has 10-14 blades, meadow grass 14-20, dry grass 9-13, and fern 12-17. Each type has its own color and height range.

## Rendering passes

Each frame runs these passes:

1. **Shadow maps.** Two cascades are rendered from the sun direction. The far cascade is 4096x4096 and covers a 130-block radius around the player with opaque geometry only. The near cascade is 4096x4096, covers 22 blocks, and includes grass blades so grass casts shadows. Each cascade has its own normal offset and bias. Both are cached and re-rendered only when the sun, focus, or near-cascade wind animation changes. Sampling uses a 16-tap rotated Poisson-disk PCF kernel for soft edges; the 18-22 block transition takes the darker of the two cascades to avoid sunlight leaks.
2. **Reflection.** The scene is rendered from a camera mirrored across the water plane into an offscreen color/depth target. The sky and opaque geometry are included. Geometry below the water plane is clipped.
3. **Sky.** A fullscreen triangle computes sky gradient, sun, moon, stars, and moving clouds from the inverse view-projection matrix.
4. **Opaque world.** Block textures are lit by sun color, ambient sky/ground, and the shadow map.
5. **Grass.** Procedural tapering blade quads are displaced by wind and pushed away from the player. The grass shader samples the far shadow map for terrain shadows and applies blade shading, sheen, and moving glints.
6. **Transparent blocks.** Glass and ice use a sorted transparent pass.
7. **Water.** Water vertices are displaced by waves and ripples. The fragment shader blends deep water color with the planar reflection, applies Fresnel, blurred reflections, sun path, and specular highlights.
8. **HUD.** Crosshair, hotbar icons, bitmap text, and status lines.

All world passes use frustum culling against cached chunk bounds.

## Water simulation

- Breaking a block below sea level schedules a fluid update.
- A queued air cell becomes water if it touches an existing water block and is below sea level.
- Newly filled cells schedule their air neighbors, producing a breadth-first flood through connected caves and holes.
- Fluid updates are budgeted to 64 cells per frame and return the affected chunks for remeshing.
- The simulation does not model partial water levels or flowing textures; water is a full block.

## Slabs

- Slab blocks copy their base block textures and use a half-height box in the mesher.
- Slabs are non-opaque for face culling so adjacent full blocks still render their sides.
- Collision uses a half-height AABB.
- The player has a 0.6-block step-up assist for walking onto slabs.

## Swimming and underwater rendering

- The player detects liquid at the feet and chest. In water, gravity is reduced, vertical drag is applied, Space swims up, and Ctrl swims down.
- When the eye is inside a liquid, the render environment switches to a blue-green underwater fog and ambient palette.
- Underwater terrain receives animated caustic light and a depth-based blue-green tint.
- The water shader brightens the surface seen from below and adds a sun-through-water glow and surface caustics.
- Water is rendered double-sided when the camera is below the water level so the underside of the surface is visible.
- A translucent full-screen tint is drawn in the HUD while submerged.

## Player model

- `PlayerRenderer` builds six cuboid parts from the original `textures/entity/steve.png` skin: head, body, arms, and legs.
- Walk cycle, idle bob, jump pose, swimming motion, head pitch, and attack arm swing are driven by `PlayerRenderState`.
- The model is rendered in third-person view and in the water reflection when third person is active.
- The model is rendered into both shadow cascades with a depth-only shader, so the player casts a soft shadow.
- F5 or `--third-person` toggles the camera.

## Inventory

- 27 storage slots and 9 hotbar slots store block stacks with counts.
- E opens the inventory; movement is disabled while it is open.
- Left-click picks up, swaps, or places a stack; right-clicking a hotbar slot selects it.
- The UI uses the same block icons and bitmap font as the HUD.

## Skylight

- Each chunk mesh computes the topmost opaque block for a 3x3 neighborhood of columns.
- A vertex's skylight uses the nearest opening in that neighborhood and falls off smoothly with depth below it, with a dark floor for deep caves.
- Direct sun is faded out smoothly when skylight is low, so caves receive point/flashlight illumination instead of sunlight.
- Tall grass and other non-opaque vegetation do not block skylight; leaves do not block ambient but still cast sun shadows.
- This makes dug tunnels and caves dark until lit by glowstone, lava, or the flashlight.

## TNT and particles

- Right-clicking a TNT block or pressing G primes it with a 2.4 second fuse.
- The fuse emits smoke particles and plays `random/fuse.ogg`.
- Explosion destroys blocks in a 4.2 block radius, primes nearby TNT for chain reactions, and rebuilds affected chunks.
- Effects: smoke, fire, debris particles, a transient point light, a HUD flash, an explosion sound, player knockback and damage.
- Particles are GPU point sprites with radial alpha, color interpolation, gravity, drag, and fog.

## Survival and crafting

- Health, hunger, and oxygen are tracked and shown as HUD bars.
- Fall damage, drowning damage, lava damage, starvation, regeneration, death, and R-to-respawn are implemented.
- Breaking solid blocks adds them to the inventory; placing blocks consumes a stack item.
- The inventory includes a crafting panel with nine recipes. Inputs are consumed across hotbar and storage slots; outputs stack into free space.

## Dynamic lighting

- Glowstone and lava blocks carry a per-vertex emission attribute; the world shader adds their texture color as emission.
- The world tracks emissive block positions in the chunk map, including restored chunks from disk.
- Up to 16 nearest point lights are uploaded to the world and grass shaders as position, color, and range arrays.
- Point lights use smooth range attenuation and a small ambient-facing term so the surrounding blocks visibly glow.
- The flashlight is a camera-aligned spotlight with range attenuation and smooth inner/outer cone falloff. `L` or `--flashlight` toggles it.
- Point lights are analytic; they do not cast shadow maps.

## Ambient audio

- Background music loops `music/game/calm1.ogg` at low gain.
- A looping `liquid/water.ogg` source fades in while the player is in water.
- Bird chirps are synthesized into an OpenAL buffer with a short frequency sweep and played at randomized pitch.
- Bird chirps play every 9-15 seconds during daytime when the camera is not underwater.

## Atmosphere

- Distance fog is combined with a subtle low-altitude mist near sea level.
- Underwater fog, ambient color, caustics, and sun-through-water glints are described above.

## Lighting model

- Sun direction and color are derived from `timeOfDay` in `render/Environment.cpp`.
- Sky zenith, horizon, fog, and ambient colors interpolate between day and night values.
- The world shader uses `ambient = mix(ambientGround, ambientSky, normal.y * 0.5 + 0.5)` and `light = ambient + sunColor * max(dot(normal, sunDir), 0) * shadow`.
- Shadows use a 16-tap rotated Poisson-disk PCF kernel with a slope-scaled bias and normal offset.
- The near shadow cascade refreshes at 10 Hz so wind-driven grass shadows animate.
- Time advances at `--time-speed` per second and can be paused with `T`.
- Wind direction and strength are computed in `Game::update` and passed to the grass shader.

## Software ray tracing test

`--raytrace PATH` renders a 1280x720 image with a CPU voxel ray tracer:

- Primary rays use DDA traversal over the chunk map.
- Hits sample the original 16x16 block textures with nearest filtering and apply per-face tints.
- Secondary rays trace toward the sun to test direct shadowing.
- Rows are distributed across up to 16 worker threads.
- The test excludes water, vegetation blades, and transparent geometry; it is intended to validate terrain shadows and lighting, not to match the GPU renderer.

## Threading

- Chunk generation and chunk file loading run on a worker pool (`ThreadPool`).
- The default worker count uses the available hardware threads, capped at 8. `--threads N` overrides it up to 16.
- The main thread owns the chunk map, player, input, shadow/reflection passes, mesh uploads, and rendering.
- Mesh construction is budgeted to two chunks per frame in nearest-first order.
- OpenGL calls stay on the main thread.

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
- Chunk worker pool: auto up to 8 threads (configurable up to 16)
- Mesh build budget: 2 chunks per frame
- 169 meshed chunks and more than 225 loaded chunks
- About 400,000 triangles in the default view
- 87 blocks, 101 textures, 103 texture-array layers

## Next work

1. Worker-side mesh construction with immutable chunk snapshots.
2. Anvil and NBT support for importing existing 1.12.2 saves.
3. Full block model geometry: `elements`, `multipart`, rotations, `uvlock`, cross models, stairs, fences, panes.
4. Item entities, block drops, and item pickup.
5. Blocklight propagation and light attenuation through water.
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
