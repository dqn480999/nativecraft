cmake_minimum_required(VERSION 3.20)
project(nativecraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(OPENAL REQUIRED openal)
pkg_check_modules(VORBIS REQUIRED vorbisfile vorbis ogg)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(nativecraft
    src/main.cpp
    src/util/Json.cpp
    src/util/Zip.cpp
    src/util/Image.cpp
    src/assets/Assets.cpp
    src/render/GL.cpp
    src/render/Shader.cpp
    src/render/TextureArray.cpp
    src/render/WorldRenderer.cpp
    src/render/Hud.cpp
    src/world/Blocks.cpp
    src/world/World.cpp
    src/game/Player.cpp
    src/game/Game.cpp
    src/audio/Audio.cpp)

target_include_directories(nativecraft PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
    ${OPENAL_INCLUDE_DIRS}
    ${VORBIS_INCLUDE_DIRS})

target_link_libraries(nativecraft PRIVATE
    ${SDL2_LINK_LIBRARIES}
    ${OPENAL_LINK_LIBRARIES}
    ${VORBIS_LINK_LIBRARIES}
    PNG::PNG
    ZLIB::ZLIB
    OpenGL::GL
    ${CMAKE_DL_LIBS})

target_compile_options(nativecraft PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS nativecraft RUNTIME DESTINATION bin)
