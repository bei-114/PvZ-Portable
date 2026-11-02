// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
class Board;
class Zombie;
class Plant;
class Projectile;
namespace Sexy { class Graphics; }
// Stable string identities + per-entity sidecars, never inserted ZombieType IDs.
namespace PvzpCustomZombies {
Zombie* Spawn(Board* board, std::string_view id, int row=-1);
bool Exists(std::string_view id);
bool CanSpawn(Board* board, std::string_view id, int row=-1);
std::string Id(const Zombie* zombie);
std::string Name(const Zombie* zombie);
int Int(const Zombie* zombie, std::string_view field, int fallback);
void Update(Board* board);
// Apply a bounded control effect without HitIceTrap's implicit 20 damage.
void FreezeZombie(Zombie* zombie, int ticks);
void PrepareVisuals(Zombie* zombie);
void Draw(Sexy::Graphics* graphics, Zombie* zombie);
bool DrawProjectile(Sexy::Graphics* graphics, Projectile* projectile);
bool HitPlant(Projectile* projectile, Plant* plant);
bool PlantFrozen(const Plant* plant);
void Forget(Projectile* projectile);
void Reset(Board* board);
std::vector<uint8_t> Save(Board* board);
bool Load(Board* board, const std::vector<uint8_t>& data);
}
