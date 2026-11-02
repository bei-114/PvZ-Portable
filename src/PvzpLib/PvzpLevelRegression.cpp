// SPDX-License-Identifier: LGPL-3.0-or-later
// Destructive Linux-only integration harness. Run in a disposable HOME with
// level/variant content packs. Chooser flights and EACH level's first wave use
// natural Board updates. Later finite-wave completion is explicitly DIRECT
// director simulation (not a claim of playing three full games naturally).
#include "PvzpLevelRegression.h"
#include "PvzpCustomLevels.h"
#include "PvzpCustomZombies.h"
#include "PvzpMod.h"
#include "../LawnApp.h"
#include "../Lawn/Board.h"
#include "../Lawn/Cutscene.h"
#include "../Lawn/SeedPacket.h"
#include "../Lawn/LawnCommon.h"
#include "../Lawn/System/PlayerInfo.h"
#include "../Lawn/System/SaveGame.h"
#include "../Lawn/Widget/CustomLevelDialog.h"
#include "../Lawn/Widget/GameSelector.h"
#include "../Lawn/Widget/SeedChooserScreen.h"
#include "../GameConstants.h"
#include "widget/WidgetManager.h"
#include <SDL_log.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

bool PvzpLevelRegressionEnabled()
{
#if defined(__linux__) && !defined(__ANDROID__)
    const char* value = std::getenv("PVZP_LEVEL_TEST");
    return value && value[0] == '1' && value[1] == '\0' && !std::getenv("PVZP_CUSTOM_TEST");
#else
    return false;
#endif
}

#if defined(__linux__) && !defined(__ANDROID__)
namespace
{
using PvzpCustomLevels::Phase;
using PvzpCustomLevels::State;
constexpr SeedType Seeds[8] = {SEED_SUNFLOWER, SEED_PEASHOOTER, SEED_WALLNUT,
    SEED_POTATOMINE, SEED_SNOWPEA, SEED_REPEATER, SEED_CHERRYBOMB, SEED_SQUASH};
struct Test
{
    Board* board = nullptr;
    int phase = 0, selected = 0, frames = 0, boardFrames = 0, level = 0;
    int checks = 0, failures = 0, naturalFirstWaves = 0, directWins = 0;
    uint64_t directTicks = 0;
    bool begun = false, finished = false, resuming = false;
    bool preparingLoss = false, lossRetried = false;
    std::vector<std::string> levels;
    State previous, saved;
    int32_t profileLevel = 0;
    uint32_t finishedAdventure = 0;
    std::array<uint32_t, 100> records{};
    std::array<uint32_t, 80> purchases{};
    std::array<int32_t, 3> unlocks{};
    std::string adventurePath, survivalPath;
    std::vector<uint8_t> adventureBytes, survivalBytes;
};

bool Check(Test& t, bool pass, const char* label, int a = 0, int b = 0)
{
    ++t.checks;
    if (!pass) ++t.failures;
    SDL_Log("LTEST %s %s a=%d b=%d phase=%d level=%d frames=%d direct_ticks=%llu",
        pass ? "PASS" : "FAIL", label, a, b, t.phase, t.level, t.frames,
        static_cast<unsigned long long>(t.directTicks));
    return pass;
}

std::vector<uint8_t> Bytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool ProfileUnchanged(Test& t, LawnApp* app)
{
    const auto* p = app->mPlayerInfo;
    return p && p->mLevel == t.profileLevel && p->mFinishedAdventure == t.finishedAdventure &&
        std::equal(t.records.begin(), t.records.end(), p->mChallengeRecords) &&
        std::equal(t.purchases.begin(), t.purchases.end(), p->mPurchases) &&
        p->mHasUnlockedMinigames == t.unlocks[0] && p->mHasUnlockedPuzzleMode == t.unlocks[1] &&
        p->mHasUnlockedSurvivalMode == t.unlocks[2];
}

void Finish(Test& t, LawnApp* app)
{
    if (t.finished) return;
    t.finished = true;
    SDL_Log("LTEST RESULT %s checks=%d failures=%d natural_first_waves=%d direct_finite_wins=%d frames=%d direct_ticks=%llu",
        t.failures ? "FAIL" : "PASS", t.checks, t.failures, t.naturalFirstWaves,
        t.directWins, t.frames, static_cast<unsigned long long>(t.directTicks));
    app->Shutdown();
    if (t.failures) std::exit(1);
}

const PvzpMod::CustomLevel* Level(std::string_view id)
{
    for (const auto& level : PvzpMod::CustomLevels()) if (level.id == id) return &level;
    return nullptr;
}

int WaveSize(const PvzpMod::CustomWave& wave)
{
    int count = 0;
    for (const auto& spawn : wave.spawns) count += spawn.count;
    return count;
}

void Click(SeedChooserScreen* chooser, int x, int y)
{
    const auto pos = chooser->GetAbsPos();
    chooser->mApp->mWidgetManager->mLastMouseX = x + pos.mX;
    chooser->mApp->mWidgetManager->mLastMouseY = y + pos.mY;
    chooser->mApp->mWidgetManager->mMouseIn = true;
    chooser->mLastMouseX = x;
    chooser->mLastMouseY = y;
    chooser->MouseDown(x, y, 1);
    chooser->MouseUp(x, y, 1);
}

bool Same(const State& a, const State& b)
{
    return a.levelId == b.levelId && a.phase == b.phase && a.nextWave == b.nextWave &&
        a.totalWaves == b.totalWaves && a.countdown == b.countdown && a.spawned == b.spawned &&
        a.ticks == b.ticks;
}

bool Begin(Test& t, Board* board)
{
    auto* app = board->mApp;
    const auto* profile = app->mPlayerInfo;
    t.profileLevel = profile->mLevel;
    t.finishedAdventure = profile->mFinishedAdventure;
    std::copy(std::begin(profile->mChallengeRecords), std::end(profile->mChallengeRecords), t.records.begin());
    std::copy(std::begin(profile->mPurchases), std::end(profile->mPurchases), t.purchases.begin());
    t.unlocks = {profile->mHasUnlockedMinigames, profile->mHasUnlockedPuzzleMode, profile->mHasUnlockedSurvivalMode};
    t.adventurePath = GetSavedGameName(GAMEMODE_ADVENTURE, profile->mId);
    t.survivalPath = GetSavedGameName(GAMEMODE_SURVIVAL_NORMAL_STAGE_1, profile->mId);
    t.adventureBytes = Bytes(t.adventurePath);
    t.survivalBytes = Bytes(t.survivalPath);
    t.levels.push_back("pvzp.seed_heist");
    for (const auto& level : PvzpMod::CustomLevels())
        if (level.id != t.levels.front() && t.levels.size() < 3) t.levels.push_back(level.id);
    if (!Check(t, t.levels.size() == 3 && Level(t.levels.front()), "three_real_catalog_levels_include_seed_heist")) return false;
    bool refs = true;
    for (const auto& id : t.levels)
    {
        const auto* level = Level(id);
        refs = refs && level && !level->waves.empty();
        if (level) for (const auto& wave : level->waves) for (const auto& spawn : wave.spawns)
            refs = refs && PvzpCustomZombies::CanSpawn(board, spawn.zombie, spawn.row < 0 ? 0 : spawn.row);
    }
    if (!Check(t, refs, "all_three_real_level_spawn_references_resolve")) return false;
    const std::string checkpoint = GetSavedGameName(GAMEMODE_MOD_LEVEL, profile->mId);
    Check(t, checkpoint != t.adventurePath && checkpoint != t.survivalPath, "checkpoint_filename_isolated_from_adventure_and_survival");
    PvzpCustomLevels::OpenPicker(app);
    auto* picker = dynamic_cast<CustomLevelDialog*>(app->GetDialog(PvzpCustomLevels::PickerDialogId));
    Check(t, picker != nullptr, "native_catalog_picker_opens");
    if (picker) picker->ButtonDepress(113); // Close; native stone button ID
    return Check(t, !app->GetDialog(PvzpCustomLevels::PickerDialogId) && ProfileUnchanged(t, app),
        "picker_close_no_profile_unlock_or_purchase_mutation");
}

bool DirectGuards(Test& t, Board* board)
{
    const State before = PvzpCustomLevels::Inspect(board);
    board->mPaused = true;
    for (int i = 0; i < 9; ++i) PvzpCustomLevels::Update(board);
    const bool paused = Same(before, PvzpCustomLevels::Inspect(board));
    board->mPaused = false;
    board->mTimeStopCounter = 15;
    for (int i = 0; i < 9; ++i) PvzpCustomLevels::Update(board);
    const bool stopped = Same(before, PvzpCustomLevels::Inspect(board));
    board->mTimeStopCounter = 0;
    auto* app = board->mApp;
    app->DoDialog(Dialogs::DIALOG_MESSAGE, true, "LTEST", "Director guard", "[DIALOG_BUTTON_OK]", Sexy::Dialog::BUTTONS_FOOTER);
    // Deliberately clear mPaused: this verifies the dialog guard independently.
    board->mPaused = false;
    for (int i = 0; i < 9; ++i) PvzpCustomLevels::Update(board);
    const bool dialog = Same(before, PvzpCustomLevels::Inspect(board));
    board->mPaused = true; // let normal ModalClose restore audio as well
    app->KillDialog(Dialogs::DIALOG_MESSAGE);
    board->mPaused = false;
    return Check(t, paused && stopped && dialog, "DIRECT_director_pause_timestop_dialog_guards_no_tick_or_spawn");
}

bool PortableRoundtrip(Test& t, Board* board)
{
    auto* app = board->mApp;
    const std::string path = GetSavedGameName(GAMEMODE_MOD_LEVEL, app->mPlayerInfo->mId);
    Zombie* variant = PvzpCustomZombies::Spawn(board, "pvzp.seed_packet_head", 2);
    if (!Check(t, variant && PvzpCustomZombies::Id(variant) == "pvzp.seed_packet_head",
        "DIRECT_add_live_variant_fixture_before_full_portable_roundtrip")) return false;
    const std::string payload = PvzpCustomLevels::SavePayload(board);
    const State before = PvzpCustomLevels::Inspect(board);
    const auto variants = PvzpCustomZombies::Save(board);
    if (!Check(t, LawnSaveGame(board, path) && app->FileExists(path), "full_portable_custom_checkpoint_written")) return false;
    if (!Check(t, LawnLoadGame(board, path) && Same(before, PvzpCustomLevels::Inspect(board)) &&
        PvzpCustomLevels::SavePayload(board) == payload && PvzpCustomZombies::Save(board) == variants,
        "full_portable_entities_variants_director_roundtrip_exact_midwave")) return false;
    const std::string badPath = Sexy::GetAppDataPath("userdata/level_regression_bad.v4");
    auto corrupted = Bytes(path);
    if (!corrupted.empty()) corrupted[0] ^= 0xFF;
    if (!Check(t, !corrupted.empty() && app->WriteBytesToFile(badPath, corrupted.data(), int(corrupted.size())) &&
        !LawnLoadGame(board, badPath), "malformed_full_portable_file_rejected")) return false;
    if (!Check(t, LawnLoadGame(board, path) && PvzpCustomLevels::SavePayload(board) == payload,
        "failed_import_discarded_then_valid_checkpoint_restores_exact_state")) return false;
    const int entities = board->mZombies.mSize;
    Check(t, !PvzpCustomLevels::StageLoadPayload(board, payload + " trailing") &&
        PvzpCustomLevels::SavePayload(board) == payload, "director_trailing_payload_rejected_without_state_mutation");
    std::string wrongVersion = payload;
    wrongVersion.replace(0, 12, "PVZP_LEVEL 9 ");
    Check(t, !PvzpCustomLevels::StageLoadPayload(board, wrongVersion), "director_unknown_version_rejected");
    Check(t, !PvzpCustomLevels::StageLoadPayload(board, std::string(1025, 'x')), "director_oversized_payload_rejected");
    if (!Check(t, PvzpCustomLevels::StageLoadPayload(board, payload) &&
        PvzpCustomLevels::Inspect(board).pendingLoad && board->mZombies.mSize == entities,
        "director_stage_defers_entity_and_active_counter_mutation")) return false;
    if (!Check(t, PvzpCustomLevels::ApplyPendingLoad(board) && Same(before, PvzpCustomLevels::Inspect(board)),
        "director_apply_after_entities_restores_exact_counters")) return false;
    return Check(t, ProfileUnchanged(t, app) && Bytes(t.adventurePath) == t.adventureBytes &&
        Bytes(t.survivalPath) == t.survivalBytes, "portable_roundtrip_does_not_touch_vanilla_progress_or_files");
}

void KillHostiles(Board* board)
{
    for (Zombie* zombie : board->mZombies)
        if (!zombie->mDead && !zombie->mMindControlled) zombie->DieNoLoot();
    board->ProcessDeleteQueue();
}

void MenuAndFinish(Test& t, LawnApp* app)
{
    auto* selector = app->mGameSelector.get();
    if (!Check(t, selector && selector->mCustomLevelsButton, "loss_main_menu_retains_added_native_selector_entry"))
    { Finish(t, app); return; }
    // Skip only the selector's entry animation. Exercise its actual dispatch,
    // not the layout/artwork (screenshots are a separate visual test).
    selector->mSelectorState = SELECTOR_IDLE;
    selector->mCustomLevelsButton->SetDisabled(false);
    selector->ButtonDepress(selector->mCustomLevelsButton->mId);
    Check(t, dynamic_cast<CustomLevelDialog*>(app->GetDialog(PvzpCustomLevels::PickerDialogId)) != nullptr,
        "main_menu_native_entry_dispatches_to_level_picker");
    app->KillDialog(PvzpCustomLevels::PickerDialogId);
    Check(t, ProfileUnchanged(t, app), "three_levels_win_loss_retry_menu_preserve_adventure_records_purchases_unlocks");
    Check(t, Bytes(t.adventurePath) == t.adventureBytes && Bytes(t.survivalPath) == t.survivalBytes,
        "all_test_paths_leave_vanilla_checkpoint_files_untouched");
    // Empty-catalog behavior is tested only after every gameplay snapshot has
    // been checked. This tooling reset is never used to shortcut live levels.
    const auto empty = std::filesystem::path(Sexy::GetAppDataPath("level_regression_empty"));
    std::error_code error;
    std::filesystem::create_directories(empty, error);
    const bool loaded = !error && PvzpMod::LoadFromDirectory(empty);
    PvzpCustomLevels::OpenPicker(app);
    auto* picker = dynamic_cast<CustomLevelDialog*>(app->GetDialog(PvzpCustomLevels::PickerDialogId));
    if (picker) picker->ButtonDepress(112); // Play is inert for an empty catalog.
    Check(t, loaded && PvzpMod::CustomLevels().empty() && picker && !app->mBoard &&
        app->GetDialog(PvzpCustomLevels::PickerDialogId) == picker,
        "empty_catalog_native_picker_informative_and_play_inert");
    Check(t, t.naturalFirstWaves == 3 && t.directWins == 3,
        "coverage_three_natural_first_waves_and_three_DIRECT_finite_completions", t.naturalFirstWaves, t.directWins);
    Finish(t, app);
}
}
#endif

void PvzpRunLevelRegression(Board* board)
{
#if defined(__linux__) && !defined(__ANDROID__)
    if (!PvzpLevelRegressionEnabled() || !board || !board->mApp) return;
    static Test test;
    Test& t = test;
    if (t.finished) return;
    auto* app = board->mApp;
    ++t.frames;
    if (!t.begun)
    {
        t.begun = true;
        SDL_Log("LTEST BEGIN natural chooser/flights/first waves; later completions are DIRECT director checks; use disposable HOME");
        if (!Begin(t, board)) { Finish(t, app); return; }
    }
    if (t.board != board)
    {
        t.board = board;
        t.boardFrames = 0;
        t.selected = 0;
        t.phase = t.resuming ? 3 : 0;
    }
    ++t.boardFrames;
    if (t.frames > 18000 || t.directTicks > 6500000ULL)
    { Check(t, false, "bounded_level_test_timeout", int(app->mGameScene)); Finish(t, app); return; }
    if (!PvzpCustomLevels::IsActive(board))
    { Check(t, false, "dedicated_custom_mode_never_falls_back_to_vanilla"); Finish(t, app); return; }
    if (t.phase == 0)
    {
        const State state = PvzpCustomLevels::Inspect(board);
        bool fresh = Check(t, state.phase == Phase::WaitingForSeeds, "fresh_director_waits_for_seeds", int(state.phase));
        fresh &= Check(t, state.nextWave == 0 && state.spawned == 0 && state.ticks == 0, "fresh_director_counters_zero", state.nextWave, state.spawned);
        fresh &= Check(t, state.levelId == t.levels[t.level], "fresh_director_has_selected_stable_id");
        fresh &= Check(t, board->mBackground == BACKGROUND_1_DAY, "fresh_director_uses_day_background", int(board->mBackground));
        fresh &= Check(t, board->mSeedBank->mNumPackets == 8, "fresh_sandbox_has_eight_seed_slots", board->mSeedBank->mNumPackets);
        fresh &= Check(t, app->mGameScene != SCENE_PLAYING || board->mShowShovel, "fresh_tools_follow_native_intro_visibility");
        fresh &= Check(t, app->HasSeedType(SEED_IMITATER) && app->GetSeedsAvailable() == 49, "fresh_sandbox_seed_ownership_without_profile_unlock", app->GetSeedsAvailable());
        if (!fresh) { Finish(t, app); return; }
        if (t.lossRetried)
        {
            // A second real loss exercises Menu after the prior loss's Retry.
            board->ZombiesWon(nullptr);
            auto* result = dynamic_cast<CustomLevelResultDialog*>(app->GetDialog(PvzpCustomLevels::ResultDialogId));
            if (!Check(t, result && PvzpCustomLevels::Inspect(board).phase == Phase::Lost, "real_loss_after_retry_owns_result_dialog"))
            { Finish(t, app); return; }
            result->ButtonDepress(Sexy::Dialog::ID_NO);
            MenuAndFinish(t, app);
            return; // old Board is queued for SafeDelete; never read it again
        }
        t.phase = 1;
    }
    if (t.phase == 1)
    {
        auto* chooser = app->mSeedChooserScreen.get();
        if (!chooser || !chooser->mMouseVisible || chooser->mX != 0 || chooser->mY != 0 ||
            !board->mCutScene->mSeedChoosing || app->GetDialogCount()) return;
        const State state = PvzpCustomLevels::Inspect(board);
        if (state.ticks != 0 || state.spawned != 0)
        { Check(t, false, "natural_chooser_does_not_advance_wave_director"); Finish(t, app); return; }
        if (chooser->mSeedsInFlight) return; // natural Widget updates land it
        if (t.selected < 8)
        {
            const SeedType seed = Seeds[t.selected];
            int x = 0, y = 0;
            chooser->GetSeedPositionInChooser(seed, x, y);
            x += SEED_PACKET_WIDTH / 2; y += SEED_PACKET_HEIGHT / 2;
            if (!Check(t, chooser->ChooserHasSeed(seed) && chooser->SeedHitTest(x, y) == seed,
                "real_owned_chooser_packet_hit", int(seed))) { Finish(t, app); return; }
            Click(chooser, x, y);
            if (!Check(t, chooser->mSeedsInFlight == 1 && chooser->mChosenSeeds[seed].mSeedState == SEED_FLYING_TO_BANK,
                "real_packet_click_starts_natural_flight", int(seed))) { Finish(t, app); return; }
            ++t.selected;
            return;
        }
        if (!Check(t, chooser->mSeedsInBank == 8 && !chooser->mStartButton->mDisabled && state.ticks == 0 && state.spawned == 0,
            "eight_natural_flights_fill_bank_enable_lets_rock_without_advancing_director")) { Finish(t, app); return; }
        auto* start = chooser->mStartButton.get();
        Click(chooser, start->mX + start->mWidth / 2, start->mY + start->mHeight / 2);
        bool exact = !board->mCutScene->mSeedChoosing;
        for (int i = 0; i < 8; ++i) exact = exact && board->mSeedBank->mSeedPackets[i].mPacketType == Seeds[i];
        if (!Check(t, exact, "real_lets_rock_click_preserves_exact_bank_and_closes_chooser")) { Finish(t, app); return; }
        t.previous = PvzpCustomLevels::Inspect(board);
        t.phase = 2;
        return;
    }
    if (t.phase == 2)
    {
        if (app->mGameScene != SCENE_PLAYING) return;
        if (!board->mShowShovel) { Check(t,false,"playing_custom_mode_has_shovel_without_profile_unlock"); Finish(t,app); return; }
        if (t.preparingLoss)
        {
            board->ZombiesWon(nullptr);
            auto* result = dynamic_cast<CustomLevelResultDialog*>(app->GetDialog(PvzpCustomLevels::ResultDialogId));
            if (!Check(t, result && PvzpCustomLevels::Inspect(board).phase == Phase::Lost && !board->mLevelComplete,
                "real_ZombiesWon_custom_loss_no_vanilla_completion_or_award")) { Finish(t, app); return; }
            t.lossRetried = true;
            result->ButtonDepress(Sexy::Dialog::ID_YES);
            return;
        }
        const auto* level = Level(t.levels[t.level]);
        const State state = PvzpCustomLevels::Inspect(board);
        if (state.nextWave == 0)
        {
            const bool advancing = state.ticks == t.previous.ticks + 1 && state.countdown == t.previous.countdown - 1;
            if (!advancing || state.alive != 0 || state.spawned != 0)
            { Check(t, false, "natural_pre_wave_timer_exact_no_background_vanilla_zombies", state.countdown); Finish(t, app); return; }
            if (state.ticks == 1 || state.ticks % 1000 == 0)
                Check(t, true, "natural_pre_wave_timer_exact_no_background_vanilla_zombies", state.countdown);
            t.previous = state;
            return;
        }
        if (!Check(t, level && state.nextWave == 1 && state.ticks == uint64_t(level->waves.front().delay) &&
            state.spawned == WaveSize(level->waves.front()) && state.alive == state.spawned &&
            board->mCurrentWave == 1 && board->mTotalSpawnedWaves == 1,
            "natural_first_wave_spawns_exact_catalog_count_no_vanilla_conflict", state.spawned, state.alive))
        { Finish(t, app); return; }
        ++t.naturalFirstWaves;
        if (!DirectGuards(t, board)) { Finish(t, app); return; }
        if (t.level == 0)
        {
            if (!PortableRoundtrip(t, board)) { Finish(t, app); return; }
            t.saved = PvzpCustomLevels::Inspect(board);
            t.resuming = true;
            PvzpCustomLevels::OpenPicker(app);
            auto* picker = dynamic_cast<CustomLevelDialog*>(app->GetDialog(PvzpCustomLevels::PickerDialogId));
            if (!Check(t, picker && PvzpCustomLevels::CanContinue(app), "native_continue_available_for_isolated_checkpoint"))
            { Finish(t, app); return; }
            picker->ButtonDepress(111); // highlight another catalog item
            picker->ButtonDepress(114); // Continue saved stable ID, not highlight
            return;
        }
        t.phase = 4;
    }
    if (t.phase == 3)
    {
        const State state = PvzpCustomLevels::Inspect(board);
        if (!Check(t, !app->mSeedChooserScreen && state.levelId == t.saved.levelId && state.nextWave == t.saved.nextWave &&
            state.spawned == t.saved.spawned && state.ticks == t.saved.ticks + 1 && state.countdown == t.saved.countdown - 1,
            "native_continue_resumes_saved_stable_ID_and_next_natural_tick_not_highlight"))
        { Finish(t, app); return; }
        t.resuming = false;
        t.phase = 4;
    }
    if (t.phase == 4)
    {
        // Explicit DIRECT simulation. Do not recursively update Board/chooser.
        // Zombies are removed by the test; this proves finite waves/objective
        // result flow, not player balance or natural combat completion.
        for (int i = 0; i < 512 && PvzpCustomLevels::Inspect(board).phase == Phase::Running; ++i)
        {
            KillHostiles(board);
            PvzpCustomLevels::Update(board);
            ++t.directTicks;
        }
        const State state = PvzpCustomLevels::Inspect(board);
        if (state.phase == Phase::Running) return;
        auto* result = dynamic_cast<CustomLevelResultDialog*>(app->GetDialog(PvzpCustomLevels::ResultDialogId));
        if (!Check(t, state.phase == Phase::Won && result && state.nextWave == state.totalWaves && state.alive == 0 &&
            app->mBoardResult == BOARDRESULT_WON && !board->mLevelComplete && ProfileUnchanged(t, app),
            "DIRECT_all_catalog_waves_exhausted_actual_custom_win_dialog_no_profile_awards", state.nextWave, state.totalWaves))
        { Finish(t, app); return; }
        ++t.directWins;
        if (t.level < 2)
        {
            app->KillDialog(PvzpCustomLevels::ResultDialogId);
            ++t.level;
            if (!PvzpCustomLevels::Start(app, t.levels[t.level])) { Check(t, false, "start_next_real_level"); Finish(t, app); }
            return; // old Board SafeDelete; no pointer use after Start
        }
        t.preparingLoss = true;
        result->ButtonDepress(Sexy::Dialog::ID_YES); // real win Retry starts fresh
        return;
    }
#else
    (void)board;
#endif
}
