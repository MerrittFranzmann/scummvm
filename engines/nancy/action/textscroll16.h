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

#ifndef NANCY_ACTION_TEXTSCROLL16_H
#define NANCY_ACTION_TEXTSCROLL16_H

#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// Nancy16 types 197 and 199, both described "Text Scroll" in the data. This is
// how the game shows readable documents: the laptop's deleted chess logs, the
// bee-sting failure message, the costume-shop price tags and the end credits
// are all one of these records. Nothing about it resembles the nancy6-15
// TextScroll (type 68/70), which is an Autotext feeding a PeepholePuzzle; the
// Nancy16 record is a flat struct that names an AUTOTEXT key and a box to lay
// it out in.
//
// Layout, measured over all 21 type-197 and all 26 type-199 records:
//
//   int32    leading field, type 199 only, 12 in 26/26 - purpose unknown
//   int32    initial text top, in viewport coordinates
//   RECT     destination box (left, top, right, bottom), viewport coordinates
//   float    4.0 in every type-197 record, 0.0 in every type-199 one
//   float    scroll velocity in px/sec; negative scrolls up
//   uint32   FONT chunk id
//   uint32   COLR chunk id used when the string sets no colour of its own
//   byte     1 in 47/47 - the Autotext "text is a key, not a literal" flag
//   byte     0 in 47/47
//   char[33] AUTOTEXT key
//   int16    event flag label, -1 for none
//   byte     event flag value
//
// The initial top is a separate field from the box top because the credits use
// it: the box is (244, -15)-(639, 399) but the text starts at y=385, just off
// the bottom, and rises at -30 px/sec. In the eighteen records that do not
// scroll the two fields are equal, which is what makes the pair readable.
//
// The event flag is raised when the scroll finishes. Only the credits use it -
// S6562/S6563 raise flag 1010, and a type 16 scene change in the same scene
// waits on it - so "when the text is done" is the only reading that produces
// the observed behaviour. A record with no scroll raises it as soon as it draws.
//
// The FONT ids confirm themselves against the chunk table: 4 is UIFont/Tahoma 16
// for the log pages and the 30px-high index rows, 6 is UIFontLarge/Tahoma 22 bold
// for the credits and the bee-sting message, and 2 is Diagnostic/Arial 12 for
// prices that have to fit a 29x16 box.
class Nancy16TextScroll : public RenderActionRecord {
public:
	Nancy16TextScroll(bool hasLeadingField) :
		RenderActionRecord(8), _hasLeadingField(hasLeadingField) {}
	virtual ~Nancy16TextScroll() {}

	void init() override;
	void updateGraphics() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "Nancy16TextScroll"; }
	Common::String getRecordExtraInfo() const override;

	void buildTextSurface();
	void redraw();
	void raiseDoneFlag();

	bool _hasLeadingField;

	int32 _leading = 0;
	int32 _startY = 0;
	Common::Rect _dest;
	float _unknownFloat = 0.0f;
	float _scrollSpeed = 0.0f;
	uint32 _fontID = 0;
	uint32 _colourID = 0;
	byte _useAutotextChunk = 0;
	byte _unknownByte = 0;
	Common::String _textKey;
	FlagDescription _doneFlag;

	Common::Rect _clipped;
	Graphics::ManagedSurface _textSurface;
	int _textHeight = 0;
	float _curY = 0.0f;
	uint32 _lastTick = 0;
	bool _initialised = false;
	bool _scrollDone = false;
};

// Nancy16 type 26, "Begin a stream". 43 records in scenes plus 21 more in the
// non-scene scripts, a constant 78 bytes:
//
//   char[33] destination script, by NAME ("s2963", "Journal_Nancy")
//   uint16   frame, 0 in 43/43
//   uint16   continue scene sound, 0 in 43/43
//   char[33] stream name ("OFFPush", "PDA_Call", "MoiveFades", ...)
//   byte x4  mode bits, five distinct combinations
//   uint32   15 for the OFFPush and TUNNEL FLOW CHART records, 0 otherwise
//
// This starts a parallel script flow; see streams.h for what one is and for the
// evidence. The record is not a scene change - six of these (S2925, S2962 and
// S5470-S5475) have no dependencies at all, so taking them as one would
// teleport the player out of the Venice map the instant it loads. All six name
// backgroundless targets and so become concurrent streams, which is what stops
// that from happening.
//
// The destination is stored by name because a stream target need not be a
// scene: ONBECOMENANCY starts one on the script `Journal_Nancy`. The numeric id
// is resolved here as well, for the `S<digits>` names, because the runtime needs
// it for the scene-visit counter and for a close-up's return address.
//
// GUESS, not acted on: the four mode bytes. mode[0] is 0 on 37 of the 43
// in-scene records, 1 on the six "MoiveFades" ones and 2 on 18 of the 19
// ambient-environment records; mode[1] is 1 exactly on the OFFPush and TUNNEL
// FLOW CHART records among the 43, which is also exactly the set whose target
// has viewport artwork - but OFF_EXTERIOR_AMB_SFX has mode[1] set too and its
// target has none, so that correlation is not the field's meaning. The runtime
// reads the target's own summary chunk instead, which cannot be wrong.
class BeginNamedStream : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "BeginNamedStream"; }
	Common::String getRecordExtraInfo() const override;

	Common::String _sceneName;
	uint16 _sceneID = kNoScene;
	uint16 _frameID = 0;
	uint16 _continueSceneSound = 0;
	Common::String _streamName;
	byte _mode[4] = {};
	uint32 _param = 0;
};

// Nancy16 type 31, "Automatic U-Turn in a node". 8 records in the three 360
// panorama nodes S2910, S3210 and S3510:
//
//   RECT     hotspot, viewport coordinates
//   uint16   cursor, 19 in 8/8 - the Nancy13 system Exit cursor
//   byte     0 or 1
//   uint16   count, then count x uint16 panorama frame ids
//
// Every rect is a wide strip along the bottom of the viewport (y 350-385, the
// floor), the cursor is the exit/back arrow, and the frame list matches the
// frames the scene's other hotspots use - in S2910 the same 0, 1 and 19 that
// its type 91 records name. An empty list means every frame.
//
// GUESS: the click turns the player around, i.e. steps the panorama half a
// revolution. The layout is measured; the action is inferred from the record's
// own name plus the shape of the data, and is skipped entirely when the scene's
// background is not a panorama.
//
// GUESS: the byte at offset 18 is read but not used. It is 1 on the records
// whose frame list is empty or covers the whole turn and 0 on the five that name
// a single frame, which fits "this is the whole-node fallback" but is not enough
// to act on.
class AutoUTurn : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	bool canHaveHotspot() const override { return true; }
	CursorManager::CursorType getHoverCursor() const override {
		return (CursorManager::CursorType)_cursorType;
	}
	bool cursorSetFromScript() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "AutoUTurn"; }

	Common::Rect _uTurnHotspot;
	uint16 _cursorType = 0;
	byte _unknown18 = 0;
	Common::Array<uint16> _frames;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_TEXTSCROLL16_H
