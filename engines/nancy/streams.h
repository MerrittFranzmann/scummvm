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

#ifndef NANCY_STREAMS_H
#define NANCY_STREAMS_H

#include "common/array.h"
#include "common/str.h"

#include "engines/nancy/time.h"
#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionmanager.h"

namespace Common {
class Serializer;
}

namespace Nancy {

// Nancy16 runs several streams, each an independent script player with its
// own action records. The player's flow is the main stream ("MainStream").
// Record types 26 and 27 begin and end a stream, 93 detects a stream's scene
// changes and 21 changes scene inside a named stream.
//
// A stream whose target script has no background is kConcurrent: it gets its
// own ActionManager and runs beside the main flow, without viewport, scene
// sound or input. A target with artwork is kViewport: a close-up that the main
// flow shows until the stream ends, then returns from.
//
// The generic scratch flags are per flow; each stream keeps and clears its own.
// The ENVS ambient script of the current environment also runs as a stream. Its
// sound channels survive scene changes and it is not saved.

class Stream {
	friend class StreamManager;

public:
	enum Kind {
		kConcurrent,	// Own ActionManager, runs beside the main flow
		kViewport		// A close-up: the main flow is showing this stream's scene
	};

	Stream(const Common::String &name, Kind kind) : _name(name), _kind(kind) {}
	~Stream();

	const Common::String &getName() const { return _name; }
	Kind getKind() const { return _kind; }

	// True for the ENVS ambient script and everything it starts
	bool isAmbient() const { return _ambient; }
	uint16 getSceneID() const { return _sceneID; }
	const Common::String &getScriptName() const { return _script; }

	// Bumped every time this stream moves to a new script. Type 93 watches it.
	uint32 getSceneChangeCount() const { return _sceneChangeCount; }

	// The stream's own scene clock, reset every time the stream changes script.
	Time getSceneTime() const { return _sceneTime; }

	// A scene change made by one of this stream's records moves the stream, not
	// the player.
	void requestSceneChange(const SceneChangeDescription &desc) { _nextScene = desc; }
	bool hasPendingSceneChange() const { return _nextScene.sceneID != kNoScene; }

	// Per-stream copy of the scratch flags
	void setScratchFlag(uint index, byte value);
	byte getScratchFlag(uint index) const;
	void clearScratchFlags();

	// Channels of sounds started by this stream, silenced when it ends
	void noteSoundChannel(uint16 channelID);

private:
	Common::String _name;
	Kind _kind = kConcurrent;

	Common::String _script;			// Cif member name: "S3665", "Journal_Nancy"
	uint16 _sceneID = kNoScene;		// The numeric id when _script is an S<digits> scene

	SceneChangeDescription _nextScene;
	SceneChangeDescription _returnScene;	// kViewport only: where ending it goes

	uint32 _sceneChangeCount = 0;

	// The main flow's scene-change counter when the current script was loaded,
	// so a type 93 that executes a frame late still sees that change.
	uint32 _baselineMainCount = 0;

	bool _ended = false;
	bool _ambient = false;
	Time _sceneTime = 0;

	Common::Array<uint16> _soundChannels;
	Common::Array<byte> _scratchFlags;
	Action::ActionManager _manager;
};

class StreamManager {
public:
	~StreamManager();

	// Starts (or re-targets) the named stream on `scriptName`. sceneID is
	// kNoScene when the script is not an S<digits> scene.
	// Returns null when the script cannot be loaded.
	Stream *begin(const Common::String &name, const Common::String &scriptName,
			uint16 sceneID, uint16 frameID, uint16 continueSceneSound);

	// Ends a stream by name. An empty name means the stream this record belongs
	// to: the executing concurrent stream, or the kViewport stream the main flow
	// is showing. Returns true when a stream was ended.
	bool end(const Common::String &name);
	void endAll();

	// The current ENVS environment code ("ALT", "OFF_EXT_INT", ...)
	const Common::String &getEnvironment() const { return _environment; }

	// Called from Scene::load with the new scene's environment and its ambient
	// script. No-op when the code has not changed; an empty `ambientScript`
	// just ends the current ambience.
	void setEnvironment(const Common::String &code, const Common::String &ambientScript);

	// Ends the ambient script and every stream it started
	void endAmbient();

	Stream *find(const Common::String &name) const;
	uint size() const { return _streams.size(); }

	// The concurrent stream whose records are executing, or null in the main flow
	Stream *executing() const { return _executing; }

	// Used by type 93. "MainStream" and the empty name mean the main flow;
	// an unknown name always returns 0.
	uint32 getSceneChangeCount(const Common::String &streamName) const;

	// The count getSceneChangeCount returned when the executing stream's script
	// was loaded
	uint32 getWatchBaseline(const Common::String &streamName) const;

	void process();

	// Called from Scene::load once the new main scene is in place.
	void onMainSceneLoaded(uint16 sceneID);
	void onPause(bool pause);

	// Routes a scene change made from inside a concurrent stream to that stream.
	// Returns true when the change was consumed and must not touch the main flow.
	bool redirectSceneChange(const SceneChangeDescription &desc);

	// Records a mixer channel against the executing stream
	void noteSoundChannel(uint16 channelID);

	void synchronize(Common::Serializer &ser);

	// One line per running stream, for debug output
	Common::String describe() const;

	// Reports whether a script's summary has no viewport artwork. `exists` is
	// false when there is no such script.
	static bool scriptIsBackgroundless(const Common::String &scriptName, bool &exists);

	// Drops any Scene-held pointer into the given records before they are freed.
	static void detachScenePointers(Action::ActionManager &manager);

private:
	// countVisit is false on load, where the scene counts were already restored.
	bool loadScript(Stream &stream, const Common::String &scriptName, uint16 sceneID,
		bool countVisit = true);

	void destroy(Stream *stream);

	Common::Array<Stream *> _streams;
	Stream *_executing = nullptr;
	uint32 _mainSceneChangeCount = 0;
	uint32 _lastTick = 0;
	Common::String _environment;
};

} // End of namespace Nancy

#endif // NANCY_STREAMS_H
