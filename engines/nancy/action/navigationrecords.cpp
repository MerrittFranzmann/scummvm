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

#include "engines/nancy/nancy.h"
#include "engines/nancy/util.h"

#include "engines/nancy/action/navigationrecords.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

void SceneChange::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() >= kGameTypeNancy16 && _type == 15) {
		// Nancy16's type 15 carries a bare scene id and nothing else - its whole
		// payload is 2 bytes, against 6 for the type 16 descriptor. Reading the
		// full descriptor here over-reads by four and yields a garbage
		// destination, which is how a change to kNoScene got requested.
		_sceneChange.sceneID = stream.readUint16LE();
		_sceneChange.frameID = 0;
		_sceneChange.continueSceneSound = kContinueSceneSound;
		return;
	}

	_sceneChange.readData(stream);
}

void SceneChange::execute() {
	NancySceneState.changeScene(_sceneChange);
	_isDone = true;
}

void HotMultiframeSceneChange::readData(Common::SeekableReadStream &stream) {
	if (_isTerse) {
		_hoverCursor = (CursorManager::CursorType)stream.readUint16LE();
		_sceneChange.sceneID = stream.readUint16LE();
		_sceneChange.frameID = stream.readUint16LE();
		_sceneChange.verticalOffset = 0;
		_sceneChange.continueSceneSound = stream.readUint16LE();
		_sceneChange.listenerFrontVector.set(0, 0, 1);
		_sceneChange.frontVectorFrameID = _sceneChange.frameID;
	} else {
		SceneChange::readData(stream);
	}
	
	uint16 numHotspots = stream.readUint16LE();

	_hotspots.reserve(numHotspots);
	for (uint i = 0; i < numHotspots; ++i) {
		_hotspots.push_back(HotspotDescription());
		HotspotDescription &newDesc = _hotspots[i];
		newDesc.readData(stream);
	}
}

void HotMultiframeSceneChange::execute() {
	switch (_state) {
	case kBegin:
		// turn main rendering on
		_state = kRun;
		// fall through
	case kRun:
		_hasHotspot = false;
		for (uint i = 0; i < _hotspots.size(); ++i) {
			if (_hotspots[i].frameID == NancySceneState.getSceneInfo().frameID) {
				_hasHotspot = true;
				_hotspot = _hotspots[i].coords;
			}
		}
		break;
	case kActionTrigger:
		SceneChange::execute();
		break;
	}
}

// The hover cursor is stored as the id of the matching directional scene-change
// action record type, which gets translated into the actual cursor to display.
static CursorManager::CursorType getNavigationCursor(uint16 id) {
	switch (id) {
	case 14:
		return CursorManager::kExit;
	case 15:
	case 23:
		return CursorManager::kMoveForward;
	case 16:
		return CursorManager::kMoveBackward;
	case 17:
	case 24:
		return CursorManager::kMoveUp;
	case 18:
	case 25:
		return CursorManager::kMoveDown;
	case 19:
		return CursorManager::kMoveLeft;
	case 20:
		return CursorManager::kMoveRight;
	default:
		return CursorManager::kHotspot;
	}
}

void HotSingleFrameSceneChange::readData(Common::SeekableReadStream &stream) {
	_hoverCursor = getNavigationCursor(stream.readUint16LE());
	_sceneChange.sceneID = stream.readUint16LE();
	_sceneChange.continueSceneSound = kContinueSceneSound;
	_sceneChange.listenerFrontVector.set(0, 0, 1);
	readRect(stream, _sceneHotspot.coords);
}

void HotSingleFrameSceneChange::execute() {
	switch (_state) {
	case kBegin:
		_hotspot = _sceneHotspot.coords;
		_state = kRun;
		// fall through
	case kRun:
		_hasHotspot = true;
		break;
	case kActionTrigger:
		SceneChange::execute();
		break;
	}
}

void Hot1FrSceneChange::readData(Common::SeekableReadStream &stream) {
	if (_dynamicCursor)
		_hoverCursor = (CursorManager::CursorType)stream.readUint16LE();

	if (!_isTerse) {
		SceneChange::readData(stream);

		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			// Constant 26 bytes in nancy18, all 476 records: the 6-byte scene
			// core, a 32-bit CURSOR id, then the rect.
			//
			// This was read as a hotspot frame id, which gates _hasHotspot on
			// frameID == the current viewport frame. Every scene carrying one of
			// these records has single-frame art, so any nonzero value could
			// never match - and 438 of the 476 are nonzero, leaving 92% of the
			// game's plain hotspots permanently dead. The value histogram also
			// matches type 91's known uint32 cursor field, not any frame index.
			_hoverCursor = (CursorManager::CursorType)stream.readUint32LE();
			_dynamicCursor = true;
			_hotspotDesc.frameID = 0;
			readRect(stream, _hotspotDesc.coords);
		} else {
			_hotspotDesc.readData(stream);
		}
	} else {
		_sceneChange.sceneID = stream.readUint16LE();
		if (g_nancy->getGameType() >= kGameTypeNancy10 && _dynamicCursor) {
			_sceneChange.frameID = stream.readUint16LE();
			_sceneChange.continueSceneSound = stream.readUint16LE();
		} else {
			_sceneChange.continueSceneSound = kContinueSceneSound;
		}
		_sceneChange.listenerFrontVector.set(0, 0, 1);
		readRect(stream, _hotspotDesc.coords);
	}
}

void Hot1FrSceneChange::execute() {
	switch (_state) {
	case kBegin:
		_hotspot = _hotspotDesc.coords;
		_state = kRun;
		// fall through
	case kRun:
		if (_hotspotDesc.frameID == NancySceneState.getSceneInfo().frameID) {
			_hasHotspot = true;
		} else {
			_hasHotspot = false;
		}
		break;
	case kActionTrigger:
		SceneChange::execute();
		break;
	}
}

void HotMultiframeMultiSceneChange::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() <= kGameTypeNancy2) {
		_onTrue._sceneChange.readData(stream);
		_onFalse._sceneChange.readData(stream);
	} else {
		_onTrue.readData(stream, true);
		_onFalse.readData(stream, true);
	}

	_condType = stream.readByte();
	_conditionID = stream.readUint16LE();
	_conditionPayload = stream.readByte();
	uint numHotspots = stream.readUint16LE();

	_hotspots.resize(numHotspots);

	for (uint i = 0; i < numHotspots; ++i) {
		_hotspots[i].readData(stream);
	}
}

void HotMultiframeMultiSceneChange::execute() {
	switch (_state) {
	case kBegin:
		// set something to 1
		_state = kRun;
		// fall through
	case kRun:
		_hasHotspot = false;

		for (HotspotDescription &desc : _hotspots) {
			if (desc.frameID == NancySceneState.getSceneInfo().frameID) {
				_hotspot = desc.coords;
				_hasHotspot = true;
			}
		}

		break;
	case kActionTrigger: {
		bool conditionMet = false;
		switch (_condType) {
		case kFlagEvent:
			if (NancySceneState.getEventFlag(_conditionID, _conditionPayload)) {
				conditionMet = true;
			}
			break;
		case kFlagInventory:
			if (NancySceneState.hasItem(_conditionID) == _conditionPayload) {
				conditionMet = true;
			}
			break;
		case kFlagCursor:
			if (NancySceneState.getHeldItem() == _conditionPayload) {
				conditionMet = true;
			}
			break;
		}

		if (conditionMet) {
			_onTrue.execute();
		} else {
			_onFalse.execute();
		}
		_isDone = true;

		break;
	}
	}
}

void HotMultiframeMultiSceneCursorTypeSceneChange::readData(Common::SeekableReadStream &stream) {
	uint16 numScenes = stream.readUint16LE();
	_scenes.resize(numScenes);
	_cursorTypes.resize(numScenes);
	for (uint i = 0; i < numScenes; ++i) {
		_cursorTypes[i] = stream.readUint16LE();
		_scenes[i].readData(stream);
	}

	stream.skip(2);
	_defaultScene.readData(stream);

	uint16 numHotspots = stream.readUint16LE();
	_hotspots.resize(numHotspots);
	for (uint i = 0; i < numHotspots; ++i) {
		_hotspots[i].readData(stream);
	}
}

void HotMultiframeMultiSceneCursorTypeSceneChange::execute() {
	switch (_state) {
	case kBegin:
		// turn main rendering on
		_state = kRun;
		// fall through
	case kRun:
		_hasHotspot = false;
		for (uint i = 0; i < _hotspots.size(); ++i) {
			if (_hotspots[i].frameID == NancySceneState.getSceneInfo().frameID) {
				_hasHotspot = true;
				_hotspot = _hotspots[i].coords;
			}
		}
		break;
	case kActionTrigger:
		for (uint i = 0; i < _cursorTypes.size(); ++i) {
			if (NancySceneState.getHeldItem() == _cursorTypes[i]) {
				NancySceneState.changeScene(_scenes[i]);

				_isDone = true;
				return;
			}
		}

		NancySceneState.changeScene(_defaultScene);
		_isDone = true;
		break;
	}
}

void MapCall::readData(Common::SeekableReadStream &stream) {
	stream.skip(1);
}

void MapCall::execute() {
	_execType = kRepeating;
	NancySceneState.requestStateChange(NancyState::kMap);
	finishExecution();
}

void MapCallHot1Fr::readData(Common::SeekableReadStream &stream) {
	_hotspotDesc.readData(stream);
}

void MapCallHot1Fr::execute() {
	switch (_state) {
	case kBegin:
		_hotspot = _hotspotDesc.coords;
		_state = kRun;
		// fall through
	case kRun:
		if (_hotspotDesc.frameID == NancySceneState.getSceneInfo().frameID) {
			_hasHotspot = true;
		}
		break;
	case kActionTrigger:
		MapCall::execute();
		break;
	}
}

void MapCallHotMultiframe::readData(Common::SeekableReadStream &stream) {
	uint16 numDescs = stream.readUint16LE();
	_hotspots.reserve(numDescs);
	for (uint i = 0; i < numDescs; ++i) {
		_hotspots.push_back(HotspotDescription());
		_hotspots[i].readData(stream);
	}
}

void MapCallHotMultiframe::execute() {
	switch (_state) {
	case kBegin:
		_state = kRun;
		// fall through
	case kRun:
		_hasHotspot = false;
		for (uint i = 0; i < _hotspots.size(); ++i) {
			if (_hotspots[i].frameID == NancySceneState.getSceneInfo().frameID) {
				_hasHotspot = true;
				_hotspot = _hotspots[i].coords;
			}
		}
		break;
	case kActionTrigger:
		MapCall::execute();
		break;
	}
}

void SceneChangeTheWorks::readData(Common::SeekableReadStream &stream) {
	SceneChange::readData(stream);

	Common::String unusedName;	// empty in all 66 records
	readFilename(stream, unusedName);
	stream.skip(2);				// 1 in all 66

	_flag.label = stream.readSint16LE();
	_flag.flag = (byte)stream.readUint16LE();
	_hoverCursor = (CursorManager::CursorType)stream.readUint32LE();

	const uint16 numHotspots = stream.readUint16LE();
	_hotspots.resize(numHotspots);
	for (uint i = 0; i < numHotspots; ++i) {
		_hotspots[i].readData(stream);
	}
}

void SceneChangeTheWorks::execute() {
	if (_state == kActionTrigger) {
		NancySceneState.setEventFlag(_flag);
	}

	HotMultiframeSceneChange::execute();
}

void SceneChangeWithStream::readData(Common::SeekableReadStream &stream) {
	_sceneChange.readData(stream);
	readFilename(stream, _streamName);
}

void SceneChangeWithStream::execute() {
	if (_streamName.empty() || _streamName.equalsIgnoreCase("MainStream")) {
		// Addressed to the player's flow. `true` bypasses the stream redirect,
		// so this works the same whether the record is running in the main flow
		// or inside a stream - and inside a stream it is the whole point.
		NancySceneState.changeScene(_sceneChange, true);
		_isDone = true;
		return;
	}

	Stream *target = NancySceneState.getStreams().find(_streamName);
	if (target) {
		target->requestSceneChange(_sceneChange);
	} else {
		// Every record in the game names MainStream, so this is defensive.
		debugC(1, kDebugActionRecord, "Ignoring scene change addressed to stream '%s', which is not running",
			_streamName.c_str());
	}

	_isDone = true;
}

} // End of namespace Action
} // End of namespace Nancy
