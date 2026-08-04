#include "raylib.h"
const Color BLACK={0,0,0,255};
const Color WHITE={255,255,255,255};
static double now_value;
void InitWindow(int w,int h,const char*t){(void)w;(void)h;(void)t;}
void SetTargetFPS(int f){(void)f;}
Texture2D LoadTextureFromImage(Image i){Texture2D t={1,(int)i.width,(int)i.height,1,i.format};return t;}
void SetTextureFilter(Texture2D t,int f){(void)t;(void)f;}
int WindowShouldClose(void){return 0;}
int IsKeyDown(int k){(void)k;return 0;}
int IsKeyPressed(int k){(void)k;return 0;}
void UpdateTextureRec(Texture2D t,Rectangle r,const void*p){(void)t;(void)r;(void)p;}
void UpdateTexture(Texture2D t,const void*p){(void)t;(void)p;}
double GetTime(void){now_value+=0.016;return now_value;}
float GetFrameTime(void){return 0.016f;}
void BeginDrawing(void){}
void ClearBackground(Color c){(void)c;}
void DrawTexturePro(Texture2D t,Rectangle s,Rectangle d,Vector2 o,float r,Color c){(void)t;(void)s;(void)d;(void)o;(void)r;(void)c;}
void EndDrawing(void){}
void UnloadTexture(Texture2D t){(void)t;}
void CloseWindow(void){}
