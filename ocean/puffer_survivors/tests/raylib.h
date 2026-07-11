#pragma once
#include <stdbool.h>
#include <stdarg.h>

typedef struct { float x, y; } Vector2;
typedef struct { float x, y, width, height; } Rectangle;
typedef struct { unsigned char r, g, b, a; } Color;
typedef struct { void* data; int width, height; } Image;
typedef struct { unsigned int id; int width, height; } Texture2D;

#define PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 7
#define KEY_ESCAPE 256
#define KEY_H 72
#define KEY_Q 81
#define WHITE ((Color){255,255,255,255})
#define RAYWHITE ((Color){245,245,245,255})
#define GOLD ((Color){255,203,0,255})
#define ORANGE ((Color){255,161,0,255})
#define PINK ((Color){255,109,194,255})
#define RED ((Color){230,41,55,255})
#define SKYBLUE ((Color){102,191,255,255})
#define GRAY ((Color){130,130,130,255})

void ImageFormat(Image*, int);
int GetScreenWidth(void);
int GetScreenHeight(void);
void DrawTexturePro(Texture2D, Rectangle, Rectangle, Vector2, float, Color);
void DrawCircleV(Vector2, float, Color);
void DrawRectangle(int,int,int,int,Color);
void DrawRectangleLines(int,int,int,int,Color);
void DrawRectangleLinesEx(Rectangle,float,Color);
void ClearBackground(Color);
void DrawLineEx(Vector2,Vector2,float,Color);
void DrawCircleGradient(int,int,float,Color,Color);
void DrawCircleLines(int,int,float,Color);
void DrawRing(Vector2,float,float,float,float,int,Color);
void DrawText(const char*,int,int,int,Color);
int MeasureText(const char*,int);
const char* TextFormat(const char*, ...);
bool IsWindowReady(void);
void InitWindow(int,int,const char*);
void SetTargetFPS(int);
bool FileExists(const char*);
Image LoadImage(const char*);
Texture2D LoadTextureFromImage(Image);
void UnloadImage(Image);
bool IsKeyDown(int);
bool IsKeyPressed(int);
void BeginDrawing(void);
void EndDrawing(void);
void UnloadTexture(Texture2D);
void CloseWindow(void);
double GetTime(void);
