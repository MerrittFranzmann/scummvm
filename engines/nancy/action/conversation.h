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

#ifndef NANCY_ACTION_CONVERSATION_H
#define NANCY_ACTION_CONVERSATION_H

#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/movieplayer.h"

namespace Nancy {
namespace Action {

// The base class for conversations, with no video data. Contains the following:
// - a base sound for the NPC's speech and its caption (mandatory)
// - a list of possible player responses, also with sounds and captions (optional)
// Captions are displayed in the Textbox, and player responses are also selectable there.
// Captions are hypertext; meaning, they contain extra data related to the text (see misc/hypertext.h)
// A conversation will auto-advance to a next scene when no responses are available; the next scene
// can either be described within the Conversation data, or can be whatever's pushed onto the scene "stack".
// Also supports branching scenes depending on a condition, though that is only used in older games.
// Player responses can also be conditional. Up to Nancy11 the condition data comes from nancy.dat
// (see devtools/create_nancy); from Nancy12 on it lives in per-character data scenes (S<800 + charID> / S<900 + charID>).
class ConversationSound : public RenderActionRecord {
public:
	ConversationSound();
	virtual ~ConversationSound();

	void init() override;
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	virtual bool isVideoDonePlaying() { return true; }

	// Nancy16 reordered this record and put a counted name array at the front.
	// Verified by exact byte consumption: 702/702 records of types 57 and 58.
	void readDataNancy16(Common::SeekableReadStream &stream);

	// The names the record opens with. names[0] is the sound, and for the cel
	// variant also the XSheet; the rest are alternate takes.
	Common::Array<Common::String> _nancy16Names;

	// Nancy16 names the conditional-dialogue and goodbye scripts directly
	// rather than by character id. Empty means the record has none.
	Common::String _nancy16ConditionalScript;
	Common::String _nancy16GoodbyeScript;
	bool isViewportRelative() const override { return true; }

protected:
	struct ConversationFlag {
		byte type;
		FlagDescription flag;
		byte orFlag;

		void read(Common::SeekableReadStream &stream);
		bool isSatisfied() const;
		void set() const;
	};

	struct ConversationFlags {
		Common::Array<ConversationFlag> conditionFlags;

		void read(Common::SeekableReadStream &stream);
		bool isSatisfied() const;
	};

	struct ResponseStruct {
		enum AddRule { kAddIfNotFound, kRemoveAndAddToEnd, kRemove };

		ConversationFlags conditionFlags;
		Common::String text;
		Common::String soundName;
		byte addRule = kAddIfNotFound;
		SceneChangeDescription sceneChange;
		FlagDescription flagDesc;

		bool isOnScreen = false;
	};

	struct FlagsStruct {
		ConversationFlags conditions;
		ConversationFlag flagToSet;
	};

	struct SceneBranchStruct {
		ConversationFlags conditions;
		SceneChangeDescription sceneChange;
	};

	static const byte kDefaultNextSceneEnabled	= 1;
	static const byte kDefaultNextSceneDisabled	= 2;

	static const byte kPopNextScene				= 1;
	static const byte kNoPopNextScene			= 2;

	Common::String getRecordTypeName() const override { return "ConversationSound"; }

	// Functions for reading captions are virtual to allow easier support for the terse Conversation variants
	virtual void readCaptionText(Common::SeekableReadStream &stream);
	virtual void readResponseText(Common::SeekableReadStream &stream, ResponseStruct &response);

	// Used in subclasses
	void readTerseData(Common::SeekableReadStream &stream);
	void readTerseCaptionText(Common::SeekableReadStream &stream);
	void readTerseResponseText(Common::SeekableReadStream &stream, ResponseStruct &response);

	// Nancy13 compact conversation format. readCelDataNancy13 is the only
	// per-class difference: the base skips the cel-only frame fields, while
	// ConversationCel reads them plus the XSheet.
	void readDataNancy13(Common::SeekableReadStream &stream);
	virtual void readCelDataNancy13(Common::SeekableReadStream &stream);

	// Add conditional/goodbye responses: from nancy.dat up to Nancy11, from data scenes for Nancy12+
	void addConditionalDialogue();
	void addGoodbye();
	void addConditionalDialogueNancy12();
	void addGoodbyeNancy12();

	// Nancy12 addresses the conditional-dialogue and goodbye tables by
	// character id (S<800 + id> / S<900 + id>); Nancy16 names the script
	// outright ("infocheck_marg", "bye_marg"). Same table format either way, so
	// both callers share these.
	void addConditionalDialogueFromScript(const Common::Path &scriptName);
	void addGoodbyeFromScript(const Common::Path &scriptName);

	Common::String _text;

	SoundDescription _sound;
	SoundDescription _responseGenericSound;

	byte _conditionalResponseCharacterID;
	byte _goodbyeResponseCharacterID;
	byte _defaultNextScene = kDefaultNextSceneEnabled;
	byte _popNextScene = kNoPopNextScene;
	SceneChangeDescription _sceneChange;

	Common::Array<ResponseStruct> _responses;
	Common::Array<FlagsStruct> _flagsStructs;
	Common::Array<SceneBranchStruct> _sceneBranchStructs;

	bool _hasDrawnTextbox;
	int16 _pickedResponse;

	const byte _noResponse;
};

// Conversation with an AVF video. Originally called PlayPrimaryVideoChan0
class ConversationVideo : public ConversationSound {
public:
	void init() override;
	void updateGraphics() override;
	void onPause(bool pause) override;

	void readData(Common::SeekableReadStream &stream) override;

	bool isVideoDonePlaying() override;

protected:
	Common::String getRecordTypeName() const override;

	Common::String _videoName;
	Common::Path _paletteName;
	uint _videoFormat = kLargeVideoFormat;
	uint16 _firstFrame = 0;
	int16 _lastFrame = 0;
	MoviePlayer _decoder;
};

class ConversationCelLoader;

// Conversation with separate cels for the body and head of the character.
// Cels are separate images bundled inside a .cal file
class ConversationCel : public ConversationSound {
public:
	ConversationCel() {}
	virtual ~ConversationCel();

	void init() override;
	void registerGraphics() override;
	void updateGraphics() override;

	void readData(Common::SeekableReadStream &stream) override;

	bool load();
	uint getCurFrame() const { return _curFrame; }

protected:
	Common::String getRecordTypeName() const override { return "ConversationCel"; }

	struct Cel {
		Graphics::ManagedSurface surf;
		Common::Rect src;
		Common::Rect dest;
	};

	class RenderedCel : public RenderObject {
	public:
		RenderedCel() : RenderObject(9) {}
		bool isViewportRelative() const override { return true; }
	};

	static const byte kCelOverrideTreeRectsOff	= 1;
	static const byte kCelOverrideTreeRectsOn	= 2;

	bool isVideoDonePlaying() override;
	Cel &loadCel(const Common::Path &name, const Common::String &treeName);

	void readXSheet(Common::SeekableReadStream &stream, const Common::String &xsheetName);
	void readCelDataNancy13(Common::SeekableReadStream &stream) override;

	Common::Array<Common::Array<Common::Path>> _celNames;
	Common::Array<Common::String> _treeNames;

	uint32 _frameTime = 0;
	uint _videoFormat = kLargeVideoFormat;
	uint16 _firstFrame = 0;
	uint16 _lastFrame = 0;

	Common::Array<byte> _drawingOrder;
	Common::Array<byte> _overrideTreeRects;

	Common::Array<Common::Rect> _overrideRectSrcs;
	Common::Array<Common::Rect> _overrideRectDests;

	uint _curFrame = 0;
	uint32 _nextFrameTime = 0;

	// --- Nancy16 -------------------------------------------------------
	// The cel pipeline is gone: each XSheet layer is a Bink video with an alpha
	// channel, overlaid on the viewport. Verified across the corpus - all 275
	// type 57 records name a .bik that ships with the game, while only 1 of the
	// 1004 names in the sound-only type 58 records does.
	//
	// There are two layers: layer 1 is the head, named after the sheet itself
	// and animated one frame per sheet entry; layer 0 is the character's body,
	// a single long video shared by every sheet for that character, indexed
	// per head frame by the sheet's entry table. See readXSheetNancy16().
	void readXSheetNancy16(const Common::String &xsheetName);
	void drawNancy16Layer(RenderedCel &obj, const Graphics::Surface &frame, const Common::Rect &dest);
	int nancy16BodyFrameFor(uint headFrame) const;

	// Indices into _celRObjects on the Nancy16 path.
	static const uint kNancy16HeadCel = 0;
	static const uint kNancy16BodyCel = 1;

	MoviePlayer _nancy16Video;
	bool _nancy16HasVideo = false;
	Common::Rect _nancy16Dest;	// from the XSheet's layer 1 (head) rect

	MoviePlayer _nancy16BodyVideo;
	bool _nancy16HasBody = false;
	Common::String _nancy16BodyName;			// XSheet layer 0 name, e.g. "COLINBODY"
	Common::Rect _nancy16BodyDest;				// from the XSheet's layer 0 (body) rect
	Common::Array<int32> _nancy16BodyFrames;	// one body frame index per head frame
	int _nancy16BodyCurFrame = -1;				// body frame currently pasted, -1 = none

	Common::Array<RenderedCel> _celRObjects;

	Common::HashMap<Common::Path, Cel, Common::Path::IgnoreCase_Hash, Common::Path::IgnoreCase_EqualTo> _celCache;
	Common::SharedPtr<ConversationCelLoader> _loaderPtr;
};

// A ConversationSound without embedded text; uses the CONVO chunk instead
class ConversationSoundT : public ConversationSound {
protected:
	Common::String getRecordTypeName() const override { return "ConversationSoundT"; }

	void readCaptionText(Common::SeekableReadStream &stream) override { readTerseCaptionText(stream); }
	void readResponseText(Common::SeekableReadStream &stream, ResponseStruct &response) override { readTerseResponseText(stream, response); }
};

// A ConversationCel without embedded text; uses the CONVO chunk instead
class ConversationCelT : public ConversationCel {
protected:
	Common::String getRecordTypeName() const override { return "ConversationCelT"; }

	void readCaptionText(Common::SeekableReadStream &stream) override { readTerseCaptionText(stream); }
	void readResponseText(Common::SeekableReadStream &stream, ResponseStruct &response) override { readTerseResponseText(stream, response); }
};

// A ConversationSound with a much smaller data footprint
class ConversationSoundTerse : public ConversationSound {
public:
	void readData(Common::SeekableReadStream &stream) override;

protected:
	Common::String getRecordTypeName() const override { return "ConversationSoundTerse"; }
};

// A ConversationCel with a much smaller data footprint
class ConversationCelTerse : public ConversationCel {
public:
	void readData(Common::SeekableReadStream &stream) override;

protected:
	Common::String getRecordTypeName() const override { return "ConversationCelTerse"; }
};

// Nancy12+ conditional dialogue record (type 35), one per response, stored in scene S<800 + charID>
class ConversationInfoCheck : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	Common::String getRecordTypeName() const override { return "ConversationInfoCheck"; }

	Common::String _soundID; // Doubles as the CONVO text key
	uint16 _sceneID = 0;
};

// Nancy12+ goodbye record (type 36), stored in scene S<900 + charID>; one sound and scene are picked at random
class ConversationGoodbye : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	Common::String getRecordTypeName() const override { return "ConversationGoodbye"; }

	Common::Array<Common::String> _soundIDs;
	Common::Array<uint16> _sceneIDs;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_CONVERSATION_H
