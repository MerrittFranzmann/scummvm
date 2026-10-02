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

// NDUI is the UI description format introduced in Nancy16, replacing the earlier
// per-widget chunks (TASK, TBOX, UIIV, UICO, ...). Each screen is described by NDUI
// chunks in the player-character trees. The original toolkit (Ndui.dll) was built
// on DXUT, so the control set and per-state colors match DXUT's.

// The leading byte of a chunk selects the top-level object and header layout
enum NDUIFileType {
	kNDUIFileTypePanel			= 0x00,	// 1 + 33 + 42 byte header, no trailer
	kNDUIFileTypeScrollPanel	= 0x0b,	// same header, plus a 37-byte trailer
	kNDUIFileTypeControl		= 0x28	// 1 + 33 byte header, a single standalone control
};

// Widget class ids. Panel and ScrollPanel have none; they use the file type byte.
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

// Per-state color slots
enum NDUIColorSlot {
	kNDUIColorNormal	= 0,
	kNDUIColorDisabled	= 1,
	kNDUIColorMouseOver	= 2,	// Possibly focus instead
	kNDUIColorPressed	= 3,
	kNDUINumColors		= 4
};

enum NDUITextAlign {
	kNDUIAlignLeft		= 0,
	kNDUIAlignRight		= 1,
	kNDUIAlignCenter	= 2
};

// panelHeader[1]. Children authored at 0,0 are laid out by the panel, from the
// near or far edge of its box.
enum NDUILayoutMode {
	kNDUILayoutNone		= 0,
	kNDUILayoutForward	= 1,	// From the padding-left edge, rightwards
	kNDUILayoutReverse	= 2		// From the padding-right edge, leftwards
};

// Command and event ids used by NDUIAction. Only the ones acted on are named.
enum NDUICommand {
	kNDUICommandShow = 1,
	kNDUICommandHide = 2,
	kNDUICommandSet = 8,	// Set flag, or enable a widget
	kNDUICommandClear = 9,	// Clear flag, or disable a widget
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

// When eventID fires, send commandID to the widget or "Engine_*" object named by target
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
	// If not kNoParamID, an event flag id that overrides param
	int16 paramID = kNoParamID;
	Common::String paramString;
};

// Texture and font descriptor. Every control has at least one; ListBox has two and
// ScrollBar four (track, up, down, thumb).
struct NDUIElement {
	static const uint kSize = 125;

	// PNG member and the sub-rect inside it
	Common::String sourceName;
	Common::Rect sourceRect;
	uint32 textureColors[kNDUINumColors] = {};	// ARGB, big-endian on disk

	// Alias into the FONT registry
	Common::String fontName;
	// Appends "_Bold" to the font lookup; also set only on runtime-populated templates
	byte boldOrRuntimeTemplate = 0;
	uint32 hAlign = 0;		// NDUITextAlign
	uint32 unknown103 = 0;
	byte wordWrap = 0;
	uint32 fontColors[kNDUINumColors] = {};		// ARGB, big-endian on disk
	byte unknown124 = 0;
};

// A single control record. Fields up to actions are common to every class; the
// rest depend on classID.
struct NDUIControl {
	// Initial state. An enum, not a bitfield.
	enum State {
		kNDUIStateNormal = 0,	// Visible and enabled
		kNDUIStateDisabled = 1,	// Drawn greyed out
		kNDUIStateHidden = 2	// Hidden but still enabled
	};

	uint32 classID = 0;		// NDUIClassID
	Common::String version;	// "1.0.0.0", "7.2.5.0", "7.3.0.0", "7.4.8.0"
	uint32 state = kNDUIStateNormal;

	bool isInitiallyVisible() const { return state != kNDUIStateHidden; }
	bool isInitiallyEnabled() const { return state != kNDUIStateDisabled; }
	Common::String name;	// Instance name; action records refer to controls by it
	// Relative to the enclosing Panel, even for controls owned by another control
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
	uint32 buttonUnknown[2] = {};

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

	// CheckBox (10) and RadioButton (6). The checked mark only; the frame is part
	// of the dialog's background.
	Common::String checkedImageName;
	Common::Rect checkedImageRect;
	byte checked = 0;	// CheckBox only; RadioButton has no authored initial state

	// RadioButton (6): the owning group. Usually empty; membership comes from the
	// RadioGroup's child list.
	Common::String radioGroupName;

	// RadioGroup (5): the child that starts out selected
	Common::String radioSelectedName;

	// Var::Static (14)
	uint32 varStaticUnknown[2] = {};

	// Tooltip (15)
	double tooltipHoverDelay = 0.0;		// Seconds
	double tooltipDisplayTime = 0.0;	// Seconds
	uint16 tooltipPreventEventID = 0;	// Usually EV_PreventTooltips
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

	// The 42-byte block that follows the version string
	//   [0] state       same enum as NDUIControl::state, applied to the dialog
	//   [1] layoutMode  NDUILayoutMode
	//   [2] unknown, always 3
	//   [3] gapX  [4] gapY
	//   [5..8] padding rect
	//   [9] backgroundFill, uint32 big-endian ARGB
	uint32 panelHeader[10] = {};

	// [0] savable: whether the children's show/hide state is saved
	// [1] modal: swallow clicks that land on the dialog background
	byte panelHeaderFlags[2] = {};

	// The Static's bounds are the panel's absolute rect, in the game's 800x600
	// UI space; every child rect is relative to it.
	NDUIControl panelStatic;
	NDUIControl panelTooltip;

	// Direct children of the panel, or the single control of a kNDUIFileTypeControl chunk
	Common::Array<NDUIControl> children;

	// ScrollPanel trailer: the name of a ScrollBar in another chunk of the same
	// member, and the scroll step in 800x600 UI pixels. The scroll offset snaps
	// to multiples of the step (Ndui.dll 0x1001c9d2).
	Common::String scrollBarName;
	uint32 scrollStep = 1;

	// True when the whole chunk was consumed with nothing left over
	bool valid = false;
	uint32 leftoverBytes = 0;
	uint numRecords = 0;
};

const char *getNDUIClassName(uint32 classID);

// Loads one NDUI chunk from the player-UI trees, since some member names also
// exist in ciftree.dat. Caller owns the result.
NDUI *loadNDUIChunk(const Common::String &memberName, uint chunkIndex);

// Debug helper. Appends a report on every NDUI chunk to out. Returns the number
// of chunks parsed exactly and sets outTotal to the number found.
uint dumpAllNDUI(Common::Array<Common::String> &out, const Common::String &memberFilter, bool verbose, uint &outTotal);

} // End of namespace Nancy

#endif // NANCY_NDUI_H
