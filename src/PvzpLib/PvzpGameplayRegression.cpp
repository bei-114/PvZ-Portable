// SPDX-License-Identifier: LGPL-3.0-or-later
// Opt-in destructive LIVE Board regression. Run PVZP_GAMEPLAY_TEST=1 in a
// disposable HOME and enter adventure 1-1. Never invoke Board::Update recursively.
#include "../Lawn/Board.h"
#include "../Lawn/Coin.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Projectile.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/Widget/GameButton.h"
#include "../LawnApp.h"
#include "PvzpModTools.h"
#include "PvzpModUI.h"
#include "PvzpPlantFood.h"
#include "widget/WidgetManager.h"
#include <SDL_log.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>
#include <vector>

namespace {
struct Test {
    Board* board = nullptr;
    int phase = 0, ticks = 0, checks = 0, failures = 0, index = 0, applied = 0;
    uint32_t plant = 0, target = 0;
    float shotZ = 0, shotVZ = 0;
    uint32_t shot = 0;
    int baselineHP = 0, tailHP = 0;
    bool smokeApplied = false;
    std::vector<uint32_t> plants, targets;
    std::unordered_set<uint32_t> shots;
    int originOne = 0, originThree = 0;
};
void Check(Test& t, bool pass, const char* name, long long a = 0, long long b = 0, long long c = 0) {
    ++t.checks; if (!pass) ++t.failures;
    SDL_Log("GTEST %s %s a=%lld b=%lld c=%lld phase=%d tick=%d", pass ? "PASS" : "FAIL", name, a,b,c,t.phase,t.ticks);
}
void Terrain(Board* b, BackgroundType background = BACKGROUND_1_DAY) {
    b->mBackground = background;
    for (int row=0;row<MAX_GRID_SIZE_Y;++row) {
        bool water = background == BACKGROUND_3_POOL && (row==2 || row==3);
        b->mPlantRow[row] = row==5 && background!=BACKGROUND_3_POOL ? PLANTROW_DIRT : water ? PLANTROW_POOL : PLANTROW_NORMAL;
        for (int col=0;col<MAX_GRID_SIZE_X;++col)
            b->mGridSquareType[col][row] = water ? GRIDSQUARE_POOL : GRIDSQUARE_GRASS;
    }
}
void ClearShots(Board* b) {
    for (Projectile* p:b->mProjectiles) if (!p->mDead) p->Die();
    b->ProcessDeleteQueue();
}
void Clear(Board* b) {
    b->Pause(false); b->ResetModGameplay(); b->ClearCursor();
    for (Plant* p:b->mPlants) if (!p->mDead) p->Die();
    for (Zombie* z:b->mZombies) if (!z->mDead) z->DieNoLoot();
    for (Projectile* p:b->mProjectiles) if (!p->mDead) p->Die();
    for (Coin* c:b->mCoins) if (!c->mDead) c->Die();
    for (GridItem* g:b->mGridItems) if (!g->mDead) g->GridItemDie();
    b->ProcessDeleteQueue(); ResetElectricProjectiles(b); Terrain(b);
    b->mIgnoreMouseUp=false; b->mDbgConsumeMouseUp=false;
}
void Pointer(Board* b, int x, int y) {
    b->mApp->mWidgetManager->mLastMouseX=x+b->mX;
    b->mApp->mWidgetManager->mLastMouseY=y+b->mY;
}
void Click(Board* b, const Rect& r) {
    int x=r.mX+r.mWidth/2,y=r.mY+r.mHeight/2;
    Pointer(b,x,y);b->MouseMove(x,y);b->MouseDown(x,y,1);b->MouseUp(x,y,1);
}
void Down(Board* b, Plant* p) {
    int x=p->mX+40,y=p->mY+40;Pointer(b,x,y);b->MouseMove(x,y);b->MouseDown(x,y,1);
}
void Up(Board* b,int x,int y) {
    Pointer(b,x,y);b->MouseDrag(x,y);b->MouseUp(x,y,1);
}
void Glove(Board* b) { if (!PvzpModTools::IsActive(b)) Click(b,PvzpModTools::GetGloveRect(b)); }
bool Overlap(Rect a,Rect b) {
    return a.mX<b.mX+b.mWidth && b.mX<a.mX+a.mWidth && a.mY<b.mY+b.mHeight && b.mY<a.mY+a.mHeight;
}
Zombie* Target(Board* b,int row,float x,ZombieType type=ZOMBIE_NORMAL) {
    Zombie* z=b->AddZombieInRow(type,row,Zombie::ZOMBIE_WAVE_DEBUG);
    if(z){z->mPosX=x;z->mX=int(x);z->mBodyHealth=z->mBodyMaxHealth=100000;}
    return z;
}
Projectile* PeaAt(Board* b,Zombie* z,bool electric,unsigned flags) {
    Rect r=z->GetZombieRect();
    Projectile* p=b->AddProjectile(r.mX+2,r.mY+20,400000,z->mRow,PROJECTILE_PEA);
    if(p){p->mDamageRangeFlags=flags;if(electric)p->SetElectric();}
    return p;
}
int Suns(Board* b) { int n=0;for(Coin* c:b->mCoins)if(!c->mDead&&c->IsSun())++n;return n; }
void Record(Test& t,Board* b,bool origins=false) {
    for(Projectile* q:b->mProjectiles) if(q->IsElectric() && t.shots.insert(b->mProjectiles.DataArrayGetID(q)).second)
        if(origins){t.originOne+=q->mRow==1;t.originThree+=q->mRow==3;}
}
void Quiet(Board* b) { for(Plant* p:b->mPlants){p->mLaunchCounter=100000;p->mShootingCounter=0;} }
uint32_t U32(const std::vector<uint8_t>& bytes,size_t at) {
    if(at+4>bytes.size())return 0;
    uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(bytes[at+i])<<(8*i);return v;
}
void Put(std::vector<uint8_t>& bytes,size_t at,uint32_t v) {
    if(at+4>bytes.size())return;for(int i=0;i<4;++i)bytes[at+i]=uint8_t(v>>(8*i));
}
void Tools(Test& t,Board* b) {
    Rect glove=PvzpModTools::GetGloveRect(b),food=PvzpModTools::GetFoodRect(b),dbg=PvzpModTools::GetDebugRect(b);
    Rect menu(b->mMenuButton->mX,b->mMenuButton->mY,b->mMenuButton->mWidth,b->mMenuButton->mHeight);
    Check(t,!Overlap(glove,food)&&!Overlap(glove,dbg)&&!Overlap(food,dbg)&&!Overlap(glove,menu)&&!Overlap(food,menu)&&!Overlap(dbg,menu),"tool_rects_no_overlap",glove.mX,food.mX,dbg.mY);
    Check(t,dbg.mY>=menu.mY+menu.mHeight,"debug_below_menu",dbg.mY,menu.mY+menu.mHeight);
    Plant* p=b->AddPlant(1,2,SEED_WALLNUT);uint32_t id=b->mPlants.DataArrayGetID(p);
    p->mPlantHealth=1234;p->mStateCountdown=123;p->mLaunchCounter=321;
    Glove(b);Down(b,p);Pointer(b,400,420);b->MouseDrag(400,420);
    Check(t,p->mPlantCol==1&&p->mRow==2,"glove_preview_not_mutation",p->mPlantCol,p->mRow);
    Up(b,b->GridToPixelX(4,3)+40,b->GridToPixelY(4,3)+40);
    Check(t,b->mPlants.DataArrayTryToGet(id)==p&&p->mPlantCol==4&&p->mRow==3&&p->mPlantHealth==1234&&p->mStateCountdown==123&&p->mLaunchCounter==321,"glove_real_board_drag_preserves_identity_health_timers",p->mPlantHealth,p->mStateCountdown,p->mLaunchCounter);
    Down(b,p);Up(b,810,420);
    Check(t,p->mPlantCol==4&&p->mRow==3,"glove_outside_cancel",p->mPlantCol,p->mRow);
    Down(b,p);b->Pause(true);Up(b,200,200);b->Pause(false);
    Check(t,p->mPlantCol==4&&p->mRow==3,"glove_pause_cancel",p->mPlantCol,p->mRow);
    Plant* occupied=b->AddPlant(5,3,SEED_SUNFLOWER);uint32_t old=b->mPlants.DataArrayGetID(occupied);
    Glove(b);Down(b,p);Up(b,b->GridToPixelX(5,3)+40,b->GridToPixelY(5,3)+40);
    Check(t,p->mPlantCol==5&&p->mRow==3&&!occupied->mDead&&b->mPlants.DataArrayTryToGet(old)==occupied,"glove_stack_keeps_both",p->mPlantCol,p->mRow,b->mPlants.mSize);
    Check(t,!PvzpModTools::MovePlant(b,p,4,5)&&p->mPlantCol==5&&p->mRow==3,"glove_sixth_row_rejected_on_five_rows");
    PvzpModTools::Cancel(b);
}
void Food(Test& t,Board* b) {
    Check(t,b->mPlantFoodCount==1&&b->mPlantFoodTimer<=1,"food_initial_one",b->mPlantFoodCount,b->mPlantFoodTimer);
    b->mPlantFoodTimer=2999;b->UpdateModGameplay();
    Check(t,b->mPlantFoodCount==2&&b->mPlantFoodTimer==0,"food_charge_2999_to_two",b->mPlantFoodCount,b->mPlantFoodTimer);
    Rect r=b->GetPlantFoodRect();b->Pause(true);
    for(int i=0;i<100;++i)b->UpdateModGameplay();
    bool hit=b->PlantFoodMouseDown(r.mX+20,r.mY+20);
    Check(t,b->mPlantFoodCount==2&&b->mPlantFoodTimer==0&&!hit&&!b->mPlantFoodSelecting,"food_paused_update_and_input_frozen",b->mPlantFoodCount,b->mPlantFoodTimer,hit);
    b->Pause(false);b->mPlantFoodCount=0;b->mPlantFoodSelecting=false;
    hit=b->PlantFoodMouseDown(r.mX+20,r.mY+20);
    Check(t,hit&&b->mPlantFoodCount==0&&!b->mPlantFoodSelecting,"food_zero_stock_not_negative",b->mPlantFoodCount,hit);
    Plant* p=b->AddPlant(8,2,SEED_WALLNUT);p->mPlantHealth=100;int hp=p->mPlantHealth;
    b->mPlantFoodCount=1;b->PlantFoodMouseDown(r.mX+20,r.mY+20);b->PlantFoodMouseDown(810,p->mY+40);
    Check(t,b->mPlantFoodCount==1&&p->mPlantHealth==hp,"food_right_edge_does_not_feed_last_column",b->mPlantFoodCount,p->mPlantHealth);
}
void RegistryAndShield(Test& t,Board* b) {
    bool complete=PvzpPlantFood::SkillCount()==50;std::unordered_set<int> seen;
    for(size_t i=0;i<PvzpPlantFood::SkillCount();++i){auto* s=PvzpPlantFood::SkillAt(i);complete=complete&&s&&s->name&&*s->name&&s->description&&*s->description&&seen.insert(int(s->seed)).second&&PvzpPlantFood::Query(s->seed)==s;}
    for(int i=0;i<49;++i)complete=complete&&PvzpPlantFood::Query(SeedType(i));
    complete=complete&&PvzpPlantFood::Query(SEED_ELECTRIC_GATLINGPEA);
    Check(t,complete,"registry_fifty_explicit_unique_skills",PvzpPlantFood::SkillCount(),seen.size());
    Plant* sunflower=b->AddPlant(1,1,SEED_SUNFLOWER);int before=Suns(b),sunMoney=b->mSunMoney;
    bool applied=PvzpPlantFood::Apply(b,sunflower);
    Check(t,applied&&Suns(b)>=before+2&&b->mSunMoney==sunMoney,"sunflower_collectible_entities_not_account_credit",before,Suns(b),b->mSunMoney-sunMoney);
    Plant* nut=b->AddPlant(2,2,SEED_WALLNUT);uint32_t id=b->mPlants.DataArrayGetID(nut);nut->mPlantHealth=100;int maximum=nut->mPlantMaxHealth;
    bool first=PvzpPlantFood::Apply(b,nut),second=PvzpPlantFood::Apply(b,nut);
    int hp=nut->mPlantHealth,shield=PvzpPlantFood::ShieldHealth(b,id);
    auto bytes=PvzpPlantFood::Save(b);bool load1=PvzpPlantFood::Load(b,bytes),load2=PvzpPlantFood::Load(b,bytes);
    Check(t,first&&!second&&maximum==nut->mPlantMaxHealth&&shield>0&&shield<=2000&&load1&&load2&&nut->mPlantHealth==hp&&PvzpPlantFood::ShieldHealth(b,id)==shield,"wallnut_bounded_shield_max_health_and_load_idempotent",maximum,nut->mPlantHealth,shield);
}
void Collisions(Test& t,Board* b) {
    Clear(b);Terrain(b,BACKGROUND_3_POOL);
    Zombie* z=Target(b,2,450,ZOMBIE_SNORKEL);z->mInPool=true;z->mIsEating=false;z->mZombiePhase=PHASE_SNORKEL_WALKING_IN_POOL;
    unsigned submerged=(1U<<DAMAGES_GROUND)|(1U<<DAMAGES_SUBMERGED);
    Projectile* ordinary=PeaAt(b,z,false,submerged);Projectile* noFlag=PeaAt(b,z,true,1U<<DAMAGES_GROUND);Projectile* electric=PeaAt(b,z,true,submerged);
    bool oldGate=ordinary->FindCollisionTarget()==nullptr,flagGate=noFlag->FindCollisionTarget()==nullptr,found=electric->FindCollisionTarget()==z;int hp=z->mBodyHealth;
    electric->CheckForCollision();
    Check(t,oldGate&&flagGate&&found&&z->mBodyHealth<hp,"snorkel_actual_first_collision_electric_and_flag_only",oldGate,flagGate,hp-z->mBodyHealth);
    Clear(b);Zombie* balloon=Target(b,2,450,ZOMBIE_BALLOON);
    Projectile* low=PeaAt(b,balloon,true,1U<<DAMAGES_GROUND);Projectile* high=PeaAt(b,balloon,true,(1U<<DAMAGES_GROUND)|(1U<<DAMAGES_FLYING));
    bool flying=balloon->IsFlying(),blocked=low->FindCollisionTarget()==nullptr,hit=high->FindCollisionTarget()==balloon;hp=balloon->mBodyHealth;high->CheckForCollision();
    // A flying balloon is popped on the first hit (LandFlyer), not body-damaged.
    bool popped=balloon->mZombiePhase==PHASE_BALLOON_POPPING||balloon->mZombieHeight==HEIGHT_FALLING;
    Check(t,flying&&blocked&&hit&&popped,"balloon_flying_flag_real_first_collision",flying,blocked,popped?1:0);
    Clear(b);Terrain(b,BACKGROUND_5_ROOF);
    ordinary=b->AddProjectile(300,250,400000,2,PROJECTILE_PEA);electric=b->AddProjectile(300,250,400000,2,PROJECTILE_PEA);electric->SetElectric();ordinary->mShadowY=electric->mShadowY=270;
    ordinary->CheckForHighGround();electric->CheckForHighGround();
    Check(t,ordinary->mDead&&!electric->mDead&&!electric->CantHitHighGround(),"roof_ordinary_collides_electric_hovers",ordinary->mDead,electric->mDead);
    Clear(b);ordinary=b->AddProjectile(300,210,400000,2,PROJECTILE_PEA);electric=b->AddProjectile(300,210,400000,2,PROJECTILE_PEA);
    for(Projectile* p:{ordinary,electric}){p->mMotionType=MOTION_STAR;p->mVelX=0;p->mVelY=1;p->mShadowY=320;p->mDamageRangeFlags=0;}
    electric->SetElectric();ordinary->UpdateNormalMotion();electric->UpdateNormalMotion();
    Check(t,ordinary->mRow==1&&electric->mRow==2,"electric_star_uses_shadow_not_head_row",ordinary->mRow,electric->mRow);
}
void Saves(Test& t,Board* b) {
    Clear(b);Zombie* z=Target(b,2,440);Zombie* cross=Target(b,1,440);
    Projectile* p=PeaAt(b,z,true,1U<<DAMAGES_GROUND);p->CheckForCollision();
    uint32_t id=b->mProjectiles.DataArrayGetID(p);auto bytes=SaveElectricProjectiles(b);int hp=z->mBodyHealth,crossHP=cross->mBodyHealth;
    p->SetElectric(false);bool loaded=LoadElectricProjectiles(b,bytes);p->CheckForCollision();p->DoImpact(z);
    Check(t,loaded&&p->IsElectric()&&z->mBodyHealth==hp&&cross->mBodyHealth==crossHP&&b->mProjectiles.DataArrayGetID(p)==id,"electric_save_restores_lifetime_dedup",bytes.size(),hp-z->mBodyHealth,crossHP-cross->mBodyHealth);
    auto baseline=SaveElectricProjectiles(b);std::vector<std::vector<uint8_t>> bad;
    auto v=baseline;v[0]^=1;bad.push_back(v);v=baseline;v.pop_back();bad.push_back(v);v=baseline;v.push_back(0);bad.push_back(v);
    v=baseline;Put(v,8,b->mProjectiles.mMaxSize+1);bad.push_back(v);v=baseline;Put(v,16,2);bad.push_back(v);
    v=baseline;Put(v,12,id+0x10000U);bad.push_back(v);
    if(U32(baseline,20)>=2){v=baseline;Put(v,28,U32(v,24));bad.push_back(v);}
    v=baseline;Put(v,8,2);v.insert(v.end(),baseline.begin()+12,baseline.end());bad.push_back(v);
    bool rejected=true;for(auto& corrupt:bad)rejected=rejected&&!LoadElectricProjectiles(b,corrupt)&&SaveElectricProjectiles(b)==baseline;
    Check(t,rejected,"electric_corrupt_payload_rejected_without_mutation",bad.size(),baseline.size());
    Projectile* snow=b->AddProjectile(250,200,400000,1,PROJECTILE_SNOWPEA);
    loaded=LoadElectricProjectiles(b,baseline);
    Check(t,loaded&&!snow->IsElectric()&&snow->mProjectileType==PROJECTILE_SNOWPEA,"electric_load_does_not_convert_ordinary_cold_pea");
    Plant* nut=b->AddPlant(1,3,SEED_WALLNUT);PvzpPlantFood::Apply(b,nut);uint32_t plantID=b->mPlants.DataArrayGetID(nut);
    b->mPlantFoodCount=2;b->mPlantFoodTimer=123;hp=nut->mPlantHealth;int maximum=nut->mPlantMaxHealth,shield=PvzpPlantFood::ShieldHealth(b,plantID);
    auto ui=PvzpModUI::Save(b);b->mPlantFoodCount=0;b->mPlantFoodTimer=12;
    bool ui1=PvzpModUI::Load(b,ui),ui2=PvzpModUI::Load(b,ui);
    Check(t,ui1&&ui2&&b->mPlantFoodCount==2&&b->mPlantFoodTimer==123&&nut->mPlantHealth==hp&&nut->mPlantMaxHealth==maximum&&PvzpPlantFood::ShieldHealth(b,plantID)==shield&&p->IsElectric(),"modui_food_shield_electric_roundtrip_idempotent",b->mPlantFoodCount,b->mPlantFoodTimer,shield);
}
void SmokeFixture(Test& t,Board* b) {
    Clear(b);const auto* info=PvzpPlantFood::SkillAt(size_t(t.index));
    if(!info){t.smokeApplied=false;return;}
    if(info->seed==SEED_TANGLEKELP||info->seed==SEED_SEASHROOM||info->seed==SEED_LILYPAD||info->seed==SEED_CATTAIL)Terrain(b,BACKGROUND_3_POOL);
    if(info->seed==SEED_GRAVEBUSTER)b->AddAGraveStone(1,2);
    b->AddAGraveStone(2,2);b->AddACrater(2,1);
    if(info->seed==SEED_INSTANT_COFFEE)b->AddPlant(1,2,SEED_FUMESHROOM);
    Plant* p=b->AddPlant(1,2,info->seed,info->seed==SEED_IMITATER?SEED_PEASHOOTER:SEED_NONE);
    Target(b,2,330);Target(b,1,390);Target(b,2,490,ZOMBIE_PAIL);Target(b,3,420,ZOMBIE_BALLOON);
    if(b->StageHasPool())for(Zombie* z:b->mZombies)if(z->mRow==2){z->mInPool=true;z->mIsEating=false;}
    SDL_Log("GTEST SMOKE_BEGIN index=%d seed=%d name=%s (Apply + one live Board tick; NOT full semantic coverage)",t.index,int(info->seed),info->name);
    t.smokeApplied=PvzpPlantFood::Apply(b,p);if(t.smokeApplied)++t.applied;
}
}

void PvzpRunGameplayRegression(Board* b) {
#if defined(__linux__) && !defined(__ANDROID__)
    const char* enabled=std::getenv("PVZP_GAMEPLAY_TEST");
    if(!enabled||!*enabled||!b||!b->mApp||b->mApp->mGameScene!=SCENE_PLAYING)return;
    static Test t;if(t.board!=b){t=Test{};t.board=b;}
    if(t.phase<0)return;
    b->mZombieCountDown=100000;b->mZombieCountDownStart=100000;b->mHugeWaveCountDown=-1;b->mTutorialState=TUTORIAL_OFF;
    if(t.phase==0){
        if(b->mLevel!=1||!b->mApp->IsAdventureMode()){SDL_Log("GTEST WAIT requires adventure 1-1, level=%d",b->mLevel);t.phase=-1;return;}
        SDL_Log("GTEST BEGIN destructive isolated-HOME live Board test HOME=%s",std::getenv("HOME")?std::getenv("HOME"):"<unset>");
        Clear(b);t.phase=1;return;
    }
    if(t.phase==1){Tools(t,b);Clear(b);t.phase=2;return;}
    if(t.phase==2){Food(t,b);Clear(b);t.phase=3;return;}
    if(t.phase==3){RegistryAndShield(t,b);Clear(b);t.index=0;t.phase=4;return;}
    constexpr SeedType pults[]={SEED_CABBAGEPULT,SEED_KERNELPULT,SEED_MELONPULT,SEED_WINTERMELON};
    if(t.phase==4){
        Clear(b);Plant* p=b->AddPlant(1,2,pults[t.index]);Zombie* z=Target(b,2,550);
        bool applied=PvzpPlantFood::Apply(b,p);p->Fire(z,2,WEAPON_PRIMARY);Quiet(b);
        std::vector<Projectile*> shots;for(Projectile* q:b->mProjectiles)if(!q->mDead)shots.push_back(q);
        bool valid=applied&&shots.size()==2;
        for(Projectile* q:shots)valid=valid&&q->mMotionType==MOTION_LOBBED&&q->mVelX>0&&q->mVelZ<0&&q->mAccZ>0;
        if(shots.size()==2)valid=valid&&shots[0]->mVelZ==shots[1]->mVelZ&&shots[0]->mAccZ==shots[1]->mAccZ;
        Check(t,valid,"pult_primary_and_boost_real_lob_setup",int(pults[t.index]),shots.size());
        t.shot=shots.empty()?0:b->mProjectiles.DataArrayGetID(shots[0]);t.shotZ=shots.empty()?0:shots[0]->mPosZ;t.shotVZ=shots.empty()?0:shots[0]->mVelZ;t.phase=5;return;
    }
    if(t.phase==5){
        Projectile* q=b->mProjectiles.DataArrayTryToGet(t.shot);
        Check(t,q&&!q->mDead&&q->mPosZ<t.shotZ&&q->mVelZ>t.shotVZ,"pult_real_board_tick_advances_arc_and_gravity",int(pults[t.index]),q?int(q->mPosZ*100):0,q?int(q->mVelZ*100):0);
        if(++t.index<4){t.phase=4;return;}
        Clear(b);Plant* split=b->AddPlant(3,2,SEED_SPLITPEA);Zombie* back=Target(b,2,120);PvzpPlantFood::Apply(b,split);ClearShots(b);split->Fire(back,2,WEAPON_SECONDARY);
        int count=0;bool reverse=true;for(Projectile* p:b->mProjectiles)if(!p->mDead){++count;reverse=reverse&&p->mMotionType==MOTION_BACKWARDS;}
        Check(t,count==2&&reverse,"splitpea_boost_preserves_backward_motion",count);
        Clear(b);t.plants.clear();t.targets.clear();t.shots.clear();t.baselineHP=0;
        const SeedType kinds[]={SEED_FUMESHROOM,SEED_GLOOMSHROOM,SEED_STARFRUIT};
        for(int i=0;i<3;++i){int row=i*2;Plant* p=b->AddPlant(1+i,row,kinds[i]);p->SetSleeping(false);Zombie* z=Target(b,row,float(p->mX+(i==1?60:i==2?-100:150)));t.baselineHP+=z->mBodyHealth;t.plants.push_back(b->mPlants.DataArrayGetID(p));t.targets.push_back(b->mZombies.DataArrayGetID(z));Check(t,PvzpPlantFood::Apply(b,p),"timed_area_skill_armed",int(kinds[i]));}
        Quiet(b);t.ticks=0;t.phase=6;return;
    }
    if(t.phase==6){
        ++t.ticks;Quiet(b);int hp=0;
        for(size_t i=0;i<t.targets.size();++i)if(Zombie* z=b->mZombies.DataArrayTryToGet(t.targets[i])){hp+=z->mBodyHealth;Plant* p=b->mPlants.DataArrayTryToGet(t.plants[i]);if(p){z->mPosX=float(p->mX+(i==1?60:i==2?-100:150));z->mX=int(z->mPosX);}}
        for(Projectile* q:b->mProjectiles)if(q->mProjectileType==PROJECTILE_STAR)t.shots.insert(b->mProjectiles.DataArrayGetID(q));
        if(t.ticks==400){
            for(size_t i=0;i<t.targets.size();++i){Zombie* z=b->mZombies.DataArrayTryToGet(t.targets[i]);Check(t,z&&z->mBodyHealth<(i<2?99980:100000),i==0?"fume_repeated_live_damage":i==1?"gloom_repeated_live_damage":"star_real_projectile_damage",z?100000-z->mBodyHealth:0);}
            Check(t,PvzpPlantFood::ActiveCount(b)==0&&hp<t.baselineHP&&t.shots.size()>=40,"fume_gloom_star_live_effects_and_finite_duration",t.baselineHP-hp,t.shots.size(),PvzpPlantFood::ActiveCount(b));
        }
        if(t.ticks==600)t.tailHP=hp;
        if(t.ticks<650)return;
        Check(t,hp==t.tailHP&&PvzpPlantFood::ActiveCount(b)==0,"expired_area_skills_no_permanent_aura",t.tailHP,hp);t.phase=7;return;
    }
    if(t.phase==7){Collisions(t,b);t.phase=8;return;}
    if(t.phase==8){Saves(t,b);Clear(b);Plant* p=b->AddPlant(1,2,SEED_ELECTRIC_GATLINGPEA);Quiet(b);t.shots.clear();b->FireElectricFan(p);Record(t,b);t.ticks=0;t.phase=9;return;}
    if(t.phase==9){
        ++t.ticks;Quiet(b);Record(t,b);
        if(t.ticks<480)return;
        Check(t,t.shots.size()==360,"fan_real_sustained_board_scheduler_40x9",t.shots.size(),t.ticks);
        Clear(b);Plant* one=b->AddPlant(1,1,SEED_ELECTRIC_GATLINGPEA);Plant* three=b->AddPlant(1,3,SEED_ELECTRIC_GATLINGPEA);Quiet(b);t.shots.clear();t.originOne=t.originThree=0;
        b->FireElectricFan(one);b->FireElectricFan(three);Record(t,b,true);
        Plant* pausedOnly=b->AddPlant(1,2,SEED_ELECTRIC_GATLINGPEA);Quiet(b);
        auto frozen=PvzpModUI::Save(b);b->Pause(true);b->FireElectricFan(pausedOnly);for(int i=0;i<100;++i)b->UpdateModGameplay();
        auto after=PvzpModUI::Save(b);Check(t,frozen==after&&t.shots.size()==18,"fan_pause_does_not_advance_or_queue",t.shots.size());b->Pause(false);
        t.ticks=0;t.phase=10;return;
    }
    if(t.phase==10){
        ++t.ticks;Quiet(b);Record(t,b,true);if(t.ticks<480)return;
        Check(t,t.shots.size()==720&&t.originOne==360&&t.originThree==360,"two_concurrent_fans_do_not_overwrite",t.shots.size(),t.originOne,t.originThree);
        t.index=0;t.applied=0;t.phase=11;return;
    }
    if(t.phase==11){SmokeFixture(t,b);t.phase=12;return;}
    if(t.phase==12){
        const auto* skill=PvzpPlantFood::SkillAt(size_t(t.index));Check(t,t.smokeApplied,"registry_skill_apply_and_one_board_tick_smoke_only",skill?int(skill->seed):-1,t.index);
        if(++t.index<int(PvzpPlantFood::SkillCount())){t.phase=11;return;}
        Check(t,t.applied==50,"all_fifty_skills_apply_smoke_covered_not_full_semantics",t.applied,50);
        Clear(b);b->mPlantFoodCount=3;b->mSunMoney=9990;
        for(int row=0;row<5;++row){b->AddPlant(0,row,SEED_SUNFLOWER);b->AddPlant(2,row,SEED_ELECTRIC_GATLINGPEA);b->AddPlant(5,row,SEED_WALLNUT);}
        SDL_Log("GTEST %s done checks=%d failures=%d skills_applied=%d live_fan_ticks=960 area_ticks=650",t.failures?"FAIL":"PASS",t.checks,t.failures,t.applied);
        t.phase=-1;return;
    }
#else
    (void)b;
#endif
}
