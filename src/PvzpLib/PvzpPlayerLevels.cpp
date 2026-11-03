// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpPlayerLevels.h"
#include "PvzpCustomZombies.h"
#include "SexyAppFramework/Common.h"
#include "../LawnApp.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <locale>
#include <random>
#include <set>
#include <sstream>

// Private implementation bridge to the existing strict schema parser.
namespace PvzpPlayerLevelParsing {
bool Parse(std::string_view, PvzpMod::CustomLevel&, std::string&);
bool CanPublish(const std::vector<PvzpMod::CustomLevel>&);
}
namespace PvzpPlayerLevels {
namespace {
namespace fs=std::filesystem;
fs::path gRoot;
std::vector<PvzpMod::CustomLevel> gLevels;
std::vector<std::string> gDiagnostics;
// Deterministic fault injection for asset-free transaction rollback tests only.
// Not exposed in the editor API and never configured from files/environment.
int gFailRenameCountdown=0;
Result Good(){return {true,{}};}
Result Bad(std::string message){return {false,std::move(message)};}

// Inspect every component, not just the final directory: a symlink ancestor
// must never redirect reads/writes outside the selected player-level root.
Result SafeRoot(const fs::path& root,bool create){
    if(root.empty()||!root.is_absolute())return Bad("player directory is not initialized");
    std::error_code ec;
    fs::path part;
    for(const auto& component:root){
        if(component=="..")return Bad("parent path traversal refused");
        part/=component;
        auto state=fs::symlink_status(part,ec);
        if(ec==std::errc::no_such_file_or_directory){ec.clear();continue;}
        if(ec)return Bad("cannot inspect player directory: "+ec.message());
        if(fs::is_symlink(state))return Bad("symlink directory refused");
        if(fs::exists(state)&&!fs::is_directory(state))return Bad("player directory component is not a directory");
    }
    if(create){fs::create_directories(root,ec);if(ec)return Bad("cannot create player directory: "+ec.message());return SafeRoot(root,false);}
    if(!fs::is_directory(root,ec)||ec)return Bad("player directory is unavailable");
    return Good();
}
Result FileState(const fs::path& path,bool& exists){
    std::error_code ec;auto state=fs::symlink_status(path,ec);
    if(ec==std::errc::no_such_file_or_directory){exists=false;return Good();}
    if(ec)return Bad("cannot inspect player file: "+ec.message());
    exists=fs::exists(state);
    if(fs::is_symlink(state))return Bad("symlink player file refused");
    if(exists&&!fs::is_regular_file(state))return Bad("player file is not a regular file");
    return Good();
}
std::string Quote(std::string_view text){
    static constexpr char hex[]="0123456789abcdef";
    std::string out="\"";
    for(unsigned char c:text){
        if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
        else if(c<0x20){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    out+='"';return out;
}
std::string NewId(){
    static std::atomic<uint64_t> serial{0};
    std::random_device random;
    for(;;){
        std::ostringstream out;out.imbue(std::locale::classic());out<<"player."<<std::hex<<std::setfill('0')
            <<std::setw(16)<<static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())
            <<std::setw(8)<<random()<<std::setw(8)<<random()<<std::setw(16)<<++serial;
        std::string id=out.str();
        bool collision=std::any_of(PvzpMod::CustomLevels().begin(),PvzpMod::CustomLevels().end(),[&](const auto& c){return c.id==id;});
        if(!collision&&!Find(id)){
            if(gRoot.empty()||!SafeRoot(gRoot,false))return id;
            bool exists=false;auto state=FileState(gRoot/(id+".json"),exists);
            if(!exists||!state)return id;
        }
    }
}
bool CanPublish(const std::vector<PvzpMod::CustomLevel>& levels){
    return PvzpPlayerLevelParsing::CanPublish(levels);
}
void Cleanup(const fs::path& path){std::error_code ignored;fs::remove(path,ignored);}
Result Rename(const fs::path& from,const fs::path& to){
    if(gFailRenameCountdown>0&&--gFailRenameCountdown==0)return Bad("injected rename failure");
    std::error_code ec;fs::rename(from,to,ec);
    return ec?Bad("cannot rename player file: "+ec.message()):Good();
}
bool EditableState(bool hasBoard,bool hasChooser,int scene){
    return !hasBoard&&!hasChooser&&scene==static_cast<int>(GameScenes::SCENE_MENU);
}
}
namespace ModelTest {
void FailRename(int countdown){gFailRenameCountdown=countdown;}
// Pure editor guard seam: test scene/state snapshots without constructing an
// asset-loading LawnApp or fabricating an object with undefined lifetime.
bool CanEditState(bool hasBoard,bool hasChooser,int scene){return EditableState(hasBoard,hasChooser,scene);}
}

bool IsPlayerId(std::string_view id){
    if(id.size()<=7||id.size()>128||id.substr(0,7)!="player."||id.find("..")!=std::string_view::npos)return false;
    for(unsigned char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
    return true;
}
const std::vector<PvzpMod::CustomLevel>& Levels(){return gLevels;}
const PvzpMod::CustomLevel* Find(std::string_view id){
    for(const auto& c:gLevels)if(c.id==id)return &c;return nullptr;
}
fs::path Directory(){return gRoot;}
const std::vector<std::string>& Diagnostics(){return gDiagnostics;}
bool CanEdit(const LawnApp* app){
    return app&&EditableState(app->mBoard!=nullptr,bool(app->mSeedChooserScreen),static_cast<int>(app->mGameScene));
}

std::string ExportJson(const PvzpMod::CustomLevel& level){
    std::ostringstream out;out.imbue(std::locale::classic());out<<"{\"schemaVersion\":2,\"customLevels\":[{\"id\":"<<Quote(level.id)
        <<",\"name\":"<<Quote(level.name)<<",\"description\":"<<Quote(level.description)
        <<",\"startingSun\":"<<level.startingSun<<",\"waves\":[";
    bool firstWave=true;
    for(const auto& wave:level.waves){
        if(!firstWave)out<<',';firstWave=false;
        out<<"{\"delay\":"<<wave.delay<<",\"spawns\":[";bool firstSpawn=true;
        for(const auto& spawn:wave.spawns){
            if(!firstSpawn)out<<',';firstSpawn=false;
            out<<"{\"zombie\":"<<Quote(spawn.zombie)<<",\"row\":"<<spawn.row<<",\"count\":"<<spawn.count<<'}';
        }
        out<<"]}";
    }
    out<<"]}]}\n";return out.str();
}
Result Validate(const PvzpMod::CustomLevel& level,bool requireDependencies){
    if(!IsPlayerId(level.id))return Bad("player ID must use safe player. namespace (max 128 bytes)");
    // Bound before serializing externally supplied models, including oversized vectors.
    if(level.name.size()>128||level.description.size()>1024||level.waves.empty()||level.waves.size()>MaxWaves)
        return Bad("invalid player name/description/wave limits");
    for(const auto& wave:level.waves){
        if(wave.spawns.empty()||wave.spawns.size()>MaxGroups)return Bad("wave requires 1..12 spawn groups");
        for(const auto& spawn:wave.spawns)if(spawn.zombie.size()>96)return Bad("zombie reference exceeds 96 bytes");
    }
    PvzpMod::CustomLevel parsed;std::string error;
    if(!PvzpPlayerLevelParsing::Parse(ExportJson(level),parsed,error))return Bad(error);
    if(requireDependencies)for(const auto& wave:level.waves)for(const auto& spawn:wave.spawns){
        if(!PvzpCustomZombies::Exists(spawn.zombie))return Bad("unresolved zombie reference: "+spawn.zombie);
        const bool custom=std::any_of(PvzpMod::CustomZombies().begin(),PvzpMod::CustomZombies().end(),[&](const auto& c){return c.id==spawn.zombie;});
        static const std::set<std::string> safeBuiltins={"ZOMBIE","FLAG_ZOMBIE","CONEHEAD_ZOMBIE","POLE_VAULTING_ZOMBIE",
            "BUCKETHEAD_ZOMBIE","NEWSPAPER_ZOMBIE","SCREEN_DOOR_ZOMBIE","FOOTBALL_ZOMBIE","DANCING_ZOMBIE",
            "BACKUP_DANCER","JACK_IN_THE_BOX_ZOMBIE","BALLOON_ZOMBIE","DIGGER_ZOMBIE","POGO_ZOMBIE","ZOMBIE_YETI",
            "LADDER_ZOMBIE","GARGANTUAR","IMP","REDEYED_GARGANTUAR"};
        if(!custom&&!safeBuiltins.contains(spawn.zombie))return Bad("unsupported lawn zombie reference: "+spawn.zombie);
    }
    return Good();
}
PvzpMod::CustomLevel NewDraft(){
    PvzpMod::CustomLevel level;level.id=NewId();level.name="New level";level.startingSun=500;
    level.waves.push_back({800,{{"ZOMBIE",-1,1}}});return level;
}
PvzpMod::CustomLevel CopyDraft(const PvzpMod::CustomLevel& source){
    auto level=source;level.id=NewId();
    constexpr std::string_view suffix=" (copy)";
    const size_t maximum=128-suffix.size();
    if(level.name.size()>maximum){
        size_t end=maximum;
        while(end>0&&(static_cast<unsigned char>(level.name[end])&0xc0)==0x80)--end;
        level.name.resize(end);
    }
    level.name+=suffix;return level;
}
Result Initialize(const fs::path& requested){
    std::error_code ec;
    fs::path root=requested.empty()?fs::path(Sexy::GetAppDataPath("playerlevels")):requested;
    for(const auto& part:root)if(part=="..")return Bad("parent path traversal refused");
    root=fs::absolute(root,ec);if(ec)return Bad("cannot resolve player directory");
    root=root.lexically_normal();
    auto safe=SafeRoot(root,true);if(!safe)return safe;
    std::vector<fs::path> files;
    std::vector<std::string> diagnostics;
    fs::directory_iterator it(root,ec),end;
    if(ec)return Bad("cannot scan player directory: "+ec.message());
    for(;it!=end;it.increment(ec)){
        if(ec)return Bad("cannot scan player directory: "+ec.message());
        if(it->path().extension()==".json")files.push_back(it->path());
        else {
            const auto name=it->path().filename().string();
            if(name.ends_with(".json.tmp")||name.ends_with(".json.bak"))
                diagnostics.push_back(name+": interrupted transaction artifact retained for recovery");
        }
    }
    if(ec)return Bad("cannot scan player directory: "+ec.message());
    std::sort(files.begin(),files.end());
    std::vector<PvzpMod::CustomLevel> loaded;std::set<std::string> ids;
    for(const auto& file:files){
        auto report=[&](const std::string& message){diagnostics.push_back(file.filename().string()+": "+message);};
        bool exists=false;auto state=FileState(file,exists);
        if(!state||!exists){report(state.message.empty()?"file disappeared":state.message);continue;}
        const auto bytes=fs::file_size(file,ec);
        if(ec||bytes>65536){ec.clear();report("unreadable/oversized file (max 64 KiB)");continue;}
        std::ifstream stream(file,std::ios::binary);
        if(!stream){report("cannot open file");continue;}
        // Read at most the size cap even if a file grows after stat.
        std::string text(static_cast<size_t>(bytes),'\0');
        stream.read(text.data(),static_cast<std::streamsize>(text.size()));
        if(static_cast<size_t>(stream.gcount())!=bytes||stream.peek()!=std::char_traits<char>::eof()){
            report("file size changed during read");continue;
        }
        PvzpMod::CustomLevel level;std::string error;
        if(!PvzpPlayerLevelParsing::Parse(text,level,error)){report(error);continue;}
        auto valid=Validate(level,false);
        if(!valid){report(valid.message);continue;}
        if(file.filename()!=level.id+".json"){report("filename must match stable player ID");continue;}
        if(!ids.insert(level.id).second){report("duplicate player ID");continue;}
        if(!CanPublish({level})){report("player ID collides with bundled level; file retained");continue;}
        if(loaded.size()>=MaxLevels){report("player catalog exceeds 32 levels; file retained");continue;}
        const auto playable=Validate(level,true);
        if(!playable)report(playable.message+"; retained for repair");
        loaded.push_back(std::move(level));
    }
    if(!CanPublish(loaded)||!PvzpMod::PublishPlayerLevels(loaded))return Bad("player IDs collide with bundled levels");
    gRoot=root;gLevels=std::move(loaded);gDiagnostics=std::move(diagnostics);return Good();
}
Result Save(const PvzpMod::CustomLevel& level){
    auto valid=Validate(level,true);if(!valid)return valid;
    auto safe=SafeRoot(gRoot,false);if(!safe)return safe;
    auto next=gLevels;
    auto found=std::find_if(next.begin(),next.end(),[&](const auto& c){return c.id==level.id;});
    if(found==next.end())next.push_back(level);else *found=level;
    if(!CanPublish(next))return Bad("player catalog limit or bundled ID collision");
    const auto target=gRoot/(level.id+".json"),temp=gRoot/(level.id+".json.tmp"),backup=gRoot/(level.id+".json.bak");
    bool existing=false,tempExists=false,backupExists=false;
    for(const auto& item:{std::pair{target,&existing},std::pair{temp,&tempExists},std::pair{backup,&backupExists}}){
        auto result=FileState(item.first,*item.second);if(!result)return result;
    }
    if(tempExists||backupExists)return Bad("transaction artifact exists; file retained for recovery");
    // Never replace an unowned/invalid on-disk file merely because its name fits.
    if(existing&&!Find(level.id))return Bad("unowned player file already exists");
    const auto json=ExportJson(level);
    {std::ofstream stream(temp,std::ios::binary|std::ios::trunc);
        if(!stream)return Bad("cannot create temporary player file");
        stream.write(json.data(),static_cast<std::streamsize>(json.size()));stream.flush();
        const bool written=bool(stream);stream.close();
        if(!written||!stream){Cleanup(temp);return Bad("cannot write temporary player file");}
    }
    if(existing){auto moved=Rename(target,backup);if(!moved){Cleanup(temp);return moved;}}
    auto committed=Rename(temp,target);
    if(!committed){if(existing){auto rollback=Rename(backup,target);if(!rollback)return Bad(committed.message+"; rollback failed: "+rollback.message);}Cleanup(temp);return committed;}
    if(!PvzpMod::PublishPlayerLevels(next)){
        Cleanup(target);if(existing){auto rollback=Rename(backup,target);if(!rollback)return Bad("catalog publish and rollback failed: "+rollback.message);}
        return Bad("catalog publication refused");
    }
    gLevels=std::move(next);if(existing)Cleanup(backup);return Good();
}
Result Remove(std::string_view id){
    if(!IsPlayerId(id))return Bad("unsafe player ID");
    if(!Find(id))return Bad("player level not found");
    auto safe=SafeRoot(gRoot,false);if(!safe)return safe;
    auto next=gLevels;next.erase(std::remove_if(next.begin(),next.end(),[&](const auto& c){return c.id==id;}),next.end());
    if(!CanPublish(next))return Bad("player catalog conflicts with bundled levels");
    const auto target=gRoot/(std::string(id)+".json"),backup=gRoot/(std::string(id)+".json.bak");
    bool exists=false,backupExists=false;
    auto state=FileState(target,exists);if(!state)return state;
    state=FileState(backup,backupExists);if(!state)return state;
    if(!exists||backupExists)return Bad("missing player file or transaction backup already exists");
    auto moved=Rename(target,backup);if(!moved)return moved;
    std::error_code ec;fs::remove(backup,ec);
    if(ec){auto rollback=Rename(backup,target);return Bad("cannot remove player file: "+ec.message()+(rollback?"":"; rollback failed: "+rollback.message));}
    // next is a strict subset of the published, validated player catalog.
    PvzpMod::PublishPlayerLevels(next);gLevels=std::move(next);return Good();
}
}
