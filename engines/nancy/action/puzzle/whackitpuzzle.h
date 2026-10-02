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

#ifndef NANCY_ACTION_WHACKITPUZZLE_H
#define NANCY_ACTION_WHACKITPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// Bees hover over the greenhouse plants and the player sprays them with bug
// spray. The background already draws the plants; this record owns the bees,
// the spray puff and the crosshair.
//
//   char[33]   sprite atlas name
//   RECT       crosshair cell in the atlas
//   uint16     unknown
//   RECT       unknown, always empty
//   RECT       play area
//   uint32[10] tuning values, see below
//   uint16     unknown, possibly a duration in ms
//   uint32     unknown
//   byte       unknown
//   3 x animation: uint32 frame delay (ms), uint16 frame count,
//                  count x RECT source cell
//                  spray puff, bee flying, bee downed
//   5 x SoundGroup: spray, bee revive, bee buzz, angry buzz, Nancy stung
//   uint32     unknown
//   SceneChangeDescription exit scene, then an unknown byte
//   SoundGroup silence, stops the spray channel
//   uint16     unknown
//
// The tuning values are assumed from how they change across records:
//   0, 1   bee speed, min and max
//   2..5   unknown
//   6, 7   revive delay in seconds, min and max
//   8, 9   bee count, min and max
//
// The puzzle is assumed to be won when every bee is down at the same time.
class WhackItPuzzle : public RenderActionRecord {
public:
	WhackItPuzzle() : RenderActionRecord(7) {}
	virtual ~WhackItPuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "WhackItPuzzle"; }

	struct Animation {
		uint32 frameDelay = 0;
		Common::Array<Common::Rect> frames;
	};

	// One bee. Bees fly around the play area; a spray puff that covers a bee
	// knocks it down, and after a delay it revives and starts flying again.
	struct Bee {
		Common::Point pos;			// top-left, viewport space
		int dx = 0, dy = 0;			// pixels per step
		uint frame = 0;
		uint32 nextFrameTime = 0;
		bool down = false;
		uint32 reviveTime = 0;		// when a downed bee gets back up
	};

	enum SolveState { kNotSolved, kWaitForPause };

	void stepBees(uint32 now);
	void spray(const Common::Point &at, uint32 now);

	bool allBeesDown() const;
	void redraw();
	SoundDescription playSoundBlock(const RandomSoundBlock &block);
	uint32 randomBetween(uint32 lo, uint32 hi) const;

	// -- File data --
	Common::Path _imageName;
	Common::Rect _crosshairSrc;
	uint16 _unknown31 = 0;
	Common::Rect _emptyRect;
	Common::Rect _playArea;
	uint32 _tuning[10] = { 0 };
	uint16 _unknownMs = 0;
	uint32 _unknownOne = 0;
	byte _pad = 0;

	Animation _puffAnim;
	Animation _flyAnim;
	Animation _downAnim;

	RandomSoundBlock _spraySound;
	RandomSoundBlock _reviveSound;
	RandomSoundBlock _buzzSound;
	RandomSoundBlock _angrySound;
	RandomSoundBlock _stungSound;
	RandomSoundBlock _silenceSound;

	uint32 _unknown437 = 0;
	SceneChangeDescription _exitScene;

	// -- Runtime state --
	Graphics::ManagedSurface _image;
	Common::Array<Bee> _bees;

	bool _puffActive = false;
	Common::Point _puffPos;
	uint _puffFrame = 0;
	uint32 _puffNextFrameTime = 0;

	uint32 _lastStepTime = 0;
	SolveState _solveState = kNotSolved;
	uint32 _solvedTime = 0;

};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_WHACKITPUZZLE_H
