#include <stdio.h>
#include <stb/stb_image.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

GLFWwindow* window;
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
} state;

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
    int fbwidth, fbheight;
    glfwGetFramebufferSize(window, &fbwidth, &fbheight);
    glViewport(0, 0, fbwidth, fbheight);
}

static int init_glfw() {
    glfwSetErrorCallback(error_callback);
    if (!glfwInit())
        return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(state.windowWidth, state.windowHeight, "Image Viewer", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return 0;
    }
    // glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetWindowSizeCallback(window, window_size_callback);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    // glEnable(GL_FRAMEBUFFER_SRGB);
    // glEnable(GL_BLEND);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    return 1;
}

static unsigned int init_shaders() {
    unsigned int vertexShader, fragmentShader, shaderProgram;
    const char* vertexShaderSource = "#version 330 core\n"
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
    const char* fragmentShaderSource = "#version 330 core\n"
        "out vec4 FragColor;\n"
        "in vec2 TexCoord;\n"
        "uniform sampler2D ourTexture;\n"
        "void main() {\n"
        "   FragColor = texture(ourTexture, TexCoord);\n"
        "}\n\0";

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        printf("ERROR: Shader Compilation Failed\n%s\n", infoLog);
        return 0;
    }
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        printf("ERROR: Shader Compilation Failed\n%s\n", infoLog);
        return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
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

    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state.imageWidth, state.imageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    // glGenerateMipmap(GL_TEXTURE_2D);
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

    unsigned int texture = init_image_texture(filename);
    if (!texture) {
        return 3;
    }
    unsigned int shaderProgram = init_shaders();
    if (!shaderProgram)
        return 4;
    unsigned int VAO = init_vao();
    int projLoc = glGetUniformLocation(shaderProgram, "uProjection");

    while (!glfwWindowShouldClose(window)) {
        if (state.isDirty) {
            state.isDirty = 0;
            update_mouse_dragging();
            update_projection();
            glColor4f(1,1,1,1);
            glClear(GL_COLOR_BUFFER_BIT);
            glBindTexture(GL_TEXTURE_2D, texture);
            glUseProgram(shaderProgram);
            glBindVertexArray(VAO);
            glUniformMatrix4fv(projLoc, 1, GL_FALSE, state.projection);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glfwSwapBuffers(window);
        }

        // glfwPollEvents();
        glfwWaitEvents();
    }

    glfwTerminate();
    return 0;
}