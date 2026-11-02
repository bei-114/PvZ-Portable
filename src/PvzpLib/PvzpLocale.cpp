// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpLocale.h"
#include "PvzpStringFile.h"
#include "PvzpDebug.h"
#include "PvzpFontFallback.h"
#include "PvzpUiStrings.h"
#include "SexyAppBase.h"
#include <atomic>
#include <utility>

namespace {
PvzpStringTable gEnglish;
PvzpStringTable gChinese;
PvzpStringTable gLiteralChinese;
PvzpStringTable gEnglishKeys;
PvzpStringTable gChineseKeys;
PvzpStringTable gLiteralEnglish;
std::atomic<bool> gChineseLanguage{false};
std::function<void()> gOnChange;
std::atomic<std::uint64_t> gGeneration{0};
std::atomic<bool> gInitialized{false};

// Original, provisional project translations; not attributed to a fan group.
struct Entry { const char* key; const char* en; const char* zh; };
constexpr Entry kStrings[] = {
    {"OPTIONS_LANGUAGE", "Language: English", "语言：简体中文"},
    {"OPTIONS_MUSIC_LABEL", "Music", "音乐"},
    {"OPTIONS_SOUNDFX", "Sound FX", "音效"},
    {"OPTIONS_3D_ACCELERATION", "3D Acceleration", "3D 加速"},
    {"OPTIONS_FULL_SCREEN", "Full Screen", "全屏"},
    {"DIALOG_BUTTON_OK", "OK", "确定"},
    {"DIALOG_BUTTON_CANCEL", "CANCEL", "取消"},
    {"DIALOG_BUTTON_YES", "YES", "是"},
    {"DIALOG_BUTTON_NO", "NO", "否"},
    {"VIEW_ALMANAC_BUTTON", "View Almanac", "查看图鉴"},
    {"RESTART_LEVEL", "Restart Level", "重新开始"},
    {"MAIN_MENU_BUTTON", "Main Menu", "主菜单"},
    {"BACK_TO_GAME", "Back to Game", "继续游戏"},
    {"CREDITS", "Credits", "制作人员"},
    {"RESTART_LABEL", "RESTART", "重新开始"},
    {"ALMANAC_MOD_PLANTS", "Mod Plants", "模组植物"},
    {"ALMANAC_STANDARD_PLANTS", "Standard Plants", "原版植物"},
    {"ALMANAC_HEALTH", "Health", "生命值"},
    {"ALMANAC_BEHAVIOR", "Behavior", "特性"},
    {"ALMANAC_BEHAVIOR_NUT", "Defensive plant", "防御型植物"},
    {"ALMANAC_BEHAVIOR_BRAIN", "Attracts zombies", "吸引僵尸"},
    {"ALMANAC_BEHAVIOR_PRISM", "Projectile amplifier", "子弹增幅器"},
    {"ALMANAC_DAMAGE_STEP", "Damage step", "伤害增量"},
    {"ALMANAC_PULSE_INTERVAL", "Pulse interval", "脉冲间隔"},
    {"ALMANAC_TICKS", "ticks", "帧"},
};

void RebuildReverseMaps()
{
    gChineseKeys.clear();
    for (const auto& [key, value] : gChinese)
        if (!value.empty() && gEnglish.contains(key))
            gChineseKeys.try_emplace(value, key);
    gLiteralEnglish.clear();
    for (const auto& [english, chinese] : gLiteralChinese)
        if (!chinese.empty())
            gLiteralEnglish.try_emplace(chinese, english);
}

void ReloadChinese()
{
    PvzpStringTable next;
    for (const auto& entry : kStrings)
        next[entry.key] = entry.zh;
    // Text files use the same [KEY] / UTF-8 value format as LawnStrings.
    std::string overlay;
    if (gSexyAppBase->ReadUTF8StringFromFile("properties/locales/zh-CN.txt", &overlay))
    {
        PvzpStringTable parsed;
        if (PvzpStringListParseItems(overlay, parsed))
            for (auto& [key, value] : parsed)
                next[key] = std::move(value);
        else
            PvzpLogLn("Ignoring malformed Chinese locale overlay");
    }
    gChinese = std::move(next);
    RebuildReverseMaps();
}

void ApplyLanguage(std::string_view language)
{
    // Restore snapshot, then overlay. Do not clear unrelated runtime/mod strings.
    for (const auto& [key, value] : gChinese)
        if (!gEnglish.contains(key))
            gSexyAppBase->mStringProperties.erase(key);
    for (const auto& [key, value] : gEnglish)
        gSexyAppBase->SetString(key, value);
    if (language == "zh-CN")
    {
        ReloadChinese();
        for (const auto& [key, value] : gChinese)
            gSexyAppBase->SetString(key, value);
    }
    // Publish the completed tables after loading-thread initialization.
    gChineseLanguage.store(language == "zh-CN", std::memory_order_release);
    ++gGeneration;
}
}

void PvzpLocaleInitialize()
{
    if (gInitialized.load(std::memory_order_acquire) || !gSexyAppBase)
        return;
    gEnglish = gSexyAppBase->mStringProperties;
    for (const auto& entry : kStrings)
    {
        gEnglish.try_emplace(entry.key, entry.en);
        gChinese[entry.key] = entry.zh;
        gLiteralChinese.try_emplace(entry.en, entry.zh);
    }
    for (const auto& [key, value] : gEnglish)
        if (!value.empty())
            gEnglishKeys.try_emplace(value, key);
    RebuildReverseMaps();
    std::string saved;
    gSexyAppBase->RegistryReadString("Language", &saved);
    PvzpRegisterUiStrings();
    ApplyLanguage(saved == "zh-CN" && PvzpFontFallbackAvailable() ? "zh-CN" : "en");
    gInitialized.store(true, std::memory_order_release);
}

void PvzpLocaleShutdown()
{
    gInitialized.store(false, std::memory_order_release);
    gOnChange = {};
    gEnglish.clear();
    gChinese.clear();
    gEnglishKeys.clear();
    gChineseKeys.clear();
    gLiteralChinese.clear();
    gLiteralEnglish.clear();
    gChineseLanguage.store(false, std::memory_order_release);
    ++gGeneration;
}

std::string_view PvzpLocaleGetLanguage() { return PvzpLocaleIsChinese() ? "zh-CN" : "en"; }
bool PvzpLocaleIsChinese() { return gChineseLanguage.load(std::memory_order_acquire); }
std::uint64_t PvzpLocaleGetGeneration() { return gGeneration.load(); }

bool PvzpLocaleSetLanguage(std::string_view language)
{
    if (language != "en" && language != "zh-CN")
        return false;
    PvzpLocaleInitialize();
    if (!gInitialized.load(std::memory_order_acquire))
        return false;
    if (language == PvzpLocaleGetLanguage())
        return true;
    if (language == "zh-CN" && !PvzpFontFallbackAvailable())
        return false;
    if (!gSexyAppBase->RegistryWriteString("Language", std::string(language)))
        return false;
    ApplyLanguage(language);
    if (gOnChange)
        gOnChange();
    return true;
}

void PvzpLocaleSetChangeCallback(std::function<void()> callback)
{
    gOnChange = std::move(callback);
}

void PvzpLocaleRegisterLiteral(std::string_view english, std::string_view chinese)
{
    gLiteralChinese[std::string(english)] = chinese;
    RebuildReverseMaps();
}

std::string_view PvzpLocaleTranslateLiteral(std::string_view english)
{
    if (!gInitialized.load(std::memory_order_acquire))
        return english;
    if (!PvzpLocaleIsChinese())
    {
        // Widgets may have copied a translated value during a Chinese startup.
        // Reverse only exact known translations, never arbitrary CJK names.
        auto key = gChineseKeys.find(english);
        if (key != gChineseKeys.end())
        {
            auto original = gEnglish.find(key->second);
            if (original != gEnglish.end())
                return original->second;
        }
        auto literal = gLiteralEnglish.find(english);
        return literal != gLiteralEnglish.end() ? std::string_view(literal->second) : english;
    }
    // Prefer explicit resource translations, so a supplied pack can replace
    // provisional translations even when callers still hold English labels.
    auto key = gEnglishKeys.find(english);
    if (key != gEnglishKeys.end())
    {
        auto translated = gChinese.find(key->second);
        if (translated != gChinese.end())
            return translated->second;
    }
    auto literal = gLiteralChinese.find(english);
    return literal != gLiteralChinese.end() ? std::string_view(literal->second) : english;
}
