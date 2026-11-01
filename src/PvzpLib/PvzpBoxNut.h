// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "../ConstEnums.h"
class Board;
class Plant;
namespace Sexy { class Graphics; }

// Mystery box wall-nuts: every 1000 damage they gamble on an event.
namespace PvzpBoxNut
{
bool IsBoxNut(SeedType theSeedType);
bool IsReverse(SeedType theSeedType);
void Reset(Board* theBoard);
void Update(Board* theBoard);            // call once per un-paused playing tick
void DrawBox(Sexy::Graphics* g, Plant* thePlant); // cosmetic present-box overlay
}
