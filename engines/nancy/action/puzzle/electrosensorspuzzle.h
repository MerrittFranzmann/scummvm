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

// AR type 190, Nancy16 only. The record calls itself "Override Lock Puzzle" and
// that name is genuine rather than a copy-paste: the failure scenes for this
// chapter (S3894-S3896) hold typewriter text called "Override_Lose01".."03" and
// their background is named VEN_SecChaOvridLck. In-game it is the "Electro
// Sensors" panel (scene description "Electro Sensors", background
// ZAT_ElectroSensors_PUZ) -- a bank of eight wall levers that cut power to the
// laser sensors in the Zattere warehouse.
//
// Four records, one per robot-sentry approach: S3890-S3893, reached by a scene
// change out of S3871/S3873/S3877/S3879 (the "Robot Sentries" grid, AR type 210).
// The four are byte-identical apart from the scene they return to, so the panel
// is the same puzzle played four times, once per sentry.
//
// Record layout, 4/4 byte-exact with nothing left over:
//
//   char[33]   overlay atlas name ("ZAT_ElectroSensorsPUZ_OVL", 877x601)
//   uint16     29    -- lever hover cursor; kNancy13PuzzleArrow
//   uint16     8, then 8 RECTs   lever column destinations, viewport space
//   uint16     8, then 8 RECTs   lever grab hotspots (bottom 100px of each column)
//   uint32     5     -- unidentified, see _unknownA
//   uint32     25    -- unidentified, see _unknownB
//   uint16     23, then 23 RECTs animation frames inside the atlas
//   SoundGroup ratchet clicks (Click_Ratchet_Short01..06), channel 18
//   SoundGroup wind-down      (WindUp_MusicBox01..05),     channel 18
//   byte[7]    solve: uint16 scene, uint16 frame, int16 flag label, byte value
//   SoundGroup solve sound    (Laser_Power_Down), channel 15
//   byte[25]   uint16, RECT exit hotspot, uint16 cursor (10, kNancy13MoveBackward),
//              uint16 scene, int16 flag label, byte value
//
// The 23 atlas frames are one continuous per-column animation, measured off the
// art: frames 0-6 walk the T-handle down its post on an ease-out curve while the
// 16-segment power meter above stays full, then frames 7-22 drain that meter one
// segment per frame (lit count == 22 - frame) with the handle parked at the
// bottom. Frame 0 is exactly what the static background already draws, so a
// column that has not been touched blits its own starting art.
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

	// Frame at which the handle has bottomed out and the meter starts to drain.
	// Measured from the atlas, not stated in the record.
	static const uint kDrainStartFrame = 7;

	// Index of the lever whose grab hotspot is under the (screen-space) cursor, or -1.
	int leverAtCursor(const Common::Point &mousePos) const;

	bool isSolved() const;
	void redraw();

	// -- debug affordance, default off; see the comment in the .cpp --
	static bool autoPlay();
	void autoPull(uint32 now);
	SoundDescription playSoundBlock(const RandomSoundBlock &block);

	// -- File data --
	Common::Path _imageName;
	uint16 _leverCursorType = 0;		// 29 in every sample

	// GUESS: the record carries two unlabelled 32-bit values, 5 and 25, between
	// the hotspot array and the frame array. All four records hold the same pair,
	// so there is no variance to learn from and their meaning is undetermined.
	// 25 is used below as the per-frame animation delay in milliseconds purely
	// because it is the only one of the two that yields a sensible lever speed.
	uint32 _unknownA = 0;				// 5 in every sample
	uint32 _unknownB = 0;				// 25 in every sample

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
