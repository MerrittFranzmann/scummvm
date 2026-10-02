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

// "Dance Dance Yeah", Nancy16's dance audition. The player presses move
// buttons in time with cues; each cue raises an event flag that lights the
// stage through a sibling movie record. The score goes into the player table,
// where sibling records check it once the music stops.
class DancePuzzle : public RenderActionRecord {
public:
	// The stage-light movies sit at z 10 and cover almost the whole viewport, so
	// the buttons have to be drawn above them.
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
		Common::Rect srcPressed;	// pressed
		Common::Rect srcNormal;		// normal
		Common::Rect dest;			// viewport-relative position and hotspot
		Common::String animName;	// matches Cue::animName
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
		int16 eventFlag = kEvNoEvent;	// lights the sibling stage-light movie
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
	Common::String _puzzleName;
	uint16 _hoverCursorID = 0;			// unknown
	uint16 _scoreValueIndex = 0;		// player-table index of the score
	Common::String _rootAnimName;		// the idle loop video
	Common::Path _buttonImageName;

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
