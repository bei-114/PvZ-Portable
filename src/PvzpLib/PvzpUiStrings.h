// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

// Original provisional project translations, not a fan-translation pack.
// Called by locale initialization before publishing tables to renderer threads.
void PvzpRegisterUiStrings();
std::string PvzpUiText(std::string_view theText);
// Resolve actual LawnStrings resource key first; retain human-readable fallback
// names instead of showing internal uppercase plant/zombie identifiers.
std::string PvzpUiResourceName(std::string_view theKey, std::string_view theFallback = {});
// Clip at UTF-8 character boundaries; never rewrite pack IDs/diagnostic details.
std::string PvzpUiClip(std::string_view theText, std::size_t theMaxChars);
