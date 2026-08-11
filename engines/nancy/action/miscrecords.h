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

#ifndef NANCY_ACTION_RECORDTYPES_H
#define NANCY_ACTION_RECORDTYPES_H

#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/enginedata.h"

namespace Nancy {

class NancyEngine;

namespace Action {

// Changes the palette for the current scene's background. TVD only.
class PaletteThisScene : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	byte _paletteID;
	byte _unknownEnum; // enum w values 1-3
	uint16 _paletteStart;
	uint16 _paletteSize;

protected:
	Common::String getRecordTypeName() const override { return "PaletteThisScene"; }
};

// Changes the palette for the next scene's background. TVD only.
class PaletteNextScene : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	byte _paletteID;

protected:
	Common::String getRecordTypeName() const override { return "PaletteNextScene"; }
};

// Turns on (temporary) lightning effect. TVD Only.
class LightningOn : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	int16 _distance;
	uint16 _pulseTime;
	int16 _rgbPercent;

protected:
	Common::String getRecordTypeName() const override { return "LightningOn"; }
};

// Requests either a fade between two scenes, or a fade to black; fade executes when scene is changed. Nancy2 and up.
class SpecialEffect : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	// Nancy16 fades end by setting an event flag, which is what unblocks the
	// record waiting on them - the desk's ticket chain runs
	// hotspot -> flag 1013 -> this fade -> flag 1014 -> scene change.
	FlagDescription _flagOnCompletion;

	byte _type = 1;
	uint16 _fadeToBlackTime = 0;
	uint16 _frameTime = 0;
	uint16 _totalTime = 0;
	Common::Rect _rect;

protected:
	Common::String getRecordTypeName() const override { return "SpecialEffect"; }
};

// Adds a caption to the textbox. The Nancy 11+ "autotext" variant (AR 81),
// carries an extra header that makes the record wait for a sound to finish
// or a timer to elapse before it completes; in both variants the body is
// either inline text or resolved from an AUTOTEXT key.
class TextBoxWrite : public ActionRecord {
public:
	enum WaitMode { kWaitNone = 0, kWaitForSound = 1, kWaitForTimer = 2 };

	TextBoxWrite(bool isAutotext = false) : _isAutotext(isAutotext) {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::String _text;

	// Nancy 11+ AR 81 only
	bool _isAutotext;
	int16 _waitMode = 0;
	uint16 _soundChannel = 0;
	uint32 _waitTimeMs = 0;

protected:
	Common::String getRecordTypeName() const override { return _isAutotext ? "AutotextTextBoxWrite" : "TextBoxWrite"; }

private:
	uint32 _endTime = 0;
};

// Clears the textbox. Used very rarely.
class TextboxClear : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "TextboxClear"; }
};

// Nancy 10+ replacement for TextBoxWrite. Pushes a line of conversation
// text into the new (UICO-driven) textbox
class FrameTextBox : public ActionRecord {
public:
	FrameTextBox(bool fullMode) : _fullMode(fullMode) {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	bool _fullMode;
	Common::String _text;

protected:
	Common::String getRecordTypeName() const override { return "FrameTextBox"; }
};

// Nancy 10+ opcode 29. Toggles whether one of the taskbar popups
// (inventory / notebook / cellphone) is enabled.
class ControlUIItems : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	uint16 _uiButton = 0;
	byte _autoOpenOrBadgeSound = 0; // 1 = auto-open popup; 0/10 = notification-badge click-sound selector
	byte _flagB = 0;    // 0 = clear, 1 = enable+remember scene
	int16 _startScene = 0; // start scene id (9999 = none); also the auto-open cell phone's call target
	int16 _endScene = 0;   // end scene id (9999 = none)

	Common::String getRecordExtraInfo() const override {
		return Common::String::format("uiButton: %d, autoOpenOrBadgeSound: %d, flagB: %d, startScene: %d, endScene: %d",
									  _uiButton, _autoOpenOrBadgeSound, _flagB, _startScene, _endScene);
	}

protected:
	Common::String getRecordTypeName() const override { return "ControlUIItems"; }
};

// Nancy 10+ opcode 32. Prepares a UI popup
class UIPopupPrepScene : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	int32 _uiType = 0;
	int32 _signalValue = 0;

protected:
	Common::String getRecordTypeName() const override { return "UIPopupPrepScene"; }
};

// Nancy 10+ opcode 131. Pushes a new entry into either the cellphone
// search-results list (mode 0) or the URL link list (mode 1).
class AddSearchLink : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	int16 _mode = 0;
	SearchLink _link;

	Common::String getRecordExtraInfo() const override {
		return Common::String::format("Key: %s, Value: %s, Mode: %d, Extra: %d, Flag: %d, EventFlag: %d",
			_link.key.c_str(), _link.value.c_str(), _mode, _link.extra, _link.flag, _link.eventFlag);
	}

protected:
	Common::String getRecordTypeName() const override { return "AddSearchLink"; }
};

// Sets the cellphone's battery/signal indicators. Modes 0/1 toggle the
// battery (normal / low) and 2/3 toggle the signal (normal / no signal).
class SetCellPhoneBatteryAndSignal : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	uint16 _mode = 0;

protected:
	Common::String getRecordTypeName() const override { return "SetCellPhoneBatteryAndSignal"; }
};

// Adds a new entry to the cellphone directory, or overwrites an existing
// one matched by dial pattern. Used to unlock contacts as the player
// progresses (Nancy 10+).
class ChangeCellPhoneInfo : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	UICL::Contact _contact;

	Common::String getRecordExtraInfo() const override {
		return Common::String::format("Contact: %s", _contact.name.c_str());
	}

protected:
	Common::String getRecordTypeName() const override { return "ChangeCellPhoneInfo"; }
};

// Returns from a cellphone-driven conversation scene to the pre-call scene.
// sceneID == kNoScene pops the saved scene; any other sceneID overrides it.
class CellPhonePopCellSceneFromStack : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	SceneChangeDescription _sceneChange;

protected:
	Common::String getRecordTypeName() const override { return "CellPhonePopCellSceneFromStack"; }
};

// Changes the in-game time. Used prior to the introduction of SetPlayerClock.
class BumpPlayerClock : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	byte _relative;
	uint16 _hours;
	uint16 _minutes;

protected:
	Common::String getRecordTypeName() const override { return "BumpPlayerClock"; }
};

// Creates a Second Chance save.
class SaveContinueGame : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "SaveContinueGame"; }
};

// Stops the screen from rendering. Our rendering system is different from the original engine's,
// so we have no use for this.
class TurnOffMainRendering : public Unimplemented {
public:
	void readData(Common::SeekableReadStream &stream) override;

protected:
	Common::String getRecordTypeName() const override { return "TurnOffMainRendering"; }
};

// Restarts screen rendering. Our rendering system is different from the original engine's,
// so we have no use for this.
class TurnOnMainRendering : public Unimplemented {
public:
	void readData(Common::SeekableReadStream &stream) override;

protected:
	Common::String getRecordTypeName() const override { return "TurnOnMainRendering"; }
};

// Starts the timer. Used in combination with Dependency types that check for
// how much time has passed since the timer was started. Nancy 11 also carries a
// software-timer slot index (see TimerControl). From Nancy 12 the record became
// a general "Control a Timer" command: a slot index plus a command whose value
// selects a variable-size payload.
//
// Nancy16 keeps that header and only changes the configure payload; see
// readNancy16Data(). Two things read the slots it drives. A configured slot
// fires its own event flags when its duration elapses - that is how the game's
// 30-second idle prods work, and how S5005 gives the player ten minutes. A slot
// started with no duration is a stopwatch instead, read by dependency type 25
// (DependencyType::kSoftwareTimerElapsed); slots 4, 5 and 9 are used that way
// and no other, which is what identifies that dependency.
class ResetAndStartTimer : public ActionRecord {
public:
	enum Command {
		kStart           = 0, // Begin counting up from the current time
		kClear           = 1, // Reset the slot back to idle
		kConfigOneShot   = 2, // Set target/payload; fire once, then reset
		kConfigRepeating = 3, // Set target/payload; fire once, then keep counting
		kPause           = 4, // Suspend counting
		kAddTime         = 5, // Add the duration to the elapsed time
		kSubtractTime    = 6, // Subtract the duration from the elapsed time
		kSetTime         = 7  // Set the elapsed time to the duration
	};

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	int16 _timerIndex = 0;   // Software-timer slot (Nancy 11+)
	int16 _command = kStart; // Nancy 12+
	int16 _hours = 0;
	int16 _minutes = 0;
	int16 _seconds = 0;
	SoundDescription _sound;               // Played on expiry when configured
	Common::Array<FlagDescription> _flags; // Fired on expiry when configured

protected:
	// Nancy16 replaced the fixed sound block with the counted SoundGroup idiom
	void readNancy16Data(Common::SeekableReadStream &stream);

	Common::String getRecordTypeName() const override { return "ResetAndStartTimer"; }
};

// Stops the timer.
class StopTimer : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	byte _timerIndex = 0; // Nancy 11+ software-timer slot

protected:
	Common::String getRecordTypeName() const override { return "StopTimer"; }
};

// Nancy 11+ AR 69 (AT_TIMER_CONTROL). Issues a command to one of the 10
// software-timer slots (see TimerData::Timer). The fixed-size chunk
// (0xc4 header + count*4 flag entries) carries a slot index, a command, a
// target duration, an optional sound + caption, and the event flags to fire
// when the timer expires.
class TimerControl : public ActionRecord {
public:
	enum Command {
		kReset       = 0, // Clear the slot back to idle
		kStart       = 1, // Begin counting, with no target
		kPause       = 2, // Suspend counting
		kAddTime     = 3, // Add the duration to the elapsed time
		kSubtractTime = 4, // Subtract the duration from the elapsed time
		kConfigOneShot   = 5, // Set target/payload; fire once, then reset
		kConfigRepeating = 6  // Set target/payload; fire once, then keep running
	};

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	int16 _timerIndex = 0;
	int16 _command = 0;
	int16 _hours = 0;
	int16 _minutes = 0;
	int16 _seconds = 0;

	SoundDescription _sound;
	Common::String _autotextKey;
	Common::String _caption;
	Common::Array<FlagDescription> _flags;

protected:
	Common::String getRecordTypeName() const override { return "TimerControl"; }
};

// Nancy 11+ AR 30. Disables the player's ability to scroll/pan the viewport
// (both mouse-edge and keyboard movement). State persists across scene changes.
class StopPlayerScrolling : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "StopPlayerScrolling"; }
};

// Nancy 11+ AR 31. Re-enables the player's ability to scroll/pan the viewport.
class StartPlayerScrolling : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "StartPlayerScrolling"; }
};

// Returns the player back to the main menu
class GotoMenu : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "GotoMenu"; }
};

// Stops the game and boots the player back to the Menu screen, while also making sure
// they can't Continue. The devs took care to add Second Chance saves before every one
// of these, to make sure the player can return to a state just before the dangerous part.
class LoseGame : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "LoseGame"; }
};

// Adds a scene to the "stack" (which is just a single value). Used in combination with PopScene.
class PushScene : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "PushScene"; }
};

// Changes to the scene pushed onto the "stack". Scenes can be pushed via PushScene, or Conversation types.
class PopScene : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "PopScene"; }
};

// Ends the game and boots the player to the Credits screen.
// TODO: The original engine also sets a config option called PlayerWonTheGame,
// which in turn is used to trigger whichever event flag marks that the player
// has beat the game at least once, which in turn allows easter eggs to be shown.
// We currently support none of this.
class WinGame : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "WinGame"; }
};

// Checks how many hints the player is allowed to get. If they are still allowed hints,
// it selects an appropriate one and plays its sound/displays its caption in the Textbox.
// The hint system was _only_ used in nancy1, since it's pretty limited and overly punishing.
class HintSystem : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	byte _characterID; // 0x00
	SoundDescription _genericSound; // 0x01

	const Hint *selectedHint;
	int16 _hintID;

	void selectHint();

protected:
	Common::String getRecordTypeName() const override { return "HintSystem"; }
};

// Added in Nancy12 (AR 132). Adjusts a UI overlay resource (from the UIRC boot
// chunk) at runtime -- e.g. paying coins from the purse (resource 0). Applying
// the change plays a sound, optionally shows a transient overlay (a sprite and/or
// the resource's numeric value) and can change scene on success.
class ResourceUse : public RenderActionRecord {
public:
	ResourceUse() : RenderActionRecord(7) {}

	void init() override;
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "ResourceUse"; }

	// Applies the resource change (respecting affordability), sets the event
	// flag and starts the matching outcome sound.
	void applyChange();

	int16 _resourceIndex = 0;
	int16 _amount = 0;
	byte _mode = 0;        // 0 = set the resource, non-zero = add (clamped to >= 0)
	FlagDescription _flag; // event flag set when the change is applied

	Common::String _failSoundName;    // played when the change can't be applied
	Common::String _successSoundName; // played when it is applied

	// When this rect is non-degenerate the change is interactive: the player
	// clicks it (e.g. a coin slot) to pay. A degenerate rect applies at once.
	Common::Rect _paymentHotspot;
	byte _useResourceCursor = 0;      // 0 = normal cursor, else the resource's own hover cursor

	uint16 _sceneID = kNoScene;       // scene entered on success (9999 = none)
	uint16 _continueSceneSound = 0;

	bool _drawResourceOverlay = false; // blit the resource's UIRC sprite
	Common::Point _overlayDest;
	bool _drawResourceValue = false;   // draw the resource's numeric value
	Common::Point _valueDest;

	SoundDescription _sound;
	bool _hasSound = false;
	bool _interactive = false;
	bool _paymentResolved = false;
	bool _paymentApplied = false;
};

// Applies one NDUI command to a named widget - the action-record twin of the
// bindings authored inside the NDUI panels themselves. Nancy16+ only.
//
// Constant 78-byte payload across all 602 records in nancy18:
//   uint32   commandID     8 Set/enable (274), 9 Clear/disable (200),
//                          2 Hide (58), 1 Show (49), 51 (12), 50 (9)
//   char[33] target        widget name: ShowPDA, ShowInv, ShowJournal, ...
//   byte[41] zero in 602/602
class NDUIControl : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "NDUIControl"; }

	uint32 _commandID = 0;
	Common::String _target;
};

// Nancy16 reuses type 114 - RotatingLockPuzzle in earlier games - for a named
// save point. Constant 33-byte payload across all 14 records in nancy18: a slot
// name such as "Start_Game".
class SaveNamedGame : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "SaveNamedGame"; }

	Common::String _slotName;
};

// Nancy16 type 115, "Load a saved Game" - LeverPuzzle in earlier games. The
// other half of type 114, and the same constant 33-byte payload: the name of
// the checkpoint to restore.
//
// This is a direct load, not a menu. Every one of the 16 records names a
// checkpoint that a type 114 record elsewhere writes under exactly that name,
// and the two sets match one for one:
//
//   S2923/S2924 "2nd Chance - Hide"        -> S3092 loads it
//   S2946       "2nd Chance - Office"      -> S3095
//   S3801       "2nd Chance - Zattere"     -> S3999
//   S5003       "2nd Chance - Urn Smash"   -> S5008
//   S5400       "2nd Chance - Final Chase" -> S5481
//   S1453       "2nd Chance - Stakeout"    -> S5628
//   S6096       "2nd Chance - Fall"        -> S6620
//   S1          "Start_Game"               -> all nine of the above, plus S2069
//
// Those nine scenes are the death screens, and each pairs its records with two
// type 53 rollover overlays named VEN_SecondChanceYes_OVL / ...No_OVL, which
// raise scratch event flags 1010-1013 - precisely the flags this record's own
// dependency tests. "Yes" restores the checkpoint, "No" restarts from
// Start_Game. Nothing else in those scenes' record lists does anything, so this
// is the whole of the second-chance mechanic.
class LoadNamedGame : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "LoadNamedGame"; }

	Common::String _slotName;
};

// Nancy16 type 108, "Clear Saved Data" - GotoMenu in Nancy7-15, an
// OrderingPuzzle before that. Three records, all naming "ScopaSave", and all in
// the Scopa card-game scenes (S1355 sets the game up, S4450/S4455 end it), so
// this drops the card game's own checkpoint once the hand is over.
//
// Same 33-byte name payload as 114 and 115. "ScopaSave" is not among the names
// type 114 writes, so on a normal playthrough there is nothing there to remove
// and this is a no-op - which is why it is safe to act on: the worst case is
// that a save the game itself considers stale goes away.
class ClearNamedGame : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "ClearNamedGame"; }

	Common::String _slotName;
};

// Nancy15 type 134, "Change player character" - the dual-protagonist mechanic
// introduced by The Creature of Kapu Cave, where the player alternates between
// Nancy and the Hardy Boys. Each player character has its own UI tree, named by
// the BOOT chunk PCUI.
//
// Constant 33-byte payload - one readFilename, and nothing else. That is the
// whole record: 0x30 description + type + execType + the Nancy16 dependency
// header (4-byte count, then the "EndOfDeps" sentinel) is 86 bytes, the chunk is
// 119, and 119 - 86 = 33. Reading it as Unimplemented consumed none of those 33
// and produced one "AUDIT underread type 134 as Unimplemented: 86 of 119 bytes"
// per environment entry.
//
// All 11 records in nancy18 are byte-identical and name "Nancy", the second of
// the two characters PCUI lists ("Tutor" -> PUI_Default, "Nancy" -> PUI_Nancy).
// Ten of them are the type-134 in an *_AMB_SFX ambient script, which the engine
// starts on entering an environment - including the environment re-derived by a
// load - and the eleventh is in TUT_ONEXIT, which leaves the tutorial. So on
// this game the record only ever re-asserts the character the game is already
// on, and it has no visible effect.
//
// TODO: the actual character switch (swapping the loaded NDUI tree) needs a
// player-character UI runtime this engine does not have yet. Nothing in nancy18
// exercises it, so this validates the name against PCUI and stops there.
class ChangePlayerCharacter : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "ChangePlayerCharacter"; }

	Common::String _characterName;
};

// Nancy16 type 18, "Scene Change, Frame and Flags". 32 records, 12 or 16 bytes:
// the 6-byte scene core, then a counted list of 4-byte flag entries. The scene
// id is a real scene in 32/32. Earlier games map 18 to Hot1FrSceneChange, whose
// layout does not fit.
class SceneChangeWithFlags : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "SceneChangeWithFlags"; }

	SceneChangeDescription _sceneChange;
	MultiEventFlagDescription _flags;
};

// Nancy16 type 209, "Typewriter Text". 35 records, a constant 309 bytes. This is
// the series' game-over card. Every record names an AUTOTEXT key of the form
// "<puzzle>_Lose<n>" - Chase_Lose01, Bee_Lose3, Override_Lose01, Water_Lose02 -
// and every one of those strings is the same joke:
//   "<c1>The Good News: The pigeons still like you.<n>The Bad News: The police?
//    Not so much."
// They live only in scenes reached by failing a timed puzzle. Earlier games map
// 209 to TurningPuzzle, which over-reads.
//
// Layout, measured over all 35 records with 0 bytes left over:
//
//   char[33] AUTOTEXT key
//   RECT     destination box, viewport coordinates; (69, 53)-(570, 337) in 35/35
//   double   typing speed: 60.0 in 34/35, 20.0 in the S2074 copy of Bee_Lose1
//   uint32   FONT chunk id: 6 (UIFontLarge, Tahoma 22 bold) in 35/35
//   uint32   COLR chunk id used when the string sets no colour of its own: 0 or 1
//   RandomSoundBlock  the interchangeable key-click sounds - an int16 count (7 in
//                     35/35), that many char[33] names (Click_TelButton01..07 in
//                     35/35), then channel, numLoops and volume
//   int16 label + byte flag    the event flag raised when the text is finished
//
// The field order (box, then font id, then colour id, then a flag at the very
// end) is the same one AR 197/199 "Text Scroll" uses, and the sound block is the
// engine's standard RandomSoundBlock, so nothing here is a fresh guess.
//
// That trailing flag is the whole point of the record. S5480, the villain
// chase's second-chance scene, runs
//   Fade -> 1040 -> pick one of 1041/1042/1043 at random -> the matching
//   Typewriter Text -> 1045 -> Fade -> 1047 -> Fade -> 1046 -> scene change to
//   S5481
// and 1045 is written by nothing else anywhere in the game. While this record
// was a stub that did no more than set _isDone, losing the chase parked the run
// on S5480 for ever. The identical shape gates S3090/S3093 (the bust), S5007
// (the urn) and S5626 (the ORP stake-out) on 1045, and S3894-S3899 (the keypad
// override and the thermal suit) on 1012, so the same dead end was waiting
// behind five other failures.
//
// GUESS, and the only one here: the double is a per-character interval in
// milliseconds. It is the record's sole timing field; 60 as seconds-of-display
// is far too long for a card nothing else waits on, and 60 characters/second
// finishes the shipped strings in under two seconds, which is not long enough
// to read them. 60 ms/char gives them 5-10 seconds, which is. The data does not
// settle it, so it is called out rather than buried.
class TypewriterText : public RenderActionRecord {
public:
	TypewriterText() : RenderActionRecord(8) {}
	virtual ~TypewriterText() {}

	void init() override;
	void updateGraphics() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "TypewriterText"; }
	Common::String getRecordExtraInfo() const override;

	void redraw();
	void playKeyClick();
	void raiseDoneFlag();

	Common::String _textKey;
	Common::Rect _dest;
	double _charTime = 60.0;
	uint32 _fontID = 0;
	uint32 _colourID = 0;
	RandomSoundBlock _keySounds;
	FlagDescription _doneFlag;

	Common::String _text;
	Common::Rect _clipped;
	uint _numChars = 0;
	uint _shown = 0;
	uint32 _startTime = 0;
	bool _initialised = false;
	bool _typingDone = false;
};

// Nancy16 type 93, "Detecting main stream scene changes". 29 records, a constant
// 36 bytes: a 33-byte stream name and an event flag set when that stream changes
// scene. The flag is 1010 (true) in 29/29 - the first of the per-scene scratch
// flags - and the name is "MainStream" in 26 of them, "UIINV_ShowPDA" in the
// other three.
//
// Every one of the 29 lives in a script that is itself a stream: 19 in the
// "<area> random sfx" scenes the ENVS ambient scripts start, one in s5450 (the
// Venice chase), and three pairs in the PDA scenes. The idiom is always the
// same - watch the flow the player is driving, and when it moves, re-run. In
// s2009 the very next record is a type 15 scene change back to s2009 gated on
// 1010, which re-rolls the area's random ambience each time the player walks
// somewhere. Since 1010 is per-flow (see streams.h) that loop is the stream's
// own and does not touch the player's.
//
// A watcher on a stream that does not exist never fires, which is what happens
// to the three "UIINV_ShowPDA" records: that stream is created by the inventory
// panel, which this runtime does not model.
class DetectStreamSceneChange : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "DetectStreamSceneChange"; }
	Common::String getRecordExtraInfo() const override;

	Common::String _streamName;
	FlagDescription _flag;

	uint32 _watchedCountAtStart = 0;
};

// Nancy16 type 27, "End a stream". 57 records, a constant 33 bytes: just the
// name of the stream to end. 22 name one ("OFFPush", "Prudence Phone Ringing",
// "ET_Fidget", ...) and 35 name nothing at all, which means "end the stream
// this record belongs to" - the idiom ONBECOMENANCY and TUT_ONEXIT use to
// terminate themselves, and the idiom s6729's exit strip uses to close the
// tunnel flow chart close-up. See StreamManager::end() for how the empty name
// is resolved and for why it can never end the main flow.
class EndStream : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "EndStream"; }
	Common::String getRecordExtraInfo() const override;

	Common::String _streamName;
};

// Nancy16 records that are consumed exactly but not acted on. Sizes verified
// over every record in the game; the behaviour needs fields that have not been
// pinned down, so acting on a guess would be worse than doing nothing.
//
//   type 25 "Scene Change with The Works"  69 + (uint16@49 - 1) * 18, 66/66
//
// The four shapes below are fully laid out rather than measured, but are still
// only consumed - see the comment on each for what the fields are.
//
//   type 42  "Random Movie/Fidget"    11/11   (same layout as 43)
//   type 43  "Random Movie/Fidget"    29/29
//   type 53  "rollover overlay"       75/75
//   type 54  "Static Overlay Image"   21/21
class Nancy16ConsumeOnly : public ActionRecord {
public:
	enum Shape {
		kSceneChangeTheWorks, kFixedSize, kCountedTail,
		kRandomMovieGraph, kRolloverOverlay, kStaticOverlayImage,
		kPaperDollPuzzle
	};

	Nancy16ConsumeOnly(Shape shape) : _shape(shape) {}

	// Fixed-size variant: the record is always `size` bytes in this game, so it
	// can be consumed exactly without knowing what the fields mean.
	Nancy16ConsumeOnly(uint size) : _shape(kFixedSize), _size(size) {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override;

	Shape _shape;
	uint _size = 0;
	Common::String _name;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_MISCRECORDS_H
