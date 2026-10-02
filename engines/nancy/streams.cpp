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

// The scratch flag range comes from the static data (1010-1059 in Nancy16)
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

// Scene keeps bare pointers to the active movie and conversation, which a
// stream's records can set. Clear them before those records are freed.
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

	// A script with no summary cannot own a viewport, so it runs concurrently
	bool backgroundless = true;

	if (summary) {
		// Both chunk shapes open with a 0x32-byte description followed by the
		// background video's filename.
		summary->seek(0x32);
		Common::String videoFile;
		readFilename(*summary, videoFile);

		// An empty video name is not artwork either (S2605, S4708, S5006)
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

	// Entering a scene counts as a visit, since some stream scenes are only ever
	// entered by their stream. Not on the load path.
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
		// The main flow is not a Stream object and cannot be restarted
		warning("Refusing to begin a stream called MainStream on %s", scriptName.c_str());
		return nullptr;
	}

	bool exists = false;
	const bool concurrent = scriptIsBackgroundless(scriptName, exists);
	if (!exists) {
		warning("Stream '%s' names script %s, which does not exist", name.c_str(), scriptName.c_str());
		return nullptr;
	}

	// A stream started from inside the ambient script belongs to the ambience
	const bool ambient = _executing && _executing->_ambient;

	Stream *existing = find(name);
	if (existing) {
		// Re-target rather than stack a second copy, since the player can re-enter
		// the scene that starts the stream.
		if (existing->_kind == Stream::kConcurrent && concurrent) {
			existing->_ambient = existing->_ambient || ambient;

			if (!existing->_script.equalsIgnoreCase(scriptName)) {
				if (existing == _executing) {
					// A record retargeting its own stream. loadScript() would free the
					// records still being walked, so defer it to process().
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

	// A stream on a script with viewport artwork is a close-up shown by the main flow
	if (ambient) {
		// The ambience is never a close-up
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
		// An empty name ends the stream this record belongs to: the executing
		// concurrent stream, or in the main flow the close-up currently shown.
		// The main flow itself is never ended.
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
		// Hand the viewport back. The return scene is kept on the stream rather
		// than pushed with Scene::pushScene, so it cannot corrupt that stack.
		SceneChangeDescription back = target->_returnScene;
		destroy(target);

		if (back.sceneID != kNoScene) {
			NancySceneState.changeScene(back, true);
		}

		return true;
	}

	// Ending a stream silences the sounds it started; the data relies on this
	// (S3502 ends the phone-ringing streams without stopping their channel).
	Common::String stopped;
	for (uint i = 0; i < target->_soundChannels.size(); ++i) {
		stopped += Common::String::format("%s%u", i ? "," : "", target->_soundChannels[i]);
		g_nancy->_sound->stopSound(target->_soundChannels[i]);
	}

	if (!stopped.empty()) {
		debugC(0, kDebugScene, "Stream '%s' silenced channels %s", target->_name.c_str(), stopped.c_str());
	}

	if (target == _executing) {
		// The manager's records are mid-loop; let process() reap it afterwards.
		target->_ended = true;
		return true;
	}

	destroy(target);
	return true;
}

void StreamManager::endAll() {
	for (uint i = 0; i < _streams.size(); ++i) {
		// Ambient channels are exempt from the per-scene-change stop, so silence
		// them here or the old bed keeps looping after a load.
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
	// Collect first: end() mutates _streams. end() also silences the looping beds.
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
		// Same location; do not restart the ambience.
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

	// Use an engine-owned stream name, since data-defined names can collide
	// (OFF_EXTERIOR_AMB_SFX starts a stream named after itself).
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

	// A viewport stream may span several scenes. Keep its return path while
	// navigating inside the close-up.
	for (int i = (int)_streams.size() - 1; i >= 0; --i) {
		Stream *stream = _streams[i];
		if (stream->_kind != Stream::kViewport || stream->_sceneID == sceneID)
			continue;

		if (stream->_returnScene.sceneID == sceneID) {
			destroy(stream);
		} else {
			stream->_sceneID = sceneID;
			stream->_script = Common::String::format("S%u", sceneID);
			++stream->_sceneChangeCount;
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

// Only the names and scripts are saved; records are rebuilt by reloading the
// script. Ambient streams are not saved, since Scene::load re-derives them.
void StreamManager::synchronize(Common::Serializer &ser) {
	if (ser.getVersion() < 13) {
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
				// The main flow is already being restored onto the close-up's scene, so
				// only the bookkeeping that lets it be closed again is needed.
				Stream *stream = new Stream(name, Stream::kViewport);
				stream->_script = script;
				stream->_sceneID = sceneID;
				stream->_returnScene.sceneID = returnScene;
				stream->_returnScene.continueSceneSound = kContinueSceneSound;
				++stream->_sceneChangeCount;
				_streams.push_back(stream);
			} else {
				Stream *stream = new Stream(name, Stream::kConcurrent);
				// Scene::synchronize already restored the scene counts, so do not count
				// this visit again.
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
