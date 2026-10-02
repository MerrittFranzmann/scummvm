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

#ifndef NANCY_ACTION_LOCKPICKPUZZLE_H
#define NANCY_ACTION_LOCKPICKPUZZLE_H

#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// Nancy16 action record type 171, "Lock Pick Puzzle". Each slot shows a
// tumbler from the overlay atlas. Clicking a slot steps its tumbler on by one
// frame and drags every slot it is linked to along with it; the lock opens
// when all slots sit on the frame the record's solution table names.
//
//   char[33]  unused name
//   uint16    cursor type
//   double    unknown
//   char[33]  overlay atlas name
//   uint16    numTumblers
//     char[33] tumbler name
//     uint16   numFrames
//       uint16 flag, RECT srcRect            (one per frame)
//   uint16    numSlots
//     char[33] slot label
//     char[33] name of the tumbler drawn in this slot
//     uint16   numLinks
//       char[33] label of a slot dragged along with this one
//     RECT     destRect
//     int32    initial frame
//     int16    unknown
//   SoundGroup click sounds
//   byte[4]   unknown
//   uint16    numSolution
//     char[33] slot label, int32 required frame
//   byte, int16 label, byte value          - the flag raised when it opens
//   Tail25    exit hotspot, cursor, exit scene, exit flag
class LockPickPuzzle : public RenderActionRecord {
public:
	LockPickPuzzle() : RenderActionRecord(7) {}
	virtual ~LockPickPuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "LockPickPuzzle"; }

	struct Tumbler {
		Common::String name;
		Common::Array<Common::Rect> frames;
	};

	struct Slot {
		Common::String label;
		Common::String tumblerName;
		Common::Array<Common::String> linkLabels;

		Common::Rect destRect;
		int32 initialFrame = 0;

		// Resolved once the whole record is read.
		int tumblerID = -1;
		Common::Array<uint> links;
		int requiredFrame = -1;

		int currentFrame = 0;
	};

	void drawSlot(uint slotID);
	void drawAllSlots();
	bool isSolved() const;
	void advanceSlot(uint slotID);

	void clickSlot(uint slotID);
	int findSlot(const Common::String &label) const;
	int findTumbler(const Common::String &name) const;

	Common::Path _imageName;
	uint16 _hoverCursorType = 0;

	Common::Array<Tumbler> _tumblers;
	Common::Array<Slot> _slots;

	RandomSoundBlock _clickSounds;
	SoundDescription _clickSound;

	FlagDescription _solveFlag;

	Common::Rect _exitHotspot;
	uint16 _exitCursorType = 0;
	SceneChangeWithFlag _exitScene;

	Graphics::ManagedSurface _image;
	bool _solved = false;

};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_LOCKPICKPUZZLE_H
