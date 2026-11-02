// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpPlantFood.h"
#include "PvzpCustomPlants.h"
#include "../Lawn/Board.h"
#include "../Lawn/Projectile.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/Coin.h"
#include "../Lawn/GridItem.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "Reanimator.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace PvzpPlantFood
{
namespace
{
// Deliberately explicit: a balance pass is not a blanket percentage discount.
// cost / recharge ticks / launch ticks / base health / skill lifetime ticks.
constexpr SkillInfo kSkills[] = {
 {SEED_PEASHOOTER, "Pea parade", "360-degree ring of peas every 0.4 s for six seconds.", 75,650,140,300,600},
 {SEED_SUNFLOWER, "Pocket sunrise", "Four waves of two collectible suns.", 50,750,2200,300,301},
 {SEED_CHERRYBOMB, "Cherry festival", "1800 damage in a visible, enlarged five-row blast; consumed.", 150,4500,0,300,30},
 {SEED_WALLNUT, "Hard candy shell", "Full repair plus at most 2000 temporary shield health for nine seconds.", 50,2600,0,4200,900},
 {SEED_POTATOMINE, "Express spud", "Arm immediately and detonate a 900-damage warning burst nearby.", 25,2400,0,300,30},
 {SEED_SNOWPEA, "Snowball fight", "Snow peas plus a wide chill pulse every 0.45 s.", 150,750,140,300,600},
 {SEED_CHOMPER, "Second helping", "Finish digestion, bite now, then digest rapidly for six seconds.", 125,750,0,400,600},
 {SEED_REPEATER, "Pea drumroll", "Sustained double-barrel fire for six seconds.", 175,750,140,300,600},
 {SEED_PUFFSHROOM, "Puff party", "Short-range paired puffs plus a four-second spore volley.", 0,850,145,200,400},
 {SEED_SUNSHROOM, "Mushroom dawn", "Mature immediately and release five waves of collectible sun.", 25,750,2300,250,401},
 {SEED_FUMESHROOM, "Steam express", "Ten extra piercing fumes using the original row attack.", 75,750,140,350,301},
 {SEED_GRAVEBUSTER, "Grave rebate", "Clear nearby graves and craters; each cleared tile drops sun (maximum five).", 50,650,0,350,30},
 {SEED_HYPNOSHROOM, "Dance partners", "Hypnotize up to three nearby ordinary ground zombies; consumed.", 75,2400,0,300,30},
 {SEED_SCAREDYSHROOM, "Brave little spore", "Four seconds of aimed spore volleys, even while frightened.", 25,750,140,250,400},
 {SEED_ICESHROOM, "Deep winter", "Repeated freezes, 250 cold damage each; enough to kill a normal zombie.", 75,4500,0,300,301},
 {SEED_DOOMSHROOM, "Friendly mushroom cloud", "Full-lawn 3600 damage; craters a 3x3 area as the price.", 125,5000,0,300,30},
 {SEED_LILYPAD, "Floating first aid", "Repair the same tile and adjacent water supports; grant bounded shields.", 25,650,0,400,600},
 {SEED_SQUASH, "Double stomp", "Two visible 900-damage stomps around the closest target; consumed.", 50,2500,0,300,30},
 {SEED_THREEPEATER, "Three-lane carnival", "Six seconds of five-lane volleys (rows +-2).", 300,750,145,300,600},
 {SEED_TANGLEKELP, "Pool whirlpool", "Drag under up to three nearby ordinary pool zombies; consumed.", 25,2200,0,300,30},
 {SEED_JALAPENO, "Three-alarm chili", "Burn three rows with the original fire-row effect and damage; consumed.", 125,4500,0,300,30},
 {SEED_SPIKEWEED, "Tap dance", "Eight visible local spike pulses over four seconds.", 100,700,0,300,351},
 {SEED_TORCHWOOD, "Campfire rally", "Six seconds of nearby shooter reload assistance and a one-time close flame burst.", 150,750,0,400,600},
 {SEED_TALLNUT, "Fortress confection", "Full repair plus at most 4000 temporary shield health for nine seconds.", 125,2800,0,8500,900},
 {SEED_SEASHROOM, "Tidal spores", "Paired short-range spores and four seconds of water-lane volleys.", 0,1800,145,250,400},
 {SEED_PLANTERN, "Beacon of recovery", "Six visible repair pulses in the surrounding nine tiles, no passive damage aura.", 25,1400,0,400,501},
 {SEED_CACTUS, "Needle storm", "Six seconds of three-lane needle fire.", 100,750,140,350,600},
 {SEED_BLOVER, "Gale force", "Blow away fliers and chill ground enemies across the lawn; consumed.", 75,900,0,300,30},
 {SEED_SPLITPEA, "Backstage encore", "Six seconds of paired front and rear shots.", 125,750,140,350,600},
 {SEED_STARFRUIT, "Meteor shower", "Eight extra five-direction star volleys over four seconds.", 125,750,140,300,351},
 {SEED_PUMPKINSHELL, "Pumpkin carriage", "Repair the shell and protect every plant on its tile with a bounded shield.", 125,2600,0,4200,900},
 {SEED_MAGNETSHROOM, "Metal detector", "Strip up to five eligible metal objects with the original magnet API.", 100,750,0,300,30},
 {SEED_CABBAGEPULT, "Salad catapult", "Six seconds of paired cabbages with fully preserved lob physics.", 100,750,270,300,600},
 {SEED_FLOWERPOT, "Terracotta workshop", "Repair and shield plants in the surrounding nine tiles, especially rooftop supports.", 25,650,0,400,600},
 {SEED_KERNELPULT, "Butter banquet", "Six seconds of extra butter lobs; real ballistics and original butter stun.", 100,750,280,300,600},
 {SEED_INSTANT_COFFEE, "Coffee break", "Wake surrounding mushrooms, repair them and advance their next attack; consumed.", 50,650,0,300,30},
 {SEED_GARLIC, "Musical lanes", "Repair garlic and redirect up to five nearby walkers to valid adjacent dry lanes.", 50,650,0,500,30},
 {SEED_UMBRELLA, "Picnic shelter", "Trigger umbrella defence and grant six-second shields to the surrounding nine tiles.", 100,750,0,400,600},
 {SEED_MARIGOLD, "Golden bouquet", "Four waves of collectible gold coins, not invisible account credit.", 50,2200,2200,300,301},
 {SEED_MELONPULT, "Melon picnic", "Six seconds of paired splash melons with original lob trajectories.", 275,750,280,350,600},
 {SEED_GATLINGPEA, "Pea fireworks", "Summons three Torchwoods ahead and fires three lanes of fire peas.", 225,4200,140,350,600},
 {SEED_TWINSUNFLOWER, "Twin sunrise", "Five waves of three collectible suns.", 125,4000,2200,350,401},
 {SEED_GLOOMSHROOM, "Purple disco", "Spinning spore storm with a mushroom ring summoned around it.", 150,4200,190,350,351},
 {SEED_CATTAIL, "Nine lives volley", "Six seconds of three homing spikes per shot; each retains its target ID.", 225,4200,140,350,600},
 {SEED_WINTERMELON, "Winter picnic", "Six seconds of paired cold splash melons plus one nearby chill pulse.", 200,4500,280,350,600},
 {SEED_GOLD_MAGNET, "Treasure sweep", "Four rapid original coin-attraction sweeps and two collectible gold coins.", 50,3500,0,350,301},
 {SEED_SPIKEROCK, "Rock concert", "Repair the rock and perform ten heavy local spike pulses over five seconds.", 125,4200,0,600,451},
 {SEED_COBCANNON, "Fresh ammunition", "Reload immediately, then reload once more after the next fired cob; no hidden area damage.", 450,4500,600,400,1800},
 {SEED_IMITATER, "Standing ovation", "Transform immediately and apply the copied plant's own skill.", 0,750,0,300,30},
 {SEED_ELECTRIC_GATLINGPEA, "Electric fan festival", "Activate the board's sustained real electric fan for about five seconds; ordinary four-shot electric chains remain unchanged.", 300,4500,45,350,500}
};
static_assert(sizeof(kSkills) / sizeof(kSkills[0]) == 50);
static_assert(SEED_ELECTRIC_GATLINGPEA == 53);
constexpr bool RegistryValid()
{
    for (std::size_t i=0;i<50;++i)
    {
        const SkillInfo& s=kSkills[i];
        if (static_cast<int>(s.seed)!=(i<49 ? static_cast<int>(i) : 53) ||
            !s.name[0] || !s.description[0] || s.cost<0 || s.recharge<=0 ||
            s.launch<0 || s.health<=0 || s.duration<=0) return false;
    }
    return true;
}
static_assert(RegistryValid(), "Every original plant and E requires an explicit valid skill/balance record");
struct Charge { SeedType seed; int remaining; int elapsed = 0; int extraBudget = 96; bool cobFired = false; };
struct Shield { int remaining; int cap; };
struct State { std::map<uint32_t, Charge> charges; std::map<uint32_t, Shield> shields; std::vector<std::pair<uint32_t,int>> tempPlants; };
std::map<Board*, State> gStates;

Plant* Live(Board* b, uint32_t id)
{
    Plant* p = b->mPlants.DataArrayTryToGet(id);
    return p && !p->mDead && !p->mSquished ? p : nullptr;
}
bool Enemy(Zombie* z)
{
    return !z->mMindControlled && !z->mDead && !z->IsDeadOrDying();
}
void Effect(Plant* p, ParticleEffect effect)
{
    p->mApp->AddPvzpParticle(p->mX + 40, p->mY + 40,
        static_cast<int>(RENDER_LAYER_TOP), effect);
}
void Drop(Plant* p, CoinType kind, int count)
{
    Board* b = p->mBoard;
    for (int i = 0; i < count && b->mCoins.mSize < b->mCoins.mMaxSize; ++i)
        b->AddCoin(p->mX + 15 + i * 15, p->mY + 10, kind, COIN_MOTION_FROM_PLANT);
}
void Repair(Plant* p, int amount)
{
    if (p->mPlantHealth < p->mPlantMaxHealth)
        p->mPlantHealth = std::min(p->mPlantMaxHealth, p->mPlantHealth + amount);
    if (p->mSeedType == SEED_SPIKEROCK)
        if (Reanimation* body=p->mApp->ReanimationTryToGet(p->mBodyReanimID))
        {
            body->AssignRenderGroupToTrack("bigspike3",p->mPlantHealth>300 ? RENDER_GROUP_NORMAL : RENDER_GROUP_HIDDEN);
            body->AssignRenderGroupToTrack("bigspike2",p->mPlantHealth>150 ? RENDER_GROUP_NORMAL : RENDER_GROUP_HIDDEN);
        }
}
void Protect(Plant* p, int amount, int ticks)
{
    // A bounded damage buffer, NOT an ever-doubling maximum-health mutation.
    // Storing it as overhealth also protects against lethal damage within one tick.
    int cap = std::min(amount, p->mPlantMaxHealth);
    auto& shield = gStates[p->mBoard].shields[p->mBoard->mPlants.DataArrayGetID(p)];
    shield = {std::max(shield.remaining,ticks), std::max(shield.cap, cap)};
    p->mPlantHealth = std::max(p->mPlantHealth, p->mPlantMaxHealth + cap);
    p->mPlantHealth = std::min(p->mPlantHealth, p->mPlantMaxHealth + shield.cap);
}
void Neighbours(Plant* source, int repair, int shield, int ticks, bool sameTile = false)
{
    for (Plant* p : source->mBoard->mPlants)
    {
        if (p->mDead || p->mSquished) continue;
        int range = sameTile ? 0 : 1;
        if (std::abs(p->mPlantCol-source->mPlantCol) > range || std::abs(p->mRow-source->mRow) > range) continue;
        Repair(p, repair);
        if (shield) Protect(p, shield, ticks);
        p->mEatenFlashCountdown = 25;
    }
}
void Burst(Plant* p, int x, int y, int radius, int rows, int damage, unsigned flags = 0)
{
    for (Zombie* z : p->mBoard->mZombies)
    {
        if (!Enemy(z) || std::abs(z->mRow-p->mRow) > rows || !z->EffectedByDamage(p->GetDamageRangeFlags())) continue;
        Rect r = z->GetZombieRect();
        float dx = r.mX + r.mWidth/2 - x, dy = r.mY + r.mHeight/2 - y;
        if (dx*dx + dy*dy <= float(radius*radius)) z->TakeDamage(damage, flags);
    }
    p->mApp->AddPvzpParticle(x, y, static_cast<int>(RENDER_LAYER_TOP), PARTICLE_POWIE);
    p->mBoard->ShakeBoard(2, -3);
}
void Chill(Plant* p, int radius)
{
    for (Zombie* z : p->mBoard->mZombies)
        if (Enemy(z) && std::abs(z->mPosX-p->mX) <= radius && std::abs(z->mRow-p->mRow) <= 1)
            z->ApplyChill(false);
    Effect(p, PARTICLE_SNOWPEA_PUFF);
}
void FireLane(Plant* p, int row, PlantWeapon weapon = WEAPON_PRIMARY)
{
    if (row < 0 || row >= (p->mBoard->StageHas6Rows() ? 6 : 5)) return;
    if (p->mBoard->mProjectiles.mSize + 4 >= p->mBoard->mProjectiles.mMaxSize) return;
    Zombie* target = p->FindTargetZombie(row, weapon);
    if (target) p->Fire(target, row, weapon);
}
bool CellOk(Board* b,int col,int row)
{
    return col>=0 && col<MAX_GRID_SIZE_X && row>=0 && row<(b->StageHas6Rows()?6:5);
}
void Summon(Plant* source, SeedType seed, int col, int row, int ticks)
{
    Board* b=source->mBoard;
    if(!CellOk(b,col,row) || b->CanPlantAt(col,row,seed)!=PlantingReason::PLANTING_OK) return;
    Plant* q=b->AddPlant(col,row,seed);
    if(q && ticks>0) gStates[b].tempPlants.push_back({b->mPlants.DataArrayGetID(q),ticks});
}
void RingVolley(Plant* p,int count)
{
    Board* b=p->mBoard;
    int x=p->mX+40,y=p->mY+20;
    for(int i=0;i<count;++i)
    {
        if(b->mProjectiles.mSize+4>=b->mProjectiles.mMaxSize) break;
        float a=DEG_TO_RAD(360.f*i/count);
        Projectile* q=b->AddProjectile(x,y,p->mRenderOrder-1,p->mRow,PROJECTILE_PEA);
        if(!q) continue;
        q->mDamageRangeFlags=p->GetDamageRangeFlags();
        q->mMotionType=ProjectileMotion::MOTION_STAR;
        q->mVelX=std::cos(a)*4.5f; q->mVelY=std::sin(a)*4.5f;
    }
}
void LancetRow(Plant* p,int row,int damage)
{
    if(!CellOk(p->mBoard,0,row)) return;
    p->DoRowAreaDamage(damage,0U);
}
void DoomCraters(Plant* p)
{
    Board* b=p->mBoard;
    for(int dx=-1;dx<=1;++dx) for(int dy=-1;dy<=1;++dy)
    {
        int c=p->mPlantCol+dx,r=p->mRow+dy;
        if(!CellOk(b,c,r) || b->GetCraterAt(c,r)) continue;
        b->AddACrater(c,r);
    }
}
void FreezeAll(Plant* p,int damage)
{
    p->IceZombies();
    for(Zombie* z : p->mBoard->mZombies)
        if(Enemy(z) && z->EffectedByDamage(p->GetDamageRangeFlags())) z->TakeDamage(damage,4U);
}
void ReloadCob(Plant* p)
{
    p->mShootingCounter = 0;
    p->mStateCountdown = 0;
    p->mState = STATE_COBCANNON_READY;
    p->PlayIdleAnim(12.0f);
    Effect(p, PARTICLE_SEED_PACKET_FLASH);
}
// Timed effects always require this live source plant. There is no perpetual
// or sourceless damage aura. Finite pulse counts follow from elapsed/lifetime.
void Pulse(Plant* p, Charge& c)
{
    int t = c.elapsed;
    switch (c.seed)
    {
    case SEED_SUNFLOWER: if (t % 100 == 0) Drop(p, COIN_SUN, 2); break;
    case SEED_SUNSHROOM: if (t % 100 == 0) Drop(p, COIN_SUN, 2); break;
    case SEED_TWINSUNFLOWER: if (t % 100 == 0) Drop(p, COIN_SUN, 3); break;
    case SEED_MARIGOLD: if (t % 100 == 0) Drop(p, COIN_GOLD, 2); break;
    case SEED_GOLD_MAGNET:
        if (t % 100 == 0) { p->GoldMagnetFindTargets(); Effect(p, PARTICLE_SEED_PACKET_FLASH); }
        break;
    case SEED_FUMESHROOM:
        if (t % 33 == 0)
        {
            p->Fire(nullptr, p->mRow);
            p->mApp->AddPvzpParticle(p->mX+85,p->mY+31,p->mRenderOrder+4,PARTICLE_FUMECLOUD);
        }
        break;
    case SEED_GLOOMSHROOM:
        // Spinning spore storm: dense pulses in every direction.
        if (t % 12 == 0)
        {
            p->Fire(nullptr,p->mRow);
            for(int k=0;k<3;++k)
            {
                float a=DEG_TO_RAD(120.f*k + (t*7)%60);
                p->mApp->AddPvzpParticle(p->mX+40+std::cos(a)*50,p->mY+40+std::sin(a)*50,p->mRenderOrder+3,PARTICLE_GLOOMCLOUD);
            }
        }
        break;
    case SEED_GATLINGPEA:
        if (t % 8 == 0)
        {
            for(int row=p->mRow-1;row<=p->mRow+1;++row) FireLane(p,row);
            if(t%24==0) Effect(p,PARTICLE_SEED_PACKET_FLASH);
        }
        break;
    case SEED_ICESHROOM:
        if (t % 60 == 0) { FreezeAll(p,250); Effect(p,PARTICLE_ICE_TRAP); }
        break;
    case SEED_PEASHOOTER:
        if (t % 40 == 0) { RingVolley(p,12); Effect(p,PARTICLE_SEED_PACKET_FLASH); }
        break;
    case SEED_SNOWPEA:
        if (t % 45 == 0) { FireLane(p,p->mRow); Chill(p,520); }
        break;
    case SEED_REPEATER:
        if (t % 25 == 0) FireLane(p,p->mRow);
        break;
    case SEED_SPLITPEA:
        if (t % 35 == 0) { FireLane(p,p->mRow,WEAPON_PRIMARY); FireLane(p,p->mRow,WEAPON_SECONDARY); }
        break;
    case SEED_STARFRUIT:
        if (t % 50 == 0 && p->mBoard->mProjectiles.mSize + 5 < p->mBoard->mProjectiles.mMaxSize)
            { p->StarFruitFire(); Effect(p, PARTICLE_SEED_PACKET_FLASH); }
        break;
    case SEED_PUFFSHROOM: case SEED_SEASHROOM: case SEED_SCAREDYSHROOM:
        if (t % 45 == 0) { FireLane(p, p->mRow); Effect(p,PARTICLE_PUFFSHROOM_MUZZLE); }
        break;
    case SEED_CACTUS:
        if (t % 60 == 0)
        {
            for(int row=p->mRow-1;row<=p->mRow+1;++row)
            {
                FireLane(p,row,WEAPON_PRIMARY);
                if(row==p->mRow) FireLane(p,row,WEAPON_SECONDARY);
            }
        }
        break;
    case SEED_SPIKEWEED: case SEED_SPIKEROCK:
        if (t % 50 == 0)
        {
            p->DoRowAreaDamage(c.seed == SEED_SPIKEROCK ? 100 : 60, 33U);
            p->SpikeweedAttack(); Effect(p, PARTICLE_POTATO_MINE);
        }
        break;
    case SEED_PLANTERN:
        if (t % 100 == 0) { Neighbours(p,100,0,0); Effect(p,PARTICLE_LANTERN_SHINE); }
        break;
    case SEED_TORCHWOOD:
        if (t % 60 == 0)
        {
            for (Plant* q : p->mBoard->mPlants)
                if (!q->mDead && !q->mSquished && q->mSubclass == SUBCLASS_SHOOTER &&
                    std::abs(q->mPlantCol-p->mPlantCol)<=1 && std::abs(q->mRow-p->mRow)<=1)
                    q->mLaunchCounter = std::min(q->mLaunchCounter,30);
            Effect(p,PARTICLE_SEED_PACKET_FLASH);
        }
        break;
    case SEED_CHOMPER:
        if (p->mState == STATE_CHOMPER_DIGESTING) p->mStateCountdown = std::min(p->mStateCountdown,20);
        break;
    case SEED_COBCANNON:
        if (p->mState == STATE_COBCANNON_FIRING) c.cobFired = true;
        if (c.cobFired && p->mState == STATE_COBCANNON_ARMING) { ReloadCob(p); c.remaining = 1; }
        break;
    default: break;
    }
}
}

std::size_t SkillCount() { return sizeof(kSkills)/sizeof(kSkills[0]); }
const SkillInfo* SkillAt(std::size_t i) { return i < SkillCount() ? &kSkills[i] : nullptr; }
const SkillInfo* Query(SeedType seed)
{
    for (const SkillInfo& s : kSkills) if (s.seed == seed) return &s;
    return nullptr;
}
const char* SkillName(SeedType seed) { auto s=Query(seed); return s ? s->name : ""; }
const char* SkillDescription(SeedType seed) { auto s=Query(seed); return s ? s->description : ""; }
void ApplyDefaultBalance(PlantDefinition& d)
{
    if (auto s=Query(d.mSeedType)) { d.mSeedCost=s->cost; d.mRefreshTime=s->recharge; d.mLaunchRate=s->launch; }
}
int DefaultHealth(SeedType seed, int original) { auto s=Query(seed); return s ? s->health : original; }

bool Apply(Board* b, Plant* p)
{
    if (!b || !p || p->mBoard != b || p->mDead || p->mSquished || !p->IsInPlay() || p->mOnBungeeState != NOT_ON_BUNGEE) return false;
    if (PvzpCustomPlants::IsCustom(p->mSeedType))
        return PvzpCustomPlants::ApplyFood(b, p);
    const SkillInfo* info = Query(p->mSeedType);
    if (!info) return false;
    uint32_t id = b->mPlants.DataArrayGetID(p);
    State& state = gStates[b];
    if (state.charges.count(id)) return false; // no stacking or infinite shield refill
    if (p->mSeedType == SEED_IMITATER)
    {
        if (!Query(p->mImitaterType) || p->mImitaterType == SEED_IMITATER) return false;
        int col=p->mPlantCol, row=p->mRow;
        p->ImitaterMorph();
        Plant* copy=b->GetTopPlantAt(col,row,PlantPriority::TOPPLANT_ONLY_NORMAL_POSITION);
        return copy && Apply(b,copy);
    }
    auto it = state.charges.emplace(id, Charge{p->mSeedType,info->duration}).first;
    Charge& c = it->second;
    p->mEatenFlashCountdown = 30;
    Effect(p,PARTICLE_SEED_PACKET_FLASH);
    if (p->mIsAsleep) p->SetSleeping(false);
    switch (p->mSeedType)
    {
    case SEED_ELECTRIC_GATLINGPEA: b->FireElectricFan(p); break;
    case SEED_WALLNUT: Repair(p,p->mPlantMaxHealth); Protect(p,2000,900); break;
    case SEED_TALLNUT: Repair(p,p->mPlantMaxHealth); Protect(p,4000,900); break;
    case SEED_PUMPKINSHELL: Repair(p,p->mPlantMaxHealth); Neighbours(p,300,1500,900,true); break;
    case SEED_LILYPAD: Neighbours(p,400,350,600); break;
    case SEED_FLOWERPOT: Neighbours(p,500,300,600); break;
    case SEED_UMBRELLA: p->DoSpecial(); Neighbours(p,150,600,600); break;
    case SEED_CHERRYBOMB: Burst(p,p->mX+40,p->mY+40,210,2,1800); p->Die(); break;
    case SEED_DOOMSHROOM:
    {
        // Full-lawn double damage, paid for by cratering a 3x3 area.
        for(Zombie* z : b->mZombies)
            if(Enemy(z) && z->EffectedByDamage(p->GetDamageRangeFlags())) z->TakeDamage(3600,0U);
        DoomCraters(p); Effect(p,PARTICLE_DOOM); p->Die(); break;
    }
    case SEED_JALAPENO:
        for (int row=std::max(0,p->mRow-1);row<=std::min((b->StageHas6Rows()?5:4),p->mRow+1);++row)
            { b->DoFwoosh(row); p->BurnRow(row); b->mIceTimer[row]=20; }
        p->Die(); break;
    case SEED_ICESHROOM:
        // Repeated freezes with lethal cold pulses are driven by Pulse.
        FreezeAll(p,250); Effect(p,PARTICLE_ICE_TRAP); break;
    case SEED_POTATOMINE:
        p->mState=STATE_POTATO_ARMED; p->mStateCountdown=0;
        p->PlayBodyReanim("anim_armed",REANIM_LOOP,10,12.0f);
        Burst(p,p->mX+40,p->mY+40,125,0,900); Effect(p,PARTICLE_POTATO_MINE_RISE); break;
    case SEED_SQUASH:
    {
        Zombie* z=p->FindSquashTarget();
        int x=z ? static_cast<int>(z->mPosX)+35 : p->mX+80;
        Burst(p,x-35,p->mY+40,105,0,900);
        Burst(p,x+35,p->mY+40,105,0,900); p->Die(); break;
    }
    case SEED_CHOMPER:
        p->mState=STATE_READY; p->mStateCountdown=0; p->PlayIdleAnim(15.0f); p->UpdateChomper(); break;
    case SEED_SUNSHROOM:
        p->mState=STATE_SUNSHROOM_BIG; p->mStateCountdown=0;
        p->PlayBodyReanim("anim_bigidle",REANIM_LOOP,10,15.0f); break;
    case SEED_SNOWPEA: case SEED_WINTERMELON: Chill(p,400); break;
    case SEED_TORCHWOOD: Burst(p,p->mX+40,p->mY+40,140,1,180); break;
    case SEED_GATLINGPEA:
        // Three temporary Torchwoods turn the barrage into fire peas.
        Summon(p,SEED_TORCHWOOD,p->mPlantCol+1,p->mRow-1,900);
        Summon(p,SEED_TORCHWOOD,p->mPlantCol+1,p->mRow,900);
        Summon(p,SEED_TORCHWOOD,p->mPlantCol+1,p->mRow+1,900);
        break;
    case SEED_GLOOMSHROOM:
        // A ring of mushrooms joins the spinning spore storm.
        Summon(p,SEED_PUFFSHROOM,p->mPlantCol-1,p->mRow,0);
        Summon(p,SEED_SCAREDYSHROOM,p->mPlantCol+1,p->mRow,0);
        Summon(p,SEED_FUMESHROOM,p->mPlantCol,p->mRow-1,0);
        Summon(p,SEED_HYPNOSHROOM,p->mPlantCol,p->mRow+1,0);
        break;
    case SEED_THREEPEATER:
        for (int row=p->mRow-2;row<=p->mRow+2;++row) FireLane(p,row); break;
    case SEED_SPLITPEA: FireLane(p,p->mRow,WEAPON_PRIMARY); FireLane(p,p->mRow,WEAPON_SECONDARY); break;
    case SEED_GRAVEBUSTER:
    {
        int cleared=0;
        Effect(p,PARTICLE_GRAVE_BUSTER_DIE);
        for (GridItem* g : b->mGridItems)
            if (!g->mDead && std::abs(g->mGridX-p->mPlantCol)<=1 && std::abs(g->mGridY-p->mRow)<=1 &&
                (g->mGridItemType==GRIDITEM_GRAVESTONE || g->mGridItemType==GRIDITEM_CRATER))
            { if(g->mGridItemType==GRIDITEM_GRAVESTONE) ++b->mGravesCleared;
              g->GridItemDie(); if (cleared++ < 5) Drop(p,COIN_SUN,1); }
        p->Die(); break;
    }
    case SEED_HYPNOSHROOM: case SEED_TANGLEKELP:
    {
        int count=0;
        for (Zombie* z : b->mZombies)
        {
            if (!Enemy(z) || !z->mHasHead || z->mZombieHeight!=HEIGHT_ZOMBIE_NORMAL ||
                std::abs(z->mPosX-p->mX)>300 || std::abs(z->mRow-p->mRow)>1 ||
                z->mZombieType==ZOMBIE_BOSS || z->mZombieType==ZOMBIE_GARGANTUAR || z->mZombieType==ZOMBIE_REDEYE_GARGANTUAR ||
                z->mZombieType==ZOMBIE_ZAMBONI || z->mZombieType==ZOMBIE_CATAPULT || z->mZombieType==ZOMBIE_BUNGEE ||
                z->IsBobsledTeamWithSled() || z->IsFlying() || !z->EffectedByDamage(p->GetDamageRangeFlags())) continue;
            if (p->mSeedType==SEED_TANGLEKELP)
            { if (!z->mInPool) continue; z->PoolSplash(false); z->DragUnder(); z->DieWithLoot(); }
            else { z->StartMindControlled(); p->mApp->AddPvzpParticle(z->mX+60,z->mY+40,z->mRenderOrder+1,PARTICLE_MIND_CONTROL); }
            if (++count==3) break;
        }
        p->Die(); break;
    }
    case SEED_BLOVER:
        p->BlowAwayFliers();
        for (Zombie* z : b->mZombies) if (Enemy(z)) z->ApplyChill(false);
        Effect(p,PARTICLE_ICE_TRAP); p->Die(); break;
    case SEED_MAGNETSHROOM:
    {
        int count=0;
        for (Zombie* z : b->mZombies)
        {
            if (!Enemy(z) || !z->mHasHead || z->mZombieHeight!=HEIGHT_ZOMBIE_NORMAL || std::abs(z->mPosX-p->mX)>450 || std::abs(z->mRow-p->mRow)>2) continue;
            bool metal=z->mHelmType==HELMTYPE_PAIL || z->mHelmType==HELMTYPE_FOOTBALL || z->mShieldType==SHIELDTYPE_DOOR || z->mShieldType==SHIELDTYPE_LADDER ||
                (z->mZombieType==ZOMBIE_POGO && z->mHasObject) || (z->mZombieType==ZOMBIE_DIGGER && z->mHasObject) || z->mZombiePhase==PHASE_JACK_IN_THE_BOX_RUNNING;
            if (!metal) continue;
            p->MagnetShroomAttactItem(z); if (++count==5) break;
        }
        break;
    }
    case SEED_INSTANT_COFFEE:
        for (Plant* q : b->mPlants)
            if (!q->mDead && !q->mSquished && Plant::IsFungus(q->mSeedType) && std::abs(q->mPlantCol-p->mPlantCol)<=1 && std::abs(q->mRow-p->mRow)<=1)
                { if(q->mIsAsleep) q->SetSleeping(false); q->mWakeUpCounter=0; Repair(q,200); q->mLaunchCounter=1; }
        p->Die(); break;
    case SEED_GARLIC:
    {
        Repair(p,p->mPlantMaxHealth);
        int count=0;
        for (Zombie* z : b->mZombies)
        {
            if (!Enemy(z) || z->mRow!=p->mRow || std::abs(z->mPosX-p->mX)>250 || z->mInPool || z->mZombieHeight!=HEIGHT_ZOMBIE_NORMAL || z->mZombiePhase!=PHASE_ZOMBIE_NORMAL || z->mZombieType==ZOMBIE_BOSS || z->mZombieType==ZOMBIE_ZAMBONI || z->mZombieType==ZOMBIE_CATAPULT) continue;
            bool up=b->RowCanHaveZombies(z->mRow-1) && !b->IsPoolSquare(p->mPlantCol,z->mRow-1);
            bool down=b->RowCanHaveZombies(z->mRow+1) && !b->IsPoolSquare(p->mPlantCol,z->mRow+1);
            if (!up && !down) continue;
            // Use the original garlic lane-change routine, including its terrain
            // restrictions and walking animation, rather than teleporting lanes.
            z->StopEating(); z->ShowYuckyFace(true);
            z->mYuckyFace=true; z->mYuckyFaceCounter=169; z->UpdateYuckyFace();
            if (++count==5) break;
        }
        break;
    }
    case SEED_SPIKEROCK: Repair(p,p->mPlantMaxHealth); break;
    case SEED_GOLD_MAGNET: Drop(p,COIN_GOLD,2); break;
    case SEED_COBCANNON:
        if (p->mState==STATE_COBCANNON_FIRING) c.cobFired=true;
        else ReloadCob(p);
        break;
    default: break; // Explicit registry shooters are handled by AfterFire/Pulse.
    }
    if (!p->mDead) Pulse(p,c);
    return true;
}

void AfterFire(Plant* p, Projectile* shot, Zombie*, PlantWeapon)
{
    if (!p || !shot || !p->mBoard) return;
    auto board=gStates.find(p->mBoard); if (board==gStates.end()) return;
    auto it=board->second.charges.find(p->mBoard->mPlants.DataArrayGetID(p));
    if (it==board->second.charges.end() || it->second.remaining<=0) return;
    Charge& c=it->second;
    int count=0;
    switch(c.seed)
    {
    case SEED_CABBAGEPULT: case SEED_KERNELPULT: case SEED_MELONPULT: case SEED_WINTERMELON: count=1; break;
    case SEED_SPLITPEA: count=1; break;
    case SEED_CATTAIL: count=2; break;
    default: return; // Not a generic triple-shot, especially never E or cob cannon.
    }
    for(int i=0;i<count && c.extraBudget>0;++i)
    {
        Board* b=p->mBoard;
        if (b->mProjectiles.mSize>=b->mProjectiles.mMaxSize) break;
        ProjectileType type=c.seed==SEED_KERNELPULT ? PROJECTILE_BUTTER : shot->mProjectileType;
        Projectile* extra=b->AddProjectile(shot->mX,shot->mY,shot->mRenderOrder,shot->mRow,type);
        if(!extra) continue;
        --c.extraBudget;
        // Do NOT copy the whole object: that aliases owned attachment IDs.
        // Copy every movement/collision/target component only after Fire's setup.
        extra->mPosX=shot->mPosX; extra->mPosY=shot->mPosY; extra->mPosZ=shot->mPosZ;
        extra->mVelX=shot->mVelX; extra->mVelY=shot->mVelY; extra->mVelZ=shot->mVelZ; extra->mAccZ=shot->mAccZ;
        extra->mShadowY=shot->mShadowY; extra->mMotionType=shot->mMotionType;
        extra->mOnHighGround=shot->mOnHighGround; extra->mDamageRangeFlags=shot->mDamageRangeFlags;
        extra->mHitTorchwoodGridX=shot->mHitTorchwoodGridX;
        extra->mCobTargetX=shot->mCobTargetX; extra->mCobTargetRow=shot->mCobTargetRow;
        extra->mTargetZombieID=shot->mTargetZombieID; extra->mLastPortalX=shot->mLastPortalX;
        extra->mRotation=shot->mRotation; extra->mRotationSpeed=shot->mRotationSpeed;
        // Spatial separation is perpendicular to the path: lob range, backwards
        // direction, threepeater easing and homing all remain their original kind.
        extra->mPosZ-=6.0f*(i+1);
        if (shot->IsElectric()) extra->SetElectric();
    }
}

void Update(Board* b)
{
    auto found=gStates.find(b); if(found==gStates.end()) return;
    State& s=found->second;
    for(auto it=s.charges.begin();it!=s.charges.end();)
    {
        Plant* p=Live(b,it->first);
        Charge& c=it->second;
        if(!p || p->mSeedType!=c.seed || p->NotOnGround() || --c.remaining<=0)
        {
            if(p && !p->mDead && c.seed==SEED_ICESHROOM) p->Die();
            it=s.charges.erase(it); continue;
        }
        ++c.elapsed; Pulse(p,c); ++it;
    }
    for(auto it=s.shields.begin();it!=s.shields.end();)
    {
        Plant* p=Live(b,it->first);
        if(!p) { it=s.shields.erase(it); continue; }
        if(--it->second.remaining<=0 || p->mPlantHealth<=p->mPlantMaxHealth)
        { p->mPlantHealth=std::min(p->mPlantHealth,p->mPlantMaxHealth); it=s.shields.erase(it); }
        else ++it;
    }
    for(auto it=s.tempPlants.begin();it!=s.tempPlants.end();)
    {
        Plant* q=b->mPlants.DataArrayTryToGet(it->first);
        if(!q || q->mDead){it=s.tempPlants.erase(it);continue;}
        if(--it->second<=0){q->Die();it=s.tempPlants.erase(it);} else ++it;
    }
    if(s.charges.empty() && s.shields.empty() && s.tempPlants.empty()) gStates.erase(found);
}
void Reset(Board* b)
{
    auto it=gStates.find(b); if(it==gStates.end()) return;
    for(auto& [id,shield]:it->second.shields)
        if(Plant* p=Live(b,id)) p->mPlantHealth=std::min(p->mPlantHealth,p->mPlantMaxHealth);
    gStates.erase(it);
}
int RemainingTicks(Board* b,uint32_t id)
{
    auto i=gStates.find(b); if(i==gStates.end() || !Live(b,id)) return 0;
    auto c=i->second.charges.find(id); auto s=i->second.shields.find(id);
    return std::max(c==i->second.charges.end()?0:c->second.remaining,s==i->second.shields.end()?0:s->second.remaining);
}
int ShieldHealth(Board* b,uint32_t id)
{
    auto i=gStates.find(b); if(i==gStates.end()) return 0;
    auto s=i->second.shields.find(id); Plant* p=Live(b,id);
    return s==i->second.shields.end() || !p ? 0 : std::clamp(p->mPlantHealth-p->mPlantMaxHealth,0,s->second.cap);
}
bool IsBoosted(Board* b,uint32_t id) { return RemainingTicks(b,id)>0; }
std::size_t ActiveCount(Board* b)
{
    auto it=gStates.find(b); if(it==gStates.end()) return 0;
    std::size_t n=0; for(auto& [id,c]:it->second.charges) if(Live(b,id)) ++n; return n;
}
void Draw(Board* b,Sexy::Graphics* g)
{
    if(!b || !g || !gStates.count(b)) return;
    Color old=g->GetColor(); auto font=g->GetFont();
    if(FONT_DWARVENTODCRAFT15) g->SetFont(FONT_DWARVENTODCRAFT15);
    for(Plant* p:b->mPlants)
    {
        if(p->mDead || p->mSquished) continue;
        uint32_t id=b->mPlants.DataArrayGetID(p); int ticks=RemainingTicks(b,id);
        if(!ticks) continue;
        int shield=ShieldHealth(b,id);
        g->SetColor(shield ? Color(90,210,255,220) : Color(255,220,70,220));
        int cx=p->mX+40, cy=p->mY+42;
        // A small animated octagonal halo works with the existing Graphics API.
        int pulse=(b->mMainCounter/6)%4;
        for(int j=0;j<8;++j)
        {
            float a=j*0.78539816f, next=(j+1)*0.78539816f;
            g->DrawLine(cx+int(std::cos(a)*(39+pulse)),cy+int(std::sin(a)*(27+pulse)),
                        cx+int(std::cos(next)*(39+pulse)),cy+int(std::sin(next)*(27+pulse)));
        }
        if(g->GetFont()) g->DrawString(std::to_string((ticks+99)/100)+"s",cx-9,p->mY-4);
        if(shield) { g->FillRect(p->mX+8,p->mY+70,64,4); }
    }
    g->SetColor(old); g->SetFont(font);
}

// Versioned little-endian sidecar payload. Plants' legacy health is already
// serialized by the parent save; loading shield metadata MUST NOT add it again.
std::vector<uint8_t> Save(Board* b)
{
    std::vector<uint8_t> out;
    auto put=[&](uint32_t v){for(int n=0;n<4;++n)out.push_back(static_cast<uint8_t>(v>>(n*8)));};
    put(1);
    auto found=gStates.find(b);
    std::map<uint32_t,bool> ids;
    if(found!=gStates.end())
    {
        for(auto& [id,c]:found->second.charges) if(Live(b,id)) ids[id]=true;
        for(auto& [id,s]:found->second.shields) if(Live(b,id) && ShieldHealth(b,id)>0) ids[id]=true;
    }
    put(static_cast<uint32_t>(ids.size()));
    if(found==gStates.end()) return out;
    State& state=found->second;
    for(auto& [id,unused]:ids)
    {
        auto c=state.charges.find(id); auto s=state.shields.find(id);
        put(id); put(static_cast<uint32_t>(Live(b,id)->mSeedType));
        put(c==state.charges.end()?0:c->second.remaining);
        put(c==state.charges.end()?0:c->second.elapsed);
        put(c==state.charges.end()?0:c->second.extraBudget);
        put(c!=state.charges.end() && c->second.cobFired ? 1:0);
        put(s==state.shields.end()?0:s->second.remaining);
        put(ShieldHealth(b,id));
    }
    return out;
}
bool Load(Board* b,const std::vector<uint8_t>& bytes)
{
    if(!b || bytes.size()<8) return false;
    std::size_t cursor=0;
    auto get=[&](){uint32_t v=0;for(int n=0;n<4;++n)v|=uint32_t(bytes[cursor++])<<(n*8);return v;};
    if(get()!=1) return false;
    uint32_t count=get();
    if(count>b->mPlants.mMaxSize || bytes.size()!=8+std::size_t(count)*32) return false;
    State pending;
    std::map<uint32_t,bool> seen;
    for(uint32_t n=0;n<count;++n)
    {
        uint32_t id=get(), seed=get(), remaining=get(), elapsed=get(), budget=get(), fired=get(), shieldTicks=get(), shield=get();
        Plant* p=Live(b,id); const SkillInfo* info=Query(static_cast<SeedType>(seed));
        if(!p || seed>=static_cast<uint32_t>(NUM_SEED_TYPES) || p->mSeedType!=static_cast<SeedType>(seed) ||
            (remaining && (!info || remaining>static_cast<uint32_t>(info->duration) || elapsed+remaining>static_cast<uint32_t>(info->duration))) ||
            elapsed>1800 || budget>96 || fired>1 || shieldTicks>900 || shield>4000 ||
            shield>static_cast<uint32_t>(std::max(0,p->mPlantMaxHealth)) || seen.count(id) ||
            (!remaining && !shield) || (shield && !shieldTicks)) return false;
        seen[id]=true;
        if(remaining) pending.charges.emplace(id,Charge{static_cast<SeedType>(seed),static_cast<int>(remaining),static_cast<int>(elapsed),static_cast<int>(budget),fired!=0});
        if(shield && shieldTicks) pending.shields.emplace(id,Shield{static_cast<int>(shieldTicks),static_cast<int>(shield)});
    }
    // No health addition: health was read from the original Plant payload.
    // Calling Load twice is idempotent. Caller resets OLD board state before
    // loading the Plant payload, not here (which would erase the saved shield).
    gStates[b]=std::move(pending);
    for(Plant* p:b->mPlants)
    {
        auto it=gStates[b].shields.find(b->mPlants.DataArrayGetID(p));
        int cap=it==gStates[b].shields.end()?0:it->second.cap;
        p->mPlantHealth=std::min(p->mPlantHealth,p->mPlantMaxHealth+cap);
    }
    return true;
}
}
