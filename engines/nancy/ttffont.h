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

// Nancy16+ FONT chunks name Windows typefaces instead of shipping glyph data.
// Original faces can be supplied via nancy_fontpath; otherwise the open
// substitutes in fonts.dat are used.
class TTFFontProvider {
public:
	~TTFFontProvider();

	// The FONT chunk's pixel height, which the original used as the line advance.
	// Falls back to getFontHeight() for fonts not built by this provider.
	uint lineHeight(const Graphics::Font *font) const;

	// Builds one Graphics::Font per registry entry; registry may be null.
	// scalePercent is the game's FontScale setting and only affects entries with
	// the scaling flag. Calling init() again rebuilds every face.
	void init(const FontRegistry *registry, uint scalePercent = 100);

	// The scale the built faces were made at.
	uint getScalePercent() const { return _scalePercent; }

	// By registry alias, e.g. "UIFont". Returns nullptr if the alias is unknown
	// or FreeType is unavailable.
	const Graphics::Font *get(const Common::String &alias, bool bold = false) const;

	// By registry id, as used by the <fN> and <bN> markup codes. Bold follows the
	// entry's weight unless forceBold is set.
	const Graphics::Font *getByID(uint32 id, bool forceBold = false) const;

private:
	// The provider owns these; Common::Array<Font>-by-value would double-free.
	Common::HashMap<Common::String, Graphics::Font *, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _fonts;

	// Authored pixel height per built font, for lineHeight()
	struct FontMetric {
		const Graphics::Font *font;
		uint lineHeight;
	};
	Common::Array<FontMetric> _metrics;

	// Used by getByID() to map an id to its alias
	const FontRegistry *_registry = nullptr;

	uint _scalePercent = 100;
};

} // End of namespace Nancy

#endif // NANCY_TTFFONT_H
