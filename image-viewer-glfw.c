#include "glad/glad.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include <turbojpeg.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define len(x)  (sizeof(x) / sizeof((x)[0]))

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

    double lastMouseX, lastMouseY;
    double mouseX,     mouseY;
    float  offsetX,    offsetY;

    int windowWidth, windowHeight;

    int isPanning;
    int isDirty;
    int isZoom;

    unsigned int VAO, EBO, VBO;
    GLFWwindow* window;

} state;

int useStb = 0;
int useMmap = 1;
clock_t time_req;
int testCycling = 1;

int imageIndex = 0;
Image imageArray[2];
Image* image;

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
        glUseProgram(shaders[shaderIndex].id);
        glUniform2f(shaders[shaderIndex].uSize, (float)image->width, (float)image->height);
        if (shaders[shaderIndex].uWSize != -1)
            glUniform2f(shaders[shaderIndex].uWSize, (float)state.windowWidth, (float)state.windowHeight);
    }
    if (testCycling && (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) && action == GLFW_RELEASE) {
        state.isDirty = 1;
        imageIndex = imageIndex+1 >= len(imageArray) ? 0 : imageIndex + 1;
        image = &imageArray[imageIndex];
        glBindTexture(GL_TEXTURE_2D, image->textureId);
        glUseProgram(shaders[shaderIndex].id);
        glUniform2f(shaders[shaderIndex].uSize, (float)image->width, (float)image->height);
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
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            state.mouseX     = x;
            state.mouseY     = y;
            state.lastMouseX = x;
            state.lastMouseY = y;
            state.isPanning  = 1;
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
            glfwSetCursorPosCallback(window, NULL);
            state.isPanning = 0;
        }
    }
}

static void window_size_callback(GLFWwindow* window, int width, int height) {
    state.isDirty      = 1;
    state.windowWidth  = width;
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
        printf("Failed to initialize GLAD\n");
        return 0;
    }
    printf("GL_VERSION: %s\n",        glGetString(GL_VERSION));
    printf("GL_RENDERER: %s\n",       glGetString(GL_RENDERER));
    printf("TURBOJPEG_VERSION: %d\n", TURBOJPEG_VERSION_NUMBER);

    glfwSetMouseButtonCallback(state.window, mouse_button_callback);
    glfwSetKeyCallback(state.window, key_callback);
    glfwSetWindowSizeCallback(state.window, window_size_callback);
    glfwSwapInterval(1);

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

    glUniform2f(uWSizeLoc, (float)state.windowWidth, (float)state.windowHeight);
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

    image->fileSize = st.st_size;
    close(fd);
    return 0;
}

static int image_read_file_src_into(Image* image, void **buf) {
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
// https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/src/tjdecomp.c
static int image_read_jpeg_into(Image* image, void **dstBuf) {
    int ret = -1, colorspace, jpegPrecision, w, h, pixelFormat = TJPF_RGB;
    tjhandle tjh = NULL;

    tjh = tj3Init(TJINIT_DECOMPRESS);
    tj3Set(tjh, TJPARAM_STOPONWARNING, 1);
    tj3Set(tjh, TJPARAM_NOREALLOC,     1);
    tj3Set(tjh, TJPARAM_MAXMEMORY,     0);
    tj3Set(tjh, TJPARAM_FASTDCT  ,     1);
    // tj3Set(tjh, TJPARAM_FASTUPSAMPLE,  1);

    time_req = clock();
    void *srcBuf = NULL;
    if (useMmap) {
        if (image_map_file_src_into(image, &srcBuf) < 0) {
            goto cleanup;
        }
    } else {
        if (image_read_file_src_into(image, &srcBuf) < 0) {
            goto cleanup;
        }
    }
    printf("IMAGE READ: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);
    printf("file: %s size: %ld\n", image->fileName, image->fileSize);

    time_req = clock();
    if (tj3DecompressHeader(tjh, srcBuf, image->fileSize) < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        goto cleanup;
    }
    printf("IMAGE HEADER: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    w = tj3Get(tjh, TJPARAM_JPEGWIDTH);
    h = tj3Get(tjh, TJPARAM_JPEGHEIGHT);
    jpegPrecision = tj3Get(tjh, TJPARAM_PRECISION);
    colorspace = tj3Get(tjh, TJPARAM_COLORSPACE);
    if (colorspace == TJCS_CMYK || colorspace == TJCS_YCCK) {
        fprintf(stderr, "ERROR:%d: CMYK/YCCK pixel formats not supported.\n", __LINE__);
        goto cleanup;
    }
    printf("precision: %d colorspace: %d\n", jpegPrecision, colorspace);

    // tjscalingfactor scalingFactor = TJUNSCALED;
    // tjscalingfactor scalingFactor = {1,4};
    // if (tj3SetScalingFactor(tjh, scalingFactor) < 0) {
    //     fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
    //     goto cleanup;
    // }
    // w = TJSCALED(w, scalingFactor);
    // h = TJSCALED(h, scalingFactor);

    time_req = clock();
    ret = tj3Decompress8(tjh, srcBuf, image->fileSize, *dstBuf, 0, pixelFormat);
    printf("> IMAGE DEC: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);
    if (ret < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        ret = -1;
        goto cleanup;
    }

    ret           = 0;
    image->width  = w;
    image->height = h;

  cleanup:
    if (useMmap) {
        munmap(srcBuf, image->fileSize);
    } else {
        tj3Free(srcBuf);
        srcBuf = NULL;
    }
    tj3Destroy(tjh);
    if (image->fileHandle) fclose(image->fileHandle);
    return ret;
}

static unsigned int image_read_stb_into(Image* image, void **dstBuf) {
    int channels;
    if (image_open(image) < 0) {
        return -1;
    }
    unsigned char* srcBuf = (unsigned char *)malloc(image->fileSize);
    if (srcBuf == NULL) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }
    if (fread(srcBuf, image->fileSize, 1, image->fileHandle) < 1) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }
    fclose(image->fileHandle);
    image->fileHandle = NULL;
    printf(">> ptr %p ", (void*)dstBuf);
    printf(">> ptr %p ", (void*)*dstBuf);
    *dstBuf = (unsigned char *)stbi_load_from_memory(srcBuf, image->fileSize, &image->width, &image->height, &channels, 3);
    if (!(*dstBuf)) {
        fprintf(stderr, "Error: Could not load image '%s'\n", image->fileName);
        fprintf(stderr, "Reason: %s\n", stbi_failure_reason());
        return -1;
    }
    printf(">> ptr %p ", (void*)dstBuf);
    printf(">> ptr %p ", (void*)*dstBuf);
    return 0;
}

static int image_load_texture(Image* image) {
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

    if (useStb) {
        time_req = clock();
        int ret = image_read_stb_into(image, &ptr);
        printf("> IMAGE READ: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);
        if (ret < 0) {
            return -1;
        }
    } else {
        if (image_read_jpeg_into(image, &ptr) < 0)
            return -1;
    }
    printf("image w:%d h:%d size:%ld \n", image->width, image->height, image->fileSize);

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

    // glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glCheckError();

    glTexImage2D(GL_TEXTURE_2D,
        0, GL_RGB8, image->width, image->height,
        0, GL_RGB, GL_UNSIGNED_BYTE, NULL
    );
    glCheckError();
    glGenerateMipmap(GL_TEXTURE_2D);
    glCheckError();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glCheckError();
    printf("> PBO TEX: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

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

static void update_mouse_panning(const Image* image) {
    if (!state.isPanning)
        return;
    state.offsetX -= (float)(state.mouseX - state.lastMouseX);
    state.offsetY += (float)(state.mouseY - state.lastMouseY);
    if (state.offsetX > image->width / 2.0f) state.offsetX = image->width / 2.0f;
    if (state.offsetX < -image->width / 2.0f) state.offsetX = -image->width / 2.0f;
    if (state.offsetY > image->height / 2.0f) state.offsetY = image->height / 2.0f;
    if (state.offsetY < -image->height / 2.0f) state.offsetY = -image->height / 2.0f;
    state.lastMouseX = state.mouseX;
    state.lastMouseY = state.mouseY;
}

static void update_ortho_matrix(float* m, float l, float r, float b, float t) {
    for(int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] = 1.0f;
}

static void update_projection(const Image* image) {
    float left, right, bottom, top;

    if (state.isZoom) {
        float centerX = (image->width / 2.0f) + state.offsetX;
        float centerY = (image->height / 2.0f) + state.offsetY;

        left   = centerX - (state.windowWidth / 2.0f);
        right  = centerX + (state.windowWidth / 2.0f);
        bottom = centerY - (state.windowHeight / 2.0f);
        top    = centerY + (state.windowHeight / 2.0f);
    } else {
        float windowAspect = (float)state.windowWidth / (float)state.windowHeight;
        float imageAspect = (float)image->width / (float)image->height;
        if (windowAspect > imageAspect) {
            float worldWidth = image->height * windowAspect;
            left = -(worldWidth - image->width) / 2.0f;
            right = image->width + (worldWidth - image->width) / 2.0f;
            bottom = 0.0f;
            top = (float)image->height;
        } else {
            float worldHeight = image->width / windowAspect;
            left = 0.0f;
            right = (float)image->width;
            bottom = -(worldHeight - image->height) / 2.0f;
            top = image->height + (worldHeight - image->height) / 2.0f;
        }
    }
    update_ortho_matrix(state.projection, left, right, bottom, top);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_image>\n", argv[0]);
        return 1;
    }

    state.isDirty      = 1;
    state.isZoom       = 0;
    state.windowWidth  = 640;
    state.windowHeight = 480;

    if (!init_glfw())
        return 2;

    if (testCycling) {
        Image t  = { .fileName = "images/architecture.jpg", .fileSize = 0 };
        imageArray[0] = t;
        Image t2 = { .fileName = "images/lost-place.jpg",   .fileSize = 0 };
        imageArray[1] = t2;
        image_load_texture(&imageArray[0]);
        image_load_texture(&imageArray[1]);
    } else {
        Image t = { .fileName = argv[1], .fileSize = 0 };
        imageArray[0] = t;
        image_load_texture(&imageArray[0]);
    }
    image = &imageArray[0];
    if (!image->textureId) {
        return 3;
    }
    time_req = clock();
    shader_create_default(&shaders[0]);
    shader_create_wave(&shaders[1]);
    printf("> SHADER: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    time_req = clock();
    init_vao();
    printf("> VAO: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    time_req = clock();
    glBindTexture(GL_TEXTURE_2D, image->textureId);

    int i = 0;
    for (;i < len(shaders); i++) {
        glUseProgram(shaders[i].id);
        glUniform2f(shaders[i].uPosition, 0.0f, 0.0f);
        glUniform1i(shaders[i].uFlipY,    1);
        glUniform2f(shaders[i].uSize,     (float)image->width, (float)image->height);
    }
    glUseProgram(shaders[shaderIndex].id);

    // glfwWaitEventsTimeout(1);
    printf("> FIN: %f\n", (float)(clock() - time_req) / CLOCKS_PER_SEC);

    while (!glfwWindowShouldClose(state.window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        if (state.isDirty) {
            state.isDirty = 0;
            update_mouse_panning(image);
            update_projection(image);

            glUniformMatrix4fv(shaders[shaderIndex].uProjection, 1, GL_FALSE, state.projection);
        }
        if (shaders[shaderIndex].uTime > 0) {
            glUniform1f(shaders[shaderIndex].uTime, (float)glfwGetTime());
        }

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glfwSwapBuffers(state.window);

        if (shaders[shaderIndex].uTime > 0)
            glfwPollEvents();
        else
            glfwWaitEvents();
    }

    glfwTerminate();
    return 0;
}
