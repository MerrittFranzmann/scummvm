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

#include "engines/nancy/nancy.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/font.h"
#include "engines/nancy/cursor.h"
#include "engines/nancy/input.h"
#include "engines/nancy/util.h"

#include "engines/nancy/action/miscrecords.h"

#include "engines/nancy/state/scene.h"
#include "engines/nancy/misc/specialeffect.h"

#include "common/events.h"
#include "common/config-manager.h"
#include "common/random.h"
#include "common/file.h"

#include "image/png.h"

#include "nancy/ui/taskbar.h"

namespace Nancy {
namespace Action {

void PaletteThisScene::readData(Common::SeekableReadStream &stream) {
	_paletteID = stream.readByte();
	_unknownEnum = stream.readByte();
	_paletteStart = stream.readUint16LE();
	_paletteSize = stream.readUint16LE();
}

void PaletteThisScene::execute() {
	NancySceneState.getSceneInfo().paletteID = _paletteID;
	const State::Scene::SceneSummary &ssum = NancySceneState.getSceneSummary();

	if (_unknownEnum > 1 && _unknownEnum < 3) {
		// Not sure what the difference is between the 3 types
		NancySceneState.getViewport().setPalette(ssum.palettes[_paletteID], _paletteStart, _paletteSize);
	} else {
		NancySceneState.getViewport().setPalette(ssum.palettes[_paletteID]);
	}

	finishExecution();
}

void PaletteNextScene::readData(Common::SeekableReadStream &stream) {
	// Structure is the same as PaletteThisScene, but the original engine only uses the palette ID
	_paletteID = stream.readByte();
	stream.skip(5);
}

void PaletteNextScene::execute() {
	NancySceneState.getNextSceneInfo().paletteID = _paletteID;
	_isDone = true;
}

void LightningOn::readData(Common::SeekableReadStream &stream) {
	_distance = stream.readSint16LE();
	_pulseTime = stream.readUint16LE();
	_rgbPercent = stream.readSint16LE();
	stream.skip(4);
}

void SpecialEffect::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		// Nancy16 replaced the special-effect block with a fade:
		//   float  fade step
		//   uint32 frames
		//   uint32 from, uint32 to     alpha endpoints, (0,255) or (255,0)
		//   RECT   area to fade
		//   int16 label + byte flag    event flag set when it finishes
		stream.skip(4);	// fade step
		_totalTime = stream.readUint32LE();
		const uint32 fadeFrom = stream.readUint32LE();
		const uint32 fadeTo = stream.readUint32LE();
		readRect(stream, _rect);

		_flagOnCompletion.label = stream.readSint16LE();
		_flagOnCompletion.flag = stream.readByte();

		// kBlackout fades out, kCrossDissolve fades in
		_type = (fadeFrom > fadeTo) ? Misc::SpecialEffect::kBlackout :
			Misc::SpecialEffect::kCrossDissolve;
		_fadeToBlackTime = _totalTime;
		return;
	}

	if (g_nancy->getGameType() <= kGameTypeNancy6) {
		_type = stream.readByte();
		_fadeToBlackTime = stream.readUint16LE();
		_frameTime = stream.readUint16LE();
	} else {
		_type = stream.readByte();
		_totalTime = stream.readUint16LE();
		_fadeToBlackTime = stream.readUint16LE();
		readRect(stream, _rect);
	}
}

void SpecialEffect::execute() {
	if (g_nancy->getGameType() <= kGameTypeNancy6) {
		NancySceneState.specialEffect(_type, _fadeToBlackTime, _frameTime);
	} else {
		NancySceneState.specialEffect(_type, _totalTime, _fadeToBlackTime, _rect);
	}

	// Nancy16 records raise a flag once the effect has started
	if (_flagOnCompletion.label != -1) {
		NancySceneState.setEventFlag(_flagOnCompletion);
	}

	_isDone = true;
}

void LightningOn::execute() {
	NancySceneState.beginLightning(_distance, _pulseTime, _rgbPercent);
	_isDone = true;
}

// Reads a textbox caption: an int16 length, then either a CVTX (AUTOTEXT) key
// to resolve (length == -1) or that many bytes of inline text. Shared by the
// textbox-writing action records.
static void readTextboxText(Common::SeekableReadStream &stream, Common::String &out) {
	const int16 size = stream.readSint16LE();

	if (size > 10000) {
		error("Textbox write action record has too many text box chars: %d", size);
	}

	if (size == -1) {
		Common::String stringID;
		readFilename(stream, stringID);

		const CVTX *autotext = (const CVTX *)g_nancy->getEngineData("AUTOTEXT");
		assert(autotext);

		out = autotext->texts.getValOrDefault(stringID, "");
	} else if (size > 0) {
		char *buf = new char[size];
		stream.read(buf, size);
		buf[size - 1] = '\0';

		assembleTextLine(buf, out, size);

		delete[] buf;
	}
}

void TextBoxWrite::readData(Common::SeekableReadStream &stream) {
	if (_writeType == kAutotextWrite) {
		// AR 81 prefixes the body with a wait header (runtime state at 0x00 is
		// always 0 in the data, so skip it)
		stream.skip(2);
		_waitMode = stream.readSint16LE();
		_soundChannel = stream.readUint16LE();
		_waitTimeMs = stream.readUint32LE();
	}

	readTextboxText(stream, _text);

	if (_writeType == kAutotextWrite) {
		// The original terminates the body with the "<e>" end-of-line hypertext tag
		_text += "<e>";
	}
}

void TextBoxWrite::execute() {
	if (_state == kBegin) {
		auto &tb = NancySceneState.getTextbox();
		tb.clear();
		if (!_text.empty()) {
			tb.addTextLine(_text);
		}
		tb.setVisible(true);

		if (_writeType == kTextBoxWrite) {
			// Plain TextBoxWrite completes immediately
			finishExecution();
			return;
		}

		if (_waitMode == kWaitForTimer) {
			_endTime = g_nancy->getTotalPlayTime() + _waitTimeMs;
		}

		_state = kRun;
		return;
	}

	// Nancy 11+ AR 81 waits for a sound or a timer before completing
	switch (_state) {
	case kRun:
		switch (_waitMode) {
		case kWaitForSound:
			if (!g_nancy->_sound->isSoundPlaying(_soundChannel)) {
				_state = kActionTrigger;
			}
			break;
		case kWaitForTimer:
			if (g_nancy->getTotalPlayTime() >= _endTime) {
				_state = kActionTrigger;
			}
			break;
		default:
			_state = kActionTrigger;
			break;
		}
		break;
	case kActionTrigger:
		finishExecution();
		break;
	default:
		break;
	}
}

void TextboxClear::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void TextboxClear::execute() {
	NancySceneState.getTextbox().clear();
	finishExecution();
}

void FrameTextBox::readData(Common::SeekableReadStream &stream) {
	readTextboxText(stream, _text);

	// The original appends the "<e>" end-of-line hypertext tag to the caption
	_text += "<e>";
}

void FrameTextBox::execute() {
	auto &tb = NancySceneState.getTextbox();
	tb.clear();
	if (!_text.empty())
		tb.addTextLine(_text);

	tb.setFullMode(_boxMode == kFullBox);
	finishExecution();
}

void ControlUIItems::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() >= kGameTypeNancy15) {
		_uiButton = stream.readByte();
		_characterIndex = stream.readByte();
	} else {
		_uiButton = stream.readUint16LE();
	}
	_autoOpenOrBadgeSound = stream.readByte();
	_flagB  = stream.readByte();
	_startScene = stream.readSint16LE();
	_endScene = stream.readSint16LE();
}

void ControlUIItems::execute() {
	// Value 1 auto-opens the popup selected by _uiButton. For the cell
	// phone, _startScene (when set) is the scene to jump to once it opens,
	// which places a call that starts a conversation there.
	if (_autoOpenOrBadgeSound == 1) {
		// Only one popup shows at a time. If the player left one open (e.g. the
		// inventory), close it before this AR auto-opens its target, so the
		// game-over incoming call doesn't stack the phone over another popup.
		NancySceneState.closeActivePopups();

		switch (_uiButton) {
		case kUITypeInventory:
			NancySceneState.getInventoryPopup().open();
			break;
		case kUITypeNotebook:
			NancySceneState.getNotebookPopup().open();
			break;
		case kUITypeCellphone: {
			UI::CellPhonePopup &phone = NancySceneState.getCellPhonePopup();

			if (_startScene != (int16)kNoScene) {
				SceneChangeDescription scene;
				scene.sceneID = _startScene;
				scene.frameID = 0;
				scene.verticalOffset = 0;
				// The destination scene's sound carries the conversation audio.
				scene.continueSceneSound = kLoadSceneSound;
				// Phone rings, picks up, and changeScenes into `scene`.
				phone.startIncomingCall(scene);
			} else {
				phone.open();
			}
			break;
		}
		default:
			break;
		}
	} else {
		// Otherwise this AR toggles whether the button is disabled while the
		// player is in scene range [_startScene, _endScene]. _flagB != 0 sets
		// the toggle (a _startScene of 9997 means "from scene 0", with the
		// range capped at 9997); _flagB == 0 clears it once a bound is 9999
		// (kNoScene). _flagB != 0 also sets the disabled button's rejection-sound
		// mode from _autoOpenOrBadgeSound (which clickSoundName line plays when
		// the button is clicked while its popup is unavailable).
		// Nancy15 keeps separate overrides per player character. Only the
		// active character's taskbar is modeled, so skip the others'.
		uint characterIndex = _characterIndex == kPlayerCharacterActive ?
			g_nancy->getPlayerCharacter() : _characterIndex;
		UI::Taskbar *taskbar = NancySceneState.getTaskbar();
		if (taskbar && characterIndex == g_nancy->getPlayerCharacter()) {
			if (_flagB != 0) {
				int16 start = _startScene;
				int16 end = _endScene;
				if (_startScene == 9997) {
					start = 0;
					end = 9997;
				}
				taskbar->setDisabledRange(_uiButton, start, end);
				taskbar->setClickSoundMode(_uiButton, _autoOpenOrBadgeSound);
			} else if (_startScene == (int16)kNoScene || _endScene == (int16)kNoScene) {
				taskbar->clearButtonOverride(_uiButton);
			}
		}
	}

	finishExecution();
}

void UIPopupPrepScene::readData(Common::SeekableReadStream &stream) {
	_uiType      = stream.readSint32LE();
	_signalValue = stream.readSint32LE();
}

void UIPopupPrepScene::execute() {
	// Terminates a UI prep scene chain: the entry-adding ARs in the prep
	// scene(s) have run, so signal the scene to restore the pre-open scene and
	// open the (now populated) popup. A no-op if no prep is currently running.
	NancySceneState.finishUIPrepScene();

	finishExecution();
}

void AddSearchLink::readData(Common::SeekableReadStream &stream) {
	_mode = stream.readSint16LE();

	readFilename(stream, _link.key);
	readFilename(stream, _link.value);

	_link.extra = stream.readSint16LE();
	_link.flag = stream.readSint16LE();
	_link.eventFlag = stream.readSint16LE();
}

void AddSearchLink::execute() {
	NancySceneState.getCellPhonePopup().addSearchLink(_mode, _link);

	// Cellphone taskbar badge: mode 0 = new email (sub-cat 1), mode != 0
	// = new web search topic (sub-cat 2).
	if (UI::Taskbar *taskbar = NancySceneState.getTaskbar()) {
		taskbar->setNotification(kTaskButtonCellphone, _mode == 0 ? 1 : 2);
	}

	finishExecution();
}

void SetCellPhoneBatteryAndSignal::readData(Common::SeekableReadStream &stream) {
	_mode = stream.readUint16LE();
}

void SetCellPhoneBatteryAndSignal::execute() {
	UI::CellPhonePopup &popup = NancySceneState.getCellPhonePopup();
	switch (_mode) {
	case 0: popup.setBatteryLow(false); break;
	case 1: popup.setBatteryLow(true);  break;
	case 2: popup.setNoSignal(false);   break;
	case 3: popup.setNoSignal(true);    break;
	default:
		warning("SetCellPhoneBatteryAndSignal: unknown mode %u", _mode);
		break;
	}
	finishExecution();
}

void ChangeCellPhoneInfo::readData(Common::SeekableReadStream &stream) {
	readContact(stream, _contact);
}

void ChangeCellPhoneInfo::execute() {
	NancySceneState.getCellPhonePopup().upsertContact(_contact);

	// Cellphone taskbar badge: a new/updated contact triggers sub-cat 0.
	if (UI::Taskbar *taskbar = NancySceneState.getTaskbar()) {
		taskbar->setNotification(kTaskButtonCellphone, 0);
	}

	finishExecution();
}

void CellPhonePopCellSceneFromStack::readData(Common::SeekableReadStream &stream) {
	_sceneChange.sceneID = stream.readUint16LE();
}

void CellPhonePopCellSceneFromStack::execute() {
	UI::CellPhonePopup &phone = NancySceneState.getCellPhonePopup();

	if (_sceneChange.sceneID != kNoScene) {
		NancySceneState.changeScene(_sceneChange);
	} else {
		// Restore the pre-call scene if one was saved. If there's no saved
		// scene (e.g. the conversation was entered without a phone call),
		// do nothing — popping the global scene stack here would clobber
		// closeup / inventory pushes that have nothing to do with the phone.
		SceneChangeDescription returnScene;
		if (phone.consumeReturnScene(returnScene))
			NancySceneState.changeScene(returnScene);
	}

	// Conversation is over. An incoming call closes the phone; a player-placed
	// call leaves it open at the welcome screen.
	phone.endCall();

	finishExecution();
}

void BumpPlayerClock::readData(Common::SeekableReadStream &stream) {
	_relative = stream.readByte();
	_hours = stream.readSint16LE();
	_minutes = stream.readSint16LE();
}

void BumpPlayerClock::execute() {
	NancySceneState.setPlayerTime((uint32)(_hours * 3600000 + _minutes * 60000), _relative);
	finishExecution();
}

void SaveContinueGame::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void SaveContinueGame::execute() {
	g_nancy->secondChance();
	_isDone = true;
}

void MakeScreenFile::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _filename);

	readRect(stream, _cropRect);

	// Image format selector, unused
	stream.skip(1);
}

void MakeScreenFile::execute() {
	Graphics::ManagedSurface screenshot;
	g_nancy->_graphics->screenshotScreen(screenshot);

	Common::Rect cropRect = _cropRect;

	if (cropRect.isValidRect()) {
		cropRect.clip(Common::Rect(screenshot.w, screenshot.h));

		if (!cropRect.isEmpty()) {
			Common::Path outName(_filename + ".png");
			Common::DumpFile outFile;

			if (outFile.open(outName)) {
				if (!Image::writePNG(outFile, screenshot.getSubArea(cropRect))) {
					warning("Could not write screen file %s", outName.toString().c_str());
				}

				outFile.finalize();
				outFile.close();
			} else {
				warning("Could not create screen file %s", outName.toString().c_str());
			}
		}
	}

	finishExecution();
}

void TurnOffMainRendering::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void TurnOnMainRendering::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

// Returns the Nancy 11+ software-timer slot at the given index, lazily creating
// the TimerData puzzle-data chunk. nullptr if the index is out of range.
static TimerData::Timer *getSoftwareTimer(int16 index) {
	if (index < 0 || (uint)index >= TimerData::kNumTimers) {
		return nullptr;
	}

	TimerData *timerData = (TimerData *)NancySceneState.getPuzzleData(TimerData::getTag());
	return timerData ? &timerData->timers[index] : nullptr;
}

// Nancy 12+: adds a trigger to a running timer, unless an identical one already
// exists (so re-running the record on scene re-entry doesn't stack duplicates)
// or the timer's kNumTriggers slots are full.
static void addTimerTrigger(TimerData::Timer &timer, TimerData::Trigger::Type type,
		uint32 durationMs, const SoundDescription &sound, const Common::Array<FlagDescription> &flags) {
	for (uint i = 0; i < timer.triggers.size(); ++i) {
		const TimerData::Trigger &t = timer.triggers[i];
		if (t.type != type || t.durationMs != durationMs ||
			t.sound.name != sound.name || t.sound.channelID != sound.channelID ||
			t.sound.volume != sound.volume) {
			continue;
		}

		bool flagsMatch = true;
		for (uint j = 0; j < ARRAYSIZE(t.flags); ++j) {
			FlagDescription incoming = j < flags.size() ? flags[j] : FlagDescription();
			if (t.flags[j].label != incoming.label || t.flags[j].flag != incoming.flag) {
				flagsMatch = false;
				break;
			}
		}

		if (flagsMatch) {
			return;
		}
	}

	if (timer.triggers.size() >= TimerData::kNumTriggers) {
		return;
	}

	TimerData::Trigger trigger;
	trigger.type = type;
	trigger.durationMs = durationMs;
	trigger.hasFired = false;
	trigger.sound = sound;
	for (uint i = 0; i < ARRAYSIZE(trigger.flags); ++i) {
		trigger.flags[i] = i < flags.size() ? flags[i] : FlagDescription();
	}

	timer.triggers.push_back(trigger);
}

// Nancy 12-13 add to, subtract from or set the elapsed time's hours, minutes and
// seconds separately (clamping each at 0 when subtracting), dropping any
// fraction of a second
static void adjustTimerComponents(TimerData::Timer &timer, int16 command, int16 hours, int16 minutes, int16 seconds) {
	int32 h = timer.currentTimeMs / 3600000;
	int32 m = (timer.currentTimeMs / 60000) % 60;
	int32 s = (timer.currentTimeMs / 1000) % 60;

	switch (command) {
	case ResetAndStartTimer::kAddTime:
		h += hours;
		m += minutes;
		s += seconds;
		break;
	case ResetAndStartTimer::kSubtractTime:
		h = MAX<int32>(h - hours, 0);
		m = MAX<int32>(m - minutes, 0);
		s = MAX<int32>(s - seconds, 0);
		break;
	case ResetAndStartTimer::kSetTime:
		h = hours;
		m = minutes;
		s = seconds;
		break;
	default:
		return;
	}

	timer.currentTimeMs = MAX<int32>((h * 60 + m) * 60 + s, 0) * 1000;
}

// Reads exactly size bytes from the stream, returning the text up to the first
// null byte. Used for the fixed-size string fields inside a TimerControl chunk.
static Common::String readFixedSizeString(Common::SeekableReadStream &stream, uint size) {
	Common::String result;
	bool ended = false;
	for (uint i = 0; i < size; ++i) {
		byte b = stream.readByte();
		if (b == 0) {
			ended = true;
		}
		if (!ended) {
			result += (char)b;
		}
	}

	return result;
}

// Nancy16 configure payload, after the slot/command header:
//   int16 hours, int16 minutes, int16 seconds, uint16 unused
//   SoundGroup: uint16 numNames, numNames x char[33],
//               uint16 channel, uint32 numLoops, uint16 volume
//   uint16 numFlags, numFlags x { int16 label, int16 value }
// The other commands have no payload.
void ResetAndStartTimer::readNancy16Data(Common::SeekableReadStream &stream) {
	if (_command != kConfigOneShot && _command != kConfigRepeating) {
		return;
	}

	_hours = stream.readSint16LE();      // 0x04
	_minutes = stream.readSint16LE();    // 0x06
	_seconds = stream.readSint16LE();    // 0x08
	stream.skip(2);                      // 0x0a, unused

	// An empty name pool means the timer expires silently
	const uint16 numNames = stream.readUint16LE();
	Common::Array<Common::String> names;
	for (uint i = 0; i < numNames; ++i) {
		Common::String name;
		readFilename(stream, name);
		if (!name.empty() && !name.equalsIgnoreCase("NO SOUND")) {
			names.push_back(name);
		}
	}

	_sound.channelID = stream.readUint16LE();
	_sound.numLoops = stream.readUint32LE();
	_sound.volume = stream.readUint16LE();
	_sound.name = names.empty() ? "NO SOUND" :
		names[g_nancy->_randomSource->getRandomNumber(names.size() - 1)];

	// Event flags fired when the configured duration elapses
	const uint16 numFlags = stream.readUint16LE();
	_flags.resize(numFlags);
	for (uint i = 0; i < numFlags; ++i) {
		_flags[i].label = stream.readSint16LE();
		_flags[i].flag = (byte)stream.readSint16LE();
	}
}

void ResetAndStartTimer::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() < kGameTypeNancy12) {
		_timerIndex = stream.readByte();
		return;
	}

	_timerIndex = stream.readSint16LE();    // 0x00
	_command = stream.readSint16LE();        // 0x02

	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		readNancy16Data(stream);
		return;
	}

	switch (_command) {
	case kAddTime:
	case kSubtractTime:
	case kSetTime:
		_hours = stream.readSint16LE();          // 0x04
		_minutes = stream.readSint16LE();        // 0x06
		_seconds = stream.readSint16LE();        // 0x08
		_milliseconds = stream.readSint16LE();   // 0x0a
		break;
	case kConfigOneShot:
	case kConfigRepeating: {
		_hours = stream.readSint16LE();          // 0x04
		_minutes = stream.readSint16LE();        // 0x06
		_seconds = stream.readSint16LE();        // 0x08
		_milliseconds = stream.readSint16LE();   // 0x0a
		_sound.volume = stream.readUint16LE();   // 0x0c
		_sound.channelID = stream.readUint16LE(); // 0x0e
		_sound.numLoops = 1;

		// Three candidate expiry-sound names (0x10, 33 bytes each); one is
		// picked at random, "NO SOUND" marks an empty slot
		Common::Array<Common::String> names;
		for (uint i = 0; i < 3; ++i) {
			Common::String name = readFixedSizeString(stream, 0x21);
			if (!name.empty() && !name.equalsIgnoreCase("NO SOUND")) {
				names.push_back(name);
			}
		}

		if (!names.empty()) {
			_sound.name = names[g_nancy->_randomSource->getRandomNumber(names.size() - 1)];
		}

		// Event flags fired on expiry (count at 0x73)
		uint16 numFlags = stream.readUint16LE();
		_flags.resize(numFlags);
		for (uint i = 0; i < numFlags; ++i) {
			_flags[i].label = stream.readSint16LE();
			_flags[i].flag = (byte)stream.readSint16LE();
		}
		break;
	}
	default:
		// kStart, kClear and kPause carry no further data
		break;
	}
}

void ResetAndStartTimer::execute() {
	if (g_nancy->getGameType() < kGameTypeNancy12) {
		NancySceneState.resetAndStartTimer();

		// Nancy 11 also resets and starts one of the software-timer slots
		if (g_nancy->getGameType() >= kGameTypeNancy11) {
			TimerData::Timer *timer = getSoftwareTimer(_timerIndex);
			if (timer) {
				timer->reset();
				timer->state = TimerData::Timer::kRunning;
			}
		}

		finishExecution();
		return;
	}

	// Nancy 12+ issues a command to one of the software-timer slots. Every
	// command other than kStart only takes effect while the slot is running.
	TimerData::Timer *timer = getSoftwareTimer(_timerIndex);
	debugC(1, kDebugActionRecord, "ControlTimer: slot %d command %d, %02d:%02d:%02d, sound '%s', %d flag(s)",
		_timerIndex, _command, _hours, _minutes, _seconds, _sound.name.c_str(), (int)_flags.size());

	if (timer) {
		if (_command == kStart) {
			timer->state = TimerData::Timer::kRunning;
		} else if (timer->state == TimerData::Timer::kRunning) {
			const bool isNancy14 = g_nancy->getGameType() >= kGameTypeNancy14;

			// Seconds from kTimerDurationIndexBase (Nancy14+: above it) index
			// the BSUM duration table
			int16 seconds = _seconds;
			if (isNancy14 ? seconds > kTimerDurationIndexBase : seconds >= kTimerDurationIndexBase) {
				auto *bootSummary = GetEngineData(BSUM);
				uint index = seconds - kTimerDurationIndexBase;
				if (bootSummary && index < bootSummary->timerDurations.size()) {
					seconds = bootSummary->timerDurations[index];
				}
			}

			// Only Nancy14+ uses the milliseconds field
			const uint32 durationMs = ((uint32)_hours * 3600 + (uint32)_minutes * 60 + (uint32)seconds) * 1000 +
				(isNancy14 ? _milliseconds : 0);

			switch (_command) {
			case kClear:
				timer->reset();
				break;
			case kPause:
				timer->state = TimerData::Timer::kPaused;
				break;
			case kAddTime:
				if (!isNancy14) {
					adjustTimerComponents(*timer, _command, _hours, _minutes, seconds);
				} else {
					timer->currentTimeMs += durationMs;
				}

				break;
			case kSubtractTime:
			case kSetTime:
				if (!isNancy14) {
					adjustTimerComponents(*timer, _command, _hours, _minutes, seconds);
				} else if (_command == kSubtractTime) {
					timer->currentTimeMs = timer->currentTimeMs > durationMs ? timer->currentTimeMs - durationMs : 0;
				} else {
					timer->currentTimeMs = durationMs;
				}

				// Reset triggers whose target time is now later than the
				// elapsed time, so they fire again once the timer reaches them
				for (TimerData::Trigger &trigger : timer->triggers) {
					if (timer->currentTimeMs < trigger.durationMs) {
						trigger.hasFired = false;
					}
				}

				break;
			case kConfigOneShot:
			case kConfigRepeating:
				addTimerTrigger(*timer,
					(_command == kConfigRepeating) ? TimerData::Trigger::kRepeating : TimerData::Trigger::kOneShot,
					durationMs, _sound, _flags);
				break;
			default:
				break;
			}
		}
	}

	// Repeating records (e.g. gradually refilling a timer) run again next frame
	finishExecution();
}

void StopTimer::readData(Common::SeekableReadStream &stream) {
	_timerIndex = stream.readByte();
}

void StopTimer::execute() {
	NancySceneState.stopTimer();

	if (g_nancy->getGameType() >= kGameTypeNancy11) {
		TimerData::Timer *timer = getSoftwareTimer(_timerIndex);
		if (timer) {
			timer->reset();
		}
	}

	finishExecution();
}

void TimerControl::readData(Common::SeekableReadStream &stream) {
	const int64 startPos = stream.pos();

	_timerIndex = stream.readSint16LE();    // 0x00
	_command = stream.readSint16LE();       // 0x02
	_hours = stream.readSint16LE();         // 0x04
	_minutes = stream.readSint16LE();       // 0x06
	_seconds = stream.readSint16LE();       // 0x08
	stream.skip(2);                         // 0x0a, unused

	// Sound to play on expiry (0x0c)
	_sound.readDIGI(stream);

	// Autotext key for the caption (0x3d, 33 bytes)
	stream.skip((startPos + 0x3d) - stream.pos());
	_autotextKey = readFixedSizeString(stream, 0x21);

	// Inline caption text (0x5e, 100 bytes)
	_caption = readFixedSizeString(stream, 0x64);

	// Event flags to fire on expiry (count at 0xc2, then count*4 bytes)
	uint16 numFlags = stream.readUint16LE();
	_flags.resize(numFlags);
	for (uint i = 0; i < numFlags; ++i) {
		_flags[i].label = stream.readSint16LE();
		_flags[i].flag = (byte)stream.readSint16LE();
	}
}

void TimerControl::execute() {
	TimerData::Timer *timer = getSoftwareTimer(_timerIndex);
	if (!timer) {
		finishExecution();
		return;
	}

	const uint32 durationMs = ((uint32)_hours * 3600 + (uint32)_minutes * 60 + (uint32)_seconds) * 1000;

	switch (_command) {
	case kReset:
		timer->reset();
		break;
	case kStart:
		timer->state = TimerData::Timer::kRunning;
		break;
	case kPause:
		timer->state = TimerData::Timer::kPaused;
		break;
	case kAddTime:
		if (timer->state != TimerData::Timer::kIdle) {
			timer->currentTimeMs += durationMs;
		}
		break;
	case kSubtractTime:
		if (timer->state != TimerData::Timer::kIdle) {
			timer->currentTimeMs = timer->currentTimeMs > durationMs ? timer->currentTimeMs - durationMs : 0;
		}
		break;
	case kConfigOneShot:
	case kConfigRepeating:
		// A timer can only be configured once it has been started
		if (timer->state == TimerData::Timer::kRunning) {
			timer->durationMs = durationMs;
			timer->hasFired = false;
			timer->sound = _sound;
			timer->autotextKey = _autotextKey;
			timer->caption = _caption;

			for (uint i = 0; i < ARRAYSIZE(timer->flags); ++i) {
				timer->flags[i] = i < _flags.size() ? _flags[i] : FlagDescription();
			}

			timer->state = _command;
		}
		break;
	default:
		break;
	}

	finishExecution();
}

void StopPlayerScrolling::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void StopPlayerScrolling::execute() {
	NancySceneState.setPlayerScrolling(false);
	_isDone = true;
}

void StartPlayerScrolling::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void StartPlayerScrolling::execute() {
	NancySceneState.setPlayerScrolling(true);
	_isDone = true;
}

void GotoMenu::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void GotoMenu::execute() {
	//NancySceneState.setDestroyOnExit();
	g_nancy->setState(NancyState::kMainMenu);

	_isDone = true;
}

void LoseGame::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void LoseGame::execute() {
	g_nancy->_sound->stopAndUnloadSceneSpecificSounds();
	NancySceneState.setDestroyOnExit();

	if (!ConfMan.hasKey("original_menus") || ConfMan.getBool("original_menus")) {
		g_nancy->setState(NancyState::kMainMenu);
	} else {
		Common::Event ev;
		ev.type = Common::EVENT_RETURN_TO_LAUNCHER;
		g_system->getEventManager()->pushEvent(ev);
	}

	_isDone = true;
}

void PushScene::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void PushScene::execute() {
	NancySceneState.pushScene();
	_isDone = true;
}

void PopScene::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void PopScene::execute() {
	NancySceneState.popScene();
	_isDone = true;
}

void WinGame::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void WinGame::execute() {
	// Set ConfMan value that will stay persistent across future playthroughs.
	// Default value in original is StillWorkingOnIt, but we just don't set it instead.
	ConfMan.set("PlayerWonTheGame", "AcedTheGame", ConfMan.getActiveDomainName());
	ConfMan.flushToDisk();

	g_nancy->_sound->stopAndUnloadSceneSpecificSounds();
	NancySceneState.setDestroyOnExit();
	g_nancy->setState(NancyState::kCredits, NancyState::kMainMenu);

	_isDone = true;
}

void HintSystem::readData(Common::SeekableReadStream &stream) {
	_characterID = stream.readByte();
	_genericSound.readNormal(stream);
}

void HintSystem::execute() {
	switch (_state) {
	case kBegin: {
		uint16 difficulty = NancySceneState.getDifficulty();
		selectHint();
		_genericSound.name = selectedHint->soundIDs[difficulty];

		NancySceneState.getTextbox().clear();
		NancySceneState.getTextbox().addTextLine(g_nancy->getStaticData().hintTexts[selectedHint->textID * 3 + difficulty]);

		g_nancy->_sound->loadSound(_genericSound);
		g_nancy->_sound->playSound(_genericSound);
		_state = kRun;
		break;
	}
	case kRun:
		if (!g_nancy->_sound->isSoundPlaying(_genericSound)) {
			g_nancy->_sound->stopSound(_genericSound);
			_state = kActionTrigger;
		} else {
			break;
		}

		// fall through
	case kActionTrigger:
		NancySceneState.useHint(_characterID, _hintID);
		NancySceneState.getTextbox().clear();

		NancySceneState.changeScene(selectedHint->sceneChange);

		_isDone = true;
		break;
	}
}

void HintSystem::selectHint() {
	if (NancySceneState.getHintsRemaining() == 0) {
		selectedHint = &g_nancy->getStaticData().hints[_characterID][0];
	}

	// Start from 1 since the first hint is always the "I give up" option
	for (uint i = 1; i < g_nancy->getStaticData().hints[_characterID].size(); ++i) {
		const auto &hint = g_nancy->getStaticData().hints[_characterID][i];

		bool isSatisfied = true;

		for (const auto &cond : hint.conditions) {
			switch (cond.type) {
			case (byte)StaticDataConditionType::kEvent :
				if (!NancySceneState.getEventFlag(cond.label, cond.flag)) {
					isSatisfied = false;
				}

				break;
			case (byte)StaticDataConditionType::kInventory :
				if (NancySceneState.hasItem(cond.label) != cond.flag) {
					isSatisfied = false;
				}

				break;
			case (byte)StaticDataConditionType::kDifficulty :
				if (	(NancySceneState.getDifficulty() != cond.label && cond.flag != 0) ||
						(NancySceneState.getDifficulty() == cond.label && cond.flag == 0) ) {
					isSatisfied = false;
				}

				break;
			}

			if (!isSatisfied) {
				break;
			}
		}

		if (isSatisfied) {
			selectedHint = &hint;
			break;
		}
	}
}

void ResourceUse::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() >= kGameTypeNancy15) {
		_characterIndex = stream.readByte();     // whose resources are changed
	}

	_resourceIndex = stream.readSint16LE();      // which UIRC resource to change
	_amount = stream.readSint16LE();             // value / delta, or a table index
	_mode = stream.readByte();                   // see ResourceUseMode
	_flag.label = stream.readSint16LE();         // event flag set on success
	_flag.flag = stream.readByte();

	readFilename(stream, _failSoundName);        // played when the change can't be applied
	readFilename(stream, _successSoundName);     // played when it is applied

	readRect(stream, _paymentHotspot);
	_useResourceCursor = stream.readByte();

	_sceneID = stream.readUint16LE();
	_continueSceneSound = stream.readUint16LE();

	_drawResourceOverlay = stream.readByte() != 0;
	_overlayDest.x = stream.readSint32LE();
	_overlayDest.y = stream.readSint32LE();

	_drawResourceValue = stream.readByte() != 0;
	_valueDest.x = stream.readSint32LE();
	_valueDest.y = stream.readSint32LE();
}

byte ResourceUse::getCharacterIndex() const {
	return _characterIndex == kPlayerCharacterActive ? (byte)g_nancy->getPlayerCharacter() : _characterIndex;
}

void ResourceUse::init() {
	Common::Rect screenBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(screenBounds.width(), screenBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	moveTo(screenBounds);

	const UIRC *uirc = GetEngineData(UIRC);
	const bool haveItem = uirc && _resourceIndex >= 0 && (uint)_resourceIndex < uirc->items.size();

	if (haveItem && _drawResourceOverlay) {
		const UIRC::ItemRecord &item = uirc->items[_resourceIndex];
		Graphics::ManagedSurface image;
		g_nancy->_resource->loadImage(item.overlayName, image);
		image.setTransparentColor(_drawSurface.getTransparentColor());

		Common::Rect src = item.rect;
		Common::Rect dest(_overlayDest.x, _overlayDest.y,
			_overlayDest.x + src.width(), _overlayDest.y + src.height());
		_drawSurface.blitFrom(image, src, dest);
	}

	if (haveItem && _drawResourceValue) {
		// Unlike the taskbar coin purse, the overlay prints the bare number:
		// no currency symbol and no decimal point, whatever the record says
		const UIRC::ItemRecord &item = uirc->items[_resourceIndex];
		const Font *font = g_nancy->_graphics->getFont(item.fontID);
		if (font) {
			const Common::String text =
				Common::String::format("%d", NancySceneState.getUIResource(_resourceIndex, getCharacterIndex()));

			// The record's y is the bottom row the glyphs are aligned on
			const int y = _valueDest.y - font->getFontHeight() + 1;
			font->drawString(&_drawSurface, text, _valueDest.x, y, screenBounds.width() - _valueDest.x, 0);
		}
	}

	setVisible(_drawResourceOverlay || _drawResourceValue);
	registerGraphics();
}

void ResourceUse::applyChange() {
	const byte characterIndex = getCharacterIndex();

	if (_mode > kSetTableValue) {
		warning("Unknown ResourceUse mode %u, treating it as an addition", _mode);
	}

	// In the table modes the amount is a table index, not the amount itself.
	int32 amount = _amount;
	if (_mode == kAddTableValue || _mode == kSetTableValue) {
		auto *tableData = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
		amount = (tableData && _amount != tableData->getNoIndex()) ? tableData->getValue(_amount) : 0;
	}

	if (_mode == kSetValue || _mode == kSetTableValue) {
		// Set the resource outright.
		NancySceneState.setUIResource(_resourceIndex, amount, characterIndex);
		NancySceneState.setEventFlag(_flag);
		_paymentApplied = true;
	} else {
		// Add the (signed) amount, but never let the resource go negative —
		// the original skips the change (e.g. when Nancy can't afford it).
		const int32 result = NancySceneState.getUIResource(_resourceIndex, characterIndex) + amount;
		_paymentApplied = result >= 0;
		if (_paymentApplied) {
			NancySceneState.setUIResource(_resourceIndex, result, characterIndex);
			NancySceneState.setEventFlag(_flag);
		}
	}

	// Start the sound matching the outcome, if there is one.
	const Common::String &soundName = _paymentApplied ? _successSoundName : _failSoundName;
	if (!soundName.empty() && !soundName.equalsIgnoreCase("NO SOUND") && !soundName.equalsIgnoreCase("NO SOUND PLAY")) {
		_sound.name = soundName;
		_sound.numLoops = 1;

		const UIRC *uirc = GetEngineData(UIRC);
		if (uirc && _resourceIndex >= 0 && (uint)_resourceIndex < uirc->items.size()) {
			_sound.channelID = uirc->items[_resourceIndex].soundChannel;
			_sound.volume = uirc->items[_resourceIndex].soundVolume;
		}

		g_nancy->_sound->loadSound(_sound);
		g_nancy->_sound->playSound(_sound);
		_hasSound = true;
	}

	_paymentResolved = true;
}

void ResourceUse::handleInput(NancyInput &input) {
	if (!_interactive || _state != kRun || _paymentResolved) {
		return;
	}

	if (NancySceneState.getViewport().convertViewportToScreen(_paymentHotspot).contains(input.mousePos)) {
		g_nancy->_cursor->setCursorType(_useResourceCursor ?
			(CursorManager::CursorType)(_resourceIndex + 0x1f) : CursorManager::kNormal, true);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			applyChange();
		}
	}
}

void ResourceUse::execute() {
	switch (_state) {
	case kBegin:
		init();

		// A non-degenerate hotspot means the player has to click it (e.g. a coin
		// slot) to trigger the change; otherwise it happens immediately.
		_interactive = _paymentHotspot.top != _paymentHotspot.bottom;
		if (!_interactive) {
			applyChange();
		}

		_state = kRun;
		break;
	case kRun:
		// Interactive changes wait for the player to click the hotspot.
		if (_interactive && !_paymentResolved) {
			return;
		}

		// Keep the overlay up until the outcome sound has finished.
		if (_hasSound && g_nancy->_sound->isSoundPlaying(_sound)) {
			return;
		}

		_state = kActionTrigger;
		break;
	case kActionTrigger:
		if (_hasSound) {
			g_nancy->_sound->stopSound(_sound);
		}

		setVisible(false);

		// Only a successful change advances the scene.
		if (_paymentApplied && _sceneID != kNoScene) {
			SceneChangeDescription sceneChange;
			sceneChange.sceneID = _sceneID;
			sceneChange.continueSceneSound = _continueSceneSound;
			NancySceneState.changeScene(sceneChange);
		}

		finishExecution();
		break;
	}
}

void PlayChar::readData(Common::SeekableReadStream &stream) {
	_characterIndex = stream.readByte();
	readFilename(stream, _videoFile);
}

void PlayChar::execute() {
	const PCUI *pcui = GetEngineData(PCUI);
	if (!pcui || _characterIndex >= pcui->characters.size()) {
		warning("PlayChar: no player character %u", _characterIndex);
		finishExecution();
		return;
	}

	// Every character owns an event flag that marks them as the one being
	// played; conditions elsewhere in the game branch on those
	for (uint i = 0; i < pcui->characters.size(); ++i) {
		const uint16 flagLabel = pcui->characters[i].id;
		if (flagLabel != 0) {
			NancySceneState.setEventFlag(flagLabel, i == _characterIndex ? g_nancy->_true : g_nancy->_false);
		}
	}

	NancySceneState.changePlayerCharacter(_characterIndex);

	auto *playerChar = (PlayerCharacterData *)NancySceneState.getPuzzleData(PlayerCharacterData::getTag());
	if (playerChar) {
		playerChar->characterIndex = _characterIndex;
	}

	// The scene itself doesn't change; only the video showing it does, so that
	// the location is seen through the incoming character's eyes
	if (!_videoFile.empty()) {
		NancySceneState.changeSceneVideo(_videoFile);
	}

	finishExecution();
}

void NDUIControl::readData(Common::SeekableReadStream &stream) {
	_commandID = stream.readUint32LE();
	readFilename(stream, _target);
	stream.skip(41);	// always zero
}

void NDUIControl::execute() {
	NancySceneState.applyNDUICommand(_target, _commandID);
	_isDone = true;
}

void SaveNamedGame::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _slotName);
}

void SaveNamedGame::execute() {
	g_nancy->saveNamedGame(_slotName);
	_isDone = true;
}

void LoadNamedGame::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _slotName);
}

void LoadNamedGame::execute() {
	// The load is only queued, so mark the record done to avoid re-requesting it
	// every frame
	_isDone = true;
	g_nancy->requestNamedLoad(_slotName);
}

void ClearNamedGame::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _slotName);
}

void ClearNamedGame::execute() {
	g_nancy->deleteNamedGame(_slotName);
	_isDone = true;
}

void ChangePlayerCharacter::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _characterName);
}

void ChangePlayerCharacter::execute() {
	// The name should be one of the characters listed in PCUI
	auto *pcui = (const PCUI *)g_nancy->getEngineData("PCUI");
	bool known = false;
	if (pcui) {
		for (const PCUI::Character &chr : pcui->characters) {
			if (chr.name.equalsIgnoreCase(_characterName)) {
				known = true;
				break;
			}
		}
	}

	if (!known && pcui) {
		warning("Change player character to '%s', which the PCUI chunk does not list",
			_characterName.c_str());
	} else {
		debugC(kDebugActionRecord, "Change player character to '%s'", _characterName.c_str());
	}

	_isDone = true;
}

void SceneChangeWithFlags::readData(Common::SeekableReadStream &stream) {
	_sceneChange.readData(stream);
	_flags.readDataCounted(stream);
}

void SceneChangeWithFlags::execute() {
	_flags.execute();
	NancySceneState.changeScene(_sceneChange);
	_isDone = true;
}

void TypewriterText::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _textKey);
	readRect(stream, _dest);

	_charTime = stream.readDoubleLE();

	_fontID = stream.readUint32LE();
	_colourID = stream.readUint32LE();

	_keySounds.readData(stream);

	_doneFlag.label = stream.readSint16LE();
	_doneFlag.flag = stream.readByte();
}

Common::String TypewriterText::getRecordExtraInfo() const {
	return Common::String::format("key %s, box (%d, %d)-(%d, %d), %g ms/char, font %u, colour %u, %u key sounds on channel %d, flag %d = %d",
		_textKey.c_str(), _dest.left, _dest.top, _dest.right, _dest.bottom,
		_charTime, _fontID, _colourID, _keySounds.names.size(), _keySounds.channel,
		_doneFlag.label, _doneFlag.flag);
}

// Number of characters to type, not counting markup codes
static uint countTypedChars(const Common::String &text) {
	uint count = 0;

	for (uint i = 0; i < text.size(); ++i) {
		char letter = 0;
		int number = -1;
		uint end = 0;

		if (parseMarkupCode(text, i, letter, number, end)) {
			i = end;
			continue;
		}

		++count;
	}

	return count;
}

// The first `numChars` typed characters of `text`, keeping any markup codes
// inside that prefix
static Common::String typedPrefix(const Common::String &text, uint numChars) {
	Common::String out;
	uint count = 0;

	for (uint i = 0; i < text.size(); ++i) {
		char letter = 0;
		int number = -1;
		uint end = 0;

		if (parseMarkupCode(text, i, letter, number, end)) {
			out += Common::String(text.c_str() + i, end - i + 1);
			i = end;
			continue;
		}

		if (count >= numChars) {
			break;
		}

		out += text[i];
		++count;
	}

	return out;
}

void TypewriterText::init() {
	_clipped = _dest;
	_clipped.clip(NancySceneState.getViewport().getBounds());

	// A missing key draws nothing and completes at once
	_text = resolveSubtitleText(_textKey, Common::String(), "AUTOTEXT");
	_numChars = countTypedChars(_text);

	if (_clipped.isEmpty() || !_numChars) {
		_initialised = true;
		return;
	}

	_drawSurface.create(_clipped.width(), _clipped.height(), g_nancy->_graphics->getInputPixelFormat());
	moveTo(_clipped);
	setTransparent(true);
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	_initialised = true;
	setVisible(true);
	redraw();

	RenderObject::init();
}

void TypewriterText::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	_needsRedraw = true;

	if (!_shown) {
		return;
	}

	// Lay the text out relative to the unclipped box
	drawStyledTextToSurface(_drawSurface, typedPrefix(_text, _shown), (int)_fontID, (int)_colourID,
		_dest.left - _clipped.left, _dest.top - _clipped.top, MAX<int>(1, _dest.width()));
}

// Plays a random key click. Clicks are longer than the per-character interval,
// so a new one only starts once the channel is free.
void TypewriterText::playKeyClick() {
	if (_keySounds.names.empty() || g_nancy->_sound->isSoundPlaying((uint16)_keySounds.channel)) {
		return;
	}

	const uint idx = _keySounds.names.size() == 1 ? 0 :
		g_nancy->_randomSource->getRandomNumber(_keySounds.names.size() - 1);
	const Common::String &name = _keySounds.names[idx];
	if (name.empty() || name == "NO SOUND") {
		return;
	}

	SoundDescription desc;
	desc.name = name;
	desc.channelID = _keySounds.channel;
	desc.numLoops = 1;
	desc.volume = _keySounds.volume;

	g_nancy->_sound->loadSound(desc);
	g_nancy->_sound->playSound(desc);
}

void TypewriterText::raiseDoneFlag() {
	if (!_typingDone) {
		_typingDone = true;
		NancySceneState.setEventFlag(_doneFlag);
	}
}

void TypewriterText::updateGraphics() {
	if (!_initialised || _typingDone) {
		return;
	}

	if (!_numChars) {
		raiseDoneFlag();
		return;
	}

	const uint32 now = g_nancy->getTotalPlayTime();
	const double perChar = _charTime > 0.0 ? _charTime : 1.0;
	uint target = (uint)((double)(now - _startTime) / perChar);
	target = MIN(target, _numChars);

	if (target > _shown) {
		_shown = target;
		playKeyClick();
		redraw();
	}

	if (_shown >= _numChars) {
		raiseDoneFlag();
	}
}

void TypewriterText::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_startTime = g_nancy->getTotalPlayTime();
		_state = kRun;
		// fall through
	case kRun:
		break;
	case kActionTrigger:
		finishExecution();
		break;
	}
}

void DetectStreamSceneChange::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _streamName);
	_flag.label = stream.readSint16LE();
	_flag.flag = stream.readByte();
}

Common::String DetectStreamSceneChange::getRecordExtraInfo() const {
	return Common::String::format("watching '%s', raises flag %d = %d",
		_streamName.c_str(), _flag.label, _flag.flag);
}

void DetectStreamSceneChange::execute() {
	StreamManager &streams = NancySceneState.getStreams();

	switch (_state) {
	case kBegin:
		// Only scene changes after this point count. Comparing counters lets
		// several watchers observe the same stream.
		_watchedCountAtStart = streams.getWatchBaseline(_streamName);
		_state = kRun;
		// fall through
	case kRun:
		if (streams.getSceneChangeCount(_streamName) != _watchedCountAtStart) {
			NancySceneState.setEventFlag(_flag);
			finishExecution();
		}

		break;
	case kActionTrigger:
		finishExecution();
		break;
	}
}

void EndStream::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _streamName);
}

Common::String EndStream::getRecordExtraInfo() const {
	return _streamName.empty() ? "ends the stream this script is" :
		Common::String::format("ends stream '%s'", _streamName.c_str());
}

void EndStream::execute() {
	if (!NancySceneState.getStreams().end(_streamName) && _streamName.empty()) {
		// No stream to end, so this closes an inventory item close-up.
		// Nancy16 has no PopInvViewPriorScene records and uses this instead.
		NancySceneState.popPushedScene();
	}

	_isDone = true;
}

// Blocks shared by the Nancy16 puzzle records:
//   SoundGroup : uint16 count, count x char[33] name pool, uint16 channel,
//                uint32 (always 1), uint16 volume        = 2 + 33n + 8
//   Tail25     : uint16, RECT exit hotspot, uint16 cursor, uint16 exit scene,
//                uint16 flag label, byte flag value      = 25, always last
Common::String Nancy16ConsumeOnly::getRecordTypeName() const {
	switch (_shape) {
	case kPaperDollPuzzle:		return "PaperDollPuzzle";
	case kFixedSize:		return "Nancy16FixedSize";
	case kCountedTail:		return "Nancy16CountedTail";
	case kRandomMovieGraph:		return "RandomMovieGraph";
	case kRolloverOverlay:		return "RolloverOverlay";
	case kStaticOverlayImage:	return "StaticOverlayImage";
	default:			return "SceneChangeTheWorks";
	}
}

// Skips a type 53 sound block: an event flag, a list of sound names, then the
// shared sound settings if the list is not empty.
static void readRolloverSoundBlock(Common::SeekableReadStream &stream) {
	stream.skip(2); // Event flag label, -1 for none
	stream.skip(1); // Event flag value

	const uint16 numNames = stream.readUint16LE();
	for (uint i = 0; i < numNames; ++i) {
		Common::String name;
		readFilename(stream, name);
	}

	if (numNames) {
		stream.skip(2); // Channel
		stream.skip(4); // Loop count
		stream.skip(2); // Volume
	}
}

void Nancy16ConsumeOnly::readData(Common::SeekableReadStream &stream) {
	switch (_shape) {
	case kSceneChangeTheWorks: {
		const int64 start = stream.pos();
		stream.seek(start + 49);
		const uint16 count = stream.readUint16LE();
		stream.seek(start + 69);

		// The field counts the inline entry too, so only count - 1 follow.
		if (count > 1) {
			stream.skip((count - 1) * 18);
		}

		break;
	}
	case kFixedSize:
		stream.skip(_size);
		break;
	case kPaperDollPuzzle: {
		// Type 166: a 256-byte header, a counted array of 143-byte garment
		// entries, then a 1132-byte tail with three SoundGroups and the
		// placement table.
		stream.seek(256, SEEK_CUR);
		const uint16 numGarments = stream.readUint16LE();
		stream.skip(143 * numGarments + 1132);
		break;
	}
	case kCountedTail: {
		// 21 bytes then a byte count at offset 19 giving 2-byte entries.
		const int64 start = stream.pos();
		stream.seek(start + 19);
		const byte count = stream.readByte();
		stream.seek(start + 21);
		stream.skip(count * 2);
		break;
	}
	case kRandomMovieGraph: {
		// Types 42 and 43, "Random Movie/Fidget". Each clip lists the clips
		// that may follow it, with weights summing to 100.
		//   char[33] unused, always empty
		//   char[33] name of the set ("random_movie", or the only clip in it)
		//   uint16   draw order; uint16 unknown; int16 unknown; int16 unknown
		//   uint32   scene to change to, 32767 for none
		//   byte     event flag value
		//   uint16   number of clip blocks
		//   per block:
		//     char[33] clip name
		//     int16 x3, then uint32 min and uint32 max delay in milliseconds
		//     byte     unknown
		//     uint16   number of successors, then per successor a char[33]
		//              clip name and an int16 weight
		//   uint16   number of frame entries, then per entry a uint16 frame ID
		//            and two rects (source in the video, destination on screen)
		stream.skip(33);
		readFilename(stream, _name);
		stream.skip(2 + 2 + 2 + 2 + 4 + 1);

		const uint16 numBlocks = stream.readUint16LE();
		for (uint i = 0; i < numBlocks; ++i) {
			stream.skip(33 + 6 + 4 + 4 + 1);
			const uint16 numSuccessors = stream.readUint16LE();
			stream.skip(numSuccessors * 35);
		}

		const uint16 numFrames = stream.readUint16LE();
		stream.skip(numFrames * 34);
		break;
	}
	case kRolloverOverlay: {
		// Type 53, "rollover overlay": an image shown while the cursor is over
		// a hotspot, with a sound and event flag for hover and for click.
		//   char[33] image name, not a video
		//   uint16 x3 draw order and two unknowns
		//   rect     screen bounds
		//   rect     source rect inside the image
		//   rect     destination rect, so far always equal to the bounds
		//   sound block (see readRolloverSoundBlock) for the hover
		//   byte     unknown
		//   uint32   scene to change to on click, 32767 for none
		//   sound block for the click
		readFilename(stream, _name);
		stream.skip(6);
		stream.skip(48);
		readRolloverSoundBlock(stream);
		stream.skip(1);
		stream.skip(4);
		readRolloverSoundBlock(stream);
		break;
	}
	case kStaticOverlayImage: {
		// Type 54, "Static Overlay Image"
		//   char[33] image name
		//   uint16   draw order
		//   uint16   count, then count entries of a uint32 colour and an int16
		//            event flag label
		//   uint32   unknown, always 1
		//   rect     source rect inside the image
		//   rect     destination rect on screen
		readFilename(stream, _name);
		stream.skip(2);

		const uint16 count = stream.readUint16LE();
		stream.skip(count * 6);

		stream.skip(4);
		stream.skip(32);
		break;
	}
	default:
		break;
	}
}

void Nancy16ConsumeOnly::execute() {
	_isDone = true;
}

} // End of namespace Action
} // End of namespace Nancy
