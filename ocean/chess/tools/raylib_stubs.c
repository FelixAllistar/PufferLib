// No-op stand-ins for the raylib calls in chess.h's renderer (2026-11-10), so headless tools such as bc_encode.c can
// link chess.h without a display stack. Never called: these tools only use the rules and observation code.
#define STUB(name) void name(void) {}
STUB(BeginDrawing) STUB(CheckCollisionPointRec) STUB(ClearBackground) STUB(CloseWindow) STUB(DrawRectangle)
STUB(DrawRectangleLines) STUB(DrawRectangleLinesEx) STUB(DrawRectangleRec) STUB(DrawText) STUB(DrawTexturePro)
STUB(EndDrawing) STUB(Fade) STUB(GetImageAlphaBorder) STUB(GetMousePosition) STUB(ImageFromImage) STUB(InitWindow)
STUB(IsKeyDown) STUB(IsKeyPressed) STUB(IsMouseButtonPressed) STUB(IsWindowReady) STUB(LoadImage)
STUB(LoadTextureFromImage) STUB(MeasureText) STUB(SetConfigFlags) STUB(SetTargetFPS) STUB(SetTextureFilter)
STUB(UnloadImage) STUB(UnloadTexture) STUB(WindowShouldClose)
