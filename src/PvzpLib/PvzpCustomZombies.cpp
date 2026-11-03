// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpCustomZombies.h"
#include "PvzpCustomPlants.h"
#include "PvzpMod.h"
#include "PvzpPrism.h"
#include "PvzpCommon.h"
#include "../Lawn/Coin.h"
#include "Reanimator.h"
#include "../Lawn/Board.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/Projectile.h"
#include "../Lawn/SeedPacket.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "graphics/Graphics.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {
struct Actor { std::string id; int cooldown=0, uses=0, flash=0, phase=0, targetRow=-1, storedSun=0; };
struct Haste { int ticks=0, percent=0; };
struct Shot { int damage=20, freeze=0; std::string kind; };
struct State { std::map<uint32_t,Actor> actors; std::map<uint32_t,int> frozen; std::map<uint32_t,Shot> shots; std::map<uint32_t,Haste> haste; };
std::map<Board*,State> boards;
const PvzpMod::CustomZombie* Definition(std::string_view id) {
    for(const auto& c:PvzpMod::CustomZombies()) if(c.id==id)return &c;
    return nullptr;
}
ZombieType Base(std::string_view name) {
    for(const auto& d:gZombieDefs)if(d.mZombieName && name==d.mZombieName)return d.mZombieType;
    return ZOMBIE_INVALID;
}
Actor* Find(const Zombie* z) {
    if(!z || !z->mBoard)return nullptr;
    auto b=boards.find(z->mBoard);if(b==boards.end())return nullptr;
    auto a=b->second.actors.find(z->mBoard->mZombies.DataArrayGetID(const_cast<Zombie*>(z)));
    return a==b->second.actors.end()?nullptr:&a->second;
}
const PvzpMod::CustomZombie* Config(const Zombie* z) { auto* s=Find(z);return s?Definition(s->id):nullptr; }
void Hurt(Plant* p,int damage) {
    if(!p || p->mDead || p->NotOnGround())return;
    p->mPlantHealth-=damage;p->mEatenFlashCountdown=std::max(p->mEatenFlashCountdown,25);
    PvzpCustomPlants::ObserveDamage(p->mBoard,p);
    if(p->mPlantHealth<=0 && !p->mDead)p->Die();
}
Plant* Target(Board* b,Zombie* z,bool requireMeleeRange=false) {
    Plant* found=nullptr;float closest=10000;
    for(Plant* p:b->mPlants) {
        if(p->mDead || p->NotOnGround() || p->mRow!=z->mRow || !z->CanTargetPlant(p,ATTACKTYPE_CHEW))continue;
        float distance=z->mPosX-p->mX;
        if(distance< -25 || (requireMeleeRange && distance>105))continue;
        if(distance<closest){found=p;closest=distance;}
    }
    return found;
}
SeedType HeadSeed(std::string_view head) {
    if(head=="snowpea")return SEED_SNOWPEA;
    if(head=="firepea" || head=="pea")return SEED_PEASHOOTER;
    if(head=="sunflower")return SEED_SUNFLOWER;
    if(head=="chomper")return SEED_CHOMPER;
    if(head=="melon")return SEED_MELONPULT;
    if(head=="gatling")return SEED_GATLINGPEA;
    if(head=="pumpkin")return SEED_PUMPKINSHELL;
    if(head=="coffee")return SEED_INSTANT_COFFEE;
    if(head=="garlic")return SEED_GARLIC;
    if(head=="cherry")return SEED_CHERRYBOMB;
    if(head=="magnet")return SEED_MAGNETSHROOM;
    if(head=="tallnut")return SEED_TALLNUT;
    return SEED_PEASHOOTER;
}
void Fire(Board* b,Zombie* z,const PvzpMod::CustomZombie& c) {
    if(b->mProjectiles.mSize>=b->mProjectiles.mMaxSize-4)return;
    if(!z->mMindControlled && !Target(b,z))return;
    bool cold=c.ability=="shoot_ice",fire=c.ability=="shoot_fire",melon=c.ability=="shoot_melon";
    const int x=int(z->mPosX)+30,y=int(z->mPosY)+38;
    if(z->mMindControlled) {
        Projectile* p=b->AddProjectile(x,y,z->mRenderOrder+1,z->mRow,cold?PROJECTILE_SNOWPEA:PROJECTILE_PEA);
        if(p && fire)p->ConvertToFireball(-3);
        return;
    }
    Projectile* p=b->AddProjectile(x,y,z->mRenderOrder+1,z->mRow,PROJECTILE_ZOMBIE_PEA);
    if(!p)return;
    p->mMotionType=MOTION_BACKWARDS;
    p->mShadowY=b->GridToPixelY(b->PixelToGridXKeepOnBoard(x,y),z->mRow)+67;
    boards[b].shots[b->mProjectiles.DataArrayGetID(p)]={c.magnitude,cold?150:0,cold?"ice":fire?"fire":melon?"melon":"pea"};
}
bool Active(Zombie* z){return z && !z->mDead && !z->IsDeadOrDying() && z->mHasHead && !z->mIceTrapCounter && !z->mButteredCounter;}
void ResolveWindup(Board* b,Zombie* z,Actor& a,const PvzpMod::CustomZombie& c){
    if(c.ability=="lane_dash"){
        const int row=a.targetRow;a.targetRow=-1;
        if(row<0 || row>4 || z->mIsEating || Target(b,z,true) || !b->RowCanHaveZombieType(row,z->mZombieType))return;
        z->mRow=row;z->mPosY=z->GetPosYBasedOnRow(row);z->mY=int(z->mPosY);
        z->mRenderOrder=Board::MakeRenderOrder(RENDER_LAYER_ZOMBIE,row,0);z->StartWalkAnim(0);a.flash=30;
    }else if(c.ability=="cherry_charge"){
        // The visible one-second fuse pauses under freeze/butter. Killing the
        // courier before it resolves disarms it, and charm changes its targets.
        if(z->mMindControlled){for(Zombie* other:b->mZombies)if(other!=z && !other->IsDeadOrDying() && !other->mMindControlled &&
            std::abs(other->mRow-z->mRow)<=1 && std::abs(other->mPosX-z->mPosX)<150)other->TakeDamage(c.magnitude,0);}
        else {for(Plant* p:b->mPlants)if(!p->mDead && std::abs(p->mRow-z->mRow)<=1 && std::abs(p->mX-z->mPosX)<150)Hurt(p,c.magnitude);}
        z->DieWithLoot();
    }
}
void Ability(Board* b,Zombie* z,Actor& a,const PvzpMod::CustomZombie& c) {
    if(c.ability=="none" || c.ability=="shield_aura" || c.ability=="rebound")return;
    if(c.ability=="rally"){
        for(Zombie* other:b->mZombies)if(!other->IsDeadOrDying() && other->mMindControlled==z->mMindControlled && other->mZombieType!=ZOMBIE_BOSS &&
            std::abs(other->mRow-z->mRow)<=1 && std::abs(other->mPosX-z->mPosX)<220){
            auto& buff=boards[b].haste[b->mZombies.DataArrayGetID(other)];buff.ticks=std::max(buff.ticks,250);buff.percent=std::max(buff.percent,std::min(100,c.magnitude));
        }
    }else if(c.ability=="lane_dash"){
        if(z->mIsEating || Target(b,z,true))return;
        int row=z->mRow+(a.uses%2?1:-1);if(row<0 || row>4)row=z->mRow+(a.uses%2?-1:1);
        if(!b->RowCanHaveZombieType(row,z->mZombieType))return;
        a.targetRow=row;a.phase=80;
    }else if(c.ability=="cherry_charge"){
        bool arm=false;
        if(z->mMindControlled){for(Zombie* other:b->mZombies)if(!other->IsDeadOrDying() && !other->mMindControlled && other->mRow==z->mRow && std::abs(other->mPosX-z->mPosX)<160){arm=true;break;}}
        else arm=Target(b,z,true)!=nullptr;
        if(!arm)return;a.phase=100;
    }else if(c.ability=="magnet_steal"){
        if(z->mMindControlled && a.storedSun){b->AddSunMoney(a.storedSun);a.storedSun=0;}
        int budget=std::min(c.magnitude,300-a.storedSun),taken=0;
        for(Coin* coin:b->mCoins){
            if(taken>=3 || coin->mDead || !coin->IsSun() || coin->mIsBeingCollected || std::abs(coin->mPosX-z->mPosX)>220 || std::abs(coin->mPosY-z->mPosY)>160)continue;
            if(z->mMindControlled){coin->Collect();++taken;}
            else {int value=coin->GetSunValue();if(value<=0 || value>budget)continue;coin->Die();a.storedSun+=value;budget-=value;++taken;}
        }
    }else
    if(c.ability=="seed_jam") {
        if(!b->mSeedBank || !b->mSeedBank->mNumPackets)return;
        int index=a.uses%b->mSeedBank->mNumPackets;
        SeedPacket& p=b->mSeedBank->mSeedPackets[index];
        if(p.mPacketType==SEED_NONE)return;
        if(z->mMindControlled) {
            if(p.mRefreshing)p.mRefreshCounter=std::min(p.mRefreshTime,p.mRefreshCounter+c.magnitude);
        } else if(p.mRefreshing)p.mRefreshCounter=std::max(0,p.mRefreshCounter-c.magnitude);
        else if(p.mActive) {p.mRefreshTime=c.magnitude;p.mRefreshCounter=0;p.mRefreshing=true;p.mActive=false;}
    } else if(c.ability=="raise") {
        if(a.uses>=3 || b->mZombies.mSize>b->mZombies.mMaxSize-8)return; // six minions per summoner lifetime
        for(int i=0;i<2;++i) {
            int row=(z->mRow+i)%5;if(!b->RowCanHaveZombieType(row,ZOMBIE_NORMAL))continue;
            Zombie* child=b->AddZombieInRow(ZOMBIE_NORMAL,row,z->mFromWave);
            if(!child)continue;
            child->mPosX=std::min(940.0f,z->mPosX+35+i*22);child->mPosY=child->GetPosYBasedOnRow(row);
            child->mX=int(child->mPosX);child->mY=int(child->mPosY);
            if(z->mMindControlled)child->StartMindControlled();
        }
    } else if(c.ability=="heal") {
        for(Zombie* other:b->mZombies)
            if(!other->IsDeadOrDying() && other->mMindControlled==z->mMindControlled && other->mZombieType!=ZOMBIE_BOSS &&
               std::abs(other->mPosX-z->mPosX)<180 && std::abs(other->mRow-z->mRow)<=1)
                other->mBodyHealth=std::min(other->mBodyMaxHealth,other->mBodyHealth+c.magnitude);
    } else if(c.ability=="bite") {
        if(!z->mMindControlled) {
            if(Plant* p=Target(b,z,true)){Hurt(p,c.magnitude);z->mBodyHealth=std::min(z->mBodyMaxHealth,z->mBodyHealth+c.magnitude/4);}
        } else {
            for(Zombie* other:b->mZombies)if(!other->IsDeadOrDying() && !other->mMindControlled && other->mRow==z->mRow && std::abs(other->mPosX-z->mPosX)<105){other->TakeDamage(c.magnitude,0);break;}
        }
    } else Fire(b,z,c);
    a.uses=std::min(20000000,a.uses+1);a.flash=30;
}
void Put(std::vector<uint8_t>& b,uint32_t n){for(int i=0;i<4;++i)b.push_back(uint8_t(n>>(8*i)));}
void PutString(std::vector<uint8_t>& b,const std::string& s){Put(b,uint32_t(s.size()));b.insert(b.end(),s.begin(),s.end());}
struct Reader {
    const std::vector<uint8_t>& data;size_t at=0;bool good=true;
    uint32_t Get(){if(data.size()-at<4){good=false;return 0;}uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(data[at++])<<(8*i);return n;}
    std::string String(size_t max){uint32_t n=Get();if(!good || n>max || n>data.size()-at){good=false;return {};}std::string s(data.begin()+at,data.begin()+at+n);at+=n;return s;}
};
}
namespace PvzpCustomZombies {
bool Exists(std::string_view id) { return Definition(id) || Base(id)!=ZOMBIE_INVALID; }
bool CanSpawn(Board* b,std::string_view id,int row) {
    if(!b || row< -1 || row>=MAX_GRID_SIZE_Y)return false;
    auto* c=Definition(id);ZombieType base=Base(c?c->base:id);
    if(base==ZOMBIE_INVALID)return false;
    if(base==ZOMBIE_BOSS)for(Zombie* z:b->mZombies)if(!z->IsDeadOrDying() && z->mZombieType==ZOMBIE_BOSS)return false;
    if(row>=0)return b->RowCanHaveZombieType(row,base);
    for(int r=0;r<MAX_GRID_SIZE_Y;++r)if(b->RowCanHaveZombieType(r,base))return true;
    return false;
}
Zombie* Spawn(Board* b,std::string_view id,int row) {
    if(!CanSpawn(b,id,row) || b->mZombies.mSize>=b->mZombies.mMaxSize-5)return nullptr;
    const auto* c=Definition(id);ZombieType base=Base(c?c->base:id);
    if(row<0){std::vector<int> rows;for(int r=0;r<MAX_GRID_SIZE_Y;++r)if(b->RowCanHaveZombieType(r,base))rows.push_back(r);row=rows[Rand(int(rows.size()))];}
    Zombie* z=b->AddZombieInRow(base,row,Zombie::ZOMBIE_WAVE_DEBUG);if(!z || !c)return z;
    z->mBodyHealth=z->mBodyMaxHealth=c->health;
    if(c->armor>0){z->mHelmType=HELMTYPE_PAIL;z->mHelmHealth=z->mHelmMaxHealth=c->armor;}
    boards[b].actors[b->mZombies.DataArrayGetID(z)]={c->id,c->ability=="rebound"?0:c->interval,0,0};
    return z;
}
std::string Id(const Zombie* z){const Actor* a=Find(z);return a?a->id:std::string{};}
std::string Name(const Zombie* z){const auto* c=Config(z);return c?c->name:std::string{};}
int Int(const Zombie* z,std::string_view field,int fallback) {
    if(!z)return fallback;
    const auto* c=Config(z);
    if(field=="speedPercent"){
        int value=c?c->speedPercent:fallback;
        if(z && z->mBoard){auto b=boards.find(z->mBoard);if(b!=boards.end()){
            auto buff=b->second.haste.find(z->mBoard->mZombies.DataArrayGetID(const_cast<Zombie*>(z)));
            if(buff!=b->second.haste.end())value=value*(100+buff->second.percent)/100;
        }}
        return std::clamp(value,1,600);
    }
    if(!c)return fallback;
    if(field=="damagePercent")return c->damagePercent;
    if(field=="slowResistancePercent")return c->slowResistancePercent;
    if(field=="sunDrop"){const auto* a=Find(z);return std::min(1000,c->sunDrop+(a?a->storedSun:0));}
    if(field=="tintR")return c->tintR;
    if(field=="tintG")return c->tintG;
    if(field=="tintB")return c->tintB;
    return fallback;
}
int RedirectDamage(Zombie* victim,int damage,unsigned int flags){
    if(!victim || damage<=0 || victim->mZombieType==ZOMBIE_BOSS)return damage;
    const auto* own=Config(victim);if(own && own->ability=="shield_aura")return damage;
    auto found=boards.find(victim->mBoard);if(found==boards.end())return damage;
    Zombie* guardian=nullptr;float closest=171;
    for(auto& [id,actor]:found->second.actors){
        const auto* c=Definition(actor.id);if(!c || c->ability!="shield_aura")continue;
        Zombie* candidate=victim->mBoard->mZombies.DataArrayTryToGet(id);
        if(!Active(candidate) || candidate->mHelmHealth<=0 || candidate->mMindControlled!=victim->mMindControlled || std::abs(candidate->mRow-victim->mRow)>1)continue;
        float d=std::abs(candidate->mPosX-victim->mPosX);if(d<closest){closest=d;guardian=candidate;}
    }
    if(!guardian)return damage;
    int percent=std::clamp(Config(guardian)->magnitude,1,60);
    int share=damage/100*percent+(damage%100)*percent/100;if(share<=0)return damage;
    if(auto* a=Find(guardian))a->flash=30;
    guardian->TakeDamage(share,flags); // guardians never redirect their own hits
    return damage-share;
}
bool ReflectProjectile(Projectile* p,Zombie* z){
    if(!p || !Active(z) || z->mMindControlled || z->mHelmHealth<=0 || p->IsElectric() || p->mMotionType==MOTION_LOBBED || PvzpPrism::IsRefracted(p) ||
       (p->mProjectileType!=PROJECTILE_PEA && p->mProjectileType!=PROJECTILE_SNOWPEA))return false;
    auto* a=Find(z);const auto* c=Config(z);if(!a || !c || c->ability!="rebound" || a->cooldown>0)return false;
    Board* b=z->mBoard;if(b->mProjectiles.mSize>=b->mProjectiles.mMaxSize-4)return false;
    Projectile* shot=b->AddProjectile(int(z->mPosX)-8,int(p->mPosY),z->mRenderOrder+1,z->mRow,PROJECTILE_ZOMBIE_PEA);
    if(!shot)return false;
    shot->mMotionType=MOTION_BACKWARDS;shot->mShadowY=p->mShadowY;
    boards[b].shots[b->mProjectiles.DataArrayGetID(shot)]={c->magnitude,p->mProjectileType==PROJECTILE_SNOWPEA?90:0,"rebound"};
    a->cooldown=c->interval;a->flash=30;a->uses=std::min(20000000,a->uses+1);
    z->TakeDamage(p->GetProjectileDef().mDamage,0); // each reflection wears its finite armor
    p->Die();return true;
}
void FreezeZombie(Zombie* z,int ticks) {
    if(!z || z->IsDeadOrDying())return;
    z->ApplyChill(true);
    if(!z->CanBeFrozen())return;
    const int resistance=Int(z,"slowResistancePercent",PvzpMod::ZombieInt(GetZombieDefinition(z->mZombieType).mZombieName,"slowResistancePercent",0));
    int duration=std::max(1,std::clamp(ticks,1,60000)*(100-resistance)/100);
    z->mIceTrapCounter=std::max(z->mIceTrapCounter,duration);
    z->StopZombieSound();z->UpdateAnimSpeed();
}
void Update(Board* b) {
    if(!b || !b->CanUseModTools())return;
    auto found=boards.find(b);if(found==boards.end())return;State& state=found->second;
    for(auto it=state.haste.begin();it!=state.haste.end();){
        Zombie* z=b->mZombies.DataArrayTryToGet(it->first);
        if(!z || z->IsDeadOrDying() || --it->second.ticks<=0)it=state.haste.erase(it);else ++it;
    }
    for(auto it=state.frozen.begin();it!=state.frozen.end();) {
        Plant* p=b->mPlants.DataArrayTryToGet(it->first);
        if(!p || p->mDead || --it->second<=0)it=state.frozen.erase(it);else ++it;
    }
    for(auto it=state.shots.begin();it!=state.shots.end();) {
        Projectile* p=b->mProjectiles.DataArrayTryToGet(it->first);
        if(!p || p->mDead)it=state.shots.erase(it);else ++it;
    }
    for(auto it=state.actors.begin();it!=state.actors.end();) {
        Zombie* z=b->mZombies.DataArrayTryToGet(it->first);
        if(!z || z->mDead){it=state.actors.erase(it);continue;}
        const auto* c=Definition(it->second.id);
        if(c && !z->IsDeadOrDying() && z->mHasHead && z->mIceTrapCounter==0 && z->mButteredCounter==0) {
            if(it->second.flash>0)--it->second.flash;
            Actor& actor=it->second;
            if(actor.phase>0){if(--actor.phase==0)ResolveWindup(b,z,actor,*c);}
            else if(c->ability=="rebound")actor.cooldown=std::max(0,actor.cooldown-1);
            else if(--actor.cooldown<=0){actor.cooldown=c->interval;Ability(b,z,actor,*c);}
        }
        ++it;
    }
}
bool PlantFrozen(const Plant* p) {
    if(!p || !p->mBoard || p->mDead || p->mPlantHealth<=0)return false;
    auto b=boards.find(p->mBoard);if(b==boards.end())return false;
    return b->second.frozen.contains(p->mBoard->mPlants.DataArrayGetID(const_cast<Plant*>(p)));
}
void PrepareVisuals(Zombie* z) {
    if(!Config(z) || !z->mHasHead || z->mDead || z->IsDeadOrDying())return;
    auto* body=z->mApp->ReanimationTryToGet(z->mBodyReanimID);if(!body)return;
    for(const char* track:{"anim_head","anim_hair","anim_tongue","anim_bucket","anim_cone"})
        body->AssignRenderGroupToPrefix(track,RENDER_GROUP_HIDDEN);
}
void Draw(Sexy::Graphics* g,Zombie* z) {
    const auto* c=Config(z);if(!c || !z->mHasHead || z->mDead || z->IsDeadOrDying())return;
    auto* body=z->mApp->ReanimationTryToGet(z->mBodyReanimID);if(!body)return;
    float x=35,y=28;
    if(body->TrackExists("anim_head1")){float gx,gy;z->GetTrackPosition("anim_head1",gx,gy);x=gx-z->mPosX+22;y=gy-z->mPosY+25;}
    PrepareVisuals(z);
    Sexy::Graphics local(*g);local.SetColorizeImages(true);
    Color color(c->tintR,c->tintG,c->tintB);
    if(z->mMindControlled)color=Color(180,255,170);else if(z->mChilledCounter>0 || z->mIceTrapCounter>0)color=Color(150,170,255);
    local.SetColor(color);
    const int hp=z->mHelmMaxHealth>0?z->mHelmHealth:z->mBodyHealth,maxhp=z->mHelmMaxHealth>0?z->mHelmMaxHealth:z->mBodyMaxHealth;
    if(c->head=="tombstone") {
        if(IMAGE_TOMBSTONES)local.DrawImageCel(IMAGE_TOMBSTONES,Rect(int(x)-27,int(y)-38,55,70),maxhp>0 && hp<maxhp/3?2:0);
    } else if(c->head=="seed_packet") {
        if(IMAGE_SEEDPACKET_LARGER)local.DrawImageCel(IMAGE_SEEDPACKET_LARGER,Rect(int(x)-24,int(y)-35,48,70),0,0);
        Sexy::Graphics icon(local);icon.SetScale(0.45f,0.45f,0,0);
        Plant::DrawSeedType(&icon,SEED_PEASHOOTER,SEED_NONE,VARIATION_NORMAL,x-16,y-20);
    } else {
        Sexy::Graphics icon(local);icon.SetScale(z->mMindControlled?0.72f:-0.72f,0.72f,0,0);
        Plant::DrawSeedType(&icon,HeadSeed(c->head),SEED_NONE,VARIATION_NORMAL,x-28+(z->mMindControlled?0.0f:57.6f),y-30);
    }
    if(maxhp>0 && hp<maxhp){local.SetColor(Color(30,20,20));local.FillRect(int(x)-23,int(y)-43,46,4);local.SetColor(Color(135,225,150));local.FillRect(int(x)-23,int(y)-43,std::max(0,hp)*46/maxhp,4);}
    const Actor* actor=Find(z);if(!actor)return;
    local.SetColorizeImages(false);
    auto ring=[&](float cx,float cy,float rx,float ry,Color color){local.SetColor(color);for(int i=0;i<24;++i){float a=i*6.2831853f/24,b=(i+1)*6.2831853f/24;local.DrawLine(int(cx+std::cos(a)*rx),int(cy+std::sin(a)*ry),int(cx+std::cos(b)*rx),int(cy+std::sin(b)*ry));}};
    if(c->ability=="shield_aura" && z->mHelmHealth>0 && Active(z))ring(x,y,39,45,Color(255,200,80,actor->flash?220:120));
    if(c->ability=="rebound" && z->mHelmHealth>0 && Active(z))ring(x,y,38,44,actor->cooldown==0?Color(120,205,255,200):Color(100,110,130,100));
    if(c->ability=="rally" && actor->flash)ring(x,y+75,70,22,Color(255,220,90,180));
    if(actor->phase>0){
        if(c->ability=="lane_dash" && actor->targetRow>=0){
            float dy=z->mBoard->GetPosYBasedOnRow(z->mPosX,actor->targetRow)-z->mBoard->GetPosYBasedOnRow(z->mPosX,z->mRow);
            int end=int(y+80+dy),sign=dy>0?1:-1;local.SetColor(Color(150,235,255));
            local.DrawLine(int(x),int(y+80),int(x),end);local.DrawLine(int(x),end,int(x)-8,end-sign*10);local.DrawLine(int(x),end,int(x)+8,end-sign*10);
        }else if(c->ability=="cherry_charge"){
            ring(x,y,42+(100-actor->phase)/10.0f,42+(100-actor->phase)/10.0f,actor->phase%20<10?Color(255,70,70,220):Color(255,230,120,220));
            PvzpDrawString(&local,"1s",int(x),int(y)-51,FONT_BRIANNETOD12,Color(255,100,100),DS_ALIGN_CENTER);
        }
    }
    if(c->ability=="magnet_steal" && actor->storedSun>0)PvzpDrawString(&local,"+"+std::to_string(actor->storedSun),int(x),int(y)-51,FONT_BRIANNETOD12,Color(255,225,80),DS_ALIGN_CENTER);
}
bool DrawProjectile(Sexy::Graphics* g,Projectile* p) {
    if(!p || !p->mBoard)return false;
    auto b=boards.find(p->mBoard);if(b==boards.end())return false;
    auto s=b->second.shots.find(p->mBoard->mProjectiles.DataArrayGetID(p));if(s==b->second.shots.end())return false;
    Sexy::Graphics local(*g);local.SetColorizeImages(true);
    local.SetColor(s->second.kind=="ice"?Color(135,220,255):s->second.kind=="fire"?Color(255,110,65):Color(150,235,125));
    Image* image=s->second.kind=="melon"?IMAGE_REANIM_MELONPULT_MELON:IMAGE_PROJECTILEPEA;
    local.DrawImageCel(image,Rect(0,0,s->second.kind=="melon"?36:24,s->second.kind=="melon"?32:24),0,0);return true;
}
bool HitPlant(Projectile* p,Plant* plant) {
    auto b=boards.find(p->mBoard);if(b==boards.end())return false;
    auto s=b->second.shots.find(p->mBoard->mProjectiles.DataArrayGetID(p));if(s==b->second.shots.end())return false;
    const Shot shot=s->second;Hurt(plant,shot.damage);
    if(shot.freeze && !plant->mDead)b->second.frozen[p->mBoard->mPlants.DataArrayGetID(plant)]=shot.freeze;
    if(shot.kind=="melon")for(Plant* other:p->mBoard->mPlants)if(other!=plant && !other->mDead && other->mRow==plant->mRow && std::abs(other->mX-plant->mX)<=85)Hurt(other,shot.damage/2);
    return true;
}
void Forget(Projectile* p){if(!p || !p->mBoard)return;auto b=boards.find(p->mBoard);if(b!=boards.end())b->second.shots.erase(p->mBoard->mProjectiles.DataArrayGetID(p));}
void Reset(Board* b){boards.erase(b);}
std::vector<uint8_t> Save(Board* b) {
    std::vector<uint8_t> out;State empty;auto found=boards.find(b);State& s=found==boards.end()?empty:found->second;
    bool extended=!s.haste.empty();
    for(const auto& [id,a]:s.actors)extended=extended || a.phase>0 || a.targetRow!=-1 || a.storedSun>0;
    for(const auto& [id,shot]:s.shots)extended=extended || shot.kind=="rebound";
    Put(out,extended?2:1);
    uint32_t count=0;for(auto& [id,a]:s.actors){auto* z=b->mZombies.DataArrayTryToGet(id);if(z && !z->mDead)++count;}
    Put(out,count);for(auto& [id,a]:s.actors){auto* z=b->mZombies.DataArrayTryToGet(id);if(!z || z->mDead)continue;Put(out,id);PutString(out,a.id);Put(out,a.cooldown);Put(out,a.uses);Put(out,a.flash);if(extended){Put(out,a.phase);Put(out,uint32_t(a.targetRow));Put(out,a.storedSun);}}
    count=0;for(auto& [id,t]:s.frozen){auto* p=b->mPlants.DataArrayTryToGet(id);if(p && !p->mDead)++count;}Put(out,count);
    for(auto& [id,t]:s.frozen){auto* p=b->mPlants.DataArrayTryToGet(id);if(p && !p->mDead){Put(out,id);Put(out,t);}}
    count=0;for(auto& [id,a]:s.shots){auto* p=b->mProjectiles.DataArrayTryToGet(id);if(p && !p->mDead)++count;}Put(out,count);
    for(auto& [id,a]:s.shots){auto* p=b->mProjectiles.DataArrayTryToGet(id);if(p && !p->mDead){Put(out,id);Put(out,a.damage);Put(out,a.freeze);PutString(out,a.kind);}}
    if(extended){
        count=0;for(const auto& [id,h]:s.haste){auto* z=b->mZombies.DataArrayTryToGet(id);if(z && !z->IsDeadOrDying())++count;}Put(out,count);
        for(const auto& [id,h]:s.haste){auto* z=b->mZombies.DataArrayTryToGet(id);if(z && !z->IsDeadOrDying()){Put(out,id);Put(out,h.ticks);Put(out,h.percent);}}
    }
    return out.size()<=1024*1024?out:std::vector<uint8_t>{};
}
bool Load(Board* b,const std::vector<uint8_t>& data) {
    if(!b || data.size()<16 || data.size()>1024*1024)return false;
    Reader r{data};uint32_t version=r.Get();if(version!=1 && version!=2)return false;State next;uint32_t n=r.Get();if(n>uint32_t(b->mZombies.mMaxSize))return false;
    for(uint32_t i=0;i<n && r.good;++i){uint32_t id=r.Get();Actor a;a.id=r.String(96);a.cooldown=r.Get();a.uses=r.Get();a.flash=r.Get();if(version==2){a.phase=r.Get();uint32_t row=r.Get();if(row!=UINT32_MAX && row>4)return false;a.targetRow=row==UINT32_MAX?-1:int(row);a.storedSun=r.Get();}Zombie* z=b->mZombies.DataArrayTryToGet(id);
        if(!r.good || !z || z->mDead || !Definition(a.id) || Base(Definition(a.id)->base)!=z->mZombieType || a.cooldown<0 || a.cooldown>60000 || a.uses<0 || a.uses>20000000 || a.flash<0 || a.flash>30 || !next.actors.emplace(id,a).second)return false;
        const auto* c=Definition(a.id);
        if(a.phase<0 || a.phase>100 || a.storedSun<0 || a.storedSun>300 || a.storedSun%5 ||
           (a.storedSun && c->ability!="magnet_steal") ||
           (a.phase && c->ability!="lane_dash" && c->ability!="cherry_charge") ||
           (!a.phase && a.targetRow!=-1) ||
           (a.phase && c->ability=="cherry_charge" && a.targetRow!=-1) ||
           (a.phase && c->ability=="lane_dash" && (a.phase>80 || a.targetRow<0 || std::abs(a.targetRow-z->mRow)!=1)))return false;
    }
    n=r.Get();if(n>uint32_t(b->mPlants.mMaxSize))return false;
    for(uint32_t i=0;i<n && r.good;++i){uint32_t id=r.Get(),t=r.Get();Plant* p=b->mPlants.DataArrayTryToGet(id);if(!r.good || !p || p->mDead || !t || t>60000 || !next.frozen.emplace(id,int(t)).second)return false;}
    n=r.Get();if(n>uint32_t(b->mProjectiles.mMaxSize))return false;
    for(uint32_t i=0;i<n && r.good;++i){uint32_t id=r.Get(),damage=r.Get(),freeze=r.Get();std::string kind=r.String(8);Projectile* p=b->mProjectiles.DataArrayTryToGet(id);
        if(!r.good || !p || p->mDead || p->mProjectileType!=PROJECTILE_ZOMBIE_PEA || damage>1000 || !damage || freeze>60000 || (kind!="pea" && kind!="ice" && kind!="fire" && kind!="melon" && !(version==2 && kind=="rebound")) || !next.shots.emplace(id,Shot{int(damage),int(freeze),kind}).second)return false;}
    if(version==2){n=r.Get();if(n>uint32_t(b->mZombies.mMaxSize))return false;
        for(uint32_t i=0;i<n && r.good;++i){uint32_t id=r.Get(),ticks=r.Get(),percent=r.Get();auto* z=b->mZombies.DataArrayTryToGet(id);
            if(!r.good || !z || z->IsDeadOrDying() || !ticks || ticks>250 || !percent || percent>100 || !next.haste.emplace(id,Haste{int(ticks),int(percent)}).second)return false;}
    }
    if(!r.good || r.at!=data.size())return false;boards[b]=std::move(next);return true;
}
}
