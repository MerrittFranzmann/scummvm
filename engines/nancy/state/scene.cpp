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

#include "engines/nancy/action/datarecords.h"
#include "engines/nancy/action/secondarymovie.h"

#include "engines/nancy/ui/button.h"
#include "engines/nancy/ui/ornaments.h"
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

	// The by-role pointers alias entries of the array that was just emptied.
	// Action records are torn down after the scene on engine shutdown, and
	// StakeOutPuzzle's destructor asks for its panel back, so leaving these
	// dangling would be a use-after-free on the way out.
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

	// A record running inside a Nancy16 stream is moving its own flow. This has
	// to come before the kLoad guard: the stream's move is independent of
	// whatever the main flow happens to be doing this frame.
	if (!bypassStreams && _streams.redirectSceneChange(sceneDescription)) {
		return;
	}

	if (_state == kLoad) {
		return;
	}

	// HACK: Nancy 9 tries to reload the same scene when changing
	// angle/power in scene 5651 (stuck bottle in rocks). This ends up
	// resetting the scene flags, which makes the angle/power buttons
	// unresponsive. We avoid reloading the scene in this case, if the
	// new scene is the same as the current one. This has the negative
	// side-effect that the button arrows are not updated, but at least
	// it makes them usable.
	// TODO: find a better solution for this.
	if (sceneDescription.sceneID == _sceneState.currentScene.sceneID &&
		g_nancy->getGameType() == kGameTypeNancy9 && sceneDescription.sceneID == 5651) {
		return;
	}

	_sceneState.nextScene = sceneDescription;

	// A Nancy16 scene change whose destination is the scene already on screen is
	// a refresh, not a departure, and must not reset the scene's sound.
	//
	// Nancy16 shrank the scene descriptor to six bytes and left continueSceneSound
	// as its last field. That field is zero - kLoadSceneSound - in all 1453 of
	// nancy18's scene-change records (875 type 16, 476 type 19, 66 type 25, 32
	// type 18, 4 type 21), so it expresses no authored intent whatsoever; taking
	// it literally makes every change, including a scene reloading itself, stop
	// channels 0-13 and restart the location music. When the destination is the
	// current scene, "load the destination's sound" and "continue the current
	// sound" name the same sound, and only the latter leaves it running.
	//
	// S6700, the flooded-vault pressure puzzle, cannot work any other way. Its
	// five wheels are ten rotate buttons, and each one reloads S6700 so the
	// gauge overlays re-evaluate. The air supply is a 46-second one-shot,
	// Tunnel_456 on channel 10, started once per attempt and latched by flag
	// 2637; the record that drowns Nancy (Explosion_Underwater01 -> S6702) is
	// gated on exactly "2637 is set and channel 10 has gone quiet". Stopping
	// channel 10 on the reload therefore killed her on the first click of any
	// rotate button, before a single gauge could move.
	if (g_nancy->getGameType() >= kGameTypeNancy16 &&
			_sceneState.nextScene.sceneID == _sceneState.currentScene.sceneID) {
		_sceneState.nextScene.continueSceneSound = kContinueSceneSound;
	}

	_state = kLoad;
}

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

	// "s6428" -> 6428. Anything else means the table said the item has a view
	// scene but did not name one, so there is nowhere to go.
	const Common::String &nm = item->viewSceneName;
	uint16 viewScene = kNoScene;
	if (nm.size() > 1 && (nm[0] == 's' || nm[0] == 'S')) {
		viewScene = (uint16)atoi(nm.c_str() + 1);
	}

	if (viewScene == kNoScene || viewScene == 0) {
		return false;
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
	if (relative == kRelativeClockBump) {
		// Relative, add the specified time to current playerTime
		_timers.playerTime += time;
	} else {
		// Absolute, maintain days but replace hours and minutes
		_timers.playerTime = _timers.playerTime.getDays() * 86400000 + time;
	}

	auto *bootSummary = GetEngineData(BSUM);
	assert(bootSummary);

	_timers.playerTimeNextMinute = g_nancy->getTotalPlayTime() + bootSummary->playerTimeMinuteLength;
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

		uint16 minutes = _timers.playerTime.getHours() * 60 + _timers.playerTime.getMinutes();

		if (minutes >= bootSummary->dayStartMinutes && minutes < bootSummary->dayEndMinutes) {
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
			// Nancy16 has no InventoryPopup - the NDUI INVENTORY panel replaces it,
			// and the popup is never init'd, so touching it walks off empty state.
			// The order list below is data rather than UI and is still maintained.
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
				// Repaints the grid's own panel and nothing else. Picking an item up
				// also changes InvScrollBar's thumb LENGTH, not merely its position,
				// and that bar is in the container panel - but no call belongs here
				// for it: the container notices on its own next frame
				// (NDUIPanel::updateGraphics), which is the one place in this design
				// that has to remember, instead of every site like this one.
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

void Scene::installInventorySoundOverride(byte command, const SoundDescription &sound, const Common::String &caption, uint16 itemID) {
	InventorySoundOverride newOverride;

	switch (command) {
	case kInvSoundOverrideCommandNoSound :
		// Make the sound silent
		newOverride.sound = sound;
		newOverride.sound.name = "NO SOUND";
		newOverride.caption = caption; // Assumes the caption will be empty
		_inventorySoundOverrides.setVal(itemID, newOverride);
		break;
	case kInvSoundOverrideCommandNewSound :
		newOverride.sound = sound;
		newOverride.caption = caption;
		_inventorySoundOverrides.setVal(itemID, newOverride);
		break;
	case kInvSoundOverrideCommandICant :
		// Make the sound the default "I can't use that here"
		newOverride.isDefault = true;
		_inventorySoundOverrides.setVal(itemID, newOverride);
		break;
	case kInvSoundOverrideCommandTurnOff :
		// Remove any previous override
		_inventorySoundOverrides.erase(itemID);
		break;
	default :
		return;
	}
}

// Nancy9 and newer no longer store the "can't" caption alongside the sound.
// Instead, the caption is looked up in the CVTX text chunks by the played
// sound's name: the narration/observations (AUTOTEXT) chunk is searched first,
// then the conversation (CONVO) chunk.
static Common::String getSoundSubtitle(const Common::String &soundName) {
	if (soundName.empty() || soundName.equalsIgnoreCase("NO SOUND")) {
		return Common::String();
	}

	const CVTX *autotext = (const CVTX *)g_nancy->getEngineData("AUTOTEXT");
	if (autotext) {
		Common::String text = autotext->texts.getValOrDefault(soundName, "");
		if (!text.empty()) {
			return text;
		}
	}

	const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
	if (convo) {
		return convo->texts.getValOrDefault(soundName, "");
	}

	return Common::String();
}

void Scene::playItemCantSound(int16 itemID, bool notHoldingSound) {
	// Improvement: nancy2 never shows the caption text, even though it exists in the data; we show it
	auto *inventoryData = GetEngineData(INV);

	// Nancy16 has no INV chunk at all: the inventory table moved into its own
	// INVD member, and INVD carries only id/name/flags/view scene/source rect -
	// no "can't" sounds or captions. Every line below dereferences inventoryData,
	// so the assert was a live crash rather than a debug aid: any click that fell
	// through to a hotspot with an unsatisfied cursor dependency aborted the
	// process. Seen for real in a playthrough attempt, not just in theory.
	// There is nothing to play, so say nothing.
	if (!inventoryData) {
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
				_textbox.addTextLine(inventoryData->cantText, inventoryData->captionAutoClearTime);
			}
		} else {
			// TVD and nancy1 contain no sound data in INV, and have no captions
			g_nancy->_sound->playSound("CANT");
		}
	} else if ((uint)itemID < _flags.items.size()) {
		if (_inventorySoundOverrides.contains(itemID)) {
			// We have an override installed
			InventorySoundOverride &override = _inventorySoundOverrides[itemID];
			if (!override.isDefault) {
				// Not set to the default sound, play the override
				g_nancy->_sound->loadSound(override.sound);
				g_nancy->_sound->playSound(override.sound);

				if (ConfMan.getBool("subtitles")) {
					_textbox.addTextLine(override.caption, inventoryData->captionAutoClearTime);
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
						_textbox.addTextLine(inventoryData->cantText, inventoryData->captionAutoClearTime);
					}
				} else {
					// Should be unreachable
					g_nancy->_sound->playSound("CANT");
				}
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
				cantText = getSoundSubtitle(cantSound.name);
				if (cantText.empty()) {
					cantText = item.cantTexts[soundIndex];
				}
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
				_textbox.addTextLine(inventoryData->cantText, inventoryData->captionAutoClearTime);
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

// The 1010-1059 per-scene scratch flags belong to whichever flow is executing,
// not to the game. Returns the index into the executing stream's private array,
// or -1 when the label is not scratch or the main flow is running. See the note
// in streams.h for the S5400/s5450 pair that forces this.
int Scene::scratchFlagIndex(int16 label) const {
	if (!_streams.executing()) {
		return -1;
	}

	const Common::Array<uint16> &generic = g_nancy->getStaticData().genericEventFlags;
	if (generic.empty()) {
		return -1;
	}

	// The table is contiguous by construction (populateStaticData fills it with
	// base + i), so the ends bound it.
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

// On first use, seed each resource value from the UIRC boot chunk (record id =
// initial value). After a save is loaded `seeded` is already true, so the
// restored values are kept.
static void seedUIResourceData(UIResourceData *data) {
	if (!data || data->seeded) {
		return;
	}

	const UIRC *uirc = GetEngineData(UIRC)

	data->seeded = true;
	if (uirc) {
		data->values.resize(uirc->items.size());
		for (uint i = 0; i < uirc->items.size(); ++i) {
			data->values[i] = uirc->items[i].id;
		}
	}
}

int32 Scene::getUIResource(uint index) {
	UIResourceData *data = (UIResourceData *)getPuzzleData(UIResourceData::getTag());
	seedUIResourceData(data);
	if (!data || index >= data->values.size()) {
		return 0;
	}
	return data->values[index];
}

void Scene::setUIResource(uint index, int32 value) {
	UIResourceData *data = (UIResourceData *)getPuzzleData(UIResourceData::getTag());
	seedUIResourceData(data);
	if (data && index < data->values.size()) {
		data->values[index] = value;
	}
}

// Nancy 11+ AR 30/31 store the "player scrolling disabled" state in an event
// flag (eventData[0x21] in the original). It persists across scenes and is
// saved/restored together with the rest of the event flags. Nancy12 shifted the
// engine's generic flag numbering up by 10, moving this flag from 1033 to 1043.
static int16 playerScrollingDisabledFlag() {
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

bool Scene::getNthHotspotCentre(uint n, Common::Point &out) {
	Common::Array<Common::Rect> live;
	for (auto &rec : _actionManager.getActionRecords()) {
		if (rec->_isActive && !rec->_isDone && rec->_hasHotspot && rec->_hotspot.isValidRect()) {
			Common::Rect screen = _viewport.convertViewportToScreen(rec->_hotspot);
			if (!screen.isEmpty()) {
				live.push_back(screen);
			}
		}
	}

	// A conversation's replies are not action-record hotspots - they are laid
	// out into the CONVO NDUI panel at runtime - so an explorer that only walks
	// the records sees a live conversation as a screen with nothing on it and
	// can never answer. Every one of the game's conversations would stall.
	for (uint i = 0; i < _conversationPanels.size(); ++i) {
		_conversationPanels[i]->debugGetConvoResponseRects(live);
	}

	if (live.empty()) {
		return false;
	}

	// ANTI-PING-PONG. `n` is a plain counter, so in a two-scene pair whose
	// close-ups link only to each other the explorer alternates A-B-A-B for the
	// rest of the run and learns nothing. Measured before this filter, 80 s per
	// start scene: S3523 gave 43 entries over 6 distinct scenes with 20 A-B-A
	// triples, and the two-scene trap in the phone close-ups is what cost
	// DRIVE2.md's tier-3 explorer its whole budget.
	//
	// The fix needs no offline corpus: remember where each click actually went,
	// and skip any candidate whose learned destination is the scene we just
	// came from. If that would leave nothing to click - a dead-end close-up
	// whose only exit is the way back in - take the whole set instead, so the
	// explorer can never be made less live than it was.
	//
	// The memo is keyed on (scene, viewport frame, point) because a panoramic
	// scene scopes each hotspot to one frame, so the same screen point on two
	// frames is two different edges.
	//
	// THE BAN HAS TO EXPIRE, and finding that out cost a measurement. "Unless
	// nothing else is available" is not enough: at S5813 the shop's two other
	// hotspots are the refusal pair that reloads the scene, so the candidate set
	// was never empty and the only working exit stayed banned for ever - 7 scene
	// entries in 80 s against 15 without the filter. So the ban lasts only while
	// every live hotspot has not yet had a turn in this visit; after that the
	// back edge is fair game again, which makes the explorer provably no less
	// live than it was before while still breaking the two-scene pairs.
	const int here = (int)_sceneState.currentScene.sceneID;
	const int frame = (int)_viewport.getCurFrame();
	++_exploreTriesHere;

	Common::Array<uint> pick;
	if (_exploreTriesHere <= (int)live.size()) {
		for (uint i = 0; i < live.size(); ++i) {
			const int cx = live[i].left + live[i].width() / 2;
			const int cy = live[i].top + live[i].height() / 2;
			const int dest = debugExploreEdgeDest(here, frame, cx, cy);
			if (dest < 0 || dest != _explorePrevScene) {
				pick.push_back(i);
			}
		}
	}

	if (pick.empty()) {
		for (uint i = 0; i < live.size(); ++i) {
			pick.push_back(i);
		}
	}

	const Common::Rect &r = live[pick[n % pick.size()]];
	out = Common::Point(r.left + r.width() / 2, r.top + r.height() / 2);

	// Arm the memo. The click itself is synthesised by InputManager on this same
	// poll, so the next scene entry is what this choice led to.
	debugExploreArm(here, frame, out.x, out.y);
	return true;
}

// The learned destination of one click point, or -1 if it has never been taken.
// A linear scan: a run learns a few hundred edges at most and this is called
// once per explorer decision, so an index would cost more than it saves.
int Scene::debugExploreEdgeDest(int scene, int frame, int x, int y) const {
	for (uint i = 0; i < _exploreEdges.size(); ++i) {
		const DebugExploreEdge &e = _exploreEdges[i];
		if (e.scene == scene && e.frame == frame && e.x == x && e.y == y) {
			return e.dest;
		}
	}

	return -1;
}

void Scene::debugExploreArm(int scene, int frame, int x, int y) {
	for (uint i = 0; i < _exploreEdges.size(); ++i) {
		const DebugExploreEdge &e = _exploreEdges[i];
		if (e.scene == scene && e.frame == frame && e.x == x && e.y == y) {
			_explorePendingIdx = (int)i;
			return;
		}
	}

	DebugExploreEdge e;
	e.scene = scene;
	e.frame = frame;
	e.x = x;
	e.y = y;
	e.dest = -1;
	_exploreEdges.push_back(e);
	_explorePendingIdx = (int)_exploreEdges.size() - 1;
}

// Called on every scene entry, whether or not any harness key is set: this is
// bookkeeping for getNthHotspotCentre above, not tracing, and it has to stay
// correct on runs that have tracing off.
//
// Honest limit: an armed edge is credited with whatever scene arrives next, so
// a scene change caused by something other than the explorer's click - a movie
// ending, a timer - can teach the memo a wrong destination. The window is only
// as long as the explorer's own cadence, because the next decision re-arms, and
// the consequence is bounded: at worst one hotspot is skipped once, and the
// empty-set fallback above means it can still be reached.
void Scene::debugNoteSceneEntry() {
	const int now = (int)_sceneState.currentScene.sceneID;

	if (_explorePendingIdx >= 0 && (uint)_explorePendingIdx < _exploreEdges.size()) {
		DebugExploreEdge &e = _exploreEdges[_explorePendingIdx];
		if (e.scene == _exploreCurScene) {
			e.dest = now;
		}

		_explorePendingIdx = -1;
	}

	_explorePrevScene = _exploreCurScene;
	_exploreCurScene = now;
	_exploreTriesHere = 0;
}

void Scene::setConversationUIVisible(bool visible) {
	for (uint i = 0; i < _conversationPanels.size(); ++i) {
		_conversationPanels[i]->setVisible(visible);
	}
}

void Scene::applyNDUICommand(const Common::String &target, uint32 commandID) {
	// Both lists are re-derived from the event flags when they are opened rather
	// than accumulated as the game runs, so the content has to be in place before
	// the panel goes on screen - a Show that composed an empty box and then
	// filled it would flash.
	if (commandID == kNDUICommandShow) {
		if (target.equalsIgnoreCase("JournalDialog")) {
			refreshJournal();
		} else if (target.equalsIgnoreCase("TasklistDialog")) {
			refreshTasklist();
		}
	} else if (commandID == kNDUICommandHide && target.equalsIgnoreCase("JournalDialog")) {
		// JournalDialog's own OnHide bindings take the four pages off screen; this
		// is the selection that went with them, so a re-opened journal comes back
		// to the heading list rather than to a highlighted heading with no page.
		_journalPage = -1;
	}

	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		if (_nduiPanels[i]->applyCommand(target, commandID)) {
			return;
		}
	}
}

// Deliberately unchanged by the thumb work, and the absence is the point. The
// list and the bar that scrolls it are in different panels - InvDialog is
// INVENTORY chunk[4] while InvScrollBar is chunk[2] - so this repaints the list
// and leaves the thumb where it was, and a repaint of the bar's panel is NOT
// bolted on here. It would be one more caller to keep in step, it would still
// miss the movement keys, the two whole-tree recompose loops and
// selectJournalPage, and the panel holding the bar settles itself a moment later
// in NDUIPanel::updateGraphics regardless.
void Scene::applyNDUIScroll(const Common::String &target, int delta) {
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		if (_nduiPanels[i]->ownsControl(target)) {
			_nduiPanels[i]->listScrollBy(delta);
		}
	}
}

// Read twin of applyNDUIScroll. Two asymmetries against it, both deliberate:
//
//   * this stops at the first answer, where applyNDUIScroll keeps going. One bar
//     has one thumb, so it needs one position. The write side's fan-out -
//     JournalDetailsScrollBar names all four VENJH pages and scrolls the three
//     hidden ones too - is pre-existing and left alone.
//   * this filters on isVisible(), where the write side does not, and that
//     filter IS the live-page rule. selectJournalPage() shows one page panel and
//     hides the other three by NDUI command on their ROOT names, and
//     NDUIPanel::applyCommand answers a command aimed at its own root by calling
//     setVisible(). So _isVisible is written by the very command that picks the
//     page: reading it back is not a proxy for the rule, it is the rule. The
//     same line correctly rejects a hidden ConvoDialog, TasklistDialog,
//     InvDialog or StakeOutDialog, which is one rule for all six bars.
//
// Rejected: _journalPage, an index into _journalPagePanels, which would make the
// read side journal-specific; and NDUIPanel's _visible map, which is wrong for
// CONVO, because setConversationUIVisible() calls RenderObject::setVisible()
// directly and never touches the map, so the two can disagree with the screen.
//
// A freshly opened journal needs no special case. No page is shown until a
// heading is clicked, so nothing matches and the details bar keeps an empty
// thumb - which is the required behaviour, not a degradation. When the heading
// IS clicked, selectJournalPage shows the page and never touches the panel
// holding the details bar; the thumb appears because NDUIPanel::updateGraphics
// re-asks this question every frame and sees the answer change from "nothing" to
// a model. That per-frame caller is why this stays a pure read: it must not
// recompose, lay out or load anything, and it does not.
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

// --- The journal and the task list -------------------------------------
//
// Nancy16 keeps neither list in the scene scripts. Each is a member of
// ciftree.dat named <LIST>_<character> - a TSUM plus one action record per
// possible entry - and the record's own dependencies decide whether the player
// has earned it yet. So there is nothing to accumulate and nothing to save: the
// list is a pure function of the event flags, re-derived every time the panel is
// opened. See action/datarecords.h for the two payload layouts.
//
// Which members: `PCUI` names the player characters and their trees, and the four
// members' suffixes are exactly those tree names with the "PUI_" stripped -
// JOURNAL_DEFAULT / JOURNAL_NANCY against PUI_Default / PUI_Nancy. Both are read,
// in PCUI order, and their entries concatenated. Read as *alternatives* one of
// the two would be dead data; read as a base list plus the character's, both are
// live and the timeline closes:
//
//   * the _DEFAULT records carry no dependencies at all, while every _NANCY
//     record needs a flag first raised in S3552 (EV_Woke_Up / EV_Met_Opening_Cine,
//     and S3552 is the only record in the game that sets 2272);
//   * the task-list button is *enabled* (AR 34 command 8) in S6458, in the desk
//     prologue, which runs before S3552 - so with only _NANCY loaded that button
//     opens an empty panel for the whole prologue;
//   * TASKLIST_DEFAULT's one entry is "Check out the Case File folder on my desk
//     to learn about my current assignment", the prologue's one action, and its
//     completion flag is EV_Met_Opening_Cine - it ticks exactly as the prologue
//     ends and the _NANCY entries become available.
//
// The dependency sets and the flag S3552 raises are byte-exact; that the two
// members are meant to be concatenated is the reading those facts support.
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

// Reads a list member and returns the records whose dependencies are satisfied,
// in file order. The caller owns them.
//
// The dependency evaluation is the engine's own: these really are action records,
// so re-implementing the event-flag, difficulty, OR and parenthesis logic here
// would be a second implementation to keep in step with the first. They are never
// *executed* - their execute() does nothing but finish - and the caller reads the
// fields directly.
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
			// The tick is the completion flag, not a click: nancy18's record
			// carries only the flag, where Nancy10-15's carried a mark the player
			// toggled. Nothing in the data holds a per-entry checked state, so
			// there is nowhere for a manual tick to live.
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
			// The record names the page by the root control of the panel it goes
			// in - VENJH1..VENJH4 - so the two are matched by name rather than by
			// an index this code would have to invent.
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

	// The heading list. Each page's caption is its own root control name resolved
	// in AUTOTEXT: VENJH1 "Observations", VENJH2 "Suspects", VENJH3 "Clues",
	// VENJH4 "Phone Numbers". A heading with nothing filed under it still shows -
	// "Clues" has no entries in the whole game - because the original ships four
	// pages and hides none of them.
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

// The load screen's "New" button, and MODAL[1]'s full-screen invisible one, both
// send Invoke(Engine_Loader, "_StartNewGame_") followed by Notify - select an
// entry, then commit it - with nothing in between and no restart verb anywhere in
// the vocabulary. So the game models "new game" as *a load of a named slot*, and
// the slot it means is the one its own first scene writes: S1 carries two type 114
// records, both naming "Start_Game", and nine type 115 records elsewhere in the
// game read it back to send the player to the beginning.
//
// Matched case-insensitively, which is not tidiness: three of the sixteen type 115
// records spell it "start_game" while every type 114 writes "Start_Game".
//
// The consequence worth stating plainly: the slot does not exist until the game
// has run S1 once, so on a profile that has never booted normally - a run started
// with nancy_start_scene, or a save copied in from elsewhere - there is nothing to
// load and New reports that rather than inventing a restart. nancy has no
// restart-in-place, and synthesising one out of destroyState()/setState() would
// put a whole savegame's worth of state behind a guess.
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

	// Through the same queue AR 115 uses: the load destroys the Scene whose input
	// handler is asking for it, so it cannot happen on that handler's stack.
	g_nancy->requestNamedLoad(kStartGameSaveName);
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

// --- The options screen ------------------------------------------------
//
// OPTIONS NDUI[0] has twelve controls. Eleven of them are settings, and every one
// aims a SetValue/Notify pair at an "Engine_*" pseudo-target; the twelfth is the
// close button. Which ScummVM setting each pseudo-target is was established from
// the data rather than from the control names - see OPTIONS.md for the full
// derivation, in brief:
//
//   Engine_VoiceVolume  auditions ND_VEN027Sample, a copy of VEN027, and VEN027
//                       has a subtitle in the AUTOTEXT table ("Mi chiamo
//                       Nancy."). Only speech is captioned.   -> speech_volume
//   Engine_FXVolume     auditions water_dropSample, of the water_drop_* family
//                       that S6009/S6420 play as environment effects; it has no
//                       caption and is in no music list.      -> sfx_volume
//   Engine_MusicVolume  auditions NDSigSample, a trim of NDSig - which BOOT's
//                       ENVS table lists as the music of environment "LET",
//                       "Opening and Closing Letters".        -> music_volume
//   Engine_ClosedCaption  the engine already gates PlaySound's caption on
//                       ConfMan "subtitles".                  -> subtitles
//   Engine_Matte        parameters 4.0 and 5.0 are COLR chunk ids: COLR 4 is
//                       0xff000000, and the retail INI ships
//                       BackgroundColor=4278190080, the same number.
//   Engine_GameWindow   parameters 0.0/1.0/2.0 are the retail INI's WindowMode,
//                       whose own comment reads "0=Standard (CRT), 1=Windowed,
//                       2=Widescreen(LCD)".
//   Engine_Font         parameters 100.0 and 127.0 are a percentage: the INI
//                       ships FontScale=1270, which is 127.0 x 10.
//
// SetValue applies; Notify applies and writes the configuration out. That split
// is what the sliders need - the volume follows the thumb, and only letting go
// commits - and it costs the radio buttons nothing, since a click on one sends
// only the Notify.
bool Scene::applyNDUISetting(const Common::String &target, uint32 commandID, double value) {
	if (commandID != kNDUICommandSetValue && commandID != kNDUICommandNotify) {
		return false;
	}

	const Common::String &domain = ConfMan.getActiveDomainName();
	const int rounded = (int)(value + (value < 0 ? -0.5 : 0.5));

	// The three volume sliders. ScummVM's mixer reads these three keys and nothing
	// else, so writing them is what makes the setting real instead of a parallel
	// store the engine would then have to apply by hand.
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
			// A COLR id the palette does not have. Leaving the matte alone beats
			// inventing a colour for it; nancy18 ships ids 0..8 and the screen
			// only ever sends 4 and 5.
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

		// Every face is rebuilt at the new size, so everything already rasterised
		// has to be composed again. In this engine that is exactly the NDUI
		// panels: they are the only consumers of the provider.
		//
		// Order is not this loop's problem, and must not become it. _nduiPanels is
		// in chunk order, which composes every routed scroll bar's panel BEFORE the
		// panel whose list it describes, so each thumb here is composed from its
		// target's pre-loop state. NDUIPanel::updateGraphics catches that on the way
		// to the screen - which is the only reason a plain loop is still correct
		// here, and the reason a future one will be too.
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

	// Engine_GameWindow answers to Hide as well, which is the quit; only SetValue
	// is the display mode, and this function only ever sees SetValue and Notify.
	if (target.equalsIgnoreCase("Engine_GameWindow")) {
		const int mode = CLIP<int>(rounded, 0, 2);
		ConfMan.setInt(kConfWindowMode, mode, domain);

		// Mode 1 is the game's "Windowed"; 0 and 2 are its two full-screen modes,
		// and the difference between them is which display they were meant for -
		// a 4:3 CRT, whose whole screen the 800x600 image filled, against an LCD
		// or widescreen panel, where it does not. ScummVM expresses that as the
		// stretch mode, and mapping "Standard (CRT)" onto stretch and
		// "Widescreen(LCD)" onto fit is a reading of those two INI comments, not
		// something the data states.
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
		// Cross-checked against ScummVM's own key rather than trusted, because
		// that key is the one the launcher and the ScummVM options dialog write.
		// A stored "widescreen full screen" with fullscreen off would otherwise
		// leave the screen claiming a mode it is not in.
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

// The "Engine_*" targets. Each one is a subsystem, and the command vocabulary is
// reused against all of them with a per-subsystem meaning:
//
//   Engine_Loader     Show open the load screen, Hide close it, Invoke select an
//                     entry, Notify commit the selection.
//   Engine_Saver      the same four against the save screen.
//   Engine_SaveQuery  Show = yes, Hide = no, Clear = dismiss, on SAVEGAME[1] -
//                     which is not an overwrite prompt: its caption UISAV02 is
//                     "Do you want to save first?", so it is what stands between
//                     the player and losing progress on the way out.
//   Engine_GameWindow the game's own window: Hide quits, SetValue picks a video
//                     mode, Set/Clear enable and disable it behind a modal.
//
// Every one of those readings is taken off the widgets that send the command;
// the derivation is written up in the run's BUTTONS.md. The subsystems that are
// already handled inside NDUIPanel (Engine_Inv, Engine_Flags) and the ones that
// are only ever read rather than commanded (Engine_Index) return false here and
// fall through to it.
bool Scene::applyNDUIEngineCommand(const Common::String &target, uint32 commandID,
		const Common::String &paramString, double value) {
	const bool isLoader = target.equalsIgnoreCase("Engine_Loader");
	const bool isSaver = target.equalsIgnoreCase("Engine_Saver");

	// The options screen first: Engine_GameWindow is on both lists, because Hide
	// against it quits while SetValue against it picks a display mode.
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
			// Only the taskbar sends this; nothing inside the dialog does. The
			// list is read at open time rather than kept live, so a save written
			// from the other screen is picked up without either screen having to
			// know about the other.
			panel->saveLoadRefresh();
			_loaderNewGameSelected = false;

			if (isSaver) {
				// Seed the name field with the scene the player is in, which is
				// the closest thing the engine has to the original's default and
				// beats an empty box that would save as "".
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
				// Backing out of the save screen cancels the quit it was opened
				// for: the player asked to save before leaving and then did not
				// save, so leaving is no longer what they asked for.
				applyNDUICommand("SaveQueryDialog", kNDUICommandHide);
				_quitAfterSave = false;
			}

			return true;
		case kNDUICommandInvoke:
			// Selecting an entry. The list rows are laid out by the panel and it
			// records the click itself; what reaches here is the bare Invoke the
			// ListBox also sends, and LoadNewGame's, which names a pseudo-entry.
			//
			// "_StartNewGame_" is selected and then committed, with nothing in
			// between - LoadNewGame sends Invoke then Notify, and MODAL[1] does the
			// same pair independently. That is a *load of a named entry*, not a
			// restart verb, so it is held here exactly like a picked row and acted
			// on by the Notify below.
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

			// Saves are keyed by name, exactly as the data has it: the authored
			// binding for a click on SaveList is a *pair*, Invoke to Engine_Saver
			// and Invoke to SaveName, i.e. picking a row loads its name into the
			// field. Saving under a name that is already there therefore means
			// replacing it. There is no separate "overwrite?" prompt to raise -
			// the only Yes/No box in SAVEGAME asks "Do you want to save first?",
			// which is a different question - so this replaces silently.
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
				// A new name: the lowest free slot below the reserved band, which
				// is where ScummVM's own save dialog puts a new save too.
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

			// Close first, save second. The panels' show/hide state is part of
			// the savegame (registry Y15), so saving with the dialog still up
			// writes "the save dialog is open" into the file and the player
			// resumes staring at it.
			applyNDUICommand(rootName, kNDUICommandHide);

			const Common::Error err = g_nancy->saveGameState(slot, name, false);
			if (err.getCode() != Common::kNoError) {
				warning("NDUI save '%s' to slot %d failed: %s",
					name.c_str(), slot, err.getDesc().c_str());
				return true;
			}

			debugC(1, kDebugEngine, "NDUI save '%s' written to slot %d", name.c_str(), slot);

			// This save was the answer to "do you want to save first?", so now
			// go where the player was heading.
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

	// SAVEGAME[1]: "Do you want to save first?", Yes / No / X. Its three buttons
	// send Show, Hide and Clear, in that order, which is the whole vocabulary -
	// the box carries no other state, so the answer is read straight off the
	// command id.
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

	// The two list subsystems. JournalDialog's OnShow sends Show(Engine_Journal)
	// and TasklistDialog's OnHide sends Hide(Engine_Tasklist); JournalItems and
	// TasklistEntry each send Invoke/Notify on a click. All four are already
	// covered - applyNDUICommand refreshes the list before the panel is composed,
	// and the panel records its own row clicks the way the conversation replies
	// and the save rows do - so these are answered rather than left to fall
	// through into the widget vocabulary, where the name matches nothing.
	if (target.equalsIgnoreCase("Engine_Journal") || target.equalsIgnoreCase("Engine_Tasklist")) {
		return true;
	}

	if (target.equalsIgnoreCase("Engine_GameWindow")) {
		if (commandID == kNDUICommandHide) {
			// Not a bare quit. The game authors a "Do you want to save first?"
			// box and nothing else in the data can raise it: the only other
			// candidate trigger, LoadNewGame, is a Loader verb. So the way out
			// goes through it. Deliberately not gated on
			// canSaveGameStateCurrently() - that is ScummVM's policy for its own
			// menu, and here it would turn the box into an intermittent one,
			// which is worse than a Save that occasionally has nothing good to
			// write. The box's third answer is "neither", so it earns its place
			// even when saving is pointless.
			if (findNDUIPanelWithControl("SaveQueryDialog")) {
				applyNDUICommand("SaveQueryDialog", kNDUICommandShow);
			} else {
				g_nancy->quitGame();
			}

			return true;
		}

		// SetValue is the video mode, handled by applyNDUISetting above. Set and
		// Clear are the modal enable/disable, and a disabled state nothing draws
		// is indistinguishable from an enabled one.
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
}

// Debug affordance: every set event flag, not the first sixteen. The
// fingerprint deliberately truncates its list, but diagnosing "which flag is
// this gate missing" needs the whole set.
Common::String Scene::debugAllEventFlags() const {
	Common::String out;
	for (uint i = 0; i < _flags.eventFlags.size(); ++i) {
		if (_flags.eventFlags[i] == g_nancy->_true) {
			out += Common::String::format("%s%u", out.empty() ? "" : ",", i);
		}
	}

	return out;
}

Common::String Scene::getStateFingerprint() const {
	Common::String items;
	uint numItems = 0;
	for (uint i = 0; i < _flags.items.size(); ++i) {
		if (_flags.items[i] == g_nancy->_true) {
			++numItems;
			items += Common::String::format("%s%u", numItems > 1 ? "," : "", i);
		}
	}

	// The full list would be hundreds of entries, so the count is the identity
	// and the head of the list is there to make a mismatch readable.
	Common::String eventFlags;
	uint numEventFlags = 0;
	for (uint i = 0; i < _flags.eventFlags.size(); ++i) {
		if (_flags.eventFlags[i] == g_nancy->_true) {
			++numEventFlags;
			if (numEventFlags <= 16) {
				eventFlags += Common::String::format("%s%u", numEventFlags > 1 ? "," : "", i);
			}
		}
	}

	// Nancy16 parallel flows (see streams.h). A stream is scene state a load can
	// drop just as quietly as a puzzle chunk, and the phone-ring case makes the
	// drop unrecoverable, so it belongs in the fingerprint.
	const Common::String streams = _streams.describe();

	// Nancy16 collections (ARs 37/38/39). They are puzzle state that outlives a
	// scene change, so a load that dropped them would leave a keypad half-typed.
	Common::String collections;
	if (_puzzleData.contains(CollectionData::getTag())) {
		const CollectionData *cd = (const CollectionData *)_puzzleData[CollectionData::getTag()];
		for (auto &c : cd->collections) {
			collections += Common::String::format("%s%s:", collections.empty() ? "" : " ", c._key.c_str());
			for (uint i = 0; i < c._value.size(); ++i) {
				collections += c._value.isCharacter ?
					Common::String::format("%s", c._value.strings[i].c_str()) :
					Common::String::format("%g", c._value.numbers[i]);
			}
		}
	}

	// _puzzleData is wiped and rebuilt from the file by a load, and by nothing
	// else short of the destructor, so it is the part of the state a load is
	// most able to quietly drop. The tags are four-character codes; printing
	// them makes it visible which blocks came back rather than just how many.
	Common::String puzzleData;
	uint numPuzzleData = 0;
	for (const auto &pd : _puzzleData) {
		const uint32 tag = pd._key;
		puzzleData += Common::String::format("%s%c%c%c%c", numPuzzleData > 0 ? "," : "",
			(char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8), (char)tag);
		++numPuzzleData;
	}

	// Nancy16 NDUI show/hide deltas (registry Y15). Same reasoning as the
	// streams and the puzzle chunks: it is scene state a load can quietly drop,
	// and the drop is invisible in every other field. Deltas only, so a run that
	// never touched the UI prints ndui=0[] and the line stays readable. Sorted
	// by panel, then in whatever order the panel reports - the counts and the
	// set are what a round trip is compared on.
	Common::String ndui;
	uint numNDUI = 0;
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		Common::StringArray shownNames, enabledNames;
		Common::Array<byte> shownValues, enabledValues;
		_nduiPanels[i]->getStateDelta(shownNames, shownValues, enabledNames, enabledValues);

		for (uint j = 0; j < shownNames.size(); ++j) {
			ndui += Common::String::format("%s%s.%s%c", numNDUI > 0 ? "," : "",
				_nduiPanels[i]->getPanelName().c_str(), shownNames[j].c_str(),
				shownValues[j] ? '+' : '-');
			++numNDUI;
		}

		for (uint j = 0; j < enabledNames.size(); ++j) {
			ndui += Common::String::format("%s%s.%s%c", numNDUI > 0 ? "," : "",
				_nduiPanels[i]->getPanelName().c_str(), enabledNames[j].c_str(),
				enabledValues[j] ? 'E' : 'D');
			++numNDUI;
		}
	}

	return Common::String::format(
		"scene=%u frame=%u vofs=%u held=%d items=%u[%s] eventflags=%u[%s] scenecounts=%u playertime=%u difficulty=%u collections=[%s] puzzledata=%u[%s] streams=%u[%s] env=%s ndui=%u[%s] sfxchannels=[%s]",
		_sceneState.currentScene.sceneID, _sceneState.currentScene.frameID,
		_sceneState.currentScene.verticalOffset, _flags.heldItem,
		numItems, items.c_str(), numEventFlags, eventFlags.c_str(),
		(uint)_flags.sceneCounts.size(), (uint32)_timers.playerTime, _difficulty,
		collections.c_str(), numPuzzleData, puzzleData.c_str(),
		_streams.size(), streams.c_str(),
		_streams.getEnvironment().empty() ? "-" : _streams.getEnvironment().c_str(),
		numNDUI, ndui.c_str(),
		g_nancy->_sound->describeActiveChannels().c_str());
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

	auto &order = getInventoryBox().getOrder();
	uint prevSize = order.size();
	order.resize(g_nancy->getStaticData().numItems);

	if (ser.isSaving()) {
		for (uint i = prevSize; i < order.size(); ++i) {
			order[i] = -1;
		}
	}

	ser.syncArray(order.data(), g_nancy->getStaticData().numItems, Common::Serializer::Sint16LE);

	while (order.size() && order.back() == -1) {
		order.pop_back();
	}

	if (ser.isLoading() && g_nancy->getGameType() <= kGameTypeNancy9) {
		// Make sure the shades are open if we have items
		getInventoryBox().onReorder();
	}

	ser.syncArray(_flags.items.data(), g_nancy->getStaticData().numItems, Common::Serializer::Byte);
	ser.syncAsSint16LE(_flags.heldItem);
	g_nancy->_cursor->setCursorItemID(_flags.heldItem);

	if (g_nancy->getGameType() >= kGameTypeNancy7) {
		ser.syncArray(_flags.disabledItems.data(), g_nancy->getStaticData().numItems, Common::Serializer::Byte);
	}

	ser.syncAsUint32LE((uint32 &)_timers.lastTotalTime);
	ser.syncAsUint32LE((uint32 &)_timers.sceneTime);
	ser.syncAsUint32LE((uint32 &)_timers.playerTime);
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
			// genericEventFlags holds labels; every other access converts with
			// eventFlagToIndex(), which subtracts 1000. Indexing raw here wrote
			// the story-flag range instead and left the generic flags set for
			// the whole game.
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
		// Replace, do not merge. Every other container in this function is
		// written wholesale - the event flags and both item arrays are plain
		// syncArrays, _puzzleData calls clearPuzzleData() and _streams calls
		// endAll() - and this one was the exception: setVal() over whatever the
		// live session had accumulated, so any scene visited *after* the save
		// was taken kept its live count, because its key is simply not in the
		// file to overwrite it.
		//
		// That is the "Try Again" bug. The chase gives up on a kSceneCount
		// dependency counting 20 entries to s5450 (streams.cpp), the
		// "2nd Chance - Final Chase" checkpoint is written by S5400's first
		// record - before the VillianChase stream that is the only thing that
		// ever enters s5450 exists - so the file holds no key 5450 at all, and
		// the retry inherited the timed-out count of 21 and lost on Nancy's
		// first move. Worse, S5400 re-runs its own checkpoint record after the
		// load, so the merged 5450:21 was then written back into the file and
		// the second chance stayed dead for the rest of the game.
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

		// Restore the taskbar disable overrides now that the persisted
		// TaskbarData is available. A disable can be set from an earlier
		// scene's AR that won't re-run here, so it has to come from the save.
		if (_taskbar && g_nancy->getGameType() >= kGameTypeNancy10) {
			_taskbar->syncFromPuzzleData();
			_taskbar->updateNotificationStates(_sceneState.currentScene.sceneID);
		}
	}

	// Nancy16's parallel flows. A load mid-stream that dropped them would leave
	// the phone ringing with nothing able to stop it, so the running streams and
	// the script each is on are part of the save. Their records are rebuilt by
	// reloading the script, exactly as the main flow's are. Written last so the
	// main scene, the event flags and the scene counts are all already in place.
	_streams.synchronize(ser);

	if (ser.isLoading()) {
		// The ambient environment is not in the save (see the note in streams.h),
		// and the endAll() above has just cleared whatever Scene::load() derived
		// a moment ago - a load runs load() first and deserialises over the top.
		// Re-derive it here or the location stays silent until the player's next
		// step out of the scene the save restored into.
		updateEnvironment();
	}

	// Nancy16 replaced the popups with NDUI panels, and those hold state that no
	// longer follows from _flags once a load has written it wholesale.
	if (ser.isLoading() && g_nancy->getGameType() >= kGameTypeNancy16) {
		if (_inventoryPanel) {
			// The grid is only redrawn when addItemToInventory or
			// removeItemFromInventory poke it. A load writes _flags.items and the
			// order list straight from the file and goes through neither, so
			// without this the panel keeps showing the pre-load inventory.
			_inventoryPanel->refreshInventory();

			// InvScrollBar's thumb is stale too - a load can change the item count by
			// any amount - and is deliberately not repainted from here. It cannot be:
			// synchronizeNDUI below recomposes every panel in _nduiPanels order,
			// which puts the container holding the bar BEFORE the grid it reads, so
			// anything done here is overwritten a few lines later with the thumb the
			// container composed while the grid was still hidden. That is the bug
			// this ordering used to produce; NDUIPanel::updateGraphics is what fixes
			// it, by checking after every recompose rather than before one.
		}

		if (_conversationPanel) {
			// The conversation box is not part of the save: a save can only be
			// taken while no conversation is running (canSaveGameStateCurrently),
			// so "closed" is the state to restore into. Without this an in-session
			// load leaves the last line of dialogue on screen, where a --save-slot
			// boot - which builds the panels hidden - does not.
			_conversationPanel->convoClose();
		}
	}

	// Nancy16 NDUI show/hide deltas (registry Y15). Last, so that on a load the
	// panels have already had every other fixup applied to them and the save is
	// what gets the final word on what is on screen.
	synchronizeNDUI(ser);

	_isRunningAd = false;
	ConfMan.removeKey("restore_after_ad", Common::ConfigManager::kTransientDomain);

	g_nancy->_graphics->suppressNextDraw();
}

// Registry Y15. The NDUI widget tree's visibility is changed at runtime and none
// of it follows from _flags, so without this a load restored the authored tree:
// most visibly the HUD went away, because the only thing that reveals CoinPurse
// and ShowPaperDoll is a one-shot Show in S3552 that a resumed session never
// re-runs.
//
// Only the delta against the state each panel was built in is stored, keyed on
// (panel root name, control name). Control names are unique only within a panel
// - every panel owns a "Tooltip" - while panel root names are unique across the
// twelve panels nancy18 loads, so the pair is a stable key that does not depend
// on the order initStaticData() happens to build them in.
void Scene::synchronizeNDUI(Common::Serializer &ser) {
	// Version 9 introduced the block. Nothing before it wrote one, and the 36
	// converted originals carry no NDUI state either, so an older save simply
	// leaves the panels as the engine built them - which is what it did before.
	if (ser.getVersion() < 9 || g_nancy->getGameType() < kGameTypeNancy16) {
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

	// Read the whole block before touching anything, then apply it to every
	// panel - including the ones the save does not mention, which get an empty
	// delta and so go back to how they were built. An in-session load (the
	// second-chance AR 115) runs against panels that still carry the pre-load
	// session's deltas, and only a full reset can undo those.
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

	// applyStateDelta re-composes and then settles the panel's own visibility, so
	// in _nduiPanels order - chunk order - INVENTORY[2] composes InvScrollBar's
	// thumb while INVENTORY[4] is still hidden and reports no model, and the thumb
	// comes back empty from a save taken with ten or more items and the inventory
	// open. Deliberately NOT fixed by reordering or by a second pass here:
	// _nduiPanels' order is load-bearing elsewhere (Scene::init tells the four
	// VENJH pages apart by their position in it), and a second pass would cover
	// this loop and no future one. NDUIPanel::updateGraphics settles every routed
	// thumb after any recompose, including this one.
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
	_timers.playerTime = bootSummary->startTimeHours * 3600000;
	_timers.sceneTime = 0;
	_timers.timerTime = 0;
	_timers.timerIsActive = false;
	_timers.playerTimeNextMinute = 0;
	_timers.pushedPlayTime = 0;

	if (ConfMan.hasKey("load_ad", Common::ConfigManager::kTransientDomain)) {
		changeScene(bootSummary->adScene);
		ConfMan.removeKey("load_ad", Common::ConfigManager::kTransientDomain);
		_isRunningAd = true;
	} else {
		SceneChangeDescription firstScene = bootSummary->firstScene;

		// Debug affordance: boot straight into a chosen scene. Nancy16's first
		// scene is a black splash, so testing anything visual otherwise means
		// playing forward to it by hand.
		if (ConfMan.hasKey("nancy_start_scene")) {
			firstScene.sceneID = ConfMan.getInt("nancy_start_scene");
			firstScene.frameID = 0;
			firstScene.verticalOffset = 0;
		}

		// Debug affordance: parse every scene's action records once and report,
		// rather than playing. The only way to measure record coverage over the
		// whole game instead of the handful of scenes a run happens to visit.
		if (ConfMan.getBool("nancy_audit_all_scenes")) {
			uint scenesSeen = 0, recordsSeen = 0;
			for (uint id = 0; id < 7000; ++id) {
				IFF *iff = g_nancy->_resource->loadIFF(Common::Path(Common::String::format("S%u", id)));
				if (!iff) {
					continue;
				}

				++scenesSeen;
				Common::SeekableReadStream *chunk = nullptr;
				uint n = 0;
				while (chunk = iff->getChunkStream("ACT", n), chunk != nullptr) {
					_actionManager.addNewActionRecord(*chunk);
					delete chunk;
					++n;
					++recordsSeen;
				}

				_actionManager.clearActionRecords(false);
				delete iff;
			}

			warning("AUDIT done: %u scenes, %u action records", scenesSeen, recordsSeen);
			g_nancy->quitGame();
			return;
		}

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
		_actionManager.addNewActionRecord(*actionRecordChunk);
		delete actionRecordChunk;
		++numRecords;
	}

	if (_sceneState.currentScene.paletteID == -1) {
		_sceneState.currentScene.paletteID = 0;
	}

	// "NO_ART_SCENE", (Nancy 11+) "POPUP_PREP_SCENE" and "NO_BG" are videoless
	// sentinel scenes that carry only logic ARs; they have no viewport art to
	// load. StreamManager::isBackgroundless() already treats all three alike -
	// the main scene path must agree, or entering one as a main scene (the PDA
	// block S6428+, reached by the AR-126 forced inventory jump, and the chase
	// map S5450) aborts with "Couldn't load video file NO_BG.avf or NO_BG.bik".
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
		// Skipping the load leaves the viewport surface uninitialised. Arriving
		// at one of these scenes from another one is fine - the previous art is
		// still there, which is exactly what an overlay-only scene wants - but
		// entering one FIRST (a direct boot, or a save that was taken in one)
		// leaves a 0-bytes-per-pixel surface, and the compositor blits it and
		// aborts in blitFromInner. That kills the process before anything can be
		// logged, which is why the record audit never saw it: 44 scenes,
		// including the whole S1400-S1456 Sophia phone block, S5490 and the PDA.
		// Give it a blank frame so there is always something valid to blit.
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

	for (auto &override : _inventorySoundOverrides) {
		g_nancy->_sound->stopSound(override._value.sound);
	}
	_inventorySoundOverrides.clear();

	_timers.sceneTime = 0;
	g_nancy->_sound->clearListenerPositionOverride();
	g_nancy->_sound->recalculateSoundEffects();

	// Increment the number of times we've visited this scene, unless we're
	// loading from a save
	if (!fromSaveFile) {
		_flags.sceneCounts.getOrCreateVal(_sceneState.currentScene.sceneID)++;
	}

	// Re-evaluate taskbar notification states against the new scene.
	// Nancy16+ has no TASK chunk and so no taskbar - its equivalent is an NDUI
	// panel - so check the pointer rather than only the game version.
	if (g_nancy->getGameType() >= kGameTypeNancy10 && _taskbar) {
		_taskbar->updateNotificationStates(_sceneState.currentScene.sceneID);
	}

	delete sceneIFF;
	_state = kStartSound;

	// Tell the Nancy16 stream runtime the main flow has moved. This is what AR
	// 93 watches, and it is where a close-up the player has navigated out of is
	// dropped.
	_streams.onMainSceneLoaded(_sceneState.currentScene.sceneID);

	// ...and which environment it has moved into, which is what starts the
	// location's ambient soundscape. Done after onMainSceneLoaded so a stream
	// started here gets the new scene-change count as its watch baseline.
	// Skipped on the save path - Scene::synchronize is the only caller that
	// passes true, and the endAll() a few lines further down its own body would
	// throw this away again; it re-derives the environment itself afterwards.
	if (!fromSaveFile) {
		updateEnvironment();
	}

	debugNoteSceneEntry();
	traceSceneEntry(fromSaveFile);
}

// Nancy16 ENVS: the scene summary's sound name is the environment's scene-prefix
// code ("ALT", "OFF_EXT_INT", "TUN"), the same string resolveMusicMix() looks up
// to pick the location's music. ENVS additionally names the environment's
// ambient script, and nothing in the action-record corpus ever loads one, so
// entering the environment is the only place it can be started from. See the
// "ambient environment" note in streams.h.
void Scene::updateEnvironment() {
	if (g_nancy->getGameType() < kGameTypeNancy16) {
		return;
	}

	auto *envs = (const ENVS *)g_nancy->getEngineData("ENVS");
	if (!envs) {
		return;
	}

	// The summary's 0x32-byte description carries the same code on many scenes,
	// and it is the only signal the 19 Bank scenes have - S4110..S4128 are
	// described "BAN" but leave the sound field blank. The two never disagree:
	// on all 530 scenes where both name an ENVS entry they name the same one, so
	// the description is a safe second look rather than a guess.
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

	// 425 of the 1531 scenes name no environment either way - "NO SOUND" on
	// every conversation and most close-ups, blank on the NO_BG logic scenes.
	// Those are places inside a location, not locations, so the ambience of
	// whatever location the player is standing in carries on.
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

	handleInput();

	if (g_nancy->getState() == NancyState::kMainMenu) {
		// Player pressed esc, do not process further
		return;
	}

	_actionManager.processActionRecords();

	// Nancy16's parallel flows run after the main one, so a stream that reacts
	// to a main-flow scene change sees it in the same frame it happens.
	_streams.process();

	// The coin purse is an NDUI Var::Static bound to player-table index 5, and
	// the records that spend Nancy's money (SetValue, and the ResourceUse
	// purchases) know nothing about the UI. Poll after the records have run so a
	// purchase shows up on the same frame it is deducted.
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		_nduiPanels[i]->refreshBoundValues();
	}

	// The journal's heading list is laid out at runtime, so its rows are not
	// action-record hotspots and the panel records the click itself; this is the
	// other half of that, the same shape as the conversation's picked reply.
	if (_journalPanel) {
		const int picked = _journalPanel->listTakePicked();
		if (picked >= 0) {
			selectJournalPage(picked);
		}
	}

	// Debug affordance: one line whenever the audible picture changes - the
	// environment, the running streams, or the set of channels the mixer is
	// actually playing. "The ambient stream is running" and "you can hear it"
	// are different claims, and a headless run can only check the second one by
	// watching the channels.
	if (ConfMan.hasKey("nancy_amb_trace")) {
		static Common::String last;
		const Common::String now = Common::String::format("scene=%u env=%s streams=%u[%s] chans=[%s]",
			_sceneState.currentScene.sceneID,
			_streams.getEnvironment().empty() ? "-" : _streams.getEnvironment().c_str(),
			_streams.size(), _streams.describe().c_str(),
			g_nancy->_sound->describeActiveChannels().c_str());

		if (now != last) {
			last = now;
			warning("AMBIENT %s", now.c_str());
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

	debugLogHotspots();
	traceTick();

	if (_state == kLoad) {
		g_nancy->_graphics->suppressNextDraw();
	}
}

// Debug affordance: print the set of clickable hotspots whenever it changes.
// A headless run otherwise has no way to know what is on screen, and "the click
// did nothing" is indistinguishable from "there was nothing there to click".
// Prints screen-space rects, so the coordinates can be fed straight back in as
// nancy_autoclick_script points.
void Scene::debugLogHotspots() {
	if (!ConfMan.getBool("nancy_debug_hotspots")) {
		return;
	}

	Common::String line = Common::String::format("HOTSPOTS scene=%u",
		_sceneState.currentScene.sceneID);

	uint idx = 0;
	for (auto &rec : _actionManager.getActionRecords()) {
		if (!rec->_isActive || rec->_isDone || !rec->_hasHotspot || !rec->_hotspot.isValidRect()) {
			continue;
		}

		Common::Rect screen = _viewport.convertViewportToScreen(rec->_hotspot);
		if (screen.isEmpty()) {
			continue;
		}

		line += Common::String::format(" | [%u] %d,%d %dx%d type=%d %s '%s'",
			idx++, screen.left, screen.top, screen.width(), screen.height(),
			rec->_type, rec->getRecordTypeName().c_str(), rec->_description.c_str());
	}

	Common::Array<Common::Rect> replies;
	for (uint i = 0; i < _conversationPanels.size(); ++i) {
		_conversationPanels[i]->debugGetConvoResponseRects(replies);
	}

	for (uint i = 0; i < replies.size(); ++i) {
		line += Common::String::format(" | reply[%u] %d,%d %dx%d (centre %d,%d)", i,
			replies[i].left, replies[i].top, replies[i].width(), replies[i].height(),
			replies[i].left + replies[i].width() / 2, replies[i].top + replies[i].height() / 2);
	}

	Common::Array<NDUIPanel::DebugWidget> widgets;
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		_nduiPanels[i]->debugGetClickableWidgets(widgets);
	}

	for (uint i = 0; i < widgets.size(); ++i) {
		line += Common::String::format(" | ndui[%u] %s %d,%d %dx%d (centre %d,%d) %s", i,
			widgets[i].name.c_str(), widgets[i].rect.left, widgets[i].rect.top,
			widgets[i].rect.width(), widgets[i].rect.height(),
			widgets[i].rect.left + widgets[i].rect.width() / 2,
			widgets[i].rect.top + widgets[i].rect.height() / 2,
			widgets[i].actions.c_str());
	}

	// Fourth source: the inventory grid, which is laid out at runtime by
	// NDUIPanel::drawInventory and so is not a control debugGetClickableWidgets
	// can see. Clicking one of these is the only way to get an item onto the
	// cursor, and several route steps are gated on holding one.
	Common::Array<NDUIPanel::DebugInvItem> invItems;
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		_nduiPanels[i]->debugGetInvItemRects(invItems);
	}

	for (uint i = 0; i < invItems.size(); ++i) {
		const Common::Rect &s = invItems[i].rect;
		line += Common::String::format(" | inv[%u] item=%d %d,%d %dx%d (centre %d,%d)", i,
			invItems[i].itemID, s.left, s.top, s.width(), s.height(),
			s.left + s.width() / 2, s.top + s.height() / 2);
	}

	if (line != _lastHotspotLine) {
		_lastHotspotLine = line;
		warning("%s", line.c_str());
	}
}

// ---------------------------------------------------------------------------
// Harness: machine-readable trace, stall detection, goal assertions.
//
// Everything below is inert unless a nancy_trace* / nancy_stall_polls /
// nancy_goals config key is set, so a human player's run is untouched.
// Contract and format: engines/nancy/trace.h and parallel/x_hooks/HOOKS.md.
// ---------------------------------------------------------------------------

// The clickable set, as a JSON array. Same three sources as debugLogHotspots -
// action-record hotspots, conversation replies, NDUI widgets - but structured,
// and carrying the centre point so a driver never has to do rect arithmetic.
Common::String Scene::traceHotspotArray() {
	Common::String out = "[";
	bool first = true;

	uint idx = 0;
	for (auto &rec : _actionManager.getActionRecords()) {
		if (!rec->_isActive || rec->_isDone || !rec->_hasHotspot || !rec->_hotspot.isValidRect()) {
			continue;
		}

		Common::Rect s = _viewport.convertViewportToScreen(rec->_hotspot);
		if (s.isEmpty()) {
			continue;
		}

		out += Common::String::format(
			"%s{\"kind\":\"ar\",\"i\":%u,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,"
			"\"cx\":%d,\"cy\":%d,\"type\":%d,\"name\":\"%s\",\"desc\":\"%s\"}",
			first ? "" : ",", idx++, s.left, s.top, s.width(), s.height(),
			s.left + s.width() / 2, s.top + s.height() / 2, rec->_type,
			Trace::escape(rec->getRecordTypeName()).c_str(),
			Trace::escape(rec->_description).c_str());
		first = false;
	}

	Common::Array<Common::Rect> replies;
	for (uint i = 0; i < _conversationPanels.size(); ++i) {
		_conversationPanels[i]->debugGetConvoResponseRects(replies);
	}

	for (uint i = 0; i < replies.size(); ++i) {
		const Common::Rect &s = replies[i];
		out += Common::String::format(
			"%s{\"kind\":\"reply\",\"i\":%u,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,"
			"\"cx\":%d,\"cy\":%d}",
			first ? "" : ",", i, s.left, s.top, s.width(), s.height(),
			s.left + s.width() / 2, s.top + s.height() / 2);
		first = false;
	}

	Common::Array<NDUIPanel::DebugWidget> widgets;
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		_nduiPanels[i]->debugGetClickableWidgets(widgets);
	}

	for (uint i = 0; i < widgets.size(); ++i) {
		const Common::Rect &s = widgets[i].rect;
		out += Common::String::format(
			"%s{\"kind\":\"ndui\",\"i\":%u,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,"
			"\"cx\":%d,\"cy\":%d,\"name\":\"%s\",\"actions\":\"%s\"}",
			first ? "" : ",", i, s.left, s.top, s.width(), s.height(),
			s.left + s.width() / 2, s.top + s.height() / 2,
			Trace::escape(widgets[i].name).c_str(),
			Trace::escape(widgets[i].actions).c_str());
		first = false;
	}

	// Fourth source: the runtime inventory grid. Same reason as in
	// debugLogHotspots - it is not an authored control, and a click on one of
	// these rects is the only way to put an item on the cursor.
	Common::Array<NDUIPanel::DebugInvItem> invItems;
	for (uint i = 0; i < _nduiPanels.size(); ++i) {
		_nduiPanels[i]->debugGetInvItemRects(invItems);
	}

	for (uint i = 0; i < invItems.size(); ++i) {
		const Common::Rect &s = invItems[i].rect;
		out += Common::String::format(
			"%s{\"kind\":\"inv\",\"i\":%u,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,"
			"\"cx\":%d,\"cy\":%d,\"item\":%d}",
			first ? "" : ",", i, s.left, s.top, s.width(), s.height(),
			s.left + s.width() / 2, s.top + s.height() / 2, invItems[i].itemID);
		first = false;
	}

	return out + "]";
}

// The whole of the state a driver needs to decide what to do next, as fields
// rather than as prose. `fp` is getStateFingerprint()'s string verbatim, so
// nothing that used to be in the log is lost - but note fp lists event flags by
// INDEX (label - 1000) while the "flags" array below lists them by LABEL, which
// is the number every nancy_* config key and every walkthrough uses.
Common::String Scene::traceStateJson() const {
	Common::Array<uint> items;
	for (uint i = 0; i < _flags.items.size(); ++i) {
		if (_flags.items[i] == g_nancy->_true) {
			items.push_back(i);
		}
	}

	Common::Array<uint> flags;
	for (uint i = 0; i < _flags.eventFlags.size(); ++i) {
		if (_flags.eventFlags[i] == g_nancy->_true) {
			flags.push_back(i + 1000);
		}
	}

	Common::String puzzle = "[";
	uint np = 0;
	for (const auto &pd : _puzzleData) {
		const uint32 tag = pd._key;
		puzzle += Common::String::format("%s\"%c%c%c%c\"", np++ ? "," : "",
			(char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8), (char)tag);
	}
	puzzle += "]";

	Common::String out;
	out += Common::String::format("\"held\":%d", _flags.heldItem);
	out += ",\"items\":" + Trace::numArray(items);
	out += ",\"flags\":" + Trace::numArray(flags);
	out += Common::String::format(",\"nflags\":%u", flags.size());
	out += Common::String::format(",\"scenecounts\":%u", (uint)_flags.sceneCounts.size());
	out += Common::String::format(",\"playertime\":%u", (uint32)_timers.playerTime);
	out += Common::String::format(",\"difficulty\":%u", _difficulty);
	out += ",\"puzzle\":" + puzzle;
	out += Common::String::format(",\"streams\":%u", _streams.size());
	out += Common::String::format(",\"env\":\"%s\"",
		Trace::escape(_streams.getEnvironment()).c_str());
	out += Common::String::format(",\"fp\":\"%s\"",
		Trace::escape(getStateFingerprint()).c_str());
	return out;
}

// One line per scene entry, emitted at the end of Scene::load - after the new
// scene's records are installed and the visit is counted, before any of them
// has run. So the state reported is the state ON ENTRY, and a record's effects
// show up in the NEXT scene event. That is the only definition that is stable
// against how long a scene takes.
void Scene::traceSceneEntry(bool fromSaveFile) {
	if (!Trace::isOn()) {
		return;
	}

	++_traceEntries;

	TraceEvent ev("scene");
	ev.num("n", _traceEntries)
		.num("scene", _sceneState.currentScene.sceneID)
		.num("prev", _tracePrevScene)
		.str("desc", _sceneState.summary.description)
		.num("frame", _sceneState.currentScene.frameID)
		.num("vofs", _sceneState.currentScene.verticalOffset)
		.num("visits", getSceneCounts(_sceneState.currentScene.sceneID))
		.boolean("fromsave", fromSaveFile)
		.raw("state", "{" + traceStateJson() + "}");
	ev.emit();

	_tracePrevScene = _sceneState.currentScene.sceneID;

	// A scene change is the one moment at which "am I making progress" has a
	// well-defined answer, so it is where goals are judged.
	traceCheckGoals();

	// New scene, new stall episode.
	_traceStallPolls = 0;
	_traceStallKey.clear();
}

// Called once per Scene::run(). Emits the clickable set whenever it changes,
// and a stall event when neither the scene nor the clickable set has changed
// for nancy_stall_polls consecutive polls. That is the "stuck vs slow"
// discriminator: a slow scene is still producing hotspot changes (a record
// going live, a conversation reply appearing); a stuck one is not.
void Scene::traceTick() {
	if (!Trace::isOn()) {
		return;
	}

	static int stallFor = -1;
	if (stallFor < 0) {
		stallFor = ConfMan.hasKey("nancy_stall_polls") ? ConfMan.getInt("nancy_stall_polls") : 0;
	}

	const Common::String hs = traceHotspotArray();
	const Common::String key = Common::String::format("%u|", _sceneState.currentScene.sceneID) + hs;

	if (key != _traceHotspotKey) {
		if (!_traceHotspotKey.empty() || hs != "[]") {
			TraceEvent("hotspots")
				.num("scene", _sceneState.currentScene.sceneID)
				.raw("hs", hs)
				.emit();
		}

		_traceHotspotKey = key;
		_traceStallPolls = 0;
		return;
	}

	if (stallFor <= 0) {
		return;
	}

	++_traceStallPolls;
	if (_traceStallPolls % stallFor == 0) {
		TraceEvent("stall")
			.num("scene", _sceneState.currentScene.sceneID)
			.num("polls", _traceStallPolls)
			.num("limit", stallFor)
			.num("nhs", hs == "[]" ? 0 : 1)
			.raw("hs", hs)
			.emit();
	}
}

void Scene::traceParseGoals() {
	_traceGoalsParsed = true;

	if (!ConfMan.hasKey("nancy_goals")) {
		return;
	}

	Common::StringTokenizer goals(ConfMan.get("nancy_goals"), ";");
	while (!goals.empty()) {
		Common::String g = goals.nextToken();
		const uint at = g.findFirstOf('@');
		const uint colon = g.findFirstOf(':');
		if (at == Common::String::npos || colon == Common::String::npos || colon < at) {
			warning("NTRACE bad nancy_goals entry '%s', want id@deadline:preds", g.c_str());
			continue;
		}

		TraceGoal goal;
		goal.id = g.substr(0, at);
		const Common::String deadline = g.substr(at + 1, colon - at - 1);
		goal.deadlineKind = deadline.empty() ? 'n' : deadline[0];
		goal.deadlineVal = atoi(deadline.c_str() + (deadline.empty() ? 0 : 1));

		Common::StringTokenizer preds(g.substr(colon + 1), ",");
		while (!preds.empty()) {
			goal.preds.push_back(preds.nextToken());
		}

		if (goal.preds.empty() || (goal.deadlineKind != 's' && goal.deadlineKind != 'n')) {
			warning("NTRACE bad nancy_goals entry '%s'", g.c_str());
			continue;
		}

		_traceGoals.push_back(goal);
	}
}

bool Scene::traceEvalPred(const Common::String &pred) const {
	if (pred.empty()) {
		return false;
	}

	uint i = 0;
	bool negate = false;
	if (pred[0] == '!') {
		negate = true;
		i = 1;
	}

	if (i >= pred.size()) {
		return false;
	}

	const char kind = pred[i];
	const char *arg = pred.c_str() + i + 1;
	bool val = false;

	switch (kind) {
	case 'f':
		val = getEventFlag((int16)atoi(arg), g_nancy->_true);
		break;
	case 'i':
		// hasItem() already counts the item currently in the cursor.
		val = hasItem((int16)atoi(arg)) == g_nancy->_true;
		break;
	case 'h':
		val = getHeldItem() == (int16)atoi(arg);
		break;
	case 's':
		val = getSceneCounts((int16)atoi(arg)) > 0;
		break;
	case 'v': {
		// v<index><op><value>, op one of = > >= < <=
		Common::String rest(arg);
		uint opAt = Common::String::npos;
		for (uint k = 0; k < rest.size(); ++k) {
			if (rest[k] == '=' || rest[k] == '>' || rest[k] == '<') {
				opAt = k;
				break;
			}
		}

		if (opAt == Common::String::npos) {
			return false;
		}

		const int index = atoi(rest.substr(0, opAt).c_str());
		Common::String op = rest.substr(opAt, 1);
		uint valAt = opAt + 1;
		if (valAt < rest.size() && rest[valAt] == '=') {
			op += '=';
			++valAt;
		}

		const int want = atoi(rest.c_str() + valAt);
		const TableData *table = (const TableData *)
			(_puzzleData.contains(TableData::getTag()) ? _puzzleData[TableData::getTag()] : nullptr);
		if (!table || index < 0 || (uint)index >= table->getNumSingleValues()) {
			return negate;
		}

		const int have = table->getSingleValue((uint)index);
		if (op == "=") {
			val = have == want;
		} else if (op == ">") {
			val = have > want;
		} else if (op == ">=") {
			val = have >= want;
		} else if (op == "<") {
			val = have < want;
		} else if (op == "<=") {
			val = have <= want;
		}
		break;
	}
	default:
		return false;
	}

	return negate ? !val : val;
}

// Judged at scene entries, never on elapsed time: "met" the first entry at
// which every predicate holds, "deadline" at the entry the goal named. A driver
// that sees motion but no "met" is watching a run that is busy and not
// progressing, which is exactly the thing a scene log cannot tell it.
void Scene::traceCheckGoals() {
	if (!_traceGoalsParsed) {
		traceParseGoals();
	}

	for (uint i = 0; i < _traceGoals.size(); ++i) {
		TraceGoal &goal = _traceGoals[i];
		if (goal.deadlineDone) {
			continue;
		}

		Common::String predJson = "[";
		bool all = true;
		for (uint p = 0; p < goal.preds.size(); ++p) {
			const bool ok = traceEvalPred(goal.preds[p]);
			all = all && ok;
			predJson += Common::String::format("%s{\"p\":\"%s\",\"ok\":%s}", p ? "," : "",
				Trace::escape(goal.preds[p]).c_str(), ok ? "true" : "false");
		}
		predJson += "]";

		if (all && !goal.met) {
			goal.met = true;
			TraceEvent("goal")
				.str("id", goal.id)
				.str("when", "met")
				.boolean("ok", true)
				.num("scene", _sceneState.currentScene.sceneID)
				.num("entry", _traceEntries)
				.raw("preds", predJson)
				.emit();
		}

		const bool due = goal.deadlineKind == 's' ?
			(int)_sceneState.currentScene.sceneID == goal.deadlineVal :
			(int)_traceEntries >= goal.deadlineVal;

		if (due) {
			goal.deadlineDone = true;
			TraceEvent("goal")
				.str("id", goal.id)
				.str("when", "deadline")
				.boolean("ok", all)
				.num("scene", _sceneState.currentScene.sceneID)
				.num("entry", _traceEntries)
				.str("deadline", Common::String::format("%c%d", goal.deadlineKind, goal.deadlineVal))
				.raw("preds", predJson)
				.emit();
		}
	}
}

// Emitted once when the engine is shutting down, so a driver that is killed by
// its own timeout still finds a verdict for every goal in the trace.
void Scene::traceFinalGoals() {
	if (!Trace::isOn() || !_traceGoalsParsed) {
		return;
	}

	for (uint i = 0; i < _traceGoals.size(); ++i) {
		TraceGoal &goal = _traceGoals[i];
		if (goal.deadlineDone) {
			continue;
		}

		goal.deadlineDone = true;
		TraceEvent("goal")
			.str("id", goal.id)
			.str("when", "end")
			.boolean("ok", goal.met)
			.num("scene", _sceneState.currentScene.sceneID)
			.num("entry", _traceEntries)
			.str("deadline", Common::String::format("%c%d", goal.deadlineKind, goal.deadlineVal))
			.emit();
	}
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

	// Warp the mouse below the inactive zone during dialogue scenes
	if (_activeConversation != nullptr) {
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
	} else if (!_activeMovie) {
		// Check if player has pressed esc
		if (input.input & NancyInput::kOpenMainMenu) {
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
		// Nancy16+ has no chunk to build these popups from - their replacements
		// are NDUI panels - so they were never initialised and must not be
		// driven. Same for the textbox below.
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
		// Nancy16+: the NDUI panels take input in place of the taskbar and
		// popups. Later-registered panels sit on top, so ask them first.
		// Two passes: visible widgets first, then art-less catch-alls. Panel
		// registration order alone is not a safe priority, because the splash's
		// full-width invisible skip button sits in a later panel than the
		// taskbar buttons it overlaps.
		// Inventory items and conversation replies first: both are laid out at
		// runtime and already take priority inside the panel.
		for (int i = (int)_nduiPanels.size() - 1; i >= 0; --i) {
			_nduiPanels[i]->handleInput(input);
		}

		// Then the authored controls, choosing the SMALLEST candidate across all
		// panels rather than trusting registration order. LOWERMATTE's
		// "InvisibleSkipSplashButton" is a 640x52 catch-all lying over the whole
		// taskbar strip; it sits in a later panel than the 33x34 taskbar buttons,
		// so it used to eat every click on them and none of the buttons worked.
		int bestPanel = -1, bestIndex = -1, bestArea = 0;
		for (int i = (int)_nduiPanels.size() - 1; i >= 0; --i) {
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
			// Modality. The options / save / load screens each raise MODAL's
			// "ModalDialog" on show, whose whole authored job is to send Clear to
			// every other panel root and to Engine_GameWindow - i.e. "nothing
			// behind me takes input". Without this a click on the dialog's own
			// backdrop falls through to the viewport and changes the scene behind
			// it, which is how the save dialog managed to save the wrong room.
			//
			// After the control dispatch, not before: eating the click first
			// would take it away from the dialog's own buttons.
			input.eatMouseInput();
		}
	}
	if (g_nancy->getGameType() <= kGameTypeNancy9) {
		_inventoryBox.handleInput(input);
	}

	// Handle invisible map button
	// We do this before the viewport since TVD's map button overlaps the viewport's right hotspot
	for (uint16 id : g_nancy->getStaticData().mapAccessSceneIDs) {
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
	if (getClock()) {
		getClock()->handleInput(input);
	}

	_viewport.handleInput(input);

	_sceneState.currentScene.verticalOffset = _viewport.getCurVerticalScroll();

	if (_sceneState.currentScene.frameID != _viewport.getCurFrame()) {
		_sceneState.currentScene.frameID = _viewport.getCurFrame();
		g_nancy->_sound->recalculateSoundEffects();
	}

	_actionManager.handleInput(input);

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
		// The taskbar, textbox and popups all came from chunks this game does not
		// have; their replacements are NDUI panels. Bring up the viewport plus
		// the two mattes, which are the always-on-screen furniture.
		_viewport.init();

		// CONVO is loaded alongside the mattes but starts hidden: it stands in
		// for the ConversationPopup, which Nancy16 has no TBOX chunk to build.
		// CONVO and INVENTORY are loaded alongside the mattes but start hidden:
		// they stand in for the ConversationPopup and InventoryPopup, neither of
		// which Nancy16 has the chunks to build.
		// STAKEOUT joins them for the same reason: AR 212 (S5610) needs a list
		// panel it can fill, and building one mid-scene would have to happen
		// during record execution. It starts hidden, so every other scene is
		// unaffected - a hidden panel neither draws (drawControl bails on the
		// root) nor takes input (handleInput and findControlHit both return
		// early on !_isVisible).
		//
		// OPTIONS, LOADGAME and SAVEGAME join them, and for the same reason: the
		// four right-hand taskbar buttons are the only way into them, and
		// ShowOptions is a plain `Show OptionsDialog` - OptionsDialog being the
		// root Static of OPTIONS[0], not a pseudo-target - so the command was
		// being dropped purely because no loaded panel owned that name. The
		// other three go through the Engine_* subsystems, which need the same
		// panels resident to show them.
		//
		// JOURNAL and TASKLIST join them last, and were the last two panels the
		// taskbar could reach nothing with: their buttons dump
		// `show(1,JournalDialog,-4)` and `show(1,TasklistDialog,-4)` into a scene
		// where no resident panel owned either name, and an NDUI command with no
		// owner is silently dropped. They are popups in the same family as the
		// inventory, so they take the same z 12; JOURNAL brings five panels, one
		// for the heading list and one for each of the four heading pages.
		static const char *const kPersistentPanels[] = {
			"UPPERMATTE", "LOWERMATTE", "CONVO", "INVENTORY", "STAKEOUT",
			"OPTIONS", "LOADGAME", "SAVEGAME", "JOURNAL", "TASKLIST"
		};
		for (uint i = 0; i < ARRAYSIZE(kPersistentPanels); ++i) {
			// The panels this member built, so its row template - which arrives in
			// a chunk of its own, after the first panel and before the last - can
			// be handed to all of them once the member is done.
			Common::Array<NDUIPanel *> memberPanels;
			NDUIControl rowTemplate;
			bool haveRowTemplate = false;

			for (uint chunk = 0; ; ++chunk) {
				NDUI *data = loadNDUIChunk(kPersistentPanels[i], chunk);
				if (!data) {
					break;
				}

				if (data->hasPanel) {
					// The panel keeps a pointer to the chunk for hit-testing and
					// redraws, so it takes ownership rather than borrowing it.
					// Action records render at z 7-11, and every NDUI panel used
					// to be built at 7 - so scene overlays drew straight over the
					// caption and conversation boxes. The mattes belong with the
					// taskbar at 7; the panels that stand in for the popups take
					// 12, the z the pre-Nancy16 popups already use.
					//
					// The dialogs sit above even those: an options or save screen
					// covers the inventory box, never the other way round.
					const bool isPopupPanel = !strcmp(kPersistentPanels[i], "CONVO") ||
						!strcmp(kPersistentPanels[i], "INVENTORY") ||
						!strcmp(kPersistentPanels[i], "STAKEOUT") ||
						!strcmp(kPersistentPanels[i], "JOURNAL") ||
						!strcmp(kPersistentPanels[i], "TASKLIST");
					const bool isDialogPanel = !strcmp(kPersistentPanels[i], "OPTIONS") ||
						!strcmp(kPersistentPanels[i], "LOADGAME") ||
						!strcmp(kPersistentPanels[i], "SAVEGAME");
					NDUIPanel *panel = new NDUIPanel(isDialogPanel ? 13 : (isPopupPanel ? 12 : 7));
					panel->init(data);

					if (isDialogPanel) {
						// Authored state=0, i.e. visible, because in the original
						// they are only ever built when they are opened. Here they
						// are resident, so they have to be forced off screen -
						// including their root in _visible, or the first Show is a
						// no-op against the authored state.
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
						// are the four heading pages, in chunk order, which is the
						// order their names resolve in AUTOTEXT (Observations,
						// Suspects, Clues, Phone Numbers). Told apart by what they
						// own rather than by name - the page names are data, and
						// the records that file entries under them carry the same
						// strings.
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
					// A panel-less chunk is a standalone control, and in the four
					// list members it is the row template: STAKEOUT's TextEntry,
					// JOURNAL's JournalEntry, TASKLIST's TasklistEntry. Copied out
					// rather than kept, so the chunk can be dropped exactly as
					// before and each of the five journal panels can hold one.
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

		// Debug affordance: grant a few items and open the inventory, so the grid
		// can be checked without first playing far enough to earn them.
		if (ConfMan.getBool("nancy_debug_inventory")) {
			auto *inv = (const INVD *)g_nancy->getEngineData("INVD");
			if (inv) {
				uint granted = 0;
				for (uint n = 0; n < inv->items.size() && granted < 6; ++n) {
					if (!inv->items[n].isPseudoItem()) {
						_flags.items[inv->items[n].id] = g_nancy->_true;
						++granted;
					}
				}
			}

			applyNDUICommand("InvDialog", 1);
		}

		// Debug affordance: put the top-left HUD on screen for a run that did not
		// start from the top.
		//
		// The coin purse and the paper-doll button are authored hidden and are
		// revealed by data, not by the engine: S1 shows and enables the taskbar
		// group, and S3552 - "RED", the first playable scene - is the single
		// place in the whole game that sends Show to CoinPurse and ShowPaperDoll.
		// (S6457/S6458, the handbook close-up, hide and restore them; nothing
		// else touches their visibility.) A run booted with nancy_start_scene, or
		// resumed from a save, therefore never executes that reveal and has no
		// HUD - which is not a rendering bug, but it does mean every scene the
		// reference harness renders is missing widgets the recording has.
		//
		// So: synthesise the reveal, the same way nancy_debug_set_values
		// synthesises the money it displays. Automatic whenever nancy_start_scene
		// is set, since that key already means "pretend we played to here".
		if (ConfMan.getBool("nancy_debug_hud") || ConfMan.hasKey("nancy_start_scene")) {
			applyNDUICommand("CoinPurse", 1);
			applyNDUICommand("ShowPaperDoll", 1);
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

	if (_textbox.hasBeenDrawn() || g_nancy->getGameType() >= kGameTypeNancy10) {
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

bool debugGetNthHotspotCentre(uint n, Common::Point &out) {
	return NancySceneState.getNthHotspotCentre(n, out);
}

int debugGetCurrentSceneID() {
	if (!State::Scene::hasInstance()) {
		return -1;
	}

	return (int)NancySceneState.getSceneInfo().sceneID;
}

void debugSetViewportFrame(uint frame) {
	if (!State::Scene::hasInstance()) {
		return;
	}

	// Panoramic scenes only expose a hotspot while the viewport is on the frame
	// that hotspot belongs to, and the only way to pan is to hold the pointer in
	// the edge scroll zone for an indeterminate number of frames. A headless run
	// cannot aim that, so the click harness jumps the viewport directly.
	// setFrame asserts on an out-of-range index, and a caller counting frames up
	// has no way to know how many a given scene's panorama has, so wrap here.
	const uint16 count = NancySceneState.getViewport().getFrameCount();
	if (count == 0) {
		return;
	}

	NancySceneState.getViewport().setFrame(frame % count);
}

int debugGetViewportFrame() {
	if (!State::Scene::hasInstance()) {
		return -1;
	}

	return (int)NancySceneState.getViewport().getCurFrame();
}

} // End of namespace Nancy
