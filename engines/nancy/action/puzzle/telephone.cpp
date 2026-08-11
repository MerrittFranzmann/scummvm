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
#include "engines/nancy/graphics.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/input.h"
#include "engines/nancy/util.h"
#include "engines/nancy/font.h"
#include "engines/nancy/cursor.h"

#include "engines/nancy/action/puzzle/telephone.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

void Telephone::init() {
	Common::Rect screenBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(screenBounds.width(), screenBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(screenBounds);

	g_nancy->_resource->loadImage(_imageName, _image);
	g_nancy->_resource->loadImage(_displayAnimName, _animImage);

	if (_isNewPhone) {
		_font = g_nancy->_graphics->getFont(_displayFont);
	}

	// Set the phone tutorial flag to false for Nancy9, so that
	// the actual phone interface is available after the tutorial.
	// TODO: Is this the right place to set this flag?
	if (g_nancy->getGameType() == kGameTypeNancy9) {
		NancySceneState.setEventFlag(592, g_nancy->_false);
	}
}

void Telephone::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);

	uint16 numButtons = 12;
	uint16 maxNumButtons = _isNewPhone ? 20 : 12;

	if (_isNewPhone) {
		_hasDisplay = stream.readByte();
		_displayFont = stream.readUint16LE();
		readFilename(stream, _displayAnimName);
		_displayAnimFrameTime = stream.readUint32LE();
		uint16 numFrames = stream.readUint16LE();
		readRectArray(stream, _displaySrcs, numFrames, 10);
		readRect(stream, _displayDest);
		_dialAutomatically = stream.readByte();

		numButtons = stream.readUint16LE();
	}

	readRectArray(stream, _srcRects, numButtons, maxNumButtons);
	readRectArray(stream, _destRects, numButtons, maxNumButtons);

	if (_isNewPhone) {
		readRect(stream, _dirHighlightSrc);
		readRect(stream, _dialHighlightSrc);

		_upDirButtonID = stream.readUint16LE();
		_downDirButtonID = stream.readUint16LE();
		_dialButtonID = stream.readUint16LE();
		_dirButtonID = stream.readUint16LE();

		readRect(stream, _displayDialingSrc);
	}

	if (!_isNewPhone) {
		_genericDialogueSound.readNormal(stream);
		_genericButtonSound.readNormal(stream);
		_ringSound.readNormal(stream);
		_dialToneSound.readNormal(stream);
		_dialAgainSound.readNormal(stream);
		_hangUpSound.readNormal(stream);
	} else {
		_ringSound.readNormal(stream);
		_dialToneSound.readNormal(stream);
		_preCallSound.readNormal(stream);
		_hangUpSound.readNormal(stream);
		_genericButtonSound.readNormal(stream);
	}

	readFilenameArray(stream, _buttonSoundNames, numButtons);
	stream.skip(33 * (maxNumButtons - numButtons));

	char textBuf[200];
	if (!_isNewPhone) {
		stream.read(textBuf, 200);
		textBuf[199] = '\0';
		_addressBookString = textBuf;
	} else {
		_dialAgainSound.readNormal(stream);
	}

	stream.read(textBuf, 200);
	textBuf[199] = '\0';
	_dialAgainString = textBuf;

	_reloadScene.readData(stream);
	stream.skip(1);
	_exitScene.readData(stream);
	stream.skip(1);
	readRect(stream, _exitHotspot);

	uint numCalls = stream.readUint16LE();

	_calls.resize(numCalls);
	for (uint i = 0; i < numCalls; ++i) {
		PhoneCall &call = _calls[i];

		if (_isNewPhone) {
			call.directoryDisplayCondition = stream.readSint16LE();
		}

		call.phoneNumber.resize(11);
		for (uint j = 0; j < 11; ++j) {
			call.phoneNumber[j] = stream.readByte();
		}

		if (!_isNewPhone) {
			readFilename(stream, call.soundName);
			stream.read(textBuf, 200);
			textBuf[199] = '\0';
			call.text = textBuf;
		} else {
			readRect(stream, call.displaySrc);
		}

		call.sceneChange.readData(stream);
		stream.skip(1);
	}
}

void Telephone::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		g_nancy->_sound->loadSound(_dialToneSound);
		g_nancy->_sound->playSound(_dialToneSound);
		NancySceneState.getTextbox().clear();
		NancySceneState.getTextbox().addTextLine(_addressBookString);
		_state = kRun;
		// fall through
	case kRun:
		switch (_callState) {
		case kWaiting:
			if (_isNewPhone && !_animIsStopped) {
				if (g_nancy->getTotalPlayTime() > _displayAnimEnd) {
					if (_displayAnimEnd == 0) {
						_displayAnimEnd = g_nancy->getTotalPlayTime() + _displayAnimFrameTime;
					} else {
						_displayAnimEnd += _displayAnimFrameTime;
					}

					_drawSurface.blitFrom(_animImage, _displaySrcs[_displayAnimFrame], _displayDest);
					_needsRedraw = true;
					++_displayAnimFrame;

					if (_displayAnimFrame >= _displaySrcs.size()) {
						_displayAnimFrame = 0;
					}
				}
			}

			if (_checkNumbers) {
				// Pressed a new button, check all numbers for match
				// We do this before going to the ringing state to support nancy4's voice mail system,
				// where call numbers can be 1 digit long
				for (uint i = 0; i < _calls.size(); ++i) {
					auto &call = _calls[i];
					bool invalid = false;

					for (uint j = 0; j < _calledNumber.size(); ++j) {
						if (_calledNumber[j] != call.phoneNumber[j]) {
							// Invalid number, move onto next
							invalid = true;
							break;
						}
					}

					// We do not want to check for a terminator if the dialed number is of
					// appropriate size (7 digits, or 11 when the number starts with '1')
					bool checkNextDigit = true;
					if (_calledNumber.size() >= 11 || (_calledNumber.size() >= 7 && (_calledNumber[0] != 1))) {
						checkNextDigit = false;
					}

					if (!invalid && checkNextDigit) {
						// Check if the next digit in the phone number is '10' (star). Presumably, that will never
						// be contained in a valid phone number
						if (_calls[i].phoneNumber[_calledNumber.size()] != 10) {
							invalid = true;
						}
					}

					if (!invalid) {
						_selected = i;
						break;
					}
				}

				bool shouldRing = false;

				if (_selected == -1) {
					// Did not find a suitable match, check if the dialed number is above allowed size
					if (_calledNumber.size() >= 11 || (_calledNumber.size() >= 7 && (_calledNumber[0] != 1))) {
						shouldRing = true;
					}
				} else {
					shouldRing = true;
				}

				if (shouldRing) {
					if (_ringSound.name != "NO SOUND") {
						if (_hasDisplay) {
							_drawSurface.blitFrom(_image, _displayDialingSrc, _displayDest);
							_needsRedraw = true;
						} else {
							NancySceneState.getTextbox().clear();
							NancySceneState.getTextbox().addTextLine(g_nancy->getStaticData().ringingText);
						}

						g_nancy->_sound->loadSound(_ringSound);
						g_nancy->_sound->playSound(_ringSound);
					}

					_callState = kRinging;
				}

				_checkNumbers = false;
			}

			break;
		case kButtonPress:
			if (!g_nancy->_sound->isSoundPlaying(_genericButtonSound)) {
				g_nancy->_sound->stopSound(_genericButtonSound);
				_drawSurface.fillRect(_destRects[_buttonLastPushed], g_nancy->_graphics->getTransColor());
				_needsRedraw = true;

				if (_isShowingDirectory) {
					_drawSurface.blitFrom(_image, _dirHighlightSrc, _destRects[_dirButtonID]);
					_drawSurface.blitFrom(_image, _calls[_displayedDirectory].displaySrc, _displayDest);
				} else if (_dirButtonID != -1) {
					_drawSurface.fillRect(_destRects[_dirButtonID], _drawSurface.getTransparentColor());
				}

				_buttonLastPushed = -1;
				_callState = kWaiting;
			}

			break;
		case kRinging:
			if (!g_nancy->_sound->isSoundPlaying(_ringSound)) {
				g_nancy->_sound->stopSound(_ringSound);

				if (_selected != -1) {
					// Called a valid number

					if (_preCallSound.name == "NO SOUND") {
						// Old phone, go directly to call
						NancySceneState.getTextbox().clear();
						NancySceneState.getTextbox().addTextLine(_calls[_selected].text);

						_genericDialogueSound.name = _calls[_selected].soundName;
						g_nancy->_sound->loadSound(_genericDialogueSound);
						g_nancy->_sound->playSound(_genericDialogueSound);
						_callState = kCall;
					} else {
						// New phone, play a short sound of phone being picked up
						g_nancy->_sound->loadSound(_preCallSound);
						g_nancy->_sound->playSound(_preCallSound);
						_callState = kPreCall;
					}
				} else {
					// Called an invalid number
					NancySceneState.getTextbox().clear();
					NancySceneState.getTextbox().addTextLine(_dialAgainString);

					if (_hasDisplay) {
						_drawSurface.fillRect(_displayDest, _drawSurface.getTransparentColor());
						_needsRedraw = true;
					}

					if (_dialButtonID != -1) {
						_drawSurface.fillRect(_destRects[_dialButtonID], _drawSurface.getTransparentColor());
						_needsRedraw = true;
					}

					_calledNumber.clear();

					g_nancy->_sound->loadSound(_dialAgainSound);
					g_nancy->_sound->playSound(_dialAgainSound);
					_callState = kBadNumber;
				}

				return;
			}

			break;
		case kPreCall:
			if (!g_nancy->_sound->isSoundPlaying(_preCallSound)) {
				g_nancy->_sound->stopSound(_preCallSound);

				if (!_calls[_selected].text.empty()) {
					NancySceneState.getTextbox().clear();
					NancySceneState.getTextbox().addTextLine(_calls[_selected].text);
				}

				_genericDialogueSound.name = _calls[_selected].soundName;
				g_nancy->_sound->loadSound(_genericDialogueSound);
				g_nancy->_sound->playSound(_genericDialogueSound);
				_callState = kCall;
			}

			break;
		case kBadNumber:
			if (!g_nancy->_sound->isSoundPlaying(_dialAgainSound)) {
				_state = kActionTrigger;
			}

			break;
		case kCall:
			if (!g_nancy->_sound->isSoundPlaying(_genericDialogueSound)) {
				_state = kActionTrigger;
			}

			break;
		case kHangUp:
			if (!g_nancy->_sound->isSoundPlaying(_hangUpSound)) {
				_state = kActionTrigger;
			}

			break;
		}

		break;
	case kActionTrigger:
		switch (_callState) {
		case kBadNumber:
			_reloadScene.execute();
			_calledNumber.clear();
			_state = kRun;
			_callState = kWaiting;

			break;
		case kCall: {
			PhoneCall &call = _calls[_selected];

			// Make sure we don't get stuck here. Happens in nancy3 when calling George's number
			// Check ignored in nancy1 since the HintSystem AR is in the same scene as the Telephone
			if (call.sceneChange._sceneChange.sceneID == kNoScene && g_nancy->getGameType() != kGameTypeNancy1) {
				call.sceneChange._sceneChange = NancySceneState.getSceneInfo();
			}

			call.sceneChange.execute();

			break;
		}
		case kHangUp:
			_exitScene.execute();

			break;
		default:
			break;
		}

		g_nancy->_sound->stopSound(_hangUpSound);
		g_nancy->_sound->stopSound(_genericDialogueSound);
		g_nancy->_sound->stopSound(_genericButtonSound);
		g_nancy->_sound->stopSound(_dialAgainSound);
		g_nancy->_sound->stopSound(_ringSound);
		g_nancy->_sound->stopSound(_dialToneSound);

		finishExecution();
	}
}

void Telephone::handleInput(NancyInput &input) {
	int buttonNr = -1;
	// Cursor gets changed regardless of state
	for (int i = 0; i < (int)_destRects.size(); ++i) {
		// Dial button is an exception
		if (i == _dialButtonID && !_calledNumber.size() && !_isShowingDirectory) {
			continue;
		}

		if (NancySceneState.getViewport().convertViewportToScreen(_destRects[i]).contains(input.mousePos)) {
			g_nancy->_cursor->setCursorType(CursorManager::kHotspot);
			buttonNr = i;
			break;
		}
	}

	if (_callState != kWaiting && _callState != kRinging) {
		return;
	}

	if (NancySceneState.getViewport().convertViewportToScreen(_exitHotspot).contains(input.mousePos)) {
		g_nancy->_cursor->setCursorType(g_nancy->_cursor->_puzzleExitCursor);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			g_nancy->_sound->loadSound(_hangUpSound);
			g_nancy->_sound->playSound(_hangUpSound);

			_callState = kHangUp;
		}

		return;
	}

	if (_callState != kWaiting) {
		return;
	}

	if (buttonNr != -1) {
		if (input.input & NancyInput::kLeftMouseButtonUp) {
			if (g_nancy->_sound->isSoundPlaying(_dialToneSound)) {
				g_nancy->_sound->stopSound(_dialToneSound);
			}

			// Handle non-digit numbers
			bool directorySwitch = false;
			bool changeDirectoryEntry = false;
			int dirEntryDelta = 1;
			if (_dialButtonID != -1 && buttonNr == _dialButtonID) {
				if (_isShowingDirectory) {
					_calledNumber = _calls[_displayedDirectory].phoneNumber;
					while (_calledNumber.back() == 10) {
						_calledNumber.pop_back();
					}
				}

				_checkNumbers = true;

				// Dial button doesn't make sound, and doesn't get pressed down
				_drawSurface.blitFrom(_image, _dialHighlightSrc, _destRects[_dialButtonID]);

				if (_dirButtonID != -1) {
					_drawSurface.fillRect(_destRects[_dirButtonID], _drawSurface.getTransparentColor());
				}

				_animIsStopped = true;
				return;
			} else if (_upDirButtonID != -1 && buttonNr == _upDirButtonID) {
				if (!_isShowingDirectory) {
					directorySwitch = true;
				} else {
					++_displayedDirectory;
					changeDirectoryEntry = true;
				}
				_animIsStopped = true;
			} else if (_downDirButtonID != -1 && buttonNr == _downDirButtonID) {
				if (!_isShowingDirectory) {
					directorySwitch = true;
				} else {
					--_displayedDirectory;
					dirEntryDelta = -1;
					changeDirectoryEntry = true;
				}
				_animIsStopped = true;
			} else if (_dirButtonID != -1 && buttonNr == _dirButtonID) {
				if (!_isShowingDirectory) {
					directorySwitch = true;
				}
				_animIsStopped = true;
			} else {
				if (_isShowingDirectory || !_calledNumber.size()) {
					_isShowingDirectory = false;
					_displayedDirectory = 0;
					_drawSurface.fillRect(_displayDest, _drawSurface.getTransparentColor());
				}

				_calledNumber.push_back(buttonNr);
				_checkNumbers = _dialAutomatically;
				_animIsStopped = true;

				if (_calledNumber.size() > 11) {
					_calledNumber.clear();

					if (_hasDisplay) {
						_drawSurface.fillRect(_displayDest, _drawSurface.getTransparentColor());
					} else if (_isNewPhone) {
						NancySceneState.getTextbox().clear();
					}

					_checkNumbers = false;
				}

				if (_isNewPhone && _calledNumber.size()) {
					Common::String numberString;
					for (uint j = 0; j < _calledNumber.size(); ++j) {
						numberString += '0' + _calledNumber[j];
					}

					if (_hasDisplay) {
						_font->drawString(&_drawSurface, numberString, _displayDest.left + 19, _displayDest.top + 21 - _font->getFontHeight(),
							_displayDest.width() - 20, 0);
					} else {
						NancySceneState.getTextbox().clear();
						NancySceneState.getTextbox().addTextLine(numberString);
					}
				}
			}

			if (directorySwitch) {
				// Handle switch to directory mode
				_isShowingDirectory = true;
				changeDirectoryEntry = true;
				_calledNumber.clear();
			}

			if (changeDirectoryEntry) {
				int start = _displayedDirectory;

				do {
					if (_displayedDirectory >= (int)_calls.size()) {
						_displayedDirectory = 0;
					} else if (_displayedDirectory < 0) {
						_displayedDirectory = _calls.size() - 1;
					}

					if (_calls[_displayedDirectory].directoryDisplayCondition == kEvNoEvent) {
						break;
					}

					if (NancySceneState.getEventFlag(_calls[_displayedDirectory].directoryDisplayCondition, g_nancy->_true)) {
						break;
					}

					_displayedDirectory += dirEntryDelta;
				} while (_displayedDirectory != start);
			}

			_genericButtonSound.name = _buttonSoundNames[buttonNr];
			g_nancy->_sound->loadSound(_genericButtonSound);
			g_nancy->_sound->playSound(_genericButtonSound);

			_drawSurface.blitFrom(_image, _srcRects[buttonNr], _destRects[buttonNr]);
			_needsRedraw = true;

			_displayAnimEnd = 0;
			_displayAnimFrame = 0;

			_buttonLastPushed = buttonNr;
			_callState = kButtonPress;
		}
	}
}


// -- AR 157 in Nancy16 ----------------------------------------------------

// The Nancy16 counted sound group: uint16 count, count x char[33], uint16
// channel, uint32 loop count, uint16 volume.
static void readSoundGroup(Common::SeekableReadStream &stream, SoundDescription &desc) {
	RandomSoundBlock block;
	block.readData(stream);

	if (block.names.empty()) {
		return;
	}

	desc.name = block.names[0];
	desc.channelID = block.channel;
	desc.numLoops = block.numLoops > 0 ? block.numLoops : 1;
	desc.volume = block.volume;
}

// The game's short scene change: uint16 scene (0x7fff for "stay here"), uint16
// frame, int16 event flag, byte value.
static void readShortSceneChange(Common::SeekableReadStream &stream, SceneChangeWithFlag &out) {
	out._sceneChange.sceneID = stream.readUint16LE();
	if (out._sceneChange.sceneID == kNancy16NoScene) {
		out._sceneChange.sceneID = kNoScene;
	}

	out._sceneChange.frameID = stream.readUint16LE();
	out._sceneChange.continueSceneSound = kContinueSceneSound;
	out._flag.label = stream.readSint16LE();
	out._flag.flag = stream.readByte();
}

void Nancy16Telephone::readData(Common::SeekableReadStream &stream) {
	_numberLength = stream.readUint16LE();
	stream.skip(2);		// always 0
	stream.skip(4);		// float, always 10.0
	stream.skip(4);		// float, always 1.0

	const uint16 numCalls = stream.readUint16LE();
	_calls.resize(numCalls);
	for (uint i = 0; i < numCalls; ++i) {
		Call &c = _calls[i];
		readFilename(stream, c.number);
		c.flagA.label = stream.readSint16LE();
		c.flagA.flag = stream.readByte();

		SceneChangeWithFlag scene;
		readShortSceneChange(stream, scene);
		c.scene = scene._sceneChange;
		c.flagB = scene._flag;
	}

	stream.skip(2);		// always 29
	readFilename(stream, _imageName);
	stream.skip(4);		// float, always 0.1

	const uint16 numButtons = stream.readUint16LE();
	_buttons.resize(numButtons);
	for (uint i = 0; i < numButtons; ++i) {
		Button &b = _buttons[i];
		b.label = (char)stream.readByte();
		readRect(stream, b.src);
		readRect(stream, b.dest);
		readSoundGroup(stream, b.sound);

		if (i == 0) {
			_screenPosition = b.dest;
		} else {
			_screenPosition.extend(b.dest);
		}
	}

	stream.skip(2);		// always 1, the length of the prefix list below
	readFilename(stream, _prefix);
	stream.skip(4);		// always 10, the digits after the prefix
	stream.skip(4);		// always 10 as well

	readShortSceneChange(stream, _reloadScene);
	readSoundGroup(stream, _dialTone);

	const uint16 numZones = stream.readUint16LE();
	_zones.resize(numZones);
	for (uint i = 0; i < numZones; ++i) {
		Zone &z = _zones[i];
		readRect(stream, z.hotspot);
		z.cursorType = stream.readUint16LE();

		SceneChangeWithFlag scene;
		scene._sceneChange.frameID = 0;
		z.scene.sceneID = stream.readUint16LE();
		if (z.scene.sceneID == kNancy16NoScene) {
			z.scene.sceneID = kNoScene;
		}

		z.scene.continueSceneSound = kContinueSceneSound;
		z.flag.label = stream.readSint16LE();
		z.flag.flag = stream.readByte();
	}
}

Common::String Nancy16Telephone::getRecordExtraInfo() const {
	Common::String ret = Common::String::format("Keypad \"%s\", %u buttons, %u-digit numbers, prefix \"%s\"\n",
		_imageName.toString().c_str(), _buttons.size(), _numberLength, _prefix.c_str());

	for (uint i = 0; i < _calls.size(); ++i) {
		ret += Common::String::format("    number \"%s\" -> flag %d (%s) = %u, scene %u\n",
			_calls[i].number.c_str(), _calls[i].flagB.label,
			g_nancy->getEventFlagName(_calls[i].flagB.label).c_str(),
			_calls[i].flagB.flag, _calls[i].scene.sceneID);
	}

	for (uint i = 0; i < _zones.size(); ++i) {
		ret += Common::String::format("    zone (%d,%d,%d,%d), cursor %u, scene %u, flag %d = %u\n",
			_zones[i].hotspot.left, _zones[i].hotspot.top, _zones[i].hotspot.right, _zones[i].hotspot.bottom,
			_zones[i].cursorType, _zones[i].scene.sceneID, _zones[i].flag.label, _zones[i].flag.flag);
	}

	return ret;
}

void Nancy16Telephone::init() {
	g_nancy->_resource->loadImage(_imageName, _image);

	_drawSurface.create(_screenPosition.width(), _screenPosition.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
}

int Nancy16Telephone::buttonAtCursor(const Common::Point &mousePos) const {
	for (uint i = 0; i < _buttons.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_buttons[i].dest).contains(mousePos)) {
			return (int)i;
		}
	}

	return -1;
}

int Nancy16Telephone::zoneAtCursor(const Common::Point &mousePos) const {
	for (uint i = 0; i < _zones.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_zones[i].hotspot).contains(mousePos)) {
			return (int)i;
		}
	}

	return -1;
}

// GUESSED, in the safe direction: the record carries both a 13-digit number and
// a "011" prefix with a count of ten digits after it, and nothing in it says
// whether the player is expected to dial the prefix or whether the phone
// supplies it. A number is therefore accepted either way - typed in full, or
// typed without the prefix - which cannot turn a right number into a wrong one
// under either reading. The empty number, if the record has one, is the
// wrong-number case and only matches once the full length has been dialled.
int Nancy16Telephone::matchedCall() const {
	if (_dialled.empty()) {
		return -1;
	}

	for (uint i = 0; i < _calls.size(); ++i) {
		const Common::String &number = _calls[i].number;
		if (number.empty()) {
			continue;
		}

		if (number == _dialled || number == _prefix + _dialled) {
			return (int)i;
		}
	}

	if (_dialled.size() < _numberLength) {
		return -1;
	}

	for (uint i = 0; i < _calls.size(); ++i) {
		if (_calls[i].number.empty()) {
			return (int)i;
		}
	}

	return -1;
}

void Nancy16Telephone::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	// The atlas holds the lit face of each button, so the only thing ever drawn
	// is the one being pressed; the unlit keypad is the scene background.
	if (_litButton >= 0) {
		const Button &b = _buttons[_litButton];
		_drawSurface.blitFrom(_image, b.src,
			Common::Point(b.dest.left - _screenPosition.left, b.dest.top - _screenPosition.top));
	}

	setNeedsRedraw(true);
}

void Nancy16Telephone::press(uint button) {
	if (_dialTonePlaying) {
		g_nancy->_sound->stopSound(_dialTone);
		_dialTonePlaying = false;
	}

	g_nancy->_sound->loadSound(_buttons[button].sound);
	g_nancy->_sound->playSound(_buttons[button].sound);

	_litButton = (int)button;
	_litUntil = g_nancy->getTotalPlayTime() + 250;
	redraw();

	if (_dialled.size() < _numberLength) {
		_dialled += _buttons[button].label;
	}

	const int call = matchedCall();
	if (call < 0) {
		return;
	}

	// The number connects. The scene change is a sibling record's job - each
	// number's flag has its own AR 16 in the scene, which waits for the last
	// button beep to finish before it fires - so all this record does is raise
	// the flags and stop taking input.
	_connected = true;
	NancySceneState.setEventFlag(_calls[call].flagA);
	NancySceneState.setEventFlag(_calls[call].flagB);

	// Fallback for a game whose data does not have the scene's own AR 16s: the
	// number's own scene, then the record's reload scene. Both are "stay here"
	// in nancy18, so neither fires.
	if (_calls[call].scene.sceneID != kNoScene) {
		_pendingScene = _calls[call].scene;
		_zoneRequested = true;
	} else if (_reloadScene._sceneChange.sceneID != kNoScene && _calls[call].number.empty()) {
		_pendingScene = _reloadScene._sceneChange;
		_pendingFlag = _reloadScene._flag;
		_zoneRequested = true;
	}
}

void Nancy16Telephone::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		NancySceneState.setNoHeldItem();

		g_nancy->_sound->loadSound(_dialTone);
		g_nancy->_sound->playSound(_dialTone);
		_dialTonePlaying = true;

		_state = kRun;
		// fall through
	case kRun:
		// GUESSED: the atlas holds the darkened face of each key, so it can only
		// be the pressed state, but nothing in the record says how long a key
		// stays down. It is held for as long as that key's beep lasts, with a
		// quarter-second floor so a missing sound still shows something.
		if (_litButton >= 0 && g_nancy->getTotalPlayTime() > _litUntil &&
				!g_nancy->_sound->isSoundPlaying(_buttons[_litButton].sound)) {
			_litButton = -1;
			redraw();
		}

		if (_zoneRequested) {
			_state = kActionTrigger;
		}

		break;
	case kActionTrigger:
		if (_dialTonePlaying) {
			g_nancy->_sound->stopSound(_dialTone);
			_dialTonePlaying = false;
		}

		NancySceneState.setEventFlag(_pendingFlag);
		NancySceneState.changeScene(_pendingScene);
		finishExecution();
		break;
	}
}

void Nancy16Telephone::handleInput(NancyInput &input) {
	if (_state != kRun || _connected || _zoneRequested) {
		return;
	}

	const int zone = zoneAtCursor(input.mousePos);
	if (zone != -1) {
		g_nancy->_cursor->setCursorType(_zones[zone].cursorType ?
			(CursorManager::CursorType)_zones[zone].cursorType : g_nancy->_cursor->_puzzleExitCursor, true);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			// The zone in nancy18 has no scene of its own: it raises flag 1040,
			// and the scene's AR 145 plays the hang-up sound and does the
			// leaving. So the flag is raised here and the record keeps running.
			NancySceneState.setEventFlag(_zones[zone].flag);

			if (_zones[zone].scene.sceneID != kNoScene) {
				_pendingScene = _zones[zone].scene;
				_zoneRequested = true;
			} else {
				_connected = true;
			}

			if (_dialTonePlaying) {
				g_nancy->_sound->stopSound(_dialTone);
				_dialTonePlaying = false;
			}

			input.eatMouseInput();
		}

		return;
	}

	const int button = buttonAtCursor(input.mousePos);
	if (button != -1) {
		g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			press((uint)button);
			input.eatMouseInput();
		}
	}
}


} // End of namespace Action
} // End of namespace Nancy
