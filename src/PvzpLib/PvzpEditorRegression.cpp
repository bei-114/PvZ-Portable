// SPDX-License-Identifier: LGPL-3.0-or-later
// Native-widget integration, not JSON injection. Desktop Linux + disposable HOME
// only. Chooser and first spawn advance naturally; post-spawn kills are labelled
// DIRECT test cleanup, not a balance/gameplay-completion claim.
#include "PvzpEditorRegression.h"
#include "PvzpPlayerLevels.h"
#include "PvzpCustomLevels.h"
#include "PvzpMod.h"
#include "PvzpLocale.h"
#include "../LawnApp.h"
#include "../Lawn/Board.h"
#include "../Lawn/Cutscene.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/SeedPacket.h"
#include "../Lawn/LawnCommon.h"
#include "../Lawn/System/PlayerInfo.h"
#include "../Lawn/Widget/GameSelector.h"
#include "../Lawn/Widget/LevelEditorDialog.h"
#include "../Lawn/Widget/CustomLevelDialog.h"
#include "../Lawn/Widget/CustomLevelResultDialog.h"
#include "../Lawn/Widget/SeedChooserScreen.h"
#include "../GameConstants.h"
#include "widget/WidgetManager.h"
#include <SDL_log.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

bool PvzpEditorRegressionEnabled()
{
#if defined(__linux__) && !defined(__ANDROID__)
    const char* value=std::getenv("PVZP_EDITOR_TEST");
    return value&&std::string_view(value)=="1"&&!std::getenv("PVZP_CUSTOM_TEST")&&!std::getenv("PVZP_LEVEL_TEST");
#else
    return false;
#endif
}
#if defined(__linux__) && !defined(__ANDROID__)
namespace {
using E=LevelEditorDialog;
using P=CustomLevelDialog;
struct Test {
    bool begun=false,finished=false,playing=false,finishing=false;
    int drawTail=0;
    int checks=0,failures=0,frames=0,selected=0,phase=0,nativeClicks=0;
    int32_t level=0;
    uint32_t finishedAdventure=0;
    std::array<uint32_t,100> records{};
    std::array<uint32_t,80> purchases{};
    std::array<int32_t,3> unlocks{};
    std::string adventurePath,survivalPath,playId;
    std::vector<uint8_t> adventureBytes,survivalBytes;
    std::vector<std::string> builtins;
};
Test& State(){static Test test;return test;}
std::vector<uint8_t> Bytes(const std::string& path)
{std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
bool Check(bool pass,const char* label,int a=0,int b=0)
{
    auto& t=State();++t.checks;if(!pass)++t.failures;
    SDL_Log("ETEST %s %s a=%d b=%d clicks=%d frames=%d",pass?"PASS":"FAIL",label,a,b,t.nativeClicks,t.frames);
    return pass;
}
void Snapshot(LawnApp* app)
{
    auto& t=State();const auto* p=app->mPlayerInfo;
    t.level=p->mLevel;t.finishedAdventure=p->mFinishedAdventure;
    std::copy(std::begin(p->mChallengeRecords),std::end(p->mChallengeRecords),t.records.begin());
    std::copy(std::begin(p->mPurchases),std::end(p->mPurchases),t.purchases.begin());
    t.unlocks={p->mHasUnlockedMinigames,p->mHasUnlockedPuzzleMode,p->mHasUnlockedSurvivalMode};
    t.adventurePath=GetSavedGameName(GAMEMODE_ADVENTURE,p->mId);
    t.survivalPath=GetSavedGameName(GAMEMODE_SURVIVAL_NORMAL_STAGE_1,p->mId);
    t.adventureBytes=Bytes(t.adventurePath);t.survivalBytes=Bytes(t.survivalPath);
}
bool Unchanged(LawnApp* app)
{
    auto& t=State();const auto* p=app->mPlayerInfo;
    return p&&p->mLevel==t.level&&p->mFinishedAdventure==t.finishedAdventure&&
        std::equal(t.records.begin(),t.records.end(),p->mChallengeRecords)&&
        std::equal(t.purchases.begin(),t.purchases.end(),p->mPurchases)&&
        t.unlocks==std::array<int32_t,3>{p->mHasUnlockedMinigames,p->mHasUnlockedPuzzleMode,p->mHasUnlockedSurvivalMode}&&
        Bytes(t.adventurePath)==t.adventureBytes&&Bytes(t.survivalPath)==t.survivalBytes;
}
void Finish(LawnApp* app)
{
    auto& t=State();if(t.finished)return;
    if(!t.failures && !t.finishing){t.finishing=true;return;}
    if(!t.failures)Check(t.drawTail>=20 && app->GetDialog(PvzpCustomLevels::PickerDialogId),"native_picker_survives20_real_update_and_draw_cycles_after_interactions",t.drawTail);
    Check(Unchanged(app),"all_native_editor_and_play_paths_preserve_profile_and_vanilla_checkpoint_bytes");
    t.finished=true;
    SDL_Log("ETEST RESULT %s checks=%d failures=%d native_clicks=%d natural_frames=%d",t.failures?"FAIL":"PASS",t.checks,t.failures,t.nativeClicks,t.frames);
    if(t.failures){app->Shutdown();std::exit(1);}
    if(!std::getenv("PVZP_EDITOR_HOLD"))app->Shutdown();
    else {
        // Screenshot-only presentation AFTER the real interaction assertions.
        PvzpLocaleSetLanguage("zh-CN");
        auto* picker=dynamic_cast<CustomLevelDialog*>(app->GetDialog(PvzpCustomLevels::PickerDialogId));
        if(picker){picker->SelectLevel("pvzp.seed_heist");if(picker->Layout()!=CustomLevelDialog::LayoutMode::Cards)picker->ButtonDepress(117);}
        const char* capture=std::getenv("PVZP_EDITOR_CAPTURE");
        if(capture && std::string_view(capture)=="details"){if(picker)picker->ButtonDepress(117);}
        if(capture && (std::string_view(capture)=="editor" || std::string_view(capture)=="waves")){
            if(picker)app->KillDialog(PvzpCustomLevels::PickerDialogId);
            LevelEditorDialog::Open(app,"pvzp.garlic_delivery");
            auto* editor=dynamic_cast<LevelEditorDialog*>(app->GetDialog(LevelEditorDialog::DialogId));
            if(editor && std::string_view(capture)=="waves")editor->ButtonDepress(LevelEditorDialog::TabWaves);
        }
    }
}
// WidgetManager hit-testing, capture and ButtonWidget MouseDown/MouseUp all run.
// The target pointer is never read after MouseUp: the callback may queue deletion.
bool Click(LawnApp* app,Sexy::Widget* widget)
{
    if(!widget||!widget->mVisible||widget->mDisabled)return false;
    const auto pos=widget->GetAbsPos();
    const int x=pos.mX+widget->mWidth/2,y=pos.mY+widget->mHeight/2;
    auto* manager=app->mWidgetManager.get();
    manager->mHasFocus=true;manager->mMouseIn=true;
    manager->MouseMove(x,y);manager->MouseDown(x,y,1);manager->MouseUp(x,y,1);
    ++State().nativeClicks;
    return true;
}
E* Editor(LawnApp* app){return dynamic_cast<E*>(app->GetDialog(E::DialogId));}
P* Picker(LawnApp* app){return dynamic_cast<P*>(app->GetDialog(PvzpCustomLevels::PickerDialogId));}
bool ClickE(LawnApp* app,int id){auto* d=Editor(app);return d&&Click(app,d->Button(id));}
bool ClickP(LawnApp* app,int id){auto* d=Picker(app);return d&&Click(app,d->Button(id));}
bool Geometry(LawnApp* app,Sexy::Widget* dialog,int first,int last,bool editor)
{
    if(!dialog)return false;
    bool good=dialog->mWidth<=800&&dialog->mHeight<=600;
    int visible=0;std::vector<Sexy::Rect> targets;
    for(int id=first;id<=last;++id)
    {
        auto* b=editor?Editor(app)->Button(id):Picker(app)->Button(id);
        if(!b||!b->mVisible)continue;
        ++visible;const auto p=b->GetAbsPos();
        good=good&&b->mWidth>=40&&b->mHeight>=40&&p.mX>=0&&p.mY>=0&&p.mX+b->mWidth<=800&&p.mY+b->mHeight<=600;
        const Sexy::Rect target(p.mX,p.mY,b->mWidth,b->mHeight);
        for(const auto& old:targets)
            good=good&&!(target.mX<old.mX+old.mWidth&&old.mX<target.mX+target.mWidth&&target.mY<old.mY+old.mHeight&&old.mY<target.mY+target.mHeight);
        targets.push_back(target);
    }
    return good&&visible>0;
}
bool Require(bool pass,const char* label,LawnApp* app)
{if(Check(pass,label))return true;Finish(app);return false;}
void Name(LawnApp* app,const std::string& text)
{
    auto* d=Editor(app);d->NameWidget()->SetText(text);d->Update();
}
bool Ui(LawnApp* app)
{
    auto& t=State();Snapshot(app);
    SDL_Log("ETEST BEGIN disposable HOME; native widgets, real backend persistence, natural chooser/first spawn");
    if(!Require(PvzpMod::List().size()==9&&PvzpMod::CustomZombies().size()==14&&PvzpMod::CustomLevels().size()==7,
        "v8_fixture_baseline_nine_packs_fourteen_custom_zombies_seven_levels",app))return false;
    if(!Require(app->mPlayerInfo&&app->mPlayerInfo->mFinishedAdventure==0&&app->mPlayerInfo->mHasUnlockedMinigames==0,
        "fresh_profile_has_no_test_unlocks",app))return false;
    for(const auto& level:PvzpMod::CustomLevels())t.builtins.push_back(level.id);
    auto* selector=app->mGameSelector.get();
    if(!Require(selector&&Click(app,selector->mCustomLevelsButton)&&Picker(app),"native_selector_button_opens_picker",app))return false;
    auto* p=Picker(app);
    if(p->Layout()!=P::LayoutMode::Detail)ClickP(app,P::ToggleLayout);
    p=Picker(app);p->SelectLevel(t.builtins.back());
    const std::string selected=p->SelectedId();
    ClickP(app,P::ToggleLayout);
    int layout=-1;app->RegistryReadInteger("CustomLevelLayout",&layout);
    if(!Require(Picker(app)->Layout()==P::LayoutMode::Cards&&layout==1&&Picker(app)->SelectedId()==selected&&Picker(app)->Page()==1,
        "native_grid_toggle_persists_layout_retains_selected_stable_id",app))return false;
    Check(Geometry(app,Picker(app),110,125,false),"card_grid_visible_native_targets_in_800x600_minimum40");
    ClickP(app,P::Previous);
    Check(Picker(app)->SelectedId()==selected&&Picker(app)->Page()==0,"grid_page_navigation_does_not_silently_change_selection");
    for(int i=0;i<6;++i)ClickP(app,P::Card0+i);
    Check(Picker(app)->SelectedId()==t.builtins[5],"all_first_grid_page_cards_have_real_native_dispatch");
    ClickP(app,P::Next);ClickP(app,P::Card0);
    Check(Picker(app)->SelectedId()==selected,"seventh_level_reachable_by_grid_second_page");
    ClickP(app,P::ToggleLayout);
    Check(Picker(app)->Layout()==P::LayoutMode::Detail&&Picker(app)->SelectedId()==selected,"detail_toggle_retains_stable_selection");
    Check(Geometry(app,Picker(app),110,125,false),"detail_visible_native_targets_in_800x600_minimum40");
    ClickP(app,P::ToggleLayout);
    ClickP(app,P::Editor);
    if(!Require(Editor(app)&&PvzpPlayerLevels::Validate(Editor(app)->Draft()),"editor_opens_valid_backend_draft_from_visible_native_button",app))return false;
    Check(Geometry(app,Editor(app),200,291,true),"editor_level_tab_native_targets_in_800x600_minimum40");
    std::string oversizedName;for(int i=0;i<50;++i)oversizedName+="关";
    Name(app,oversizedName);ClickE(app,E::Save);
    Check(Editor(app)->Draft().name==oversizedName&&!Editor(app)->Status().empty()&&PvzpPlayerLevels::Levels().empty(),
        "native_save_validation_error_is_visible_and_retains_unsaved_utf8_draft");
    Name(app,"手机关卡 Café 🌻");
    Check(Editor(app)->Draft().name=="手机关卡 Café 🌻"&&Editor(app)->Dirty(),"native_utf8_edit_widget_updates_real_draft_without_catalog_publish");
    const int sun=Editor(app)->Draft().startingSun;
    ClickE(app,E::SunPlus100);ClickE(app,E::SunPlus25);ClickE(app,E::SunMinus25);
    Check(Editor(app)->Draft().startingSun==sun+100&&PvzpPlayerLevels::Levels().empty(),"native_sun_buttons_modify_only_unsaved_draft");
    ClickE(app,E::TabWaves);
    Check(Geometry(app,Editor(app),200,291,true),"editor_wave_tab_native_targets_in_800x600_minimum40");
    const int delay=Editor(app)->Draft().waves[0].delay;
    ClickE(app,E::DelayPlus5);ClickE(app,E::DelayPlus1);ClickE(app,E::DelayMinus5);ClickE(app,E::DelayMinus1);
    Check(Editor(app)->Draft().waves[0].delay==delay,"native_wave_delay_plus_minus_one_and_five_seconds_exact_ticks");
    ClickE(app,E::WaveAdd);ClickE(app,E::DelayPlus1);ClickE(app,E::WaveDuplicate);
    ClickE(app,E::WaveEarlier);
    Check(Editor(app)->Draft().waves.size()==3&&Editor(app)->ActiveWave()==1&&Editor(app)->Draft().waves[1].delay==delay+100,
        "native_wave_add_duplicate_move_earlier_preserve_selected_wave_data");
    ClickE(app,E::WaveLater);ClickE(app,E::WaveDelete);
    Check(Editor(app)->Draft().waves.size()==2,"native_wave_later_and_delete");
    for(int i=0;i<40;++i)ClickE(app,E::WaveAdd);
    Check(Editor(app)->Draft().waves.size()==30&&Editor(app)->Button(E::WaveAdd)->mDisabled&&Editor(app)->Button(E::WaveDuplicate)->mDisabled,
        "native_wave_limit_thirty_enforced");
    for(int i=0;i<40;++i)ClickE(app,E::WaveDelete);
    Check(Editor(app)->Draft().waves.size()==1&&Editor(app)->Button(E::WaveDelete)->mDisabled,"native_wave_delete_keeps_one_valid_wave");
    ClickE(app,E::TabSpawns);
    Check(Geometry(app,Editor(app),200,291,true),"editor_spawn_tab_native_targets_in_800x600_minimum40");
    ClickE(app,E::Lane5);ClickE(app,E::CountPlus5);ClickE(app,E::CountMinus1);
    Check(Editor(app)->Draft().waves[0].spawns[0].row==4&&Editor(app)->Draft().waves[0].spawns[0].count==5,"native_lane_and_quantity_edit_real_spawn_group");
    ClickE(app,E::LaneAuto);
    for(int i=0;i<20;++i)ClickE(app,E::CountPlus5);
    Check(Editor(app)->Draft().waves[0].spawns[0].count==50&&Editor(app)->Button(E::GroupAdd)->mDisabled,"native_wave_total_fifty_disables_add_and_quantity_overflow");
    for(int i=0;i<20;++i)ClickE(app,E::CountMinus5);
    for(int i=0;i<20;++i)ClickE(app,E::GroupAdd);
    Check(Editor(app)->Draft().waves[0].spawns.size()==12&&Editor(app)->Button(E::GroupAdd)->mDisabled,"native_group_limit_twelve_enforced");
    for(int i=0;i<20;++i)ClickE(app,E::CountPlus5);
    int waveTotal=0;for(const auto& group:Editor(app)->Draft().waves[0].spawns)waveTotal+=group.count;
    Check(waveTotal==50&&Editor(app)->Draft().waves[0].spawns.back().count==39,"native_quantity_limit_uses_total_of_all_twelve_groups");
    for(int i=0;i<20;++i)ClickE(app,E::GroupDelete);
    Check(Editor(app)->Draft().waves[0].spawns.size()==1&&Editor(app)->Button(E::GroupDelete)->mDisabled,"native_group_delete_keeps_one_valid_group");
    ClickE(app,E::ChooseEnemy);
    Check(Geometry(app,Editor(app),200,291,true),"paged_enemy_catalog_native_targets_in_800x600_minimum40");
    int pages=1;while(Editor(app)->Button(E::EnemyNext)->mVisible&&!Editor(app)->Button(E::EnemyNext)->mDisabled&&pages<30){ClickE(app,E::EnemyNext);++pages;}
    ClickE(app,E::Enemy0);
    Check(pages>=9&&Editor(app)->ActiveView()==E::View::Spawns&&PvzpPlayerLevels::Validate(Editor(app)->Draft()),"native_enemy_catalog_reaches_registered_custom_enemies_after_safe_builtins",pages);
    const auto dirty=PvzpPlayerLevels::ExportJson(Editor(app)->Draft());
    ClickE(app,E::Back);
    Check(Editor(app)->ActiveView()==E::View::Confirmation,"dirty_back_opens_real_native_discard_confirmation");
    ClickE(app,E::Cancel);
    Check(Editor(app)&&PvzpPlayerLevels::ExportJson(Editor(app)->Draft())==dirty,"dirty_back_cancel_retains_exact_unsaved_draft");
    ClickE(app,E::TabLevel);ClickE(app,E::Save);
    if(!Require(Editor(app)&&PvzpPlayerLevels::Find(Editor(app)->Draft().id),"native_save_validates_persists_and_immediately_publishes_catalog",app))return false;
    const std::string originalId=Editor(app)->Draft().id;
    Name(app,"Overwrite 玩家关卡");ClickE(app,E::Save);
    Check(Editor(app)->ActiveView()==E::View::Confirmation,"own_overwrite_requires_native_confirmation");
    ClickE(app,E::Cancel);
    Check(PvzpPlayerLevels::Find(originalId)->name!="Overwrite 玩家关卡"&&Editor(app)->Dirty(),"overwrite_cancel_leaves_saved_file_and_dirty_draft_intact");
    ClickE(app,E::Save);ClickE(app,E::Confirm);
    Check(PvzpPlayerLevels::Find(originalId)->name=="Overwrite 玩家关卡"&&!Editor(app)->Dirty(),"confirmed_own_overwrite_persists_without_new_id");
    ClickE(app,E::SaveAsCopy);
    const std::string copiedId=Editor(app)->Draft().id;
    Check(copiedId!=originalId&&PvzpPlayerLevels::Find(copiedId)&&PvzpPlayerLevels::Levels().size()==2,"save_as_copy_creates_new_stable_id_without_overwriting_original");
    ClickE(app,E::TabLibrary);
    Check(Geometry(app,Editor(app),200,291,true),"player_library_native_targets_in_800x600_minimum40");
    ClickE(app,E::LibraryDelete);ClickE(app,E::Cancel);
    Check(PvzpPlayerLevels::Find(originalId)&&PvzpPlayerLevels::Levels().size()==2,"library_delete_cancel_preserves_own_file");
    ClickE(app,E::LibraryDelete);ClickE(app,E::Confirm);
    Check(!PvzpPlayerLevels::Find(originalId)&&PvzpPlayerLevels::Find(copiedId),"library_confirm_delete_removes_only_selected_own_file");
    ClickE(app,E::Back);
    p=Picker(app);p->Update();p->SelectLevel(t.builtins.front());
    const auto builtinBefore=PvzpPlayerLevels::ExportJson(PvzpMod::CustomLevels().front());
    ClickP(app,P::EditCopy);
    if(!Require(Editor(app)&&Editor(app)->Draft().id!=t.builtins.front(),"builtin_edit_copy_uses_new_player_id",app))return false;
    ClickE(app,E::SunPlus25);ClickE(app,E::Save);
    Check(PvzpPlayerLevels::ExportJson(PvzpMod::CustomLevels().front())==builtinBefore&&PvzpPlayerLevels::Find(Editor(app)->Draft().id),"saving_builtin_copy_never_mutates_bundled_level_definition");
    ClickE(app,E::Back);Picker(app)->Update();Picker(app)->SelectLevel(copiedId);ClickP(app,P::EditCopy);
    Check(Editor(app)&&Editor(app)->Draft().id==copiedId&&Editor(app)->Draft().name==PvzpPlayerLevels::Find(copiedId)->name,"close_reopen_own_level_edits_same_saved_stable_id");
    ClickE(app,E::Back);
    const auto root=PvzpPlayerLevels::Directory();const auto initialized=PvzpPlayerLevels::Initialize(root);
    Check(initialized&&PvzpPlayerLevels::Find(copiedId)&&PvzpPlayerLevels::Levels().size()==2,"explicit_appdata_reinitialize_recovers_native_saved_player_levels");
    Picker(app)->Update();ClickP(app,P::Editor);
    Name(app,"Native phone play 手机测试");
    ClickE(app,E::TabWaves);ClickE(app,E::DelayMinus5);ClickE(app,E::DelayMinus1);ClickE(app,E::DelayMinus1);
    ClickE(app,E::TabSpawns);ClickE(app,E::Lane2);ClickE(app,E::TabLevel);
    t.playId=Editor(app)->Draft().id;
    Check(Editor(app)->Draft().waves.size()==1&&Editor(app)->Draft().waves[0].delay==100&&Editor(app)->Draft().waves[0].spawns[0].count==1,"native_play_fixture_built_entirely_with_editor_controls");
    Check(Unchanged(app),"all_editor_management_paths_preserve_fresh_profile_and_vanilla_files");
    if(std::getenv("PVZP_EDITOR_UI_ONLY"))
    {
        ClickE(app,E::Save);Check(PvzpPlayerLevels::Find(t.playId)!=nullptr,"ui_only_final_native_save_published");
        ClickE(app,E::Back);Picker(app)->Update();Picker(app)->SelectLevel(t.playId);
        if(Picker(app)->Layout()!=P::LayoutMode::Cards)ClickP(app,P::ToggleLayout);
        Finish(app);return false;
    }
    ClickE(app,E::SaveAndPlay);
    Check(Editor(app)->ActiveView()==E::View::Confirmation&&!app->mBoard,"save_and_play_warns_before_replacing_only_custom_checkpoint");
    ClickE(app,E::Cancel);
    Check(Editor(app)&&Editor(app)->Draft().id==t.playId&&!app->mBoard,"save_and_play_warning_cancel_retains_unsaved_draft");
    t.playing=true;
    ClickE(app,E::SaveAndPlay);ClickE(app,E::Confirm);
    return Require(app->mBoard&&PvzpCustomLevels::IsMode(app)&&PvzpCustomLevels::Inspect(app->mBoard).levelId==t.playId&&PvzpPlayerLevels::Find(t.playId),
        "confirmed_native_save_and_play_immediately_starts_saved_stable_id_custom_board",app);
}
constexpr SeedType Seeds[8]={SEED_SUNFLOWER,SEED_PEASHOOTER,SEED_WALLNUT,SEED_POTATOMINE,SEED_SNOWPEA,SEED_REPEATER,SEED_CHERRYBOMB,SEED_SQUASH};
void ChooserClick(SeedChooserScreen* chooser,int x,int y)
{
    const auto p=chooser->GetAbsPos();auto* manager=chooser->mApp->mWidgetManager.get();
    manager->mLastMouseX=p.mX+x;manager->mLastMouseY=p.mY+y;manager->mMouseIn=true;
    chooser->mLastMouseX=x;chooser->mLastMouseY=y;chooser->MouseDown(x,y,1);chooser->MouseUp(x,y,1);
    ++State().nativeClicks;
}
bool ResultClick(LawnApp* app,CustomLevelResultDialog* result,int id)
{
    const auto r=result->ButtonRect(id);const auto p=result->GetAbsPos();
    if(r.mWidth<=0||r.mHeight<=0)return false;
    auto* manager=app->mWidgetManager.get();manager->mHasFocus=true;manager->mMouseIn=true;
    manager->MouseMove(p.mX+r.mX+r.mWidth/2,p.mY+r.mY+r.mHeight/2);
    manager->MouseDown(p.mX+r.mX+r.mWidth/2,p.mY+r.mY+r.mHeight/2,1);
    manager->MouseUp(p.mX+r.mX+r.mWidth/2,p.mY+r.mY+r.mHeight/2,1);
    ++State().nativeClicks;return true;
}
}
#endif

void PvzpRunEditorMenuRegression(LawnApp* app)
{
#if defined(__linux__) && !defined(__ANDROID__)
    if(!PvzpEditorRegressionEnabled())return;
    auto& t=State();if(t.finished||!app||!app->mGameSelector)return;
    if(t.finishing){if(++t.drawTail>=20)Finish(app);return;}
    if(t.begun)return;
    auto* selector=app->mGameSelector.get();
    if(selector->mSelectorState!=SELECTOR_IDLE||selector->mLoading||selector->mSlideCounter||app->GetDialogCount())return;
    t.begun=true;Ui(app); // last action may SafeDelete the selector
#else
    (void)app;
#endif
}
void PvzpRunEditorBoardRegression(Board* board)
{
#if defined(__linux__) && !defined(__ANDROID__)
    if(!PvzpEditorRegressionEnabled()||!board)return;
    auto& t=State();if(t.finished||!t.playing)return;
    auto* app=board->mApp;++t.frames;
    if(t.frames>5000){Check(false,"bounded_natural_editor_play_timeout",int(app->mGameScene));Finish(app);return;}
    if(!PvzpCustomLevels::IsActive(board)){Check(false,"editor_play_never_falls_back_to_vanilla");Finish(app);return;}
    if(t.phase==0)
    {
        const auto state=PvzpCustomLevels::Inspect(board);
        if(!Require(state.levelId==t.playId&&state.phase==PvzpCustomLevels::Phase::WaitingForSeeds&&state.spawned==0&&
            board->mSeedBank->mNumPackets==8&&Unchanged(app),"native_editor_play_waits_for_natural_eight_seed_chooser_without_profile_unlock",app))return;
        t.phase=1;
    }
    if(t.phase==1)
    {
        auto* chooser=app->mSeedChooserScreen.get();
        if(!chooser||!chooser->mMouseVisible||chooser->mX!=0||chooser->mY!=0||!board->mCutScene->mSeedChoosing||app->GetDialogCount()||chooser->mSeedsInFlight)return;
        if(t.selected<8)
        {
            const auto seed=Seeds[t.selected];int x=0,y=0;chooser->GetSeedPositionInChooser(seed,x,y);x+=SEED_PACKET_WIDTH/2;y+=SEED_PACKET_HEIGHT/2;
            if(!Require(chooser->ChooserHasSeed(seed)&&chooser->SeedHitTest(x,y)==seed,"native_editor_play_owned_seed_hit",app))return;
            ChooserClick(chooser,x,y);
            if(!Require(chooser->mSeedsInFlight==1&&chooser->mChosenSeeds[seed].mSeedState==SEED_FLYING_TO_BANK,"native_editor_play_seed_click_starts_natural_flight",app))return;
            ++t.selected;return;
        }
        if(!Require(chooser->mSeedsInBank==8&&!chooser->mStartButton->mDisabled,"native_editor_play_eight_natural_flights_enable_lets_rock",app))return;
        auto* start=chooser->mStartButton.get();ChooserClick(chooser,start->mX+start->mWidth/2,start->mY+start->mHeight/2);
        t.phase=2;return;
    }
    if(t.phase==2)
    {
        if(app->mGameScene!=SCENE_PLAYING)return;
        const auto state=PvzpCustomLevels::Inspect(board);if(!state.spawned)return;
        if(!Require(state.nextWave==1&&state.spawned==1&&state.alive==1&&state.ticks==100&&board->mCurrentWave==1,
            "natural_editor_saved_first_wave_exact_delay_lane_and_count",app))return;
        bool lane=false;for(Zombie* z:board->mZombies)if(!z->mDead)lane=z->mRow==1;
        Check(lane,"natural_editor_saved_lane_two_spawn");
        // Explicit DIRECT cleanup after the natural first spawn. No test claims
        // to have played combat or a whole game naturally.
        for(Zombie* z:board->mZombies)if(!z->mDead)z->DieNoLoot();board->ProcessDeleteQueue();
        PvzpCustomLevels::Update(board);t.phase=3;
    }
    if(t.phase==3)
    {
        const auto state=PvzpCustomLevels::Inspect(board);
        auto* result=dynamic_cast<CustomLevelResultDialog*>(app->GetDialog(PvzpCustomLevels::ResultDialogId));
        if(!Require(state.phase==PvzpCustomLevels::Phase::Won&&result&&state.nextWave==1&&state.totalWaves==1&&state.spawned==1&&Unchanged(app),
            "DIRECT_cleanup_native_editor_one_wave_win_owns_real_result_stats_source",app))return;
        // The created level is last in the effective catalog; Next must be inert.
        Board* before=app->mBoard;ResultClick(app,result,CustomLevelResultDialog::NextLevel);
        Check(app->mBoard==before&&app->GetDialog(PvzpCustomLevels::ResultDialogId)==result,"last_level_result_next_button_is_disabled");
        ResultClick(app,result,CustomLevelResultDialog::SelectLevel);
        if(!Require(!app->mBoard&&Picker(app)&&Picker(app)->SelectedId()==t.playId&&Picker(app)->Layout()==P::LayoutMode::Cards,
            "native_result_choose_level_returns_selected_stable_id_and_persisted_layout",app))return;
        Finish(app);return; // old Board may be queued for SafeDelete
    }
#else
    (void)board;
#endif
}
