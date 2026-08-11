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

#ifndef NANCY_ACTION_DATARECORDS_H
#define NANCY_ACTION_DATARECORDS_H

#include "engines/nancy/action/actionrecord.h"

namespace Nancy {

class NancyEngine;

namespace Action {

// Changes the selected value inside the TableData. Value can be incremented, decremented, or not changed.
// Also responsible for checking whether all values are correct (as described in the TABL chunk). nancy6 only.
class TableIndexSetValueHS : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	CursorManager::CursorType getHoverCursor() const override { return (CursorManager::CursorType)_cursorType; }
	bool cursorSetFromScript() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "TableIndexSetValueHS"; }

	uint16 _tableIndex = 0;
	byte _valueChangeType = kNoChangeTableValue;
	int16 _entryCorrectFlagID = -1;
	int16 _allEntriesCorrectFlagID = -1;

	MultiEventFlagDescription _flags;
	uint16 _cursorType = 1;
	Common::Array<HotspotDescription> _hotspots;
};

// Sets (or adds to) a value inside the TableData struct
class SetValue : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::String getRecordExtraInfo() const override {
		return Common::String::format("Value %d -> %d (should set is %d)", _index, _value, _shouldSet);
	}

	// Read-only view of the operands. The vault-gauge autoplay hook builds each
	// wheel's effect on the five pressure gauges out of the SetValue records the
	// scene itself carries, rather than out of a table of its own that could
	// silently disagree with the shipped data; see vaultgaugeautoplay.cpp.
	byte getTableIndex() const { return _index; }
	bool getShouldSet() const { return _shouldSet; }
	int16 getOperand() const { return _value; }

protected:
	Common::String getRecordTypeName() const override { return "SetValue"; }

	byte _index = 0;
	bool _shouldSet = false;
	int16 _value = kNoTableValue;
};

class SetValueCombo : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::String getRecordExtraInfo() const override {
		return Common::String::format("Value %d", _valueIndex);
	}

protected:
	Common::String getRecordTypeName() const override { return "SetValueCombo"; }

	byte _valueIndex = 0;
	Common::Array<byte> _indices;
	Common::Array<int16> _percentages;
};

class ValueTest : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::String getRecordExtraInfo() const override {
		return Common::String::format("Value %d. Test type %d, condition %d. Set flag %d", _valueIndex, _testType, _condition, _flagToSet);
	}

protected:
	Common::String getRecordTypeName() const override { return "ValueTest"; }

	byte _valueIndex = 0;
	byte _testType = 0;
	byte _condition = 0;
	Common::Array<byte> _indicesToTest;

	int16 _flagToSet = kFlagNoLabel;
};

// Sets up to 10 flags at once.
class EventFlags : public ActionRecord {
public:
	EventFlags(bool terse = false) : _isTerse(terse) {}
	virtual ~EventFlags() {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	MultiEventFlagDescription _flags;
	bool _isTerse;

protected:
	Common::String getRecordTypeName() const override { return _isTerse ? "EventFlagsTerse" : "EventFlags"; }
};

// Sets up to 10 flags when clicked. Hotspot can move alongside background frame.
class EventFlagsMultiHS : public EventFlags {
public:
	EventFlagsMultiHS(bool isCursor, bool terse = false) : EventFlags(terse), _isCursor(isCursor) {}
	virtual ~EventFlagsMultiHS() {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	CursorManager::CursorType getHoverCursor() const override;
	bool cursorSetFromScript() const override;

	CursorManager::CursorType _hoverCursor = CursorManager::kHotspot;
	Common::Array<HotspotDescription> _hotspots;

	bool _isCursor;

	bool canHaveHotspot() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return _isCursor ? (_isTerse ? "EventFlagsHSTerse" : "EventFlagsCursorHS") : "EventFlagsMultiHS"; }
};

// Nancy 11+ AR 96. Sets each of a list of event flags to a random boolean value.
//
// Nancy16 (AR 92) is not that record. Its entries are 7 bytes rather than 2 -
// label, the value to set, and a uint32 weight - and the weights sum to exactly
// 100 in all 68 records. It is a d100 table from which exactly ONE entry fires.
//
// S2600 settles it beyond argument: nine of its records are
//   { flag 2673 = 1, weight 20 }, { flag 2673 = 0, weight 80 }
// - the same flag listed twice with opposite values. Under "set each entry from
// its own coin flip" the two entries fight over one flag and the weights mean
// nothing; under "roll once, one entry wins" it reads exactly as authored, "this
// is true one time in five". The 1041/1042/1043 pickers in S5480, S5007, S3090
// and S3093 are the same table with a 33/33/34 split, choosing which game-over
// card the failed puzzle shows.
class RandomizeEventFlags : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::Array<int16> _flagLabels;

	// Nancy16 only; empty for the older two-byte form
	Common::Array<byte> _flagValues;
	Common::Array<uint32> _weights;

protected:
	Common::String getRecordTypeName() const override { return "RandomizeEventFlags"; }
	Common::String getRecordExtraInfo() const override;
};

// Sets the difficulty level for the current save. Only appears at the start of the game.
// First appears in nancy1. Nancy1 and nancy2 have three difficulty values, while later games
// only have two: 0 and 2.
class DifficultyLevel : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	uint16 _difficulty = 0;
	FlagDescription _flag;

protected:
	Common::String getRecordTypeName() const override { return "DifficultyLevel"; }
};

class ModifyListEntry : public ActionRecord {
public:
	enum Type { kAdd, kDelete, kMark };

	ModifyListEntry(Type type) : _type(type) {}
	virtual ~ModifyListEntry() {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Type _type;

	uint16 _surfaceID = 0;
	Common::String _stringID;
	uint16 _mark = 0;
	uint16 _sceneID = kNoScene;

protected:
	Common::String getRecordTypeName() const override;
};

// --- Nancy16 journal / task-list entries -------------------------------
//
// Nancy16 moved the two lists out of the scene scripts entirely. Where Nancy10-15
// scatter AddListEntry records through the game (types 71/72/73, of which nancy18
// has *none* in its 14143 scene records), Nancy16 keeps one member per list per
// player character - JOURNAL_<character> and TASKLIST_<character> in
// ciftree.dat - each a TSUM plus one record per possible entry. The record's own
// dependencies are what decide whether the player has earned it yet, so the list
// is not accumulated as the game runs: it is re-derived from the flags every time
// the panel opens.
//
// Both records describe themselves "Add entry to UI Notebook Journal Page",
// on both types. As usual in this game the description is not evidence.
//
// These two classes are readers, not doers. Nothing executes them during play -
// they only ever appear in those members, and Scene::refreshJournal /
// refreshTasklist evaluate their dependencies and read the fields directly.

// Type 71 in Nancy16. Payload 66 bytes, exact: two 33-byte names.
class Nancy16JournalEntry : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override { _isDone = true; }

	// The heading this entry files under: VENJH1..VENJH4 in nancy18, which are
	// the roots of the JOURNAL member's four ScrollPanel chunks and resolve in
	// AUTOTEXT to "Observations", "Suspects", "Clues" and "Phone Numbers".
	Common::String _page;

	// Key into the AUTOTEXT CVTX table.
	Common::String _stringID;

protected:
	Common::String getRecordTypeName() const override { return "Nancy16JournalEntry"; }
};

// Type 72 in Nancy16. Payload 35 bytes, exact: a name and one uint16.
class Nancy16TaskEntry : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override { _isDone = true; }

	// Key into the AUTOTEXT CVTX table.
	Common::String _stringID;

	// The event flag that marks this task done, i.e. that ticks its check box.
	// The same fact Nancy10-15 carry in ModifyListEntry's trailing sceneID; see
	// NotebookPopup::toggleCheckbox, which already documents that reading.
	uint16 _completionFlag = 0;

protected:
	Common::String getRecordTypeName() const override { return "Nancy16TaskEntry"; }
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_DATARECORDS_H
