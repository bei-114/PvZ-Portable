// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "LawnDialog.h"
#include "GameButton.h"
#include "CustomLevelResultDialog.h"
#include <array>
#include <string>
#include <string_view>

// Both layouts use native widgets and retain a stable selected catalog ID.
class CustomLevelDialog : public LawnDialog
{
public:
    enum ButtonId { Previous=110, Next=111, Play=112, Close=113, Resume=114,
        Editor=115, EditCopy=116, ToggleLayout=117, Card0=120, Card1=121,
        Card2=122, Card3=123, Card4=124, Card5=125 };
    enum class LayoutMode { Detail=0, Cards=1 };
    explicit CustomLevelDialog(LawnApp* app);
    ~CustomLevelDialog() override;
    void AddedToManager(Sexy::WidgetManager* manager) override;
    void RemovedFromManager(Sexy::WidgetManager* manager) override;
    void Draw(Sexy::Graphics* graphics) override;
    void Update() override;
    void ButtonDepress(int id) override;
    void KeyDown(Sexy::KeyCode key) override;
    void SelectLevel(std::string_view id);
    const std::string& SelectedId() const { return mSelectedId; }
    LayoutMode Layout() const { return mLayout; }
    size_t Page() const { return mLayout==LayoutMode::Cards ? mGridPage : mPage; }
    LawnStoneButton* Button(int id) const;
private:
    std::array<std::unique_ptr<LawnStoneButton>, 8> mButtons;
    std::array<std::unique_ptr<LawnStoneButton>, 6> mCards;
    std::string mHeaderText, mSelectedId, mName, mDescription, mSummary, mRules, mCounter, mEmpty;
    size_t mPage = 0, mGridPage = 0;
    LayoutMode mLayout = LayoutMode::Detail;
    uint64_t mLocaleGeneration = 0, mCatalogRevision = 0;
    void Refresh();
    void Arrange();
};

