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

#include "engines/nancy/cursor.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/input.h"
#include "engines/nancy/nancy.h"
#include "engines/nancy/puzzledata.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/util.h"

#include "engines/nancy/state/scene.h"
#include "engines/nancy/action/puzzle/draggableimagepuzzle.h"

namespace Nancy {
namespace Action {

void DraggableImagePuzzle::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);
	readRect(stream, _initialWindow);
	readRect(stream, _dest);
	readFilename(stream, _puzzleName);

	// Always 1 and always 4 in both records, so nothing here can tell them
	// apart and neither is applied. The int32 is most likely the drag step in
	// pixels; a step of 4 would make the image jump visibly, and the drag reads
	// smooth against the eyepiece, so it is left alone.
	stream.skip(2);
	stream.skip(4);

	const uint16 numZones = stream.readUint16LE();
	_zones.resize(numZones);
	for (uint i = 0; i < numZones; ++i) {
		Zone &z = _zones[i];
		readRect(stream, z.hotspot);
		z.cursorType = stream.readUint16LE();
		z.scene.sceneID = stream.readUint16LE();
		if (z.scene.sceneID == kNancy16NoScene) {
			z.scene.sceneID = kNoScene;
		}

		z.flag.label = stream.readSint16LE();
		z.flag.flag = stream.readByte();
	}

	_window = _initialWindow;
	_screenPosition = _dest;
}

Common::String DraggableImagePuzzle::getRecordExtraInfo() const {
	Common::String ret = Common::String::format("Draggable image \"%s\" (\"%s\"), window (%d,%d,%d,%d), dest (%d,%d,%d,%d)\n",
		_imageName.toString().c_str(), _puzzleName.c_str(),
		_initialWindow.left, _initialWindow.top, _initialWindow.right, _initialWindow.bottom,
		_dest.left, _dest.top, _dest.right, _dest.bottom);

	for (uint i = 0; i < _zones.size(); ++i) {
		ret += Common::String::format("    zone %u (%d,%d,%d,%d), cursor %u, scene %u, flag %d = %u\n",
			i, _zones[i].hotspot.left, _zones[i].hotspot.top, _zones[i].hotspot.right, _zones[i].hotspot.bottom,
			_zones[i].cursorType, _zones[i].scene.sceneID, _zones[i].flag.label, _zones[i].flag.flag);
	}

	return ret;
}

void DraggableImagePuzzle::init() {
	g_nancy->_resource->loadImage(_imageName, _image);

	_drawSurface.create(_dest.width(), _dest.height(), g_nancy->_graphics->getInputPixelFormat());
	_screenPosition = _dest;

	loadOffset();
	redraw();
}

void DraggableImagePuzzle::loadOffset() {
	CollectionData *data = (CollectionData *)NancySceneState.getPuzzleData(CollectionData::getTag());
	if (!data || !data->collections.contains(_puzzleName)) {
		return;
	}

	const CollectionData::Collection &c = data->collections[_puzzleName];
	if (c.isCharacter || c.numbers.size() < 2) {
		return;
	}

	pan((int)c.numbers[0] - _window.left, (int)c.numbers[1] - _window.top);
}

void DraggableImagePuzzle::storeOffset() {
	CollectionData *data = (CollectionData *)NancySceneState.getPuzzleData(CollectionData::getTag());
	if (!data) {
		return;
	}

	CollectionData::Collection &c = data->collections.getOrCreateVal(_puzzleName);
	c.isCharacter = false;
	c.maxEntries = 2;
	c.numbers.resize(2);
	c.numbers[0] = _window.left;
	c.numbers[1] = _window.top;
}

bool DraggableImagePuzzle::pan(int dx, int dy) {
	const int maxLeft = MAX<int>(0, _image.w - _window.width());
	const int maxTop = MAX<int>(0, _image.h - _window.height());

	int newLeft = CLIP<int>(_window.left + dx, 0, maxLeft);
	int newTop = CLIP<int>(_window.top + dy, 0, maxTop);

	if (newLeft == _window.left && newTop == _window.top) {
		return false;
	}

	_window.moveTo(newLeft, newTop);
	return true;
}

void DraggableImagePuzzle::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	_drawSurface.blitFrom(_image, _window, Common::Point(0, 0));
	setNeedsRedraw(true);
}

int DraggableImagePuzzle::zoneAtCursor(const Common::Point &mousePos) const {
	for (uint i = 0; i < _zones.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_zones[i].hotspot).contains(mousePos)) {
			return (int)i;
		}
	}

	return -1;
}

void DraggableImagePuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		NancySceneState.setNoHeldItem();
		_state = kRun;
		// fall through
	case kRun:
		if (_zoneRequested) {
			_state = kActionTrigger;
		}

		break;
	case kActionTrigger:
		// The zone's flag and its scene change are raised on separate frames,
		// which matters here. ActionManager::processActionRecords abandons the
		// rest of the pass the moment a record calls changeScene, and the record
		// that reacts to this flag sits *ahead* of us in the scene: S3263's
		// "Add Inventory Item" is gated on flag 1015 and puts the microdot slide
		// back in the inventory as Nancy takes it off the stage. Raising the flag
		// and leaving in one pass would drop the slide out of the game for good.
		if (!_flagRaised) {
			storeOffset();
			NancySceneState.setEventFlag(_pendingFlag);
			_flagRaised = true;
			break;
		}

		NancySceneState.changeScene(_pendingScene);
		finishExecution();
		break;
	}
}

void DraggableImagePuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _zoneRequested) {
		return;
	}

	// A drag already under way keeps the image even when the pointer wanders
	// over an action zone, so this is tested before the zones are.
	if (_dragging) {
		if (input.input & NancyInput::kLeftMouseButtonHeld) {
			// The image follows the pointer, the way the slide follows the hand
			// on a real microscope stage, so the window moves the other way.
			if (pan(_lastDragPos.x - input.mousePos.x, _lastDragPos.y - input.mousePos.y)) {
				redraw();
			}

			_lastDragPos = input.mousePos;
			g_nancy->_cursor->setCursorType(CursorManager::kDragHand);
			return;
		}

		_dragging = false;
		storeOffset();
	}

	const int zone = zoneAtCursor(input.mousePos);
	if (zone != -1) {
		g_nancy->_cursor->setCursorType(_zones[zone].cursorType ?
			(CursorManager::CursorType)_zones[zone].cursorType : g_nancy->_cursor->_puzzleExitCursor, true);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			_pendingScene = _zones[zone].scene;
			_pendingFlag = _zones[zone].flag;
			_zoneRequested = true;
			input.eatMouseInput();
		}

		return;
	}

	if (!NancySceneState.getViewport().convertViewportToScreen(_dest).contains(input.mousePos)) {
		return;
	}

	g_nancy->_cursor->setCursorType(CursorManager::kDragHand);

	if (input.input & NancyInput::kLeftMouseButtonDown) {
		_dragging = true;
		_lastDragPos = input.mousePos;
		input.eatMouseInput();
	}
}

} // End of namespace Action
} // End of namespace Nancy
