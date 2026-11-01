/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 *
 * PvZ-Portable is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PvZ-Portable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PvZ-Portable. If not, see <https://www.gnu.org/licenses/>.
 */

#include "Board.h"
#include "Plant.h"
#include "Zombie.h"
#include "Cutscene.h"
#include "Projectile.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "../GameConstants.h"
#include "../PvzpLib/PvzpFoley.h"
#include "../PvzpLib/PvzpDebug.h"
#include "../PvzpLib/Reanimator.h"
#include "../PvzpLib/Attachment.h"
#include "../SexyAppFramework/misc/SexyVector.h"
#include "Widget/AchievementsScreen.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr float ELECTRIC_CHAIN_RADIUS = 160.0f;
constexpr int ELECTRIC_STUN_TICKS = 30; // 0.3 seconds at the game's 100 Hz update rate.
constexpr int ELECTRIC_BOLT_TICKS = 12;

struct ElectricBolt
{
	std::array<SexyVector2, 7> mPoints;
	int mTicksLeft = ELECTRIC_BOLT_TICKS;
};

struct ElectricProjectileState
{
	Board* mBoard = nullptr;
	bool mElectric = false;
	std::unordered_set<unsigned int> mHitZombieIDs;
	std::vector<ElectricBolt> mBolts;
};

// Keep transient ownership outside the object's binary layout: legacy saves copy sizeof(Projectile).
// DataArray resets/destructs its objects on reuse/load. Destructor cleanup and
// ProjectileInitialize reset prevent recycled addresses from inheriting electric state.
// Keep Projectile's constructor defaulted so value-initialization still zeroes its fields.
std::unordered_map<const Projectile*, ElectricProjectileState> gElectricProjectiles;

ElectricProjectileState* GetElectricState(const Projectile* theProjectile)
{
	auto anEntry = gElectricProjectiles.find(theProjectile);
	return anEntry != gElectricProjectiles.end() && anEntry->second.mBoard == theProjectile->mBoard && anEntry->second.mElectric
		? &anEntry->second : nullptr;
}

bool CanElectricHit(Projectile* theProjectile, Zombie* theZombie, const ElectricProjectileState& theState)
{
	return theZombie && !theZombie->IsDeadOrDying() && !theZombie->mMindControlled &&
		theZombie->EffectedByDamage(static_cast<unsigned int>(theProjectile->mDamageRangeFlags)) &&
		!theState.mHitZombieIDs.contains(static_cast<unsigned int>(theProjectile->mBoard->ZombieGetID(theZombie)));
}

SexyVector2 ElectricTargetPoint(Zombie* theZombie)
{
	Rect aRect = theZombie->GetZombieRect();
	return SexyVector2(aRect.mX + aRect.mWidth * 0.5f, aRect.mY + aRect.mHeight * 0.5f);
}

void AddElectricBolt(ElectricProjectileState& theState, const SexyVector2& theFrom, const SexyVector2& theTo, unsigned int theSeed)
{
	ElectricBolt aBolt;
	SexyVector2 aDelta = theTo - theFrom;
	SexyVector2 aNormal(-aDelta.y, aDelta.x);
	if (aDelta.x != 0.0f || aDelta.y != 0.0f)
		aNormal = aNormal.Normalize();
	for (size_t i = 0; i < aBolt.mPoints.size(); ++i)
	{
		float aFraction = static_cast<float>(i) / (aBolt.mPoints.size() - 1);
		// Stable jagged geometry, without consuming gameplay's random-number stream in Draw.
		float aJitter = i == 0 || i == aBolt.mPoints.size() - 1 ? 0.0f :
			static_cast<float>((theSeed + static_cast<unsigned int>(i) * 13U) % 17U) - 8.0f;
		aBolt.mPoints[i] = theFrom + aDelta * aFraction + aNormal * aJitter;
	}
	theState.mBolts.push_back(aBolt);
}

void TraceElectricImpact(size_t theHitCount, size_t theLifetimeHits)
{
	static const bool aTrace = [] {
		const char* aValue = std::getenv("PVZP_ELECTRIC_TRACE");
		return aValue && *aValue && *aValue != '0';
	}();
	if (!aTrace)
		return;
	static unsigned long long aImpacts = 0;
	static unsigned long long aTargets = 0;
	++aImpacts;
	aTargets += theHitCount;
	if (aImpacts == 1 || aImpacts % 32 == 0)
		std::fprintf(stderr, "[electric] impacts=%llu damaged_targets=%llu last_chain=%zu projectile_unique_hits=%zu\n",
			aImpacts, aTargets, theHitCount, theLifetimeHits);
}
}

constinit const ProjectileDefinition gProjectileDefinition[] = {
	{ .mProjectileType = ProjectileType::PROJECTILE_PEA, .mImageRow = 0, .mDamage = 20 },
	{ .mProjectileType = ProjectileType::PROJECTILE_SNOWPEA, .mImageRow = 0, .mDamage = 20 },
	{ .mProjectileType = ProjectileType::PROJECTILE_CABBAGE, .mImageRow = 0, .mDamage = 40 },
	{ .mProjectileType = ProjectileType::PROJECTILE_MELON, .mImageRow = 0, .mDamage = 80 },
	{ .mProjectileType = ProjectileType::PROJECTILE_PUFF, .mImageRow = 0, .mDamage = 20 },
	{ .mProjectileType = ProjectileType::PROJECTILE_WINTERMELON, .mImageRow = 0, .mDamage = 80 },
	{ .mProjectileType = ProjectileType::PROJECTILE_FIREBALL, .mImageRow = 0, .mDamage = 40 },
	{ .mProjectileType = ProjectileType::PROJECTILE_STAR, .mImageRow = 0, .mDamage = 20 },
	{ .mProjectileType = ProjectileType::PROJECTILE_SPIKE, .mImageRow = 0, .mDamage = 20 },
	{ .mProjectileType = ProjectileType::PROJECTILE_BASKETBALL, .mImageRow = 0, .mDamage = 75 },
	{ .mProjectileType = ProjectileType::PROJECTILE_KERNEL, .mImageRow = 0, .mDamage = 20 },
	{ .mProjectileType = ProjectileType::PROJECTILE_COBBIG, .mImageRow = 0, .mDamage = 300 },
	{ .mProjectileType = ProjectileType::PROJECTILE_BUTTER, .mImageRow = 0, .mDamage = 40 },
	{ .mProjectileType = ProjectileType::PROJECTILE_ZOMBIE_PEA, .mImageRow = 0, .mDamage = 20 }
};

Projectile::~Projectile()
{
	gElectricProjectiles.erase(this);
	AttachmentDie(mAttachmentID);
}

void Projectile::SetElectric(bool theElectric)
{
	if (!theElectric)
	{
		gElectricProjectiles.erase(this);
		return;
	}
	if (IsElectric())
		return;
	ElectricProjectileState& aState = gElectricProjectiles[this];
	aState = ElectricProjectileState{};
	aState.mBoard = mBoard;
	aState.mElectric = true;
	// Electricity is not a cold pea: remove an accidental snow trail/type as well as freeze flags.
	if (mProjectileType == ProjectileType::PROJECTILE_SNOWPEA)
	{
		AttachmentDie(mAttachmentID);
		mProjectileType = ProjectileType::PROJECTILE_PEA;
	}
}

bool Projectile::IsElectric() const
{
	return GetElectricState(this) != nullptr;
}

void Projectile::ProjectileInitialize(int theX, int theY, int theRenderOrder, int theRow, ProjectileType theProjectileType)
{
	SetElectric(false);
	int aGridX = mBoard->PixelToGridXKeepOnBoard(theX, theY);
	mProjectileType = theProjectileType;
	mPosX = theX;
	mPosY = theY;
	mPosZ = 0.0f;
	mVelX = 0.0f;
	mVelY = 0.0f;
	mVelZ = 0.0f;
	mAccZ = 0.0f;
	mShadowY = mBoard->GridToPixelY(aGridX, theRow) + 67.0f;
	mHitTorchwoodGridX = -1;
	mMotionType = ProjectileMotion::MOTION_STRAIGHT;
	mFrame = 0;
	mNumFrames = 1;
	mRow = theRow;
	mCobTargetX = 0.0f;
	mDamageRangeFlags = 0;
	mDead = false;
	mAttachmentID = AttachmentID::ATTACHMENTID_NULL;
	mCobTargetRow = 0;
	mTargetZombieID = ZombieID::ZOMBIEID_NULL;
	mOnHighGround = mBoard->mGridSquareType[aGridX][theRow] == GridSquareType::GRIDSQUARE_HIGH_GROUND;
	if (mBoard->StageHasRoof() && theX < 480)
	{
		mShadowY -= 12.0f;
	}
	mRenderOrder = theRenderOrder;
	mRotation = 0.0f;
	mRotationSpeed = 0.0f;
	mWidth = 40;
	mHeight = 40;
	mProjectileAge = 0;
	mClickBackoffCounter = 0;
	mAnimTicksPerFrame = 0;

	switch (mProjectileType)
	{
	case ProjectileType::PROJECTILE_CABBAGE:
	case ProjectileType::PROJECTILE_BUTTER:
		mRotation = -7 * PI / 25;  // DEG_TO_RAD(-50.4f);
		mRotationSpeed = RandRangeFloat(-0.08f, -0.02f);
		break;
	case ProjectileType::PROJECTILE_MELON:
	case ProjectileType::PROJECTILE_WINTERMELON:
		mRotation = -2 * PI / 5;  // DEG_TO_RAD(-72.0f);
		mRotationSpeed = RandRangeFloat(-0.08f, -0.02f);
		break;
	case ProjectileType::PROJECTILE_KERNEL:
		mRotation = 0.0f;
		mRotationSpeed = RandRangeFloat(-0.2f, -0.08f);
		break;
	case ProjectileType::PROJECTILE_SNOWPEA:
	{
		PvzpParticleSystem* aParticle = mApp->AddPvzpParticle(mPosX + 8.0f, mPosY + 13.0f, 400000, ParticleEffect::PARTICLE_SNOWPEA_TRAIL);
		AttachParticle(mAttachmentID, aParticle, 8.0f, 13.0f);
		break;
	}
	case ProjectileType::PROJECTILE_FIREBALL:
		PVZP_ASSERT(false);
		break;
	case ProjectileType::PROJECTILE_COBBIG:
		mWidth = IMAGE_REANIM_COBCANNON_COB->GetWidth();
		mHeight = IMAGE_REANIM_COBCANNON_COB->GetHeight();
		mRotation = PI / 2;
		break;
	case ProjectileType::PROJECTILE_PUFF:
	{
		PvzpParticleSystem* aParticle = mApp->AddPvzpParticle(mPosX + 13.0f, mPosY + 13.0f, 400000, ParticleEffect::PARTICLE_PUFFSHROOM_TRAIL);
		AttachParticle(mAttachmentID, aParticle, 13.0f, 13.0f);
		break;
	}
	case ProjectileType::PROJECTILE_BASKETBALL:
		mRotation = RandRangeFloat(0.0f, 2 * PI);
		mRotationSpeed = RandRangeFloat(0.05f, 0.1f);
		break;
	case ProjectileType::PROJECTILE_STAR:
		mShadowY += 15.0f;
		mRotationSpeed = RandRangeFloat(0.05f, 0.1f);
		if (Rand(2) == 0)
		{
			mRotationSpeed = -mRotationSpeed;
		}
		break;
	default:
		break;
	}

	mAnimCounter = 0;
	mX = static_cast<int>(mPosX);
	mY = static_cast<int>(mPosY);
}

Plant* Projectile::FindCollisionTargetPlant()
{
	Rect aProjectileRect = GetProjectileRect();

	for (Plant* aPlant : mBoard->mPlants)
	{
		if (aPlant->mDead)
			continue;
		if (aPlant->mRow != mRow)
			continue;

		if (mProjectileType == ProjectileType::PROJECTILE_ZOMBIE_PEA)
		{
			if (aPlant->mSeedType == SeedType::SEED_PUFFSHROOM ||
				aPlant->mSeedType == SeedType::SEED_SUNSHROOM ||
				aPlant->mSeedType == SeedType::SEED_POTATOMINE ||
				aPlant->mSeedType == SeedType::SEED_SPIKEWEED ||
				aPlant->mSeedType == SeedType::SEED_SPIKEROCK ||
				aPlant->mSeedType == SeedType::SEED_LILYPAD)  // zombie peas cannot hit low plants
				continue;
		}

		Rect aPlantRect = aPlant->GetPlantRect();
		if (GetRectOverlap(aProjectileRect, aPlantRect) > 8)
		{
			if (mProjectileType == ProjectileType::PROJECTILE_ZOMBIE_PEA)
			{
				return mBoard->GetTopPlantAt(aPlant->mPlantCol, aPlant->mRow, PlantPriority::TOPPLANT_EATING_ORDER);
			}
			else
			{
				return mBoard->GetTopPlantAt(aPlant->mPlantCol, aPlant->mRow, PlantPriority::TOPPLANT_CATAPULT_ORDER);
			}
		}
	}

	return nullptr;
}

bool Projectile::PeaAboutToHitTorchwood()
{
	if (mMotionType != ProjectileMotion::MOTION_STRAIGHT)
		return false;

	if (mProjectileType != ProjectileType::PROJECTILE_PEA && mProjectileType != ProjectileType::PROJECTILE_SNOWPEA)
		return false;

	for (Plant* aPlant : mBoard->mPlants)
	{
		if (aPlant->mDead)
			continue;
		if (aPlant->mSeedType == SeedType::SEED_TORCHWOOD && aPlant->mRow == mRow && !aPlant->NotOnGround() && mHitTorchwoodGridX != aPlant->mPlantCol)
		{
			Rect aPlantAttackRect = aPlant->GetPlantAttackRect(PlantWeapon::WEAPON_PRIMARY);
			Rect aProjectileRect = GetProjectileRect();
			aProjectileRect.mX += 40;

			if (GetRectOverlap(aPlantAttackRect, aProjectileRect) > 10)
			{
				return true;
			}
		}
	}

	return false;
}

Zombie* Projectile::FindCollisionTarget()
{
	if (!IsElectric() && PeaAboutToHitTorchwood())  // normal peas retain the "torchwood clip" trick
		return nullptr;

	Rect aProjectileRect = GetProjectileRect();
	Zombie* aBestZombie = nullptr;
	int aMinX = 0;
	ElectricProjectileState* anElectricState = GetElectricState(this);

	for (Zombie* aZombie : mBoard->mZombies)
	{
		if (aZombie->mDead)
			continue;
		if (anElectricState && !CanElectricHit(this, aZombie, *anElectricState))
			continue;
		if ((aZombie->mZombieType == ZombieType::ZOMBIE_BOSS || aZombie->mRow == mRow) && aZombie->EffectedByDamage(static_cast<unsigned int>(mDamageRangeFlags)))
		{
			if (aZombie->mZombiePhase == ZombiePhase::PHASE_SNORKEL_WALKING_IN_POOL && mPosZ <= 45.0f)
			{
				continue;
			}

			if (mProjectileType == ProjectileType::PROJECTILE_STAR && mProjectileAge < 25 && mVelX >= 0.0f && aZombie->mZombieType == ZombieType::ZOMBIE_DIGGER)
			{
				continue;
			}

			Rect aZombieRect = aZombie->GetZombieRect();
			if (GetRectOverlap(aProjectileRect, aZombieRect) >= 0)
			{
				if (aBestZombie == nullptr || aZombie->mX < aMinX)
				{
					aBestZombie = aZombie;
					aMinX = aZombie->mX;
				}
			}
		}
	}

	return aBestZombie;
}

void Projectile::CheckForCollision()
{
	if (mMotionType == ProjectileMotion::MOTION_PUFF && mProjectileAge >= 75)
	{
		Die();
		return;
	}

	if (mPosX > WIDE_BOARD_WIDTH || mPosX + mWidth < 0.0f)
	{
		Die();
		return;
	}

	if (IsElectric() && mMotionType == ProjectileMotion::MOTION_STAR && (mPosY < 0.0f || mPosY > BOARD_HEIGHT))
	{
		Die();
		return;
	}

	if (mMotionType == ProjectileMotion::MOTION_HOMING)
	{
		Zombie* aZombie = mBoard->ZombieTryToGet(mTargetZombieID);
		if (aZombie && aZombie->EffectedByDamage(static_cast<unsigned int>(mDamageRangeFlags)))
		{
			Rect aProjectileRect = GetProjectileRect();
			Rect aZombieRect = aZombie->GetZombieRect();
			if (GetRectOverlap(aProjectileRect, aZombieRect) >= 0 && mPosY > aZombieRect.mY&& mPosY < aZombieRect.mY + aZombieRect.mHeight)
			{
				DoImpact(aZombie);
			}
		}
		return;
	}

	if (mProjectileType == ProjectileType::PROJECTILE_STAR && (mPosY > 600.0f || mPosY < 40.0f))
	{
		Die();
		return;
	}

	if ((mProjectileType == ProjectileType::PROJECTILE_PEA || mProjectileType == ProjectileType::PROJECTILE_STAR) && mShadowY - mPosY > 90.0f)
	{
		return;
	}

	if (mMotionType == ProjectileMotion::MOTION_FLOAT_OVER)
	{
		return;
	}

	if (mProjectileType == ProjectileType::PROJECTILE_ZOMBIE_PEA)
	{
		Plant* aPlant = FindCollisionTargetPlant();
		if (aPlant)
		{
			const ProjectileDefinition& aProjectileDef = GetProjectileDef();
			aPlant->mPlantHealth -= aProjectileDef.mDamage;
			aPlant->mEatenFlashCountdown = std::max(aPlant->mEatenFlashCountdown, 25);

			mApp->PlayFoley(FoleyType::FOLEY_SPLAT);
			mApp->AddPvzpParticle(mPosX - 3.0f, mPosY + 17.0f, mRenderOrder + 1, ParticleEffect::PARTICLE_PEA_SPLAT);
			Die();
		}
		return;
	}

	Zombie* aZombie = FindCollisionTarget();
	if (aZombie)
	{
		if (aZombie->mOnHighGround && CantHitHighGround())
		{
			return;
		}

		DoImpact(aZombie);
	}
}

bool Projectile::CantHitHighGround()
{
	if (mMotionType == ProjectileMotion::MOTION_BACKWARDS || mMotionType == ProjectileMotion::MOTION_HOMING)
		return false;

	return (
		mProjectileType == ProjectileType::PROJECTILE_PEA ||
		mProjectileType == ProjectileType::PROJECTILE_SNOWPEA ||
		mProjectileType == ProjectileType::PROJECTILE_STAR ||
		mProjectileType == ProjectileType::PROJECTILE_PUFF ||
		mProjectileType == ProjectileType::PROJECTILE_FIREBALL
		) && !mOnHighGround;
}

void Projectile::CheckForHighGround()
{
	float aShadowDelta = mShadowY - mPosY;

	if (mProjectileType == ProjectileType::PROJECTILE_PEA ||
		mProjectileType == ProjectileType::PROJECTILE_SNOWPEA ||
		mProjectileType == ProjectileType::PROJECTILE_FIREBALL ||
		mProjectileType == ProjectileType::PROJECTILE_SPIKE ||
		mProjectileType == ProjectileType::PROJECTILE_COBBIG)
	{
		if (aShadowDelta < 28.0f)
		{
			DoImpact(nullptr);
			return;
		}
	}

	if (mProjectileType == ProjectileType::PROJECTILE_PUFF && aShadowDelta < 0.0f)
	{
		DoImpact(nullptr);
		return;
	}

	if (mProjectileType == ProjectileType::PROJECTILE_STAR && aShadowDelta < 23.0f)
	{
		DoImpact(nullptr);
		return;
	}

	if (CantHitHighGround())
	{
		int aGridX = mBoard->PixelToGridXKeepOnBoard(mPosX + 30, mPosY);
		if (mBoard->mGridSquareType[aGridX][mRow] == GridSquareType::GRIDSQUARE_HIGH_GROUND)
		{
			DoImpact(nullptr);
		}
	}
}

bool Projectile::IsSplashDamage(Zombie* theZombie)
{
	if (mProjectileType == ProjectileType::PROJECTILE_FIREBALL && theZombie && theZombie->IsFireResistant())
		return false;

	return
		mProjectileType == ProjectileType::PROJECTILE_MELON ||
		mProjectileType == ProjectileType::PROJECTILE_WINTERMELON ||
		mProjectileType == ProjectileType::PROJECTILE_FIREBALL;
}

unsigned int Projectile::GetDamageFlags(Zombie* theZombie)
{
	unsigned int aDamageFlags = 0U;

	if (IsSplashDamage(theZombie))
	{
		SetBit(aDamageFlags, static_cast<int>(DamageFlags::DAMAGE_HITS_SHIELD_AND_BODY), true);
	}
	else if (mMotionType == ProjectileMotion::MOTION_LOBBED || mMotionType == ProjectileMotion::MOTION_BACKWARDS)
	{
		SetBit(aDamageFlags, static_cast<int>(DamageFlags::DAMAGE_BYPASSES_SHIELD), true);
	}
	else if (mMotionType == ProjectileMotion::MOTION_STAR && mVelX < 0.0f)
	{
		SetBit(aDamageFlags, static_cast<int>(DamageFlags::DAMAGE_BYPASSES_SHIELD), true);
	}

	if (!IsElectric() && (mProjectileType == ProjectileType::PROJECTILE_SNOWPEA || mProjectileType == ProjectileType::PROJECTILE_WINTERMELON))
	{
		SetBit(aDamageFlags, static_cast<int>(DamageFlags::DAMAGE_FREEZE), true);
	}

	return aDamageFlags;
}

bool Projectile::IsZombieHitBySplash(Zombie* theZombie)
{
	Rect aProjectileRect = GetProjectileRect();
	if (mProjectileType == ProjectileType::PROJECTILE_FIREBALL)
	{
		aProjectileRect.mWidth = 100;
	}

	int aRowDeviation = theZombie->mRow - mRow;
	Rect aZombieRect = theZombie->GetZombieRect();
	if (theZombie->IsFireResistant() && mProjectileType == ProjectileType::PROJECTILE_FIREBALL)
	{
		return false;
	}

	if (theZombie->mZombieType == ZombieType::ZOMBIE_BOSS)
	{
		aRowDeviation = 0;
	}
	if (mProjectileType == ProjectileType::PROJECTILE_FIREBALL)
	{
		if (aRowDeviation != 0)
		{
			return false;
		}
	}
	else if (aRowDeviation > 1 || aRowDeviation < -1)
	{
		return false;
	}

	return theZombie->EffectedByDamage(static_cast<unsigned int>(mDamageRangeFlags)) && GetRectOverlap(aProjectileRect, aZombieRect) >= 0;
}

void Projectile::DoSplashDamage(Zombie* theZombie)
{
	const ProjectileDefinition& aProjectileDef = GetProjectileDef();

	int aZombiesGetSplashed = 0;
	for (Zombie* aZombie : mBoard->mZombies)
	{
		if (aZombie->mDead)
			continue;
		if (aZombie != theZombie && IsZombieHitBySplash(aZombie))
		{
			aZombiesGetSplashed++;
		}
	}

	int aOriginalDamage = aProjectileDef.mDamage;
	int aSplashDamage = aProjectileDef.mDamage / 3;
	int aMaxSplashDamageAmount = aOriginalDamage * 7;
	if (mProjectileType == ProjectileType::PROJECTILE_FIREBALL)
	{
		aMaxSplashDamageAmount = aOriginalDamage;
	}
	int aSplashDamageAmount = aSplashDamage * aZombiesGetSplashed;
	if (aSplashDamageAmount > aMaxSplashDamageAmount)
	{
		//aSplashDamage *= aMaxSplashDamageAmount / aSplashDamage;
		aSplashDamage = aOriginalDamage * aMaxSplashDamageAmount / (aSplashDamageAmount * 3);
		aSplashDamage = std::max(aSplashDamage, 1);
	}

	for (Zombie* aZombie : mBoard->mZombies)
	{
		if (aZombie->mDead)
			continue;
		if (IsZombieHitBySplash(aZombie))
		{
			unsigned int aDamageFlags = GetDamageFlags(aZombie);
			if (aZombie == theZombie)
			{
				aZombie->TakeDamage(aOriginalDamage, aDamageFlags);
			}
			else
			{
				aZombie->TakeDamage(aSplashDamage, aDamageFlags);
			}
		}
	}
}

void Projectile::UpdateLobMotion()
{
	if (mProjectileType == ProjectileType::PROJECTILE_COBBIG && mPosZ < -700.0f)
	{
		mVelZ = 8.0f;
		mRow = mCobTargetRow;
		mPosX = mCobTargetX;
		int aCobTargetCol = mBoard->PixelToGridXKeepOnBoard(mCobTargetX, 0);
		mPosY = mBoard->GridToPixelY(aCobTargetCol, mCobTargetRow);
		mShadowY = mPosY + 67.0f;
		mRotation = -PI / 2;
	}

	mVelZ += mAccZ;
	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_HIGH_GRAVITY)
	{
		mVelZ += mAccZ;
	}
	mPosX += mVelX;
	mPosY += mVelY;
	mPosZ += mVelZ;

	bool isRising = mVelZ < 0.0f;
	if (isRising && (mProjectileType == ProjectileType::PROJECTILE_BASKETBALL || mProjectileType == ProjectileType::PROJECTILE_COBBIG))
	{
		return;
	}
	if (mProjectileAge > 20)
	{
		if (isRising)
		{
			return;
		}

		float aMinCollisionZ = 0.0f;
		if (mProjectileType == ProjectileType::PROJECTILE_BUTTER)
		{
			aMinCollisionZ = -32.0f;
		}
		else if (mProjectileType == ProjectileType::PROJECTILE_BASKETBALL)
		{
			aMinCollisionZ = 60.0f;
		}
		else if (mProjectileType == ProjectileType::PROJECTILE_MELON || mProjectileType == ProjectileType::PROJECTILE_WINTERMELON)
		{
			aMinCollisionZ = -35.0f;
		}
		else if (mProjectileType == ProjectileType::PROJECTILE_CABBAGE || mProjectileType == ProjectileType::PROJECTILE_KERNEL)
		{
			aMinCollisionZ = -30.0f;
		}
		else if (mProjectileType == ProjectileType::PROJECTILE_COBBIG)
		{
			aMinCollisionZ = -60.0f;
		}
		if (mBoard->mPlantRow[mRow] == PlantRowType::PLANTROW_POOL)
		{
			aMinCollisionZ += 40.0f;
		}

		if (mPosZ <= aMinCollisionZ)
		{
			return;
		}
	}

	Plant* aPlant = nullptr;
	Zombie* aZombie = nullptr;
	if (mProjectileType == ProjectileType::PROJECTILE_BASKETBALL || mProjectileType == ProjectileType::PROJECTILE_ZOMBIE_PEA)
	{
		aPlant = FindCollisionTargetPlant();
	}
	else
	{
		aZombie = FindCollisionTarget();
	}

	float aGroundZ = 80.0f;
	if (mProjectileType == ProjectileType::PROJECTILE_COBBIG)
	{
		aGroundZ = -40.0f;
	}
	bool hitGround = mPosZ > aGroundZ;
	if (aZombie == nullptr && aPlant == nullptr && !hitGround)
	{
		return;
	}

	if (aPlant)
	{
		Plant* aUmbrellaPlant = mBoard->FindUmbrellaPlant(aPlant->mPlantCol, aPlant->mRow);
		if (aUmbrellaPlant)
		{
			if (aUmbrellaPlant->mState == PlantState::STATE_UMBRELLA_REFLECTING)
			{
				mApp->PlayFoley(FoleyType::FOLEY_SPLAT);
				int aRenderPosition = Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 1);
				mApp->AddPvzpParticle(mPosX + 20.0f, mPosY + 20.0f, aRenderPosition, ParticleEffect::PARTICLE_UMBRELLA_REFLECT);
				Die();
			}
			else if (aUmbrellaPlant->mState != PlantState::STATE_UMBRELLA_TRIGGERED)
			{
				mApp->PlayFoley(FoleyType::FOLEY_UMBRELLA);
				aUmbrellaPlant->DoSpecial();
			}
		}
		else
		{
			aPlant->mPlantHealth -= GetProjectileDef().mDamage;
			aPlant->mEatenFlashCountdown = std::max(aPlant->mEatenFlashCountdown, 25);
			mApp->PlayFoley(FoleyType::FOLEY_SPLAT);
			Die();
		}
	}
	else if (mProjectileType == ProjectileType::PROJECTILE_COBBIG)
	{
		int aBeforeGargantuarCount = mBoard->GetLiveGargantuarCount();
		mBoard->KillAllZombiesInRadius(mRow, mPosX + 80, mPosY + 40, 115, 1, true, mDamageRangeFlags);
		int aAfterGargantuarCount = mBoard->GetLiveGargantuarCount();
		mBoard->mGargantuarsKillsByCornCob += aBeforeGargantuarCount - aAfterGargantuarCount;
		if (mBoard->mGargantuarsKillsByCornCob >= 2)
			ReportAchievement::GiveAchievement(mApp, PopcornParty, true);

		DoImpact(nullptr);
	}
	else
	{
		DoImpact(aZombie);
	}
}

void Projectile::UpdateNormalMotion()
{
	if (mMotionType == ProjectileMotion::MOTION_BACKWARDS)
	{
		mPosX -= 3.33f;
	}
	else if (mMotionType == ProjectileMotion::MOTION_HOMING)
	{
		Zombie* aZombie = mBoard->ZombieTryToGet(mTargetZombieID);
		if (aZombie && aZombie->EffectedByDamage(static_cast<unsigned int>(mDamageRangeFlags)))
		{
			Rect aZombieRect = aZombie->GetZombieRect();
			SexyVector2 aTargetCenter(aZombie->ZombieTargetLeadX(0.0f), aZombieRect.mY + aZombieRect.mHeight / 2);
			SexyVector2 aProjectileCenter(mPosX + mWidth / 2, mPosY + mHeight / 2);
			SexyVector2 aToTarget = (aTargetCenter - aProjectileCenter).Normalize();
			SexyVector2 aMotion(mVelX, mVelY);

			aMotion += aToTarget * (0.001f * mProjectileAge);
			aMotion = aMotion.Normalize();
			aMotion *= 2.0f;

			mVelX = aMotion.x;
			mVelY = aMotion.y;
			mRotation = -atan2(mVelY, mVelX);
		}

		mPosY += mVelY;
		mPosX += mVelX;
		mShadowY += mVelY;
		mRow = mBoard->PixelToGridYKeepOnBoard(mPosX, mPosY);
	}
	else if (mMotionType == ProjectileMotion::MOTION_STAR)
	{
		mPosY += mVelY;
		mPosX += mVelX;
		mShadowY += mVelY;

		if (mVelY != 0.0f)
		{
			mRow = mBoard->PixelToGridYKeepOnBoard(mPosX, mPosY);
		}
	}
	else if (mMotionType == ProjectileMotion::MOTION_BEE)
	{
		if (mProjectileAge < 60)
		{
			mPosY -= 0.5f;
		}
		mPosX += 3.33f;
	}
	else if (mMotionType == ProjectileMotion::MOTION_FLOAT_OVER)
	{
		if (mVelZ < 0.0f)
		{
			mVelZ += 0.002f;
			mVelZ = std::min(mVelZ, 0.0f);
			mPosY += mVelZ;
			mRotation = 0.3f - 0.7f * mVelZ * PI * 0.25f;
		}
		mPosX += 0.4f;
	}
	else if (mMotionType == ProjectileMotion::MOTION_BEE_BACKWARDS)
	{
		if (mProjectileAge < 60)
		{
			mPosY -= 0.5f;
		}
		mPosX -= 3.33f;
	}
	else if (mMotionType == ProjectileMotion::MOTION_THREEPEATER)
	{
		mPosX += 3.33f;
		mPosY += mVelY;
		mVelY *= 0.97f;
		mShadowY += mVelY;
	}
	else
	{
		mPosX += 3.33f;
	}

	if (mApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_HIGH_GRAVITY)
	{
		if (mMotionType == ProjectileMotion::MOTION_FLOAT_OVER)
		{
			mVelZ += 0.004f;
		}
		else
		{
			mVelZ += 0.2f;
		}

		mPosY += mVelZ;
	}

	CheckForCollision();
	CheckForHighGround();
}

void Projectile::UpdateMotion()
{
	if (mAnimTicksPerFrame > 0)
	{
		mAnimCounter = (mAnimCounter + 1) % (mNumFrames * mAnimTicksPerFrame);
		mFrame = mAnimCounter / mAnimTicksPerFrame;
	}

	int aOldRow = mRow;
	float aOldY = mBoard->GetPosYBasedOnRow(mPosX, mRow);
	if (mMotionType == ProjectileMotion::MOTION_LOBBED)
	{
		UpdateLobMotion();
	}
	else
	{
		UpdateNormalMotion();
	}

	float aSlopeHeightChange = mBoard->GetPosYBasedOnRow(mPosX, aOldRow) - aOldY;
	if (mProjectileType == ProjectileType::PROJECTILE_COBBIG)
	{
		aSlopeHeightChange = 0.0f;  // Fix The Roof Offset Bug of Corn Cob
	}
	if (mMotionType == ProjectileMotion::MOTION_FLOAT_OVER)
	{
		mPosY += aSlopeHeightChange;
	}
	if (mMotionType == ProjectileMotion::MOTION_LOBBED)
	{
		mPosY += aSlopeHeightChange;
		mPosZ -= aSlopeHeightChange;
	}
	mShadowY += aSlopeHeightChange;
	mX = static_cast<int>(mPosX);
	mY = static_cast<int>(mPosY + mPosZ);
}

void Projectile::PlayImpactSound(Zombie* theZombie)
{
	bool aPlayHelmSound = true;
	bool aPlaySplatSound = true;
	if (mProjectileType == ProjectileType::PROJECTILE_KERNEL)
	{
		mApp->PlayFoley(FoleyType::FOLEY_KERNEL_SPLAT);
		aPlayHelmSound = false;
		aPlaySplatSound = false;
	}
	else if (mProjectileType == ProjectileType::PROJECTILE_BUTTER)
	{
		mApp->PlayFoley(FoleyType::FOLEY_BUTTER);
		aPlaySplatSound = false;
	}
	else if (mProjectileType == ProjectileType::PROJECTILE_FIREBALL && IsSplashDamage(theZombie))
	{
		mApp->PlayFoley(FoleyType::FOLEY_IGNITE);
		aPlayHelmSound = false;
		aPlaySplatSound = false;
	}
	else if (mProjectileType == ProjectileType::PROJECTILE_MELON || mProjectileType == ProjectileType::PROJECTILE_WINTERMELON)
	{
		mApp->PlayFoley(FoleyType::FOLEY_MELONIMPACT);
		aPlaySplatSound = false;
	}

	if (aPlayHelmSound && theZombie)
	{
		if (theZombie->mHelmType == HELMTYPE_PAIL)
		{
			mApp->PlayFoley(FoleyType::FOLEY_SHIELD_HIT);
			aPlaySplatSound = false;
		}
		else if (theZombie->mHelmType == HELMTYPE_TRAFFIC_CONE || theZombie->mHelmType == HELMTYPE_DIGGER || theZombie->mHelmType == HELMTYPE_FOOTBALL)
		{
			mApp->PlayFoley(FoleyType::FOLEY_PLASTIC_HIT);
		}
	}

	if (aPlaySplatSound)
	{
		mApp->PlayFoley(FoleyType::FOLEY_SPLAT);
	}
}

void Projectile::DoElectricImpact(Zombie* theZombie)
{
	ElectricProjectileState* aState = GetElectricState(this);
	if (!aState || !CanElectricHit(this, theZombie, *aState))
		return;

	PlayImpactSound(theZombie);
	// Queue fixed board-space impact points, not pointers that damage callbacks could invalidate.
	// Each newly reached zombie becomes a source: no row restriction or arbitrary target cap.
	std::vector<SexyVector2> aSources;
	aSources.push_back(ElectricTargetPoint(theZombie));
	AddElectricBolt(*aState, SexyVector2(mPosX + 12.0f, mPosY + mPosZ + 12.0f), aSources.front(), mProjectileAge);
	size_t aHitCount = 0;
	auto aDamageZombie = [&](Zombie* aZombie) {
		unsigned int anID = static_cast<unsigned int>(mBoard->ZombieGetID(aZombie));
		aState->mHitZombieIDs.insert(anID); // lifetime deduplication, before any damage side effects
		++aHitCount;
		aZombie->TakeDamage(GetProjectileDef().mDamage, GetDamageFlags(aZombie));
		if (!aZombie->IsDeadOrDying())
		{
			// Reuse butter's immobilization/animation/sound handling and immunity rules.
			// This visibly shows butter, but lasts only 30 ticks; it is NOT ice/slow.
			// Never shorten a stronger pre-existing butter stun.
			int aPreviousButter = aZombie->mButteredCounter;
			if (aPreviousButter < ELECTRIC_STUN_TICKS)
			{
				aZombie->ApplyButter();
				if (aZombie->mButteredCounter > aPreviousButter)
					aZombie->mButteredCounter = std::max(aPreviousButter, std::min(aZombie->mButteredCounter, ELECTRIC_STUN_TICKS));
			}
		}
	};
	aDamageZombie(theZombie);
	for (size_t i = 0; i < aSources.size(); ++i)
	{
		// Copy before pushing: vector growth may invalidate its existing elements.
		SexyVector2 aSource = aSources[i];
		for (Zombie* aZombie : mBoard->mZombies)
		{
			if (!CanElectricHit(this, aZombie, *aState))
				continue;
			SexyVector2 aTarget = ElectricTargetPoint(aZombie);
			SexyVector2 aDelta = aTarget - aSource;
			if (aDelta.x * aDelta.x + aDelta.y * aDelta.y > ELECTRIC_CHAIN_RADIUS * ELECTRIC_CHAIN_RADIUS)
				continue;
			AddElectricBolt(*aState, aSource, aTarget, static_cast<unsigned int>(mBoard->ZombieGetID(aZombie)));
			aSources.push_back(aTarget);
			aDamageZombie(aZombie);
		}
	}
	TraceElectricImpact(aHitCount, aState->mHitZombieIDs.size());
	// A homing projectile must not keep steering into its already-hit target. Continue
	// ballistically with its current velocity, using STAR's existing cross-row tracking.
	if (mMotionType == ProjectileMotion::MOTION_HOMING)
	{
		mMotionType = ProjectileMotion::MOTION_STAR;
		mTargetZombieID = ZombieID::ZOMBIEID_NULL;
	}
	// No Die(): the primary electric projectile pierces and may start another chain later.
}

void Projectile::DoImpact(Zombie* theZombie)
{
	if (IsElectric() && theZombie)
	{
		DoElectricImpact(theZombie);
		return;
	}
	PlayImpactSound(theZombie);

	if (IsSplashDamage(theZombie))
	{
		if (mProjectileType == ProjectileType::PROJECTILE_FIREBALL && theZombie)
		{
			theZombie->RemoveColdEffects();
		}

		DoSplashDamage(theZombie);
	}
	else if (theZombie)
	{
		unsigned int aDamageFlags = GetDamageFlags(theZombie);
		theZombie->TakeDamage(GetProjectileDef().mDamage, aDamageFlags);
	}

	float aLastPosX = mPosX - mVelX;
	float aLastPosY = mPosY + mPosZ - mVelY - mVelZ;
	ParticleEffect aEffect = ParticleEffect::PARTICLE_NONE;
	float aSplatPosX = mPosX + 12.0f;
	float aSplatPosY = mPosY + 12.0f;
	switch (mProjectileType)
	{
	case ProjectileType::PROJECTILE_MELON:
		mApp->AddPvzpParticle(aLastPosX + 30.0f, aLastPosY + 30.0f, mRenderOrder + 1, ParticleEffect::PARTICLE_MELONSPLASH);
		break;
	case ProjectileType::PROJECTILE_WINTERMELON:
		mApp->AddPvzpParticle(aLastPosX + 30.0f, aLastPosY + 30.0f, mRenderOrder + 1, ParticleEffect::PARTICLE_WINTERMELON);
		break;
	case ProjectileType::PROJECTILE_COBBIG:
	{
		int aRenderOrder = Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_GROUND, mCobTargetRow, 2);
		mApp->AddPvzpParticle(mPosX + 80.0f, mPosY + 40.0f, aRenderOrder, ParticleEffect::PARTICLE_BLASTMARK);
		mApp->AddPvzpParticle(mPosX + 80.0f, mPosY + 40.0f, mRenderOrder + 1, ParticleEffect::PARTICLE_POPCORNSPLASH);
		mApp->PlaySample(SOUND_DOOMSHROOM);
		mBoard->ShakeBoard(3, -4);
		break;
	}
	case ProjectileType::PROJECTILE_PEA:
		aSplatPosX -= 15.0f;
		aEffect = ParticleEffect::PARTICLE_PEA_SPLAT;
		break;
	case ProjectileType::PROJECTILE_SNOWPEA:
		aSplatPosX -= 15.0f;
		aEffect = ParticleEffect::PARTICLE_SNOWPEA_SPLAT;
		break;
	case ProjectileType::PROJECTILE_FIREBALL:
	{
		if (IsSplashDamage(theZombie))
		{
			Reanimation* aFireReanim = mApp->AddReanimation(mPosX + 38.0f, mPosY - 20.0f, mRenderOrder + 1, ReanimationType::REANIM_JALAPENO_FIRE);
			aFireReanim->mAnimTime = 0.25f;
			aFireReanim->mAnimRate = 24.0f;
			aFireReanim->OverrideScale(0.7f, 0.4f);
		}
		break;
	}
	case ProjectileType::PROJECTILE_STAR:
		aEffect = ParticleEffect::PARTICLE_STAR_SPLAT;
		break;
	case ProjectileType::PROJECTILE_PUFF:
		aSplatPosX -= 20.0f;
		aEffect = ParticleEffect::PARTICLE_PUFF_SPLAT;
		break;
	case ProjectileType::PROJECTILE_CABBAGE:
		aSplatPosX = aLastPosX - 38.0f;
		aSplatPosY = aLastPosY + 23.0f;
		aEffect = ParticleEffect::PARTICLE_CABBAGE_SPLAT;
		break;
	case ProjectileType::PROJECTILE_BUTTER:
		aSplatPosX = aLastPosX - 20.0f;
		aSplatPosY = aLastPosY + 63.0f;
		aEffect = ParticleEffect::PARTICLE_BUTTER_SPLAT;

		if (theZombie)
		{
			theZombie->ApplyButter();
		}
		break;
	default:
		break;
	}

	if (aEffect != ParticleEffect::PARTICLE_NONE)
	{
		if (theZombie)
		{
			float aPosX = aSplatPosX + 52.0f - theZombie->mX;
			float aPosY = aSplatPosY - theZombie->mY;
			if (theZombie->mZombiePhase == ZombiePhase::PHASE_SNORKEL_WALKING_IN_POOL || theZombie->mZombiePhase == ZombiePhase::PHASE_DOLPHIN_WALKING_IN_POOL)
			{
				aPosY += 60.0f;
			}
			if (mMotionType == ProjectileMotion::MOTION_BACKWARDS)
			{
				aPosX -= 80.0f;
			}
			else if (mPosX > theZombie->mX + 40 && mMotionType != ProjectileMotion::MOTION_LOBBED)
			{
				aPosX -= 60.0f;
			}

			aPosY = std::clamp(aPosY, 20.0f, 100.0f);
			theZombie->AddAttachedParticle(aPosX, aPosY, aEffect);
		}
		else
		{
			mApp->AddPvzpParticle(aSplatPosX, aSplatPosY, mRenderOrder + 1, aEffect);
		}
	}

	Die();
}

void Projectile::Update()
{
	mProjectileAge++;
	if (mApp->mGameScene != GameScenes::SCENE_PLAYING && !mBoard->mCutScene->ShouldRunUpsellBoard())
		return;

	if (ElectricProjectileState* aState = GetElectricState(this))
	{
		for (ElectricBolt& aBolt : aState->mBolts)
			--aBolt.mTicksLeft;
		std::erase_if(aState->mBolts, [](const ElectricBolt& aBolt) { return aBolt.mTicksLeft <= 0; });
	}

	int aTime = 20;
	if (mProjectileType == ProjectileType::PROJECTILE_PEA ||
		mProjectileType == ProjectileType::PROJECTILE_SNOWPEA ||
		mProjectileType == ProjectileType::PROJECTILE_CABBAGE ||
		mProjectileType == ProjectileType::PROJECTILE_MELON ||
		mProjectileType == ProjectileType::PROJECTILE_WINTERMELON ||
		mProjectileType == ProjectileType::PROJECTILE_KERNEL ||
		mProjectileType == ProjectileType::PROJECTILE_BUTTER ||
		mProjectileType == ProjectileType::PROJECTILE_COBBIG ||
		mProjectileType == ProjectileType::PROJECTILE_ZOMBIE_PEA ||
		mProjectileType == ProjectileType::PROJECTILE_SPIKE)
	{
		aTime = 0;
	}
	if (mProjectileAge > aTime)
	{
		mRenderOrder = Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_PROJECTILE, mRow, 0);
	}

	if (mClickBackoffCounter > 0)
	{
		mClickBackoffCounter--;
	}
	mRotation += mRotationSpeed;

	UpdateMotion();
	AttachmentUpdateAndMove(mAttachmentID, mPosX, mPosY + mPosZ);
}

void Projectile::DrawElectric(Graphics* g)
{
	ElectricProjectileState* aState = GetElectricState(this);
	if (!aState)
		return;

	// BeginDraw translated by this projectile's current mX/mY. Stored bolts are fixed
	// board coordinates; undo ONLY that translation, retaining the Board/camera frame.
	Graphics aElectricGraphics(*g);
	MakeParentGraphicsFrame(&aElectricGraphics);
	aElectricGraphics.SetDrawMode(Graphics::DRAWMODE_NORMAL);
	aElectricGraphics.SetColorizeImages(false);
	aElectricGraphics.SetScale(1.0f, 1.0f, 0.0f, 0.0f);
	for (const ElectricBolt& aBolt : aState->mBolts)
	{
		int anAlpha = 255 * aBolt.mTicksLeft / ELECTRIC_BOLT_TICKS;
		for (size_t i = 1; i < aBolt.mPoints.size(); ++i)
		{
			const SexyVector2& aFrom = aBolt.mPoints[i - 1];
			const SexyVector2& aTo = aBolt.mPoints[i];
			int aFromX = static_cast<int>(aFrom.x);
			int aFromY = static_cast<int>(aFrom.y);
			int aToX = static_cast<int>(aTo.x);
			int aToY = static_cast<int>(aTo.y);
			aElectricGraphics.SetColor(Color(40, 140, 255, anAlpha));
			aElectricGraphics.DrawLine(aFromX, aFromY - 1, aToX, aToY - 1);
			aElectricGraphics.DrawLine(aFromX, aFromY + 1, aToX, aToY + 1);
			aElectricGraphics.SetColor(Color(220, 255, 255, anAlpha));
			aElectricGraphics.DrawLine(aFromX, aFromY, aToX, aToY);
		}
	}

	// A distinct electric pea, never the snow-pea sprite. State changes stay in this copy.
	aElectricGraphics.SetColorizeImages(true);
	aElectricGraphics.SetColor(Color(100, 220, 255));
	aElectricGraphics.DrawImage(IMAGE_PROJECTILEPEA, static_cast<int>(mPosX), static_cast<int>(mPosY + mPosZ));
	aElectricGraphics.SetColor(Color(220, 255, 255));
	int aX = static_cast<int>(mPosX) + 12;
	int aY = static_cast<int>(mPosY + mPosZ) + 12;
	int aFlicker = (mProjectileAge / 3) % 2 == 0 ? 3 : -3;
	aElectricGraphics.DrawLine(aX - 11, aY, aX - 3, aY + aFlicker);
	aElectricGraphics.DrawLine(aX - 3, aY + aFlicker, aX + 3, aY - aFlicker);
	aElectricGraphics.DrawLine(aX + 3, aY - aFlicker, aX + 11, aY);
}

void Projectile::Draw(Graphics* g)
{
	if (IsElectric())
	{
		DrawElectric(g);
		return;
	}
	const ProjectileDefinition& aProjectileDef = GetProjectileDef();

	Image* aImage = nullptr;
	float aScale = 1.0f;
	switch (mProjectileType)
	{
	case ProjectileType::PROJECTILE_COBBIG:
		aImage = IMAGE_REANIM_COBCANNON_COB;
		aScale = 0.9f;
		break;
	case ProjectileType::PROJECTILE_PEA:
	case ProjectileType::PROJECTILE_ZOMBIE_PEA:
		aImage = IMAGE_PROJECTILEPEA;
		break;
	case ProjectileType::PROJECTILE_SNOWPEA:
		aImage = IMAGE_PROJECTILESNOWPEA;
		break;
	case ProjectileType::PROJECTILE_FIREBALL:
		aImage = nullptr;
		break;
	case ProjectileType::PROJECTILE_SPIKE:
		aImage = IMAGE_PROJECTILECACTUS;
		break;
	case ProjectileType::PROJECTILE_STAR:
		aImage = IMAGE_PROJECTILE_STAR;
		break;
	case ProjectileType::PROJECTILE_PUFF:
		aImage = IMAGE_PUFFSHROOM_PUFF1;
		aScale = PvzpAnimateCurveFloat(0, 30, mProjectileAge, 0.3f, 1.0f, PvzpCurves::CURVE_LINEAR);
		break;
	case ProjectileType::PROJECTILE_BASKETBALL:
		aImage = IMAGE_REANIM_ZOMBIE_CATAPULT_BASKETBALL;
		aScale = 1.1f;
		break;
	case ProjectileType::PROJECTILE_CABBAGE:
		aImage = IMAGE_REANIM_CABBAGEPULT_CABBAGE;
		aScale = 1.0f;
		break;
	case ProjectileType::PROJECTILE_KERNEL:
		aImage = IMAGE_REANIM_CORNPULT_KERNAL;
		aScale = 0.95f;
		break;
	case ProjectileType::PROJECTILE_BUTTER:
		aImage = IMAGE_REANIM_CORNPULT_BUTTER;
		aScale = 0.8f;
		break;
	case ProjectileType::PROJECTILE_MELON:
		aImage = IMAGE_REANIM_MELONPULT_MELON;
		aScale = 1.0f;
		break;
	case ProjectileType::PROJECTILE_WINTERMELON:
		aImage = IMAGE_REANIM_WINTERMELON_PROJECTILE;
		aScale = 1.0f;
		break;
	default:
		PVZP_ASSERT(false);
		break;
	}

	bool aMirror = false;
	if (mMotionType == ProjectileMotion::MOTION_BEE_BACKWARDS)
	{
		aMirror = true;
	}

	if (aImage)
	{
		PVZP_ASSERT(aProjectileDef.mImageRow < aImage->mNumRows);
		PVZP_ASSERT(mFrame < aImage->mNumCols);

		int aCelWidth = aImage->GetCelWidth();
		int aCelHeight = aImage->GetCelHeight();
		Rect aSrcRect(aCelWidth * mFrame, aCelHeight * aProjectileDef.mImageRow, aCelWidth, aCelHeight);
		if (FloatApproxEqual(mRotation, 0.0f) && FloatApproxEqual(aScale, 1.0f))
		{
			Rect aDestRect(0, 0, aCelWidth, aCelHeight);
			g->DrawImageMirror(aImage, aDestRect, aSrcRect, aMirror);
		}
		else
		{
			float aOffsetX = mPosX + aCelWidth * 0.5f;
			float aOffsetY = mPosZ + mPosY + aCelHeight * 0.5f;
			SexyTransform2D aTransform;
			PvzpScaleRotateTransformMatrix(aTransform, aOffsetX + mBoard->mX, aOffsetY + mBoard->mY, mRotation, aScale, aScale);
			PvzpBltMatrix(g, aImage, aTransform, g->mClipRect, Color::White, g->mDrawMode, aSrcRect);
		}
	}

	if (mAttachmentID != AttachmentID::ATTACHMENTID_NULL)
	{
		Graphics theParticleGraphics(*g);
		MakeParentGraphicsFrame(&theParticleGraphics);
		AttachmentDraw(mAttachmentID, &theParticleGraphics, false);
	}
}

void Projectile::DrawShadow(Graphics* g)
{
	int aCelCol = 0;
	float aScale = 1.0f;
	float aStretch = 1.0f;
	float aOffsetX = mPosX - mX;
	float aOffsetY = mPosY - mY;

	int aGridX = mBoard->PixelToGridXKeepOnBoard(mX, mY);
	bool isHighGround = false;
	if (mBoard->mGridSquareType[aGridX][mRow] == GridSquareType::GRIDSQUARE_HIGH_GROUND)
	{
		isHighGround = true;
	}
	if (mOnHighGround && !isHighGround)
	{
		aOffsetY += HIGH_GROUND_HEIGHT;
	}
	else if (!mOnHighGround && isHighGround)
	{
		aOffsetY -= HIGH_GROUND_HEIGHT;
	}

	if (mBoard->StageIsNight())
	{
		aCelCol = 1;
	}

	switch (mProjectileType)
	{
	case ProjectileType::PROJECTILE_PEA:
	case ProjectileType::PROJECTILE_ZOMBIE_PEA:
		aOffsetX += 3.0f;
		break;

	case ProjectileType::PROJECTILE_SNOWPEA:
		aOffsetX += -1.0f;
		aScale = 1.3f;
		break;

	case ProjectileType::PROJECTILE_STAR:
		aOffsetX += 7.0f;
		break;

	case ProjectileType::PROJECTILE_CABBAGE:
	case ProjectileType::PROJECTILE_KERNEL:
	case ProjectileType::PROJECTILE_BUTTER:
	case ProjectileType::PROJECTILE_MELON:
	case ProjectileType::PROJECTILE_WINTERMELON:
		aOffsetX += 3.0f;
		aOffsetY += 10.0f;
		aScale = 1.6f;
		break;

	case ProjectileType::PROJECTILE_PUFF:
		return;

	case ProjectileType::PROJECTILE_COBBIG:
		aScale = 1.0f;
		aStretch = 3.0f;
		aOffsetX += 57.0f;
		break;

	case ProjectileType::PROJECTILE_FIREBALL:
		aScale = 1.4f;
		break;
	default:
		break;
	}

	if (mMotionType == ProjectileMotion::MOTION_LOBBED)
	{
		float aHeight = std::clamp(-mPosZ, 0.0f, 200.0f);
		aScale *= 200.0f / (aHeight + 200.0f);
	}

	PvzpDrawImageCelScaledF(g, IMAGE_PEA_SHADOWS, aOffsetX, (mShadowY - mPosY + aOffsetY), aCelCol, 0, aScale * aStretch, aScale);
}

void Projectile::Die()
{
	mDead = true;

	if (mProjectileType == ProjectileType::PROJECTILE_PUFF || mProjectileType == ProjectileType::PROJECTILE_SNOWPEA)
	{
		AttachmentCrossFade(mAttachmentID, "FadeOut");
		AttachmentDetach(mAttachmentID);
	}
	else
	{
		AttachmentDie(mAttachmentID);
	}
}

Rect Projectile::GetProjectileRect()
{
	if (mProjectileType == ProjectileType::PROJECTILE_PEA ||
		mProjectileType == ProjectileType::PROJECTILE_SNOWPEA ||
		mProjectileType == ProjectileType::PROJECTILE_ZOMBIE_PEA)
	{
		return Rect(mX - 15, mY, mWidth + 15, mHeight);
	}
	else if (mProjectileType == ProjectileType::PROJECTILE_COBBIG)
	{
		return Rect(mX + mWidth / 2 - 115, mY + mHeight / 2 - 115, 230, 230);
	}
	else if (mProjectileType == ProjectileType::PROJECTILE_MELON || mProjectileType == ProjectileType::PROJECTILE_WINTERMELON)
	{
		return Rect(mX + 20, mY, 60, mHeight);
	}
	else if (mProjectileType == ProjectileType::PROJECTILE_FIREBALL)
	{
		return Rect(mX, mY, mWidth - 10, mHeight);
	}
	else if (mProjectileType == ProjectileType::PROJECTILE_SPIKE)
	{
		return Rect(mX - 25, mY, mWidth + 25, mHeight);
	}
	else
	{
		return Rect(mX, mY, mWidth, mHeight);
	}
}

void Projectile::ConvertToFireball(int theGridX)
{
	if (IsElectric()) // Torchwood must not replace electricity with fire/splash damage.
		return;
	if (mHitTorchwoodGridX == theGridX)
		return;

	mProjectileType = ProjectileType::PROJECTILE_FIREBALL;
	mHitTorchwoodGridX = theGridX;
	mApp->PlayFoley(FoleyType::FOLEY_FIREPEA);

	float aOffsetX = -25.0f;
	float aOffsetY = -25.0f;
	Reanimation* aFirePeaReanim = mApp->AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_FIRE_PEA);
	if (mMotionType == ProjectileMotion::MOTION_BACKWARDS)
	{
		aFirePeaReanim->OverrideScale(-1.0f, 1.0f);
		aOffsetX += 80.0f;
	}

	aFirePeaReanim->SetPosition(mPosX + aOffsetX, mPosY + aOffsetY);
	aFirePeaReanim->mLoopType = ReanimLoopType::REANIM_LOOP;
	aFirePeaReanim->mAnimRate = RandRangeFloat(50.0f, 80.0f);
	AttachReanim(mAttachmentID, aFirePeaReanim, aOffsetX, aOffsetY);
}

void Projectile::ConvertToPea(int theGridX)
{
	if (mHitTorchwoodGridX == theGridX)
		return;

	AttachmentDie(mAttachmentID);
	mProjectileType = ProjectileType::PROJECTILE_PEA;
	mHitTorchwoodGridX = theGridX;
	mApp->PlayFoley(FoleyType::FOLEY_THROW);
}

const ProjectileDefinition& Projectile::GetProjectileDef()
{
	const ProjectileDefinition& aProjectileDef = gProjectileDefinition[mProjectileType];
	PVZP_ASSERT(aProjectileDef.mProjectileType == mProjectileType);

	return aProjectileDef;
}
