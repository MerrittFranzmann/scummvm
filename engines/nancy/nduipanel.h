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

// Draws one NDUI chunk. Widgets are composited into the panel's own surface
// rather than being RenderObjects of their own.
//
// NDUI is authored in 800x600 while the engine renders 640x480. Artwork is
// composed at authored size and scaled down once; text is then drawn directly
// at output size. Hit-test rects are stored at output scale.
class NDUIPanel : public RenderObject {
public:
	NDUIPanel(uint16 z) : RenderObject(z) {}
	virtual ~NDUIPanel();

	// Composes the panel and its initially-visible children. Takes ownership of
	// the chunk. A chunk with no panel draws nothing.
	using RenderObject::init;
	void init(NDUI *chunk);

	// Used by the scene for conversation panels; survives recomposition
	void setVisible(bool visible);

	// Hit-tests the cursor against the visible widgets and runs the click
	// bindings of whichever one is under it.
	void handleInput(NancyInput &input);

	// Split out so the scene can pick the smallest hit across all panels, since
	// some large invisible buttons overlap smaller ones in other panels.
	// parentOrigin is the offset of the control's owner, if any.
	Common::Rect controlScreenRect(const NDUIControl &c,
		const Common::Point &parentOrigin = Common::Point()) const;
	bool findControlHit(const Common::Point &mouse, int &outIndex, int &outArea) const;
	void activateControl(int index, NancyInput &input);

	// Conversation support. Nancy16 has no TBOX chunk; conversation text lives in
	// the panel that owns a ConvoCCText control.

	// True if this panel is the one that hosts conversation text.
	bool isConversationPanel() const;

	// Narration captions go into the CCText control, not the conversation box
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

	// Stake-out list. Built like JOURNAL and TASKLIST: a container panel with a
	// scrollbar, a text panel inside it, and a row template.

	// True if this panel is the stake-out list (it owns StakeOutDialog).
	bool isStakeoutPanel() const;

	// Replaces the whole list; an empty array clears it
	void stakeoutSetLines(const Common::StringArray &lines);

	// Journal and task lists. The member ships a single row template in a
	// standalone chunk; the scene fills in the rows from the event flags.

	struct ListRow {
		Common::String text;
		bool checked = false;
	};

	// Takes a copy of the row template. Without one the anchor's font is used.
	void setRowTemplate(const NDUIControl &tmpl);

	// anchorName is the control whose bounds hold the rows
	void setListRows(const Common::String &anchorName, const Common::Array<ListRow> &rows);

	// Highlights one row. -1 for none.
	void setListSelection(int index);

	// Index of the row under the last click, or -1. Cleared once read.
	int listTakePicked();

	// Scrolls this panel's list by delta, in the list's own units, clamped.
	// All scrolling of the save list, journal/task list and item grid goes here.
	void listScrollBy(int delta);

	// True if this panel holds a runtime list at all.
	bool hasList() const { return !_listAnchor.empty(); }

	// This panel's list position, in the same units as listScrollBy. False when
	// the panel has no scrollable list.
	bool scrollModel(int &position, int &page, int &range);

	// Applies an NDUI command to one of this panel's controls. Returns false if
	// the panel does not own that control, so a caller can try the next one.
	bool applyCommand(const Common::String &target, uint32 commandID);
	bool isControlEnabled(const Common::String &target) const {
		return !_enabled.contains(target) || _enabled.getVal(target);
	}

	// Settings widgets show a value owned by the engine, found through the
	// Engine_* target of their value bindings.

	// Save/restore of runtime show/hide state, stored as a delta against the
	// state the panel was built in.

	// This panel's identity in a save: its root control's name. Control names
	// are only unique within a panel.
	Common::String getPanelName() const;

	// Names whose runtime visibility, or runtime enabled-ness, differs from the
	// built state. Appends; does not clear.
	void getStateDelta(Common::StringArray &shownNames, Common::Array<byte> &shownValues,
		Common::StringArray &enabledNames, Common::Array<byte> &enabledValues) const;

	// Resets to the built state, applies the delta and re-composes. Does not fire
	// OnShow/OnHide bindings, since their results are already in the delta.
	void applyStateDelta(const Common::StringArray &shownNames, const Common::Array<byte> &shownValues,
		const Common::StringArray &enabledNames, const Common::Array<byte> &enabledValues);

	// True if this panel hosts the inventory grid (it owns InvDialog).
	bool isInventoryPanel() const;

	// Re-reads the held-item list and redraws the grid.
	void refreshInventory();

	// Save/load dialog support. The panel supplies the save list, the name edit
	// box and the "Engine_LoadSave" thumbnail.

	// True if this panel owns the named control (public form of findControl).
	bool ownsControl(const Common::String &name);

	// Re-reads the save list and re-composes
	void saveLoadRefresh();

	// The slot currently picked in the list, or -1.
	int saveLoadSelectedSlot() const;

	// The contents of the SaveName edit box.
	const Common::String &saveLoadName() const { return _saveName; }
	void saveLoadSetName(const Common::String &name);

	// Polls the values shown by bound Var::Static widgets and re-composes if one
	// has changed.
	void refreshBoundValues();

	// Re-composes when a list scrolled by one of this panel's bars, which may be
	// in another panel, has moved since the thumb was drawn.
	void updateGraphics() override;

	// Re-composes the panel, keeping its runtime state. Used after a font change.
	void recompose() { redraw(); }

	// Number of widget images blitted by the last init()
	uint getBlitCount() const { return _blitCount; }
	uint getTextCount() const { return _textCount; }

protected:
	bool isViewportRelative() const override { return false; }

private:
	// The dialog's background fill (panelHeader[9]), drawn under its artwork
	void drawPanelFill(const NDUI *chunk, const Common::Point &origin);

	// Blits one control's element artwork, then recurses into its children.
	// origin is the panel's top-left, since control bounds are panel-relative.
	void drawControl(const NDUIControl &control, const Common::Point &origin);

	// Blits an atlas sub-rect into the authored-size surface, modulated by an ARGB
	// colour. False if the atlas or the rect is unusable.
	bool blitAtlasRect(const Common::String &atlasName, const Common::Rect &srcRect,
		const Common::Point &dest, uint32 modulate, const Common::String &controlName,
		const Common::Rect *clipTo = nullptr);

	// A clickable control and the origin its bounds are measured from.
	// findControlHit returns an index into this list.
	struct HitCandidate {
		const NDUIControl *control;
		Common::Point origin;
	};
	void collectControls(const Common::Array<NDUIControl> &children,
		const Common::Point &origin, Common::Array<HitCandidate> &out) const;

	// The Engine_* target a control's value bindings name, or empty
	static Common::String settingTarget(const NDUIControl &control);

	// A slider's current position, in its own authored range.
	int sliderValue(const NDUIControl &control) const;

	// True when a radio button is the current choice, or a check box is ticked
	bool controlIsSet(const NDUIControl &control) const;

	// Track, filled bar and thumb, positioned from the value.
	void drawSlider(const NDUIControl &control, const Common::Point &origin);

	// Up button, track and down button. The thumb is drawn by drawScrollBarThumbs.
	void drawScrollBar(const NDUIControl &control, const Common::Point &origin);

	// Screen rects of a scroll bar's parts and its list position, shared by
	// drawing and hit testing. thumb is empty when the bar is not draggable.
	struct ScrollBarGeom {
		Common::Rect bar;
		Common::Rect up;
		Common::Rect down;
		Common::Rect track;
		Common::Rect thumb;
		int position = 0;
		int page = 0;
		int range = 0;
		int srcHeight = 0;	// Authored crop height for elements[3]
	};
	bool scrollBarGeom(const NDUIControl &control, const Common::Point &origin,
		ScrollBarGeom &out);

	// True if this bar has an event-15 binding, i.e. scrolls a list in another panel
	static bool barIsRouted(const NDUIControl &control);

	// Reads or scrolls the list a bar controls: its event-15 targets if it is
	// routed, otherwise this panel's own list.
	bool barScrollModel(const NDUIControl &control, int &position, int &page, int &range);
	void barScrollBy(const NDUIControl &control, int delta);

	// The thumb of every visible bar. Drawn after the text, since the page size
	// is only known once the rows have been laid out.
	void drawScrollBarThumbs(const Common::Array<NDUIControl> &children,
		const Common::Point &origin);

	// The scroll model a routed bar showed at the last compose, checked by
	// updateGraphics(). resolved is false when the target reported nothing.
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

	// Press, drag and release on a slider. Returns true if it consumed the input.
	bool handleSliderInput(NancyInput &input);

	// Resolves the control's caption id against the UI string table and draws it
	// in the font its descriptor names.
	void drawControlText(const NDUIControl &control, const Common::Point &origin);

	// Draws one string with inline markup at output scale. Returns its height.
	// skipLines and maxLines (0 = no limit) select a range of wrapped lines.
	int drawStyledText(const Common::String &text, const TextStyle &base,
		int x, int y, int wrapWidth, Graphics::TextAlign align,
		uint skipLines = 0, uint maxLines = 0);

	// Positions flow-laid children (authored at 0,0) into _flowPos
	void layOutFlowChildren(const NDUI *chunk);

	// Runs one action binding. Returns true if it changed what is on screen.
	bool runAction(const NDUIAction &action, const NDUIControl &source);

	// Finds a control by instance name anywhere in this panel's tree.
	NDUIControl *findControl(const Common::String &name);

	// Re-composes after a Show/Hide changed a widget's visibility.
	void redraw();

	NDUI *_chunk = nullptr;

	// Runtime visibility, by control name
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _visible;

	// _visible as the panel was built; the baseline for save deltas
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _baselineVisible;

	// Panel-assigned positions for flow-laid children, in panel space.
	Common::HashMap<Common::String, Common::Point, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _flowPos;

	// Conversation state. Empty caption and no responses means not in use.
	Common::String _convoCaption;
	Common::String _narrationCaption;
	Common::StringArray _convoResponses;
	Common::Array<Common::Rect> _convoResponseRects;	// Surface space, output scale
	int _convoPicked = -1;
	int _convoHovered = -1;
	int _convoScroll = 0; // In units of scrollStep
	int _convoPage = 1;
	int _convoRange = 1;

	// Stake-out list rows. Empty means not in use.
	Common::StringArray _stakeoutLines;

	// Journal / task-list rows. _listAnchor empty means not in use.
	Common::String _listAnchor;
	Common::Array<ListRow> _listRows;
	Common::Array<Common::Rect> _listRowRects;	// Surface space, output scale
	Common::Array<uint> _listRowIndices;		// Which row each rect belongs to
	int _listScroll = 0;
	int _listSel = -1;
	int _listPicked = -1;

	// _listScroll counts stops, not rows: a row taller than the box gets one stop
	// per boxful of lines. Computed by drawList.
	int _listStops = 0;

	// Owned copy of the row template
	NDUIControl *_rowTemplate = nullptr;

	// Draws the rows into the anchor's box, one wrapped paragraph each, with the
	// template's tick box in front of them when it has one.
	void drawList();

	// Blits an atlas sub-rect directly into _drawSurface, downscaled. Used for
	// artwork placed after the text has been laid out.
	bool blitAtlasRectToOutput(const Common::String &atlasName, const Common::Rect &srcRect,
		const Common::Point &dest, const Common::String &controlName);

	// Press on a ScrollBar. Routed bars scroll through the scene; others scroll
	// this panel's own list.
	bool handleScrollBarInput(NancyInput &input);

	// Authored-size surface for the artwork pass; only valid during init()
	Graphics::ManagedSurface *_artSurface = nullptr;

	// Captions found during the artwork pass, drawn after the downscale
	struct PendingText {
		const NDUIControl *control;
		Common::Point origin;
	};
	Common::Array<PendingText> _pendingText;

	// Top-left of the composed surface in authored space
	Common::Point _surfaceTopLeft;

	// Last value drawn for each bound Var::Static, by control name
	Common::HashMap<Common::String, int16, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _boundValues;

	// Item ids drawn in the grid, in draw order, with their on-panel rects.
	Common::Array<uint16> _invItems;
	Common::Array<Common::Rect> _invViewRects;	// Eye buttons, same indices as _invRects
	Common::Array<Common::Rect> _invRects;	// Surface space, output scale
	// First visible row of the item grid, clamped in drawInventory()
	int _invScroll = 0;

	// The grid's page and range in rows, from the last drawInventory(). Zero
	// until the grid has been drawn.
	int _invPageRows = 0;
	int _invRangeRows = 0;

	// Draws the inventory grid for the panel that owns InvDialog.
	void drawInventory();

	// The save list shown by this panel's ListBox, in slot order
	struct SaveRow {
		int slot;
		Common::String name;
	};
	Common::Array<SaveRow> _saveRows;
	Common::Array<Common::Rect> _saveRowRects;	// Surface space, output scale
	int _saveSel = -1;							// Index into _saveRows, -1 for none
	int _saveScroll = 0;						// First row drawn

	// Rows that fit on the last page of the save list, from the last drawSaveLoad.
	// Used as the page size so the thumb length stays constant.
	int _savePageRows = 0;

	// Makes the next drawSaveLoad scroll the selection into view
	bool _saveScrollToSel = false;
	Common::String _saveName;					// Contents of the SaveName EditBox

	// Draws the save list, the name field and the thumbnail
	void drawSaveLoad();

	// The LoadList or SaveList control of this panel, or null
	NDUIControl *saveListControl();

	// Fires a control's OnShow/OnHide bindings through the scene
	void fireVisibilityBindings(const Common::String &target, bool shown);

	// Set by the scene for panels that must not appear until asked for.
	bool _startsHidden = false;
	bool _wasEverVisible = false;

public:
	// Hides the panel until something shows it
	void setStartsHidden();

private:

	// Draws the caption and responses, and refills _convoResponseRects.
	void drawConversation();
	void drawNarration();

	// Draws the stake-out list, one agent report per row.
	void drawStakeout();

	// Runtime enabled state, set by commands 8/9. Disabled widgets still draw.
	Common::HashMap<Common::String, bool, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _enabled;

	// Slider positions during a drag, by control name. Committed on release.
	Common::HashMap<Common::String, int, Common::IgnoreCase_Hash,
		Common::IgnoreCase_EqualTo> _sliderValues;

	// Index into _chunk->children of the slider a drag is in progress on, or -1.
	int _sliderDrag = -1;

	// The scroll bar a thumb drag is in progress on, or null. A pointer, since
	// these bars are nested in a ListBox. Stays valid across redraw().
	const NDUIControl *_barDrag = nullptr;
	Common::Point _barDragOrigin;	// The origin the press was hit-tested against
	// The drag follows the pointer's delta from where it started
	int _barDragGrabY = 0;			// Pointer y at mouse-down, output pixels
	int _barDragGrabPos = 0;		// Scroll position at mouse-down

	uint _blitCount = 0;
	uint _textCount = 0;
};

} // End of namespace Nancy

#endif // NANCY_NDUIPANEL_H
