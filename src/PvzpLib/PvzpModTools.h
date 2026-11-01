#pragma once

#include "misc/Rect.h"

class Board;
class Plant;
namespace Sexy { class Graphics; }

// Gameplay-only tools. All transient state lives outside the portable Board
// layout; call Reset when constructing AND disposing a Board.
namespace PvzpModTools
{
void Reset(Board* board);
void Cancel(Board* board);
bool MouseDown(Board* board, int x, int y);
bool MouseDrag(Board* board, int x, int y);
bool MouseUp(Board* board, int x, int y);
void Draw(Board* board, Sexy::Graphics* graphics);
Sexy::Rect GetGloveRect(Board* board);
Sexy::Rect GetFoodRect(Board* board);
Sexy::Rect GetDebugRect(Board* board);

// Uses exactly the same validation/relocation path as a glove drop. Does not
// allocate, destroy, wake, recharge or heal a plant. One plant moves at a time;
// an occupied support plant cannot be moved out from under its occupants.
bool MovePlant(Board* board, Plant* plant, int col, int row);
bool IsActive(Board* board);
}
