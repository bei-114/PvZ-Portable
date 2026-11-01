// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "../ConstEnums.h"
#include "../Lawn/Plant.h"

class Board;
class Projectile;
class Zombie;
namespace Sexy { class Graphics; }

// All state is transient, keyed by Board and generation-bearing PlantID.
// Update must only be called by a playing, unpaused board. Reset before
// load, board destruction and level replacement; neither Plant nor Projectile
// acquires serialized fields.
namespace PvzpPlantFood
{
struct SkillInfo
{
    SeedType seed;
    const char* name;
    const char* description;
    int cost;
    int recharge;
    int launch;
    int health;
    int duration;
};
std::size_t SkillCount();
const SkillInfo* SkillAt(std::size_t index);
const SkillInfo* Query(SeedType seed);
const char* SkillName(SeedType seed);
const char* SkillDescription(SeedType seed);
// Defaults are applied BEFORE explicit PvzpMod overrides.
void ApplyDefaultBalance(PlantDefinition& definition);
int DefaultHealth(SeedType seed, int originalHealth);

bool Apply(Board* board, Plant* plant);
void Update(Board* board);
void Reset(Board* board);
void Draw(Board* board, Sexy::Graphics* graphics);
bool IsBoosted(Board* board, uint32_t plantID);
std::size_t ActiveCount(Board* board);
int RemainingTicks(Board* board, uint32_t plantID);
int ShieldHealth(Board* board, uint32_t plantID);
std::vector<uint8_t> Save(Board* board);
bool Load(Board* board, const std::vector<uint8_t>& bytes);
// Called only once, after ALL of the original shot's motion/target setup.
void AfterFire(Plant* plant, Projectile* shot, Zombie* target, PlantWeapon weapon);
}
