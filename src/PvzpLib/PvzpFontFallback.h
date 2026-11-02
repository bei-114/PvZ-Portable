// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef PVZP_FONT_FALLBACK_H
#define PVZP_FONT_FALLBACK_H

#include <string_view>
namespace Sexy {
class ImageFont;
class Graphics;
class Color;
}

// Loads the separately licensed fonts/PvzpCJK.ttf lazily, using existing pak I/O.
// No SDL_ttf/platform dependency. Latin letters keep the original bitmap font.
bool PvzpFontFallbackAvailable();
bool PvzpFontFallbackNeedsGlyph(Sexy::ImageFont* theFont, char32_t theChar);
int PvzpFontFallbackCharWidth(Sexy::ImageFont* theFont, char32_t theChar);
// Returns true when this mixed UTF-8 string was handled. Native-only runs are
// delegated back to ImageFont before its render lock is acquired (no recursion).
bool PvzpFontFallbackDrawString(Sexy::ImageFont* theFont, Sexy::Graphics* g,
    int theX, int theY, std::string_view theString, const Sexy::Color& theColor,
    int* theWidth);
// Call while the graphics context/application is still alive, before teardown.
void PvzpFontFallbackShutdown();

#endif
