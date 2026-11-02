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

#include "../Board.h"
#include "../Zombie.h"
#include "GameButton.h"
#include "StoreScreen.h"
#include "../Cutscene.h"
#include "../SeedPacket.h"
#include "../../LawnApp.h"
#include "AlmanacDialog.h"
#include "ImitaterDialog.h"
#include "../System/Music.h"
#include "../../Resources.h"
#include "../../Lawn/Plant.h"
#include "../../PvzpLib/PvzpMod.h"
#include "../../PvzpLib/PvzpCustomPlants.h"
#include "../../PvzpLib/PvzpCustomLevels.h"
#include "../ToolTipWidget.h"
#include "SeedChooserScreen.h"
#include "../../GameConstants.h"
#include "../System/PlayerInfo.h"
#include "misc/Debug.h"
#include "widget/Dialog.h"
#include "misc/MTRand.h"
#include "../../PvzpLib/PvzpStringFile.h"
#include "widget/WidgetManager.h"
#include <algorithm>
#include <vector>

SeedChooserScreen::SeedChooserScreen()
{
	mApp = (LawnApp*)gSexyAppBase;
	mBoard = mApp->mBoard;
	mClip = false;
	mSeedChooserAge = 0;
	mSeedsInFlight = 0;
	mSeedsInBank = 0;
	mLastMouseX = -1;
	mLastMouseY = -1;
	mChooseState = CHOOSE_NORMAL;
	mViewLawnTime = 0;
	mToolTip = std::make_unique<ToolTipWidget>();
	mToolTip->mMaxLinesWidth = mApp->GetInteger("SEED_CHOOSER_SCREEN_TOOL_TIP_MAX_LINE_WIDTH", 0);
	mToolTipSeed = -1;

	mStartButton = std::make_unique<GameButton>(SeedChooserScreen::SeedChooserScreen_Start);
	mStartButton->SetLabel("[LETS_ROCK_BUTTON]"); // the localization key name is wrong
	mStartButton->mButtonImage = Sexy::IMAGE_SEEDCHOOSER_BUTTON;
	mStartButton->mOverImage = nullptr;
	mStartButton->mDownImage = nullptr;
	mStartButton->mDisabledImage = Sexy::IMAGE_SEEDCHOOSER_BUTTON_DISABLED;
	mStartButton->mOverOverlayImage = Sexy::IMAGE_SEEDCHOOSER_BUTTON_GLOW;
	mStartButton->SetFont(Sexy::FONT_DWARVENTODCRAFT18YELLOW);
	mStartButton->SetLabelHiliteColor(Color::White);
	mStartButton->Resize(154, 545, 156, 42);
	mStartButton->mTextOffsetY = -1;
	EnableStartButton(false);

	mMenuButton = std::make_unique<GameButton>(SeedChooserScreen::SeedChooserScreen_Menu);
	mMenuButton->SetLabel("[MENU_BUTTON]");
	mMenuButton->Resize(681, -10, 117, 46);
	mMenuButton->mDrawStoneButton = true;

	mRandomButton = std::make_unique<GameButton>(SeedChooserScreen::SeedChooserScreen_Random);
	mRandomButton->SetLabel("(Debug Play)");
	mRandomButton->mButtonImage = Sexy::IMAGE_BLANK;
	mRandomButton->mOverImage = Sexy::IMAGE_BLANK;
	mRandomButton->mDownImage = Sexy::IMAGE_BLANK;
	mRandomButton->SetFont(Sexy::FONT_BRIANNETOD12);
	mRandomButton->SetLabelColor(Color(255, 240, 0));
	mRandomButton->SetLabelHiliteColor(Color(200, 200, 255));
	mRandomButton->Resize(332, 546, 100, 30);
	if (!mApp->mCheatKeys)
	{
		mRandomButton->mBtnNoDraw = true;
		mRandomButton->mDisabled = true;
	}

	Color aBtnColor = Color(42, 42, 90);
	Image* aBtnImage = Sexy::IMAGE_SEEDCHOOSER_BUTTON2;
	Image* aOverImage = Sexy::IMAGE_SEEDCHOOSER_BUTTON2_GLOW;
	int aImageWidth = aBtnImage->GetWidth();
	int aImageHeight = aOverImage->GetHeight();

	mViewLawnButton = std::make_unique<GameButton>(SeedChooserScreen::SeedChooserScreen_ViewLawn);
	mViewLawnButton->SetLabel("[VIEW_LAWN]");
	mViewLawnButton->mButtonImage = aBtnImage;
	mViewLawnButton->mOverImage = aOverImage;
	mViewLawnButton->mDownImage = nullptr;
	mViewLawnButton->SetFont(Sexy::FONT_BRIANNETOD12);
	mViewLawnButton->SetLabelColor(aBtnColor);
	mViewLawnButton->SetLabelHiliteColor(aBtnColor);
	mViewLawnButton->Resize(22, 561, aImageWidth, aImageHeight);
	mViewLawnButton->mParentWidget = this;
	mViewLawnButton->mTextOffsetY = 1;
	if (!mBoard->mCutScene->IsSurvivalRepick())
	{
		mViewLawnButton->mBtnNoDraw = true;
		mViewLawnButton->mDisabled = true;
	}

	mAlmanacButton = std::make_unique<GameButton>(SeedChooserScreen::SeedChooserScreen_Almanac);
	mAlmanacButton->SetLabel("[ALMANAC_BUTTON]");
	mAlmanacButton->mButtonImage = aBtnImage;
	mAlmanacButton->mOverImage = aOverImage;
	mAlmanacButton->mDownImage = nullptr;
	mAlmanacButton->SetFont(Sexy::FONT_BRIANNETOD12);
	mAlmanacButton->SetLabelColor(aBtnColor);
	mAlmanacButton->SetLabelHiliteColor(aBtnColor);
	mAlmanacButton->Resize(560, 572, aImageWidth, aImageHeight);
	mAlmanacButton->mParentWidget = this;
	mAlmanacButton->mTextOffsetY = 1;

	mStoreButton = std::make_unique<GameButton>(SeedChooserScreen::SeedChooserScreen_Store);
	mStoreButton->SetLabel("[SHOP_BUTTON]");
	mStoreButton->mButtonImage = aBtnImage;
	mStoreButton->mOverImage = aOverImage;
	mStoreButton->mDownImage = nullptr;
	mStoreButton->SetFont(Sexy::FONT_BRIANNETOD12);
	mStoreButton->SetLabelColor(aBtnColor);
	mStoreButton->SetLabelHiliteColor(aBtnColor);
	mStoreButton->Resize(680, 572, aImageWidth, aImageHeight);
	mStoreButton->mParentWidget = this;
	mStoreButton->mTextOffsetY = 1;

	mImitaterButton = std::make_unique<GameButton>(SeedChooserScreen::SeedChooserScreen_Imitater);
	mImitaterButton->mButtonImage = Sexy::IMAGE_IMITATERSEED;
	mImitaterButton->mOverImage = Sexy::IMAGE_IMITATERSEED;
	mImitaterButton->mDownImage = Sexy::IMAGE_IMITATERSEED;
	mImitaterButton->mDisabledImage = Sexy::IMAGE_IMITATERSEEDDISABLED;
	mImitaterButton->Resize(464, 515, Sexy::IMAGE_IMITATERSEED->mWidth, Sexy::IMAGE_IMITATERSEED->mHeight);
	mImitaterButton->mParentWidget = this;

	if (!mApp->CanShowAlmanac())
	{
		mAlmanacButton->mBtnNoDraw = true;
		mAlmanacButton->mDisabled = true;
	}
	if (!mApp->CanShowStore())
	{
		mStoreButton->mBtnNoDraw = true;
		mStoreButton->mDisabled = true;
	}

	int aAvailableCapacity = int(NUM_SEEDS_IN_CHOOSER);
	for (int i = int(SEED_CUSTOM_FIRST); i <= int(SEED_CUSTOM_LAST); ++i)
		if (PvzpCustomPlants::IsSlotUsed(static_cast<SeedType>(i))) ++aAvailableCapacity;
	DBG_ASSERT(mApp->GetSeedsAvailable() <= aAvailableCapacity);
	memset(mChosenSeeds, 0, sizeof(mChosenSeeds));
	for (int aIndex = 0; aIndex < int(NUM_SEED_SLOTS); ++aIndex)
	{
		SeedType aSeedType = static_cast<SeedType>(aIndex);
		ChosenSeed& aChosenSeed = mChosenSeeds[aIndex];
		aChosenSeed.mSeedType = aSeedType;
		if (aIndex < int(NUM_SEEDS_IN_CHOOSER) || IsModSeed(aSeedType))
			GetSeedPositionInChooser(aIndex, aChosenSeed.mX, aChosenSeed.mY);
		aChosenSeed.mTimeStartMotion = 0;
		aChosenSeed.mTimeEndMotion = 0;
		aChosenSeed.mStartX = aChosenSeed.mX;
		aChosenSeed.mStartY = aChosenSeed.mY;
		aChosenSeed.mEndX = aChosenSeed.mX;
		aChosenSeed.mEndY = aChosenSeed.mY;
		aChosenSeed.mSeedState = aSeedType != SEED_IMITATER &&
			(aIndex < int(NUM_SEEDS_IN_CHOOSER) || (IsModSeed(aSeedType) && ChooserHasSeed(aSeedType))) ? SEED_IN_CHOOSER : SEED_PACKET_HIDDEN;
		aChosenSeed.mSeedIndexInBank = -1;
		aChosenSeed.mRefreshCounter = 0;
		aChosenSeed.mRefreshing = false;
		aChosenSeed.mImitaterType = SEED_NONE;
		aChosenSeed.mCrazyDavePicked = false;
	}
	if (mBoard->mCutScene->IsSurvivalRepick())
	{
		for (int anIdx = 0; anIdx < mBoard->mSeedBank->mNumPackets; anIdx++)
		{
			SeedPacket* aSeedPacket = &mBoard->mSeedBank->mSeedPackets[anIdx];
			if (!ChooserHasSeed(aSeedPacket->mPacketType)) continue;
			ChosenSeed& aChosenSeed = mChosenSeeds[aSeedPacket->mPacketType];
			aChosenSeed.mRefreshing = aSeedPacket->mRefreshing;
			aChosenSeed.mRefreshCounter = aSeedPacket->mRefreshCounter;
		}
		mBoard->mSeedBank->mNumPackets = 0;
	}
	if (mApp->mGameMode == GAMEMODE_CHALLENGE_SEEING_STARS)
	{
		ChosenSeed& aStarFruit = mChosenSeeds[SEED_STARFRUIT];
		int aX = mBoard->GetSeedPacketPositionX(0);
		aStarFruit.mX = aX, aStarFruit.mY = 8;
		aStarFruit.mStartX = aX, aStarFruit.mStartY = 8;
		aStarFruit.mEndX = aX, aStarFruit.mEndY = 8;
		aStarFruit.mSeedState = SEED_IN_BANK;
		aStarFruit.mSeedIndexInBank = 0;
		mSeedsInBank++;
	}
	if (mApp->IsAdventureMode() && !mApp->IsFirstTimeAdventureMode())
		CrazyDavePickSeeds();
	UpdateImitaterButton();
}

int SeedChooserScreen::PickFromWeightedArrayUsingSpecialRandSeed(PvzpWeightedArray* theArray, int theCount, MTRand& theLevelRNG)
{
	int aTotalWeight = 0;
	for (int i = 0; i < theCount; i++)
		aTotalWeight += theArray[i].mWeight;
	DBG_ASSERT(aTotalWeight > 0);

	int aRndResult = theLevelRNG.Next((unsigned long)aTotalWeight);
	int aWeight = 0;
	for (int j = 0; j < theCount; j++)
	{
		aWeight += theArray[j].mWeight;
		if (aWeight > aRndResult) return theArray[j].mItem;
	}
	DBG_ASSERT(false);
	unreachable();
}

void SeedChooserScreen::CrazyDavePickSeeds()
{
	PvzpWeightedArray aSeedArray[NUM_SEED_TYPES];
	for (SeedType aSeedType = SEED_PEASHOOTER; aSeedType < NUM_SEEDS_IN_CHOOSER; aSeedType = (SeedType)(aSeedType + 1))
	{
		aSeedArray[aSeedType].mItem = aSeedType;
		if (!mApp->HasSeedType(aSeedType) || SeedNotRecommendedToPick(aSeedType) || SeedNotAllowedToPick(aSeedType) || Plant::IsUpgrade(aSeedType) ||
			aSeedType == SEED_IMITATER || aSeedType == SEED_UMBRELLA || aSeedType == SEED_BLOVER)
		{
			aSeedArray[aSeedType].mWeight = 0;
		}
		else
		{
			aSeedArray[aSeedType].mWeight = 1;
		}
	}
	if (mBoard->mZombieAllowed[ZOMBIE_BUNGEE] || mBoard->mZombieAllowed[ZOMBIE_CATAPULT])
	{
		aSeedArray[SEED_UMBRELLA].mWeight = 1;
	}
	if (mBoard->mZombieAllowed[ZOMBIE_BALLOON] || mBoard->StageHasFog())
	{
		aSeedArray[SEED_BLOVER].mWeight = 1;
	}
	if (mBoard->StageHasRoof())
	{
		aSeedArray[SEED_TORCHWOOD].mWeight = 0;
	}

	MTRand aLevelRNG = MTRand(mBoard->GetLevelRandSeed());
	for (int i = 0; i < 3; i++)
	{
		SeedType aPickedSeed = (SeedType)PickFromWeightedArrayUsingSpecialRandSeed(aSeedArray, NUM_SEEDS_IN_CHOOSER, aLevelRNG);
		aSeedArray[aPickedSeed].mWeight = 0;
		ChosenSeed& aChosenSeed = mChosenSeeds[aPickedSeed];

		int aPosX = mBoard->GetSeedPacketPositionX(i);
		aChosenSeed.mX = aPosX;
		aChosenSeed.mY = 8;
		aChosenSeed.mStartX = aPosX;
		aChosenSeed.mStartY = 8;
		aChosenSeed.mEndX = aPosX;
		aChosenSeed.mEndY = 8;
		aChosenSeed.mSeedState = SEED_IN_BANK;
		aChosenSeed.mSeedIndexInBank = i;
		aChosenSeed.mCrazyDavePicked = true;
		mSeedsInBank++;
	}
}

bool SeedChooserScreen::Has7Rows()
{
	if (PvzpCustomLevels::IsMode(mApp)) return true;
	// PlayerInfo* aPlayer = mApp->mPlayerInfo; // unused
	if (mApp->HasFinishedAdventure() || mApp->mPlayerInfo->mPurchases[STORE_ITEM_PLANT_GATLINGPEA]) return true;
	for (SeedType aSeedType = SEED_TWINSUNFLOWER; aSeedType < SEED_COBCANNON; aSeedType = (SeedType)(aSeedType + 1))
		if (aSeedType != SEED_SPIKEROCK && mApp->HasSeedType(aSeedType)) return true;
	return false;
}

void SeedChooserScreen::GetSeedPositionInChooser(int theIndex, int& x, int& y)
{
	if (PvzpCustomPlants::IsCustom(static_cast<SeedType>(theIndex)))
	{
		int aSlot = theIndex - int(SEED_CUSTOM_FIRST);
		x = (aSlot % 8) * 53 + 22;
		y = (aSlot / 8) * 70 + 128;
	}
	else if (theIndex == SEED_IMITATER)
	{
		x = mImitaterButton->mX;
		y = mImitaterButton->mY;
	}
	else
	{
		int aRow = theIndex / 8;
		int aCol = theIndex % 8;

		x = aCol * 53 + 22;
		if (Has7Rows())
		{
			y = aRow * 70 + 123;
		}
		else
		{
			y = aRow * 73 + 128;
		}
	}
}

void SeedChooserScreen::GetSeedPositionInBank(int theIndex, int& x, int& y)
{
	x = mBoard->mSeedBank->mX - mX + mBoard->GetSeedPacketPositionX(theIndex);
	y = mBoard->mSeedBank->mY - mY + 8;
}

SeedChooserScreen::~SeedChooserScreen() = default;

unsigned int SeedChooserScreen::SeedNotRecommendedToPick(SeedType theSeedType)
{
	uint aRecFlags = mBoard->SeedNotRecommendedForLevel(theSeedType);
	if (TestBit(aRecFlags, NOT_RECOMMENDED_NOCTURNAL) && PickedPlantType(SEED_INSTANT_COFFEE))
		SetBit(aRecFlags, NOT_RECOMMENDED_NOCTURNAL, false);
	return aRecFlags;
}

bool SeedChooserScreen::SeedNotAllowedToPick(SeedType theSeedType)
{
	return mApp->mGameMode == GAMEMODE_CHALLENGE_LAST_STAND && (theSeedType == SEED_SUNFLOWER || theSeedType == SEED_SUNSHROOM ||
		theSeedType == SEED_TWINSUNFLOWER || theSeedType == SEED_SEASHROOM || theSeedType == SEED_PUFFSHROOM);
}

bool SeedChooserScreen::SeedNotAllowedDuringTrial(SeedType theSeedType)
{
	if (PvzpCustomLevels::IsMode(mApp)) return false; // sandbox availability is not a purchase
	return mApp->IsTrialStageLocked() && (theSeedType == SEED_SQUASH || theSeedType == SEED_THREEPEATER);
}

bool SeedChooserScreen::IsModSeed(SeedType theSeedType)
{
	return PvzpCustomPlants::IsCustom(theSeedType);
}
bool SeedChooserScreen::ChooserHasSeed(SeedType theSeedType) const
{
	if (IsModSeed(theSeedType)) return PvzpCustomPlants::IsSlotUsed(theSeedType);
	return theSeedType >= SEED_PEASHOOTER && theSeedType < NUM_SEEDS_IN_CHOOSER && mApp->HasSeedType(theSeedType);
}
bool SeedChooserScreen::OnChooserPage(SeedType theSeedType) const
{
	return IsModSeed(theSeedType) ? mChooserPage == 1 : mChooserPage == 0;
}
Rect SeedChooserScreen::GetPageButtonRect() const
{
	return Rect(596, 462, 184, 30);
}
SeedType SeedChooserScreen::ModPageSeed(int theIndex)
{
	return theIndex >= 0 && theIndex < ModPageCount() ? static_cast<SeedType>(int(SEED_CUSTOM_FIRST) + theIndex) : SEED_NONE;
}
int SeedChooserScreen::ModPageCount() { return int(NUM_CUSTOM_PLANT_SLOTS); }
void SeedChooserScreen::DrawModPage(Graphics* g)
{
	PvzpDrawString(g, PvzpStringListExists("[ALMANAC_MOD_PLANTS]") ? std::string(PvzpStringTranslate("[ALMANAC_MOD_PLANTS]")) : "Mod plants", 240, 110, Sexy::FONT_DWARVENTODCRAFT18YELLOW, Color(255, 226, 140), DS_ALIGN_CENTER);
	for (int i = 0; i < ModPageCount(); ++i)
	{
		SeedType aSeed = ModPageSeed(i);
		if (!ChooserHasSeed(aSeed)) continue;
		int px = 0, py = 0;
		GetSeedPositionInChooser(aSeed, px, py);
		if (mChosenSeeds[aSeed].mSeedState != SEED_IN_CHOOSER)
			DrawSeedPacket(g, px, py, aSeed, SEED_NONE, 0.0f, 55, true, false);
	}
}

static void DrawChosenSeedPacket(Graphics* g, int x, int y, const ChosenSeed& theSeed, int theGrayness)
{
	DrawSeedPacket(g, x, y, theSeed.mSeedType, theSeed.mImitaterType, 0, theGrayness, true, false);
}

void SeedChooserScreen::Draw(Graphics* g)
{
	if (mApp->GetDialog(DIALOG_STORE) || mApp->GetDialog(DIALOG_ALMANAC))
		return;

	g->SetLinearBlend(true);
	if (!mBoard->ChooseSeedsOnCurrentLevel() || (mBoard->mCutScene && mBoard->mCutScene->IsBeforePreloading()))
		return;

	g->DrawImage(Sexy::IMAGE_SEEDCHOOSER_BACKGROUND, 0, 87);
	if (mApp->HasSeedType(SEED_IMITATER))
	{
		g->DrawImage(Sexy::IMAGE_SEEDCHOOSER_IMITATERADDON, 459, 503);
	}
	// the localization key name is wrong
	PvzpDrawString(g, "[CHOOSE_YOUR_PLANTS]", 229, 110, Sexy::FONT_DWARVENTODCRAFT18YELLOW, Color::White, DS_ALIGN_CENTER);

	if (mChooserPage == 1)
	{
		DrawModPage(g);
	}
	int aNumSeeds = mChooserPage == 1 ? 0 : (Has7Rows() ? 48 : 40);
	for (SeedType aSeedShadow = SEED_PEASHOOTER; aSeedShadow < aNumSeeds; aSeedShadow = (SeedType)(aSeedShadow + 1))
	{
		int x, y;
		GetSeedPositionInChooser(aSeedShadow, x, y);
		if (aSeedShadow == SEED_IMITATER)
		{
			continue;
		}

		if (mApp->HasSeedType(aSeedShadow))
		{
			ChosenSeed& aChosenSeed = mChosenSeeds[aSeedShadow];
			if (aChosenSeed.mSeedState != SEED_IN_CHOOSER)
			{
				DrawSeedPacket(g, x, y, aSeedShadow, SEED_NONE, 0, 55, true, false);
			}
		}
		else
		{
			g->DrawImage(Sexy::IMAGE_SEEDPACKETSILHOUETTE, x, y);
		}
	}

	int aNumSeedsInBank = mBoard->mSeedBank->mNumPackets;
	for (int anIndex = 0; anIndex < aNumSeedsInBank; anIndex++)
	{
		if (FindSeedInBank(anIndex) == SEED_NONE)
		{
			int x, y;
			GetSeedPositionInBank(anIndex, x, y);
			g->DrawImage(Sexy::IMAGE_SEEDPACKETSILHOUETTE, x, y);
		}
	}

	for (SeedType aSeedType = SEED_PEASHOOTER; aSeedType < NUM_SEED_SLOTS; aSeedType = (SeedType)(aSeedType + 1))
	{
		ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
		ChosenSeedState aSeedState = aChosenSeed.mSeedState;
		if (aSeedState == SEED_IN_CHOOSER && !OnChooserPage(aSeedType)) continue;
		if (ChooserHasSeed(aSeedType) && aSeedState != SEED_FLYING_TO_BANK && aSeedState != SEED_FLYING_TO_CHOOSER &&
			aSeedState != SEED_PACKET_HIDDEN && (aSeedState == SEED_IN_CHOOSER || mBoard->mCutScene->mSeedChoosing))
		{
			bool aGrayed = false;
			if (((SeedNotRecommendedToPick(aSeedType) || SeedNotAllowedToPick(aSeedType)) && aSeedState == SEED_IN_CHOOSER) ||
				SeedNotAllowedDuringTrial(aSeedType))
				aGrayed = true;

			int aPosX = aChosenSeed.mX;
			int aPosY = aChosenSeed.mY;
			if (aSeedState == SEED_IN_BANK)
			{
				GetSeedPositionInBank(aChosenSeed.mSeedIndexInBank, aPosX, aPosY);
			}
			DrawChosenSeedPacket(g, aPosX, aPosY, aChosenSeed, aGrayed ? 115 : 255);
		}
	}

	mImitaterButton->Draw(g);
	for (SeedType aSeedType = SEED_PEASHOOTER; aSeedType < NUM_SEED_SLOTS; aSeedType = (SeedType)(aSeedType + 1))
	{
		ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
		ChosenSeedState aSeedState = aChosenSeed.mSeedState;
		if (ChooserHasSeed(aSeedType) && (aSeedState == SEED_FLYING_TO_BANK || aSeedState == SEED_FLYING_TO_CHOOSER))
		{
			DrawChosenSeedPacket(g, aChosenSeed.mX, aChosenSeed.mY, aChosenSeed, 255);
		}
	}

	mStartButton->Draw(g);
	mRandomButton->Draw(g);
	mViewLawnButton->Draw(g);
	mAlmanacButton->Draw(g);
	mStoreButton->Draw(g);
	Graphics aBoardFrameG = Graphics(*g);
	aBoardFrameG.mTransX -= mX;
	aBoardFrameG.mTransY -= mY;
	mMenuButton->Draw(&aBoardFrameG);
	mToolTip->Draw(g);
	{
		Rect aPage = GetPageButtonRect();
		g->SetColor(Color(24, 40, 28, 235));
		g->FillRect(aPage);
		g->SetColor(Color(126, 214, 150));
		g->DrawRect(aPage);
		PvzpDrawString(g, mChooserPage == 0 ? "MOD PLANTS  >" : "<  NORMAL PLANTS",
			aPage.mX + aPage.mWidth / 2, aPage.mY + 23, Sexy::FONT_DWARVENTODCRAFT12, Color::White, DS_ALIGN_CENTER);
	}

}

void SeedChooserScreen::UpdateViewLawn()
{
	if (mChooseState != CHOOSE_VIEW_LAWN) return;
	mViewLawnTime++;
	if (mViewLawnTime == 100) mBoard->DisplayAdviceAgain("[CLICK_TO_CONTINUE]", MESSAGE_STYLE_HINT_STAY, ADVICE_CLICK_TO_CONTINUE);
	else if (mViewLawnTime == 251) mViewLawnTime = 250;

	int aBoardX = BOARD_IMAGE_WIDTH_OFFSET - mApp->mWidth;
	int aSeedChooserY = SEED_CHOOSER_OFFSET_Y - Sexy::IMAGE_SEEDCHOOSER_BACKGROUND->mHeight;
	if (mViewLawnTime <= 100)
	{
		mBoard->Move(-PvzpAnimateCurve(0, 100, mViewLawnTime, aBoardX, 0, CURVE_EASE_IN_OUT), 0);
		Move(0, PvzpAnimateCurve(0, 40, mViewLawnTime, aSeedChooserY, SEED_CHOOSER_OFFSET_Y, CURVE_EASE_IN_OUT));
	}
	else if (mViewLawnTime <= 250)
	{
		mBoard->Move(0, 0);
		Move(0, SEED_CHOOSER_OFFSET_Y);
	}
	else if (mViewLawnTime <= 350)
	{
		mBoard->ClearAdvice(ADVICE_CLICK_TO_CONTINUE);
		mBoard->Move(-PvzpAnimateCurve(250, 350, mViewLawnTime, 0, aBoardX, CURVE_EASE_IN_OUT), 0);
		Move(0, PvzpAnimateCurve(310, 350, mViewLawnTime, SEED_CHOOSER_OFFSET_Y, aSeedChooserY, CURVE_EASE_IN_OUT));
	}
	else
	{
		mChooseState = CHOOSE_NORMAL;
		mViewLawnTime = 0;
		mMenuButton->mDisabled = false;
	}
}

void SeedChooserScreen::LandFlyingSeed(ChosenSeed& theChosenSeed)
{
	if (theChosenSeed.mSeedState == SEED_FLYING_TO_BANK)
	{
		theChosenSeed.mX = theChosenSeed.mEndX;
		theChosenSeed.mY = theChosenSeed.mEndY;
		theChosenSeed.mTimeStartMotion = 0;
		theChosenSeed.mTimeEndMotion = 0;
		theChosenSeed.mSeedState = SEED_IN_BANK;
		mSeedsInFlight--;
	}
	else if (theChosenSeed.mSeedState == SEED_FLYING_TO_CHOOSER)
	{
		theChosenSeed.mX = theChosenSeed.mEndX;
		theChosenSeed.mY = theChosenSeed.mEndY;
		theChosenSeed.mTimeStartMotion = 0;
		theChosenSeed.mTimeEndMotion = 0;
		theChosenSeed.mSeedState = SEED_IN_CHOOSER;
		mSeedsInFlight--;
		if (theChosenSeed.mSeedType == SEED_IMITATER)
		{
			theChosenSeed.mSeedState = SEED_PACKET_HIDDEN;
			theChosenSeed.mImitaterType = SEED_NONE;
			UpdateImitaterButton();
		}
	}
}

void SeedChooserScreen::UpdateCursor()
{
	if (mApp->GetDialogCount() || mBoard->mCutScene->IsInShovelTutorial() || mApp->mGameMode == GAMEMODE_UPSELL) return;
	SeedType aMouseSeedType = SeedHitTest(mLastMouseX, mLastMouseY);
	if (aMouseSeedType != SEED_NONE)
	{
		ChosenSeed& aMouseChosenSeed = mChosenSeeds[aMouseSeedType];
		if (aMouseChosenSeed.mSeedState == SEED_IN_BANK && aMouseChosenSeed.mCrazyDavePicked)
			aMouseSeedType = SEED_NONE;
	}

	if (mMouseVisible && mChooseState != CHOOSE_VIEW_LAWN && ((aMouseSeedType != SEED_NONE && !SeedNotAllowedToPick(aMouseSeedType)) ||
		mRandomButton->IsMouseOver() || mViewLawnButton->IsMouseOver() || mAlmanacButton->IsMouseOver() || mImitaterButton->IsMouseOver() ||
		mStoreButton->IsMouseOver() || mMenuButton->IsMouseOver() || mStartButton->IsMouseOver() ||
		GetPageButtonRect().Contains(mLastMouseX, mLastMouseY)))
		mApp->SetCursor(CURSOR_HAND);
	else
		mApp->SetCursor(CURSOR_POINTER);
}

void SeedChooserScreen::Update()
{
	Widget::Update();

	mLastMouseX = mApp->mWidgetManager->mLastMouseX - mX;
	mLastMouseY = mApp->mWidgetManager->mLastMouseY - mY;

	mSeedChooserAge++;
	mToolTip->Update();

	for (SeedType aSeedType = SEED_PEASHOOTER; aSeedType < NUM_SEED_SLOTS; aSeedType = (SeedType)(aSeedType + 1))
	{
		if (ChooserHasSeed(aSeedType))
		{
			ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
			if (aChosenSeed.mSeedState == SEED_FLYING_TO_BANK || aChosenSeed.mSeedState == SEED_FLYING_TO_CHOOSER)
			{
				int aTimeStart = aChosenSeed.mTimeStartMotion;
				int aTimeEnd = aChosenSeed.mTimeEndMotion;
				aChosenSeed.mX = PvzpAnimateCurve(aTimeStart, aTimeEnd, mSeedChooserAge, aChosenSeed.mStartX, aChosenSeed.mEndX, CURVE_EASE_IN_OUT);
				aChosenSeed.mY = PvzpAnimateCurve(aTimeStart, aTimeEnd, mSeedChooserAge, aChosenSeed.mStartY, aChosenSeed.mEndY, CURVE_EASE_IN_OUT);
				if (mSeedChooserAge >= aChosenSeed.mTimeEndMotion)
				{
					LandFlyingSeed(aChosenSeed);
				}
			}
		}
	}

	ShowToolTip();
	mStartButton->Update();
	mRandomButton->Update();
	mViewLawnButton->Update();
	mAlmanacButton->Update();
	mImitaterButton->Update();
	mStoreButton->Update();
	mMenuButton->Update();
	UpdateViewLawn();
	UpdateCursor();
	MarkDirty();
}

bool SeedChooserScreen::DisplayRepickWarningDialog(const char* theMessage)
{
	return mApp->LawnMessageBox(
		Dialogs::DIALOG_CHOOSER_WARNING,
		"[DIALOG_WARNING]",
		theMessage,
		"[DIALOG_BUTTON_YES]",
		"[REPICK_BUTTON]",
		Dialog::BUTTONS_YES_NO
	) == Dialog::ID_YES;
}

bool SeedChooserScreen::FlyersAreComming()
{
	for (int aWave = 0; aWave < mBoard->mNumWaves; aWave++)
	{
		for (int anIndex = 0; anIndex < MAX_ZOMBIES_IN_WAVE; anIndex++)
		{
			ZombieType aZombieType = mBoard->mZombiesInWave[aWave][anIndex];
			if (aZombieType == ZOMBIE_INVALID)
				break;

			if (aZombieType == ZOMBIE_BALLOON)
				return true;
		}
	}
	return false;
}

bool SeedChooserScreen::FlyProtectionCurrentlyPlanted()
{
	for (Plant* aPlant : mBoard->mPlants)
	{
		if (aPlant->mDead)
			continue;
		if (aPlant->mSeedType == SEED_CATTAIL || aPlant->mSeedType == SEED_CACTUS)
		{
			return true;
		}
	}
	return false;
}

bool SeedChooserScreen::CheckSeedUpgrade(SeedType theSeedTypeTo, SeedType theSeedTypeFrom)
{
	if (mApp->IsSurvivalMode() || !PickedPlantType(theSeedTypeTo) || PickedPlantType(theSeedTypeFrom))
		return true;

	std::string aWarning(PvzpStringTranslate("[SEED_CHOOSER_UPGRADE_WARNING]"));
	aWarning = PvzpReplaceString(aWarning, "{UPGRADE_TO}", Plant::GetNameString(theSeedTypeTo));
	aWarning = PvzpReplaceString(aWarning, "{UPGRADE_FROM}", Plant::GetNameString(theSeedTypeFrom));
	return DisplayRepickWarningDialog(aWarning.c_str());
}

void SeedChooserScreen::OnStartButton()
{
	if (mSeedsInFlight > 0 || mSeedsInBank != mBoard->mSeedBank->mNumPackets) return;
	if (mApp->mGameMode == GAMEMODE_CHALLENGE_SEEING_STARS && !PickedPlantType(SEED_STARFRUIT))
	{
		if (!DisplayRepickWarningDialog("[SEED_CHOOSER_SEEING_STARS_WARNING]"))
		{
			return;
		}
	}

	if (mApp->IsFirstTimeAdventureMode() && mBoard->mLevel == 11 && !PickedPlantType(SEED_PUFFSHROOM))
	{
		if (!DisplayRepickWarningDialog("[SEED_CHOOSER_PUFFSHROOM_WARNING]"))
		{
			return;
		}
	}
	if (!PickedPlantType(SEED_SUNFLOWER) && !PickedPlantType(SEED_TWINSUNFLOWER) && !PickedPlantType(SEED_SUNSHROOM) &&
		!mBoard->mCutScene->IsSurvivalRepick() && mApp->mGameMode != GAMEMODE_CHALLENGE_LAST_STAND)
	{
		if (mApp->IsFirstTimeAdventureMode() && mBoard->mLevel == 11)
		{
			if (!DisplayRepickWarningDialog("[SEED_CHOOSER_NIGHT_SUN_WARNING]"))
			{
				return;
			}
		}
		else if (!DisplayRepickWarningDialog("[SEED_CHOOSER_SUN_WARNING]"))
		{
			return;
		}
	}
	if (mBoard->StageHasPool() && !PickedPlantType(SEED_LILYPAD) && !PickedPlantType(SEED_SEASHROOM) && !PickedPlantType(SEED_TANGLEKELP) && !mBoard->mCutScene->IsSurvivalRepick())
	{
		if (mApp->IsFirstTimeAdventureMode() && mBoard->mLevel == 21)
		{
			if (!DisplayRepickWarningDialog("[SEED_CHOOSER_LILY_WARNING]"))
			{
				return;
			}
		}
		else if (!DisplayRepickWarningDialog("[SEED_CHOOSER_POOL_WARNING]"))
		{
			return;
		}
	}
	if (mBoard->StageHasRoof() && !PickedPlantType(SEED_FLOWERPOT) && mApp->HasSeedType(SEED_FLOWERPOT))
	{
		if (!DisplayRepickWarningDialog("[SEED_CHOOSER_ROOF_WARNING]"))
		{
			return;
		}
	}

	if (mApp->mGameMode == GAMEMODE_CHALLENGE_ART_CHALLENGE_WALLNUT && !PickedPlantType(SEED_WALLNUT))
	{
		if (!DisplayRepickWarningDialog("[SEED_CHOOSER_ART_WALLNUT_WARNING]"))
		{
			return;
		}
	}
	if (mApp->mGameMode == GAMEMODE_CHALLENGE_ART_CHALLENGE_SUNFLOWER &&
		(!PickedPlantType(SEED_STARFRUIT) || !PickedPlantType(SEED_UMBRELLA) || !PickedPlantType(SEED_WALLNUT)))
	{
		if (!DisplayRepickWarningDialog("[SEED_CHOOSER_ART_2_WARNING]"))
		{
			return;
		}
	}

	if (FlyersAreComming() && !FlyProtectionCurrentlyPlanted() && !PickedPlantType(SEED_CATTAIL) && !PickedPlantType(SEED_CACTUS) && !PickedPlantType(SEED_BLOVER))
	{
		if (!DisplayRepickWarningDialog("[SEED_CHOOSER_FLYER_WARNING]"))
		{
			return;
		}
	}

	if (!CheckSeedUpgrade(SEED_GATLINGPEA, SEED_REPEATER) ||
		!CheckSeedUpgrade(SEED_WINTERMELON, SEED_MELONPULT) ||
		!CheckSeedUpgrade(SEED_TWINSUNFLOWER, SEED_SUNFLOWER) ||
		!CheckSeedUpgrade(SEED_SPIKEROCK, SEED_SPIKEWEED) ||
		!CheckSeedUpgrade(SEED_COBCANNON, SEED_KERNELPULT) ||
		!CheckSeedUpgrade(SEED_GOLD_MAGNET, SEED_MAGNETSHROOM) ||
		!CheckSeedUpgrade(SEED_GLOOMSHROOM, SEED_FUMESHROOM) ||
		!CheckSeedUpgrade(SEED_CATTAIL, SEED_LILYPAD))
		return;

	CloseSeedChooser();
}

void SeedChooserScreen::PickRandomSeeds()
{
	for (int i = 0; i < int(NUM_SEED_SLOTS); ++i) LandFlyingSeed(mChosenSeeds[i]);
	std::vector<SeedType> aCandidates;
	for (int i = 0; i < int(NUM_SEED_SLOTS); ++i)
	{
		SeedType aSeedType = static_cast<SeedType>(i);
		if (ChooserHasSeed(aSeedType) && aSeedType != SEED_IMITATER &&
			mChosenSeeds[i].mSeedState == SEED_IN_CHOOSER &&
			!SeedNotAllowedToPick(aSeedType) && !SeedNotAllowedDuringTrial(aSeedType))
			aCandidates.push_back(aSeedType);
	}
	if (int(aCandidates.size()) < mBoard->mSeedBank->mNumPackets - mSeedsInBank)
	{
		mApp->PlaySample(Sexy::SOUND_BUZZER);
		return;
	}
	for (int anIndex = mSeedsInBank; anIndex < mBoard->mSeedBank->mNumPackets; ++anIndex)
	{
		int aPick = Rand(static_cast<int>(aCandidates.size()));
		SeedType aSeedType = aCandidates[aPick];
		aCandidates.erase(aCandidates.begin() + aPick);
		ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
		GetSeedPositionInBank(anIndex, aChosenSeed.mX, aChosenSeed.mY);
		aChosenSeed.mTimeStartMotion = aChosenSeed.mTimeEndMotion = 0;
		aChosenSeed.mStartX = aChosenSeed.mEndX = aChosenSeed.mX;
		aChosenSeed.mStartY = aChosenSeed.mEndY = aChosenSeed.mY;
		aChosenSeed.mSeedState = SEED_IN_BANK;
		aChosenSeed.mSeedIndexInBank = anIndex;
		++mSeedsInBank;
	}
	CloseSeedChooser();
}

void SeedChooserScreen::ButtonDepress(int theId)
{
	if (mSeedsInFlight > 0 || mChooseState == CHOOSE_VIEW_LAWN || !mMouseVisible)
		return;

	if (theId == SeedChooserScreen::SeedChooserScreen_ViewLawn)
	{
		mChooseState = CHOOSE_VIEW_LAWN;
		mMenuButton->mDisabled = true;
		mViewLawnTime = 0;
	}
	else if (theId == SeedChooserScreen::SeedChooserScreen_Almanac)
	{
		mApp->DoAlmanacDialog()->WaitForResult(true);
		mApp->mMusic->MakeSureMusicIsPlaying(MUSIC_TUNE_CHOOSE_YOUR_SEEDS);
	}
	else if (theId == SeedChooserScreen::SeedChooserScreen_Store)
	{
		StoreScreen* aStore = mApp->ShowStoreScreen();
		aStore->mBackButton->SetLabel("[STORE_BACK_TO_GAME]");
		aStore->WaitForResult();
		if (aStore->mGoToTreeNow)
		{
			mApp->KillBoard();
			mApp->PreNewGame(GAMEMODE_TREE_OF_WISDOM, false);
		}
		else mApp->mMusic->MakeSureMusicIsPlaying(MUSIC_TUNE_CHOOSE_YOUR_SEEDS);
	}
	else if (theId == SeedChooserScreen::SeedChooserScreen_Menu)
	{
		mMenuButton->mIsOver = false;
		mMenuButton->mIsDown = false;
		UpdateCursor();
		mApp->DoNewOptions(false);
	}
	else
	{
		if (theId == SeedChooserScreen::SeedChooserScreen_Start)
			OnStartButton();
		else if (theId == SeedChooserScreen::SeedChooserScreen_Random)
			PickRandomSeeds();
	}
}

SeedType SeedChooserScreen::SeedHitTest(int x, int y)
{
	if (!mMouseVisible) return SEED_NONE;
	for (int aIdx = 0; aIdx < int(NUM_SEED_SLOTS); ++aIdx)
	{
		SeedType aSeedType = static_cast<SeedType>(aIdx);
		if (!ChooserHasSeed(aSeedType)) continue;
		ChosenSeed& aChosenSeed = mChosenSeeds[aIdx];
		int aX = aChosenSeed.mX, aY = aChosenSeed.mY;
		if (aChosenSeed.mSeedState == SEED_IN_BANK)
			GetSeedPositionInBank(aChosenSeed.mSeedIndexInBank, aX, aY);
		else if (aChosenSeed.mSeedState != SEED_IN_CHOOSER || !OnChooserPage(aSeedType))
			continue;
		if (Rect(aX, aY, SEED_PACKET_WIDTH, SEED_PACKET_HEIGHT).Contains(x, y)) return aSeedType;
	}
	return SEED_NONE;
}

SeedType SeedChooserScreen::FindSeedInBank(int theIndexInBank)
{
	for (SeedType aSeedType = SEED_PEASHOOTER; aSeedType < NUM_SEED_SLOTS; aSeedType = (SeedType)(aSeedType + 1))
	{
		if (ChooserHasSeed(aSeedType))
		{
			ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
			if (aChosenSeed.mSeedState == SEED_IN_BANK && aChosenSeed.mSeedIndexInBank == theIndexInBank) return aSeedType;
		}
	}
	return SEED_NONE;
}

void SeedChooserScreen::EnableStartButton(bool theEnabled)
{
	mStartButton->SetDisabled(!theEnabled);
	if (theEnabled) mStartButton->SetLabelColor(Color::White);
	else mStartButton->SetLabelColor(Color(64, 64, 64));
}

void SeedChooserScreen::ClickedSeedInBank(ChosenSeed& theChosenSeed)
{
	if (theChosenSeed.mSeedState != SEED_IN_BANK || theChosenSeed.mCrazyDavePicked || mSeedsInFlight > 0)
		return;
	for (int anIndex = theChosenSeed.mSeedIndexInBank + 1; anIndex < mBoard->mSeedBank->mNumPackets; anIndex++)
	{
		SeedType aSeedType = FindSeedInBank(anIndex);
		if (aSeedType != SEED_NONE)
		{
			ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
			aChosenSeed.mTimeStartMotion = mSeedChooserAge;
			aChosenSeed.mTimeEndMotion = mSeedChooserAge + 15;
			GetSeedPositionInBank(anIndex, aChosenSeed.mStartX, aChosenSeed.mStartY);
			aChosenSeed.mX = aChosenSeed.mStartX;
			aChosenSeed.mY = aChosenSeed.mStartY;
			GetSeedPositionInBank(anIndex - 1, aChosenSeed.mEndX, aChosenSeed.mEndY);
			aChosenSeed.mSeedState = SEED_FLYING_TO_BANK;
			aChosenSeed.mSeedIndexInBank = anIndex - 1;
			mSeedsInFlight++;
		}
	}
	theChosenSeed.mTimeStartMotion = mSeedChooserAge;
	theChosenSeed.mTimeEndMotion = mSeedChooserAge + 25;
	GetSeedPositionInBank(theChosenSeed.mSeedIndexInBank, theChosenSeed.mStartX, theChosenSeed.mStartY);
	theChosenSeed.mX = theChosenSeed.mStartX;
	theChosenSeed.mY = theChosenSeed.mStartY;
	GetSeedPositionInChooser(theChosenSeed.mSeedType, theChosenSeed.mEndX, theChosenSeed.mEndY);
	theChosenSeed.mSeedState = SEED_FLYING_TO_CHOOSER;
	theChosenSeed.mSeedIndexInBank = -1;
	mSeedsInBank--;
	mSeedsInFlight++;
	RemoveToolTip();
	EnableStartButton(false);
	mApp->PlaySample(Sexy::SOUND_TAP);
}

void SeedChooserScreen::ClickedSeedInChooser(ChosenSeed& theChosenSeed)
{
	if (theChosenSeed.mSeedState != SEED_IN_CHOOSER || !ChooserHasSeed(theChosenSeed.mSeedType) ||
		SeedNotAllowedToPick(theChosenSeed.mSeedType) || SeedNotAllowedDuringTrial(theChosenSeed.mSeedType) ||
		mSeedsInBank >= mBoard->mSeedBank->mNumPackets)
		return;

	theChosenSeed.mTimeStartMotion = mSeedChooserAge;
	theChosenSeed.mTimeEndMotion = mSeedChooserAge + 25;
	theChosenSeed.mStartX = theChosenSeed.mX;
	theChosenSeed.mStartY = theChosenSeed.mY;
	GetSeedPositionInBank(mSeedsInBank, theChosenSeed.mEndX, theChosenSeed.mEndY);
	theChosenSeed.mSeedState = SEED_FLYING_TO_BANK;
	theChosenSeed.mSeedIndexInBank = mSeedsInBank;
	mSeedsInFlight++;
	mSeedsInBank++;

	RemoveToolTip();
	mApp->PlaySample(Sexy::SOUND_TAP);
	if (mSeedsInBank == mBoard->mSeedBank->mNumPackets)
		EnableStartButton(true);
}

void SeedChooserScreen::ShowToolTip()
{
	if (!mApp->mWidgetManager->mMouseIn || !mApp->mActive || mApp->GetDialogCount() > 0 || mChooseState == CHOOSE_VIEW_LAWN)
	{
		RemoveToolTip();
	}
	else if (mSeedsInFlight <= 0)
	{
		if (mImitaterButton->IsMouseOver() && mMouseVisible)
		{
			mToolTipSeed = SEED_NONE;
			mToolTip->SetLabel(Plant::GetToolTip(SEED_IMITATER));
			mToolTip->SetTitle(Plant::GetNameString(SEED_IMITATER));
			mToolTip->SetWarningText("");
			mToolTip->mX = (SEED_PACKET_WIDTH - mToolTip->mWidth) / 2 + mImitaterButton->mX;
			mToolTip->mY = mImitaterButton->mY - mToolTip->mHeight;
			mToolTip->mVisible = true;
		}
		else
		{
			SeedType aSeedType = SeedHitTest(mLastMouseX, mLastMouseY);
			if (aSeedType == SEED_NONE)
			{
				RemoveToolTip();
			}
			else if (aSeedType != mToolTipSeed)
			{
				RemoveToolTip();
				ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
				if (PvzpCustomPlants::IsCustom(aSeedType))
				{
					int aSlot = int(aSeedType) - int(SEED_CUSTOM_FIRST);
					std::string aTitle = "MOD PLANT";
					for (const auto& cp : PvzpMod::CustomPlants()) if (cp.slot == aSlot) aTitle = cp.name;
					int aCX = 0, aCY = 0;
					if (aChosenSeed.mSeedState == SEED_IN_BANK)
						GetSeedPositionInBank(aChosenSeed.mSeedIndexInBank, aCX, aCY);
					else
						GetSeedPositionInChooser(aSeedType, aCX, aCY);
					mToolTip->SetWarningText("");
					mToolTip->SetTitle(aTitle.c_str());
					mToolTip->SetLabel("A mod-defined plant.");
					mToolTip->mX = std::clamp((SEED_PACKET_WIDTH - mToolTip->mWidth) / 2 + aCX, 0, BOARD_WIDTH - mToolTip->mWidth);
					mToolTip->mY = aCY + 70;
					mToolTip->mVisible = true;
					mToolTipSeed = aSeedType;
					return;
				}
				uint aRecFlags = SeedNotRecommendedToPick(aSeedType);
				if (SeedNotAllowedToPick(aSeedType))
				{
					mToolTip->SetWarningText("[NOT_ALLOWED_ON_THIS_LEVEL]");
				}
				else if (SeedNotAllowedDuringTrial(aSeedType))
				{
					mToolTip->SetWarningText("[FULL_VERSION_ONLY]");
				}
				else if (aChosenSeed.mSeedState == SEED_IN_BANK && aChosenSeed.mCrazyDavePicked)
				{
					mToolTip->SetWarningText("[CRAZY_DAVE_WANTS]");
				}
				else if (aRecFlags != 0U)
				{
					if (TestBit(aRecFlags, NOT_RECOMMENDED_NOCTURNAL))
					{
						mToolTip->SetWarningText("[NOCTURNAL_WARNING]");
					}
					else
					{
						mToolTip->SetWarningText("[NOT_RECOMMENDED_FOR_LEVEL]");
					}
				}
				else
				{
					mToolTip->SetWarningText("");
				}

				if (aSeedType == SEED_IMITATER)
				{
					mToolTip->SetTitle(Plant::GetNameString(aSeedType, aChosenSeed.mImitaterType));
					mToolTip->SetLabel(Plant::GetToolTip(aChosenSeed.mImitaterType));
				}
				else
				{
					mToolTip->SetTitle(Plant::GetNameString(aSeedType, SEED_NONE));
					mToolTip->SetLabel(Plant::GetToolTip(aSeedType));
				}

				int aSeedX, aSeedY;
				if (aChosenSeed.mSeedState == SEED_IN_BANK)
				{
					GetSeedPositionInBank(aChosenSeed.mSeedIndexInBank, aSeedX, aSeedY);
				}
				else
				{
					GetSeedPositionInChooser(aSeedType, aSeedX, aSeedY);
				}

				mToolTip->mX = std::clamp((SEED_PACKET_WIDTH - mToolTip->mWidth) / 2 + aSeedX, 0, BOARD_WIDTH - mToolTip->mWidth);
				mToolTip->mY = aSeedY + 70;
				mToolTip->mVisible = true;
				mToolTipSeed = aSeedType;
			}
		}
	}
}

void SeedChooserScreen::RemoveToolTip()
{
	mToolTip->mVisible = false;
	mToolTipSeed = SEED_NONE;
}

void SeedChooserScreen::CancelLawnView()
{
	if (mChooseState == CHOOSE_VIEW_LAWN && mViewLawnTime > 100 && mViewLawnTime <= 250) mViewLawnTime = 251;
}

void SeedChooserScreen::MouseUp([[maybe_unused]] int x, [[maybe_unused]] int y, int theClickCount)
{
	if (theClickCount == 1)
	{
		if (mMenuButton->IsMouseOver()) ButtonDepress(SeedChooserScreen::SeedChooserScreen_Menu);
		else if (mStartButton->IsMouseOver()) ButtonDepress(SeedChooserScreen::SeedChooserScreen_Start);
		else if (mAlmanacButton->IsMouseOver()) ButtonDepress(SeedChooserScreen::SeedChooserScreen_Almanac);
		else if (mStoreButton->IsMouseOver()) ButtonDepress(SeedChooserScreen::SeedChooserScreen_Store);
	}
}

void SeedChooserScreen::UpdateImitaterButton()
{
	if (!mApp->HasSeedType(SEED_IMITATER))
	{
		mImitaterButton->mBtnNoDraw = true;
		mImitaterButton->mDisabled = true;
	}
	else
	{
		mImitaterButton->mBtnNoDraw = false;
		mImitaterButton->mDisabled = mChosenSeeds[SEED_IMITATER].mSeedState != SEED_PACKET_HIDDEN;
	}
}

void SeedChooserScreen::MouseDown(int x, int y, int theClickCount)
{
	Widget::MouseDown(x, y, theClickCount);

	if (!mMouseVisible || mApp->GetDialogCount() > 0) return;
	if (mChooseState == CHOOSE_VIEW_LAWN)
	{
		CancelLawnView();
		return;
	}

	if (GetPageButtonRect().Contains(x, y))
	{
		for (int i = 0; i < int(NUM_SEED_SLOTS); ++i)
			LandFlyingSeed(mChosenSeeds[i]);
		mChooserPage = mChooserPage == 0 ? 1 : 0;
		mApp->PlaySample(Sexy::SOUND_TAP);
		RemoveToolTip();
		return;
	}

	if (mSeedsInFlight > 0)
	{
		for (int i = 0; i < int(NUM_SEED_SLOTS); i++)
		{
			LandFlyingSeed(mChosenSeeds[i]);
		}
	}

	if (mRandomButton->IsMouseOver())
	{
		mApp->PlaySample(Sexy::SOUND_TAP);
		ButtonDepress(SeedChooserScreen::SeedChooserScreen_Random);
	}
	else if (mViewLawnButton->IsMouseOver())
	{
		mApp->PlaySample(Sexy::SOUND_TAP);
		ButtonDepress(SeedChooserScreen::SeedChooserScreen_ViewLawn);
	}
	else if (mMenuButton->IsMouseOver())
	{
		mApp->PlaySample(Sexy::SOUND_GRAVEBUTTON);
	}
	else if (mStartButton->IsMouseOver() || mAlmanacButton->IsMouseOver() || mStoreButton->IsMouseOver())
	{
		mApp->PlaySample(Sexy::SOUND_TAP);
	}
	else if (mImitaterButton->IsMouseOver())
	{
		if (mSeedsInBank != mBoard->mSeedBank->mNumPackets)
		{
			mApp->PlaySample(Sexy::SOUND_TAP);
			ImitaterDialog* aDialog = new ImitaterDialog();
			mApp->AddDialog(aDialog->mId, aDialog);
			aDialog->Resize((mWidth - aDialog->mWidth) / 2, (mHeight - aDialog->mHeight) / 2, aDialog->mWidth, aDialog->mHeight);
			mApp->mWidgetManager->SetFocus(aDialog);
		}
	}
	else
	{
		if (!mBoard->mSeedBank->ContainsPoint(x, y) && !mAlmanacButton->IsMouseOver() && !mStoreButton->IsMouseOver() && mApp->CanShowAlmanac())
		{
			Zombie* aZombie = mBoard->ZombieHitTest(x - mBoard->mX, y - mBoard->mY);
			if (aZombie && aZombie->mFromWave == Zombie::ZOMBIE_WAVE_CUTSCENE && aZombie->mZombieType != ZOMBIE_REDEYE_GARGANTUAR)
			{
				mApp->PlaySample(Sexy::SOUND_TAP);
				mApp->DoAlmanacDialog(SEED_NONE, aZombie->mZombieType)->WaitForResult(true);
				mApp->mMusic->MakeSureMusicIsPlaying(MUSIC_TUNE_CHOOSE_YOUR_SEEDS);
				return;
			}
		}

		SeedType aSeedType = SeedHitTest(x, y);
		if (aSeedType != SEED_NONE && !SeedNotAllowedToPick(aSeedType))
		{
			if (SeedNotAllowedDuringTrial(aSeedType))
			{
				mApp->PlaySample(Sexy::SOUND_TAP);
				if (mApp->LawnMessageBox(
					DIALOG_MESSAGE,
					"[GET_FULL_VERSION_TITLE]",
					"[GET_FULL_VERSION_BODY]",
					"[GET_FULL_VERSION_YES_BUTTON]",
					"[GET_FULL_VERSION_NO_BUTTON]",
					Dialog::BUTTONS_YES_NO
				) == Dialog::ID_YES)
				{
					mApp->DoBackToMain();
				}
			}
			else
			{
				ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
				if (aChosenSeed.mSeedState == SEED_IN_BANK)
				{
					if (aChosenSeed.mCrazyDavePicked)
					{
						mApp->PlaySample(Sexy::SOUND_BUZZER);
						mToolTip->FlashWarning();
					}
					else ClickedSeedInBank(aChosenSeed);
				}
				else if (aChosenSeed.mSeedState == SEED_IN_CHOOSER)
					ClickedSeedInChooser(aChosenSeed);
			}
		}
	}
}

bool SeedChooserScreen::PickedPlantType(SeedType theSeedType)
{
	for (SeedType aSeedType = SEED_PEASHOOTER; aSeedType < NUM_SEED_SLOTS; aSeedType = (SeedType)(aSeedType + 1))
	{
		ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
		if (aChosenSeed.mSeedState == SEED_IN_BANK)
		{
			if (aChosenSeed.mSeedType == theSeedType || (aChosenSeed.mSeedType == SEED_IMITATER && aChosenSeed.mImitaterType == theSeedType))
			{
				return true;
			}
		}
	}
	return false;
}

void SeedChooserScreen::CloseSeedChooser()
{
	DBG_ASSERT(mBoard->mSeedBank->mNumPackets == mBoard->GetNumSeedsInBank());
	if (mSeedsInFlight > 0 || mSeedsInBank != mBoard->mSeedBank->mNumPackets) return;
	// Validate the complete selection before indexing or mutating any packet.
	for (int anIndex = 0; anIndex < mBoard->mSeedBank->mNumPackets; ++anIndex)
		if (FindSeedInBank(anIndex) == SEED_NONE) return;
	for (int anIndex = 0; anIndex < mBoard->mSeedBank->mNumPackets; anIndex++)
	{
		SeedType aSeedType = FindSeedInBank(anIndex);
		ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
		SeedPacket& aSeedPacket = mBoard->mSeedBank->mSeedPackets[anIndex];
		aSeedPacket.SetPacketType(aSeedType, aChosenSeed.mImitaterType);
		if (aChosenSeed.mRefreshing)
		{
			aSeedPacket.mRefreshCounter = aChosenSeed.mRefreshCounter;
			aSeedPacket.mRefreshTime = Plant::GetRefreshTime(aSeedPacket.mPacketType, aSeedPacket.mImitaterType);
			aSeedPacket.mRefreshing = true;
			aSeedPacket.mActive = false;
		}
	}
	mBoard->mCutScene->EndSeedChooser();
}

void SeedChooserScreen::KeyDown(KeyCode theKey)
{
	mBoard->DoTypingCheck(theKey);

	if (mChooseState == CHOOSE_VIEW_LAWN && (theKey == KeyCode::KEYCODE_SPACE || theKey == KeyCode::KEYCODE_RETURN || theKey == KeyCode::KEYCODE_ESCAPE))
	{
		CancelLawnView();
	}
	else if (theKey == KeyCode::KEYCODE_ESCAPE)
	{
		ButtonDepress(SeedChooserScreen::SeedChooserScreen_Menu);
	}
}

void SeedChooserScreen::KeyChar(char theChar)
{
	mBoard->KeyChar(theChar);
}

void SeedChooserScreen::UpdateAfterPurchase()
{
	for (SeedType aSeedType = SEED_PEASHOOTER; aSeedType < NUM_SEED_SLOTS; aSeedType = (SeedType)(aSeedType + 1))
	{
		ChosenSeed& aChosenSeed = mChosenSeeds[aSeedType];
		if (aChosenSeed.mSeedState == SEED_IN_BANK)
			GetSeedPositionInBank(aChosenSeed.mSeedIndexInBank, aChosenSeed.mX, aChosenSeed.mY);
		else if (aChosenSeed.mSeedState == SEED_IN_CHOOSER)
			GetSeedPositionInChooser(aSeedType, aChosenSeed.mX, aChosenSeed.mY);
		else continue;
		aChosenSeed.mStartX = aChosenSeed.mX;
		aChosenSeed.mStartY = aChosenSeed.mY;
		aChosenSeed.mEndX = aChosenSeed.mX;
		aChosenSeed.mEndY = aChosenSeed.mY;
	}
	EnableStartButton(mSeedsInBank == mBoard->mSeedBank->mNumPackets);
	UpdateImitaterButton();
}
