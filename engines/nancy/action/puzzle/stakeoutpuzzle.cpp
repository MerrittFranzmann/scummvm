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

#include "common/config-manager.h"
#include "common/random.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/cursor.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/input.h"
#include "engines/nancy/nduipanel.h"
#include "engines/nancy/puzzledata.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/util.h"

#include "engines/nancy/action/puzzle/stakeoutpuzzle.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

StakeOutPuzzle::~StakeOutPuzzle() {
	// The narration panel is persistent, so clear the last spoken line
	if (NDUIPanel *narration = NancySceneState.getNarrationPanel()) {
		narration->narrationClear();
	}

	// The list panel is persistent too, so take the reports down
	if (NDUIPanel *panel = NancySceneState.getStakeoutPanel()) {
		panel->stakeoutSetLines(Common::StringArray());
		NancySceneState.applyNDUICommand(_dialogName, 2);
	}
}

void StakeOutPuzzle::readCountedRects(Common::SeekableReadStream &stream, Common::Array<Common::Rect> &out) {
	const uint16 count = stream.readUint16LE();
	readRectArray(stream, out, count);
}

void StakeOutPuzzle::readData(Common::SeekableReadStream &stream) {
	_valueIndexA = stream.readUint16LE();
	_valueIndexB = stream.readUint16LE();

	readFilename(stream, _dialogName);
	readFilename(stream, _entryName);

	_unknown29 = stream.readUint16LE();

	for (int i = 0; i < 5; ++i) {
		_tuning[i] = stream.readDoubleLE();
	}

	for (int i = 0; i < 3; ++i) {
		_ranges[i][0] = stream.readDoubleLE();
		_ranges[i][1] = stream.readDoubleLE();
	}

	const uint16 numSilhouettes = stream.readUint16LE();
	_silhouettes.resize(numSilhouettes);

	for (uint i = 0; i < numSilhouettes; ++i) {
		Silhouette &s = _silhouettes[i];
		readFilename(stream, s.label);
		readRect(stream, s.dest);
		readFilename(stream, s.imageName);
		// Coming out and going back in, 17 frames each
		readCountedRects(stream, s.framesA);
		readCountedRects(stream, s.framesB);
		s.line.readData(stream);
	}

	const uint16 numSuspects = stream.readUint16LE();
	_suspects.resize(numSuspects);

	for (uint i = 0; i < numSuspects; ++i) {
		Suspect &s = _suspects[i];
		readFilename(stream, s.name);
		s.unknownByte = stream.readByte();
		s.unknownValue = stream.readUint16LE();

		// Nico's three groups are stored with a zero count
		for (int g = 0; g < 3; ++g) {
			s.lines[g].readData(stream);
		}
	}
}

// _tuning[4] (12.0), assumed to be frames per second
uint32 StakeOutPuzzle::frameDelayMs() const {
	const double fps = _tuning[4];
	if (fps <= 0.0) {
		return 80;
	}

	return (uint32)(1000.0 / fps);
}

// The three (0, 2) ranges, assumed to be in seconds
uint32 StakeOutPuzzle::randomRangeMs(uint which) const {
	if (which >= 3) {
		return 0;
	}

	const int32 lo = (int32)(_ranges[which][0] * 1000.0);
	const int32 hi = (int32)(_ranges[which][1] * 1000.0);
	if (hi <= lo) {
		return (uint32)MAX<int32>(0, lo);
	}

	return (uint32)(lo + (int32)g_nancy->_randomSource->getRandomNumber(hi - lo));
}

// Spoken lines are captioned in the narration panel, not the conversation box.
void StakeOutPuzzle::say(const Common::String &line) {
	if (line.empty() || !ConfMan.getBool("subtitles")) {
		return;
	}

	if (NDUIPanel *narration = NancySceneState.getNarrationPanel()) {
		narration->narrationSetCaption(line);
	}
}

SoundDescription StakeOutPuzzle::playSoundBlock(const RandomSoundBlock &block) {
	SoundDescription desc;
	if (block.names.empty()) {
		return desc;
	}

	const uint idx = block.names.size() == 1 ? 0 :
		g_nancy->_randomSource->getRandomNumber(block.names.size() - 1);
	const Common::String &name = block.names[idx];
	if (name.empty() || name.equalsIgnoreCase("NO SOUND")) {
		return desc;
	}

	desc.name = name;
	desc.channelID = block.channel;
	desc.numLoops = block.numLoops > 0 ? block.numLoops : 1;
	desc.volume = block.volume;

	g_nancy->_sound->loadSound(desc);
	g_nancy->_sound->playSound(desc);
	say(resolveSubtitleText(name, Common::String(), "CONVO"));
	return desc;
}

void StakeOutPuzzle::init() {
	const Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	// No transparent colour: each frame is a full cell of repainted courtyard
	// that replaces the background rather than being keyed over it.
	for (uint i = 0; i < _silhouettes.size(); ++i) {
		Silhouette &s = _silhouettes[i];
		g_nancy->_resource->loadImage(s.imageName, s.image);
	}

	// Bruno, Eva, Gino and Sabina in record order; Nico is the one with no lines.
	_agents.clear();
	_nico = -1;
	for (uint i = 0; i < _suspects.size(); ++i) {
		if (_suspects[i].isTarget()) {
			if (_nico < 0) {
				_nico = (int)i;
			}
		} else {
			_agents.push_back(i);
		}
	}

	for (uint i = 0; i < _silhouettes.size(); ++i) {
		_silhouettes[i].occupied = false;
		_silhouettes[i].peek = Silhouette::kHidden;
		_silhouettes[i].frame = 0;
	}

	for (uint i = 0; i < _suspects.size(); ++i) {
		_suspects[i].position = -1;
	}

	placeAgents();
	placeNico();

	// The list fills in as the reports are spoken
	_reportsDone = 0;
	_reportWaitUntil = g_nancy->getTotalPlayTime();
	_phase = kReporting;
	publishList();
	redrawSilhouettes();
}

// The four agents take four distinct places and keep them for the whole
// stake-out.
void StakeOutPuzzle::placeAgents() {
	Common::Array<uint> pool;
	for (uint i = 0; i < _silhouettes.size(); ++i) {
		if (!_silhouettes[i].occupied) {
			pool.push_back(i);
		}
	}

	const uint32 now = g_nancy->getTotalPlayTime();

	for (uint a = 0; a < _agents.size() && !pool.empty(); ++a) {
		const uint pick = g_nancy->_randomSource->getRandomNumber(pool.size() - 1);
		const uint where = pool[pick];
		pool.remove_at(pick);

		_suspects[_agents[a]].position = (int)where;
		Silhouette &s = _silhouettes[where];
		s.occupied = true;
		s.peek = Silhouette::kHidden;
		s.frame = 0;
		s.nextStepTime = now + randomRangeMs(0);
	}
}

// Nico moves after every judged click, onto a place no agent holds.
void StakeOutPuzzle::placeNico() {
	if (_nico < 0) {
		return;
	}

	Suspect &nico = _suspects[_nico];
	const int was = nico.position;
	if (was >= 0) {
		_silhouettes[was].occupied = false;
		_silhouettes[was].peek = Silhouette::kHidden;
		_silhouettes[was].frame = 0;
	}

	Common::Array<uint> pool;
	for (uint i = 0; i < _silhouettes.size(); ++i) {
		// Not where he already was, so that "he's gone" holds on screen
		if (!_silhouettes[i].occupied && (int)i != was) {
			pool.push_back(i);
		}
	}

	if (pool.empty()) {
		nico.position = -1;
		return;
	}

	const uint where = pool[g_nancy->_randomSource->getRandomNumber(pool.size() - 1)];
	nico.position = (int)where;

	Silhouette &s = _silhouettes[where];
	s.occupied = true;
	s.peek = Silhouette::kHidden;
	s.frame = 0;
	s.nextStepTime = g_nancy->getTotalPlayTime() + randomRangeMs(0);

}

// One line per report already spoken. The CONVO strings, keyed
// <SuspectName><SilhouetteLetter>, carry their own <cN> colour codes.
void StakeOutPuzzle::publishList() {
	NDUIPanel *panel = NancySceneState.getStakeoutPanel();
	if (!panel) {
		return;
	}

	Common::StringArray lines;
	for (uint a = 0; a < _reportsDone && a < _agents.size(); ++a) {
		const Suspect &s = _suspects[_agents[a]];
		if (s.position < 0) {
			continue;
		}

		const Common::String key = s.name + _silhouettes[s.position].label;
		const Common::String text = resolveSubtitleText(key, Common::String(), "CONVO");
		if (!text.empty()) {
			lines.push_back(text);
		}
	}

	NancySceneState.applyNDUICommand(_dialogName, lines.empty() ? 2 : 1);
	panel->stakeoutSetLines(lines);
}

// One report at a time, each waiting for the last to finish, as in S5611-S5614.
void StakeOutPuzzle::stepReports(uint32 now) {
	if (_reportsDone >= _agents.size()) {
		_phase = kWatching;
		return;
	}

	if (now < _reportWaitUntil || g_nancy->_sound->isSoundPlaying((uint16)kReportChannel)) {
		return;
	}

	const Suspect &s = _suspects[_agents[_reportsDone]];
	if (s.position >= 0) {
		SoundDescription desc;
		desc.name = s.name + _silhouettes[s.position].label;
		desc.channelID = kReportChannel;
		desc.numLoops = 1;
		desc.volume = kReportVolume;
		g_nancy->_sound->loadSound(desc);
		g_nancy->_sound->playSound(desc);
	}

	++_reportsDone;
	// Minimum wait, so a missing sound file cannot fire all reports in one frame
	_reportWaitUntil = now + 400;
	publishList();
}

void StakeOutPuzzle::stepSilhouettes(uint32 now) {
	const uint32 step = frameDelayMs();
	bool dirty = false;

	for (uint i = 0; i < _silhouettes.size(); ++i) {
		Silhouette &s = _silhouettes[i];
		if (!s.occupied || now < s.nextStepTime) {
			continue;
		}

		switch (s.peek) {
		case Silhouette::kHidden:
			s.peek = Silhouette::kEmerging;
			s.frame = 0;
			s.nextStepTime = now + step;
			dirty = true;
			break;
		case Silhouette::kEmerging:
			++s.frame;
			if (s.frame >= s.framesA.size()) {
				s.frame = s.framesA.empty() ? 0 : s.framesA.size() - 1;
				s.peek = Silhouette::kOut;
				s.nextStepTime = now + randomRangeMs(1);
			} else {
				s.nextStepTime = now + step;
			}

			dirty = true;
			break;
		case Silhouette::kOut:
			s.peek = Silhouette::kWithdrawing;
			s.frame = 0;
			s.nextStepTime = now + step;
			dirty = true;
			break;
		case Silhouette::kWithdrawing:
			++s.frame;
			if (s.frame >= s.framesB.size()) {
				s.peek = Silhouette::kHidden;
				s.frame = 0;
				s.nextStepTime = now + randomRangeMs(2) + randomRangeMs(0);
			} else {
				s.nextStepTime = now + step;
			}

			dirty = true;
			break;
		}
	}

	if (dirty) {
		redrawSilhouettes();
	}
}

void StakeOutPuzzle::redrawSilhouettes() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	for (uint i = 0; i < _silhouettes.size(); ++i) {
		const Silhouette &s = _silhouettes[i];
		if (s.peek == Silhouette::kHidden || !s.image.getPixels()) {
			continue;
		}

		const Common::Array<Common::Rect> &frames =
			(s.peek == Silhouette::kWithdrawing) ? s.framesB : s.framesA;
		if (frames.empty()) {
			continue;
		}

		const Common::Rect &src = frames[MIN<uint>(s.frame, frames.size() - 1)];
		_drawSurface.blitFrom(s.image, src, Common::Point(s.dest.left, s.dest.top));
	}

	setNeedsRedraw(true);
}

// A click on an agent is answered by that agent. For any other click the
// record gives no rule, so an agent is picked at random.
uint StakeOutPuzzle::pickResponder() const {
	if (_agents.empty()) {
		return 0;
	}

	return _agents[g_nancy->_randomSource->getRandomNumber(_agents.size() - 1)];
}

void StakeOutPuzzle::judgeClick(uint index, uint32 now) {
	// Nancy names the place first; the agent's reply waits until her line ends.
	const SoundDescription nancyLine = playSoundBlock(_silhouettes[index].line);
	_nancyChannel = nancyLine.name.empty() ? -1 : nancyLine.channelID;

	_pendingResponder = -1;
	_pendingGroup = 1;

	if (_nico >= 0 && _suspects[_nico].position == (int)index) {
		_pendingResponder = (int)pickResponder();
		_pendingGroup = 2;					// "Fermati!"
		_pendingValueIndex = _valueIndexA;
	} else {
		for (uint a = 0; a < _agents.size(); ++a) {
			if (_suspects[_agents[a]].position == (int)index) {
				_pendingResponder = (int)_agents[a];
				_pendingGroup = 0;			// "That's me, not Nico!"
				break;
			}
		}

		if (_pendingResponder < 0) {
			_pendingResponder = (int)pickResponder();
			_pendingGroup = 1;				// "He's not there."
		}

		_pendingValueIndex = _valueIndexB;
	}

	_pendingDelta = 1;
	_responseChannel = -1;
	_responsePlayed = false;

	// Apply the score only after the reply has played, since the scene's exits
	// fire as soon as the values match and would cut the line off.
	_judgeWaitUntil = now + 600;
	_phase = kJudging;
}

// Written the same way AR 108 SetValue writes one. Indices past the single
// values are combo (float) slots.
void StakeOutPuzzle::addToValue(uint16 index, int16 delta) {
	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	if (!playerTable) {
		return;
	}

	const uint numSingleValues = playerTable->getNumSingleValues();

	if (index < numSingleValues) {
		const int16 cur = playerTable->getSingleValue(index);
		playerTable->setSingleValue(index, cur == kNoTableValue ? delta : (int16)(cur + delta));
	} else {
		const float cur = playerTable->getComboValue(index - numSingleValues);
		playerTable->setComboValue(index - numSingleValues,
			cur == (float)kNoTableValue ? (float)delta : cur + (float)delta);
	}
}

void StakeOutPuzzle::applyScore() {
	if (_pendingValueIndex < 0) {
		return;
	}

	addToValue((uint16)_pendingValueIndex, _pendingDelta);

	_pendingValueIndex = -1;
}

void StakeOutPuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_state = kRun;
		// fall through
	case kRun: {
		const uint32 now = g_nancy->getTotalPlayTime();

		stepSilhouettes(now);

		switch (_phase) {
		case kReporting:
			stepReports(now);
			break;
		case kWatching:
			break;
		case kJudging:
			// Nancy first, then the agent who answers her.
			if (!_responsePlayed) {
				if (_nancyChannel >= 0 && g_nancy->_sound->isSoundPlaying((uint16)_nancyChannel)) {
					break;
				}

				if (_pendingResponder >= 0) {
					const SoundDescription desc =
						playSoundBlock(_suspects[_pendingResponder].lines[_pendingGroup]);
					if (!desc.name.empty()) {
						_responseChannel = desc.channelID;
					}
				}

				_responsePlayed = true;
				break;
			}

			if (now < _judgeWaitUntil) {
				break;
			}

			if (_responseChannel >= 0 && g_nancy->_sound->isSoundPlaying((uint16)_responseChannel)) {
				break;
			}

			applyScore();

			// S5610's own scene change records decide when the stake-out ends
			placeNico();

			// Back to reporting if not every agent has radioed in yet
			_phase = (_reportsDone < _agents.size()) ? kReporting : kWatching;
			break;
		}

		break;
	}
	case kActionTrigger:
		finishExecution();
		break;
	}
}

void StakeOutPuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _phase == kJudging) {
		return;
	}

	for (uint i = 0; i < _silhouettes.size(); ++i) {
		const Common::Rect screen =
			NancySceneState.getViewport().convertViewportToScreen(_silhouettes[i].dest);
		if (!screen.contains(input.mousePos)) {
			continue;
		}

		g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			input.eatMouseInput();
			judgeClick(i, g_nancy->getTotalPlayTime());
		}

		return;
	}
}

} // End of namespace Action
} // End of namespace Nancy
