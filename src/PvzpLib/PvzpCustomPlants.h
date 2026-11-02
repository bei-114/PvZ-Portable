// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "../ConstEnums.h"
#include "graphics/Color.h"
#include <cstdint>
#include <vector>
class Board;
class Plant;
namespace Sexy { class Graphics; }

// Data-driven custom plants: the engine owns four generic slots; mod.json
// supplies cost/health/tint and an event list. No per-plant C++ is required.
namespace PvzpCustomPlants
{
bool IsCustom(SeedType theSeedType);
bool IsBrain(SeedType theSeedType);
bool IsSlotUsed(SeedType theSeedType);
void BrainClick(Board* theBoard, Plant* thePlant);
void BrainUltimate(Board* theBoard, Plant* thePlant);
int HealthForSeed(SeedType theSeedType);
int CostForSeed(SeedType theSeedType, int theFallback);
void ApplyVisuals(Plant* thePlant);      // called after the body reanimation exists
void Update(Board* theBoard);            // damage-step events
void Reset(Board* theBoard);
void DrawBox(Sexy::Graphics* g, Plant* thePlant);
void DrawBrain(Sexy::Graphics* g, Plant* thePlant);
Sexy::Color ColorForSeed(SeedType theSeedType);
void Initialize(Plant* thePlant);
void ObserveDamage(Board* theBoard, Plant* thePlant);
bool ApplyFood(Board* theBoard, Plant* thePlant);
bool DrawPacketIcon(Sexy::Graphics* g, SeedType theSeedType, float x, float y);
std::vector<uint8_t> Save(Board* theBoard);
bool Load(Board* theBoard, const std::vector<uint8_t>& data);
}
