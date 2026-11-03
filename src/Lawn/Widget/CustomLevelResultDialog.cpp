// SPDX-License-Identifier: LGPL-3.0-or-later
#include "CustomLevelResultDialog.h"
#include "CustomLevelDialog.h"
#include "../../PvzpLib/PvzpCustomLevels.h"
#include "../../PvzpLib/PvzpMod.h"
#include "../../PvzpLib/PvzpLocale.h"
#include "../../PvzpLib/PvzpUiStrings.h"
#include "../../LawnApp.h"
#include "../Board.h"
#include "../../Resources.h"
#include "widget/WidgetManager.h"
#include <algorithm>

namespace {
std::string Text(LawnApp* app,const char* key,const char* en,const char* zh){return PvzpCustomLevels::Text(app,key,en,zh);}
}
CustomLevelResultDialog::CustomLevelResultDialog(LawnApp* app,std::string id,bool won,bool error)
    : LawnDialog(app,PvzpCustomLevels::ResultDialogId,true,"","","",Sexy::Dialog::BUTTONS_NONE),
      mLevelId(std::move(id)),mWon(won),mError(error) {
    if(app->mBoard){auto s=PvzpCustomLevels::Inspect(app->mBoard);mWaves=s.nextWave;mTotalWaves=s.totalWaves;mSpawned=s.spawned;mTicks=s.ticks;mSun=app->mBoard->mSunMoney;}
    const auto& levels=PvzpMod::CustomLevels();
    for(size_t i=0;i<levels.size();++i)if(levels[i].id==mLevelId){
        mName=levels[i].name;mDescription=levels[i].description;
        if(i+1<levels.size())mNextId=levels[i+1].id;break;
    }
    mButtons[0]=MakeButton(Sexy::Dialog::ID_YES,this,"");
    mButtons[1]=MakeButton(NextLevel,this,"");
    mButtons[2]=MakeButton(SelectLevel,this,"");
    mButtons[3]=MakeButton(Sexy::Dialog::ID_NO,this,"");
    int bw=Sexy::IMAGE_DIALOG_TOPLEFT->mWidth+Sexy::IMAGE_DIALOG_TOPRIGHT->mWidth;
    int tw=Sexy::IMAGE_DIALOG_TOPMIDDLE->mWidth;
    int width=bw+std::max(1,(650-bw+tw-1)/tw)*tw;
    int bh=Sexy::IMAGE_DIALOG_TOPLEFT->mHeight+Sexy::IMAGE_DIALOG_BOTTOMLEFT->mHeight+45;
    int th=Sexy::IMAGE_DIALOG_CENTERLEFT->mHeight;
    int height=bh+std::max(0,(490-bh+th-1)/th)*th;
    Resize((BOARD_WIDTH-width)/2,(BOARD_HEIGHT-height)/2,width,height);
    int gap=16,buttonWidth=(mWidth-104-gap)/2,buttonHeight=Sexy::IMAGE_BUTTON_LEFT->mHeight;
    for(int i=0;i<4;++i)mButtons[i]->Resize(52+(i%2)*(buttonWidth+gap),mHeight-38-(2-i/2)*(buttonHeight+12),buttonWidth,buttonHeight);
    Refresh();
}
CustomLevelResultDialog::~CustomLevelResultDialog()=default;
void CustomLevelResultDialog::Refresh(){
    mLocaleGeneration=PvzpLocaleGetGeneration();
    mDialogHeader=mError?Text(mApp,"CUSTOM_LEVEL_ERROR","Level unavailable","关卡不可用"):
        mWon?Text(mApp,"CUSTOM_LEVEL_WON","Level Complete!","关卡完成！"):
        Text(mApp,"CUSTOM_LEVEL_LOST","Defense breached","防线被突破了");
    mButtons[0]->SetLabel(Text(mApp,"CUSTOM_LEVEL_RETRY","Retry","重试本关"));
    mButtons[1]->SetLabel(Text(mApp,"CUSTOM_LEVEL_NEXT_LEVEL","Next Level","下一关"));
    mButtons[2]->SetLabel(Text(mApp,"CUSTOM_LEVEL_RETURN_PICKER","Choose a Level","返回选关"));
    mButtons[3]->SetLabel(Text(mApp,"CUSTOM_LEVEL_MENU","Main Menu","主菜单"));
    mButtons[0]->SetDisabled(mName.empty());
    mButtons[1]->SetDisabled(!mWon || mNextId.empty());
}
void CustomLevelResultDialog::AddedToManager(Sexy::WidgetManager* manager){LawnDialog::AddedToManager(manager);for(auto& b:mButtons)AddWidget(b.get());}
void CustomLevelResultDialog::RemovedFromManager(Sexy::WidgetManager* manager){for(auto& b:mButtons)RemoveWidget(b.get());LawnDialog::RemovedFromManager(manager);}
void CustomLevelResultDialog::Update(){LawnDialog::Update();if(mLocaleGeneration!=PvzpLocaleGetGeneration())Refresh();}
void CustomLevelResultDialog::Draw(Sexy::Graphics* g){
    LawnDialog::Draw(g);const int left=52,width=mWidth-104;
    g->PushState();g->ClipRect(left,128,width,mHeight-290);
    PvzpDrawString(g,PvzpUiClip(PvzpUiText(mName),30),mWidth/2,151,Sexy::FONT_BRIANNETOD16,Color(255,230,145),DS_ALIGN_CENTER);
    std::string outcome=mError?Text(mApp,"CUSTOM_LEVEL_ERROR_BODY","Rules changed or a required enemy is missing. Your checkpoint has not been replaced.","规则已变化或缺少出怪定义，没有用错误状态覆盖原存档。"):
        mWon?Text(mApp,"CUSTOM_LEVEL_WON_BODY","Every wave defeated. Adventure and Survival progress is untouched.","已消灭所有波次！冒险与生存进度保持不变。"):
        Text(mApp,"CUSTOM_LEVEL_LOST_BODY","Adjust your defense and retry, or choose another level.","调整防线再试一次，也可以返回选关换个挑战。");
    g->SetFont(Sexy::FONT_BRIANNETOD12);g->WriteWordWrapped(Rect(left,171,width,46),outcome);
    const uint64_t seconds=mTicks/100;
    std::string stats=Text(mApp,"CUSTOM_RESULT_WAVES","Waves sent","已出波次")+": "+std::to_string(mWaves)+" / "+std::to_string(mTotalWaves)+"    "+
        Text(mApp,"CUSTOM_RESULT_TIME","Active time","有效用时")+": "+std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);
    PvzpDrawString(g,stats,left,234,Sexy::FONT_BRIANNETOD12,Color(255,246,205),DS_ALIGN_LEFT);
    g->SetColor(Color(55,45,30));g->FillRect(left,246,width,13);
    if(mTotalWaves>0){g->SetColor(mWon?Color(145,205,90):Color(220,145,75));g->FillRect(left+2,248,(width-4)*std::clamp(mWaves,0,mTotalWaves)/mTotalWaves,9);}
    std::string counts=Text(mApp,"CUSTOM_RESULT_SPAWNED","Enemies sent","累计出场")+": "+std::to_string(mSpawned)+"    "+Text(mApp,"CUSTOM_RESULT_SUN","Sun remaining","剩余阳光")+": "+std::to_string(mSun);
    PvzpDrawString(g,counts,left,284,Sexy::FONT_BRIANNETOD12,Color(255,246,205),DS_ALIGN_LEFT);
    g->SetColor(Color(220,208,170));g->WriteWordWrapped(Rect(left,301,width,45),PvzpUiText(mDescription));
    g->PopState();
}
Sexy::Rect CustomLevelResultDialog::ButtonRect(int id)const{for(const auto& b:mButtons)if(b->mId==id)return Rect(b->mX,b->mY,b->mWidth,b->mHeight);return {};}
void CustomLevelResultDialog::ButtonDepress(int id){
    bool valid=false;for(const auto& b:mButtons)if(b->mId==id && !b->mDisabled)valid=true;
    if(!valid)return;
    LawnApp* app=mApp;const std::string selected=id==NextLevel?mNextId:mLevelId;
    const bool play=id==Sexy::Dialog::ID_YES || id==NextLevel, picker=id==SelectLevel;
    app->KillDialog(mId);app->KillBoard();
    if(play && PvzpCustomLevels::Start(app,selected))return;
    app->ShowGameSelector();
    if(picker){PvzpCustomLevels::OpenPicker(app);auto* d=dynamic_cast<CustomLevelDialog*>(app->GetDialog(PvzpCustomLevels::PickerDialogId));if(d)d->SelectLevel(selected);}
}
void CustomLevelResultDialog::KeyDown(Sexy::KeyCode key){
    if(key==Sexy::KeyCode::KEYCODE_ESCAPE)ButtonDepress(SelectLevel);
    else if(key==Sexy::KeyCode::KEYCODE_RETURN || key==Sexy::KeyCode::KEYCODE_SPACE)ButtonDepress(Sexy::Dialog::ID_YES);
    else LawnDialog::KeyDown(key);
}
