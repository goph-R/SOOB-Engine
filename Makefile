# Makefile for Linux - FPS Demo
# Requires libsdl1.2-dev, libopenal-dev.
CPP = g++
BIN = soob

# Shared engine (2D / audio / scripting) lives in ../SOOB-Core/.
# Bullet stays here — it's 3D-only and unique to the FPS demo.
ENGINE = ../SOOB-Core
BULLET_SRC = vendor/bullet3-3.25/src
BULLET_CXXFLAGS = -I$(BULLET_SRC)
BULLET_OBJS = bullet_linear_math.o bullet_collision.o bullet_dynamics.o

LUA_SRC = $(ENGINE)/vendor/lua-5.1.5/src
LUA_CFLAGS = -I$(LUA_SRC) -Dluaall_c -DLUA_USE_POSIX

CXXFLAGS = $(shell sdl-config --cflags) -I$(ENGINE) $(BULLET_CXXFLAGS) -I$(LUA_SRC) -O2
LIBS = $(shell sdl-config --libs) -lGL -lopenal

OBJ = main.o $(BULLET_OBJS) lua.o vorbis.o

all: $(BIN) scripts/engine

# Mirror the engine's Lua modules next to the exe so shipped builds find
# `require "engine.scene"` via ./scripts/?.lua without needing the
# SOOB-Core repo on the player's machine. Re-runs whenever the source
# files in $(ENGINE)/scripts/engine change.
scripts/engine: $(wildcard $(ENGINE)/scripts/engine/*.lua)
	mkdir -p scripts/engine
	cp $(ENGINE)/scripts/engine/*.lua scripts/engine/
	touch scripts/engine

$(BIN): $(OBJ)
	$(CPP) $(OBJ) -o $(BIN) $(LIBS)

main.o: main.cpp
	$(CPP) -c main.cpp -o main.o $(CXXFLAGS)

# stb_vorbis (Ogg Vorbis decoder, public domain). Built as its own C TU
# so editing main.cpp doesn't pay its recompile cost. music.h includes
# the same file with STB_VORBIS_HEADER_ONLY for prototypes only.
vorbis.o: $(ENGINE)/vendor/stb/stb_vorbis.c
	gcc -x c -c $< -o $@ -O2

bullet_linear_math.o: $(BULLET_SRC)/btLinearMathAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

bullet_collision.o: $(BULLET_SRC)/btBulletCollisionAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

bullet_dynamics.o: $(BULLET_SRC)/btBulletDynamicsAll.cpp
	$(CPP) -c $< -o $@ $(BULLET_CXXFLAGS) -O2

# Lua 5.1.5 compiled as a single C TU via the unity-build aggregator.
lua.o: $(LUA_SRC)/lua_all.c
	gcc -x c -c $< -o $@ $(LUA_CFLAGS) -O2

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
