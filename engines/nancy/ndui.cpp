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

#include "engines/nancy/ndui.h"
#include "engines/nancy/nduipanel.h"
#include "engines/nancy/nancy.h"
#include "engines/nancy/iff.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/util.h"

#include "common/algorithm.h"
#include "common/config-manager.h"

namespace Nancy {

// Bytes each class writes after its element descriptor(s), for reference. Every
// one of these is now read field by field below.
//
//   Button / InvButton (57)  name[33] + RECT + 2 x uint32
//   Slider (110)             2 x (name[33] + RECT) + 3 x uint32
//   RadioGroup (35)          name[33] + the int16 child count
//   RadioButton (82)         name[33] + RECT + name[33]
//   EditBox (16)             3 x ARGB + uint32
//   CheckBox (50)            name[33] + RECT + byte
//   Var::Static (8)          2 x uint32
//   Tooltip (18)             2 x double + uint16
//   ScrollBar (4)            uint32, after four descriptors instead of one
//   ListBox (43)             2 x uint32 + int16 + name[33], after the owned ScrollBar

static const uint kNumScrollBarElements		= 4;	// track / up / down / thumb
static const uint kNumListBoxElements		= 2;

// The largest sensible number of direct children a container declares. Nancy18's
// highest is 12; this only exists to keep a corrupt count from allocating wildly.
static const int16 kMaxChildren = 256;

// Nancy18 never goes deeper than one level of owned sub-controls
static const uint kMaxNDUIDepth = 4;

const char *getNDUIClassName(uint32 classID) {
	switch (classID) {
	case kNDUIClassButton:		return "Button";
	case kNDUIClassStatic:		return "Static";
	case kNDUIClassSlider:		return "Slider";
	case kNDUIClassRadioGroup:	return "RadioGroup";
	case kNDUIClassRadioButton:	return "RadioButton";
	case kNDUIClassEditBox:		return "EditBox";
	case kNDUIClassListBox:		return "ListBox";
	case kNDUIClassScrollBar:	return "ScrollBar";
	case kNDUIClassCheckBox:	return "CheckBox";
	case kNDUIClassInvButton:	return "InvButton";
	case kNDUIClassVarStatic:	return "Var::Static";
	case kNDUIClassTooltip:		return "Tooltip";
	default:					return "<unknown>";
	}
}

// NDUI rects are stored as four 32-bit values that are already exclusive on the
// right and bottom edge, so readRect()'s correction for post-TVD games must not
// be applied to them.
static void readNDUIRect(Common::SeekableReadStream &stream, Common::Rect &rect) {
	rect.left = stream.readSint32LE();
	rect.top = stream.readSint32LE();
	rect.right = stream.readSint32LE();
	rect.bottom = stream.readSint32LE();
}

static void readNDUIColors(Common::SeekableReadStream &stream, uint32 *colors, uint num) {
	for (uint i = 0; i < num; ++i) {
		colors[i] = stream.readUint32BE();
	}
}

static void readNDUIAction(Common::SeekableReadStream &stream, NDUIAction &action) {
	action.eventID = stream.readSint32LE();
	action.commandID = stream.readUint32LE();
	readFilename(stream, action.target);
	action.hasParam = stream.readByte();
	// Note this double is not 8-byte aligned inside the record
	action.param = stream.readDoubleLE();
	action.paramID = stream.readSint16LE();
	readFilename(stream, action.paramString);

	// Debug affordance: every event flag the UI can raise, dumped as it is
	// parsed. This is the one choke point every NDUI action in the game passes
	// through, so it is complete by construction - iterating the panels that
	// happen to be loaded would not be.
	//
	// It exists because 35 event flags are depended on by action records and
	// set by no action record anywhere. At least some of them are raised by
	// widgets rather than records: the splash screen's skip button carries
	// set(8, Engine_Flags, 2702), and 2702 is one of the 35.
	if (ConfMan.hasKey("nancy_dump_ndui_flags") &&
			(action.commandID == NDUIAction::kCommandSetFlag ||
			 action.commandID == NDUIAction::kCommandClearFlag) &&
			action.target.equalsIgnoreCase("Engine_Flags") &&
			action.paramID != NDUIAction::kNoParamID) {
		warning("NDUIFLAG %s %d", action.commandID == NDUIAction::kCommandSetFlag ?
			"set" : "clear", (int)action.paramID);
	}
}

static void readNDUIElement(Common::SeekableReadStream &stream, NDUIElement &element) {
	readFilename(stream, element.sourceName);
	readNDUIRect(stream, element.sourceRect);
	readNDUIColors(stream, element.textureColors, kNDUINumColors);

	readFilename(stream, element.fontName);
	element.boldOrRuntimeTemplate = stream.readByte();
	element.hAlign = stream.readUint32LE();
	element.unknown103 = stream.readUint32LE();
	element.wordWrap = stream.readByte();
	readNDUIColors(stream, element.fontColors, kNDUINumColors);
	element.unknown124 = stream.readByte();
}

// Reads a complete record, including the class id tag that opens it, and
// recursively reads any sub-controls the class owns. Returns false and warns if
// the record can't be read, in which case the stream position is meaningless.
static bool readNDUIControl(Common::SeekableReadStream &stream, NDUIControl &control, uint depth) {
	if (depth > kMaxNDUIDepth) {
		warning("NDUI: control nesting deeper than %d levels", kMaxNDUIDepth);
		return false;
	}

	const int64 recordStart = stream.pos();

	control.classID = stream.readUint32LE();
	readFilename(stream, control.version);
	control.state = stream.readUint32LE();
	readFilename(stream, control.name);
	readNDUIRect(stream, control.bounds);
	readFilename(stream, control.captionTextID);
	readFilename(stream, control.tooltipTextID);

	// Signed, and the sign matters: a negative count means we've lost sync
	const int16 numActions = stream.readSint16LE();

	if (stream.err() || stream.eos()) {
		warning("NDUI: ran out of data reading the record at %d", (int)recordStart);
		return false;
	}

	if (numActions < 0 || stream.pos() + (int64)numActions * NDUIAction::kSize > stream.size()) {
		warning("NDUI: bad action count %d in record '%s' at %d",
			numActions, control.name.c_str(), (int)recordStart);
		return false;
	}

	control.actions.resize(numActions);
	for (int16 i = 0; i < numActions; ++i) {
		readNDUIAction(stream, control.actions[i]);
	}

	// Every class carries at least one element descriptor
	uint numElements = 1;
	if (control.classID == kNDUIClassScrollBar) {
		numElements = kNumScrollBarElements;
	} else if (control.classID == kNDUIClassListBox) {
		numElements = kNumListBoxElements;
	}

	control.elements.resize(numElements);
	for (uint i = 0; i < numElements; ++i) {
		readNDUIElement(stream, control.elements[i]);
	}

	switch (control.classID) {
	case kNDUIClassStatic:
		// No extras at all; this is what pins the descriptor size at 125
		break;
	case kNDUIClassButton:
		// InvButton is identical in shape and adds nothing of its own
		// fall through
	case kNDUIClassInvButton:
		readFilename(stream, control.buttonImageName);
		readNDUIRect(stream, control.buttonImageRect);
		control.buttonUnknown[0] = stream.readUint32LE();
		control.buttonUnknown[1] = stream.readUint32LE();
		break;
	case kNDUIClassSlider:
		readFilename(stream, control.sliderThumbImageName);
		readNDUIRect(stream, control.sliderThumbRect);
		readFilename(stream, control.sliderBarImageName);
		readNDUIRect(stream, control.sliderBarRect);
		control.sliderMin = stream.readUint32LE();
		control.sliderMax = stream.readUint32LE();
		control.sliderValue = stream.readUint32LE();
		break;
	case kNDUIClassEditBox:
		readNDUIColors(stream, control.editBoxColors, ARRAYSIZE(control.editBoxColors));
		control.editBoxUnknown = stream.readUint32LE();
		break;
	case kNDUIClassCheckBox:
		readFilename(stream, control.checkedImageName);
		readNDUIRect(stream, control.checkedImageRect);
		control.checked = stream.readByte();
		break;
	case kNDUIClassVarStatic:
		control.varStaticUnknown[0] = stream.readUint32LE();
		control.varStaticUnknown[1] = stream.readUint32LE();
		break;
	case kNDUIClassScrollBar:
		control.scrollBarUnknown = stream.readUint32LE();
		break;
	case kNDUIClassRadioButton:
		// The same shape CheckBox writes, plus the owning group's name - which is
		// how DXUT's CDXUTRadioButton is built (it derives from CDXUTCheckBox and
		// adds nButtonGroup). The image is the *selected* mark: element[0] is the
		// hit box, authored with a fully transparent NORMAL texture colour on all
		// eight of nancy18's radio buttons, and this is the sprite that appears
		// once the option is the current one.
		//
		// Every nancy18 instance leaves the group name empty; membership comes
		// from being a child of the RadioGroup record instead.
		readFilename(stream, control.checkedImageName);
		readNDUIRect(stream, control.checkedImageRect);
		readFilename(stream, control.radioGroupName);
		break;
	case kNDUIClassTooltip:
		// The int16 that follows a Tooltip is the enclosing Panel's child count,
		// not part of the Tooltip, which keeps every leaf class countless
		control.tooltipHoverDelay = stream.readDoubleLE();
		control.tooltipDisplayTime = stream.readDoubleLE();
		control.tooltipPreventEventID = stream.readUint16LE();
		break;
	case kNDUIClassRadioGroup: {
		// The name of the button that starts out selected. Empty in all three of
		// nancy18's groups: the options screen's selection is the setting, so it
		// is restored from the saved configuration rather than authored here.
		readFilename(stream, control.radioSelectedName);
		const int16 numChildren = stream.readSint16LE();
		if (numChildren < 0 || numChildren > kMaxChildren) {
			warning("NDUI: bad RadioGroup child count %d in '%s'", numChildren, control.name.c_str());
			return false;
		}

		control.children.resize(numChildren);
		for (int16 i = 0; i < numChildren; ++i) {
			if (!readNDUIControl(stream, control.children[i], depth + 1)) {
				return false;
			}
		}

		break;
	}
	case kNDUIClassListBox:
		// A ListBox has no child count; it unconditionally owns one ScrollBar,
		// and writes its own trailer after it
		control.children.resize(1);
		if (!readNDUIControl(stream, control.children[0], depth + 1)) {
			return false;
		}

		if (control.children[0].classID != kNDUIClassScrollBar) {
			warning("NDUI: ListBox '%s' owns a %s instead of a ScrollBar",
				control.name.c_str(), getNDUIClassName(control.children[0].classID));
			return false;
		}

		control.listBoxUnknown[0] = stream.readUint32LE();
		control.listBoxUnknown[1] = stream.readUint32LE();
		control.listBoxItemCount = stream.readSint16LE();
		readFilename(stream, control.listBoxUnknownName);
		break;
	default:
		warning("NDUI: unknown class id %u in the record at %d", control.classID, (int)recordStart);
		return false;
	}

	if (stream.err() || (stream.pos() > stream.size())) {
		warning("NDUI: overran the chunk reading the %s record '%s' at %d",
			getNDUIClassName(control.classID), control.name.c_str(), (int)recordStart);
		return false;
	}

	return true;
}

static uint countNDUIRecords(const Common::Array<NDUIControl> &controls) {
	uint total = 0;
	for (const NDUIControl &control : controls) {
		total += 1 + countNDUIRecords(control.children);
	}

	return total;
}

// Reads a whole chunk into `ndui`. Split out of the constructor so that the number
// of bytes left unconsumed can be reported even when the walk gives up early.
static bool readNDUIChunk(Common::SeekableReadStream &stream, NDUI &ndui) {
	ndui.fileType = stream.readByte();
	readFilename(stream, ndui.version);

	switch (ndui.fileType) {
	case kNDUIFileTypePanel:
	case kNDUIFileTypeScrollPanel:
		ndui.hasPanel = true;
		break;
	case kNDUIFileTypeControl:
		break;
	default:
		warning("NDUI: unknown file type 0x%02x", ndui.fileType);
		return false;
	}

	if (ndui.hasPanel) {
		for (uint i = 0; i < ARRAYSIZE(ndui.panelHeader); ++i) {
			ndui.panelHeader[i] = stream.readUint32LE();
		}

		ndui.panelHeaderFlags[0] = stream.readByte();
		ndui.panelHeaderFlags[1] = stream.readByte();

		// Panel::Load writes an embedded Static whose bounds are the panel's
		// absolute screen rect, then an embedded Tooltip, then the child count
		if (!readNDUIControl(stream, ndui.panelStatic, 0)) {
			return false;
		}

		if (ndui.panelStatic.classID != kNDUIClassStatic) {
			warning("NDUI: panel's embedded record is a %s instead of a Static",
				getNDUIClassName(ndui.panelStatic.classID));
			return false;
		}

		if (!readNDUIControl(stream, ndui.panelTooltip, 0)) {
			return false;
		}

		if (ndui.panelTooltip.classID != kNDUIClassTooltip) {
			warning("NDUI: panel's second embedded record is a %s instead of a Tooltip",
				getNDUIClassName(ndui.panelTooltip.classID));
			return false;
		}

		const int16 numChildren = stream.readSint16LE();
		if (numChildren < 0 || numChildren > kMaxChildren) {
			warning("NDUI: bad panel child count %d", numChildren);
			return false;
		}

		ndui.children.resize(numChildren);
		for (int16 i = 0; i < numChildren; ++i) {
			if (!readNDUIControl(stream, ndui.children[i], 0)) {
				return false;
			}
		}

		if (ndui.fileType == kNDUIFileTypeScrollPanel) {
			readFilename(stream, ndui.scrollBarName);
			ndui.scrollStep = stream.readUint32LE();
		}
	} else {
		ndui.children.resize(1);
		if (!readNDUIControl(stream, ndui.children[0], 0)) {
			return false;
		}
	}

	if (stream.err() || stream.pos() > stream.size()) {
		warning("NDUI: overran the chunk");
		return false;
	}

	return true;
}

NDUI::NDUI(Common::SeekableReadStream *chunkStream) : EngineData(chunkStream) {
	const bool readOK = readNDUIChunk(*chunkStream, *this);

	int64 consumed = chunkStream->pos();
	if (consumed > chunkStream->size()) {
		consumed = chunkStream->size();
	}

	numRecords = countNDUIRecords(children) + (hasPanel ? 2 : 0);
	leftoverBytes = (uint32)(chunkStream->size() - consumed);
	valid = readOK && (leftoverBytes == 0);
}

// Debug dump

static Common::String describeNDUIRect(const Common::Rect &rect) {
	return Common::String::format("(%d,%d)-(%d,%d)", rect.left, rect.top, rect.right, rect.bottom);
}

static void dumpNDUIControl(Common::Array<Common::String> &out, const NDUIControl &control, uint indent) {
	Common::String line;
	for (uint i = 0; i < indent; ++i) {
		line += "  ";
	}

	line += Common::String::format("%-11s %-28s %-8s state=%u %-22s",
		getNDUIClassName(control.classID),
		control.name.empty() ? "<unnamed>" : control.name.c_str(),
		control.version.c_str(),
		control.state,
		describeNDUIRect(control.bounds).c_str());

	if (!control.elements.empty() && !control.elements[0].sourceName.empty()) {
		line += Common::String::format(" src=%s%s", control.elements[0].sourceName.c_str(),
			describeNDUIRect(control.elements[0].sourceRect).c_str());
	}

	if (!control.elements.empty() && !control.elements[0].fontName.empty()) {
		line += Common::String::format(" font=%s", control.elements[0].fontName.c_str());
	}

	if (!control.captionTextID.empty()) {
		line += Common::String::format(" caption=%s", control.captionTextID.c_str());
	}

	if (!control.tooltipTextID.empty()) {
		line += Common::String::format(" tooltip=%s", control.tooltipTextID.c_str());
	}

	// Class-specific artwork lives outside elements[0], so a dump that only
	// showed the element source made Button/InvButton/CheckBox look artless.
	if (!control.buttonImageName.empty()) {
		line += Common::String::format(" btnimg=%s%s", control.buttonImageName.c_str(),
			describeNDUIRect(control.buttonImageRect).c_str());
	}

	if (control.classID == kNDUIClassVarStatic) {
		line += Common::String::format(" var=[%u,%u]",
			control.varStaticUnknown[0], control.varStaticUnknown[1]);
	}

	// A Slider's range and its thumb artwork are the whole of what makes it a
	// slider rather than a Static, and neither is in elements[0].
	if (control.classID == kNDUIClassSlider) {
		line += Common::String::format(" range=[%u..%u]=%u",
			control.sliderMin, control.sliderMax, control.sliderValue);

		if (!control.sliderThumbImageName.empty()) {
			line += Common::String::format(" thumb=%s%s", control.sliderThumbImageName.c_str(),
				describeNDUIRect(control.sliderThumbRect).c_str());
		}

		if (!control.sliderBarImageName.empty()) {
			line += Common::String::format(" bar=%s%s", control.sliderBarImageName.c_str(),
				describeNDUIRect(control.sliderBarRect).c_str());
		}
	}

	if (control.classID == kNDUIClassCheckBox) {
		line += Common::String::format(" checked=%u", (uint)control.checked);
	}

	// The set-state mark, on both classes that own one. Which option a group is
	// currently on is invisible without it, since element[0] is transparent.
	if ((control.classID == kNDUIClassCheckBox || control.classID == kNDUIClassRadioButton) &&
			!control.checkedImageName.empty()) {
		line += Common::String::format(" checkimg=%s%s", control.checkedImageName.c_str(),
			describeNDUIRect(control.checkedImageRect).c_str());
	}

	if (control.classID == kNDUIClassRadioGroup && !control.radioSelectedName.empty()) {
		line += Common::String::format(" selected=%s", control.radioSelectedName.c_str());
	}

	if (!control.elements.empty()) {
		const NDUIElement &el = control.elements[0];
		line += Common::String::format(" align=%u wrap=%u bold=%u fg=%08x,%08x,%08x,%08x tex=%08x,%08x,%08x,%08x",
			el.hAlign, (uint)el.wordWrap, (uint)el.boldOrRuntimeTemplate,
			el.fontColors[0], el.fontColors[1], el.fontColors[2], el.fontColors[3],
			el.textureColors[0], el.textureColors[1], el.textureColors[2], el.textureColors[3]);
	}

	out.push_back(line);

	for (const NDUIAction &action : control.actions) {
		Common::String actionLine;
		for (uint i = 0; i < indent + 1; ++i) {
			actionLine += "  ";
		}

		actionLine += Common::String::format("action event %d command %d -> %s",
			action.eventID, action.commandID, action.target.c_str());

		if (action.paramID != NDUIAction::kNoParamID) {
			actionLine += Common::String::format(" paramID %d", action.paramID);
		} else if (action.hasParam) {
			actionLine += Common::String::format(" param %f", action.param);
		}

		if (!action.paramString.empty()) {
			actionLine += Common::String::format(" '%s'", action.paramString.c_str());
		}

		out.push_back(actionLine);
	}

	for (const NDUIControl &child : control.children) {
		dumpNDUIControl(out, child, indent + 1);
	}
}

// One script member of one player-character tree
struct NDUIScript {
	Common::String treeName;
	Common::String memberName;

	bool operator<(const NDUIScript &other) const {
		return memberName < other.memberName;
	}
};

// Returns every script member of every loaded player-character tree, sorted by
// member name. Nancy16+ names those trees in the PCUI chunk.
static Common::Array<NDUIScript> listPlayerUIScripts() {
	Common::Array<NDUIScript> scripts;
	const PCUI *pcui = GetEngineData(PCUI);
	if (!pcui) {
		warning("NDUI: no PCUI chunk, so the player-character trees can't be named");
		return scripts;
	}

	for (const PCUI::Character &character : pcui->characters) {
		if (character.imageName.empty()) {
			continue;
		}

		Common::Array<Common::Path> members;
		g_nancy->_resource->list(character.imageName, members, CifInfo::kResTypeScript);

		for (const Common::Path &member : members) {
			NDUIScript script;
			script.treeName = character.imageName;
			script.memberName = member.toString();
			scripts.push_back(script);
		}
	}

	Common::sort(scripts.begin(), scripts.end());
	return scripts;
}

NDUI *loadNDUIChunk(const Common::String &memberName, uint chunkIndex) {
	const Common::Array<NDUIScript> scripts = listPlayerUIScripts();

	for (const NDUIScript &script : scripts) {
		if (!script.memberName.equalsIgnoreCase(memberName)) {
			continue;
		}

		IFF *iff = g_nancy->_resource->loadIFFFromTree(script.treeName, Common::Path(script.memberName));
		if (!iff) {
			return nullptr;
		}

		NDUI *result = nullptr;
		Common::SeekableReadStream *chunkStream = iff->getChunkStream("NDUI", chunkIndex);
		if (chunkStream) {
			result = new NDUI(chunkStream);
			delete chunkStream;
		}

		delete iff;
		return result;
	}

	return nullptr;
}

uint dumpAllNDUI(Common::Array<Common::String> &out, const Common::String &memberFilter, bool verbose, uint &outTotal) {
	uint numExact = 0;
	uint numRecords = 0;
	outTotal = 0;

	if (g_nancy->getGameType() < kGameTypeNancy16) {
		out.push_back("NDUI was introduced in Nancy16; this game has no NDUI chunks.");
		return 0;
	}

	const Common::Array<NDUIScript> scripts = listPlayerUIScripts();

	for (const NDUIScript &script : scripts) {
		const Common::String &member = script.memberName;
		if (!memberFilter.empty() && !member.equalsIgnoreCase(memberFilter)) {
			continue;
		}

		// Load from the owning tree, not through the global search order: several
		// of these members share a name with an unrelated member of ciftree.dat
		IFF *iff = g_nancy->_resource->loadIFFFromTree(script.treeName, Common::Path(member));
		if (!iff) {
			out.push_back(Common::String::format("Failed to load IFF '%s' from '%s'",
				member.c_str(), script.treeName.c_str()));
			continue;
		}

		for (uint index = 0; ; ++index) {
			Common::SeekableReadStream *chunkStream = iff->getChunkStream("NDUI", index);
			if (!chunkStream) {
				break;
			}

			const uint32 chunkSize = (uint32)chunkStream->size();
			NDUI ndui(chunkStream);
			delete chunkStream;

			++outTotal;
			numRecords += ndui.numRecords;
			if (ndui.valid) {
				++numExact;
			}

			out.push_back(Common::String::format("=== %s NDUI[%d]  %d bytes  fileType 0x%02x  %d records  %s",
				member.c_str(), index, chunkSize, ndui.fileType, ndui.numRecords,
				ndui.valid ? "exact" :
					Common::String::format("LEFTOVER %d", ndui.leftoverBytes).c_str()));

			if (ndui.hasPanel) {
				// Compose the panel to prove the artwork actually resolves and
				// lands. Counts blits rather than rendering to screen, since the
				// scene UI it would live in does not exist yet.
				NDUIPanel probe(1);
				probe.init(new NDUI(ndui));  // the panel takes ownership
				out.push_back(Common::String::format("    composed %dx%d, %d blits, %d strings",
					probe.getScreenPosition().width(), probe.getScreenPosition().height(),
					probe.getBlitCount(), probe.getTextCount()));
			}

			if (!verbose) {
				continue;
			}

			if (ndui.fileType == kNDUIFileTypeScrollPanel) {
				out.push_back(Common::String::format("    scrollBar=%s step=%d",
					ndui.scrollBarName.c_str(), ndui.scrollStep));
			}

			if (ndui.hasPanel) {
				dumpNDUIControl(out, ndui.panelStatic, 1);
				dumpNDUIControl(out, ndui.panelTooltip, 1);
			}

			for (const NDUIControl &child : ndui.children) {
				dumpNDUIControl(out, child, 1);
			}
		}

		delete iff;
	}

	out.push_back(Common::String::format("%d/%d NDUI chunks consumed exactly, %d records total",
		numExact, outTotal, numRecords));

	return numExact;
}

} // End of namespace Nancy
