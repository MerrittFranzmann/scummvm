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

#ifndef NANCY_ACTION_ELECTROSENSORSPUZZLE_H
#define NANCY_ACTION_ELECTROSENSORSPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// The "Electro Sensors" panel in Nancy16, which the record calls "Override
// Lock Puzzle": a bank of levers that cut power to the warehouse lasers.
//
//   char[33]   overlay atlas name
//   uint16     lever hover cursor
//   uint16 + n x RECT  lever column destinations, viewport space
//   uint16 + n x RECT  lever grab hotspots
//   uint32     unknown
//   uint32     unknown
//   uint16 + n x RECT  animation frames inside the atlas
//   SoundGroup ratchet clicks
//   SoundGroup wind-down
//   byte[7]    solve: uint16 scene, uint16 frame, int16 flag label, byte value
//   SoundGroup solve sound
//   byte[25]   uint16, RECT exit hotspot, uint16 cursor, uint16 scene,
//              int16 flag label, byte value
//
// The atlas frames are one per-column animation: the handle moves down, then
// the power meter above it drains. Frame 0 matches the static background.
class ElectroSensorsPuzzle : public RenderActionRecord {
public:
	ElectroSensorsPuzzle() : RenderActionRecord(7) {}
	virtual ~ElectroSensorsPuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "ElectroSensorsPuzzle"; }

	enum SolveState { kNotSolved, kWaitForSound };

	// Frame at which the handle has bottomed out and the meter starts to drain
	static const uint kDrainStartFrame = 7;

	// Index of the lever whose grab hotspot is under the (screen-space) cursor, or -1.
	int leverAtCursor(const Common::Point &mousePos) const;

	bool isSolved() const;
	void redraw();

	void autoPull(uint32 now);
	void pullLever(uint lever, uint32 now);
	SoundDescription playSoundBlock(const RandomSoundBlock &block);

	// -- File data --
	Common::Path _imageName;
	uint16 _leverCursorType = 0;

	// Unknown. The second one is assumed to be the animation frame delay in ms.
	uint32 _unknownA = 0;
	uint32 _unknownB = 0;

	Common::Array<Common::Rect> _leverDests;	// column positions, viewport space
	Common::Array<Common::Rect> _leverHotspots;	// grab areas, viewport space
	Common::Array<Common::Rect> _frameSrcs;		// animation frames inside the atlas

	RandomSoundBlock _ratchetSound;
	RandomSoundBlock _windDownSound;
	RandomSoundBlock _solveSound;

	SceneChangeDescription _solveScene;
	FlagDescription _solveFlag;

	Common::Rect _exitHotspot;
	uint16 _exitCursorType = 0;
	SceneChangeDescription _exitScene;
	FlagDescription _exitFlag;

	// -- Runtime state --
	Graphics::ManagedSurface _image;

	Common::Array<uint> _leverOrder;
	uint _nextLever = 0;
	bool _resetting = false;
	Common::Array<uint> _leverFrame;	// current animation frame per column
	Common::Array<bool> _leverMoving;	// column is mid-animation
	Common::Array<uint32> _leverNextTick;

	SolveState _solveState = kNotSolved;
	SoundDescription _playingSolveSound;
	bool _exitRequested = false;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_ELECTROSENSORSPUZZLE_H
