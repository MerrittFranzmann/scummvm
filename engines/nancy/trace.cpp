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

#include "engines/nancy/trace.h"

#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/system.h"
#include "common/textconsole.h"

namespace Nancy {

bool Trace::_init = false;
bool Trace::_on = false;
int Trace::_movieSkip = -1;
bool Trace::_toStderr = false;
uint32 Trace::_seq = 0;
Common::WriteStream *Trace::_file = nullptr;

bool Trace::isOn() {
	if (!_init) {
		_init = true;
		_toStderr = ConfMan.getBool("nancy_trace");
		_on = _toStderr;

		if (ConfMan.hasKey("nancy_trace_file")) {
			// NOT Common::DumpFile: its FSNode::createWriteStream() defaults to
			// atomic mode, which writes "<path>.tmp" and only renames it on
			// close. A driver tailing the trace of a live run would find no file
			// at all, and a run killed by its own timeout would leave the whole
			// trace behind under the wrong name. Non-atomic, flushed per line.
			const Common::String path = ConfMan.get("nancy_trace_file");
			Common::FSNode node(Common::Path(path, '/'));
			Common::WriteStream *ws = node.createWriteStream(false);
			if (ws) {
				_file = ws;
				_on = true;
			} else {
				warning("NTRACE could not open nancy_trace_file '%s' "
					"(does its directory exist?)", path.c_str());
			}
		}
	}

	return _on;
}

bool Trace::movieSkip() {
	if (_movieSkip < 0) {
		_movieSkip = ConfMan.getBool("nancy_movie_skip") ? 1 : 0;
	}

	return _movieSkip == 1;
}

void Trace::write(const Common::String &body) {
	if (!isOn()) {
		return;
	}

	if (_file) {
		_file->writeString(body);
		_file->writeByte('\n');
		_file->flush();
	}

	if (_toStderr) {
		// warning() wraps this as "WARNING: NTRACE1 {...}!" - the trailing '!'
		// is textconsole's, not ours. trace.py strips it.
		warning("NTRACE1 %s", body.c_str());
	}
}

void Trace::shutdown() {
	if (_file) {
		_file->flush();
		delete _file;
		_file = nullptr;
	}
}

Common::String Trace::escape(const Common::String &in) {
	Common::String out;
	for (uint i = 0; i < in.size(); ++i) {
		const char c = in[i];
		switch (c) {
		case '"':	out += "\\\""; break;
		case '\\':	out += "\\\\"; break;
		case '\n':	out += "\\n"; break;
		case '\r':	out += "\\r"; break;
		case '\t':	out += "\\t"; break;
		default:
			if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) {
				// Scene descriptions are authored in a Windows codepage; a raw
				// high byte is not valid UTF-8 and would break json.loads.
				out += Common::String::format("\\u%04x", (unsigned char)c);
			} else {
				out += c;
			}
		}
	}

	return out;
}

Common::String Trace::numArray(const Common::Array<uint> &vals) {
	Common::String out = "[";
	for (uint i = 0; i < vals.size(); ++i) {
		out += Common::String::format("%s%u", i ? "," : "", vals[i]);
	}

	return out + "]";
}

TraceEvent::TraceEvent(const char *ev) : _first(true) {
	str("ev", ev);
	num("seq", Trace::nextSeq());
	num("ms", g_system ? g_system->getMillis() : 0);
}

TraceEvent &TraceEvent::num(const char *key, int64 value) {
	_body += Common::String::format("%s\"%s\":%lld", _first ? "" : ",", key, (long long)value);
	_first = false;
	return *this;
}

TraceEvent &TraceEvent::boolean(const char *key, bool value) {
	_body += Common::String::format("%s\"%s\":%s", _first ? "" : ",", key, value ? "true" : "false");
	_first = false;
	return *this;
}

TraceEvent &TraceEvent::str(const char *key, const Common::String &value) {
	_body += Common::String::format("%s\"%s\":\"%s\"", _first ? "" : ",", key,
		Trace::escape(value).c_str());
	_first = false;
	return *this;
}

TraceEvent &TraceEvent::raw(const char *key, const Common::String &value) {
	_body += Common::String::format("%s\"%s\":%s", _first ? "" : ",", key, value.c_str());
	_first = false;
	return *this;
}

void TraceEvent::emit() {
	Trace::write("{" + _body + "}");
}

// ---------------------------------------------------------------------------
// NancyRandomSource
// ---------------------------------------------------------------------------

NancyRandomSource::NancyRandomSource(const Common::String &name) :
		_rnd(name), _name(name), _draws(0), _isAmbient(name == "NancyAmbient"),
		_ambientOn(false), _ambient(nullptr) {

	if (!_isAmbient) {
		// nancy_rng_split routes the wall-clock-driven cosmetic draws onto a
		// second stream. Default off: with it off the engine draws exactly the
		// numbers, in exactly the order, that it did before this file existed.
		_ambientOn = ConfMan.getBool("nancy_rng_split");
		if (_ambientOn) {
			_ambient = new NancyRandomSource("NancyAmbient");
			// Deterministically derived from the main seed, so one
			// random_seed still pins both streams.
			_ambient->setSeed(_rnd.getSeed() ^ 0x5bf03fadu);
		}
	}
}

void NancyRandomSource::setSeed(uint32 seed) {
	_rnd.setSeed(seed);
	if (_ambient) {
		_ambient->setSeed(seed ^ 0x5bf03fadu);
	}
}

void NancyRandomSource::note(int64 value, int64 max) {
	++_draws;

	static int on = -1;
	if (on < 0) {
		on = ConfMan.getBool("nancy_rng_trace") ? 1 : 0;
	}

	if (on && Trace::isOn()) {
		TraceEvent(_isAmbient ? "rngamb" : "rng")
			.num("n", _draws)
			.num("max", max)
			.num("v", value)
			.emit();
	}
}

uint NancyRandomSource::getRandomNumber(uint max) {
	const uint v = _rnd.getRandomNumber(max);
	note(v, max);
	return v;
}

uint NancyRandomSource::getRandomBit() {
	const uint v = _rnd.getRandomBit();
	note(v, 1);
	return v;
}

uint NancyRandomSource::getRandomNumberRng(uint min, uint max) {
	const uint v = _rnd.getRandomNumberRng(min, max);
	note(v, max);
	return v;
}

int NancyRandomSource::getRandomNumberRngSigned(int min, int max) {
	const int v = _rnd.getRandomNumberRngSigned(min, max);
	note(v, max);
	return v;
}

} // End of namespace Nancy
