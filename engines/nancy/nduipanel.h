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

#ifndef NANCY_NDUIPANEL_H
#define NANCY_NDUIPANEL_H

#include "engines/nancy/renderobject.h"

#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str-array.h"

#include "graphics/font.h"

namespace Nancy {

struct NDUI;
struct NDUIControl;
struct NDUIAction;
struct NancyInput;
struct TextStyle;

// Draws one NDUI chunk.
//
// The unit here is the chunk, not the widget: a panel owns the alpha of its
// backdrop (eight of them paint a translucent dimmer via backgroundFill), flow
// layout is a panel-level pass over its *visible* children so hiding one
// reflows the rest, and ScrollPanel scrolling is a panel-level offset. Widgets
// are composited into the panel's own surface rather than being RenderObjects
// of their own, which also matches how InventoryPopup and ScrollTextBox already
// work in this engine.
//
// Coordinates need care. NDUI is authored in the game's 800x600 UI space while
// the engine renders 640x480. Artwork is composed at the authored size into a
// scratch surface and scaled down once into _drawSurface, which is output-sized;
// text is drawn afterwards, straight into _drawSurface, so glyphs are rasterised
// at their true output size instead of being resampled. Authored rects convert
// with toScreen(); the conversation and inventory hit-test rects are recorded at
// output scale, relative to the surface.
class NDUIPanel : public RenderObject {
public:
	NDUIPanel(uint16 z) : RenderObject(z) {}
	virtual ~NDUIPanel();

	// Composes the panel and its initially-visible children into _drawSurface.
	// Safe to call with a chunk that has no panel, in which case nothing is drawn.
	// Takes ownership: the chunk is kept for hit-testing and redraws.
	void init(NDUI *chunk);

	// Hit-tests the cursor against the visible widgets and runs the click
	// bindings of whichever one is under it.
	void handleInput(NancyInput &input);

	// Authored-control hit testing is split out so the scene can pick the
	// *smallest* candidate across every panel before dispatching. Panel order
	// alone is not a safe priority: LOWERMATTE's 640x52
	// "InvisibleSkipSplashButton" covers the whole taskbar strip, and being in a
	// later panel it used to swallow every taskbar click.
	// `parentOrigin` is the offset of the control's owner, zero for a control the
	// panel holds directly. Only nested controls need it, and in nancy18 every
	// nested one is inside a zero-sized RadioGroup - but the accumulation matches
	// drawControl's, so the rect tested is the rect drawn.
	Common::Rect controlScreenRect(const NDUIControl &c,
		const Common::Point &parentOrigin = Common::Point()) const;
	bool findControlHit(const Common::Point &mouse, int &outIndex, int &outArea) const;
	void activateControl(int index, NancyInput &input);

	// --- Conversation support -------------------------------------------
	// Nancy16 has no TBOX chunk: conversation text lives in the NDUI CONVO
	// panel instead of the ConversationPopup the engine uses for Nancy10-15.
	// A panel answers to these only if it owns a ConvoCCText control.

	// True if this panel is the one that hosts conversation text.
	bool isConversationPanel() const;

	// Nancy's narration is NOT a conversation. The data separates the two: the
	// centred "CCText" control in LOWERMATTE is the VO caption line, while the
	// framed "ConvoCCText" box is for talking to a character. The engine only
	// ever drove the latter, so narration appeared in the dialogue box.
	bool isNarrationPanel() const;
	void narrationSetCaption(const Common::String &text);
	void narrationClear();

	// Clears any previous line and shows the panel.
	void convoOpen();
	void convoClose();

	// The NPC's line, drawn into ConvoCCText.
	void convoSetCaption(const Common::String &text);

	// Appends a player response below the caption. Returns its index.
	uint convoAddResponse(const Common::String &text);

	// Index of the response under the last click, or -1. Cleared once read.
	int convoTakePickedResponse();

	// --- Stake-out list --------------------------------------------------
	// The STAKEOUT member is built exactly like JOURNAL and TASKLIST: a framed
	// container panel with a scrollbar (`StakeOutDialogContainer`), a bare text
	// panel inside it (`StakeOutDialog`), and a standalone 0x28 chunk holding a
	// single row template (`TextEntry` - class 2 Static, font Convo, wrap on).
	//
	// `TextEntry` is NOT an edit box despite the name. nancy18's only two
	// class-7 EditBoxes are `SceneName` in the cheat panel and `SaveName` in the
	// save dialog; this one is a list row, byte-for-byte the same shape as
	// JOURNAL's `JournalEntry` (also a 317-byte 0x28 chunk). Nothing is typed
	// here - AR 212 writes the four agents' radio reports into the list and the
	// player answers by clicking a silhouette out in the courtyard.

	// True if this panel is the stake-out list (it owns StakeOutDialog).
	bool isStakeoutPanel() const;

	// Replaces the whole list; an empty array clears it. The strings are CONVO
	// entries and carry their own <cN> colour codes - one colour per agent,
	// which is where the four colours in the retail frame come from.
	void stakeoutSetLines(const Common::StringArray &lines);

	// --- Journal / task-list lists ---------------------------------------
	// Both are runtime lists in exactly the sense the stake-out is: the members
	// ship one row *template* rather than N authored rows, and what the rows say
	// is a function of the event flags. The scene derives them (see
	// Scene::refreshJournal) and pushes them here.
	//
	// The two templates are `JournalEntry`, a class-2 Static, and
	// `TasklistEntry`, a class-10 CheckBox carrying a 17x17 checked sprite from
	// TaskList_Source. They live in 0x28 chunks with no panel of their own, so
	// the scene copies each into the panels of its member rather than the panel
	// finding it - the same problem drawConversation has with `ConvoResponse`,
	// solved by keeping the descriptor instead of falling back to the box's font.

	struct ListRow {
		Common::String text;
		bool checked = false;
	};

	// Takes a copy of the member's 0x28 row template. Optional: a panel without
	// one falls back to the anchor's own font, as drawStakeout does.
	void setRowTemplate(const NDUIControl &tmpl);

	// `anchorName` is the control whose bounds are the box the rows go in: the
	// panel's own root for the task list and the four journal pages, and the
	// `JournalItems` ListBox for the journal's heading list.
	void setListRows(const Common::String &anchorName, const Common::Array<ListRow> &rows);

	// Highlights one row. -1 for none.
	void setListSelection(int index);

	// Index of the row under the last click, or -1. Cleared once read.
	int listTakePicked();

	// Moves the visible window by `delta`, in that list's own units, clamped.
	// Redraws only if it moved.
	//
	// THE SINGLE WRITER of all three list positions - the save list's, the
	// journal/task list's and the item grid's. Arrows, track pages, thumb drags,
	// Scene::applyNDUIScroll and the inventory's movement keys all come through
	// here, so there is one clamp per list rather than one per caller.
	void listScrollBy(int delta);

	// True if this panel holds a runtime list at all.
	bool hasList() const { return !_listAnchor.empty(); }

	// This panel's own list position, in that list's own units: rows for the save
	// list, scroll stops for the journal/task list, rows of the item grid for the
	// inventory. False when the panel has no scrollable list, which is what keeps
	// the bars with nothing behind them looking and behaving exactly as they did.
	//
	// Public, beside listScrollBy, because the two are twins and the Scene needs
	// both: applyNDUIScroll writes a panel's list through one, nduiScrollModel
	// reads the same list back through the other.
	bool scrollModel(int &position, int &page, int &range);

	// Debug affordance: the on-screen rects of the responses currently offered.
	// Conversation replies are laid out at runtime and are not action-record
	// hotspots, so without this a headless run can see a conversation but has
	// no way to answer one - every reply looks like empty screen.
	void debugGetConvoResponseRects(Common::Array<Common::Rect> &out) const;

	// Debug affordance: the on-screen rect of every inventory item currently
	// laid out in the grid, with its item id. Like the conversation replies
	// above, the grid is built at runtime (drawInventory) rather than authored
	// as controls, so debugGetClickableWidgets cannot see it - and clicking one
	// of these is the only way to put an item ON THE CURSOR. That matters
	// beyond tidiness: S3803's keypad cover has two records on the same click
	// point, one requiring an empty hand and one requiring item 23, and the
	// empty-handed one is the one that gets Nancy caught. A route that cannot
	// pick an item up cannot take the warehouse gate at all.
	struct DebugInvItem {
		int itemID;
		Common::Rect rect;
	};

	void debugGetInvItemRects(Common::Array<DebugInvItem> &out) const;

	// Debug affordance: every widget handleInput() would accept a click on right
	// now, with the action it would run. The PDA - and with it the entire chase
	// interface (PDAArrow*, PDAChase*) - is authored as NDUI widgets rather than
	// action-record hotspots, so a headless run is blind to it without this.
	struct DebugWidget {
		Common::String name;
		Common::Rect rect;
		Common::String actions;
	};
	void debugGetClickableWidgets(Common::Array<DebugWidget> &out) const;

	// Applies an NDUI command to one of this panel's controls. Returns false if
	// the panel does not own that control, so a caller can try the next one.
	bool applyCommand(const Common::String &target, uint32 commandID);

	// --- Settings widgets ------------------------------------------------
	// The options screen's sliders, radio buttons and check box show a setting
	// the engine owns rather than state of their own, so they are read back
	// through Scene::getNDUISettingValue() by the pseudo-target their value
	// bindings name. Nothing here knows a control name: which setting a widget
	// drives is read off its own action list, exactly as the click dispatch is.
	// A panel being shown drops its runtime slider positions, so a re-opened
	// screen shows what is configured now rather than what it was left showing.

	// --- Save/restore of the runtime show/hide deltas --------------------
	// The widget tree's visibility is authored in the chunk but moves at
	// runtime: the taskbar swaps ShowInv for HideInv, InvDialog pulls its own
	// backdrop up, and S3552 is the one place in the game that reveals the HUD
	// (CoinPurse, ShowPaperDoll). None of that follows from _flags, so a save
	// that did not carry it restored the authored tree instead of the player's.
	//
	// What is stored is the *delta* against the state the panel was built in -
	// the authored `state` enum, plus whatever setStartsHidden() forced - so a
	// save stays small and a panel the save does not mention is left alone.

	// This panel's identity in a save: its root control's name. Every panel
	// nancy18 loads has a distinct one (LowerMatteLeft, CoinPurseDialog, ...),
	// while control names are only unique within a panel - every panel owns a
	// control called "Tooltip" - so the pair (panel root, control) is the key.
	Common::String getPanelName() const;

	// Names whose runtime visibility, or runtime enabled-ness, differs from the
	// built state. Appends; does not clear.
	void getStateDelta(Common::StringArray &shownNames, Common::Array<byte> &shownValues,
		Common::StringArray &enabledNames, Common::Array<byte> &enabledValues) const;

	// Resets to the built state and applies `delta` over it, then re-composes.
	//
	// Deliberately does NOT fire the OnShow/OnHide bindings: those already ran
	// before the save was taken and their results are themselves entries in the
	// delta, so firing them again would overwrite restored state with authored
	// propagation.
	void applyStateDelta(const Common::StringArray &shownNames, const Common::Array<byte> &shownValues,
		const Common::StringArray &enabledNames, const Common::Array<byte> &enabledValues);

	// True if this panel hosts the inventory grid (it owns InvDialog).
	bool isInventoryPanel() const;

	// Re-reads the held-item list and redraws the grid.
	void refreshInventory();

	// --- Save / load dialog support --------------------------------------
	// LOADGAME and SAVEGAME are authored screens whose *contents* are savegame
	// data: a ListBox of saves, an EditBox for the name, and a Static whose
	// element sourceName is the pseudo-member "Engine_LoadSave" - the thumbnail
	// the engine is expected to produce. None of that is in the file, so the
	// panel supplies it, the same way it supplies the inventory grid and the
	// conversation replies.

	// True if this panel owns the named control (public form of findControl).
	bool ownsControl(const Common::String &name);

	// Re-reads the save list off the metaengine and re-composes. Called when the
	// dialog opens and after a save is written.
	void saveLoadRefresh();

	// The slot currently picked in the list, or -1.
	int saveLoadSelectedSlot() const;

	// The contents of the SaveName edit box.
	const Common::String &saveLoadName() const { return _saveName; }
	void saveLoadSetName(const Common::String &name);

	// Var::Static widgets show a live player-table value (the coin purse shows
	// index 5, Nancy's money). Nothing else in the engine knows when a SetValue
	// record has moved one, so the panel polls: this re-reads every binding it
	// owns and re-composes only when one has actually changed. Cheap enough to
	// call every frame - Nancy18 authors a single bound Var::Static.
	void refreshBoundValues();

	// The same shape of poll as refreshBoundValues, for the same reason, and the
	// ONLY thing that couples a scroll bar to the list it scrolls: the two are in
	// different panels, so a thumb is a cache of another panel's state and this
	// re-composes when that state has moved out from under it.
	//
	// A poll rather than a refresh the writers call, because the writers cannot be
	// enumerated: the two loops that recompose every panel do it in an order that
	// composes each thumb before its target (see nduipanel.cpp), selectJournalPage
	// changes which page is live without touching the panel holding the bar, and
	// drawInventory re-clamps the grid's own offset from inside its own compose.
	// Anything added later is covered without knowing this exists.
	//
	// RenderObject's hook, so it runs once a frame right before the compositor
	// reads the surface - which makes the settle same-frame for every one of those
	// paths, not just the ones that happen to run inside Scene::process().
	void updateGraphics() override;

	// Re-composes the panel from the chunk it already holds, keeping every
	// runtime delta. Needed when something the panel draws with has changed under
	// it - the text-size setting rebuilds every font, and a panel that has already
	// rasterised its strings has to lay them out again at the new size.
	void recompose() { redraw(); }

	// Number of widget images actually blitted by the last init(). Zero means
	// the panel resolved no artwork, which is a bug rather than an empty screen.
	uint getBlitCount() const { return _blitCount; }
	uint getTextCount() const { return _textCount; }

protected:
	bool isViewportRelative() const override { return false; }

private:
	// The dialog's authored background wash (panelHeader[9], big-endian ARGB),
	// laid down under the panelStatic's artwork.
	void drawPanelFill(const NDUI *chunk, const Common::Point &origin);

	// Blits one control's element artwork, then recurses into its children.
	// `origin` is the panel's own top-left, since control bounds are panel-relative.
	void drawControl(const NDUIControl &control, const Common::Point &origin);

	// One atlas sub-rect into the authored-size scratch surface, modulated by an
	// ARGB texture colour. The element pass, the slider parts and the set-state
	// marks all go through here. False if the atlas or the rect is unusable.
	bool blitAtlasRect(const Common::String &atlasName, const Common::Rect &srcRect,
		const Common::Point &dest, uint32 modulate, const Common::String &controlName,
		const Common::Rect *clipTo = nullptr);

	// One entry per control a click can reach: the control and the origin its
	// bounds are measured from. The index findControlHit returns is an index into
	// this list, so activateControl rebuilds it the same way.
	struct HitCandidate {
		const NDUIControl *control;
		Common::Point origin;
	};
	void collectControls(const Common::Array<NDUIControl> &children,
		const Common::Point &origin, Common::Array<HitCandidate> &out) const;

	// The Engine_* pseudo-target a control's value bindings name, or empty. This
	// is what makes a widget a settings widget.
	static Common::String settingTarget(const NDUIControl &control);

	// A slider's current position, in its own authored range.
	int sliderValue(const NDUIControl &control) const;

	// True when a radio button is the current choice, or a check box is ticked.
	// Read from the setting: a radio compares it against the parameter its own
	// binding carries, a check box against zero.
	bool controlIsSet(const NDUIControl &control) const;

	// Track, filled bar and thumb, positioned from the value.
	void drawSlider(const NDUIControl &control, const Common::Point &origin);

	// Up button, track and down button in DXUT's places, rather than the four
	// elements stacked in the control's top-left corner. Element 3, the thumb,
	// is *not* drawn here - see drawScrollBarThumbs.
	void drawScrollBar(const NDUIControl &control, const Common::Point &origin);

	// Where a scroll bar's parts are on screen, and how far through its list it
	// is. One function answers this for both the paint and the hit test, because
	// the two disagreeing is the bug being fixed: the thumb the player aims at
	// has to be the rect the click is measured against.
	//
	// Every rect is ABSOLUTE SCREEN, composed exactly as handleScrollBarInput
	// already composed the two buttons, so the arrow hit boxes are unchanged.
	// `thumb` is EMPTY whenever the bar must not be draggable - no list behind
	// it, a list that fits, or a track too short to hold a thumb - which is
	// DXUT's collapsed m_rcThumb and the signal the rest of the code branches on.
	struct ScrollBarGeom {
		Common::Rect bar;
		Common::Rect up;
		Common::Rect down;
		Common::Rect track;
		Common::Rect thumb;
		int position = 0;
		int page = 0;
		int range = 0;
		int srcHeight = 0;	// authored crop height for elements[3]
	};
	bool scrollBarGeom(const NDUIControl &control, const Common::Point &origin,
		ScrollBarGeom &out);

	// True if this bar names what it scrolls in an event-15 binding, i.e. scrolls a
	// list in some other panel. THE LATCH the read and the write both branch on,
	// factored out so there is exactly one answer to "whose list is this?" - see
	// nduipanel.cpp for the measured case that forces it to be the binding's mere
	// presence rather than whether the target resolves.
	static bool barIsRouted(const NDUIControl &control);

	// Which model a bar describes, and where its delta goes: resolved through the
	// bar's own event-15 targets, or - only for a bar carrying none - by this
	// panel's own list. Written as the same shape over the same latch on purpose;
	// see nduipanel.cpp for what goes wrong when they disagree.
	bool barScrollModel(const NDUIControl &control, int &position, int &page, int &range);
	void barScrollBy(const NDUIControl &control, int delta);

	// Element 3 of every visible bar, cropped to its computed length. Separate
	// from drawScrollBar because the page size is only known once the rows have
	// been laid out, which happens after the artwork pass has been downscaled -
	// the same reason the task list's tick boxes go through
	// blitAtlasRectToOutput. Mirrors drawControl's own recursion so a thumb
	// appears exactly where a track was painted.
	void drawScrollBarThumbs(const Common::Array<NDUIControl> &children,
		const Common::Point &origin);

	// What the last compose resolved for one ROUTED bar: the answer barScrollModel
	// gave while the thumb above was being painted. updateGraphics() asks the same
	// question again each frame and re-composes when the answer has changed, which
	// is the whole of the coupling between a bar and the list it scrolls.
	//
	// `resolved` is separate from the three numbers rather than folded into them:
	// a routed bar whose target is hidden reports nothing at all, and that state
	// has to be distinguishable from a target reporting zeroes - going from one to
	// the other is exactly the load-a-save case updateGraphics exists for.
	//
	// The bare pointer is safe for _barDrag's reason, and only that one: the only
	// place the parsed tree is deleted is the guard at the top of init(), and every
	// entry here is dropped a few lines below it, before anything can read one.
	// Unrouted bars are deliberately absent - their list is in this same panel and
	// was laid out by this same compose - which is what keeps the five bars with no
	// binding out of this mechanism entirely.
	struct ThumbWatch {
		const NDUIControl *control = nullptr;
		bool resolved = false;
		int position = 0;
		int page = 0;
		int range = 0;
	};

	Common::Array<ThumbWatch> _thumbWatch;

	// The class-specific "checked" sprite, drawn only when the option is current.
	void drawSetMark(const NDUIControl &control, const Common::Point &origin);

	// Press, drag and release on a slider. Returns true if it consumed the input,
	// which it does for the whole gesture - a drag that wanders off the track
	// still belongs to the slider it started on.
	bool handleSliderInput(NancyInput &input);

	// Resolves the control's caption id against the UI string table and draws it
	// in the font its descriptor names.
	void drawControlText(const NDUIControl &control, const Common::Point &origin);

	// Draws one string, honouring the inline markup the string tables carry, into
	// _drawSurface at output scale. `base` is the style a string with no codes of
	// its own gets. Returns the height the laid-out text consumed.
	//
	// skipLines drops that many wrapped lines off the front and maxLines caps how
	// many are drawn (0 = to the end); together they let one paragraph be shown
	// a boxful at a time. Only drawList uses them - see the note there.
	int drawStyledText(const Common::String &text, const TextStyle &base,
		int x, int y, int wrapWidth, Graphics::TextAlign align,
		uint skipLines = 0, uint maxLines = 0);

	// Assigns positions to children that were authored at 0,0 and are meant to be
	// spaced out by the panel. Fills _flowPos; a control absent from it keeps its
	// authored bounds.
	void layOutFlowChildren(const NDUI *chunk);

	// Runs one action binding. Returns true if it changed what is on screen.
	bool runAction(const NDUIAction &action, const NDUIControl &source);

	// Finds a control by instance name anywhere in this panel's tree.
	NDUIControl *findControl(const Common::String &name);

	// Re-composes after a Show/Hide changed a widget's visibility.
	void redraw();

	NDUI *_chunk = nullptr;

	// Visibility is authored in the chunk but changes at runtime, so it is
	// tracked here by control name rather than mutated in the parsed data.
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _visible;

	// _visible as the panel was built: the authored `state` enum, then whatever
	// setStartsHidden() forced on top. Written by those two places only, and
	// never again - it is the baseline the save deltas are taken against, so it
	// has to stay independent of anything the player did.
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _baselineVisible;

	// Panel-assigned positions for flow-laid children, in panel space.
	Common::HashMap<Common::String, Common::Point, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _flowPos;

	// Conversation state. Empty caption and no responses means "not in use",
	// which is how non-CONVO panels stay unaffected.
	Common::String _convoCaption;
	Common::String _narrationCaption;
	Common::StringArray _convoResponses;
	Common::Array<Common::Rect> _convoResponseRects;	// surface space, output scale
	int _convoPicked = -1;

	// Stake-out list rows. Empty means "not in use", which is how every other
	// panel stays unaffected by this.
	Common::StringArray _stakeoutLines;

	// Journal / task-list rows. _listAnchor empty means "not in use".
	Common::String _listAnchor;
	Common::Array<ListRow> _listRows;
	Common::Array<Common::Rect> _listRowRects;	// surface space, output scale
	Common::Array<uint> _listRowIndices;		// which row each rect belongs to
	int _listScroll = 0;
	int _listSel = -1;
	int _listPicked = -1;

	// _listScroll counts scroll *stops*, not rows: a row that fits the box is one
	// stop, as a row has always been, but a row taller than the box gets one stop
	// per boxful of its own wrapped lines. drawList works the count out (it needs
	// the wrap width) and leaves it here for listScrollBy to clamp against.
	int _listStops = 0;

	// A copy, so the 0x28 chunk it came from can be dropped like every other
	// panel-less chunk and the four journal pages can each hold one.
	NDUIControl *_rowTemplate = nullptr;

	// Draws the rows into the anchor's box, one wrapped paragraph each, with the
	// template's tick box in front of them when it has one.
	void drawList();

	// One atlas sub-rect straight into _drawSurface, downscaled on the way. The
	// artwork pass composes into _artSurface and is long gone by the time the
	// rows are laid out, so the task list's tick boxes cannot go through
	// blitAtlasRect - their positions are not known until the text has wrapped.
	bool blitAtlasRectToOutput(const Common::String &atlasName, const Common::Rect &srcRect,
		const Common::Point &dest, const Common::String &controlName);

	// Press on a ScrollBar. A bar names what it scrolls in its own event-15
	// bindings, so this routes through the scene; a bar with no bindings
	// (JournalItemsScrollBar, LOADGAME's, SAVEGAME's) scrolls its own panel, and
	// is the only kind whose thumb and track are live - see scrollBarGeom.
	bool handleScrollBarInput(NancyInput &input);

	// The authored-size scratch surface the artwork pass composes into. Only
	// non-null for the duration of init(); text never goes here, because this
	// surface is scaled down on its way into _drawSurface.
	Graphics::ManagedSurface *_artSurface = nullptr;

	// Captions found during the artwork pass, drawn once the downscale is done.
	// The pointers are into _chunk, which outlives init().
	struct PendingText {
		const NDUIControl *control;
		Common::Point origin;
	};
	Common::Array<PendingText> _pendingText;

	// Top-left of the composed surface in authored space. Control bounds have to
	// be rebased against it: the panel root's are absolute, and the surface
	// starts at the union of the root box and everything inside it.
	Common::Point _surfaceTopLeft;

	// Last value drawn for each bound Var::Static, by control name. Absent means
	// "not drawn yet", which is what makes the first poll after a value appears
	// count as a change.
	Common::HashMap<Common::String, int16, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _boundValues;

	// Item ids drawn in the grid, in draw order, with their on-panel rects.
	Common::Array<uint16> _invItems;
	Common::Array<Common::Rect> _invRects;	// surface space, output scale
	// First visible ROW of the item grid. Clamped in drawInventory() against
	// the real row count, so it can be nudged freely from input handling.
	int _invScroll = 0;

	// The grid's page and range, in ROWS, recorded by the last drawInventory() and
	// read back by scrollModel(). Zero means no draw has happened yet, which is
	// what stops a bar showing a thumb for a grid that has not been laid out.
	// Recorded rather than recomputed because deriving them needs INVD and
	// UI_INVENTORY, and scrollModel() is called from inside a compose.
	int _invPageRows = 0;
	int _invRangeRows = 0;

	// Draws the inventory grid for the panel that owns InvDialog.
	void drawInventory();

	// The save list this panel's ListBox shows, in slot order, and where each
	// row landed. Empty on every panel that owns no LoadList/SaveList.
	struct SaveRow {
		int slot;
		Common::String name;
	};
	Common::Array<SaveRow> _saveRows;
	Common::Array<Common::Rect> _saveRowRects;	// surface space, output scale
	int _saveSel = -1;							// index into _saveRows, -1 for none
	int _saveScroll = 0;						// first row drawn

	// The save list's page, in ROWS, recorded by the last drawSaveLoad and read
	// back by scrollModel(); zero means no draw has happened yet, like the grid's
	// pair above. It is the number of rows on the LAST page and not the number
	// currently on screen: a wrapped name makes rows variable-height, so the count
	// that fits depends on where the list starts, and a page that changed with the
	// position would change the thumb's length and the drag's scale under the
	// pointer. size - this is exactly the draw's own scroll clamp, which is what
	// keeps the thumb's travel and the list's travel the same distance.
	int _savePageRows = 0;

	// Set by saveLoadRefresh, honoured and cleared by the next drawSaveLoad,
	// which is the only place that knows how many rows fit the box. One-shot
	// because the scroll bar owns the position the rest of the time.
	bool _saveScrollToSel = false;
	Common::String _saveName;					// contents of the SaveName EditBox

	// The list, the name field and the thumbnail, drawn after the downscale so
	// the text is rasterised at output size (same reason drawConversation is
	// there). Returns immediately on a panel with no save list.
	void drawSaveLoad();

	// The ListBox / EditBox control of this panel, or null. Which of the two
	// names it answers to is what tells save and load apart.
	NDUIControl *saveListControl();

	// Fires a control's OnShow/OnHide bindings through the scene, so a widget in
	// another panel can react (the taskbar buttons do exactly this).
	void fireVisibilityBindings(const Common::String &target, bool shown);

	// Set by the scene for panels that must not appear until asked for.
	bool _startsHidden = false;
	bool _wasEverVisible = false;

public:
	// Marks a panel as off-screen until something asks for it. Also records the
	// root control as hidden, so a later Show is a real transition rather than a
	// no-op against the authored state.
	void setStartsHidden();

private:

	// Draws the caption and responses, and refills _convoResponseRects.
	void drawConversation();
	void drawNarration();

	// Draws the stake-out list, one agent report per row.
	void drawStakeout();

	// Runtime enable/disable, set by NDUI command 8/9. Separate from _visible:
	// a disabled widget still draws, it just stops taking input.
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _enabled;

	// Slider positions, by control name. A slider is the one settings widget that
	// needs runtime state of its own: the value moves continuously under the
	// mouse and only the release commits it, so the widget cannot simply mirror
	// the setting the way a radio button does.
	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _sliderValues;

	// Index into _chunk->children of the slider a drag is in progress on, or -1.
	int _sliderDrag = -1;

	// The scroll bar a thumb drag is in progress on, or null. A pointer rather
	// than the children[] index _sliderDrag uses because the three bars this can
	// name are all nested inside their own ListBox and are reached through
	// collectControls, so there is no children[] index that identifies them. The
	// parsed tree is immutable and redraw() hands init() the same _chunk pointer,
	// which the guard at the top of init() will not delete, so the control
	// outlives any number of recompositions - and a drag scrolls, which
	// recomposes, on nearly every frame of the gesture.
	const NDUIControl *_barDrag = nullptr;
	Common::Point _barDragOrigin;	// the origin the press was hit-tested against
	// Where the drag started: the pointer's y, and the scroll position it had.
	// The position follows the pointer's DELTA from these, not the thumb's
	// painted top - see handleScrollBarInput for the drift that caused.
	int _barDragGrabY = 0;			// pointer y at mouse-down, output pixels
	int _barDragGrabPos = 0;		// scroll position at mouse-down

	uint _blitCount = 0;
	uint _textCount = 0;
};

} // End of namespace Nancy

#endif // NANCY_NDUIPANEL_H
