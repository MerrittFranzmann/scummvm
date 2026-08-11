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
#include "engines/nancy/trace.h"

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

	// A scene change requested while a Nancy16 stream's records are executing
	// moves that stream, not the player - see streams.h. `bypassStreams` is for
	// the two callers that mean the main flow specifically: AR 21, which names
	// the stream it addresses, and the stream runtime's own viewport handover.
	void changeScene(const SceneChangeDescription &sceneDescription, bool bypassStreams = false);
	void pushScene(int16 itemID = -1);
	void popScene(bool inventory = false);

	// Nancy16: open the close-up ("view") scene of an INVD item, pushing the
	// current scene so the close-up can come back to it. Shared by AR 126
	// GoInvViewScene and by the NDUI InvButtons, which are the same operation on
	// the same table - INVD item 45 is literally named "ShowPDA" and item 46
	// "ShowPaperDoll", matching the taskbar widgets that invoke Engine_Inv.
	// Returns false when the item has no usable view scene.
	bool openItemViewScene(int16 itemID);

	// Undo the most recent pushScene, whichever kind it was. Returns false when
	// nothing is pushed. This is the engine half of the Nancy16 close-up idiom:
	// the data closes a close-up with an empty-name "End a stream" (AR 27) and
	// never with a PopInvViewPriorScene, of which the game has zero.
	bool popPushedScene();

	// Nancy16 parallel script flows. Empty on every earlier game.
	StreamManager &getStreams() { return _streams; }

	// Scene-elapsed time for whichever flow is executing: the running stream's
	// own clock inside a stream, the player's otherwise.
	Time getFlowSceneTime() const;

	// Counts a visit to a scene the way Scene::load does. Used by the stream
	// runtime, whose scene entries also have to be counted (s5450's chase gives
	// up after 20 of them).
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
	byte getPlayerTOD() const;

	void addItemToInventory(int16 id);
	void removeItemFromInventory(int16 id, bool pickUp = true);
	int16 getHeldItem() const { return _flags.heldItem; }
	void setHeldItem(int16 id);
	void setNoHeldItem();
	byte hasItem(int16 id) const;
	byte getItemDisabledState(int16 id) const { return _flags.disabledItems[id]; }
	void setItemDisabledState(int16 id, byte state) {
		if ((uint16)id < _flags.disabledItems.size())
			_flags.disabledItems[id] = state;
	}

	void installInventorySoundOverride(byte command, const SoundDescription &sound, const Common::String &caption, uint16 itemID);
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
	int32 getUIResource(uint index);
	void setUIResource(uint index, int32 value);

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

	void synchronize(Common::Serializer &serializer);

	// The Nancy16 NDUI show/hide deltas (registry Y15). Split out of
	// synchronize() because it is a self-contained, versioned block; see
	// nduipanel.h for what a "delta" is and why the baseline is what it is.
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

	// The STAKEOUT list panel, used by AR 212 (the ORP night stake-out, S5610).
	// Null in every game before Nancy16 and whenever STAKEOUT is absent.
	NDUIPanel *getStakeoutPanel() { return _stakeoutPanel; }

	// Routes an NDUI command to whichever panel owns the named widget.
	void applyNDUICommand(const Common::String &target, uint32 commandID);

	// Routes a scroll step to whichever panel owns the named widget. A ScrollBar
	// names what it scrolls in its own event-15 bindings, and in every one of the
	// six routed cases that target is in a *different* chunk from the bar -
	// TasklistScrollBar lives in the container while TasklistDialog is the
	// ScrollPanel inside it, and JournalDetailsScrollBar names all four VENJH
	// pages from the panel that holds the heading list - so this goes through the
	// scene for the same reason applyNDUICommand does.
	void applyNDUIScroll(const Common::String &target, int delta);

	// Read twin of applyNDUIScroll: the scroll model of the panel that owns
	// `target`, in that model's own units. False when no visible panel owns it, or
	// the owner has no model - which is the signal for "no thumb".
	//
	// Called once a frame per visible routed bar, from NDUIPanel::updateGraphics,
	// which is what couples a thumb to a list in another panel. A pure read: it
	// must stay cheap and must not compose anything.
	bool nduiScrollModel(const Common::String &target, int &position, int &page, int &range);

	// Re-derives the journal and the task list from their ciftree members and
	// pushes the result into the panels. Called when the panel is opened, not
	// kept live: the entries are a pure function of the event flags, so there is
	// nothing to accumulate and nothing to save. See datarecords.h.
	void refreshJournal();
	void refreshTasklist();

	// Shows the journal page for heading index 0..3 (VENJH1..VENJH4) and hides
	// the other three. -1 hides all four, which is what closing the journal does.
	void selectJournalPage(int index);

	// Routes an NDUI command aimed at an "Engine_*" pseudo-target - a subsystem
	// rather than a widget. Returns false for the ones nothing here answers to,
	// so the caller can fall through to its widget handling. See nduipanel.cpp
	// for what each target means and where that reading comes from.
	// `value` is the number the command carries: the action's own `param` where it
	// has one (every radio button on the options screen names its setting that
	// way), and otherwise the source widget's current value, which is how the
	// three sliders say where they were dragged to.
	bool applyNDUIEngineCommand(const Common::String &target, uint32 commandID,
		const Common::String &paramString, double value = 0.0);

	// The current value of a settings pseudo-target, for the widgets that display
	// it: a slider's position, a checkbox's tick, a radio group's selection. False
	// for a target that is not one of the settings. See applyNDUISetting().
	bool getNDUISettingValue(const Common::String &target, double &out) const;

	// The panel owning the named control, or null. Panels are only unique by
	// their root name, but LoadList / SaveList / SaveName occur once each in the
	// whole game, so a control name is enough to pick one out.
	NDUIPanel *findNDUIPanelWithControl(const Common::String &name);

	// True while one of the modal NDUI dialogs (options, save, load) is up.
	// Nothing behind it should take a click; see handleInput.
	bool isNDUIDialogOpen() const;

	// Shows or hides the whole conversation dialog.
	void setConversationUIVisible(bool visible);

	// Debug affordance: centre of the nth clickable hotspot currently live in
	// this scene, in screen coordinates. `n` wraps, so callers can just count up.
	// False when the scene has no hotspots at all.
	bool getNthHotspotCentre(uint n, Common::Point &out);

	// Debug affordance: log every live hotspot, with screen rect and owning
	// record, whenever the set changes. Gated on nancy_debug_hotspots.
	void debugLogHotspots();

	// Explorer bookkeeping: remember which scene each explored click led to, so
	// getNthHotspotCentre can refuse to walk straight back into the scene it
	// just left. Called on every scene entry; not gated on any harness key,
	// because it has to stay correct when tracing is off.
	void debugNoteSceneEntry();

	// Harness: machine-readable trace, stall detection, goal assertions.
	// All no-ops unless the matching nancy_* config key is set. See trace.h.
	void traceSceneEntry(bool fromSaveFile);
	void traceTick();
	void traceCheckGoals();
	void traceFinalGoals();
	Common::String traceStateJson() const;
	Common::String traceHotspotArray();
	void traceParseGoals();
	bool traceEvalPred(const Common::String &pred) const;
	UI::Clock *getClock();
	UI::Taskbar *getTaskbar() { return _taskbar; }

	Action::ActionManager &getActionManager() { return _actionManager; }

	// One line naming everything a save is supposed to carry: where the player
	// is, what they hold, and which event flags are up. Comparing the line from
	// before a save with the line from after a load is how the round trip is
	// checked without the save/load GUI or the console.
	Common::String getStateFingerprint() const;

	// Debug affordance: the full set-event-flag list (the fingerprint truncates).
	Common::String debugAllEventFlags() const;

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
	};

	Timers _timers;

	RenderObject _hotspotDebug;

private:
	void init();
	void load(bool fromSaveFile = false);

	// Resolves the just-loaded scene's ENVS environment and hands it to the
	// stream runtime, which starts/stops the location's ambient script.
	void updateEnvironment();
	void run();
	void handleInput();

	// Nancy 11+ AR 69. Advances all running software timers (stored as TimerData
	// puzzle data) and fires any whose configured duration has just elapsed.
	void tickSoftwareTimers(uint32 deltaMs);
	void fireSoftwareTimer(TimerData::Timer &timer);
	void fireTimerTrigger(TimerData::Trigger &trigger);

	// Rect of the open Nancy 10+ taskbar popup, or empty if none.
	Common::Rect activePopupConfinement() const;

	void initStaticData();

	void clearSceneData(bool nextIsNoArt = false);
	void clearPuzzleData();

	// Maps an event flag label to its index in the eventFlags array
	int16 eventFlagToIndex(int16 label) const;

	// Index into the executing stream's private copy of the 1010-1040 scratch
	// flags, or -1 when the flag is not scratch or the main flow is running.
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

	// Nancy16+ replaces the taskbar and popups with NDUI panels. These are the
	// persistent screen furniture - the upper and lower mattes.
	Common::Array<NDUIPanel *> _nduiPanels;

	// The CONVO panel, owned by _nduiPanels. Null before initStaticData and on
	// games that route conversations through the ConversationPopup instead.
	NDUIPanel *_conversationPanel = nullptr;
	NDUIPanel *_narrationPanel = nullptr;

	// Every panel loaded from the CONVO member. The dialog is split across
	// several chunks - a backdrop, a scrollbar and the scrolling text area - and
	// they have to be shown and hidden together or the empty box stays on screen.
	Common::Array<NDUIPanel *> _conversationPanels;

	// The panel holding the inventory grid, owned by _nduiPanels.
	NDUIPanel *_inventoryPanel = nullptr;

	// The panel holding the stake-out report list, owned by _nduiPanels.
	NDUIPanel *_stakeoutPanel = nullptr;

	// The journal is a master/detail: _journalPanel is JOURNAL[2], which owns the
	// JournalItems ListBox of headings, and _journalPagePanels are JOURNAL[4..7],
	// whose roots VENJH1..VENJH4 are the four heading pages. All owned by
	// _nduiPanels.
	NDUIPanel *_journalPanel = nullptr;
	static const uint kNumJournalPages = 4;
	NDUIPanel *_journalPagePanels[kNumJournalPages] = {};

	// TASKLIST[4], whose root TasklistDialog is what the taskbar button names.
	NDUIPanel *_tasklistPanel = nullptr;

	// Which heading page is showing, or -1. Kept so re-opening the journal comes
	// back to the same page rather than to nothing.
	int _journalPage = -1;

	// Parses <base>_<character> for every PCUI character and returns the records
	// whose dependencies are currently satisfied. The caller owns them.
	Common::Array<Action::ActionRecord *> readListMember(const char *base);

	// The load screen's "New" button selects a pseudo-entry called
	// "_StartNewGame_" and then commits, exactly as a list row would. This holds
	// that selection between the two halves. See applyNDUIEngineCommand.
	bool _loaderNewGameSelected = false;

	// Commits that selection: loads the checkpoint the game's own first scene
	// writes under the name "Start_Game". See the comment on the definition.
	void startNewGame();

	// Root controls of the panels that behave modally: while any of them is on
	// screen the scene behind it must not take clicks. Fixed list rather than a
	// property of the panel, because it is the *authoring* that makes them modal
	// - each one raises MODAL's ModalDialog on show - and that is a fact about
	// these four chunks, not about NDUIPanel.
	static const char *const kNDUIModalRoots[];

	// Set while the save screen is up because the player answered "yes" to the
	// quit-time "Do you want to save first?" box, so the save commit knows it
	// still owes them the quit. Cleared if they close the screen instead.
	bool _quitAfterSave = false;

	// The options screen's twelve controls, dispatched by pseudo-target. Split out
	// of applyNDUIEngineCommand because none of them touches the scene: they read
	// and write ConfMan, which is where ScummVM keeps the settings the game is
	// asking about and what makes them outlive the session.
	bool applyNDUISetting(const Common::String &target, uint32 commandID, double value);

	Time _buttonPressActivationTime;

	// A clicked MENU/HELP taskbar button whose state change is deferred until
	// its click sound finishes playing (see handleInput). -1 = none pending.
	int _pendingTaskbarButton;

	UI::ViewportOrnaments *_viewportOrnaments;
	UI::TextboxOrnaments *_textboxOrnaments;
	UI::InventoryBoxOrnaments *_inventoryBoxOrnaments;
	RenderObject *_clock;

	Common::Rect _mapHotspot;

	// General data
	SceneState _sceneState;
	PlayFlags _flags;
	uint16 _difficulty;
	Common::Array<uint16> _hintsRemaining;
	int16 _lastHintCharacter;
	int16 _lastHintID;
	NancyState::NancyState _gameStateRequested;
	Common::HashMap<uint16, InventorySoundOverride> _inventorySoundOverrides;

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

	// Last line printed by debugLogHotspots, so it only prints on a change.
	Common::String _lastHotspotLine;

	// Explorer anti-ping-pong memo. One entry per click point the explorer has
	// taken, carrying the scene it turned out to lead to. See
	// getNthHotspotCentre in scene.cpp for why this exists and what it costs.
	struct DebugExploreEdge {
		int scene;
		int frame;
		int x;
		int y;
		int dest;
	};

	Common::Array<DebugExploreEdge> _exploreEdges;
	int _explorePendingIdx = -1;
	int _exploreCurScene = -1;
	int _explorePrevScene = -1;
	int _exploreTriesHere = 0;

	int debugExploreEdgeDest(int scene, int frame, int x, int y) const;
	void debugExploreArm(int scene, int frame, int x, int y);

	// Harness trace state. Inert unless a nancy_trace* key is set.
	uint32 _traceEntries = 0;
	int _tracePrevScene = -1;
	Common::String _traceHotspotKey;
	int _traceStallPolls = 0;
	Common::String _traceStallKey;
	bool _traceGoalsParsed = false;
	Common::Array<TraceGoal> _traceGoals;

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
