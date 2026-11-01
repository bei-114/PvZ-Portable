/*
 * Opt-in, destructive local regression scene. Never enabled by normal gameplay.
 * Run a Linux build with PVZP_ELECTRIC_TEST=1 and enter level 1-1.
 * Board::Update calls PvzpRunElectricRegression after its playing update.
 */
#include "../Lawn/Board.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Projectile.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/CursorObject.h"
#include "../LawnApp.h"
#include "../SexyAppFramework/widget/WidgetManager.h"
#include <SDL_log.h>
#include <algorithm>
#include <cstdlib>
#include <unordered_set>
#include <vector>

namespace
{
struct ElectricTestState
{
    Board* board = nullptr;
    int phase = 0;
    int ticks = 0;
    int failures = 0;
    unsigned int plant = 0;
    unsigned int cross = 0;
    unsigned int primary = 0;
    int crossStart = 0;
    int primaryStart = 0;
    int minCross = 2000;
    int maxChill = 0;
    int maxButter = 0;
    bool missingTarget = false;
    std::unordered_set<unsigned int> shots;
    std::vector<int> shotTicks;
};

void Result(ElectricTestState& state, bool pass, const char* name)
{
    SDL_Log("ETEST %s %s", pass ? "PASS" : "FAIL", name);
    if (!pass)
        ++state.failures;
}

int Projectiles(Board* board)
{
    int count = 0;
    for (Projectile* p : board->mProjectiles)
        if (!p->mDead)
            ++count;
    return count;
}

void PlaceX(Zombie* zombie, float x)
{
    if (zombie)
    {
        zombie->mPosX = x;
        zombie->mX = static_cast<int>(x);
    }
}

Zombie* MakeTarget(Board* board, int row, float x)
{
    Zombie* zombie = board->AddZombieInRow(ZombieType::ZOMBIE_NORMAL, row, Zombie::ZOMBIE_WAVE_DEBUG);
    if (zombie)
    {
        PlaceX(zombie, x);
        zombie->mBodyHealth = 2000;
        zombie->mBodyMaxHealth = 2000;
    }
    return zombie;
}

void Click(Board* board, int x, int y)
{
    // MouseDown's UpdateMousePosition reads the manager's absolute pointer.
    board->mApp->mWidgetManager->mLastMouseX = x + board->mX;
    board->mApp->mWidgetManager->mLastMouseY = y + board->mY;
    board->MouseMove(x, y);
    board->MouseDown(x, y, 1);
    board->MouseUp(x, y, 1);
}
}

void PvzpRunElectricRegression(Board* board)
{
#if defined(__linux__) && !defined(__ANDROID__)
    const char* enabled = std::getenv("PVZP_ELECTRIC_TEST");
    if (!enabled || !*enabled || !board || !board->mApp ||
        board->mApp->mGameScene != GameScenes::SCENE_PLAYING)
        return;

    static ElectricTestState state;
    if (state.board != board)
    {
        state = ElectricTestState{};
        state.board = board;
    }
    if (state.phase < 0)
        return;

    // Suppress natural waves throughout the observation, not just at setup.
    board->mZombieCountDown = 100000;
    board->mZombieCountDownStart = 100000;
    board->mHugeWaveCountDown = -1;
    board->mTutorialState = TutorialState::TUTORIAL_OFF;

    if (state.phase == 0)
    {
        SDL_Log("ETEST BEGIN destructive live-engine regression level=%d tick=%u", board->mLevel, board->mMainCounter);
        for (Plant* p : board->mPlants)
            if (!p->mDead)
                p->Die();
        for (Zombie* z : board->mZombies)
            if (!z->mDead)
                z->DieNoLoot();
        for (Projectile* p : board->mProjectiles)
            if (!p->mDead)
                p->Die();
        board->ClearCursor();
        board->ClearAdviceImmediately();
        board->mDbgOpen = false;
        board->mDbgPlantPending = false;
        board->mDbgConsumeMouseUp = false;
        for (int row = 0; row < 5; ++row)
        {
            board->mPlantRow[row] = PlantRowType::PLANTROW_NORMAL;
            for (int col = 0; col < MAX_GRID_SIZE_X; ++col)
                board->mGridSquareType[col][row] = GridSquareType::GRIDSQUARE_GRASS;
        }
        Plant* plant = board->AddPlant(1, 2, SeedType::SEED_ELECTRIC_GATLINGPEA);
        Zombie* cross = MakeTarget(board, 1, 600.0f);
        Result(state, plant && cross, "setup_allocations");
        if (!plant || !cross)
        {
            SDL_Log("ETEST FAIL prerequisites unavailable; done failures=%d", state.failures);
            state.phase = -1;
            return;
        }
        state.plant = board->mPlants.DataArrayGetID(plant);
        state.cross = board->mZombies.DataArrayGetID(cross);
        state.crossStart = cross->mBodyHealth;
        state.phase = 1;
        SDL_Log("ETEST phase=1 no-aura duration=180 cross_start_x=600 probe_x=480 row=1 health=%d", cross->mBodyHealth);
        // 480 is inside the old 320-pixel aura, while remaining off the shot row.
        PlaceX(cross, 480.0f);
        return;
    }

    Plant* plant = board->mPlants.DataArrayTryToGet(state.plant);
    Zombie* cross = board->mZombies.DataArrayTryToGet(state.cross);
    if (state.phase == 1)
    {
        ++state.ticks;
        if (!cross || cross->mDead)
            state.missingTarget = true;
        else
        {
            state.minCross = std::min(state.minCross, cross->mBodyHealth);
            state.maxChill = std::max(state.maxChill, cross->mChilledCounter);
            state.maxButter = std::max(state.maxButter, cross->mButteredCounter);
            PlaceX(cross, 480.0f);
        }
        if (state.ticks < 180)
            return;
        SDL_Log("ETEST no-aura observed ticks=%d hp_start=%d hp_min=%d chill_max=%d butter_max=%d missing=%d",
            state.ticks, state.crossStart, state.minCross, state.maxChill, state.maxButter, state.missingTarget);
        Result(state, !state.missingTarget && state.minCross == state.crossStart &&
            state.maxChill == 0 && state.maxButter == 0, "no_passive_aura");
        Zombie* primary = MakeTarget(board, 2, 480.0f);
        Result(state, primary != nullptr, "same_row_target_allocation");
        if (!primary)
        {
            SDL_Log("ETEST FAIL cannot observe automatic shooting; done failures=%d", state.failures);
            state.phase = -1;
            return;
        }
        state.primary = board->mZombies.DataArrayGetID(primary);
        state.primaryStart = primary->mBodyHealth;
        // Separate phase baselines: an aura failure must not falsely prove
        // chain damage in the next, independently reported observation.
        state.crossStart = cross ? cross->mBodyHealth : -1;
        state.ticks = 0;
        state.maxChill = 0;
        state.maxButter = 0;
        state.missingTarget = !cross;
        state.phase = 2;
        SDL_Log("ETEST phase=2 live-shoot duration=400 primary_x=480 row=2 hp=%d cross_hp=%d",
            primary->mBodyHealth, cross ? cross->mBodyHealth : -1);
        return;
    }

    if (state.phase != 2)
        return;
    ++state.ticks;
    Zombie* primary = board->mZombies.DataArrayTryToGet(state.primary);
    if (!primary || primary->mDead || !cross || cross->mDead)
        state.missingTarget = true;
    for (Zombie* z : {primary, cross})
    {
        if (!z || z->mDead)
            continue;
        state.maxChill = std::max(state.maxChill, z->mChilledCounter);
        state.maxButter = std::max(state.maxButter, z->mButteredCounter);
        PlaceX(z, 480.0f);
    }
    for (Projectile* p : board->mProjectiles)
    {
        if (p->mDead || !p->IsElectric())
            continue;
        if (state.shots.insert(board->mProjectiles.DataArrayGetID(p)).second)
        {
            state.shotTicks.push_back(state.ticks);
            if (state.shotTicks.size() <= 12)
                SDL_Log("ETEST shot n=%d tick=%d x=%.1f y=%.1f row=%d",
                    static_cast<int>(state.shotTicks.size()), state.ticks, p->mPosX, p->mPosY, p->mRow);
        }
    }
    if (state.ticks < 400)
        return;

    bool fourShotBurst = false;
    for (size_t i = 3; i < state.shotTicks.size(); ++i)
        if (state.shotTicks[i] - state.shotTicks[i - 1] == 8 &&
            state.shotTicks[i - 1] - state.shotTicks[i - 2] == 8 &&
            state.shotTicks[i - 2] - state.shotTicks[i - 3] == 8)
            fourShotBurst = true;
    const int primaryHP = primary ? primary->mBodyHealth : -1;
    const int crossHP = cross ? cross->mBodyHealth : -1;
    SDL_Log("ETEST live-shoot ticks=%d shots=%d primary_hp=%d->%d cross_hp=%d->%d chill_max=%d butter_max=%d burst8=%d",
        state.ticks, static_cast<int>(state.shots.size()), state.primaryStart, primaryHP,
        state.crossStart, crossHP, state.maxChill, state.maxButter, fourShotBurst);
    Result(state, !state.missingTarget && primaryHP < state.primaryStart && state.shots.size() >= 8 && fourShotBurst,
        "automatic_four_shot_damage");
    Result(state, !state.missingTarget && crossHP < state.crossStart, "actual_cross_row_chain_damage");
    Result(state, state.maxChill == 0 && state.maxButter > 0, "electric_stun_without_ice");

    // The remaining synchronous checks run after the observation update, so no
    // unrelated projectile movement can masquerade as damage between samples.
    if (plant && !plant->mDead && primary && !primary->mDead)
    {
        const int flags = plant->GetDamageRangeFlags(PlantWeapon::WEAPON_PRIMARY);
        Projectile* electric = board->AddProjectile(480, primary->mY, plant->mRenderOrder - 1, 2, ProjectileType::PROJECTILE_PEA);
        int before = primary->mBodyHealth;
        int first = before;
        int second = before;
        bool aliveAfterImpact = false;
        if (electric)
        {
            electric->mDamageRangeFlags = flags;
            electric->SetElectric();
            electric->DoImpact(primary);
            first = primary->mBodyHealth;
            electric->DoImpact(primary);
            second = primary->mBodyHealth;
            aliveAfterImpact = !electric->mDead;
        }
        SDL_Log("ETEST dedup hp=%d,%d,%d electric_alive=%d", before, first, second, aliveAfterImpact);
        Result(state, electric && before - first == 20 && first == second && aliveAfterImpact,
            "same_projectile_hits_target_once");
        if (electric)
            electric->Die();
        Projectile* ordinary = board->AddProjectile(480, primary->mY, plant->mRenderOrder - 1, 2, ProjectileType::PROJECTILE_PEA);
        before = primary->mBodyHealth;
        bool ordinaryDead = false;
        bool ordinaryElectric = true;
        if (ordinary)
        {
            ordinary->mDamageRangeFlags = flags;
            ordinaryElectric = ordinary->IsElectric();
            ordinary->DoImpact(primary);
            ordinaryDead = ordinary->mDead;
        }
        SDL_Log("ETEST ordinary hp=%d->%d dead=%d electric=%d", before, primary->mBodyHealth, ordinaryDead, ordinaryElectric);
        Result(state, ordinary && !ordinaryElectric && ordinaryDead && before - primary->mBodyHealth == 20,
            "ordinary_pea_damage_and_destruction");

        std::unordered_set<unsigned int> previous;
        for (Projectile* p : board->mProjectiles)
            previous.insert(board->mProjectiles.DataArrayGetID(p));
        board->FireElectricFan(plant);
        board->SpawnElectricFanVolley(plant);
        board->SpawnElectricFanVolley(plant);
        board->mFanVolleys = 0;
        int added = 0, electricCount = 0, up = 0, down = 0;
        for (Projectile* p : board->mProjectiles)
        {
            if (p->mDead || previous.contains(board->mProjectiles.DataArrayGetID(p)))
                continue;
            ++added;
            electricCount += p->IsElectric();
            up += p->mVelY < 0.0f;
            down += p->mVelY > 0.0f;
        }
        SDL_Log("ETEST fan added=%d electric=%d vy_negative=%d vy_positive=%d", added, electricCount, up, down);
        Result(state, added == 27 && electricCount == 27 && up == 12 && down == 12, "fan_27_real_electric_projectiles");

        board->mDbgOpen = false;
        board->mDbgPlantPending = false;
        board->ClearCursor();
        board->mShowShovel = true;
        const Rect shovel = board->GetShovelButtonRect();
        Click(board, shovel.mX + shovel.mWidth / 2, shovel.mY + shovel.mHeight / 2);
        const bool pickedUp = board->mCursorObject->mCursorType == CursorType::CURSOR_TYPE_SHOVEL;
        const int projectileCount = Projectiles(board);
        Click(board, board->GridToPixelX(1, 2) + 40, board->GridToPixelY(1, 2) + 40);
        SDL_Log("ETEST shovel picked=%d dead=%d projectiles=%d->%d", pickedUp, plant->mDead, projectileCount, Projectiles(board));
        Result(state, pickedUp && plant->mDead && Projectiles(board) == projectileCount, "shovel_not_intercepted_by_fan");
    }
    else
    {
        Result(state, false, "same_projectile_hits_target_once_missing_fixture");
        Result(state, false, "ordinary_pea_damage_and_destruction_missing_fixture");
        Result(state, false, "fan_27_real_electric_projectiles_missing_fixture");
        Result(state, false, "shovel_not_intercepted_by_fan_missing_fixture");
    }

    board->ClearCursor();
    board->mDbgOpen = false;
    board->mDbgPlantPending = false;
    const int initialPlants = board->CountPlantByType(SeedType::SEED_ELECTRIC_GATLINGPEA);
    // Board::DrawDbgPanel: DBG=(600,2,76,22), PLANT is index 4 at y=178..210.
    const bool menu = board->DbgMouseDown(638, 14);
    const bool arm = board->DbgMouseDown(554, 194);
    const int x = board->GridToPixelX(5, 4) + 40;
    const int y = board->GridToPixelY(5, 4) + 40;
    const bool place = board->DbgMouseDown(x, y);
    Plant* placed = board->GetTopPlantAt(5, 4, PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
    const int afterPlace = board->CountPlantByType(SeedType::SEED_ELECTRIC_GATLINGPEA);
    SDL_Log("ETEST debug-place menu=%d arm=%d place=%d col=5 row=4 count=%d->%d valid=%d",
        menu, arm, place, initialPlants, afterPlace, placed && placed->mSeedType == SeedType::SEED_ELECTRIC_GATLINGPEA);
    Result(state, menu && arm && place && placed && placed->mSeedType == SeedType::SEED_ELECTRIC_GATLINGPEA &&
        afterPlace == initialPlants + 1, "debug_plant_at_non_middle_cell");
    board->DbgMouseDown(638, 14);
    board->DbgMouseDown(554, 194);
    board->DbgMouseDown(x, y);
    const int afterRepeat = board->CountPlantByType(SeedType::SEED_ELECTRIC_GATLINGPEA);
    // Debug tool intentionally allows stacking on an occupied cell for testing.
    SDL_Log("ETEST debug-stack count=%d->%d pending=%d", afterPlace, afterRepeat, board->mDbgPlantPending);
    Result(state, afterRepeat == afterPlace + 1 && !board->mDbgPlantPending,
        "debug_plant_anywhere_stacks");
    board->DbgMouseDown(638, 14); // Cancel pending placement; leave the scene running.
    SDL_Log("ETEST %s done failures=%d observed_ticks=580", state.failures == 0 ? "PASS" : "FAIL", state.failures);
    state.phase = -1;
#else
    (void)board;
#endif
}
