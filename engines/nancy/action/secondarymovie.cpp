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
#include "engines/nancy/trace.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/util.h"
#include "engines/nancy/video.h"

#include "engines/nancy/action/secondarymovie.h"
#include "engines/nancy/state/scene.h"

#include "common/random.h"
#include "common/serializer.h"
#include "common/system.h"

#include "video/bink_decoder.h"

namespace Nancy {
namespace Action {

PlaySecondaryMovie::PlaySecondaryMovie(bool isRandom)
		: RenderActionRecord(8), _isRandom(isRandom) {
	if (_isRandom) {
		NancySceneState.notifyRandomMovieARLoaded();
	}
}


PlaySecondaryMovie::~PlaySecondaryMovie() {
	if (NancySceneState.getActiveMovie() == this) {
		NancySceneState.setActiveMovie(nullptr);
	}

	if (_playerCursorAllowed == kNoPlayerCursorAllowed) {
		g_nancy->setMouseEnabled(true);
	}
}

bool PlaySecondaryMovie::survivesSceneChange(bool nextSceneIsNoArt) const {
	// Nancy11's random movies can be ambient loops that intentionally keep
	// playing across scene changes. Nancy13's per-character reaction movies
	// (AR 42) are scene-local: they must stop when their scene is left, and are
	// reloaded if it's re-entered. A plain (non-random) cinematic movie is
	// self-contained and does not persist, not even into a NO_ART_SCENE — so the
	// NO_ART flag is deliberately ignored here.
	return _isRandom && g_nancy->getGameType() < kGameTypeNancy13 && !_isDone && !_randomStopRequested;
}

void PlaySecondaryMovie::handleInput(NancyInput &input) {
	// The character's box (set as the hotspot while it is on screen) is
	// clickable; clicking opens its conversation scene, and hovering drives the
	// recognition movie. The talk hover cursor is applied by ActionManager via
	// getHoverCursor().
	if (!_hasHotspot || _talkSceneID == kNoScene) {
		_isHovered = false;
		return;
	}

	_isHovered = NancySceneState.getViewport().convertViewportToScreen(_hotspot).contains(input.mousePos);

	if (_isHovered && (input.input & NancyInput::kLeftMouseButtonUp)) {
		input.eatMouseInput();
		SceneChangeDescription desc;
		desc.sceneID = _talkSceneID;
		NancySceneState.changeScene(desc);
	}
}

CursorManager::CursorType PlaySecondaryMovie::getHoverCursor() const {
	// The character's own cursor type (a raw Nancy13 cursor id) comes from the
	// secondary record; cursorSetFromScript() routes it through the raw-slot path.
	return (CursorManager::CursorType)_talkCursorType;
}

void PlaySecondaryMovie::readRandomSequence(Common::Serializer &ser, RandomSequence &seq) {
	readFilename(ser, seq.name);
	ser.syncAsUint16LE(seq.startFrame);
	ser.syncAsUint16LE(seq.lastFrame);
	ser.syncAsSint32LE(seq.minPauseMs);
	ser.syncAsSint32LE(seq.maxPauseMs);

	if (g_nancy->getGameType() >= kGameTypeNancy13) {
		// Percent (0-100) chance to stay on this sequence and pause, rather
		// than transition to one of the next sequences.
		byte pauseChance = 0;
		ser.syncAsByte(pauseChance);
		seq.stayWeight = pauseChance;
	} else {
		ser.syncAsUint16LE(seq.stayWeight);
	}

	uint16 nextCount = 0;
	ser.syncAsUint16LE(nextCount);

	seq.nextSequences.resize(nextCount);
	for (uint i = 0; i < nextCount; ++i) {
		readFilename(ser, seq.nextSequences[i].name);
		ser.syncAsUint16LE(seq.nextSequences[i].weight);
	}
}

void PlaySecondaryMovie::readSecondaryRandomMovie(Common::Serializer &ser, RandomSequence &seq) {
	// Nancy13's random-movie chunk carries one extra "secondary" movie record
	// (a character's recognition animation) between the sequence list and the
	// hotspot list: a name + 5 uint16 + a next-list. The 4th uint16 is the scene
	// to open when the character is clicked (its conversation); 9999 means the
	// character isn't clickable. The whole record must be consumed here or the
	// hotspot list below misaligns.
	readFilename(ser, seq.name);
	ser.syncAsUint16LE(seq.startFrame);
	ser.syncAsUint16LE(seq.lastFrame);
	ser.syncAsUint16LE(_talkCursorType);	// hover cursor for the character
	ser.syncAsUint16LE(_talkSceneID);
	ser.skip(2);	// conversation frameID (0 in known data)

	uint16 nextCount = 0;
	ser.syncAsUint16LE(nextCount);
	seq.nextSequences.resize(nextCount);
	for (uint i = 0; i < nextCount; ++i) {
		readFilename(ser, seq.nextSequences[i].name);
		ser.syncAsUint16LE(seq.nextSequences[i].weight);
	}
}

void PlaySecondaryMovie::readRandomMovieData(Common::Serializer &ser, Common::SeekableReadStream &stream) {
	readFilename(ser, _startingSequenceName);
	ser.syncAsUint16LE(_randomPlayerCursorAllowed);

	uint16 sequenceCount = 0, hotspotCount = 0;

	if (g_nancy->getGameType() >= kGameTypeNancy13) {
		// Nancy13 replaced the inline hotspot count with two header fields (a
		// flag tested against 1, and a u16); the hotspot list is now length-
		// prefixed after the sequences instead.
		ser.skip(2);
		ser.skip(2);
		ser.syncAsUint16LE(sequenceCount);
	} else {
		ser.syncAsUint16LE(sequenceCount);
		ser.syncAsUint16LE(hotspotCount);
	}

	_sequences.resize(sequenceCount);
	for (uint i = 0; i < sequenceCount; ++i) {
		readRandomSequence(ser, _sequences[i]);
	}

	if (g_nancy->getGameType() >= kGameTypeNancy13) {
		readSecondaryRandomMovie(ser, _secondaryMovie);
		ser.syncAsUint16LE(hotspotCount);
	}

	_videoDescs.resize(hotspotCount);
	for (uint i = 0; i < hotspotCount; ++i) {
		_videoDescs[i].readData(stream);
	}

	applyStartingRandomSequence();
}

// Nancy14+ random-movie layout (verified byte-identical in Nancy14 and Nancy15).
// The header grew to mirror the non-random AR (videoFormat / visibility / cursor
// / sceneID / frameID, plus two currently unmapped u16s and a per-movie volume
// byte). The sequence records are unchanged. The tail is a blt-descriptor list
// for the main movie, then the recognition ("secondary") movie's name and its
// own blt-descriptor list, in place of Nancy13's secondaryMovie record + hotspot
// list.
void PlaySecondaryMovie::readRandomMovieDataNancy14(Common::Serializer &ser, Common::SeekableReadStream &stream) {
	readFilename(ser, _startingSequenceName);

	ser.syncAsUint16LE(_videoFormat);
	_videoFormat = kLargeVideoFormat;
	ser.skip(2);	// Visibility frame ID; ScummVM drives visibility from the videoDescs
	ser.syncAsUint16LE(_randomPlayerCursorAllowed);
	ser.skip(4);	// Two u16s (object offsets 0x8c / 0xe7); purpose not yet mapped
	ser.syncAsSint16LE(_sceneChange.sceneID);
	ser.syncAsUint16LE(_sceneChange.frameID);
	ser.skip(1);	// Per-movie volume byte (movie sound off since Nancy6)

	uint16 sequenceCount = 0;
	ser.syncAsUint16LE(sequenceCount);
	_sequences.resize(sequenceCount);
	for (uint i = 0; i < sequenceCount; ++i) {
		readRandomSequence(ser, _sequences[i]);
	}

	// Main movie blt/hotspot descriptors.
	uint16 numVideoDescs = 0;
	ser.syncAsUint16LE(numVideoDescs);
	_videoDescs.resize(numVideoDescs);
	for (uint i = 0; i < numVideoDescs; ++i) {
		_videoDescs[i].readData(stream);
	}

	// Recognition ("secondary") movie: its name followed by its own blt
	// descriptors. Stored for future playback; the descriptors are consumed to
	// keep the stream aligned (no home in the struct yet).
	readFilename(ser, _secondaryMovie.name);
	uint16 numSecondaryDescs = 0;
	ser.syncAsUint16LE(numSecondaryDescs);
	for (uint i = 0; i < numSecondaryDescs; ++i) {
		SecondaryVideoDescription unused;
		unused.readData(stream);
	}

	applyStartingRandomSequence();
}

// Nancy16's "Random Movie/Fidget" record, carried by both AR 42 and AR 43 with
// one layout - a Markov chain over idle clips. Measured on all 40 records
// (29 of type 43, 11 of type 42), consumed byte-exactly:
//
//   char[33] label ("", "NancyDancy", "SerenadeVids")
//   char[33] starting sequence, or "random_movie"/"RANDOM_MOVIE" for "any"
//   uint16   draw order (10, 11 or 14)
//   uint16   video format (1 or 2)
//   int16    -1 in all 40 records
//   int16    a scene-change selector; -1 exactly when there is no scene change
//   uint16   scene ID, 32767 for none; uint16 frame ID, 0 in all 40
//   byte     per-movie volume (85, or 55 on the two ALT records)
//   uint16   sequence count, then that many sequence records
//   uint16   blt-descriptor count, then that many descriptors
//
// The sequence record is Nancy14's with one extra int16 in front of the frame
// range (-1 in 238 of 240 blocks, 2650 in the other two), so the -1/-2 "play the
// whole clip" sentinels still land on startFrame/lastFrame and resolve in
// resolveSentinelFrames() as before. That int16 is an event flag the sequence
// raises when it starts: the two records carrying 2650 (EV_Fango_InRoot) are in
// S2066 and S2072 and carry it on their STARTING sequence "off_c1root_anim", and
// 2650 is the sole event-flag gate on the only transition out of S2066.
// Measured: booting S2066 with the flag injected runs S2066 -> S2036 (which sets
// 2473 = EV_Saw_Fango_Pigeon, the flag that opens the Venice map) -> S2013 ->
// S1404 -> S1405 -> S3511; booting it without the flag sits in S2066 for 70
// seconds with no flags set at all.
void PlaySecondaryMovie::readRandomMovieDataNancy16(Common::Serializer &ser, Common::SeekableReadStream &stream) {
	Common::String label;
	readFilename(ser, label);
	readFilename(ser, _startingSequenceName);

	uint16 z = 0;
	ser.syncAsUint16LE(z);
	_z = z;

	ser.syncAsUint16LE(_videoFormat);
	ser.skip(2);	// -1 in all 40 records
	ser.skip(2);	// scene-change selector; the sceneID below already says whether there is one

	ser.syncAsSint16LE(_sceneChange.sceneID);
	ser.syncAsUint16LE(_sceneChange.frameID);

	if (_sceneChange.sceneID == kNancy16NoScene) {
		_sceneChange.sceneID = kNoScene;
	}

	byte movieVolume = 0;
	ser.syncAsByte(movieVolume);

	uint16 sequenceCount = 0;
	ser.syncAsUint16LE(sequenceCount);
	_sequences.resize(sequenceCount);
	for (uint i = 0; i < sequenceCount; ++i) {
		readFilename(ser, _sequences[i].name);
		ser.syncAsSint16LE(_sequences[i].raiseFlag);
		ser.syncAsUint16LE(_sequences[i].startFrame);
		ser.syncAsUint16LE(_sequences[i].lastFrame);
		ser.syncAsSint32LE(_sequences[i].minPauseMs);
		ser.syncAsSint32LE(_sequences[i].maxPauseMs);

		byte pauseChance = 0;
		ser.syncAsByte(pauseChance);
		_sequences[i].stayWeight = pauseChance;

		uint16 nextCount = 0;
		ser.syncAsUint16LE(nextCount);
		_sequences[i].nextSequences.resize(nextCount);
		for (uint j = 0; j < nextCount; ++j) {
			readFilename(ser, _sequences[i].nextSequences[j].name);
			ser.syncAsUint16LE(_sequences[i].nextSequences[j].weight);
		}
	}

	uint16 numVideoDescs = 0;
	ser.syncAsUint16LE(numVideoDescs);
	_videoDescs.resize(numVideoDescs);
	for (uint i = 0; i < numVideoDescs; ++i) {
		_videoDescs[i].readData(stream);
	}

	applyStartingRandomSequence();
}

void PlaySecondaryMovie::applyStartingRandomSequence() {
	// "RandomMovie" picks any sequence; otherwise look up by name.
	// Only the first sequence is played; chained playback is TODO.
	if (!_sequences.empty()) {
		int startIdx = -1;
		// Nancy16 spells the "any sequence" sentinel "random_movie" (and
		// "RANDOM_MOVIE" on the two ALT records).
		if (_startingSequenceName.equalsIgnoreCase("RandomMovie") ||
				_startingSequenceName.equalsIgnoreCase("random_movie")) {
			startIdx = g_nancy->_randomSource->getRandomNumber(_sequences.size() - 1);
		} else {
			for (uint i = 0; i < _sequences.size(); ++i) {
				if (_sequences[i].name.toString() == _startingSequenceName) {
					startIdx = (int)i;
					break;
				}
			}
			if (startIdx < 0) {
				warning("PlayRandomMovie: starting sequence \"%s\" is not part of this AR",
					_startingSequenceName.c_str());
			}
		}

		if (startIdx >= 0) {
			const RandomSequence &src = _sequences[startIdx];
			_activeSequenceIndex = startIdx;
			_videoName = src.name;
			_firstFrame = src.startFrame;
			_lastFrame = src.lastFrame;
			_videoFormat = kLargeVideoFormat;
			_videoSceneChange = kMovieNoSceneChange;
			_playerCursorAllowed = (byte)_randomPlayerCursorAllowed;
			_playDirection = kPlayMovieForward;
		}
	}

	_sound.name = "NO SOUND";
}

void PlaySecondaryMovie::raiseSequenceFlag(int index) {
	if (index < 0 || index >= (int)_sequences.size()) {
		return;
	}

	const int16 label = _sequences[index].raiseFlag;
	if (label <= kEvNoEvent) {
		return;
	}

	debugC(1, kDebugScene, "PlayRandomMovie: sequence \"%s\" raises flag %d (%s)",
		_sequences[index].name.toString().c_str(), label,
		g_nancy->getEventFlagName(label).c_str());
	NancySceneState.setEventFlag(label, g_nancy->_true);
}

bool PlaySecondaryMovie::activateRandomSequence(int index) {
	if (index < 0 || index >= (int)_sequences.size()) {
		return false;
	}

	raiseSequenceFlag(index);

	const RandomSequence &src = _sequences[index];
	_activeSequenceIndex = index;
	_videoName = src.name;
	_firstFrame = src.startFrame;
	_lastFrame = src.lastFrame;

	if (!_decoder.loadFile(_videoName)) {
		warning("PlayRandomMovie: couldn't load %s", _videoName.toString().c_str());
		return false;
	}

	resolveSentinelFrames();

	_isFinished = false;
	_curViewportFrame = -1;	// force visibility re-evaluation next tick
	return true;
}

bool PlaySecondaryMovie::activateSecondaryMovie() {
	_videoName = _secondaryMovie.name;
	_firstFrame = _secondaryMovie.startFrame;
	_lastFrame = _secondaryMovie.lastFrame;

	if (!_decoder.loadFile(_videoName)) {
		warning("PlayRandomMovie: couldn't load recognition movie %s", _videoName.toString().c_str());
		return false;
	}

	resolveSentinelFrames();

	_isFinished = false;
	_curViewportFrame = -1;
	return true;
}

void PlaySecondaryMovie::resolveSentinelFrames() {
	// Random sequences use -1/-2 for the start/last frame to mean "play the
	// movie's own first/last frame". Resolve them now that the decoder (and
	// thus the real frame count) is available.
	if (_firstFrame == 0xFFFF) {
		_firstFrame = 0;
	}
	if (_lastFrame == 0xFFFE || _lastFrame == 0xFFFF) {
		int frameCount = _decoder.getFrameCount();
		_lastFrame = frameCount > 0 ? (uint16)(frameCount - 1) : 0;
	}
}

void PlaySecondaryMovie::playRandomSequence() {
	if (!_isRandom || _sequences.empty()) {
		return;
	}
	int picked = g_nancy->_randomSource->getRandomNumber(_sequences.size() - 1);
	_randomChainState = kRandomPlaying;
	_randomStopRequested = false;
	activateRandomSequence(picked);
}

int PlaySecondaryMovie::beginRandomPause(const RandomSequence &seq) {
	int32 pauseMs = seq.minPauseMs;
	if (seq.maxPauseMs > seq.minPauseMs) {
		pauseMs += g_nancy->_randomSource->getRandomNumber(seq.maxPauseMs - seq.minPauseMs - 1);
	}
	_randomPauseEndTime = g_system->getMillis() + (uint32)MAX<int32>(0, pauseMs);
	_randomChainState = kRandomPaused;

	// Nancy16's pause is the gap between two fidgets of a character who is
	// standing right there in the shot, so it holds the clip's last frame.
	// Blanking it makes the office worker flicker out of existence between
	// sips of his drink.
	if (g_nancy->getGameType() < kGameTypeNancy16) {
		setVisible(false);
	}

	_decoder.pauseVideo(true);
	return -1;
}

// The weighted pick among a sequence's successors, without the "stay here"
// roll that precedes it in the Nancy13 chain.
int PlaySecondaryMovie::pickSuccessorSequence(const RandomSequence &seq) {
	if (seq.nextSequences.empty()) {
		return -1;
	}

	// All-EQUAL_CHANCE lists (the eight Serenade vignettes) are a uniform pick;
	// otherwise the weights are percentages summing to 100.
	const bool equalChance = seq.nextSequences[0].weight == 0xFFFF;
	const uint step = 100 / seq.nextSequences.size();
	const uint roll = g_nancy->_randomSource->getRandomNumber(99);
	uint cumulative = 0;

	for (uint i = 0; i < seq.nextSequences.size(); ++i) {
		if (i == seq.nextSequences.size() - 1) {
			cumulative = 100;
		} else {
			cumulative += equalChance ? step : seq.nextSequences[i].weight;
		}

		if (roll < cumulative) {
			return lookupSequence(seq.nextSequences[i].name);
		}
	}

	return -1;
}

int PlaySecondaryMovie::lookupSequence(const Common::Path &name) const {
	for (uint j = 0; j < _sequences.size(); ++j) {
		if (_sequences[j].name == name) {
			return (int)j;
		}
	}
	warning("PlayRandomMovie: next-sequence \"%s\" not part of this AR", name.toString().c_str());
	return -1;
}

int PlaySecondaryMovie::rollNextSequence() {
	if (_activeSequenceIndex < 0 || _activeSequenceIndex >= (int)_sequences.size()) {
		return -1;
	}

	const RandomSequence &seq = _sequences[_activeSequenceIndex];

	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		// Nancy16 uses the same byte, but as "chance to pause before moving on"
		// rather than "chance to stay here". It has to be: 40 of the 240 blocks
		// carry 100, and under the stay-here reading a 100 parks the chain on
		// that clip permanently - the office worker in S2066 takes exactly one
		// drink and then never moves again. Read as a pause chance, the two
		// numbers next to it (a min/max delay in ms) are the gap between
		// fidgets, which is what they look like everywhere in the data.
		if (!_randomPauseExpired && seq.stayWeight != 0 &&
				(uint)g_nancy->_randomSource->getRandomNumber(99) < seq.stayWeight) {
			return beginRandomPause(seq);
		}

		_randomPauseExpired = false;

		const int next = pickSuccessorSequence(seq);

		// A terminal clip (the second half of the CAS_ETCONVO pairs) simply
		// keeps going rather than freezing the character.
		return next >= 0 ? next : _activeSequenceIndex;
	}

	if (g_nancy->getGameType() >= kGameTypeNancy13) {
		// Two independent rolls: first a percent chance to stay on this
		// sequence and pause, then a percent-weighted pick among the next
		// sequences (weights sum to 100, or all EQUAL_CHANCE for a uniform pick).
		if (seq.stayWeight != 0 && (uint)g_nancy->_randomSource->getRandomNumber(99) < seq.stayWeight) {
			return beginRandomPause(seq);
		}

		if (seq.nextSequences.empty()) {
			_randomChainState = kRandomPaused;
			_randomPauseEndTime = g_system->getMillis() + 1000;	// re-check in 1s
			return -1;
		}

		const bool equalChance = seq.nextSequences[0].weight == 0xFFFF;
		const uint step = 100 / seq.nextSequences.size();
		uint roll = g_nancy->_randomSource->getRandomNumber(99);
		uint cumulative = 0;
		for (uint i = 0; i < seq.nextSequences.size(); ++i) {
			if (i == seq.nextSequences.size() - 1) {
				cumulative = 100;
			} else {
				cumulative += equalChance ? step : seq.nextSequences[i].weight;
			}
			if (roll < cumulative) {
				return lookupSequence(seq.nextSequences[i].name);
			}
		}

		return -1;
	}

	uint32 totalWeight = seq.stayWeight;
	for (const NextSequenceRef &ns : seq.nextSequences) {
		totalWeight += ns.weight;
	}

	if (totalWeight == 0) {
		// No weights at all: stay indefinitely without a pause.
		_randomChainState = kRandomPaused;
		_randomPauseEndTime = g_system->getMillis() + 1000;	// re-check in 1s
		return -1;
	}

	uint32 roll = g_nancy->_randomSource->getRandomNumber(totalWeight - 1);

	if (roll < seq.stayWeight) {
		return beginRandomPause(seq);
	}

	uint32 cumulative = seq.stayWeight;
	for (uint i = 0; i < seq.nextSequences.size(); ++i) {
		cumulative += seq.nextSequences[i].weight;
		if (roll < cumulative) {
			return lookupSequence(seq.nextSequences[i].name);
		}
	}

	return -1;
}

// Nancy14 compacted the non-random layout: the videoSceneChange 5/6 flag is
// gone (a scene change is now requested via the sceneID sentinel), playDirection
// moved after lastFrame, and a "hide on finish" flag was added. AR 44 matches
// AR 41 plus a trailing movie-volume byte.
//
// Nancy15 adds two things on top: AR 44 gained a "play style" u16 (1/3) after
// the hide-on-finish flag, and the firstFrame field can be -1 (LOOP_RANDOM),
// in which case a min/max loop-count pair follows and a random value in that
// range is chosen.
void PlaySecondaryMovie::readDataNancy14(Common::Serializer &ser, Common::SeekableReadStream &stream) {
	const bool isNancy15 = g_nancy->getGameType() >= kGameTypeNancy15;

	readFilename(ser, _videoName);

	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		// Nancy16 inserted a z-order field ahead of the video format, and
		// replaced firstFrame/lastFrame/playDirection with a five-byte run that
		// is constant across all 593 records. Reading this with the Nancy14
		// layout is actively harmful: _firstFrame lands on -1 in 593/593, which
		// trips the LOOP_RANDOM branch below and desynchronises every record by
		// four bytes.
		uint16 z = 0;
		ser.syncAsUint16LE(z);
		_z = z;

		ser.syncAsUint16LE(_videoFormat);
		ser.syncAsUint16LE(_playerCursorAllowed);
		ser.syncAsUint16LE(_hideOnFinish);
		ser.syncAsUint16LE(_playStyle);

		// 00 ff ff fe ff in 593/593, so the internal boundaries are not
		// observable. Two readings fit equally well - see FORMATS.md - and both
		// mean "play the whole clip forwards", so treat it as that and move on
		// rather than guessing at a split.
		ser.skip(5);
		_firstFrame = 0;
		_playDirection = kPlayMovieForward;

		// The record carries no frame range, and "play the whole clip" is what
		// the constant run means, so the end comes from the decoder in init().
		// Leaving this at 0 stops the movie after its first frame - which is how
		// the HeR intro ended up audible but frozen on a black frame.
		_lastFrame = -1;

		ser.syncAsSint16LE(_sceneChange.sceneID);
		ser.syncAsUint16LE(_sceneChange.frameID);

		// Nancy16's "no scene" sentinel is 0x7fff, not kNoScene
		if (_sceneChange.sceneID == kNancy16NoScene) {
			_sceneChange.sceneID = kNoScene;
		}

		_videoSceneChange = _sceneChange.sceneID != kNoScene ? kMovieSceneChange : kMovieNoSceneChange;

		byte movieVolume = 0;
		ser.syncAsByte(movieVolume);

		uint16 numFrameFlags = 0;
		ser.syncAsUint16LE(numFrameFlags);
		_frameFlags.resize(numFrameFlags);
		for (uint i = 0; i < numFrameFlags; ++i) {
			ser.syncAsSint16LE(_frameFlags[i].frameID);
			ser.syncAsSint16LE(_frameFlags[i].flagDesc.label);
			ser.syncAsUint16LE(_frameFlags[i].flagDesc.flag);
		}

		uint16 numVideoDescs = 0;
		ser.syncAsUint16LE(numVideoDescs);
		_videoDescs.resize(numVideoDescs);
		for (uint i = 0; i < numVideoDescs; ++i) {
			_videoDescs[i].readData(stream);
		}

		_sound.name = "NO SOUND";
		return;
	}

	ser.syncAsUint16LE(_videoFormat);
	_videoFormat = kLargeVideoFormat;

	ser.skip(2);	// Visibility frame ID; ScummVM drives visibility from the videoDescs instead
	ser.syncAsUint16LE(_playerCursorAllowed);
	ser.syncAsUint16LE(_hideOnFinish);

	// AR 44 and its subclass AR 47 read the play-style field (retail "mode 0");
	// AR 41 ("mode 1") does not.
	if (isNancy15 && (_type == 44 || _type == 47)) {
		ser.syncAsUint16LE(_playStyle);
	}

	ser.syncAsUint16LE(_firstFrame);

	if (isNancy15 && (int16)_firstFrame == -1) {
		// LOOP_RANDOM: firstFrame -1 is followed by a min/max loop count; the
		// game picks a random value in [min, max) (min must be < max).
		uint16 minLoops = 0, maxLoops = 0;
		ser.syncAsUint16LE(minLoops);
		ser.syncAsUint16LE(maxLoops);
		_firstFrame = maxLoops > minLoops ?
			(uint16)(minLoops + g_nancy->_randomSource->getRandomNumber(maxLoops - minLoops - 1)) : minLoops;
	}

	ser.syncAsUint16LE(_lastFrame);
	ser.syncAsUint16LE(_playDirection);
	ser.syncAsSint16LE(_sceneChange.sceneID);
	ser.syncAsUint16LE(_sceneChange.frameID);

	_videoSceneChange = _sceneChange.sceneID != kNoScene ? kMovieSceneChange : kMovieNoSceneChange;

	// Per-movie volume; consumed but unused (movie sound is off since Nancy6).
	// AR 44 always carries it. AR 47 does too, but only from Nancy15 - the
	// retail flipped the "mode" convention, and Nancy14's AR 47 omits the byte.
	if (_type == 44 || (_type == 47 && isNancy15)) {
		byte movieVolume = 0;
		ser.syncAsByte(movieVolume);
	}

	uint16 numFrameFlags = 0;
	ser.syncAsUint16LE(numFrameFlags);
	_frameFlags.resize(numFrameFlags);
	for (uint i = 0; i < numFrameFlags; ++i) {
		ser.syncAsSint16LE(_frameFlags[i].frameID);
		ser.syncAsSint16LE(_frameFlags[i].flagDesc.label);
		ser.syncAsUint16LE(_frameFlags[i].flagDesc.flag);
	}

	uint16 numVideoDescs = 0;
	ser.syncAsUint16LE(numVideoDescs);
	_videoDescs.resize(numVideoDescs);
	for (uint i = 0; i < numVideoDescs; ++i) {
		_videoDescs[i].readData(stream);
	}

	_sound.name = "NO SOUND";

	// AR 47 ("InteractiveVideo") appends a name, a flag byte, and a list of
	// named {value, flag} entries on top of the AR-44 movie data.
	if (_type == 47) {
		readFilename(ser, _interactiveName);
		byte flag = 0;
		ser.syncAsByte(flag);
		_interactiveFlag = flag != 0;

		uint16 numEntries = 0;
		ser.syncAsUint16LE(numEntries);
		_interactiveEntries.resize(numEntries);
		for (uint i = 0; i < numEntries; ++i) {
			readFilename(ser, _interactiveEntries[i].name);
			ser.syncAsUint32LE(_interactiveEntries[i].value);
			ser.syncAsByte(_interactiveEntries[i].flag);
		}
	}
}

void PlaySecondaryMovie::readData(Common::SeekableReadStream &stream) {
	Common::Serializer ser(&stream, nullptr);
	ser.setVersion(g_nancy->getGameType());

	if (_isRandom) {
		// Nancy14 reworked the random-movie layout (Nancy13 and earlier use the
		// older secondaryMovie-record + hotspot-list form).
		if (g_nancy->getGameType() >= kGameTypeNancy16) {
			readRandomMovieDataNancy16(ser, stream);
		} else if (g_nancy->getGameType() >= kGameTypeNancy14) {
			readRandomMovieDataNancy14(ser, stream);
		} else {
			readRandomMovieData(ser, stream);
		}
		return;
	}

	if (g_nancy->getGameType() >= kGameTypeNancy14) {
		readDataNancy14(ser, stream);
		return;
	}

	readFilename(ser, _videoName);
	readFilename(ser, _paletteName, kGameTypeVampire, kGameTypeVampire);
	readFilename(ser, _bitmapOverlayName, kGameTypeVampire, kGameTypeNancy9);

	ser.skip(2, kGameTypeNancy7);	// videoType
	ser.skip(2, kGameTypeVampire, kGameTypeNancy9); // videoPlaySource
	ser.syncAsUint16LE(_videoFormat);
	if (g_nancy->getGameType() >= kGameTypeNancy10)
		_videoFormat = kLargeVideoFormat;
	ser.skip(4, kGameTypeVampire, kGameTypeVampire); // paletteStart, paletteSize
	ser.skip(2, kGameTypeVampire, kGameTypeNancy9);  // hasBitmapOverlaySurface
	ser.skip(2, kGameTypeVampire, kGameTypeNancy9);  // VIDEO_STOP_RENDERING, VIDEO_CONTINUE_RENDERING

	ser.syncAsUint16LE(_videoSceneChange);
	ser.syncAsUint16LE(_playerCursorAllowed);
	ser.syncAsUint16LE(_playDirection);
	ser.syncAsUint16LE(_firstFrame);
	ser.syncAsUint16LE(_lastFrame);

	if (g_nancy->getGameType() >= kGameTypeNancy10) {
		ser.syncAsSint16LE(_sceneChange.sceneID);
		ser.syncAsUint16LE(_sceneChange.frameID);
		ser.syncAsUint16LE(_sceneChange.verticalOffset);
		ser.syncAsUint16LE(_sceneChange.continueSceneSound);
	}

	if (g_nancy->getGameType() >= kGameTypeNancy10) {
		ser.syncAsSint16LE(_videoStartFlag.label);
		ser.syncAsUint16LE(_videoStartFlag.flag);
	}

	if (ser.getVersion() >= kGameTypeNancy1) {
		uint size = 15;
		
		if (ser.getVersion() >= kGameTypeNancy10)
			ser.syncAsUint16LE(size);

		_frameFlags.resize(size);
		for (uint i = 0; i < size; ++i) {
			ser.syncAsSint16LE(_frameFlags[i].frameID);
			ser.syncAsSint16LE(_frameFlags[i].flagDesc.label);
			ser.syncAsUint16LE(_frameFlags[i].flagDesc.flag);
		}
	}

	if (ser.getVersion() <= kGameTypeNancy9) {
		_triggerFlags.readData(stream);
		_sound.readNormal(stream);
		_sceneChange.readData(stream, ser.getVersion() == kGameTypeVampire);
	}

	uint16 numVideoDescs = 0;
	ser.syncAsUint16LE(numVideoDescs);
	_videoDescs.resize(numVideoDescs);
	for (uint i = 0; i < numVideoDescs; ++i) {
		_videoDescs[i].readData(stream);
	}

	if (ser.getVersion() >= kGameTypeNancy6) {
		// Movie sound was deliberately disabled in nancy6
		_sound.name = "NO SOUND";
	}
}

void PlaySecondaryMovie::init() {
	if (!_decoder.isVideoLoaded()) {
		if (!_decoder.loadFile(_videoName)) {
			error("Couldn't load video file %s", _videoName.toString().c_str());
		}

		// Nancy16 records do not carry a frame range; play to the end of the clip.
		if (_lastFrame < 0 && _decoder.getFrameCount() > 0) {
			_lastFrame = (int16)(_decoder.getFrameCount() - 1);
		}

		if (!_paletteName.empty()) {
			GraphicsManager::loadSurfacePalette(_fullFrame, _paletteName);
			GraphicsManager::loadSurfacePalette(_drawSurface, _paletteName);
		}

		if (g_nancy->getGameType() == kGameTypeVampire) {
			setTransparent(true);
			_fullFrame.setTransparentColor(_drawSurface.getTransparentColor());

			// TVD uses empty video files during the endgame ceremony
			// This makes sure the screen doesn't go black while the sound is playing
			_drawSurface.clear(_drawSurface.getTransparentColor());
		}
	}

	if (_isRandom) {
		resolveSentinelFrames();
	}

	_screenPosition = _drawSurface.getBounds();

	RenderObject::init();
}

void PlaySecondaryMovie::onPause(bool pause) {
	_decoder.pauseVideo(pause);
	RenderActionRecord::onPause(pause);
}

void PlaySecondaryMovie::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		g_nancy->_sound->loadSound(_sound);
		g_nancy->_sound->playSound(_sound);

		if (_sound.name != "NO SOUND" && g_nancy->getGameType() <= kGameTypeNancy5) {
			// Sync audio and video. This is mostly relevant for some nancy2 scenes, as the
			// devs stopped using the built-in movie sound around nancy4. The 12 ms
			// difference is roughly how long it takes for a single execution of the main game loop
			_decoder.addFrameTime(12);
		}

		if (_playerCursorAllowed == kNoPlayerCursorAllowed) {
			g_nancy->setMouseEnabled(false);
		}

		NancySceneState.setActiveMovie(this);

		if (g_nancy->getGameType() >= kGameTypeNancy10)
			NancySceneState.setEventFlag(_videoStartFlag);

		// The starting sequence begins here rather than in
		// applyStartingRandomSequence(), which runs during readData() when the
		// scene state is not yet ready to take a flag.
		if (_isRandom) {
			raiseSequenceFlag(_activeSequenceIndex);
		}

		_state = kRun;

		if (Common::Rect(_decoder.getWidth(), _decoder.getHeight()) == NancySceneState.getViewport().getBounds()) {
			g_nancy->_graphics->suppressNextDraw();
			break;
		}

		// fall through
	case kRun: {
		// Random-movie chain: while paused, wait for the pause to expire
		// then re-roll. The roll itself may set up another pause, swap to
		// the next sequence, or finish the AR if stop was requested.
		if (_isRandom && _randomChainState == kRandomPaused) {
			if (_randomStopRequested) {
				_state = kActionTrigger;
				break;
			}
			if (g_system->getMillis() < _randomPauseEndTime) {
				break;
			}
			_randomChainState = kRandomPlaying;
			_randomPauseExpired = true;	// Nancy16: the pause just happened, move on
			int picked = rollNextSequence();
			if (picked >= 0) {
				activateRandomSequence(picked);
			}
			// If picked < 0, rollNextSequence() set up another pause.
			break;
		}

		// Talkable character: swap immediately between the idle loop and the
		// recognition ("turn around") movie as the mouse enters/leaves the
		// character, without waiting for the current cycle to finish.
		if (isTalkable()) {
			if (_isHovered && !_playingSecondary) {
				_playingSecondary = true;
				activateSecondaryMovie();
			} else if (!_isHovered && _playingSecondary) {
				_playingSecondary = false;
				activateRandomSequence(_activeSequenceIndex);
			}
		}

		int newFrame = NancySceneState.getSceneInfo().frameID;

		if (newFrame != _curViewportFrame) {
			_curViewportFrame = newFrame;
			int activeFrame = -1;
			for (uint i = 0; i < _videoDescs.size(); ++i) {
				if (newFrame == _videoDescs[i].frameID) {
					activeFrame = i;
					break;
				}
			}

			if (activeFrame != -1) {
				_screenPosition = _videoDescs[activeFrame].destRect;
				setVisible(true);

				// Nancy13 talkable characters: the character's on-screen box
				// doubles as a clickable hotspot that opens its conversation.
				if (_talkSceneID != kNoScene) {
					_hotspot = _screenPosition;
					_hasHotspot = true;
				}
			} else if (_isRandom && g_nancy->getGameType() < kGameTypeNancy16) {
				// Random movies aren't gated on hotspot/viewport-frame
				// matches the way regular PSMs are: play full viewport.
				// Nancy16's records do carry per-frame blt descriptors (the
				// piazza fidgets list four panorama frames each), so there the
				// absence of a match means "not on screen from here", not
				// "stretch me over the whole viewport".
				_screenPosition = NancySceneState.getViewport().getBounds();
				setVisible(true);
				_hasHotspot = false;
			} else {
				setVisible(false);
				_hasHotspot = false;
			}
		}

		// We update the decoder here instead of in updateGraphics() to avoid an
		// edge case in nancy4 (scene 3180) where the very last frame has a frameFlag that should trigger
		// another action record, but doesn't do so, because updateGraphics() gets called after all
		// action record execution. Instead, the movie's own scene change (which is inexplicably enabled)
		// gets triggered, and teleports the player to the wrong place instead of making them lose the game
		if (!_decoder.isPlaying() && _isVisible && !_isFinished) {
			_decoder.start();

			if (_playDirection == kPlayMovieReverse) {
				_decoder.setRate(-_decoder.getRate());
				_decoder.seekToFrame(_lastFrame);
			} else {
				_decoder.seekToFrame(_firstFrame);
			}

			if (Trace::isOn() && !_traceStartMs) {
				_traceStartMs = g_system->getMillis();
			}
		}

		// Harness: nancy_movie_skip. Cinematics only - a random movie is a
		// character's idle fidget loop that never ends, so there is nothing to
		// skip and skipping it would freeze them mid-gesture.
		//
		// This does NOT short-circuit to the movie's outcome, and it does NOT
		// seek. Seeking was the first attempt and it asserted in
		// BinkDecoder::findKeyFrame: _lastFrame is still the 0xFFFF "play to the
		// end" sentinel at this point on most records, and a Bink seek to the
		// far end of an 18 MB file is expensive even when the number is valid.
		//
		// Instead the movie's end is moved to where the playhead already is.
		// The completion branch below is watching for getCurFrame() == _lastFrame
		// and for the sound to stop, so both of its conditions become true on
		// this same tick and the record finishes through its own kActionTrigger
		// path: its trigger flags fire, its scene change happens, nothing is
		// bypassed. Every frame flag between here and the real end is raised
		// first, because those set event flags and dropping them would silently
		// lose game state.
		if (!_isRandom && !_traceSkipped && !_isFinished && Trace::movieSkip() &&
				_decoder.isVideoLoaded() && _decoder.getCurFrame() >= 0) {
			_traceSkipped = true;
			const int cur = _decoder.getCurFrame();
			const int count = _decoder.getFrameCount();
			const bool reverse = _playDirection == kPlayMovieReverse;

			// Resolve the sentinels the same way resolveSentinelFrames() does,
			// then clamp, so the range swept for frame flags is a real range.
			int realFirst = (_firstFrame == 0xFFFF) ? 0 : (int)_firstFrame;
			int realLast = (_lastFrame == 0xFFFE || _lastFrame == 0xFFFF) ?
				MAX(0, count - 1) : (int)_lastFrame;
			realFirst = CLIP<int>(realFirst, 0, MAX(0, count - 1));
			realLast = CLIP<int>(realLast, 0, MAX(0, count - 1));

			for (auto &f : _frameFlags) {
				const bool passed = reverse ?
					(f.frameID <= cur && f.frameID >= realFirst) :
					(f.frameID >= cur && f.frameID <= realLast);
				if (passed) {
					NancySceneState.setEventFlag(f.flagDesc);
				}
			}

			if (reverse) {
				_firstFrame = (uint16)cur;
			} else {
				_lastFrame = (uint16)cur;
			}

			g_nancy->_sound->stopSound(_sound);
		}

		if (_decoder.needsUpdate()) {
			uint descID = 0;

			for (uint i = 0; i < _videoDescs.size(); ++i) {
				if (_videoDescs[i].frameID == _curViewportFrame) {
					descID = i;
				}
			}

			// Nancy16's movies are Bink at final size, so nothing is ever doubled -
			// the quarter-size AVF format that flag exists for is gone. It matters
			// because _videoFormat reads 1 (kSmallVideoFormat) on 530 of the 593
			// records, and copyToManaged's doubling path flips vertically even
			// when asked not to, which is what turned the intro title upside down.
			const bool doubleSize = g_nancy->getGameType() < kGameTypeNancy16 &&
				_videoFormat == kSmallVideoFormat;

			GraphicsManager::copyToManaged(*_decoder.decodeNextFrame(), _fullFrame,
				g_nancy->getGameType() == kGameTypeVampire, doubleSize);

			// Nancy14 stores an all -1 srcRect to mean "use the whole frame".
			Common::Rect srcRect = _videoDescs[descID].srcRect;
			if (srcRect.isEmpty()) {
				srcRect = Common::Rect(_fullFrame.w, _fullFrame.h);
			}

			Common::Rect destRect = _videoDescs[descID].destRect;

			// The videoDesc's size might be larger than the decoded video (for example, nancy10's
			// COR_AceFidgetEars_ANIM, and nancy12's PAR_ArcadeAnimationB); clamp here to avoid
			// reading out-of-bounds during draw. (Adjust destRect too: avoid stretching)
			const int16 decodedWidth = (int16)_decoder.getWidth();
			if (srcRect.width() > decodedWidth) {
				srcRect.setWidth(decodedWidth);
				destRect.setWidth(decodedWidth);
			}
			const int16 decodedHeight = (int16)_decoder.getHeight();
			if (srcRect.height() > decodedHeight) {
				srcRect.setHeight(decodedHeight);
				destRect.setHeight(decodedHeight);
			}

			_drawSurface.create(_fullFrame, srcRect);
			moveTo(destRect);

			_needsRedraw = true;

			for (auto &f : _frameFlags) {
				if (_decoder.getCurFrame() == f.frameID) {
					NancySceneState.setEventFlag(f.flagDesc);
				}
			}
		}

		if ((_decoder.getCurFrame() == _lastFrame && _playDirection == kPlayMovieForward) ||
			(_decoder.getCurFrame() == _firstFrame && _playDirection == kPlayMovieReverse) ||
			_decoder.endOfVideo()) {

			_decoder.pauseVideo(true);
			_isFinished = true;

			if (_isRandom) {
				// Sequence finished: roll for next. If stop was requested
				// by a PlayRandomMovieControl, wind the AR down normally.
				if (_randomStopRequested) {
					_state = kActionTrigger;
				} else if (isTalkable()) {
					// Hover swaps are handled at the top of kRun. Here we only
					// keep the idle loop going; the recognition movie, once
					// finished, holds on its last frame while the mouse stays.
					if (!_playingSecondary) {
						// Replay the idle movie in place without reopening it.
						_isFinished = false;
						_decoder.seekToFrame(_playDirection == kPlayMovieReverse ? _lastFrame : _firstFrame);
						_decoder.pauseVideo(false);
					}
				} else {
					int picked = rollNextSequence();
					if (picked >= 0) {
						activateRandomSequence(picked);
					}
					// Otherwise the chain entered the paused state; no
					// state-trigger transition.
				}
			} else if (!g_nancy->_sound->isSoundPlaying(_sound)) {
				// Stop the video and block it from starting again, but also wait for
				// sound to end before changing state
				g_nancy->_sound->stopSound(_sound);

				// Harness: one line per cinematic, whether or not it was skipped,
				// so "how many seconds of movie is on this route" is a number the
				// trace answers rather than a guess.
				if (Trace::isOn() && !_traceReported) {
					_traceReported = true;
					// No natural/estimated duration here on purpose: the first
					// version reported _decoder.getDuration(), and Bink handed
					// back 4369066 ms (72 minutes) for all three of the movies
					// measured, so the field was fiction. actual_ms is the wall
					// clock this record really cost, which is the number the
					// nancy_movie_skip A/B is measured with.
					const int actual = _traceStartMs ? (int)(g_system->getMillis() - _traceStartMs) : 0;
					TraceEvent("movie")
						.str("name", _videoName.toString('/'))
						.num("frames", _decoder.getFrameCount())
						.num("endframe", _decoder.getCurFrame())
						.num("actual_ms", actual)
						.boolean("skipped", _traceSkipped)
						.num("scene", NancySceneState.getSceneInfo().sceneID)
						.emit();
				}

				_state = kActionTrigger;
			}
		}

		break;
	}
	case kActionTrigger:
		_triggerFlags.execute();
		if (_videoSceneChange == kMovieSceneChange) {
			NancySceneState.changeScene(_sceneChange);
		}

		NancySceneState.setActiveMovie(nullptr);
		finishExecution();

		// Allow looping
		if (!_isDone) {
			_isFinished = false;
			_decoder.seek(0);
			_decoder.pauseVideo(false);
		} else if (_playerCursorAllowed == kNoPlayerCursorAllowed) {
			// The movie finished and isn't looping, so restore the cursor now.
			// WORKAROUND: Don't restore the cursor for Nancy 8, scenes 5420 - 5422
			// (confrontation with the culprit). For some reason, the original engine
			// doesn't restore the cursor in this scene - restoring it allows the user
			// to examine items and break the scene itself.
			// Refer to bug #16728 for more details
			const uint16 sceneId = NancySceneState.getSceneInfo().sceneID;
			if (!(g_nancy->getGameType() == kGameTypeNancy8 && (sceneId >= 5420 && sceneId <= 5422)))
				g_nancy->setMouseEnabled(true);
		}

		break;
	}
}

// --- PlayRandomMovieControl --------------------------------------------

void PlayRandomMovieControl::readData(Common::SeekableReadStream &stream) {
	_mode = stream.readByte();
	_sceneChange.readData(stream, true, true);
}

void PlayRandomMovieControl::execute() {
	PlaySecondaryMovie *target = NancySceneState.getActiveMovie();
	if (target && target->_isRandom) {
		target->stopRandom();
	}

	_sceneChange.execute();
	finishExecution();
}

} // End of namespace Action
} // End of namespace Nancy
