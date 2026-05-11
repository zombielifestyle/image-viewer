
.PHONY: all clean run

RUNENV  = GALLIUM_DRIVER=d3d12
CC = cc -g -Wall -Wno-unused-function -Wno-unused-variable
# CC += -O3

LDFLAGS =
CFLAGS  =
BACKEND ?= GLFW

# stb
# CFLAGS  += -Ilibs/stb

# libjpeg-turbo
LDFLAGS += libs/libjpeg-turbo/build/libturbojpeg.a
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
	CFLAGS  += -Ilibs/glad/include -Ilibs/glfw/include image-viewer-glfw.c ./libs/glad/src/glad.c
	LDFLAGS += ./libs/glfw/build/src/libglfw3.a -lX11 -lc -lm -lGL
else ifeq ($(BACKEND), RAYLIB)
	CFLAGS  += image-viewer-raylib.c
	LDFLAGS += -I/usr/local/include -lraylib -lm
endif

all:
	$(CC) -o image-viewer $(CFLAGS) $(LDFLAGS)

run: clean all
# 	$(RUNENV) ./image-viewer images/lost-place.jpg
# 	$(RUNENV) ./image-viewer images/lost-place-8.png
	$(RUNENV) ./image-viewer images/architecture.jpg

clean:
	rm -f ./image-viewer
