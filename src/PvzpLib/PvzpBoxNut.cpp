// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpBoxNut.h"
#include "../Lawn/Board.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/System/PlayerInfo.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "PvzpFoley.h"
#include <map>
#include <vector>

namespace
{
	struct Shown { int bucket = 0; };
	std::map<Board*, std::map<uint32_t, Shown>> gSeen;

	Plant* RandomEmptyPlantSpot(Board* b, int& col, int& row)
	{
		std::vector<std::pair<int,int>> spots;
		for (int c = 0; c < MAX_GRID_SIZE_X; ++c)
			for (int r = 0; r < (b->StageHas6Rows() ? 6 : 5); ++r)
				if (b->CanPlantAt(c, r, SEED_PEASHOOTER) == PlantingReason::PLANTING_OK && !b->GetTopPlantAt(c, r, PlantPriority::TOPPLANT_ANY))
					spots.push_back({c, r});
		if (spots.empty())
			return nullptr;
		auto [aCol, aRow] = spots[Rand(static_cast<int>(spots.size()))];
		col = aCol;
		row = aRow;
		return reinterpret_cast<Plant*>(1); // sentinel: spot chosen
	}

	void GoodEvent(Board* b, Plant* p)
	{
		switch (Rand(5))
		{
		case 0:
			b->AddSunMoney(150);
			break;
		case 1:
		{
			int c = 0, r = 0;
			if (RandomEmptyPlantSpot(b, c, r))
			{
				static const SeedType aPool[] = { SEED_PEASHOOTER, SEED_SUNFLOWER, SEED_SNOWPEA, SEED_REPEATER,
					SEED_WALLNUT, SEED_CHERRYBOMB, SEED_POTATOMINE, SEED_PUFFSHROOM, SEED_GATLINGPEA, SEED_TWINSUNFLOWER };
				b->AddPlant(c, r, aPool[Rand(static_cast<int>(std::size(aPool)))]);
			}
			break;
		}
		case 2:
			p->mPlantHealth = std::min(p->mPlantMaxHealth, p->mPlantHealth + 1200);
			break;
		case 3:
			for (Zombie* z : b->mZombies)
				if (!z->mDead && !z->mMindControlled)
					z->ApplyChill(false);
			break;
		default:
			for (int i = 0; i < 3; ++i)
				b->AddSunMoney(25);
			break;
		}
	}

	void BadEvent(Board* b, Plant* p)
	{
		switch (Rand(5))
		{
		case 0:
			b->AddZombieInRow(ZOMBIE_GARGANTUAR, Rand((b->StageHas6Rows() ? 6 : 5)), Zombie::ZOMBIE_WAVE_DEBUG);
			break;
		case 1:
			for (int i = 0; i < 2; ++i)
				b->AddZombieInRow(ZOMBIE_BUNGEE, Rand((b->StageHas6Rows() ? 6 : 5)), Zombie::ZOMBIE_WAVE_DEBUG);
			break;
		case 2:
			for (int i = 0; i < 2; ++i)
				b->AddZombieInRow(ZOMBIE_DIGGER, Rand((b->StageHas6Rows() ? 6 : 5)), Zombie::ZOMBIE_WAVE_DEBUG);
			break;
		case 3:
		{
			std::vector<Plant*> victims;
			for (Plant* q : b->mPlants)
				if (!q->mDead && q != p)
					victims.push_back(q);
			if (!victims.empty())
				victims[Rand(static_cast<int>(victims.size()))]->Die();
			break;
		}
		default:
			b->AddZombieInRow(ZOMBIE_BALLOON, Rand((b->StageHas6Rows() ? 6 : 5)), Zombie::ZOMBIE_WAVE_DEBUG);
			break;
		}
	}
}

namespace PvzpBoxNut
{
bool IsBoxNut(SeedType t) { return t == SEED_MYSTERY_NUT || t == SEED_REVERSE_NUT; }
bool IsReverse(SeedType t) { return t == SEED_REVERSE_NUT; }

void Reset(Board* b) { if (b) gSeen.erase(b); }

void Update(Board* b)
{
	if (!b || !b->mApp || b->mApp->mGameScene != SCENE_PLAYING || b->mPaused)
		return;
	auto& seen = gSeen[b];
	std::map<uint32_t, Shown> keep;
	for (Plant* p : b->mPlants)
	{
		if (p->mDead || !IsBoxNut(p->mSeedType))
			continue;
		uint32_t id = b->mPlants.DataArrayGetID(p);
		int bucket = (p->mPlantMaxHealth - std::max(0, p->mPlantHealth)) / 1000;
		int last = seen.count(id) ? seen[id].bucket : 0;
		if (bucket > last)
		{
			seen[id].bucket = bucket;
			for (int i = last; i < bucket; ++i)
			{
				bool bad = IsReverse(p->mSeedType) && Rand(2) == 0;
				if (bad) BadEvent(b, p); else GoodEvent(b, p);
				b->mApp->PlaySample(bad ? Sexy::SOUND_BUZZER : Sexy::SOUND_TAP);
			}
		}
		keep[id] = seen[id];
	}
	seen.swap(keep);
	if (seen.empty())
		gSeen.erase(b);
}

void DrawBox(Sexy::Graphics* g, Plant* p)
{
	if (!IsBoxNut(p->mSeedType) || p->mDead)
		return;
	int y = p->mY + 10 - static_cast<int>(PlantDrawHeightOffset(p->mBoard, p, p->mSeedType, p->mPlantCol, p->mRow));
	g->SetColorizeImages(true);
	g->SetColor(IsReverse(p->mSeedType) ? Color(120, 170, 255) : Color(255, 245, 220));
	g->DrawImage(Sexy::IMAGE_PRESENT, p->mX + 12, y + 12);
	g->SetColorizeImages(false);
}
}
