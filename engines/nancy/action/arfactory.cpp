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

#include "engines/nancy/action/collectionrecords.h"
#include "engines/nancy/action/datarecords.h"
#include "engines/nancy/action/inventoryrecords.h"
#include "engines/nancy/action/navigationrecords.h"
#include "engines/nancy/action/soundrecords.h"
#include "engines/nancy/action/miscrecords.h"

#include "engines/nancy/action/autotext.h"
#include "engines/nancy/action/conversation.h"
#include "engines/nancy/action/interactivevideo.h"
#include "engines/nancy/action/overlay.h"
#include "engines/nancy/action/secondaryvideo.h"
#include "engines/nancy/action/secondarymovie.h"
#include "engines/nancy/action/textscroll16.h"

#include "engines/nancy/action/puzzle/angletosspuzzle.h"
#include "engines/nancy/action/puzzle/arcadepuzzle.h"
#include "engines/nancy/action/puzzle/assemblypuzzle.h"
#include "engines/nancy/action/puzzle/bballpuzzle.h"
#include "engines/nancy/action/puzzle/beadpuzzle.h"
#include "engines/nancy/action/puzzle/blockspuzzle.h"
#include "engines/nancy/action/puzzle/boardgamepuzzle.h"
#include "engines/nancy/action/puzzle/bulpuzzle.h"
#include "engines/nancy/action/puzzle/bombpuzzle.h"
#include "engines/nancy/action/puzzle/cardgamepuzzle.h"
#include "engines/nancy/action/puzzle/chasemappuzzle.h"
#include "engines/nancy/action/puzzle/chesscodepuzzle.h"
#include "engines/nancy/action/puzzle/collisionpuzzle.h"
#include "engines/nancy/action/puzzle/cubepuzzle.h"
#include "engines/nancy/action/puzzle/cuttingpuzzle.h"
#include "engines/nancy/action/puzzle/dancepuzzle.h"
#include "engines/nancy/action/puzzle/dotconnectpuzzle.h"
#include "engines/nancy/action/puzzle/draggableimagepuzzle.h"
#include "engines/nancy/action/puzzle/drivingpuzzle.h"
#include "engines/nancy/action/puzzle/dropsortpuzzle.h"
#include "engines/nancy/action/puzzle/electrosensorspuzzle.h"
#include "engines/nancy/action/puzzle/gridmappuzzle.h"
#include "engines/nancy/action/puzzle/matchpuzzle.h"
#include "engines/nancy/action/puzzle/hamradiopuzzle.h"
#include "engines/nancy/action/puzzle/laptoppasswordpuzzle.h"
#include "engines/nancy/action/puzzle/leverpuzzle.h"
#include "engines/nancy/action/puzzle/lockpickpuzzle.h"
#include "engines/nancy/action/puzzle/magnetmazepuzzle.h"
#include "engines/nancy/action/puzzle/mazechasepuzzle.h"
#include "engines/nancy/action/puzzle/memorypuzzle.h"
#include "engines/nancy/action/puzzle/mindpuzzle.h"
#include "engines/nancy/action/puzzle/minigolfpuzzle.h"
#include "engines/nancy/action/puzzle/mirrorlightpuzzle.h"
#include "engines/nancy/action/puzzle/mosaicpuzzle.h"
#include "engines/nancy/action/puzzle/whackitpuzzle.h"
#include "engines/nancy/action/puzzle/mouselightpuzzle.h"
#include "engines/nancy/action/puzzle/multibuildpuzzle.h"
#include "engines/nancy/action/puzzle/onebuildpuzzle.h"
#include "engines/nancy/action/puzzle/orderingpuzzle.h"
#include "engines/nancy/action/puzzle/overridelockpuzzle.h"
#include "engines/nancy/action/puzzle/pachinkopuzzle.h"
#include "engines/nancy/action/puzzle/paperdollpuzzle.h"
#include "engines/nancy/action/puzzle/passwordpuzzle.h"
#include "engines/nancy/action/puzzle/peepholepuzzle.h"
#include "engines/nancy/action/puzzle/pegspuzzle.h"
#include "engines/nancy/action/puzzle/quizpuzzle.h"
#include "engines/nancy/action/puzzle/raycastpuzzle.h"
#include "engines/nancy/action/puzzle/riddlepuzzle.h"
#include "engines/nancy/action/puzzle/rippedletterpuzzle.h"
#include "engines/nancy/action/puzzle/rotatinglockpuzzle.h"
#include "engines/nancy/action/puzzle/safedialpuzzle.h"
#include "engines/nancy/action/puzzle/scalepuzzle.h"
#include "engines/nancy/action/puzzle/scopapuzzle.h"
#include "engines/nancy/action/puzzle/sentrypuzzle.h"
#include "engines/nancy/action/puzzle/setplayerclock.h"
#include "engines/nancy/action/puzzle/sewingmachinepuzzle.h"
#include "engines/nancy/action/puzzle/sliderpuzzle.h"
#include "engines/nancy/action/puzzle/sortpuzzle.h"
#include "engines/nancy/action/puzzle/soundequalizerpuzzle.h"
#include "engines/nancy/action/puzzle/soundmatchpuzzle.h"
#include "engines/nancy/action/puzzle/spigotpuzzle.h"
#include "engines/nancy/action/puzzle/stakeoutpuzzle.h"
#include "engines/nancy/action/puzzle/stepobjectspuzzle.h"
#include "engines/nancy/action/puzzle/tangrampuzzle.h"
#include "engines/nancy/action/puzzle/telephone.h"
#include "engines/nancy/action/puzzle/towerpuzzle.h"
#include "engines/nancy/action/puzzle/turningpuzzle.h"
#include "engines/nancy/action/puzzle/twodialpuzzle.h"
#include "engines/nancy/action/puzzle/typingquizpuzzle.h"
#include "engines/nancy/action/puzzle/whalesurvivorpuzzle.h"
#include "engines/nancy/action/puzzle/wordfindpuzzle.h"

#include "engines/nancy/state/scene.h"

#include "engines/nancy/nancy.h"

namespace Nancy {
namespace Action {

ActionRecord *ActionManager::createActionRecord(uint16 type, Common::SeekableReadStream *recordStream) {
	switch (type) {
	case 10:
		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new Hot1FrSceneChange(CursorManager::kHotspot);
		else
			return new SceneChange();	// Moved from 12 in Nancy10
	case 11:
		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new HotMultiframeSceneChange(CursorManager::kHotspot);
		else
			return new Hot1FrSceneChange(CursorManager::kNormal, true, true);
	case 12:
		if (g_nancy->getGameType() <= kGameTypeNancy9) {
			return new SceneChange();
		} else {
			return new HotMultiframeSceneChange(CursorManager::kNormal, true);
		}
	case 13:
		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new HotMultiframeMultiSceneChange();
		else
			return new Hot1FrSceneChange(CursorManager::kHotspot);
	case 14:
		return new Hot1FrSceneChange(CursorManager::kExit);
	case 15:
		// Nancy16 renumbered the navigation block. The record descriptions name
		// each one, and the payload sizes confirm the shape: 15 "Scene Change"
		// is 2 bytes, 16 "Scene Change with Frame" is a bare 6-byte descriptor,
		// 19 "Scene Change with Hotspot" adds an 18-byte HotspotDescription.
		// Reading these with the older layouts over-reads and produces garbage
		// destinations - which is how a scene change to kNoScene appeared.
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SceneChange();
		}

		return new Hot1FrSceneChange(CursorManager::kMoveForward);
	case 16:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SceneChange();
		}

		return new Hot1FrSceneChange(CursorManager::kMoveBackward);
	case 17:
		return new Hot1FrSceneChange(CursorManager::kMoveUp);
	case 18:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SceneChangeWithFlags();
		}

		return new Hot1FrSceneChange(CursorManager::kMoveDown);
	case 19:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// 26 bytes against the 24 that a 6-byte descriptor plus an 18-byte
			// HotspotDescription accounts for. The two extra bytes are not yet
			// identified, so this reads correctly and leaves them - harmless now
			// that dependencies are read from the head of the record rather than
			// whatever is left at the end.
			return new Hot1FrSceneChange(CursorManager::kHotspot);
		}

		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new HotMultiframeSceneChange(CursorManager::kMoveForward);
		else
			return new Hot1FrSceneChange(CursorManager::kMoveLeft);		// Moved from 22 in Nancy10
	case 20:
		if (g_nancy->getGameType() == kGameTypeVampire)
			return new PaletteThisScene();
		else if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new HotMultiframeSceneChange(CursorManager::kMoveUp);
		else
			return new Hot1FrSceneChange(CursorManager::kMoveRight);	// Moved from 23 in Nancy10
	case 21:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SceneChangeWithStream();
		}

		if (g_nancy->getGameType() == kGameTypeVampire)
			return new PaletteNextScene();
		else if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new HotMultiframeSceneChange(CursorManager::kMoveDown);
		else
			return new HotSingleFrameSceneChange();
	case 22:
		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new Hot1FrSceneChange(CursorManager::kMoveLeft);
		else
			return new HotMultiframeSceneChange(CursorManager::kHotspot);		// Moved from 11 in Nancy 10
	case 23:
		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new Hot1FrSceneChange(CursorManager::kMoveRight);
		else
			return new HotMultiframeSceneChange(CursorManager::kMoveForward);	// Moved from 19 in Nancy 10
	case 24:
		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new HotMultiframeMultiSceneCursorTypeSceneChange();
		else
			return new HotMultiframeSceneChange(CursorManager::kMoveUp);		// Moved from 20 in Nancy 10
	case 25: {
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SceneChangeTheWorks();
		}

		if (g_nancy->getGameType() <= kGameTypeNancy9) {
			// Weird case; instead of storing the cursor id, they instead chose to store
			// an AR id corresponding to one of the directional Hot1FrSceneChange variants.
			// Thus, we need to scan the incoming chunk and make another call to createActionRecord().
			// This is not the most elegant solution, but it works :)
			assert(recordStream);
			uint16 innerID = recordStream->readUint16LE();
			Hot1FrSceneChange *newRec = dynamic_cast<Hot1FrSceneChange *>(createActionRecord(innerID));
			assert(newRec);
			newRec->_isTerse = true;
			return newRec;
		} else {
			return new HotMultiframeSceneChange(CursorManager::kMoveDown);		// Moved from 21 in Nancy 10
		}
	}
	case 26:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Begin a stream": starts a named parallel script flow on the
			// script it names. See streams.h.
			return new BeginNamedStream();
		}

		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new InteractiveVideo();
		else
			return new HotMultiframeMultiSceneChange();	// Moved from 13 in Nancy 10
	case 27:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "End a stream": ends a Nancy16 parallel script flow, by name or
			// (35 of 57) the one the executing script is.
			return new EndStream();
		}

		return new HotMultiframeMultiSceneCursorTypeSceneChange(); // Moved from 24 to 27 in Nancy10
	case 28:	// Nancy10
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SetFlagsBasedOnInvCursor();
		}

		return new InteractiveVideo();	// Moved from 26 to 28 in Nancy10
	case 29:	// Nancy10
		return new ControlUIItems();
	case 30:	// Nancy11
		return new StopPlayerScrolling();
	case 31:	// Nancy11
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Automatic U-Turn in a node": the floor strip of a 360 panorama.
			return new AutoUTurn();
		}

		return new StartPlayerScrolling();
	case 32:	// Nancy10
		return new UIPopupPrepScene();
	case 34:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new NDUIControl();
		}

		return nullptr;
	case 35:	// Nancy12
		return new ConversationInfoCheck();
	case 36:	// Nancy12
		return new ConversationGoodbye();
	case 37:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Collection creation" - declares a named value collection that a
			// puzzle then fills in (the fax number, the keypad codes, the
			// Chinese box button order).
			return new CollectionCreate();
		}

		return nullptr;
	case 38:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Collection editing" - appends one value to a collection.
			return new CollectionEdit();
		}

		return nullptr;
	case 39:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Collection checking" - tests the contents, or the number of
			// entries, of a named value collection a puzzle fills in.
			return new CollectionCheck();
		}

		// Unknown in every earlier game, so keep the default's complaint.
		warning("Unknown action record type %d", type);
		return nullptr;
	case 40:
		if (g_nancy->getGameType() <= kGameTypeNancy1)
			return new LightningOn(); // Only used in TVD
		else
			return new SpecialEffect();
	case 41:	// Nancy14
	case 44:	// Nancy14 (adds a trailing volume byte)
		return new PlaySecondaryMovie();
	case 42:	// Nancy14
	case 43:	// Nancy14
		// Both numbers carry the same Nancy16 record: a weighted graph of idle
		// animations, which is what PlaySecondaryMovie's random mode already
		// plays. See readRandomMovieDataNancy16().
		// fall through
	case 45:	// Nancy11
		return new PlaySecondaryMovie(true);
	case 46:	// Nancy11
		return new PlayRandomMovieControl();
	case 47:	// Nancy14
		// A PlaySecondaryMovie subclass that appends a named {value, flag} list.
		// Handled inside PlaySecondaryMovie via _type == 47.
		return new PlaySecondaryMovie();
	case 50:
		return new ConversationVideo(); // PlayPrimaryVideoChan0
	case 51:
		return new PlaySecondaryVideo();
	case 52:
		// Nancy16 reused this number for a static overlay image - by far the most
		// common record in nancy18 at 3349 of 14143. Building a PlaySecondaryVideo
		// here parses without complaint and is entirely wrong.
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new OverlayStaticTerse();
		}

		return new PlaySecondaryVideo();
	case 53:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "rollover overlay" in Nancy16 - an image shown on mouse-over, not
			// a movie. The inherited mapping below fed an overlay's image name
			// to the movie loader, which error()s on the miss, so this is read
			// fresh rather than shared with PlaySecondaryMovie.
			return new RolloverOverlay();
		}

		return new PlaySecondaryMovie();
	case 54:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Static Overlay Image" in Nancy16: the terse overlay of AR 52
			// plus a flag-selected tint.
			return new OverlayStaticTinted();
		}

		if (g_nancy->getGameType() <= kGameTypeNancy1)
			return new Overlay(false); // PlayStaticBitmapAnimation
		else
			return new Overlay(true);
	case 55:
		if (g_nancy->getGameType() <= kGameTypeNancy1)
			return new Overlay(true); // PlayIntStaticBitmapAnimation
		else if (g_nancy->getGameType() >= kGameTypeNancy7)
			return new OverlayStaticTerse();
		else
			return nullptr;
	case 56:
		if (g_nancy->getGameType() <= kGameTypeNancy6)
			return new ConversationVideo();
		else
			return new OverlayAnimTerse();
	case 57:
		return new ConversationCel();
	case 58:
		return new ConversationSound();
	case 59:
		return new ConversationCelT();
	case 60:
		if (g_nancy->getGameType() <= kGameTypeNancy5)
			return new MapCall();	// Only used in tvd and nancy1
		else
			return new ConversationSoundT();
	case 61:
		if (g_nancy->getGameType() <= kGameTypeNancy5)
			return new MapCallHot1Fr();	// Only used in tvd and nancy1
		else
			return new Autotext();
	case 62:
		if (g_nancy->getGameType() <= kGameTypeNancy7)
			return new MapCallHotMultiframe(); // TVD/nancy1 only
		else
			return new ConversationCelTerse(); // nancy8 and up
	case 63:
		return new ConversationSoundTerse();
	case 65:
		return new TableIndexOverlay();
	case 66:
		return new TableIndexPlaySound();
	case 67:
		if (g_nancy->getGameType() >= kGameTypeNancy10)
			return new Autotext();		// Moved from 61 in Nancy 10
		else
			return new TableIndexSetValueHS();
	case 68:
		if (g_nancy->getGameType() >= kGameTypeNancy12)
			return new TextLineOverlay();
		else
			return new TextScroll(false);
	case 69:	// Nancy11
		return new TimerControl();
	case 70:
		return new TextScroll(true); // AutotextEntryList
	case 71:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// Nancy16 reuses 71 and 72 for the journal and task-list entries that
			// live in the JOURNAL_<character> / TASKLIST_<character> members. No
			// S<n> scene in nancy18 carries either number, so the audit is
			// untouched; see datarecords.h for the two 66/66 and 35/35 layouts.
			return new Nancy16JournalEntry();
		}

		return new ModifyListEntry(ModifyListEntry::kAdd);
	case 72:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new Nancy16TaskEntry();
		}

		return new ModifyListEntry(ModifyListEntry::kDelete);
	case 73:
		return new ModifyListEntry(ModifyListEntry::kMark);
	case 74:	// Nancy10 only: writes the full, taskbar-covering box
		return new FrameTextBox(true);
	case 75:
		if (g_nancy->getGameType() <= kGameTypeNancy9)
			return new TextBoxWrite();
		return new FrameTextBox(false);
	case 76:
		return new TextboxClear();
	case 77:
		return new SetValue();
	case 78:
		return new SetValueCombo();
	case 79:
		return new ValueTest();
	case 81:	// Nancy11
		return new TextBoxWrite(true);
	// Nancy16 moved the event-flag block down from 94-99 to 90-93 and reordered
	// it internally (95 -> 90, 94 -> 91, 96 -> 92). Without these the 2905
	// records in this block get no class at all.
	case 90:	// Nancy16
		return new EventFlags();
	case 91:	// Nancy16
		return new EventFlagsMultiHS(true);
	case 92:	// Nancy16
		return new RandomizeEventFlags();
	case 93:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new DetectStreamSceneChange();
		}

		return nullptr;
	case 94:	// Nancy12
		return new EventFlagsMultiHS(false);	// moved from 106
	case 95:	// Nancy12
		return new EventFlags();	// moved from 107
	case 96:	// Nancy11
		return new RandomizeEventFlags();
	case 97:
		return new EventFlags(true);
	case 98:
		return new EventFlagsMultiHS(true, true);
	case 99:
		return new EventFlagsMultiHS(true);
	case 100:
		return new BumpPlayerClock();
	case 101:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "bump player's clock", one record, in S1 - the scene that sets the
			// game up before it hands over to the first real one. Its nine bytes
			// are 01 | 0d 00 | 00 00 | fc ff fc ff: the first five are exactly
			// the pre-Nancy16 BumpPlayerClock record above (byte relative,
			// uint16 hours, uint16 minutes) and carry the same designer-written
			// description, which reads as "add 13 hours"; the four that follow
			// are new and unexplained.
			//
			// DELIBERATELY INERT. BSUM starts the clock at 6:00, so the two
			// readings of the record - add 13 hours, or set the clock to 13:00 -
			// give 19:00 and 13:00, and a single record cannot tell them apart.
			// Nothing in the game can either: of the 14143 action records, none
			// has a kPlayerTOD, kElapsedPlayerTime or kElapsedPlayerDay
			// dependency (the 125 time dependencies are all kElapsedSceneTime,
			// which is scene-local and unaffected), and nancy18's BOOT has no
			// MAP chunk, so the map's day/night scene pick - the engine's only
			// other reader of the time of day - never runs. There is therefore
			// no observation anywhere in the game that the two readings disagree
			// about, which is exactly why the choice cannot be settled and why
			// making it would be a coin flip rather than a decode.
			return new Nancy16ConsumeOnly(9);
		}

		return new SaveContinueGame();
	case 102:
		return new TurnOffMainRendering();
	case 103:
		return new TurnOnMainRendering();
	case 104:
		// Nancy16 keeps the Nancy12 slot/command header and only changes the
		// configure payload; see ResetAndStartTimer::readNancy16Data().
		return new ResetAndStartTimer();
	case 105:
		return new StopTimer();
	case 106:
		return new EventFlagsMultiHS(false);
	case 107:
		return new EventFlags();
	case 108:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new ClearNamedGame();
		}

		if (g_nancy->getGameType() <= kGameTypeNancy6)
			return new OrderingPuzzle(OrderingPuzzle::kOrdering);
		else
			return new GotoMenu();
	case 109:
		return new LoseGame();
	case 110:
		return new PushScene();
	case 111:
		return new PopScene();
	case 112:
		return new WinGame();
	case 113:
		return new DifficultyLevel();
	case 114:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SaveNamedGame();
		}

		return new RotatingLockPuzzle();
	case 115:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new LoadNamedGame();
		}

		return new LeverPuzzle();
	case 116:
		return new Telephone(false);
	case 117:
		return new SliderPuzzle();
	case 118:
		return new PasswordPuzzle();
	case 119:	// Nancy7
		return new OrderingPuzzle(OrderingPuzzle::kOrdering);
	case 120:
		return new AddInventoryNoHS();
	case 121:
		return new RemoveInventoryNoHS();
	case 122:
		return new ShowInventoryItem();
	case 123:
		return new InventorySoundOverride();
	case 124:
		return new EnableDisableInventory();
	case 125:
		return new PopInvViewPriorScene();
	case 126:
		return new GoInvViewScene();
	case 128:	// Nancy10
		return new CellPhonePopCellSceneFromStack();
	case 129:	// Nancy10
		return new SetCellPhoneBatteryAndSignal();
	case 130:	// Nancy10
		return new ChangeCellPhoneInfo();
	case 131:	// Nancy10
		return new AddSearchLink();
	case 132:	// Nancy12
		return new ResourceUse();
	case 133:	// Nancy14 - CameraAction
		// Cell-phone camera action (introduced alongside the UICM camera UI).
		// TODO: not yet implemented
		return nullptr;
	case 134:	// Nancy15 - PlayCharAR
		// Switches the active player character (Nancy / Frank / Joe), the
		// dual-protagonist mechanic new to The Creature of Kapu Cave. The
		// payload is a single 33-byte PCUI character name; the tree swap it
		// implies is still a TODO (see miscrecords.h).
		return new ChangePlayerCharacter();
	case 140:
		if (g_nancy->getGameType() >= kGameTypeNancy12)
			return new SetPlayerClock();	// Moved from 170 in Nancy12
		else
			return new SetVolume();			// Legacy SetVolume slot (used up to Nancy8)
	case 141:
		// MakeScreenFile, moved here from 148 in Nancy12.
		// Saves a cropped image of the screen to a bitmap/TGA file.
		// TODO: debug-only feature, not implemented
		return nullptr;
	case 143:	// Nancy14 - ConcatSound
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new ConcatSound();
		}

	case 144:	// Nancy14 - MultiSound (dropped from the Nancy15 dispatch)
		// Sibling sound ARs. ConcatSound plays a list of named sounds back-to-back;
		// TODO: not yet implemented
		return nullptr;
	case 145:	// Nancy13
		return new PlaySound(); // Moved from 150 in Nancy13
	case 146:	// Nancy13
		return new FadeSoundToSilence(); // Moved from 147 in Nancy13
	case 147:	// Nancy11
		if (g_nancy->getGameType() >= kGameTypeNancy13)
			return new SetVolume();			// Moved from 148 in Nancy13
		return new FadeSoundToSilence();	// Nancy11
	case 148:
		if (g_nancy->getGameType() >= kGameTypeNancy13)
			return new StopSound();	// Nancy13: StopSound moved here (was 154)
		if (g_nancy->getGameType() >= kGameTypeNancy12)
			return new SetVolume();	// Moved from 149 in Nancy12
		// MakeScreenFile - seems to save a cropped image of the screen in a bitmap file?
		// TODO: Used in Nancy 9, sand castle puzzle. Moved to 141 in Nancy12.
		return nullptr;
	case 149:
		if (g_nancy->getGameType() >= kGameTypeNancy13)
			return new StopSound();	// Nancy13: StopAndUnloadSound moved here (was 155)
		if (g_nancy->getGameType() >= kGameTypeNancy12)
			return new PlaySoundEventFlagTerse();	// Moved from 161 in Nancy12
		else if (g_nancy->getGameType() >= kGameTypeNancy9)
			return new SetVolume();	// Moved from 140 in Nancy9, then to 148 in Nancy12
		else
			return nullptr;
	case 150:
		if (g_nancy->getGameType() >= kGameTypeNancy14)
			return nullptr;	// Nancy14: SetMovieVolume, TODO. PlaySound moved to 145 in Nancy13.
		return new PlaySound();
	case 151:
		if (g_nancy->getGameType() <= kGameTypeNancy6)
			return new PlaySound(); // PlayStreamSound
		else
			return new PlayRandomSoundTerse();
	case 152:
		return new PlaySoundFrameAnchor();
	case 153:
		return new PlaySoundMultiHS();
	case 154:
		return new StopSound();
	case 155:
		return new StopSound(); // StopAndUnloadSound, but we always unload
	case 156:	// Nancy11
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Rotating Lock": the propane cabinet lock in S4408. Nancy16 reuses
			// the pre-Nancy12 puzzle with a rewritten record; see
			// RotatingLockPuzzle::readNancy16Data.
			return new RotatingLockPuzzle();
		}

		return new Update3DSound();
	case 157:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Telephone": the bedside phone in S3561. Nancy16's record is not
			// the one the Telephone class reads; see Nancy16Telephone.
			return new Nancy16Telephone();
		}

		return new PlaySoundCC();
	case 158:
		return new PlayRandomSound();
	case 159:
		if (g_nancy->getGameType() >= kGameTypeNancy14)
			return new GridMapPuzzle();	// moved from 244
		return new PlaySoundTerse();
	case 160:
		if (g_nancy->getGameType() >= kGameTypeNancy12)
			return new DrivingPuzzle(DrivingPuzzle::kDriving);
		return new HintSystem();
	case 161:
		if (g_nancy->getGameType() >= kGameTypeNancy12)
			return new MinigolfPuzzle();
		return new PlaySoundEventFlagTerse();
	// -- Nancy 12 new puzzles/action records --
	case 162:
		return new SewingMachinePuzzle();
	case 163:
		return new MirrorLightPuzzle();
	case 164:
		return new BoardGamePuzzle();
	case 165:
		return new MindPuzzle();
	case 166:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new PaperDollPuzzle();
		}

		return new OneBuildPuzzle();	// moved from 234 in Nancy12
	case 167:
		return new DrivingPuzzle(DrivingPuzzle::kChase);
	case 168:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SentryPuzzleExit();
		}

		return new Set3DSoundListenerPosition();
	// -- Nancy 13 new/relocated puzzles (types 169-176) --
	case 169:
		return new StepObjectsPuzzle();
	case 170:
		if (g_nancy->getGameType() >= kGameTypeNancy13)
			return new WordFindPuzzle();
		return new SetPlayerClock();	// moved to 140 in Nancy12
	case 171:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new LockPickPuzzle();
		}

		return new TurningPuzzle();	// moved from 209 in Nancy13
	case 172:
		return new BlocksPuzzle();
	case 173:
		return new PegsPuzzle();
	case 174:
		return new ScalePuzzle();	// balance scale
	case 175:
		return new PachinkoPuzzle();	// ball drop / pinball
	case 176:
		return new DropSortPuzzle();	// conveyor-belt candy sorting
	// -- Nancy14 new puzzles (types 177-182) --
	case 177:	// HangmanPuzzle
		// TODO: not yet implemented
		return nullptr;
	case 178:	// AdjustPuzzle
		// TODO: not yet implemented
		return nullptr;
	case 179:	// MeterPuzzle
		// TODO: not yet implemented
		return nullptr;
	case 180:	// BlockingPuzzle
		// TODO: not yet implemented
		return nullptr;
	case 181:	// PaintPuzzle
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new MosaicPuzzle();
		}

		// TODO: not yet implemented
		return nullptr;
	case 182:	// DecoderPuzzle
		// TODO: not yet implemented
		return nullptr;
	// -- Nancy15 new puzzles (types 183-185) --
	case 183:	// MagicBoxPuzzle
		// TODO: not yet implemented
		return nullptr;
	case 184:	// EscapeGridPuzzle
		// TODO: not yet implemented
		return nullptr;
	case 185:	// WeightSortPuzzle
		// TODO: not yet implemented
		return nullptr;
	case 200:
		return new SoundEqualizerPuzzle();
	case 201:
		return new TowerPuzzle();
	case 202:
		return new BombPuzzle();
	case 188:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new ChessCodePuzzle();
		}

		return nullptr;
	case 189:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Quiz Puzzle": the office laptop password box (S2929)
			return new LaptopPasswordPuzzle();
		}

		return nullptr;
	case 190:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Override Lock Puzzle": the Electro Sensors lever panel (S3890-S3893)
			return new ElectroSensorsPuzzle();
		}

		return nullptr;
	case 197:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new Nancy16TextScroll(false);
		}

		return nullptr;
	case 199:	// Nancy16
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// Same record as 197 plus one leading int32.
			return new Nancy16TextScroll(true);
		}

		return nullptr;
	case 203:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new WhackItPuzzle();
		}

		return new RippedLetterPuzzle();
	case 204:
		return new OverrideLockPuzzle();
	case 205:
		return new RiddlePuzzle();
	case 206:
		return new RaycastPuzzle();
	case 207:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Draggable Image with Action Zones": the microdot viewer
			return new DraggableImagePuzzle();
		}

		return new TangramPuzzle();
	case 208:
		return new OrderingPuzzle(OrderingPuzzle::PuzzleType::kPiano);
	case 209:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new TypewriterText();
		}

		return new TurningPuzzle();
	case 210:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new SentryPuzzle();
		}

		return new SafeDialPuzzle();
	case 211:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			return new ScopaPuzzle();
		}

		return new CollisionPuzzle(CollisionPuzzle::PuzzleType::kCollision);
	case 212:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// The Venice night stake-out (S5610), described "UI Control" by what
			// is plainly a copy-pasted label - that is AR type 34's description.
			// See stakeoutpuzzle.h for the 8797/8797 layout and for which parts
			// of the runtime are measured and which are a reading.
			return new StakeOutPuzzle();
		}

		return new OrderingPuzzle(OrderingPuzzle::PuzzleType::kOrderItems);
	case 213:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// "Graph Puzzle": the Venice chase map's node network (S5450).
			// Decoded, but deliberately inert - see chasemappuzzle.h.
			return new ChaseMapPuzzle();
		}

		return new CollisionPuzzle(CollisionPuzzle::PuzzleType::kTileMove);
	case 214:
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// Nancy16 reuses 214 for the Dance Audition ("Dance Dance Yeah")
			return new DancePuzzle();
		}

		return new OrderingPuzzle(OrderingPuzzle::PuzzleType::kKeypad);
	case 215:
		return new MazeChasePuzzle();
	case 216:
		return new PeepholePuzzle();
	case 217:
		return new MouseLightPuzzle();
	case 218:
		return new BulPuzzle();
	case 219:
		return new BBallPuzzle();
	case 220:
		return new TwoDialPuzzle();
	case 221:
		return new HamRadioPuzzle();
	case 222:
		return new AssemblyPuzzle();
	case 223:
		return new CubePuzzle();
	case 224:
		return new OrderingPuzzle(OrderingPuzzle::kKeypadTerse);
	case 225:
		return new SpigotPuzzle();
	// -- Nancy 8 and up --
	case 226:
		return new CuttingPuzzle();
	case 228:
		return new MatchPuzzle();
	case 229:
		return new ArcadePuzzle();
	case 230:
		return new Telephone(true);
	case 231:
		return new QuizPuzzle();
	case 232:
		return new AngleTossPuzzle();
	// -- Nancy 9 and up --
	case 233:
		return new SoundMatchPuzzle();
	case 234:
		return new OneBuildPuzzle();	// moved to 166 in Nancy12
	case 235:
		return new MultiBuildPuzzle();
	case 237:
		return new WhaleSurvivorPuzzle();
	case 238:
		return new MemoryPuzzle();
	// -- Nancy 10 and up --
	case 239:
		return new SortPuzzle();
	case 241:
		return new DotConnectPuzzle();
	case 242:
		return new MagnetMazePuzzle();
	case 243:
		return new BeadPuzzle();
	case 244:
		return new GridMapPuzzle();
	// -- Nancy 11 and up --
	case 245:
		return new TypingQuizPuzzle();
	case 246:
		return new CardGamePuzzle();
	default:
		warning("Unknown action record type %d", type);
		return nullptr;
	}
}

} // End of namespace Action
} // End of namespace Nancy
