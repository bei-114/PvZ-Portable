// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

class Board;
class LawnApp;
namespace Sexy { class Graphics; }

// A finite-wave director. Dedicated GAMEMODE_MOD_LEVEL must not use vanilla
// challenge-record indices, level awards, adventure progress or survival saves.
namespace PvzpCustomLevels
{
    constexpr int PickerDialogId = 18000;
    constexpr int ResultDialogId = 18001;
    enum class Phase { WaitingForSeeds, Running, Won, Lost, Error };
    struct State
    {
        std::string levelId;
        Phase phase = Phase::WaitingForSeeds;
        int nextWave = 0, totalWaves = 0, countdown = 0;
        int spawned = 0, alive = 0;
        uint64_t ticks = 0;
        bool pendingLoad = false;
    };

    bool IsMode(const LawnApp* app);
    bool IsActive(const Board* board);
    void OpenPicker(LawnApp* app);
    // Fresh play replaces only this profile's last custom-level checkpoint.
    bool Start(LawnApp* app, std::string_view levelId);
    bool CanContinue(LawnApp* app);
    // Resumes the payload's stable ID, never the highlighted picker item.
    bool Continue(LawnApp* app);
    // End of Board::InitLevel, after seed bank/background/tool initialization.
    void Init(Board* board);
    // Replaces vanilla UpdateZombieSpawning entirely in custom mode.
    void Update(Board* board);
    void Draw(Board* board, Sexy::Graphics* graphics);
    // Top of ZombiesWon: if IsMode, Lose and return (no vanilla game-over flow).
    void Lose(Board* board);
    void Forget(Board* board);

    // Snapshot contents contain version, stable ID, rules+level fingerprint and
    // bounded scalar counters only. The parent bounds the outer string to 1024
    // bytes before allocation. Stage never mutates entities. Apply only AFTER
    // all entity arrays and variant state have been restored. False aborts load.
    std::string SavePayload(const Board* board);
    bool StageLoadPayload(Board* board, std::string_view payload);
    bool ApplyPendingLoad(Board* board);
    State Inspect(const Board* board);

    // Keys with fallbacks; registers project literals for live locale changes.
    std::string Text(LawnApp* app, const char* key, const char* english, const char* chinese);
}
