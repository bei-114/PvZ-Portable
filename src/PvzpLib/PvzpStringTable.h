// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef PVZP_STRING_TABLE_H
#define PVZP_STRING_TABLE_H

#include <map>
#include <string>
#include <string_view>

using PvzpStringTable = std::map<std::string, std::string, std::less<>>;
// Pure parser, independent of app/graphics/registry. On failure the output is
// unchanged. LawnStrings format: [ASCII_KEY], then UTF-8 value until next '['.
bool PvzpStringListParseItems(std::string_view theFileText, PvzpStringTable& theStrings);

#endif
