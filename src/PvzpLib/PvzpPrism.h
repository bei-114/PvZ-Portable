// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>
class Board;
class Plant;
class Projectile;
class Zombie;
namespace Sexy { class Graphics; }
// A projectile lineage may split only once, even through stacked fields.
namespace PvzpPrism {
void Reset(Board* board);
void Forget(Projectile* projectile);
void UpdateProjectile(Projectile* projectile);
bool IsRefracted(const Projectile* projectile);
bool CanHit(const Projectile* projectile, const Zombie* zombie);
void BeforeImpact(Projectile* projectile, Zombie* zombie);
bool KeepAfterImpact(Projectile* projectile, Zombie* zombie);
unsigned int DamageFlagsFor(const Projectile* projectile);
void DrawField(Sexy::Graphics* graphics, Plant* plant, bool empowered);
std::vector<uint8_t> Save(Board* board);
bool Load(Board* board, const std::vector<uint8_t>& data);
}
