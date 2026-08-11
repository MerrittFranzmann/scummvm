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
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NANCY_UTIL_H
#define NANCY_UTIL_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/rect.h"
#include "common/serializer.h"

#include "graphics/font.h"
#include "graphics/managed_surface.h"

#include "engines/nancy/commontypes.h"

namespace Nancy {

void readRect(Common::SeekableReadStream &stream, Common::Rect &inRect);
void readRect(Common::Serializer &stream, Common::Rect &inRect, Common::Serializer::Version minVersion = 0, Common::Serializer::Version maxVersion = Common::Serializer::kLastVersion);
void readRectArray(Common::SeekableReadStream &stream, Common::Array<Common::Rect> &inArray, uint num, uint totalNum = 0);
void readRectArray(Common::Serializer &stream, Common::Array<Common::Rect> &inArray, uint num, uint totalNum = 0, Common::Serializer::Version minVersion = 0, Common::Serializer::Version maxVersion = Common::Serializer::kLastVersion);

void readRect16(Common::SeekableReadStream &stream, Common::Rect &inRect);
void readRect16(Common::Serializer &stream, Common::Rect &inRect, Common::Serializer::Version minVersion = 0, Common::Serializer::Version maxVersion = Common::Serializer::kLastVersion);
void readRectArray16(Common::SeekableReadStream &stream, Common::Array<Common::Rect> &inArray, uint num, uint totalNum = 0);
void readRectArray16(Common::Serializer &stream, Common::Array<Common::Rect> &inArray, uint num, uint totalNum = 0, Common::Serializer::Version minVersion = 0, Common::Serializer::Version maxVersion = Common::Serializer::kLastVersion);

// Draws one markup-bearing string into an arbitrary surface, honouring the same
// inline codes NDUI panels do (<cN> COLR id, <fN>/<bN> FONT id, <n>, <u>).
// The two id parameters give the style a string with no codes of its own gets;
// pass -1 for either to leave it at the default. Returns the height the laid-out
// text consumed, which lets a caller measure a block before deciding where to
// scroll it.
int drawStyledTextToSurface(Graphics::ManagedSurface &surf, const Common::String &text,
	int baseFontID, int baseColourID,
	int x, int y, int wrapWidth, Graphics::TextAlign align = Graphics::kTextAlignLeft);

// Recognises one inline markup code, "<c1>" or "<n>", at `pos`. Returns false
// unless the whole thing is '<', one letter, nothing or decimal digits, and '>',
// which is what stops literal angle brackets in prose from being eaten.
// `endPos` comes back as the index of the '>'.
//
// Exported because AR 209's typewriter has to count the characters a string will
// actually type, and a code is not typed. Sharing the renderer's own scanner is
// the only way that count cannot drift from what gets drawn.
bool parseMarkupCode(const Common::String &in, uint pos, char &letter, int &number, uint &endPos);

void readFilename(Common::SeekableReadStream &stream, Common::String &inString);
void readFilename(Common::Serializer &stream, Common::String &inString, Common::Serializer::Version minVersion = 0, Common::Serializer::Version maxVersion = Common::Serializer::kLastVersion);
inline void readFilename(Common::SeekableReadStream &stream, Common::Path &inPath) {
	Common::String inString;
	readFilename(stream, inString);
	inPath = Common::Path(inString);
}
inline void readFilename(Common::Serializer &stream, Common::Path &inPath, Common::Serializer::Version minVersion = 0, Common::Serializer::Version maxVersion = Common::Serializer::kLastVersion) {
	Common::String inString;
	readFilename(stream, inString, minVersion, maxVersion);
	inPath = Common::Path(inString);
}
void readFilenameArray(Common::SeekableReadStream &stream, Common::Array<Common::String> &inArray, uint num);
void readFilenameArray(Common::Serializer &stream, Common::Array<Common::String> &inArray, uint num, Common::Serializer::Version minVersion = 0, Common::Serializer::Version maxVersion = Common::Serializer::kLastVersion);
void readFilenameArray(Common::SeekableReadStream &stream, Common::Array<Common::Path> &inArray, uint num);
void readFilenameArray(Common::Serializer &stream, Common::Array<Common::Path> &inArray, uint num, Common::Serializer::Version minVersion = 0, Common::Serializer::Version maxVersion = Common::Serializer::kLastVersion);

void assembleTextLine(char *rawCaption, Common::String &output, uint size);

// Resolves a subtitle/caption string that may be a key into an engine-data CVTX text
// table (AUTOTEXT by default, CONVO for some puzzles). Returns the table's entry for
// `keyOrText` when the table exists and contains that key, otherwise returns `fallback`.
Common::String resolveSubtitleText(const Common::String &keyOrText, const Common::String &fallback = Common::String(), const char *tableID = "AUTOTEXT");

// Reads a 30-byte, NUL-terminated subtitle string from `stream` and resolves it as an
// AUTOTEXT key, falling back to the literal text when the key is not present in the table.
Common::String readSubtitleText(Common::SeekableReadStream &stream);

// Shows `text` as a single line in the game textbox, replacing its current contents.
// Does nothing when `text` is empty or when the player has subtitles disabled. A
// non-negative `overrideFontID` selects a font other than the textbox default. When
// `forceRedraw` is true, the textbox is redrawn immediately instead of on the next
// render pass.
void showSubtitle(const Common::String &text, bool forceRedraw = false, int overrideFontID = -1);

void readUIButton(Common::SeekableReadStream &stream, UIButtonRecord &dst);
void readUISlider(Common::SeekableReadStream &stream, UISliderRecord &dst);
void readUIPopupHeader(Common::SeekableReadStream &stream, UIPopupHeader &dst);
void readUIButtonSlot(Common::SeekableReadStream &stream, UIButtonSlot &dst);

// Abstract base class used for loading data that would take too much time in a single frame
class DeferredLoader {
public:
	DeferredLoader() {}
	virtual ~DeferredLoader() {}

	// Calls loadInner() one or many times, until its allotted time is done
	bool load(uint32 endTime);

protected:
	// Contains the actual loading logic, split up into tasks that are as small as possible
	virtual bool loadInner() = 0;
};

// Debug affordance used by the synthetic-click harness in InputManager. Defined
// in state/scene.cpp; declared here so input.cpp does not have to pull in
// state/scene.h, whose State::Action clashes with the keymapper's Action.
bool debugGetNthHotspotCentre(uint n, Common::Point &out);

// Debug affordance: the scene the player is currently in, or -1 when there is
// no Scene instance yet. Same reason for living here as the above.
int debugGetCurrentSceneID();

// Debug affordance: jump the viewport of a panoramic scene straight to a frame.
void debugSetViewportFrame(uint frame);
int debugGetViewportFrame();

} // End of namespace Nancy

#endif // NANCY_UTIL_H
