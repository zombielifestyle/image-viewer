#include "glad/glad.h"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include <stdio.h>

#define len(x)  (sizeof(x) / sizeof((x)[0]))

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

    unsigned int shaderIndex;
    unsigned int shaders[2];
} state;

int projLoc;
int timeLoc;

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
        state.shaderIndex = state.shaderIndex+1 >= len(state.shaders) ? 0 : state.shaderIndex + 1;
        glUseProgram(state.shaders[state.shaderIndex]);
        projLoc = glGetUniformLocation(state.shaders[state.shaderIndex], "uProjection");
        timeLoc = glGetUniformLocation(state.shaders[state.shaderIndex], "seconds");
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
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // glDisable(GL_BLEND);
    return 1;
}

static unsigned int load_shader(const char* fsSource) {
    int success;
    char infoLog[512];
    unsigned int shaderProgram, vertexShader, fragmentShader;

    shaderProgram = glCreateProgram();

    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fsSource, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        printf("ERROR: Shader Compilation Failed\n%s\n", infoLog);
        return 0;
    }
    glAttachShader(shaderProgram, fragmentShader);

    const char* vsSource = "#version 330 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "layout (location = 1) in vec2 aTexCoord;\n"
        "\n"
        "out vec2 TexCoord;\n"
        "uniform mat4 uProjection;\n"
        "\n"
        "void main() {\n"
        "    gl_Position = uProjection * vec4(aPos, 1.0);\n"
        "    TexCoord = aTexCoord;\n"
        "}\n";
    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vsSource, NULL);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        printf("ERROR: Shader Compilation Failed\n%s\n", infoLog);
        return 0;
    }
    glAttachShader(shaderProgram, vertexShader);

    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

static unsigned int load_wave_shader() {
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
    unsigned int shader = load_shader(fsSource);
    glUseProgram(shader);

    int freqXLoc = glGetUniformLocation(shader, "freqX");
    int freqYLoc = glGetUniformLocation(shader, "freqY");
    int ampXLoc = glGetUniformLocation(shader, "ampX");
    int ampYLoc = glGetUniformLocation(shader, "ampY");
    int speedXLoc = glGetUniformLocation(shader, "speedX");
    int speedYLoc = glGetUniformLocation(shader, "speedY");
    int sizeLoc = glGetUniformLocation(shader, "size");

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

    return shader;
}

static unsigned int load_default_shader() {
    const char* fsSource = "#version 330 core\n"
        "out vec4 FragColor;\n"
        "in vec2 TexCoord;\n"
        "uniform sampler2D ourTexture;\n"
        "void main() {\n"
        "   FragColor = texture(ourTexture, TexCoord);\n"
        "}\n\0";
    return load_shader(fsSource);
}

static unsigned int init_image_texture(const char* filename) {
    int channels;
    unsigned int texture;
    stbi_set_flip_vertically_on_load(1);
    unsigned char *data = stbi_load(filename, &state.imageWidth, &state.imageHeight, &channels, 4);
    if (!data) {
        fprintf(stderr, "Error: Could not load image '%s'\n", filename);
        fprintf(stderr, "Reason: %s\n", stbi_failure_reason());
        return 0;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    // float maxAnisotropy = 0.0f;
    // glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
    // glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisotropy);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state.imageWidth, state.imageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
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

    state.shaderIndex = 0;
    state.shaders[0] = load_default_shader();
    state.shaders[1] = load_wave_shader();

    state.VAO = init_vao();
    glBindVertexArray(state.VAO);

    glUseProgram(state.shaders[state.shaderIndex]);
    projLoc = glGetUniformLocation(state.shaders[state.shaderIndex], "uProjection");
    timeLoc = glGetUniformLocation(state.shaders[state.shaderIndex], "seconds");
    glBindTexture(GL_TEXTURE_2D, state.texture);
    // glfwWaitEventsTimeout(1);

    while (!glfwWindowShouldClose(state.window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(state.shaders[state.shaderIndex]);

        if (state.isDirty) {
            state.isDirty = 0;
            update_mouse_dragging();
            update_projection();
            glUniformMatrix4fv(projLoc, 1, GL_FALSE, state.projection);
        }
        if (state.shaderIndex && timeLoc != -1)
            glUniform1f(timeLoc, (float)glfwGetTime());

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glfwSwapBuffers(state.window);

        if (state.shaderIndex)
            glfwPollEvents();
        else
            glfwWaitEvents();
    }

    glfwTerminate();
    return 0;
}
