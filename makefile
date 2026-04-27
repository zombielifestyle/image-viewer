
.PHONY: all clean run

RUNENV  = GALLIUM_DRIVER=d3d12
# RUNENV += MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA
# RUNENV += MESA_LOADER_DRIVER_OVERRIDE=nvidia
# RUNENV  =
LDFLAGS =
BIN = cc -Wall
# BIN += -O3
CFLAGS += -Ilibs/glad/include

# stb
CFLAGS  += -Ilibs/stb

# libjpeg-turbo
LDFLAGS += libs/libjpeg-turbo/build/libturbojpeg.a
CFLAGS  += -Ilibs/libjpeg-turbo/src

# libpng
CFLAGS  += -Ilibs/libpng-1.6.58/
LDFLAGS += libs/libpng-1.6.58/libpng.a

# vendored
# RUNENV  += MESA_LOADER_DRIVER_OVERRIDE=NVIDIA
CFLAGS  += -Ilibs/glfw/include
LDFLAGS += ./libs/glfw/build/src/libglfw3.a
LDFLAGS += -lm -lGL

# system install
# BIN += -DGL_GLEXT_PROTOTYPES
# LDFLAGS = -lm -lglfw

all: glfw

glfw:
	$(BIN) -o image-viewer $(CFLAGS) image-viewer-glfw.c ./libs/glad/src/glad.c $(LDFLAGS)

raylib:
	$(BIN) -o image-viewer image-viewer-raylib.c -I/usr/local/include -lraylib -lm 

run: clean all
	$(RUNENV) ./image-viewer images/lost-place.jpg
# 	$(RUNENV) ./image-viewer images/lost-place-8.png
# 	$(RUNENV) ./image-viewer images/architecture.jpg

clean:
	rm -f ./image-viewer
