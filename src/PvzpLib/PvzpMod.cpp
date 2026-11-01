/*
 * PvZ-Portable 模组接口（PvzpMod）实现
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "PvzpMod.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <cmath>
#include <charconv>
#include <set>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "PvzpCommon.h"
#include "SexyAppFramework/Common.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"

namespace
{
	/* ---------------- 迷你 JSON 解析（够用就好） ---------------- */
	struct JsonValue
	{
		enum class Type { Null, Bool, Number, String, Array, Object };

		Type mType = Type::Null;
		bool mBool = false;
		double mNumber = 0.0;
		std::string mString;
		std::vector<JsonValue> mArray;
		std::map<std::string, JsonValue> mObject;
	};

	class JsonParser
	{
	public:
		explicit JsonParser(const std::string& theText) : mText(theText), mPos(0) {}

		bool Parse(JsonValue& theOut)
		{
			SkipSpace();
			if (!ParseValue(theOut))
				return false;
			SkipSpace();
			return mPos == mText.size();
		}

	private:
		const std::string& mText;
		size_t mPos;
		int mDepth = 0;
		bool mDuplicate = false;

		void SkipSpace()
		{
			while (mPos < mText.size() && std::isspace(static_cast<unsigned char>(mText[mPos])))
				mPos++;
		}

		bool ParseValue(JsonValue& theValue)
		{
			SkipSpace();
			if (mPos >= mText.size())
				return false;

			const char aChar = mText[mPos];
			switch (aChar)
			{
			case '{':
				return ++mDepth <= 32 && ParseObject(theValue) && (--mDepth, true);
			case '[':
				return ++mDepth <= 32 && ParseArray(theValue) && (--mDepth, true);
			case '"':
			{
				theValue.mType = JsonValue::Type::String;
				return ParseString(theValue.mString);
			}
			case 't':
				if (mText.compare(mPos, 4, "true") == 0)
				{
					mPos += 4;
					theValue.mType = JsonValue::Type::Bool;
					theValue.mBool = true;
					return true;
				}
				return false;
			case 'f':
				if (mText.compare(mPos, 5, "false") == 0)
				{
					mPos += 5;
					theValue.mType = JsonValue::Type::Bool;
					theValue.mBool = false;
					return true;
				}
				return false;
			case 'n':
				if (mText.compare(mPos, 4, "null") == 0)
				{
					mPos += 4;
					theValue.mType = JsonValue::Type::Null;
					return true;
				}
				return false;
			default:
				return ParseNumber(theValue);
			}
		}

		bool ParseNumber(JsonValue& theValue)
		{
			const size_t aStart=mPos;
			if (mPos<mText.size() && mText[mPos]=='-') ++mPos;
			if (mPos>=mText.size()) return false;
			if (mText[mPos]=='0') ++mPos;
			else if (mText[mPos]>='1' && mText[mPos]<='9')
				while (mPos<mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos]))) ++mPos;
			else return false;
			if (mPos<mText.size() && mText[mPos]=='.') {
				++mPos; const auto begin=mPos;
				while(mPos<mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos]))) ++mPos;
				if(begin==mPos)return false;
			}
			if (mPos<mText.size() && (mText[mPos]=='e'||mText[mPos]=='E')) {
				++mPos;if(mPos<mText.size() && (mText[mPos]=='+'||mText[mPos]=='-')) ++mPos;
				const auto begin=mPos;
				while(mPos<mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos]))) ++mPos;
				if(begin==mPos)return false;
			}
			const char* end=mText.data()+mPos;
			double value=0;
			auto result=std::from_chars(mText.data()+aStart,end,value,std::chars_format::general);
			if(result.ec!=std::errc{} || result.ptr!=end || !std::isfinite(value)) return false;
			theValue.mType=JsonValue::Type::Number;theValue.mNumber=value;
			return true;
		}

		bool ParseString(std::string& theOut)
		{
			if (mPos >= mText.size() || mText[mPos] != '"')
				return false;
			mPos++;
			theOut.clear();
			while (mPos < mText.size())
			{
				const char aChar = mText[mPos++];
				if (aChar == '"')
					return true;
				if (aChar != '\\')
				{
					if (static_cast<unsigned char>(aChar)<0x20) return false;
					theOut.push_back(aChar);
					continue;
				}
				if (mPos >= mText.size())
					return false;
				const char aEsc = mText[mPos++];
				switch (aEsc)
				{
				case '"': theOut.push_back('"'); break;
				case '\\': theOut.push_back('\\'); break;
				case '/': theOut.push_back('/'); break;
				case 'b': theOut.push_back('\b'); break;
				case 'f': theOut.push_back('\f'); break;
				case 'n': theOut.push_back('\n'); break;
				case 'r': theOut.push_back('\r'); break;
				case 't': theOut.push_back('\t'); break;
				case 'u':
				{
					auto readHex = [&]() -> unsigned {
						if (mPos+4>mText.size()) return 0xFFFFFFFFU;
						unsigned code=0;
						for (int n=0;n<4;++n) {
							char ch=mText[mPos++];
							if (ch>='0'&&ch<='9') code=(code<<4)+(ch-'0');
							else if (ch>='a'&&ch<='f') code=(code<<4)+(ch-'a'+10);
							else if (ch>='A'&&ch<='F') code=(code<<4)+(ch-'A'+10);
							else return 0xFFFFFFFFU;
						}
						return code;
					};
					unsigned code=readHex();
					if (code>=0xD800&&code<=0xDBFF) {
						if (mPos+2>mText.size()||mText[mPos]!='\\'||mText[mPos+1]!='u')return false;
						mPos+=2;unsigned low=readHex();
						if(low<0xDC00||low>0xDFFF)return false;
						code=0x10000+((code-0xD800)<<10)+(low-0xDC00);
					} else if (code>=0xDC00&&code<=0xDFFF)return false;
					if(code>0x10FFFF)return false;
					if(code<0x80)theOut.push_back(char(code));
					else if(code<0x800){theOut.push_back(char(0xC0|(code>>6)));theOut.push_back(char(0x80|(code&63)));}
					else if(code<0x10000){theOut.push_back(char(0xE0|(code>>12)));theOut.push_back(char(0x80|((code>>6)&63)));theOut.push_back(char(0x80|(code&63)));}
					else {theOut.push_back(char(0xF0|(code>>18)));theOut.push_back(char(0x80|((code>>12)&63)));theOut.push_back(char(0x80|((code>>6)&63)));theOut.push_back(char(0x80|(code&63)));}
					break;
				}
				default:
					return false;
				}
			}
			return false;
		}

		bool ParseObject(JsonValue& theValue)
		{
			theValue.mType = JsonValue::Type::Object;
			mPos++; // {
			SkipSpace();
			if (mPos < mText.size() && mText[mPos] == '}')
			{
				mPos++;
				return true;
			}
			while (mPos < mText.size())
			{
				SkipSpace();
				std::string aKey;
				if (!ParseString(aKey))
					return false;
				SkipSpace();
				if (mPos >= mText.size() || mText[mPos] != ':')
					return false;
				mPos++;
				JsonValue aChild;
				if (!ParseValue(aChild))
					return false;
				if (!theValue.mObject.emplace(std::move(aKey), std::move(aChild)).second) return false;
				SkipSpace();
				if (mPos < mText.size() && mText[mPos] == ',')
				{
					mPos++;
					continue;
				}
				if (mPos < mText.size() && mText[mPos] == '}')
				{
					mPos++;
					return true;
				}
				return false;
			}
			return false;
		}

		bool ParseArray(JsonValue& theValue)
		{
			theValue.mType = JsonValue::Type::Array;
			mPos++; // [
			SkipSpace();
			if (mPos < mText.size() && mText[mPos] == ']')
			{
				mPos++;
				return true;
			}
			while (mPos < mText.size())
			{
				JsonValue aChild;
				if (!ParseValue(aChild))
					return false;
				theValue.mArray.push_back(std::move(aChild));
				SkipSpace();
				if (mPos < mText.size() && mText[mPos] == ',')
				{
					mPos++;
					continue;
				}
				if (mPos < mText.size() && mText[mPos] == ']')
				{
					mPos++;
					return true;
				}
				return false;
			}
			return false;
		}
	};

	/* Snapshot built once before definitions cache; queries remain cheap. */
	using Map=std::map<std::string,int>;
	struct Loaded { PvzpMod::ModInfo info; Map plants,zombies,globals; std::vector<PvzpMod::CustomPlant> customs; };
	std::vector<Loaded> gModules;
	std::vector<PvzpMod::ModInfo> gInfos;
	std::vector<PvzpMod::CustomPlant> gCustomPlants;
	std::vector<PvzpMod::Diagnostic> gDiagnostics;
	Map gPlantValues,gZombieValues,gGlobalValues;
	bool gInitialized=false;
	std::string MakeKey(const char* n,const char* f){return std::string(n?n:"")+"|"+(f?f:"");}
	void Report(const std::filesystem::path& path,const std::string& message) {
		gDiagnostics.push_back({path.string(),message});
		PvzpLogLn("[mod] {}: {}",path.string(),message);
	}
	bool Utf8(const std::string& text){
		for(size_t i=0;i<text.size();){
			unsigned char c=text[i++];if(c<0x80)continue;
			int continuation=c>=0xC2&&c<=0xDF?1:c>=0xE0&&c<=0xEF?2:c>=0xF0&&c<=0xF4?3:-1;
			if(continuation<0||i+continuation>text.size())return false;
			unsigned char first=static_cast<unsigned char>(text[i]);
			if((c==0xE0&&first<0xA0)||(c==0xED&&first>=0xA0)||(c==0xF0&&first<0x90)||(c==0xF4&&first>=0x90))return false;
			while(continuation--){if((static_cast<unsigned char>(text[i++])&0xC0)!=0x80)return false;}
		}
		return true;
	}
	bool Integer(const JsonValue& v,int low,int high,int& out){
		if(v.mType!=JsonValue::Type::Number||!std::isfinite(v.mNumber)||
			v.mNumber<low||v.mNumber>high||std::trunc(v.mNumber)!=v.mNumber)return false;
		out=static_cast<int>(v.mNumber);return true;
	}
	bool Identifier(const std::string& s){
		if(s.empty()||s.size()>120||s.find("..")!=std::string::npos)return false;
		for(unsigned char c:s)if(c=='/'||c=='\\'||c<0x20||c==0x7f)return false;
		return true;
	}
	bool KnownPlant(const std::string& s){for(const auto& d:gPlantDefs)if(d.mPlantName&&s==d.mPlantName)return true;return false;}
	bool KnownZombie(const std::string& s){for(const auto& d:gZombieDefs)if(d.mZombieName&&s==d.mZombieName)return true;return false;}
	bool Section(const JsonValue& v,bool plant,Map& out,std::string& error){
		if(v.mType!=JsonValue::Type::Object){error="section must be an object";return false;}
		for(const auto& [name,fields]:v.mObject){
			if(!(plant?KnownPlant(name):KnownZombie(name))){error="unknown entity: "+name;return false;}
			if(fields.mType!=JsonValue::Type::Object){error="entity fields must be an object: "+name;return false;}
			for(const auto& [key,value]:fields.mObject){
				int low=-1,high=-1,n=0;
				if(plant){
					if(key=="cost"){low=0;high=9990;}
					else if(key=="refresh"||key=="launchRate"){low=1;high=60000;}
					else if(key=="health"){low=1;high=10000000;}
				}else{
					if(key=="health"){low=1;high=10000000;}
					else if(key=="pickWeight"){low=0;high=100000;}
					else if(key=="value"){low=0;high=10000;}
					else if(key=="startingLevel"){low=1;high=50;}
				}
				if(low<0){error="unknown field: "+name+"."+key;return false;}
				if(!Integer(value,low,high,n)){error="invalid integer/range: "+name+"."+key;return false;}
				out[name+"|"+key]=n;
			}
		}
		return true;
	}
	bool Manifest(const std::filesystem::path& file,Loaded& mod,std::string& error){
		std::error_code ec;
		const auto bytes=std::filesystem::file_size(file,ec);
		if(ec||bytes>65536){error="unreadable/oversized mod.json (max 64 KiB)";return false;}
		std::ifstream stream(file,std::ios::binary);if(!stream){error="cannot open mod.json";return false;}
		std::string text((std::istreambuf_iterator<char>(stream)),{});
		if(text.size()!=bytes||!Utf8(text)){error="invalid file size or UTF-8";return false;}
		JsonValue root;
		if(!JsonParser(text).Parse(root)||root.mType!=JsonValue::Type::Object){error="invalid JSON, duplicate key, or trailing content";return false;}
		mod.info.folder=file.parent_path().filename().string();
		mod.info.id=mod.info.folder;mod.info.name=mod.info.id;
		for(const auto& [key,v]:root.mObject){
			if(key=="schemaVersion"){
				int n=0;if(!Integer(v,1,2,n)){error="unsupported schemaVersion";return false;}
				if(n!=2){error="schemaVersion must be 2 when present";return false;}
			}else if(key=="id"||key=="name"){
				if(v.mType!=JsonValue::Type::String||v.mString.empty()||v.mString.size()>120){error="invalid "+key;return false;}
				if(key=="id")mod.info.id=v.mString;else mod.info.name=v.mString;
			}else if(key=="priority"){
				if(!Integer(v,-1000,1000,mod.info.priority)){error="invalid priority";return false;}
			}else if(key=="enabled"){
				if(v.mType!=JsonValue::Type::Bool){error="enabled must be boolean";return false;}
				mod.info.enabled=v.mBool;
			}else if(key=="plants"||key=="zombies"){
				if(!Section(v,key=="plants",key=="plants"?mod.plants:mod.zombies,error))return false;
			}else if(key=="customPlants"){
				if(v.mType!=JsonValue::Type::Array){error="customPlants must be an array";return false;}
				std::set<int> slots;
				for(const auto& e:v.mArray){
					if(e.mType!=JsonValue::Type::Object){error="customPlants entry must be an object";return false;}
					PvzpMod::CustomPlant c;
					for(const auto& [k,val]:e.mObject){
						if(k=="slot"){if(!Integer(val,0,3,c.slot)){error="customPlants.slot";return false;}}
						else if(k=="name"||k=="tint"||k=="behavior"){if(val.mType!=JsonValue::Type::String||val.mString.empty()||val.mString.size()>40){error="customPlants."+k;return false;}
							if(k=="name")c.name=val.mString;
							else if(k=="tint")c.tint=val.mString;
							else {if(val.mString!="nut"&&val.mString!="brain"){error="customPlants.behavior must be nut or brain";return false;}c.behavior=val.mString;}}
						else if(k=="cost"){if(!Integer(val,0,9990,c.cost)){error="customPlants.cost";return false;}}
						else if(k=="health"){if(!Integer(val,1,10000000,c.health)){error="customPlants.health";return false;}}
						else if(k=="step"){if(!Integer(val,100,10000,c.step)){error="customPlants.step";return false;}}
						else if(k=="badChance"){if(!Integer(val,0,100,c.badChance)){error="customPlants.badChance";return false;}}
						else if(k=="events"||k=="badEvents"){
							if(val.mType!=JsonValue::Type::Array||val.mArray.size()>16){error="customPlants."+k;return false;}
							std::vector<std::string> list;
							for(const auto& item:val.mArray){if(item.mType!=JsonValue::Type::String||item.mString.empty()||item.mString.size()>40){error="customPlants event token";return false;}list.push_back(item.mString);}
							if(k=="events")c.events=list;else c.badEvents=list;
						}else{error="unknown customPlants field: "+k;return false;}
					}
					if(c.slot<0||c.name.empty()||!slots.insert(c.slot).second){error="customPlants slot/name/duplicate";return false;}
					mod.customs.push_back(std::move(c));
				}
			}else if(key=="globals"){
				if(v.mType!=JsonValue::Type::Object){error="globals must be an object";return false;}
				for(const auto& [k,val]:v.mObject){int n=0;
					if(k!="startingSun"||!Integer(val,0,9990,n)){error="unknown/invalid global: "+k;return false;}
					mod.globals[k]=n;
				}
			}else{error="unknown top-level field: "+key;return false;}
		}
		if(!Identifier(mod.info.id)){error="invalid id/path traversal";return false;}
		return true;
	}
	void Rebuild(){
		gPlantValues.clear();gZombieValues.clear();gGlobalValues.clear();gInfos.clear();gCustomPlants.clear();
		for(const auto& m:gModules){gInfos.push_back(m.info);if(!m.info.enabled)continue;
			for(const auto& [k,v]:m.plants)gPlantValues[k]=v;
			for(const auto& [k,v]:m.zombies)gZombieValues[k]=v;
			for(const auto& [k,v]:m.globals)gGlobalValues[k]=v;
			for(const auto& c:m.customs){auto it=std::find_if(gCustomPlants.begin(),gCustomPlants.end(),[&](const PvzpMod::CustomPlant& x){return x.slot==c.slot;});if(it!=gCustomPlants.end())*it=c;else gCustomPlants.push_back(c);}
		}
	}
}
namespace PvzpMod {
 bool LoadFromDirectory(const std::filesystem::path& dir){
	 gModules.clear();gDiagnostics.clear();Rebuild();
	 namespace fs=std::filesystem;std::error_code ec;
	 if(!fs::exists(dir,ec))return !ec;
	 if(ec||!fs::is_directory(dir,ec)||ec){Report(dir,"mods directory inaccessible");return false;}
	 std::vector<fs::path> files;
	 fs::directory_iterator it(dir,fs::directory_options::skip_permission_denied,ec),end;
	 if(ec){Report(dir,"cannot scan mods directory: "+ec.message());return false;}
	 for(;it!=end;it.increment(ec)){
		 if(ec){Report(dir,"directory scan error: "+ec.message());return false;}
		 const fs::path folder=it->path();auto state=fs::symlink_status(folder,ec);
		 if(ec){Report(folder,"cannot stat folder");ec.clear();continue;}
		 if(fs::is_symlink(state)){Report(folder,"symlink folder refused");continue;}
		 if(!fs::is_directory(state))continue;
		 const auto file=folder/"mod.json";state=fs::symlink_status(file,ec);
		 if(ec==std::errc::no_such_file_or_directory){ec.clear();continue;}
		 if(ec){Report(folder,"cannot stat manifest");ec.clear();continue;}
		 if(fs::is_symlink(state)){Report(folder,"symlink mod.json refused");continue;}
		 if(fs::is_regular_file(state)) files.push_back(file);
		 else Report(folder,"mod.json is not a regular file");
		 if(files.size()>128){Report(dir,"too many mods (max 128)");return false;}
	 }
	 if(ec){Report(dir,"directory scan error: "+ec.message());return false;}
	 std::sort(files.begin(),files.end());std::set<std::string> ids;
	 for(const auto& file:files){Loaded mod;std::string error;
		 try {if(!Manifest(file,mod,error)){Report(file.parent_path(),error);continue;}}
		 catch(const std::exception& e){Report(file.parent_path(),e.what());continue;}
		 if(!ids.insert(mod.info.id).second){Report(file.parent_path(),"duplicate mod id: "+mod.info.id);continue;}
		 gModules.push_back(std::move(mod));
	 }
	 std::sort(gModules.begin(),gModules.end(),[](const Loaded& a,const Loaded& b){
		 return a.info.priority!=b.info.priority?a.info.priority<b.info.priority:a.info.folder<b.info.folder;
	 });Rebuild();return true;
 }
 void Init(){if(gInitialized)return;gInitialized=true;
	 const auto root=std::filesystem::path(Sexy::GetAppDataPath("mods"));
	 std::error_code ec;std::filesystem::create_directories(root,ec);
	 if(ec)Report(root,"cannot create mods directory: "+ec.message());
	 else LoadFromDirectory(root);
	 PvzpLogLn("[mod] {}: {} loaded, {} diagnostics (restart for changes)",root.string(),gInfos.size(),gDiagnostics.size());
 }
 bool SetEnabled(std::string_view id,bool enabled){for(auto& m:gModules)if(m.info.id==id){m.info.enabled=enabled;Rebuild();return true;}return false;}
 const std::vector<ModInfo>& List(){return gInfos;}
 const std::vector<Diagnostic>& Diagnostics(){return gDiagnostics;}
 const std::vector<CustomPlant>& CustomPlants(){return gCustomPlants;}
 uint64_t RulesFingerprint(){
	 uint64_t h=14695981039346656037ULL;
	 auto add=[&](std::string_view s){for(unsigned char c:s){h^=c;h*=1099511628211ULL;}h^=0;h*=1099511628211ULL;};
	 for(const auto& mod:gModules)if(mod.info.enabled){add(mod.info.id);add(std::to_string(mod.info.priority));}
	 auto values=[&](const char* section,const Map& fields){add(section);for(const auto& [k,v]:fields){add(k);add(std::to_string(v));}};
	 values("plants",gPlantValues);values("zombies",gZombieValues);values("globals",gGlobalValues);
	 return h;
 }
 void WarnSavedRules(uint64_t saved){
	 if(saved!=RulesFingerprint()){
		 Report("savegame","saved mod rules differ from currently enabled mods; existing entities retain saved stats; restart/new level for consistent balance");
	 }
 }
 bool HasMods(){return !gPlantValues.empty()||!gZombieValues.empty()||!gGlobalValues.empty();}
 int PlantInt(const char* n,const char* f,int def){auto i=gPlantValues.find(MakeKey(n,f));return i==gPlantValues.end()?def:i->second;}
 int ZombieInt(const char* n,const char* f,int def){auto i=gZombieValues.find(MakeKey(n,f));return i==gZombieValues.end()?def:i->second;}
 int GlobalInt(const char* f,int def){auto i=gGlobalValues.find(f?f:"");return i==gGlobalValues.end()?def:i->second;}
}
