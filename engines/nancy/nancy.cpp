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

#include "common/system.h"
#include "common/random.h"
#include "common/debug-channels.h"
#include "common/config-manager.h"
#include "common/memstream.h"
#include "common/compression/installshield_cab.h"
#include "common/serializer.h"
#include "common/translation.h"

#include "gui/message.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/cif.h"
#include "engines/nancy/iff.h"
#include "engines/nancy/input.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/console.h"
#include "engines/nancy/ndui.h"
#include "engines/nancy/util.h"
#include "engines/nancy/trace.h"

#include "engines/nancy/action/conversation.h"

#include "engines/nancy/state/logo.h"
#include "engines/nancy/state/scene.h"
#include "engines/nancy/state/help.h"
#include "engines/nancy/state/map.h"
#include "engines/nancy/state/credits.h"
#include "engines/nancy/state/mainmenu.h"
#include "engines/nancy/state/setupmenu.h"
#include "engines/nancy/state/loadsave.h"
#include "engines/nancy/state/savedialog.h"

namespace Nancy {

NancyEngine *g_nancy;

const char *const kConfMatteColour	= "nancy_matte_colour";
const char *const kConfFontScale	= "nancy_font_scale";
const char *const kConfWindowMode	= "nancy_window_mode";

NancyEngine::NancyEngine(OSystem *syst, const NancyGameDescription *gd) :
		Engine(syst),
		_gameDescription(gd),
		_system(syst),
		_datFileMajorVersion(1),
		_datFileMinorVersion(0),
		_false(gd->gameType <= kGameTypeNancy2 ? 1 : 0),
		_true(gd->gameType <= kGameTypeNancy2 ? 2 : 1) {

	g_nancy = this;

	// The options-screen settings that have no ScummVM key of their own. Without
	// a registered default ConfMan.getInt() hands back 0 for a profile that has
	// never opened the options screen, and 0 is a meaningful value for two of the
	// three - COLR 0 is the UI blue, and a 0% font scale is unrenderable.
	ConfMan.registerDefault(kConfMatteColour, kDefaultMatteColourID);
	ConfMan.registerDefault(kConfFontScale, kDefaultFontScalePercent);
	ConfMan.registerDefault(kConfWindowMode, 1);	// windowed; what the port opens as

	// Common::RandomSource's own constructor already seeds from
	// generateNewSeed(), which honours the random_seed config key; the explicit
	// setSeed here is what makes that visible at the call site. NancyRandomSource
	// is a drop-in wrapper that adds a draw counter, an optional per-draw trace
	// and an optional second stream for the wall-clock-driven cosmetic draws.
	// With no nancy_rng_* key set it forwards one-for-one to Common::RandomSource.
	_randomSource = new NancyRandomSource("Nancy");
	_randomSource->setSeed(NancyRandomSource::generateNewSeed());

	_input = new InputManager();
	_sound = new SoundManager();
	_graphics = new GraphicsManager();
	_cursor = new CursorManager();
	_resource = new ResourceManager();

	_hasJustSaved = false;
}

NancyEngine::~NancyEngine() {
	destroyState(NancyState::kLogo);
	destroyState(NancyState::kCredits);
	destroyState(NancyState::kMap);
	destroyState(NancyState::kHelp);
	destroyState(NancyState::kScene);
	destroyState(NancyState::kMainMenu);
	destroyState(NancyState::kSetup);
	destroyState(NancyState::kLoadSave);
	destroyState(NancyState::kSaveDialog);

	delete _randomSource;

	delete _graphics;
	delete _cursor;
	delete _input;
	delete _sound;
	delete _resource;

	for (auto &data : _engineData) {
		delete data._value;
	}
}

NancyEngine *NancyEngine::create(GameType type, OSystem *syst, const NancyGameDescription *gd) {
	if (type >= kGameTypeVampire && type <= kGameTypeNancy32) {
		return new NancyEngine(syst, gd);
	}

	error("Unknown GameType");
}

Common::Error NancyEngine::loadGameState(int slot) {
	auto save = g_nancy->getMetaEngine()->querySaveMetaInfos(ConfMan.getActiveDomainName().c_str(), slot);
	if (save.isValid() && save.getDescription() != "SECOND CHANCE") {
		// Ensure the nancy8+ save screen will display the last non-autosave name
		ConfMan.setInt("display_slot", slot, Common::ConfigManager::kTransientDomain);
	}

	Common::Error result = Engine::loadGameState(slot);
	if (result.getCode() == Common::kNoError) {
		if (_gameFlow.curState != NancyState::kScene)
			destroyState(_gameFlow.curState);
		g_nancy->setState(NancyState::kScene);
		g_nancy->setMouseEnabled(true);
	}
	return result;
}

Common::Error NancyEngine::loadGameStream(Common::SeekableReadStream *stream) {
	Common::Serializer ser(stream, nullptr);
	return synchronize(ser);
}

Common::Error NancyEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	Common::Serializer ser(nullptr, stream);

	return synchronize(ser);
}

bool NancyEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	return NancySceneState.getActiveConversation() == nullptr &&
		   NancySceneState.getActiveMovie() == nullptr &&
		   !NancySceneState.isRunningAd();
}

bool NancyEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	return State::Scene::hasInstance() &&
		   NancySceneState.getState() == State::Scene::kRun &&
		   canLoadGameStateCurrently();
}

void NancyEngine::secondChance() {
	uint secondChanceSlot = getMetaEngine()->getMaximumSaveSlot();
	saveGameState(secondChanceSlot, "SECOND CHANCE", true);
}

// Nancy16 checkpoint saves are addressed by name, ScummVM slots by number, so
// the two have to be reconciled. The mapping obeys three rules:
//
//  * A named save must never land on a slot the player is using. The reserved
//    band sits at the very top of the range, directly below the second chance
//    slot, while the save dialog offers the lowest free slot for manual saves -
//    so the two only meet after ~980 saves. On top of that, a band slot holding
//    anything that is not one of our named saves is stepped over rather than
//    overwritten, which also protects saves made before this band existed.
//
//  * A given name always resolves back to the same slot, so re-triggering a
//    checkpoint overwrites it instead of filling the save list with duplicates.
//    The lookup key is the slot's description, which is the name we wrote, so
//    the mapping is recovered from the save files themselves and survives a
//    restart with no side-car bookkeeping to keep in sync.
//
//  * The band is scanned low to high and the name takes the first free slot, so
//    the assignment depends only on the saves that already exist. A hash of the
//    name would also be stable, but 14 names over 20 slots collide with near
//    certainty, and a collision would silently destroy another checkpoint.
int NancyEngine::firstNamedSaveSlot() const {
	const int lastSlot = getMetaEngine()->getMaximumSaveSlot() - 1;
	return MAX<int>(0, lastSlot - kNumNamedSaveSlots + 1);
}

int NancyEngine::getNamedSaveSlot(const Common::String &name) const {
	const int secondChanceSlot = getMetaEngine()->getMaximumSaveSlot();
	const int lastSlot = secondChanceSlot - 1;
	const int firstSlot = MAX<int>(0, lastSlot - kNumNamedSaveSlots + 1);

	SaveStateList saves = getMetaEngine()->listSaves(_targetName.c_str());

	// Anything already in the band is off limits, whoever wrote it - unless it is
	// this very name, in which case it is the slot we are looking for.
	Common::Array<bool> taken(kNumNamedSaveSlots);
	for (const SaveStateDescriptor &save : saves) {
		const int slot = save.getSaveSlot();
		if (slot < firstSlot || slot > lastSlot) {
			continue;
		}

		if (save.getDescription().equalsIgnoreCase(name)) {
			return slot;
		}

		taken[slot - firstSlot] = true;
	}

	for (int slot = firstSlot; slot <= lastSlot; ++slot) {
		if (!taken[slot - firstSlot]) {
			return slot;
		}
	}

	return -1;
}

// Names are matched case-insensitively, and that is not tidiness: three of the
// sixteen type 115 records ask for "start_game" while every type 114 record
// writes "Start_Game". A case-sensitive lookup silently loses the second chance
// in S2069, S6620 and S6621 - the only three scenes where the restart branch is
// the one that leads anywhere.
int NancyEngine::findNamedSaveSlot(const Common::String &name) const {
	const int secondChanceSlot = getMetaEngine()->getMaximumSaveSlot();
	const int lastSlot = secondChanceSlot - 1;
	const int firstSlot = MAX<int>(0, lastSlot - kNumNamedSaveSlots + 1);

	SaveStateList saves = getMetaEngine()->listSaves(_targetName.c_str());
	for (const SaveStateDescriptor &save : saves) {
		const int slot = save.getSaveSlot();
		if (slot >= firstSlot && slot <= lastSlot && save.getDescription().equalsIgnoreCase(name)) {
			return slot;
		}
	}

	return -1;
}

void NancyEngine::saveNamedGame(const Common::String &name) {
	if (name.empty()) {
		warning("Named save requested with an empty name, ignoring");
		return;
	}

	if (!canSaveGameStateCurrently()) {
		// Checkpoints are fired from action records, which only run while the
		// scene is live, so this should not happen - but a refused save is not a
		// reason to break the scene.
		warning("Named save '%s' skipped: the engine cannot save right now", name.c_str());
		return;
	}

	const int slot = getNamedSaveSlot(name);
	if (slot < 0) {
		warning("Named save '%s' skipped: no free slot in the reserved band", name.c_str());
		return;
	}

	// isAutosave, so the save dialog marks the slot and the player is warned
	// before overwriting it by hand.
	const Common::Error err = saveGameState(slot, name, true);
	if (err.getCode() != Common::kNoError) {
		warning("Named save '%s' to slot %d failed: %s", name.c_str(), slot, err.getDesc().c_str());
		return;
	}

	debugC(1, kDebugEngine, "Named save '%s' written to slot %d", name.c_str(), slot);
}

void NancyEngine::requestNamedLoad(const Common::String &name) {
	if (name.empty()) {
		warning("Named load requested with an empty name, ignoring");
		return;
	}

	// Last request wins. Two of these cannot legitimately fire in one frame -
	// the second chance screens gate each on its own button flag - but if they
	// ever did, only one load can happen and queueing a list would just hide the
	// clash.
	_pendingNamedLoad = name;
	_namedLoadWasDeferred = false;
}

void NancyEngine::requestSlotLoad(int slot) {
	if (slot < 0 || slot > getMetaEngine()->getMaximumSaveSlot()) {
		warning("Slot load requested for out-of-range slot %d, ignoring", slot);
		return;
	}

	_pendingSlotLoad = slot;
}

void NancyEngine::runPendingNamedLoad() {
	// The player's own Load click first, and it cancels anything a record had
	// queued: both end in the same loadGameState(), so running the record's one
	// afterwards would silently undo the load the player just asked for.
	if (_pendingSlotLoad >= 0) {
		const int slot = _pendingSlotLoad;
		_pendingSlotLoad = -1;
		_pendingNamedLoad.clear();
		_namedLoadWasDeferred = false;

		const Common::Error err = loadGameState(slot);
		if (err.getCode() != Common::kNoError) {
			warning("Load from slot %d failed: %s", slot, err.getDesc().c_str());
			return;
		}

		if (State::Scene::hasInstance()) {
			debugC(1, kDebugEngine, "Loaded slot %d: %s",
				slot, NancySceneState.getStateFingerprint().c_str());
		}

		return;
	}

	if (_pendingNamedLoad.empty()) {
		return;
	}

	const Common::String name = _pendingNamedLoad;

	const int slot = findNamedSaveSlot(name);
	if (slot < 0) {
		// The checkpoint this asks for was never written. That happens when the
		// player jumped straight into a late scene rather than playing up to it,
		// so it is a missing prerequisite rather than a broken record: leave the
		// scene alone instead of erroring out of it.
		warning("Named load '%s' skipped: no save by that name", name.c_str());
		_pendingNamedLoad.clear();
		return;
	}

	if (!canLoadGameStateCurrently()) {
		// Held, not dropped. Every second chance screen opens by playing
		// VEN_SecondChance_ANIM, and an active movie is one of the things this
		// gate refuses on - so a load asked for while the dialog is still
		// animating in would be thrown away, which is precisely when the player
		// is most likely to ask for one. The gate exists to stop the player
		// loading from the menu mid-cutscene; a load the game itself scripted is
		// not that, so it waits for its turn instead of being cancelled.
		//
		// The wait is unbounded on purpose: every blocker it can trip on (movie,
		// conversation, ad) ends on its own, and silently giving up after n
		// frames would put back the same lost-second-chance bug in a form that
		// only shows up on a slow machine.
		if (!_namedLoadWasDeferred) {
			_namedLoadWasDeferred = true;
			debugC(1, kDebugEngine, "Named load '%s' held: the engine cannot load yet", name.c_str());
		}

		return;
	}

	_pendingNamedLoad.clear();
	_namedLoadWasDeferred = false;

	debugC(1, kDebugEngine, "Named load '%s' from slot %d", name.c_str(), slot);

	const Common::Error err = loadGameState(slot);
	if (err.getCode() != Common::kNoError) {
		warning("Named load '%s' from slot %d failed: %s", name.c_str(), slot, err.getDesc().c_str());
		return;
	}

	if (State::Scene::hasInstance()) {
		debugC(1, kDebugEngine, "Named load '%s' restored: %s",
			name.c_str(), NancySceneState.getStateFingerprint().c_str());
	}
}

void NancyEngine::deleteNamedGame(const Common::String &name) {
	if (name.empty()) {
		warning("Named save deletion requested with an empty name, ignoring");
		return;
	}

	const int slot = findNamedSaveSlot(name);
	if (slot < 0) {
		// Nothing to clear. The record fires unconditionally on entering its
		// scene, so this is the normal case on a first playthrough.
		debugC(1, kDebugEngine, "Named save '%s' not present, nothing to clear", name.c_str());
		return;
	}

	getMetaEngine()->removeSaveState(_targetName.c_str(), slot);
	debugC(1, kDebugEngine, "Named save '%s' cleared from slot %d", name.c_str(), slot);
}

void NancyEngine::errorString(const char *buf_input, char *buf_output, int buf_output_size) {
	if (State::Scene::hasInstance()) {
		if (NancySceneState.getState() == State::Scene::kLoad) {
			// Error while loading scene
			snprintf(buf_output, buf_output_size, "While loading scene S%u, frame %u, action record %u:\n%s",
				NancySceneState.getSceneInfo().sceneID,
				NancySceneState.getSceneInfo().frameID,
				NancySceneState.getActionManager().getActionRecords().size(),
				buf_input);
		} else {
			// Error while running
			snprintf(buf_output, buf_output_size, "In current scene S%u, frame %u:\n%s",
				NancySceneState.getSceneInfo().sceneID,
				NancySceneState.getSceneInfo().frameID,
				buf_input);
		}
	} else {
		strncpy(buf_output, buf_input, buf_output_size);
		if (buf_output_size > 0)
			buf_output[buf_output_size - 1] = '\0';
	}
}

bool NancyEngine::hasFeature(EngineFeature f) const {
	return	(f == kSupportsReturnToLauncher) ||
			(f == kSupportsLoadingDuringRuntime) ||
			(f == kSupportsSavingDuringRuntime) ||
			(f == kSupportsChangingOptionsDuringRuntime) ||
			(f == kSupportsSubtitleOptions);
}

const char *NancyEngine::getCopyrightString() const {
	return "Copyright 1989-1997 David P Gray, All Rights Reserved.";
}

uint32 NancyEngine::getGameFlags() const {
	return _gameDescription->desc.flags;
}

const char *NancyEngine::getGameId() const {
	return _gameDescription->desc.gameId;
}

GameType NancyEngine::getGameType() const {
	return _gameDescription->gameType;
}

Common::Language NancyEngine::getGameLanguage() const {
	return _gameDescription->desc.language;
}

Common::Platform NancyEngine::getPlatform() const {
	return _gameDescription->desc.platform;
}

const StaticData &NancyEngine::getStaticData() const {
	return _staticData;
}

const EngineData *NancyEngine::getEngineData(const Common::String &name) const {
	if (_engineData.contains(name)) {
		return _engineData[name];
	}

	return nullptr;
}

// From Nancy12 the event flags are split into two ranges: 1000 generic engine
// flags (labels 1000-1999) followed by the game-specific flags (labels from 2000),
// whose names are listed in the EVNT chunk.
static const uint kNumGenericEventFlags = 1000;

// Nancy12 keeps no names for its engine-internal flags in the 1xxx range; it
// builds them at runtime by joining a category name with the flag's position
// inside the category. These are the label ranges it assigns to each category.
struct GenericEventFlagCategory {
	uint16 firstLabel;
	uint16 lastLabel;
	const char *name;
};

static const GenericEventFlagCategory kGenericEventFlagCategories[] = {
	{ 1010, 1040, "Generic" },
	{ 1100, 1110, "Timer" },
	{ 1512, 1532, "Meta_Award" },
	{ 1533, 1558, "Said_Comment" },
	{ 1559, 1658, "Empty" }
};

const Common::String NancyEngine::getEventFlagName(uint flagID) const {
	if (getGameType() <= kGameTypeNancy11) {
		// All flag names are stored in the executable
		if (flagID >= 1000) {
			// In nancy3 and onwards flags begin from 1000
			flagID -= 1000;
		}
		return (flagID < _staticData.eventFlagNames.size()) ? _staticData.eventFlagNames[flagID] : "";
	}

	// Nancy12 split the flags in two: the game-specific flags were moved to the
	// EVNT chunk and renumbered from 2000, while the engine's generic flags kept
	// their 1xxx numbering
	if (flagID >= 2000) {
		const EVNT *evnt = dynamic_cast<const EVNT *>(getEngineData("EVNT"));

		if (getGameType() >= kGameTypeNancy16) {
			// Nancy16 prepends a block of inventory flags numbered from 0, so an
			// entry's position no longer implies its ID - look it up instead.
			for (uint i = 0; i < evnt->eventFlagIDs.size(); ++i) {
				if (evnt->eventFlagIDs[i] == flagID) {
					return evnt->eventFlagNames[i];
				}
			}

			return "";
		}

		flagID -= 2000;

		const Common::Array<Common::String> &flagNames = evnt->eventFlagNames;
		return (flagID < flagNames.size()) ? flagNames[flagID] : "";
	}

	for (uint i = 0; i < ARRAYSIZE(kGenericEventFlagCategories); ++i) {
		const GenericEventFlagCategory &category = kGenericEventFlagCategories[i];
		if (flagID >= category.firstLabel && flagID <= category.lastLabel) {
			return Common::String::format("%s%u", category.name, flagID - category.firstLabel);
		}
	}

	return "";
}

void NancyEngine::setState(NancyState::NancyState state, NancyState::NancyState overridePrevious) {
	// Handle special cases first
	switch (state) {
	case NancyState::kBoot:
		bootGameEngine();
		setState(NancyState::kLogo);
		return;
	case NancyState::kLogo:
		// Nancy16+ dropped the LG0/PLG0 logo images from BOOT; the splash is a Bink video
		// driven from the scene flow instead, so there is nothing for Logo to display.
		if (getGameType() >= kGameTypeNancy16 && !getEngineData("LG0")) {
			setState(NancyState::kScene);
			return;
		}

		break;
	case NancyState::kMainMenu: {
		if (!ConfMan.hasKey("original_menus") || ConfMan.getBool("original_menus")) {
			break;
		}

		// Do not use the original engine's menus, call the GMM instead
		openMainMenuDialog();

		if (shouldQuit()) {
			return;
		}

		_input->forceCleanInput();

		return;
	}
	default:
		break;
	}

	if (overridePrevious != NancyState::kNone) {
		_gameFlow.prevState = overridePrevious;
	} else {
		_gameFlow.prevState = _gameFlow.curState;
	}

	_gameFlow.nextState = state;
	_gameFlow.changingState = true;
}

void NancyEngine::setToPreviousState() {
	setState(_gameFlow.prevState);
}

void NancyEngine::setMouseEnabled(bool enabled) {
	_cursor->showCursor(enabled); _input->setMouseInputEnabled(enabled);
}

void NancyEngine::addDeferredLoader(Common::SharedPtr<DeferredLoader> &loaderPtr) {
	_deferredLoaderObjects.push_back(Common::WeakPtr<DeferredLoader>(loaderPtr));
}

Common::Error NancyEngine::run() {
	setDebugger(new NancyConsole());

	// Set the default number of saves for earlier games
	if (!ConfMan.hasKey("nancy_max_saves", ConfMan.getActiveDomainName())) {
		if (getGameType() <= kGameTypeNancy7) {
			ConfMan.setInt("nancy_max_saves", 8, ConfMan.getActiveDomainName());
		}
	}

	if (!ConfMan.getBool("originalsaveload")) {
		ConfMan.setInt("nancy_max_saves", 999, ConfMan.getActiveDomainName());
	}

	// Harness: first line of the trace, so a driver can assert the run it is
	// reading is the run it launched (and which seed it got).
	if (Trace::isOn()) {
		TraceEvent("boot")
			.num("seed", _randomSource->getSeed())
			.boolean("seedpinned", ConfMan.hasKey("random_seed"))
			.str("gameid", ConfMan.getActiveDomainName())
			.num("startscene", ConfMan.hasKey("nancy_start_scene") ? ConfMan.getInt("nancy_start_scene") : -1)
			.str("script", ConfMan.hasKey("nancy_scene_script") ? ConfMan.get("nancy_scene_script") : "")
			.str("goals", ConfMan.hasKey("nancy_goals") ? ConfMan.get("nancy_goals") : "")
			.num("stallpolls", ConfMan.hasKey("nancy_stall_polls") ? ConfMan.getInt("nancy_stall_polls") : 0)
			.boolean("movieskip", ConfMan.getBool("nancy_movie_skip"))
			.emit();
	}

	// Boot the engine
	setState(NancyState::kBoot);

	// Check if we need to load a save state from the launcher
	if (ConfMan.hasKey("save_slot")) {
		int saveSlot = ConfMan.getInt("save_slot");
		if (saveSlot >= 0 && saveSlot <= getMetaEngine()->getMaximumSaveSlot()) {
			// Set to Scene but do not do the loading yet
			setState(NancyState::kScene);
		}
	}

	bool graphicsWereSuppressed = false;

	// Main loop
	while (true) {
		_input->processEvents();
		if (shouldQuit()) {
			break;
		}

		// A checkpoint load asked for by an action record on an earlier frame.
		// It has to happen with nothing of the scene on the stack, since the load
		// destroys and rebuilds it - including the record array ActionManager
		// iterates while it calls execute(). This is the same point in the frame
		// at which a load from ScummVM's own menu lands, that path being driven
		// from inside processEvents() just above.
		runPendingNamedLoad();

		uint32 frameEndTime = _system->getMillis() + 16;

		if (!graphicsWereSuppressed) {
			_cursor->setCursorType(CursorManager::kNormalArrow);
		}

		State::State *s;

		if (_gameFlow.changingState) {
			_gameFlow.curState = _gameFlow.nextState;
			_gameFlow.nextState = NancyState::kNone;

			s = getStateObject(_gameFlow.curState);
			if (s) {
				s->onStateEnter(_gameFlow.curState);
			}

			_gameFlow.changingState = false;
		}

		s = getStateObject(_gameFlow.curState);
		if (s) {
			s->process();
		}

		graphicsWereSuppressed = _graphics->getIsSuppressed();

		_graphics->draw();

		if (_gameFlow.changingState) {
			_graphics->clearObjects();

			s = getStateObject(_gameFlow.curState);
			if (s) {
				if (s->onStateExit(_gameFlow.nextState)) {
					destroyState(_gameFlow.curState);
				}
			}
		}

		_system->updateScreen();

		// In cases where the graphics were not drawn for a frame, we want to make sure the next
		// frame is processed as fast as possible. Thus, we skip deferred loaders and the time
		// delay that normally maintains 60fps
		if (!graphicsWereSuppressed) {
			// Use the spare time until the next frame to load larger data objects
			// Some loading is guaranteed to happen even with no time left, to ensure
			// slower systems won't be stuck waiting forever
			if (_deferredLoaderObjects.size()) {
				uint i = _deferredLoaderObjects.size() - 1;
				int32 timePerObj = (frameEndTime - g_system->getMillis()) / _deferredLoaderObjects.size();

				if (timePerObj < 0) {
					timePerObj = 0;
				}

				for (auto *iter = _deferredLoaderObjects.begin(); iter < _deferredLoaderObjects.end(); ++iter) {
					if (iter->expired()) {
						iter = _deferredLoaderObjects.erase(iter);
					} else {
						auto objectPtr = iter->lock();
						if (objectPtr) {
							if (objectPtr->load(frameEndTime - (i * timePerObj))) {
								iter = _deferredLoaderObjects.erase(iter);
							}
							--i;
						}

						if (_system->getMillis() > frameEndTime) {
							break;
						}
					}
				}
			}

			uint32 frameFinishTime = _system->getMillis();
			if (frameFinishTime < frameEndTime) {
				_system->delayMillis(frameEndTime - frameFinishTime);
			}
		}
	}

	// Harness: a verdict for every outstanding goal, then close the trace file.
	if (Trace::isOn()) {
		if (State::Scene::hasInstance()) {
			NancySceneState.traceFinalGoals();
		}

		TraceEvent("quit").num("rngdraws", _randomSource->getDrawCount()).emit();
		Trace::shutdown();
	}

	return Common::kNoError;
}

void NancyEngine::pauseEngineIntern(bool pause) {
	State::State *s = getStateObject(_gameFlow.curState);

	if (s) {
		if (pause) {
			s->onStateExit(NancyState::kPause);
		} else {
			s->onStateEnter(NancyState::kPause);
		}
	}

	Engine::pauseEngineIntern(pause);
}

void NancyEngine::bootGameEngine() {
	// Load paths
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "game");
	SearchMan.addSubDirectoryMatching(gameDataDir, "datafiles");
	SearchMan.addSubDirectoryMatching(gameDataDir, "ciftree");
	SearchMan.addSubDirectoryMatching(gameDataDir, "hdsound");
	SearchMan.addSubDirectoryMatching(gameDataDir, "cdsound");
	SearchMan.addSubDirectoryMatching(gameDataDir, "hdvideo");
	SearchMan.addSubDirectoryMatching(gameDataDir, "cdvideo");
	// Nancy16+ ships its media in plain directories - the discs use sound/,
	// video/ and video2/ rather than the hd/cd split earlier games used. Without
	// "sound" every music and speech lookup misses and the game plays silently.
	SearchMan.addSubDirectoryMatching(gameDataDir, "sound");
	SearchMan.addSubDirectoryMatching(gameDataDir, "video");
	SearchMan.addSubDirectoryMatching(gameDataDir, "video2");
	SearchMan.addSubDirectoryMatching(gameDataDir, "iff");
	SearchMan.addSubDirectoryMatching(gameDataDir, "art");
	SearchMan.addSubDirectoryMatching(gameDataDir, "font");

	// Load archive if running a compressed variant
	if (isCompressed()) {
		Common::Archive *cabinet = Common::makeInstallShieldArchive("data");
		if (cabinet) {
			SearchMan.add("data1.cab", cabinet);
		}
	}

	_resource->readCifTree("ciftree", "dat", 1);
	_resource->readCifTree("promotree", "dat", 1);

	if (getGameType() >= kGameTypeNancy16) {
		// Nancy16+ names its player-character trees in the PCUI chunk, which lives in
		// BOOT and so isn't available yet. They get loaded further down instead.
	} else if (getGameType() >= kGameTypeNancy15) {
		_resource->readCifTree("PUI_CRE_Nancy_Default", "dat", 1);
		// Other player character CIF trees are loaded on demand,
		// based on the PCUI chunk:
		// - PUI_CRE_Nancy_Jungle
		// - PUI_CRE_Nancy_Pink_Hibiscus
		// - PUI_CRE_Nancy_Teal_Hibiscus
		// - PUI_CRE_Frank_Default
		// - PUI_CRE_HB_Default
		// - PUI_CRE_Joe_Default
	}

	// Read the static data. Up to Nancy11 it lives in nancy.dat; from Nancy12
	// onwards the game ships it in its own data files, so the engine only needs
	// to provide the few remaining hardcoded values itself.
	if (getGameType() <= kGameTypeNancy11) {
		readDatFile();
	} else {
		populateStaticData();
	}

	// Setup mixer
	syncSoundSettings();

	IFF *iff = _resource->loadIFF("boot");
	if (!iff)
		error("Failed to load boot script");

	// Load BOOT chunks data
	Common::SeekableReadStream *chunkStream = nullptr;
	#define LOAD_BOOT_L(t, s)	if (chunkStream = iff->getChunkStream(s), chunkStream) {	\
									_engineData.setVal(s, new t(chunkStream));				\
									delete chunkStream;										\
								}
	#define LOAD_BOOT(t) LOAD_BOOT_L(t, #t)

	#define LOAD_CHUNK(n, t, s, k)	iff = _resource->loadIFF(n);								\
									if (!iff)													\
										error("Failed to load %s", n);							\
									if (chunkStream = iff->getChunkStream(s), chunkStream) {	\
										_engineData.setVal(k, new t(chunkStream));				\
										delete chunkStream;										\
									}															\
									delete iff;

	LOAD_BOOT_L(ImageChunk, "OB0")
	LOAD_BOOT_L(ImageChunk, "FR0")
	LOAD_BOOT_L(ImageChunk, "LG0")

	// One weird version of nancy3 has a partner logo implemented the same way as the other image chunks
	LOAD_BOOT_L(ImageChunk, "PLG0")

	// For all other games (starting with nancy4) the partner logo is a larger struct,
	// containing video and sound data as well. Those go unused, however, so we still
	// treat is as a simple image. Note the O instead of the 0 above.
	LOAD_BOOT_L(ImageChunk, "PLGO")

	LOAD_BOOT(BSUM) // This checks for PLG0, do NOT reorder
	LOAD_BOOT(VIEW)
	LOAD_BOOT(PCAL)
	LOAD_BOOT(INV)
	LOAD_BOOT(TBOX)
	LOAD_BOOT(HELP)
	LOAD_BOOT(CRED)
	LOAD_BOOT(MENU)
	LOAD_BOOT(LOAD)
	LOAD_BOOT(SET)
	LOAD_BOOT(SDLG)
	LOAD_BOOT(MAP)
	LOAD_BOOT(HINT)
	LOAD_BOOT(SPUZ)
	LOAD_BOOT(CLOK)
	LOAD_BOOT(SPEC)
	LOAD_BOOT(RCPR)
	LOAD_BOOT(RCLB)
	LOAD_BOOT(TABL)
	LOAD_BOOT(MARK)

	// Nancy 10+
	// FONT chunk has been moved into a separate file
	// FR0 chunk has been removed
	LOAD_BOOT(SHUI)	// Shared UI elements
	LOAD_BOOT(TASK)	// Task bar (main UI)
	LOAD_BOOT(UIIV)	// Inventory UI
	LOAD_BOOT(UICO)	// Conversation UI
	LOAD_BOOT(UICL)	// Cell phone UI
	LOAD_BOOT(UIBW)	// Web browser UI
	LOAD_BOOT(UINB)	// Notebook UI

	// Nancy 11+
	LOAD_BOOT(SCTB)	// Scrollable text box UI

	// Nancy 12+
	// HINT chunk has been removed (the hint system and its action record were dropped in Nancy12)
	// EVNT chunk added, which contains event flag names (loaded below)
	LOAD_BOOT(UIRC)	// UI overlay element table

	// Nancy 13+
	// RCPR and RCLB chunks have been removed (used in the RayCast puzzle, which has been dropped)
	LOAD_BOOT(MMIX)	// Music mix table (location code -> music tracks)

	// Nancy 14
	LOAD_BOOT(UICM)	// Camera UI

	// Nancy 15+
	// The EVNT, UICO, SCTB, TASK, UIRC, UIIV, UICM, UICL, UIBW, UINB chunks, plus two new ones
	// (PUIH, PUIV) have been moved from the BOOT chunk to different per-character chunks,
	// based on names specified in the PCUI chunk.
	// For now, we load Nancy's chunk at boot below, from PUI_CRE_NANCY_DEFAULT_BOOT
	LOAD_BOOT(LVLN)	// Level name table (scene-prefix code -> display name)
	LOAD_BOOT(PCUI)	// Player-character selector (Nancy / Frank / Joe)
	LOAD_BOOT(LDSN)	// Player-character "Design Select" screen layout

	// Nancy 16+
	// LVLN and MMIX have been merged into ENVS, which additionally names each
	// environment's ambient sound
	LOAD_BOOT(ENVS)	// Environment table (scene-prefix code -> name, ambience, music)

	_cursor->init(iff->getChunkStream("CURS"));

	_graphics->init();

	if (getGameType() >= kGameTypeNancy16) {
		// Nancy16 ships a font *registry* rather than glyph atlases: N FONT
		// chunks naming real typefaces, plus M COLR palette chunks, with no
		// count field anywhere - so iterate until the chunks run out.
		IFF *fontIFF = _resource->loadIFF("font");
		if (!fontIFF) {
			error("Failed to load font IFF");
		}

		FontRegistry *reg = new FontRegistry();
		for (uint i = 0; ; ++i) {
			Common::SeekableReadStream *fc = fontIFF->getChunkStream("FONT", i);
			if (!fc) {
				break;
			}

			reg->readFontChunk(*fc);
			delete fc;
		}

		for (uint i = 0; ; ++i) {
			Common::SeekableReadStream *cc = fontIFF->getChunkStream("COLR", i);
			if (!cc) {
				break;
			}

			reg->readColourChunk(*cc);
			delete cc;
		}

		delete fontIFF;
		_engineData.setVal("FONTREG", reg);

		// The options screen's text-size radio, restored. Nothing has rasterised
		// a glyph yet, so this is the one place it costs nothing.
		_ttfFonts.init(reg, (uint)ConfMan.getInt(kConfFontScale));

		// ...and its matte colour swatch. The parameter the swatch sends is a COLR
		// id, which is only resolvable now that the palette is loaded.
		if (const FontRegistry::ColourEntry *colour =
				reg->findColourByID((uint32)ConfMan.getInt(kConfMatteColour))) {
			_graphics->setMatteColour(colour->argb[kNDUIColorNormal]);
		}
	} else if (getGameType() <= kGameTypeNancy9) {
		_graphics->loadFonts(iff->getChunkStream("FONT"));
	} else {
		IFF *fontIFF = _resource->loadIFF("font");
		if (!fontIFF)
			error("Failed to load font IFF");
		_graphics->loadFonts(fontIFF->getChunkStream("FONT"));
		delete fontIFF;
	}

	preloadCals();

	_sound->initSoundChannels();
	_sound->loadCommonSounds(iff);

	delete iff;

	if (getGameType() >= kGameTypeNancy15) {
		const PCUI *pcui = GetEngineData(PCUI);

		// The character list is read from the data, so both of these are reachable
		// from a broken or unexpected BOOT rather than being engine invariants
		if (!pcui) {
			error("Nancy15 and newer need a PCUI chunk to name the player-character trees");
		}

		if (pcui->characters.empty()) {
			error("The PCUI chunk lists no player characters");
		}

		if (getGameType() >= kGameTypeNancy16) {
			// The character trees couldn't be opened before BOOT was read, since PCUI is
			// what names them. Do it now, then boot from the first character's tree.
			for (const PCUI::Character &chr : pcui->characters) {
				if (!chr.imageName.empty()) {
					_resource->readCifTree(chr.imageName, "dat", 1);
				}
			}
		}

		// Note: the default character is Nancy, so we load her boot chunks here. Her CIF name is
		// PUI_CRE_NANCY_DEFAULT_BOOT.
		iff = _resource->loadIFF(Common::Path(pcui->characters[0].defaultImageName + "_boot"));

		if (!iff) {
			error("Failed to load player-character boot chunks for '%s'",
				pcui->characters[0].defaultImageName.c_str());
		}

		// Nancy16 rebuilt the player UI around the per-screen NDUI description format, so
		// most of these chunks are simply absent from its boot file. LOAD_BOOT skips any
		// chunk it can't find, which leaves the corresponding engine data unset.
		LOAD_BOOT(TASK)
		LOAD_BOOT(UIIV)
		LOAD_BOOT(UICO)
		LOAD_BOOT(UICL)
		LOAD_BOOT(UIBW)
		LOAD_BOOT(UINB)
		LOAD_BOOT(SCTB)
		LOAD_BOOT(UIRC)
		LOAD_BOOT(UICM)
		LOAD_BOOT(PUIH)	// Player-UI header (theme name + swatch image)
		LOAD_BOOT(PUIV)	// Player-UI random-sound bank ("can't" responses)
		delete iff;

		// Nancy16+: PUIH names the tree's own string table, which holds every UI
		// caption and tooltip the NDUI descriptors refer to by id.
		if (getGameType() >= kGameTypeNancy16) {
			auto *puih = (const PUIH *)getEngineData("PUIH");
			if (puih && !puih->textTableName.empty()) {
				LOAD_CHUNK(puih->textTableName.c_str(), CVTX, "CVTX", "UITEXT")
			}
		}
	}

	if (getGameType() >= kGameTypeNancy16 && gDebugLevel >= 1) {
		// Nancy16 describes its UI with NDUI chunks inside the player-character
		// trees. There is no widget runtime for them yet, so this is only a
		// parser self-check; see also the ndui_dump console command.
		// -d3 gives the per-widget listing the console's ndui_dump prints, which
		// a headless run has no other way to reach.
		Common::Array<Common::String> report;
		uint numChunks = 0;
		dumpAllNDUI(report, Common::String(), gDebugLevel >= 3, numChunks);
		for (const Common::String &line : report) {
			debug(1, "%s", line.c_str());
		}
	}

	if (getGameType() >= kGameTypeNancy12) {
		LOAD_CHUNK("FLAGS", EVNT, "EVNT", "EVNT")

		// The inventory table moved out of BOOT into its own member in Nancy16.
		LOAD_CHUNK("INVENTORY", INVD, "INVD", "INVD")

		// The total number of event flags is the 1000 generic flags plus the
		// game-specific flags listed in the EVNT chunk, so compute it from the
		// game data instead of relying on the hardcoded value
		auto *evnt = (const EVNT *)getEngineData("EVNT");
		if (evnt) {
			_staticData.numEventFlags = (uint16)(kNumGenericEventFlags + evnt->eventFlagNames.size());
		}
	}

	// Load convo texts and autotext
	auto *bsum = GetEngineData(BSUM);
	if (bsum && !bsum->conversationTextsFilename.empty() && !bsum->autotextFilename.empty()) {
		LOAD_CHUNK(bsum->conversationTextsFilename.toString().c_str(), CVTX, "CVTX", "CONVO")
		LOAD_CHUNK(bsum->autotextFilename.toString().c_str(), CVTX, "CVTX", "AUTOTEXT")
	}

	#undef LOAD_BOOT_L
	#undef LOAD_BOOT
}

State::State *NancyEngine::getStateObject(NancyState::NancyState state) const {
	switch (state) {
	case NancyState::kLogo:
		return &State::Logo::instance();
	case NancyState::kCredits:
		return &State::Credits::instance();
	case NancyState::kMap:
		return &State::Map::instance();
	case NancyState::kSetup:
		return &State::SetupMenu::instance();
	case NancyState::kHelp:
		return &State::Help::instance();
	case NancyState::kScene:
		return &State::Scene::instance();
	case NancyState::kMainMenu:
		return &State::MainMenu::instance();
	case NancyState::kLoadSave:
		return &State::LoadSaveMenu::instance();
	case NancyState::kSaveDialog:
		return &State::SaveDialog::instance();
	default:
		return nullptr;
	}
}

void NancyEngine::destroyState(NancyState::NancyState state) const {
	switch (state) {
	case NancyState::kLogo:
		if (State::Logo::hasInstance()) {
			State::Logo::instance().destroy();
		}
		break;
	case NancyState::kCredits:
		if (State::Credits::hasInstance()) {
			State::Credits::instance().destroy();
		}
		break;
	case NancyState::kMap:
		if (State::Map::hasInstance()) {
			State::Map::instance().destroy();
		}
		break;
	case NancyState::kHelp:
		if (State::Help::hasInstance()) {
			State::Help::instance().destroy();
		}
		break;
	case NancyState::kScene:
		if (State::Scene::hasInstance()) {
			State::Scene::instance().destroy();
		}
		break;
	case NancyState::kMainMenu:
		if (State::MainMenu::hasInstance()) {
			State::MainMenu::instance().destroy();
		}
		break;
	case NancyState::kSetup:
		if (State::SetupMenu::hasInstance()) {
			State::SetupMenu::instance().destroy();
		}
		break;
	case NancyState::kLoadSave:
		if (State::LoadSaveMenu::hasInstance()) {
			State::LoadSaveMenu::instance().destroy();
		}
		break;
	case NancyState::kSaveDialog:
		if (State::SaveDialog::hasInstance()) {
			State::SaveDialog::instance().destroy();
		}
		break;
	default:
		break;
	}
}

void NancyEngine::preloadCals() {
	auto *pcal = GetEngineData(PCAL);
	if (!pcal) {
		// CALs only appeared in nancy2 so a PCAL chunk may not exist
		return;
	}

	for (const Common::String &name : pcal->calNames) {
		if (!_resource->readCifTree(name, "cal", 2)) {
			error("Failed to preload CAL '%s'", name.c_str());
		}
	}
}

void NancyEngine::readDatFile() {
	Common::SeekableReadStream *datFile = SearchMan.createReadStreamForMember("nancy.dat");
	if (!datFile) {
		error("Unable to find nancy.dat");
	}

	if (datFile->readUint32BE() != MKTAG('N', 'N', 'C', 'Y')) {
		error("nancy.dat is invalid");
	}

	int8 major = datFile->readSByte();
	int8 minor = datFile->readSByte();
	if (major != _datFileMajorVersion) {
		error("Incorrect nancy.dat version. Expected '%d.%d', found %d.%d",
			_datFileMajorVersion, _datFileMinorVersion, major, minor);
	} else {
		if (minor < _datFileMinorVersion) {
			warning("Incorrect nancy.dat version. Expected at least '%d.%d', found %d.%d. Game may still work, but expect bugs",
			_datFileMajorVersion, _datFileMinorVersion, major, minor);
		}
	}

	uint16 numGames = datFile->readUint16LE();
	uint16 gameType = getGameType();
	if (gameType > numGames) {
		// Fallback for when no data is present for the current game:
		// throw a warning and use the last available game data
		warning("Data for game type %d is not in nancy.dat", getGameType());
		gameType = numGames;
	}

	// Seek to offset containing current game
	datFile->skip((gameType - 1) * 4);
	uint32 thisGameOffset = datFile->readUint32LE();
	uint32 nextGameOffset = gameType == numGames ? datFile->size() : datFile->readUint32LE();
	datFile->seek(thisGameOffset);

	_staticData.readData(*datFile, _gameDescription->desc.language, nextGameOffset, major, minor);

	delete datFile;
}

void NancyEngine::populateStaticData() {
	// The number of inventory items and cursor types is the only per-game data
	// that still has to be hardcoded: both are needed to parse the INV and CURS
	// chunks, which consume the counts rather than publishing them.
	switch (getGameType()) {
	case kGameTypeNancy12:
		_staticData.numItems = 70;
		_staticData.numCursorTypes = 37;
		break;
	case kGameTypeNancy13:
		_staticData.numItems = 42;
		_staticData.numCursorTypes = 45;
		break;
	case kGameTypeNancy14:
	case kGameTypeNancy15:
		_staticData.numItems = 50;
		_staticData.numCursorTypes = 44;
		break;
	case kGameTypeNancy18:
		// 48 items is confirmed three ways: the INVD chunk consumes exactly
		// 2 + 48 * 90 bytes, the flag table holds 48 INV_* entries with IDs
		// 0-47 matching the item names one to one, and UI_InvCursors is a
		// 12x8 grid of 96 = 48 x 2 cells.
		_staticData.numItems = 48;
		// 46 satisfies 66 + 24 * 188 + 20 == 4598, the real CURS chunk size,
		// but the 90-vs-92 record boundary has not been checked at runtime.
		_staticData.numCursorTypes = 46;
		break;
	default:
		_staticData.numItems = 50;
		_staticData.numCursorTypes = 37;
		break;
	}

	// numEventFlags is computed from the EVNT chunk later in bootGameEngine; this
	// is just a fallback if it is absent.
	_staticData.numEventFlags = kNumGenericEventFlags;

	// Nancy16's flag table is keyed by id rather than by position, and the
	// generic per-scene flags sit at 1010-1059 rather than 10-40. Storing the
	// index here meant clearSceneData() cleared ids nothing references, so the
	// generic flags leaked from one scene into the next - which, among other
	// things, made the difficulty-select screen skip itself.
	//
	// The extent is 50 labels, 1010-1059, MEASURED over the 14143-record corpus,
	// not carried over from nancy3-11's 31. Three facts fix it:
	//   * the game's own EVNT name table names labels 0-47 (INV_*) and 2000-2756
	//     (EV_*) and nothing in between, so the whole 1xxx band is anonymous
	//     engine scratch by construction;
	//   * the band the data uses is exactly 1010-1055 and 1058-1059 (1056/1057 are
	//     never referenced), and every one of those labels is read only in scenes
	//     that also write it - the "tested but never set" scene ratio is 0.098 for
	//     1010-1040, 0.051 for 1041-1055 and 0.000 for 1058-1059, against 0.826
	//     for the named 2xxx story flags;
	//   * the data mixes the ranges inside single expressions: S4712 rec 132 ORs
	//     Event(1013) through Event(1051) in one chain, and S4450 rec 7 ORs
	//     Event(1058)/Event(1059) with Event(1010)/Event(1011).
	// Stopping at 1040 left 1058/1059 permanently raised, and the ten money-hunt
	// scenes re-entered themselves at frame rate off a "search here" click.
	const uint16 genericBase = getGameType() >= kGameTypeNancy16 ? 1010 : 10;
	_staticData.genericEventFlags.resize(getGameType() >= kGameTypeNancy16 ? 50 : 31);
	for (uint i = 0; i < _staticData.genericEventFlags.size(); ++i) {
		_staticData.genericEventFlags[i] = genericBase + i;
	}

	// Nancy16 has no known won-game flag. The 1042 previously used here was
	// nancy1's wonGameFlagID (42) plus 1000, and in nancy18 label 1042 is an
	// ordinary scratch slot - S3561's Nancy16Telephone raises 1040/1041/1042/1043
	// to report which number was dialled, and 1042 exits to S3662. Pointing
	// wonGameFlagID at it meant that once ConfMan held PlayerWonTheGame the flag
	// was raised at Scene::init and the next phone the player touched teleported
	// them. Winning is recorded in ConfMan by WinGame::execute(), not in a flag,
	// so leaving this unset costs only the replay easter eggs, which are
	// unimplemented anyway (see the TODO on WinGame in action/miscrecords.h).
	_staticData.wonGameFlagID = getGameType() >= kGameTypeNancy16 ? -1 : 42;
	_staticData.logoEndAfter = 4000;

	// Sound channel layout, unchanged since Nancy3
	SoundChannelInfo &sci = _staticData.soundChannelInfo;
	sci.numChannels = 32;
	sci.numSceneSpecificChannels = 14;
	sci.speechChannels = { 12, 13, 30 };
	sci.musicChannels = { 0, 1, 2, 3, 19, 26, 27, 29 };
	sci.sfxChannels = { 4, 5, 6, 7, 8, 9, 10, 11, 17, 18, 20, 21, 22, 23, 24, 25, 31 };

	if (getGameType() >= kGameTypeNancy16) {
		// ...but nancy18 does not put speech where Nancy3 did, and the difference
		// matters as soon as the options screen's Voice slider is live: the mixer
		// only knows a channel's volume group from this table, so dialogue on an
		// SFX channel ignores speech_volume entirely.
		//
		// Measured rather than assumed. A sound is speech exactly when it has a
		// subtitle, since only speech is captioned - the engine itself resolves a
		// caption by sound name against AUTOTEXT and CONVO (soundrecords.cpp).
		// Across the game's 2294 readable type 145 PlaySound records, the sounds
		// that carry a caption play on channel 25 (428 of them) and channel 24
		// (186); every other channel is in single or low double figures and is
		// dominated by uncaptioned effects. Channels 12, 13 and 30 carry *no*
		// captioned sound at all.
		//
		// The inherited three are kept rather than removed: they carry ~22 sounds
		// between them, all uncaptioned crowd walla and footsteps, and nothing
		// measured says they are not speech - only that they are not where the
		// dialogue is.
		sci.speechChannels = { 12, 13, 24, 25, 30 };
		sci.sfxChannels = { 4, 5, 6, 7, 8, 9, 10, 11, 17, 18, 20, 21, 22, 23, 31 };
	}
}

Common::Error NancyEngine::synchronize(Common::Serializer &ser) {
	auto *bootSummary = GetEngineData(BSUM)
	assert(bootSummary);

	// Sync boot summary header, which includes full game title
	ser.syncVersion(kSavegameVersion);

	if (ser.getVersion() > kSavegameVersion) {
		GUI::MessageDialog dialog(_s("Saved game was created with a newer version of ScummVM. Unable to load."));
		dialog.runModal();
		return Common::kUnknownError;
	}

	ser.matchBytes((const char *)bootSummary->header, 90);

	// Sync scene and action records
	NancySceneState.synchronize(ser);
	NancySceneState.getActionManager().synchronize(ser);

	return Common::kNoError;
}

bool NancyEngine::isCompressed() {
	return getGameFlags() & GF_COMPRESSED;
}

} // End of namespace Nancy
