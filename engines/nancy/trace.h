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
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef NANCY_TRACE_H
#define NANCY_TRACE_H

#include "common/str.h"
#include "common/array.h"
#include "common/random.h"

namespace Common {
class WriteStream;
}

namespace Nancy {

// ---------------------------------------------------------------------------
// Machine-readable trace for unattended playthroughs.
//
// Everything here is debug-only harness code and every entry point is a no-op
// unless the matching nancy_* config key is present, so a human player's run is
// bit-for-bit what it was before.
//
// The wire format is NDJSON: one self-contained JSON object per line, each
// carrying "ev", "seq" and "ms". See HOOKS.md for the field-by-field contract;
// it is frozen, so fields may be ADDED but never renamed or removed.
// ---------------------------------------------------------------------------

// One parsed entry of nancy_goals. See HOOKS.md for the grammar.
struct TraceGoal {
	Common::String id;
	// 's': evaluate on entry to scene deadlineVal.
	// 'n': evaluate on the deadlineVal'th scene entry.
	char deadlineKind;
	int deadlineVal;
	Common::Array<Common::String> preds;
	bool met;
	bool deadlineDone;

	TraceGoal() : deadlineKind('n'), deadlineVal(0), met(false), deadlineDone(false) {}
};

class TraceEvent {
public:
	explicit TraceEvent(const char *ev);

	TraceEvent &num(const char *key, int64 value);
	TraceEvent &boolean(const char *key, bool value);
	TraceEvent &str(const char *key, const Common::String &value);
	// Value is emitted verbatim - use for arrays and pre-built objects.
	TraceEvent &raw(const char *key, const Common::String &value);

	void emit();

private:
	Common::String _body;
	bool _first;
};

class Trace {
public:
	// True when nancy_trace or nancy_trace_file is set. Cached; the config is
	// read once, on the first call.
	static bool isOn();

	// nancy_movie_skip. Cached; default false.
	static bool movieSkip();

	// Raw line emitter. `body` is the complete JSON object including braces.
	static void write(const Common::String &body);

	static uint32 nextSeq() { return ++_seq; }

	static void shutdown();

	// JSON string escaping, without the surrounding quotes.
	static Common::String escape(const Common::String &in);

	// Renders an array of numbers as a JSON array.
	static Common::String numArray(const Common::Array<uint> &vals);

private:
	static bool _init;
	static bool _on;
	static int _movieSkip;
	static bool _toStderr;
	static uint32 _seq;
	static Common::WriteStream *_file;
};

// ---------------------------------------------------------------------------
// Determinism: a drop-in replacement for Common::RandomSource that can count
// and log every draw, and that can route the wall-clock-driven cosmetic
// consumers (ambient SFX repeat delays, 3D sound wander, lightning) onto a
// second stream so they cannot shift the position of the gameplay stream.
//
// The methods are the same names and signatures as Common::RandomSource's, so
// existing `g_nancy->_randomSource->getRandomNumber(...)` call sites bind here
// with no edit.
// ---------------------------------------------------------------------------

class NancyRandomSource {
public:
	explicit NancyRandomSource(const Common::String &name);

	uint getRandomNumber(uint max);
	uint getRandomBit();
	uint getRandomNumberRng(uint min, uint max);
	int getRandomNumberRngSigned(int min, int max);

	// The ambient stream. Only reached through the explicit accessor below, so
	// a call site has to opt in.
	NancyRandomSource &ambient() { return _ambientOn && _ambient ? *_ambient : *this; }

	void setSeed(uint32 seed);
	uint32 getSeed() const { return _rnd.getSeed(); }
	static uint32 generateNewSeed() { return Common::RandomSource::generateNewSeed(); }

	uint32 getDrawCount() const { return _draws; }

private:
	void note(int64 value, int64 max);

	Common::RandomSource _rnd;
	Common::String _name;
	uint32 _draws;
	bool _isAmbient;
	bool _ambientOn;
	NancyRandomSource *_ambient;
};

} // End of namespace Nancy

#endif // NANCY_TRACE_H
