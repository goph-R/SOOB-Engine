cmake_minimum_required(VERSION 2.8)
project(SOOB-Engine)

# Shared engine (2D / audio / scripting) lives at ../SOOB-Core/.
# Bullet stays here -- 3D-only, FPS-demo-specific.
set(ENGINE ${CMAKE_SOURCE_DIR}/../SOOB-Core)

option(USE_VENDOR_SDL "Use vendored SDL from the shared engine" OFF)

if(USE_VENDOR_SDL)
    set(SDL_INCLUDE_DIR ${ENGINE}/vendor/include)
    set(SDL_LIBRARY ${ENGINE}/vendor/lib/libSDL.dll.a)
    set(SDLMAIN_LIBRARY ${ENGINE}/vendor/lib/libSDLmain.a)
    set(SDL_LIBS mingw32 ${SDLMAIN_LIBRARY} ${SDL_LIBRARY})
else()
    find_package(SDL REQUIRED)
    set(SDL_LIBS ${SDL_LIBRARY})
endif()

find_package(OpenGL REQUIRED)
find_package(OpenAL REQUIRED)

include_directories(${ENGINE} ${SDL_INCLUDE_DIR} ${OPENAL_INCLUDE_DIR}
                    ${ENGINE}/vendor/lua-5.1.5/src
                    vendor/bullet3-3.25/src)

add_executable(SOOB main.cpp ${ENGINE}/vendor/stb/stb_vorbis.c)
target_link_libraries(SOOB ${SDL_LIBS} ${OPENGL_LIBRARIES} ${OPENAL_LIBRARY})

# Mirror the engine's Lua modules next to the exe so shipped builds find
# require "engine.scene" via ./scripts/?.lua without needing SOOB-Core
# on the player's machine.
add_custom_command(TARGET SOOB POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_SOURCE_DIR}/scripts/engine
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${ENGINE}/scripts/engine
            ${CMAKE_SOURCE_DIR}/scripts/engine)
