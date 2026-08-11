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

#ifndef NANCY_ACTION_ROTATINGLOCKPUZZLE_H
#define NANCY_ACTION_ROTATINGLOCKPUZZLE_H

#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/cursor.h"

namespace Nancy {
namespace Action {

class RotatingLockPuzzle : public RenderActionRecord {
public:
	enum SolveState { kNotSolved, kPlaySound, kWaitForSound };
	RotatingLockPuzzle() : RenderActionRecord(7) {}
	virtual ~RotatingLockPuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	Common::Path _imageName;
	Common::Array<Common::Rect> _srcRects;
	Common::Array<Common::Rect> _destRects;
	Common::Array<Common::Rect> _upHotspots;
	Common::Array<Common::Rect> _downHotspots;
	Common::Array<byte> _correctSequence;
	uint16 _iconsPerDial = 10;
	// Cursor types shown while hovering a dial's up/down hotspot. Nancy 10+
	// stores these per-puzzle (e.g. a crank uses the rotate-clockwise cursor);
	// older games and unset fields default to the up/down movement cursors.
	CursorManager::CursorType _upCursorType = CursorManager::kMoveUp;
	CursorManager::CursorType _downCursorType = CursorManager::kMoveDown;
	SoundDescription _clickSound;
	SceneChangeWithFlag _solveExitScene;
	uint16 _solveSoundDelay = 0;
	SoundDescription _solveSound;
	SceneChangeWithFlag _exitScene;
	Common::Rect _exitHotspot;
	// Nancy16 carries the exit hotspot's cursor in the record; 0 means "use the
	// engine's puzzle-exit cursor", as it does everywhere else in that game.
	uint16 _exitCursorType = 0;

	SolveState _solveState = kNotSolved;
	Graphics::ManagedSurface _image;
	Common::Array<byte> _currentSequence;
	Time _solveSoundPlayTime;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "RotatingLockPuzzle"; }

	void readNancy16Data(Common::SeekableReadStream &stream);
	void drawDial(uint id);

	// Every dial sits on its target. execute() uses this to decide the puzzle is
	// over; the autoplay below uses the SAME function to decide it is done, so
	// the two can never disagree about what "solved" means.
	bool isSolved() const;

	// The one implementation of "a click on dial id's up (or down) wheel was
	// accepted": plays the click sound, advances the icon with the ring wrap,
	// redraws. handleInput() calls it for a real click and the autoplay calls it
	// for a synthetic one, so a synthetic step cannot drift from a real one.
	void stepDial(uint id, bool up);

	// -- debug affordance, default off; see the comment in the .cpp --
	static bool autoPlay();
	void autoStep();

	// Autoplay only: how many notches the hook has moved, for its one log line.
	uint _autoClicks = 0;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_ROTATINGLOCKPUZZLE_H
