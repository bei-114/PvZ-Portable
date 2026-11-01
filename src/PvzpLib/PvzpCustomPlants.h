// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "../ConstEnums.h"
class Board;
class Plant;
namespace Sexy { class Graphics; }

// Data-driven custom plants: the engine owns four generic slots; mod.json
// supplies cost/health/tint and an event list. No per-plant C++ is required.
namespace PvzpCustomPlants
{
bool IsCustom(SeedType theSeedType);
int HealthForSeed(SeedType theSeedType);
int CostForSeed(SeedType theSeedType, int theFallback);
void ApplyVisuals(Plant* thePlant);      // called after the body reanimation exists
void Update(Board* theBoard);            // damage-step events
void Reset(Board* theBoard);
void DrawBox(Sexy::Graphics* g, Plant* thePlant);
}
