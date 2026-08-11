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

// ---------------------------------------------------------------------------
// The Nancy16 stream runtime.
//
// Nancy16 does not run one script at a time. It runs several *streams*, each of
// which is an independent script player: a name, the script it is on, and that
// script's action records executing on their own. The flow the player navigates
// is itself a stream, and the data calls it "MainStream". Four action record
// types address the mechanism:
//
//   26  "Begin a stream"                       starts a named stream on a script
//   27  "End a stream"                         ends one, by name or "the one I'm in"
//   93  "Detecting main stream scene changes"  raises a flag when a stream moves
//   21  "Scene Change with Frame and Stream"   changes scene *inside* a named stream
//
// MEASURED, and this is what pins the whole model down: the non-scene scripts
// in the cif tree use the same four records for things a scene change plainly
// cannot express.
//
//   * ONBECOMENANCY starts "Journal_Nancy_Stream" on the script `Journal_Nancy`
//     and "Tasklist_Nancy_Stream" on `Tasklist_Nancy`, then ends itself with a
//     type 27 that names no stream. ONLEAVENANCY ends both by name and then
//     ends itself the same way. So a stream target is any script member, not
//     only an `S<digits>` scene, and "end the stream I'm in" is how a one-shot
//     script terminates.
//   * 20 of the 22 ENVS environments name an `*_AMB_SFX` script, and 19 of
//     those hold a type 26: `ALT_AMB_SFX` starts "ALT_Random_sfx" on s2009,
//     which is the scene literally described "ALT random sfx" and which holds
//     one of the 29 type 93 records. So the ambient soundscape of every area in
//     the game is a stream. (An earlier note here said the script's "only job"
//     was that one record. It is not - see the ambient-environment section
//     below; the scripts also hold the looping bed and, in RED, nine polling
//     type 90 records.)
//   * S3199 ("OFF", no background) is started as the stream "OFF_Hide" from
//     S2962 and, once its door-open sounds have finished, uses a type 21 to
//     move *MainStream* to s2960. A stream drives the player's flow; it is not
//     a place the player goes.
//
// TWO KINDS OF STREAM
// -------------------
// The 43 in-scene type 26 records split cleanly on whether the target script
// has viewport artwork, and the two halves need different runtimes:
//
//   * kConcurrent - the target's summary names NO_BG. The stream gets its own
//     ActionManager and runs alongside the main flow every frame. It has no
//     viewport, no scene sound and no input. 16 of the 43 records, plus all 19
//     ambient ones.
//   * kViewport - the target names real artwork (OFF_Filing_Cabinets,
//     TUN_WaterFlowChartCU_TXT, ...). These cannot be concurrent: there is only
//     one viewport. They are close-ups, and the data says so - every one of the
//     four targets contains exactly one type 91 hotspot (a wide exit strip, the
//     system exit cursor) that raises a generic flag, and one type 27 gated on
//     that flag which ends the same stream. Click in, click out. The runtime
//     hands the main flow to the target and remembers where to go back to.
//     27 of the 43 records ("OFFPush" x9, "TUNNEL FLOW CHART" x18).
//
// The split is decided by reading the target's own TSUM at the moment the
// stream starts, not from a mode byte, because a mode byte would only ever be a
// correlation: `mode[1]`, the byte that correlates 43/43 in-scene, is 1 on
// OFF_EXTERIOR_AMB_SFX too, whose target s2909 has no artwork at all.
//
// PER-STREAM SCRATCH FLAGS
// ------------------------
// Event flags 1010-1059 are per-scene scratch, cleared on every scene change.
// They are per-*flow*, not global. S5400 (the Venice map) starts "VillianChase"
// on s5450 and, with no dependencies, raises 1010 itself at the end of its own
// Fade record; s5450 contains a type 93 that raises 1010 and a type 15 scene
// change gated on it. Shared, the chase would restart on its first frame,
// forever. Split, each flow sees only its own - which is also what lets s3199
// raise story flag 2374 and have the whole game see it, since only the
// 1010-1059 range is redirected. (No stream script uses 1041-1059: the 48 scenes
// that touch that sub-range contain none of the stream scripts.)
//
// The answer to "whose scene change clears the scratch flags" is therefore:
// each flow clears its own, and neither clears the other's.
//
// THE AMBIENT ENVIRONMENT (Nancy16 ENVS)
// --------------------------------------
// A stream is also how a location gets its soundscape, and that half is not
// started by any action record: nothing in the 14143-record corpus loads an
// `*_AMB_SFX` script. The ENVS boot chunk names one per environment, and the
// engine has to run it while the player is in that environment.
//
// The chain, MEASURED from the cif tree:
//
//   scene summary sound name  ->  ENVS.sceneCode  ->  ENVS.ambientSoundName
//        "OFF_EXT_INT"              "OFF_EXT_INT"       "OFF_EXT_INT_Amb_sfx"
//
// The script that names is a NO_BG script - so a concurrent stream - holding
//
//   * the environment's looping ambient *bed* (a type 145 PlaySound with
//     numLoops 0, on channel 6, 10 or 21), usually gated on a type 17
//     "that channel is silent" dependency and sometimes on a story flag;
//   * one type 26 that starts a second concurrent stream, `<code>_Random_sfx`,
//     on the scene described "<code> random sfx" (s2009, s3509, s6009, ...).
//     That scene is the random-stinger picker: N type 145 records each behind a
//     type 20 kRandom roll and a type 17 "channel free", plus the type 93 /
//     type 15 pair that makes it reload itself on every main-flow scene change.
//     So one dice roll per player move, which is what "random sfx" means;
//   * in RED_AMB_SFX, nine type 90 records that poll story conditions - the
//     environment script doubles as the Red Room's housekeeping.
//
// Both halves are marked ambient (`Stream::isAmbient`), which buys two things:
//
//   * the sound channels they start are exempt from
//     SoundManager::stopAndUnloadSceneSpecificSounds(), which stops channels
//     0-13 on every kLoadSceneSound scene change. The beds live on 6/10/21 and
//     the records are kOneShot, so without the exemption the bed would be
//     silenced by the first scene change inside its own environment and the
//     record would already be _isDone. Ambience that dies the moment you turn
//     around is the bug this whole path exists to fix;
//   * they are left out of the save. Ambience is a pure function of the
//     environment, so a load re-derives it from the restored scene instead of
//     restoring stale streams that nothing would then own.
//
// Entry and exit are driven from Scene::load: the environment is re-resolved on
// every main scene load, and setEnvironment() is a no-op unless the code
// actually changed. A scene whose summary sound names no ENVS entry ("NO SOUND"
// and the 148 blank ones, which is every conversation and most close-ups)
// leaves the environment alone rather than clearing it - that is what makes the
// ambience survive a conversation.
//
// WHAT IS NOT MODELLED
// --------------------
//   * Concurrent streams get no input. None of the concurrent targets in this
//     game holds a hotspot-bearing record; the ones that do are the kViewport
//     close-ups, and those get input because the main flow is showing them.
//   * Concurrent streams have no scene summary: no scene sound, no panorama, no
//     listener position. Their targets are backgroundless by construction.
//   * Nothing ever starts a stream called "MainStream", and nothing ends one.
//     The main flow is modelled as itself and is not a Stream object, so no
//     record can take the game down by ending it.
// ---------------------------------------------------------------------------

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

	// True for the ENVS ambient script and everything it starts. See the
	// "ambient environment" note above: it exempts the stream's sound channels
	// from the per-scene-change stop and keeps it out of the save.
	bool isAmbient() const { return _ambient; }
	uint16 getSceneID() const { return _sceneID; }
	const Common::String &getScriptName() const { return _script; }

	// Bumped every time this stream moves to a new script. Type 93 watches it.
	uint32 getSceneChangeCount() const { return _sceneChangeCount; }

	// The stream's own scene clock, reset every time the stream changes script.
	// kElapsedSceneTime dependencies inside a stream measure against this, for
	// the same reason the scratch flags are split.
	Time getSceneTime() const { return _sceneTime; }

	// A scene change made by one of this stream's records moves the stream, not
	// the player.
	void requestSceneChange(const SceneChangeDescription &desc) { _nextScene = desc; }
	bool hasPendingSceneChange() const { return _nextScene.sceneID != kNoScene; }

	// Per-stream copy of the 1010-1040 scratch flags; see the note above.
	void setScratchFlag(uint index, byte value);
	byte getScratchFlag(uint index) const;
	void clearScratchFlags();

	// Every mixer channel a sound started while this stream was executing, so
	// ending the stream can silence them. See StreamManager::end().
	void noteSoundChannel(uint16 channelID);

private:
	Common::String _name;
	Kind _kind = kConcurrent;

	Common::String _script;			// Cif member name: "S3665", "Journal_Nancy"
	uint16 _sceneID = kNoScene;		// The numeric id when _script is an S<digits> scene

	SceneChangeDescription _nextScene;
	SceneChangeDescription _returnScene;	// kViewport only: where ending it goes

	uint32 _sceneChangeCount = 0;

	// The main flow's scene-change counter as it stood when this stream's
	// current script was loaded. A type 93 in the script is asking "has the
	// watched flow moved since I came into being", and its first execute() can
	// be a frame or two late - a stream started on the same frame the player
	// clicks a map location would otherwise never see that click.
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

	// Starts (or re-targets) the named stream on `scriptName`. sceneID is the
	// numeric id when the script is an S<digits> scene, kNoScene otherwise.
	// Returns null when the script cannot be loaded.
	Stream *begin(const Common::String &name, const Common::String &scriptName,
			uint16 sceneID, uint16 frameID, uint16 continueSceneSound);

	// Ends a stream by name. An empty name means "the stream this record belongs
	// to": the executing concurrent stream, or - in the main flow - the kViewport
	// stream whose scene the main flow is currently showing. Never the main flow
	// itself. Returns true when a stream was actually ended.
	bool end(const Common::String &name);
	void endAll();

	// The ENVS environment the player is currently in, by scene-prefix code
	// ("ALT", "OFF_EXT_INT", ...). Empty before the first resolvable scene.
	const Common::String &getEnvironment() const { return _environment; }

	// Called from Scene::load once per main scene load, with the environment the
	// new scene belongs to and the script ENVS names as its ambience. A no-op
	// when the code has not changed, so walking around inside one location does
	// not retrigger anything. An empty `ambientScript` (the UI and LET
	// environments) just ends the current ambience.
	void setEnvironment(const Common::String &code, const Common::String &ambientScript);

	// Ends the ambient script and every stream it started, silencing their
	// channels the same way end() does.
	void endAmbient();

	Stream *find(const Common::String &name) const;
	uint size() const { return _streams.size(); }

	// The concurrent stream whose records are executing right now, or null when
	// the main flow is running.
	Stream *executing() const { return _executing; }

	// Type 93's observable. "MainStream" and the empty name mean the main flow.
	// An unknown name returns 0 forever, so a watcher on a stream this runtime
	// never creates simply never fires.
	uint32 getSceneChangeCount(const Common::String &streamName) const;

	// The value getSceneChangeCount would have returned when the executing
	// stream's script was loaded. Type 93 compares against this rather than
	// against whatever it happens to read on its first frame.
	uint32 getWatchBaseline(const Common::String &streamName) const;

	void process();

	// Called from Scene::load once the new main scene is in place.
	void onMainSceneLoaded(uint16 sceneID);
	void onPause(bool pause);

	// Routes a scene change made from inside a concurrent stream to that stream.
	// Returns true when the change was consumed and must not touch the main flow.
	bool redirectSceneChange(const SceneChangeDescription &desc);

	// Records a mixer channel against the executing stream. No-op in the main
	// flow.
	void noteSoundChannel(uint16 channelID);

	void synchronize(Common::Serializer &ser);

	// One line per running stream, for the debug fingerprint and the console.
	Common::String describe() const;

	// Reads a script's summary chunk and reports whether it has viewport
	// artwork. `exists` comes back false when there is no such script.
	static bool scriptIsBackgroundless(const Common::String &scriptName, bool &exists);

	// Drops any Scene-held pointer into the given records before they are freed.
	static void detachScenePointers(Action::ActionManager &manager);

private:
	// countVisit false suppresses the scene-count bump below. A load has just
	// restored the counts from the file; re-entering the scripts the save says
	// its streams were on must not add to them, or every load would inflate
	// each running stream's scene by one.
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
