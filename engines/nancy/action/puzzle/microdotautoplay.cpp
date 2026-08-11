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

#include "common/config-manager.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/puzzledata.h"

#include "engines/nancy/action/actionmanager.h"
#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/action/puzzle/microdotautoplay.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {

// -- The microdot viewers, S3280 and S3281 ------------------------------------
//
// WHAT THE SCENES ARE. Nancy puts microdot A (or B) under Colin's microscope in
// the Piazza flat. S3280 and S3281 are the two halves of the slide: each carries
// two `PIA_MicroscopePUZ0n_OVL` overlays and a block of `ColinNN` narration
// lines, and the pair swaps back and forth on a five-second software timer while
// Colin talks over it.
//
// WHAT IS BYTE-EXACT. All of the following is read straight out of the two
// scenes' action records in the shipped data; none of it is inferred from art or
// from a name string:
//
//   S3280 rec  3 / S3281 rec  3   AR 104, timer slot 5, command kStart
//   S3280 rec 12 / S3281 rec 11   AR 90, dep SwTimer(5), sets scratch flag 1010
//   S3280 rec 16 / S3281 rec 15   AR 77 SetValue, index 7, shouldSet 0, value +1
//                                 -> increments player-table index 7 by one
//   S3280 rec 17 / S3281 rec 16   AR 16 SceneChange, to the OTHER viewer
//   S3280 rec 13 / S3281 rec 12   AR 91 EventFlagsCursorHS, ONE hotspot,
//                                 viewport rect (0,355)-(639,384), sets scratch
//                                 flag 1022, dep type 13 on table index 7,
//                                 condition 2, hours 0, literal 5
//   S3280 rec 15 / S3281 rec 14   AR 16 SceneChange -> S1280, dep flag 1022
//
// Condition 2 is ">=" in the engine's operator table, and hours 0 selects the
// literal reading, so the exit hotspot goes live exactly when player-table
// index 7 reaches 5 - five slide swaps, about twenty-five seconds of Colin.
//
// Player-table index 7 is used in exactly four places in all 14143 action
// records - the two SetValue records and the two dependencies above - so the
// counter is private to this puzzle and cannot be moved by anything else. That
// sweep is what makes it safe for this hook to key on the scene ids: they are
// the only two scenes the model can apply to.
//
// WHAT IS INFERRED. Only the reading of the puzzle as a puzzle: nothing in the
// data says "watch five cycles", it says "table 7 >= 5". Whether the designer
// meant the five swaps to be a viewing time or a count of something Colin says
// is not recoverable from the records, and the hook does not need it either way.
//
// WHY A HOOK IS NEEDED AT ALL. The viewer has no other live hotspot, so an
// automated run has nothing to click until the counter arrives, and a fixed
// click list cannot know when that is: nancy_autoclick_script advances its index
// on every firing rather than on every accepted click, so a script that clicks
// the strip early burns its steps against a dead rect and then aims the rest of
// the playthrough at the wrong screens. This hook fires one click, at the moment
// the record itself says it will be taken.
//
// HOW IT DECIDES. It does not count cycles and it does not carry the rect. It
// finds the viewer's only hotspot-bearing record, reads that record's own
// player-table dependency, and evaluates it through Action::comparePlayerTableValue()
// - the engine's own gate, the same function processDependency() calls, exported
// for this purpose. The model and the live check are therefore the same code by
// construction. The record's _isActive flag is then used as an independent second
// opinion: if the two ever disagree the hook says so and does nothing, rather
// than clicking into a rect the engine considers dead.
//
// nancy_microdot_autoplay defaults off; ordinary play is unchanged.

static bool autoPlay() {
	return ConfMan.hasKey("nancy_microdot_autoplay") && ConfMan.getBool("nancy_microdot_autoplay");
}

// The two slide halves. Keyed by id because the corpus sweep above proves these
// are the only two scenes whose exit is gated on player-table index 7.
static bool isMicrodotViewer(int sceneID) {
	return sceneID == 3280 || sceneID == 3281;
}

// The record's own player-table dependency, or null if it does not have exactly
// one. Both the "how many cycles are left" report and the "may I click" gate
// read the threshold from here, so neither can drift from the shipped data.
static const Action::DependencyRecord *findTableDependency(const Action::ActionRecord *rec) {
	const Action::DependencyRecord *found = nullptr;

	for (uint i = 0; i < rec->_dependencies.children.size(); ++i) {
		const Action::DependencyRecord &dep = rec->_dependencies.children[i];
		if (dep.type != Action::DependencyType::kTimerLessThanDependencyTime &&
				dep.type != Action::DependencyType::kTimerGreaterThanDependencyTime) {
			continue;
		}

		if (found) {
			return nullptr;
		}

		found = &dep;
	}

	return found;
}

// The viewer's exit: the scene's only record gated on a player-table compare.
// Found by shape rather than by index, and found whether or not it is live yet -
// EventFlagsMultiHS only publishes _hasHotspot from execute(), which the engine
// does not call until the dependency block is satisfied, so a search keyed on
// the hotspot could never see the record while the puzzle was still running.
// Returns null if the scene has no such record, or more than one: either would
// mean the reading above no longer matches the data, and guessing which record
// to click would be worse than leaving the puzzle alone.
static Action::ActionRecord *findExitRecord() {
	Action::ActionRecord *found = nullptr;

	for (auto &rec : NancySceneState.getActionManager().getActionRecords()) {
		if (rec->_isDone || !findTableDependency(rec)) {
			continue;
		}

		if (found) {
			return nullptr;
		}

		found = rec;
	}

	return found;
}

bool microdotAutoPlayNextClick(Common::Point &clickAt) {
	if (!autoPlay() || !State::Scene::hasInstance() || g_nancy->getState() != NancyState::kScene) {
		return false;
	}

	const int sceneID = (int)NancySceneState.getSceneInfo().sceneID;
	if (!isMicrodotViewer(sceneID)) {
		return false;
	}

	Action::ActionRecord *exitRec = findExitRecord();
	if (!exitRec) {
		return false;
	}

	const Action::DependencyRecord *dep = findTableDependency(exitRec);
	if (!dep) {
		return false;
	}

	// The engine's own evaluation of the record's own condition.
	const bool open = Action::comparePlayerTableValue(*dep);

	// Report the counter's progress once per value, so an unattended run leaves
	// a record of the puzzle being watched rather than merely being clicked.
	TableData *table = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	const int16 cycles = table ? table->getValue(dep->label) : -1;

	static int lastReported = -2;
	if (cycles != lastReported) {
		lastReported = cycles;
		debugC(1, kDebugScene, "Microdot autoplay: scene %d, table[%d] = %d, exit opens at %d, %s",
				sceneID, dep->label, cycles, (int)(int16)(uint32)dep->timeData,
				open ? "OPEN" : "still closed");
	}

	if (!open) {
		return false;
	}

	// Second opinion. _isActive is set by processActionRecords() from the same
	// dependency block, and _hasHotspot is published by the record's own
	// execute(); both have to agree with the gate above before a click is worth
	// synthesising. A poll or two of disagreement is normal - the gate opens
	// between the record being evaluated and it next being executed - so this is
	// a wait, not a fault.
	if (!exitRec->_isActive || !exitRec->_hasHotspot || !exitRec->_hotspot.isValidRect()) {
		return false;
	}

	const Common::Rect screen = NancySceneState.getViewport().convertViewportToScreen(exitRec->_hotspot);
	if (screen.isEmpty()) {
		return false;
	}

	clickAt = Common::Point(screen.left + screen.width() / 2, screen.top + screen.height() / 2);
	debugC(1, kDebugScene, "Microdot autoplay: taking the exit from scene %d at %d,%d after %d cycles",
			sceneID, clickAt.x, clickAt.y, cycles);
	return true;
}

} // End of namespace Nancy
