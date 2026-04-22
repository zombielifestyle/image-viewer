#include <stdio.h>
#include <math.h>
#include "raylib.h"
#include "raymath.h"

typedef enum { MODE_FIT, MODE_ZOOM } ViewMode;

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_image>\n", argv[0]);
        return 1;
    }
    const char* filename = argv[1];

    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "image viewer");
    Image image = LoadImage(filename);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);

    Shader shader = LoadShader(0, "shaders/wave.fs");
    int secondsLoc = GetShaderLocation(shader, "seconds");
    int freqXLoc = GetShaderLocation(shader, "freqX");
    int freqYLoc = GetShaderLocation(shader, "freqY");
    int ampXLoc = GetShaderLocation(shader, "ampX");
    int ampYLoc = GetShaderLocation(shader, "ampY");
    int speedXLoc = GetShaderLocation(shader, "speedX");
    int speedYLoc = GetShaderLocation(shader, "speedY");

    float freqX = 25.0f;
    float freqY = 25.0f;
    float ampX = 5.0f;
    float ampY = 5.0f;
    float speedX = 8.0f;
    float speedY = 8.0f;

    float screenSize[2] = { (float)GetScreenWidth(), (float)GetScreenHeight() };
    SetShaderValue(shader, GetShaderLocation(shader, "size"), &screenSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, freqXLoc, &freqX, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, freqYLoc, &freqY, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, ampXLoc, &ampX, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, ampYLoc, &ampY, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, speedXLoc, &speedX, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, speedYLoc, &speedY, SHADER_UNIFORM_FLOAT);

    float seconds = 0.0f;

    SetTargetFPS(60);
    EnableEventWaiting();

    Camera2D camera = { 0 };
    camera.offset = (Vector2){ screenWidth / 2.0f, screenHeight / 2.0f };
    camera.zoom = 1.0f;
    ViewMode currentMode = MODE_FIT;
    int shaderEnabled = 0;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_S)) {
            if (shaderEnabled) {
                shaderEnabled = 0;
                EnableEventWaiting();
            } else {
                shaderEnabled = 1;
                DisableEventWaiting();
            }
        }
        if (IsKeyPressed(KEY_SPACE)) {
            currentMode = (currentMode == MODE_FIT) ? MODE_ZOOM : MODE_FIT;
            camera.offset = (Vector2){ (float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f};
            if (currentMode == MODE_FIT)
                camera.target = (Vector2){ texture.width / 2.0f, texture.height / 2.0f };
        }
        if (currentMode == MODE_FIT) {
            float scale = fminf((float)GetScreenWidth() / texture.width, (float)GetScreenHeight() / texture.height);
            camera.zoom = scale;
            camera.target = (Vector2){ texture.width / 2.0f, texture.height / 2.0f };
        } else {
            camera.zoom = 1.0f;
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Vector2 delta = GetMouseDelta();
                delta = Vector2Scale(delta, -1.0f / camera.zoom);
                camera.target = Vector2Add(camera.target, delta);
            }
        }
        seconds += GetFrameTime();
        if (shaderEnabled) {
            SetShaderValue(shader, secondsLoc, &seconds, SHADER_UNIFORM_FLOAT);
        }

        BeginDrawing();
            ClearBackground(BLACK);
            if (shaderEnabled) BeginShaderMode(shader);
                BeginMode2D(camera);
                    DrawTexture(texture, 0, 0, WHITE);
                EndMode2D();
            if (shaderEnabled) EndShaderMode();
        EndDrawing();
    }

    UnloadTexture(texture);
    CloseWindow();
    return 0;
}
