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

#ifndef NANCY_ACTION_DANCEPUZZLE_H
#define NANCY_ACTION_DANCEPUZZLE_H

#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/commontypes.h"

namespace Nancy {
namespace Action {

// Nancy16 (Phantom of Venice) action record type 214, described "Dance Dance
// Yeah" - the Dance Audition minigame. Six records, S2670-72 (first audition,
// with Faustino's "Fau##" reaction sounds) and S2770-72 (second audition, with
// the Dance_Hit_* sounds).
//
// The record carries seven dance-move buttons cut out of a single overlay
// atlas (MIC_StageButtons_OVL, two 75px rows: one normal, one pressed), a
// music track, and a list of timed cues. Each cue names one of the moves, an
// event flag, a time window in music-track milliseconds, and a descending
// ladder of (points, time) grading steps: click early for +10, later for 0,
// later still for -10.
//
// The coloured stage light that tells the player which move is wanted is NOT
// in this record: the cue's event flag (1010..1016) gates one of the seven
// sibling type 44 movie records in the same scene, each of which plays a
// MIC_DanceLight<Colour>_ANIM video. All this record has to do is raise the
// flag while the cue is live.
//
// Likewise the win/lose branch is not in this record. Scoring is written to
// player-table value 38 (initialised to 50 by the sibling SetValue record, and
// the value the DANCEPUZZLE NDUI's "DanceMeter" widget is bound to, min 0 max
// 100). When the music channel goes quiet the scene's SceneChange records
// compare that value against 30/50 and branch.
class DancePuzzle : public RenderActionRecord {
public:
	// z-order 11: the coloured stage-light cues are sibling PlaySecondaryMovie
	// records that the scene data puts at z 10, and their video covers almost
	// the whole viewport opaquely. Anything at or below that vanishes the
	// moment a cue lights, so the buttons have to sit above them.
	DancePuzzle() : RenderActionRecord(11) {}
	virtual ~DancePuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "DancePuzzle"; }

	// One of the seven dance moves the player can press
	struct MoveButton {
		Common::Rect srcPressed;	// atlas row 2 (y 76..150)
		Common::Rect srcNormal;		// atlas row 1 (y 1..75)
		Common::Rect dest;			// viewport-relative position and hotspot
		Common::String animName;	// MIC_NDDance<Move>_ANIM, matches Cue::animName
	};

	// A single grading step. The player scores the points of the last step
	// whose time has already passed.
	struct GradingStep {
		int16 points = 0;
		uint32 time = 0;
	};

	// One cue: "do this move, now"
	struct Cue {
		Common::String animName;	// which move is wanted
		int16 eventFlag = kEvNoEvent;	// 1010..1016, lights the sibling colour movie
		uint32 startTime = 0;		// music-track ms
		uint32 endTime = 0;
		Common::Array<GradingStep> steps;

		// Runtime
		bool lit = false;
		bool resolved = false;
	};

	// A hit-reaction sound bank, selected by the points the player just scored
	struct HitSoundBank {
		int16 minPoints = 0;
		int16 maxPoints = 0;
		RandomSoundBlock sounds;
	};

	// File data
	Common::String _puzzleName;			// "NancyDancy"
	uint16 _hoverCursorID = 0;			// 29 in all six records; purpose unverified
	uint16 _scoreValueIndex = 0;		// 38 in all six records; the DanceMeter value
	Common::String _rootAnimName;		// MIC_NDDanceRoot_ANIM, the idle loop video
	Common::Path _buttonImageName;		// MIC_StageButtons_OVL

	Common::Array<MoveButton> _buttons;
	RandomSoundBlock _music;
	Common::Array<Cue> _cues;
	Common::Array<HitSoundBank> _hitSounds;

	// Runtime state
	Graphics::ManagedSurface _buttonImage;

	SoundDescription _musicSound;
	SoundDescription _lastHitSound;

	uint32 _startTime = 0;			// getTotalPlayTime() when the music started
	int _pressedButton = -1;		// button drawn in its pressed state, -1 = none
	uint32 _pressedUntil = 0;
	bool _finished = false;

	static const uint32 kPressedDrawTime = 250;

	// Internal methods
	int16 gradeClick(const Cue &cue, uint32 now) const;
	int16 worstPoints(const Cue &cue) const;
	int16 readScore() const;
	void applyScore(int16 points);
	void playHitSound(int16 points);
	void lightCue(Cue &cue, bool on);
	void redraw();
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_DANCEPUZZLE_H
