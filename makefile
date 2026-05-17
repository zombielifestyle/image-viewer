
.PHONY: all clean run build

RUNENV  = GALLIUM_DRIVER=d3d12
CC = clang -g -Wall -Wno-unused-function -Wno-unused-variable
# CC += -O3 -march=haswell

ARGS    = images/lost-place.jpg images/architecture.jpg
LDFLAGS =
CFLAGS  =
BACKEND ?= GLFW
TARGET  ?= LINUX

ifeq ($(TARGET), WIN)
	CC += -static -static-libgcc
	CC += -target x86_64-pc-windows-gnu
endif

# stb
# CFLAGS  += -Ilibs/stb

# libjpeg-turbo
ifeq ($(TARGET), WIN)
	LDFLAGS += libs/libjpeg-turbo/build-x86_64-w64-mingw32/libturbojpeg.a
else
	LDFLAGS += libs/libjpeg-turbo/build/libturbojpeg.a
endif
CFLAGS  += -Ilibs/libjpeg-turbo/src
CFLAGS  += -Ilibs/libjpeg-turbo/build

# libpng
CFLAGS  += -Ilibs/libpng-1.6.58/
LDFLAGS += libs/libpng-1.6.58/libpng.a

ifeq ($(BACKEND), WAYLAND)
# 	RUNENV += WAYLAND_DEBUG=client
	LDFLAGS += -lwayland-client -lm -lc
	CFLAGS  += xdg-shell-client-protocol.c viewporter-protocol.c image-viewer-wayland.c
else ifeq ($(BACKEND), X11)
	CFLAGS  += -Ilibs/glad/include -Ilibs/glfw/include image-viewer-x11.c
	LDFLAGS += ./libs/glfw/build/src/libglfw3.a -lX11 -lc -lm -lGL -lEGL -lGLEW
else ifeq ($(BACKEND), GLFW)
	CFLAGS  += -Ilibs/glad/include -Ilibs/glfw/include 
	ifeq ($(TARGET), WIN)
		CFLAGS += -L./libs/glfw/build-x86_64-w64-mingw32/src -L./libs/libjpeg-turbo/build-x86_64-w64-mingw32
	endif
	CFLAGS  += image-viewer-glfw.c ./libs/glad/src/glad.c
	ifeq ($(TARGET), LINUX)
		LDFLAGS += ./libs/glfw/build/src/libglfw3.a -lX11 -lc -lm -lGL
	else ifeq ($(TARGET), WIN)
		LDFLAGS += libs/glfw/build-x86_64-w64-mingw32/src/libglfw3.a -lopengl32 -lgdi32 -luser32 -lshell32
	endif
else ifeq ($(BACKEND), RAYLIB)
	CFLAGS  += image-viewer-raylib.c
	LDFLAGS += -I/usr/local/include -lraylib -lm
endif

build: clean
	$(CC) -o image-viewer $(CFLAGS) $(LDFLAGS)

run: build
	$(RUNENV) ./image-viewer $(ARGS)

clean:
	rm -f ./image-viewer ./image-viewer.exe
