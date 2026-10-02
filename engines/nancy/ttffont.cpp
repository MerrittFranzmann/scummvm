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
#include "common/config-manager.h"
#include "common/archive.h"
#include "common/fs.h"

#ifdef USE_FREETYPE2
#include "graphics/fonts/ttf.h"
#endif

namespace Nancy {

// Guards against unreadably small sizes from bad data
static const uint kMinFontHeight = 6;

// The options screen sends 100 or 127; these guard against bad config values.
static const uint kMinFontScalePercent = 50;
static const uint kMaxFontScalePercent = 400;

// Maps a game typeface (by lowercase substring) onto a substitute in fonts.dat.
// cellNum / cellDen is the ratio of the substitute's hhea cell height to the
// original face's, so the substitute renders at the same em size.
struct FaceSubstitute {
	const char *nameFragment;
	const char *regular;
	const char *bold;
	uint cellNum;
	uint cellDen;
};

static const FaceSubstitute kSubstitutes[] = {
	// Metric-compatible with the originals, so no correction is needed
	{ "courier",	"LiberationMono-Regular.ttf",	"LiberationMono-Bold.ttf",	1, 1 },
	{ "times",		"LiberationSerif-Regular.ttf",	"LiberationSerif-Bold.ttf",	1, 1 },
	{ "arial",		"LiberationSans-Regular.ttf",	"LiberationSans-Bold.ttf",	1, 1 },

	// Tahoma's cell is taller than Liberation Sans's (2472 vs 2288 font units)
	{ "tahoma",		"LiberationSans-Regular.ttf",	"LiberationSans-Bold.ttf",	2288, 2472 },

	// Placeholder; there is no open equivalent of Lucida Handwriting
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
	SearchMan.remove("nancy-original-fonts");
	for (auto &f : _fonts) {
		delete f._value;
	}
}

void TTFFontProvider::init(const FontRegistry *registry, uint scalePercent) {
	_registry = registry;
	_scalePercent = MAX<uint>(kMinFontScalePercent, MIN<uint>(kMaxFontScalePercent, scalePercent));

	// Rebuild from scratch, since the options screen calls this again on a scale change
	for (auto &f : _fonts) {
		delete f._value;
	}

	_fonts.clear();
	_metrics.clear();

	if (!registry) {
		return;
	}

#ifdef USE_FREETYPE2
	// The string tables are CP1252, not Latin-1 (e.g. the euro sign is 0x80)
	char bytes[256];
	for (uint i = 0; i < 256; ++i)
		bytes[i] = (char)i;
	const Common::U32String unicode(bytes, 256, Common::kWindows1252);
	uint32 mapping[256];
	for (uint i = 0; i < 256; ++i)
		mapping[i] = unicode[i];

	// Original faces may be supplied locally; never bundle proprietary fonts.
	// Match family/style metadata instead of depending on OS-specific filenames.
	Common::Array<Common::Path> fontFiles;
	SearchMan.remove("nancy-original-fonts");
	if (ConfMan.hasKey("nancy_fontpath")) {
		Common::FSNode directory(ConfMan.getPath("nancy_fontpath"));
		Common::FSList files;
		if (directory.getChildren(files, Common::FSNode::kListFilesOnly)) {
			SearchMan.addDirectory("nancy-original-fonts", directory);
			for (const auto &file : files) {
				Common::String name = file.getName();
				name.toLowercase();
				if (name.hasSuffix(".ttf") || name.hasSuffix(".ttc") || name.hasSuffix(".otf"))
					fontFiles.push_back(Common::Path(file.getName(), Common::Path::kNoSeparator));
			}
		} else {
			warning("Nancy font directory is unavailable; using bundled substitutes");
		}
	}

	for (uint i = 0; i < registry->entries.size(); ++i) {
		const FontRegistry::Entry &e = registry->entries[i];
		const FaceSubstitute *sub = findSubstitute(e.face);

		// Build a regular and a bold variant of every face. 700 is FW_BOLD.
		for (uint pass = 0; pass < 2; ++pass) {
			const bool bold = (pass == 1) || (e.weight >= 700);
			Common::String key = e.alias + (pass == 1 ? "_Bold" : "");
			if (_fonts.contains(key))
				continue;

			// FontScale only applies to entries with the scaling flag set
			// (Ndui.dll 0x10028824). The scaled height is truncated.
			const uint scaled = e.flag ? (e.height * _scalePercent) / 100 : e.height;
			const uint cellHeight = MAX<uint>(kMinFontHeight, scaled);
			Graphics::Font *font = nullptr;
			if (!fontFiles.empty()) {
				// Grid fitting matches GDI; light hinting would change line breaks
				font = Graphics::findTTFace(fontFiles, Common::U32String(e.face),
					bold, false, cellHeight, 72, 72, Graphics::kTTFRenderModeNormalWithGridFitting, mapping);
			}

			if (font) {
				debugC(1, kDebugEngine, "Font '%s': original %s, cell %u", key.c_str(), e.face.c_str(), cellHeight);
			} else if (sub) {
				// Only substitutes need the face-metric correction
				const uint height = MAX<uint>(kMinFontHeight,
					(scaled * sub->cellNum + sub->cellDen / 2) / sub->cellDen);
				const char *file = bold ? sub->bold : sub->regular;
				font = Graphics::loadTTFFontFromArchive(file, height,
					Graphics::kTTFSizeModeCell, 0, 0, Graphics::kTTFRenderModeLight, mapping);
				debugC(1, kDebugEngine, "Font '%s': substitute %s, cell %u", key.c_str(), file, height);
			}

			if (!font) {
				warning("Could not load font '%s' (face '%s')", key.c_str(), e.face.c_str());
				continue;
			}

			_fonts[key] = font;

			// Lines advance by the scaled authored height, not getFontHeight()
			FontMetric metric;
			metric.font = font;
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
