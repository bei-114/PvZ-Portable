// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpFontFallback.h"
#include "PvzpDebug.h"
#include "SexyAppBase.h"
#include "graphics/ImageFont.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImage.h"
#include "paklib/PakInterface.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// The vendored header retains its own public-domain/MIT license.
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

using namespace Sexy;
namespace {
constexpr std::size_t kMaxGlyphs = 2048;
constexpr std::size_t kMaxBitmapBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxFontBytes = 32 * 1024 * 1024;
struct Glyph {
    std::unique_ptr<MemoryImage> image;
    int x = 0;
    int y = 0;
    std::size_t bytes = 0;
    std::uint64_t used = 0;
};
struct FontState {
    std::vector<std::uint8_t> data;
    stbtt_fontinfo font{};
    bool attempted = false;
    bool ready = false;
    std::map<std::pair<int, char32_t>, Glyph> glyphs;
    std::size_t bitmapBytes = 0;
    std::uint64_t clock = 0;
};
FontState gFont;
std::mutex gMutex;

bool ReadPakFont(std::vector<std::uint8_t>& data)
{
    if (!gPakInterface)
        return false;
    PFILE* file = p_fopen("fonts/PvzpCJK.ttf", "rb");
    if (!file)
        return false;
    bool valid = p_fseek(file, 0, SEEK_END) == 0;
    const int size = valid ? p_ftell(file) : -1;
    valid = size >= 12 && static_cast<std::size_t>(size) <= kMaxFontBytes &&
        p_fseek(file, 0, SEEK_SET) == 0;
    if (valid)
    {
        data.resize(size);
        valid = p_fread(data.data(), 1, size, file) == static_cast<std::size_t>(size);
    }
    p_fclose(file);
    if (!valid)
        data.clear();
    return valid;
}

bool ReadSDLFont(const std::string& path, std::vector<std::uint8_t>& data)
{
    SDL_RWops* file = SDL_RWFromFile(path.c_str(), "rb");
    if (!file)
        return false;
    const Sint64 size = SDL_RWsize(file);
    bool valid = size >= 12 && size <= static_cast<Sint64>(kMaxFontBytes);
    if (valid)
    {
        data.resize(static_cast<std::size_t>(size));
        valid = SDL_RWread(file, data.data(), 1, data.size()) == data.size();
    }
    SDL_RWclose(file);
    if (!valid)
        data.clear();
    return valid;
}

bool LoadFontData(std::vector<std::uint8_t>& data)
{
    if (ReadPakFont(data) || ReadSDLFont("fonts/PvzpCJK.ttf", data))
        return true;
    // SDL Android's relative RWops also searches packaged APK assets; native
    // builds keep the font beside the executable even when -resdir changes cwd.
    char* base = SDL_GetBasePath();
    if (!base)
        return false;
    std::string path = std::string(base) + "fonts/PvzpCJK.ttf";
    SDL_free(base);
    return ReadSDLFont(path, data);
}

bool EnsureFont()
{
    if (gFont.attempted)
        return gFont.ready;
    if (!gSexyAppBase)
        return false;
    gFont.attempted = true;
    if (!LoadFontData(gFont.data))
    {
        PvzpLogLn("CJK fallback font missing: fonts/PvzpCJK.ttf");
        return false;
    }
    const int offset = stbtt_GetFontOffsetForIndex(gFont.data.data(), 0);
    if (offset < 0 || static_cast<std::size_t>(offset) >= gFont.data.size() ||
        !stbtt_InitFont(&gFont.font, gFont.data.data(), offset))
    {
        PvzpLogLn("Invalid CJK fallback TrueType font");
        return false;
    }
    gFont.ready = true;
    return true;
}

int PixelHeight(ImageFont* font)
{
    font->Prepare();
    // Match the visible bitmap capital height rather than its shadow/padding.
    return std::clamp(font->GetAscent() - font->GetAscentPadding(), 8, 128);
}

float FontScale(int pixels)
{
    // Noto's em square gives a CJK glyph a natural full-width advance.
    return stbtt_ScaleForMappingEmToPixels(&gFont.font, static_cast<float>(pixels));
}

bool HasNativeGlyph(ImageFont* font, char32_t ch)
{
    font->Prepare();
    ch = font->GetMappedChar(ch);
    for (const auto& layer : font->mActiveLayerList)
    {
        const auto& chars = layer.mBaseFontLayer->mCharDataMap;
        auto it = chars.find(ch);
        if (it != chars.end() && (it->second.mWidth > 0 || it->second.mImageRect.mWidth > 0))
            return true;
    }
    return false;
}

int Advance(char32_t ch, int pixels)
{
    int advance = 0, bearing = 0;
    stbtt_GetCodepointHMetrics(&gFont.font, static_cast<int>(ch), &advance, &bearing);
    return std::max(1, static_cast<int>(std::lround(advance * FontScale(pixels))));
}

void EvictFor(std::size_t bytes)
{
    while (!gFont.glyphs.empty() &&
        (gFont.glyphs.size() >= kMaxGlyphs || gFont.bitmapBytes + bytes > kMaxBitmapBytes))
    {
        auto oldest = std::min_element(gFont.glyphs.begin(), gFont.glyphs.end(),
            [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
        gFont.bitmapBytes -= oldest->second.bytes;
        gFont.glyphs.erase(oldest);
    }
}

Glyph& GetGlyph(char32_t ch, int pixels)
{
    const auto key = std::make_pair(pixels, ch);
    auto found = gFont.glyphs.find(key);
    if (found != gFont.glyphs.end())
    {
        found->second.used = ++gFont.clock;
        return found->second;
    }
    Glyph glyph;
    const float scale = FontScale(pixels);
    int width = 0, height = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&gFont.font, scale, scale,
        static_cast<int>(ch), &width, &height, &glyph.x, &glyph.y);
    if (bitmap && width > 0 && height > 0)
    {
        glyph.image = std::make_unique<MemoryImage>(gSexyAppBase);
        glyph.image->Create(width, height);
        auto bits = glyph.image->GetBits();
        for (int i = 0; i < width * height; ++i)
            bits[i] = (static_cast<std::uint32_t>(bitmap[i]) << 24) | 0x00ffffffU;
        glyph.image->BitsChanged();
        glyph.bytes = static_cast<std::size_t>(width) * height * sizeof(std::uint32_t);
    }
    stbtt_FreeBitmap(bitmap, nullptr);
    EvictFor(glyph.bytes);
    glyph.used = ++gFont.clock;
    gFont.bitmapBytes += glyph.bytes;
    return gFont.glyphs.emplace(key, std::move(glyph)).first->second;
}
}

bool PvzpFontFallbackAvailable()
{
    std::scoped_lock lock(gMutex);
    return EnsureFont();
}

bool PvzpFontFallbackNeedsGlyph(ImageFont* font, char32_t ch)
{
    if (ch < 0x80 || ch > 0x10ffff || HasNativeGlyph(font, ch))
        return false;
    std::scoped_lock lock(gMutex);
    return EnsureFont() && stbtt_FindGlyphIndex(&gFont.font, static_cast<int>(ch)) != 0;
}

int PvzpFontFallbackCharWidth(ImageFont* font, char32_t ch)
{
    const int pixels = PixelHeight(font);
    std::scoped_lock lock(gMutex);
    return EnsureFont() ? Advance(ch, pixels) : 0;
}

bool PvzpFontFallbackDrawString(ImageFont* font, Graphics* g, int x, int y,
    std::string_view text, const Color& color, int* width)
{
    std::size_t offset = 0;
    char32_t ch = 0;
    bool needsFallback = false;
    while (UTF8DecodeNext(text, offset, ch))
        if (PvzpFontFallbackNeedsGlyph(font, ch))
        {
            needsFallback = true;
            break;
        }
    if (!needsFallback)
        return false;

    const int pixels = PixelHeight(font);
    const int startX = x;
    std::size_t runStart = 0;
    offset = 0;
    GraphicsStateGuard state(*g);
    g->SetColorizeImages(true);
    g->SetColor(color);
    while (offset < text.size())
    {
        const std::size_t charStart = offset;
        if (!UTF8DecodeNext(text, offset, ch))
            break;
        if (!PvzpFontFallbackNeedsGlyph(font, ch))
            continue;
        // Flush the native run before drawing a CJK glyph. This recursive call
        // contains no fallback glyphs and happens before ImageFont's global lock.
        if (charStart > runStart)
        {
            auto native = text.substr(runStart, charStart - runStart);
            font->DrawStringEx(g, x, y, native, color, nullptr, nullptr);
            x += font->StringWidth(native);
        }
        {
            std::scoped_lock lock(gMutex);
            Glyph& glyph = GetGlyph(ch, pixels);
            if (glyph.image)
                g->DrawImage(glyph.image.get(), x + glyph.x, y + glyph.y);
            x += Advance(ch, pixels);
        }
        runStart = offset;
    }
    if (runStart < text.size())
    {
        auto native = text.substr(runStart);
        font->DrawStringEx(g, x, y, native, color, nullptr, nullptr);
        x += font->StringWidth(native);
    }
    if (width)
        *width = x - startX;
    return true;
}

void PvzpFontFallbackShutdown()
{
    std::scoped_lock lock(gMutex);
    gFont.glyphs.clear();
    gFont.bitmapBytes = 0;
    gFont.data.clear();
    gFont.ready = false;
    gFont.attempted = false;
}
