// SPDX-License-Identifier: LGPL-3.0-or-later
// Destructive opt-in LIVE integration test. Use a disposable HOME, loaded custom
// packs, and a fresh day-survival board. Call once at the END of Board::Update,
// including SCENE_LEVEL_INTRO; never recursively update Board or the chooser.
#include "../Lawn/Board.h"
#include "../Lawn/Cutscene.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Projectile.h"
#include "../Lawn/Widget/AlmanacDialog.h"
#include "../Lawn/Widget/NewOptionsDialog.h"
#include "../Resources.h"
#include "PvzpPrism.h"
#include "PvzpLocale.h"
#include "PvzpFontFallback.h"
#include "PvzpStringFile.h"
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
#include "PvzpCustomZombies.h"
#include "PvzpModTools.h"
#include "PvzpMod.h"
#include "Reanimator.h"
#include "widget/WidgetManager.h"
#include <SDL_log.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <map>
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
    uint32_t postLoadPlant = 0, postLoadShooter = 0, postLoadGatling = 0;
    int postLoadHealth = 0, postLoadMainCounter = 0;
    std::vector<uint32_t> postLoadShots;
    std::vector<float> postLoadShotX;
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
        // Screenshot-only fixture, created AFTER every real regression passed.
        // Never changes gameplay, the asserted board or ordinary test output.
        if (const char* showcase=std::getenv("PVZP_CUSTOM_SHOWCASE"); showcase && std::string(showcase)=="1") {
            for (Zombie* z:b->mZombies) if(!z->mDead)z->DieNoLoot();
            for (Plant* p:b->mPlants) if(!p->mDead)p->Die();
            for (Projectile* p:b->mProjectiles) if(!p->mDead)p->Die();
            b->ProcessDeleteQueue(); PvzpCustomZombies::Reset(b);
            int i=0;
            for(const auto& c:PvzpMod::CustomZombies()) {
                int row=i%4; Zombie* z=PvzpCustomZombies::Spawn(b,c.id,row);
                if(z){z->mPosX=450.0f+150*(i/4);z->mX=int(z->mPosX);z->mIceTrapCounter=0;z->mChilledCounter=0;}
                if(++i==8)break;
            }
            b->AddPlant(1,0,SEED_SUNFLOWER);b->AddPlant(2,1,SEED_CUSTOM_3);
            b->AddPlant(2,2,SEED_CUSTOM_4);b->AddPlant(1,3,SEED_CUSTOM_0);
            PvzpLocaleSetLanguage("zh-CN");
        }
        b->mPaused=true; b->MarkAllDirty();
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
    for (Projectile* p : b->mProjectiles) if (!p->mDead) p->Die();
    b->ProcessDeleteQueue();
    PvzpCustomPlants::Reset(b);
    PvzpCustomZombies::Reset(b);
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
    if (!Check(t, config && config->cost == 125 && config->interval == 400 && config->health == 8000 && config->step == 400 &&
        config->events == std::vector<std::string>{"damage:row:600", "heal:1000", "freeze:row", "push:row:120"} &&
        config->pulseEvents == std::vector<std::string>{"chill:row", "freeze:row", "damage:row:300", "push:row:30"} &&
        config->foodEvents == std::vector<std::string>{"chill", "freeze:row", "damage:row:1800", "push:row:160", "heal:8000", "sun:250"},
        "aurora400_tank8000_stronger_row_pulse_and_food_fixture")) return false;
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
        z->mBodyHealth = z->mBodyMaxHealth = 10000;
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
int Projectiles(Board* b) {
    int count = 0;
    for (Projectile* p : b->mProjectiles) if (!p->mDead) ++count;
    return count;
}
std::vector<uint32_t> ProjectileIDs(Board* b) {
    std::vector<uint32_t> ids;
    for (Projectile* p : b->mProjectiles) if (!p->mDead) ids.push_back(b->mProjectiles.DataArrayGetID(p));
    return ids;
}
bool NutArtChecks(CustomTest& t, Board* b) {
    ClearFixture(b);
    Plant* nut = b->AddPlant(1, 1, SEED_CUSTOM_3);
    if (!Check(t, nut && PvzpCustomPlants::IsNut(nut->mSeedType) &&
        PvzpCustomPlants::BaseSeed(nut->mSeedType) == SEED_WALLNUT, "custom_nut_wallnut_art_fixture")) return false;
    Reanimation* body = b->mApp->ReanimationTryToGet(nut->mBodyReanimID);
    if (!Check(t, body != nullptr, "custom_nut_actual_reanimation_exists")) return false;
    nut->mPlantHealth = nut->mPlantMaxHealth;
    nut->Animate();
    nut->UpdateReanim();
    Check(t, body->GetImageOverride("anim_face") == nullptr, "nut_actual_update_reanim_healthy_face");
    nut->mPlantHealth = nut->mPlantMaxHealth / 2;
    nut->Animate();
    nut->UpdateReanim();
    Check(t, body->GetImageOverride("anim_face") == Sexy::IMAGE_REANIM_WALLNUT_CRACKED1,
        "nut_actual_update_reanim_middle_cracked_face");
    nut->mPlantHealth = nut->mPlantMaxHealth / 4;
    nut->Animate();
    nut->UpdateReanim();
    Check(t, body->GetImageOverride("anim_face") == Sexy::IMAGE_REANIM_WALLNUT_CRACKED2,
        "nut_actual_update_reanim_low_cracked_face");
    nut->mPlantHealth = nut->mPlantMaxHealth;
    nut->Animate();
    nut->UpdateReanim();
    nut->DoBlink();
    Check(t, body->GetImageOverride("anim_face") == nullptr &&
        b->mApp->ReanimationTryToGet(nut->mBlinkReanimID) != nullptr && nut->mBlinkCountdown >= 1000,
        "nut_repaired_face_and_actual_wallnut_blink_track");
    nut->EndBlink();
    return true;
}
bool PrismPacketChecks(CustomTest& t, Board* b) {
    ClearFixture(b);
    const auto seed = static_cast<SeedType>(int(SEED_CUSTOM_FIRST) + 4);
    if (!Check(t, PvzpCustomPlants::IsSlotUsed(seed) && PvzpCustomPlants::IsPrism(seed),
        "prism204_fixture_enabled")) return false;
    SeedPacket& packet = b->mSeedBank->mSeedPackets[0];
    packet.SetPacketType(seed, SEED_NONE);
    // SetPacketType itself must supply initial readiness; do not force flags.
    b->mSunMoney = 9990;
    Check(t, packet.mPacketType == seed && packet.mActive && Plant::GetRefreshTime(seed, SEED_NONE) == 3000,
        "prism204_real_bank_packet_ready_and_refresh3000");
    BoardClick(b, b->mSeedBank->mX + packet.mX + packet.mOffsetX + SEED_PACKET_WIDTH / 2,
        b->mSeedBank->mY + packet.mY + SEED_PACKET_HEIGHT / 2);
    BoardClick(b, b->GridToPixelX(2, 2) + 40, b->GridToPixelY(2, 2) + 40);
    Plant* flower = nullptr;
    for (Plant* p : b->mPlants) if (!p->mDead && p->mSeedType == seed) flower = p;
    if (!Check(t, flower && packet.mRefreshing && !packet.mActive && packet.mRefreshTime == 3000,
        "prism204_real_packet_plant_starts3000_recharge")) return false;
    const auto& def = GetPlantDefinition(seed);
    Check(t, def.mReanimationType == REANIM_MARIGOLD && flower->mLaunchRate == 0 &&
        def.mSeedCost == 400 && PvzpCustomPlants::CostForSeed(seed, -1) == 400 &&
        flower->mPlantHealth == 300 && !PvzpCustomPlants::IsEmpowered(flower),
        "prism_marigold_art_health300_no_passive_launch_initial_unempowered");
    const int coins = b->mCoins.mSize;
    for (int i = 0; i < 50; ++i) flower->Update();
    Check(t, b->mCoins.mSize == coins && flower->mLaunchRate == 0,
        "prism_actual_plant_updates_do_not_produce_marigold_coins");
    return true;
}
Projectile* PrismShot(Board* b, Plant* flower, ProjectileType type) {
    return b->AddProjectile(flower->mX + 120, flower->mY + 25, flower->mRenderOrder + 2, flower->mRow, type);
}
bool PrismSplitChecks(CustomTest& t, Board* b) {
    const auto seed = static_cast<SeedType>(int(SEED_CUSTOM_FIRST) + 4);
    Plant* flower = nullptr;
    for (Plant* p : b->mPlants) if (!p->mDead && p->mSeedType == seed) flower = p;
    if (!Check(t, flower != nullptr, "prism_split_existing_real_planted_fixture")) return false;
    Projectile* pea = PrismShot(b, flower, PROJECTILE_PEA);
    if (!Check(t, pea != nullptr, "prism_friendly_pea_created")) return false;
    pea->mPosX = float(flower->mX + 120 - 60); pea->mX = int(pea->mPosX);
    PvzpPrism::UpdateProjectile(pea);
    Check(t, Projectiles(b) == 1, "pea_outside_prism_field_does_not_split");
    for (int i = 0; i < 12 && Projectiles(b) == 1; ++i) pea->Update();
    if (!Check(t, Projectiles(b) == 3 && !pea->mDead && pea->mProjectileType == PROJECTILE_PEA,
        "real_pea_projectile_updates_cross_field_create_original_plus_two_fans", Projectiles(b))) return false;
    Plant* second = b->AddPlant(3, flower->mRow, seed);
    if (!Check(t, second != nullptr, "second_prism_stacked_field_fixture")) return false;
    const auto family = ProjectileIDs(b);
    bool fans = true;
    for (auto id : family) {
        Projectile* p = b->mProjectiles.DataArrayTryToGet(id);
        if (p != pea) fans = fans && p->mMotionType == MOTION_STAR && std::fabs(p->mVelY) > 0.1f;
        p->mPosX = float(second->mX + 120); p->mX = int(p->mPosX);
        PvzpPrism::UpdateProjectile(p);
        PvzpPrism::UpdateProjectile(p);
    }
    Check(t, fans && Projectiles(b) == 3, "original_and_fan_clones_split_once_across_two_flowers_no_exponential_stack");
    for (Projectile* p : b->mProjectiles) if (!p->mDead) p->Die();
    b->ProcessDeleteQueue();
    Projectile* cold = PrismShot(b, flower, PROJECTILE_SNOWPEA);
    if (!Check(t, cold != nullptr, "cold_prism_projectile_fixture")) return false;
    PvzpPrism::UpdateProjectile(cold);
    bool coldTypes = Projectiles(b) == 3;
    for (Projectile* p : b->mProjectiles) if (!p->mDead) coldTypes = coldTypes && p->mProjectileType == PROJECTILE_SNOWPEA;
    Check(t, coldTypes, "normal_prism_snow_pea_original_and_fans_keep_cold_type");
    for (Projectile* p : b->mProjectiles) if (!p->mDead) p->Die();
    b->ProcessDeleteQueue();
    Projectile* melon = PrismShot(b, flower, PROJECTILE_WINTERMELON);
    if (!Check(t, melon != nullptr, "lobbed_prism_projectile_fixture")) return false;
    melon->mMotionType = MOTION_LOBBED; melon->mPosZ = -80; melon->mVelZ = -2.5f;
    melon->mAccZ = 0.12f; melon->mVelX = 4.0f; melon->mVelY = 0;
    PvzpPrism::UpdateProjectile(melon);
    bool physics = Projectiles(b) == 3;
    for (Projectile* p : b->mProjectiles) if (!p->mDead) physics = physics &&
        p->mProjectileType == PROJECTILE_WINTERMELON && p->mMotionType == MOTION_LOBBED &&
        p->mPosZ == -80 && p->mVelZ == -2.5f && p->mAccZ == 0.12f && p->mVelX == 4.0f;
    Check(t, physics && melon->mVelY == 0, "lobbed_winter_melon_keeps_type_height_vertical_velocity_and_acceleration");
    for (Projectile* p : b->mProjectiles) if (!p->mDead) p->Die();
    b->ProcessDeleteQueue();
    Projectile* hostile = PrismShot(b, flower, PROJECTILE_ZOMBIE_PEA);
    Projectile* ball = PrismShot(b, flower, PROJECTILE_BASKETBALL);
    Projectile* backwards = PrismShot(b, flower, PROJECTILE_PEA);
    if (!Check(t, hostile && ball && backwards, "hostile_prism_exclusion_fixtures")) return false;
    backwards->mMotionType = MOTION_BACKWARDS;
    PvzpPrism::UpdateProjectile(hostile); PvzpPrism::UpdateProjectile(ball); PvzpPrism::UpdateProjectile(backwards);
    Check(t, Projectiles(b) == 3, "enemy_pea_basketball_and_backwards_pea_do_not_split");
    return true;
}
bool PrismEmpowerAndSave(CustomTest& t, Board* b) {
    ClearFixture(b);
    const auto seed = static_cast<SeedType>(int(SEED_CUSTOM_FIRST) + 4);
    Plant* flower = b->AddPlant(2, 2, seed);
    if (!Check(t, flower != nullptr, "prism_food_live_flower_fixture")) return false;
    b->mPlantFoodCount = 3;
    BoardClick(b, b->GetPlantFoodRect());
    const bool selecting = b->mPlantFoodSelecting;
    BoardClick(b, flower->mX + 40, flower->mY + 40);
    Check(t, selecting && b->mPlantFoodCount == 2 && PvzpCustomPlants::IsEmpowered(flower),
        "real_bean_ui_permanently_empowers_prism_and_consumes_one");
    BoardClick(b, b->GetPlantFoodRect());
    BoardClick(b, flower->mX + 40, flower->mY + 40);
    Check(t, b->mPlantFoodCount == 2 && PvzpCustomPlants::IsEmpowered(flower) &&
        !PvzpCustomPlants::ApplyFood(b, flower), "already_empowered_prism_rejects_second_real_bean_without_consumption");
    b->ClearCursor(); b->mPlantFoodSelecting = false;
    Projectile* shot = PrismShot(b, flower, PROJECTILE_SNOWPEA);
    Zombie* first = Enemy(b, ZOMBIE_NORMAL, 2);
    Zombie* next = Enemy(b, ZOMBIE_NORMAL, 2);
    Zombie* friendly = Enemy(b, ZOMBIE_NORMAL, 2);
    if (!Check(t, shot && first && next && friendly, "prism_piercing_live_projectile_and_zombies_fixture")) return false;
    for (Zombie* z : {first, next, friendly}) z->mBodyHealth = z->mBodyMaxHealth = 10000;
    friendly->StartMindControlled();
    PvzpPrism::UpdateProjectile(shot);
    const int hp = first->mBodyHealth;
    shot->DoImpact(first);
    Check(t, !shot->mDead && first->mBodyHealth < hp && first->mIceTrapCounter > 0 &&
        !PvzpPrism::CanHit(shot, first) && !PvzpPrism::CanHit(shot, friendly) && PvzpPrism::CanHit(shot, next),
        "empowered_actual_impact_damages_freezes_pierces_and_excludes_touched_id_and_friendly");
    const uint32_t flowerID = b->mPlants.DataArrayGetID(flower), shotID = b->mProjectiles.DataArrayGetID(shot);
    const uint32_t firstID = b->mZombies.DataArrayGetID(first), nextID = b->mZombies.DataArrayGetID(next);
    const uint32_t friendlyID = b->mZombies.DataArrayGetID(friendly);
    const auto state = PvzpCustomPlants::Save(b);
    std::vector<uint8_t> legacy;
    auto put = [&](uint32_t value) { for (int i = 0; i < 4; ++i) legacy.push_back(uint8_t(value >> (8 * i))); };
    put(1); put(1); put(flowerID); put(uint32_t(seed)); put(uint32_t(flower->mPlantHealth)); put(0); put(0); put(0);
    Check(t, PvzpCustomPlants::Load(b, legacy) && !PvzpCustomPlants::IsEmpowered(flower),
        "version1_custom_state_loads_with_unempowered_default");
    if (!Check(t, PvzpCustomPlants::Load(b, state) && PvzpCustomPlants::IsEmpowered(flower) &&
        !PvzpPrism::CanHit(shot, first), "version2_restores_empowerment_and_projectile_touched_ids_after_v1")) return false;
    const char* home = std::getenv("HOME");
    if (!Check(t, home && *home, "prism_portable_disposable_home")) return false;
    const std::string path = std::string(home) + "/pvzp-prism-regression.dat";
    if (!Check(t, LawnSaveGame(b, path), "prism_full_portable_save_with_permanent_and_lineage_state")) return false;
    if (!Check(t, b->LoadGame(path), "prism_full_portable_load_at_end_of_board_update")) return false;
    flower = b->mPlants.DataArrayTryToGet(flowerID); shot = b->mProjectiles.DataArrayTryToGet(shotID);
    first = b->mZombies.DataArrayTryToGet(firstID); next = b->mZombies.DataArrayTryToGet(nextID);
    friendly = b->mZombies.DataArrayTryToGet(friendlyID);
    if (!Check(t, flower && shot && first && next && friendly && PvzpCustomPlants::IsEmpowered(flower) &&
        !PvzpPrism::CanHit(shot, first) && !PvzpPrism::CanHit(shot, friendly) && PvzpPrism::CanHit(shot, next),
        "portable_restores_permanent_prism_and_prevents_repeat_id_and_friendly_hits")) return false;
    const int count = Projectiles(b);
    shot->mPosX = float(flower->mX + 120); shot->mX = int(shot->mPosX);
    PvzpPrism::UpdateProjectile(shot);
    Check(t, Projectiles(b) == count, "portable_restored_touched_prism_lineage_does_not_split_again");
    const auto loaded = PvzpCustomPlants::Save(b);
    Check(t, PvzpCustomPlants::Load(b, loaded) && PvzpCustomPlants::Load(b, loaded) &&
        PvzpCustomPlants::Save(b) == loaded, "prism_custom_repeated_load_is_idempotent");
    const int nextHP = next->mBodyHealth;
    shot->DoImpact(next);
    Check(t, !shot->mDead && next->mBodyHealth < nextHP && next->mIceTrapCounter > 0 &&
        !PvzpPrism::CanHit(shot, next), "restored_piercing_shot_hits_next_enemy_once_with_damage_and_frost");
    return true;
}
const PvzpMod::CustomZombie* CompositeConfig(std::string_view id) {
    for (const auto& c : PvzpMod::CustomZombies()) if (c.id == id) return &c;
    return nullptr;
}
Zombie* CompositeEnemy(Board* b, std::string_view id, int row = 2) {
    Zombie* z = PvzpCustomZombies::Spawn(b, id, row);
    if (z) {
        z->mPosX = 650; z->mPosY = z->GetPosYBasedOnRow(row);
        z->mX = int(z->mPosX); z->mY = int(z->mPosY);
        z->mZombiePhase = PHASE_ZOMBIE_NORMAL;
        z->mChilledCounter = z->mIceTrapCounter = z->mButteredCounter = 0;
        z->mIsEating = false;
    }
    return z;
}
void CompositeTicks(Board* b, int ticks) {
    // Deterministic unit ticks for the production sidecar updater only. Never
    // recursively call Board::Update from this end-of-frame live harness.
    for (int i = 0; i < ticks; ++i) PvzpCustomZombies::Update(b);
}
bool CompositeAbilities(CustomTest& t, Board* b) {
    ClearFixture(b);
    if (!Check(t, PvzpMod::CustomZombies().size() == 14 && PvzpMod::CustomLevels().size() == 7 && b->CanUseModTools(),
        "composite_live_fixture_fourteen_variants_seven_levels_tools_enabled")) return false;
    for (const auto& c : PvzpMod::CustomZombies()) {
        Zombie* z = CompositeEnemy(b, c.id);
        if (!Check(t, z && z->mZombieType == ZOMBIE_NORMAL && PvzpCustomZombies::Id(z) == c.id &&
            PvzpCustomZombies::Name(z) == c.name && z->mBodyHealth == c.health && z->mBodyMaxHealth == c.health &&
            (c.armor == 0 || (z->mHelmHealth == c.armor && z->mHelmMaxHealth == c.armor)) &&
            PvzpCustomZombies::Int(z, "speedPercent", -1) == c.speedPercent &&
            PvzpCustomZombies::Int(z, "damagePercent", -1) == c.damagePercent &&
            PvzpCustomZombies::Int(z, "slowResistancePercent", -1) == c.slowResistancePercent &&
            PvzpCustomZombies::Int(z, "sunDrop", -1) == c.sunDrop &&
            PvzpCustomZombies::Int(z, "tintR", -1) == c.tintR && PvzpCustomZombies::Int(z, "tintG", -1) == c.tintG &&
            PvzpCustomZombies::Int(z, "tintB", -1) == c.tintB && PvzpCustomZombies::Int(z, "unknown-field", -77) == -77,
            "composite_spawn_by_stable_id_retains_builtin_enum_actual_hp_armor_name_and_effective_stats")) return false;
    }
    const int population = LiveZombies(b);
    Check(t, !PvzpCustomZombies::CanSpawn(b, "pvzp.unknown", 2) && !PvzpCustomZombies::Spawn(b, "pvzp.unknown", 2) &&
        !PvzpCustomZombies::CanSpawn(b, "pvzp.seed_packet_head", -2) &&
        !PvzpCustomZombies::CanSpawn(b, "pvzp.seed_packet_head", 6) && LiveZombies(b) == population,
        "composite_unknown_identity_and_invalid_rows_do_not_spawn_or_mutate_population");

    ClearFixture(b);
    const auto* jam = CompositeConfig("pvzp.seed_packet_head");
    Zombie* saboteur = CompositeEnemy(b, "pvzp.seed_packet_head");
    if (!Check(t, jam && saboteur && b->mSeedBank->mNumPackets >= 2, "composite_seed_jam_fixture")) return false;
    auto& firstPacket = b->mSeedBank->mSeedPackets[0];
    auto& secondPacket = b->mSeedBank->mSeedPackets[1];
    firstPacket.SetPacketType(SEED_PEASHOOTER, SEED_NONE);
    secondPacket.SetPacketType(SEED_SUNFLOWER, SEED_NONE);
    CompositeTicks(b, jam->interval - 1);
    Check(t, firstPacket.mActive && !firstPacket.mRefreshing, "seed_jam_no_early_effect_before_exact_interval");
    const auto beforePause = PvzpCustomZombies::Save(b);
    b->Pause(true); CompositeTicks(b, 20); b->Pause(false);
    Check(t, PvzpCustomZombies::Save(b) == beforePause && firstPacket.mActive,
        "paused_composite_update_does_not_advance_cooldown_or_mutate_seed_bank");
    CompositeTicks(b, 1);
    Check(t, firstPacket.mRefreshing && !firstPacket.mActive && firstPacket.mRefreshCounter == 0 &&
        firstPacket.mRefreshTime == jam->magnitude && secondPacket.mActive,
        "seed_jam_exact_interval_delays_one_real_ready_packet_by_configured200_ticks");
    CompositeTicks(b, jam->interval);
    Check(t, secondPacket.mRefreshing && !secondPacket.mActive && secondPacket.mRefreshTime == jam->magnitude &&
        firstPacket.mRefreshCounter == 0, "seed_jam_next_interval_advances_to_second_packet_without_double_jamming_first");
    ClearFixture(b);
    saboteur = CompositeEnemy(b, "pvzp.seed_packet_head");
    if (!Check(t, saboteur != nullptr, "seed_jam_refreshing_packet_fixture")) return false;
    firstPacket.mRefreshing = true; firstPacket.mActive = false;
    firstPacket.mRefreshTime = 1000; firstPacket.mRefreshCounter = 350;
    saboteur->mIceTrapCounter = 1;
    CompositeTicks(b, jam->interval);
    Check(t, firstPacket.mRefreshCounter == 350, "ice_trapped_saboteur_cannot_advance_or_jam_refreshing_packet");
    saboteur->mIceTrapCounter = 0;
    CompositeTicks(b, jam->interval);
    Check(t, firstPacket.mRefreshCounter == 150 && firstPacket.mRefreshTime == 1000,
        "unfrozen_saboteur_subtracts200_existing_recharge_progress_without_replacing_recharge_length");

    ClearFixture(b);
    const auto* raise = CompositeConfig("pvzp.tombstone_head");
    Zombie* summoner = CompositeEnemy(b, "pvzp.tombstone_head");
    if (!Check(t, raise && summoner, "composite_tombstone_summoner_fixture")) return false;
    CompositeTicks(b, raise->interval - 1);
    Check(t, LiveZombies(b) == 1, "tombstone_no_early_summon_before_interval");
    CompositeTicks(b, 1);
    Check(t, LiveZombies(b) == 3, "tombstone_exact_interval_spawns_two_real_normal_minions");
    CompositeTicks(b, raise->interval * 2);
    Check(t, LiveZombies(b) == 7, "tombstone_three_activations_spawn_exactly_six_minions");
    CompositeTicks(b, raise->interval * 2);
    Check(t, LiveZombies(b) == 7, "tombstone_lifetime_cap_prevents_fourth_and_fifth_activation_growth");
    ClearFixture(b);
    summoner = CompositeEnemy(b, "pvzp.tombstone_head");
    if (!Check(t, summoner != nullptr, "friendly_tombstone_fixture")) return false;
    summoner->StartMindControlled();
    CompositeTicks(b, raise->interval);
    bool charmed = LiveZombies(b) == 3;
    for (Zombie* z : b->mZombies) if (!z->mDead) charmed = charmed && z->mMindControlled;
    Check(t, charmed, "mindcontrolled_tombstone_raises_same_faction_minions_not_hostile_zombies");

    ClearFixture(b);
    const auto* heal = CompositeConfig("pvzp.sunflower_head");
    Zombie* medic = CompositeEnemy(b, "pvzp.sunflower_head");
    Zombie* nearbyAlly = Enemy(b, ZOMBIE_NORMAL, 3);
    Zombie* distantAlly = Enemy(b, ZOMBIE_NORMAL, 4);
    Zombie* hostileFaction = Enemy(b, ZOMBIE_NORMAL, 2);
    if (!Check(t, heal && medic && nearbyAlly && distantAlly && hostileFaction, "composite_medic_range_faction_fixture")) return false;
    for (Zombie* z : {nearbyAlly, distantAlly, hostileFaction}) { z->mBodyMaxHealth = 2000; z->mBodyHealth = 1000; }
    hostileFaction->StartMindControlled(); medic->mBodyHealth -= 200;
    const int medicHP = medic->mBodyHealth;
    CompositeTicks(b, heal->interval - 1);
    Check(t, nearbyAlly->mBodyHealth == 1000 && medic->mBodyHealth == medicHP, "medic_does_not_heal_before_interval");
    CompositeTicks(b, 1);
    Check(t, nearbyAlly->mBodyHealth == 1100 && medic->mBodyHealth == medicHP + 100 &&
        distantAlly->mBodyHealth == 1000 && hostileFaction->mBodyHealth == 1000,
        "sunflower_medic_heals100_self_and_near_adjacent_row_allies_but_not_far_row_or_opposite_faction");
    nearbyAlly->mBodyHealth = nearbyAlly->mBodyMaxHealth - 20;
    CompositeTicks(b, heal->interval);
    Check(t, nearbyAlly->mBodyHealth == nearbyAlly->mBodyMaxHealth, "sunflower_medic_healing_clamps_to_actual_max_health");

    ClearFixture(b);
    const auto* bite = CompositeConfig("pvzp.chomper_head");
    Plant* victim = b->AddPlant(3, 2, SEED_WALLNUT);
    Plant* otherRow = b->AddPlant(3, 3, SEED_WALLNUT);
    Zombie* chomper = CompositeEnemy(b, "pvzp.chomper_head");
    if (!Check(t, bite && victim && otherRow && chomper, "composite_chomper_target_fixture")) return false;
    victim->mPlantHealth = victim->mPlantMaxHealth = 10000;
    const int otherHP = otherRow->mPlantHealth;
    chomper->mPosX = float(victim->mX + 80); chomper->mX = int(chomper->mPosX);
    chomper->mBodyHealth -= 200;
    const int beforeBite = chomper->mBodyHealth;
    CompositeTicks(b, bite->interval);
    Check(t, victim->mPlantHealth == 9500 && otherRow->mPlantHealth == otherHP &&
        chomper->mBodyHealth == beforeBite + 125 && victim->mEatenFlashCountdown > 0,
        "chomper_ability_deals500_to_real_near_row_plant_and_lifesteals125_without_other_row_damage");
    return true;
}
uint32_t CarnivalGet(const std::vector<uint8_t>& bytes, size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) return UINT32_MAX;
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= uint32_t(bytes[at + i]) << (8 * i);
    return value;
}
void CarnivalSet(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    if (at > bytes.size() || bytes.size() - at < 4) return;
    for (int i = 0; i < 4; ++i) bytes[at + i] = uint8_t(value >> (8 * i));
}
struct CarnivalLayout {
    bool ok = false;
    std::map<uint32_t, size_t> actors;
    size_t frozen = 0, shots = 0, haste = 0;
    explicit CarnivalLayout(const std::vector<uint8_t>& bytes) {
        const uint32_t version = CarnivalGet(bytes, 0), count = CarnivalGet(bytes, 4);
        if ((version != 1 && version != 2) || count > 1024) return;
        size_t at = 8;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t length = CarnivalGet(bytes, at + 4);
            if (length > 96 || at + 8 + length + (version == 2 ? 24 : 12) > bytes.size()) return;
            actors[CarnivalGet(bytes, at)] = at + 8 + length;
            at += 8 + length + (version == 2 ? 24 : 12);
        }
        frozen = at; const uint32_t frozenCount = CarnivalGet(bytes, at);
        if (frozenCount > 1024) return;
        at += 4 + 8 * size_t(frozenCount); shots = at;
        const uint32_t shotCount = CarnivalGet(bytes, at); at += 4;
        if (shotCount > 1024) return;
        for (uint32_t i = 0; i < shotCount; ++i) {
            const uint32_t length = CarnivalGet(bytes, at + 12);
            if (length > 8 || at + 16 + length > bytes.size()) return;
            at += 16 + length;
        }
        haste = at;
        if (version == 2) {
            const uint32_t hasteCount = CarnivalGet(bytes, at);
            if (hasteCount > 1024) return;
            at += 4 + 12 * size_t(hasteCount);
        }
        ok = at == bytes.size();
    }
};
int CarnivalPhase(Board* b, Zombie* z) {
    const auto bytes = PvzpCustomZombies::Save(b); const CarnivalLayout layout(bytes);
    if (!layout.ok || !z || CarnivalGet(bytes, 0) != 2) return 0;
    const auto found = layout.actors.find(b->mZombies.DataArrayGetID(z));
    return found == layout.actors.end() ? -1 : int(CarnivalGet(bytes, found->second + 12));
}
Coin* CarnivalSun(Board* b, Zombie* z, int dx = 0, int dy = 0, CoinType type = COIN_SUN) {
    Coin* coin = b->AddCoin(int(z->mPosX) + dx, int(z->mPosY) + dy, type, COIN_MOTION_FROM_PLANT);
    if (coin) { coin->mPosX = z->mPosX + dx; coin->mPosY = z->mPosY + dy; coin->mIsBeingCollected = false; }
    return coin;
}
Projectile* CarnivalPea(Board* b, Zombie* target, ProjectileType type = PROJECTILE_PEA) {
    return b->AddProjectile(int(target->mPosX), int(target->mPosY) + 35, target->mRenderOrder + 1, target->mRow, type);
}
bool CarnivalGuardianAndRally(CustomTest& t, Board* b) {
    ClearFixture(b);
    Zombie* guard = CompositeEnemy(b, "pvzp.pumpkin_guard");
    Zombie* ally = Enemy(b, ZOMBIE_NORMAL, 2);
    if (!Check(t, guard && ally, "carnival_guard_actual_damage_fixture")) return false;
    ally->mBodyHealth = ally->mBodyMaxHealth = 10000;
    const int armor = guard->mHelmHealth;
    ally->TakeDamage(100, 0);
    Check(t, ally->mBodyHealth == 9940 && guard->mHelmHealth == armor - 40,
        "pumpkin_real_TakeDamage_redirects40percent_to_near_same_faction_armor_once");
    guard->mIceTrapCounter = 1; ally->TakeDamage(100, 0);
    Check(t, ally->mBodyHealth == 9840 && guard->mHelmHealth == armor - 40,
        "pumpkin_ice_trap_disables_guardian_redirect_without_wearing_armor");
    guard->mIceTrapCounter = 0; guard->mButteredCounter = 1; ally->TakeDamage(100, 0);
    Check(t, ally->mBodyHealth == 9740 && guard->mHelmHealth == armor - 40,
        "pumpkin_butter_disables_guardian_redirect");
    guard->mButteredCounter = 0; ally->StartMindControlled(); ally->TakeDamage(100, 0);
    Check(t, ally->mBodyHealth == 9640 && guard->mHelmHealth == armor - 40,
        "pumpkin_never_shields_opposite_faction");
    ally->mMindControlled = false; guard->mPosX = 900; guard->mX = 900; ally->TakeDamage(100, 0);
    Check(t, ally->mBodyHealth == 9540 && guard->mHelmHealth == armor - 40,
        "pumpkin_distant_guardian_outside170px_does_not_redirect");
    guard->mPosX = 650; guard->mX = 650;
    Zombie* secondGuard = CompositeEnemy(b, "pvzp.pumpkin_guard", 3);
    if (!Check(t, secondGuard != nullptr, "pumpkin_second_guardian_nonrecursive_fixture")) return false;
    const int secondArmor = secondGuard->mHelmHealth;
    guard->TakeDamage(100, 0);
    Check(t, guard->mHelmHealth == armor - 140 && secondGuard->mHelmHealth == secondArmor,
        "pumpkin_guardian_self_damage_never_recurses_into_another_guardian");
    secondGuard->mIceTrapCounter = 1; guard->mHelmHealth = 0;
    ally->TakeDamage(100, 0);
    Check(t, ally->mBodyHealth == 9440, "pumpkin_broken_armor_disables_guardian_aura");
    guard->mHelmHealth = armor; // Ensure a live, otherwise eligible guardian exists.
    const auto oldType = ally->mZombieType; ally->mZombieType = ZOMBIE_BOSS;
    Check(t, PvzpCustomZombies::RedirectDamage(ally, 100, 0) == 100,
        "pumpkin_boss_damage_is_never_redirected");
    ally->mZombieType = oldType;

    ClearFixture(b);
    Zombie* commander = CompositeEnemy(b, "pvzp.espresso_commander");
    Zombie* commander2 = CompositeEnemy(b, "pvzp.espresso_commander", 3);
    ally = Enemy(b, ZOMBIE_NORMAL, 2);
    Zombie* far = Enemy(b, ZOMBIE_NORMAL, 0);
    Zombie* hostile = Enemy(b, ZOMBIE_NORMAL, 2);
    Zombie* bossExclusion = Enemy(b, ZOMBIE_NORMAL, 2);
    if (!Check(t, commander && commander2 && ally && far && hostile && bossExclusion, "coffee_nonstacking_vanilla_faction_fixture")) return false;
    hostile->StartMindControlled();
    bossExclusion->mZombieType = ZOMBIE_BOSS; // No Board/Zombie updates while temporarily testing the predicate.
    CompositeTicks(b, 499);
    Check(t, PvzpCustomZombies::Int(ally, "speedPercent", 100) == 100,
        "coffee_rally_does_not_apply_before_exact500tick_interval");
    CompositeTicks(b, 1);
    Check(t, PvzpCustomZombies::Int(ally, "speedPercent", 100) == 150 &&
        PvzpCustomZombies::Int(commander, "speedPercent", 100) == 142 &&
        PvzpCustomZombies::Int(far, "speedPercent", 100) == 100 &&
        PvzpCustomZombies::Int(hostile, "speedPercent", 100) == 100,
        "coffee_two_commanders_apply_single50percent_rally_to_near_vanilla_and_custom_same_faction_only");
    Check(t, PvzpCustomZombies::Int(bossExclusion, "speedPercent", 100) == 100,
        "coffee_rally_never_buffs_boss_even_when_otherwise_near_same_faction");
    bossExclusion->mZombieType = ZOMBIE_NORMAL;
    // Compare production walking displacement, not only the sidecar accessor.
    Reanimation* body = b->mApp->ReanimationTryToGet(ally->mBodyReanimID);
    if (!Check(t, body != nullptr, "coffee_vanilla_walking_reanimation_fixture")) return false;
    ally->mVelX = 1; float base = ally->mVelX;
    if (body->TrackExists("_ground")) {
        base = 0;
        for (int frame = 0; frame < 100 && std::fabs(base) < 0.001f; ++frame) {
            body->mAnimTime = frame / 100.0f; base = body->GetTrackVelocity("_ground") * ally->mScaleZombie;
        }
    }
    const float beforeX = ally->mPosX; ally->UpdateZombieWalking();
    Check(t, std::fabs(base) > 0.001f && std::fabs(beforeX - ally->mPosX - base * 1.5f) < 0.001f,
        "coffee_rally_changes_actual_vanilla_walking_displacement_once");
    CompositeTicks(b, 175);
    const auto partial = PvzpCustomZombies::Save(b);
    Check(t, CarnivalGet(partial, 0) == 2 && PvzpCustomZombies::Load(b, partial) && PvzpCustomZombies::Save(b) == partial,
        "coffee_remaining75tick_haste_saves_and_roundtrips_as_v2");
    CompositeTicks(b, 74);
    Check(t, PvzpCustomZombies::Int(ally, "speedPercent", 100) == 150,
        "coffee_restored75tick_rally_remains_active_after74_updates");
    CompositeTicks(b, 1);
    Check(t, PvzpCustomZombies::Int(ally, "speedPercent", 100) == 100,
        "coffee_rally_expires_on_exact250th_update_not_one_early_or_late");
    return true;
}
bool CarnivalWindups(CustomTest& t, Board* b) {
    ClearFixture(b);
    Zombie* garlic = CompositeEnemy(b, "pvzp.garlic_hopper");
    if (!Check(t, garlic != nullptr, "garlic_adjacent_lane_windup_fixture")) return false;
    const float x = garlic->mPosX;
    CompositeTicks(b, 399);
    Check(t, garlic->mRow == 2 && CarnivalPhase(b, garlic) == 0, "garlic_no_early_lane_dash_before400");
    CompositeTicks(b, 1);
    Check(t, garlic->mRow == 2 && CarnivalPhase(b, garlic) == 80, "garlic_exact400_arms_visible80tick_lane_windup");
    CompositeTicks(b, 30);
    const auto pending = PvzpCustomZombies::Save(b);
    garlic->mIceTrapCounter = 1; CompositeTicks(b, 20);
    Check(t, PvzpCustomZombies::Save(b) == pending, "garlic_freeze_pauses_pending_phase_and_target_row_exactly");
    garlic->mIceTrapCounter = 0; garlic->mButteredCounter = 1; CompositeTicks(b, 20);
    Check(t, PvzpCustomZombies::Save(b) == pending, "garlic_butter_also_pauses_pending_lane_windup");
    garlic->mButteredCounter = 0;
    Check(t, PvzpCustomZombies::Load(b, pending) && PvzpCustomZombies::Save(b) == pending,
        "garlic_midwindup_phase50_and_adjacent_target_row_roundtrip_without_reset");
    CompositeTicks(b, 49);
    Check(t, garlic->mRow == 2 && CarnivalPhase(b, garlic) == 1, "garlic_restored50tick_windup_does_not_shift_after49");
    CompositeTicks(b, 1);
    Check(t, garlic->mRow == 1 && garlic->mPosX == x && garlic->mX == int(x) &&
        garlic->mPosY == garlic->GetPosYBasedOnRow(1) && garlic->mY == int(garlic->mPosY),
        "garlic_exact80_changes_one_adjacent_row_with_synced_y_and_no_x_teleport");
    for (bool eating : {true, false}) {
        ClearFixture(b); garlic = CompositeEnemy(b, "pvzp.garlic_hopper");
        Plant* blocker = eating ? nullptr : b->AddPlant(3, 2, SEED_WALLNUT);
        if (!Check(t, garlic && (eating || blocker), "garlic_blocker_or_eating_fixture")) return false;
        garlic->mIsEating = eating;
        if (blocker) { garlic->mPosX = float(blocker->mX + 80); garlic->mX = int(garlic->mPosX); }
        CompositeTicks(b, 480);
        Check(t, garlic->mRow == 2 && CarnivalPhase(b, garlic) == 0,
            "garlic_cannot_arm_or_shift_while_eating_or_within105_of_plant");
    }
    ClearFixture(b);
    Plant* plant = b->AddPlant(3, 2, SEED_WALLNUT);
    Plant* adjacent = b->AddPlant(3, 1, SEED_WALLNUT);
    Plant* distant = b->AddPlant(0, 2, SEED_WALLNUT);
    Zombie* cherry = CompositeEnemy(b, "pvzp.cherry_courier");
    if (!Check(t, plant && adjacent && distant && cherry, "cherry_fuse_radius_and_row_fixture")) return false;
    for (Plant* p : {plant, adjacent, distant}) p->mPlantHealth = p->mPlantMaxHealth = 10000;
    cherry->mPosX = float(plant->mX + 80); cherry->mX = int(cherry->mPosX);
    CompositeTicks(b, 50);
    Check(t, CarnivalPhase(b, cherry) == 100 && plant->mPlantHealth == 10000,
        "cherry_arms_visible100tick_fuse_near_real_plant_without_immediate_damage");
    cherry->mIceTrapCounter = 1; const auto fuse = PvzpCustomZombies::Save(b); CompositeTicks(b, 100);
    Check(t, PvzpCustomZombies::Save(b) == fuse && plant->mPlantHealth == 10000,
        "cherry_freeze_pauses_pending_fuse_and_prevents_early_explosion");
    cherry->mIceTrapCounter = 0; cherry->mButteredCounter = 1; CompositeTicks(b, 10);
    Check(t, PvzpCustomZombies::Save(b) == fuse, "cherry_butter_pauses_visible_fuse");
    cherry->mButteredCounter = 0;
    Check(t, PvzpCustomZombies::Load(b, fuse) && PvzpCustomZombies::Save(b) == fuse,
        "cherry_pending_fuse_roundtrip_does_not_restart_or_immediately_explode");
    CompositeTicks(b, 99);
    Check(t, !cherry->mDead && plant->mPlantHealth == 10000, "cherry_no_explosion_before_final_fuse_tick");
    CompositeTicks(b, 1);
    Check(t, cherry->mDead && cherry->mDroppedLoot && plant->mPlantHealth == 9100 && adjacent->mPlantHealth == 9100 &&
        distant->mPlantHealth == 10000, "cherry_exact_fuse_deals900_within150px_adjacent_rows_then_dies_with_loot");
    ClearFixture(b); plant = b->AddPlant(3, 2, SEED_WALLNUT); cherry = CompositeEnemy(b, "pvzp.cherry_courier");
    if (!Check(t, plant && cherry, "cherry_killed_fuse_disarm_fixture")) return false;
    cherry->mPosX = float(plant->mX + 80); cherry->mX = int(cherry->mPosX);
    CompositeTicks(b, 50); const int hp = plant->mPlantHealth; cherry->TakeDamage(5000, 0); CompositeTicks(b, 150);
    Check(t, cherry->IsDeadOrDying() && plant->mPlantHealth == hp,
        "cherry_killed_before_fuse_completion_cannot_explode_later");
    ClearFixture(b); plant = b->AddPlant(3, 2, SEED_WALLNUT); cherry = CompositeEnemy(b, "pvzp.cherry_courier");
    Zombie* hostile = Enemy(b, ZOMBIE_NORMAL, 2); Zombie* friendZombie = Enemy(b, ZOMBIE_NORMAL, 3);
    if (!Check(t, plant && cherry && hostile && friendZombie, "cherry_charmed_faction_explosion_fixture")) return false;
    cherry->StartMindControlled(); friendZombie->StartMindControlled();
    for (Zombie* z : {hostile, friendZombie}) z->mBodyHealth = z->mBodyMaxHealth = 10000;
    const int plantHP = plant->mPlantHealth;
    CompositeTicks(b, 150);
    Check(t, cherry->mDead && hostile->mBodyHealth == 9100 && friendZombie->mBodyHealth == 10000 && plant->mPlantHealth == plantHP,
        "cherry_charmed_fuse_targets_hostile_zombies_not_plants_or_charmed_allies");
    return true;
}
bool CarnivalMagnetAndRebound(CustomTest& t, Board* b) {
    ClearFixture(b);
    Zombie* magnet = CompositeEnemy(b, "pvzp.magnet_pickpocket");
    if (!Check(t, magnet != nullptr, "magnet_uncollected_sun_fixture")) return false;
    std::vector<Coin*> nearby;
    for (int i = 0; i < 5; ++i) nearby.push_back(CarnivalSun(b, magnet, i * 3));
    Coin* outsideX = CarnivalSun(b, magnet, 221), *outsideY = CarnivalSun(b, magnet, 0, 161);
    Coin* collecting = CarnivalSun(b, magnet), *moneyCoin = CarnivalSun(b, magnet, 0, 0, COIN_SILVER);
    if (!Check(t, std::all_of(nearby.begin(), nearby.end(), [](Coin* c) { return c != nullptr; }) &&
        outsideX && outsideY && collecting && moneyCoin, "magnet_actual_coin_range_collection_and_money_fixtures")) return false;
    collecting->mIsBeingCollected = true;
    b->mSunMoney = 1000;
    CompositeTicks(b, 349);
    Check(t, PvzpCustomZombies::Int(magnet, "sunDrop", 0) == 100 && Suns(b) == 8,
        "magnet_does_not_steal_before_exact350tick_interval");
    CompositeTicks(b, 1);
    int stolen = 0; for (Coin* c : nearby) stolen += c->mDead;
    Check(t, stolen == 3 && PvzpCustomZombies::Int(magnet, "sunDrop", 0) == 175 && b->mSunMoney == 1000 &&
        !outsideX->mDead && !outsideY->mDead && !collecting->mDead && !moneyCoin->mDead,
        "magnet_takes_only3_uncollected_near_suns_up_to75_without_touching_banked_money_or_other_coins");
    for (int i = 0; i < 12; ++i) CarnivalSun(b, magnet);
    CompositeTicks(b, 350 * 4);
    Check(t, PvzpCustomZombies::Int(magnet, "sunDrop", 0) == 400 && b->mSunMoney == 1000,
        "magnet_held_sun_caps_at300_and_total_defeat_bounty_includes_base100");
    const int remaining = Suns(b); CompositeTicks(b, 350);
    Check(t, Suns(b) == remaining && PvzpCustomZombies::Int(magnet, "sunDrop", 0) == 400,
        "magnet_at300_capacity_leaves_additional_uncollected_sun_untouched");
    const int beforeBounty = Suns(b); magnet->DropLoot();
    Check(t, Suns(b) == beforeBounty + 16 && b->mSunMoney == 1000, "magnet_defeat_returns_held300_plus_base100_as16_collectible_suns");
    magnet->DropLoot();
    Check(t, Suns(b) == beforeBounty + 16, "magnet_held_bounty_is_returned_only_once_by_existing_loot_guard");
    for (const auto type : {COIN_SMALLSUN, COIN_LARGESUN}) {
        ClearFixture(b); magnet = CompositeEnemy(b, "pvzp.magnet_pickpocket");
        if (!Check(t, magnet != nullptr, "magnet_independent_coin_count_and_budget_fixture")) return false;
        for (int i = 0; i < 5; ++i) if (!CarnivalSun(b, magnet, 0, 0, type)) return false;
        CompositeTicks(b, 350);
        Check(t, Suns(b) == (type == COIN_SMALLSUN ? 2 : 4) &&
            PvzpCustomZombies::Int(magnet, "sunDrop", 0) == (type == COIN_SMALLSUN ? 145 : 150),
            "magnet_three_small15sun_count_cap_and_one_large50sun_budget_cap_are_independent");
    }
    ClearFixture(b); magnet = CompositeEnemy(b, "pvzp.magnet_pickpocket");
    if (!Check(t, magnet != nullptr, "magnet_charmed_return_fixture")) return false;
    for (int i = 0; i < 3; ++i) CarnivalSun(b, magnet);
    CompositeTicks(b, 350); magnet->StartMindControlled(); Coin* charmCoin = CarnivalSun(b, magnet);
    const int bank = b->mSunMoney; CompositeTicks(b, 350);
    Check(t, charmCoin && charmCoin->mIsBeingCollected && b->mSunMoney == bank + 75 &&
        PvzpCustomZombies::Int(magnet, "sunDrop", 0) == 100,
        "magnet_charm_returns_previously_held_sun_and_autocollects_new_coin_instead_of_stealing");

    ClearFixture(b);
    Zombie* rebound = CompositeEnemy(b, "pvzp.spring_tallnut");
    Plant* target = b->AddPlant(2, 2, SEED_WALLNUT);
    if (!Check(t, rebound && target, "rebound_actual_impact_fixture")) return false;
    target->mPlantHealth = target->mPlantMaxHealth = 10000;
    Projectile* pea = CarnivalPea(b, rebound);
    if (!Check(t, pea != nullptr, "rebound_incoming_real_pea_fixture")) return false;
    const int armor = rebound->mHelmHealth; pea->DoImpact(rebound);
    Projectile* reflected = nullptr;
    for (Projectile* p : b->mProjectiles) if (!p->mDead && p->mProjectileType == PROJECTILE_ZOMBIE_PEA) reflected = p;
    if (!Check(t, pea->mDead && reflected && Projectiles(b) == 1 && reflected->mMotionType == MOTION_BACKWARDS &&
        rebound->mHelmHealth == armor - 20, "rebound_real_DoImpact_consumes_incoming20_armor_and_emits_one_hostile_pea")) return false;
    const auto flight = PvzpCustomZombies::Save(b);
    Check(t, CarnivalGet(flight, 0) == 2 && PvzpCustomZombies::Load(b, flight) && PvzpCustomZombies::Save(b) == flight,
        "rebound_inflight_damage35_and_cooldown80_roundtrip_as_v2");
    const Rect rect = target->GetPlantRect(); reflected->mPosX = float(rect.mX + rect.mWidth / 2);
    reflected->mPosY = float(rect.mY + 10); reflected->mPosZ = 0; reflected->mX = int(reflected->mPosX); reflected->mY = int(reflected->mPosY);
    reflected->CheckForCollision();
    Check(t, reflected->mDead && target->mPlantHealth == 9965, "rebound_actual_hostile_projectile_collision_deals_configured35_damage");
    Projectile* duringCooldown = CarnivalPea(b, rebound); if (!duringCooldown) return false;
    duringCooldown->DoImpact(rebound);
    Check(t, duringCooldown->mDead && Projectiles(b) == 0 && rebound->mHelmHealth == armor - 40,
        "rebound_second_actual_impact_during80tick_cooldown_damages_normally_without_reflecting");
    CompositeTicks(b, 79); pea = CarnivalPea(b, rebound); if (!pea) return false;
    Check(t, !PvzpCustomZombies::ReflectProjectile(pea, rebound), "rebound_not_ready_after79_cooldown_updates");
    CompositeTicks(b, 1);
    Check(t, PvzpCustomZombies::ReflectProjectile(pea, rebound), "rebound_rearms_on_exact80th_update");
    for (int exclusion = 0; exclusion < 8; ++exclusion) {
        ClearFixture(b); rebound = CompositeEnemy(b, "pvzp.spring_tallnut");
        if (!rebound) return false;
        pea = CarnivalPea(b, rebound, exclusion == 0 ? PROJECTILE_FIREBALL : exclusion == 1 ? PROJECTILE_MELON : PROJECTILE_PEA);
        if (!pea) return false;
        if (exclusion == 2) pea->SetElectric(true);
        if (exclusion == 3) pea->mMotionType = MOTION_LOBBED;
        if (exclusion == 4) rebound->mIceTrapCounter = 1;
        if (exclusion == 5) rebound->mHelmHealth = 0;
        if (exclusion == 6) rebound->mButteredCounter = 1;
        if (exclusion == 7) rebound->StartMindControlled();
        const int beforeShots = Projectiles(b);
        Check(t, !PvzpCustomZombies::ReflectProjectile(pea, rebound) && !pea->mDead && Projectiles(b) == beforeShots,
            "rebound_fire_melon_electric_lobbed_frozen_broken_buttered_or_charmed_bypasses_without_spawning");
    }
    ClearFixture(b); rebound = CompositeEnemy(b, "pvzp.spring_tallnut");
    Plant* prism = b->AddPlant(2, 2, static_cast<SeedType>(int(SEED_CUSTOM_FIRST) + 4));
    if (!Check(t, rebound && prism, "rebound_prism_lineage_bypass_fixture")) return false;
    pea = PrismShot(b, prism, PROJECTILE_PEA); if (!pea) return false;
    PvzpPrism::UpdateProjectile(pea);
    Check(t, PvzpPrism::IsRefracted(pea) && !PvzpCustomZombies::ReflectProjectile(pea, rebound) && !pea->mDead,
        "rebound_prism_refracted_pea_lineage_bypasses_reflection");
    return true;
}

bool CarnivalSaveV2(CustomTest& t, Board* b) {
    ClearFixture(b);
    Zombie* garlic = CompositeEnemy(b, "pvzp.garlic_hopper", 4);
    if (!Check(t, garlic != nullptr, "v2_pending_garlic_source_fixture")) return false;
    CompositeTicks(b, 400); garlic->mIceTrapCounter = 1;
    Zombie* cherry = CompositeEnemy(b, "pvzp.cherry_courier", 2);
    Plant* cherryTarget = b->AddPlant(3, 2, SEED_WALLNUT);
    if (!Check(t, cherry && cherryTarget, "v2_pending_cherry_source_fixture")) return false;
    cherryTarget->mPlantHealth = cherryTarget->mPlantMaxHealth = 10000;
    cherry->mPosX = float(cherryTarget->mX + 80); cherry->mX = int(cherry->mPosX);
    CompositeTicks(b, 50); cherry->mButteredCounter = 1;
    Zombie* magnet = CompositeEnemy(b, "pvzp.magnet_pickpocket", 0);
    if (!Check(t, magnet != nullptr, "v2_stored_sun_source_fixture")) return false;
    for (int i = 0; i < 3; ++i) if (!CarnivalSun(b, magnet)) return false;
    CompositeTicks(b, 350); magnet->mIceTrapCounter = 1;
    Zombie* commander = CompositeEnemy(b, "pvzp.espresso_commander", 0);
    Zombie* vanilla = Enemy(b, ZOMBIE_NORMAL, 0);
    if (!Check(t, commander && vanilla, "v2_haste_source_and_vanilla_beneficiary_fixture")) return false;
    CompositeTicks(b, 500); commander->mIceTrapCounter = 1;
    Zombie* coldRebound = CompositeEnemy(b, "pvzp.spring_tallnut", 3);
    Plant* frozen = b->AddPlant(2, 3, SEED_PEASHOOTER);
    if (!Check(t, coldRebound && frozen, "v2_freeze75_source_fixture")) return false;
    frozen->mPlantHealth = frozen->mPlantMaxHealth = 10000;
    Projectile* cold = CarnivalPea(b, coldRebound, PROJECTILE_SNOWPEA);
    if (!cold) return false;
    cold->DoImpact(coldRebound);
    Projectile* returned = nullptr;
    for (Projectile* p : b->mProjectiles) if (!p->mDead && p->mProjectileType == PROJECTILE_ZOMBIE_PEA) returned = p;
    if (!Check(t, returned != nullptr, "v2_snow_rebound_produces_real_freezing_enemy_projectile")) return false;
    const Rect rect = frozen->GetPlantRect(); returned->mPosX = float(rect.mX + rect.mWidth / 2);
    returned->mPosY = float(rect.mY + 10); returned->mPosZ = 0; returned->mX = int(returned->mPosX); returned->mY = int(returned->mPosY);
    returned->CheckForCollision(); CompositeTicks(b, 15);
    Check(t, frozen->mPlantHealth == 9965 && PvzpCustomZombies::PlantFrozen(frozen),
        "v2_fixture_has_actual35_damage_and_remaining75tick_freeze_from_snow_rebound");
    Zombie* rebound = CompositeEnemy(b, "pvzp.spring_tallnut", 1);
    if (!Check(t, rebound != nullptr, "v2_live_rebound_shot_source_fixture")) return false;
    Projectile* incoming = CarnivalPea(b, rebound); if (!incoming) return false;
    incoming->DoImpact(rebound);
    uint32_t shotID = 0;
    for (Projectile* p : b->mProjectiles) if (!p->mDead && p->mProjectileType == PROJECTILE_ZOMBIE_PEA) shotID = b->mProjectiles.DataArrayGetID(p);
    const uint32_t garlicID = b->mZombies.DataArrayGetID(garlic), cherryID = b->mZombies.DataArrayGetID(cherry);
    const uint32_t magnetID = b->mZombies.DataArrayGetID(magnet), vanillaID = b->mZombies.DataArrayGetID(vanilla);
    const uint32_t frozenID = b->mPlants.DataArrayGetID(frozen), targetID = b->mPlants.DataArrayGetID(cherryTarget);
    const auto state = PvzpCustomZombies::Save(b); const CarnivalLayout layout(state);
    if (!Check(t, layout.ok && CarnivalGet(state, 0) == 2 && layout.actors.contains(garlicID) && layout.actors.contains(cherryID) &&
        layout.actors.contains(magnetID) && CarnivalGet(state, layout.frozen) == 1 && CarnivalGet(state, layout.frozen + 8) == 75 &&
        CarnivalGet(state, layout.shots) == 1 && CarnivalGet(state, layout.haste) >= 2 && shotID != 0,
        "v2_fixture_contains_pending_dash_fuse_held_sun_haste_freeze75_and_live_rebound_shot")) return false;
    const size_t garlicAt = layout.actors.at(garlicID), cherryAt = layout.actors.at(cherryID), magnetAt = layout.actors.at(magnetID);
    Check(t, CarnivalGet(state, garlicAt + 12) == 80 && CarnivalGet(state, garlicAt + 16) == 3 &&
        CarnivalGet(state, cherryAt + 12) == 100 && CarnivalGet(state, cherryAt + 16) == UINT32_MAX &&
        CarnivalGet(state, magnetAt + 20) == 75 && PvzpCustomZombies::Int(vanilla, "speedPercent", 100) == 150,
        "v2_encoded_pending_fields_match_real80dash100fuse_adjacent_target75held_and_haste");
    Check(t, PvzpCustomZombies::Load(b, state) && PvzpCustomZombies::Load(b, state) && PvzpCustomZombies::Save(b) == state,
        "v2_repeated_roundtrip_is_exact_and_does_not_retrigger_source_abilities");
    const int bank = b->mSunMoney, coins = Suns(b), hp = frozen->mPlantHealth;
    auto rejected = [&](const std::vector<uint8_t>& bad, const char* label) {
        Check(t, !PvzpCustomZombies::Load(b, bad) && PvzpCustomZombies::Save(b) == state &&
            b->mSunMoney == bank && Suns(b) == coins && frozen->mPlantHealth == hp &&
            PvzpCustomZombies::PlantFrozen(frozen) && PvzpCustomZombies::Int(magnet, "sunDrop", 0) == 175 &&
            PvzpCustomZombies::Int(vanilla, "speedPercent", 100) == 150, label);
    };
    auto bad = state; bad.pop_back(); rejected(bad, "v2_truncation_rejected_atomically_across_all_four_sidecar_maps");
    bad = state; bad.push_back(0); rejected(bad, "v2_trailing_byte_rejected_without_any_live_state_mutation");
    bad = state; CarnivalSet(bad, 0, 3); rejected(bad, "v2_unknown_future_version_rejected_atomically");
    bad = state; CarnivalSet(bad, garlicAt + 12, 81); rejected(bad, "v2_garlic_phase_above80_rejected_atomically");
    bad = state; CarnivalSet(bad, cherryAt + 12, 101); rejected(bad, "v2_cherry_fuse_above100_rejected_atomically");
    bad = state; CarnivalSet(bad, garlicAt + 16, 0); rejected(bad, "v2_garlic_nonadjacent_target_row_rejected_atomically");
    bad = state; CarnivalSet(bad, garlicAt + 16, UINT32_MAX); rejected(bad, "v2_pending_garlic_missing_target_row_rejected_atomically");
    bad = state; CarnivalSet(bad, garlicAt + 12, 0); rejected(bad, "v2_unarmed_garlic_cannot_keep_pending_target_row");
    bad = state; CarnivalSet(bad, cherryAt + 16, 1); rejected(bad, "v2_cherry_cannot_borrow_garlic_pending_target_row");
    bad = state; CarnivalSet(bad, magnetAt + 12, 1); rejected(bad, "v2_nonwindup_actor_cannot_have_pending_phase");
    bad = state; CarnivalSet(bad, magnetAt + 20, 301); rejected(bad, "v2_held_sun_above300_rejected_atomically");
    bad = state; CarnivalSet(bad, magnetAt + 20, 74); rejected(bad, "v2_held_sun_must_be_real_five_sun_units");
    bad = state; CarnivalSet(bad, cherryAt + 20, 25); rejected(bad, "v2_nonmagnet_actor_cannot_claim_stored_sun");
    bad = state; CarnivalSet(bad, layout.frozen + 8, 0); rejected(bad, "v2_zero_frozen_plant_duration_rejected_atomically");
    bad = state; CarnivalSet(bad, layout.shots + 8, 1001); rejected(bad, "v2_oversized_rebound_projectile_damage_rejected_atomically");
    bad = state; CarnivalSet(bad, layout.haste + 4, UINT32_MAX); rejected(bad, "v2_stale_haste_zombie_id_rejected_atomically");
    bad = state; CarnivalSet(bad, layout.haste + 8, 0); rejected(bad, "v2_zero_haste_duration_rejected_atomically");
    bad = state; CarnivalSet(bad, layout.haste + 8, 251); rejected(bad, "v2_haste_duration_above250_rejected_atomically");
    bad = state; CarnivalSet(bad, layout.haste + 12, 0); rejected(bad, "v2_zero_haste_percent_rejected_atomically");
    bad = state; CarnivalSet(bad, layout.haste + 12, 101); rejected(bad, "v2_haste_percent_above100_rejected_atomically");
    bad = state;
    const std::vector<uint8_t> duplicate(state.begin() + layout.haste + 4, state.begin() + layout.haste + 16);
    bad.insert(bad.end(), duplicate.begin(), duplicate.end()); CarnivalSet(bad, layout.haste, CarnivalGet(state, layout.haste) + 1);
    rejected(bad, "v2_duplicate_haste_record_rejected_without_replacing_live_maps");
    const char* home = std::getenv("HOME");
    if (!Check(t, home && *home, "v2_portable_midwindup_disposable_home_fixture")) return false;
    const std::string path = std::string(home) + "/pvzp-carnival-v2-regression.dat";
    if (!Check(t, LawnSaveGame(b, path), "v2_full_portable_save_mid_dash_fuse_held_sun_haste_freeze_and_reflection")) return false;
    PvzpCustomZombies::Reset(b);
    if (!Check(t, b->LoadGame(path), "v2_full_portable_load_at_end_of_board_update")) return false;
    garlic = b->mZombies.DataArrayTryToGet(garlicID); cherry = b->mZombies.DataArrayTryToGet(cherryID);
    magnet = b->mZombies.DataArrayTryToGet(magnetID); vanilla = b->mZombies.DataArrayTryToGet(vanillaID);
    frozen = b->mPlants.DataArrayTryToGet(frozenID); cherryTarget = b->mPlants.DataArrayTryToGet(targetID);
    Projectile* pending = b->mProjectiles.DataArrayTryToGet(shotID);
    if (!Check(t, garlic && cherry && magnet && vanilla && frozen && cherryTarget && pending && !pending->mDead &&
        PvzpCustomZombies::Save(b) == state && PvzpCustomZombies::PlantFrozen(frozen) && frozen->mPlantHealth == hp &&
        PvzpCustomZombies::Int(magnet, "sunDrop", 0) == 175 && PvzpCustomZombies::Int(vanilla, "speedPercent", 100) == 150,
        "v2_full_portable_roundtrip_preserves_real_pending_phases_held_bounty_haste_and_live_shot_exactly")) return false;
    garlic->mIceTrapCounter = 0;
    const float beforeX = garlic->mPosX;
    CompositeTicks(b, 74); frozen->mLaunchCounter = 123; frozen->Update();
    Check(t, garlic->mRow == 4 && PvzpCustomZombies::PlantFrozen(frozen) && frozen->mLaunchCounter == 123,
        "v2_loaded75tick_freeze_blocks_real_plant_after74_updates_and_dash_still_waits");
    CompositeTicks(b, 1); frozen->Update();
    Check(t, !PvzpCustomZombies::PlantFrozen(frozen) && frozen->mLaunchCounter < 123,
        "v2_loaded_freeze_expires_exactly75_and_real_plant_timer_resumes");
    CompositeTicks(b, 5);
    Check(t, garlic->mRow == 3 && garlic->mPosX == beforeX && CarnivalPhase(b, garlic) == 0,
        "v2_portable_loaded_dash_finishes_after_exact_remaining80_without_reset_or_x_teleport");
    const int cherryHP = cherryTarget->mPlantHealth;
    cherry->mButteredCounter = 0; CompositeTicks(b, 99);
    Check(t, !cherry->mDead && cherryTarget->mPlantHealth == cherryHP, "v2_portable_pending_cherry_fuse_does_not_restart_or_finish_early");
    CompositeTicks(b, 1);
    Check(t, cherry->mDead && cherryTarget->mPlantHealth == cherryHP - 900,
        "v2_portable_loaded_cherry_fuse_resolves_exactly_once_after100_unpaused_ticks");
    return true;
}

bool CompositeProjectileAndSave(CustomTest& t, Board* b) {
    for (const char* id : {"pvzp.firepea_head", "pvzp.melon_head", "pvzp.gatling_head", "pvzp.snowpea_head"}) {
        ClearFixture(b);
        const auto* c = CompositeConfig(id);
        Zombie* shooter = CompositeEnemy(b, id);
        Plant* target = b->AddPlant(2, 2, SEED_PEASHOOTER);
        Plant* neighbor = b->AddPlant(3, 2, SEED_WALLNUT);
        Plant* other = b->AddPlant(2, 3, SEED_WALLNUT);
        if (!Check(t, c && shooter && target && neighbor && other, "composite_shooter_real_plant_targets_fixture")) return false;
        for (Plant* p : {target, neighbor, other}) p->mPlantHealth = p->mPlantMaxHealth = 10000;
        CompositeTicks(b, c->interval - 1);
        Check(t, Projectiles(b) == 0, "composite_shooter_no_early_projectile_before_configured_interval");
        CompositeTicks(b, 1);
        Projectile* shot = nullptr;
        for (Projectile* p : b->mProjectiles) if (!p->mDead) shot = p;
        if (!Check(t, Projectiles(b) == 1 && shot && shot->mProjectileType == PROJECTILE_ZOMBIE_PEA &&
            shot->mMotionType == MOTION_BACKWARDS, "composite_shooter_exact_interval_emits_one_real_hostile_backwards_projectile")) return false;
        const auto inFlight = PvzpCustomZombies::Save(b);
        Check(t, PvzpCustomZombies::Load(b, inFlight) && PvzpCustomZombies::Load(b, inFlight) &&
            PvzpCustomZombies::Save(b) == inFlight, "composite_actor_and_inflight_projectile_sidecars_repeat_load_exactly_without_firing_again");
        const Rect rect = target->GetPlantRect();
        shot->mPosX = float(rect.mX + rect.mWidth / 2); shot->mPosY = float(rect.mY + 10); shot->mPosZ = 0;
        shot->mX = int(shot->mPosX); shot->mY = int(shot->mPosY);
        if (!Check(t, shot->FindCollisionTargetPlant() == target, "composite_shot_overlaps_intended_real_plant_collision_rect")) return false;
        shot->CheckForCollision();
        const bool ice = c->ability == "shoot_ice", melon = c->ability == "shoot_melon";
        Check(t, shot->mDead && target->mPlantHealth == 10000 - c->magnitude &&
            neighbor->mPlantHealth == 10000 - (melon ? c->magnitude / 2 : 0) && other->mPlantHealth == 10000 &&
            PvzpCustomZombies::PlantFrozen(target) == ice,
            "composite_actual_projectile_collision_applies_configured_damage_melon_neighbor_splash_or_ice_freeze_only");
        if (!ice) continue;

        // Keep a live second shooter/projectile plus a partly elapsed frozen
        // plant in the same portable save, exercising all three sidecar maps.
        Zombie* gatling = CompositeEnemy(b, "pvzp.gatling_head", 3);
        const auto* gatlingConfig = CompositeConfig("pvzp.gatling_head");
        if (!Check(t, gatling && gatlingConfig, "composite_portable_second_shooter_fixture")) return false;
        CompositeTicks(b, gatlingConfig->interval); // ice freeze:150 ->75
        const uint32_t targetID = b->mPlants.DataArrayGetID(target), shooterID = b->mZombies.DataArrayGetID(shooter);
        const uint32_t gatlingID = b->mZombies.DataArrayGetID(gatling);
        uint32_t shotID = 0;
        for (Projectile* p : b->mProjectiles) if (!p->mDead) shotID = b->mProjectiles.DataArrayGetID(p);
        target->mLaunchCounter = 123;
        const int hp = target->mPlantHealth;
        target->Update();
        Check(t, PvzpCustomZombies::PlantFrozen(target) && target->mLaunchCounter == 123 &&
            target->mPlantHealth == hp && Projectiles(b) == 1,
            "real_frozen_plant_update_suppresses_ability_timer_while_inflight_second_shot_exists");
        const auto state = PvzpCustomZombies::Save(b);
        Check(t, CarnivalGet(state, 0) == 1, "legacy_composite_without_extended_state_keeps_original_v1_payload_layout");
        PvzpCustomZombies::Reset(b);
        Check(t, !PvzpCustomZombies::PlantFrozen(target) && PvzpCustomZombies::Id(shooter).empty(),
            "composite_reset_removes_actor_and_frozen_maps_without_deleting_engine_entities");
        if (!Check(t, PvzpCustomZombies::Load(b, state) && PvzpCustomZombies::PlantFrozen(target) &&
            PvzpCustomZombies::Id(shooter) == "pvzp.snowpea_head" && PvzpCustomZombies::Save(b) == state,
            "composite_sidecar_restores_actor_frozen_and_pending_projectile_maps_atomically")) return false;
        auto set32 = [](std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
            if (at + 4 > bytes.size()) return;
            for (int i = 0; i < 4; ++i) bytes[at + i] = uint8_t(value >> (i * 8));
        };
        auto get32 = [&](size_t at) {
            uint32_t value = 0;
            if (at + 4 <= state.size()) for (int i = 0; i < 4; ++i) value |= uint32_t(state[at + i]) << (i * 8);
            return value;
        };
        size_t frozenAt = 8;
        for (uint32_t i = 0; i < get32(4); ++i) frozenAt += 4 + 4 + get32(frozenAt + 4) + 12;
        const size_t shotAt = frozenAt + 4 + size_t(get32(frozenAt)) * 8;
        if (!Check(t, get32(4) == 2 && get32(frozenAt) == 1 && get32(shotAt) == 1 &&
            shotAt + 20 <= state.size(), "composite_saved_fixture_contains_two_actors_one_frozen_plant_one_live_shot")) return false;
        auto rejected = [&](const std::vector<uint8_t>& bytes, const char* label) {
            Check(t, !PvzpCustomZombies::Load(b, bytes) && PvzpCustomZombies::Save(b) == state &&
                PvzpCustomZombies::PlantFrozen(target) && target->mPlantHealth == hp &&
                PvzpCustomZombies::Id(shooter) == "pvzp.snowpea_head", label);
        };
        auto bad = state; bad.pop_back(); rejected(bad, "composite_truncated_save_rejected_without_actor_frozen_or_shot_mutation");
        bad = state; bad.push_back(0); rejected(bad, "composite_trailing_save_byte_rejected_without_mutation");
        bad = state; set32(bad, 0, 3); rejected(bad, "composite_unknown_save_version_rejected_without_mutation");
        bad = state; set32(bad, 8, 0xFFFFFFFFU); rejected(bad, "composite_stale_actor_id_rejected_without_mutation");
        bad = state; set32(bad, frozenAt + 4, 0xFFFFFFFFU); rejected(bad, "composite_stale_frozen_plant_id_rejected_without_mutation");
        bad = state; set32(bad, frozenAt + 8, 0); rejected(bad, "composite_zero_frozen_duration_rejected_without_mutation");
        bad = state; set32(bad, frozenAt + 8, 60001); rejected(bad, "composite_oversized_frozen_duration_rejected_without_mutation");
        bad = state; set32(bad, shotAt + 8, 0); rejected(bad, "composite_zero_projectile_damage_rejected_without_mutation");
        bad = state; set32(bad, shotAt + 12, 60001); rejected(bad, "composite_oversized_projectile_freeze_rejected_without_mutation");
        bad = state;
        const std::vector<uint8_t> duplicate(state.begin() + frozenAt + 4, state.begin() + frozenAt + 12);
        bad.insert(bad.begin() + frozenAt + 12, duplicate.begin(), duplicate.end()); set32(bad, frozenAt, 2);
        rejected(bad, "composite_duplicate_frozen_plant_record_rejected_without_mutation");
        const char* home = std::getenv("HOME");
        if (!Check(t, home && *home, "composite_portable_disposable_home")) return false;
        const std::string path = std::string(home) + "/pvzp-composite-regression.dat";
        if (!Check(t, LawnSaveGame(b, path), "composite_full_portable_save_actor_frozen_map_and_inflight_projectile")) return false;
        PvzpCustomZombies::Reset(b);
        if (!Check(t, b->LoadGame(path), "composite_full_portable_load_at_end_of_board_update")) return false;
        target = b->mPlants.DataArrayTryToGet(targetID); shooter = b->mZombies.DataArrayTryToGet(shooterID);
        gatling = b->mZombies.DataArrayTryToGet(gatlingID); Projectile* pending = b->mProjectiles.DataArrayTryToGet(shotID);
        if (!Check(t, target && shooter && gatling && pending && !pending->mDead && target->mPlantHealth == hp &&
            PvzpCustomZombies::PlantFrozen(target) && PvzpCustomZombies::Id(shooter) == "pvzp.snowpea_head" &&
            PvzpCustomZombies::Id(gatling) == "pvzp.gatling_head" && PvzpCustomZombies::Save(b) == state,
            "portable_roundtrip_preserves_real_variant_ids_cooldowns_frozen_plant_and_pending_shot_exactly")) return false;
        CompositeTicks(b, 74);
        target->Update();
        Check(t, PvzpCustomZombies::PlantFrozen(target) && target->mLaunchCounter == 123,
            "portable_restored75tick_freeze_still_blocks_actual_plant_after74_ticks");
        CompositeTicks(b, 1);
        target->Update();
        Check(t, !PvzpCustomZombies::PlantFrozen(target) && target->mLaunchCounter < 123,
            "portable_restored_freeze_expires_on75th_tick_and_real_plant_ability_timer_resumes");
        t.postLoadPlant = targetID; t.postLoadShooter = shooterID; t.postLoadGatling = gatlingID;
        t.postLoadHealth = target->mPlantHealth; t.postLoadMainCounter = b->mMainCounter;
        t.postLoadShots = ProjectileIDs(b); t.postLoadShotX.clear();
        for (uint32_t id : t.postLoadShots) t.postLoadShotX.push_back(b->mProjectiles.DataArrayTryToGet(id)->mPosX);
    }
    return true;
}
bool LocaleAndAlmanacChecks(CustomTest& t, Board* b) {
    if (!Check(t, PvzpFontFallbackAvailable(), "licensed_cjk_fallback_font_available")) return false;
    const std::string originalLanguage(PvzpLocaleGetLanguage());
    PvzpLocaleSetLanguage("en");
    const std::string english(PvzpStringTranslate("[ALMANAC_MOD_PLANTS]"));
    const auto generation = PvzpLocaleGetGeneration();
    if (!Check(t, PvzpLocaleSetLanguage("zh-CN") && PvzpLocaleIsChinese() &&
        PvzpLocaleGetGeneration() > generation, "locale_zh_switch_updates_generation")) return false;
    std::string saved;
    b->mApp->RegistryReadString("Language", &saved);
    const std::string chinese(PvzpStringTranslate("[ALMANAC_MOD_PLANTS]"));
    Check(t, saved == "zh-CN" && chinese != english, "locale_zh_registry_persistence_and_almanac_label");
    // Construct in Chinese, then exercise the actual settings control handler.
    // No claim of complete fan translation: overlays are provisional project text.
    {
        NewOptionsDialog options(b->mApp, true);
        const auto copied = options.mLanguageButton->mLabel;
        const int control = options.mLanguageButton->mId;
        options.ButtonDepress(control);
        b->mApp->RegistryReadString("Language", &saved);
        Check(t, !PvzpLocaleIsChinese() && saved == "en" && options.mLanguageButton->mLabel != copied &&
            PvzpLocaleTranslateLiteral(chinese) == english,
            "actual_options_language_handler_restores_en_and_chinese_created_labels_roundtrip");
    }
    PvzpLocaleRegisterLiteral("CTEST literal", "回归测试文字");
    Check(t, PvzpLocaleSetLanguage("zh-CN") && PvzpLocaleTranslateLiteral("CTEST literal") == "回归测试文字" &&
        PvzpLocaleSetLanguage("en") && PvzpLocaleTranslateLiteral("回归测试文字") == "CTEST literal",
        "registered_literal_english_chinese_english_roundtrip");
    auto* almanac = b->mApp->DoAlmanacDialog(SEED_PEASHOOTER, ZOMBIE_INVALID);
    if (!Check(t, almanac != nullptr, "real_almanac_dialog_created")) return false;
    auto click = [&](int x, int y) {
        const auto pos = almanac->GetAbsPos();
        auto* manager = b->mApp->mWidgetManager.get();
        manager->mLastMouseX = x + pos.mX; manager->mLastMouseY = y + pos.mY; manager->mMouseIn = true;
        almanac->MouseDown(x, y, 1); almanac->MouseUp(x, y, 1);
    };
    auto* selector = almanac->mPlantPageButton.get();
    click(selector->mX + selector->mWidth / 2, selector->mY + selector->mHeight / 2);
    const auto seed = static_cast<SeedType>(int(SEED_CUSTOM_FIRST) + 4);
    int x = 0, y = 0; almanac->GetSeedPosition(seed, x, y);
    click(x + SEED_PACKET_WIDTH / 2, y + SEED_PACKET_HEIGHT / 2);
    const auto visible = almanac->GetPlantSeeds();
    Check(t, almanac->mShowModPlants && almanac->mSelectedSeed == seed && almanac->mPlant &&
        almanac->mPlant->mSeedType == seed && std::find(visible.begin(), visible.end(), seed) != visible.end() &&
        std::all_of(visible.begin(), visible.end(), [](SeedType s) { return PvzpCustomPlants::IsSlotUsed(s); }),
        "real_almanac_selector_and_custom_packet_click_select_enabled_prism_properties_preview");
    const auto selected = almanac->mSelectedSeed;
    almanac->ShowPlant(SEED_ZOMBIE_NORMAL);
    Check(t, almanac->mSelectedSeed == selected, "almanac_rejects_zombie_pseudo_seed_as_plant_entry");
    b->mApp->KillAlmanacDialog();
    PvzpLocaleSetLanguage(originalLanguage);
    b->Pause(false);
    return true;
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
        if (t.phaseTicks == 200) {
            if (!Check(t, own->mChilledCounter == 0 && other->mChilledCounter == 0,
                "aurora_no_early_pulse_before_portable_midinterval_save") ||
                !PortableRoundtrip(t, b)) { Finish(t, b); return; }
            return; // Every pointer above became stale during the import.
        }
        if (t.phaseTicks == 399) {
            Check(t, own->mChilledCounter == 0 && other->mChilledCounter == 0 &&
                own->mBodyHealth == 10000 && other->mBodyHealth == 10000 && own->mPosX == 650,
                "aurora_399_real_updates_no_early_chill_damage_or_push_after_save200_load");
            // Test a FRESH pulse freeze without asking production code to shorten
            // this fixture's artificial 100000-tick stationary freeze. One short
            // butter hold prevents a walking displacement before the next pulse.
            own->mIceTrapCounter = 0;
            own->mButteredCounter = 2;
        }
        if (t.phaseTicks < 400) return;
        Check(t, own->mChilledCounter > 0 && other->mChilledCounter == 0,
            "aurora_400th_real_board_update_chills_own_row_only_after_load",
            own->mChilledCounter, other->mChilledCounter);
        Check(t, own->mBodyHealth == 9700 && other->mBodyHealth == 10000,
            "aurora400_pulse_deals300_actual_body_damage_only_in_own_row", own->mBodyHealth, other->mBodyHealth);
        Check(t, own->mPosX == 680 && own->mX == 680 && other->mPosX == 650,
            "aurora400_pulse_pushes_own_row30_with_synced_position", own->mX, other->mX);
        Check(t, own->mIceTrapCounter > 0 && own->mIceTrapCounter <= 120 && other->mIceTrapCounter > 120,
            "aurora400_pulse_applies_bounded120_freeze_only_to_own_row", own->mIceTrapCounter, other->mIceTrapCounter);
        const int ownBeforeFood = own->mBodyHealth, otherBeforeFood = other->mBodyHealth;
        const float ownXBeforeFood = own->mPosX, otherXBeforeFood = other->mPosX;
        const int money = b->mSunMoney;
        b->mPlantFoodCount = 3;
        BoardClick(b, b->GetPlantFoodRect());
        const bool selected = b->mPlantFoodSelecting;
        BoardClick(b, aurora->mX + 40, aurora->mY + 40);
        bool allChilled = true;
        for (Zombie* z : b->mZombies)
            if (!z->mDead && !z->mMindControlled) allChilled = allChilled && z->mChilledCounter > 0;
        Check(t, selected && b->mPlantFoodCount == 2 && !b->mPlantFoodSelecting && allChilled &&
            aurora->mPlantHealth == 8000 && aurora->mPlantHealth == aurora->mPlantMaxHealth && b->mSunMoney == money + 250,
            "aurora_real_food_all_row_chill_full8000_heal_250_sun_consumes_one",
            aurora->mPlantHealth, b->mSunMoney - money);
        Check(t, !own->mDead && own->mBodyHealth == ownBeforeFood - 1800 && other->mBodyHealth == otherBeforeFood,
            "aurora_food_deals1800_actual_own_row_damage_without_killing10000hp_target", own->mBodyHealth, other->mBodyHealth);
        Check(t, own->mPosX == ownXBeforeFood + 160 && own->mX == int(own->mPosX) && other->mPosX == otherXBeforeFood,
            "aurora_food_pushes_own_row160_and_preserves_other_row_position", own->mX, other->mX);
        Check(t, own->mIceTrapCounter > 0 && own->mIceTrapCounter <= 120,
            "aurora_food_reapplies_real_bounded_row_freeze", own->mIceTrapCounter);
        t.phase = 5;
        t.phaseTicks = 0;
        return;
    }
    if (t.phase == 5) {
        b->mZombieCountDown = b->mZombieCountDownStart = 100000;
        b->mHugeWaveCountDown = -1;
        if (!NutArtChecks(t, b) || !PrismPacketChecks(t, b) || !PrismSplitChecks(t, b) ||
            !PrismEmpowerAndSave(t, b) || !LocaleAndAlmanacChecks(t, b) ||
            !CompositeAbilities(t, b) || !CarnivalGuardianAndRally(t, b) || !CarnivalWindups(t, b) ||
            !CarnivalMagnetAndRebound(t, b) || !CarnivalSaveV2(t, b) || !CompositeProjectileAndSave(t, b)) { Finish(t, b); return; }
        t.phase = 6;
        t.phaseTicks = 0;
        return;
    }
    if (t.phase == 6) {
        b->mZombieCountDown = b->mZombieCountDownStart = 100000;
        b->mHugeWaveCountDown = -1;
        // Do not quit in the same frame as portable import: effects, entity
        // iterators, sidecars and restored particle emitters must survive real
        // subsequent Board updates (not synchronous unit-update loops).
        if (t.phaseTicks < 20) return;
        Plant* target = b->mPlants.DataArrayTryToGet(t.postLoadPlant);
        Zombie* shooter = b->mZombies.DataArrayTryToGet(t.postLoadShooter);
        Zombie* gatling = b->mZombies.DataArrayTryToGet(t.postLoadGatling);
        Check(t, b->mMainCounter >= t.postLoadMainCounter + 20 && target && !target->mDead &&
            shooter && !shooter->mDead && gatling && !gatling->mDead &&
            target->mPlantHealth == t.postLoadHealth && !PvzpCustomZombies::PlantFrozen(target) &&
            PvzpCustomZombies::Id(shooter) == "pvzp.snowpea_head" && PvzpCustomZombies::Id(gatling) == "pvzp.gatling_head",
            "portable_postload20_natural_board_updates_preserve_live_entity_ids_hp_and_variant_sidecars");
        bool advanced = t.postLoadShots.size() == 2 && t.postLoadShots.size() == t.postLoadShotX.size() &&
            Projectiles(b) == int(t.postLoadShots.size());
        for (size_t i = 0; i < t.postLoadShots.size(); ++i) {
            Projectile* p = b->mProjectiles.DataArrayTryToGet(t.postLoadShots[i]);
            advanced = advanced && p && !p->mDead && p->mMotionType == MOTION_BACKWARDS &&
                p->mPosX < t.postLoadShotX[i] - 60 && p->mPosX > t.postLoadShotX[i] - 75;
        }
        Check(t, advanced, "portable_postload20_natural_updates_advance_each_restored_hostile_projectile_without_duplicates_or_early_hits");
        Finish(t, b);
    }
#else
    (void)b;
#endif
}
