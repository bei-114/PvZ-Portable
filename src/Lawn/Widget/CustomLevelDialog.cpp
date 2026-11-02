// SPDX-License-Identifier: LGPL-3.0-or-later
#include "CustomLevelDialog.h"
#include "../../PvzpLib/PvzpCustomLevels.h"
#include "../../PvzpLib/PvzpMod.h"
#include "../../PvzpLib/PvzpLocale.h"
#include "../../LawnApp.h"
#include "../../Resources.h"
#include "../../GameConstants.h"
#include "graphics/Graphics.h"
#include "graphics/Image.h"
#include "widget/WidgetManager.h"
#include <algorithm>

namespace
{
enum { Previous = 110, Next, Play, Close, Resume };

std::string Label(LawnApp* app, const char* key, const char* en, const char* zh)
{
    return PvzpCustomLevels::Text(app, key, en, zh);
}

// LawnDialog paints whole native stone tiles; choose a size that contains only
// whole tiles rather than stretching artwork or placing buttons past its edge.
void PickerSize(CustomLevelDialog* dialog)
{
    const int baseWidth = Sexy::IMAGE_DIALOG_TOPLEFT->mWidth + Sexy::IMAGE_DIALOG_TOPRIGHT->mWidth;
    const int tileWidth = Sexy::IMAGE_DIALOG_TOPMIDDLE->mWidth;
    const int width = baseWidth + std::max(1, (660 - baseWidth + tileWidth - 1) / tileWidth) * tileWidth;
    const int baseHeight = Sexy::IMAGE_DIALOG_TOPLEFT->mHeight + Sexy::IMAGE_DIALOG_BOTTOMLEFT->mHeight + 45;
    const int tileHeight = Sexy::IMAGE_DIALOG_CENTERLEFT->mHeight;
    const int height = baseHeight + std::max(0, (560 - baseHeight + tileHeight - 1) / tileHeight) * tileHeight;
    dialog->Resize((BOARD_WIDTH - width) / 2, (BOARD_HEIGHT - height) / 2, width, height);
}
}

CustomLevelDialog::CustomLevelDialog(LawnApp* app) : LawnDialog(app, PvzpCustomLevels::PickerDialogId,
    true, Label(app, "CUSTOM_LEVELS", "Custom Levels", "自定义关卡"), "", "", Sexy::Dialog::BUTTONS_NONE)
{
    mButtons[0] = MakeButton(Previous, this, "");
    mButtons[1] = MakeButton(Next, this, "");
    mButtons[2] = MakeButton(Play, this, "");
    mButtons[3] = MakeButton(Close, this, "");
    mButtons[4] = MakeButton(Resume, this, "");
    PickerSize(this);
    const int left = 48, gap = 16, width = (mWidth - left * 2 - gap) / 2;
    const int height = Sexy::IMAGE_BUTTON_LEFT->mHeight;
    mButtons[0]->Resize(left, mHeight - height * 3 - 58, width, height);
    mButtons[1]->Resize(left + width + gap, mHeight - height * 3 - 58, width, height);
    mButtons[2]->Resize(left, mHeight - height * 2 - 44, width, height);
    mButtons[3]->Resize(left + width + gap, mHeight - height * 2 - 44, width, height);
    mButtons[4]->Resize(left, mHeight - height - 30, width * 2 + gap, height);
    Refresh();
}

CustomLevelDialog::~CustomLevelDialog() = default;

void CustomLevelDialog::Refresh()
{
    mLocaleGeneration = PvzpLocaleGetGeneration();
    mCatalogRevision = PvzpMod::SnapshotRevision();
    mDialogHeader = Label(mApp, "CUSTOM_LEVELS", "Custom Levels", "自定义关卡");
    mButtons[0]->SetLabel(Label(mApp, "CUSTOM_LEVEL_PREVIOUS", "Previous", "上一关"));
    mButtons[1]->SetLabel(Label(mApp, "CUSTOM_LEVEL_NEXT_BUTTON", "Next", "下一关"));
    mButtons[2]->SetLabel(Label(mApp, "CUSTOM_LEVEL_PLAY_NEW", "Start New Level", "开始新关卡"));
    mButtons[3]->SetLabel(Label(mApp, "CUSTOM_LEVEL_CLOSE", "Back", "返回"));
    mButtons[4]->SetLabel(Label(mApp, "CUSTOM_LEVEL_CONTINUE", "Continue last custom level", "继续上次的自定义关卡"));
    mButtons[4]->SetVisible(PvzpCustomLevels::CanContinue(mApp));
    mButtons[4]->SetDisabled(!mButtons[4]->mVisible);
    const auto& levels = PvzpMod::CustomLevels();
    mPage = levels.empty() ? 0 : std::min(mPage, levels.size() - 1);
    mSelectedId = levels.empty() ? "" : levels[mPage].id;
    mButtons[0]->SetDisabled(levels.size() < 2 || mPage == 0);
    mButtons[1]->SetDisabled(levels.size() < 2 || mPage + 1 >= levels.size());
    mButtons[2]->SetDisabled(levels.empty());
}

void CustomLevelDialog::AddedToManager(Sexy::WidgetManager* manager)
{
    LawnDialog::AddedToManager(manager);
    for (auto& button : mButtons) AddWidget(button.get());
}

void CustomLevelDialog::RemovedFromManager(Sexy::WidgetManager* manager)
{
    for (auto& button : mButtons) RemoveWidget(button.get());
    LawnDialog::RemovedFromManager(manager);
}

void CustomLevelDialog::Update()
{
    LawnDialog::Update();
    if (mLocaleGeneration != PvzpLocaleGetGeneration() || mCatalogRevision != PvzpMod::SnapshotRevision()) Refresh();
}

void CustomLevelDialog::Draw(Sexy::Graphics* g)
{
    LawnDialog::Draw(g);
    const auto& levels = PvzpMod::CustomLevels();
    const int left = 52, width = mWidth - 104;
    g->SetColor(Sexy::Color(255, 235, 170));
    g->SetFont(Sexy::FONT_BRIANNETOD16);
    if (levels.empty() || mPage >= levels.size())
    {
        g->WriteWordWrapped(Sexy::Rect(left, 175, width, 130), Label(mApp, "CUSTOM_LEVEL_EMPTY",
            "No custom levels are installed. Enable a content pack and restart the game.",
            "尚未安装自定义关卡。请启用关卡内容包并重新启动游戏。"));
        return;
    }
    const auto& level = levels[mPage];
    const std::string name(PvzpLocaleTranslateLiteral(level.name));
    g->PushState();
    g->ClipRect(left, 140, width, 58);
    g->WriteWordWrapped(Sexy::Rect(left, 140, width, 58), name);
    g->PopState();
    PvzpDrawString(g, std::to_string(mPage + 1) + " / " + std::to_string(levels.size()),
        mWidth / 2, 128, Sexy::FONT_BRIANNETOD12, Sexy::Color(224, 187, 98), DS_ALIGN_CENTER);
    std::string summary = Label(mApp, "CUSTOM_LEVEL_SUN", "Starting sun", "初始阳光") + ": " +
        std::to_string(level.startingSun) + "   " + Label(mApp, "CUSTOM_LEVEL_WAVES", "Waves", "波次") + ": " + std::to_string(level.waves.size());
    PvzpDrawString(g, summary, left, 218, Sexy::FONT_BRIANNETOD12, Sexy::Color(255, 246, 205), DS_ALIGN_LEFT);
    g->SetFont(Sexy::FONT_BRIANNETOD16);
    g->PushState();
    g->ClipRect(left, 234, width, 78);
    g->WriteWordWrapped(Sexy::Rect(left, 234, width, 78), PvzpLocaleTranslateLiteral(level.description));
    g->PopState();
    g->SetFont(Sexy::FONT_BRIANNETOD12);
    g->WriteWordWrapped(Sexy::Rect(left, 322, width, 40), Label(mApp, "CUSTOM_LEVEL_RULES",
        "New play replaces only the last custom checkpoint. Adventure and Survival progress is never changed.",
        "开始新关卡仅覆盖上次的自定义关卡存档，不会修改冒险或生存进度。"));
}

void CustomLevelDialog::ButtonDepress(int id)
{
    if (id == Previous && mPage > 0) { --mPage; Refresh(); return; }
    if (id == Next && mPage + 1 < PvzpMod::CustomLevels().size()) { ++mPage; Refresh(); return; }
    if (id == Close) { mApp->KillDialog(mId); return; }
    if (id == Resume && PvzpCustomLevels::CanContinue(mApp))
    {
        LawnApp* app = mApp;
        app->KillDialog(mId);
        PvzpCustomLevels::Continue(app);
        return;
    }
    if (id == Play && !mSelectedId.empty())
    {
        // KillDialog may defer deletion, but never touch members afterwards.
        LawnApp* app = mApp;
        const std::string idCopy = mSelectedId;
        app->KillDialog(mId);
        if (!PvzpCustomLevels::Start(app, idCopy))
            app->DoDialog(Dialogs::DIALOG_MESSAGE, true,
                Label(app, "CUSTOM_LEVELS", "Custom Levels", "自定义关卡"),
                Label(app, "CUSTOM_LEVEL_UNAVAILABLE", "This level is unavailable or invalid. Restart after checking the content pack.",
                    "该关卡不可用或无效，请检查内容包后重新启动。"),
                "[DIALOG_BUTTON_OK]", Sexy::Dialog::BUTTONS_FOOTER);
    }
}

void CustomLevelDialog::KeyDown(Sexy::KeyCode key)
{
    if (key == Sexy::KeyCode::KEYCODE_ESCAPE) ButtonDepress(Close);
    else if (key == Sexy::KeyCode::KEYCODE_LEFT) ButtonDepress(Previous);
    else if (key == Sexy::KeyCode::KEYCODE_RIGHT) ButtonDepress(Next);
    else if (key == Sexy::KeyCode::KEYCODE_RETURN || key == Sexy::KeyCode::KEYCODE_SPACE) ButtonDepress(Play);
    else LawnDialog::KeyDown(key);
}

CustomLevelResultDialog::CustomLevelResultDialog(LawnApp* app, std::string levelId, bool won, bool error) :
    LawnDialog(app, PvzpCustomLevels::ResultDialogId, true,
        error ? Label(app, "CUSTOM_LEVEL_ERROR", "Level unavailable", "关卡不可用") :
        won ? Label(app, "CUSTOM_LEVEL_WON", "Level Complete!", "关卡完成！") :
        Label(app, "CUSTOM_LEVEL_LOST", "The zombies reached your house!", "僵尸进入了房屋！"),
        error ? Label(app, "CUSTOM_LEVEL_ERROR_BODY", "The level rules changed or a zombie could not be spawned. Check the content pack and restart.",
                    "关卡规则已改变或僵尸无法生成。请检查内容包并重新启动。") :
        won ? Label(app, "CUSTOM_LEVEL_WON_BODY", "Every wave defeated. Your Adventure and Survival progress is untouched.",
                    "已消灭所有波次！冒险与生存模式进度保持不变。") :
        Label(app, "CUSTOM_LEVEL_LOST_BODY", "Try a different defense and retry this level.", "调整防线，重新挑战本关。"),
        "", Sexy::Dialog::BUTTONS_OK_CANCEL), mLevelId(std::move(levelId))
{
    mLawnYesButton->SetLabel(Label(app, "CUSTOM_LEVEL_RETRY", "Retry", "重试"));
    mLawnNoButton->SetLabel(Label(app, "CUSTOM_LEVEL_MENU", "Main Menu", "主菜单"));
    CalcSize(100, 15);
    Move((BOARD_WIDTH - mWidth) / 2, (BOARD_HEIGHT - mHeight) / 2);
}

CustomLevelResultDialog::~CustomLevelResultDialog() = default;

void CustomLevelResultDialog::ButtonDepress(int id)
{
    if (id != Sexy::Dialog::ID_YES && id != Sexy::Dialog::ID_NO) return;
    LawnApp* app = mApp;
    const std::string levelId = mLevelId;
    const bool retry = id == Sexy::Dialog::ID_YES;
    app->KillDialog(mId);
    app->KillBoard();
    if (!retry || !PvzpCustomLevels::Start(app, levelId)) app->ShowGameSelector();
}

void CustomLevelResultDialog::KeyDown(Sexy::KeyCode key)
{
    if (key == Sexy::KeyCode::KEYCODE_ESCAPE) ButtonDepress(Sexy::Dialog::ID_NO);
    else if (key == Sexy::KeyCode::KEYCODE_RETURN || key == Sexy::KeyCode::KEYCODE_SPACE) ButtonDepress(Sexy::Dialog::ID_YES);
    else LawnDialog::KeyDown(key);
}
