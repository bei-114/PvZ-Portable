// SPDX-License-Identifier: LGPL-3.0-or-later
#include "LevelEditorDialog.h"
#include "../../PvzpLib/PvzpPlayerLevels.h"
#include "../../PvzpLib/PvzpCustomLevels.h"
#include "../../PvzpLib/PvzpLocale.h"
#include "../../PvzpLib/PvzpUiStrings.h"
#include "../../LawnApp.h"
#include "../../Resources.h"
#include "../../GameConstants.h"
#include "../LawnCommon.h"
#include "graphics/Graphics.h"
#include "graphics/Image.h"
#include "widget/WidgetManager.h"
#include <algorithm>

namespace {
std::string T(LawnApp* app, const char* key, const char* en, const char* zh)
{ return PvzpCustomLevels::Text(app,key,en,zh); }
void EditorSize(LawnDialog* dialog)
{
    const int bw=Sexy::IMAGE_DIALOG_TOPLEFT->mWidth+Sexy::IMAGE_DIALOG_TOPRIGHT->mWidth;
    const int tw=Sexy::IMAGE_DIALOG_TOPMIDDLE->mWidth;
    const int bh=Sexy::IMAGE_DIALOG_TOPLEFT->mHeight+Sexy::IMAGE_DIALOG_BOTTOMLEFT->mHeight+45;
    const int th=Sexy::IMAGE_DIALOG_CENTERLEFT->mHeight;
    const int w=bw+std::max(0,(BOARD_WIDTH-bw)/tw)*tw;
    const int h=bh+std::max(0,(BOARD_HEIGHT-bh)/th)*th;
    dialog->Resize((BOARD_WIDTH-w)/2,(BOARD_HEIGHT-h)/2,w,h);
}
const PvzpMod::CustomLevel* Source(std::string_view id)
{
    if (const auto* own=PvzpPlayerLevels::Find(id)) return own;
    for (const auto& level:PvzpMod::CustomLevels()) if (level.id==id) return &level;
    return nullptr;
}
}

void LevelEditorDialog::Open(LawnApp* app, std::string_view sourceId)
{
    if (!PvzpPlayerLevels::CanEdit(app) || app->GetDialog(DialogId)) return;
    auto* editor=new LevelEditorDialog(app,sourceId);
    app->AddDialog(editor);
    app->mWidgetManager->SetFocus(editor);
}

LevelEditorDialog::LevelEditorDialog(LawnApp* app, std::string_view sourceId)
    : LawnDialog(app,DialogId,true,T(app,"LEVEL_EDITOR","Level Editor","关卡编辑器"),"","",Sexy::Dialog::BUTTONS_NONE)
{
    const int ids[]={TabLevel,TabWaves,TabSpawns,TabLibrary,Back,Save,SaveAndPlay,SaveAsCopy,
        SunMinus100,SunMinus25,SunPlus25,SunPlus100,WavePrevious,WaveNext,WaveAdd,WaveDuplicate,
        WaveDelete,WaveEarlier,WaveLater,DelayMinus5,DelayMinus1,DelayPlus1,DelayPlus5,
        GroupPrevious,GroupNext,GroupAdd,GroupDelete,ChooseEnemy,CountMinus5,CountMinus1,
        CountPlus1,CountPlus5,LaneAuto,Lane1,Lane2,Lane3,Lane4,Lane5,EnemyPrevious,EnemyNext,
        Enemy0,Enemy1,Enemy2,Enemy3,LibraryPrevious,LibraryNext,LibraryEdit,LibraryCopy,
        LibraryDelete,NewLevel,DuplicateDraft,Confirm,Cancel};
    for (int id:ids) mButtons.push_back(MakeButton(id,this,""));
    mName=CreateEditWidget(NameEdit,this,this);
    mName->mMaxChars=80;
    mName->mAutoCapFirstLetter=false;
    EditorSize(this);
    mName->Resize(64,193,mWidth-128,40);
    if (const auto* source=Source(sourceId))
    {
        const bool own=PvzpPlayerLevels::Find(source->id)!=nullptr;
        Load(own ? *source : PvzpPlayerLevels::CopyDraft(*source));
        mDirty=!own;
    }
    else Load(PvzpPlayerLevels::NewDraft());
    RebuildEnemies();
    Refresh();
}
LevelEditorDialog::~LevelEditorDialog()=default;
LawnStoneButton* LevelEditorDialog::Button(int id) const
{
    for (const auto& button:mButtons) if (button->mId==id) return button.get();
    return nullptr;
}
void LevelEditorDialog::AddedToManager(Sexy::WidgetManager* manager)
{
    LawnDialog::AddedToManager(manager);
    for (auto& button:mButtons) AddWidget(button.get());
    AddWidget(mName.get());
}
void LevelEditorDialog::RemovedFromManager(Sexy::WidgetManager* manager)
{
    RemoveWidget(mName.get());
    for (auto& button:mButtons) RemoveWidget(button.get());
    LawnDialog::RemovedFromManager(manager);
}
void LevelEditorDialog::Load(PvzpMod::CustomLevel draft)
{
    mDraft=std::move(draft);
    mName->SetText(mDraft.name);
    mWave=mGroup=0;
    mDirty=false;
    mStatus.clear();
}
void LevelEditorDialog::SyncName()
{
    if (mName->mString!=mDraft.name)
    {
        mDraft.name=mName->mString;
        mDirty=true;
        mStatus.clear();
    }
}
void LevelEditorDialog::Changed()
{
    mDirty=true;
    mStatus.clear();
    Refresh();
}
void LevelEditorDialog::SetView(View view)
{
    SyncName();
    // Releasing native edit focus also releases the mobile software keyboard.
    if (mWidgetManager) mWidgetManager->SetFocus(this);
    mView=view;
    Refresh();
}
void LevelEditorDialog::EditWidgetText(int, const std::string&)
{
    SyncName();
    if (mWidgetManager) mWidgetManager->SetFocus(this);
    Refresh();
}
int LevelEditorDialog::WaveTotal() const
{
    if (mWave>=mDraft.waves.size()) return 0;
    int total=0;
    for (const auto& group:mDraft.waves[mWave].spawns) total+=group.count;
    return total;
}
void LevelEditorDialog::RebuildEnemies()
{
    mEnemies.clear();
    // Day lawn only: no pool-only, bungee, boss, vehicle or challenge variants.
    const char* builtin[]={"ZOMBIE","FLAG_ZOMBIE","CONEHEAD_ZOMBIE","POLE_VAULTING_ZOMBIE",
        "BUCKETHEAD_ZOMBIE","NEWSPAPER_ZOMBIE","SCREEN_DOOR_ZOMBIE","FOOTBALL_ZOMBIE",
        "DANCING_ZOMBIE","BACKUP_DANCER","JACK_IN_THE_BOX_ZOMBIE","BALLOON_ZOMBIE",
        "DIGGER_ZOMBIE","POGO_ZOMBIE","ZOMBIE_YETI","LADDER_ZOMBIE","GARGANTUAR","IMP","REDEYED_GARGANTUAR"};
    for (const char* id:builtin) mEnemies.push_back({id,PvzpUiResourceName(id)});
    for (const auto& zombie:PvzpMod::CustomZombies())
        mEnemies.push_back({zombie.id,PvzpUiText(zombie.name)});
    mEnemyPage=std::min(mEnemyPage,(mEnemies.size()-1)/4);
}
void LevelEditorDialog::ShowButton(int id,int column,int columns,int y,const std::string& label,bool enabled)
{
    auto* b=Button(id);
    const int gap=10,left=48,w=(mWidth-96-gap*(columns-1))/columns;
    b->Resize(left+column*(w+gap),y,w,std::max(40,Sexy::IMAGE_BUTTON_LEFT->mHeight));
    b->SetLabel(label);
    b->SetVisible(true);
    b->SetDisabled(!enabled);
}

void LevelEditorDialog::Refresh()
{
    mLocaleGeneration=PvzpLocaleGetGeneration();
    mCatalogRevision=PvzpMod::SnapshotRevision();
    mHeaderText=T(mApp,"LEVEL_EDITOR","Level Editor","关卡编辑器");
    mDialogHeader.clear();
    for (auto& b:mButtons) { b->SetVisible(false); b->SetDisabled(true); }
    mName->SetVisible(mView==View::Level);
    mName->SetDisabled(mView!=View::Level);
    mLines.clear();
    const auto text=[&](const char* key,const char* en,const char* zh){return T(mApp,key,en,zh);};
    const auto show=[&](int id,int col,int cols,int y,const char* key,const char* en,const char* zh,bool on=true)
        {ShowButton(id,col,cols,y,text(key,en,zh),on);};
    if (mView!=View::Confirmation)
    {
        show(TabLevel,0,4,100,"EDITOR_LEVEL_TAB","Level","关卡");
        show(TabWaves,1,4,100,"EDITOR_WAVES_TAB","Waves","波次");
        show(TabSpawns,2,4,100,"EDITOR_GROUPS_TAB","Enemies","敌人");
        show(TabLibrary,3,4,100,"EDITOR_LIBRARY_TAB","My Levels","我的关卡");
        show(Back,0,1,mHeight-76,"CUSTOM_LEVEL_CLOSE","Back","返回");
    }
    mWave=mDraft.waves.empty()?0:std::min(mWave,mDraft.waves.size()-1);
    auto* wave=mDraft.waves.empty()?nullptr:&mDraft.waves[mWave];
    mGroup=!wave||wave->spawns.empty()?0:std::min(mGroup,wave->spawns.size()-1);
    auto* group=!wave||wave->spawns.empty()?nullptr:&wave->spawns[mGroup];
    const auto validation=PvzpPlayerLevels::Validate(mDraft);
    mPreview=validation?text("EDITOR_VALID","Ready to save","可以保存"):
        text("EDITOR_FIX_DRAFT","Fix before saving: ","保存前请修正：")+PvzpUiText(validation.message);
    const std::string waveTitle=text("EDITOR_WAVE","Wave","波次")+" "+std::to_string(mWave+1)+" / "+std::to_string(mDraft.waves.size())+" (30)";
    if (mView==View::Level)
    {
        mLines.push_back({175,text("EDITOR_NAME","Level name (tap to type)","关卡名称（点击输入）")+(mDirty?" *":"")});
        mLines.push_back({267,text("CUSTOM_LEVEL_SUN","Starting sun","初始阳光")+": "+std::to_string(mDraft.startingSun)+" / 9990"});
        ShowButton(SunMinus100,0,4,280,"-100",mDraft.startingSun>0);
        ShowButton(SunMinus25,1,4,280,"-25",mDraft.startingSun>0);
        ShowButton(SunPlus25,2,4,280,"+25",mDraft.startingSun<9990);
        ShowButton(SunPlus100,3,4,280,"+100",mDraft.startingSun<9990);
        show(Save,0,3,340,"EDITOR_SAVE","Save","保存");
        show(SaveAsCopy,1,3,340,"EDITOR_SAVE_COPY","Save as Copy","另存副本");
        show(SaveAndPlay,2,3,340,"EDITOR_SAVE_PLAY","Save & Play","保存并游玩");
        show(DuplicateDraft,0,2,400,"EDITOR_DUPLICATE","Duplicate draft","复制草稿");
        show(NewLevel,1,2,400,"EDITOR_NEW","New level","新建关卡");
    }
    else if (mView==View::Waves)
    {
        mLines.push_back({165,waveTitle});
        show(WavePrevious,0,2,178,"EDITOR_PREVIOUS","Previous","上一个",mWave>0);
        show(WaveNext,1,2,178,"EDITOR_NEXT","Next","下一个",mWave+1<mDraft.waves.size());
        show(WaveAdd,0,3,231,"EDITOR_ADD","Add","添加",mDraft.waves.size()<30);
        show(WaveDuplicate,1,3,231,"EDITOR_COPY","Duplicate","复制",wave&&mDraft.waves.size()<30);
        show(WaveDelete,2,3,231,"EDITOR_DELETE","Delete","删除",mDraft.waves.size()>1);
        show(WaveEarlier,0,2,284,"EDITOR_EARLIER","Move earlier","向前移动",mWave>0);
        show(WaveLater,1,2,284,"EDITOR_LATER","Move later","向后移动",mWave+1<mDraft.waves.size());
        if (wave) mLines.push_back({357,text("EDITOR_DELAY_SECONDS","Delay (seconds)","间隔（秒）")+": "+std::to_string(wave->delay/100)+"."+(wave->delay%100<10?"0":"")+std::to_string(wave->delay%100)});
        show(DelayMinus5,0,4,370,"EDITOR_MINUS5S","-5 sec","-5 秒",wave&&wave->delay>1);
        show(DelayMinus1,1,4,370,"EDITOR_MINUS1S","-1 sec","-1 秒",wave&&wave->delay>1);
        show(DelayPlus1,2,4,370,"EDITOR_PLUS1S","+1 sec","+1 秒",wave&&wave->delay<60000);
        show(DelayPlus5,3,4,370,"EDITOR_PLUS5S","+5 sec","+5 秒",wave&&wave->delay<60000);
        mLines.push_back({435,text("EDITOR_WAVE_HELP","Delay is before this wave; use Enemies to build its groups.","间隔为本波开始前的时间；在“敌人”页设置出怪组。")});
    }
    else if (mView==View::Spawns)
    {
        mLines.push_back({165,waveTitle+"   "+text("EDITOR_GROUP","Group","出怪组")+" "+std::to_string(mGroup+1)+" / "+std::to_string(wave?wave->spawns.size():0)+" (12)"});
        show(GroupPrevious,0,4,178,"EDITOR_PREVIOUS","Previous","上一个",mGroup>0);
        show(GroupNext,1,4,178,"EDITOR_NEXT","Next","下一个",wave&&mGroup+1<wave->spawns.size());
        show(GroupAdd,2,4,178,"EDITOR_ADD","Add","添加",wave&&wave->spawns.size()<12&&WaveTotal()<50);
        show(GroupDelete,3,4,178,"EDITOR_DELETE","Delete","删除",wave&&wave->spawns.size()>1);
        std::string enemy=group?PvzpUiResourceName(group->zombie):"-";
        if (group) for (const auto& e:mEnemies) if (e.id==group->zombie) {enemy=e.label;break;}
        ShowButton(ChooseEnemy,0,1,236,text("EDITOR_CHOOSE_ENEMY","Choose enemy","选择敌人")+": "+PvzpUiClip(enemy,32),group);
        mLines.push_back({307,text("EDITOR_LANE_HELP","Lane (top = 1); Auto chooses a lane per zombie","路线（最上方为1）；自动为每只僵尸选择路线")});
        ShowButton(LaneAuto,0,6,320,text("EDITOR_AUTO","Auto","自动")+(group&&group->row==-1?" *":""),group);
        for (int row=0;row<5;++row) ShowButton(Lane1+row,row+1,6,320,std::to_string(row+1)+(group&&group->row==row?" *":""),group);
        mLines.push_back({392,text("EDITOR_QUANTITY","Quantity","数量")+": "+std::to_string(group?group->count:0)+" / 50   "+text("EDITOR_WAVE_TOTAL","Wave total","本波总数")+": "+std::to_string(WaveTotal())+" / 50"});
        ShowButton(CountMinus5,0,4,405,"-5",group&&group->count>1);
        ShowButton(CountMinus1,1,4,405,"-1",group&&group->count>1);
        ShowButton(CountPlus1,2,4,405,"+1",group&&WaveTotal()<50);
        ShowButton(CountPlus5,3,4,405,"+5",group&&WaveTotal()<50);
    }
    else if (mView==View::Enemies)
    {
        mLines.push_back({165,text("EDITOR_ENEMY_CATALOG","Enemy catalog","敌人目录")+"  "+std::to_string(mEnemyPage+1)+" / "+std::to_string((mEnemies.size()+3)/4)});
        for (int i=0;i<4;++i)
        {
            const size_t n=mEnemyPage*4+i;
            if (n<mEnemies.size()) ShowButton(Enemy0+i,0,1,178+53*i,PvzpUiClip(mEnemies[n].label,32));
        }
        show(EnemyPrevious,0,2,400,"EDITOR_PREVIOUS","Previous","上一页",mEnemyPage>0);
        show(EnemyNext,1,2,400,"EDITOR_NEXT","Next","下一页",(mEnemyPage+1)*4<mEnemies.size());
    }
    else if (mView==View::Library)
    {
        const auto& own=PvzpPlayerLevels::Levels();
        mLibraryPage=own.empty()?0:std::min(mLibraryPage,own.size()-1);
        mLines.push_back({165,text("EDITOR_OWN_LEVELS","My saved levels","我保存的关卡")+"  "+std::to_string(own.empty()?0:mLibraryPage+1)+" / "+std::to_string(own.size())+" (32)"});
        show(LibraryPrevious,0,2,178,"EDITOR_PREVIOUS","Previous","上一个",mLibraryPage>0);
        show(LibraryNext,1,2,178,"EDITOR_NEXT","Next","下一个",mLibraryPage+1<own.size());
        mLines.push_back({259,own.empty()?text("EDITOR_LIBRARY_EMPTY","No saved player levels yet.","还没有保存的玩家关卡。"):PvzpUiClip(PvzpUiText(own[mLibraryPage].name),44)});
        show(LibraryEdit,0,2,280,"EDITOR_EDIT","Edit","编辑",!own.empty());
        show(LibraryCopy,1,2,280,"EDITOR_COPY","Duplicate","复制",!own.empty());
        show(NewLevel,0,2,340,"EDITOR_NEW","New level","新建关卡");
        show(LibraryDelete,1,2,340,"EDITOR_DELETE_OWN","Delete own file","删除自己的文件",!own.empty());
        mLines.push_back({416,text("EDITOR_LIBRARY_HELP","Bundled levels are read-only. Open one from the picker to make a copy.","内置关卡为只读。可从关卡选择器打开并制作副本。")});
    }
    else if (mView==View::Confirmation)
    {
        show(Confirm,0,2,350,"EDITOR_CONFIRM","Confirm","确认");
        show(Cancel,1,2,350,"EDITOR_CANCEL","Cancel","取消");
    }
    MarkDirty();
}

void LevelEditorDialog::Update()
{
    LawnDialog::Update();
    const bool nameChanged=mDraft.name!=mName->mString;
    SyncName();
    if (mLocaleGeneration!=PvzpLocaleGetGeneration()||mCatalogRevision!=PvzpMod::SnapshotRevision())
    { RebuildEnemies(); Refresh(); }
    else if (nameChanged) Refresh();
}
void LevelEditorDialog::Draw(Sexy::Graphics* g)
{
    LawnDialog::Draw(g);
    PvzpDrawString(g,mHeaderText,mWidth/2,88,Sexy::FONT_DWARVENTODCRAFT24,Sexy::Color(255,220,135),DS_ALIGN_CENTER);
    g->SetFont(Sexy::FONT_BRIANNETOD12);
    g->SetColor(Sexy::Color(255,235,170));
    for (const auto& line:mLines)
    {
        g->PushState();
        g->ClipRect(48,line.first-20,mWidth-96,34);
        g->WriteWordWrapped(Sexy::Rect(48,line.first-20,mWidth-96,34),line.second);
        g->PopState();
    }
    if (mName->mVisible) DrawEditBox(g,mName.get());
    g->PushState();
    g->ClipRect(48,mHeight-139,mWidth-96,54);
    g->WriteWordWrapped(Sexy::Rect(48,mHeight-139,mWidth-96,54),mStatus.empty()?mPreview:mStatus);
    g->PopState();
    if (mView==View::Confirmation)
    {
        g->PushState();
        g->ClipRect(58,180,mWidth-116,145);
        g->WriteWordWrapped(Sexy::Rect(58,180,mWidth-116,145),mConfirmText);
        g->PopState();
    }
}

void LevelEditorDialog::ConfirmAction(Pending pending,const std::string& text,const std::string& id)
{
    mPending=pending;
    mPendingId=id;
    mConfirmText=text;
    mReturnView=mView;
    SetView(View::Confirmation);
}
void LevelEditorDialog::Request(Pending pending,const std::string& id)
{
    SyncName();
    if (pending==Pending::Save||pending==Pending::Play)
    {
        const auto result=PvzpPlayerLevels::Validate(mDraft);
        if (!result) {mStatus=PvzpUiText(result.message); Refresh(); return;}
        const bool overwrite=PvzpPlayerLevels::Find(mDraft.id)!=nullptr;
        if (pending==Pending::Play)
            ConfirmAction(pending,overwrite?
                T(mApp,"EDITOR_OVERWRITE_PLAY","Overwrite your player file and play? Only the last custom checkpoint is replaced; Adventure and Survival are untouched.","覆盖自己的关卡并游玩？仅替换上次的自定义关卡存档，不影响冒险和生存进度。"):
                T(mApp,"EDITOR_PLAY_WARNING","Save and play? Only the last custom checkpoint is replaced; Adventure and Survival are untouched.","保存并游玩？仅替换上次的自定义关卡存档，不影响冒险和生存进度。"));
        else if (overwrite) ConfirmAction(pending,T(mApp,"EDITOR_OVERWRITE_WARNING","Overwrite your saved player level? Bundled packs are never changed.","覆盖自己保存的关卡？不会修改内置内容包。"));
        else Execute(pending);
        return;
    }
    if (pending==Pending::Delete)
    {
        if (!PvzpPlayerLevels::Find(id)) return;
        ConfirmAction(pending,T(mApp,"EDITOR_DELETE_WARNING","Delete this saved player file? If it is the current draft, its unsaved changes will also be discarded. Bundled levels are never deleted.","删除这个玩家关卡文件？若为当前草稿，其未保存的修改也将丢弃。不会删除内置关卡。"),id);
        return;
    }
    if (mDirty && pending!=Pending::Duplicate)
        ConfirmAction(pending,T(mApp,"EDITOR_DISCARD_WARNING","Discard unsaved changes? Your saved files will not change.","丢弃未保存的修改？不会改变已保存的文件。"),id);
    else Execute(pending,id);
}
void LevelEditorDialog::Execute(Pending pending,const std::string& id)
{
    if (pending==Pending::Close) {LawnApp* app=mApp; const int dialog=mId; app->KillDialog(dialog); return;}
    if (!PvzpPlayerLevels::CanEdit(mApp))
    {mStatus=T(mApp,"EDITOR_MENU_ONLY","Editing is only available in the main menu.","只能在主菜单编辑关卡。"); Refresh(); return;}
    if (pending==Pending::Save||pending==Pending::Play) {SaveDraft(pending==Pending::Play);return;}
    if (pending==Pending::Delete)
    {
        // Only the player's library can be removed; never trust a picker ID.
        if (!PvzpPlayerLevels::Find(id))
        {mStatus=T(mApp,"EDITOR_MISSING","Player level no longer exists.","玩家关卡已不存在。");Refresh();return;}
        const auto result=PvzpPlayerLevels::Remove(id);
        if (!result) mStatus=PvzpUiText(result.message);
        else
        {
            if (mDraft.id==id) Load(PvzpPlayerLevels::NewDraft());
            mStatus=T(mApp,"EDITOR_DELETED","Player level deleted.","已删除玩家关卡。");
        }
        SetView(View::Library);
        return;
    }
    if (pending==Pending::New) Load(PvzpPlayerLevels::NewDraft());
    else if (pending==Pending::Duplicate) {auto copy=PvzpPlayerLevels::CopyDraft(mDraft);Load(std::move(copy));mDirty=true;}
    else if (pending==Pending::Edit||pending==Pending::Copy)
    {
        const auto* source=PvzpPlayerLevels::Find(id);
        if (!source) {mStatus=T(mApp,"EDITOR_MISSING","Player level no longer exists.","玩家关卡已不存在。"); Refresh(); return;}
        Load(pending==Pending::Copy?PvzpPlayerLevels::CopyDraft(*source):*source);
        if (pending==Pending::Copy) mDirty=true;
    }
    SetView(View::Level);
}
void LevelEditorDialog::SaveDraft(bool play,bool asCopy)
{
    if (!PvzpPlayerLevels::CanEdit(mApp)) return;
    SyncName();
    auto candidate=asCopy?PvzpPlayerLevels::CopyDraft(mDraft):mDraft;
    auto result=PvzpPlayerLevels::Validate(candidate);
    if (result) result=PvzpPlayerLevels::Save(candidate);
    if (!result) {mStatus=PvzpUiText(result.message); Refresh(); return;}
    mDraft=std::move(candidate);
    mName->SetText(mDraft.name);
    mDirty=false;
    mStatus=T(mApp,"EDITOR_SAVED","Saved to your player library.","已保存到玩家关卡库。");
    if (!play) {Refresh(); return;}
    // Start by saved stable ID, not by library position. Keep the draft alive
    // for a failed launch; after a successful launch KillDialog may delete us.
    LawnApp* app=mApp;
    const std::string stableId=mDraft.id;
    if (!PvzpCustomLevels::Start(app,stableId))
    {mStatus=T(app,"EDITOR_START_FAILED","Saved, but could not start. Your draft is retained.","已保存，但无法开始游戏。草稿已保留。"); Refresh(); return;}
    app->KillDialog(PvzpCustomLevels::PickerDialogId);
    app->KillDialog(DialogId);
}

void LevelEditorDialog::ButtonDepress(int id)
{
    SyncName();
    if (id==Cancel && mView==View::Confirmation)
    {mPending=Pending::None;SetView(mReturnView);return;}
    if (id==Confirm && mView==View::Confirmation)
    {
        const Pending pending=mPending;
        const std::string target=mPendingId;
        mPending=Pending::None;
        SetView(mReturnView);
        Execute(pending,target);
        return;
    }
    if (mView==View::Confirmation) return;
    // Real and test clicks share visibility/disabled checks and runtime guard.
    auto* button=Button(id);
    if (!button || !button->mVisible || button->mDisabled) return;
    if (id==Back)
    {
        if (mView==View::Enemies) {SetView(View::Spawns);return;}
        Request(Pending::Close);return;
    }
    if (!PvzpPlayerLevels::CanEdit(mApp))
    {mStatus=T(mApp,"EDITOR_MENU_ONLY","Editing is only available in the main menu.","只能在主菜单编辑关卡。");Refresh();return;}
    if (id>=TabLevel&&id<=TabLibrary) {SetView(static_cast<View>(id-TabLevel));return;}
    if (id==Save) {Request(Pending::Save);return;}
    if (id==SaveAndPlay) {Request(Pending::Play);return;}
    if (id==SaveAsCopy) {SaveDraft(false,true);return;}
    if (id==NewLevel) {Request(Pending::New);return;}
    if (id==DuplicateDraft) {Request(Pending::Duplicate);return;}
    if (id==LibraryPrevious) {if(mLibraryPage) --mLibraryPage;Refresh();return;}
    if (id==LibraryNext) {if(mLibraryPage+1<PvzpPlayerLevels::Levels().size()) ++mLibraryPage;Refresh();return;}
    if (id==LibraryEdit||id==LibraryCopy||id==LibraryDelete)
    {
        const auto& levels=PvzpPlayerLevels::Levels();
        if (mLibraryPage<levels.size()) Request(id==LibraryEdit?Pending::Edit:id==LibraryCopy?Pending::Copy:Pending::Delete,levels[mLibraryPage].id);
        return;
    }
    if (id>=SunMinus100&&id<=SunPlus100)
    {
        const int delta[]={-100,-25,25,100};
        mDraft.startingSun=std::clamp(mDraft.startingSun+delta[id-SunMinus100],0,9990);
        Changed();return;
    }
    if (id==WavePrevious||id==WaveNext)
    {if(id==WavePrevious&&mWave) --mWave;else if(id==WaveNext&&mWave+1<mDraft.waves.size()) ++mWave;mGroup=0;Refresh();return;}
    if (id==WaveAdd||id==WaveDuplicate)
    {
        if(mDraft.waves.size()>=30) return;
        const auto defaults=PvzpPlayerLevels::NewDraft();
        const auto wave=id==WaveDuplicate&&!mDraft.waves.empty()?mDraft.waves[mWave]:defaults.waves.front();
        const size_t at=mDraft.waves.empty()?0:mWave+1;
        mDraft.waves.insert(mDraft.waves.begin()+at,wave);mWave=at;mGroup=0;Changed();return;
    }
    if (mWave>=mDraft.waves.size()) return;
    auto& wave=mDraft.waves[mWave];
    if (id==WaveDelete)
    {if(mDraft.waves.size()>1){mDraft.waves.erase(mDraft.waves.begin()+mWave);mGroup=0;Changed();}return;}
    if (id==WaveEarlier||id==WaveLater)
    {
        if(id==WaveEarlier&&mWave){std::swap(mDraft.waves[mWave],mDraft.waves[mWave-1]);--mWave;Changed();}
        else if(id==WaveLater&&mWave+1<mDraft.waves.size()){std::swap(mDraft.waves[mWave],mDraft.waves[mWave+1]);++mWave;Changed();}
        return;
    }
    if (id>=DelayMinus5&&id<=DelayPlus5)
    {const int delta[]={-500,-100,100,500};wave.delay=std::clamp(wave.delay+delta[id-DelayMinus5],1,60000);Changed();return;}
    if (id==GroupPrevious||id==GroupNext)
    {if(id==GroupPrevious&&mGroup)--mGroup;else if(id==GroupNext&&mGroup+1<wave.spawns.size())++mGroup;Refresh();return;}
    if (id==GroupAdd)
    {if(wave.spawns.size()<12&&WaveTotal()<50){wave.spawns.push_back({"ZOMBIE",-1,1});mGroup=wave.spawns.size()-1;Changed();}return;}
    if (id==GroupDelete)
    {if(wave.spawns.size()>1){wave.spawns.erase(wave.spawns.begin()+mGroup);Changed();}return;}
    if (mGroup>=wave.spawns.size()) return;
    auto& group=wave.spawns[mGroup];
    if(id==ChooseEnemy)
    {
        for(size_t i=0;i<mEnemies.size();++i)if(mEnemies[i].id==group.zombie){mEnemyPage=i/4;break;}
        SetView(View::Enemies);return;
    }
    if(id==EnemyPrevious){if(mEnemyPage)--mEnemyPage;Refresh();return;}
    if(id==EnemyNext){if((mEnemyPage+1)*4<mEnemies.size())++mEnemyPage;Refresh();return;}
    if(id>=Enemy0&&id<=Enemy3)
    {const size_t n=mEnemyPage*4+id-Enemy0;if(n<mEnemies.size()){group.zombie=mEnemies[n].id;mDirty=true;mStatus.clear();SetView(View::Spawns);}return;}
    if(id>=LaneAuto&&id<=Lane5){group.row=id-LaneAuto-1;Changed();return;}
    if(id>=CountMinus5&&id<=CountPlus5)
    {const int delta[]={-5,-1,1,5};const int cap=std::max(1,50-WaveTotal()+group.count);group.count=std::clamp(group.count+delta[id-CountMinus5],1,cap);Changed();return;}
}
void LevelEditorDialog::KeyDown(Sexy::KeyCode key)
{
    if (key==Sexy::KeyCode::KEYCODE_ESCAPE)
    {ButtonDepress(mView==View::Confirmation?Cancel:Back);return;}
    // Enter is deliberately not Save/Confirm: phone IME completion is harmless.
    LawnDialog::KeyDown(key);
}
