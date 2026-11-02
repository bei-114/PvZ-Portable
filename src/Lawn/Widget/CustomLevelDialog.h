// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "LawnDialog.h"
#include "GameButton.h"
#include <array>
#include <string>

// One catalog item per page keeps arbitrarily long catalogs accessible without
// introducing a second widget toolkit or overlapping translated level names.
class CustomLevelDialog : public LawnDialog
{
public:
    explicit CustomLevelDialog(LawnApp* app);
    ~CustomLevelDialog() override;
    void AddedToManager(Sexy::WidgetManager* manager) override;
    void RemovedFromManager(Sexy::WidgetManager* manager) override;
    void Draw(Sexy::Graphics* graphics) override;
    void Update() override;
    void ButtonDepress(int id) override;
    void KeyDown(Sexy::KeyCode key) override;
private:
    std::array<std::unique_ptr<LawnStoneButton>, 5> mButtons;
    std::string mSelectedId;
    size_t mPage = 0;
    uint64_t mLocaleGeneration = 0, mCatalogRevision = 0;
    void Refresh();
};

class CustomLevelResultDialog : public LawnDialog
{
public:
    CustomLevelResultDialog(LawnApp* app, std::string levelId, bool won, bool error);
    ~CustomLevelResultDialog() override;
    void ButtonDepress(int id) override;
    void KeyDown(Sexy::KeyCode key) override;
private:
    std::string mLevelId;
};
