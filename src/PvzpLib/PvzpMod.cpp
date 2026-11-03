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
#include <initializer_list>
#include <cmath>
#include <charconv>
#include <set>
#include <map>
#include <sstream>
#include <locale>
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
			while (mPos < mText.size() && (mText[mPos]==' '||mText[mPos]=='\t'||mText[mPos]=='\r'||mText[mPos]=='\n'))
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
			// Locale-independent and available on older iOS libc++ versions,
			// unlike floating-point from_chars. Reject conversion range errors.
			const std::string token=mText.substr(aStart,mPos-aStart);
			std::istringstream number(token);
			number.imbue(std::locale::classic());
			double value=0;
			number>>value;
			if(number.fail()||number.peek()!=std::char_traits<char>::eof()||!std::isfinite(value))return false;
			bool nonzeroMantissa=false;
			for(char ch:token){if(ch=='e'||ch=='E')break;if(ch>='1'&&ch<='9')nonzeroMantissa=true;}
			// Some streams silently round tiny values to zero. Such a value
			// must not bypass an integer lower bound (e.g. sunDrop:1e-10000).
			if(nonzeroMantissa&&(value==0||std::abs(value)<std::numeric_limits<double>::min()))return false;
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
	struct Loaded {
		PvzpMod::ModInfo info; Map plants,zombies,globals;
		std::vector<PvzpMod::CustomPlant> customs;
		std::vector<PvzpMod::CustomZombie> customZombies;
		std::vector<PvzpMod::CustomLevel> customLevels;
	};
	std::vector<Loaded> gModules;
	std::vector<PvzpMod::ModInfo> gInfos;
	std::vector<PvzpMod::CustomPlant> gCustomPlants;
	std::vector<PvzpMod::CustomZombie> gCustomZombies;
	std::vector<PvzpMod::CustomLevel> gCustomLevels, gBundledLevels, gPlayerLevels;
	// Persistent parse/save diagnostics plus transient effective-rule conflicts.
	// Rebuild replaces the latter so repeated toggles do not accumulate stale warnings.
	std::vector<PvzpMod::Diagnostic> gPersistentDiagnostics, gDiagnostics;
	Map gPlantValues,gZombieValues,gGlobalValues;
	bool gInitialized=false;
	uint64_t gSnapshotRevision=0;
	std::string MakeKey(const char* n,const char* f){return std::string(n?n:"")+"|"+(f?f:"");}
	void Report(const std::filesystem::path& path,const std::string& message) {
		gPersistentDiagnostics.push_back({path.string(),message});
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
	bool DisplayName(const std::string& s){
		if(s.empty()||s.size()>40)return false;
		for(unsigned char c:s)if(c<0x20||c==0x7f)return false;
		return true;
	}
	bool EventToken(const std::string& token){
		// Runtime consumers may parse the numeric suffix with stoi. Validate the
		// complete ASCII grammar and bounds here, before publishing any definition.
		if(token.empty()||token.size()>40)return false;
		if(token=="chill"||token=="chill:row"||token=="freeze:row"||token=="plant:random"||token=="destroy:plant")return true;
		if(token=="zombie:NORMAL"||token=="zombie:GARGANTUAR"||token=="zombie:BUNGEE"||
			token=="zombie:DIGGER"||token=="zombie:BALLOON"||token=="zombie:CONEHEAD"||token=="zombie:BUCKETHEAD")return true;
		size_t prefix=0;int high=0;
		if(token.compare(0,4,"sun:")==0){prefix=4;high=9990;}
		else if(token.compare(0,5,"heal:")==0){prefix=5;high=10000000;}
		else if(token.compare(0,11,"damage:row:")==0){prefix=11;high=10000;}
		else if(token.compare(0,9,"push:row:")==0){prefix=9;high=160;}
		else return false;
		if(prefix==token.size())return false;
		for(size_t i=prefix;i<token.size();++i)if(token[i]<'0'||token[i]>'9')return false;
		int value=0;
		const auto result=std::from_chars(token.data()+prefix,token.data()+token.size(),value);
		return result.ec==std::errc{}&&result.ptr==token.data()+token.size()&&value<=high;
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
					else if(key=="speedPercent"){low=25;high=300;}
					else if(key=="damagePercent"){low=25;high=500;}
					else if(key=="helmetHealth"||key=="shieldHealth"){low=1;high=10000000;}
					else if(key=="slowResistancePercent"){low=0;high=100;}
					else if(key=="sunDrop"){low=0;high=1000;}
					else if(key=="tintR"||key=="tintG"||key=="tintB"){low=0;high=255;}
				}
				if(low<0){error="unknown field: "+name+"."+key;return false;}
				if(!Integer(value,low,high,n)){error="invalid integer/range: "+name+"."+key;return false;}
				out[name+"|"+key]=n;
			}
		}
		return true;
	}
	bool StableId(const std::string& text,size_t maximum){
		if(text.empty()||text.size()>maximum)return false;
		for(unsigned char ch:text)
			if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='.'||ch=='-'))return false;
		return true;
	}
	bool TextField(const JsonValue& value,std::string& out,size_t maximum,bool description=false){
		if(value.mType!=JsonValue::Type::String||value.mString.size()>maximum||
			value.mString.find('\0')!=std::string::npos||!Utf8(value.mString))return false;
		if(!description){
			if(value.mString.empty())return false;
			for(unsigned char ch:value.mString)if(ch<0x20||ch==0x7f)return false;
		}
		out=value.mString;return true;
	}
	bool ChoiceField(const JsonValue& value,std::string& out,std::initializer_list<std::string_view> choices){
		if(value.mType!=JsonValue::Type::String)return false;
		for(auto choice:choices)if(value.mString==choice){out=value.mString;return true;}
		return false;
	}
	bool CustomZombieSection(const JsonValue& value,Loaded& mod,std::string& error){
		if(value.mType!=JsonValue::Type::Array||value.mArray.size()>64){error="customZombies must be an array (max 64 entries)";return false;}
		std::set<std::string> ids;
		for(const auto& entry:value.mArray){
			if(entry.mType!=JsonValue::Type::Object){error="customZombies entry must be an object";return false;}
			PvzpMod::CustomZombie c;
			for(const auto& [key,v]:entry.mObject){
				bool ok=false;
				if(key=="id")ok=TextField(v,c.id,96)&&StableId(c.id,96);
				else if(key=="name")ok=TextField(v,c.name,96);
				else if(key=="base")ok=ChoiceField(v,c.base,{"ZOMBIE","CONEHEAD_ZOMBIE","BUCKETHEAD_ZOMBIE","FOOTBALL_ZOMBIE","SCREEN_DOOR_ZOMBIE","NEWSPAPER_ZOMBIE"});
				else if(key=="head")ok=ChoiceField(v,c.head,{"seed_packet","tombstone","pea","snowpea","firepea","sunflower","chomper","melon","gatling","pumpkin","coffee","garlic","cherry","magnet","tallnut"});
				else if(key=="ability")ok=ChoiceField(v,c.ability,{"none","seed_jam","raise","shoot_ice","shoot_fire","heal","bite","shoot_melon","shoot_pea","shield_aura","rally","lane_dash","cherry_charge","magnet_steal","rebound"});
				else if(key=="health")ok=Integer(v,1,10000000,c.health);
				else if(key=="armor")ok=Integer(v,0,10000000,c.armor);
				else if(key=="interval")ok=Integer(v,50,60000,c.interval);
				else if(key=="magnitude")ok=Integer(v,1,1000,c.magnitude);
				else if(key=="speedPercent")ok=Integer(v,25,300,c.speedPercent);
				else if(key=="damagePercent")ok=Integer(v,25,500,c.damagePercent);
				else if(key=="slowResistancePercent")ok=Integer(v,0,100,c.slowResistancePercent);
				else if(key=="sunDrop")ok=Integer(v,0,1000,c.sunDrop);
				else if(key=="tintR")ok=Integer(v,0,255,c.tintR);
				else if(key=="tintG")ok=Integer(v,0,255,c.tintG);
				else if(key=="tintB")ok=Integer(v,0,255,c.tintB);
				else {error="unknown customZombies field: "+key;return false;}
				if(!ok){error="invalid customZombies."+key;return false;}
			}
			if(c.id.empty()||c.name.empty()||!ids.insert(c.id).second){error="customZombies requires unique id and name";return false;}
			mod.customZombies.push_back(std::move(c));
		}
		return true;
	}
	bool CustomLevelSection(const JsonValue& value,Loaded& mod,std::string& error){
		if(value.mType!=JsonValue::Type::Array||value.mArray.size()>64){error="customLevels must be an array (max 64 entries)";return false;}
		std::set<std::string> ids;
		for(const auto& entry:value.mArray){
			if(entry.mType!=JsonValue::Type::Object){error="customLevels entry must be an object";return false;}
			PvzpMod::CustomLevel c;
			for(const auto& [key,v]:entry.mObject){
				bool ok=false;
				if(key=="id")ok=TextField(v,c.id,128)&&StableId(c.id,128);
				else if(key=="name")ok=TextField(v,c.name,128);
				else if(key=="description")ok=TextField(v,c.description,1024,true);
				else if(key=="startingSun")ok=Integer(v,0,9990,c.startingSun);
				else if(key=="waves"){
					if(v.mType!=JsonValue::Type::Array||v.mArray.empty()||v.mArray.size()>100){error="customLevels.waves must contain 1..100 waves";return false;}
					for(const auto& wave:v.mArray){
						if(wave.mType!=JsonValue::Type::Object){error="customLevels wave must be an object";return false;}
						PvzpMod::CustomWave w;
						for(const auto& [wk,wv]:wave.mObject){
							if(wk=="delay"){
								if(!Integer(wv,1,60000,w.delay)){error="invalid customLevels wave delay";return false;}
							}else if(wk=="spawns"){
								if(wv.mType!=JsonValue::Type::Array||wv.mArray.empty()||wv.mArray.size()>50){error="customLevels wave requires 1..50 spawns";return false;}
								int total=0;
								for(const auto& spawn:wv.mArray){
									if(spawn.mType!=JsonValue::Type::Object){error="customLevels spawn must be an object";return false;}
									PvzpMod::CustomSpawn s;
									for(const auto& [sk,sv]:spawn.mObject){
										bool valid=false;
										// References may resolve in a later enabled mod. Validate
										// syntax here; runtime CanStart checks the effective registry.
										if(sk=="zombie")valid=TextField(sv,s.zombie,96)&&StableId(s.zombie,96);
										else if(sk=="row")valid=Integer(sv,-1,4,s.row);
										else if(sk=="count")valid=Integer(sv,1,50,s.count);
										else {error="unknown customLevels spawn field: "+sk;return false;}
										if(!valid){error="invalid customLevels spawn "+sk;return false;}
									}
									if(s.zombie.empty()||(total+=s.count)>50){error="customLevels requires zombie reference and at most 50 zombies per wave";return false;}
									w.spawns.push_back(std::move(s));
								}
							}else {error="unknown customLevels wave field: "+wk;return false;}
						}
						if(w.spawns.empty()){error="customLevels wave missing spawns";return false;}
						c.waves.push_back(std::move(w));
					}
					ok=true;
				}else {error="unknown customLevels field: "+key;return false;}
				if(!ok){error="invalid customLevels."+key;return false;}
			}
			if(c.id.empty()||c.name.empty()||c.waves.empty()||!ids.insert(c.id).second){error="customLevels requires unique id, name and waves";return false;}
			mod.customLevels.push_back(std::move(c));
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
			}else if(key=="customZombies"){
				if(!CustomZombieSection(v,mod,error))return false;
			}else if(key=="customLevels"){
				if(!CustomLevelSection(v,mod,error))return false;
			}else if(key=="customPlants"){
				if(v.mType!=JsonValue::Type::Array){error="customPlants must be an array";return false;}
				std::set<int> slots;
				for(const auto& e:v.mArray){
					if(e.mType!=JsonValue::Type::Object){error="customPlants entry must be an object";return false;}
					PvzpMod::CustomPlant c;
					for(const auto& [k,val]:e.mObject){
						if(k=="slot"){if(!Integer(val,0,int(NUM_CUSTOM_PLANT_SLOTS)-1,c.slot)){error="customPlants.slot";return false;}}
						else if(k=="name"||k=="tint"||k=="behavior"||k=="base"){if(val.mType!=JsonValue::Type::String||val.mString.empty()||val.mString.size()>40){error="customPlants."+k;return false;}
							if(k=="name"){if(!DisplayName(val.mString)){error="invalid customPlants.name";return false;}c.name=val.mString;}
							else if(k=="tint"){
								if(val.mString!="gold"&&val.mString!="blue"&&val.mString!="red"&&val.mString!="green"&&val.mString!="purple"){
									error="customPlants.tint must be gold, blue, red, green, or purple";return false;
								}c.tint=val.mString;
							}
							else if(k=="base") {
								if(val.mString!="WALL_NUT"&&val.mString!="MARIGOLD"){error="customPlants.base must be WALL_NUT or MARIGOLD";return false;}
								c.base=val.mString;
							}
							else {if(val.mString!="nut"&&val.mString!="brain"&&val.mString!="prism"){error="customPlants.behavior must be nut, brain, or prism";return false;}c.behavior=val.mString;}}
						else if(k=="cost"){if(!Integer(val,0,9990,c.cost)){error="customPlants.cost";return false;}}
						else if(k=="health"){if(!Integer(val,1,10000000,c.health)){error="customPlants.health";return false;}}
						else if(k=="refresh"){if(!Integer(val,1,60000,c.refresh)){error="customPlants.refresh";return false;}}
						else if(k=="step"){if(!Integer(val,100,10000,c.step)){error="customPlants.step";return false;}}
						else if(k=="badChance"){if(!Integer(val,0,100,c.badChance)){error="customPlants.badChance";return false;}}
						else if(k=="interval"){
							if(!Integer(val,0,60000,c.interval)||(c.interval>0&&c.interval<50)){error="customPlants.interval must be 0 or 50..60000";return false;}
						}
						else if(k=="events"||k=="badEvents"||k=="pulseEvents"||k=="foodEvents"){
							if(val.mType!=JsonValue::Type::Array||val.mArray.size()>16){error="customPlants."+k;return false;}
							std::vector<std::string> list;
							for(const auto& item:val.mArray){if(item.mType!=JsonValue::Type::String||!EventToken(item.mString)){error="invalid customPlants."+k+" token (unknown event, malformed argument, or out of range)";return false;}list.push_back(item.mString);}
							if(k=="events")c.events=std::move(list);
							else if(k=="badEvents")c.badEvents=std::move(list);
							else if(k=="pulseEvents")c.pulseEvents=std::move(list);
							else c.foodEvents=std::move(list);
						}else{error="unknown customPlants field: "+k;return false;}
					}
					if(c.behavior=="prism"&&!e.mObject.contains("base"))c.base="MARIGOLD";
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
		gCustomZombies.clear();gCustomLevels.clear();
		gDiagnostics=gPersistentDiagnostics;
		std::map<int,const PvzpMod::ModInfo*> owners;
		std::map<std::string,const PvzpMod::ModInfo*> zombieOwners,levelOwners;
		auto mergeRegistry=[&](const auto& entries,auto& effective,auto& registryOwners,const Loaded& m,const char* section){
			for(const auto& entry:entries){
				auto it=std::find_if(effective.begin(),effective.end(),[&](const auto& previous){return previous.id==entry.id;});
				if(it!=effective.end()){
					const auto* previous=registryOwners.at(entry.id);
					const std::string message=std::string(section)+" id "+entry.id+" collision: "+
						m.info.id+" ("+m.info.folder+") overrides "+previous->id+" ("+previous->folder+") by priority/folder order";
					gDiagnostics.push_back({m.info.folder,message});
					PvzpLogLn("[mod] {}: {}",m.info.folder,message);
					*it=entry;
				}else effective.push_back(entry);
				registryOwners[entry.id]=&m.info;
			}
		};
		for(const auto& m:gModules){gInfos.push_back(m.info);if(!m.info.enabled)continue;
			for(const auto& [k,v]:m.plants)gPlantValues[k]=v;
			for(const auto& [k,v]:m.zombies)gZombieValues[k]=v;
			for(const auto& [k,v]:m.globals)gGlobalValues[k]=v;
			mergeRegistry(m.customZombies,gCustomZombies,zombieOwners,m,"customZombies");
			mergeRegistry(m.customLevels,gCustomLevels,levelOwners,m,"customLevels");
			for(const auto& c:m.customs){
				auto it=std::find_if(gCustomPlants.begin(),gCustomPlants.end(),[&](const PvzpMod::CustomPlant& x){return x.slot==c.slot;});
				if(it!=gCustomPlants.end()){
					const auto* previous=owners[c.slot];
					const std::string message="customPlants slot "+std::to_string(c.slot)+" collision: "+
						m.info.id+" ("+m.info.folder+") overrides "+previous->id+" ("+previous->folder+") by priority/folder order";
					gDiagnostics.push_back({m.info.folder,message});
					PvzpLogLn("[mod] {}: {}",m.info.folder,message);
					*it=c;
				}else gCustomPlants.push_back(c);
				owners[c.slot]=&m.info;
			}
		}
		gBundledLevels=gCustomLevels;
		for(const auto& level:gPlayerLevels)
			if(std::none_of(gCustomLevels.begin(),gCustomLevels.end(),[&](const auto& c){return c.id==level.id;}))
				gCustomLevels.push_back(level);
		++gSnapshotRevision;
	}
}
// Private adapter shared only with the editor backend; no second JSON parser.
namespace PvzpPlayerLevelParsing {
 bool CanPublish(const std::vector<PvzpMod::CustomLevel>& levels){
	 if(levels.size()>32)return false;
	 std::set<std::string> ids;
	 for(const auto& level:levels){
		 if(level.id.compare(0,7,"player.")!=0||level.id.size()<=7||!StableId(level.id,128)||
			 level.id.find("..")!=std::string::npos||!ids.insert(level.id).second)return false;
		 if(std::any_of(gBundledLevels.begin(),gBundledLevels.end(),[&](const auto& c){return c.id==level.id;}))return false;
	 }
	 return true;
 }
 bool Parse(std::string_view text,PvzpMod::CustomLevel& out,std::string& error){
	 if(text.size()>65536||!Utf8(std::string(text))){error="invalid UTF-8 or file exceeds 64 KiB";return false;}
	 JsonValue root;
	 if(!JsonParser(std::string(text)).Parse(root)||root.mType!=JsonValue::Type::Object){error="invalid JSON, duplicate key, or trailing content";return false;}
	 if(root.mObject.size()!=2||!root.mObject.contains("schemaVersion")||!root.mObject.contains("customLevels")){
		 error="player file permits only schemaVersion and customLevels";return false;
	 }
	 int version=0;
	 if(!Integer(root.mObject.at("schemaVersion"),2,2,version)){error="schemaVersion must be 2";return false;}
	 Loaded parsed;
	 if(!CustomLevelSection(root.mObject.at("customLevels"),parsed,error))return false;
	 if(parsed.customLevels.size()!=1){error="player file requires exactly one customLevel";return false;}
	 const auto& level=parsed.customLevels.front();
	 if(level.waves.size()>30){error="player level exceeds 30 waves";return false;}
	 for(const auto& wave:level.waves)if(wave.spawns.size()>12){error="player wave exceeds 12 spawn groups";return false;}
	 out=level;return true;
 }
}
namespace PvzpMod {
 bool LoadFromDirectory(const std::filesystem::path& dir){
	 gPlayerLevels.clear();gModules.clear();gPersistentDiagnostics.clear();gDiagnostics.clear();Rebuild();
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
 const std::vector<CustomZombie>& CustomZombies(){return gCustomZombies;}
 const std::vector<CustomLevel>& CustomLevels(){return gCustomLevels;}
 bool PublishPlayerLevels(const std::vector<CustomLevel>& levels){
	 if(!PvzpPlayerLevelParsing::CanPublish(levels))return false;
	 auto catalog=gBundledLevels;
	 catalog.insert(catalog.end(),levels.begin(),levels.end());
	 gPlayerLevels=levels;gCustomLevels=std::move(catalog);++gSnapshotRevision;
	 return true;
 }
 static uint64_t Fingerprint(bool includeLevels){
	 uint64_t h=14695981039346656037ULL;
	 auto add=[&](std::string_view s){for(unsigned char c:s){h^=c;h*=1099511628211ULL;}h^=0;h*=1099511628211ULL;};
	 for(const auto& mod:gModules)if(mod.info.enabled){add(mod.info.id);add(std::to_string(mod.info.priority));}
	 auto values=[&](const char* section,const Map& fields){add(section);for(const auto& [k,v]:fields){add(k);add(std::to_string(v));}};
	 values("plants",gPlantValues);values("zombies",gZombieValues);values("globals",gGlobalValues);
	 // Hash effective definitions in slot order, not manifest array order. Keep
	 // fingerprints unchanged for older numeric-only packs with no custom plants.
	 if(!gCustomPlants.empty()){
		 add("customPlants");
		 std::vector<const CustomPlant*> ordered;
		 for(const auto& c:gCustomPlants)ordered.push_back(&c);
		 std::sort(ordered.begin(),ordered.end(),[](const CustomPlant* a,const CustomPlant* b){return a->slot<b->slot;});
		 auto events=[&](const char* field,const std::vector<std::string>& list){add(field);add(std::to_string(list.size()));for(const auto& token:list)add(token);};
		 for(const auto* c:ordered){
			 add(std::to_string(c->slot));add(c->name);add(c->tint);add(c->behavior);
				 add(c->base);add(std::to_string(c->refresh));
			 add(std::to_string(c->cost));add(std::to_string(c->health));add(std::to_string(c->step));
			 add(std::to_string(c->badChance));add(std::to_string(c->interval));
			 events("events",c->events);events("badEvents",c->badEvents);
			 events("pulseEvents",c->pulseEvents);events("foodEvents",c->foodEvents);
		 }
	 }
	 // Registry order is observable in the tools/campaign menu: first encounter
	 // fixes the position; a collision replaces the whole entry in that position.
	 // Wave and spawn order are gameplay rules, never sort them for hashing.
	 if(!gCustomZombies.empty()){
		 add("customZombies");add(std::to_string(gCustomZombies.size()));
		 for(const auto& c:gCustomZombies){
			 add(c.id);add(c.name);add(c.base);add(c.head);add(c.ability);
			 for(int n:{c.health,c.armor,c.interval,c.magnitude,c.speedPercent,c.damagePercent,
				 c.slowResistancePercent,c.sunDrop,c.tintR,c.tintG,c.tintB})add(std::to_string(n));
		 }
	 }
	 if(includeLevels&&!gBundledLevels.empty()){
		 add("customLevels");add(std::to_string(gBundledLevels.size()));
		 for(const auto& c:gBundledLevels){
			 add(c.id);add(c.name);add(c.description);add(std::to_string(c.startingSun));
			 add(std::to_string(c.waves.size()));
			 for(const auto& w:c.waves){
				 add("wave");add(std::to_string(w.delay));add(std::to_string(w.spawns.size()));
				 for(const auto& s:w.spawns){add(s.zombie);add(std::to_string(s.row));add(std::to_string(s.count));}
			 }
		 }
	 }
	 return h;
 }
 uint64_t RulesFingerprint(){return Fingerprint(true);}
 uint64_t GameplayFingerprint(){return Fingerprint(false);}
 void WarnSavedRules(uint64_t saved){
	 if(saved!=RulesFingerprint()){
		 Report("savegame","saved mod rules differ from currently enabled mods; existing entities retain saved stats; restart/new level for consistent balance");
	 }
 }
 uint64_t SnapshotRevision(){return gSnapshotRevision;}
 bool HasMods(){return !gPlantValues.empty()||!gZombieValues.empty()||!gGlobalValues.empty()||!gCustomPlants.empty()||!gCustomZombies.empty()||!gCustomLevels.empty();}
 int PlantInt(const char* n,const char* f,int def){auto i=gPlantValues.find(MakeKey(n,f));return i==gPlantValues.end()?def:i->second;}
 int ZombieInt(const char* n,const char* f,int def){auto i=gZombieValues.find(MakeKey(n,f));return i==gZombieValues.end()?def:i->second;}
 int GlobalInt(const char* f,int def){auto i=gGlobalValues.find(f?f:"");return i==gGlobalValues.end()?def:i->second;}
}
