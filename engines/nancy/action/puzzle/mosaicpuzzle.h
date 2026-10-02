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

#ifndef NANCY_ACTION_MOSAICPUZZLE_H
#define NANCY_ACTION_MOSAICPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/misc/mousefollow.h"

namespace Nancy {
namespace Action {

// Paint-fill / mosaic puzzle. The player picks a gem out of a tray and drops
// it into a hole in the mosaic, and must match every hole to its solution
// colour. The background already draws the tray and the holes, so an
// unpainted tile draws nothing.
//
//   char[33]   puzzle name
//   char[33]   tile atlas name, twice
//   uint16     grid metrics, x2, unused
//   int32[6]   unknown
//   uint16     player-table slots for the wrong and correct tile counts
//   uint16     colour count, byte
//     per colour, 54 bytes:
//       byte[3], RECT tray position, RECT atlas source, byte[19]
//   uint16     tile count
//     per tile, 20 bytes: RECT position, int16 unknown, uint16 solution colour
//   byte[3]
//   SoundGroup pick-up sound
//   SoundGroup place sound
//   byte[7]    solve: uint16 scene, uint16 frame, int16 flag label, byte value
//   SoundGroup solve sound
//   byte[25]   uint16, RECT exit hotspot, uint16 cursor, uint16 scene,
//              int16 flag label, byte value
//
// A solution colour of -1 means the hole must be left empty.
class MosaicPuzzle : public RenderActionRecord {
public:
	MosaicPuzzle() : RenderActionRecord(7) {}
	virtual ~MosaicPuzzle() {}

	void init() override;
	void registerGraphics() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "MosaicPuzzle"; }

	enum SolveState { kNotSolved, kWaitForSound };

	static const int16 kNoColour = -1;

	// Index of the tray swatch / grid tile under the (screen-space) cursor, or -1.
	int swatchAtCursor(const Common::Point &mousePos) const;
	int tileAtCursor(const Common::Point &mousePos) const;

	bool isSolved() const;
	void redraw();
	void setHeldColour(int16 colour);
	SoundDescription playSoundBlock(const RandomSoundBlock &block);

	// Publishes the tile tally into the two player-table slots the record names
	void writeProgressToPlayerTable();

	// -- File data --
	Common::String _puzzleName;
	Common::Path _imageName;
	uint16 _gridMetricA = 0;
	uint16 _gridMetricB = 0;

	// Player-table slots for the number of holes that are not yet right, and the
	// number that are. Sibling records in the scene test them to solve the puzzle.
	uint16 _numWrongTableIndex = 0;
	uint16 _numCorrectTableIndex = 0;

	Common::Array<Common::Rect> _swatchDests;	// tray positions, viewport space
	Common::Array<Common::Rect> _swatchSrcs;	// gem sprites inside the atlas

	Common::Array<Common::Rect> _tileRects;		// holes in the mosaic, viewport space
	Common::Array<int16> _tileSolution;			// colour index, or kNoColour

	RandomSoundBlock _pickUpSound;
	RandomSoundBlock _placeSound;
	RandomSoundBlock _solveSound;

	SceneChangeDescription _solveScene;
	FlagDescription _solveFlag;

	Common::Rect _exitHotspot;
	uint16 _exitCursorType = 0;
	SceneChangeDescription _exitScene;
	FlagDescription _exitFlag;

	// -- Runtime state --
	Graphics::ManagedSurface _image;
	Common::Array<int16> _tileColour;	// what the player has placed, or kNoColour
	int16 _heldColour = kNoColour;		// the gem being carried, or kNoColour
	Misc::MouseFollowObject _heldGem;

	bool _exitRequested = false;
	SolveState _solveState = kNotSolved;
	SoundDescription _playingSolveSound;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_MOSAICPUZZLE_H
