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
#include "common/stack.h"
#include "common/config-manager.h"
#include "common/random.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/input.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/font.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/puzzledata.h"

#include "engines/nancy/action/actionmanager.h"
#include "engines/nancy/action/actionrecord.h"

#include "engines/nancy/action/secondarymovie.h"
#include "engines/nancy/action/soundrecords.h"

#include "engines/nancy/state/scene.h"
namespace Nancy {
namespace Action {

ActionManager::~ActionManager() {
	clearActionRecords();
}

void ActionManager::handleInput(NancyInput &input) {
	bool setHoverCursor = false;

	// First record that refused the click on a cursor dependency. Held rather
	// than acted on immediately, so the "can't use that" sound is only played
	// once the whole walk has failed to find a record that accepts the click.
	// See the long note in the walk below.
	ActionRecord *cantRecord = nullptr;

	for (auto &rec : _records) {
		if (rec->_isActive && !rec->_isDone) {
			// First, loop through all records and handle special cases.
			// This needs to be a separate loop to handle Overlays as a special case
			// (see note in Overlay::handleInput())
			rec->handleInput(input);
		}
	}

	// Record order IS the click priority, lowest index first. Nancy16 is no
	// exception, and a local "topmostWins" reversal for Nancy16+ used to live
	// here on the strength of one scene read without its dependencies. It was
	// wrong; see the measurement in parallel/hijack/HIJACK.md.
	//
	// Every rect in nancy18 that fully contains another (265 pairs, 1531 scenes)
	// was scored against the three candidate readings. Counting only hotspots
	// shadowed by a record that is live from the instant the scene loads:
	//
	//                                  index order   topmost   smallest-area
	//   hotspot never clickable                  1        33               0
	//   deliberate blocker never blocks          0       108             108
	//
	// The data is authored for index order, in two idioms that both need it:
	// specific hotspots first with an unconditional catch-all last (S3800,
	// S4407, S4720, S4722, S5212, S5810, S5830), and a conditional full-screen
	// blocker first that overrides the specifics while its flag is up (the
	// S5411-S5436 map screens, S3051's fax, S3250's mosaic).
	//
	// The sharpest consequence: S4722, S5212 and S5830 each hold five hotspots
	// under 200px^2, one live at a time by player-table value, and those fifteen
	// records are the ONLY writers of event flag 2502 in all 14143. Each sits
	// behind a 639x353 record with no dependencies at a higher index, so walking
	// topmost-first makes 2502 unreachable game-wide - and it has 25 readers,
	// S5400's SetValue among them.
	//
	// The desk that the reversal was introduced for does not need it: in S6401
	// the dictionary is AR17 gated on Inventory(13)==0 and the ticket is AR20
	// gated on Inventory(13)==1, so the two are never live at the same instant
	// and either order gives the same answer.
	for (auto &rec : _records) {
		if (	rec->_isActive &&
				!rec->_isDone &&
				rec->_hasHotspot &&
				rec->_hotspot.isValidRect() && // Needed for nancy2 scene 1600
				NancySceneState.getViewport().convertViewportToScreen(rec->_hotspot).contains(input.mousePos)) {
			if (!setHoverCursor) {
				// Hotspots may overlap, but we want the hover cursor for the first one we encounter
				// This fixes the stairs in nancy3
				g_nancy->_cursor->setCursorType(rec->getHoverCursor(), rec->cursorSetFromScript());
				setHoverCursor = true;
			}

			if (input.input & NancyInput::kLeftMouseButtonUp) {
				rec->_cursorDependency = nullptr;
				processDependency(rec->_dependencies, *rec, false);

				if (!rec->_dependencies.satisfied) {
					// A CURSOR dependency is the only way a record that is live
					// can still refuse a click: a hotspot record is drawn with
					// doNotCheckCursor set, so anything whose flags or inventory
					// failed is not _isActive and never reaches this loop at all.
					// So "unsatisfied here" means "wrong thing in hand", and the
					// right answer is to offer the click to the NEXT record on the
					// same point rather than to swallow it.
					//
					// MEASURED, S3803 (ZAT_SideEntXCU, the warehouse side entrance).
					// Two type-28 records share the rect screen(125,76,514,392):
					// AR3 needs an EMPTY hand and starts the get-caught chain, AR10
					// needs item 23 held and opens the keypad cover. Index order
					// reaches AR3 first; holding item 23 it fails its cursor test,
					// and because the click was consumed above the walk could never
					// reach AR10. Trace p10_gate: held=23, both records live, the
					// click at 319,234 produced no state change and no scene change
					// for the remaining 100 s. The side entrance was unreachable in
					// every state - which is why this was on the list as a route gap.
					//
					// This is not the topmost-vs-index question and does not reopen
					// it: the walk order is unchanged, and a record that accepts a
					// click today still accepts it, because every record before it
					// evaluates exactly as before. The only behaviour that changes
					// is a click that is currently guaranteed to do nothing.
					//
					// The "can't" sound is still played - just deferred to after the
					// walk, so it only fires when NOTHING on that point accepted the
					// click, which is what it means.
					if (rec->_cursorDependency != nullptr && cantRecord == nullptr) {
						cantRecord = rec;
					}

					continue;
				} else {
					input.input &= ~NancyInput::kLeftMouseButtonUp;
					cantRecord = nullptr;
					rec->_state = ActionRecord::ExecutionState::kActionTrigger;

					input.eatMouseInput();

					if (rec->_cursorDependency) {
						int16 item = rec->_cursorDependency->label;

						// Re-add the object to the inventory unless it's marked as a one-time use
						if (item == NancySceneState.getHeldItem() && item != -1 &&
								g_nancy->getGameType() >= kGameTypeNancy16) {
							// Nancy16 replaced INV with INVD, and INVD carries a single
							// keepItem BIT per item instead of INV's four-way enum. The
							// GetEngineData(INV) below returns null here and the assert
							// fires - which is why this whole branch had never run in
							// nancy18 until a cursor-gated record could be satisfied at
							// all (see the note above). Measured: S3803, step 4, "Assertion
							// failed: (inventoryData), actionmanager.cpp:160".
							const INVD *invd = (const INVD *)g_nancy->getEngineData("INVD");
							bool keep = true;
							if (invd) {
								for (uint k = 0; k < invd->items.size(); ++k) {
									if (invd->items[k].id == (uint16)item) {
										keep = invd->items[k].keepItem();
										break;
									}
								}
							}

							if (!keep) {
								NancySceneState.setHeldItem(-1);
							}
						} else if (item == NancySceneState.getHeldItem() && item != -1) {
							auto *inventoryData = GetEngineData(INV);
							assert(inventoryData);

							switch (inventoryData->itemDescriptions[item].keepItem) {
							case kInvItemKeepAlways :
								if (g_nancy->getGameType() >= kGameTypeNancy3) {
									// In nancy3 and up this means the object remains in hand, so do nothing
									// Older games had the kInvItemReturn behavior instead
									break;
								}

								// fall through
							case kInvItemReturn :
								NancySceneState.addItemToInventory(item);
								// fall through
							case kInvItemUseThenLose :
								NancySceneState.setHeldItem(-1);
								break;
							}
						}

						rec->_cursorDependency = nullptr;
					}

				}

				break;
			}
		}
	}

	// Nothing on that point took the click, and at least one record refused it
	// because of what was in hand. That - and only that - is what the "can't"
	// sound means.
	if (cantRecord != nullptr && cantRecord->_cursorDependency != nullptr &&
			(input.input & NancyInput::kLeftMouseButtonUp)) {
		input.input &= ~NancyInput::kLeftMouseButtonUp;
		NancySceneState.playItemCantSound(
			cantRecord->_cursorDependency->label,
			(g_nancy->getGameType() <= kGameTypeNancy2 &&
			 cantRecord->_cursorDependency->condition == kCursInvNotHolding));
	}
}

void ActionManager::addNewActionRecord(Common::SeekableReadStream &inputData) {
	ActionRecord *newRecord = createAndLoadNewRecord(inputData);
	if (!newRecord) {
		inputData.seek(0x30);
		byte ARType = inputData.readByte();

		warning("Action Record type %i is unimplemented or invalid!", ARType);
		return;
	}
	_records.push_back(newRecord);
}

// Width of the Nancy16+ "EndOfDeps" field that terminates the dependency block
static const uint kEndOfDepsSize = 32;

ActionRecord *ActionManager::createAndLoadNewRecord(Common::SeekableReadStream &inputData) {
	inputData.seek(0);
	char descBuf[0x30];
	inputData.read(descBuf, 0x30);
	descBuf[0x2F] = '\0';
	byte ARType = inputData.readByte();
	byte execType = inputData.readByte();

	// Nancy16 moved the dependency block from the end of the record to the start,
	// prefixed it with an explicit count and terminated it with an "EndOfDeps"
	// string. Read past it here so readData() below starts at the payload; the
	// dependencies themselves are parsed afterwards by the shared code, which
	// needs the record to exist first.
	int64 headDepsOffset = -1;
	uint headNumDeps = 0;
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		headNumDeps = inputData.readUint32LE();
		headDepsOffset = inputData.pos();
		inputData.skip(headNumDeps * 16);

		char sentinel[kEndOfDepsSize];
		inputData.read(sentinel, kEndOfDepsSize);
		sentinel[kEndOfDepsSize - 1] = '\0';
		if (Common::String(sentinel) != "EndOfDeps") {
			warning("Action record '%s' has %u dependencies but no EndOfDeps marker", descBuf, headNumDeps);
			return nullptr;
		}
	}

	ActionRecord *newRecord = createActionRecord(ARType, &inputData);

	if (!newRecord) {
		newRecord = new Unimplemented();
	}

	newRecord->_description = descBuf;
	newRecord->_type = ARType;
	newRecord->_execType = (ActionRecord::ExecutionType)execType;

	newRecord->readData(inputData);

	// A record whose class does not match this game's numbering reads past the
	// end of its own chunk. That used to walk off the buffer and crash inside
	// readRect; catching it here turns a mis-mapped type into one warning and a
	// skipped record, which is survivable and much easier to diagnose.
	if (inputData.eos() || inputData.pos() > inputData.size()) {
		warning("AUDIT overread type %u as %s", ARType, newRecord->getRecordTypeName().c_str());
		warning("Action record '%s' (type %u) read past the end of its chunk as %s; skipping it",
			descBuf, ARType, newRecord->getRecordTypeName().c_str());
		delete newRecord;
		return nullptr;
	}

	if (headDepsOffset >= 0 && inputData.pos() != inputData.size()) {
		warning("AUDIT underread type %u as %s: %d of %d bytes",
			ARType, newRecord->getRecordTypeName().c_str(),
			(int)inputData.pos(), (int)inputData.size());
	}

	// If the remaining data is less than the total data, there must be dependencies at the end of the chunk
	int64 dataRemaining = inputData.size() - inputData.pos();
	uint singleDepSize = g_nancy->getGameType() <= kGameTypeNancy2 ? 12 : 16;
	uint numDependencies = 0;
	bool haveDeps = false;

	if (headDepsOffset >= 0) {
		// Nancy16+: rewind to the block we skipped above
		inputData.seek(headDepsOffset);
		numDependencies = headNumDeps;
		haveDeps = true;
	} else if (dataRemaining > 0 && newRecord->getRecordTypeName() != "Unimplemented") {
		numDependencies = dataRemaining / singleDepSize;
		haveDeps = true;
	}

	if (haveDeps) {
		if (headDepsOffset < 0 && dataRemaining % singleDepSize) {
			warning("Action record type %u, %s has incorrect read size!\ndescription:\n%s",
				newRecord->_type,
				newRecord->getRecordTypeName().c_str(),
				newRecord->_description.c_str());

				delete newRecord;

				newRecord = new Unimplemented();
				newRecord->_description = descBuf;
				newRecord->_type = ARType;
				newRecord->_execType = (ActionRecord::ExecutionType)execType;
		}

		if (numDependencies == 0) {
			newRecord->_dependencies.satisfied = true;
		}

		Common::Stack<DependencyRecord *> depStack;
		depStack.push(&newRecord->_dependencies);

		// Initialize the dependencies data
		for (uint16 i = 0; i < numDependencies; ++i) {
			depStack.top()->children.push_back(DependencyRecord());
			DependencyRecord &dep = depStack.top()->children.back();

			if (singleDepSize == 12) {
				dep.type = (DependencyType)inputData.readByte();
				dep.label = inputData.readByte();
				dep.condition = inputData.readByte();
				dep.orFlag = inputData.readByte();
			} else if (singleDepSize == 16) {
				dep.type = (DependencyType)inputData.readUint16LE();
				dep.label = inputData.readUint16LE();
				dep.condition = inputData.readUint16LE();
				dep.orFlag = inputData.readUint16LE();
			}

			dep.hours = inputData.readSint16LE();
			dep.minutes = inputData.readSint16LE();
			dep.seconds = inputData.readSint16LE();
			dep.milliseconds = inputData.readSint16LE();

			switch (dep.type) {
			case DependencyType::kElapsedPlayerTime:
				dep.timeData = dep.hours * 3600000 + dep.minutes * 60000;

				if (g_nancy->getGameType() < kGameTypeNancy3) {
					// Older titles only checked if the time is less than the one in the dependency
					dep.condition = 0;
				}

				break;
			case DependencyType::kSceneCount:
				break;
			case DependencyType::kOpenParenthesis:
				depStack.push(&dep);
				break;
			case DependencyType::kCloseParenthesis:
				depStack.top()->children.pop_back();
				depStack.pop();
				break;
			default:
				if (dep.hours != -1 || dep.minutes != -1 || dep.seconds != -1) {
					dep.timeData = ((dep.hours * 60 + dep.minutes) * 60 + dep.seconds) * 1000 + dep.milliseconds;
				}

				break;
			}
		}
	} else {
		// Set new record to active if it doesn't depend on anything
		newRecord->_isActive = true;
	}

	return newRecord;
}

void ActionManager::processActionRecords() {
	_activatedRecordsThisFrame.clear();

	for (auto record : _records) {
		if (record->_isDone) {
			continue;
		}

		// Process dependencies every call. We make sure to ignore cursor dependencies,
		// as they are only handled when calling from handleInput()
		processDependency(record->_dependencies, *record, record->canHaveHotspot());
		record->_isActive = _previousRecordWasExecuted = record->_dependencies.satisfied;

		if (record->_isActive) {
			if(record->_state == ActionRecord::kBegin) {
				_activatedRecordsThisFrame.push_back(record);
			}

			record->execute();
		}

		if (g_nancy->getGameType() >= kGameTypeNancy4 && NancySceneState.getState() == State::Scene::kLoad) {
			// changeScene() must have been called, abort any further processing.
			// Both old and new behavior is needed (nancy3 intro narration, nancy4 garden gate)
			return;
		}
	}

	synchronizeMovieWithSound();

	// The hotspot overlay is a single shared RenderObject. A concurrent Nancy16
	// stream has no hotspots and would only wipe the main flow's boxes off it.
	if (!NancySceneState.getStreams().executing()) {
		debugDrawHotspots();
	}
}

// Nancy16 reuses dependency type 13 for a player-table value comparison:
// label is a table value index, condition is the operator, and the time fields
// carry the number compared against. Three things pin this down for the Dance
// Audition (S2670-72, S2770-72): the scene's SetValue record seeds value 38
// with 50, the DANCEPUZZLE NDUI binds its "DanceMeter" widget to Engine_Index
// 38 with range 0..100, and the scene's SceneChange records split on 38 vs
// 30/50. S2775 then gives the operators away by banding the same value into
// [50,60) [60,70) [70,80) [80,90) [90,-) with alternating conditions 2 and 3.
//
// Labels at or above kNumTimers cannot name a software-timer slot at all, so
// they are always the table reading. Labels below it are ambiguous on their
// face - but in this game they are overwhelmingly table indices too, and the
// money system is one of them:
//
//   * S1, the difficulty screen, runs SetValue(index 5, set, 200). That is the
//     200 euros the player starts with.
//   * Every shop then spends it with SetValue(index 5, add, -N) - the kiosk
//     magazines at -5, the costume shop's dress at -60, wig -40, gloves -5 -
//     and gates the purchase on a type-13 dependency with label 5.
//   * S5211 shows both halves side by side: the same magazine hotspot appears
//     twice, once under TimerLT(5, condition 2, 5) which buys it, and once
//     under TimerLT(5, condition 3, 5) whose only action is Nancy saying she
//     cannot afford it. Those are ">= 5" and "< 5" on the purse.
//
// Measured over the whole game: 37 scenes carry a type-13/14 dependency whose
// label is below kNumTimers. In 29 of them the same scene also writes that
// exact index with a SetValue record; in none of them does the same scene start
// that timer slot with AR 104. Read as timer slots these 351 dependencies are
// all permanently false, which silently disables the kiosk, the costume shop,
// the Rialto vendors, the gondolier fares and the microscope's exit.
//
// The rule below still gives a running timer priority, so no dependency that is
// satisfiable today changes meaning: an inactive slot's timer reading is
// unconditionally false, and comparePlayerTableValue is likewise false when the
// table holds no value at that index. It can only turn never-true into correct.
static bool isPlayerTableCompare(const DependencyRecord &dep) {
	if (g_nancy->getGameType() < kGameTypeNancy16) {
		return false;
	}

	if (dep.label >= (int16)TimerData::kNumTimers) {
		return true;
	}

	return !NancySceneState.isSoftwareTimerActive(dep.label);
}

bool comparePlayerTableValue(const DependencyRecord &dep) {
	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	if (!playerTable) {
		return false;
	}

	const int16 value = playerTable->getValue(dep.label);
	if (value == kNoTableValue) {
		return false;
	}

	// The `hours` field selects what the value is compared against: 0 means the
	// literal the time fields carry, 1 means the value held at the table index
	// they name. Only four dependencies in the whole game set it to 1, and all
	// four are the single scene-change record that opens the flooded vault's
	// hatch in S6700: (30 vs 31), (31 vs 32), (32 vs 33), (33 vs 34) - the five
	// pressure gauges the scene's five wheels drive, chained pairwise into "all
	// five are equal". The sign on the vault wall states the rule outright:
	// "il portello bloccherà se la pressione dell'acqua non è uguale".
	//
	// Folding hours into timeData the way the literal reading does gives
	// ((1*60)*60)*1000 + 31 = 3600031, which truncates to -4449 as an int16 and
	// can never equal a gauge reading of 1-10. The hatch could therefore never
	// open however the wheels were set, which is the other half of "the rotate
	// buttons do nothing".
	int16 against;
	if (dep.hours == 1) {
		against = playerTable->getValue((uint16)dep.milliseconds);
		if (against == kNoTableValue) {
			return false;
		}
	} else {
		against = (int16)(uint32)dep.timeData;
	}

	switch (dep.condition) {
	case 0:
		return value == against;
	case 1:
		return value > against;
	case 2:
		return value >= against;
	case 3:
		return value < against;
	case 4:
		return value <= against;
	case 5:
		// The sixth operator, "!=". Nancy16 uses it 12 times, always on player
		// table index 4 - the location register that S1/S2605 set to 1 (MIC),
		// S4420 to 2 (CAS), S5207/S5235 to 3 (KIO) and S4713/S4716/S4721/S4723
		// to 4 (COS) - and always as the exact complement of a cond-0 "== this
		// site" test in the same scene. Falling into `default: return false`
		// deleted the "you are NOT at this site, behave normally" half of four
		// scenes, and in S2600 (Campo San Polo) that is every exit the scene
		// has, including the only door to the dance club.
		return value != against;
	default:
		return false;
	}
}

void ActionManager::processDependency(DependencyRecord &dep, ActionRecord &record, bool doNotCheckCursor) {
	if (dep.children.size()) {
		// Recursively process child dependencies
		for (uint i = 0; i < dep.children.size(); ++i) {
			processDependency(dep.children[i], record, doNotCheckCursor);
		}

		// An orFlag marks that its corresponding dependency and the one after it
		// mutually satisfy each other; if one is satisfied, so is the other. The effect
		// can be chained indefinitely (for example, the chiming clock in nancy3)
		for (uint i = 0; i < dep.children.size(); ++i) {
			if (dep.children[i].orFlag) {
				// Found an orFlag, start going down the chain of dependencies with orFlags
				bool foundSatisfied = false;
				for (uint j = i; j < dep.children.size(); ++j) {
					if (dep.children[j].satisfied) {
						// A dependency has been satisfied
						foundSatisfied = true;
						break;
					}

					if (!dep.children[j].orFlag) {
						// orFlag chain ended, no satisfied dependencies
						break;
					}
				}

				if (foundSatisfied) {
					for (; i < dep.children.size(); ++i) {
						dep.children[i].satisfied = true;
						if (!dep.children[i].orFlag) {
							// Last element of orFlag chain
							break;
						}
					}
				}
			}
		}

		// If all children are satisfied, so is the parent
		dep.satisfied = true;
		for (uint i = 0; i < dep.children.size(); ++i) {
			if (!dep.children[i].satisfied) {
				dep.satisfied = false;
				break;
			}
		}
	} else {
		switch (dep.type) {
		case DependencyType::kNone:
			dep.satisfied = true;
			break;
		case DependencyType::kInventory:
			dep.satisfied = NancySceneState.hasItem(dep.label) == dep.condition;

			break;
		case DependencyType::kEvent:
			if (NancySceneState.getEventFlag(dep.label, dep.condition)) {
				// nancy1 has code for some timer array that never gets used
				// and is discarded from nancy2 onward
				dep.satisfied = true;
			} else {
				dep.satisfied = false;
			}

			break;
		case DependencyType::kLogic:
			if (g_nancy->getGameType() <= kGameTypeNancy2) {
				// First few games used 2 for false and 1 for true, but we store them the
				// other way around here. So, we need to check for inequality
				if (!NancySceneState.getLogicCondition(dep.label, dep.condition)) {
					// Wait for specified time before satisfying dependency condition
					Time elapsed = NancySceneState._timers.lastTotalTime - NancySceneState.getLogicConditionTimestamp(dep.label);

					if (elapsed >= dep.timeData) {
						dep.satisfied = true;
					} else {
						dep.satisfied = false;
					}
				} else {
					dep.satisfied = false;
				}
			} else {
				dep.satisfied = NancySceneState.getLogicCondition(dep.label, dep.condition);
			}

			break;
		case DependencyType::kElapsedGameTime:
			if (NancySceneState._timers.lastTotalTime >= dep.timeData) {
				dep.satisfied = true;
			} else {
				dep.satisfied = false;
			}

			break;
		case DependencyType::kElapsedSceneTime:
			// Inside a Nancy16 stream this is the stream's own clock, not the
			// player's - s3199's key-jiggling beats are timed against the stream
			// it runs in, which starts long after the scene the player is on.
			if (NancySceneState.getFlowSceneTime() >= dep.timeData) {
				dep.satisfied = true;
			} else {
				dep.satisfied = false;
			}

			break;
		case DependencyType::kElapsedPlayerTime: {
			// We're only interested in the hours and minutes
			Time playerTime = NancySceneState.getPlayerTime().getHours() * 3600000 +
								NancySceneState.getPlayerTime().getMinutes() * 60000;
			switch (dep.condition) {
			case 0:
				dep.satisfied = dep.timeData < playerTime;
				break;
			case 1:
				dep.satisfied = dep.timeData > playerTime;
				break;
			case 2:
				dep.satisfied = dep.timeData == playerTime;
			}

			break;
		}
		case DependencyType::kSceneCount: {
			// Check how many times a scene has been visited.
			// This dependency type keeps its data in the time variables
			// Note: nancy7 completely flipped the meaning of 1 and 2
			int count = NancySceneState.getSceneCounts(dep.hours);
			switch (dep.milliseconds) {
			case 0:
				// Nancy16 leaves the comparison selector at 0 on 15 of its 25
				// scene-count dependencies, which fell through this switch and
				// left `satisfied` at whatever it happened to be - so records
				// gated this way never ran. Equality is the reading that fits:
				// Nancy's opening narration is gated on visit 1 of the desk and
				// is meant to play once.
				dep.satisfied = (dep.minutes == count);
				break;
			case 1:
				if (	(dep.minutes < count && g_nancy->getGameType() <= kGameTypeNancy6) ||
						(dep.minutes > count && g_nancy->getGameType() >= kGameTypeNancy7)) {
					dep.satisfied = true;
				} else {
					dep.satisfied = false;
				}

				break;
			case 2:
				if (	(dep.minutes > count && g_nancy->getGameType() <= kGameTypeNancy6) ||
						(dep.minutes < count && g_nancy->getGameType() >= kGameTypeNancy7)) {
					dep.satisfied = true;
				} else {
					dep.satisfied = false;
				}

				break;
			case 3:
				// Selector 3 is used EXACTLY ONCE in nancy18 - S6718 record 3,
				// the drowning handler in the flooded water vault - and it is
				// the other half of a complementary pair with a selector-2
				// record on the same counted scene (6717) and the same
				// threshold (3):
				//
				//   idx 3  sel 3  thr 3  -> S6717   (go round again)
				//   idx 4  sel 2  thr 3  -> S6621   (give up on the vault)
				//
				// Selector 2 on Nancy7+ is "count > threshold", so 3 has to be
				// its complement, and it must be "count <= threshold" rather
				// than "count < threshold": with `<` the value count == 3 would
				// satisfy neither record and S6718 would have no live exit at
				// all. Reading it as equality (which is what this case did, a
				// straight duplicate of case 0) leaves BOTH halves false on the
				// first drowning - count arrives at 0 - so the player drowned
				// once and then sat in S6718 forever.
				//
				// Honest limit: with a single use in the corpus there is no
				// second instance to cross-check against. The evidence is the
				// complementary pairing, the fact that case 0 already covers
				// equality, and that this is the only reading that leaves the
				// scene a live exit for every possible count.
				dep.satisfied = (count <= dep.minutes);
				break;
			default:
				dep.satisfied = false;
				break;
			}

			break;
		}
		case DependencyType::kElapsedPlayerDay:
			if (g_nancy->getGameType() >= kGameTypeNancy12) {
				// Nancy12 repurposed dependency type 10 as a resource check (e.g. the
				// car's gas gauge): resource value vs. threshold, by condition modifier.
				int32 resVal = NancySceneState.getUIResource(dep.label);
				int32 threshold = dep.milliseconds;
				switch (dep.condition) {
				case 0:	// equal
					dep.satisfied = (resVal == threshold);
					break;
				case 1:	// resource greater than the threshold
					dep.satisfied = (resVal > threshold);
					break;
				case 2:	// resource greater than or equal to the threshold
					dep.satisfied = (resVal >= threshold);
					break;
				case 3:	// resource less than the threshold
					dep.satisfied = (resVal < threshold);
					break;
				case 4:	// resource less than or equal to the threshold
					dep.satisfied = (resVal <= threshold);
					break;
				default:
					dep.satisfied = false;
					break;
				}

				break;
			}

			if (record._days == -1) {
				record._days = NancySceneState.getPlayerTime().getDays();
				dep.satisfied = true;
				break;
			}

			if (record._days < NancySceneState.getPlayerTime().getDays()) {
				record._days = NancySceneState.getPlayerTime().getDays();

				// This is not used in nancy3 and up, so it's a safe assumption that we
				// do not need to check types recursively
				for (uint j = 0; j < record._dependencies.children.size(); ++j) {
					if (record._dependencies.children[j].type == DependencyType::kElapsedPlayerTime) {
						record._dependencies.children[j].satisfied = false;
					}
				}
			}

			break;
		case DependencyType::kCursorType: {
			if (doNotCheckCursor) {
				dep.satisfied = true;
			} else {
				bool isSatisfied = false;
				int heldItem = NancySceneState.getHeldItem();
				if (heldItem == -1 && dep.label == kCursStandard) {
					isSatisfied = true;
				} else {
					if (g_nancy->getGameType() <= kGameTypeNancy2 && dep.condition == kCursInvNotHolding) {
						// Activate if _not_ holding the specified item. Dropped in nancy3
						if (heldItem != dep.label) {
							isSatisfied = true;
						}
					} else {
						// Activate if holding the specified item.
						if (heldItem == dep.label) {
							isSatisfied = true;
						}
					}
				}

				dep.satisfied = isSatisfied;

				if (isSatisfied) {
					// A satisfied dependency must be moved into the _cursorDependency slot, to make sure
					// the remove from/re-add to inventory logic works correctly
					record._cursorDependency = &dep;
				} else {
					if (record._cursorDependency == nullptr) {
						// However, if the current dependency was not satisfied, we only move it into
						// the _cursorDependency slot if nothing else was there before. This ensures
						// the "can't" sound played is the first dependency's
						record._cursorDependency = &dep;
					}
				}
			}

			break;
		}
		case DependencyType::kPlayerTOD:
			if (dep.label == NancySceneState.getPlayerTOD()) {
				dep.satisfied = true;
			} else {
				dep.satisfied = false;
			}

			break;
		case DependencyType::kTimerLessThanDependencyTime:
			if (isPlayerTableCompare(dep)) {
				dep.satisfied = comparePlayerTableValue(dep);
			} else if (g_nancy->getGameType() >= kGameTypeNancy11) {
				// Nancy11+ checks a software-timer slot (label = slot index)
				dep.satisfied = NancySceneState.isSoftwareTimerActive(dep.label) &&
					NancySceneState.getSoftwareTimerElapsed(dep.label) <= (uint32)dep.timeData;
			} else {
				dep.satisfied = NancySceneState._timers.timerTime <= dep.timeData;
			}

			break;
		case DependencyType::kTimerGreaterThanDependencyTime:
			if (isPlayerTableCompare(dep)) {
				dep.satisfied = comparePlayerTableValue(dep);
			} else if (g_nancy->getGameType() >= kGameTypeNancy11) {
				dep.satisfied = NancySceneState.isSoftwareTimerActive(dep.label) &&
					(uint32)dep.timeData < NancySceneState.getSoftwareTimerElapsed(dep.label);
			} else {
				dep.satisfied = NancySceneState._timers.timerTime > dep.timeData;
			}

			break;
		case DependencyType::kTimerIsActive:
			// Nancy11+ only: satisfied while the software-timer slot is running/counting
			dep.satisfied = NancySceneState.isSoftwareTimerActive(dep.label);

			break;
		case DependencyType::kSoftwareTimerElapsed: {
			// Nancy16's software-timer compare. Type 13 became a value compare in
			// this game (see isPlayerTableCompare above) and the timer test moved
			// here. What pins it down: the label is only ever 4, 5 or 9, which is
			// exactly the set of timer slots that AR 104 starts but never gives a
			// duration to - every other slot fires its own event flags instead and
			// is never named by one of these. The times form a millisecond ladder
			// (0.8s, 0.9s, 1.2s ... 12.1s) driving a cue sequence, and in each
			// scene that uses one the largest time belongs to the AR 104 record
			// that clears the slot again: S4460 sets flags at 3s/4s/5s/6s and
			// clears slot 9 at 6.5s.
			const bool active = NancySceneState.isSoftwareTimerActive(dep.label);
			const uint32 elapsed = NancySceneState.getSoftwareTimerElapsed(dep.label);
			const uint32 against = (uint32)dep.timeData;

			// GUESS: only condition 1 occurs, in all 100 dependencies of this type,
			// and it has to mean "the slot has been running this long" for the cue
			// ladders to play in order. The other conditions are mirrored from the
			// Nancy16 value-compare table (0 ==, 1 >, 2 >=, 3 <, 4 <=) rather than
			// measured, since the game never uses them.
			switch (dep.condition) {
			case 0:
				dep.satisfied = active && elapsed == against;
				break;
			case 2:
				dep.satisfied = active && elapsed >= against;
				break;
			case 3:
				dep.satisfied = active && elapsed < against;
				break;
			case 4:
				dep.satisfied = active && elapsed <= against;
				break;
			case 1:
			default:
				dep.satisfied = active && elapsed > against;
				break;
			}

			break;
		}
		case DependencyType::kDifficultyLevel:
			if (dep.condition == NancySceneState.getDifficulty()) {
				dep.satisfied = true;
			} else {
				dep.satisfied = false;
			}

			break;
		case DependencyType::kClosedCaptioning:
			if (ConfMan.getBool("subtitles")) {
				if (dep.condition == 2) {
					dep.satisfied = true;
				} else {
					dep.satisfied = false;
				}
			} else {
				if (dep.condition == 1) {
					dep.satisfied = true;
				} else {
					dep.satisfied = false;
				}
			}

			break;
		case DependencyType::kSound:
			if (g_nancy->_sound->isSoundPlaying(dep.label)) {
				dep.satisfied = dep.condition == 1;
			} else {
				dep.satisfied = dep.condition == 0;
			}

			break;
		case DependencyType::kRandom:
			// Pick a random number and compare it with the value in condition
			// This is only executed once
			if (!dep.stopEvaluating) {
				if ((int)g_nancy->_randomSource->getRandomNumber(99) < dep.condition) {
					dep.satisfied = true;
				} else {
					dep.satisfied = false;
				}

				dep.stopEvaluating = true;
			}

			break;
		case DependencyType::kDefaultAR:
			dep.satisfied = !_previousRecordWasExecuted;
			break;
		default:
			warning("Unimplemented Dependency type %i", (int)dep.type);
			break;
		}
	}
}

void ActionManager::clearActionRecords(bool nextIsNoArt) {
	for (auto it = _records.begin(); it != _records.end(); ) {
		if ((*it)->survivesSceneChange(nextIsNoArt)) {
			++it;
			continue;
		}
		delete *it;
		it = _records.erase(it);
	}
	_activatedRecordsThisFrame.clear();
	_previousRecordWasExecuted = false;
}

void ActionManager::onPause(bool pause) {
	for (auto &r : _records) {
		if (r->_isActive && !r->_isDone) {
			r->onPause(pause);
		}
	}
}

void ActionManager::synchronize(Common::Serializer &ser) {
	// When loading, the records should already have been initialized by scene
	for (auto &rec : _records) {
		ser.syncAsByte(rec->_isActive);
		ser.syncAsByte(rec->_isDone);

		if (ser.isLoading()) {
			// Records restart fresh on load, just like a normal scene entry: clearing
			// _isDone lets one-shot records run again -- chained tutorial narration
			// sounds, conversations, overlays, etc. Anything that must stay "done" is
			// gated by its own dependencies (event flags, scene counts, inventory),
			// which are restored separately. Without this, a sound or conversation
			// that had finished before the save stays silent on load (e.g. the
			// Nancy 10 movement tutorial). _isActive is recomputed from the record's
			// dependencies on the next frame.
			rec->_isDone = false;
		}
	}
}

void ActionManager::synchronizeMovieWithSound() {
	// Improvement:

	// The original engine had really bad timing issues with AVF videos,
	// as it set the next frame time by adding the frame length to the current evaluation
	// time, instead of to the time the previous frame was drawn. As a result, all
	// movie (and SecondaryVideos) frames play about 12 ms slower than they should.
	// This results in some unfortunate issues in nancy4: if we do as the original
	// engine did and just make frames 12 ms slower, some dialogue scenes (like scene 1400)
	// are very visibly not in sync; also, the entire videocam sequence suffers from
	// visible stitches where the scene changes not at the time it was intended to.
	// On the other hand, if instead we don't add those 12ms, that same videocam
	// sequence has a really nasty sound cutoff in the middle of a character speaking.

	// This function intends to fix this issue by subtly manipulating the playback rate
	// of the movie so its length ends up matching that of the sound; if the sound rate was
	// changed instead, we would get slightly off-pitch dialogue, which would be undesirable.

	// The heuristic for catching these cases relies on the scene having a movie and a sound
	// record start at the same frame, and have a (valid) scene change to the same scene.
	PlaySecondaryMovie *movie = nullptr;
	PlaySound *sound = nullptr;

	for (uint i = 0; i < _activatedRecordsThisFrame.size(); ++i) {
		byte type = _activatedRecordsThisFrame[i]->_type;
		// Rely on _type for cheaper type check
		if (type == 53) {
			movie = dynamic_cast<PlaySecondaryMovie *>(_activatedRecordsThisFrame[i]);
		} else if (type == 150 || type == 151 || type == 157) {
			sound = dynamic_cast<PlaySound *>(_activatedRecordsThisFrame[i]);
		}

		if (movie && sound) {
			break;
		}
	}

	if (movie && sound && movie->_sound.name != "NO SOUND") {
		// A movie and a sound both got activated this frame, check if their scene changes match
		if (	movie->_videoSceneChange == PlaySecondaryMovie::kMovieSceneChange &&
				movie->_sceneChange.sceneID == sound->_sceneChange.sceneID &&
				movie->_sceneChange.sceneID != kNoScene) {
			// They match, check how long the sound is...
			Audio::Timestamp length = g_nancy->_sound->getLength(sound->_sound);

			if (length.msecs() != 0) {
				// ..and set the movie's playback speed to match
				movie->_decoder.setRate(Common::Rational(movie->_decoder.getDuration().msecs(), length.msecs()));
			}
		}
	}
}

void ActionManager::debugDrawHotspots() {
	// Draws a rectangle around (non-puzzle) hotspots as well as the id
	// and type of the owning ActionRecord. Hardcoded to font 0 since that's
	// the smallest one available in the engine.
	RenderObject &obj = NancySceneState._hotspotDebug;
	if (ConfMan.getBool("debug_hotspots", Common::ConfigManager::kTransientDomain)) {
		const Font *font = g_nancy->_graphics->getFont(0);
		assert(font);
		uint16 yOffset = NancySceneState.getViewport().getCurVerticalScroll();
		obj.setVisible(true);
		obj._drawSurface.clear(obj._drawSurface.getTransparentColor());

		for (uint i = 0; i < _records.size(); ++i) {
			ActionRecord *rec = _records[i];
			if (rec->_isActive && !rec->_isDone && rec->_hasHotspot) {
				Common::Rect hotspot = rec->_hotspot;
				hotspot.translate(0, -yOffset);
				hotspot.clip(obj._drawSurface.getBounds());

				if (!hotspot.isEmpty()) {
					font->drawString(&obj._drawSurface, Common::String::format("%u, %s", i, rec->getRecordTypeName().c_str()),
					hotspot.left, hotspot.bottom - font->getFontHeight() - 2, hotspot.width(), 0,
					Graphics::kTextAlignCenter, 0, true);
					obj._drawSurface.frameRect(hotspot, 0xFFFFFF);
				}
			}
		}
	} else {
		if (obj.isVisible()) {
			obj.setVisible(false);
		}
	}
}

} // End of namespace Action
} // End of namespace Nancy
