// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpCustomPlants.h"
#include "PvzpMod.h"
#include "../Lawn/Board.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Zombie.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "Reanimator.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <vector>

namespace {
struct Runtime {
    int lastHealth = 0, damage = 0, pulse = 0, cooldown = 0;
};
std::map<Board*, std::map<uint32_t, Runtime>> states;
const PvzpMod::CustomPlant* Config(SeedType type) {
    if (type < SEED_CUSTOM_0 || type > SEED_CUSTOM_3) return nullptr;
    for (const auto& config : PvzpMod::CustomPlants())
        if (config.slot == int(type) - int(SEED_CUSTOM_0)) return &config;
    return nullptr;
}
bool Live(Board* b, Plant* p) {
    return b && p && p->mBoard == b && !p->mDead && !p->mSquished && p->IsInPlay() &&
        p->mOnBungeeState == NOT_ON_BUNGEE;
}
Runtime& State(Board* b, Plant* p) {
    auto& entries = states[b];
    auto [it, inserted] = entries.try_emplace(b->mPlants.DataArrayGetID(p));
    // Old saves without a custom-state chunk start from current health: no
    // retroactive rewards for damage dealt before this session.
    if (inserted) it->second.lastHealth = std::max(0, p->mPlantHealth);
    return it->second;
}
int Number(const std::string& token, size_t prefix, int maximum) {
    int value = 0;
    auto result = std::from_chars(token.data()+prefix, token.data()+token.size(), value);
    return result.ec == std::errc{} && result.ptr == token.data()+token.size() ? std::clamp(value, 0, maximum) : 0;
}
void Chill(Board* b, int row = -1) {
    for (Zombie* z : b->mZombies)
        if (!z->mDead && !z->mMindControlled && (row < 0 || z->mRow == row)) z->ApplyChill(false);
}
Zombie* Friendly(Board* b, ZombieType type, int col, int row, int offset = 0) {
    if (b->mZombies.mSize >= b->mZombies.mMaxSize-4) return nullptr;
    Zombie* z = b->AddZombieInRow(type, row, Zombie::ZOMBIE_WAVE_DEBUG);
    if (!z) return nullptr;
    z->mPosX = b->GridToPixelX(col, row) + offset;
    z->mPosY = z->GetPosYBasedOnRow(row);
    z->mX = int(z->mPosX); z->mY = int(z->mPosY);
    // Use the engine transition, not a flag assignment: detach leader links,
    // reset portal state and preserve the normal charmed-zombie lifecycle.
    z->StartMindControlled();
    return z;
}
void Event(Board* b, Plant* p, const std::string& event) {
    if (event.starts_with("sun:")) { b->AddSunMoney(Number(event, 4, 9990)); return; }
    if (event.starts_with("heal:")) {
        // A fatal bite still emits its final event, but cannot resurrect a dead nut.
        if (p->mPlantHealth > 0) p->mPlantHealth = std::min(p->mPlantMaxHealth, p->mPlantHealth + Number(event, 5, 10000000));
        return;
    }
    if (event == "chill") { Chill(b); return; }
    if (event == "chill:row") { Chill(b, p->mRow); return; }
    if (event == "plant:random") {
        static const SeedType pool[] = {SEED_PEASHOOTER, SEED_SUNFLOWER, SEED_SNOWPEA, SEED_REPEATER,
            SEED_WALLNUT, SEED_CHERRYBOMB, SEED_POTATOMINE, SEED_PUFFSHROOM, SEED_GATLINGPEA, SEED_TWINSUNFLOWER};
        SeedType type = pool[Rand(int(std::size(pool)))];
        std::vector<std::pair<int,int>> spots;
        for (int r=0; r<(b->StageHas6Rows()?6:5); ++r)
            for (int c=0; c<MAX_GRID_SIZE_X; ++c)
                if (b->CanPlantAt(c,r,type)==PLANTING_OK && !b->GetTopPlantAt(c,r,TOPPLANT_ONLY_NORMAL_POSITION)) spots.emplace_back(c,r);
        if (!spots.empty()) { auto [c,r]=spots[Rand(int(spots.size()))]; b->AddPlant(c,r,type); }
        return;
    }
    if (event.starts_with("zombie:")) {
        if (b->mZombies.mSize >= b->mZombies.mMaxSize-4) return;
        ZombieType type = ZOMBIE_NORMAL;
        std::string name = event.substr(7);
        if (name=="GARGANTUAR") type=ZOMBIE_GARGANTUAR;
        else if (name=="BUNGEE") type=ZOMBIE_BUNGEE;
        else if (name=="DIGGER") type=ZOMBIE_DIGGER;
        else if (name=="BALLOON") type=ZOMBIE_BALLOON;
        else if (name=="CONEHEAD") type=ZOMBIE_TRAFFIC_CONE;
        else if (name=="BUCKETHEAD") type=ZOMBIE_PAIL;
        b->AddZombie(type, Zombie::ZOMBIE_WAVE_DEBUG); // terrain-aware row selection
        return;
    }
    if (event == "destroy:plant") {
        std::vector<Plant*> targets;
        for (Plant* q : b->mPlants) if (!q->mDead && q!=p && q->IsInPlay()) targets.push_back(q);
        if (!targets.empty()) targets[Rand(int(targets.size()))]->Die();
    }
}
void RandomEvent(Board* b, Plant* p, const std::vector<std::string>& events) {
    if (!events.empty()) Event(b,p,events[Rand(int(events.size()))]);
}
void Put(std::vector<uint8_t>& out, uint32_t n) { for (int i=0;i<4;++i) out.push_back(uint8_t(n>>(i*8))); }
uint32_t Get(const std::vector<uint8_t>& in, size_t& at) {
    uint32_t n=0; for (int i=0;i<4;++i) n|=uint32_t(in[at++])<<(i*8); return n;
}
}

namespace PvzpCustomPlants {
bool IsCustom(SeedType t) { return t>=SEED_CUSTOM_0 && t<=SEED_CUSTOM_3; }
bool IsSlotUsed(SeedType t) { return Config(t)!=nullptr; }
bool IsBrain(SeedType t) { const auto* c=Config(t); return c && c->behavior=="brain"; }
int HealthForSeed(SeedType t) { const auto* c=Config(t); return c?c->health:300; }
int CostForSeed(SeedType t,int fallback) { const auto* c=Config(t); return c?c->cost:fallback; }
Color ColorForSeed(SeedType t) {
    const auto* c=Config(t); if (!c) return Color::White;
    if (c->tint=="blue") return Color(120,170,255);
    if (c->tint=="red") return Color(255,120,110);
    if (c->tint=="green") return Color(130,235,130);
    if (c->tint=="purple") return Color(190,140,255);
    return Color(255,236,170);
}
void Reset(Board* b) { states.erase(b); }
void Initialize(Plant* p) {
    if (p && p->mBoard && IsSlotUsed(p->mSeedType)) {
        Runtime s; s.lastHealth=std::max(0,p->mPlantHealth);
        states[p->mBoard][p->mBoard->mPlants.DataArrayGetID(p)]=s;
    }
}
void ApplyVisuals(Plant* p) {
    if (!p || !IsSlotUsed(p->mSeedType)) return;
    if (auto* body=p->mApp->ReanimationTryToGet(p->mBodyReanimID)) body->mColorOverride=ColorForSeed(p->mSeedType);
}
void ObserveDamage(Board* b, Plant* p) {
    if (!b || !p || p->mBoard!=b || p->mDead) return;
    const auto* c=Config(p->mSeedType); if (!c || c->behavior=="brain") return;
    auto& s=State(b,p);
    int hp=std::clamp(p->mPlantHealth,0,p->mPlantMaxHealth);
    s.damage+=std::max(0,s.lastHealth-hp);
    s.lastHealth=hp;
    int events=0;
    while (s.damage>=c->step && ++events<=16) {
        s.damage-=c->step; // commit before dispatch, including fatal threshold
        bool bad=!c->badEvents.empty() && Rand(100)<c->badChance;
        RandomEvent(b,p,bad?c->badEvents:c->events);
    }
    s.lastHealth=std::max(0,p->mPlantHealth); // healing does not erase progress
}
void BrainClick(Board* b, Plant* p) {
    if (!Live(b,p) || !IsBrain(p->mSeedType) || !b->CanUseModTools()) return;
    auto& s=State(b,p);
    if (s.cooldown>0) return;
    s.cooldown=50;
    if (b->mZombies.mSize>=b->mZombies.mMaxSize-4) return;
    if (Rand(2)==0) {
        std::vector<ZombieType> pool;
        bool boss=false; for (Zombie* z:b->mZombies) if (!z->mDead && z->mZombieType==ZOMBIE_BOSS) boss=true;
        // Every actual engine zombie type has a chance; cached preview types do not.
        for (int i=0;i<int(NUM_ZOMBIE_TYPES);++i) {
            if (i==int(ZOMBIE_BOSS) && boss) continue;
            bool legal=false;
            for (int row=0;row<MAX_GRID_SIZE_Y;++row)
                legal=legal || b->RowCanHaveZombieType(row,ZombieType(i));
            // Water-only zombies and sleds without ice have no legal row on
            // some boards. Never feed an all-zero row distribution to the engine.
            if (legal) pool.push_back(ZombieType(i));
        }
        if (!pool.empty()) b->AddZombie(pool[Rand(int(pool.size()))],Zombie::ZOMBIE_WAVE_DEBUG);
    } else {
        int cost=100+Rand(401);
        if (b->mSunMoney<cost) return;
        static const ZombieType pool[]={ZOMBIE_NORMAL,ZOMBIE_TRAFFIC_CONE,ZOMBIE_PAIL,ZOMBIE_FOOTBALL,ZOMBIE_DOOR,ZOMBIE_NEWSPAPER};
        // No pole/pogo/digger/bungee/vehicle/linked formations in the friendly pool.
        if (Friendly(b,pool[Rand(int(std::size(pool)))],p->mPlantCol,p->mRow)) b->mSunMoney-=cost;
    }
}
void BrainUltimate(Board* b, Plant* p) {
    if (!Live(b,p) || !IsBrain(p->mSeedType)) return;
    static const ZombieType pool[]={ZOMBIE_FOOTBALL,ZOMBIE_PAIL,ZOMBIE_TRAFFIC_CONE};
    for (int r=0;r<(b->StageHas6Rows()?6:5);++r)
        for (int i=0;i<5;++i) Friendly(b,pool[Rand(3)],0,r,i*8);
}
bool ApplyFood(Board* b, Plant* p) {
    if (!Live(b,p)) return false;
    const auto* c=Config(p->mSeedType); if (!c) return false;
    if (c->behavior=="brain") { BrainUltimate(b,p); return true; }
    if (c->foodEvents.empty()) return false;
    for (const auto& event:c->foodEvents) Event(b,p,event);
    State(b,p).lastHealth=std::max(0,p->mPlantHealth);
    return true;
}
void Update(Board* b) {
    if (!b || !b->CanUseModTools()) return;
    std::vector<uint32_t> ids;
    for (Plant* p:b->mPlants) if (!p->mDead && IsSlotUsed(p->mSeedType)) ids.push_back(b->mPlants.DataArrayGetID(p));
    for (uint32_t id:ids) {
        Plant* p=b->mPlants.DataArrayTryToGet(id); if (!Live(b,p)) continue;
        ObserveDamage(b,p);
        auto& s=State(b,p); if (s.cooldown>0) --s.cooldown;
        const auto* c=Config(p->mSeedType);
        if (c && c->interval>0 && !c->pulseEvents.empty() && !p->mIsAsleep && ++s.pulse>=c->interval) {
            s.pulse=0; for (const auto& event:c->pulseEvents) Event(b,p,event);
            s.lastHealth=std::max(0,p->mPlantHealth);
        }
    }
    auto it=states.find(b);
    if (it!=states.end()) {
        for (auto e=it->second.begin();e!=it->second.end();) {
            Plant* p=b->mPlants.DataArrayTryToGet(e->first);
            if (!p || p->mDead || !IsSlotUsed(p->mSeedType)) e=it->second.erase(e); else ++e;
        }
        if (it->second.empty()) states.erase(it);
    }
}
std::vector<uint8_t> Save(Board* b) {
    std::vector<uint8_t> out; Put(out,1);
    auto it=states.find(b); uint32_t count=0;
    if (it!=states.end()) for (auto& [id,s]:it->second) { auto* p=b->mPlants.DataArrayTryToGet(id); if(p && !p->mDead && IsSlotUsed(p->mSeedType)) ++count; }
    Put(out,count);
    if (it!=states.end()) for (auto& [id,s]:it->second) {
        auto* p=b->mPlants.DataArrayTryToGet(id); if (!p || p->mDead || !IsSlotUsed(p->mSeedType)) continue;
        Put(out,id); Put(out,uint32_t(p->mSeedType)); Put(out,uint32_t(s.lastHealth));
        Put(out,uint32_t(s.damage)); Put(out,uint32_t(s.pulse)); Put(out,uint32_t(s.cooldown));
    }
    return out;
}
bool Load(Board* b,const std::vector<uint8_t>& data) {
    if (!b || data.size()<8) return false;
    size_t at=0; if(Get(data,at)!=1) return false; uint32_t count=Get(data,at);
    if(count>uint32_t(b->mPlants.mMaxSize) || data.size()!=8+size_t(count)*24) return false;
    std::map<uint32_t,Runtime> restored;
    for(uint32_t i=0;i<count;++i) {
        uint32_t id=Get(data,at), type=Get(data,at), hp=Get(data,at), damage=Get(data,at), pulse=Get(data,at), cooldown=Get(data,at);
        Plant* p=b->mPlants.DataArrayTryToGet(id);
        if(!p || p->mDead || uint32_t(p->mSeedType)!=type || !IsCustom(p->mSeedType) || hp>10000000 || damage>10000000 || pulse>60000 || cooldown>50 || restored.count(id)) return false;
        // A disabled/missing mod leaves a harmless base plant; never dispatch unknown rules.
        if(IsSlotUsed(p->mSeedType)) restored.emplace(id,Runtime{int(hp),int(damage),int(pulse),int(cooldown)});
    }
    states[b]=std::move(restored); return true;
}
void DrawBrain(Sexy::Graphics* g, Plant* p) {
    Sexy::Graphics local(*g);
    int offset=int(PlantDrawHeightOffset(p->mBoard,p,p->mSeedType,p->mPlantCol,p->mRow));
    local.DrawImage(Sexy::IMAGE_BRAIN,8,12+offset,60,p->mSquished?12:48);
}
void DrawBox(Sexy::Graphics* g,Plant* p) {
    const auto* c=Config(p->mSeedType); if(!c || p->mDead || c->behavior=="brain") return;
    if(c->tint!="gold" && c->tint!="blue") return;
    Sexy::Graphics local(*g); local.SetColorizeImages(true); local.SetColor(ColorForSeed(p->mSeedType));
    int offset=int(PlantDrawHeightOffset(p->mBoard,p,p->mSeedType,p->mPlantCol,p->mRow));
    local.DrawImage(Sexy::IMAGE_PRESENT,-2,-8+offset,70,p->mSquished?18:70);
}
bool DrawPacketIcon(Sexy::Graphics* g,SeedType type,float x,float y) {
    if(!IsBrain(type)) return false;
    // x/y already include caller's scale. Do not call SetScale on the caller:
    // this is shared by chooser, flight, bank, cooldown overlay and cursor.
    g->DrawImage(Sexy::IMAGE_BRAIN,int(x+8*g->mScaleX),int(y+12*g->mScaleY),int(34*g->mScaleX),int(30*g->mScaleY));
    return true;
}
}
