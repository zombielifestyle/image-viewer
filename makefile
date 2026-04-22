
.PHONY: all clean run

all: glfw

glfw:
	cc -DGL_GLEXT_PROTOTYPES -o image-viewer image-viewer-glfw.c -lstb -lGL -lglfw  -Wall

raylib:
	cc -o image-viewer image-viewer-raylib.c -I/usr/local/include -lraylib -lm -Wall

run: clean all
	#./image-viewer images/architecture.jpg
	./image-viewer images/lost-place.jpg

clean:
	rm -f ./image-viewer
