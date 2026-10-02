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

// Nancy16 types 197 and 199, "Text Scroll". Unrelated to the earlier TextScroll;
// it lays out an AUTOTEXT string in a box, optionally scrolling it.
//   int32    type 199 only, unknown
//   int32    initial text top, in viewport coordinates
//   RECT     destination box, viewport coordinates
//   float    unknown
//   float    scroll velocity in px/sec; negative scrolls up
//   uint32   FONT chunk id
//   uint32   COLR chunk id used when the string sets no colour of its own
//   byte     text is an AUTOTEXT key rather than a literal
//   byte     unknown
//   char[33] AUTOTEXT key
//   int16    event flag label, -1 for none
//   byte     event flag value, raised when the scroll finishes
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

// Nancy16 type 26, "Begin a stream". Starts a parallel script flow; see streams.h.
//   char[33] destination script name ("s2963", "Journal_Nancy")
//   uint16   frame
//   uint16   continue scene sound
//   char[33] stream name ("OFFPush", "PDA_Call", ...)
//   byte x4  mode bits, unknown
//   uint32   unknown
// The target need not be a scene, so it is kept by name; S<digits> names are
// also resolved to a scene id.
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

// Nancy16 type 31, "Automatic U-Turn in a node", used in panorama scenes.
//   RECT     hotspot, viewport coordinates
//   uint16   cursor type
//   byte     unknown
//   uint16   count, then count x uint16 frame ids; an empty list means every frame
// Clicking is assumed to turn the panorama half a revolution.
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
