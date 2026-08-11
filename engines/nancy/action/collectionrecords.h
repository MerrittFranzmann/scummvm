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

#ifndef NANCY_ACTION_COLLECTIONRECORDS_H
#define NANCY_ACTION_COLLECTIONRECORDS_H

#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/puzzledata.h"

namespace Nancy {
namespace Action {

// The Nancy16 "collections" subsystem, ARs 37, 38 and 39. A collection is a
// named, ordered list of values that a puzzle fills in one entry at a time; the
// contents live in CollectionData so they survive a scene change (see the
// comment there). Four collections exist in nancy18:
//
//   FaxNumberEntries  character, unbounded, created in S2959 and S3051
//   CPuzzleBox        numeric, 4 entries,   created in S3275
//   ZAT_KeypadPUZ     numeric, 6 entries,   created in S3806
//   VEN_KeypadPUZ     numeric, 5 entries,   created in S6650
//
// The element type - a 33-byte string or an 8-byte IEEE double - is declared
// once, by the AR 37 creation record, and is NOT repeated in the AR 38 records
// that append to the collection. Those records are therefore only parseable
// once the declaration has been seen, which is why the declarations are kept in
// a parse-time registry (see getCollectionDeclaration in the .cpp) rather than
// being guessed from each record's length. Every scene holding AR 38 or AR 39
// records also holds the matching AR 37 creation record, ahead of them in the
// chunk order, so the registry is always populated in time.

// AR 37, "Collection creation". Two forms, picked by a leading mode byte:
// mode 0 declares a collection and its parameters, mode 1 destroys one.
class CollectionCreate : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	byte _mode = 0;
	Common::String _collectionName;

	// mode 0 only
	bool _overwrite = false;
	uint32 _maxEntries = 0;
	bool _isCharacter = false;
	Common::Rect _displayBounds;

protected:
	Common::String getRecordTypeName() const override { return "CollectionCreate"; }
	Common::String getRecordExtraInfo() const override;
};

// AR 38, "Collection editing". Appends values to a named collection.
class CollectionEdit : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	byte _mode = 0;
	Common::String _collectionName;
	bool _isCharacter = false;

	Common::Array<Common::String> _strings;
	Common::Array<double> _numbers;

protected:
	Common::String getRecordTypeName() const override { return "CollectionEdit"; }
	Common::String getRecordExtraInfo() const override;
};

// AR 39, "Collection checking". Two forms, picked by a leading kind byte:
// kind 1 tests the whole contents against a literal answer, kind 2 tests the
// number of entries against a literal count. Both report through event flags.
class CollectionCheck : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	byte _kind = 0;
	Common::String _collectionName;
	bool _isCharacter = false;

	// kind 1
	FlagDescription _flagOnMatch;
	Common::Array<Common::String> _answerStrings;
	Common::Array<double> _answerNumbers;

	// kind 2
	uint32 _testCount = 0;
	FlagDescription _flagIfFewer;
	FlagDescription _flagIfEqual;
	FlagDescription _flagIfMore;

protected:
	Common::String getRecordTypeName() const override { return "CollectionCheck"; }
	Common::String getRecordExtraInfo() const override;
};

// AR 28, "Set Flags Based on Inv Cursor". Not part of the collections subsystem;
// it shares this file because it came in with the same batch. A hotspot that
// sets an event flag when it is clicked while a particular inventory item is
// held. Both records in nancy18 are in S3803 and their item ID matches the
// kCursorType dependency the record is already gated on.
class SetFlagsBasedOnInvCursor : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	uint32 _itemID = 0;
	FlagDescription _flagOnMatch;
	FlagDescription _flagA;
	FlagDescription _flagB;
	Common::Rect _hotspotRect;

	bool canHaveHotspot() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "SetFlagsBasedOnInvCursor"; }
	Common::String getRecordExtraInfo() const override;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_COLLECTIONRECORDS_H
