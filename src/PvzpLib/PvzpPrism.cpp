// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpPrism.h"
#include "PvzpCustomPlants.h"
#include "../Lawn/Board.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Projectile.h"
#include "../Lawn/Zombie.h"
#include "../LawnApp.h"
#include "graphics/Graphics.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {
constexpr uint32_t SPLIT=1, PIERCE=2, FROST=4, FIRE=8;
struct State { Board* board=nullptr; uint32_t flags=0; std::set<uint32_t> hits; };
std::map<const Projectile*,State> states;
State* Find(const Projectile* p) {
    auto it=states.find(p); return it!=states.end() && it->second.board==p->mBoard ? &it->second : nullptr;
}
bool Friendly(const Projectile* p) {
    if(!p || p->mDead || !p->mBoard) return false;
    switch(p->mProjectileType) {
    case PROJECTILE_PEA: case PROJECTILE_SNOWPEA: case PROJECTILE_FIREBALL:
    case PROJECTILE_CABBAGE: case PROJECTILE_MELON: case PROJECTILE_WINTERMELON:
    case PROJECTILE_KERNEL: case PROJECTILE_BUTTER: case PROJECTILE_STAR:
    case PROJECTILE_SPIKE: case PROJECTILE_PUFF: return p->mMotionType!=MOTION_BACKWARDS && p->mMotionType!=MOTION_BEE_BACKWARDS;
    default:return false; // Never copy enemy peas, basketballs or the massive cob explosion.
    }
}
void Ring(Sexy::Graphics& g,int cx,int cy,int rx,int ry) {
    for(int i=0;i<32;++i) {
        float a=i*0.19634954f,b=(i+1)*0.19634954f;
        g.DrawLine(cx+int(std::cos(a)*rx),cy+int(std::sin(a)*ry),cx+int(std::cos(b)*rx),cy+int(std::sin(b)*ry));
    }
}
void Empower(Projectile* p,State& s) {
    s.flags|=PIERCE;
    if(p->mProjectileType==PROJECTILE_SNOWPEA || p->mProjectileType==PROJECTILE_WINTERMELON ||
       (p->mBoard->mProjectiles.DataArrayGetID(p)&1U)) s.flags|=FROST;
    else s.flags|=FIRE;
    // Preserve lobbed/electric type/physics. Only an ordinary non-electric pea
    // becomes a fire pea; snow peas and winter melons keep their cold identity.
    if((s.flags&FIRE) && p->mProjectileType==PROJECTILE_PEA && !p->IsElectric()) p->ConvertToFireball(-2);
}
void Clone(Projectile* source,int sign,const State& inherited) {
    Board* b=source->mBoard;
    if(b->mProjectiles.mSize>=b->mProjectiles.mMaxSize-2) return;
    ProjectileType type=source->mProjectileType;
    Projectile* p=b->AddProjectile(int(source->mPosX),int(source->mPosY),source->mRenderOrder,source->mRow,
        type==PROJECTILE_FIREBALL?PROJECTILE_PEA:type);
    if(!p) return;
    // Do not memcpy a projectile: attachments and per-object ownership are unique.
    p->mPosX=source->mPosX; p->mPosY=source->mPosY; p->mPosZ=source->mPosZ;
    p->mVelX=source->mVelX; p->mVelY=source->mVelY; p->mVelZ=source->mVelZ;
    p->mAccZ=source->mAccZ; p->mShadowY=source->mShadowY;
    p->mMotionType=source->mMotionType; p->mProjectileAge=source->mProjectileAge;
    p->mDamageRangeFlags=source->mDamageRangeFlags; p->mOnHighGround=source->mOnHighGround;
    p->mHitTorchwoodGridX=source->mHitTorchwoodGridX; p->mLastPortalX=source->mLastPortalX;
    p->mTargetZombieID=source->mTargetZombieID; p->mCobTargetX=source->mCobTargetX; p->mCobTargetRow=source->mCobTargetRow;
    p->mRotation=source->mRotation; p->mRotationSpeed=source->mRotationSpeed;
    if(source->mMotionType==MOTION_LOBBED) p->mVelY+=sign*0.45f;
    else {
        p->mMotionType=MOTION_STAR;
        float speed=source->mVelX>0.5f?source->mVelX:3.33f;
        p->mVelX=speed*0.985f; p->mVelY=source->mVelY+sign*0.58f;
    }
    p->mX=int(p->mPosX); p->mY=int(p->mPosY);
    if(type==PROJECTILE_FIREBALL) { p->mHitTorchwoodGridX=-1; p->ConvertToFireball(-2); }
    if(source->IsElectric()) p->SetElectric();
    State child=inherited; child.board=b; child.flags|=SPLIT; child.hits.clear();
    states[p]=std::move(child);
}
void Put(std::vector<uint8_t>& out,uint32_t n) { for(int i=0;i<4;++i)out.push_back(uint8_t(n>>(8*i))); }
uint32_t Get(const std::vector<uint8_t>& data,size_t& at) { uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(data[at++])<<(8*i);return n; }
}
namespace PvzpPrism {
void Reset(Board* b) { for(auto it=states.begin();it!=states.end();) if(it->second.board==b)it=states.erase(it);else ++it; }
void Forget(Projectile* p) { states.erase(p); }
void UpdateProjectile(Projectile* p) {
    if(!Friendly(p) || !p->mBoard->CanUseModTools()) return;
    // Test the ground-projected row, so a melon high in its arc can pass the
    // same field as a straight pea without snapping height/velocity to the ground.
    for(Plant* plant:p->mBoard->mPlants) {
        if(plant->mDead || plant->mSquished || plant->NotOnGround() || plant->mIsAsleep ||
           !PvzpCustomPlants::IsPrism(plant->mSeedType) || plant->mRow!=p->mRow) continue;
        float field=plant->mX+120.0f;
        if(p->mPosX<field-48.0f || p->mPosX>field+48.0f) continue;
        State& state=states[p]; state.board=p->mBoard;
        if(PvzpCustomPlants::IsEmpowered(plant) && !(state.flags&PIERCE)) Empower(p,state);
        if(!(state.flags&SPLIT)) {
            state.flags|=SPLIT; // descendants inherit this before they enter any field
            State copy=state;
            Clone(p,-1,copy); Clone(p,1,copy);
        }
    }
}
bool IsRefracted(const Projectile* p) { const State* s=Find(p); return s && (s->flags&SPLIT); }
bool CanHit(const Projectile* p,const Zombie* z) {
    const State* s=Find(p);
    if(!s || !(s->flags&PIERCE)) return true;
    return z && !z->mMindControlled && !s->hits.count(uint32_t(p->mBoard->ZombieGetID(const_cast<Zombie*>(z))));
}
void BeforeImpact(Projectile* p,Zombie* z) {
    State* s=Find(p); if(!s || !z || !(s->flags&PIERCE)) return;
    s->hits.insert(uint32_t(p->mBoard->ZombieGetID(z)));
    if(s->flags&FROST) {
        z->HitIceTrap();
        z->mIceTrapCounter=std::min(z->mIceTrapCounter,100);
    }
    if(s->flags&FIRE) {
        // Additional energy burn uses normal damage routing (helm/shield/traits)
        // and never bypasses a zombie's death/loot bookkeeping.
        z->TakeDamage(15,0);
    }
}
bool KeepAfterImpact(Projectile* p,Zombie* z) {
    State* s=Find(p);
    return s && z && (s->flags&PIERCE) && s->hits.size()<16 && p->mMotionType!=MOTION_LOBBED;
}
unsigned int DamageFlagsFor(const Projectile* p) {
    const State* s=Find(p); return s && (s->flags&FROST) ? 1U<<int(DamageFlags::DAMAGE_FREEZE) : 0U;
}
void DrawField(Sexy::Graphics* g,Plant* p,bool empowered) {
    Sexy::Graphics local(*g);
    const float t=(p->mBoard?p->mBoard->mMainCounter:0)*0.035f;
    Color c(int(150+90*std::sin(t)),int(170+80*std::sin(t+2.1f)),int(170+80*std::sin(t+4.2f)),empowered?210:145);
    local.SetColor(c);
    int offset=int(PlantDrawHeightOffset(p->mBoard,p,p->mSeedType,p->mPlantCol,p->mRow));
    Ring(local,120,offset+40,48,38);
    Ring(local,120,offset+40,42,32);
    if(empowered) { Ring(local,120,offset+40,52,42); local.DrawLine(72,offset+40,168,offset+40); }
    for(int i=0;i<4;++i) {
        float phase=t+i*1.5708f;
        int x=120+int(std::cos(phase)*46),y=offset+40+int(std::sin(phase)*34);
        local.FillRect(x-2,y-2,5,5);
    }
}
std::vector<uint8_t> Save(Board* b) {
    std::vector<uint8_t> out;Put(out,1);uint32_t count=0;
    for(auto& [p,s]:states) if(s.board==b && !p->mDead)++count;
    Put(out,count);
    for(auto& [p,s]:states) if(s.board==b && !p->mDead) {
        Put(out,b->mProjectiles.DataArrayGetID(const_cast<Projectile*>(p)));Put(out,s.flags);Put(out,uint32_t(s.hits.size()));
        for(uint32_t id:s.hits)Put(out,id);
        if(out.size()>1024*1024) return {};
    }
    return out;
}
bool Load(Board* b,const std::vector<uint8_t>& data) {
    if(!b || data.size()<8 || data.size()>1024*1024) return false;
    size_t at=0; if(Get(data,at)!=1)return false;uint32_t count=Get(data,at);
    if(count>uint32_t(b->mProjectiles.mMaxSize))return false;
    std::map<const Projectile*,State> restored;
    for(uint32_t i=0;i<count;++i) {
        if(data.size()-at<12)return false;
        uint32_t id=Get(data,at),flags=Get(data,at),hits=Get(data,at);
        Projectile* p=b->mProjectiles.DataArrayTryToGet(id);
        if(!p || p->mDead || !flags || (flags&~15U) || hits>512 || hits>(data.size()-at)/4 || restored.count(p))return false;
        State s; s.board=b;s.flags=flags;
        for(uint32_t j=0;j<hits;++j) if(!s.hits.insert(Get(data,at)).second)return false;
        restored.emplace(p,std::move(s));
    }
    if(at!=data.size())return false;
    Reset(b);states.insert(restored.begin(),restored.end());return true;
}
}
