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

#include "common/debug.h"
#include "common/random.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/input.h"
#include "engines/nancy/util.h"
#include "engines/nancy/puzzledata.h"

#include "engines/nancy/state/scene.h"
#include "engines/nancy/action/puzzle/dancepuzzle.h"

namespace Nancy {
namespace Action {

void DancePuzzle::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _puzzleName);			// 0x00, "NancyDancy"
	_hoverCursorID = stream.readUint16LE();		// 0x21, 29 in every record
	_scoreValueIndex = stream.readUint16LE();	// 0x23, 38 in every record
	readFilename(stream, _rootAnimName);		// 0x25, MIC_NDDanceRoot_ANIM
	readFilename(stream, _buttonImageName);		// 0x46, MIC_StageButtons_OVL

	uint16 numButtons = stream.readUint16LE();	// 0x67, always 7
	_buttons.resize(numButtons);
	for (uint i = 0; i < numButtons; ++i) {
		MoveButton &button = _buttons[i];
		readRect(stream, button.srcPressed);
		readRect(stream, button.srcNormal);
		readRect(stream, button.dest);
		readFilename(stream, button.animName);
	}

	// The music track the cue times are measured against
	_music.readData(stream);

	uint16 numCues = stream.readUint16LE();
	_cues.resize(numCues);
	for (uint i = 0; i < numCues; ++i) {
		Cue &cue = _cues[i];
		readFilename(stream, cue.animName);
		cue.eventFlag = stream.readUint16LE();
		cue.startTime = stream.readUint32LE();
		cue.endTime = stream.readUint32LE();

		uint16 numSteps = stream.readUint16LE();
		cue.steps.resize(numSteps);
		for (uint j = 0; j < numSteps; ++j) {
			cue.steps[j].points = stream.readSint16LE();
			cue.steps[j].time = stream.readUint32LE();
		}
	}

	// Hit-reaction sound banks, keyed by the points scored. Always three:
	// [-10,-1] bad, [-1,1] or [0,8] neutral, [9,10] good.
	uint16 numBanks = stream.readUint16LE();
	_hitSounds.resize(numBanks);
	for (uint i = 0; i < numBanks; ++i) {
		_hitSounds[i].minPoints = stream.readSint16LE();
		_hitSounds[i].maxPoints = stream.readSint16LE();
		_hitSounds[i].sounds.readData(stream);
	}
}

void DancePuzzle::init() {
	Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(),
			g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	g_nancy->_resource->loadImage(_buttonImageName, _buttonImage);
	_buttonImage.setTransparentColor(_drawSurface.getTransparentColor());

	for (uint i = 0; i < _cues.size(); ++i) {
		_cues[i].lit = false;
		_cues[i].resolved = false;
	}

	_pressedButton = -1;
	_pressedUntil = 0;
	_finished = false;

	redraw();
}

// The grading steps are stored in ascending time order, with descending
// points: (10, t0), (0, t0 + 1s), (-10, t0 + 3s). A click scores the points of
// the last step whose time has already passed.
int16 DancePuzzle::gradeClick(const Cue &cue, uint32 now) const {
	int16 points = 0;
	for (uint i = 0; i < cue.steps.size(); ++i) {
		if (now >= cue.steps[i].time) {
			points = cue.steps[i].points;
		} else {
			break;
		}
	}

	return points;
}

int16 DancePuzzle::worstPoints(const Cue &cue) const {
	if (cue.steps.empty()) {
		return 0;
	}

	return cue.steps[cue.steps.size() - 1].points;
}

// The score lives in the player table, at the index named in the record's
// header (38). The sibling SetValue record seeds it with 50 on scene entry,
// and the scene's SceneChange records read it back once the music stops.
// Index routing has to match SetValue's: indices at or above the single-value
// count address the combo (float) half of the table, and 38 does.
int16 DancePuzzle::readScore() const {
	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	return playerTable ? playerTable->getValue(_scoreValueIndex) : kNoTableValue;
}

void DancePuzzle::applyScore(int16 points) {
	if (points == 0) {
		return;
	}

	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	if (!playerTable) {
		return;
	}

	int16 current = playerTable->getValue(_scoreValueIndex);
	if (current == kNoTableValue) {
		current = 0;
	}

	int32 updated = (int32)current + points;

	// The DanceMeter widget this value drives has min 0 and max 100.
	updated = CLIP<int32>(updated, 0, 100);

	uint numSingleValues = playerTable->getNumSingleValues();
	if (_scoreValueIndex < numSingleValues) {
		playerTable->setSingleValue(_scoreValueIndex, (int16)updated);
	} else {
		playerTable->setComboValue(_scoreValueIndex - numSingleValues, (float)updated);
	}
}

void DancePuzzle::playHitSound(int16 points) {
	for (uint i = 0; i < _hitSounds.size(); ++i) {
		const HitSoundBank &bank = _hitSounds[i];
		if (points < bank.minPoints || points > bank.maxPoints) {
			continue;
		}

		if (bank.sounds.names.empty()) {
			return;
		}

		uint idx = bank.sounds.names.size() == 1 ? 0 :
			g_nancy->_randomSource->getRandomNumber(bank.sounds.names.size() - 1);
		const Common::String &name = bank.sounds.names[idx];
		if (name.empty() || name.equalsIgnoreCase("NO SOUND")) {
			return;
		}

		g_nancy->_sound->stopSound(_lastHitSound);

		_lastHitSound = SoundDescription();
		_lastHitSound.name = name;
		_lastHitSound.channelID = bank.sounds.channel;
		_lastHitSound.numLoops = 1;
		_lastHitSound.volume = bank.sounds.volume;

		g_nancy->_sound->loadSound(_lastHitSound);
		g_nancy->_sound->playSound(_lastHitSound);
		return;
	}
}

// Raising the cue's event flag is what makes the matching coloured stage light
// appear: the seven sibling type 44 movie records in the scene each depend on
// one of flags 1010..1016.
void DancePuzzle::lightCue(Cue &cue, bool on) {
	if (cue.lit == on) {
		return;
	}

	cue.lit = on;
	debugC(1, kDebugScene, "DancePuzzle: cue %s flag %d -> %s",
		cue.animName.c_str(), cue.eventFlag, on ? "on" : "off");
	if (cue.eventFlag != kEvNoEvent) {
		NancySceneState.setEventFlag(cue.eventFlag, on ? g_nancy->_true : g_nancy->_false);
	}
}

void DancePuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();

		if (!_music.names.empty() && !_music.names[0].equalsIgnoreCase("NO SOUND")) {
			_musicSound = SoundDescription();
			_musicSound.name = _music.names[0];
			_musicSound.channelID = _music.channel;
			_musicSound.numLoops = _music.numLoops > 0 ? _music.numLoops : 1;
			_musicSound.volume = _music.volume;
			g_nancy->_sound->loadSound(_musicSound);
			g_nancy->_sound->playSound(_musicSound);
		}

		_startTime = g_nancy->getTotalPlayTime();
		_state = kRun;
		// fall through

	case kRun: {
		if (_finished) {
			break;
		}

		uint32 now = g_nancy->getTotalPlayTime() - _startTime;
		bool needRedraw = false;

		// Drop the pressed-button highlight once its hold time is up
		if (_pressedButton != -1 && now >= _pressedUntil) {
			_pressedButton = -1;
			needRedraw = true;
		}

		bool allResolved = true;
		for (uint i = 0; i < _cues.size(); ++i) {
			Cue &cue = _cues[i];
			if (cue.resolved) {
				continue;
			}

			if (now >= cue.endTime) {
				// The window closed without a click. A maximally late click
				// would have scored the last grading step, so that is what a
				// no-show costs.
				// NOTE: unverified - the record says nothing about what a
				// missed cue is worth.
				lightCue(cue, false);
				applyScore(worstPoints(cue));
				cue.resolved = true;
				continue;
			}

			allResolved = false;

			if (now >= cue.startTime) {
				lightCue(cue, true);
			}
		}

		if (needRedraw) {
			redraw();
		}

		// The scene's own SceneChange records wait on the music channel going
		// quiet and then compare the score, so all this record has to do is
		// stop taking input and let go.
		if (allResolved && !g_nancy->_sound->isSoundPlaying(_musicSound)) {
			for (uint i = 0; i < _cues.size(); ++i) {
				lightCue(_cues[i], false);
			}

			debugC(1, kDebugScene, "DancePuzzle: finished, score value %d = %d",
				_scoreValueIndex, readScore());
			_finished = true;
			finishExecution();
		}

		break;
	}
	case kActionTrigger:
		finishExecution();
		break;
	}
}

void DancePuzzle::handleInput(NancyInput &input) {
	if (_finished || _state != kRun) {
		return;
	}

	Common::Rect vpScreen = NancySceneState.getViewport().getScreenPosition();
	Common::Point mouseVP = input.mousePos - Common::Point(vpScreen.left, vpScreen.top);

	for (uint i = 0; i < _buttons.size(); ++i) {
		if (!_buttons[i].dest.contains(mouseVP)) {
			continue;
		}

		g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			uint32 now = g_nancy->getTotalPlayTime() - _startTime;

			_pressedButton = (int)i;
			_pressedUntil = now + kPressedDrawTime;

			// Find the cue that is currently lit, if any
			Cue *live = nullptr;
			for (uint j = 0; j < _cues.size(); ++j) {
				if (!_cues[j].resolved && now >= _cues[j].startTime && now < _cues[j].endTime) {
					live = &_cues[j];
					break;
				}
			}

			if (live) {
				int16 points;
				if (live->animName.equalsIgnoreCase(_buttons[i].animName)) {
					points = gradeClick(*live, now);
				} else {
					// Wrong move. NOTE: unverified - scored as the worst the
					// live cue can give, which is always the bad bank.
					points = worstPoints(*live);
				}

				debugC(1, kDebugScene, "DancePuzzle: hit %s during cue %s at %ums -> %d points",
					_buttons[i].animName.c_str(), live->animName.c_str(), now, points);

				applyScore(points);
				playHitSound(points);
				lightCue(*live, false);
				live->resolved = true;
			}
			// NOTE: unverified - a click with no cue lit is ignored here. The
			// record gives no rule for it either way.

			redraw();
		}

		return;
	}
}

void DancePuzzle::redraw() {
	_drawSurface.clear(_drawSurface.getTransparentColor());

	for (uint i = 0; i < _buttons.size(); ++i) {
		const MoveButton &button = _buttons[i];
		const Common::Rect &src = ((int)i == _pressedButton) ? button.srcPressed : button.srcNormal;
		_drawSurface.blitFrom(_buttonImage, src, Common::Point(button.dest.left, button.dest.top));
	}

	_needsRedraw = true;
}

} // End of namespace Action
} // End of namespace Nancy
