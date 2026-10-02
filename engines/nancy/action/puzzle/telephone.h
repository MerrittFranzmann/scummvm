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

#ifndef NANCY_ACTION_TELEPHONE_H
#define NANCY_ACTION_TELEPHONE_H

#include "engines/nancy/action/puzzlerecord.h"

namespace Nancy {

class Font;

namespace Action {

class Telephone : public PuzzleRecord {
public:
	struct PhoneCall {
		Common::Array<byte> phoneNumber;
		Common::String soundName;
		Common::String text;
		SceneChangeWithFlag sceneChange;

		// NewPhone members
		int16 directoryDisplayCondition = -1;
		Common::Rect displaySrc;
	};

	enum CallState { kWaiting, kButtonPress, kRinging, kBadNumber, kPreCall, kCall, kHangUp };
	enum PhoneType { kTelephone, kNewPhone };

	Telephone(PhoneType phoneType) :
		PuzzleRecord(7),
		_callState(kWaiting),
		_buttonLastPushed(-1),
		_selected(-1),
		_checkNumbers(false),
		_font(nullptr),
		_animIsStopped(false),
		_phoneType(phoneType) {}
	virtual ~Telephone() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return _phoneType == kNewPhone ? "NewPhone" : "Telephone"; }

	Common::Array<Common::Rect> _srcRects;
	Common::Array<Common::Rect> _destRects;
	SoundDescription _genericDialogueSound;
	SoundDescription _genericButtonSound;
	SoundDescription _ringSound;
	SoundDescription _dialToneSound;
	SoundDescription _dialAgainSound;
	SoundDescription _hangUpSound;
	Common::Array<Common::String> _buttonSoundNames;
	Common::String _addressBookString;
	Common::String _dialAgainString;
	SceneChangeWithFlag _reloadScene;
	Common::Array<PhoneCall> _calls;

	// Number of digits a number needs before the phone starts dialing. Numbers
	// beginning with a '1' are long distance and have their own length
	uint16 _numberLength = 7;
	uint16 _longDistanceNumberLength = 11;

	// NewPhone properties
	bool _hasDisplay = false;
	uint16 _displayFont = 0;
	Common::Path _displayAnimName;
	uint32 _displayAnimFrameTime = 0;
	Common::Array<Common::Rect> _displaySrcs;
	Common::Rect _displayDest;

	bool _dialAutomatically = true;

	Common::Rect _dirHighlightSrc;
	Common::Rect _dialHighlightSrc;

	int16 _upDirButtonID = -1;
	int16 _downDirButtonID = -1;
	int16 _dialButtonID = -1;
	int16 _dirButtonID = -1;

	Common::Rect _displayDialingSrc;

	SoundDescription _preCallSound;

	Common::Array<byte> _calledNumber;
	Graphics::ManagedSurface _animImage;
	CallState _callState;
	int _buttonLastPushed;
	int _selected;
	bool _checkNumbers;
	bool _animIsStopped;

	uint32 _displayAnimEnd = 0;
	uint16 _displayAnimFrame = 0;
	int16 _displayedDirectory = 0;
	bool _isShowingDirectory = false;

	const Font *_font;

	PhoneType _phoneType;
};

// AR 157 in Nancy16. A cut-down Telephone with only the keypad, the numbers
// and an event flag per number; everything that happens after a number
// connects is done by sibling records keyed on that flag.
//
//   uint16     digits in a phone number
//   uint16     unknown
//   float      unknown
//   float      unknown
//   uint16     number count
//     per number, 43 bytes:
//       char[33] the number
//       int16    event flag, and its byte value
//       uint16   scene
//       uint16   frame
//       int16    event flag, and its byte value
//   uint16     unknown
//   char[33]   keypad atlas, the lit face of each button
//   float      unknown
//   uint16     button count
//     per button, 76 bytes:
//       byte     the character it dials, ASCII
//       RECT     source in the atlas
//       RECT     destination in the viewport
//       group    the sound it makes
//   uint16     prefix count
//   char[33]   dialling prefix
//   int32      digits after the prefix, twice
//   uint16     reload scene, + frame/flag/value
//   group      dial tone
//   uint16     action zone count, + 23 bytes each
//
// An empty number is what a wrong number connects to.
class Nancy16Telephone : public RenderActionRecord {
public:
	Nancy16Telephone() : RenderActionRecord(7) {}
	virtual ~Nancy16Telephone() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "Nancy16Telephone"; }
	Common::String getRecordExtraInfo() const override;

	struct Button {
		char label = 0;
		Common::Rect src;
		Common::Rect dest;
		SoundDescription sound;
	};

	struct Call {
		Common::String number;
		FlagDescription flagA;
		SceneChangeDescription scene;
		FlagDescription flagB;
	};

	struct Zone {
		Common::Rect hotspot;
		uint16 cursorType = 0;
		SceneChangeDescription scene;
		FlagDescription flag;
	};

	int buttonAtCursor(const Common::Point &mousePos) const;
	int zoneAtCursor(const Common::Point &mousePos) const;

	// Index of the number `_dialled` has completed, or -1
	int matchedCall() const;

	void press(uint button);
	void redraw();

	// -- File data --
	uint16 _numberLength = 0;
	Common::Array<Call> _calls;
	Common::Path _imageName;
	Common::Array<Button> _buttons;
	Common::String _prefix;
	SceneChangeWithFlag _reloadScene;
	SoundDescription _dialTone;
	Common::Array<Zone> _zones;

	// -- Runtime state --
	Graphics::ManagedSurface _image;
	Common::String _dialled;
	int _litButton = -1;
	uint32 _litUntil = 0;
	bool _dialTonePlaying = false;
	bool _connected = false;
	bool _zoneRequested = false;
	SceneChangeDescription _pendingScene;
	FlagDescription _pendingFlag;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_TELEPHONE_H
