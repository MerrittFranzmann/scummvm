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

#include "common/serializer.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/iff.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/util.h"
#include "engines/nancy/streams.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {

// The scratch flag range is 1010-1059 in Nancy16 (see NancyEngine::populateStaticData).
// Read out of the static data rather than hardcoded so this stays correct if the
// table ever moves.
static uint genericFlagCount() {
	return g_nancy->getStaticData().genericEventFlags.size();
}

void Stream::setScratchFlag(uint index, byte value) {
	if (_scratchFlags.size() < genericFlagCount()) {
		_scratchFlags.resize(genericFlagCount(), g_nancy->_false);
	}

	if (index < _scratchFlags.size()) {
		_scratchFlags[index] = value;
	}
}

byte Stream::getScratchFlag(uint index) const {
	if (index < _scratchFlags.size()) {
		return _scratchFlags[index];
	}

	return g_nancy->_false;
}

void Stream::clearScratchFlags() {
	_scratchFlags.clear();
	_scratchFlags.resize(genericFlagCount(), g_nancy->_false);
}

void Stream::noteSoundChannel(uint16 channelID) {
	for (uint i = 0; i < _soundChannels.size(); ++i) {
		if (_soundChannels[i] == channelID) {
			return;
		}
	}

	_soundChannels.push_back(channelID);
}

// Scene keeps a bare pointer to the movie and the conversation it considers
// current, and a stream's records can set either. Freeing those records without
// clearing the pointer would leave the main flow chasing a dangling one - the
// chase stream on s5450 alone holds 26 movie records.
void StreamManager::detachScenePointers(Action::ActionManager &manager) {
	if (!State::Scene::hasInstance()) {
		return;
	}

	const Action::ActionRecord *movie = (const Action::ActionRecord *)NancySceneState.getActiveMovie();
	const Action::ActionRecord *convo = (const Action::ActionRecord *)NancySceneState.getActiveConversation();
	if (!movie && !convo) {
		return;
	}

	Common::Array<Action::ActionRecord *> &records = manager.getActionRecords();
	for (uint i = 0; i < records.size(); ++i) {
		if (movie && records[i] == movie) {
			NancySceneState.setActiveMovie(nullptr);
		}

		if (convo && records[i] == convo) {
			NancySceneState.setActiveConversation(nullptr);
		}
	}
}

Stream::~Stream() {
	StreamManager::detachScenePointers(_manager);
}

StreamManager::~StreamManager() {
	endAll();
}

bool StreamManager::scriptIsBackgroundless(const Common::String &scriptName, bool &exists) {
	exists = false;

	if (scriptName.empty()) {
		return false;
	}

	IFF *iff = g_nancy->_resource->loadIFF(Common::Path(scriptName));
	if (!iff) {
		return false;
	}

	exists = true;

	Common::SeekableReadStream *summary = iff->getChunkStream("SSUM");
	if (!summary) {
		summary = iff->getChunkStream("TSUM");
	}

	// A script with no summary at all cannot own a viewport, so it runs
	// concurrently. Nothing in this game is shaped that way, but a missing chunk
	// must not be read as "has artwork".
	bool backgroundless = true;

	if (summary) {
		// Both chunk shapes open with a 0x32-byte description followed by the
		// background video's filename, which is all that is needed here.
		summary->seek(0x32);
		Common::String videoFile;
		readFilename(*summary, videoFile);

		// An empty video name is not artwork either. Three scenes are shaped
		// that way - S2605, S4708 and S5006, all blank-description stubs - and
		// s4708 is live: it is the target of COS_INT_AMB_SFX's type 26. Read as
		// "has a background" it made the Costume Store's ambient stream seize
		// the viewport and the main flow then died on
		// "Couldn't load video file .avf or .bik".
		backgroundless = videoFile.empty() ||
			videoFile.equalsIgnoreCase("NO_BG") ||
			videoFile.equalsIgnoreCase("NO_ART_SCENE") ||
			videoFile.equalsIgnoreCase("POPUP_PREP_SCENE");
		delete summary;
	}

	delete iff;
	return backgroundless;
}

bool StreamManager::loadScript(Stream &stream, const Common::String &scriptName, uint16 sceneID,
		bool countVisit) {
	IFF *iff = g_nancy->_resource->loadIFF(Common::Path(scriptName));
	if (!iff) {
		warning("Stream '%s' cannot load script %s", stream._name.c_str(), scriptName.c_str());
		return false;
	}

	detachScenePointers(stream._manager);
	stream._manager.clearActionRecords(false);
	stream.clearScratchFlags();
	stream._script = scriptName;
	stream._sceneID = sceneID;
	stream._nextScene = SceneChangeDescription();
	stream._sceneTime = 0;
	++stream._sceneChangeCount;
	stream._baselineMainCount = _mainSceneChangeCount;

	Common::SeekableReadStream *chunk = nullptr;
	uint num = 0;
	while (chunk = iff->getChunkStream("ACT", num), chunk != nullptr) {
		stream._manager.addNewActionRecord(*chunk);
		delete chunk;
		++num;
	}

	delete iff;

	// A stream entering a scene is a scene entry as far as the game is
	// concerned: s5450's chase gives up on a kSceneCount dependency that counts
	// 20 visits to scene 5450, and the only thing that ever enters 5450 is the
	// stream itself. Not on the load path, though - see the header comment.
	if (sceneID != kNoScene && countVisit) {
		NancySceneState.bumpSceneCount(sceneID);
	}

	debugC(0, kDebugScene, "Stream '%s' now on %s (%u action records)",
		stream._name.c_str(), scriptName.c_str(), num);

	return true;
}

Stream *StreamManager::find(const Common::String &name) const {
	for (uint i = 0; i < _streams.size(); ++i) {
		if (_streams[i]->_name.equalsIgnoreCase(name)) {
			return _streams[i];
		}
	}

	return nullptr;
}

uint32 StreamManager::getSceneChangeCount(const Common::String &streamName) const {
	if (streamName.empty() || streamName.equalsIgnoreCase("MainStream")) {
		return _mainSceneChangeCount;
	}

	const Stream *stream = find(streamName);
	return stream ? stream->_sceneChangeCount : 0;
}

uint32 StreamManager::getWatchBaseline(const Common::String &streamName) const {
	if (_executing && (streamName.empty() || streamName.equalsIgnoreCase("MainStream"))) {
		return _executing->_baselineMainCount;
	}

	return getSceneChangeCount(streamName);
}

Stream *StreamManager::begin(const Common::String &name, const Common::String &scriptName,
		uint16 sceneID, uint16 frameID, uint16 continueSceneSound) {
	if (name.empty() || scriptName.empty()) {
		return nullptr;
	}

	if (name.equalsIgnoreCase("MainStream")) {
		// The main flow is not a Stream object and cannot be restarted. Nothing
		// in the shipped data does this; refusing is cheaper than the crash.
		warning("Refusing to begin a stream called MainStream on %s", scriptName.c_str());
		return nullptr;
	}

	bool exists = false;
	const bool concurrent = scriptIsBackgroundless(scriptName, exists);
	if (!exists) {
		warning("Stream '%s' names script %s, which does not exist", name.c_str(), scriptName.c_str());
		return nullptr;
	}

	// A stream started from inside the ambient script belongs to the ambience -
	// that is how `<code>_Random_sfx` gets marked without naming it anywhere.
	const bool ambient = _executing && _executing->_ambient;

	Stream *existing = find(name);
	if (existing) {
		// Re-targeting rather than stacking a second copy: a type 26 record is
		// one-shot per script load, but the player can walk back into the scene
		// that starts it, and two copies of "Prudence Phone Ringing" would ring
		// over each other.
		if (existing->_kind == Stream::kConcurrent && concurrent) {
			existing->_ambient = existing->_ambient || ambient;

			if (!existing->_script.equalsIgnoreCase(scriptName)) {
				if (existing == _executing) {
					// A record retargeting the stream it is itself running in.
					// loadScript() would free the record array the caller is
					// still walking, so go through the pending-scene-change slot
					// process() already drains after the loop unwinds.
					if (sceneID != kNoScene) {
						SceneChangeDescription desc;
						desc.sceneID = sceneID;
						desc.frameID = frameID;
						desc.continueSceneSound = continueSceneSound;
						existing->requestSceneChange(desc);
					} else {
						warning("Stream '%s' cannot retarget itself to non-scene script %s",
							name.c_str(), scriptName.c_str());
					}

					return existing;
				}

				loadScript(*existing, scriptName, sceneID);
			}

			return existing;
		}

		// Same name, different kind: drop the old one and start again.
		end(name);
	}

	if (concurrent) {
		Stream *newStream = new Stream(name, Stream::kConcurrent);
		newStream->_ambient = ambient;
		if (!loadScript(*newStream, scriptName, sceneID)) {
			delete newStream;
			return nullptr;
		}

		_streams.push_back(newStream);
		debugC(0, kDebugScene, "Stream '%s' started on %s (concurrent)", name.c_str(), scriptName.c_str());
		return newStream;
	}

	// A stream on a script that owns viewport artwork is a close-up: there is
	// only one viewport, so the main flow has to show it. See the note in
	// streams.h for why this is the reading the data forces.
	if (ambient) {
		// ...but the ambience is never a close-up. Nothing should reach here -
		// all 19 `<code>_Random_sfx` targets are backgroundless - and a stream
		// that dragged the player into a sound-effect scene would be a far worse
		// failure than a missing noise.
		warning("Refusing to give the viewport to ambient stream '%s' (%s)",
			name.c_str(), scriptName.c_str());
		return nullptr;
	}

	if (sceneID == kNoScene) {
		warning("Stream '%s' wants the viewport for %s, which is not a scene", name.c_str(), scriptName.c_str());
		return nullptr;
	}

	Stream *newStream = new Stream(name, Stream::kViewport);
	newStream->_script = scriptName;
	newStream->_sceneID = sceneID;
	newStream->_returnScene = NancySceneState.getSceneInfo();
	newStream->_returnScene.continueSceneSound = kContinueSceneSound;
	++newStream->_sceneChangeCount;
	_streams.push_back(newStream);

	SceneChangeDescription target;
	target.sceneID = sceneID;
	target.frameID = frameID;
	target.continueSceneSound = continueSceneSound;
	NancySceneState.changeScene(target, true);

	debugC(0, kDebugScene, "Stream '%s' took the viewport for %s (return to %u)",
		name.c_str(), scriptName.c_str(), newStream->_returnScene.sceneID);
	return newStream;
}

void StreamManager::destroy(Stream *stream) {
	for (uint i = 0; i < _streams.size(); ++i) {
		if (_streams[i] == stream) {
			_streams.remove_at(i);
			break;
		}
	}

	delete stream;
}

bool StreamManager::end(const Common::String &name) {
	Stream *target = nullptr;

	if (name.empty()) {
		// "End the stream this record belongs to." 35 of the 57 type 27 records
		// name no stream. Inside a concurrent stream that is unambiguous; in the
		// main flow it means the close-up the main flow is currently showing,
		// which is how s6729's exit strip closes the tunnel flow chart. When the
		// main flow is on a scene no stream owns there is nothing to end, and
		// ending the main flow is never an option.
		if (_executing) {
			target = _executing;
		} else {
			const uint16 current = NancySceneState.getSceneInfo().sceneID;
			for (uint i = 0; i < _streams.size(); ++i) {
				if (_streams[i]->_kind == Stream::kViewport && _streams[i]->_sceneID == current) {
					target = _streams[i];
					break;
				}
			}
		}
	} else {
		target = find(name);
	}

	if (!target) {
		debugC(1, kDebugScene, "No stream '%s' to end", name.c_str());
		return false;
	}

	debugC(0, kDebugScene, "Stream '%s' ended (was on %s)", target->_name.c_str(), target->_script.c_str());

	if (target->_kind == Stream::kViewport) {
		// Hand the viewport back. The return scene is remembered on the stream
		// rather than going through Scene::pushScene, so a close-up opened over
		// an already-pushed scene cannot corrupt that stack.
		SceneChangeDescription back = target->_returnScene;
		destroy(target);

		if (back.sceneID != kNoScene) {
			NancySceneState.changeScene(back, true);
		}

		return true;
	}

	// MEASURED, by elimination: S3502 ("RED exit housekeeping") ends both
	// "MF Calling" and "Prudence Phone Ringing" and stops sound channels 6-9,
	// but the ring those two streams loop plays on channel 17 and nothing in
	// S3502 touches it. If ending a stream did not silence it the phone would
	// ring for the rest of the game. Where the script does stop the channel
	// itself (S2960 stops 20 and 21 beside its two end records) this is
	// harmless duplication.
	Common::String stopped;
	for (uint i = 0; i < target->_soundChannels.size(); ++i) {
		stopped += Common::String::format("%s%u", i ? "," : "", target->_soundChannels[i]);
		g_nancy->_sound->stopSound(target->_soundChannels[i]);
	}

	if (!stopped.empty()) {
		debugC(0, kDebugScene, "Stream '%s' silenced channels %s", target->_name.c_str(), stopped.c_str());
	}

	if (target == _executing) {
		// Deleting the manager whose records are mid-loop would be fatal; mark
		// it and let process() reap it once the loop unwinds.
		target->_ended = true;
		return true;
	}

	destroy(target);
	return true;
}

void StreamManager::endAll() {
	for (uint i = 0; i < _streams.size(); ++i) {
		// An ambient channel is exempt from the per-scene-change stop, so
		// dropping the stream without silencing it would leave the old
		// location's bed looping over the new one - and the incoming bed's
		// "that channel is silent" dependency would then never open. This is
		// the in-session load path: endAll() runs, then Scene::load() derives
		// the environment the save restored into.
		if (_streams[i]->_ambient && g_nancy && g_nancy->_sound) {
			for (uint j = 0; j < _streams[i]->_soundChannels.size(); ++j) {
				g_nancy->_sound->stopSound(_streams[i]->_soundChannels[j]);
			}
		}

		delete _streams[i];
	}

	_streams.clear();
	_executing = nullptr;
	_environment.clear();
}

void StreamManager::endAmbient() {
	// Collect first: end() mutates _streams. Both the ambient script and the
	// `<code>_Random_sfx` stream it started are flagged. end() does the channel
	// silencing, which is the whole point - the bed loops forever and nothing in
	// the data ever stops it.
	Common::Array<Common::String> names;
	for (uint i = 0; i < _streams.size(); ++i) {
		if (_streams[i]->_ambient) {
			names.push_back(_streams[i]->_name);
		}
	}

	for (uint i = 0; i < names.size(); ++i) {
		end(names[i]);
	}

	_environment.clear();
}

void StreamManager::setEnvironment(const Common::String &code, const Common::String &ambientScript) {
	if (_environment.equalsIgnoreCase(code)) {
		// Same location. Walking from one scene of the Red Room to the next must
		// not restart the ambience, so this is the common case and it does
		// nothing at all.
		return;
	}

	const Common::String previous = _environment;
	endAmbient();
	_environment = code;

	if (ambientScript.empty()) {
		// ENVS gives the UI and LET environments no ambience.
		debugC(0, kDebugScene, "Environment %s -> %s (silent)",
			previous.empty() ? "(none)" : previous.c_str(), code.c_str());
		return;
	}

	// The stream name is the engine's, not the data's: no type 26 or type 27
	// record anywhere in the game names an ambient script, so any name taken
	// from the data risks colliding with one that does. OFF_EXTERIOR_AMB_SFX is
	// the live case - its own type 26 starts a stream called
	// "OFF_Exterior_Amb_sfx", which is also its ENVS ambientSoundName.
	const Common::String streamName = Common::String("ENVS ") + code;

	Stream *stream = begin(streamName, ambientScript, kNoScene, 0, kContinueSceneSound);
	if (!stream) {
		warning("Environment %s names ambient script %s, which did not start",
			code.c_str(), ambientScript.c_str());
		return;
	}

	stream->_ambient = true;

	debugC(0, kDebugScene, "Environment %s -> %s, ambient stream '%s' on %s",
		previous.empty() ? "(none)" : previous.c_str(), code.c_str(),
		streamName.c_str(), ambientScript.c_str());
}

bool StreamManager::redirectSceneChange(const SceneChangeDescription &desc) {
	if (!_executing || desc.sceneID == kNoScene) {
		return false;
	}

	_executing->requestSceneChange(desc);
	return true;
}

void StreamManager::noteSoundChannel(uint16 channelID) {
	if (!_executing) {
		return;
	}

	const uint before = _executing->_soundChannels.size();
	_executing->noteSoundChannel(channelID);

	if (_executing->_soundChannels.size() != before) {
		debugC(1, kDebugScene, "Stream '%s' owns sound channel %u",
			_executing->_name.c_str(), channelID);
	}
}

void StreamManager::process() {
	// Tracked unconditionally so a stream started after a long gap is not handed
	// the whole gap as its first delta.
	const uint32 now = g_nancy->getTotalPlayTime();
	const uint32 delta = (_lastTick != 0 && now > _lastTick) ? now - _lastTick : 0;
	_lastTick = now;

	if (_streams.empty()) {
		return;
	}

	// A main scene load is about to replace everything; let it happen first.
	if (NancySceneState.getState() != State::Scene::kRun) {
		return;
	}

	// Snapshot: a stream may start or end another stream while running.
	Common::Array<Stream *> toRun = _streams;

	for (uint i = 0; i < toRun.size(); ++i) {
		Stream *stream = toRun[i];

		if (stream->_kind != Stream::kConcurrent) {
			continue;
		}

		// Still alive? (an earlier stream this frame may have ended it)
		bool alive = false;
		for (uint j = 0; j < _streams.size(); ++j) {
			if (_streams[j] == stream) {
				alive = true;
				break;
			}
		}

		if (!alive || stream->_ended) {
			continue;
		}

		stream->_sceneTime += delta;

		_executing = stream;
		stream->_manager.processActionRecords();
		_executing = nullptr;

		if (stream->_ended) {
			destroy(stream);
			continue;
		}

		if (stream->hasPendingSceneChange()) {
			const SceneChangeDescription next = stream->_nextScene;
			loadScript(*stream, Common::String::format("S%u", next.sceneID), next.sceneID);
		}

		// A record inside the stream asked the main flow to move; stop here so
		// the rest of the streams see the new scene rather than the old one.
		if (NancySceneState.getState() != State::Scene::kRun) {
			break;
		}
	}
}

void StreamManager::onMainSceneLoaded(uint16 sceneID) {
	++_mainSceneChangeCount;

	// A close-up whose scene the main flow has navigated away from is over,
	// whether or not its own type 27 got to run. Without this a stream left
	// behind by a scene change would keep the game from ever popping back.
	for (int i = (int)_streams.size() - 1; i >= 0; --i) {
		Stream *stream = _streams[i];
		if (stream->_kind == Stream::kViewport && stream->_sceneID != sceneID) {
			debugC(0, kDebugScene, "Stream '%s' dropped: main flow left scene %u for %u",
				stream->_name.c_str(), stream->_sceneID, sceneID);
			destroy(stream);
		}
	}
}

void StreamManager::onPause(bool pause) {
	for (uint i = 0; i < _streams.size(); ++i) {
		_streams[i]->_manager.onPause(pause);
	}
}

Common::String StreamManager::describe() const {
	Common::String out;
	for (uint i = 0; i < _streams.size(); ++i) {
		if (i) {
			out += ", ";
		}

		out += Common::String::format("%s@%s%s", _streams[i]->_name.c_str(),
			_streams[i]->_script.c_str(),
			_streams[i]->_kind == Stream::kViewport ? "(vp)" :
				(_streams[i]->_ambient ? "(amb)" : ""));
	}

	return out;
}

// Only the names and scripts are saved. The records themselves are rebuilt by
// reloading the script, exactly as a fresh stream start would, which matches
// how the main flow's records are restored on load.
//
// The ambient streams are deliberately left out. They are a pure function of
// the environment the restored scene belongs to, and Scene::load re-derives
// them; saving them would restore a soundscape under an _environment the load
// has just cleared, so nothing would own it or be able to end it.
void StreamManager::synchronize(Common::Serializer &ser) {
	if (ser.getVersion() < 8) {
		if (ser.isLoading()) {
			endAll();
		}

		return;
	}

	Common::Array<Stream *> persisted;
	if (ser.isSaving()) {
		for (uint i = 0; i < _streams.size(); ++i) {
			if (!_streams[i]->_ambient) {
				persisted.push_back(_streams[i]);
			}
		}
	}

	uint16 count = persisted.size();
	ser.syncAsUint16LE(count);

	if (ser.isLoading()) {
		endAll();

		for (uint i = 0; i < count; ++i) {
			Common::String name, script;
			uint16 sceneID = kNoScene;
			uint16 returnScene = kNoScene;
			byte kind = Stream::kConcurrent;
			ser.syncString(name);
			ser.syncString(script);
			ser.syncAsUint16LE(sceneID);
			ser.syncAsByte(kind);
			ser.syncAsUint16LE(returnScene);

			if (kind == Stream::kViewport) {
				// The main flow is already being restored onto the close-up's
				// scene, so only the bookkeeping that lets it be closed again
				// has to come back - re-running begin() would change the scene
				// out from under the load.
				Stream *stream = new Stream(name, Stream::kViewport);
				stream->_script = script;
				stream->_sceneID = sceneID;
				stream->_returnScene.sceneID = returnScene;
				stream->_returnScene.continueSceneSound = kContinueSceneSound;
				++stream->_sceneChangeCount;
				_streams.push_back(stream);
			} else {
				Stream *stream = new Stream(name, Stream::kConcurrent);
				// countVisit false: Scene::synchronize has already restored the
				// scene counts from this same file a few fields earlier, and the
				// visit that put this stream on this script is part of them.
				// Counting it again would make each load inflate the count, which
				// is the same class of drift as the merge this fixes.
				if (loadScript(*stream, script, sceneID, false)) {
					_streams.push_back(stream);
				} else {
					delete stream;
				}
			}
		}

		return;
	}

	for (uint i = 0; i < count; ++i) {
		byte kind = (byte)persisted[i]->_kind;
		uint16 returnScene = persisted[i]->_returnScene.sceneID;
		ser.syncString(persisted[i]->_name);
		ser.syncString(persisted[i]->_script);
		ser.syncAsUint16LE(persisted[i]->_sceneID);
		ser.syncAsByte(kind);
		ser.syncAsUint16LE(returnScene);
	}
}

} // End of namespace Nancy
