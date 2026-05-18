#!/bin/bash

#rm -rf libs/raylib
#git clone --depth 1 --branch 5.0 https://github.com/raysan5/raylib.git libs/raylib

## stb
mkdir -p libs/stb
curl -o libs/stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

# windows cross compile
#sudo apt install mingw-w64

## glfw
git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git libs/glfw
mkdir -p libs/glfw/build
cd libs/glfw/build
cmake .. -D GLFW_BUILD_WAYLAND=1 -D GLFW_BUILD_X11=1 -D BUILD_SHARED_LIBS=0 -D GLFW_BUILD_EXAMPLES=0 -D GLFW_BUILD_TESTS=0 -D GLFW_BUILD_DOCS=0
make -j$(nproc)
cd -
mkdir -p libs/glfw/build-build-x86_64-w64-mingw32
cd libs/glfw/build-build-x86_64-w64-mingw32
# windows cross compile
cmake .. -D CMAKE_TOOLCHAIN_FILE=./CMake/x86_64-w64-mingw32.cmake -D BUILD_SHARED_LIBS=0 -D GLFW_BUILD_EXAMPLES=0 -D GLFW_BUILD_TESTS=0 -D GLFW_BUILD_DOCS=0
make -j$(nproc)
cd -

## libjpeg-turbo
# https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/BUILDING.md
#sudo apt-get install nasm
git clone --depth 1 --branch 3.1.90 https://github.com/libjpeg-turbo/libjpeg-turbo.git libs/libjpeg-turbo
mkdir -p libs/libjpeg-turbo/build
cd libs/libjpeg-turbo/build
cmake ..
make -j$(nproc)
cd -
mkdir -p libs/libjpeg-turbo/build-x86_64-w64-mingw32
echo -e "set(CMAKE_ASM_NASM_COMPILER /usr/bin/nasm)\nset(CMAKE_SYSTEM_NAME Windows)\nset(CMAKE_SYSTEM_PROCESSOR AMD64)\nset(CMAKE_C_COMPILER /usr/bin/x86_64-w64-mingw32-gcc)\nset(CMAKE_RC_COMPILER /usr/bin/x86_64-w64-mingw32-windres)\n" > ./libs/libjpeg-turbo/toolchain.cmake
cd libs/libjpeg-turbo/build-x86_64-w64-mingw32
cmake .. -D CMAKE_TOOLCHAIN_FILE=../toolchain.cmake -DCMAKE_INSTALL_PREFIX=./install_dist
make -j$(nproc)
cd -

## libpng
#curl -L https://sourceforge.net/projects/libpng/files/libpng16/1.6.58/libpng-1.6.58.tar.xz/download |tar -C libs/ -vxJ
#cd libs/libpng-1.6.58/
#cp scripts/makefile.clang ./Makefile
#./configure --enable-hardware-optimizations=yes
#make -j$(nproc) CFLAGS=" -g -O3 "
#cd -
