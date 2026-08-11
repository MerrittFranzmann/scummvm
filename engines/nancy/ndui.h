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

#ifndef NANCY_NDUI_H
#define NANCY_NDUI_H

#include "engines/nancy/enginedata.h"

namespace Nancy {

// NDUI is the general-purpose UI description format introduced in Nancy16, which
// replaced the earlier hardcoded per-widget chunks (TASK, TBOX, UIIV, UICO, ...).
// Every popup/screen is described by one or more NDUI chunks inside a script member
// of a player-character tree (PUI_Default.dat / PUI_Nancy.dat). The widget toolkit
// that consumed them (Ndui.dll) was layered on DXUT, so the control set and the
// four per-state colors below match DXUT's.
//
// This file only provides the reader and the in-memory tree; there is no widget
// runtime yet, so nothing else in the engine consumes it.

// The leading byte of a chunk selects the top-level object, and with it the
// header layout and whether a trailer is present.
enum NDUIFileType {
	kNDUIFileTypePanel			= 0x00,	// 1 + 33 + 42 byte header, no trailer
	kNDUIFileTypeScrollPanel	= 0x0b,	// same header, plus a 37-byte trailer
	kNDUIFileTypeControl		= 0x28	// 1 + 33 byte header, a single standalone control
};

// Widget class ids, as read by the factory inside Ndui.dll. Ids 4, 11 and 13 are
// unused by Nancy18. Note that Panel and ScrollPanel have no class id of their own;
// they are selected by the file type byte instead.
enum NDUIClassID {
	kNDUIClassButton		= 1,
	kNDUIClassStatic		= 2,	// Doubles as both label and image
	kNDUIClassSlider		= 3,
	kNDUIClassRadioGroup	= 5,
	kNDUIClassRadioButton	= 6,
	kNDUIClassEditBox		= 7,
	kNDUIClassListBox		= 8,
	kNDUIClassScrollBar		= 9,
	kNDUIClassCheckBox		= 10,
	kNDUIClassInvButton		= 12,
	kNDUIClassVarStatic		= 14,	// Static plus two extra values
	kNDUIClassTooltip		= 15	// Never reaches the factory; embedded by Panel::Load
};

// Every color group has four slots. DXUT itself has six; NDUI keeps only these.
enum NDUIColorSlot {
	kNDUIColorNormal	= 0,
	kNDUIColorDisabled	= 1,
	kNDUIColorMouseOver	= 2,	// Possibly FOCUS; the two can't be told apart in Nancy18's data
	kNDUIColorPressed	= 3,
	kNDUINumColors		= 4
};

enum NDUITextAlign {
	kNDUIAlignLeft		= 0,
	kNDUIAlignRight		= 1,
	kNDUIAlignCenter	= 2
};

// panelHeader[1]. Children authored at 0,0 are positioned by the panel, running
// from the near or the far edge of its box depending on this. Twelve of the
// forty panels in Nancy18 set it, and exactly one - LOWERMATTE[4]
// `LowerMatteRight`, the quit/save/load/options group - sets kNDUILayoutReverse.
enum NDUILayoutMode {
	kNDUILayoutNone		= 0,
	kNDUILayoutForward	= 1,	// From the padding-left edge, rightwards
	kNDUILayoutReverse	= 2		// From the padding-right edge, leftwards
};

// One entry of a control's behaviour graph: when `eventID` fires, send `commandID`
// to the widget (or "Engine_*" pseudo-object) named by `target`.
// The command and event vocabulary, recovered by correlating each id against the
// widget classes that emit it and the targets that receive it. Only the ids that
// are actually acted on are named here; see FORMATS.md for the full table.
enum NDUICommand {
	kNDUICommandShow = 1,
	kNDUICommandHide = 2,
	kNDUICommandSet = 8,	// against Engine_Flags; enable, against a widget
	kNDUICommandClear = 9,	// against Engine_Flags; disable, against a widget
	kNDUICommandInvoke = 12,
	kNDUICommandNotify = 13,
	kNDUICommandSetValue = 30
};

enum NDUIEvent {
	kNDUIEventClick = -1,
	kNDUIEventScroll = 15,
	kNDUIEventOnShow = 16,
	kNDUIEventOnHide = 17,
	kNDUIEventValueChanged = 18
};

struct NDUIAction {
	static const uint kSize = 85;
	static const int16 kNoParamID = -4;	// 0xFFFC

	// Known event ids
	enum { kEventDefault = -1, kEventOnShow = 16, kEventOnHide = 17 };
	// Known command ids
	enum { kCommandShow = 1, kCommandHide = 2, kCommandSetFlag = 8, kCommandClearFlag = 9, kCommandSetValue = 30 };

	int32 eventID = 0;
	uint32 commandID = 0;
	Common::String target;
	byte hasParam = 0;
	double param = 0.0;
	// When not kNoParamID, this is an event flag id (resolvable against the EVNT
	// table in FLAGS) and overrides `param`.
	int16 paramID = kNoParamID;
	Common::String paramString;
};

// The 125-byte element descriptor. Every control carries at least one; ListBox
// carries two and ScrollBar four (track / up / down / thumb, in that order).
// It bundles a texture-atlas reference with a font reference.
struct NDUIElement {
	static const uint kSize = 125;

	// Element block. `sourceName` is a PNG member; `sourceRect` is its sub-rect
	// inside that atlas, and is empty exactly when `sourceName` is empty.
	Common::String sourceName;
	Common::Rect sourceRect;
	uint32 textureColors[kNDUINumColors] = {};	// ARGB, big-endian on disk

	// Font block. `fontName` resolves in the FONT member of ciftree.dat, and is
	// empty exactly when the font colors are all zero.
	Common::String fontName;
	// Two published readings of this byte are confounded across the same 18
	// records in nancy18, so both are exposed rather than guessing: the
	// disassembly says it appends "_Bold" before the font lookup, while the data
	// says it marks the templates that get populated at runtime. Derive the
	// template set independently (from the ScrollPanel families) and honour the
	// bold flag; picking one would be invisible until text rendered wrong.
	byte boldOrRuntimeTemplate = 0;
	uint32 hAlign = 0;		// NDUITextAlign
	uint32 unknown103 = 0;
	byte wordWrap = 0;
	uint32 fontColors[kNDUINumColors] = {};		// ARGB, big-endian on disk
	byte unknown124 = 0;
};

// A single record. Everything up to and including `actions` is read by
// Control::LoadCommon and is common to every class; the fields below that are
// filled in depending on `classID`.
struct NDUIControl {
	// Authored initial state. Confirmed from Ndui.dll: Control::LoadCommon
	// branches on exact equality, so this is an enum and must never be bit-tested.
	// kNDUIStateDisabled does not occur in nancy18, but the engine implements it.
	enum State {
		kNDUIStateNormal = 0,	// CDXUTControl defaults: visible and enabled
		kNDUIStateDisabled = 1,	// m_bEnabled = false; still drawn, greyed
		kNDUIStateHidden = 2	// m_bVisible = false; still enabled
	};

	uint32 classID = 0;		// NDUIClassID
	Common::String version;	// "1.0.0.0", "7.2.5.0", "7.3.0.0", "7.4.8.0"
	uint32 state = kNDUIStateNormal;

	bool isInitiallyVisible() const { return state != kNDUIStateHidden; }
	bool isInitiallyEnabled() const { return state != kNDUIStateDisabled; }
	Common::String name;	// Instance name; action records refer to controls by it
	// Bounds are relative to the enclosing Panel, even for controls owned by
	// another control, and are already exclusive on the right/bottom edge.
	Common::Rect bounds;
	Common::String captionTextID;	// Key into the CVTX chunk of the UI_TEXT member
	Common::String tooltipTextID;

	Common::Array<NDUIAction> actions;
	Common::Array<NDUIElement> elements;
	// Sub-controls this control owns: RadioGroup -> RadioButtons, ListBox -> ScrollBar.
	Common::Array<NDUIControl> children;

	// Button (1) and InvButton (12)
	Common::String buttonImageName;
	Common::Rect buttonImageRect;
	uint32 buttonUnknown[2] = {};	// Zero in every Nancy18 instance

	// Slider (3)
	Common::String sliderThumbImageName;
	Common::Rect sliderThumbRect;
	Common::String sliderBarImageName;
	Common::Rect sliderBarRect;
	uint32 sliderMin = 0;
	uint32 sliderMax = 0;
	uint32 sliderValue = 0;

	// EditBox (7)
	uint32 editBoxColors[3] = {};	// ARGB, big-endian on disk
	uint32 editBoxUnknown = 0;

	// ListBox (8)
	uint32 listBoxUnknown[2] = {};
	int16 listBoxItemCount = 0;
	Common::String listBoxUnknownName;

	// ScrollBar (9)
	uint32 scrollBarUnknown = 0;

	// CheckBox (10) and RadioButton (6). Both write the same name + rect pair, and
	// it is the *set* mark rather than the frame: the frame is painted into the
	// dialog's backdrop art, and element[0] is only the hit box (its NORMAL
	// texture colour is 0x00ffffff - fully transparent - on every one of them).
	Common::String checkedImageName;
	Common::Rect checkedImageRect;
	byte checked = 0;	// CheckBox only; RadioButton has no authored initial state

	// RadioButton (6): the owning group, by name. Empty in every nancy18
	// instance - membership is structural, via the RadioGroup's child list.
	Common::String radioGroupName;

	// RadioGroup (5): the child that starts out selected. Empty in all three of
	// nancy18's groups, because the options screen's selection is the setting.
	Common::String radioSelectedName;

	// Var::Static (14)
	uint32 varStaticUnknown[2] = {};

	// Tooltip (15)
	double tooltipHoverDelay = 0.0;		// Seconds
	double tooltipDisplayTime = 0.0;	// Seconds
	uint16 tooltipPreventEventID = 0;	// EV_PreventTooltips in every instance
};

// One NDUI chunk: either a Panel/ScrollPanel with its embedded Static and Tooltip
// and a counted list of children, or a single standalone control.
struct NDUI : public EngineData {
	NDUI(Common::SeekableReadStream *chunkStream);

	static const uint kPanelHeaderSize = 42;
	static const uint kScrollPanelTrailerSize = 37;

	byte fileType = kNDUIFileTypeControl;
	Common::String version;

	// True for Panel and ScrollPanel chunks
	bool hasPanel = false;

	// The 42-byte block that follows the version string. Decoded, except for
	// panelHeader[2], which is 3 in every Nancy18 chunk and is never read on the
	// layout path.
	//   [0] state       same enum as NDUIControl::state, applied to the dialog
	//   [1] layoutMode  0 none, 1 flow forward, 2 flow reverse
	//   [2] constant 3  unread
	//   [3] gapX  [4] gapY
	//   [5..8] padding rect
	//   [9] backgroundFill, uint32 big-endian ARGB
	uint32 panelHeader[10] = {};

	// [0] savable: gates save/restore of the children's show/hide deltas. Nothing
	//     reads it during Load, so a first-cut port can ignore it.
	// [1] modal: swallow clicks that land on the dialog background.
	byte panelHeaderFlags[2] = {};

	// Written by Panel::Load ahead of the child list. The Static's bounds are the
	// panel's absolute rect, which is what every child rect is relative to.
	//
	// NOTE: that rect is in the game's own 800x600 UI space, not ScummVM's
	// 640x480 screen - panels reach x=800 and y=607. Scale by 0.8, or render the
	// UI at 800x600. RenderObject already scales when _drawSurface and
	// _screenPosition differ in size.
	NDUIControl panelStatic;
	NDUIControl panelTooltip;

	// Direct children of the panel. For a kNDUIFileTypeControl chunk this holds
	// the single standalone control instead.
	Common::Array<NDUIControl> children;

	// ScrollPanel trailer: a by-name reference to a ScrollBar defined in another
	// chunk of the same member, plus its scroll step.
	//
	// scrollStep is in AUTHORED PIXELS - the same 800x600 unit as the panel box,
	// the bar's page size and the content extent. Read from Ndui.dll, where the
	// field is written once and read in exactly three places, all of them in
	// ScrollPanel and all of them pixel arithmetic:
	//
	//   Load        0x1001cf71  reads the name, then this uint32, into +0x40f/+0x413
	//   ctor        0x1001d1c1  defaults it to 1, i.e. "no quantisation"
	//   GetValue(0) 0x1001b9f2  rounds the content extent V up until
	//                           (V - panelHeight) % step == 0, so the *last* scroll
	//                           position lands on a step boundary. The modulus is
	//                           taken against the panel's own height (+0x3de top
	//                           minus +0x3e6 bottom), which is what pins the unit.
	//   GetValue(15) 0x1001b9cf *v *= step - pixels per wheel notch (event 15).
	//   TakeAction(cmd 13) 0x1001c9d2
	//                           panelScrollOffset = ((int)pos + step/2) / step * step,
	//                           i.e. the bar's raw position snapped to the nearest
	//                           multiple. The bar itself is NOT re-snapped.
	//
	// Authored values: 39 for INVENTORY, 6 for the other seven ScrollPanels.
	// 39 is exactly the inventory grid pitch (InvButton 38 + gapY 1), which is why
	// drawInventory's `cell` is also 39; 6 is not a row pitch anywhere - CELLPHONE
	// stacks 22px entries on gapY 4, a pitch of 26 - so this is a pixel grain, not
	// a row or a stop count.
	//
	// DELIBERATELY UNREAD. Every scroll model this engine has counts something
	// else - the save list counts rows, the journal and task list count stops
	// measured in 640x480 output pixels, the inventory counts grid rows - so there
	// is no quantity here to apply 6 or 39 authored pixels to. Converted into those
	// units the authored step is 1 in all eight panels (39/39 for the grid, and
	// under one stop for every text list), which is exactly what the arrow clicks
	// already hardcode. Wiring it would change nothing correct and could only
	// introduce a unit error. See work/scrollstep/ for the derivation.
	Common::String scrollBarName;
	uint32 scrollStep = 1;

	// True when the whole chunk was consumed with nothing left over
	bool valid = false;
	uint32 leftoverBytes = 0;
	uint numRecords = 0;
};

const char *getNDUIClassName(uint32 classID);

// Debug helper. Parses every NDUI chunk in the loaded player-character trees and
// appends a human-readable report to `out`. When `memberFilter` is non-empty only
// that member is dumped, and when `verbose` is false only per-chunk summary lines
// are emitted. Returns the number of chunks consumed exactly, and sets `outTotal`
// to the number of chunks found.
// Loads one NDUI chunk by member name, reading from the owning player-UI tree
// rather than the global search order - several members share a name with
// unrelated members of ciftree.dat. Caller owns the result.
NDUI *loadNDUIChunk(const Common::String &memberName, uint chunkIndex);

uint dumpAllNDUI(Common::Array<Common::String> &out, const Common::String &memberFilter, bool verbose, uint &outTotal);

} // End of namespace Nancy

#endif // NANCY_NDUI_H
