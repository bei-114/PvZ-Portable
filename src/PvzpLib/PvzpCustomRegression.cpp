// SPDX-License-Identifier: LGPL-3.0-or-later
// Destructive opt-in LIVE integration test. Use a disposable HOME, loaded custom
// packs, and a fresh day-survival board. Call once at the END of Board::Update,
// including SCENE_LEVEL_INTRO; never recursively update Board or the chooser.
#include "../Lawn/Board.h"
#include "../Lawn/Cutscene.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/CursorObject.h"
#include "../Lawn/SeedPacket.h"
#include "../Lawn/Coin.h"
#include "../Lawn/System/SaveGame.h"
#include "../Lawn/Widget/GameButton.h"
#include "../Lawn/Widget/SeedChooserScreen.h"
#include "../LawnApp.h"
#include "../GameConstants.h"
#include "PvzpCustomPlants.h"
#include "PvzpModTools.h"
#include "PvzpMod.h"
#include "Reanimator.h"
#include "widget/WidgetManager.h"
#include <SDL_log.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

namespace {
enum class Op { Select, Page, Remove, Wait, Start };
struct Action { Op op; int value = 0; };
struct CustomTest {
    Board* board = nullptr;
    int phase = 0, ticks = 0, phaseTicks = 0, checks = 0, failures = 0;
    size_t action = 0;
    bool finished = false;
    std::vector<SeedType> custom, expected;
    std::vector<Action> actions;
    std::vector<uint32_t> friendly;
    std::vector<float> friendlyX;
    uint32_t aurora = 0, ownRow = 0, otherRow = 0, bounty = 0, partialNut = 0;
};
bool Check(CustomTest& t, bool pass, const char* label, int a = 0, int b = 0) {
    ++t.checks;
    if (!pass) ++t.failures;
    SDL_Log("CTEST %s %s a=%d b=%d phase=%d action=%zu tick=%d phase_tick=%d",
        pass ? "PASS" : "FAIL", label, a, b, t.phase, t.action, t.ticks, t.phaseTicks);
    return pass;
}
void Finish(CustomTest& t, Board* b) {
    if (t.finished) return;
    t.finished = true;
    SDL_Log("CTEST RESULT %s checks=%d failures=%d phase=%d action=%zu ticks=%d",
        t.failures ? "FAIL" : "PASS", t.checks, t.failures, t.phase, t.action, t.ticks);
    if (t.failures) { b->mApp->Shutdown(); std::exit(1); }
    const char* hold = std::getenv("PVZP_CUSTOM_HOLD");
    if (hold && hold[0] == '1' && hold[1] == '\0') {
        SDL_Log("CTEST HOLD successful settled board retained for screenshot; stop process when finished");
        return;
    }
    b->mApp->Shutdown();
}
void Pointer(SeedChooserScreen* s, int x, int y) {
    const auto pos = s->GetAbsPos();
    auto* manager = s->mApp->mWidgetManager.get();
    manager->mLastMouseX = x + pos.mX;
    manager->mLastMouseY = y + pos.mY;
    manager->mMouseIn = true;
    s->mLastMouseX = x;
    s->mLastMouseY = y;
}
void Click(SeedChooserScreen* s, int x, int y) {
    Pointer(s, x, y);
    s->MouseDown(x, y, 1);
    s->MouseUp(x, y, 1);
}
void BoardPointer(Board* b, int x, int y) {
    const auto pos = b->GetAbsPos();
    b->mApp->mWidgetManager->mLastMouseX = x + pos.mX;
    b->mApp->mWidgetManager->mLastMouseY = y + pos.mY;
    b->mApp->mWidgetManager->mMouseIn = true;
}
void BoardClick(Board* b, int x, int y) {
    BoardPointer(b, x, y);
    b->MouseMove(x, y);
    b->MouseDown(x, y, 1);
    b->MouseUp(x, y, 1);
}
void BoardClick(Board* b, const Rect& r) {
    BoardClick(b, r.mX + r.mWidth / 2, r.mY + r.mHeight / 2);
}
int LiveZombies(Board* b) {
    int count = 0;
    for (Zombie* z : b->mZombies) if (!z->mDead) ++count;
    return count;
}
bool PlayingChecks(CustomTest& t, Board* b) {
    b->Pause(false);
    b->ClearCursor();
    b->mTutorialState = TUTORIAL_OFF;
    b->mIgnoreMouseUp = b->mDbgConsumeMouseUp = false;
    for (Zombie* z : b->mZombies) if (!z->mDead) z->DieNoLoot();
    for (Plant* p : b->mPlants) if (!p->mDead) p->Die();
    b->ProcessDeleteQueue();
    PvzpModTools::Cancel(b);
    PvzpCustomPlants::Reset(b);
    b->mSunMoney = 9990;
    if (!Check(t, PvzpCustomPlants::IsSlotUsed(SEED_CUSTOM_0) &&
        PvzpCustomPlants::IsBrain(SEED_CUSTOM_2), "playing_fixture_nut200_brain202")) return false;
    SeedPacket* packet = nullptr;
    for (int i = 0; i < b->mSeedBank->mNumPackets; ++i)
        if (b->mSeedBank->mSeedPackets[i].mPacketType == SEED_CUSTOM_0)
            packet = &b->mSeedBank->mSeedPackets[i];
    if (!Check(t, packet && packet->mActive && !packet->mRefreshing &&
        Plant::GetRefreshTime(SEED_CUSTOM_0, SEED_NONE) == 3000,
        "custom_initially_ready_but_normal_refresh_is_3000")) return false;
    // Select the real seed packet, then plant on the real lawn via Board input.
    BoardClick(b, b->mSeedBank->mX + packet->mX + packet->mOffsetX + SEED_PACKET_WIDTH / 2,
        b->mSeedBank->mY + packet->mY + SEED_PACKET_HEIGHT / 2);
    const int nutX = b->GridToPixelX(1, 1) + 40;
    const int nutY = b->GridToPixelY(1, 1) + 40;
    BoardClick(b, nutX, nutY);
    Plant* nut = nullptr;
    for (Plant* p : b->mPlants)
        if (!p->mDead && p->mSeedType == SEED_CUSTOM_0 && p->mPlantCol == 1 && p->mRow == 1) nut = p;
    if (!Check(t, nut && packet->mRefreshing && !packet->mActive && packet->mRefreshTime == 3000,
        "real_custom_packet_plant_starts_3000_recharge", packet->mRefreshTime, packet->mRefreshCounter)) return false;
    b->mSunMoney = 1000; // Do not hide a duplicate sun event behind the 9990 cap.
    nut->mPlantHealth -= 1000;
    PvzpCustomPlants::ObserveDamage(b, nut);
    const auto saved = PvzpCustomPlants::Save(b);
    const int money = b->mSunMoney, hp = nut->mPlantHealth;
    const int plants = b->mPlants.mSize, zombies = LiveZombies(b);
    const bool load1 = PvzpCustomPlants::Load(b, saved);
    const bool load2 = PvzpCustomPlants::Load(b, saved);
    PvzpCustomPlants::ObserveDamage(b, nut);
    Check(t, load1 && load2 && PvzpCustomPlants::Save(b) == saved &&
        money == b->mSunMoney && hp == nut->mPlantHealth && plants == b->mPlants.mSize &&
        zombies == LiveZombies(b), "custom_save_load_roundtrip_damage_threshold_not_replayed");
    auto truncated = saved;
    if (!truncated.empty()) truncated.pop_back();
    Check(t, !PvzpCustomPlants::Load(b, truncated) && PvzpCustomPlants::Save(b) == saved,
        "custom_truncated_state_rejected_without_mutation", int(saved.size()));
    // Remove any random reward plant before the brain fixture. This test never
    // invokes BrainClick while unpaused, so it cannot randomly spawn a boss.
    for (Plant* p : b->mPlants) if (!p->mDead) p->Die();
    for (Zombie* z : b->mZombies) if (!z->mDead) z->DieNoLoot();
    b->ProcessDeleteQueue();
    PvzpCustomPlants::Reset(b);
    b->ClearCursor();
    Plant* brain = b->AddPlant(2, 2, SEED_CUSTOM_2);
    if (!Check(t, brain != nullptr, "brain_fixture_created")) return false;
    const uint32_t id = b->mPlants.DataArrayGetID(brain);
    brain->mPlantHealth -= 37;
    const int brainHP = brain->mPlantHealth;
    const int before = LiveZombies(b);
    b->Pause(true);
    BoardClick(b, brain->mX + 40, brain->mY + 40);
    Check(t, LiveZombies(b) == before && brain->mPlantHealth == brainHP,
        "paused_real_brain_click_no_spawn_no_mutation", before, LiveZombies(b));
    b->Pause(false);
    BoardClick(b, PvzpModTools::GetGloveRect(b));
    if (!Check(t, PvzpModTools::IsActive(b), "real_glove_button_activated")) return false;
    int x = brain->mX + 40, y = brain->mY + 40;
    BoardPointer(b, x, y); b->MouseMove(x, y); b->MouseDown(x, y, 1);
    x = b->GridToPixelX(4, 3) + 40;
    y = b->GridToPixelY(4, 3) + 40;
    BoardPointer(b, x, y); b->MouseDrag(x, y); b->MouseUp(x, y, 1);
    if (!Check(t, b->mPlants.DataArrayTryToGet(id) == brain &&
        brain->mPlantCol == 4 && brain->mRow == 3 && brain->mPlantHealth == brainHP &&
        LiveZombies(b) == before, "real_brain_glove_drag_identity_hp_no_click_spawn",
        brain->mPlantCol, brain->mRow)) return false;
    b->mPlantFoodCount = 3;
    BoardClick(b, b->GetPlantFoodRect());
    if (!Check(t, b->mPlantFoodSelecting && !PvzpModTools::IsActive(b),
        "full_stock_real_bean_click_cancels_glove_and_selects_food")) return false;
    BoardClick(b, brain->mX + 40, brain->mY + 40);
    bool valid = b->mPlantFoodCount == 2 && !b->mPlantFoodSelecting;
    int rows[MAX_GRID_SIZE_Y] = {};
    for (Zombie* z : b->mZombies) if (!z->mDead) {
        const bool safe = z->mZombieType == ZOMBIE_FOOTBALL || z->mZombieType == ZOMBIE_PAIL ||
            z->mZombieType == ZOMBIE_TRAFFIC_CONE;
        const int left = b->GridToPixelX(0, z->mRow);
        valid = valid && safe && z->mMindControlled && z->mPosX >= left && z->mPosX <= left + 32 &&
            z->mX == int(z->mPosX) && z->mY == int(z->mPosY) && z->mRow >= 0 && z->mRow < 5;
        if (z->mRow >= 0 && z->mRow < MAX_GRID_SIZE_Y) ++rows[z->mRow];
        t.friendly.push_back(b->mZombies.DataArrayGetID(z));
        t.friendlyX.push_back(z->mPosX);
    }
    for (int row = 0; row < 5; ++row) valid = valid && rows[row] == 5;
    return Check(t, valid && t.friendly.size() == 25,
        "real_bean_on_brain_consumes_one_spawns_25_safe_charmed_synced_col0",
        int(t.friendly.size()), b->mPlantFoodCount);
}
int Suns(Board* b) {
    int count = 0;
    for (Coin* c : b->mCoins) if (!c->mDead && c->IsSun()) ++count;
    return count;
}
Zombie* Enemy(Board* b, ZombieType type, int row) {
    Zombie* z = b->AddZombieInRow(type, row, Zombie::ZOMBIE_WAVE_DEBUG);
    if (z) { z->mPosX = 650; z->mX = 650; z->mIsEating = false; }
    return z;
}
void ClearFixture(Board* b) {
    b->ClearCursor();
    PvzpModTools::Cancel(b);
    for (Plant* p : b->mPlants) if (!p->mDead) p->Die();
    for (Zombie* z : b->mZombies) if (!z->mDead) z->DieNoLoot();
    for (Coin* c : b->mCoins) if (!c->mDead) c->Die();
    b->ProcessDeleteQueue();
    PvzpCustomPlants::Reset(b);
}
bool BountyChecks(CustomTest& t, Board* b) {
    ClearFixture(b);
    const ZombieType types[] = {ZOMBIE_NEWSPAPER, ZOMBIE_PAIL};
    const int health[] = {400, 350}, speed[] = {150, 125}, bounty[] = {75, 100};
    const int red[] = {255, 120}, green[] = {195, 215}, blue[] = {100, 255};
    for (int i = 0; i < 2; ++i) {
        Zombie* z = Enemy(b, types[i], i);
        if (!Check(t, z != nullptr, "bounty_enemy_fixture", int(types[i]))) return false;
        const char* name = GetZombieDefinition(types[i]).mZombieName;
        Check(t, z->mBodyHealth == health[i] && z->mBodyMaxHealth == health[i] &&
            PvzpMod::ZombieInt(name, "speedPercent", 100) == speed[i] &&
            PvzpMod::ZombieInt(name, "sunDrop", 0) == bounty[i] &&
            PvzpMod::ZombieInt(name, "tintR", 255) == red[i] &&
            PvzpMod::ZombieInt(name, "tintG", 255) == green[i] &&
            PvzpMod::ZombieInt(name, "tintB", 255) == blue[i],
            "bounty_spawn_health_speed_reward_and_rgb_config_fields", int(types[i]), z->mBodyHealth);
        Reanimation* body = b->mApp->ReanimationTryToGet(z->mBodyReanimID);
        if (!Check(t, body != nullptr, "bounty_actual_body_reanimation_exists")) return false;
        z->mZombiePhase = PHASE_ZOMBIE_NORMAL;
        z->mChilledCounter = z->mIceTrapCounter = z->mButteredCounter = 0;
        z->mVelX = 1.0f;
        float base = z->mVelX;
        if (body->TrackExists("_ground")) {
            base = 0;
            // Pick a nonzero actual walking-track segment without advancing the
            // Board or changing the loaded rules. Ground-track movement is the
            // path where multiplying only mVelX used to have no effect.
            for (int frame = 0; frame < 100 && std::fabs(base) < 0.001f; ++frame) {
                body->mAnimTime = frame / 100.0f;
                base = body->GetTrackVelocity("_ground") * z->mScaleZombie;
            }
        }
        const float before = z->mPosX;
        z->UpdateZombieWalking();
        const float actual = before - z->mPosX;
        const float expected = base * speed[i] / 100.0f;
        Check(t, std::fabs(base) > 0.001f && std::fabs(actual - expected) < 0.001f,
            "bounty_real_walking_displacement_applies_multiplier_once",
            int(actual * 100000), int(expected * 100000));
    }
    Zombie* bucket = Enemy(b, ZOMBIE_PAIL, 2);
    if (!Check(t, bucket && b->CanDropLoot(), "bounty_drop_fixture_allowed")) return false;
    const int before = Suns(b), money = b->mSunMoney;
    bucket->DropLoot();
    Check(t, Suns(b) == before + 4 && b->mSunMoney == money && bucket->mDroppedLoot,
        "bucket_bounty_100_is_four_collectible_suns_not_direct_credit", Suns(b) - before);
    bucket->DropLoot();
    Check(t, Suns(b) == before + 4 && b->mSunMoney == money,
        "bucket_bounty_second_drop_does_not_duplicate");
    Zombie* friendly = Enemy(b, ZOMBIE_PAIL, 3);
    if (!Check(t, friendly != nullptr, "friendly_bounty_fixture")) return false;
    friendly->StartMindControlled();
    const int friendlyBefore = Suns(b);
    friendly->DropLoot();
    Check(t, Suns(b) == friendlyBefore && b->mSunMoney == money,
        "friendly_bucket_has_no_mod_sun_bounty");
    Zombie* puzzle = Enemy(b, ZOMBIE_PAIL, 4);
    if (!Check(t, puzzle != nullptr, "izombie_bounty_fixture")) return false;
    const auto mode = b->mApp->mGameMode;
    b->mApp->mGameMode = GAMEMODE_PUZZLE_I_ZOMBIE_1;
    puzzle->DropLoot();
    b->mApp->mGameMode = mode; // Restore before ANY further live Board update.
    Check(t, Suns(b) == friendlyBefore && b->mSunMoney == money,
        "izombie_bucket_has_no_mod_sun_bounty");
    return true;
}
bool AuroraFixture(CustomTest& t, Board* b) {
    ClearFixture(b);
    const PvzpMod::CustomPlant* config = nullptr;
    for (const auto& c : PvzpMod::CustomPlants()) if (c.slot == 3) config = &c;
    if (!Check(t, config && config->interval == 800 && config->health == 1800 &&
        config->pulseEvents == std::vector<std::string>{"chill:row"} &&
        config->foodEvents == std::vector<std::string>{"chill", "heal:1800", "sun:100"},
        "aurora800_row_pulse_and_food_fixture")) return false;
    Plant* aurora = b->AddPlant(2, 2, SEED_CUSTOM_3);
    Plant* brain = b->AddPlant(4, 3, SEED_CUSTOM_2);
    Plant* partial = b->AddPlant(1, 1, SEED_CUSTOM_0);
    if (!Check(t, aurora && brain && partial, "aurora_brain_partial_nut_visible_fixture")) return false;
    t.aurora = b->mPlants.DataArrayGetID(aurora);
    t.partialNut = b->mPlants.DataArrayGetID(partial);
    aurora->mPlantHealth -= 300; // Below its damage step: no row-chill event.
    partial->mPlantHealth -= 500; // Nonzero persisted progress, no random event.
    PvzpCustomPlants::ObserveDamage(b, aurora);
    PvzpCustomPlants::ObserveDamage(b, partial);
    Zombie* own = Enemy(b, ZOMBIE_NORMAL, 2);
    Zombie* other = Enemy(b, ZOMBIE_NORMAL, 0);
    Zombie* bucket = Enemy(b, ZOMBIE_PAIL, 4);
    if (!Check(t, own && other && bucket, "aurora_chill_targets_and_persisted_bounty_fixture")) return false;
    for (Zombie* z : {own, other, bucket}) {
        z->mIceTrapCounter = 100000; // Stationary, no attacks; still chillable.
        z->mChilledCounter = 0;
    }
    t.ownRow = b->mZombies.DataArrayGetID(own);
    t.otherRow = b->mZombies.DataArrayGetID(other);
    t.bounty = b->mZombies.DataArrayGetID(bucket);
    bucket->DropLoot();
    b->mSunMoney = 1000;
    return true;
}
bool PortableRoundtrip(CustomTest& t, Board* b) {
    const char* home = std::getenv("HOME");
    if (!Check(t, home && *home, "portable_roundtrip_disposable_home_available")) return false;
    const std::string path = std::string(home) + "/pvzp-custom-regression.dat";
    const auto custom = PvzpCustomPlants::Save(b);
    Plant* aurora = b->mPlants.DataArrayTryToGet(t.aurora);
    Plant* partial = b->mPlants.DataArrayTryToGet(t.partialNut);
    Zombie* bucket = b->mZombies.DataArrayTryToGet(t.bounty);
    if (!Check(t, aurora && partial && bucket, "portable_roundtrip_live_ids_present")) return false;
    const int hp = aurora->mPlantHealth, partialHP = partial->mPlantHealth;
    const int food = b->mPlantFoodCount, money = b->mSunMoney, suns = Suns(b);
    std::vector<SeedType> packets;
    std::vector<int> counters;
    for (int i = 0; i < b->mSeedBank->mNumPackets; ++i) {
        packets.push_back(b->mSeedBank->mSeedPackets[i].mPacketType);
        counters.push_back(b->mSeedBank->mSeedPackets[i].mRefreshCounter);
    }
    if (!Check(t, LawnSaveGame(b, path), "portable_full_board_save_with_custom_chunk")) return false;
    aurora->mPlantHealth = 1;
    partial->mPlantHealth = 1;
    b->mSunMoney = 7;
    b->mPlantFoodCount = 0;
    // The callback is at the END of Board::Update. No entity iterator or old
    // plant/reanimation pointer survives this import; never create another Board
    // (its destructor would invalidate shared live resources).
    if (!Check(t, b->LoadGame(path), "portable_full_board_load_same_board_at_end_of_update")) return false;
    aurora = b->mPlants.DataArrayTryToGet(t.aurora);
    partial = b->mPlants.DataArrayTryToGet(t.partialNut);
    bucket = b->mZombies.DataArrayTryToGet(t.bounty);
    bool exact = aurora && partial && bucket && aurora->mSeedType == SEED_CUSTOM_3 &&
        partial->mSeedType == SEED_CUSTOM_0 && aurora->mPlantHealth == hp && partial->mPlantHealth == partialHP &&
        bucket->mDroppedLoot && b->mPlantFoodCount == food && b->mSunMoney == money && Suns(b) == suns &&
        PvzpCustomPlants::Save(b) == custom && b->mSeedBank->mNumPackets == int(packets.size());
    for (size_t i = 0; i < packets.size() && exact; ++i)
        exact = b->mSeedBank->mSeedPackets[i].mPacketType == packets[i] &&
            b->mSeedBank->mSeedPackets[i].mRefreshCounter == counters[i];
    if (!Check(t, exact, "portable_roundtrip_custom_ids_health_partial_damage_pulse_packets_food_and_loot_guard")) return false;
    PvzpCustomPlants::ObserveDamage(b, partial);
    bucket->DropLoot();
    return Check(t, PvzpCustomPlants::Save(b) == custom && Suns(b) == suns && b->mSunMoney == money,
        "portable_load_does_not_replay_partial_damage_or_bounty");
}
bool ExactBank(CustomTest& t, SeedChooserScreen* s, const char* label) {
    bool exact = s->mSeedsInFlight == 0 && s->mSeedsInBank == int(t.expected.size());
    for (size_t i = 0; i < t.expected.size(); ++i) {
        const auto seed = t.expected[i];
        const auto& chosen = s->mChosenSeeds[seed];
        exact = exact && s->FindSeedInBank(int(i)) == seed &&
            chosen.mSeedState == SEED_IN_BANK && chosen.mSeedIndexInBank == int(i);
        int x = 0, y = 0;
        s->GetSeedPositionInBank(int(i), x, y);
        exact = exact && s->SeedHitTest(x + SEED_PACKET_WIDTH / 2,
            y + SEED_PACKET_HEIGHT / 2) == seed;
    }
    return Check(t, exact, label, s->mSeedsInBank, int(t.expected.size()));
}
void SelectAction(CustomTest& t, SeedType seed) {
    t.actions.push_back({Op::Select, int(seed)});
    t.actions.push_back({Op::Wait});
}
bool Prepare(CustomTest& t, Board* b, SeedChooserScreen* s) {
    if (!Check(t, b->mApp->IsSurvivalMode() && !b->StageHasPool() &&
        !b->StageHasRoof() && !b->StageIsNight(), "fixture_fresh_day_survival")) return false;
    if (!Check(t, s->mSeedsInBank == 0 && b->mSeedBank->mNumPackets >= 6,
        "fixture_empty_bank_at_least_six_packets", s->mSeedsInBank, b->mSeedBank->mNumPackets)) return false;
    for (int i = int(SEED_CUSTOM_0); i <= int(SEED_CUSTOM_3); ++i) {
        const auto seed = static_cast<SeedType>(i);
        if (PvzpCustomPlants::IsSlotUsed(seed)) {
            if (t.custom.size() < 3) t.custom.push_back(seed);
        } else {
            if (!Check(t, !s->ChooserHasSeed(seed) &&
                s->mChosenSeeds[i].mSeedState == SEED_PACKET_HIDDEN,
                "inactive_custom_hidden_and_unavailable", i)) return false;
        }
    }
    if (!Check(t, t.custom.size() >= 2, "fixture_at_least_two_active_custom_slots", int(t.custom.size()))) return false;
    if (!Check(t, s->ChooserHasSeed(SEED_SUNFLOWER), "fixture_sunflower_owned")) return false;
    // The test uses the actual layout helper, not hardcoded seed coordinates.
    SelectAction(t, SEED_SUNFLOWER);
    t.actions.push_back({Op::Page, 1});
    for (auto seed : t.custom) SelectAction(t, seed);
    // Remove a selected custom packet while on NORMAL, with later custom packets
    // still to its right; then remove a normal packet while on MOD.
    t.actions.push_back({Op::Page, 0});
    t.actions.push_back({Op::Remove, int(t.custom.front())});
    t.actions.push_back({Op::Wait});
    t.actions.push_back({Op::Page, 1});
    t.actions.push_back({Op::Remove, int(SEED_SUNFLOWER)});
    t.actions.push_back({Op::Wait});
    // Reselect and switch twice in successive LIVE frames without waiting for
    // the 25-tick flight. Page switching must land, not lose, the flying packet.
    t.actions.push_back({Op::Select, int(t.custom.front())});
    t.actions.push_back({Op::Page, 0});
    t.actions.push_back({Op::Page, 1});
    t.actions.push_back({Op::Wait});
    t.actions.push_back({Op::Page, 0});
    SelectAction(t, SEED_SUNFLOWER);
    int remaining = b->mSeedBank->mNumPackets - int(t.custom.size()) - 1;
    // Blover prevents a real start-button balloon warning from blocking the run.
    const SeedType normal[] = {SEED_BLOVER, SEED_PEASHOOTER, SEED_WALLNUT,
        SEED_POTATOMINE, SEED_SNOWPEA, SEED_REPEATER, SEED_CHERRYBOMB,
        SEED_SQUASH, SEED_JALAPENO};
    for (auto seed : normal) {
        if (remaining == 0) break;
        if (!s->ChooserHasSeed(seed) || s->SeedNotAllowedToPick(seed) ||
            s->SeedNotAllowedDuringTrial(seed)) continue;
        SelectAction(t, seed);
        --remaining;
    }
    if (!Check(t, remaining == 0 && (!s->FlyersAreComming() || s->ChooserHasSeed(SEED_BLOVER)),
        "fixture_enough_normal_fillers_and_flyer_protection", remaining)) return false;
    t.actions.push_back({Op::Start});
    SDL_Log("CTEST BEGIN real chooser MouseDown/MouseUp, natural flight updates; custom=%zu packets=%d actions=%zu; drawing not asserted",
        t.custom.size(), b->mSeedBank->mNumPackets, t.actions.size());
    return true;
}
bool RunAction(CustomTest& t, Board* b, SeedChooserScreen* s) {
    const Action a = t.actions[t.action];
    const auto seed = static_cast<SeedType>(a.value);
    if (a.op == Op::Wait) {
        if (s->mSeedsInFlight > 0) {
            if (t.phaseTicks <= 100) return false;
            Check(t, false, "flight_timeout_natural_chooser_update", s->mSeedsInFlight);
            Finish(t, b);
            return false;
        }
        if (!ExactBank(t, s, "settled_bank_order_indices_and_hit_rects")) Finish(t, b);
    } else if (a.op == Op::Page) {
        if (s->mChooserPage != a.value) {
            Rect r = s->GetPageButtonRect();
            Click(s, r.mX + r.mWidth / 2, r.mY + r.mHeight / 2);
        }
        if (!Check(t, s->mChooserPage == a.value && s->mSeedsInFlight == 0,
            "page_switch_real_click_preserves_and_lands_flights", s->mChooserPage, s->mSeedsInFlight) ||
            !ExactBank(t, s, "all_bank_packets_hittable_on_either_page")) Finish(t, b);
    } else if (a.op == Op::Select) {
        int x = 0, y = 0;
        s->GetSeedPositionInChooser(a.value, x, y);
        x += SEED_PACKET_WIDTH / 2;
        y += SEED_PACKET_HEIGHT / 2;
        if (!Check(t, s->OnChooserPage(seed) && s->SeedHitTest(x, y) == seed,
            "chooser_real_packet_rect_hits_expected_seed", a.value, int(s->SeedHitTest(x, y)))) {
            Finish(t, b); return false;
        }
        const int old = s->mSeedsInBank;
        Click(s, x, y);
        if (!Check(t, s->mSeedsInBank == old + 1 && s->mSeedsInFlight == 1 &&
            s->mChosenSeeds[seed].mSeedState == SEED_FLYING_TO_BANK,
            "select_real_click_starts_flight", a.value, s->mSeedsInFlight)) {
            Finish(t, b); return false;
        }
        t.expected.push_back(seed);
    } else if (a.op == Op::Remove) {
        auto it = std::find(t.expected.begin(), t.expected.end(), seed);
        if (!Check(t, it != t.expected.end() && !s->OnChooserPage(seed),
            "remove_fixture_bank_seed_on_opposite_chooser_page", a.value)) {
            Finish(t, b); return false;
        }
        const int index = int(it - t.expected.begin());
        int x = 0, y = 0;
        s->GetSeedPositionInBank(index, x, y);
        x += SEED_PACKET_WIDTH / 2;
        y += SEED_PACKET_HEIGHT / 2;
        if (!Check(t, s->SeedHitTest(x, y) == seed, "opposite_page_bank_hit", a.value, index)) {
            Finish(t, b); return false;
        }
        Click(s, x, y);
        if (!Check(t, s->mSeedsInBank == int(t.expected.size()) - 1 &&
            s->mChosenSeeds[seed].mSeedState == SEED_FLYING_TO_CHOOSER,
            "remove_real_click_starts_return_and_reorder_flights", a.value, s->mSeedsInFlight)) {
            Finish(t, b); return false;
        }
        t.expected.erase(it);
    } else if (a.op == Op::Start) {
        if (!ExactBank(t, s, "mixed_full_bank_before_start") ||
            !Check(t, s->mSeedsInBank == b->mSeedBank->mNumPackets && !s->mStartButton->mDisabled,
                "full_mixed_bank_enables_start")) {
            Finish(t, b); return false;
        }
        auto* start = s->mStartButton.get();
        Click(s, start->mX + start->mWidth / 2, start->mY + start->mHeight / 2);
        bool exact = !b->mCutScene->mSeedChoosing;
        for (size_t i = 0; i < t.expected.size(); ++i)
            exact = exact && b->mSeedBank->mSeedPackets[i].mPacketType == t.expected[i];
        if (!Check(t, exact, "real_start_click_closes_and_writes_exact_mixed_packet_ids")) {
            Finish(t, b); return false;
        }
        t.phase = 2;
    }
    return !t.finished;
}
}

void PvzpRunCustomRegression(Board* b) {
#if defined(__linux__) && !defined(__ANDROID__)
    const char* enabled = std::getenv("PVZP_CUSTOM_TEST");
    if (!enabled || enabled[0] != '1' || enabled[1] != '\0' || !b || !b->mApp) return;
    static CustomTest t;
    if (t.board != b) { t = CustomTest{}; t.board = b; }
    if (t.finished) return;
    ++t.ticks;
    ++t.phaseTicks;
    if (t.ticks > 6000) {
        Check(t, false, "bounded_live_test_timeout", int(b->mApp->mGameScene));
        Finish(t, b); return;
    }
    if (t.phase == 0) {
        auto* s = b->mApp->mSeedChooserScreen.get();
        if (!s || !s->mMouseVisible || s->mX != 0 || s->mY != 0 ||
            !b->mCutScene->mSeedChoosing || b->mApp->GetDialogCount() > 0) return;
        if (!Prepare(t, b, s)) { Finish(t, b); return; }
        t.phase = 1;
        t.phaseTicks = 0;
        return;
    }
    if (t.phase == 1) {
        auto* s = b->mApp->mSeedChooserScreen.get();
        if (!s || !s->mMouseVisible || b->mApp->GetDialogCount() > 0) {
            Check(t, false, "chooser_disappeared_or_unexpected_dialog");
            Finish(t, b); return;
        }
        if (t.action >= t.actions.size()) {
            Check(t, false, "script_exhausted_without_start");
            Finish(t, b); return;
        }
        if (RunAction(t, b, s)) { ++t.action; t.phaseTicks = 0; }
        return;
    }
    if (t.phase == 2) {
        if (b->mApp->mGameScene != SCENE_PLAYING) return;
        b->mZombieCountDown = b->mZombieCountDownStart = 100000;
        b->mHugeWaveCountDown = -1;
        bool ready = true, exact = b->mSeedBank->mNumPackets == int(t.expected.size());
        for (size_t i = 0; i < t.expected.size(); ++i) {
            const auto& packet = b->mSeedBank->mSeedPackets[i];
            exact = exact && packet.mPacketType == t.expected[i];
            if (PvzpCustomPlants::IsCustom(t.expected[i]))
                ready = ready && packet.mActive && !packet.mRefreshing && packet.mRefreshCounter == 0;
        }
        Check(t, exact, "live_playing_preserves_exact_mixed_bank_ids");
        Check(t, ready, "custom_packets_initially_ready_not_initial_cooldown");
        if (!PlayingChecks(t, b)) { Finish(t, b); return; }
        t.phase = 3;
        t.phaseTicks = 0;
        return;
    }
    if (t.phase == 3) {
        b->mZombieCountDown = b->mZombieCountDownStart = 100000;
        b->mHugeWaveCountDown = -1;
        if (t.phaseTicks < 20) return;
        bool right = t.friendly.size() == 25;
        for (size_t i = 0; i < t.friendly.size(); ++i) {
            Zombie* z = b->mZombies.DataArrayTryToGet(t.friendly[i]);
            right = right && z && !z->mDead && z->mMindControlled &&
                z->mPosX > t.friendlyX[i] + 0.001f;
        }
        Check(t, right, "25_charmed_brain_zombies_walk_right_during_real_board_updates",
            int(t.friendly.size()), t.phaseTicks);
        if (!BountyChecks(t, b) || !AuroraFixture(t, b)) { Finish(t, b); return; }
        t.phase = 4;
        t.phaseTicks = 0;
        return;
    }
    if (t.phase == 4) {
        b->mZombieCountDown = b->mZombieCountDownStart = 100000;
        b->mHugeWaveCountDown = -1;
        Plant* aurora = b->mPlants.DataArrayTryToGet(t.aurora);
        Zombie* own = b->mZombies.DataArrayTryToGet(t.ownRow);
        Zombie* other = b->mZombies.DataArrayTryToGet(t.otherRow);
        if (!aurora || !own || !other || aurora->mDead || own->mDead || other->mDead) {
            Check(t, false, "aurora_live_fixture_disappeared"); Finish(t, b); return;
        }
        if (t.phaseTicks == 400) {
            if (!Check(t, own->mChilledCounter == 0 && other->mChilledCounter == 0,
                "aurora_no_early_pulse_before_portable_midinterval_save") ||
                !PortableRoundtrip(t, b)) { Finish(t, b); return; }
            return; // Every pointer above became stale during the import.
        }
        if (t.phaseTicks == 799)
            Check(t, own->mChilledCounter == 0 && other->mChilledCounter == 0,
                "aurora_799_real_board_updates_no_early_chill_after_load");
        if (t.phaseTicks < 800) return;
        Check(t, own->mChilledCounter > 0 && other->mChilledCounter == 0,
            "aurora_800th_real_board_update_chills_own_row_only_after_load",
            own->mChilledCounter, other->mChilledCounter);
        const int money = b->mSunMoney;
        b->mPlantFoodCount = 3;
        BoardClick(b, b->GetPlantFoodRect());
        const bool selected = b->mPlantFoodSelecting;
        BoardClick(b, aurora->mX + 40, aurora->mY + 40);
        bool allChilled = true;
        for (Zombie* z : b->mZombies)
            if (!z->mDead && !z->mMindControlled) allChilled = allChilled && z->mChilledCounter > 0;
        Check(t, selected && b->mPlantFoodCount == 2 && !b->mPlantFoodSelecting && allChilled &&
            aurora->mPlantHealth == aurora->mPlantMaxHealth && b->mSunMoney == money + 100,
            "aurora_real_food_all_row_chill_full_heal_100_sun_consumes_one",
            aurora->mPlantHealth, b->mSunMoney - money);
        Finish(t, b);
    }
#else
    (void)b;
#endif
}
