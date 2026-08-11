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

#include "common/hashmap.h"
#include "common/hash-str.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/util.h"
#include "engines/nancy/puzzledata.h"

#include "engines/nancy/action/collectionrecords.h"

#include "engines/nancy/state/scene.h"

#include "common/debug.h"

namespace Nancy {
namespace Action {

// Parse-time registry of the collection declarations made by AR 37.
//
// AR 38 and AR 39 name a collection but do not repeat its element type, so the
// only way to know whether the value that follows is a 33-byte string or an
// 8-byte double is to have read the AR 37 creation record first. That record is
// static game data: a given name always has the same element type wherever it is
// declared (FaxNumberEntries is declared identically in S2959 and S3051), so the
// registry is a plain process-wide map rather than per-scene state, and is not
// affected by saving, loading or restarting.
//
// Every scene holding AR 38 or AR 39 records also holds the matching AR 37
// creation record ahead of them in chunk order, in all five scenes that use
// collections, so a lookup never misses. If one ever does, the reader falls back
// to the number of bytes left in the chunk and warns; that fallback exists only
// so a future game cannot make the record over-read.
struct CollectionDeclaration {
	uint32 maxEntries = 0;
	bool isCharacter = false;
};

static Common::HashMap<Common::String, CollectionDeclaration> &collectionDeclarations() {
	static Common::HashMap<Common::String, CollectionDeclaration> decls;
	return decls;
}

// Returns true when `name` was declared as a character collection. `stream` is
// only consulted for the fallback described above.
static bool isCharacterCollection(const Common::String &name, uint valueSize, Common::SeekableReadStream &stream) {
	Common::HashMap<Common::String, CollectionDeclaration> &decls = collectionDeclarations();
	if (decls.contains(name)) {
		return decls[name].isCharacter;
	}

	// Fallback: a character entry is 33 bytes and a numeric one 8, so the bytes
	// left over pick the type. Never taken in nancy18.
	const bool guess = (uint)(stream.size() - stream.pos()) >= valueSize * 33;
	warning("Collection '%s' used before it was declared; guessing %s entries from the record length",
		name.c_str(), guess ? "character" : "numeric");
	return guess;
}

static CollectionData::Collection *getCollection(const Common::String &name) {
	CollectionData *data = (CollectionData *)NancySceneState.getPuzzleData(CollectionData::getTag());
	if (!data || !data->collections.contains(name)) {
		return nullptr;
	}

	return &data->collections[name];
}

// AR 37 ------------------------------------------------------------------

void CollectionCreate::readData(Common::SeekableReadStream &stream) {
	// byte     mode, 0 = create, 1 = destroy
	// char[33] collection name
	// mode 0 only:
	//   uint16 overwrite an existing collection rather than keeping it
	//   uint32 maximum number of entries
	//   uint16 element type, 1 = char[33] string, 0 = 8-byte double
	//   rect   bounds the contents are displayed in, empty when they are not
	//   byte   always 0xff
	//   byte   0xff when the bounds above are set, 0 when they are not
	//   uint16 always 0
	//
	// 12/12 exact.
	_mode = stream.readByte();
	readFilename(stream, _collectionName);

	if (_mode != 0) {
		return;
	}

	// GUESSED: the uint16 is 1 on exactly one record, the second VEN_KeypadPUZ
	// creation in S6650, whose dependencies are the keypad's clear/reset flags
	// while the first creation's are not. Reading it as "wipe what is already
	// there" is what makes that pair of records differ at all.
	_overwrite = stream.readUint16LE() != 0;
	_maxEntries = stream.readUint32LE();
	_isCharacter = stream.readUint16LE() != 0;
	readRect(stream, _displayBounds);

	// GUESSED and inert: the two bytes track with whether _displayBounds is set,
	// so they are probably a "draw the contents" switch, but nothing in nancy18
	// needs the collections drawn - the visible feedback all comes from overlay
	// records keyed on the event flags AR 39 sets.
	stream.skip(2);
	stream.skip(2);

	CollectionDeclaration &decl = collectionDeclarations().getOrCreateVal(_collectionName);
	decl.maxEntries = _maxEntries;
	decl.isCharacter = _isCharacter;
}

void CollectionCreate::execute() {
	CollectionData *data = (CollectionData *)NancySceneState.getPuzzleData(CollectionData::getTag());
	if (!data) {
		_isDone = true;
		return;
	}

	if (_mode != 0) {
		data->collections.erase(_collectionName);
		debugC(kDebugActionRecord, "Destroyed collection '%s'", _collectionName.c_str());
		_isDone = true;
		return;
	}

	if (!_overwrite && data->collections.contains(_collectionName)) {
		_isDone = true;
		return;
	}

	CollectionData::Collection &c = data->collections.getOrCreateVal(_collectionName);
	c.maxEntries = _maxEntries;
	c.isCharacter = _isCharacter;
	c.strings.clear();
	c.numbers.clear();

	debugC(kDebugActionRecord, "Created collection '%s', max %u %s entries",
		_collectionName.c_str(), _maxEntries, _isCharacter ? "character" : "numeric");

	_isDone = true;
}

Common::String CollectionCreate::getRecordExtraInfo() const {
	if (_mode != 0) {
		return Common::String::format("Destroy collection '%s'", _collectionName.c_str());
	}

	return Common::String::format("Create collection '%s', max %u %s entries%s",
		_collectionName.c_str(), _maxEntries, _isCharacter ? "character" : "numeric",
		_overwrite ? ", overwriting" : "");
}

// AR 38 ------------------------------------------------------------------

void CollectionEdit::readData(Common::SeekableReadStream &stream) {
	// byte     operation, always 1 (append) in nancy18
	// char[33] collection name
	// uint16   number of values that follow
	// values   char[33] each for a character collection, an 8-byte double each
	//          for a numeric one - see the note at the top of the header
	//
	// 49/49 exact.
	_mode = stream.readByte();
	readFilename(stream, _collectionName);

	const uint16 numValues = stream.readUint16LE();
	_isCharacter = isCharacterCollection(_collectionName, numValues, stream);

	for (uint i = 0; i < numValues; ++i) {
		if (_isCharacter) {
			Common::String value;
			readFilename(stream, value);
			_strings.push_back(value);
		} else {
			_numbers.push_back(stream.readDoubleLE());
		}
	}
}

void CollectionEdit::execute() {
	CollectionData::Collection *c = getCollection(_collectionName);
	if (!c) {
		// The creation record is in the same scene and earlier in the list, so
		// this only happens if its dependencies kept it from running.
		_isDone = true;
		return;
	}

	// GUESSED: the operation byte is 1 on all 49 records, so only append has
	// ever been seen. Anything else is left alone rather than guessed at.
	if (_mode != 1) {
		warning("Unknown collection edit operation %u on '%s'", _mode, _collectionName.c_str());
		_isDone = true;
		return;
	}

	if (c->isCharacter != _isCharacter) {
		warning("Collection '%s' was declared as %s but is being appended to as %s",
			_collectionName.c_str(), c->isCharacter ? "character" : "numeric",
			_isCharacter ? "character" : "numeric");
		_isDone = true;
		return;
	}

	for (uint i = 0; i < (_isCharacter ? _strings.size() : _numbers.size()); ++i) {
		// GUESSED: a full collection drops its oldest entry rather than refusing
		// the new one. The declared maximum is exactly the length of the answer
		// in every case, and it has to behave this way for the Chinese puzzle box
		// in S3275 to be playable: nothing anywhere in the game destroys or
		// recreates CPuzzleBox, and its creation record does not overwrite, so
		// with a hard cap - or with no cap at all, since the comparison is
		// length-sensitive - four wrong presses would lock the box forever.
		// Sliding the window keeps it showing the last four presses, which is
		// what a combination box does. The other three collections are emptied by
		// the game before they can ever overflow, so this only affects S3275.
		while (c->maxEntries && c->size() >= c->maxEntries) {
			if (_isCharacter) {
				c->strings.remove_at(0);
			} else {
				c->numbers.remove_at(0);
			}
		}

		if (_isCharacter) {
			c->strings.push_back(_strings[i]);
			debugC(kDebugActionRecord, "Collection '%s' += '%s', %u entries",
				_collectionName.c_str(), _strings[i].c_str(), c->size());
		} else {
			c->numbers.push_back(_numbers[i]);
			debugC(kDebugActionRecord, "Collection '%s' += %g, %u entries",
				_collectionName.c_str(), _numbers[i], c->size());
		}
	}

	_isDone = true;
}

Common::String CollectionEdit::getRecordExtraInfo() const {
	Common::String ret = Common::String::format("Append to collection '%s':", _collectionName.c_str());

	if (_isCharacter) {
		for (uint i = 0; i < _strings.size(); ++i) {
			ret += Common::String::format(" '%s'", _strings[i].c_str());
		}
	} else {
		for (uint i = 0; i < _numbers.size(); ++i) {
			ret += Common::String::format(" %g", _numbers[i]);
		}
	}

	return ret;
}

// AR 39 ------------------------------------------------------------------

// The event flags in AR 39 are the compact three-byte form: an int16 label, -1
// for none, and a single byte value.
static void readTerseFlag(Common::SeekableReadStream &stream, FlagDescription &flag) {
	flag.label = stream.readSint16LE();
	flag.flag = stream.readByte();
}

void CollectionCheck::readData(Common::SeekableReadStream &stream) {
	// byte     kind, 1 = test the contents, 2 = test the number of entries
	// char[33] collection name
	// kind 1:
	//   int16, byte  event flag set when the contents match
	//   int16 (unused, always -1), byte element type - the same 1 = string /
	//         0 = double flag AR 37 declares, and it agrees with the declaration
	//         on all five kind-1 records
	//   uint16 number of answer entries, then the entries themselves
	// kind 2:
	//   int16, byte x3  event flags for fewer than / equal to / more than
	//   uint32          the number of entries compared against
	//
	// 18/18 exact.
	_kind = stream.readByte();
	readFilename(stream, _collectionName);

	if (_kind == 1) {
		readTerseFlag(stream, _flagOnMatch);

		stream.skip(2);
		const bool ownIsCharacter = stream.readByte() != 0;

		// The declaration is what decides, so that this record and AR 38 always
		// agree; the record's own byte is only a cross-check.
		_isCharacter = isCharacterCollection(_collectionName, 1, stream);
		if (_isCharacter != ownIsCharacter) {
			warning("Collection '%s' is declared %s but its check record says %s",
				_collectionName.c_str(), _isCharacter ? "character" : "numeric",
				ownIsCharacter ? "character" : "numeric");
			_isCharacter = ownIsCharacter;
		}

		const uint16 numEntries = stream.readUint16LE();
		for (uint i = 0; i < numEntries; ++i) {
			if (_isCharacter) {
				Common::String value;
				readFilename(stream, value);
				_answerStrings.push_back(value);
			} else {
				_answerNumbers.push_back(stream.readDoubleLE());
			}
		}

		return;
	}

	// GUESSED: which of the three flags goes with which comparison. Only the
	// middle one is ever populated together with a plausible count (1..5 entries
	// mapping to flags 1021..1025, and "as many entries as the collection holds"
	// mapping to the flag the button hotspots test to stop accepting input), and
	// the third is populated on exactly one record, the VEN keypad's "0 entries"
	// test, where either "not equal" or "greater than" gives the same answer.
	readTerseFlag(stream, _flagIfFewer);
	readTerseFlag(stream, _flagIfEqual);
	readTerseFlag(stream, _flagIfMore);
	_testCount = stream.readUint32LE();
}

void CollectionCheck::execute() {
	CollectionData::Collection *c = getCollection(_collectionName);
	if (!c) {
		_isDone = true;
		return;
	}

	if (_kind == 1) {
		bool matches;

		if (_isCharacter) {
			matches = c->strings.size() == _answerStrings.size();
			for (uint i = 0; matches && i < _answerStrings.size(); ++i) {
				matches = c->strings[i].equalsIgnoreCase(_answerStrings[i]);
			}
		} else {
			matches = c->numbers.size() == _answerNumbers.size();
			for (uint i = 0; matches && i < _answerNumbers.size(); ++i) {
				matches = c->numbers[i] == _answerNumbers[i];
			}
		}

		debugC(kDebugActionRecord, "Collection '%s' contents %s the answer",
			_collectionName.c_str(), matches ? "match" : "do not match");

		if (matches) {
			NancySceneState.setEventFlag(_flagOnMatch);
		}

		_isDone = true;
		return;
	}

	const uint32 size = c->size();
	debugC(kDebugActionRecord, "Collection '%s' holds %u entries, testing against %u",
		_collectionName.c_str(), size, _testCount);

	if (size < _testCount) {
		NancySceneState.setEventFlag(_flagIfFewer);
	} else if (size == _testCount) {
		NancySceneState.setEventFlag(_flagIfEqual);
	} else {
		NancySceneState.setEventFlag(_flagIfMore);
	}

	_isDone = true;
}

Common::String CollectionCheck::getRecordExtraInfo() const {
	if (_kind != 1) {
		return Common::String::format("Check collection '%s' has %u entries", _collectionName.c_str(), _testCount);
	}

	Common::String ret = Common::String::format("Check collection '%s' against", _collectionName.c_str());

	if (_isCharacter) {
		for (uint i = 0; i < _answerStrings.size(); ++i) {
			ret += Common::String::format(" '%s'", _answerStrings[i].c_str());
		}
	} else {
		for (uint i = 0; i < _answerNumbers.size(); ++i) {
			ret += Common::String::format(" %g", _answerNumbers[i]);
		}
	}

	return ret;
}

// AR 28 ------------------------------------------------------------------

void SetFlagsBasedOnInvCursor::readData(Common::SeekableReadStream &stream) {
	// uint16   always 1
	// uint16   always 0
	// int16, uint16   event flag, -1 on both records
	// uint16   always 1
	// uint32   inventory item ID the held item is compared against
	// int16, uint16   event flag set when the held item matches
	// int16, uint16   event flag, -1 on both records
	// uint16   always 1
	// uint16   always 0
	// rect     hotspot
	//
	// 2/2 exact. The event flags here are the four-byte form Nancy16 uses for
	// its counted flag lists, not the three-byte form AR 39 uses.
	stream.skip(2);
	stream.skip(2);

	_flagA.label = stream.readSint16LE();
	_flagA.flag = stream.readUint16LE();

	stream.skip(2);

	_itemID = stream.readUint32LE();

	_flagOnMatch.label = stream.readSint16LE();
	_flagOnMatch.flag = stream.readUint16LE();

	_flagB.label = stream.readSint16LE();
	_flagB.flag = stream.readUint16LE();

	stream.skip(2);
	stream.skip(2);

	readRect(stream, _hotspotRect);
}

void SetFlagsBasedOnInvCursor::execute() {
	switch (_state) {
	case kBegin:
		_state = kRun;
		// fall through
	case kRun:
		_hasHotspot = true;
		_hotspot = _hotspotRect;
		break;
	case kActionTrigger:
		_hasHotspot = false;

		// GUESSED, though it cannot misfire: both records are already gated on a
		// kCursorType dependency naming the same item, so the comparison can only
		// ever agree with it. _flagA and _flagB are -1 on both records, so
		// applying them is a no-op; they are applied anyway so the record does
		// not silently drop them if another game ever populates them.
		debugC(kDebugActionRecord, "Inv cursor check: holding %d, wanted %u",
			NancySceneState.getHeldItem(), _itemID);

		if (NancySceneState.getHeldItem() == (int16)_itemID) {
			NancySceneState.setEventFlag(_flagOnMatch);
		}

		NancySceneState.setEventFlag(_flagA);
		NancySceneState.setEventFlag(_flagB);

		finishExecution();
		break;
	}
}

Common::String SetFlagsBasedOnInvCursor::getRecordExtraInfo() const {
	return Common::String::format("Holding item %u sets flag %d to %u", _itemID, _flagOnMatch.label, _flagOnMatch.flag);
}

} // End of namespace Action
} // End of namespace Nancy
