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

// "Whack It Puzzle", AR type 203 in Nancy16. Six records, in the greenhouse
// scenes S2019, S2028-S2031 and S2048: bees hover over the carnivorous plants
// and the player sprays them with bug spray. The background art (a Bink still)
// already draws the plants; this record owns the bees, the spray puff and the
// crosshair.
//
// LAYOUT - 6/6 byte-exact. Every record is 1135 payload bytes; the 1237 vs 1221
// body sizes differ only by one 16-byte dependency. Exactly TEN bytes differ
// across all six records: the nine tuning values below and the low byte of the
// exit scene id, which is what makes the reading below checkable rather than
// merely fitted.
//
//   char[33]   sprite atlas name ("ALT_Bee_PUZ_OVL", 615x260)
//   RECT       crosshair cell in the atlas, (1,1,45,45) in all six
//   uint16     23                                        (unidentified)
//   RECT       (-7,-7,-1,-1), an empty/disabled rect
//   RECT       play area, (0,0,640,385) - the viewport
//   uint32[10] tuning values, see below
//   uint16     2000                                      (unidentified, looks
//                                                         like a millisecond
//                                                         duration)
//   uint32     1
//   byte       0
//   3 x animation: uint32 frame delay (ms), uint16 frame count,
//                  count x RECT source cell
//                  [0] spray puff,  delay 15, 5 frames
//                  [1] bee flying,  delay 60, 13 frames
//                  [2] bee downed,  delay 10, 2 frames
//   5 x SoundGroup: spray hiss (AirRelease_Short01..08, ch 14)
//                   bee revive  (Bee_Revive01..05,       ch 15)
//                   bee buzz    (Bee_Single01,           ch 16)
//                   angry buzz  (Bee_Single_Angry01,     ch 17)
//                   Nancy stung (ouch, oww,              ch 25)
//   uint32     7                                         (unidentified)
//   uint16 scene, uint16 frame, uint16 vertical scroll, byte continue sound
//   SoundGroup silence (Silence, ch 14 - stops the spray channel)
//   uint16     0
//
// The three animations and the atlas agree exactly: the atlas holds a crosshair,
// five puff frames of growing size, thirteen flying-bee frames and two
// downed-bee frames, and nothing else.
//
// WHAT IS INFERRED. The ten tuning values are named below from how they move
// across the six records, not from anything that states their meaning. The
// difficulty ramps S2019 -> S2031 and the trends are consistent, but the
// mapping is a reading, not a fact:
//
//   idx  S2019  S2028  S2029  S2030  S2031  S2048   reading
//    0     12     18     21     26     27     12    bee speed, min
//    1     15     20     23     27     28     15    bee speed, max
//    2      5      4      4      4      4      5    unidentified pair
//    3      6      6      5      5      5      6
//    4      1      1      1      1      1      1    constant
//    5      3      3      1      3      5      3    unidentified
//    6      8      6      4      3      3      0    revive delay, min (s)
//    7     24     10     10      9      9      0    revive delay, max (s)
//    8      2      3      3      3      3      0    bee count, min
//    9      3      4      4      4      4      1    bee count, max
//
// The win condition - every bee down at the same moment - is likewise inferred.
// It is the reading that explains why the record ships five "Bee_Revive" sounds
// and a revive delay at all: downing them one at a time cannot be enough if
// they get back up. Nothing in the data states it.
//
// Also worth recording: the record's own dependency is "event flag 2458 is
// clear", and **no record anywhere in the game sets flag 2458**, so that gate is
// permanently satisfied and the puzzle always runs.
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

	// -- debug affordance, default off; see the comment in the .cpp --
	static bool autoPlay();
	void autoSpray(uint32 now);
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

	// Autoplay only: earliest time the next automatic spray may go off.
	uint32 _autoNextSpray = 0;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_WHACKITPUZZLE_H
