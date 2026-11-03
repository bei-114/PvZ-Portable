// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "LawnDialog.h"
#include "GameButton.h"
#include <array>
#include <memory>
#include <string>
class CustomLevelResultDialog : public LawnDialog {
public:
    enum { SelectLevel=18020, NextLevel=18021 };
    CustomLevelResultDialog(LawnApp* app,std::string levelId,bool won,bool error);
    ~CustomLevelResultDialog() override;
    void AddedToManager(Sexy::WidgetManager* manager) override;
    void RemovedFromManager(Sexy::WidgetManager* manager) override;
    void Update() override;
    void Draw(Sexy::Graphics* graphics) override;
    void ButtonDepress(int id) override;
    void KeyDown(Sexy::KeyCode key) override;
    Sexy::Rect ButtonRect(int id) const;
private:
    std::string mLevelId,mNextId,mName,mDescription;
    bool mWon=false,mError=false;
    int mWaves=0,mTotalWaves=0,mSpawned=0,mSun=0;
    uint64_t mTicks=0,mLocaleGeneration=0;
    std::array<std::unique_ptr<LawnStoneButton>,4> mButtons;
    void Refresh();
};
