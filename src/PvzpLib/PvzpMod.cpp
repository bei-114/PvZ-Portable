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
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "PvzpCommon.h"
#include "SexyAppFramework/Common.h"

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
			return true;
		}

	private:
		const std::string& mText;
		size_t mPos;

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
				return ParseObject(theValue);
			case '[':
				return ParseArray(theValue);
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
			const char* aStart = mText.c_str() + mPos;
			char* aEnd = nullptr;
			const double aNumber = std::strtod(aStart, &aEnd);
			if (aEnd == aStart)
				return false;
			mPos += static_cast<size_t>(aEnd - aStart);
			theValue.mType = JsonValue::Type::Number;
			theValue.mNumber = aNumber;
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
					if (mPos + 4 > mText.size())
						return false;
					const std::string aHex = mText.substr(mPos, 4);
					mPos += 4;
					const unsigned int aCode = static_cast<unsigned int>(std::strtoul(aHex.c_str(), nullptr, 16));
					// BMP -> UTF-8
					if (aCode < 0x80)
					{
						theOut.push_back(static_cast<char>(aCode));
					}
					else if (aCode < 0x800)
					{
						theOut.push_back(static_cast<char>(0xC0 | (aCode >> 6)));
						theOut.push_back(static_cast<char>(0x80 | (aCode & 0x3F)));
					}
					else
					{
						theOut.push_back(static_cast<char>(0xE0 | (aCode >> 12)));
						theOut.push_back(static_cast<char>(0x80 | ((aCode >> 6) & 0x3F)));
						theOut.push_back(static_cast<char>(0x80 | (aCode & 0x3F)));
					}
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
				theValue.mObject[aKey] = std::move(aChild);
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

	/* ---------------- 模组数据 ---------------- */
	std::map<std::string, int> gPlantValues;   // "NAME|field" -> 值
	std::map<std::string, int> gZombieValues;  // "NAME|field" -> 值
	std::map<std::string, int> gGlobalValues;  // "field" -> 值
	bool gInitialized = false;

	std::string MakeKey(const char* theName, const char* theField)
	{
		return std::string(theName ? theName : "") + "|" + (theField ? theField : "");
	}

	void ApplyObject(const JsonValue& theObject, std::map<std::string, int>& theTarget, const char* thePrefix)
	{
		for (const auto& aPair : theObject.mObject)
		{
			if (aPair.second.mType != JsonValue::Type::Object)
				continue;
			const std::string aName = thePrefix ? std::string(thePrefix) + aPair.first : aPair.first;
			for (const auto& aField : aPair.second.mObject)
			{
				if (aField.second.mType != JsonValue::Type::Number)
					continue;
				theTarget[aName + "|" + aField.first] = static_cast<int>(aField.second.mNumber);
			}
		}
	}

	void ApplyFlat(const JsonValue& theObject, std::map<std::string, int>& theTarget)
	{
		for (const auto& aPair : theObject.mObject)
		{
			if (aPair.second.mType == JsonValue::Type::Number)
				theTarget[aPair.first] = static_cast<int>(aPair.second.mNumber);
		}
	}

	bool LoadOneMod(const std::filesystem::path& theFile)
	{
		std::ifstream aStream(theFile, std::ios::binary);
		if (!aStream)
		{
			PvzpLogLn("[mod] 打不开 {}", theFile.string());
			return false;
		}
		std::stringstream aBuffer;
		aBuffer << aStream.rdbuf();
		const std::string aText = aBuffer.str();

		JsonValue aRoot;
		JsonParser aParser(aText);
		if (!aParser.Parse(aRoot) || aRoot.mType != JsonValue::Type::Object)
		{
			PvzpLogLn("[mod] JSON 解析失败：{}", theFile.string());
			return false;
		}

		std::string aName = theFile.parent_path().filename().string();
		auto aNameIt = aRoot.mObject.find("name");
		if (aNameIt != aRoot.mObject.end() && aNameIt->second.mType == JsonValue::Type::String)
			aName = aNameIt->second.mString;

		auto aPlants = aRoot.mObject.find("plants");
		if (aPlants != aRoot.mObject.end() && aPlants->second.mType == JsonValue::Type::Object)
			ApplyObject(aPlants->second, gPlantValues, nullptr);

		auto aZombies = aRoot.mObject.find("zombies");
		if (aZombies != aRoot.mObject.end() && aZombies->second.mType == JsonValue::Type::Object)
			ApplyObject(aZombies->second, gZombieValues, nullptr);

		auto aGlobals = aRoot.mObject.find("globals");
		if (aGlobals != aRoot.mObject.end() && aGlobals->second.mType == JsonValue::Type::Object)
			ApplyFlat(aGlobals->second, gGlobalValues);

		PvzpLogLn("[mod] 已加载模组：{}", aName);
		return true;
	}
}

namespace PvzpMod
{
	void Init()
	{
		if (gInitialized)
			return;
		gInitialized = true;

		namespace fs = std::filesystem;
		std::vector<fs::path> aFiles;

		std::error_code aError;
		fs::path aModsDir;
		try
		{
			aModsDir = fs::path(Sexy::GetAppDataPath("mods"));
			fs::create_directories(aModsDir, aError);
			if (fs::exists(aModsDir, aError))
			{
				for (const auto& aEntry : fs::directory_iterator(aModsDir, aError))
				{
					if (!aEntry.is_directory())
						continue;
					const fs::path aManifest = aEntry.path() / "mod.json";
					if (fs::exists(aManifest, aError))
						aFiles.push_back(aManifest);
				}
			}
		}
		catch (const std::exception& aException)
		{
			PvzpLogLn("[mod] 扫描模组目录失败：{}", aException.what());
			return;
		}

		std::sort(aFiles.begin(), aFiles.end());
		int aLoaded = 0;
		for (const auto& aFile : aFiles)
		{
			if (LoadOneMod(aFile))
				aLoaded++;
		}
		PvzpLogLn("[mod] 模组目录：{}，共加载 {} 个模组", aModsDir.string(), aLoaded);
	}

	bool HasMods()
	{
		return !gPlantValues.empty() || !gZombieValues.empty() || !gGlobalValues.empty();
	}

	int PlantInt(const char* thePlantName, const char* theField, int theDefault)
	{
		if (gPlantValues.empty())
			return theDefault;
		auto aIt = gPlantValues.find(MakeKey(thePlantName, theField));
		return aIt == gPlantValues.end() ? theDefault : aIt->second;
	}

	int ZombieInt(const char* theZombieName, const char* theField, int theDefault)
	{
		if (gZombieValues.empty())
			return theDefault;
		auto aIt = gZombieValues.find(MakeKey(theZombieName, theField));
		return aIt == gZombieValues.end() ? theDefault : aIt->second;
	}

	int GlobalInt(const char* theField, int theDefault)
	{
		if (gGlobalValues.empty())
			return theDefault;
		auto aIt = gGlobalValues.find(theField ? theField : "");
		return aIt == gGlobalValues.end() ? theDefault : aIt->second;
	}
}
