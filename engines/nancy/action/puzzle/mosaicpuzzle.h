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

// Paint-fill / mosaic puzzle, AR type 181 ("paint fill puzzle"). Nancy16 uses it for
// the four Mosaic Puzzle scenes (S3252-S3255, "Mosaic1".."Mosaic4"): a wall mosaic
// with square holes in it, a tray of ten coloured gems along the top, and a photo
// reference of the finished pattern below. The player picks a gem out of the tray,
// drops it into a hole, and solves the puzzle by matching every hole to the colour
// the record records for it.
//
// The background art (a Bink still) already draws the tray and the mosaic with its
// holes; every hole in the grid is one of this record's tiles, so an unpainted tile
// draws nothing and simply lets the hole show through.
//
// Record layout, 4/4 exact on the four Nancy16 scenes:
//
//   char[33]   puzzle name ("Mosaic1")
//   char[33]   tile atlas name ("PIA_MosaicPUZ_OVL"), twice
//   uint16     28, uint16 29                             (grid metrics, unused)
//   int32[6]   0, 0, 12, 0, 625, 191                     (constant, unidentified)
//   uint16     two small ids, +2 per puzzle              (unidentified)
//   uint16     colour count (10), byte 0xff
//     per colour, 54 bytes:
//       byte[3] 0xff, RECT tray position, RECT atlas source, byte[19] 0xff
//   uint16     tile count (96/84/100/102)
//     per tile, 20 bytes: RECT position, int16 -1, uint16 solution colour
//   byte[3]
//   SoundGroup pick-up clicks (Click_gem05..08)
//   SoundGroup place clicks   (Click_gem01..04)
//   byte[7]    solve: uint16 scene, uint16 frame, int16 flag label, byte value
//   SoundGroup solve sound    (ChestClose_Wood01..03)
//   byte[25]   uint16, RECT exit hotspot, uint16 cursor, uint16 scene,
//              int16 flag label, byte value
//
// A solution colour of -1 means the hole must be left empty; only Mosaic3 has any
// (two of its hundred tiles).
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

	// -- debug affordance, default off; see the comment in the .cpp --
	static bool autoPlay();
	void autoStep();

	// Publishes the tile tally into the two player-table slots the record names.
	// The sibling records watch those slots; see the comment on the two indices.
	void writeProgressToPlayerTable();

	// -- File data --
	Common::String _puzzleName;
	Common::Path _imageName;
	uint16 _gridMetricA = 0;			// 28 in every sample
	uint16 _gridMetricB = 0;			// 29 in every sample

	// Player-table slots this puzzle publishes its running tally into: how many
	// holes are not yet right, and how many are. 10/11, 12/13, 14/15, 16/17 across
	// S3252-S3255, which is exactly what each scene's sibling type-18 record tests
	// ("[11] >= 94 && [10] <= 0" in S3252, and so on) and what the endgame scoring
	// scene S5490 reads back ("[11] >= 96, [13] >= 84, [15] >= 98, [17] >= 101").
	// Leaving them unwritten is what kept S3252[1] dead, and that record is the
	// only writer of flag 2571 on the solve path - without it S3251 has neither a
	// hotspot nor an exit.
	uint16 _numWrongTableIndex = 0;		// 10 / 12 / 14 / 16 across the four puzzles
	uint16 _numCorrectTableIndex = 0;	// 11 / 13 / 15 / 17

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
