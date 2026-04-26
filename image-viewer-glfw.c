#include "glad/glad.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include <turbojpeg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#define len(x)  (sizeof(x) / sizeof((x)[0]))

typedef struct {
    unsigned int id;
    int projection;
    int time;
    int flipy;
} Shader;
Shader shaders[2];
unsigned int shaderIndex = 0;

struct State {
    float projection[16];

    double lastMouseX, lastMouseY;
    double mouseX,     mouseY;
    float  offsetX,    offsetY;

    int windowWidth, windowHeight;
    int imageWidth,  imageHeight;

    int isDragging;
    int isDirty;
    int isZoom;

    unsigned int VAO;
    unsigned int texture;
    GLFWwindow* window;

} state;

int useStb = 0;
clock_t time_req;

static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_SPACE && action == GLFW_RELEASE) {
        state.isDirty = 1;
        state.isZoom = state.isZoom ? 0 : 1;
    }
    if (key == GLFW_KEY_S && action == GLFW_RELEASE) {
        state.isDirty = 1;
        shaderIndex = shaderIndex+1 >= len(shaders) ? 0 : shaderIndex + 1;
    }
}

static void cursor_position_callback(GLFWwindow* window, double x, double y) {
    state.mouseX  = x;
    state.mouseY  = y;
    state.isDirty = 1;
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (state.isZoom) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            glfwSetCursorPosCallback(window, cursor_position_callback);
            state.isDragging = 1;
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            state.mouseX = x;
            state.mouseY = y;
            state.lastMouseX = x;
            state.lastMouseY = y;
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
            glfwSetCursorPosCallback(window, NULL);
            state.isDragging = 0;
        }
    }
}

static void window_size_callback(GLFWwindow* window, int width, int height) {
    state.isDirty = 1;
    state.windowWidth = width;
    state.windowHeight = height;
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
}
GLenum glCheckError_(const char *file, int line)
{
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR)
    {
        const char* error;
        switch (errorCode)
        {
            case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
            // case GL_STACK_OVERFLOW:                error = "STACK_OVERFLOW"; break;
            // case GL_STACK_UNDERFLOW:               error = "STACK_UNDERFLOW"; break;
            case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
        }
        printf("GLERROR:%d: %s\n", line, error);
    }
    return errorCode;
}
#define glCheckError() glCheckError_(__FILE__, __LINE__)

static int init_glfw() {
    glfwSetErrorCallback(error_callback);
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    // glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    // glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_PREFER_LIBDECOR);
    if (!glfwInit())
        return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    state.window = glfwCreateWindow(state.windowWidth, state.windowHeight, "Image Viewer", NULL, NULL);
    if (!state.window) {
        glfwTerminate();
        return 0;
    }
    glfwMakeContextCurrent(state.window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        printf("Failed to nitialize GLAD\n");
        return 0;
    }
    printf("gl version: %s\n", glGetString(GL_VERSION));
    printf("renderer: %s\n", glGetString(GL_RENDERER));
    // glfwSetCursorPosCallback(window, cursor_position_callback);
    // glfwSetWindowRefreshCallback(window, window_refresh_callback);
    glfwSetMouseButtonCallback(state.window, mouse_button_callback);
    glfwSetKeyCallback(state.window, key_callback);
    glfwSetWindowSizeCallback(state.window, window_size_callback);
    glfwSwapInterval(1);
    // glEnable(GL_FRAMEBUFFER_SRGB);
    // glEnable(GL_BLEND);
    // glDisable(GL_BLEND);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return 1;
}

static int shader_create(Shader* s, const char* fsSource) {
    int success;
    char infoLog[512];
    unsigned int program, vertShader, fragShader;

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

    const char* vsSource = "#version 330 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "layout (location = 1) in vec2 aTexCoord;\n"
        "\n"
        "out vec2 TexCoord;\n"
        "uniform mat4 uProjection;\n"
        "uniform int uFlipY;"
        "\n"
        "void main() {\n"
        "    gl_Position = uProjection * vec4(aPos, 1.0);\n"
        "    TexCoord.x = aTexCoord.x;\n"
        "    TexCoord.y = abs(uFlipY - aTexCoord.y);\n"
        "}\n";
    vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &vsSource, NULL);
    glCompileShader(vertShader);
    glGetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertShader, 512, NULL, infoLog);
        printf("ERROR: Shader Compilation Failed\n%s\n", infoLog);
        return 0;
    }
    glAttachShader(program, vertShader);

    glLinkProgram(program);
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    s->id = program;
    return 0;
}

static int shader_create_wave(Shader* s) {
    const char* fsSource = " #version 330\n"
    " out vec4 FragColor;\n"
    " in vec2 TexCoord;\n"
    " uniform sampler2D texture0;\n"
    " uniform vec4 colDiffuse;\n"
    " out vec4 finalColor;\n"
    " uniform float seconds;\n"
    " uniform vec2 size;\n"
    " uniform float freqX;\n"
    " uniform float freqY;\n"
    " uniform float ampX;\n"
    " uniform float ampY;\n"
    " uniform float speedX;\n"
    " uniform float speedY;\n"
    " void main()\n"
    " {\n"
    "     float pixelWidth = 1.0/size.x;\n"
    "     float pixelHeight = 1.0/size.y;\n"
    "     float aspect = pixelHeight/pixelWidth;\n"
    "     float boxLeft = 0.0;\n"
    "     float boxTop = 0.0;\n"
    "     vec2 p = TexCoord;\n"
    "     p.x += cos((TexCoord.y - boxTop)*freqX/(pixelWidth*750.0) + (seconds*speedX))*ampX*pixelWidth;\n"
    "     p.y += sin((TexCoord.x - boxLeft)*freqY*aspect/(pixelHeight*750.0) + (seconds*speedY))*ampY*pixelHeight;\n"
    // "     finalColor = texture(texture0, p)*colDiffuse*fragColor;\n"
    "     finalColor = texture(texture0, p);\n"
    " }\n";
    shader_create(s, fsSource);
    glUseProgram(s->id);

    int freqXLoc = glGetUniformLocation(s->id, "freqX");
    int freqYLoc = glGetUniformLocation(s->id, "freqY");
    int ampXLoc = glGetUniformLocation(s->id, "ampX");
    int ampYLoc = glGetUniformLocation(s->id, "ampY");
    int speedXLoc = glGetUniformLocation(s->id, "speedX");
    int speedYLoc = glGetUniformLocation(s->id, "speedY");
    int sizeLoc = glGetUniformLocation(s->id, "size");

    float freqX = 25.0f;
    float freqY = 25.0f;
    float ampX = 5.0f;
    float ampY = 5.0f;
    float speedX = 8.0f;
    float speedY = 8.0f;

    glUniform2f(sizeLoc, (float)state.windowWidth, (float)state.windowHeight);
    glUniform1f(freqXLoc, freqX);
    glUniform1f(freqYLoc, freqY);
    glUniform1f(ampXLoc, ampX);
    glUniform1f(ampYLoc, ampY);
    glUniform1f(speedXLoc, speedX);
    glUniform1f(speedYLoc, speedY);

    s->projection = glGetUniformLocation(s->id, "uProjection");
    s->time = glGetUniformLocation(s->id, "seconds");
    s->flipy = glGetUniformLocation(s->id, "uFlipY");

    return 0;
}

static int shader_create_default(Shader* s) {
    const char* fsSource = "#version 330 core\n"
        "out vec4 FragColor;\n"
        "in vec2 TexCoord;\n"
        "uniform sampler2D ourTexture;\n"
        "void main() {\n"
        "   FragColor = texture(ourTexture, TexCoord);\n"
        "}\n\0";

    shader_create(s, fsSource);
    glUseProgram(s->id);

    s->projection = glGetUniformLocation(s->id, "uProjection");
    s->time = glGetUniformLocation(s->id, "seconds");
    s->flipy = glGetUniformLocation(s->id, "uFlipY");

    return 0;
}

static FILE* open_file_handle(const char* filename, size_t *fileSize) {
    long size = 0;
    FILE* fh = NULL;
    if ((fh = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return 0;
    }
    if (fseek(fh, 0, SEEK_END) < 0 || ((size = ftell(fh)) < 0) || fseek(fh, 0, SEEK_SET) < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        fclose(fh);
        return 0;
    }
    if (size == 0) {
        fprintf(stderr, "ERROR:%d: file empty\n", __LINE__);
        fclose(fh);
        return 0;
    }
    *fileSize = size;
    return fh;
}

static int read_jpeg_into(const char* filename, void **dstBuf, int *width, int *height) {
    // https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/src/tjdecomp.c
    int ret = -1, colorspace, jpegPrecision, lossless, w, h,
        pixelFormat = TJPF_UNKNOWN, precision = -1, stopOnWarning = -1;
    tjhandle tjh = NULL;
    FILE *fh = NULL;
    size_t size, sampleSize;
    unsigned char *srcBuf = NULL;

    fh = open_file_handle(filename, &size);
    if (fh == NULL) {
        return ret;
    }
    printf("file: %s size: %ld;; \n", filename, size);

    tjh = tj3Init(TJINIT_DECOMPRESS);
    printf("TJ Version: %d\n", TURBOJPEG_VERSION_NUMBER);
    tj3Set(tjh, TJPARAM_STOPONWARNING, stopOnWarning);
    tj3Set(tjh, TJPARAM_NOREALLOC, 1);
    tj3Set(tjh, TJPARAM_MAXMEMORY, 0);
    // tj3Set(tjh, TJPARAM_SCANLIMIT, 0);
    tj3Set(tjh, TJPARAM_FASTDCT  , 1);
    // tj3Set(tjh, TJPARAM_FASTUPSAMPLE, 1);
    // tjscalingfactor scalingFactor = TJUNSCALED;
    // tjscalingfactor scalingFactor = {1,1};

    srcBuf = (unsigned char *)malloc(size);
    if (srcBuf == NULL) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        goto cleanup;
    }

    time_req = clock();
    if (fread(srcBuf, size, 1, fh) < 1) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        goto cleanup;
    }
    printf("IMAGE READ: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);
    fclose(fh);
    fh = NULL;

    time_req = clock();
    if (tj3DecompressHeader(tjh, srcBuf, size) < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        goto cleanup;
    }
    printf("IMAGE HEADER: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    w = tj3Get(tjh, TJPARAM_JPEGWIDTH);
    h = tj3Get(tjh, TJPARAM_JPEGHEIGHT);
    jpegPrecision = tj3Get(tjh, TJPARAM_PRECISION);
    colorspace = tj3Get(tjh, TJPARAM_COLORSPACE);
    if (colorspace == TJCS_CMYK || colorspace == TJCS_YCCK)
      pixelFormat = TJPF_CMYK;
    else
      pixelFormat = TJPF_RGBA;

    if (precision == -1 || lossless || jpegPrecision != 8)
        precision = jpegPrecision;

    sampleSize = (precision <= 8 ? 1 : 2);
    pixelFormat = TJPF_RGB;
    printf("size w:%d, h:%d, prec: %d\n", w, h, precision);

    // if (tj3SetScalingFactor(tjh, scalingFactor) < 0) {
    //     fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
    //     goto cleanup;
    // }
    // w = TJSCALED(w, scalingFactor);
    // h = TJSCALED(h, scalingFactor);

    if (*dstBuf == NULL) {
        *dstBuf = (unsigned char *)malloc(
            w * h * tjPixelSize[pixelFormat] * sampleSize
        );
        if (*dstBuf == NULL) {
            fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
            goto cleanup;
        }
    }

    time_req = clock();
    if (precision <= 8) {
      ret = tj3Decompress8 (tjh, srcBuf, size, *dstBuf, 0, pixelFormat);
    } else if (precision <= 12) {
      ret = tj3Decompress12(tjh, srcBuf, size, *dstBuf, 0, pixelFormat);
    } else {
      ret = tj3Decompress16(tjh, srcBuf, size, *dstBuf, 0, pixelFormat);
    }
    printf("> IMAGE DEC: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);
    if (ret < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        ret = -1;
        goto cleanup;
    }
    ret     = 0;
    *width  = w;
    *height = h;

  cleanup:
    tj3Free(srcBuf);
    srcBuf = NULL;
    tj3Destroy(tjh);
    if (fh) fclose(fh);
    return ret;
}

static unsigned int read_stb_image_into(const char* filename, void **dstBuf, int *width, int *height) {
    int channels;
    // stbi_set_flip_vertically_on_load(0);
    *dstBuf = (unsigned char *)stbi_load(filename, &state.imageWidth, &state.imageHeight, &channels, 4);
    if (!dstBuf) {
        fprintf(stderr, "Error: Could not load image '%s'\n", filename);
        fprintf(stderr, "Reason: %s\n", stbi_failure_reason());
        return -1;
    }
    return 0;
}

static unsigned int init_image_texture(const char* filename) {
    GLuint pbo;
    unsigned int texture;

    time_req = clock();
    glGenBuffers(1, &pbo);
    glCheckError();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    glCheckError();
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 4000 * 6000 * 3, NULL, GL_STREAM_DRAW);
    glCheckError();
    // glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    // glCheckError();
    // glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    // glCheckError();
    // glBufferData(GL_PIXEL_UNPACK_BUFFER, 4000 * 6000 * 4, NULL, GL_STREAM_DRAW);
    // glCheckError();
    printf("> PBO SETUP: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    time_req = clock();
    void* ptr = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    glCheckError();
    printf("> PBO MAP: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    read_jpeg_into(filename, &ptr, &state.imageWidth, &state.imageHeight);
    // if (useStb) {
    //     read_stb_image_into(filename, &ptr, &state.imageWidth, &state.imageHeight);
    // }
    time_req = clock();
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glCheckError();
    printf("> PBO UNMAP: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    time_req = clock();
    glGenTextures(1, &texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    glCheckError();
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glCheckError();
    glTexImage2D(GL_TEXTURE_2D,
        0, GL_RGB8, state.imageWidth, state.imageHeight,
        0, GL_RGB, GL_UNSIGNED_BYTE, NULL
    );
    glCheckError();
    glGenerateMipmap(GL_TEXTURE_2D);
    glCheckError();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glCheckError();
    printf("> PBO TEX: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    return texture;
}

static unsigned int init_image_texture2(const char* filename) {
    void *dstBuf = NULL;
    unsigned int texture;

    if (useStb) {
        if (read_stb_image_into(filename, &dstBuf, &state.imageWidth, &state.imageHeight) < 0) {
            return 0;
        }
    } else {
        if (read_jpeg_into(filename, &dstBuf, &state.imageWidth, &state.imageHeight) < 0) {
            return 0;
        }
    }

    time_req = clock();
    glGenTextures(1, &texture);
    glCheckError();

    glBindTexture(GL_TEXTURE_2D, texture);
    glCheckError();

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glCheckError();
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    // float maxAnisotropy = 0.0f;
    // glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
    // glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisotropy);


    // glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
        0, GL_RGB8, state.imageWidth, state.imageHeight,
        0, GL_RGB, GL_UNSIGNED_BYTE, dstBuf
    );
    glCheckError();

    glGenerateMipmap(GL_TEXTURE_2D);
    glCheckError();

    printf("> NORM TEX: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);
    free(dstBuf);
    return texture;
}

static unsigned int init_vao() {
    float vertices[] = {
        (float)state.imageWidth, (float)state.imageHeight, 0.0f,  1.0f, 1.0f,
        (float)state.imageWidth, 0.0f,                     0.0f,  1.0f, 0.0f,
        0.0f,                    0.0f,                     0.0f,  0.0f, 0.0f,
        0.0f,                    (float)state.imageHeight, 0.0f,  0.0f, 1.0f
    };
    unsigned int indices[] = {
        0, 1, 3,
        1, 2, 3
    };
    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    return VAO;
}

static void update_mouse_dragging() {
    if (!state.isDragging)
        return;
    state.offsetX -= (float)(state.mouseX - state.lastMouseX);
    state.offsetY += (float)(state.mouseY - state.lastMouseY);
    if (state.offsetX > state.imageWidth / 2.0f) state.offsetX = state.imageWidth / 2.0f;
    if (state.offsetX < -state.imageWidth / 2.0f) state.offsetX = -state.imageWidth / 2.0f;
    if (state.offsetY > state.imageHeight / 2.0f) state.offsetY = state.imageHeight / 2.0f;
    if (state.offsetY < -state.imageHeight / 2.0f) state.offsetY = -state.imageHeight / 2.0f;
    state.lastMouseX = state.mouseX;
    state.lastMouseY = state.mouseY;
}

static void get_ortho_matrix(float* m, float l, float r, float b, float t) {
    for(int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] = 1.0f;
}

static void update_projection() {
    float left, right, bottom, top;

    if (state.isZoom) {
        float centerX = (state.imageWidth / 2.0f) + state.offsetX;
        float centerY = (state.imageHeight / 2.0f) + state.offsetY;

        left   = centerX - (state.windowWidth / 2.0f);
        right  = centerX + (state.windowWidth / 2.0f);
        bottom = centerY - (state.windowHeight / 2.0f);
        top    = centerY + (state.windowHeight / 2.0f);
    } else {
        float windowAspect = (float)state.windowWidth / (float)state.windowHeight;
        float imageAspect = (float)state.imageWidth / (float)state.imageHeight;
        if (windowAspect > imageAspect) {
            float worldWidth = state.imageHeight * windowAspect;
            left = -(worldWidth - state.imageWidth) / 2.0f;
            right = state.imageWidth + (worldWidth - state.imageWidth) / 2.0f;
            bottom = 0.0f;
            top = (float)state.imageHeight;
        } else {
            float worldHeight = state.imageWidth / windowAspect;
            left = 0.0f;
            right = (float)state.imageWidth;
            bottom = -(worldHeight - state.imageHeight) / 2.0f;
            top = state.imageHeight + (worldHeight - state.imageHeight) / 2.0f;
        }
    }
    get_ortho_matrix(state.projection, left, right, bottom, top);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_image>\n", argv[0]);
        return 1;
    }
    const char* filename = argv[1];

    state.isDirty = 1;
    state.windowWidth = 640;
    state.windowHeight = 480;

    if (!init_glfw())
        return 2;

    state.texture = init_image_texture(filename);
    if (!state.texture) {
        return 3;
    }
    time_req = clock();
    shader_create_default(&shaders[0]);
    shader_create_wave(&shaders[1]);
    printf("> SHADER: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    time_req = clock();
    state.VAO = init_vao();
    glBindVertexArray(state.VAO);
    printf("> VAO: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    time_req = clock();
    glUseProgram(shaders[0].id);
    glUniform1i(shaders[0].flipy, 1);
    glUseProgram(shaders[1].id);
    glUniform1i(shaders[1].flipy, 1);
    glBindTexture(GL_TEXTURE_2D, state.texture);
    // glfwWaitEventsTimeout(1);
    printf("> FIN: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    while (!glfwWindowShouldClose(state.window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaders[shaderIndex].id);

        if (state.isDirty) {
            state.isDirty = 0;
            update_mouse_dragging();
            update_projection();
            glUniformMatrix4fv(shaders[shaderIndex].projection, 1, GL_FALSE, state.projection);
        }
        if (shaderIndex && shaders[shaderIndex].time != -1)
            glUniform1f(shaders[shaderIndex].time, (float)glfwGetTime());

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glfwSwapBuffers(state.window);

        if (shaderIndex)
            glfwPollEvents();
        else
            glfwWaitEvents();
    }

    glfwTerminate();
    return 0;
}
