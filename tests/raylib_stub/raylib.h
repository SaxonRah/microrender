#ifndef RAYLIB_H
#define RAYLIB_H
#include <stddef.h>
typedef struct Color { unsigned char r,g,b,a; } Color;
typedef struct Image { void *data; int width,height,mipmaps,format; } Image;
typedef struct Texture2D { unsigned int id; int width,height,mipmaps,format; } Texture2D;
typedef struct Rectangle { float x,y,width,height; } Rectangle;
typedef struct Vector2 { float x,y; } Vector2;
#define PIXELFORMAT_UNCOMPRESSED_R5G6B5 3
#define TEXTURE_FILTER_POINT 0
#define KEY_LEFT 1
#define KEY_A 2
#define KEY_RIGHT 3
#define KEY_D 4
#define KEY_UP 5
#define KEY_W 6
#define KEY_DOWN 7
#define KEY_S 8
#define KEY_ENTER 9
#define KEY_SPACE 10
#define KEY_P 11
#define KEY_F1 12
#define KEY_GRAVE 13
#define KEY_ESCAPE 14
extern const Color BLACK;
extern const Color WHITE;
void InitWindow(int width,int height,const char *title);
void SetTargetFPS(int fps);
Texture2D LoadTextureFromImage(Image image);
void SetTextureFilter(Texture2D texture,int filter);
int WindowShouldClose(void);
int IsKeyDown(int key);
int IsKeyPressed(int key);
void UpdateTextureRec(Texture2D texture, Rectangle rec, const void *pixels);
void UpdateTexture(Texture2D texture, const void *pixels);
double GetTime(void);
float GetFrameTime(void);
void BeginDrawing(void);
void ClearBackground(Color color);
void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest, Vector2 origin, float rotation, Color tint);
void EndDrawing(void);
void UnloadTexture(Texture2D texture);
void CloseWindow(void);
#endif
