/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef NANCY_TTFFONT_H
#define NANCY_TTFFONT_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

namespace Graphics {
class Font;
}

namespace Nancy {

struct FontRegistry;

// Nancy16 stopped shipping glyph atlases. Its FONT chunk is a registry naming
// real Windows typefaces (Tahoma, Arial, Courier New, Lucida Handwriting) with a
// pixel height and a LOGFONT weight, and there is no glyph data anywhere on the
// discs - the game asked Windows for the face and let GDI rasterise it.
//
// A port cannot ship those faces: Tahoma in particular is proprietary and was
// never part of Microsoft's freely distributable Core Fonts. So the aliases are
// resolved onto the metric-compatible open substitutes ScummVM already bundles
// in dists/engine-data/fonts.dat, the same approach zvision takes in
// engines/zvision/text/truetype_font.cpp.
//
// Caveat worth knowing: Liberation Sans is metric-compatible with Arial, not
// with Tahoma, and 7 of the 12 registry entries are Tahoma. Expect small
// string-width differences against the original.
class TTFFontProvider {
public:
	~TTFFontProvider();

	// The authored line height of a font this provider built, i.e. the FONT
	// chunk's own pixel height. This is what the original advanced a line by, and
	// it is NOT `font->getFontHeight()`: that returns the *substitute's* hhea line
	// height, which is a property of Liberation Sans rather than of the game.
	// Falls back to getFontHeight() for a font the provider did not build.
	uint lineHeight(const Graphics::Font *font) const;

	// Builds one Graphics::Font per registry entry. Safe to call with a null
	// registry, in which case the provider simply resolves nothing.
	//
	// `scalePercent` is the game's own FontScale setting - the options screen's
	// Small/Large Text radio, which sends Engine_Font 100.0 or 127.0, and which
	// the retail INI persists as FontScale (x10, so 1270 is the shipped default).
	// Calling init() again rebuilds every face at the new scale; callers must
	// re-compose anything that has already rasterised text.
	void init(const FontRegistry *registry, uint scalePercent = 100);

	// The scale the built faces were made at.
	uint getScalePercent() const { return _scalePercent; }

	// By registry alias, e.g. "UIFont". Nancy16 synthesizes a bold variant of
	// every face rather than storing one, so `bold` adds 300 to the weight.
	// Returns nullptr if the alias is unknown or FreeType is unavailable.
	const Graphics::Font *get(const Common::String &alias, bool bold = false) const;

	// By registry id. The inline <fN> markup code in the string tables names a
	// FONT chunk by id rather than by alias (nancy18 uses <f16>, the ATM face).
	// The bold flag comes from the entry's own weight unless `forceBold` is set,
	// which is what the <bN> code does - see the markup notes in nduipanel.cpp.
	const Graphics::Font *getByID(uint32 id, bool forceBold = false) const;

private:
	// The provider owns these; Common::Array<Font>-by-value would double-free.
	Common::HashMap<Common::String, Graphics::Font *, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _fonts;

	// Authored pixel height per built font, for lineHeight(). Twelve entries at
	// most, so a linear scan costs less than hashing a pointer would.
	struct FontMetric {
		const Graphics::Font *font;
		uint lineHeight;
	};
	Common::Array<FontMetric> _metrics;

	// Kept so getByID() can turn an id into the alias the map is keyed on.
	const FontRegistry *_registry = nullptr;

	uint _scalePercent = 100;
};

} // End of namespace Nancy

#endif // NANCY_TTFFONT_H
