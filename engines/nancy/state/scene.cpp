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

#include "common/serializer.h"
#include "common/config-manager.h"
#include "common/system.h"

#include "audio/mixer.h"
#include "common/tokenizer.h"
#include "common/func.h"
#include "common/random.h"

#include "engines/metaengine.h"
#include "engines/savestate.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/iff.h"
#include "engines/nancy/input.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/cursor.h"
#include "engines/nancy/util.h"
#include "engines/nancy/resource.h"

#include "engines/nancy/state/scene.h"
#include "engines/nancy/ndui.h"
#include "engines/nancy/state/map.h"

#include "engines/nancy/action/conversation.h"
#include "engines/nancy/action/datarecords.h"
#include "engines/nancy/action/secondarymovie.h"

#include "engines/nancy/ui/button.h"
#include "engines/nancy/ui/ornaments.h"
#include "engines/nancy/ui/camera.h"
#include "engines/nancy/ui/clock.h"
#include "engines/nancy/ui/taskbar.h"

#include "engines/nancy/misc/lightning.h"
#include "engines/nancy/misc/specialeffect.h"

namespace Common {
DECLARE_SINGLETON(Nancy::State::Scene);
}

namespace Nancy {
namespace State {

void Scene::SceneSummary::read(Common::SeekableReadStream &stream) {
	char *buf = new char[0x32];
	int32 x = 0;
	int32 y = 0;
	int32 z = 0;

	stream.seek(0);
	Common::Serializer ser(&stream, nullptr);
	ser.setVersion(g_nancy->getGameType());

	ser.syncBytes((byte *)buf, 0x32);
	description = Common::String(buf);

	readFilename(stream, videoFile);

	// skip 2 unknown bytes
	ser.skip(2);
	videoFormat = stream.readUint16LE();

	// Load the palette data in The Vampire Diaries
	ser.skip(4, kGameTypeVampire, kGameTypeVampire);
	readFilenameArray(ser, palettes, 3, kGameTypeVampire, kGameTypeVampire);

	sound.readScene(stream);

	ser.syncAsUint16LE(panningType);
	ser.syncAsUint16LE(numberOfVideoFrames, kGameTypeVampire, kGameTypeNancy2);
	ser.syncAsUint16LE(degreesPerRotation);
	ser.syncAsUint16LE(totalViewAngle, kGameTypeVampire, kGameTypeNancy2);
	ser.syncAsUint32LE(x, kGameTypeNancy3);
	ser.syncAsUint32LE(y, kGameTypeNancy3);
	ser.syncAsUint32LE(z, kGameTypeNancy3);
	listenerPosition.set(x, y, z);
	ser.syncAsUint16LE(horizontalScrollDelta);
	ser.syncAsUint16LE(verticalScrollDelta);
	ser.syncAsUint16LE(horizontalEdgeSize);
	ser.syncAsUint16LE(verticalEdgeSize);
	ser.syncAsUint16LE((uint32 &)slowMoveTimeDelta);
	ser.syncAsUint16LE((uint32 &)fastMoveTimeDelta);
	ser.skip(1); // CD required for scene

	auto *bootSummary = GetEngineData(BSUM);
	assert(bootSummary);

	if (bootSummary->overrideMovementTimeDeltas) {
		slowMoveTimeDelta = bootSummary->slowMovementTimeDelta;
		fastMoveTimeDelta = bootSummary->fastMovementTimeDelta;
	}

	delete[] buf;
}

void Scene::SceneSummary::readTerse(Common::SeekableReadStream &stream) {
	char buf[0x32];
	stream.read(buf, 0x32);
	description = buf;
	readFilename(stream, videoFile);
	sound.readTerse(stream);
}

Scene::Scene() :
		_state (kInit),
		_lastHintCharacter(-1),
		_lastHintID(-1),
		_gameStateRequested(NancyState::kNone),
		_frame(),
		_viewport(),
		_textbox(),
		_inventoryBox(),
		_menuButton(nullptr),
		_helpButton(nullptr),
		_taskbar(nullptr),
		_pendingTaskbarButton(-1),
		_viewportOrnaments(nullptr),
		_textboxOrnaments(nullptr),
		_inventoryBoxOrnaments(nullptr),
		_clock(nullptr),
		_camera(nullptr),
		_actionManager(),
		_difficulty(0),
		_activeMovie(nullptr),
		_activeConversation(nullptr),
		_lightning(nullptr),
		_destroyOnExit(false),
		_isRunningAd(false),
		_hotspotDebug(50) {}

Scene::~Scene() {
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		delete _nduiPanels[i];
	}
	_nduiPanels.clear();

	// These alias the deleted panels, and action records may still query them
	// during shutdown.
	_conversationPanel = nullptr;
	_conversationPanels.clear();
	_narrationPanel = nullptr;
	_inventoryPanel = nullptr;
	_stakeoutPanel = nullptr;
	_journalPanel = nullptr;
	_tasklistPanel = nullptr;
	for (uint i = 0; i < kNumJournalPages; ++i) {
		_journalPagePanels[i] = nullptr;
	}

	delete _helpButton;
	delete _menuButton;
	delete _taskbar;
	delete _viewportOrnaments;
	delete _textboxOrnaments;
	delete _inventoryBoxOrnaments;
	delete _clock;
	delete _camera;
	delete _lightning;

	clearPuzzleData();
}

void Scene::process() {
	switch (_state) {
	case kInit:
		init();

		if (_state != kLoad) {
			break;
		}

		// fall through
	case kLoad:
		load();
		// fall through
	case kStartSound:
		_state = kRun;
		if (_sceneState.currentScene.continueSceneSound == kLoadSceneSound) {
			g_nancy->_sound->stopAndUnloadSceneSpecificSounds();
			g_nancy->_sound->loadSound(_sceneState.summary.sound);
			g_nancy->_sound->playSound(_sceneState.summary.sound);
		}
		// fall through
	case kRun:
		run();
		break;
	}
}

void Scene::onStateEnter(const NancyState::NancyState prevState) {
	if (_state != kInit) {
		// Picks up a look chosen on the Design Select screen while we were away
		applyPlayerCharacter(g_nancy->getPlayerCharacter());

		registerGraphics();

		if (prevState != NancyState::kPause) {
			g_nancy->setTotalPlayTime((uint32)_timers.pushedPlayTime);
		}

		_actionManager.onPause(false);
		_streams.onPause(false);

		g_nancy->_graphics->redrawAll();

		if (getHeldItem() != -1) {
			g_nancy->_cursor->setCursorItemID(getHeldItem());
		}

		if (prevState == NancyState::kPause) {
			g_nancy->_sound->pauseAllSounds(false);
		} else {
			g_nancy->_sound->pauseSceneSpecificSounds(false);
		}

		g_nancy->_sound->stopSound("MSND");
	}

	g_nancy->_hasJustSaved = false;
}

bool Scene::onStateExit(const NancyState::NancyState nextState) {
	if (_state == kRun) {
		// Exiting the state outside the kRun state means we've encountered an error
		g_nancy->_graphics->screenshotScreen(_lastScreenshot);
	}

	if (nextState != NancyState::kPause) {
		_timers.pushedPlayTime = g_nancy->getTotalPlayTime();
	}

	_actionManager.onPause(true);
	_streams.onPause(true);

	if (nextState == NancyState::kPause) {
		g_nancy->_sound->pauseAllSounds(true);
	} else {
		g_nancy->_sound->pauseSceneSpecificSounds(true);
	}

	_gameStateRequested = NancyState::kNone;

	// Re-register the clock so the open/close animation can continue playing inside Map
	if (nextState == NancyState::kMap && g_nancy->getGameType() == kGameTypeVampire) {
		_clock->registerGraphics();
	}

	return _destroyOnExit;
}

void Scene::changeScene(const SceneChangeDescription &sceneDescription, bool bypassStreams) {
	if (sceneDescription.sceneID == kNoScene) {
		return;
	}

	// A record running inside a Nancy16 stream moves its own flow. This comes
	// before the kLoad guard, since it is independent of the main flow.
	if (!bypassStreams && _streams.redirectSceneChange(sceneDescription)) {
		return;
	}

	if (_state == kLoad) {
		return;
	}

	_sceneState.nextScene = sceneDescription;

	// In Nancy16 a scene change to the current scene is a refresh and must not
	// stop the scene's sound. The continueSceneSound field is always zero in the
	// data, so it cannot be relied on for this.
	if (g_nancy->getGameType() >= kGameTypeNancy16 &&
			_sceneState.nextScene.sceneID == _sceneState.currentScene.sceneID) {
		_sceneState.nextScene.continueSceneSound = kContinueSceneSound;
	}

	_state = kLoad;
}

static const char *const kPDAToolbarControls[] = { "ShowInv", "ShowJournal", "ShowTasklist" };

void Scene::pushScene(int16 itemID) {
	if (itemID == -1) {
		_sceneState.pushedScene = _sceneState.currentScene;
		_sceneState.isScenePushed = true;
	} else {
		if (_sceneState.isInvScenePushed) {
			// Re-add current pushed item
			addItemToInventory(_sceneState.pushedInvItemID);
		} else {
			// Only set this when another item hasn't been pushed, otherwise
			// the player will never be able to exit
			_sceneState.pushedInvScene = _sceneState.currentScene;
		}

		_sceneState.isInvScenePushed = true;
		_sceneState.pushedInvItemID = itemID;
	}
}

void Scene::popScene(bool inventory) {
	if (_pdaReturnEnabled != 0xff) {
		for (uint i = 0; i < ARRAYSIZE(kPDAToolbarControls); ++i) {
			applyNDUICommand(kPDAToolbarControls[i],
				(_pdaReturnEnabled & (1 << i)) ? kNDUICommandSet : kNDUICommandClear);
		}
		_pdaReturnEnabled = 0xff;
	}

	if (!inventory || _sceneState.pushedInvItemID == -1) {
		_sceneState.pushedScene.continueSceneSound = kContinueSceneSound;
		changeScene(_sceneState.pushedScene);
		_sceneState.isScenePushed = false;
	} else {
		_sceneState.pushedInvScene.continueSceneSound = kContinueSceneSound;
		changeScene(_sceneState.pushedInvScene);
		_sceneState.isInvScenePushed = false;
		addItemToInventory(_sceneState.pushedInvItemID);
		// Returning from a close-up view restores an item the player already
		// owned, so it must not raise the "new item" taskbar badge that
		// addItemToInventory sets for a genuine pickup.
		if (_taskbar) {
			_taskbar->clearNotification(kTaskButtonInventory, 0);
		}
		_sceneState.pushedInvItemID = kEvNoEvent;
		_sceneState.pushedInvScene.sceneID = kNoScene;
	}
}

bool Scene::openItemViewScene(int16 itemID) {
	// Nancy16 has no INV chunk; the item table moved to its own INVD member.
	const INVD *invd = (const INVD *)g_nancy->getEngineData("INVD");
	if (!invd) {
		return false;
	}

	const INVD::Item *item = nullptr;
	for (uint i = 0; i < invd->items.size(); ++i) {
		if (invd->items[i].id == itemID) {
			item = &invd->items[i];
			break;
		}
	}

	if (!item || !item->hasViewScene() || getItemDisabledState(itemID)) {
		return false;
	}

	// "s6428" -> 6428
	const Common::String &nm = item->viewSceneName;
	uint16 viewScene = kNoScene;
	if (nm.size() > 1 && (nm[0] == 's' || nm[0] == 'S')) {
		viewScene = (uint16)atoi(nm.c_str() + 1);
	}

	if (viewScene == kNoScene || viewScene == 0) {
		return false;
	}

	// A PDA call disables some toolbar controls and the data never re-enables
	// them, so remember their state to restore it when the PDA closes.
	if (itemID == 45 && _pdaReturnEnabled == 0xff) {
		_pdaReturnEnabled = 0;
		for (uint i = 0; i < ARRAYSIZE(kPDAToolbarControls); ++i) {
			NDUIPanel *panel = findNDUIPanelWithControl(kPDAToolbarControls[i]);
			if (panel && panel->isControlEnabled(kPDAToolbarControls[i])) {
				_pdaReturnEnabled |= 1 << i;
			}
		}
	}

	if (hasItem(itemID)) {
		pushScene(itemID);
		// Viewing an item's close-up takes it out of the inventory; popScene
		// puts it back.
		removeItemFromInventory(itemID, false);
	} else {
		pushScene();
	}

	SceneChangeDescription sceneChange;
	sceneChange.sceneID = viewScene;
	sceneChange.continueSceneSound = kContinueSceneSound;
	changeScene(sceneChange);
	return true;
}

bool Scene::popPushedScene() {
	if (!_sceneState.isInvScenePushed && !_sceneState.isScenePushed) {
		return false;
	}

	popScene(_sceneState.isInvScenePushed);
	return true;
}

void Scene::startUIPrepScene(int16 uiType, int16 prepSceneID) {
	if (_uiPrep.active || (uint16)prepSceneID == kNoScene) {
		return;
	}

	_uiPrep.active = true;
	_uiPrep.uiType = uiType;
	_uiPrep.returnScene = _sceneState.currentScene;
	_uiPrep.startMillis = g_system->getMillis();

	SceneChangeDescription desc;
	desc.sceneID = (uint16)prepSceneID;
	desc.frameID = 0;
	desc.verticalOffset = 0;
	changeScene(desc);
}

void Scene::finishUIPrepScene() {
	if (!_uiPrep.active) {
		return;
	}

	_uiPrep.active = false;

	// Restore the scene we were in when the popup was opened, keeping its sound.
	SceneChangeDescription ret = _uiPrep.returnScene;
	ret.continueSceneSound = kContinueSceneSound;
	changeScene(ret);

	// Open the popup whose prep scene just populated its content.
	switch (_uiPrep.uiType) {
	case kUITypeInventory:
		_inventoryPopup.open();
		break;
	case kUITypeNotebook:
		_notebookPopup.open();
		break;
	case kUITypeCellphone:
		_cellPhonePopup.open();
		break;
	default:
		break;
	}
}

void Scene::setPlayerTime(Time time, byte relative) {
	auto *bootSummary = GetEngineData(BSUM);
	assert(bootSummary);

	if (relative == kRelativeClockBump) {
		// Relative, add the specified time to current playerTime. The originals wrap
		// a negative time around to the previous day, which no game script needs.
		if ((int64)(uint32)_timers.playerTime + (int32)(uint32)time < 0) {
			warning("Moving the player time back past 00:00 is not supported");
			return;
		}

		_timers.playerTime += time;
	} else if (bootSummary->endOfDayFlag != kEvNoEvent) {
		// Absolute, the clock only holds the time of the current day
		_timers.playerTime = time;
	} else {
		// Absolute, maintain days but replace hours and minutes
		_timers.playerTime = _timers.playerTime.getDays() * 86400000 + time;
	}

	_timers.playerTimeNextMinute = g_nancy->getTotalPlayTime() + bootSummary->playerTimeMinuteLength;
}

uint Scene::getPlayerTimeMinutes() const {
	auto *bootSummary = GetEngineData(BSUM);
	assert(bootSummary);

	if (bootSummary->endOfDayFlag != kEvNoEvent) {
		// Games with an end of day don't wrap the clock at midnight, so
		// staying up late keeps counting past 24:00
		return _timers.playerTime.getTotalHours() * 60 + _timers.playerTime.getMinutes();
	}

	return _timers.playerTime.getHours() * 60 + _timers.playerTime.getMinutes();
}

byte Scene::getPlayerTOD() const {
	if (g_nancy->getGameType() <= kGameTypeNancy1) {
		if (_timers.playerTime.getHours() >= 7 && _timers.playerTime.getHours() < 18) {
			return kPlayerDay;
		} else if (_timers.playerTime.getHours() >= 19 || _timers.playerTime.getHours() < 6) {
			return kPlayerNight;
		} else {
			return kPlayerDuskDawn;
		}
	} else if (g_nancy->getGameType() <= kGameTypeNancy5) {
		// nancy2 and up removed dusk/dawn
		if (_timers.playerTime.getHours() >= 6 && _timers.playerTime.getHours() < 18) {
			return kPlayerDay;
		} else {
			return kPlayerNight;
		}
	} else {
		// nancy6 added the day start/end times (in minutes) to BSUM
		auto *bootSummary = GetEngineData(BSUM);
		assert(bootSummary);

		uint minutes = getPlayerTimeMinutes();

		if (minutes >= bootSummary->dayStartMinutes && minutes <= bootSummary->dayEndMinutes) {
			return kPlayerDay;
		} else {
			return kPlayerNight;
		}
	}
}

void Scene::addItemToInventory(int16 id) {
	if (id == -1) {
		return;
	}

	if (_flags.items[id] == g_nancy->_false) {
		_flags.items[id] = g_nancy->_true;
		if (_flags.heldItem == id) {
			setHeldItem(-1);
		}

		g_nancy->_sound->playSound("BUOK");

		if (g_nancy->getGameType() <= kGameTypeNancy9) {
			_inventoryBox.addItem(id);
		} else {
			// Nancy 10+ has no always-visible inventory box; the popup renders
			// from the shared, save-persisted order list instead. Items are
			// inserted at the front, except that when the UIIV chunk opts in
			// (appendItemsWhileOpen) an item added while the popup is open goes
			// to the end. That's how the most recently dropped item ends up last.
			// Nancy16 uses the NDUI INVENTORY panel instead of the InventoryPopup
			const bool hasPopup = g_nancy->getGameType() < kGameTypeNancy16;

			bool addToBack = false;
			if (hasPopup && _inventoryPopup.isOpen()) {
				const UIIV *uiivData = GetEngineData(UIIV);
				addToBack = uiivData && uiivData->appendItemsWhileOpen;
			}

			Common::Array<int16> &order = _inventoryBox.getOrder();
			for (uint i = 0; i < order.size(); ++i) {
				if (order[i] == id) {
					order.remove_at(i);
					break;
				}
			}
			if (addToBack) {
				order.push_back(id);
			} else {
				order.insert_at(0, id);
			}

			if (_inventoryPanel) {
				// The scrollbar's panel updates its thumb on its own next frame
				_inventoryPanel->refreshInventory();
			}

			if (hasPopup && _inventoryPopup.isOpen()) {
				_inventoryPopup.refreshGrid();
			} else if (_taskbar) {
				_taskbar->setNotification(kTaskButtonInventory, 0);
			}
		}
	}
}

void Scene::removeItemFromInventory(int16 id, bool pickUp) {
	if (id == -1) {
		return;
	}

	if (_flags.items[id] == g_nancy->_true || getHeldItem() == id) {
		_flags.items[id] = g_nancy->_false;

		if (pickUp) {
			setHeldItem(id);
			g_nancy->_sound->playSound("BUOK");
		} else if (getHeldItem() == id) {
			setHeldItem(-1);
			g_nancy->_sound->playSound("BUOK");
		}

		if (g_nancy->getGameType() <= kGameTypeNancy9) {
			_inventoryBox.removeItem(id);
		} else {
			Common::Array<int16> &order = _inventoryBox.getOrder();
			for (uint i = 0; i < order.size(); ++i) {
				if (order[i] == id) {
					order.remove_at(i);
					break;
				}
			}

			if (_inventoryPanel) {
				_inventoryPanel->refreshInventory();
			}

			if (g_nancy->getGameType() < kGameTypeNancy16 && _inventoryPopup.isOpen()) {
				_inventoryPopup.refreshGrid();
			}
		}
	}
}

void Scene::addItemToCharacterInventory(uint characterIndex, int16 id) {
	if (id == -1) {
		return;
	}

	if (characterIndex == g_nancy->getPlayerCharacter()) {
		if (hasItem(id) == g_nancy->_false) {
			addItemToInventory(id);
		}

		return;
	}

	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	if (!playerChar) {
		return;
	}

	PlayerCharacterData::Inventory &inventory = playerChar->getInventory(characterIndex);

	const uint numItems = g_nancy->getStaticData().numItems;
	inventory.items.resize(numItems, g_nancy->_false);
	inventory.disabledItems.resize(numItems, 0);

	if ((uint)id >= inventory.items.size() || inventory.items[id] == g_nancy->_true ||
			inventory.heldItem == id) {
		return;
	}

	inventory.items[id] = g_nancy->_true;

	// Handing an item to a character who hasn't been played yet makes their
	// inventory real; it would be thrown away on the next switch otherwise
	inventory.isValid = true;

	for (uint i = 0; i < inventory.order.size(); ++i) {
		if (inventory.order[i] == id) {
			inventory.order.remove_at(i);
			break;
		}
	}

	inventory.order.insert_at(0, id);
}

void Scene::removeItemFromCharacterInventory(uint characterIndex, int16 id, bool pickUp) {
	if (characterIndex == g_nancy->getPlayerCharacter()) {
		if (hasItem(id) == g_nancy->_true) {
			removeItemFromInventory(id, pickUp);
		}

		return;
	}

	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	if (!playerChar) {
		return;
	}

	// A character who hasn't been played yet owns nothing to take away
	PlayerCharacterData::Inventory &inventory = playerChar->getInventory(characterIndex);
	if (!inventory.isValid) {
		return;
	}

	if ((uint)id < inventory.items.size()) {
		inventory.items[id] = g_nancy->_false;
	}

	for (uint i = 0; i < inventory.order.size(); ++i) {
		if (inventory.order[i] == id) {
			inventory.order.remove_at(i);
			break;
		}
	}

	if (pickUp) {
		inventory.heldItem = id;
	} else if (inventory.heldItem == id) {
		inventory.heldItem = -1;
	}
}

int16 Scene::getCharacterHeldItem(uint characterIndex) {
	if (characterIndex == g_nancy->getPlayerCharacter()) {
		return getHeldItem();
	}

	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	return playerChar ? playerChar->getInventory(characterIndex).heldItem : -1;
}

void Scene::setCharacterHeldItem(uint characterIndex, int16 id) {
	if (characterIndex == g_nancy->getPlayerCharacter()) {
		setHeldItem(id);
		return;
	}

	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	if (!playerChar) {
		return;
	}

	PlayerCharacterData::Inventory &inventory = playerChar->getInventory(characterIndex);
	inventory.heldItem = id;

	if (id != -1) {
		// An item waiting in a character's hand makes their inventory real
		inventory.isValid = true;
	}
}

void Scene::returnCharacterHeldItem(uint characterIndex) {
	const int16 heldItem = getCharacterHeldItem(characterIndex);
	if (heldItem == -1) {
		return;
	}

	if (characterIndex == g_nancy->getPlayerCharacter()) {
		addItemToInventory(heldItem);
		return;
	}

	setCharacterHeldItem(characterIndex, -1);
	addItemToCharacterInventory(characterIndex, heldItem);
}

void Scene::giveItemToCharacter(uint characterIndex, int16 id, bool intoHand, bool forceIntoHand) {
	if (id == -1) {
		return;
	}

	if (!intoHand) {
		addItemToCharacterInventory(characterIndex, id);
		return;
	}

	const int16 heldItem = getCharacterHeldItem(characterIndex);
	if (heldItem == id) {
		// Already holding the item, e.g. when the scene reloads itself
		return;
	}

	if (heldItem != -1) {
		if (!forceIntoHand) {
			// Their hand is full and the record doesn't insist
			addItemToCharacterInventory(characterIndex, id);
			return;
		}

		returnCharacterHeldItem(characterIndex);
	}

	// Into the hand, out of the inventory if that is where the item was
	if (hasCharacterItem(characterIndex, id) == g_nancy->_true) {
		removeItemFromCharacterInventory(characterIndex, id, true);
	} else {
		setCharacterHeldItem(characterIndex, id);
	}
}

void Scene::setCharacterItemDisabledState(uint characterIndex, int16 id, byte state) {
	if (characterIndex == g_nancy->getPlayerCharacter()) {
		setItemDisabledState(id, state);
		return;
	}

	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	if (!playerChar || id < 0) {
		return;
	}

	// Unlike an item, a disabled state doesn't make a character's inventory
	// real: one who has never been played is set up from scratch when they are
	PlayerCharacterData::Inventory &inventory = playerChar->getInventory(characterIndex);
	inventory.disabledItems.resize(g_nancy->getStaticData().numItems, 0);

	if ((uint)id < inventory.disabledItems.size()) {
		inventory.disabledItems[id] = state;
	}
}

void Scene::setHeldItem(int16 id) {
	_flags.heldItem = id; g_nancy->_cursor->setCursorItemID(id);
}

void Scene::setNoHeldItem() {
	if (getHeldItem() != -1) {
		addItemToInventory(getHeldItem());
	}
}

byte Scene::hasItem(int16 id) const {
	if (getHeldItem() == id) {
		return g_nancy->_true;
	} else if (id >= 0 && (uint)id < _flags.items.size()) {
		return _flags.items[id];
	} else {
		// Some scripts check for item IDs past the end of the inventory. The
		// original reads those out of bounds and gets a zero, so reporting the
		// item as missing matches it.
		debug(2, "Scene::hasItem: out-of-range id %d (items.size=%u)", id,
			  (uint)_flags.items.size());
		return g_nancy->_false;
	}
}

byte Scene::hasCharacterItem(uint characterIndex, int16 id) {
	if (characterIndex == g_nancy->getPlayerCharacter()) {
		return hasItem(id);
	}

	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	if (!playerChar) {
		return g_nancy->_false;
	}

	const PlayerCharacterData::Inventory &inventory = playerChar->getInventory(characterIndex);
	if (inventory.heldItem == id) {
		return g_nancy->_true;
	}

	if (id >= 0 && (uint)id < inventory.items.size()) {
		return inventory.items[id];
	}

	return g_nancy->_false;
}

int32 Scene::getCharacterUIResource(uint characterIndex, uint index) {
	if (characterIndex == g_nancy->getPlayerCharacter()) {
		return getUIResource(index);
	}

	auto *resourceData = (UIResourceData *)getPuzzleData(UIResourceData::getTag());
	if (!resourceData) {
		return 0;
	}

	// A character who hasn't been played yet has no resources of their own yet
	const Common::Array<int32> &characterSet = resourceData->getCharacterValues(characterIndex);
	return index < characterSet.size() ? characterSet[index] : 0;
}

void Scene::installInventorySoundOverride(byte command, const SoundDescription &sound,
		const Common::String &caption, uint16 itemID, byte characterIndex) {
	InventorySoundOverride newOverride;

	// An override can be installed on a character who isn't being played, so it
	// waits in their own set until they are
	uint targetCharacter = characterIndex < kMaxPlayerCharacters ?
		characterIndex : MIN<uint>(g_nancy->getPlayerCharacter(), kMaxPlayerCharacters - 1);
	Common::HashMap<uint16, InventorySoundOverride> &overrides = _inventorySoundOverrides[targetCharacter];

	switch (command) {
	case kInvSoundOverrideCommandNoSound :
		// Make the sound silent
		newOverride.sound = sound;
		newOverride.sound.name = "NO SOUND";
		newOverride.caption = caption; // Assumes the caption will be empty
		overrides.setVal(itemID, newOverride);
		break;
	case kInvSoundOverrideCommandNewSound :
		newOverride.sound = sound;
		newOverride.caption = caption;
		overrides.setVal(itemID, newOverride);
		break;
	case kInvSoundOverrideCommandICant :
		// Make the sound the default "I can't use that here"
		newOverride.isDefault = true;
		overrides.setVal(itemID, newOverride);
		break;
	case kInvSoundOverrideCommandTurnOff :
		// Remove any previous override
		overrides.erase(itemID);
		break;
	default :
		return;
	}
}

// Nancy9 and newer no longer store the "can't" caption alongside the sound.
// Instead, the caption is looked up in the CVTX text chunks by the played
// sound's name: the narration/observations (AUTOTEXT) chunk is searched first,
// then the conversation (CONVO) chunk. Games that predate the change have no
// text chunks, so they always end up with the caption stored in the data.
static Common::String getSoundSubtitle(const Common::String &soundName, const Common::String &fallback) {
	if (!soundName.empty() && !soundName.equalsIgnoreCase("NO SOUND")) {
		const CVTX *autotext = (const CVTX *)g_nancy->getEngineData("AUTOTEXT");
		if (autotext) {
			Common::String text = autotext->texts.getValOrDefault(soundName, "");
			if (!text.empty()) {
				return text;
			}
		}

		const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
		if (convo) {
			Common::String text = convo->texts.getValOrDefault(soundName, "");
			if (!text.empty()) {
				return text;
			}
		}
	}

	return fallback;
}

Common::HashMap<uint16, Scene::InventorySoundOverride> &Scene::activeSoundOverrides() {
	return _inventorySoundOverrides[MIN<uint>(g_nancy->getPlayerCharacter(), kMaxPlayerCharacters - 1)];
}

// Nancy15 moved the "can't" responses out of the inventory data and into the
// active player character's PUIV bank: one group of interchangeable sounds per
// item, keyed by item ID, plus the character's generic response, which is the
// one the InventorySoundOverride record installs for its default command. They
// all share the bank's channel and volume.
bool Scene::getPlayerCantSound(int16 itemID, SoundDescription &sound) const {
	auto *puivData = GetEngineData(PUIV);
	if (!puivData) {
		return false;
	}

	Common::String name;

	if (itemID < 0) {
		name = puivData->name;
	} else {
		for (uint i = 0; i < puivData->soundGroups.size(); ++i) {
			const PUIV::SoundGroup &group = puivData->soundGroups[i];
			if (group.tag == itemID && group.variants.size()) {
				// The variants are interchangeable, so one is picked at random
				name = group.variants[g_nancy->_randomSource->getRandomNumber(group.variants.size() - 1)];
				break;
			}
		}
	}

	if (name.empty() || name.equalsIgnoreCase("NO SOUND")) {
		return false;
	}

	sound.name = name;
	sound.channelID = puivData->channelID;
	sound.volume = puivData->volume;

	return true;
}

void Scene::playPlayerCantSound(int16 itemID) {
	auto *inventoryData = GetEngineData(INV);
	assert(inventoryData);

	SoundDescription sound;
	Common::String caption;

	if (itemID >= 0 && activeSoundOverrides().contains(itemID)) {
		InventorySoundOverride &override = activeSoundOverrides()[itemID];

		if (override.isDefault) {
			// Back to the character's generic response
			if (!getPlayerCantSound(-1, sound)) {
				return;
			}
		} else {
			sound = override.sound;
			caption = override.caption;
		}
	} else if (!getPlayerCantSound(itemID, sound)) {
		// An item with no response of its own stays silent
		return;
	}

	if (sound.name.empty() || sound.name.equalsIgnoreCase("NO SOUND")) {
		// Silenced by an override
		return;
	}

	// One response at a time: if the bank's channel is busy, the sound (and its
	// caption) are left alone instead of being restarted or overlapped
	if (g_nancy->_sound->isSoundPlaying(sound.channelID)) {
		return;
	}

	if (ConfMan.getBool("subtitles")) {
		_textbox.clear();
	}

	g_nancy->_sound->loadSound(sound);
	g_nancy->_sound->playSound(sound);

	if (ConfMan.getBool("subtitles")) {
		_textbox.addTextLine(getSoundSubtitle(sound.name, caption), inventoryData->captionAutoClearTime);
	}
}

void Scene::playItemCantSound(int16 itemID, bool notHoldingSound) {
	// Improvement: nancy2 never shows the caption text, even though it exists in the data; we show it
	auto *inventoryData = GetEngineData(INV);

	// Nancy16 has no INV chunk, and INVD has no "can't" sounds
	if (!inventoryData) {
		return;
	}

	// Nancy15 keeps no "can't" sounds in the inventory data; they come from the
	// player character's own bank instead
	if (g_nancy->getGameType() >= kGameTypeNancy15) {
		playPlayerCantSound(itemID);
		return;
	}

	// Nancy9 and newer play every "can't" sound on the same dedicated sound-effects
	// channel as the default "can't" sound. If one is already playing, leave it (and
	// the current caption) alone instead of restarting it or overlapping a new one.
	if (g_nancy->getGameType() >= kGameTypeNancy9 &&
			g_nancy->_sound->isSoundPlaying(inventoryData->cantSound.channelID)) {
		return;
	}

	if (ConfMan.getBool("subtitles") && g_nancy->getGameType() >= kGameTypeNancy2) {
		_textbox.clear();
	}

	if (itemID < 0) {
		if (inventoryData->cantSound.name.size()) {
			// Play default "can't" inside inventory data (if present)
			g_nancy->_sound->loadSound(inventoryData->cantSound);
			g_nancy->_sound->playSound(inventoryData->cantSound);

			if (ConfMan.getBool("subtitles")) {
				_textbox.addTextLine(getSoundSubtitle(inventoryData->cantSound.name, inventoryData->cantText),
					inventoryData->captionAutoClearTime);
			}
		} else {
			// TVD and nancy1 contain no sound data in INV, and have no captions
			g_nancy->_sound->playSound("CANT");
		}
	} else if ((uint)itemID < _flags.items.size()) {
		if (activeSoundOverrides().contains(itemID)) {
			// We have an override installed
			InventorySoundOverride &override = activeSoundOverrides()[itemID];
			if (!override.isDefault) {
				// Not set to the default sound, play the override
				g_nancy->_sound->loadSound(override.sound);
				g_nancy->_sound->playSound(override.sound);

				if (ConfMan.getBool("subtitles")) {
					// The caption is looked up in the CVTX text chunks by the
					// override's sound name; the stored caption is the fallback
					_textbox.addTextLine(getSoundSubtitle(override.sound.name, override.caption),
						inventoryData->captionAutoClearTime);
				}
				return;
			} else {
				// Play the default "I can't" sound
				const INV::ItemDescription item = inventoryData->itemDescriptions[itemID];

				if (notHoldingSound && item.cantSoundNotHolding.name.size()) {
					// This field only exists in nancy2
					g_nancy->_sound->loadSound(item.cantSoundNotHolding);
					g_nancy->_sound->playSound(item.cantSoundNotHolding);

					if (ConfMan.getBool("subtitles")) {
						_textbox.addTextLine(item.cantTextNotHolding, inventoryData->captionAutoClearTime);
					}
				} else if (inventoryData->cantSound.name.size()) {
					g_nancy->_sound->loadSound(inventoryData->cantSound);
					g_nancy->_sound->playSound(inventoryData->cantSound);

					if (ConfMan.getBool("subtitles")) {
						_textbox.addTextLine(getSoundSubtitle(inventoryData->cantSound.name, inventoryData->cantText),
							inventoryData->captionAutoClearTime);
					}
				} else {
					// Should be unreachable
					g_nancy->_sound->playSound("CANT");
				}

				return;
			}
		}

		// No override installed
		const INV::ItemDescription item = inventoryData->itemDescriptions[itemID];

		if (item.cantSound.name.size()) {
			// The inventory data contains a custom "can't" sound for this item
			SoundDescription cantSound = item.cantSound;
			Common::String cantText = item.cantText;

			// Nancy9 and newer store up to three "can't" sound variants per item
			// (the default in slot 0, plus two optional alternatives), but no longer
			// store the playback settings or caption alongside them.
			if (g_nancy->getGameType() >= kGameTypeNancy9) {
				// Count the valid alternatives and pick one at random, including
				// the default in slot 0
				uint numChoices = 1;
				while (numChoices < 3 && item.cantSounds[numChoices].name.size() &&
						!item.cantSounds[numChoices].name.equalsIgnoreCase("NO SOUND")) {
					++numChoices;
				}

				uint soundIndex = g_nancy->_randomSource->getRandomNumber(numChoices - 1);

				// These sounds share the playback settings (channel, volume, ...) of
				// the default "can't" sound, so they play on a sound-effects channel
				// without cutting off the background music
				cantSound = inventoryData->cantSound;
				cantSound.name = item.cantSounds[soundIndex].name;

				// The caption is looked up in the CVTX text chunks by the sound's
				// name; the item's own caption field is only a fallback
				cantText = getSoundSubtitle(cantSound.name, item.cantTexts[soundIndex]);
			}

			g_nancy->_sound->loadSound(cantSound);
			g_nancy->_sound->playSound(cantSound);

			if (ConfMan.getBool("subtitles")) {
				_textbox.addTextLine(cantText, inventoryData->captionAutoClearTime);
			}
		} else if (inventoryData->cantSound.name.size()) {
			// No custom sound, play default "can't" inside inventory data. Should (?) be unreachable
			g_nancy->_sound->loadSound(inventoryData->cantSound);
			g_nancy->_sound->playSound(inventoryData->cantSound);

			if (ConfMan.getBool("subtitles")) {
				_textbox.addTextLine(getSoundSubtitle(inventoryData->cantSound.name, inventoryData->cantText),
					inventoryData->captionAutoClearTime);
			}
		} else {
			// TVD and nancy1 contain no sound data in INV, and have no captions
			g_nancy->_sound->playSound("CANT");
		}
	}
}

int16 Scene::eventFlagToIndex(int16 label) const {
	// Nancy3+ number their event flags from 1000. Nancy12 then split the flags
	// into two ranges: the generic engine flags kept the 1xxx numbering, while
	// the game-specific flags were renumbered from 2000. Subtracting a flat 1000
	// from any 1xxx/2xxx label keeps the two ranges in separate, non-overlapping
	// regions of the flags array (generic flags in [0, 1000), game-specific flags
	// in [1000, ...)).
	if (label >= 1000) {
		label -= 1000;
	}

	return label;
}

// The per-scene scratch flags belong to whichever flow is executing; see streams.h
int Scene::scratchFlagIndex(int16 label) const {
	if (!_streams.executing()) {
		return -1;
	}

	const Common::Array<uint16> &generic = g_nancy->getStaticData().genericEventFlags;
	if (generic.empty()) {
		return -1;
	}

	// The table is contiguous, so the ends bound it
	const int16 first = (int16)generic[0];
	const int16 last = (int16)generic[generic.size() - 1];
	if (label < first || label > last) {
		return -1;
	}

	return label - first;
}

void Scene::setEventFlag(int16 label, byte flag) {
	const int scratch = scratchFlagIndex(label);
	if (scratch >= 0) {
		_streams.executing()->setScratchFlag((uint)scratch, flag);
		return;
	}

	label = eventFlagToIndex(label);

	if (label > kEvNoEvent && (uint)label < g_nancy->getStaticData().numEventFlags) {
		_flags.eventFlags[label] = flag;
	}
}

void Scene::setEventFlag(FlagDescription eventFlag) {
	setEventFlag(eventFlag.label, eventFlag.flag);
}

bool Scene::getEventFlag(int16 label, byte flag) const {
	const int scratch = scratchFlagIndex(label);
	if (scratch >= 0) {
		return _streams.executing()->getScratchFlag((uint)scratch) == flag;
	}

	label = eventFlagToIndex(label);

	if (label > kEvNoEvent && (uint)label < g_nancy->getStaticData().numEventFlags) {
		return _flags.eventFlags[label] == flag;
	} else {
		return false;
	}
}

bool Scene::getEventFlag(FlagDescription eventFlag) const {
	return getEventFlag(eventFlag.label, eventFlag.flag);
}

// On first use, seed each resource value from the UIRC boot chunk. After a save
// is loaded `seeded` is already true, so the restored values are kept.
static void seedUIResourceData(UIResourceData *data) {
	if (!data || data->seeded) {
		return;
	}

	const UIRC *uirc = GetEngineData(UIRC)

	data->seeded = true;
	if (uirc) {
		data->values.resize(uirc->items.size());
		for (uint i = 0; i < uirc->items.size(); ++i) {
			data->values[i] = uirc->items[i].startingValue;
		}
	}
}

// The character being played keeps their resources in `values`; the other
// protagonists' sets are parked in `characterValues` until they are played.
// A character who has never been played starts from the UIRC starting values.
static Common::Array<int32> *characterResourceValues(UIResourceData *data, byte characterIndex) {
	if (!data) {
		return nullptr;
	}

	if (characterIndex == kPlayerCharacterActive || characterIndex == g_nancy->getPlayerCharacter()) {
		return &data->values;
	}

	if (characterIndex >= kMaxPlayerCharacters) {
		warning("UI resource change for unknown player character %u, using the active one", characterIndex);
		return &data->values;
	}

	Common::Array<int32> &stored = data->getCharacterValues(characterIndex);
	if (stored.empty()) {
		stored.resize(data->values.size(), 0);

		const UIRC *uirc = GetEngineData(UIRC)
		if (uirc) {
			for (uint i = 0; i < stored.size() && i < uirc->items.size(); ++i) {
				stored[i] = uirc->items[i].startingValue;
			}
		}
	}

	return &stored;
}

int32 Scene::getUIResource(uint index, byte characterIndex) {
	UIResourceData *data = (UIResourceData *)getPuzzleData(UIResourceData::getTag());
	seedUIResourceData(data);
	Common::Array<int32> *values = characterResourceValues(data, characterIndex);
	if (!values || index >= values->size()) {
		return 0;
	}
	return (*values)[index];
}

void Scene::setUIResource(uint index, int32 value, byte characterIndex) {
	UIResourceData *data = (UIResourceData *)getPuzzleData(UIResourceData::getTag());
	seedUIResourceData(data);
	Common::Array<int32> *values = characterResourceValues(data, characterIndex);
	if (!values || index >= values->size()) {
		return;
	}

	// Nancy 14 added a per-resource maximum. It guards the fixed-width display
	// rather than capping the resource: a value above it empties the resource
	// outright instead of being clamped to it.
	if (g_nancy->getGameType() >= kGameTypeNancy14) {
		const UIRC *uirc = GetEngineData(UIRC)
		if (uirc && index < uirc->items.size() && value > (int32)uirc->items[index].maxValue) {
			value = 0;
		}
	}

	(*values)[index] = MAX<int32>(value, 0);
}

// Nancy 11+ AR 30/31 store the "player scrolling disabled" state in an event
// flag (eventData[0x21] in the original). It persists across scenes and is
// saved/restored together with the rest of the event flags. Nancy12 shifted the
// engine's generic flag numbering up by 10, moving this flag from 1033 to 1043.
// Nancy15 widened the scene-cleared generic block over 1043 and moved the flag
// to 1002.
static int16 playerScrollingDisabledFlag() {
	if (g_nancy->getGameType() >= kGameTypeNancy15) {
		return 1002;
	}

	return g_nancy->getGameType() >= kGameTypeNancy12 ? 1043 : 1033;
}

void Scene::setPlayerScrolling(bool enabled) {
	setEventFlag(playerScrollingDisabledFlag(), enabled ? g_nancy->_false : g_nancy->_true);
}

bool Scene::getPlayerScrolling() const {
	// Player-scrolling control only exists from Nancy 11; older games must not
	// consult this flag, since they may use that event-flag index for something else
	if (g_nancy->getGameType() < kGameTypeNancy11) {
		return true;
	}

	return !getEventFlag(playerScrollingDisabledFlag(), g_nancy->_true);
}

void Scene::setLogicCondition(int16 label, byte flag) {
	if (label > kEvNoEvent) {
		if (label >= 2000) {
			// In nancy3 and onwards logic conditions begin from 2000
			label -= 2000;
		}

		if (label > kEvNoEvent && (uint)label < 30) {
			_flags.logicConditions[label].flag = flag;
			_flags.logicConditions[label].timestamp = g_nancy->getTotalPlayTime();
		}
	}
}

bool Scene::getLogicCondition(int16 label, byte flag) const {
	if (label > kEvNoEvent) {
		return _flags.logicConditions[label].flag == flag;
	} else {
		return false;
	}
}

void Scene::clearLogicConditions() {
	for (auto &cond : _flags.logicConditions) {
		cond.flag = g_nancy->_false;
		cond.timestamp = 0;
	}
}

void Scene::useHint(uint16 characterID, uint16 hintID) {
	if (_lastHintID != hintID || _lastHintCharacter != characterID) {
		_hintsRemaining[_difficulty] += g_nancy->getStaticData().hints[characterID][hintID].hintWeight;
		_lastHintCharacter = characterID;
		_lastHintID = hintID;
	}
}

void Scene::setConversationUIVisible(bool visible) {
	for (uint i = 0; i < _conversationPanels.size(); ++i) {
		_conversationPanels[i]->setVisible(visible);
	}
}

void Scene::applyNDUICommand(const Common::String &target, uint32 commandID) {
	// The records show/hide the meter only, so its dialog has to follow it
	if (target.equalsIgnoreCase("DanceMeter") &&
			(commandID == kNDUICommandShow || commandID == kNDUICommandHide)) {
		applyNDUICommand("DanceDialog", commandID);
	}
	// Both lists are rebuilt from the event flags before they are shown
	if (commandID == kNDUICommandShow) {
		if (target.equalsIgnoreCase("JournalDialog")) {
			refreshJournal();
		} else if (target.equalsIgnoreCase("TasklistDialog")) {
			refreshTasklist();
		}
	} else if (commandID == kNDUICommandHide && target.equalsIgnoreCase("JournalDialog")) {
		// The pages are hidden by the dialog's own bindings; reset the selection too
		_journalPage = -1;
	}

	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		if (_nduiPanels[i]->applyCommand(target, commandID)) {
			return;
		}
	}
}

// The scrollbar's thumb is updated by its own panel in NDUIPanel::updateGraphics
void Scene::applyNDUIScroll(const Common::String &target, int delta) {
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		if (_nduiPanels[i]->ownsControl(target)) {
			_nduiPanels[i]->listScrollBy(delta);
		}
	}
}

// Only visible panels are considered, which picks the journal page currently
// shown out of the four a journal scrollbar names.
bool Scene::nduiScrollModel(const Common::String &target, int &position, int &page, int &range) {
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		if (!_nduiPanels[i]->ownsControl(target) || !_nduiPanels[i]->isVisible()) {
			continue;
		}

		if (_nduiPanels[i]->scrollModel(position, page, range)) {
			return true;
		}
	}

	return false;
}

// Nancy16 keeps the journal and task list in ciftree members named
// <LIST>_<character>, one action record per entry, whose dependencies decide
// whether it is shown. The members for every PCUI character are concatenated,
// with the "PUI_" prefix of the tree name stripped.
static Common::Array<Common::String> journalListMembers(const char *base) {
	Common::Array<Common::String> out;
	const PCUI *pcui = GetEngineData(PCUI);
	if (!pcui) {
		return out;
	}

	for (uint i = 0; i < pcui->characters.size(); ++i) {
		Common::String suffix = pcui->characters[i].imageName;
		if (suffix.hasPrefixIgnoreCase("PUI_")) {
			suffix = Common::String(suffix.c_str() + 4);
		}

		if (suffix.empty()) {
			continue;
		}

		out.push_back(Common::String::format("%s_%s", base, suffix.c_str()));
	}

	return out;
}

// The entries are action records, so the dependencies are evaluated by the
// ActionManager. They are never executed; the caller reads their fields.
Common::Array<Action::ActionRecord *> Scene::readListMember(const char *base) {
	Common::Array<Action::ActionRecord *> out;
	const Common::Array<Common::String> members = journalListMembers(base);

	for (uint m = 0; m < members.size(); ++m) {
		IFF *iff = g_nancy->_resource->loadIFF(Common::Path(members[m]));
		if (!iff) {
			continue;
		}

		Common::SeekableReadStream *chunk = nullptr;
		for (uint n = 0; chunk = iff->getChunkStream("ACT", n), chunk != nullptr; ++n) {
			Action::ActionRecord *record = Action::ActionManager::createAndLoadNewRecord(*chunk);
			delete chunk;

			if (!record) {
				continue;
			}

			_actionManager.processDependency(record->_dependencies, *record, true);
			if (record->_dependencies.satisfied) {
				out.push_back(record);
			} else {
				delete record;
			}
		}

		delete iff;
	}

	return out;
}

void Scene::refreshTasklist() {
	if (!_tasklistPanel) {
		return;
	}

	const CVTX *autotext = (const CVTX *)g_nancy->getEngineData("AUTOTEXT");
	Common::Array<NDUIPanel::ListRow> rows;
	Common::Array<Action::ActionRecord *> records = readListMember("TASKLIST");

	for (uint i = 0; i < records.size(); ++i) {
		auto *entry = dynamic_cast<Action::Nancy16TaskEntry *>(records[i]);
		if (entry && autotext && autotext->texts.contains(entry->_stringID)) {
			NDUIPanel::ListRow row;
			row.text = autotext->texts[entry->_stringID];
			// Entries are ticked by their completion flag, not by the player
			row.checked = getEventFlag((int16)entry->_completionFlag, g_nancy->_true);
			rows.push_back(row);
		}

		delete records[i];
	}

	_tasklistPanel->setListRows("TasklistDialog", rows);
}

void Scene::refreshJournal() {
	if (!_journalPanel) {
		return;
	}

	const CVTX *autotext = (const CVTX *)g_nancy->getEngineData("AUTOTEXT");

	Common::Array<Common::Array<NDUIPanel::ListRow> > pages;
	pages.resize(kNumJournalPages);

	Common::Array<Action::ActionRecord *> records = readListMember("JOURNAL");
	for (uint i = 0; i < records.size(); ++i) {
		auto *entry = dynamic_cast<Action::Nancy16JournalEntry *>(records[i]);
		if (entry && autotext && autotext->texts.contains(entry->_stringID)) {
			// The record names its page by the panel's root control (VENJH1..VENJH4)
			for (uint p = 0; p < kNumJournalPages; ++p) {
				if (_journalPagePanels[p] &&
						_journalPagePanels[p]->getPanelName().equalsIgnoreCase(entry->_page)) {
					NDUIPanel::ListRow row;
					row.text = autotext->texts[entry->_stringID];
					pages[p].push_back(row);
					break;
				}
			}
		}

		delete records[i];
	}

	// The heading list. Each caption is the page's root control name looked up in
	// AUTOTEXT. Empty pages are still listed.
	Common::Array<NDUIPanel::ListRow> headings;
	for (uint p = 0; p < kNumJournalPages; ++p) {
		NDUIPanel::ListRow row;
		if (_journalPagePanels[p]) {
			const Common::String name = _journalPagePanels[p]->getPanelName();
			row.text = (autotext && autotext->texts.contains(name)) ? autotext->texts[name] : name;
			_journalPagePanels[p]->setListRows(name, pages[p]);
		}

		headings.push_back(row);
	}

	_journalPanel->setListRows("JournalItems", headings);
	_journalPanel->setListSelection(_journalPage);
}

void Scene::selectJournalPage(int index) {
	_journalPage = (index >= 0 && index < (int)kNumJournalPages) ? index : -1;

	for (uint p = 0; p < kNumJournalPages; ++p) {
		if (!_journalPagePanels[p]) {
			continue;
		}

		applyNDUICommand(_journalPagePanels[p]->getPanelName(),
			((int)p == _journalPage) ? kNDUICommandShow : kNDUICommandHide);
	}

	if (_journalPanel) {
		_journalPanel->setListSelection(_journalPage);
	}
}

NDUIPanel *Scene::findNDUIPanelWithControl(const Common::String &name) {
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		if (_nduiPanels[i]->ownsControl(name)) {
			return _nduiPanels[i];
		}
	}

	return nullptr;
}

// The game has no restart command. "New game" loads the "Start_Game" checkpoint
// that the first scene writes, so it fails if that scene has never run.
void Scene::startNewGame() {
	static const char *const kStartGameSaveName = "Start_Game";

	const int slot = g_nancy->findNamedSaveSlot(kStartGameSaveName);
	if (slot < 0) {
		warning("NDUI: 'new game' needs the '%s' checkpoint, which this profile has "
			"never written - the game's first scene writes it, so start the game "
			"normally once", kStartGameSaveName);
		return;
	}

	debugC(1, kDebugEngine, "NDUI 'new game' -> named load '%s' from slot %d",
		kStartGameSaveName, slot);

	// Queued as a player load, which unlike a named load does not wait for dialogue to end
	g_nancy->requestSlotLoad(slot);
}

const char *const Scene::kNDUIModalRoots[] = {
	"OptionsDialog", "LoadDialog", "SaveDialog", "SaveQueryDialog"
};

bool Scene::isNDUIDialogOpen() const {
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		const Common::String root = _nduiPanels[i]->getPanelName();
		for (uint n = 0; n < ARRAYSIZE(kNDUIModalRoots); ++n) {
			if (root.equalsIgnoreCase(kNDUIModalRoots[n]) && _nduiPanels[i]->isVisible()) {
				return true;
			}
		}
	}

	return false;
}

// The options screen controls send SetValue/Notify to "Engine_*" pseudo-targets:
//   Engine_VoiceVolume    speech_volume
//   Engine_FXVolume       sfx_volume
//   Engine_MusicVolume    music_volume
//   Engine_ClosedCaption  subtitles
//   Engine_Matte          COLR chunk id
//   Engine_GameWindow     0 standard/CRT, 1 windowed, 2 widescreen/LCD
//   Engine_Font           font scale percentage
// SetValue applies the setting, Notify also writes the configuration out.
bool Scene::applyNDUISetting(const Common::String &target, uint32 commandID, double value) {
	if (commandID != kNDUICommandSetValue && commandID != kNDUICommandNotify) {
		return false;
	}

	const Common::String &domain = ConfMan.getActiveDomainName();
	const int rounded = (int)(value + (value < 0 ? -0.5 : 0.5));

	// The three volume sliders
	static const struct { const char *engineTarget; const char *confKey; } kVolumes[] = {
		{ "Engine_VoiceVolume",	"speech_volume" },
		{ "Engine_FXVolume",	"sfx_volume" },
		{ "Engine_MusicVolume",	"music_volume" }
	};

	for (uint i = 0; i < ARRAYSIZE(kVolumes); ++i) {
		if (!target.equalsIgnoreCase(kVolumes[i].engineTarget)) {
			continue;
		}

		// The slider's range is 0..100 and ConfMan's is 0..255.
		const int level = CLIP<int>(rounded, 0, 100) * Audio::Mixer::kMaxMixerVolume / 100;
		ConfMan.setInt(kVolumes[i].confKey, CLIP<int>(level, 0, Audio::Mixer::kMaxMixerVolume), domain);
		g_nancy->syncSoundSettings();

		if (commandID == kNDUICommandNotify) {
			debugC(1, kDebugEngine, "NDUI setting %s = %d (%s %d)",
				kVolumes[i].engineTarget, CLIP<int>(rounded, 0, 100),
				kVolumes[i].confKey, ConfMan.getInt(kVolumes[i].confKey));
			ConfMan.flushToDisk();
		}

		return true;
	}

	if (target.equalsIgnoreCase("Engine_ClosedCaption")) {
		ConfMan.setBool("subtitles", rounded != 0, domain);
		g_nancy->syncSoundSettings();

		if (commandID == kNDUICommandNotify) {
			debugC(1, kDebugEngine, "NDUI setting Engine_ClosedCaption = %d (subtitles)", rounded != 0);
			ConfMan.flushToDisk();
		}

		return true;
	}

	if (target.equalsIgnoreCase("Engine_Matte")) {
		auto *reg = (const FontRegistry *)g_nancy->getEngineData("FONTREG");
		const FontRegistry::ColourEntry *colour = reg ? reg->findColourByID((uint32)rounded) : nullptr;
		if (!colour) {
			warning("NDUI: matte colour %d has no COLR chunk, ignoring", rounded);
			return true;
		}

		ConfMan.setInt(kConfMatteColour, rounded, domain);
		g_nancy->_graphics->setMatteColour(colour->argb[kNDUIColorNormal]);
		g_nancy->_graphics->redrawAll();

		if (commandID == kNDUICommandNotify) {
			debugC(1, kDebugEngine, "NDUI setting Engine_Matte = COLR %d (%08x)",
				rounded, colour->argb[kNDUIColorNormal]);
			ConfMan.flushToDisk();
		}

		return true;
	}

	if (target.equalsIgnoreCase("Engine_Font")) {
		auto *reg = (const FontRegistry *)g_nancy->getEngineData("FONTREG");
		if (!reg || (uint)rounded == g_nancy->_ttfFonts.getScalePercent()) {
			return true;
		}

		ConfMan.setInt(kConfFontScale, rounded, domain);

		// Every face is rebuilt at the new size, so recompose all NDUI panels.
		// Scrollbar thumbs are brought up to date later in NDUIPanel::updateGraphics.
		g_nancy->_ttfFonts.init(reg, (uint)rounded);
		for (uint i = 0; i < _nduiPanels.size(); ++i) {
			_nduiPanels[i]->recompose();
		}

		if (commandID == kNDUICommandNotify) {
			debugC(1, kDebugEngine, "NDUI setting Engine_Font = %d%%", rounded);
			ConfMan.flushToDisk();
		}

		return true;
	}

	// Engine_GameWindow also takes Hide, which quits; that is handled elsewhere
	if (target.equalsIgnoreCase("Engine_GameWindow")) {
		const int mode = CLIP<int>(rounded, 0, 2);
		ConfMan.setInt(kConfWindowMode, mode, domain);

		// Mode 1 is windowed. Mode 0 (CRT) is mapped to stretch and mode 2
		// (widescreen) to fit.
		const bool wantFullscreen = (mode != 1);
		const char *stretch = (mode == 2) ? "fit" : "stretch";

		ConfMan.setBool("fullscreen", wantFullscreen, domain);
		if (g_system->hasFeature(OSystem::kFeatureStretchMode)) {
			ConfMan.set("stretch_mode", stretch, domain);
		}

		if (g_system->hasFeature(OSystem::kFeatureFullscreenMode)) {
			g_system->beginGFXTransaction();
			g_system->setFeatureState(OSystem::kFeatureFullscreenMode, wantFullscreen);
			if (g_system->hasFeature(OSystem::kFeatureStretchMode)) {
				g_system->setStretchMode(stretch);
			}

			g_system->endGFXTransaction();
			g_nancy->_graphics->redrawAll();
		}

		if (commandID == kNDUICommandNotify) {
			debugC(1, kDebugEngine, "NDUI setting Engine_GameWindow = mode %d (fullscreen %d, stretch '%s')",
				mode, (int)wantFullscreen, stretch);
			ConfMan.flushToDisk();
		}

		return true;
	}

	return false;
}

bool Scene::getNDUISettingValue(const Common::String &target, double &out) const {
	if (target.empty()) {
		return false;
	}

	static const struct { const char *engineTarget; const char *confKey; } kVolumes[] = {
		{ "Engine_VoiceVolume",	"speech_volume" },
		{ "Engine_FXVolume",	"sfx_volume" },
		{ "Engine_MusicVolume",	"music_volume" }
	};

	for (uint i = 0; i < ARRAYSIZE(kVolumes); ++i) {
		if (target.equalsIgnoreCase(kVolumes[i].engineTarget)) {
			const int level = CLIP<int>(ConfMan.getInt(kVolumes[i].confKey),
				0, Audio::Mixer::kMaxMixerVolume);
			out = (level * 100 + Audio::Mixer::kMaxMixerVolume / 2) / Audio::Mixer::kMaxMixerVolume;
			return true;
		}
	}

	if (target.equalsIgnoreCase("Engine_ClosedCaption")) {
		out = ConfMan.getBool("subtitles") ? 1.0 : 0.0;
		return true;
	}

	if (target.equalsIgnoreCase("Engine_Matte")) {
		out = ConfMan.getInt(kConfMatteColour);
		return true;
	}

	if (target.equalsIgnoreCase("Engine_Font")) {
		out = ConfMan.getInt(kConfFontScale);
		return true;
	}

	if (target.equalsIgnoreCase("Engine_GameWindow")) {
		// ScummVM's own fullscreen setting takes precedence
		int mode = CLIP<int>(ConfMan.getInt(kConfWindowMode), 0, 2);
		if (!ConfMan.getBool("fullscreen")) {
			mode = 1;
		} else if (mode == 1) {
			mode = 0;
		}

		out = mode;
		return true;
	}

	return false;
}

// The "Engine_*" pseudo-targets:
//   Engine_Loader     Show open, Hide close, Invoke select an entry, Notify commit
//   Engine_Saver      the same, for the save screen
//   Engine_SaveQuery  "Do you want to save first?": Show yes, Hide no, Clear dismiss
//   Engine_GameWindow Hide quits, SetValue picks a video mode, Set/Clear enable/disable
// Engine_Inv, Engine_Flags and Engine_Index are handled by NDUIPanel.
bool Scene::applyNDUIEngineCommand(const Common::String &target, uint32 commandID,
		const Common::String &paramString, double value) {
	const bool isLoader = target.equalsIgnoreCase("Engine_Loader");
	const bool isSaver = target.equalsIgnoreCase("Engine_Saver");

	// The options screen first, since Engine_GameWindow is also a setting
	if (applyNDUISetting(target, commandID, value)) {
		return true;
	}

	if (isLoader || isSaver) {
		const char *const rootName = isLoader ? "LoadDialog" : "SaveDialog";
		NDUIPanel *panel = findNDUIPanelWithControl(rootName);
		if (!panel) {
			return false;
		}

		switch (commandID) {
		case kNDUICommandShow:
			// The save list is read when the dialog is opened
			panel->saveLoadRefresh();
			_loaderNewGameSelected = false;

			if (isSaver) {
				// Default the save name to the current scene's description
				Common::String suggested = getSceneSummary().description;
				if (suggested.empty()) {
					suggested = Common::String::format("Scene %u", _sceneState.currentScene.sceneID);
				}

				panel->saveLoadSetName(suggested);
			}

			applyNDUICommand(rootName, kNDUICommandShow);
			return true;
		case kNDUICommandHide:
			applyNDUICommand(rootName, kNDUICommandHide);
			if (isSaver) {
				// Closing the save screen without saving cancels a pending quit
				applyNDUICommand("SaveQueryDialog", kNDUICommandHide);
				_quitAfterSave = false;
			}

			return true;
		case kNDUICommandInvoke:
			// Selecting an entry. The panel records list clicks itself; the "New" button
			// selects the "_StartNewGame_" pseudo-entry, which Notify then commits.
			_loaderNewGameSelected = isLoader && paramString.equalsIgnoreCase("_StartNewGame_");
			return true;
		case kNDUICommandNotify: {
			// Commit.
			if (isLoader) {
				if (_loaderNewGameSelected) {
					_loaderNewGameSelected = false;
					applyNDUICommand(rootName, kNDUICommandHide);
					startNewGame();
					return true;
				}

				const int slot = panel->saveLoadSelectedSlot();
				if (slot < 0) {
					return true;	// nothing picked; the dialog stays up
				}

				applyNDUICommand(rootName, kNDUICommandHide);
				g_nancy->requestSlotLoad(slot);
				return true;
			}

			const Common::String name = panel->saveLoadName();
			if (name.empty()) {
				return true;
			}

			// Saves are keyed by name, so saving under an existing name overwrites it.
			// The original has no overwrite prompt.
			int slot = -1;
			const int firstReserved = g_nancy->firstNamedSaveSlot();
			const int autosaveSlot = g_nancy->getAutosaveSlot();
			SaveStateList saves = g_nancy->getMetaEngine()->listSaves(ConfMan.getActiveDomainName().c_str());
			for (const SaveStateDescriptor &save : saves) {
				if (save.getSaveSlot() >= 0 && save.getSaveSlot() < firstReserved &&
						save.getSaveSlot() != autosaveSlot &&
						save.getDescription().equalsIgnoreCase(name)) {
					slot = save.getSaveSlot();
					break;
				}
			}

			if (slot < 0) {
				// A new name takes the lowest free slot below the reserved band
				Common::Array<bool> taken(firstReserved);
				for (const SaveStateDescriptor &save : saves) {
					if (save.getSaveSlot() >= 0 && save.getSaveSlot() < firstReserved) {
						taken[save.getSaveSlot()] = true;
					}
				}

				for (int n = 0; n < firstReserved; ++n) {
					if (!taken[n] && n != autosaveSlot) {
						slot = n;
						break;
					}
				}
			}

			if (slot < 0) {
				warning("NDUI save '%s' skipped: no free slot", name.c_str());
				return true;
			}

			// Close the dialog before saving, since the panels' visibility is saved too
			applyNDUICommand(rootName, kNDUICommandHide);

			const Common::Error err = g_nancy->saveGameState(slot, name, false);
			if (err.getCode() != Common::kNoError) {
				warning("NDUI save '%s' to slot %d failed: %s",
					name.c_str(), slot, err.getDesc().c_str());
				return true;
			}

			debugC(1, kDebugEngine, "NDUI save '%s' written to slot %d", name.c_str(), slot);

			// The save was requested by the quit prompt
			if (_quitAfterSave) {
				_quitAfterSave = false;
				g_nancy->quitGame();
			}

			return true;
		}
		default:
			return true;	// a verb this subsystem has, that this port ignores
		}
	}

	// SAVEGAME[1], "Do you want to save first?". Yes, No and X send Show, Hide and Clear.
	if (target.equalsIgnoreCase("Engine_SaveQuery")) {
		applyNDUICommand("SaveQueryDialog", kNDUICommandHide);

		switch (commandID) {
		case kNDUICommandShow:		// Yes: save, then leave.
			_quitAfterSave = true;
			applyNDUIEngineCommand("Engine_Saver", kNDUICommandShow, Common::String());
			return true;
		case kNDUICommandHide:		// No: leave without saving.
			_quitAfterSave = false;
			g_nancy->quitGame();
			return true;
		default:					// The X: neither, stay in the game.
			_quitAfterSave = false;
			return true;
		}
	}

	// Already handled by applyNDUICommand and by the panels' own row clicks
	if (target.equalsIgnoreCase("Engine_Journal") || target.equalsIgnoreCase("Engine_Tasklist")) {
		return true;
	}

	if (target.equalsIgnoreCase("Engine_GameWindow")) {
		if (commandID == kNDUICommandHide) {
			// Quitting goes through the "Do you want to save first?" box
			if (findNDUIPanelWithControl("SaveQueryDialog")) {
				applyNDUICommand("SaveQueryDialog", kNDUICommandShow);
			} else {
				g_nancy->quitGame();
			}

			return true;
		}

		// SetValue is handled by applyNDUISetting. Set and Clear are ignored.
		return true;
	}

	return false;
}

void Scene::registerGraphics() {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		_viewport.registerGraphics();
		_hotspotDebug.registerGraphics();

		for (uint i = 0; i < _nduiPanels.size(); ++i) {
			_nduiPanels[i]->registerGraphics();
		}

		return;
	}

	_frame.registerGraphics();
	_viewport.registerGraphics();

	_textbox.registerGraphics();

	// Pre-Nancy 10: inventory box is always-on-screen.
	// Nancy 10+: a separate popup widget driven by UIIV (initially hidden).
	if (g_nancy->getGameType() <= kGameTypeNancy9) {
		_inventoryBox.registerGraphics();
	} else {
		_inventoryPopup.registerGraphics();
		_notebookPopup.registerGraphics();
		_cellPhonePopup.registerGraphics();
		_conversationPopup.registerGraphics();
	}

	_hotspotDebug.registerGraphics();

	if (_menuButton) {
		_menuButton->registerGraphics();
		_menuButton->setVisible(false);
	}

	if (_helpButton) {
		_helpButton->registerGraphics();
		_helpButton->setVisible(false);
	}

	if (_taskbar) {
		_taskbar->registerGraphics();
	}

	if (_viewportOrnaments) {
		_viewportOrnaments->registerGraphics();
		_viewportOrnaments->setVisible(true);
	}

	if (_textboxOrnaments) {
		_textboxOrnaments->registerGraphics();
		_textboxOrnaments->setVisible(true);
	}

	if (_inventoryBoxOrnaments) {
		_inventoryBoxOrnaments->registerGraphics();
		_inventoryBoxOrnaments->setVisible(true);
	}

	if (_clock) {
		_clock->registerGraphics();
	}

	if (_camera) {
		_camera->registerGraphics();
	}
}

bool Scene::changePlayerCharacter(uint characterIndex) {
	uint previousCharacter = g_nancy->getPlayerCharacter();

	if (!applyPlayerCharacter(characterIndex)) {
		return false;
	}

	// Each protagonist carries their own items and resources, so the outgoing
	// character's are parked and the incoming character's are made live
	storeCharacterInventory(previousCharacter);
	storeCharacterResources(previousCharacter);
	inheritBrotherProgress(characterIndex);
	loadCharacterInventory(characterIndex);
	loadCharacterResources(characterIndex);

	return true;
}

void Scene::inheritBrotherProgress(uint characterIndex) {
	if (characterIndex != kPlayerCharacterFrank && characterIndex != kPlayerCharacterJoe) {
		return;
	}

	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	auto *journalData = (JournalData *)getPuzzleData(JournalData::getTag());
	if (!playerChar || !journalData) {
		return;
	}

	// The Hardy boys work the case as a team, so whichever brother is played
	// second takes over the notes the other has already made instead of
	// starting a fresh journal. Nancy always keeps her own. Their resources
	// (the money they carry) and their shared phone pass over the same way;
	// their items don't.
	const uint brother = characterIndex == kPlayerCharacterFrank ? kPlayerCharacterJoe : kPlayerCharacterFrank;
	if (!playerChar->getInventory(characterIndex).isValid && playerChar->getInventory(brother).isValid) {
		journalData->inheritEntries(brother, characterIndex);

		auto *resourceData = (UIResourceData *)getPuzzleData(UIResourceData::getTag());
		if (resourceData) {
			resourceData->getCharacterValues(characterIndex) = resourceData->getCharacterValues(brother);
		}

		auto *cellData = (CellPhoneData *)getPuzzleData(CellPhoneData::getTag());
		if (cellData) {
			cellData->phones[characterIndex] = cellData->phones[brother];
		}
	}
}

void Scene::storeCharacterInventory(uint characterIndex) {
	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	if (!playerChar) {
		return;
	}

	PlayerCharacterData::Inventory &inventory = playerChar->getInventory(characterIndex);
	inventory.isValid = true;
	inventory.heldItem = _flags.heldItem;
	inventory.items = _flags.items;
	inventory.disabledItems = _flags.disabledItems;
	inventory.order = _inventoryBox.getOrder();
}

void Scene::loadCharacterInventory(uint characterIndex) {
	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	if (!playerChar) {
		return;
	}

	const uint numItems = g_nancy->getStaticData().numItems;
	PlayerCharacterData::Inventory &inventory = playerChar->getInventory(characterIndex);

	if (inventory.isValid) {
		_flags.items = inventory.items;
		_flags.disabledItems = inventory.disabledItems;
		_inventoryBox.getOrder() = inventory.order;
		setHeldItem(inventory.heldItem);
	} else {
		// A character that hasn't been played yet starts out empty-handed
		_flags.items.clear();
		_flags.disabledItems.clear();
		_inventoryBox.getOrder().clear();
		setHeldItem(-1);
	}

	_flags.items.resize(numItems, g_nancy->_false);
	_flags.disabledItems.resize(numItems, 0);

	if (_inventoryPopup.isOpen()) {
		_inventoryPopup.refreshGrid();
	}
}

void Scene::storeCharacterResources(uint characterIndex) {
	auto *resourceData = (UIResourceData *)getPuzzleData(UIResourceData::getTag());
	if (!resourceData || !resourceData->seeded) {
		return;
	}

	resourceData->getCharacterValues(characterIndex) = resourceData->values;
}

void Scene::loadCharacterResources(uint characterIndex) {
	auto *resourceData = (UIResourceData *)getPuzzleData(UIResourceData::getTag());
	if (!resourceData) {
		return;
	}

	Common::Array<int32> &characterSet = resourceData->getCharacterValues(characterIndex);
	resourceData->values = characterSet;

	// A character who hasn't been played yet starts from the resource values in
	// their own UIRC, which the switch has just loaded
	resourceData->seeded = !characterSet.empty();
}

void Scene::setPlayerCharacterDesign(uint characterIndex, const Common::String &designName) {
	g_nancy->setPlayerCharacterDesign(characterIndex, designName);

	auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
	if (playerChar && characterIndex < kMaxPlayerCharacters) {
		playerChar->designs[characterIndex] = designName;
	}

	// The rebuild is left to onStateEnter(). The Design Select screen is a
	// different state, and tearing the scene's widgets down from underneath it
	// would draw them over that screen for a frame.
}

bool Scene::applyPlayerCharacter(uint characterIndex) {
	if (g_nancy->getGameType() < kGameTypeNancy15 || !g_nancy->playerCharacterNeedsReload(characterIndex)) {
		return false;
	}

	// The open popups describe the outgoing character, so get them off the
	// screen while the data they were built from is still around
	closeActivePopups();

	if (!g_nancy->setPlayerCharacter(characterIndex)) {
		return false;
	}

	auto *taskData = GetEngineData(TASK);
	assert(taskData);
	_frame.init(taskData->imageName);

	_textbox.init();
	_inventoryPopup.init();
	_notebookPopup.init();
	_cellPhonePopup.init();
	_conversationPopup.init();

	delete _taskbar;
	_taskbar = new UI::Taskbar();
	_taskbar->init();
	_taskbar->syncFromPuzzleData();
	_taskbar->updateNotificationStates(_sceneState.currentScene.sceneID);

	if (_camera) {
		_camera->init();
	}

	registerGraphics();
	g_nancy->_graphics->redrawAll();

	return true;
}

void Scene::changeSceneVideo(const Common::Path &videoFile) {
	_sceneState.summary.videoFile = videoFile;

	const Common::Path palettePath = !_sceneState.summary.palettes.empty() ?
		_sceneState.summary.palettes[(byte)_sceneState.currentScene.paletteID] :
		Common::Path();

	// The replacement covers the same location, so the vertical scroll carries
	// over, but panning restarts from the video's first frame
	_sceneState.currentScene.frameID = 0;
	_viewport.loadVideo(videoFile,
						0,
						_viewport.getCurVerticalScroll(),
						_sceneState.summary.panningType,
						_sceneState.summary.videoFormat,
						palettePath);

	// loadVideo() re-enables every edge, so the scene's own restrictions
	// have to be reapplied on top of the new video
	if (_viewport.getFrameCount() <= 1) {
		_viewport.disableEdges(kLeft | kRight);
	}

	if (_viewport.getMaxScroll() == 0) {
		_viewport.disableEdges(kUp | kDown);
	}
}

void Scene::synchronize(Common::Serializer &ser) {
	if (_flags.eventFlags.empty())
		init();

	ser.syncAsUint16LE(_sceneState.currentScene.sceneID);
	ser.syncAsUint16LE(_sceneState.currentScene.frameID);
	ser.syncAsUint16LE(_sceneState.currentScene.verticalOffset);

	if (g_nancy->getGameType() >= kGameTypeNancy3) {
		ser.syncAsUint16LE(_sceneState.currentScene.frontVectorFrameID);

		for (uint i = 0; i < 3; ++i) {
			ser.syncAsFloatLE(_sceneState.currentScene.listenerFrontVector.getData()[i]);
		}
	}

	if (ser.isLoading()) {
		_sceneState.currentScene.continueSceneSound = kLoadSceneSound;
		_sceneState.nextScene = _sceneState.currentScene;

		g_nancy->_sound->stopAllSounds();

		load(true);
	}

	ser.syncAsUint16LE(_sceneState.pushedScene.sceneID);
	ser.syncAsUint16LE(_sceneState.pushedScene.frameID);
	ser.syncAsUint16LE(_sceneState.pushedScene.verticalOffset);
	ser.syncAsByte(_sceneState.isScenePushed);

	// Inventory scene "stack" was introduced in nancy7
	if (g_nancy->getGameType() >= kGameTypeNancy7) {
		ser.syncAsUint16LE(_sceneState.pushedInvScene.sceneID);
		ser.syncAsUint16LE(_sceneState.pushedInvScene.frameID);
		ser.syncAsUint16LE(_sceneState.pushedInvScene.verticalOffset);
		ser.syncAsByte(_sceneState.isInvScenePushed);
		ser.syncAsUint16LE(_sceneState.pushedInvItemID);
	}

	// hardcoded number of logic conditions, check if there can ever be more/less
	for (uint i = 0; i < 30; ++i) {
		ser.syncAsUint32LE(_flags.logicConditions[i].flag);
	}

	for (uint i = 0; i < 30; ++i) {
		ser.syncAsUint32LE((uint32 &)_flags.logicConditions[i].timestamp);
	}

	const uint numItems = g_nancy->getStaticData().numItems;
	uint numSavedItems = numItems;
	if (ser.getVersion() < 10 && (g_nancy->getGameType() == kGameTypeNancy14 || g_nancy->getGameType() == kGameTypeNancy15)) {
		// Nancy14/15 saves made before version 10 were written with an item
		// count of 50, before the correct count of 49 was established.
		numSavedItems = 50;
	}

	auto &order = getInventoryBox().getOrder();
	uint prevSize = order.size();
	order.resize(numSavedItems);

	if (ser.isSaving()) {
		for (uint i = prevSize; i < order.size(); ++i) {
			order[i] = -1;
		}
	}

	ser.syncArray(order.data(), numSavedItems, Common::Serializer::Sint16LE);

	while (order.size() && order.back() == -1) {
		order.pop_back();
	}

	if (ser.isLoading() && g_nancy->getGameType() <= kGameTypeNancy9) {
		// Make sure the shades are open if we have items
		getInventoryBox().onReorder();
	}

	_flags.items.resize(numSavedItems, g_nancy->_false);
	ser.syncArray(_flags.items.data(), numSavedItems, Common::Serializer::Byte);
	_flags.items.resize(numItems);
	ser.syncAsSint16LE(_flags.heldItem);
	g_nancy->_cursor->setCursorItemID(_flags.heldItem);

	if (g_nancy->getGameType() >= kGameTypeNancy7) {
		_flags.disabledItems.resize(numSavedItems, 0);
		ser.syncArray(_flags.disabledItems.data(), numSavedItems, Common::Serializer::Byte);
		_flags.disabledItems.resize(numItems);
	}

	ser.syncAsUint32LE((uint32 &)_timers.lastTotalTime);
	ser.syncAsUint32LE((uint32 &)_timers.sceneTime);
	ser.syncAsUint32LE((uint32 &)_timers.playerTime);
	ser.syncAsSint16LE(_timers.playerDay, 11);

	if (ser.isLoading() && ser.getVersion() < 11) {
		auto *bootSummary = GetEngineData(BSUM);
		if (bootSummary && bootSummary->endOfDayFlag != kEvNoEvent) {
			// Older saves kept counting days into the clock. The day itself is
			// restored from the day value once the puzzle data has been loaded.
			_timers.playerTime = _timers.playerTime.getHours() * 3600000 + _timers.playerTime.getMinutes() * 60000;
		}
	}
	ser.syncAsUint32LE((uint32 &)_timers.pushedPlayTime);
	ser.syncAsUint32LE((uint32 &)_timers.timerTime);
	ser.syncAsByte(_timers.timerIsActive);
	ser.skip(1, 0, 2);

	g_nancy->setTotalPlayTime((uint32)_timers.lastTotalTime);

	uint numSavedEventFlags = g_nancy->getStaticData().numEventFlags;
	if (ser.getVersion() < 7 && g_nancy->getGameType() == kGameTypeNancy10) {
		// Nancy10 saves made before version 7 were written with an event flag
		// count of 816, before the correct count of 888 was established.
		numSavedEventFlags = 816;
	}

	ser.syncArray(_flags.eventFlags.data(), numSavedEventFlags, Common::Serializer::Byte);

	if (!ser.isSaving()) {
		// Clear generic flags
		for (uint16 id : g_nancy->getStaticData().genericEventFlags) {
			// genericEventFlags holds labels, not indices
			const int16 index = eventFlagToIndex((int16)id);
			if (index > kEvNoEvent && (uint)index < _flags.eventFlags.size()) {
				_flags.eventFlags[index] = g_nancy->_false;
			}
		}
	}

	// Skip empty sceneCount array
	ser.skip(2001 * 2, 0, 2);

	uint numSceneCounts = _flags.sceneCounts.size();
	ser.syncAsUint16LE(numSceneCounts);

	if (ser.isSaving()) {
		uint16 key;
		for (auto &entry : _flags.sceneCounts) {
			key = entry._key;
			ser.syncAsUint16LE(key);
			ser.syncAsUint16LE(entry._value);
		}
	} else {
		// Replace rather than merge, or scenes visited after the save keep their
		// live counts. Some second chance checkpoints depend on this.
		_flags.sceneCounts.clear();

		uint16 key = 0;
		uint16 val = 0;
		for (uint i = 0; i < numSceneCounts; ++i) {
			ser.syncAsUint16LE(key);
			ser.syncAsUint16LE(val);
			_flags.sceneCounts.setVal(key, val);
		}
	}

	ser.syncAsUint16LE(_difficulty);
	ser.syncArray<uint16>(_hintsRemaining.data(), _hintsRemaining.size(), Common::Serializer::Uint16LE);

	// NOTE: These two variables are only used by the hint system in
	// Nancy 1, so they can be freely repurposed by newer games to
	// store new data, if needed, to avoid bumping the save version.
	ser.syncAsSint16LE(_lastHintCharacter);
	ser.syncAsSint16LE(_lastHintID);

	// Sync game-specific puzzle data

	// Support for older savefiles
	if (ser.getVersion() < 3 && g_nancy->getGameType() <= kGameTypeNancy1) {
		PuzzleData *pd = getPuzzleData(SliderPuzzleData::getTag());
		if (pd) {
			pd->synchronize(ser);
		}

		return;
	}

	byte numPuzzleData = _puzzleData.size();
	ser.syncAsByte(numPuzzleData);

	if (ser.isSaving()) {
		for (auto &pd : _puzzleData) {
			uint32 tag = pd._key;
			ser.syncAsUint32LE(tag);
			pd._value->synchronize(ser);
		}
	} else {
		clearPuzzleData();

		uint32 tag = 0;
		for (uint i = 0; i < numPuzzleData; ++i) {
			ser.syncAsUint32LE(tag);
			PuzzleData *pd = getPuzzleData(tag);
			if (pd) {
				pd->synchronize(ser);
			}
		}

		auto *bootSummary = GetEngineData(BSUM);
		if (ser.getVersion() < 11 && bootSummary && bootSummary->endOfDayFlag != kEvNoEvent) {
			// Older saves only have the day in the day value
			TableData *table = (TableData *)getPuzzleData(TableData::getTag());
			assert(table);
			int16 day = table->getValue(bootSummary->dayValueIndex);
			_timers.playerDay = day == kNoTableValue ? 0 : day;
		}

		// Restore the taskbar disable overrides now that the persisted
		// TaskbarData is available. A disable can be set from an earlier
		// scene's AR that won't re-run here, so it has to come from the save.
		if (_taskbar && g_nancy->getGameType() >= kGameTypeNancy10) {
			_taskbar->syncFromPuzzleData();
			_taskbar->updateNotificationStates(_sceneState.currentScene.sceneID);
		}

		// Nancy15+ builds its popup UI out of the active player character's own
		// data files, so bring that data back before the widgets are used again.
		// Only the UI is swapped: the inventory restored above already is the
		// saved character's own, while the other characters' stay parked in the
		// PlayerCharacterData.
		if (g_nancy->getGameType() >= kGameTypeNancy15) {
			auto *playerChar = (PlayerCharacterData *)getPuzzleData(PlayerCharacterData::getTag());
			if (playerChar) {
				for (uint i = 0; i < kMaxPlayerCharacters; ++i) {
					g_nancy->setPlayerCharacterDesign(i, playerChar->designs[i]);
				}

				applyPlayerCharacter(playerChar->characterIndex);
			}
		}
	}

	// Nancy16's parallel flows. Written after the main scene, event flags and
	// scene counts, so those are in place when the streams are restored.
	_streams.synchronize(ser);

	if (ser.isLoading()) {
		// The ambient environment is not saved, and loading the streams cleared it
		updateEnvironment();
	}

	// The NDUI panels hold state that has to be refreshed after a load
	if (ser.isLoading() && g_nancy->getGameType() >= kGameTypeNancy16) {
		if (_inventoryPanel) {
			_inventoryPanel->refreshInventory();

			// The scrollbar thumb is updated later, by NDUIPanel::updateGraphics
		}

		if (_conversationPanel) {
			// Saving is not possible during a conversation, so the box must be closed
			_conversationPanel->convoClose();
		}
	}

	// Nancy16 NDUI show/hide state. Last, so it overrides the fixups above.
	synchronizeNDUI(ser);

	if (ser.getVersion() >= 13) {
		ser.syncAsByte(_pdaReturnEnabled);
	} else if (ser.isLoading()) {
		_pdaReturnEnabled = 0xff;
	}

	_isRunningAd = false;
	ConfMan.removeKey("restore_after_ad", Common::ConfigManager::kTransientDomain);

	g_nancy->_graphics->suppressNextDraw();
}

// The NDUI controls' visibility and enabled state are changed by the scripts, so
// they are saved as a delta against each panel's initial state, keyed by panel
// root name and control name.
void Scene::synchronizeNDUI(Common::Serializer &ser) {
	if (ser.getVersion() < 13 || g_nancy->getGameType() < kGameTypeNancy16) {
		return;
	}

	if (ser.isSaving()) {
		uint16 numPanels = _nduiPanels.size();
		ser.syncAsUint16LE(numPanels);

		for (uint i = 0; i < _nduiPanels.size(); ++i) {
			Common::String name = _nduiPanels[i]->getPanelName();
			ser.syncString(name);

			Common::StringArray shownNames, enabledNames;
			Common::Array<byte> shownValues, enabledValues;
			_nduiPanels[i]->getStateDelta(shownNames, shownValues, enabledNames, enabledValues);

			uint16 numShown = shownNames.size();
			ser.syncAsUint16LE(numShown);
			for (uint j = 0; j < numShown; ++j) {
				ser.syncString(shownNames[j]);
				ser.syncAsByte(shownValues[j]);
			}

			uint16 numEnabled = enabledNames.size();
			ser.syncAsUint16LE(numEnabled);
			for (uint j = 0; j < numEnabled; ++j) {
				ser.syncString(enabledNames[j]);
				ser.syncAsByte(enabledValues[j]);
			}
		}

		return;
	}

	// Read the whole block, then apply it to every panel. Panels missing from
	// the save get an empty delta, which resets them to their initial state.
	Common::StringArray fileNames;
	Common::Array<Common::StringArray> fileShown, fileEnabled;
	Common::Array<Common::Array<byte> > fileShownVals, fileEnabledVals;

	uint16 numPanels = 0;
	ser.syncAsUint16LE(numPanels);

	for (uint i = 0; i < numPanels; ++i) {
		Common::String name;
		ser.syncString(name);
		fileNames.push_back(name);

		Common::StringArray shownNames, enabledNames;
		Common::Array<byte> shownValues, enabledValues;

		uint16 numShown = 0;
		ser.syncAsUint16LE(numShown);
		for (uint j = 0; j < numShown; ++j) {
			Common::String ctrl;
			byte value = 0;
			ser.syncString(ctrl);
			ser.syncAsByte(value);
			shownNames.push_back(ctrl);
			shownValues.push_back(value);
		}

		uint16 numEnabled = 0;
		ser.syncAsUint16LE(numEnabled);
		for (uint j = 0; j < numEnabled; ++j) {
			Common::String ctrl;
			byte value = 0;
			ser.syncString(ctrl);
			ser.syncAsByte(value);
			enabledNames.push_back(ctrl);
			enabledValues.push_back(value);
		}

		fileShown.push_back(shownNames);
		fileShownVals.push_back(shownValues);
		fileEnabled.push_back(enabledNames);
		fileEnabledVals.push_back(enabledValues);
	}

	// Scrollbar thumbs composed here may be stale; NDUIPanel::updateGraphics fixes them
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		const Common::String panelName = _nduiPanels[i]->getPanelName();

		int match = -1;
		for (uint j = 0; j < fileNames.size(); ++j) {
			if (!panelName.empty() && fileNames[j].equalsIgnoreCase(panelName)) {
				match = (int)j;
				break;
			}
		}

		if (match < 0) {
			_nduiPanels[i]->applyStateDelta(Common::StringArray(), Common::Array<byte>(),
				Common::StringArray(), Common::Array<byte>());
			continue;
		}

		_nduiPanels[i]->applyStateDelta(fileShown[match], fileShownVals[match],
			fileEnabled[match], fileEnabledVals[match]);
	}
}

UI::Clock *Scene::getClock() {
	auto *clok = GetEngineData(CLOK);
	if (!clok || clok->clockIsDisabled || clok->clockIsDay) {
		return nullptr;
	} else {
		return (UI::Clock *)_clock;
	}
}

void Scene::init() {
	// A design may have been picked before the game itself started, so refresh
	// the engine data the widgets below are built from
	if (g_nancy->getGameType() >= kGameTypeNancy15) {
		g_nancy->setPlayerCharacter(g_nancy->getPlayerCharacter());
	}

	auto *bootSummary = GetEngineData(BSUM)
	auto *hintData = GetEngineData(HINT)
	assert(bootSummary);

	_flags.eventFlags.resize(g_nancy->getStaticData().numEventFlags, g_nancy->_false);

	// A New Game must not inherit the previous playthrough's parallel flows.
	_streams.endAll();

	_flags.sceneCounts.clear();

	_flags.items.resize(g_nancy->getStaticData().numItems, g_nancy->_false);
	_flags.disabledItems.resize(_flags.items.size(), 0);

	// The CursorManager is owned by the engine and survives a New Game (which
	// destroys and recreates the Scene). Clear any held-item cursor left over
	// from a previous playthrough so a fresh game starts with the normal cursor.
	g_nancy->_cursor->setCursorItemID(-1);

	_timers.lastTotalTime = 0;
	_timers.playerTime = bootSummary->startTimeHours * 3600000 + bootSummary->startTimeMinutes * 60000;
	_timers.sceneTime = 0;
	_timers.timerTime = 0;
	_timers.timerIsActive = false;
	_timers.playerTimeNextMinute = 0;
	_timers.pushedPlayTime = 0;
	_timers.sleepRequested = false;
	_timers.playerDay = 0;

	if (bootSummary->endOfDayFlag != kEvNoEvent) {
		setPlayerDay(0);
	}

	if (ConfMan.hasKey("load_ad", Common::ConfigManager::kTransientDomain)) {
		changeScene(bootSummary->adScene);
		ConfMan.removeKey("load_ad", Common::ConfigManager::kTransientDomain);
		_isRunningAd = true;
	} else {
		SceneChangeDescription firstScene = bootSummary->firstScene;

		changeScene(firstScene);
	}

	if (hintData) {
		_hintsRemaining.clear();
		_hintsRemaining = hintData->numHints;
		_lastHintCharacter = _lastHintID = -1;
	}

	initStaticData();

	if (!_isRunningAd && ConfMan.hasKey("save_slot", Common::ConfigManager::kTransientDomain)) {
		// Load savefile directly from the launcher
		int saveSlot = ConfMan.getInt("save_slot", Common::ConfigManager::kTransientDomain);
		if (saveSlot >= 0 && saveSlot <= g_nancy->getMetaEngine()->getMaximumSaveSlot()) {
			g_nancy->loadGameState(saveSlot);
		}

		// Remove key so clicking on "New Game" in start menu doesn't just reload the save
		ConfMan.removeKey("save_slot", Common::ConfigManager::kTransientDomain);
		// Retain the last slot used so the nancy8+ save menu shows its name on top
		ConfMan.setInt("display_slot", saveSlot, Common::ConfigManager::kTransientDomain);
	} else {
		// Normal boot, load default first scene
		_state = kLoad;
		// Make sure the nancy8+ save menu doesn't display a save name on new game
		ConfMan.removeKey("display_slot", Common::ConfigManager::kTransientDomain);
	}

	// Set relevant event flag when player has won the game at least once
	if (ConfMan.get("PlayerWonTheGame", ConfMan.getActiveDomainName()) == "AcedTheGame") {
		setEventFlag(g_nancy->getStaticData().wonGameFlagID, g_nancy->_true);
	}

	if (g_nancy->getGameType() == kGameTypeVampire) {
		_lightning = new Misc::Lightning();
	}

	Common::Rect vpPos = _viewport.getScreenPosition();
	_hotspotDebug._drawSurface.create(vpPos.width(), vpPos.height(), g_nancy->_graphics->getScreenPixelFormat());
	_hotspotDebug.moveTo(vpPos);
	_hotspotDebug.setTransparent(true);

	registerGraphics();
	g_nancy->_graphics->redrawAll();
}

void Scene::setActiveMovie(Action::PlaySecondaryMovie *activeMovie) {
	_activeMovie = activeMovie;
}

Action::PlaySecondaryMovie *Scene::getActiveMovie() {
	return _activeMovie;
}

void Scene::setActiveConversation(Action::ConversationSound *activeConversation) {
	_activeConversation = activeConversation;
}

Action::ConversationSound *Scene::getActiveConversation() {
	return _activeConversation;
}

void Scene::beginLightning(int16 distance, uint16 pulseTime, int16 rgbPercent) {
	if (_lightning) {
		_lightning->beginLightning(distance, pulseTime, rgbPercent);
	}
}

void Scene::specialEffect(byte type, uint16 fadeToBlackTime, uint16 frameTime) {
	_specialEffects.push(Misc::SpecialEffect(type, fadeToBlackTime, frameTime));
	_specialEffects.back().init();
}

void Scene::specialEffect(byte type, uint16 totalTime, uint16 fadeToBlackTime, Common::Rect rect) {
	_specialEffects.push(Misc::SpecialEffect(type, totalTime, fadeToBlackTime, rect));
	_specialEffects.back().init();
}

PuzzleData *Scene::getPuzzleData(const uint32 tag) {
	// Lazy initialization ensures both init() and synchronize() will not need
	// to care about which puzzles a specific game has

	if (_puzzleData.contains(tag)) {
		return _puzzleData[tag];
	} else {
		PuzzleData *newData = makePuzzleData(tag);
		if (newData) {
			_puzzleData.setVal(tag, newData);
		}

		return newData;
	}
}

void Scene::load(bool fromSaveFile) {
	if (_specialEffects.size()) {
		_specialEffects.front().onSceneChange();
	}

	// Scene IDs are prefixed with S inside the cif tree; e.g 100 -> S100
	Common::Path sceneName(Common::String::format("S%u", _sceneState.nextScene.sceneID));
	IFF *sceneIFF = g_nancy->_resource->loadIFF(sceneName);

	if (!sceneIFF) {
		error("Faled to load IFF %s", sceneName.toString().c_str());
	}

	Common::SeekableReadStream *sceneSummaryChunk = sceneIFF->getChunkStream("SSUM");
	if (sceneSummaryChunk) {
		_sceneState.summary.read(*sceneSummaryChunk);
	} else {
		// Reset panning type set from previous scenes, since terse summary
		// chunks don't contain panning type information
		_sceneState.summary.panningType = kPan360;

		sceneSummaryChunk = sceneIFF->getChunkStream("TSUM");
		if (sceneSummaryChunk) {
			_sceneState.summary.readTerse(*sceneSummaryChunk);
		}
	}

	if (!sceneSummaryChunk) {
		error("Invalid IFF Chunk SSUM");
	}

	delete sceneSummaryChunk;

	// A "NO_ART_SCENE" carries no viewport art: it keeps the previous scene's
	// frame on screen and only overlays new logic (used, for example, by
	// phone-call conversations). Clearing it must preserve the previous scene's
	// ambient character videos, so the scene type has to be known before the
	// scene data is wiped.
	const bool nextIsNoArt = _sceneState.summary.videoFile == "NO_ART_SCENE";

	clearSceneData(nextIsNoArt);
	g_nancy->_graphics->suppressNextDraw();

	debugC(0, kDebugScene, "Loading new scene %i: description \"%s\", frame %i, vertical scroll %i, %s",
				_sceneState.nextScene.sceneID,
				_sceneState.summary.description.c_str(),
				_sceneState.nextScene.frameID,
				_sceneState.nextScene.verticalOffset,
				_sceneState.currentScene.continueSceneSound == kContinueSceneSound ? "kContinueSceneSound" : "kLoadSceneSound");

	SceneChangeDescription lastScene = _sceneState.currentScene;
	_sceneState.currentScene = _sceneState.nextScene;

	// Make sure to discard invalid front vectors and reuse the last one
	if (_sceneState.currentScene.listenerFrontVector.isZero()) {
		_sceneState.currentScene.listenerFrontVector = lastScene.listenerFrontVector;
	}

	// Search for Action Records, maximum for a scene is 30
	Common::SeekableReadStream *actionRecordChunk = nullptr;

	uint numRecords = 0;
	while (actionRecordChunk = sceneIFF->getChunkStream("ACT", numRecords), actionRecordChunk != nullptr) {
		_actionManager.addNewActionRecord(*actionRecordChunk, sceneIFF->getChunkSource("ACT", numRecords));
		delete actionRecordChunk;
		++numRecords;
	}

	if (_sceneState.currentScene.paletteID == -1) {
		_sceneState.currentScene.paletteID = 0;
	}

	// "NO_ART_SCENE", (Nancy 11+) "POPUP_PREP_SCENE" and "NO_BG" are videoless
	// sentinel scenes that carry only logic ARs; they have no viewport art to load.
	if (!_sceneState.summary.videoFile.equalsIgnoreCase("NO_ART_SCENE") &&
			!_sceneState.summary.videoFile.equalsIgnoreCase("POPUP_PREP_SCENE") &&
			!_sceneState.summary.videoFile.equalsIgnoreCase("NO_BG")) {
		const Common::Path palettePath = !_sceneState.summary.palettes.empty() ?
			_sceneState.summary.palettes[(byte)_sceneState.currentScene.paletteID] :
			Common::Path();

		_viewport.loadVideo(_sceneState.summary.videoFile,
							_sceneState.currentScene.frameID,
							_sceneState.currentScene.verticalOffset,
							_sceneState.summary.panningType,
							_sceneState.summary.videoFormat,
							palettePath);
	} else if (_viewport._drawSurface.getPixels() == nullptr) {
		// These scenes keep the previous scene's art. If there is none, e.g. after
		// loading a save taken in one, use a blank frame.
		_viewport._drawSurface.create(_viewport.getBounds().width(),
			_viewport.getBounds().height(), g_nancy->_graphics->getInputPixelFormat());
		_viewport._drawSurface.clear(0);
		_viewport.setNeedsRedraw(true);
	}

	if (_viewport.getFrameCount() <= 1) {
		_viewport.disableEdges(kLeft | kRight);
	}

	if (_sceneState.summary.videoFormat == kSmallVideoFormat) {
		// TODO
	} else if (_sceneState.summary.videoFormat == kLargeVideoFormat) {
		// always start from the bottom
		_sceneState.currentScene.verticalOffset = _viewport.getMaxScroll();
	} else {
		error("Unrecognized Scene summary chunk video file format");
	}

	if (_sceneState.summary.videoFormat == kSmallVideoFormat) {
		// TODO
	} else if (_sceneState.summary.videoFormat == kLargeVideoFormat) {
		if (_viewport.getMaxScroll() == 0) {
			_viewport.disableEdges(kUp | kDown);
		}
	}

	for (uint i = 0; i < kMaxPlayerCharacters; ++i) {
		for (auto &override : _inventorySoundOverrides[i]) {
			g_nancy->_sound->stopSound(override._value.sound);
		}
		_inventorySoundOverrides[i].clear();
	}

	_timers.sceneTime = 0;
	g_nancy->_sound->clearListenerPositionOverride();
	g_nancy->_sound->recalculateSoundEffects();

	// Increment the number of times we've visited this scene, unless we're
	// loading from a save
	if (!fromSaveFile) {
		_flags.sceneCounts.getOrCreateVal(_sceneState.currentScene.sceneID)++;
	}

	// Re-evaluate taskbar notification states against the new scene.
	// Nancy16+ has no taskbar
	if (g_nancy->getGameType() >= kGameTypeNancy10 && _taskbar) {
		_taskbar->updateNotificationStates(_sceneState.currentScene.sceneID);
	}

	delete sceneIFF;
	_state = kStartSound;

	// Tell the Nancy16 stream runtime the main flow has moved
	_streams.onMainSceneLoaded(_sceneState.currentScene.sceneID);

	// ...and which environment it is in. When loading a save, Scene::synchronize
	// does this itself after restoring the streams.
	if (!fromSaveFile) {
		updateEnvironment();
	}

}

// Nancy16 ENVS: the scene summary's sound name is the environment code. Entering
// an environment starts its ambient script; see streams.h.
void Scene::updateEnvironment() {
	if (g_nancy->getGameType() < kGameTypeNancy16) {
		return;
	}

	auto *envs = (const ENVS *)g_nancy->getEngineData("ENVS");
	if (!envs) {
		return;
	}

	// Some scenes leave the sound name blank and carry the code in the description
	const Common::String *candidates[2] = { &_sceneState.summary.sound.name, &_sceneState.summary.description };

	for (uint c = 0; c < ARRAYSIZE(candidates); ++c) {
		if (candidates[c]->empty()) {
			continue;
		}

		for (const ENVS::Environment &env : envs->environments) {
			if (env.sceneCode.equalsIgnoreCase(*candidates[c])) {
				_streams.setEnvironment(env.sceneCode, env.ambientSoundName);
				return;
			}
		}
	}

	// Scenes with no environment keep the current one's ambience
}

Time Scene::getFlowSceneTime() const {
	const Stream *executing = _streams.executing();
	return executing ? executing->getSceneTime() : _timers.sceneTime;
}

void Scene::run() {
	if (_gameStateRequested != NancyState::kNone) {
		g_nancy->setState(_gameStateRequested);

		return;
	}

	Time currentPlayTime = g_nancy->getTotalPlayTime();
	Time deltaTime = currentPlayTime - _timers.lastTotalTime;
	_timers.lastTotalTime = currentPlayTime;

	if (_timers.timerIsActive) {
		_timers.timerTime += deltaTime;
	}

	_timers.sceneTime += deltaTime;

	// Advance the Nancy 11+ software timers before processing action records,
	// so any flags they fire this frame are visible to record dependencies
	tickSoftwareTimers((uint32)deltaTime);

	// Calculate the in-game time (playerTime)
	if (currentPlayTime > _timers.playerTimeNextMinute) {
		auto *bootSummary = GetEngineData(BSUM);
		assert(bootSummary);

		_timers.playerTime += 60000; // Add a minute
		_timers.playerTimeNextMinute = currentPlayTime + bootSummary->playerTimeMinuteLength;
	}

	updateEndOfDay();

	handleInput();

	if (g_nancy->getState() == NancyState::kMainMenu) {
		// Player pressed esc, do not process further
		return;
	}

	_actionManager.processActionRecords();

	// Nancy16's parallel flows run after the main one, so a stream that reacts
	// to a main-flow scene change sees it in the same frame it happens.
	_streams.process();

	// Update NDUI controls bound to values, such as the coin purse
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		_nduiPanels[i]->refreshBoundValues();
	}

	// The journal panel records clicks on its heading list itself
	if (_journalPanel) {
		const int picked = _journalPanel->listTakePicked();
		if (picked >= 0) {
			selectJournalPage(picked);
		}
	}

	if (_lightning) {
		_lightning->run();
	}

	// Do this after the first records are processed to fix the text in nancy3 intro
	if (_specialEffects.size()) {
		if (_specialEffects.front().isInitialized()) {
			if (_specialEffects.front().isDone()) {
				_specialEffects.pop();
				g_nancy->_graphics->redrawAll();
			}
		} else {
			_specialEffects.front().afterSceneChange();
		}
	}


	g_nancy->_sound->soundEffectMaintenance();


	if (_state == kLoad) {
		g_nancy->_graphics->suppressNextDraw();
	}
}

void Scene::updateEndOfDay() {
	auto *bootSummary = GetEngineData(BSUM);
	assert(bootSummary);

	if (bootSummary->lateNightFlag != kEvNoEvent) {
		if (_timers.playerTime.getDays() == 1 && _timers.playerTime.getHours() >= bootSummary->lateNightHour) {
			setEventFlag(bootSummary->lateNightFlag, g_nancy->_true);
		}
	}

	if (bootSummary->endOfDayFlag == kEvNoEvent) {
		return;
	}

	if (!getEventFlag(bootSummary->endOfDayFlag, g_nancy->_true) && _timers.playerTime.getTotalHours() >= bootSummary->endOfDayHour) {
		setEventFlag(bootSummary->endOfDayFlag, g_nancy->_true);
	} else if (_timers.sleepRequested) {
		_timers.sleepRequested = false;
		_timers.playerTime = bootSummary->wakeUpHour * 3600000;
		setPlayerDay(_timers.playerDay + 1);
		setEventFlag(bootSummary->endOfDayFlag, g_nancy->_false);
	}
}

void Scene::setPlayerDay(int16 day) {
	auto *bootSummary = GetEngineData(BSUM);
	assert(bootSummary);

	_timers.playerDay = day;

	TableData *table = (TableData *)getPuzzleData(TableData::getTag());
	assert(table);
	table->setValue(bootSummary->dayValueIndex, day);
}

void Scene::tickSoftwareTimers(uint32 deltaMs) {
	if (g_nancy->getGameType() < kGameTypeNancy11 || deltaMs == 0) {
		return;
	}

	// getPuzzleData() below lazily creates (and thereafter persists) the TimerData
	// chunk. This runs every frame, so without this guard every Nancy 11 save
	// would carry an empty TimerData chunk even if no timer is ever used. The
	// chunk only exists once a timer AR has configured a slot.
	if (!_puzzleData.contains(TimerData::getTag())) {
		return;
	}

	TimerData *timerData = (TimerData *)getPuzzleData(TimerData::getTag());

	for (uint i = 0; i < TimerData::kNumTimers; ++i) {
		TimerData::Timer &timer = timerData->timers[i];

		if (timer.state != TimerData::Timer::kRunning &&
			timer.state != TimerData::Timer::kOneShot &&
			timer.state != TimerData::Timer::kRepeating) {
			continue;
		}

		timer.currentTimeMs += deltaMs;

		// Nancy 11 single-config timers fire directly from the timer state
		if ((timer.state == TimerData::Timer::kOneShot || timer.state == TimerData::Timer::kRepeating) &&
			timer.durationMs > 0 && !timer.hasFired && timer.currentTimeMs >= timer.durationMs) {
			fireSoftwareTimer(timer);

			if (timer.state == TimerData::Timer::kOneShot) {
				// One-shot timers clear themselves once they fire
				timer.reset();
			} else {
				// Repeating timers keep counting up but will not fire again
				timer.state = TimerData::Timer::kRunning;
			}
		}

		// Nancy 12+ running timers fire from their triggers. A one-shot trigger
		// clears the whole timer when it fires; a repeating one leaves it running.
		if (timer.state == TimerData::Timer::kRunning) {
			bool clearTimer = false;
			for (uint j = 0; j < timer.triggers.size(); ++j) {
				TimerData::Trigger &trigger = timer.triggers[j];
				if (!trigger.hasFired && trigger.durationMs > 0 && timer.currentTimeMs >= trigger.durationMs) {
					trigger.hasFired = true;
					fireTimerTrigger(trigger);

					if (trigger.type == TimerData::Trigger::kOneShot) {
						clearTimer = true;
					}
				}
			}

			if (clearTimer) {
				timer.reset();
			}
		}
	}
}

bool Scene::isSoftwareTimerActive(uint16 index) const {
	if (index >= TimerData::kNumTimers || !_puzzleData.contains(TimerData::getTag())) {
		return false;
	}

	const TimerData::Timer &timer = ((const TimerData *)_puzzleData.getVal(TimerData::getTag()))->timers[index];

	// Nancy12+ also counts a paused timer as active
	if (g_nancy->getGameType() >= kGameTypeNancy12) {
		return timer.state != TimerData::Timer::kIdle;
	}

	return timer.state == TimerData::Timer::kRunning ||
		timer.state == TimerData::Timer::kOneShot ||
		timer.state == TimerData::Timer::kRepeating;
}

uint32 Scene::getSoftwareTimerElapsed(uint16 index) const {
	if (index >= TimerData::kNumTimers || !_puzzleData.contains(TimerData::getTag())) {
		return 0;
	}

	return ((const TimerData *)_puzzleData.getVal(TimerData::getTag()))->timers[index].currentTimeMs;
}

void Scene::fireSoftwareTimer(TimerData::Timer &timer) {
	timer.hasFired = true;

	// Set the configured event flags
	for (uint i = 0; i < ARRAYSIZE(timer.flags); ++i) {
		if (timer.flags[i].label != kFlagNoLabel) {
			setEventFlag(timer.flags[i]);
		}
	}

	// Play the optional expiry sound
	if (timer.sound.name != "NO SOUND") {
		g_nancy->_sound->loadSound(timer.sound);
		g_nancy->_sound->playSound(timer.sound);
	}

	// Show the optional caption, if captions are enabled
	if (ConfMan.getBool("subtitles", ConfMan.getActiveDomainName())) {
		if (!timer.autotextKey.empty()) {
			const CVTX *autotext = (const CVTX *)g_nancy->getEngineData("AUTOTEXT");
			if (autotext && autotext->texts.contains(timer.autotextKey)) {
				_textbox.addTextLine(autotext->texts[timer.autotextKey]);
			}
		} else if (!timer.caption.empty()) {
			_textbox.addTextLine(timer.caption);
		}
	}
}

void Scene::fireTimerTrigger(TimerData::Trigger &trigger) {
	debugC(1, kDebugActionRecord, "Software timer trigger fired after %ums, sound '%s'",
		trigger.durationMs, trigger.sound.name.c_str());

	// Set the trigger's event flags
	for (uint i = 0; i < ARRAYSIZE(trigger.flags); ++i) {
		if (trigger.flags[i].label != kFlagNoLabel) {
			setEventFlag(trigger.flags[i]);
		}
	}

	// Play the trigger's sound
	if (trigger.sound.name != "NO SOUND") {
		g_nancy->_sound->loadSound(trigger.sound);
		g_nancy->_sound->playSound(trigger.sound);
	}

	// Nancy 12+ triggers carry no inline caption; the subtitle is looked up from
	// the played sound's name
	if (ConfMan.getBool("subtitles", ConfMan.getActiveDomainName()) && trigger.sound.name != "NO SOUND") {
		const CVTX *autotext = (const CVTX *)g_nancy->getEngineData("AUTOTEXT");
		if (autotext && autotext->texts.contains(trigger.sound.name)) {
			_textbox.addTextLine(autotext->texts[trigger.sound.name]);
		}
	}
}

Common::Rect Scene::activePopupConfinement() const {
	// Pick the first visible Nancy 10+ popup; if more than one is open
	// (shouldn't normally happen) the priority order matches the input
	// order — conversation, inventory, notebook, cellphone.
	if (_conversationPopup.isVisible()) return _conversationPopup.getScreenPosition();
	if (_inventoryPopup.isVisible())    return _inventoryPopup.getScreenPosition();
	if (_notebookPopup.isVisible())     return _notebookPopup.getScreenPosition();
	// The cellphone stays up during a call it placed, but the conversation
	// (textbox) is the active UI then — don't confine the cursor to the phone,
	// or it fights the textbox as each new line starts. _activeConversation
	// can't gate this: it toggles per dialogue line (null between lines), so the
	// confinement would flicker on and snap the cursor up into the phone. Gate on
	// the phone's own call state instead, which stays set for the whole call.
	if (_cellPhonePopup.isVisible() && !_cellPhonePopup.isInCall())
		return _cellPhonePopup.getScreenPosition();
	return Common::Rect();
}

void Scene::closeActivePopups() {
	if (_conversationPopup.isVisible()) _conversationPopup.close();
	if (_inventoryPopup.isOpen())       _inventoryPopup.close();
	if (_notebookPopup.isVisible())     _notebookPopup.close();
	if (_cellPhonePopup.isVisible())    _cellPhonePopup.close();
}

void Scene::handleInput() {
	// While a UI prep scene is running the player shouldn't be able to interact
	// with the (hidden, videoless) prep scenes. Swallow all input until the
	// prep's UIPopupPrepScene AR finishes it. A safety timeout guards against a
	// prep scene that never reaches its terminator so the game can't lock up.
	if (_uiPrep.active) {
		if (g_system->getMillis() - _uiPrep.startMillis > 5000) {
			warning("UI prep scene did not finish within timeout; aborting");
			finishUIPrepScene();
		}
		g_nancy->_input->getInput();
		return;
	}

	NancyInput input = g_nancy->_input->getInput();
	// Nancy16+: a right-click puts down the held item
	if (g_nancy->getGameType() >= kGameTypeNancy16 && getHeldItem() != -1 &&
		(input.input & NancyInput::kRightMouseButtonUp)) {
		setHeldItem(-1);
		input.input &= ~NancyInput::kRightMouseButtonUp;
	}

	// Warp the mouse below the inactive zone during dialogue scenes. Nancy16
	// has NDUI controls above it.
	if (_activeConversation != nullptr && g_nancy->getGameType() < kGameTypeNancy16) {
		const Common::Rect &inactiveZone = g_nancy->_cursor->getPrimaryVideoInactiveZone();

		if (g_nancy->getGameType() == kGameTypeVampire) {
			const Common::Point cursorHotspot = g_nancy->_cursor->getCurrentCursorHotspot();
			Common::Point adjustedMousePos = input.mousePos;
			adjustedMousePos.y -= cursorHotspot.y;

			if (inactiveZone.bottom > adjustedMousePos.y) {
				input.mousePos.y = inactiveZone.bottom + cursorHotspot.y;
				g_nancy->_cursor->warpCursor(input.mousePos);
			}
		} else {
			if (inactiveZone.bottom > input.mousePos.y) {
				input.mousePos.y = inactiveZone.bottom;
				g_nancy->_cursor->warpCursor(input.mousePos);
			}
		}
	}

	// Check if player has pressed esc. While a dialogue line or a cinematic is
	// playing, esc skips it instead of opening the main menu. Only the initial
	// press counts, so holding the key down doesn't skip line after line.
	const bool escPressed = (input.input & NancyInput::kOpenMainMenu) != 0;
	const bool escJustPressed = escPressed && !_escHeld;
	_escHeld = escPressed;

	if (escJustPressed) {
		if (_activeConversation) {
			_activeConversation->skipLine();
		} else if (_activeMovie) {
			_activeMovie->skip();
		} else {
			g_nancy->setState(NancyState::kMainMenu);
			return;
		}
	}

	// We handle the textbox and inventory box first because of their scrollbars, which
	// need to take highest priority. On Nancy 10+ the taskbar-driven popups
	// (inventory/notebook/cellphone) sit visually on top of the textbox
	// strip, so they get first crack at input — otherwise a click inside
	// the popup that overlapped the textbox area could accidentally pick
	// a conversation response.
	if (g_nancy->getGameType() >= kGameTypeNancy10) {
		// Confine the cursor to whichever popup is open so the player
		// can't drag it into the underlying scene UI.
		const Common::Rect confine = activePopupConfinement();
		if (!confine.isEmpty() && !confine.contains(input.mousePos)) {
			input.mousePos.x = CLIP<int16>(input.mousePos.x,
											confine.left, confine.right - 1);
			input.mousePos.y = CLIP<int16>(input.mousePos.y,
											confine.top, confine.bottom - 1);
			g_nancy->_cursor->warpCursor(input.mousePos);
		}
		// Nancy16+ uses NDUI panels instead of these popups and the textbox
		if (g_nancy->getGameType() < kGameTypeNancy16) {
			_conversationPopup.handleInput(input);
			_inventoryPopup.handleInput(input);
			_notebookPopup.handleInput(input);
			_cellPhonePopup.handleInput(input);
		}
	}

	if (g_nancy->getGameType() < kGameTypeNancy16) {
		_textbox.handleInput(input);
	} else {
		// Only the frontmost modal dialog receives input
		NDUIPanel *modal = nullptr;
		for (const char *root : kNDUIModalRoots) {
			for (NDUIPanel *panel : _nduiPanels) {
				if (panel->isVisible() && panel->getPanelName().equalsIgnoreCase(root))
					modal = panel;
			}
		}
		// Inventory items and conversation replies first. Later-registered panels
		// sit on top, so ask them first.
		for (int i = (int)_nduiPanels.size() - 1; i >= 0; --i) {
			if (!modal || modal == _nduiPanels[i])
				_nduiPanels[i]->handleInput(input);
		}

		// Then the authored controls. The smallest hit across all panels wins, since
		// a large invisible button in LOWERMATTE overlaps the taskbar buttons.
		int bestPanel = -1, bestIndex = -1, bestArea = 0;
		for (int i = (int)_nduiPanels.size() - 1; i >= 0; --i) {
			if (modal && modal != _nduiPanels[i]) continue;
			int idx = -1, area = 0;
			if (_nduiPanels[i]->findControlHit(input.mousePos, idx, area)) {
				if (bestPanel < 0 || area < bestArea) {
					bestPanel = i;
					bestIndex = idx;
					bestArea = area;
				}
			}
		}

		if (bestPanel >= 0) {
			_nduiPanels[bestPanel]->activateControl(bestIndex, input);
		} else if (isNDUIDialogOpen()) {
			// Nothing behind a modal dialog takes input
			input.eatMouseInput();
		}
	}
	if (g_nancy->getGameType() <= kGameTypeNancy9) {
		_inventoryBox.handleInput(input);
	}

	// While the viewfinder is up the scene's own hotspots and its panning are both
	// suppressed; a click in the viewport only takes the shot.
	const bool cameraActive = _camera && _camera->isActive();
	if (cameraActive) {
		_camera->handleInput(input);
	}

	// Handle invisible map button
	// We do this before the viewport since TVD's map button overlaps the viewport's right hotspot
	for (uint16 id : g_nancy->getStaticData().mapAccessSceneIDs) {
		if (cameraActive) {
			break;
		}

		if ((int)_sceneState.currentScene.sceneID == id) {
			if (_mapHotspot.contains(input.mousePos)) {
				g_nancy->_cursor->setCursorType(g_nancy->getGameType() == kGameTypeVampire ? CursorManager::kHotspot : CursorManager::kHotspotArrow);

				if (input.input & NancyInput::kLeftMouseButtonUp) {
					requestStateChange(NancyState::kMap);

					if (g_nancy->getGameType() == kGameTypeVampire) {
						g_nancy->setMouseEnabled(false);
					}
				}

				input.eatMouseInput();
			}

			break;
		}
	}

	// Handle clock before viewport since it overlaps the left hotspot in TVD
	if (getClock() && !cameraActive) {
		getClock()->handleInput(input);
	}

	// Overlay-only Nancy16 scenes keep the previous scene's image but not its frame
	const bool overlayOnly = g_nancy->getGameType() >= kGameTypeNancy16 &&
		(_sceneState.summary.videoFile.equalsIgnoreCase("NO_ART_SCENE") ||
		 _sceneState.summary.videoFile.equalsIgnoreCase("POPUP_PREP_SCENE") ||
		 _sceneState.summary.videoFile.equalsIgnoreCase("NO_BG"));
	if (!overlayOnly) {
		if (!cameraActive) {
			_viewport.handleInput(input);
		}

		_sceneState.currentScene.verticalOffset = _viewport.getCurVerticalScroll();

		if (_sceneState.currentScene.frameID != _viewport.getCurFrame()) {
			_sceneState.currentScene.frameID = _viewport.getCurFrame();
			g_nancy->_sound->recalculateSoundEffects();
		}
	}

	if (!cameraActive) {
		_actionManager.handleInput(input);
	}

	// The whole Nancy 10+ taskbar (inventory / notebook / cell phone / MENU /
	// HELP) stays usable even while a SecondaryMovie is playing; only the
	// standalone Nancy <=9 menu/help buttons further down are disabled during a
	// movie. While a Nancy 10+ popup (inventory / notebook / cellphone /
	// conversation) is open, the original disables the entire taskbar — every
	// button, including MENU and HELP. Skip the taskbar input so it neither
	// hovers nor reacts to clicks until the popup is closed. The taskbar is also
	// skipped while the textbox is in open mode, since it visually covers the
	// buttons.
	const bool popupOpen = g_nancy->getGameType() >= kGameTypeNancy10 &&
							!activePopupConfinement().isEmpty();
	if (_taskbar) {
		// Grey out the whole taskbar while a popup is open (matches the
		// original); restored automatically once the popup closes.
		_taskbar->setPopupLockout(popupOpen);
	}
	if (_taskbar && !_textbox.coversTaskbar() && !popupOpen) {
		// MENU and HELP leave gameplay entirely, which would cut off the
		// taskbar click sound. The original defers the transition until that
		// sound finishes, so we hold the click here and only switch state
		// once the button's click sound has stopped playing.
		if (_pendingTaskbarButton != -1) {
			auto *taskData = GetEngineData(TASK);
			if (!taskData || !g_nancy->_sound->isSoundPlaying(taskData->buttons[_pendingTaskbarButton].button.clickSound)) {
				NancyState::NancyState target = _pendingTaskbarButton == kTaskButtonMenu ? NancyState::kMainMenu : NancyState::kHelp;
				_pendingTaskbarButton = -1;
				requestStateChange(target);
			}
		} else {
			_taskbar->handleInput(input);

			int clicked = _taskbar->getClickedButton();
			switch (clicked) {
			case kTaskButtonMenu:
				_pendingTaskbarButton = kTaskButtonMenu;
				break;
			case kTaskButtonInventory:
				_inventoryPopup.toggle();
				break;
			case kTaskButtonNotebook: {
				// Nancy 11+ populates the notebook lazily: opening it first
				// runs a hidden prep scene (header.linkbackScene) whose ARs
				// add the journal / task entries. Games without a prep scene
				// (linkbackScene == kNoScene, e.g. Nancy 10) just toggle.
				const int16 prepScene = _notebookPopup.getPrepSceneID();
				if (!_notebookPopup.isVisible() && (uint16)prepScene != kNoScene) {
					startUIPrepScene(kUITypeNotebook, prepScene);
				} else {
					_notebookPopup.toggle();
				}
				break;
			}
			case kTaskButtonCellphone:
				_cellPhonePopup.toggle();
				break;
			case -1:
				break;
			default:
				// HELP is always the last taskbar button. Its index shifts from
				// 4 to 5 in Nancy12, where a non-clickable coin purse occupies slot
				// 4 (and never reports a click), so match it as the fall-through.
				_pendingTaskbarButton = clicked;
				break;
			}
		}
	}

	// The standalone Nancy <=9 menu/help buttons leave the scene, so they're
	// disabled while a movie is active.
	if (!_activeMovie) {
		if (_menuButton) {
			_menuButton->handleInput(input);

			if (_menuButton->_isClicked) {
				if (_buttonPressActivationTime == 0) {
					auto *bootSummary = GetEngineData(BSUM);
					assert(bootSummary);

					g_nancy->_sound->playSound("BUOK");
					_buttonPressActivationTime = g_system->getMillis() + bootSummary->buttonPressTimeDelay;
				} else if (g_system->getMillis() > _buttonPressActivationTime) {
					_menuButton->_isClicked = false;
					requestStateChange(NancyState::kMainMenu);
					_buttonPressActivationTime = 0;
				}
			}
		}

		if (_helpButton) {
			_helpButton->handleInput(input);

			if (_helpButton->_isClicked) {
				if (_buttonPressActivationTime == 0) {
					auto *bootSummary = GetEngineData(BSUM);
					assert(bootSummary);

					g_nancy->_sound->playSound("BUOK");
					_buttonPressActivationTime = g_system->getMillis() + bootSummary->buttonPressTimeDelay;
				} else if (g_system->getMillis() > _buttonPressActivationTime) {
					_helpButton->_isClicked = false;
					requestStateChange(NancyState::kHelp);
					_buttonPressActivationTime = 0;
				}
			}
		}
	}
}

void Scene::initStaticData() {
	auto *bootSummary = GetEngineData(BSUM);
	assert(bootSummary);

	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		// Nancy16+ replaces the taskbar, textbox and popups with NDUI panels
		_viewport.init();

		// The panels are all kept resident, since an NDUI command is dropped when no
		// loaded panel owns its target. All but the two mattes start hidden.
		static const char *const kPersistentPanels[] = {
			"UPPERMATTE", "LOWERMATTE", "CONVO", "INVENTORY", "STAKEOUT",
			"OPTIONS", "LOADGAME", "SAVEGAME", "JOURNAL", "TASKLIST", "DANCEPUZZLE"
		};
		for (uint i = 0; i < ARRAYSIZE(kPersistentPanels); ++i) {
			// The row template is in a chunk of its own, and is given to every panel
			// of the member once all of them are loaded.
			Common::Array<NDUIPanel *> memberPanels;
			NDUIControl rowTemplate;
			bool haveRowTemplate = false;

			for (uint chunk = 0; ; ++chunk) {
				NDUI *data = loadNDUIChunk(kPersistentPanels[i], chunk);
				if (!data) {
					break;
				}

				if (data->hasPanel) {
					// The panel takes ownership of the chunk. The mattes go at z 7 with the
					// taskbar, the popup replacements at 12 and the dialogs above them at 13.
					const bool isPopupPanel = !strcmp(kPersistentPanels[i], "CONVO") ||
						!strcmp(kPersistentPanels[i], "INVENTORY") ||
						!strcmp(kPersistentPanels[i], "STAKEOUT") ||
						!strcmp(kPersistentPanels[i], "JOURNAL") ||
						!strcmp(kPersistentPanels[i], "TASKLIST") ||
						!strcmp(kPersistentPanels[i], "DANCEPUZZLE");
					const bool isDialogPanel = !strcmp(kPersistentPanels[i], "OPTIONS") ||
						!strcmp(kPersistentPanels[i], "LOADGAME") ||
						!strcmp(kPersistentPanels[i], "SAVEGAME");
					NDUIPanel *panel = new NDUIPanel(isDialogPanel ? 13 : (isPopupPanel ? 12 : 7));
					panel->init(data);

					if (!strcmp(kPersistentPanels[i], "DANCEPUZZLE")) {
						panel->setStartsHidden();
						panel->setVisible(false);
					}

					if (isDialogPanel) {
						// Authored as visible, since the original only builds them when opened
						panel->setStartsHidden();
						panel->setVisible(false);
					}

					if (!strcmp(kPersistentPanels[i], "INVENTORY")) {
						panel->setStartsHidden();
						panel->setVisible(false);

						if (panel->isInventoryPanel()) {
							_inventoryPanel = panel;
						}
					}

					// LOWERMATTE owns the centred VO caption line.
					if (panel->isNarrationPanel()) {
						_narrationPanel = panel;
					}

					if (!strcmp(kPersistentPanels[i], "CONVO")) {
						_conversationPanels.push_back(panel);
						panel->setStartsHidden();
						panel->setVisible(false);

						if (panel->isConversationPanel()) {
							_conversationPanel = panel;
						}
					}

					if (!strcmp(kPersistentPanels[i], "STAKEOUT")) {
						panel->setStartsHidden();
						panel->setVisible(false);

						if (panel->isStakeoutPanel()) {
							_stakeoutPanel = panel;
						}
					}

					if (!strcmp(kPersistentPanels[i], "JOURNAL")) {
						panel->setStartsHidden();
						panel->setVisible(false);

						// JOURNAL[2] owns the JournalItems ListBox; JOURNAL[4..7]
						// are the four heading pages, in chunk order.
						if (panel->ownsControl("JournalItems")) {
							_journalPanel = panel;
						} else {
							for (uint p = 0; p < kNumJournalPages; ++p) {
								if (!_journalPagePanels[p]) {
									_journalPagePanels[p] = panel;
									break;
								}
							}
						}
					}

					if (!strcmp(kPersistentPanels[i], "TASKLIST")) {
						panel->setStartsHidden();
						panel->setVisible(false);

						if (panel->ownsControl("TasklistDialog")) {
							_tasklistPanel = panel;
						}
					}

					memberPanels.push_back(panel);
					_nduiPanels.push_back(panel);
				} else {
					// A panel-less chunk is a standalone control; in the list
					// members it is the row template.
					if (data->children.size() == 1) {
						rowTemplate = data->children[0];
						haveRowTemplate = true;
					}

					delete data;
				}
			}

			if (haveRowTemplate) {
				for (uint p = 0; p < memberPanels.size(); ++p) {
					memberPanels[p]->setRowTemplate(rowTemplate);
				}
			}
		}

		g_nancy->setMouseEnabled(true);
		_state = kLoad;
		return;
	}

	Common::Path imageName;

	if (g_nancy->getGameType() <= kGameTypeNancy9) {
		const ImageChunk *fr0 = (const ImageChunk *)g_nancy->getEngineData("FR0");
		assert(fr0);
		imageName = fr0->imageName;
	} else {
		auto *taskData = GetEngineData(TASK);
		assert(taskData);
		imageName = taskData->imageName;
	}

	auto *mapData = GetEngineData(MAP);

	_frame.init(imageName);
	_viewport.init();
	_textbox.init();

	if (g_nancy->getGameType() <= kGameTypeNancy9) {
		_inventoryBox.init();
	} else {
		_inventoryPopup.init();
		_notebookPopup.init();
		_cellPhonePopup.init();
		_conversationPopup.init();
	}

	// Init buttons
	if (g_nancy->getGameType() == kGameTypeVampire) {
		_mapHotspot = bootSummary->extraButtonHotspot;
	} else if (mapData) {
		_mapHotspot = mapData->buttonDest;
	}

	if (g_nancy->getGameType() <= kGameTypeNancy9) {
		// Pre-Nancy 10: free-floating MENU and HELP buttons whose
		// rects come from BSUM. Replaced in Nancy 10+ by the taskbar.
		_menuButton = new UI::Button(5, g_nancy->_graphics->_object0, bootSummary->menuButtonSrc, bootSummary->menuButtonDest, bootSummary->menuButtonHighlightSrc);
		_helpButton = new UI::Button(5, g_nancy->_graphics->_object0, bootSummary->helpButtonSrc, bootSummary->helpButtonDest, bootSummary->helpButtonHighlightSrc);
	} else {
		// Nancy 10+: bottom-of-screen taskbar holds MENU / inventory /
		// notebook / cellphone / HELP buttons. Built from the TASK chunk.
		_taskbar = new UI::Taskbar();
		_taskbar->init();
	}

	g_nancy->setMouseEnabled(true);

	// Init ornaments and clock (TVD only)
	if (g_nancy->getGameType() == kGameTypeVampire) {
		_viewportOrnaments = new UI::ViewportOrnaments(9);
		_viewportOrnaments->init();

		_textboxOrnaments = new UI::TextboxOrnaments(9);
		_textboxOrnaments->init();

		_inventoryBoxOrnaments = new UI::InventoryBoxOrnaments(9);
		_inventoryBoxOrnaments->init();

		_clock = new UI::Clock();
		_clock->init();
	}

	// Init just the clock (nancy2 and up; nancy1 has no clock, only a map button)
	if (g_nancy->getGameType() >= kGameTypeNancy2) {
		auto *clok = GetEngineData(CLOK);
		if (clok->clockIsDay) {
			// nancy5 uses a different "clock" that mostly just indicates the in-game day
			_clock = new UI::Nancy5Clock();
			_clock->init();
		} else if (!clok->clockIsDisabled) {
			_clock = new UI::Clock();
			_clock->init();
		} else {
			// In nancy7 the clock is entirely disabled
			_clock = nullptr;
		}
	}

	// The Nancy14 standalone camera; its photo album switches the viewfinder on.
	auto *uicm = GetEngineData(UICM);
	if (uicm) {
		_camera = new UI::Camera();
		_camera->init();
	}

	_state = kLoad;
}

void Scene::clearSceneData(bool nextIsNoArt) {
	// Clear generic flags only
	for (uint16 id : g_nancy->getStaticData().genericEventFlags) {
		const int16 index = eventFlagToIndex((int16)id);
		if (index > kEvNoEvent && (uint)index < _flags.eventFlags.size()) {
			_flags.eventFlags[index] = g_nancy->_false;
		}
	}

	clearLogicConditions();

	// Stop a leftover random movie if the outgoing scene didn't include
	// its own PSM(isRandom) AR (so it doesn't bleed into the next scene).
	// A NO_ART_SCENE keeps the previous scene's ambient videos playing, so
	// leave the active movie running in that case.
	if (!nextIsNoArt && _activeMovie && _activeMovie->survivesSceneChange(false) && !_hadRandomMovieARThisScene) {
		_activeMovie->stopRandom();
	}
	_hadRandomMovieARThisScene = false;

	// The active movie is dropped unless it survives this change (a persistent
	// ambient loop). When it survives, clearActionRecords keeps the record alive,
	// so the pointer must be kept too; otherwise it is cleared to avoid dangling.
	bool clearActiveMovie = _activeMovie && !_activeMovie->survivesSceneChange(nextIsNoArt);

	_actionManager.clearActionRecords(nextIsNoArt);

	if (_lightning) {
		_lightning->endLightning();
	}

	if (_textbox.hasBeenDrawn()) {
		// Improvement: the dog portrait scenes in nancy7 queue a piece of text,
		// then immediately change the scene. This makes the text disappear instantly;
		// instead, we check if the textbox has been drawn, and don't clear it if it hasn't.
		// Hopefully this doesn't cause issues with earlier games.
		_textbox.clear();
	}

	_activeConversation = nullptr;

	if (clearActiveMovie) {
		_activeMovie = nullptr;
	}
}

void Scene::clearPuzzleData() {
	for (auto &pd : _puzzleData) {
		delete pd._value;
	}

	_puzzleData.clear();
}

Scene::PlayFlags::LogicCondition::LogicCondition() : flag(g_nancy->_false) {}

} // End of namespace State

} // End of namespace Nancy
