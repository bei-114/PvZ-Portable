// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpStringTable.h"
#include <algorithm>
#include <utility>

namespace {
std::string_view Trim(std::string_view text)
{
    constexpr std::string_view whitespace = " \r\n\t\v\f";
    auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}
}

bool PvzpStringListParseItems(std::string_view text, PvzpStringTable& strings)
{
    if (text.find('\0') != std::string_view::npos)
        return false;
    if (text.starts_with("\xef\xbb\xbf"))
        text.remove_prefix(3);
    PvzpStringTable parsed;
    std::size_t position = 0;
    for (;;)
    {
        const auto begin = text.find('[', position);
        if (begin == std::string_view::npos)
        {
            if (!Trim(text.substr(position)).empty())
                return false;
            strings = std::move(parsed);
            return true;
        }
        const auto end = text.find(']', begin + 1);
        if (end == std::string_view::npos)
            return false;
        auto name = Trim(text.substr(begin + 1, end - begin - 1));
        if (name.empty() || name.find('[') != std::string_view::npos)
            return false;
        std::string key(name);
        for (auto& ch : key)
            if (ch >= 'a' && ch <= 'z')
                ch -= 'a' - 'A';
        const auto next = text.find('[', end + 1);
        auto valueView = Trim(text.substr(end + 1,
            next == std::string_view::npos ? text.size() - end - 1 : next - end - 1));
        std::string value(valueView);
        value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
        parsed[std::move(key)] = std::move(value);
        position = next == std::string_view::npos ? text.size() : next;
    }
}

// Standalone no-app test: c++ -std=c++20 -DPVZP_STRING_TABLE_SELF_TEST
// src/PvzpLib/PvzpStringTable.cpp -o /tmp/pvzp-string-table-test
#ifdef PVZP_STRING_TABLE_SELF_TEST
#include <cstdlib>
#include <iostream>
int main()
{
    int checks = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            std::cerr << "FAIL: " << label << '\n';
            std::exit(1);
        }
    };
    PvzpStringTable table;
    check(PvzpStringListParseItems("\xef\xbb\xbf \r\n[ music ]\r\n音乐\r\n第二行\r\n[empty]\n", table), "BOM/CRLF/CJK");
    check(table.at("MUSIC") == "音乐\n第二行", "UTF8/multiline preserved, CR removed");
    check(table.at("EMPTY").empty(), "empty value");
    check(PvzpStringListParseItems("[key] first\n[KEY] second", table), "duplicate headers");
    check(table.size() == 1 && table.at("KEY") == "second", "case normalization/last wins");
    const auto snapshot = table;
    check(!PvzpStringListParseItems("[VALID] changed\n[BROKEN", table), "unclosed header rejected");
    check(table == snapshot, "failure leaves table unchanged");
    check(!PvzpStringListParseItems("[] value", table), "empty name rejected");
    check(!PvzpStringListParseItems("[BAD[NEXT] value", table), "nested name rejected");
    check(!PvzpStringListParseItems(std::string_view("[KEY] x\0y", 9), table), "NUL rejected");
    check(table == snapshot, "all malformed inputs transactional");
    check(!PvzpStringListParseItems("not a string table", table), "missing header rejected");
    check(PvzpStringListParseItems(" \r\n\t", table) && table.empty(), "empty table accepted");
    std::cout << "PASS: " << checks << " no-app string-table checks\n";
}
#endif
