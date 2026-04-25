#!/bin/bash

#rm -rf libs/raylib
mkdir -p libs/stb

#git clone --depth 1 --branch 5.0 https://github.com/raysan5/raylib.git libs/raylib

# stb
curl -o libs/stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

# glfw
git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git libs/glfw
mkdir -p libs/glfw/build
cd libs/glfw/build
cmake .. -D GLFW_BUILD_WAYLAND=1 -D GLFW_BUILD_X11=1 -D BUILD_SHARED_LIBS=0 -D GLFW_BUILD_EXAMPLES=0 -D GLFW_BUILD_TESTS=0 -D GLFW_BUILD_DOCS=0
make -j$(nproc)
cd -


# https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/BUILDING.md
# note: requires to delete build dir to rerun cmake
#sudo apt-get install nasm
git clone --depth 1 --branch 3.1.90 https://github.com/libjpeg-turbo/libjpeg-turbo.git libs/libjpeg-turbo
mkdir -p libs/libjpeg-turbo/build
cd libs/libjpeg-turbo/build
make -j$(nproc)
cd -
