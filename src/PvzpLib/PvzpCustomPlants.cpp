// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpCustomPlants.h"
#include "PvzpMod.h"
#include "../Lawn/Board.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Zombie.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "PvzpFoley.h"
#include "../PvzpLib/Reanimator.h"
#include <map>
#include <vector>

namespace
{
	struct Seen { int bucket = 0; };
	std::map<Board*, std::map<uint32_t, Seen>> gSeen;
	const PvzpMod::CustomPlant* Config(SeedType t)
	{
		if (t < SEED_CUSTOM_0 || t > SEED_CUSTOM_3) return nullptr;
		int slot = int(t) - int(SEED_CUSTOM_0);
		for (const auto& c : PvzpMod::CustomPlants()) if (c.slot == slot) return &c;
		return nullptr;
	}
	Color TintColor(const std::string& tint)
	{
		if (tint == "blue") return Color(120, 170, 255);
		if (tint == "red") return Color(255, 120, 110);
		if (tint == "green") return Color(130, 235, 130);
		if (tint == "purple") return Color(190, 140, 255);
		return Color(255, 236, 170); // gold / default
	}
	bool FreeSpot(Board* b, int& col, int& row)
	{
		std::vector<std::pair<int,int>> spots;
		for (int c = 0; c < MAX_GRID_SIZE_X; ++c)
			for (int r = 0; r < (b->StageHas6Rows() ? 6 : 5); ++r)
				if (b->CanPlantAt(c, r, SEED_PEASHOOTER) == PlantingReason::PLANTING_OK &&
					!b->GetTopPlantAt(c, r, PlantPriority::TOPPLANT_ANY))
					spots.push_back({c, r});
		if (spots.empty()) return false;
		auto [aCol, aRow] = spots[Rand(static_cast<int>(spots.size()))];
		col = aCol; row = aRow;
		return true;
	}
	void RunEvent(Board* b, Plant* p, const std::string& e)
	{
		if (e.rfind("sun:", 0) == 0) { b->AddSunMoney(std::max(0, std::stoi(e.substr(4)))); return; }
		if (e.rfind("heal:", 0) == 0) { p->mPlantHealth = std::min(p->mPlantMaxHealth, p->mPlantHealth + std::max(0, std::stoi(e.substr(5)))); return; }
		if (e == "chill") { for (Zombie* z : b->mZombies) if (!z->mDead && !z->mMindControlled) z->ApplyChill(false); return; }
		if (e == "plant:random")
		{
			int c = 0, r = 0;
			if (FreeSpot(b, c, r))
			{
				static const SeedType aPool[] = { SEED_PEASHOOTER, SEED_SUNFLOWER, SEED_SNOWPEA, SEED_REPEATER, SEED_WALLNUT,
					SEED_CHERRYBOMB, SEED_POTATOMINE, SEED_PUFFSHROOM, SEED_GATLINGPEA, SEED_TWINSUNFLOWER };
				b->AddPlant(c, r, aPool[Rand(static_cast<int>(std::size(aPool)))]);
			}
			return;
		}
		if (e.rfind("zombie:", 0) == 0)
		{
			const std::string t = e.substr(7);
			ZombieType z = ZOMBIE_NORMAL;
			if (t == "GARGANTUAR") z = ZOMBIE_GARGANTUAR;
			else if (t == "BUNGEE") z = ZOMBIE_BUNGEE;
			else if (t == "DIGGER") z = ZOMBIE_DIGGER;
			else if (t == "BALLOON") z = ZOMBIE_BALLOON;
			else if (t == "CONEHEAD") z = ZOMBIE_TRAFFIC_CONE;
			else if (t == "BUCKETHEAD") z = ZOMBIE_PAIL;
			b->AddZombieInRow(z, Rand((b->StageHas6Rows() ? 6 : 5)), Zombie::ZOMBIE_WAVE_DEBUG);
			return;
		}
		if (e == "destroy:plant")
		{
			std::vector<Plant*> victims;
			for (Plant* q : b->mPlants) if (!q->mDead && q != p) victims.push_back(q);
			if (!victims.empty()) victims[Rand(static_cast<int>(victims.size()))]->Die();
			return;
		}
	}
}

namespace PvzpCustomPlants
{
bool IsCustom(SeedType t) { return t >= SEED_CUSTOM_0 && t <= SEED_CUSTOM_3; }
int HealthForSeed(SeedType t) { const auto* c = Config(t); return c ? c->health : 6000; }
int CostForSeed(SeedType t, int fallback) { const auto* c = Config(t); return c ? c->cost : fallback; }
void Reset(Board* b) { if (b) gSeen.erase(b); }

void ApplyVisuals(Plant* p)
{
	const auto* c = Config(p->mSeedType);
	if (!c) return;
	Reanimation* body = p->mApp->ReanimationTryToGet(p->mBodyReanimID);
	if (body) body->mColorOverride = TintColor(c->tint);
}

void Update(Board* b)
{
	if (!b || !b->mApp || b->mApp->mGameScene != SCENE_PLAYING || b->mPaused) return;
	auto& seen = gSeen[b];
	std::map<uint32_t, Seen> keep;
	for (Plant* p : b->mPlants)
	{
		if (p->mDead || !IsCustom(p->mSeedType)) continue;
		const auto* c = Config(p->mSeedType);
		if (!c || c->step <= 0) continue;
		uint32_t id = b->mPlants.DataArrayGetID(p);
		int bucket = (p->mPlantMaxHealth - std::max(0, p->mPlantHealth)) / c->step;
		int last = seen.count(id) ? seen[id].bucket : 0;
		if (bucket > last)
		{
			for (int i = last; i < bucket; ++i)
			{
				bool bad = !c->badEvents.empty() && (Rand(100) < c->badChance);
				const auto& list = bad ? c->badEvents : c->events;
				if (!list.empty()) RunEvent(b, p, list[Rand(static_cast<int>(list.size()))]);
			}
			seen[id].bucket = bucket;
		}
		keep[id] = seen[id];
	}
	seen.swap(keep);
	if (seen.empty()) gSeen.erase(b);
}

void DrawBox(Sexy::Graphics* g, Plant* p)
{
	const auto* c = Config(p->mSeedType);
	if (!c || p->mDead) return;
	if (c->tint != "gold" && c->tint != "blue") return; // present-box look for the two box nuts
	// Plant::Draw runs in local plant space (GameObject translated by mX/mY).
	g->SetColorizeImages(true);
	g->SetColor(TintColor(c->tint));
	g->SetScale(0.92f, 0.92f, 0.0f, 0.0f);
	g->DrawImage(Sexy::IMAGE_PRESENT, -2, -8);
	g->SetScale(1.0f, 1.0f, 0.0f, 0.0f);
	g->SetColorizeImages(false);
}
}
