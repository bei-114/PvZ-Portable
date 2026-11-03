// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "LawnDialog.h"
#include "GameButton.h"
#include "../../PvzpLib/PvzpMod.h"
#include "widget/EditListener.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class LawnEditWidget;

// Native, touch-first editor. Every action goes through ButtonDepress and the
// backend guard/validation. IDs are stable for real WidgetManager click tests.
class LevelEditorDialog final : public LawnDialog, public Sexy::EditListener
{
public:
    static constexpr int DialogId = 18002;
    enum class View { Level, Waves, Spawns, Library, Enemies, Confirmation };
    enum ButtonId {
        TabLevel=200, TabWaves=201, TabSpawns=202, TabLibrary=203, Back=204,
        Save=210, SaveAndPlay=211, SaveAsCopy=212,
        SunMinus100=220, SunMinus25=221, SunPlus25=222, SunPlus100=223,
        WavePrevious=230, WaveNext=231, WaveAdd=232, WaveDuplicate=233,
        WaveDelete=234, WaveEarlier=235, WaveLater=236,
        DelayMinus5=240, DelayMinus1=241, DelayPlus1=242, DelayPlus5=243,
        GroupPrevious=250, GroupNext=251, GroupAdd=252, GroupDelete=253,
        ChooseEnemy=254, CountMinus5=255, CountMinus1=256,
        CountPlus1=257, CountPlus5=258,
        LaneAuto=260, Lane1=261, Lane2=262, Lane3=263, Lane4=264, Lane5=265,
        EnemyPrevious=270, EnemyNext=271, Enemy0=272, Enemy1=273, Enemy2=274, Enemy3=275,
        LibraryPrevious=280, LibraryNext=281, LibraryEdit=282, LibraryCopy=283,
        LibraryDelete=284, NewLevel=285, DuplicateDraft=286,
        Confirm=290, Cancel=291, NameEdit=300
    };
    static void Open(LawnApp* app, std::string_view sourceId={});
    explicit LevelEditorDialog(LawnApp* app, std::string_view sourceId={});
    ~LevelEditorDialog() override;
    void AddedToManager(Sexy::WidgetManager* manager) override;
    void RemovedFromManager(Sexy::WidgetManager* manager) override;
    void Draw(Sexy::Graphics* graphics) override;
    void Update() override;
    void ButtonDepress(int id) override;
    void KeyDown(Sexy::KeyCode key) override;
    void EditWidgetText(int id, const std::string& text) override;

    const PvzpMod::CustomLevel& Draft() const { return mDraft; }
    View ActiveView() const { return mView; }
    size_t ActiveWave() const { return mWave; }
    size_t ActiveGroup() const { return mGroup; }
    size_t EnemyPage() const { return mEnemyPage; }
    size_t LibraryPage() const { return mLibraryPage; }
    bool Dirty() const { return mDirty; }
    const std::string& Status() const { return mStatus; }
    // Actual native controls, not a persistence bypass or mocked action API.
    LawnStoneButton* Button(int id) const;
    LawnEditWidget* NameWidget() const { return mName.get(); }
private:
    enum class Pending { None, Close, New, Edit, Copy, Duplicate, Delete, Save, Play };
    struct Enemy { std::string id, label; };
    std::vector<std::unique_ptr<LawnStoneButton>> mButtons;
    std::unique_ptr<LawnEditWidget> mName;
    PvzpMod::CustomLevel mDraft;
    View mView=View::Level, mReturnView=View::Level;
    Pending mPending=Pending::None;
    size_t mWave=0, mGroup=0, mLibraryPage=0, mEnemyPage=0;
    bool mDirty=false;
    uint64_t mLocaleGeneration=0, mCatalogRevision=0;
    std::string mHeaderText, mStatus, mPreview, mConfirmText, mPendingId;
    std::vector<Enemy> mEnemies;
    // Cached visible text: Draw never changes labels, draft or gameplay.
    std::vector<std::pair<int,std::string>> mLines;
    void Refresh();
    void SetView(View view);
    void SyncName();
    void Changed();
    void Load(PvzpMod::CustomLevel draft);
    void Request(Pending pending, const std::string& id={});
    void Execute(Pending pending, const std::string& id={});
    void SaveDraft(bool play, bool asCopy=false);
    void ConfirmAction(Pending pending, const std::string& text, const std::string& id={});
    void RebuildEnemies();
    void ShowButton(int id, int column, int columns, int y, const std::string& label, bool enabled=true);
    int WaveTotal() const;
};
