// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpCustomLevels.h"
#include "PvzpMod.h"
#include "PvzpCustomZombies.h"
#include "PvzpLocale.h"
#include "PvzpCommon.h"
#include "../LawnApp.h"
#include "../Lawn/Board.h"
#include "../Lawn/SeedPacket.h"
#include "../Lawn/LawnCommon.h"
#include "../Lawn/System/PlayerInfo.h"
#include "../Lawn/Widget/CustomLevelDialog.h"
#include "../Resources.h"
#include "graphics/Graphics.h"
#include "widget/WidgetManager.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace PvzpCustomLevels
{
namespace
{
struct Director
{
    State state;
    uint64_t fingerprint = 0, revision = 0;
    State pending;
    bool hasPending = false;
};
std::unordered_map<const Board*, Director> gDirectors;
std::string gSelectedId;

const PvzpMod::CustomLevel* Find(std::string_view id)
{
    for (const auto& level : PvzpMod::CustomLevels())
        if (level.id == id) return &level;
    return nullptr;
}

bool Valid(const PvzpMod::CustomLevel& level)
{
    if (level.id.empty() || level.id.size() > 128 || level.waves.empty() ||
        level.waves.size() > 100 || level.startingSun < 0 || level.startingSun > 9990) return false;
    for (const auto& wave : level.waves)
    {
        if (wave.delay < 1 || wave.delay > 60000 || wave.spawns.empty() || wave.spawns.size() > 50) return false;
        int count = 0;
        for (const auto& spawn : wave.spawns)
        {
            if (spawn.zombie.empty() || spawn.zombie.size() > 128 || !PvzpCustomZombies::Exists(spawn.zombie) || spawn.row < -1 || spawn.row > 4 ||
                spawn.count < 1 || spawn.count > 50) return false;
            count += spawn.count;
        }
        if (count > 50) return false;
    }
    return true;
}

uint64_t Fingerprint(const PvzpMod::CustomLevel& level,bool legacy=false)
{
    // Include separators/lengths so different spawn grouping cannot alias by
    // concatenation. No pointers or catalog indices survive a snapshot.
    uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](std::string_view value)
    {
        for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; }
        hash ^= 255; hash *= 1099511628211ULL;
    };
    add(std::to_string(legacy?PvzpMod::RulesFingerprint():PvzpMod::GameplayFingerprint()));
    add(level.id); add(std::to_string(level.startingSun));
    add(std::to_string(level.waves.size()));
    for (const auto& wave : level.waves)
    {
        add(std::to_string(wave.delay)); add(std::to_string(wave.spawns.size()));
        for (const auto& spawn : wave.spawns)
        {
            add(spawn.zombie); add(std::to_string(spawn.row)); add(std::to_string(spawn.count));
        }
    }
    return hash;
}

int Alive(const Board* board)
{
    int count = 0;
    // DataArray's range interface is mutable, but inspection never changes it.
    for (Zombie* zombie : const_cast<Board*>(board)->mZombies)
        if (!zombie->mDead && !zombie->IsDeadOrDying() && !zombie->mMindControlled) ++count;
    return count;
}

bool SeedsReady(const Board* board)
{
    if (board->mApp->mSeedChooserScreen || !board->mSeedBank ||
        board->mSeedBank->mNumPackets < 1 || board->mSeedBank->mNumPackets > SEEDBANK_MAX) return false;
    for (int i = 0; i < board->mSeedBank->mNumPackets; ++i)
        if (board->mSeedBank->mSeedPackets[i].mPacketType == SeedType::SEED_NONE) return false;
    return true;
}

void ShowResult(Board* board, const State& state)
{
    if (board->mApp->GetDialog(ResultDialogId)) return;
    board->mPaused = true;
    board->mApp->mBoardResult = state.phase == Phase::Won ? BoardResult::BOARDRESULT_WON : BoardResult::BOARDRESULT_LOST;
    board->StopAllZombieSounds();
    auto* dialog = new CustomLevelResultDialog(board->mApp, state.levelId,
        state.phase == Phase::Won, state.phase == Phase::Error);
    board->mApp->AddDialog(dialog);
    board->mApp->mWidgetManager->SetFocus(dialog);
}

void Finish(Board* board, Director& director, Phase phase)
{
    director.state.phase = phase;
    board->mApp->mBoardResult = phase == Phase::Won ? BoardResult::BOARDRESULT_WON : BoardResult::BOARDRESULT_LOST;
    // Never set mLevelComplete: LawnApp's vanilla completion path awards
    // challenge records indexed by game mode. The result dialog owns exit.
    ShowResult(board, director.state);
}
}

std::string Text(LawnApp* app, const char* key, const char* english, const char* chinese)
{
    // Registering rebuilds the locale reverse map. Do it only once per key
    // per generation, not for every HUD draw or selector update.
    static std::unordered_map<std::string, uint64_t> registered;
    const uint64_t generation = PvzpLocaleGetGeneration();
    auto it = registered.find(english);
    if (it == registered.end() || it->second != generation)
    {
        PvzpLocaleRegisterLiteral(english, chinese);
        registered[english] = generation;
    }
    return std::string(PvzpLocaleTranslateLiteral(app->GetString(key, english)));
}

bool IsMode(const LawnApp* app)
{
    return app && app->mGameMode == GameMode::GAMEMODE_MOD_LEVEL;
}

bool IsActive(const Board* board)
{
    return board && IsMode(board->mApp);
}

void OpenPicker(LawnApp* app)
{
    if (!app || app->GetDialog(PickerDialogId)) return;
    auto* picker = new CustomLevelDialog(app);
    app->AddDialog(picker);
    app->mWidgetManager->SetFocus(picker);
}

bool Start(LawnApp* app, std::string_view id)
{
    const auto* level = Find(id);
    if (!app || !app->mPlayerInfo || !level || !Valid(*level)) return false;
    gSelectedId = level->id;
    app->RegistryWriteString("CustomLevelSelected",gSelectedId);
    app->KillGameSelector();
    app->PreNewGame(GameMode::GAMEMODE_MOD_LEVEL, false);
    return true;
}

bool CanContinue(LawnApp* app)
{
    return app && app->mPlayerInfo && app->FileExists(
        GetSavedGameName(GameMode::GAMEMODE_MOD_LEVEL, app->mPlayerInfo->mId));
}

bool Continue(LawnApp* app)
{
    if (!CanContinue(app)) return false;
    // Init cannot accidentally restart the highlighted catalog item when a
    // checkpoint is missing/invalid: only Stage/Apply supplies its stable ID.
    gSelectedId.clear();
    app->KillGameSelector();
    app->PreNewGame(GameMode::GAMEMODE_MOD_LEVEL, true);
    return true;
}

void Init(Board* board)
{
    if (!IsActive(board)) return;
    Director director;
    director.state.levelId = gSelectedId;
    const auto* level = Find(gSelectedId);
    bool terrainValid=level && Valid(*level);
    if(terrainValid)for(const auto& wave:level->waves)for(const auto& spawn:wave.spawns)
        if(!PvzpCustomZombies::CanSpawn(board,spawn.zombie,spawn.row))terrainValid=false;
    if (!terrainValid) director.state.phase = Phase::Error;
    else
    {
        director.state.totalWaves = static_cast<int>(level->waves.size());
        director.state.countdown = level->waves.front().delay;
        director.fingerprint = Fingerprint(*level);
        director.revision = PvzpMod::SnapshotRevision();
        board->mSunMoney = level->startingSun;
    }
    gDirectors[board] = std::move(director);
    // Keep nonzero finite values for legacy HUD arithmetic, but do not let
    // vanilla spawn/end/progress routines consume this table.
    board->mNumWaves = level && Valid(*level) ? static_cast<int>(level->waves.size()) : 1;
    board->mCurrentWave = 0;
    board->mTotalSpawnedWaves = 0;
    board->mZombieCountDown = board->mZombieCountDownStart = 60000;
    board->mHugeWaveCountDown = 0;
    board->mBoardFadeOutCounter = -1;
    board->mLevelComplete = false;
    board->mTutorialState = TutorialState::TUTORIAL_OFF;
    board->mShowShovel = true;
    for (auto& wave : board->mZombiesInWave)
        for (auto& zombie : wave) zombie = ZombieType::ZOMBIE_INVALID;
}

void Update(Board* board)
{
    if (!IsActive(board)) return;
    auto it = gDirectors.find(board);
    if (it == gDirectors.end()) { Init(board); it = gDirectors.find(board); }
    Director& director = it->second;
    State& state = director.state;
    if (director.hasPending) return;
    state.alive = Alive(board);
    if (state.phase == Phase::Won || state.phase == Phase::Lost || state.phase == Phase::Error)
    {
        if (board->mApp->GetDialogCount() == 0) ShowResult(board, state);
        return;
    }
    if (board->mPaused || board->mTimeStopCounter > 0 || board->mApp->GetDialogCount() != 0 ||
        board->mApp->mGameScene != GameScenes::SCENE_PLAYING || !SeedsReady(board)) return;
    const auto* level = Find(state.levelId);
    if (!level || (director.revision != PvzpMod::SnapshotRevision() &&
        (!Valid(*level) || Fingerprint(*level) != director.fingerprint)))
    { Finish(board, director, Phase::Error); return; }
    director.revision = PvzpMod::SnapshotRevision();
    state.phase = Phase::Running;
    if (state.ticks < 1000000000ULL) ++state.ticks;
    if (state.nextWave >= state.totalWaves)
    {
        if (!state.alive) Finish(board, director, Phase::Won);
        return;
    }
    if (state.countdown > 0 && --state.countdown > 0) return;
    const auto& wave = level->waves[state.nextWave];
    for (const auto& spawn : wave.spawns)
        for (int i = 0; i < spawn.count; ++i)
        {
            // Deterministic lane rotation uses only persisted spawn count; no
            // hidden random-generator state and no borrowed entity pointers.
            int row = spawn.row < 0 ? state.spawned % 5 : spawn.row;
            Zombie* zombie = PvzpCustomZombies::Spawn(board, spawn.zombie, row);
            if (!zombie) { Finish(board, director, Phase::Error); return; }
            ++state.spawned;
        }
    ++state.nextWave;
    board->mCurrentWave = state.nextWave;
    board->mTotalSpawnedWaves = state.nextWave;
    state.countdown = state.nextWave < state.totalWaves ? level->waves[state.nextWave].delay : 0;
    state.alive = Alive(board);
}

void Draw(Board* board, Sexy::Graphics* g)
{
    if (!IsActive(board) || !g) return;
    auto it = gDirectors.find(board);
    if (it == gDirectors.end()) return;
    const State& state = it->second.state;
    const auto* level = Find(state.levelId);
    std::string title = level ? std::string(PvzpLocaleTranslateLiteral(level->name)) : state.levelId;
    std::string objective = Text(board->mApp, "CUSTOM_LEVEL_OBJECTIVE", "Protect the house. Defeat every wave.", "保卫房屋，消灭所有波次的僵尸。");
    std::string progress = Text(board->mApp, "CUSTOM_LEVEL_WAVE", "Waves", "波次") + " " +
        std::to_string(state.nextWave) + "/" + std::to_string(state.totalWaves);
    if (state.phase == Phase::WaitingForSeeds)
        progress += "  " + Text(board->mApp, "CUSTOM_LEVEL_CHOOSE", "Choose your seeds", "请选择植物");
    else if (state.nextWave < state.totalWaves)
        progress += "  " + Text(board->mApp, "CUSTOM_LEVEL_NEXT", "Next in", "下一波倒计时") + " " + std::to_string((state.countdown + 99) / 100) + "s";
    else progress += "  " + Text(board->mApp, "CUSTOM_LEVEL_REMAINING", "Remaining", "剩余僵尸") + " " + std::to_string(Alive(board));
    g->PushState();
    g->ClipRect(210, 88, 570, 66);
    g->SetColor(Sexy::Color(30, 25, 15, 210));
    g->FillRect(210, 88, 570, 66);
    PvzpDrawString(g, title, 220, 107, Sexy::FONT_BRIANNETOD16, Sexy::Color(255, 230, 140), DS_ALIGN_LEFT);
    PvzpDrawString(g, objective, 220, 126, Sexy::FONT_BRIANNETOD12, Sexy::Color(255, 246, 205), DS_ALIGN_LEFT);
    PvzpDrawString(g, progress, 220, 145, Sexy::FONT_BRIANNETOD12, Sexy::Color(255, 246, 205), DS_ALIGN_LEFT);
    g->PopState();
}

void Lose(Board* board)
{
    if (!IsActive(board)) return;
    auto it = gDirectors.find(board);
    if (it == gDirectors.end()) return;
    if (it->second.state.phase != Phase::Running && it->second.state.phase != Phase::WaitingForSeeds) return;
    Finish(board, it->second, Phase::Lost);
}

void Forget(Board* board) { gDirectors.erase(board); }

State Inspect(const Board* board)
{
    auto it = gDirectors.find(board);
    if (it == gDirectors.end()) return {};
    State state = it->second.state;
    state.alive = Alive(board);
    state.pendingLoad = it->second.hasPending;
    return state;
}

std::string SavePayload(const Board* board)
{
    auto it = gDirectors.find(board);
    if (!IsActive(board) || it == gDirectors.end() || it->second.hasPending) return {};
    const State& s = it->second.state;
    if (s.phase == Phase::Error) return {}; // never overwrite a good checkpoint with a broken catalog/spawn
    std::ostringstream out;
    out << "PVZP_LEVEL 2 " << std::quoted(s.levelId) << ' ' << it->second.fingerprint << ' '
        << static_cast<int>(s.phase) << ' ' << s.nextWave << ' ' << s.countdown << ' ' << s.spawned << ' ' << s.ticks;
    return out.str();
}

bool StageLoadPayload(Board* board, std::string_view payload)
{
    if (!IsActive(board) || payload.empty() || payload.size() > 1024) return false;
    auto& director = gDirectors[board];
    State state;
    uint64_t fingerprint = 0;
    std::string magic;
    int version = 0, phase = -1;
    std::istringstream in{std::string(payload)};
    if (!(in >> magic >> version >> std::quoted(state.levelId) >> fingerprint >> phase >> state.nextWave >>
          state.countdown >> state.spawned >> state.ticks)) return false;
    in >> std::ws;
    const auto* level = Find(state.levelId);
    if (!in.eof() || magic != "PVZP_LEVEL" || (version != 1 && version != 2) || !level || !Valid(*level) ||
        fingerprint != Fingerprint(*level,version==1) || phase < 0 || phase > static_cast<int>(Phase::Error) ||
        state.nextWave < 0 || state.nextWave > static_cast<int>(level->waves.size()) ||
        state.countdown < 0 || state.countdown > 60000 || state.spawned < 0 || state.spawned > 5000 ||
        state.ticks > 1000000000ULL) return false;
    state.phase = static_cast<Phase>(phase);
    state.totalWaves = static_cast<int>(level->waves.size());
    int expectedSpawned = 0;
    for (int i = 0; i < state.nextWave; ++i)
        for (const auto& spawn : level->waves[i].spawns) expectedSpawned += spawn.count;
    // Errors can occur part-way through a wave, but are not resumable saves.
    if (state.phase == Phase::Error || state.spawned != expectedSpawned ||
        (state.nextWave == state.totalWaves && state.countdown != 0) ||
        (state.nextWave < state.totalWaves && (state.countdown < 1 || state.countdown > level->waves[state.nextWave].delay)) ||
        (state.phase == Phase::WaitingForSeeds && (state.nextWave != 0 || state.ticks != 0 || state.countdown != level->waves.front().delay)) ||
        (state.phase == Phase::Won && state.nextWave != state.totalWaves)) return false;
    director.fingerprint = Fingerprint(*level); // migrate valid v1 checkpoints to v2 identity
    director.revision = PvzpMod::SnapshotRevision();
    director.pending = state;
    director.hasPending = true;
    return true;
}

bool ApplyPendingLoad(Board* board)
{
    auto it = gDirectors.find(board);
    if (!IsActive(board) || it == gDirectors.end() || !it->second.hasPending) return false;
    Director& director = it->second;
    const auto* level = Find(director.pending.levelId);
    if (!level || Fingerprint(*level) != director.fingerprint ||
        board->mBackground != BackgroundType::BACKGROUND_1_DAY || board->mLevel != 0 ||
        board->mSeedBank->mNumPackets != 8 || board->mSunMoney < 0 || board->mSunMoney > 9990) return false;
    for (int row = 0; row < 5; ++row)
        if (board->mPlantRow[row] != PlantRowType::PLANTROW_NORMAL) return false;
    for (int i = 0; i < 8; ++i)
        if (board->mSeedBank->mSeedPackets[i].mPacketType == SeedType::SEED_NONE) return false;
    director.pending.alive = Alive(board);
    if (director.pending.phase == Phase::Won && director.pending.alive != 0) return false;
    director.state = director.pending;
    director.hasPending = false;
    gSelectedId = director.state.levelId;
    board->mNumWaves = director.state.totalWaves;
    board->mCurrentWave = board->mTotalSpawnedWaves = director.state.nextWave;
    board->mLevelComplete = false;
    board->mBoardFadeOutCounter = -1;
    board->mRiseFromGraveCounter = board->mHugeWaveCountDown = 0;
    board->mNextSurvivalStageCounter = 0;
    board->mZombieCountDown = board->mZombieCountDownStart = 60000;
    board->mShowShovel = true;
    board->mLevelAwardSpawned = false;
    board->mTutorialState = TutorialState::TUTORIAL_OFF;
    board->mPaused = false;
    board->mApp->mBoardResult = director.state.phase == Phase::Won ? BoardResult::BOARDRESULT_WON :
        director.state.phase == Phase::Lost ? BoardResult::BOARDRESULT_LOST : BoardResult::BOARDRESULT_NONE;
    return true;
}
}
