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

#include "engines/nancy/ttffont.h"
#include "engines/nancy/enginedata.h"
#include "engines/nancy/nancy.h"

#include "graphics/font.h"

#ifdef USE_FREETYPE2
#include "graphics/fonts/ttf.h"
#endif

namespace Nancy {

// Below about this the substitute faces stop being legible at all; no nancy18
// registry entry comes near it, this only guards against absurd data.
static const uint kMinFontHeight = 6;

// Guards on the game's own FontScale. The two values the options screen can send
// are 100 and 127; these only keep a hand-edited config from asking for something
// unrenderable.
static const uint kMinFontScalePercent = 50;
static const uint kMaxFontScalePercent = 400;

// Maps a typeface named by the game onto a substitute bundled in
// dists/engine-data/fonts.dat. Matching is on a lowercase substring so that
// e.g. "Courier New" and "Courier" both land on Liberation Mono.
//
// `cellNum / cellDen` converts the game's authored cell height into the cell
// height to ask the *substitute* for. It is not a fudge factor and it is not a
// resolution scale: kTTFSizeModeCell sizes a face so that its ascent + descent
// matches the request, and that sum differs between faces, so "the same cell
// height" does not mean "the same glyph size". The ratio is therefore
//
//     substitute (ascender - descender) / authored face (ascender - descender)
//
// read straight out of the two faces' hhea tables. Both numbers are in font
// units at unitsPerEm 2048 for every face involved, so they divide directly.
struct FaceSubstitute {
	const char *nameFragment;
	const char *regular;
	const char *bold;
	uint cellNum;
	uint cellDen;
};

static const FaceSubstitute kSubstitutes[] = {
	// Liberation was commissioned as a metric-compatible replacement for the
	// core Microsoft faces, and it is exact: measured on this machine's copies,
	// Courier New and Liberation Mono are both hhea 1705 / -615, Times New Roman
	// and Liberation Serif are both 1825 / -443, and Arial and Liberation Sans
	// are both 1854 / -434. So these three need no correction at all.
	{ "courier",	"LiberationMono-Regular.ttf",	"LiberationMono-Bold.ttf",	1, 1 },
	{ "times",		"LiberationSerif-Regular.ttf",	"LiberationSerif-Bold.ttf",	1, 1 },
	{ "arial",		"LiberationSans-Regular.ttf",	"LiberationSans-Bold.ttf",	1, 1 },

	// Tahoma has no metric-compatible free substitute, and the mismatch is
	// almost entirely vertical rather than horizontal: per em the two faces are
	// within 1% on running prose ("The quick brown fox" is 1.0075x, a line of
	// this game's dialogue 1.0090x). What differs is the cell: Tahoma is hhea
	// 2049 / -423, so its cell is 2472/2048 = 1.2070 em against Liberation Sans's
	// 2288/2048 = 1.1172 em. Asking Liberation Sans for Tahoma's cell height
	// therefore hands back an em 8.04% too large, which is the whole of the
	// substitution error. 2288/2472 undoes it.
	{ "tahoma",		"LiberationSans-Regular.ttf",	"LiberationSans-Bold.ttf",	2288, 2472 },

	// Lucida Handwriting is a script face with no open equivalent at all; the
	// serif substitute is a placeholder that keeps text legible. Its metrics were
	// not available to measure, so no correction is applied - nancy18 uses this
	// face for one alias ("Handwriting") that no panel in the game draws with.
	{ "lucida",		"LiberationSerif-Italic.ttf",	"LiberationSerif-BoldItalic.ttf", 1, 1 },
};

static const FaceSubstitute *findSubstitute(const Common::String &face) {
	Common::String lower = face;
	lower.toLowercase();

	for (uint i = 0; i < ARRAYSIZE(kSubstitutes); ++i) {
		if (lower.contains(kSubstitutes[i].nameFragment)) {
			return &kSubstitutes[i];
		}
	}

	return nullptr;
}

TTFFontProvider::~TTFFontProvider() {
	for (auto &f : _fonts) {
		delete f._value;
	}
}

void TTFFontProvider::init(const FontRegistry *registry, uint scalePercent) {
	_registry = registry;
	_scalePercent = MAX<uint>(kMinFontScalePercent, MIN<uint>(kMaxFontScalePercent, scalePercent));

	// Rebuilt rather than added to: the options screen's text-size radio calls
	// this again with a different scale, and without the clear the old faces
	// would leak and get_() would keep handing back the old sizes (the map is
	// first-one-wins by design, for the three duplicate "Convo" entries).
	for (auto &f : _fonts) {
		delete f._value;
	}

	_fonts.clear();
	_metrics.clear();

	if (!registry) {
		return;
	}

#ifdef USE_FREETYPE2
	for (uint i = 0; i < registry->entries.size(); ++i) {
		const FontRegistry::Entry &e = registry->entries[i];
		const FaceSubstitute *sub = findSubstitute(e.face);

		if (!sub) {
			warning("No substitute for font face '%s' (alias '%s')", e.face.c_str(), e.alias.c_str());
			continue;
		}

		if (sub->cellNum != sub->cellDen) {
			debugC(1, kDebugEngine, "Font '%s' substitutes %s for %s; asking for cell %u*%u/%u",
				e.alias.c_str(), sub->regular, e.face.c_str(), e.height, sub->cellNum, sub->cellDen);
		}

		// Nancy16 registers every face twice, at the authored weight and at
		// weight + 300, so the bold variant is synthesized rather than stored.
		// 700 is LOGFONT's FW_BOLD.
		for (uint pass = 0; pass < 2; ++pass) {
			const bool bold = (pass == 1) || (e.weight >= 700);
			const char *file = bold ? sub->bold : sub->regular;

			// The registry height is a LOGFONT lfHeight magnitude - a cell
			// height in pixels, not a point size - so ask for it in cell mode at
			// 1:1 dpi rather than letting the loader scale it.
			//
			// It is a height in DEVICE pixels, and the device is 640x480: the
			// game's own backgrounds are 640x385 Bink, and GraphicsManager opens
			// a 640x480 screen. NDUI's *rects* are authored in an 800x600 design
			// space and are converted 4/5 by nduipanel.cpp, but the FONT chunk is
			// a separate description of a GDI CreateFont call and CreateFont has
			// always taken device units. This used to be scaled 4/5 as well, and
			// that is what made every string in the game come out ~9% small
			// (registry D-4/W10): the reference recording's conversation line
			// pitch is 12.0px on all seven measurable scenes and its narration
			// caption steps 12px too, against an authored Convo height of exactly
			// 12 - and Tahoma's hhea lineGap is 0, so GDI advanced a line by
			// lfHeight with nothing added. Scaled by 4/5 that advance would be
			// 9.6.
			//
			// _scalePercent is the game's own FontScale on top of that. It is a
			// percentage: the retail INI ships FontScale=1270 and the options
			// screen's "Large Text" radio sends Engine_Font the parameter 127.0,
			// so the INI stores it x10. 100 leaves the authored height alone.
			const uint scaled = (e.height * _scalePercent + 50) / 100;
			const uint height = MAX<uint>(kMinFontHeight,
				(scaled * sub->cellNum + sub->cellDen / 2) / sub->cellDen);

			Graphics::Font *font = Graphics::loadTTFFontFromArchive(file, height,
				Graphics::kTTFSizeModeCell, 0, 0, Graphics::kTTFRenderModeLight);

			if (!font) {
				warning("Could not load substitute font %s for alias '%s'", file, e.alias.c_str());
				continue;
			}

			Common::String key = e.alias + (pass == 1 ? "_Bold" : "");
			if (_fonts.contains(key)) {
				// Three registry entries share the alias "Convo" under different
				// ids; they are byte-identical, so the first one wins.
				delete font;
				continue;
			}

			_fonts[key] = font;

			// Keep the authored height beside the font. The layout has to advance
			// by this and not by font->getFontHeight(), which is the substitute's
			// own hhea line height and does not even track the request: Liberation
			// Sans hands back 10, 12, 13, 14 for cells 10, 11, 12, 13.
			FontMetric metric;
			metric.font = font;
			// The authored height *after* the game's own FontScale, so the line
			// pitch grows with the glyphs rather than leaving larger text
			// overlapping at the original spacing.
			metric.lineHeight = MAX<uint>(kMinFontHeight, scaled);
			_metrics.push_back(metric);
		}
	}
#else
	warning("Nancy16+ needs FreeType2 for text rendering; none of the %u fonts will be available",
		(uint)registry->entries.size());
#endif
}

uint TTFFontProvider::lineHeight(const Graphics::Font *font) const {
	if (!font) {
		return 0;
	}

	for (uint i = 0; i < _metrics.size(); ++i) {
		if (_metrics[i].font == font) {
			return _metrics[i].lineHeight;
		}
	}

	// Not one of ours - a caller passing some other Graphics::Font still gets a
	// sane advance rather than zero.
	return (uint)font->getFontHeight();
}

const Graphics::Font *TTFFontProvider::get(const Common::String &alias, bool bold) const {
	Common::String key = alias + (bold ? "_Bold" : "");

	if (_fonts.contains(key)) {
		return _fonts[key];
	}

	// Fall back to the regular weight rather than rendering nothing
	if (bold && _fonts.contains(alias)) {
		return _fonts[alias];
	}

	return nullptr;
}

const Graphics::Font *TTFFontProvider::getByID(uint32 id, bool forceBold) const {
	if (!_registry) {
		return nullptr;
	}

	const FontRegistry::Entry *entry = _registry->findByID(id);
	if (!entry) {
		return nullptr;
	}

	return get(entry->alias, forceBold || entry->weight >= 700);
}

} // End of namespace Nancy
