// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpModUI.h"
#include "PvzpModTools.h"
#include "PvzpPlantFood.h"
#include "PvzpMod.h"
#include "PvzpCustomPlants.h"
#include "PvzpCustomZombies.h"
#include "../Lawn/Board.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "../Lawn/CursorObject.h"
#include "../Lawn/SeedPacket.h"
#include "../Lawn/System/PlayerInfo.h"
#include "PvzpStringFile.h"
#include "PvzpFoley.h"
#include "PvzpUiStrings.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace {
struct Fan { int volleys=0, delay=0; };
struct UIState {
 int page=0, seed=SEED_ELECTRIC_GATLINGPEA, zombie=0;
 bool sunLock=false, recharge=false;
 std::map<uint32_t,Fan> fans;
};
std::map<Board*,UIState> states;
void Text(Graphics* g, const std::string& text, int x, int y, Color c=Color(240,245,230)) {
 PvzpDrawString(g,PvzpUiText(text),x,y,FONT_DWARVENTODCRAFT12,c,DS_ALIGN_CENTER);
}
void Box(Graphics* g, const Rect& r, const std::string& label, bool selected=false) {
 g->SetColor(selected?Color(44,105,79,245):Color(27,38,48,240)); g->FillRect(r);
 g->SetColor(Color(107,161,143)); g->DrawRect(r); Text(g,label,r.mX+r.mWidth/2,r.mY+21);
}
void Ellipse(Graphics* g,int cx,int cy,int rx,int ry,Color c) {
 g->SetColor(c);
 for(int dy=-ry;dy<=ry;++dy){int dx=int(rx*std::sqrt(std::max(0.f,1.f-float(dy*dy)/float(ry*ry))));g->FillRect(cx-dx,cy+dy,2*dx+1,1);}
}
bool Cell(Board* b,int x,int y,int& col,int& row) {
 if(x<40||x>=760||y<70||y>=(b->StageHas6Rows()?590:580)) return false;
 col=b->PixelToGridX(x,y);row=b->PixelToGridY(x,y);
 if(col<0||col>=9||row<0||row>=(b->StageHas6Rows()?6:5)) return false;
 int top=b->GridToPixelY(col,row), left=b->GridToPixelX(col,row);
 return x>=left&&x<left+80&&y>=top&&y<top+(b->StageHas6Rows()?85:100);
}
Plant* Pick(Board* b,int col,int row){
 Plant* selected=nullptr;
 for(Plant* p:b->mPlants) if(!p->mDead&&!p->mSquished&&p->mRow==row&&p->mPlantCol==col) selected=p;
 return selected;
}
std::string Label(Board* b,int i){
 auto& s=states[b];
 if(s.page==0){
  switch(i){case 0:return "SUN +1000";case 1:return PvzpUiText("SUN LOCK: ")+PvzpUiText(s.sunLock?"ON":"OFF");case 2:return "FOOD x3";case 3:return PvzpUiText("RECHARGE: ")+PvzpUiText(s.recharge?"ON":"OFF");case 4:return "PLANT <";case 5:return "PLANT >";case 6:return "PLACE ANYWHERE";case 7:return "HEAL PLANTS";case 8:return "PAGE 1/4  >>";}
 }else if(s.page==1){
  switch(i){case 0:return "ZOMBIE <";case 1:return "ZOMBIE >";case 2:return "SPAWN MIDDLE";case 3:return "SPAWN ALL LANES";case 4:return "CLEAR ZOMBIES";case 5:return "CHILL ALL";case 6:return "NEXT WAVE";case 7:return "CLEAR BULLETS";case 8:return "PAGE 2/4  >>";}
 }else{
  if(s.page==2){switch(i){case 0:return "FINISH LEVEL";case 1:return "UNLOCK PROFILE";case 2:return "CANCEL TOOLS";case 3:return "FILL SEED BANK";case 4:return "PLANT: E-GATL";case 5:return "PLANT: SUNFLOWER";case 6:return "PLANT: WALLNUT";case 7:return "RESET FOOD TIMER";case 8:return "PAGE 3/4  >>";}}
  const auto& mods=PvzpMod::List();const auto& errors=PvzpMod::Diagnostics();
  int modErrors=0;const PvzpMod::Diagnostic* saveNotice=nullptr;const PvzpMod::Diagnostic* firstError=nullptr;
  for(const auto& d:errors){const bool isSave=d.folder.size()>=8&&d.folder.compare(d.folder.size()-8,8,"savegame")==0;
    if(isSave)saveNotice=&d;else{++modErrors;if(!firstError)firstError=&d;}}
  if(i==0)return PvzpUiText("LOADED: ")+std::to_string(mods.size())+PvzpUiText("  MOD ERRORS: ")+std::to_string(modErrors);
  if(i>=1&&i<=5){size_t n=size_t(i-1);return n<mods.size() ?
    PvzpUiText(mods[n].enabled?"ON":"OFF")+(mods[n].enabled?"  ":" ")+PvzpUiClip(mods[n].id,20) : "-";}
  if(i==6){if(firstError)return PvzpUiText("ERR: ")+PvzpUiClip(PvzpUiText(firstError->message),22);
    if(saveNotice)return "OLD SAVE RULES (SAFE)";return "NO LOAD ERRORS";}
  if(i==7)return "EDIT JSON / RESTART GAME";
  if(i==8)return "PAGE 4/4  >>";
 }
 return "";
}
bool DebugSeedAvailable(int seed)
{
	const auto type = static_cast<SeedType>(seed);
	return (type >= SEED_PEASHOOTER && type < SEED_IMITATER) ||
		type == SEED_ELECTRIC_GATLINGPEA || PvzpCustomPlants::IsSlotUsed(type);
}
void SeedStep(UIState& s,int dir)
{
	std::vector<int> aSeeds;
	// Imitater needs a copied seed type, which the debug placement tool does not supply.
	for (int i = int(SEED_PEASHOOTER); i < int(SEED_IMITATER); ++i) aSeeds.push_back(i);
	aSeeds.push_back(SEED_ELECTRIC_GATLINGPEA);
	for (int i = int(SEED_CUSTOM_FIRST); i <= int(SEED_CUSTOM_LAST); ++i)
		if (PvzpCustomPlants::IsSlotUsed(static_cast<SeedType>(i))) aSeeds.push_back(i);
	const int aCount = static_cast<int>(aSeeds.size());
	int aIndex = 0;
	for (int i = 0; i < aCount; ++i) if (aSeeds[i] == s.seed) { aIndex = i; break; }
	aIndex = (aIndex + dir + aCount) % aCount;
	s.seed = aSeeds[aIndex];
}
std::string DebugSeedName(int seed)
{
	const auto type = static_cast<SeedType>(seed);
	if (PvzpCustomPlants::IsCustom(type))
		for (const auto& plant : PvzpMod::CustomPlants())
			if (plant.slot == seed - int(SEED_CUSTOM_0)) return PvzpUiText(plant.name);
	return PvzpUiResourceName(GetPlantDefinition(type).mPlantName);
}
constexpr ZombieType spawnKinds[]={ZOMBIE_NORMAL,ZOMBIE_TRAFFIC_CONE,ZOMBIE_PAIL,ZOMBIE_NEWSPAPER,ZOMBIE_FOOTBALL,ZOMBIE_BALLOON,ZOMBIE_SNORKEL,ZOMBIE_GARGANTUAR};
int ZombieChoiceCount(){return int(std::size(spawnKinds))+int(PvzpMod::CustomZombies().size());}
std::string ZombieChoiceName(int index){
 if(index>=int(std::size(spawnKinds))){const auto& all=PvzpMod::CustomZombies();size_t n=index-std::size(spawnKinds);return n<all.size()?PvzpUiText(all[n].name):"-";}
 return PvzpUiResourceName(GetZombieDefinition(spawnKinds[std::clamp(index,0,int(std::size(spawnKinds))-1)]).mZombieName);
}
void Spawn(Board* b,int row){
 auto& s=states[b];s.zombie=std::clamp(s.zombie,0,ZombieChoiceCount()-1);
 if(s.zombie>=int(std::size(spawnKinds)))PvzpCustomZombies::Spawn(b,PvzpMod::CustomZombies()[s.zombie-std::size(spawnKinds)].id,row);
 else PvzpCustomZombies::Spawn(b,GetZombieDefinition(spawnKinds[s.zombie]).mZombieName,row);
}
void Put(std::vector<uint8_t>& out,uint32_t v){for(int i=0;i<4;++i)out.push_back(uint8_t(v>>(8*i)));}
bool Get(const std::vector<uint8_t>& bytes,size_t& p,uint32_t& v){if(p+4>bytes.size())return false;v=0;for(int i=0;i<4;++i)v|=uint32_t(bytes[p++])<<(8*i);return true;}
}

bool Board::CanUseModTools() const {
 return mApp&&mApp->mGameScene==SCENE_PLAYING&&!mPaused&&mTimeStopCounter==0&&
 mBoardFadeOutCounter<0&&mApp->GetDialogCount()==0&&
 mApp->mGameMode!=GAMEMODE_CHALLENGE_ZEN_GARDEN&&mApp->mGameMode!=GAMEMODE_TREE_OF_WISDOM;
}
void Board::ResetModGameplay(){
 states.erase(this); PvzpModTools::Reset(this);PvzpPlantFood::Reset(this);ResetElectricProjectiles(this);PvzpCustomPlants::Reset(this);PvzpCustomZombies::Reset(this);
 mDbgOpen=false;mDbgPlantPending=false;mPlantFoodSelecting=false;mDbgConsumeMouseUp=false;
 mPlantFoodCount=1;mPlantFoodTimer=0;mFanVolleys=0;mFanTicks=0;mFanPlantID=0xFFFFFFFFU;
}
void Board::UpdateModGameplay(){
 if(!CanUseModTools())return;
 if(mPlantFoodCount<3){if(++mPlantFoodTimer>=3000){mPlantFoodTimer=0;++mPlantFoodCount;}}else mPlantFoodTimer=0;
 auto& s=states[this];if(s.sunLock)mSunMoney=9990;if(s.recharge)mSeedBank->RefreshAllPackets();
 for(auto it=s.fans.begin();it!=s.fans.end();){
  Plant* p=mPlants.DataArrayTryToGet(it->first);
  if(!p||p->mDead||it->second.volleys<=0){it=s.fans.erase(it);continue;}
  if(--it->second.delay<=0){SpawnElectricFanVolley(p);--it->second.volleys;it->second.delay=12;}
  ++it;
 }
 PvzpPlantFood::Update(this);
}
Rect Board::GetPlantFoodRect(){return PvzpModTools::GetFoodRect(this);}
bool Board::IsPlantBoosted(uint32_t id){return PvzpPlantFood::IsBoosted(this,id);}
void Board::ApplyPlantFood(Plant* p){PvzpPlantFood::Apply(this,p);}
void Board::FireElectricFan(Plant* p){
 if(!p||p->mDead||!CanUseModTools())return;
 uint32_t id=mPlants.DataArrayGetID(p);auto& f=states[this].fans[id];
 if(f.volleys>0)return; // clicking repeatedly must not overflow the projectile pool
 SpawnElectricFanVolley(p);f={39,12};
}
void Board::SpawnElectricFanVolley(Plant* p){
 if(!p||p->mDead)return;
 int dx,dy;p->GetPeaHeadOffset(dx,dy);
 int x=p->mX+dx+34,y=p->mY+dy-33-(GetFlowerPotAt(p->mPlantCol,p->mRow)?5:0);
 constexpr unsigned flags=(1U<<DAMAGES_GROUND)|(1U<<DAMAGES_FLYING)|(1U<<DAMAGES_SUBMERGED)|(1U<<DAMAGES_OFF_GROUND);
 for(int ray=-4;ray<=4;++ray){
  if(mProjectiles.mSize+8>=mProjectiles.mMaxSize)break;
  float angle=DEG_TO_RAD(ray*9.f);
  auto* q=AddProjectile(x,y,p->mRenderOrder-1,p->mRow,PROJECTILE_PEA);
  q->mDamageRangeFlags=flags;q->mMotionType=MOTION_STAR;q->mVelX=std::cos(angle)*5.f;q->mVelY=std::sin(angle)*5.f;q->SetElectric();
 }
 mApp->PlayFoley(FOLEY_THROW);
}
bool Board::PlantFoodMouseDown(int x,int y){
 if(!CanUseModTools())return false;
 Rect r=GetPlantFoodRect();
 if(r.Contains(x,y)){
  if(mPlantFoodCount<=0){mApp->PlaySample(SOUND_BUZZER);return true;}
  bool select=!mPlantFoodSelecting;PvzpModTools::Cancel(this);ClearCursor();mDbgOpen=false;mDbgPlantPending=false;
  mPlantFoodSelecting=select;return true;
 }
 if(!mPlantFoodSelecting)return false;
 // UI comes before plant picking: cancelling a tool must never accidentally feed underneath.
 if(y<80||PvzpModTools::GetGloveRect(this).Contains(x,y)||PvzpModTools::GetDebugRect(this).Contains(x,y)){
  mPlantFoodSelecting=false;return false;
 }
 int col,row;
 if(Cell(this,x,y,col,row)&&mPlantFoodCount>0){
  Plant* p=Pick(this,col,row);
  if(p&&PvzpPlantFood::Apply(this,p)){--mPlantFoodCount;mPlantFoodSelecting=false;return true;}
 }
 mApp->PlaySample(SOUND_BUZZER);return true;
}
void Board::DrawPlantFood(Graphics* graphics){
 if(mApp->mGameScene!=SCENE_PLAYING)return;
 Graphics copy(*graphics);Graphics* g=&copy;Rect r=GetPlantFoodRect();Box(g,r,"",mPlantFoodSelecting);
 int cx=r.mX+20,cy=r.mY+17;
 Ellipse(g,cx,cy,11,15,mPlantFoodCount?Color(83,217,90):Color(76,87,78));
 Ellipse(g,cx-3,cy-4,6,10,mPlantFoodCount?Color(179,250,132):Color(98,108,99));
 g->SetColor(Color(225,255,203));g->DrawLine(cx-4,cy+8,cx+5,cy-10);g->DrawLine(cx,cy,cx+7,cy-1);
 Text(g,std::to_string(mPlantFoodCount),r.mX+34,r.mY+36);
 g->SetColor(Color(9,24,19));g->FillRect(r.mX,r.mY+r.mHeight+2,r.mWidth,5);
 g->SetColor(Color(138,235,132));g->FillRect(r.mX,r.mY+r.mHeight+2,mPlantFoodCount==3?r.mWidth:r.mWidth*mPlantFoodTimer/3000,5);
 if(mPlantFoodSelecting){Box(g,Rect(200,80,400,30),"FOOD: TAP PLANT / TAP BEAN TO CANCEL",true);}
 PvzpModTools::Draw(this,g);PvzpPlantFood::Draw(this,g);
}
namespace PvzpModUI {
Rect ActionRect(Board*,int i){return Rect(546,128+i*41,244,36);}
void UnlockProfile(LawnApp* a){
 if(!a||!a->mPlayerInfo)return;auto* p=a->mPlayerInfo;
 p->mFinishedAdventure=std::max(p->mFinishedAdventure,1U);p->mHasUnlockedMinigames=1;p->mHasUnlockedPuzzleMode=1;p->mHasUnlockedSurvivalMode=1;
 for(int i=STORE_ITEM_PLANT_GATLINGPEA;i<=STORE_ITEM_PLANT_IMITATER;++i)p->mPurchases[i]=std::max(p->mPurchases[i],1U);
 for(auto& r:p->mChallengeRecords)r=std::max(r,1U);
 p->mHasUsedCheatKeys=1;p->SaveDetails();a->WriteCurrentUserConfig();
}
}
void Board::DrawDbgPanel(Graphics* graphics){
 if(mApp->mGameScene!=SCENE_PLAYING)return;
 Graphics copy(*graphics);Graphics* g=&copy;Box(g,PvzpModTools::GetDebugRect(this),mDbgOpen?"DBG CLOSE":"DBG");
 auto& s=states[this];
 if(mDbgPlantPending)Box(g,Rect(160,80,430,30),"DEBUG PLACE: ANY CELL / DBG CANCEL",true);
 if(!mDbgOpen)return;
 g->SetColor(Color(13,23,34,246));g->FillRect(540,86,256,416);
 if(!DebugSeedAvailable(s.seed))s.seed=SEED_ELECTRIC_GATLINGPEA;
 std::string title=s.page==3?"MOD LOAD STATUS":s.page==1?ZombieChoiceName(s.zombie):DebugSeedName(s.seed);
 Text(g,title,668,108,Color(150,235,198));
 for(int i=0;i<9;++i)Box(g,PvzpModUI::ActionRect(this,i),Label(this,i));
}
bool Board::DbgMouseDown(int x,int y){
 if(!CanUseModTools())return false;
 auto& s=states[this];
 if(!DebugSeedAvailable(s.seed))s.seed=SEED_ELECTRIC_GATLINGPEA;
 if(PvzpModTools::GetDebugRect(this).Contains(x,y)){
  PvzpModTools::Cancel(this);mPlantFoodSelecting=false;ClearCursor();
  if(mDbgPlantPending)mDbgPlantPending=false;else mDbgOpen=!mDbgOpen;
  return true;
 }
 if(mDbgPlantPending){
  int col,row;if(Cell(this,x,y,col,row)&&mPlants.mSize+1<mPlants.mMaxSize){AddPlant(col,row,static_cast<SeedType>(s.seed));mDbgPlantPending=false;}
  else mApp->PlaySample(SOUND_BUZZER);
  return true;
 }
 if(!mDbgOpen)return false;
 if(!Rect(540,86,256,416).Contains(x,y)){mDbgOpen=false;return true;}
 for(int i=0;i<9;++i){if(!PvzpModUI::ActionRect(this,i).Contains(x,y))continue;
  if(i==8){s.page=(s.page+1)%4;return true;}
   if(s.page==3)return true; // read-only status; restart required to change gameplay rules
  if(s.page==0){switch(i){
   case 0:AddSunMoney(1000);break;case 1:s.sunLock=!s.sunLock;break;
   case 2:mPlantFoodCount=3;mPlantFoodTimer=0;break;case 3:s.recharge=!s.recharge;mSeedBank->RefreshAllPackets();break;
   case 4:SeedStep(s,-1);break;case 5:SeedStep(s,1);break;
   case 6:ClearCursor();mDbgOpen=false;mDbgPlantPending=true;break;
   case 7:for(Plant* p:mPlants)if(!p->mDead)p->mPlantHealth=std::max(p->mPlantHealth,p->mPlantMaxHealth);break;
  }}else if(s.page==1){switch(i){
   case 0:s.zombie=(s.zombie+ZombieChoiceCount()-1)%ZombieChoiceCount();break;case 1:s.zombie=(s.zombie+1)%ZombieChoiceCount();break;
   case 2:Spawn(this,2);break;
   case 3:for(int row=0;row<(StageHas6Rows()?6:5);++row)if(RowCanHaveZombies(row))Spawn(this,row);break;
   case 4:for(Zombie* z:mZombies)if(!z->mDead)z->DieNoLoot();break;
   case 5:for(Zombie* z:mZombies)if(!z->mDead&&!z->mMindControlled)z->ApplyChill(false);break;
   case 6:mZombieCountDown=1;if(mHugeWaveCountDown>0)mHugeWaveCountDown=1;break;
   case 7:for(Projectile* p:mProjectiles)if(!p->mDead)p->Die();break;
  }}else{switch(i){
   case 0:mLevelComplete=true;mDbgOpen=false;break;case 1:PvzpModUI::UnlockProfile(mApp);break;
   case 2:PvzpModTools::Cancel(this);mPlantFoodSelecting=false;mDbgPlantPending=false;ClearCursor();break;
   case 3:mSeedBank->RefreshAllPackets();break;case 4:s.seed=SEED_ELECTRIC_GATLINGPEA;break;case 5:s.seed=SEED_SUNFLOWER;break;case 6:s.seed=SEED_WALLNUT;break;
   case 7:mPlantFoodTimer=0;break;
  }}return true;
 }
 return true; // swallow gaps too: debug buttons must never click the lawn underneath
}
namespace PvzpModUI {
std::vector<uint8_t> Save(Board* b){
 std::vector<uint8_t> out;Put(out,1);Put(out,b->mPlantFoodCount);Put(out,b->mPlantFoodTimer);
 auto& fans=states[b].fans;Put(out,fans.size());for(auto [id,f]:fans){Put(out,id);Put(out,f.volleys);Put(out,f.delay);}
 auto skills=PvzpPlantFood::Save(b);Put(out,skills.size());out.insert(out.end(),skills.begin(),skills.end());
 auto electric=SaveElectricProjectiles(b);if(electric.empty())return {};Put(out,electric.size());out.insert(out.end(),electric.begin(),electric.end());return out;
}
bool Load(Board* b,const std::vector<uint8_t>& in){
 size_t p=0;uint32_t version,count,timer,n;if(!Get(in,p,version)||version!=1||!Get(in,p,count)||count>3||!Get(in,p,timer)||timer>=3000||!Get(in,p,n)||n>b->mPlants.mMaxSize)return false;
 std::map<uint32_t,Fan> fans;
 for(uint32_t i=0;i<n;++i){uint32_t id,v,d;if(!Get(in,p,id)||!Get(in,p,v)||v>39||!Get(in,p,d)||d>12)return false;Plant* q=b->mPlants.DataArrayTryToGet(id);if(!q||q->mDead||q->mSeedType!=SEED_ELECTRIC_GATLINGPEA||fans.contains(id))return false;fans[id]={int(v),int(d)};}
 uint32_t size;if(!Get(in,p,size)||size>65536||p+size>in.size())return false;
 std::vector<uint8_t> skills(in.begin()+p,in.begin()+p+size);p+=size;
 if(!Get(in,p,size)||size>8*1024*1024||p+size!=in.size())return false;
 std::vector<uint8_t> electric(in.begin()+p,in.end());
 auto previousElectric=SaveElectricProjectiles(b);
 if(!LoadElectricProjectiles(b,electric))return false;
 if(!PvzpPlantFood::Load(b,skills)){LoadElectricProjectiles(b,previousElectric);return false;}
 b->mPlantFoodCount=count;b->mPlantFoodTimer=timer;states[b].fans=std::move(fans);return true;
}
}
