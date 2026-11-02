// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef PVZP_LOCALE_H
#define PVZP_LOCALE_H

#include <cstdint>
#include <functional>
#include <string_view>

// Call once AFTER English LawnStrings and localized properties have loaded.
// zh-CN overlays properties/locales/zh-CN.txt; absent entries retain English.
void PvzpLocaleInitialize();
// Clear copied properties/callbacks before application destruction (tests/restarts).
void PvzpLocaleShutdown();
std::string_view PvzpLocaleGetLanguage();
bool PvzpLocaleIsChinese();
// Persists Registry value "Language" (en / zh-CN). Returns false on save failure.
bool PvzpLocaleSetLanguage(std::string_view theLanguage);
std::uint64_t PvzpLocaleGetGeneration();
// Called synchronously on a successful switch: refresh copied labels/caches here.
// Do not destroy the active options dialog in this callback.
void PvzpLocaleSetChangeCallback(std::function<void()> theCallback);
// Translate exact registered/resource values in either direction, including
// Chinese labels copied by widgets before switching back to English. Unknown
// text/names pass through unchanged. Views expire on switch/registration.
std::string_view PvzpLocaleTranslateLiteral(std::string_view theText);
// For project-owned hardcoded UI/DBG labels. No fan-translation attribution.
void PvzpLocaleRegisterLiteral(std::string_view theEnglish, std::string_view theChinese);

#endif
