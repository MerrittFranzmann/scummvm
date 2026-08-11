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

#ifndef NANCY_ACTION_NAVIGATIONRECORDS_H
#define NANCY_ACTION_NAVIGATIONRECORDS_H

#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// Simply changes the scene
class SceneChange : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	SceneChangeDescription _sceneChange;

	Common::String getRecordExtraInfo() const override { return Common::String::format("Scene %d", _sceneChange.sceneID); }

protected:
	Common::String getRecordTypeName() const override { return "SceneChange"; }
};

// Changes the scene when clicked
class HotSingleFrameSceneChange : public SceneChange {
public:
	HotSingleFrameSceneChange() {
		_hasHotspot = false;
		_hoverCursor = CursorManager::kNormal;
	}
	virtual ~HotSingleFrameSceneChange() {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	CursorManager::CursorType getHoverCursor() const override { return _hoverCursor; }
	bool cursorSetFromScript() const override { return false; }

	HotspotDescription _sceneHotspot;

	bool canHaveHotspot() const override { return true; }

protected:
	CursorManager::CursorType _hoverCursor;

	Common::String getRecordTypeName() const override { return "HotSingleFrameSceneChange"; }
};

// Changes the scene when clicked. Hotspot can move along with scene background frame.
// Nancy4 introduced several sub-types with a specific mouse cursor to show when
// hovering; all of them are handled in this class as well.
class HotMultiframeSceneChange : public SceneChange {
public:
	HotMultiframeSceneChange(CursorManager::CursorType hoverCursor, bool isTerse = false) :
		_hoverCursor(hoverCursor), _isTerse(isTerse) {}
	virtual ~HotMultiframeSceneChange() {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	CursorManager::CursorType getHoverCursor() const override { return _hoverCursor; }
	bool cursorSetFromScript() const override { return _isTerse; }

	Common::Array<HotspotDescription> _hotspots;
	bool _isTerse = false;

	bool canHaveHotspot() const override { return true; }

protected:
	Common::String getRecordTypeName() const override {
		if (_isTerse)
			return "HotMultiframeSceneChangeTerse";

		switch (_hoverCursor) {
		case CursorManager::kMoveForward:
			return "HotMultiframeForwardSceneChange";
		case CursorManager::kMoveUp:
			return "HotMultiframeUpSceneChange";
		case CursorManager::kMoveDown:
			return "HotMultiframeDownSceneChange";
		default:
			return "HotMultiframeSceneChange";
		}
	}

	CursorManager::CursorType _hoverCursor;
};

// Changes the scene when clicked; does _not_ move with scene background.
// Nancy4 introduced several sub-types with a specific mouse cursor to show when
// hovering; all of them are handled in this class as well.
class Hot1FrSceneChange : public SceneChange {
public:
	Hot1FrSceneChange(CursorManager::CursorType hoverCursor, bool dynamicCursor = false, bool isTerse = false) :
		_hoverCursor(hoverCursor), _dynamicCursor(dynamicCursor), _isTerse(isTerse) {}
	virtual ~Hot1FrSceneChange() {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	CursorManager::CursorType getHoverCursor() const override { return _hoverCursor; }
	bool cursorSetFromScript() const override { return _dynamicCursor; }

	HotspotDescription _hotspotDesc;
	bool _isTerse = false;
	bool _dynamicCursor = false;

	bool canHaveHotspot() const override { return true; }

protected:
	Common::String getRecordTypeName() const override {
		if (_isTerse)
			return "HotSceneChangeTerse";

		switch (_hoverCursor) {
		case CursorManager::kExit:
			return "Hot1FrExitSceneChange";
		case CursorManager::kMoveForward:
			return "Hot1FrForwardSceneChange";
		case CursorManager::kMoveBackward:
			return "Hot1FrBackSceneChange";
		case CursorManager::kMoveUp:
			return "Hot1FrUpSceneChange";
		case CursorManager::kMoveDown:
			return "Hot1FrDownSceneChange";
		case CursorManager::kMoveLeft:
			return "Hot1FrLeftSceneChange";
		case CursorManager::kMoveRight:
			return "Hot1FrRightSceneChange";
		default:
			return "Hot1FrSceneChange";
		}
	}

	CursorManager::CursorType _hoverCursor;
};

// Changes the scene when clicked. Hotspot can move along with scene background frame.
// However, the scene it changes to can be one of two options, picked based on
// a provided condition.
class HotMultiframeMultiSceneChange : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	SceneChangeWithFlag _onTrue;
	SceneChangeWithFlag _onFalse;
	byte _condType;
	uint16 _conditionID;
	byte _conditionPayload;
	Common::Array<HotspotDescription> _hotspots;

	bool canHaveHotspot() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "HotMultiframeMultisceneChange"; }
};

// Changes the scene when clicked. Hotspot can move along with scene background frame.
// However, the scene it changes to can be one of several options, picked based on
// the item the player is currently holding.
class HotMultiframeMultiSceneCursorTypeSceneChange : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::Array<SceneChangeDescription> _scenes;
	Common::Array<uint16> _cursorTypes;

	SceneChangeDescription _defaultScene;
	Common::Array<HotspotDescription> _hotspots;

	Common::String getRecordExtraInfo() const override { return Common::String::format("Default scene %d", _defaultScene.sceneID); }

protected:
	Common::String getRecordTypeName() const override { return "HotMultiframeMultisceneCursorTypeSceneChange"; }
};

// Simply switches to the Map state. TVD/nancy1 only.
class MapCall : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	CursorManager::CursorType getHoverCursor() const override { return CursorManager::kExit; }

protected:
	Common::String getRecordTypeName() const override { return "MapCall"; }
};

// Switches to the Map state when clicked; does _not_ move with background frame. TVD/nancy1 only.
class MapCallHot1Fr : public MapCall {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	HotspotDescription _hotspotDesc;

	bool canHaveHotspot() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "MapCallHot1Fr"; }
};

// Switches to the Map state when clicked. Hotspot can move along with scene background frame. TVD/nancy1 only.
class MapCallHotMultiframe : public MapCall {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::Array<HotspotDescription> _hotspots;

	bool canHaveHotspot() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "MapCallHotMultiframe"; }
};

// Nancy16 type 25, "Scene Change with The Works". 66 records, all in navigation
// hubs (OFF_Node, PIA_node, REDX, KIO_F). Validates 51 + 18*numHotspots on 66/66:
//
//   uint16   sceneID, frameID, continueSceneSound
//   char[33] name        empty in 66/66
//   uint16   1 in 66/66
//   int16    flag label, uint16 flag value   raised when the change fires
//   uint32   cursor
//   uint16   numHotspots, then numHotspots x (uint16 frameID + RECT)
class SceneChangeTheWorks : public HotMultiframeSceneChange {
public:
	SceneChangeTheWorks() : HotMultiframeSceneChange(CursorManager::kHotspot) {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	bool cursorSetFromScript() const override { return true; }

	FlagDescription _flag;

protected:
	Common::String getRecordTypeName() const override { return "SceneChangeTheWorks"; }
};

// Nancy16 type 21, "Scene Change with Frame and Stream". A bare 6-byte scene
// change descriptor followed by the 33-byte name of the stream the change is
// addressed to - 39 bytes on 4/4. Nancy16 can run more than one scene flow at
// once: AR 26 begins a named stream at a scene, AR 27 ends one, and AR 93
// watches one for scene changes. This record is how a scene running inside a
// side stream moves the flow the player is actually looking at, which is why
// three of the four live in stream target scenes (S3199, S5450 x2) and only
// S6431 is reachable by ordinary navigation.
//
// All four name "MainStream", the player's flow, so the change is applied to the
// main flow explicitly - bypassing the redirect that would otherwise send a
// scene change made inside a stream to that stream. This is the record that
// lets s3199, the backgroundless script the "OFF_Hide" stream runs, move the
// player to s2960 once Fango has opened the door.
//
// A change addressed to a stream this runtime is not running is dropped rather
// than guessed at - applying it to the main flow would teleport the player.
class SceneChangeWithStream : public SceneChange {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::String _streamName;

protected:
	Common::String getRecordTypeName() const override { return "SceneChangeWithStream"; }
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_NAVIGATIONRECORDS_H
