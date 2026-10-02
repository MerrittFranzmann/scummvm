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

#ifndef NANCY_STATE_SCENE_H
#define NANCY_STATE_SCENE_H

#include "common/singleton.h"
#include "common/queue.h"

#include "engines/nancy/commontypes.h"
#include "engines/nancy/nduipanel.h"
#include "engines/nancy/puzzledata.h"
#include "engines/nancy/streams.h"

#include "engines/nancy/action/actionmanager.h"

#include "engines/nancy/state/state.h"

#include "engines/nancy/ui/fullscreenimage.h"
#include "engines/nancy/ui/viewport.h"
#include "engines/nancy/ui/textbox.h"
#include "engines/nancy/ui/inventorybox.h"
#include "engines/nancy/ui/inventorypopup.h"
#include "engines/nancy/ui/notebookpopup.h"
#include "engines/nancy/ui/cellphonepopup.h"
#include "engines/nancy/ui/conversationpopup.h"

namespace Common {
class SeekableReadStream;
class Serializer;
}

namespace Nancy {

class NancyEngine;
class NancyConsole;
struct SceneChangeDescription;

namespace Action {
class ConversationSound;
class PlaySecondaryMovie;
}

namespace Misc {
class Lightning;
class SpecialEffect;
}

namespace UI {
class Button;
class Taskbar;
class ViewportOrnaments;
class TextboxOrnaments;
class InventoryBoxOrnaments;
class Clock;
class Camera;
}

namespace State {

// The game state that handles all of the gameplay
class Scene : public State, public Common::Singleton<Scene> {
public:
	struct SceneSummary {
		// SSUM and TSUM
		// Default values set to match those applied when loading from a TSUM chunk
		Common::String description;
		Common::Path videoFile;

		uint16 videoFormat = kLargeVideoFormat;
		Common::Array<Common::Path> palettes;
		SoundDescription sound;

		byte panningType = kPan360;
		uint16 numberOfVideoFrames = 0;
		uint16 degreesPerRotation = 18;
		uint16 totalViewAngle = 0;
		uint16 horizontalScrollDelta = 1;
		uint16 verticalScrollDelta = 10;
		uint16 horizontalEdgeSize = 15;
		uint16 verticalEdgeSize = 15;
		Time slowMoveTimeDelta = 400;
		Time fastMoveTimeDelta = 66;

		// Sound start vectors, used in nancy3 and up
		Math::Vector3d listenerPosition;

		void read(Common::SeekableReadStream &stream);
		void readTerse(Common::SeekableReadStream &stream);
	};

	Scene();
	virtual ~Scene();

	// State API
	void process() override;
	void onStateEnter(const NancyState::NancyState prevState) override;
	bool onStateExit(const NancyState::NancyState nextState) override;

	// Used when winning/losing game
	void setDestroyOnExit() { _destroyOnExit = true; }

	bool isRunningAd() const { return _isRunningAd; }

	// A scene change requested while a Nancy16 stream is executing moves that
	// stream instead of the player, unless bypassStreams is set.
	void changeScene(const SceneChangeDescription &sceneDescription, bool bypassStreams = false);
	void pushScene(int16 itemID = -1);
	void popScene(bool inventory = false);
	int16 getPushedInvItemID() const { return _sceneState.pushedInvItemID; }

	// Nancy16: pushes the current scene and opens an INVD item's close-up scene.
	// Returns false when the item has no usable view scene.
	bool openItemViewScene(int16 itemID);

	// Undoes the most recent pushScene. Returns false when nothing is pushed.
	bool popPushedScene();

	// Nancy16 parallel script flows. Empty on every earlier game.
	StreamManager &getStreams() { return _streams; }

	// Scene time of the executing stream, or of the main flow outside one
	Time getFlowSceneTime() const;

	// Counts a scene visit; used for scenes entered by streams
	void bumpSceneCount(uint16 sceneID) { _flags.sceneCounts.getOrCreateVal(sceneID)++; }

	// Nancy 11+ "UI prep scenes": opening a taskbar popup first runs a hidden,
	// videoless scene whose event-flag-gated ARs populate the popup's content;
	// a UIPopupPrepScene AR (32) then calls finishUIPrepScene to restore the
	// prior scene and open the (now populated) popup. startUIPrepScene saves
	// the current scene and jumps to prepSceneID; uiType (a UIType) selects
	// which popup finishUIPrepScene opens. No-op if a prep is already running
	// or prepSceneID is kNoScene.
	void startUIPrepScene(int16 uiType, int16 prepSceneID);
	void finishUIPrepScene();
	bool isUIPrepActive() const { return _uiPrep.active; }
	uint16 getSceneCounts(int16 hours) const {
		return _flags.sceneCounts.contains(hours) ? _flags.sceneCounts[hours] : 0;
	}

	void setPlayerTime(Time time, byte relative);
	Time getPlayerTime() const { return _timers.playerTime; }
	Time getTimerTime() const { return _timers.timerIsActive ? _timers.timerTime : 0; }
	uint getPlayerTimeMinutes() const;
	byte getPlayerTOD() const;
	// Nancy14-15. The day is kept separately from the clock, and copied into the
	// day value so the scripts can read it.
	void requestSleep() { _timers.sleepRequested = true; }
	void setPlayerDay(int16 day);

	void addItemToInventory(int16 id);
	void removeItemFromInventory(int16 id, bool pickUp = true);

	// Nancy15+ inventory action records and dependencies pick the character to
	// act on, which needn't be the one being played. Anyone else is served from
	// their parked inventory instead of the live one.
	void addItemToCharacterInventory(uint characterIndex, int16 id);
	void removeItemFromCharacterInventory(uint characterIndex, int16 id, bool pickUp = false);
	byte hasCharacterItem(uint characterIndex, int16 id);
	void setCharacterItemDisabledState(uint characterIndex, int16 id, byte state);

	// AddInventoryNoHS (AR 120): hand the item over, into the character's hand
	// when the record asks for it and their hand is free or forceIntoHand is set
	void giveItemToCharacter(uint characterIndex, int16 id, bool intoHand, bool forceIntoHand);

	int32 getCharacterUIResource(uint characterIndex, uint index);
	int16 getHeldItem() const { return _flags.heldItem; }
	void setHeldItem(int16 id);
	void setNoHeldItem();
	byte hasItem(int16 id) const;
	byte getItemDisabledState(int16 id) const { return _flags.disabledItems[id]; }
	void setItemDisabledState(int16 id, byte state) {
		if ((uint16)id < _flags.disabledItems.size())
			_flags.disabledItems[id] = state;
	}

	// Nancy15 records name the player character the override belongs to;
	// kPlayerCharacterActive (and every earlier game) means whoever is played
	void installInventorySoundOverride(byte command, const SoundDescription &sound,
		const Common::String &caption, uint16 itemID, byte characterIndex = kPlayerCharacterActive);
	void playItemCantSound(int16 itemID = -1, bool notHoldingSound = false);

	void setEventFlag(int16 label, byte flag);
	void setEventFlag(FlagDescription eventFlag);
	bool getEventFlag(int16 label, byte flag) const;
	bool getEventFlag(FlagDescription eventFlag) const;

	// Nancy 11+ software-timer queries used by timer dependencies.
	bool isSoftwareTimerActive(uint16 index) const;
	uint32 getSoftwareTimerElapsed(uint16 index) const;

	// Nancy 12+ UI resource values (the UIRC boot chunk). Resource 0 is the
	// coin purse amount in cents. Backed by the lazily-created, saved
	// UIResourceData puzzle chunk, seeded from UIRC on first use and mutated by
	// AR 132 (ResourceUse). Non-const because the first access creates/seeds it.
	// Nancy15 records name the protagonist whose resources they change;
	// kPlayerCharacterActive (and every earlier game) means whoever is played.
	int32 getUIResource(uint index, byte characterIndex = kPlayerCharacterActive);
	void setUIResource(uint index, int32 value, byte characterIndex = kPlayerCharacterActive);

	void setLogicCondition(int16 label, byte flag);
	bool getLogicCondition(int16 label, byte flag) const;
	void clearLogicConditions();
	Time getLogicConditionTimestamp(int16 label) const {
		return _flags.logicConditions[label].timestamp;	
	}

	void setDifficulty(uint difficulty) { _difficulty = difficulty; }
	uint16 getDifficulty() const { return _difficulty; }

	byte getHintsRemaining() const { return _hintsRemaining[_difficulty]; }
	void useHint(uint16 characterID, uint16 hintID);

	void requestStateChange(NancyState::NancyState state) { _gameStateRequested = state; }
	void resetStateToInit() { _state = kInit; }

	void resetAndStartTimer() { _timers.timerIsActive = true; _timers.timerTime = 0; }
	void stopTimer() { _timers.timerIsActive = false; _timers.timerTime = 0; }

	Time getMovementTimeDelta(bool fast) const { return fast ? _sceneState.summary.fastMoveTimeDelta : _sceneState.summary.slowMoveTimeDelta; }

	// Nancy 11+ AR 30/31. Toggles whether the player may scroll/pan the viewport.
	// Backed by a reserved event flag (so it persists across scene changes and
	// is saved/restored with the rest of the event flags).
	void setPlayerScrolling(bool enabled);
	bool getPlayerScrolling() const;

	void registerGraphics();

	// Nancy15+ AR 134. Hands the game over to another protagonist: swaps in that
	// character's own copy of the popup UI data and rebuilds every widget built
	// from it, then swaps the inventories. Returns whether the character
	// actually changed.
	bool changePlayerCharacter(uint characterIndex);

	// Nancy15+ Design Select screen. Records the look a character wears; the UI
	// is rebuilt from it the next time the scene is entered.
	void setPlayerCharacterDesign(uint characterIndex, const Common::String &designName);

	// Replaces the current scene's background video without leaving the scene.
	// Nancy15+ uses this to show the same location from the newly selected
	// player character's point of view.
	void changeSceneVideo(const Common::Path &videoFile);

	void synchronize(Common::Serializer &serializer);

	// The Nancy16 NDUI show/hide state; see nduipanel.h
	void synchronizeNDUI(Common::Serializer &serializer);

	UI::FullScreenImage &getFrame() { return _frame; }
	UI::Viewport &getViewport() { return _viewport; }
	UI::Textbox &getTextbox() { return _textbox; }
	UI::InventoryBox &getInventoryBox() { return _inventoryBox; }
	UI::InventoryPopup &getInventoryPopup() { return _inventoryPopup; }
	UI::NotebookPopup &getNotebookPopup() { return _notebookPopup; }
	UI::CellPhonePopup &getCellPhonePopup() { return _cellPhonePopup; }
	UI::ConversationPopup &getConversationPopup() { return _conversationPopup; }

	// Null unless the game routes conversations through an NDUI panel (Nancy16+).
	NDUIPanel *getNarrationPanel() { return _narrationPanel; }
	NDUIPanel *getConversationPanel() { return _conversationPanel; }

	// The STAKEOUT list panel used by AR 212. Null if STAKEOUT is absent.
	NDUIPanel *getStakeoutPanel() { return _stakeoutPanel; }

	// Routes an NDUI command to whichever panel owns the named widget.
	void applyNDUICommand(const Common::String &target, uint32 commandID);

	// Routes a scroll step to whichever panel owns the named widget, since a
	// ScrollBar's target is usually in a different panel.
	void applyNDUIScroll(const Common::String &target, int delta);

	// The scroll model of the panel that owns target. Returns false when no
	// visible panel owns it or it has no model. Called every frame.
	bool nduiScrollModel(const Common::String &target, int &position, int &page, int &range);

	// Rebuilds the journal and task list from the event flags when opened
	void refreshJournal();
	void refreshTasklist();

	// Shows journal page 0..3 (VENJH1..VENJH4) and hides the others; -1 hides all
	void selectJournalPage(int index);

	// Handles an NDUI command aimed at an "Engine_*" pseudo-target. Returns false
	// for unhandled targets. value is the action's param, or the source widget's value.
	bool applyNDUIEngineCommand(const Common::String &target, uint32 commandID,
		const Common::String &paramString, double value = 0.0);

	// The current value of a settings pseudo-target, or false if target is not a setting
	bool getNDUISettingValue(const Common::String &target, double &out) const;

	// The panel owning the named control, or null
	NDUIPanel *findNDUIPanelWithControl(const Common::String &name);

	// True while one of the modal NDUI dialogs (options, save, load) is up
	bool isNDUIDialogOpen() const;

	// Shows or hides the whole conversation dialog.
	void setConversationUIVisible(bool visible);

	UI::Clock *getClock();
	UI::Camera *getCamera() { return _camera; }
	UI::Taskbar *getTaskbar() { return _taskbar; }

	Action::ActionManager &getActionManager() { return _actionManager; }

	SceneChangeDescription &getSceneInfo() { return _sceneState.currentScene; }
	SceneChangeDescription &getNextSceneInfo() { return _sceneState.nextScene; }
	const SceneSummary &getSceneSummary() const { return _sceneState.summary; }

	void setActiveMovie(Action::PlaySecondaryMovie *activeMovie);
	Action::PlaySecondaryMovie *getActiveMovie();

	// Called when a PSM(isRandom) AR is loaded — drives stale-chain cleanup.
	void notifyRandomMovieARLoaded() { _hadRandomMovieARThisScene = true; }
	void setActiveConversation(Action::ConversationSound *activeConversation);
	Action::ConversationSound *getActiveConversation();

	Graphics::ManagedSurface &getLastScreenshot() { return _lastScreenshot; }

	// The Vampire Diaries only;
	void beginLightning(int16 distance, uint16 pulseTime, int16 rgbPercent);

	// Used from nancy2 onwards
	void specialEffect(byte type, uint16 fadeToBlackTime, uint16 frameTime);
	void specialEffect(byte type, uint16 totalTime, uint16 fadeToBlackTime, Common::Rect rect);

	// Get the persistent data for a given puzzle type
	PuzzleData *getPuzzleData(const uint32 tag);

	enum State {
		kInit,
		kLoad,
		kStartSound,
		kRun
	};

	State getState() const { return _state; }
	void setState(State state) { _state = state; }

	// Close every open Nancy 10+ popup. Used before a script auto-opens one
	// (e.g. the incoming game-over call) so two popups can't stack.
	void closeActivePopups();

	struct Timers {
		Time pushedPlayTime;
		Time lastTotalTime;
		Time sceneTime;
		Time timerTime;
		bool timerIsActive = false;
		Time playerTime;           // In-game time of day, adds a minute every 5 seconds
		Time playerTimeNextMinute; // Stores the next tick count until we add a minute to playerTime
		bool sleepRequested = false; // Nancy14-15: start the next day on the following frame
		int16 playerDay = 0;         // Nancy14-15: the current day, also copied into the day value
	};

	Timers _timers;

	RenderObject _hotspotDebug;

private:
	void init();
	void load(bool fromSaveFile = false);

	// Passes the scene's ENVS environment to the stream runtime
	void updateEnvironment();
	void run();
	void handleInput();

	// Nancy 11+ AR 69. Advances all running software timers (stored as TimerData
	// puzzle data) and fires any whose configured duration has just elapsed.
	void tickSoftwareTimers(uint32 deltaMs);

	// Raises the late night flag (Nancy11-13) or the end-of-day flag (Nancy14-15)
	// once it gets late. In Nancy14-15, also starts the next day at the wake-up
	// hour after the player has been sent to sleep.
	void updateEndOfDay();
	void fireSoftwareTimer(TimerData::Timer &timer);
	void fireTimerTrigger(TimerData::Trigger &trigger);

	// Rect of the open Nancy 10+ taskbar popup, or empty if none.
	Common::Rect activePopupConfinement() const;

	int16 getCharacterHeldItem(uint characterIndex);
	void setCharacterHeldItem(uint characterIndex, int16 id);
	void returnCharacterHeldItem(uint characterIndex);

	// Nancy15's "can't" responses live in the active player character's PUIV
	// bank instead of the inventory data
	bool getPlayerCantSound(int16 itemID, SoundDescription &sound) const;
	void playPlayerCantSound(int16 itemID);

	void initStaticData();

	void clearSceneData(bool nextIsNoArt = false);
	void clearPuzzleData();

	// Maps an event flag label to its index in the eventFlags array
	int16 eventFlagToIndex(int16 label) const;

	// Rebuilds the popup UI from a Nancy15+ player character's own data files,
	// without touching the inventory. Returns whether the character changed.
	bool applyPlayerCharacter(uint characterIndex);

	// Nancy15+ per-character inventories and UI resources. The active
	// character's are the live ones (_flags plus the inventory box order, and
	// UIResourceData::values); the others are parked in the same puzzle data.
	void storeCharacterInventory(uint characterIndex);
	void loadCharacterInventory(uint characterIndex);
	void storeCharacterResources(uint characterIndex);
	void loadCharacterResources(uint characterIndex);

	// Seeds a Hardy boy's journal and UI resources from his brother's the first
	// time he is played.
	void inheritBrotherProgress(uint characterIndex);
	// Index into the executing stream's copy of the scratch flags, or -1
	int scratchFlagIndex(int16 label) const;

	struct SceneState {
		SceneSummary summary;
		SceneChangeDescription currentScene;
		SceneChangeDescription nextScene;
		SceneChangeDescription pushedScene;
		bool isScenePushed = false;
		SceneChangeDescription pushedInvScene;
		int16 pushedInvItemID = -1;
		bool isInvScenePushed = false;
	};

	struct PlayFlags {
		struct LogicCondition {
			LogicCondition();
			byte flag;
			Time timestamp;
		};

		LogicCondition logicConditions[30];
		Common::Array<byte> eventFlags;
		Common::HashMap<uint16, uint16> sceneCounts;
		Common::Array<byte> items;
		Common::Array<byte> disabledItems;
		int16 heldItem = -1;
		int16 primaryVideoResponsePicked = -1;
	};

	struct InventorySoundOverride {
		bool isDefault = false; // When true, other fields are ignored
		SoundDescription sound;
		Common::String caption;
	};

	// The overrides of the character being played
	Common::HashMap<uint16, InventorySoundOverride> &activeSoundOverrides();

	// UI
	UI::FullScreenImage _frame;
	UI::Viewport _viewport;
	UI::Textbox _textbox;
	UI::InventoryBox _inventoryBox;
	UI::InventoryPopup _inventoryPopup;
	UI::NotebookPopup _notebookPopup;
	UI::CellPhonePopup _cellPhonePopup;
	UI::ConversationPopup _conversationPopup;

	UI::Button *_menuButton;
	UI::Button *_helpButton;
	UI::Taskbar *_taskbar;

	// Nancy16+ replaces the taskbar and popups with NDUI panels
	Common::Array<NDUIPanel *> _nduiPanels;

	// The PDA temporarily disables these toolbar controls during a call.
	// 0xff means no PDA view is holding a return state.
	byte _pdaReturnEnabled = 0xff;

	// The CONVO panel, owned by _nduiPanels. Null on games using ConversationPopup.
	NDUIPanel *_conversationPanel = nullptr;
	NDUIPanel *_narrationPanel = nullptr;

	// Every panel loaded from CONVO; they are shown and hidden together
	Common::Array<NDUIPanel *> _conversationPanels;

	// The panel holding the inventory grid, owned by _nduiPanels.
	NDUIPanel *_inventoryPanel = nullptr;

	// The panel holding the stake-out report list, owned by _nduiPanels.
	NDUIPanel *_stakeoutPanel = nullptr;

	// JOURNAL[2] holds the list of headings, JOURNAL[4..7] the four heading pages
	NDUIPanel *_journalPanel = nullptr;
	static const uint kNumJournalPages = 4;
	NDUIPanel *_journalPagePanels[kNumJournalPages] = {};

	// TASKLIST[4], root TasklistDialog
	NDUIPanel *_tasklistPanel = nullptr;

	// Which journal page is showing, or -1
	int _journalPage = -1;

	// Parses <base>_<character> for every PCUI character and returns the records
	// whose dependencies are currently satisfied. The caller owns them.
	Common::Array<Action::ActionRecord *> readListMember(const char *base);

	// Set when the load screen's "New" button selects the "_StartNewGame_" entry
	bool _loaderNewGameSelected = false;

	// Starts a new game from the "Start_Game" checkpoint
	void startNewGame();

	// Root controls of the modal panels; the scene takes no clicks while one is shown
	static const char *const kNDUIModalRoots[];

	// Set when the save screen was opened from the quit prompt, so saving then quits
	bool _quitAfterSave = false;

	// The options screen controls, stored in ConfMan
	bool applyNDUISetting(const Common::String &target, uint32 commandID, double value);

	Time _buttonPressActivationTime;

	// A clicked MENU/HELP taskbar button whose state change is deferred until
	// its click sound finishes playing (see handleInput). -1 = none pending.
	int _pendingTaskbarButton;

	UI::ViewportOrnaments *_viewportOrnaments;
	UI::TextboxOrnaments *_textboxOrnaments;
	UI::InventoryBoxOrnaments *_inventoryBoxOrnaments;
	RenderObject *_clock;

	UI::Camera *_camera;	// Nancy14 only

	Common::Rect _mapHotspot;

	// General data
	SceneState _sceneState;
	PlayFlags _flags;
	uint16 _difficulty;
	Common::Array<uint16> _hintsRemaining;
	int16 _lastHintCharacter;
	int16 _lastHintID;
	NancyState::NancyState _gameStateRequested;
	// One set of overrides per player character; everything before Nancy15 only
	// ever touches the first
	Common::HashMap<uint16, InventorySoundOverride> _inventorySoundOverrides[kMaxPlayerCharacters];

	Misc::Lightning *_lightning;
	Common::Queue<Misc::SpecialEffect> _specialEffects;

	Common::HashMap<uint32, PuzzleData *> _puzzleData;

	Action::ActionManager _actionManager;

	// Nancy16's parallel script flows. See streams.h.
	StreamManager _streams;

	Action::PlaySecondaryMovie *_activeMovie;
	Action::ConversationSound *_activeConversation;

	// Set by notifyRandomMovieARLoaded; checked in clearSceneData to wind
	// down a persistent random-movie whose scene chain is over.
	bool _hadRandomMovieARThisScene = false;

	// Whether esc was already down last frame, so a held key only skips once
	bool _escHeld = false;

	// Contains a screenshot of the Scene state from the last time it was exited
	Graphics::ManagedSurface _lastScreenshot;

	bool _destroyOnExit;
	bool _isRunningAd;

	// State for a running UI prep scene (see startUIPrepScene).
	struct UIPrepState {
		bool active = false;
		int16 uiType = 0;   // UIType of the popup that requested the prep
		SceneChangeDescription returnScene;
		uint32 startMillis = 0;
	} _uiPrep;

	State _state;
};

#define NancySceneState Nancy::State::Scene::instance()

} // End of namespace State
} // End of namespace Nancy

#endif // NANCY_STATE_SCENE_H
