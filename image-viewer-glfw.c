#include "glad/glad.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <turbojpeg.h>
#if defined(__linux__)
#include <png.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/mman.h>
#endif
#include <sys/stat.h>

typedef struct {
    unsigned int id;
    int uProjection;
    int uTime;
    int uFlipY;
    int uSize;
    int uWSize;
    int uPosition;
} Shader;
Shader shaders[2];
unsigned int shaderIndex = 0;

typedef struct {
    const char* fileName;
    FILE* fileHandle;
    long fileSize;
    int width;
    int height;
    unsigned int textureId;
    int fd;
} Image;

struct State {
    float projection[16];

    double mouseY,  mouseX;
    float  cameraX, cameraY;
    float  offsetX, offsetY;

    int width,        height;
    int windowWidth,  windowHeight;
    int displayWidth, displayHeight;

    int isPanning;
    int isDirty;
    int isZoom;
    int isMaximized;
    int isMovingWindow;
    int isFitted;
    int testCycling;
    float zoom, zoomFactor;

    unsigned int VAO, EBO, VBO;
    GLFWwindow* window;

    double lastClickTime;

    double dragX, dragY;
    int targetFrameRate;
};

struct State state = {
    .windowWidth     = 420, .width  = 420,
    .windowHeight    = 500, .height = 500,
    .isDirty         = 1,
    .isFitted        = 1,
    .testCycling     = 0,
    .zoom            = 1.0f,
    .zoomFactor      = 1.2f,
    .targetFrameRate = 60
};

int imageIndex = 0;
Image imageArray[2];
Image* image;

GLuint pbo;
unsigned int vertShader = 0;
const char* vsSource = "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "\n"
    "out vec2 TexCoord;\n"
    "uniform mat4 uProjection;\n"
    "uniform bool uFlipY;\n"
    "uniform vec2 uSize;\n"
    "uniform vec2 uPosition;\n"
    "\n"
    "void main() {\n"
    "    vec2 scaledPos = aPos * uSize;\n"
    "    gl_Position = uProjection * vec4(scaledPos + uPosition, 0.0, 1.0);\n"
    "    TexCoord.x = aTexCoord.x;\n"
    "    TexCoord.y = uFlipY ? 1.0 - aTexCoord.y : aTexCoord.y;\n"
    "}\n";

#define len(x)  (sizeof(x) / sizeof((x)[0]))

clock_t profiler_time;
clock_t profiler_time_sum;
#define profiler(s) { \
    printf("> profiler: %8.2fms - %s\n", ((float)(clock() - profiler_time) / CLOCKS_PER_SEC)*1000, s); \
    profiler_time = clock(); \
}

GLenum opengl_print_error(int line) {
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR) {
        const char* error = "";
        switch (errorCode) {
            case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
        }
        fprintf(stderr, "GL_ERROR:%d: %s\n", line, error);
    }
    return errorCode;
}
#define GLERR opengl_print_error(__LINE__)

static void iv_window_size_callback(GLFWwindow* window, int width, int height);

static void iv_sleep(double time) {
#if defined(__linux__)
    time_t sec = time;
    long nsec  = (time - sec) * 1000000000L;
    struct timespec req = { .tv_sec = sec, .tv_nsec = nsec };
    while (nanosleep(&req, &req) == -1) continue;
#endif
}

static void iv_win_maximize_toggle(GLFWwindow* window) {
    if (state.isMaximized) {
        glfwSetWindowMonitor(window, NULL, 0, 0, state.windowWidth, state.windowHeight, 0);
        // WSL + WAYLAND doesn't trigger size callback
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            iv_window_size_callback(window, state.windowWidth, state.windowHeight);
        }
        state.isMaximized = 0;
    } else {
        const GLFWvidmode* vidmode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        glfwSetWindowMonitor(window, glfwGetPrimaryMonitor(),
            0, 0, vidmode->width, vidmode->height, 0
        );
        state.isMaximized = 1;
    }
}

static void iv_win_move(GLFWwindow* window, double x, double y) {
    int winX, winY;
    glfwGetWindowPos(window, &winX, &winY);

    int newX = winX + (int)(x - state.dragX);
    int newY = winY + (int)(y - state.dragY);

    if (newY != winY || newX != winX) {
        glfwSetWindowPos(window, newX, newY);
    }
}

static void iv_camera_update_pos(float posX, float posY, float posXOld, float posYOld) {
    if (state.isFitted) {
        float winWidth  = (float)state.width;
        float winHeight = (float)state.height;

        float deltaX = posX - posXOld;
        float deltaY = posY - posYOld;

        if (deltaX == 0.0f && deltaY == 0.0f) {
            return;
        }

        float windowAspect = winWidth / winHeight;
        float worldHeight  = (float)image->height / state.zoom;
        float worldWidth   = worldHeight * windowAspect;

        float worldX = worldWidth  / winWidth;
        float worldY = worldHeight / winHeight;

        state.cameraX -= deltaX * worldX;
        state.cameraY += deltaY * worldY;

    } else {

        state.cameraX -= (float)(posX - posXOld) / state.zoom;
        state.cameraY += (float)(posY - posYOld) / state.zoom;

        if (state.cameraX < 0.0f) state.cameraX = 0.0f;
        if (state.cameraX > (float)image->width) state.cameraX = (float)image->width;
        if (state.cameraY < 0.0f) state.cameraY = 0.0f;
        if (state.cameraY > (float)image->height) state.cameraY = (float)image->height;

    }
    state.isDirty = 1;
}

static void iv_camera_update_zoom(float posX, float posY, float delta) {
    if (state.isFitted) {
        float winWidth  = (float)state.width;
        float winHeight = (float)state.height;

        float aspectRatio = winWidth / winHeight;
        float worldHeight = (float)image->height / state.zoom;
        float worldWidth  = worldHeight * aspectRatio;

        float left   = state.cameraX - (worldWidth  / 2.0f);
        float bottom = state.cameraY - (worldHeight / 2.0f);

        float worldX = left   + (posX / winWidth) * worldWidth;
        float worldY = bottom + (1.0f - (posY / winHeight)) * worldHeight;

        if (delta > 0) {
            state.zoom *= state.zoomFactor;
        } else {
            state.zoom /= state.zoomFactor;
        }

        if (state.zoom < 0.5f)   state.zoom = 0.5f;
        if (state.zoom > 100.0f) state.zoom = 100.0f;

        float newWorldHeight = (float)image->height / state.zoom;
        float newWorldWidth  = newWorldHeight * aspectRatio;

        state.cameraX = worldX - ((posX / winWidth) - 0.5f) * newWorldWidth;
        state.cameraY = worldY - ((1.0f - (posY / winHeight)) - 0.5f) * newWorldHeight;
    } else {
        float winWidth  = (float)state.width;
        float winHeight = (float)state.height;

        float worldWidth  = winWidth  / state.zoom;
        float worldHeight = winHeight / state.zoom;

        float pctOffsetX = (posX / winWidth) - 0.5f;
        float pctOffsetY = 0.5f - (posY / winHeight);

        float mouseWorldX = state.cameraX + (pctOffsetX * worldWidth);
        float mouseWorldY = state.cameraY + (pctOffsetY * worldHeight);

        if (delta > 0) {
            state.zoom *= state.zoomFactor;
        } else {
            state.zoom /= state.zoomFactor;
        }

        if (state.zoom < 0.1f)   state.zoom = 0.1f;
        if (state.zoom > 100.0f) state.zoom = 100.0f;

        float newWorldWidth  = winWidth / state.zoom;
        float newWorldHeight = winHeight / state.zoom;

        state.cameraX = mouseWorldX - (pctOffsetX * newWorldWidth);
        state.cameraY = mouseWorldY - (pctOffsetY * newWorldHeight);
    }

    state.isDirty = 1;
}

static void iv_camera_update_projection_matrix(float* m, float l, float r, float b, float t) {
    for(int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] = 1.0f;
}

static void iv_camera_update_projection(const Image* img) {
    if (!state.isFitted) {
        float winWidth  = (float)state.width  / 2.0f / state.zoom;
        float winHeight = (float)state.height / 2.0f / state.zoom;

        float l = state.cameraX - winWidth;
        float r = state.cameraX + winWidth;
        float b = state.cameraY - winHeight;
        float t = state.cameraY + winHeight;

        iv_camera_update_projection_matrix(state.projection, l, r, b, t);
    } else {
        float aspectRatio = (float)state.width / (float)state.height;

        float worldHeight = (float)img->height / state.zoom;
        float worldWidth  = worldHeight * aspectRatio;

        float l = state.cameraX - (worldWidth  / 2.0f);
        float r = state.cameraX + (worldWidth  / 2.0f);
        float b = state.cameraY - (worldHeight / 2.0f);
        float t = state.cameraY + (worldHeight / 2.0f);

        iv_camera_update_projection_matrix(state.projection, l, r, b, t);
    }
}

static void iv_camera_center(Image* img) {
    state.cameraX = img->width  / 2.0f;
    state.cameraY = img->height / 2.0f;
    state.isDirty = 1;
}

static void iv_camera_fit(Image* img) {
    float windowAspect = (float)state.width / (float)state.height;
    float imageAspect  = (float)img->width  / (float)img->height;

    if (windowAspect >= imageAspect) {
        state.zoom = 1.0f;
    } else {
        state.zoom = windowAspect / imageAspect;
    }

    iv_camera_center(img);
    state.isFitted = 1;
}

static void iv_camera_unfit(Image* img) {
    iv_camera_center(img);
    state.zoom = 1.0f;
    state.isFitted = 0;
}

static void iv_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error: %s\n", description);
}

static void iv_cursor_position_callback(GLFWwindow* window, double x, double y) {
    if (state.isMovingWindow) {
        iv_win_move(window, x, y);
    } else if (state.isPanning) {
        iv_camera_update_pos((float)x, (float)y, (float)state.mouseX, (float)state.mouseY);
    }
    state.mouseX = x;
    state.mouseY = y;
}

static void iv_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if ((key == GLFW_KEY_F || key == GLFW_KEY_F11) && action == GLFW_RELEASE) {
        iv_win_maximize_toggle(window);
    } else if (key == GLFW_KEY_C && action == GLFW_RELEASE) {
        iv_camera_center(image);
    } else if (key == GLFW_KEY_R && action == GLFW_RELEASE) {
        if (state.isFitted) {
            iv_camera_unfit(image);
        } else {
            iv_camera_fit(image);
        }

    } else if (key == GLFW_KEY_S && action == GLFW_RELEASE) {
        shaderIndex = shaderIndex+1 >= len(shaders) ? 0 : shaderIndex + 1;
        glUseProgram(shaders[shaderIndex].id); GLERR;
        glUniform2f(shaders[shaderIndex].uSize, (float)image->width, (float)image->height); GLERR;
        if (shaders[shaderIndex].uWSize != -1) {
            glUniform2f(shaders[shaderIndex].uWSize, (float)state.width, (float)state.height); GLERR;
        }
        state.isDirty = 1;
    } else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        glfwGetCursorPos(window, &state.mouseX, &state.mouseY);
        glfwSetCursorPosCallback(window, iv_cursor_position_callback);
        state.isPanning = 1;
    } else if (key == GLFW_KEY_SPACE && action == GLFW_RELEASE) {
        glfwSetCursorPosCallback(window, NULL);
        state.isPanning = 0;
    } else if (state.testCycling && (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) && action == GLFW_RELEASE) {
        imageIndex = imageIndex+1 >= len(imageArray) ? 0 : imageIndex + 1;
        image = &imageArray[imageIndex];
        glBindTexture(GL_TEXTURE_2D, image->textureId); GLERR;
        glUseProgram(shaders[shaderIndex].id); GLERR;
        glUniform2f(shaders[shaderIndex].uSize, (float)image->width, (float)image->height); GLERR;
        state.isDirty = 1;
    }
}

static void iv_mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        state.isMovingWindow = 1;
        glfwGetCursorPos(window, &state.dragX, &state.dragY);
        glfwSetCursorPosCallback(state.window, iv_cursor_position_callback);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
        glfwSetCursorPosCallback(window, NULL);
        state.isMovingWindow = 0;
    } else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        // double click
        double currentTime = glfwGetTime();
        if (currentTime - state.lastClickTime < 0.3) {
            iv_win_maximize_toggle(window);
        }
        state.lastClickTime = glfwGetTime();
    }
}

static void iv_window_scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    glfwGetCursorPos(window, &state.mouseX, &state.mouseY);
    iv_camera_update_zoom((float)state.mouseX, (float)state.mouseY, yoffset);
}

static void iv_window_size_callback(GLFWwindow* window, int width, int height) {
    state.isDirty = 1;
    state.width   = width;
    state.height  = height;
    printf("resize: %d, %d\n", width, height);
    int w, h;
    glfwGetFramebufferSize(window, &w, &h); GLERR;
    glViewport(0, 0, w, h); GLERR;
    if (state.isFitted) {
        iv_camera_fit(image);
    } else {
        iv_camera_unfit(image);
    }
}

static int init_glfw() {
    glfwSetErrorCallback(iv_error_callback);
    if (glfwPlatformSupported(GLFW_PLATFORM_X11)) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
    // glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    // glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_PREFER_LIBDECOR);
    // glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);

    if (!glfwInit())
        return -1;
    profiler("glfwInit");
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_NATIVE_CONTEXT_API);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);

    if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    const GLFWvidmode* vidmode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if (vidmode->refreshRate < state.targetFrameRate) {
        state.targetFrameRate = vidmode->refreshRate;
    }
    state.displayWidth  = vidmode->width;
    state.displayHeight = vidmode->height;

    if (0) {
        state.width  = state.windowWidth  = (int)(state.displayWidth  * 0.75f);
        state.height = state.windowHeight = (int)(state.displayHeight * 0.75f);
    } else {
        state.width  = state.windowWidth  = (int)(state.displayWidth  * 0.40f);
        state.height = state.windowHeight = (int)(state.displayHeight * 0.8f);
    }

    state.window = glfwCreateWindow(state.width, state.height, "Image Viewer", NULL, NULL);
    if (!state.window) {
        glfwTerminate();
        return 0;
    }

    profiler("glfwCreateWindow");
    glfwMakeContextCurrent(state.window);
    profiler("glfwMakeContextCurrent");
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        printf("Failed to initialize GLAD\n");
        return 0;
    }
    profiler("glad");
    printf("GL_VERSION: %s\n",        glGetString(GL_VERSION));
    printf("GL_RENDERER: %s\n",       glGetString(GL_RENDERER));
    printf("TURBOJPEG_VERSION: %d\n", TURBOJPEG_VERSION_NUMBER);

    glfwSetWindowPos(state.window,
        state.displayWidth  / 2.0f - state.width  / 2.0f,
        state.displayHeight / 2.0f - state.height / 2.0f
    );

    glfwSetMouseButtonCallback(state.window, iv_mouse_button_callback);
    glfwSetKeyCallback(state.window, iv_key_callback);
    glfwSetWindowSizeCallback(state.window, iv_window_size_callback);
    glfwSetScrollCallback(state.window, iv_window_scroll_callback);

    glfwSwapInterval(0);

    return 1;
}

static int shader_create(Shader* s, const char* fsSource) {
    int success;
    char infoLog[512];
    unsigned int program, fragShader;

    program = glCreateProgram();

    fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &fsSource, NULL);
    glCompileShader(fragShader);
    glGetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragShader, 512, NULL, infoLog);
        printf("ERROR: Shader Compilation Failed\n%s\n", infoLog);
        return 0;
    }
    glAttachShader(program, fragShader);

    if (!vertShader) {
        vertShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertShader, 1, &vsSource, NULL);
        glCompileShader(vertShader);
        glGetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertShader, 512, NULL, infoLog);
            printf("ERROR: Shader Compilation Failed\n%s\n", infoLog);
            return 0;
        }
    }
    glAttachShader(program, vertShader);

    glLinkProgram(program);
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    glUseProgram(program);
    s->id          = program;
    s->uProjection = glGetUniformLocation(s->id, "uProjection");
    s->uTime       = glGetUniformLocation(s->id, "uTime");
    s->uFlipY      = glGetUniformLocation(s->id, "uFlipY");
    s->uSize       = glGetUniformLocation(s->id, "uSize");
    s->uPosition   = glGetUniformLocation(s->id, "uPosition");
    s->uWSize      = -1;

    return 0;
}

static int shader_create_wave(Shader* s) {
    const char* fsSource = " #version 330\n"
    " out vec4 FragColor;\n"
    " in vec2 TexCoord;\n"
    " uniform sampler2D texture0;\n"
    " uniform vec4 colDiffuse;\n"
    " out vec4 finalColor;\n"
    " uniform float uTime;\n"
    " uniform vec2  uWSize;\n"
    " uniform float freqX;\n"
    " uniform float freqY;\n"
    " uniform float ampX;\n"
    " uniform float ampY;\n"
    " uniform float speedX;\n"
    " uniform float speedY;\n"
    " void main()\n"
    " {\n"
    "     float pixelWidth = 1.0/uWSize.x;\n"
    "     float pixelHeight = 1.0/uWSize.y;\n"
    "     float aspect = pixelHeight/pixelWidth;\n"
    "     float boxLeft = 0.0;\n"
    "     float boxTop = 0.0;\n"
    "     vec2 p = TexCoord;\n"
    "     p.x += cos((TexCoord.y - boxTop)*freqX/(pixelWidth*750.0) + (uTime*speedX))*ampX*pixelWidth;\n"
    "     p.y += sin((TexCoord.x - boxLeft)*freqY*aspect/(pixelHeight*750.0) + (uTime*speedY))*ampY*pixelHeight;\n"
    "     finalColor = texture(texture0, p);\n"
    " }\n";

    shader_create(s, fsSource);
    glUseProgram(s->id);

    int freqXLoc  = glGetUniformLocation(s->id, "freqX");
    int freqYLoc  = glGetUniformLocation(s->id, "freqY");
    int ampXLoc   = glGetUniformLocation(s->id, "ampX");
    int ampYLoc   = glGetUniformLocation(s->id, "ampY");
    int speedXLoc = glGetUniformLocation(s->id, "speedX");
    int speedYLoc = glGetUniformLocation(s->id, "speedY");
    int uWSizeLoc = glGetUniformLocation(s->id,  "uWSize");

    float freqX  = 25.0f;
    float freqY  = 25.0f;
    float ampX   = 5.0f;
    float ampY   = 5.0f;
    float speedX = 8.0f;
    float speedY = 8.0f;

    glUniform2f(uWSizeLoc, (float)state.width, (float)state.height);
    glUniform1f(freqXLoc,  freqX);
    glUniform1f(freqYLoc,  freqY);
    glUniform1f(ampXLoc,   ampX);
    glUniform1f(ampYLoc,   ampY);
    glUniform1f(speedXLoc, speedX);
    glUniform1f(speedYLoc, speedY);

    s->uWSize = uWSizeLoc;

    return 0;
}

static int shader_create_default(Shader* s) {
    const char* fsSource = "#version 330 core\n"
        "out vec4 FragColor;\n"
        "in vec2 TexCoord;\n"
        "uniform sampler2D ourTexture;\n"
        "void main() {\n"
        "   FragColor = texture(ourTexture, TexCoord);\n"
        "}\n";
    shader_create(s, fsSource);
    return 0;
}
#if defined(__linux__)
static int image_map_file_src_into(Image* image, void **buf) {
    int fd = open(image->fileName, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        close(fd);
        return -1;
    }
    size_t filesize = st.st_size;

    *buf = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (*buf == MAP_FAILED) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    image->fileSize = st.st_size;
    printf("file: %s size: %ld\n", image->fileName, image->fileSize);

    return 0;
}
#else
static int image_open(Image* image) {
    long size = 0;

    image->fileHandle = NULL;
    image->fileSize   = 0;

    if ((image->fileHandle = fopen(image->fileName, "rb")) == NULL) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }
    if (fseek(image->fileHandle, 0, SEEK_END) < 0
        || ((size = ftell(image->fileHandle)) < 0)
        || fseek(image->fileHandle, 0, SEEK_SET) < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        fclose(image->fileHandle);
        return -1;
    }
    if (size == 0) {
        fprintf(stderr, "ERROR:%d: file empty\n", __LINE__);
        fclose(image->fileHandle);
        return -1;
    }

    image->fileSize = size;

    return 0;
}

static int image_map_file_src_into(Image* image, void **buf) {
    if (image_open(image) < 0) {
        return -1;
    }
    *buf = (void*)malloc(image->fileSize);
    if (*buf == NULL) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }
    if (fread(*buf, image->fileSize, 1, image->fileHandle) < 1) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }
    return 0;
}
#endif

// https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/src/tjdecomp.c
static int image_read_jpeg_into(Image* image, void *srcBuf, void *dstBuf) {
    int ret = -1, colorspace, jpegPrecision, w, h, pixelFormat = TJPF_BGRX;
    tjhandle tjh = NULL;

    tjh = tj3Init(TJINIT_DECOMPRESS);
    tj3Set(tjh, TJPARAM_STOPONWARNING, 1);
    tj3Set(tjh, TJPARAM_NOREALLOC,     1);
    tj3Set(tjh, TJPARAM_MAXMEMORY,     0);
    // tj3Set(tjh, TJPARAM_FASTDCT  ,     1);
    tj3Set(tjh, TJPARAM_FASTUPSAMPLE,  1);

    if (tj3DecompressHeader(tjh, srcBuf, image->fileSize) < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        goto cleanup;
    }
    profiler("tj3DecompressHeader");

    w = tj3Get(tjh, TJPARAM_JPEGWIDTH);
    h = tj3Get(tjh, TJPARAM_JPEGHEIGHT);
    jpegPrecision = tj3Get(tjh, TJPARAM_PRECISION);
    colorspace = tj3Get(tjh, TJPARAM_COLORSPACE);
    if (colorspace == TJCS_CMYK || colorspace == TJCS_YCCK) {
        fprintf(stderr, "ERROR:%d: CMYK/YCCK pixel formats not supported.\n", __LINE__);
        goto cleanup;
    }
    printf("precision: %d colorspace: %d\n", jpegPrecision, colorspace);

    // // tjscalingfactor scalingFactor = TJUNSCALED;
    // tjscalingfactor scalingFactor = {1,8};
    // if (tj3SetScalingFactor(tjh, scalingFactor) < 0) {
    //     fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
    //     goto cleanup;
    // }
    // w = TJSCALED(w, scalingFactor);
    // h = TJSCALED(h, scalingFactor);

    ret = tj3Decompress8(tjh, srcBuf, image->fileSize, dstBuf, 0, pixelFormat);
    profiler("tj3Decompress8");
    if (ret < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        ret = -1;
        goto cleanup;
    }

    ret           = 0;
    image->width  = w;
    image->height = h;

  cleanup:
#if defined(__linux__)
    munmap(srcBuf, image->fileSize);
#else
    free(srcBuf);
#endif
    tj3Destroy(tjh);
    if (image->fileHandle) fclose(image->fileHandle);
    return ret;
}
#if defined(__linux__)
static unsigned int image_read_png_into(Image* image, void *srcBuf, void *dstBuf) {
    png_image pngImage;
    memset(&pngImage, 0, (sizeof pngImage));
    pngImage.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&pngImage, srcBuf, image->fileSize)) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, pngImage.message);
        return -1;
    }
    profiler("png_image_begin_read_from_memory");

    pngImage.format = PNG_FORMAT_RGB;
    if (!png_image_finish_read(&pngImage, 0, dstBuf, 0, NULL)) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, pngImage.message);
        return -1;
    }
    profiler("png_image_finish_read");

    image->width  = pngImage.width;
    image->height = pngImage.height;

    return 0;
}
#endif
static int is_jpeg(const unsigned char *srcBuf, size_t size) {
    if (size < 3) return 0;
    return (srcBuf[0] == 0xFF && srcBuf[1] == 0xD8 && srcBuf[2] == 0xFF);
}

static int image_load_texture(Image* image) {
    unsigned int texture;
    profiler("image_load_texture");

    glGenBuffers(1, &pbo); GLERR;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo); GLERR;
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 4000 * 6000 * 4, NULL, GL_STREAM_DRAW); GLERR;
    // void* dstBuf = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY); GLERR;
    void* dstBuf = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 4000 * 6000 * 4, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT); GLERR;
    profiler("image_load_texture -> map buffers");

    void* srcBuf = NULL;
    if (image_map_file_src_into(image, &srcBuf) < 0) {
        return -1;
    }
    profiler("image_map_file_src_into");

    if (is_jpeg(srcBuf, image->fileSize)) {
            if (image_read_jpeg_into(image, srcBuf, dstBuf) < 0) {
                return -1;
            }
#if defined(__linux__)
    } else if (png_sig_cmp(srcBuf, 0, 8) == 0) {
        if (image_read_png_into(image, srcBuf, dstBuf) < 0) {
            return -1;
        }
#endif
    } else {
        fprintf(stderr, "ERROR:%d: unknown image type\n", __LINE__);
    }
    printf("image w:%d h:%d size:%ld \n", image->width, image->height, image->fileSize);

    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); GLERR;
    profiler("image_load_texture -> unmap buffers");

    glGenTextures(1, &texture); GLERR;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo); GLERR;
    glBindTexture(GL_TEXTURE_2D, texture); GLERR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); GLERR;
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR); GLERR;
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); GLERR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D,
        0, GL_BGRA, image->width, image->height,
        0, GL_BGRA, GL_UNSIGNED_BYTE, NULL
    ); GLERR;
    glGenerateMipmap(GL_TEXTURE_2D); GLERR;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); GLERR;
    profiler("image_load_texture -> texture");

    image->textureId = texture;

    return 0;
}

static void init_vao() {
    float vertices[] = {
        1.0f, 1.0f,  1.0f, 1.0f, // Top Right
        1.0f, 0.0f,  1.0f, 0.0f, // Bottom Right
        0.0f, 0.0f,  0.0f, 0.0f, // Bottom Left
        0.0f, 1.0f,  0.0f, 1.0f  // Top Left
    };
    unsigned int indices[] = {
        0, 1, 3,
        1, 2, 3
    };
    glGenVertexArrays(1, &state.VAO);
    glGenBuffers(1, &state.VBO);
    glGenBuffers(1, &state.EBO);
    glBindVertexArray(state.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, state.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_image>\n", argv[0]);
        return 1;
    }
    profiler_time_sum = profiler_time = clock();

    if (!init_glfw())
        return 2;

    if (state.testCycling) {
        Image t  = { .fileName = "images/architecture.jpg", .fileSize = 0 };
        imageArray[0] = t;
        Image t2 = { .fileName = "images/lost-place.jpg",   .fileSize = 0 };
        imageArray[1] = t2;
        image_load_texture(&imageArray[0]);
        image_load_texture(&imageArray[1]);
    } else {
        // Image t = { .fileName = "images\\architecture.jpg" };
        Image t = { .fileName = argv[1] };
        imageArray[0] = t;
        image_load_texture(&imageArray[0]);
    }
    image = &imageArray[0];
    if (!image->textureId) {
        return 3;
    }
    shader_create_default(&shaders[0]);
    shader_create_wave(&shaders[1]);
    profiler("shader_create_*");

    init_vao();
    profiler("init_vao");

    glBindTexture(GL_TEXTURE_2D, image->textureId); GLERR;

    int i = 0;
    for (;i < len(shaders); i++) {
        glUseProgram(shaders[i].id); GLERR;
        glUniform2f(shaders[i].uPosition, 0.0f, 0.0f); GLERR;
        glUniform1i(shaders[i].uFlipY,    1); GLERR;
        glUniform2f(shaders[i].uSize,     (float)image->width, (float)image->height); GLERR;
    }
    glUseProgram(shaders[shaderIndex].id); GLERR;
    // glfwWaitEventsTimeout(1);

    iv_camera_fit(image);

    glfwShowWindow(state.window);

    profiler("last mile");
    printf("> profiler: %8.2fms - sum\n", ((float)(clock() - profiler_time_sum) / CLOCKS_PER_SEC)*1000);
    double previous = glfwGetTime();
    while (!glfwWindowShouldClose(state.window)) {
        double current = glfwGetTime();
        double update = current - previous;
        previous = current;

        if (!state.isMovingWindow) {
            glClear(GL_COLOR_BUFFER_BIT);
            if (state.isDirty) {
                state.isDirty = 0;
                iv_camera_update_projection(image);

                glUniformMatrix4fv(shaders[shaderIndex].uProjection, 1, GL_FALSE, state.projection);
            }
            if (shaders[shaderIndex].uTime > 0) {
                glUniform1f(shaders[shaderIndex].uTime, (float)glfwGetTime());
            }

            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glfwSwapBuffers(state.window);
        }

        // throttling from raylib
        current = glfwGetTime();
        double draw = current - previous;
        previous = current;
        double frame = update + draw;
        double target = (1.0/(double)state.targetFrameRate);
        if (frame < target) {
            iv_sleep(target - frame);
        }
        previous = glfwGetTime();

        if (shaders[shaderIndex].uTime > 0 || state.isPanning || state.isMovingWindow)
            glfwPollEvents();
        else
            glfwWaitEvents();
    }

    glDeleteShader(shaders[0].id);
    glDeleteShader(shaders[1].id);
    glDeleteBuffers(1, &pbo);
    glfwTerminate();
    return 0;
}
