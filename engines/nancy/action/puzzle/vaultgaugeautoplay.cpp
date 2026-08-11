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

#include "common/array.h"
#include "common/config-manager.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/puzzledata.h"

#include "engines/nancy/action/actionmanager.h"
#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/action/datarecords.h"
#include "engines/nancy/action/puzzle/vaultgaugeautoplay.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {

// -- The flooded vault's pressure gauges, S6700 -------------------------------
//
// WHAT THE SCENE IS. Five valve wheels on the vault wall, each with a left and a
// right arrow - ten rotate buttons - driving five pressure gauges. The hatch
// opens when all five gauges read the same. The sign on the wall says so
// outright: "AVVERTIMENTO! Il portello bloccherà se la pressione dell'acqua non
// è uguale."
//
// It is the only timed puzzle of the four. Entering latches event flag 2637 and
// starts `Tunnel_456`, a 46-second one-shot on channel 10; a second record fires
// the explosion on "2637 is set and channel 10 has gone quiet", and every wheel
// button is additionally gated on `Sound(10 == 1)`, so when the air runs out the
// controls die in the same instant Nancy does. Measured on this build, entering
// from S6703 at ms 2606 and drowning at ms 5936+ - see HOOKS4.md - the budget is
// 46.3 s and one accepted click costs about 0.65 s, so the 19-click solution
// this planner finds lands around 12 s. The margin is real but it is not
// enormous, which is why the hook clicks the moment a button goes live rather
// than on a timer of its own.
//
// WHAT IS BYTE-EXACT. Everything the planner uses is read out of S6700's own
// action records at runtime; none of it is a table in this file. For the record,
// this is what those records say (134 records in the scene; the relevant ones
// decoded from the shipped data):
//
//   * Ten AR 91 EventFlagsCursorHS, one per arrow, each setting exactly one
//     scratch flag 1010+k and carrying exactly one hotspot. Each is gated on the
//     other nine 101x being clear AND on Sound(10 == 1).
//   * Per arrow, a chain 1010+k -> wheel-turn movie -> `Wheel_TurnSqeaky` ->
//     1020+k -> `Air_ReleaseCanister` -> 1030+k -> one to three AR 77 SetValue
//     records -> an AR 16 that reloads S6700 so the gauge overlays re-evaluate.
//   * The SetValue records all have shouldSet = 0, i.e. they ADD their operand
//     to the table value. Their operands are +1 on every left arrow and -1 on
//     every right arrow, so the ten buttons are five ± pairs:
//         wheel 1  gauges 30, 32
//         wheel 2  gauges 30, 31, 32
//         wheel 3  gauges 32, 34
//         wheel 4  gauge  33
//         wheel 5  gauges 33, 34
//   * Two AR 90 records, each an OR-chain of five table compares - "gauge 30 is
//     0 OR gauge 31 is 0 OR ..." and the same at 10 - raising flag 2636, which
//     is the drowning branch. So ANY gauge touching 0 or 10 is fatal; that is
//     the orFlag on those dependencies, and it is also what a run measured:
//     five presses of wheel 4's left arrow walk gauge 33 from 5 to 10 and the
//     explosion fires on the fifth.
//   * One AR 16 to S6701 with four dependencies (30 vs 31), (31 vs 32),
//     (32 vs 33), (33 vs 34), all with hours = 1 - the table-to-table compare
//     mode. Those four are the only dependencies in all 14143 action records
//     that set hours to anything but 0.
//   * S6703, the scene that leads in, seeds the gauges with five absolute
//     SetValue records: 30=3, 31=8, 32=6, 33=5, 34=4.
//
// WHAT IS INFERRED. Only two things, and both are cross-checked below:
//
//   * That the arrow whose record sets flag 1010+k is the arrow whose effect is
//     carried by the SetValue records gated on flag 1030+k. The chain between
//     them runs through two sound records and is not a single link the data
//     states in one place. It is checked at runtime: a button whose flag has no
//     matching effect list, or whose effect list is empty, is dropped from the
//     model and reported.
//   * That the goal is "the four compares in the solve record are all true"
//     rather than some looser reading of "equal pressure". The planner reads
//     those four pairs out of the record and uses exactly them; it does not
//     assume the five gauges form one equality class, even though for the
//     shipped chain they do.
//
// THE ALGORITHM. Not a stored move list, and not a greedy walk. The safe state
// space is tiny - five gauges, each confined to the nine readings strictly
// between the two fatal ones, is 9^5 = 59049 states - so the planner does a
// breadth-first search over it from the live gauge readings, with the ten
// buttons as edges and every fatal state simply absent from the graph. That
// gives the shortest click sequence that never passes through a reading which
// drowns Nancy, which is exactly what a timed puzzle wants, and it is derived
// rather than remembered: change the seed values and it re-solves.
//
// From the shipped seed 3/8/6/5/4 the search returns 19 clicks, which is also
// the algebraic minimum: writing the net turns of the five wheels as w1..w5, the
// gauge equations force w1 = +5, w3 = -3, w4 = -4 whatever the target reading T
// is, with w2 = T-8 and w5 = T-1, so the cost is 12 + |T-8| + |T-1| >= 19. The
// ordering is where the search earns its keep - doing the five w1 turns first
// would put gauge 32 at 11, and it drowns at 10.
//
// The plan is recomputed from the live table whenever the readings change rather
// than cached across the puzzle, so a click that does not land, or a gauge moved
// by anything else, self-corrects on the next pass.
//
// nancy_vault_autoplay defaults off; ordinary play is unchanged.

static const uint kMaxGauges = 6;
static const uint32 kMaxSearchStates = 4000000;

static bool autoPlay() {
	return ConfMan.hasKey("nancy_vault_autoplay") && ConfMan.getBool("nancy_vault_autoplay");
}

// The scratch-flag blocks the scene wires its arrows through. 1010+k is set by
// the arrow's own hotspot record, 1030+k gates that arrow's SetValue records.
static const int16 kFirstPressFlag = 1010;
static const int16 kFirstEffectFlag = 1030;
static const int16 kNumArrows = 10;

struct Button {
	Action::ActionRecord *rec = nullptr;	// the arrow's hotspot record
	int arrow = -1;							// k, i.e. flag 1010+k
	int8 delta[kMaxGauges] = { 0 };
};

struct Model {
	bool valid = false;

	Common::Array<uint16> gauges;			// table indices, solve-record order
	Common::Array<int> pairA, pairB;		// the solve record's equality pairs,
											// as offsets into `gauges`
	int16 lo = 0, hi = 0;					// inclusive safe reading range
	Common::Array<Button> buttons;
};

// True for the two dependency types that read the player table.
static bool isTableDep(const Action::DependencyRecord &dep) {
	return dep.type == Action::DependencyType::kTimerLessThanDependencyTime ||
			dep.type == Action::DependencyType::kTimerGreaterThanDependencyTime;
}

// The literal a table dependency compares against, in its hours = 0 reading.
static int16 depLiteral(const Action::DependencyRecord &dep) {
	return (int16)(uint32)dep.timeData;
}

// Offset of a table index within the model's gauge list, or -1.
static int gaugeSlot(const Model &m, uint16 tableIndex) {
	for (uint i = 0; i < m.gauges.size(); ++i) {
		if (m.gauges[i] == tableIndex) {
			return (int)i;
		}
	}

	return -1;
}

// Reads the whole puzzle out of the scene that is on screen. Returns an invalid
// model rather than a partial one if anything does not match the shape above;
// the caller then does nothing, which is the right failure for a debug hook.
static Model buildModel() {
	Model m;

	Common::Array<Action::ActionRecord *> &records = NancySceneState.getActionManager().getActionRecords();

	// 1. The solve record: the one whose dependencies are all table-to-table
	// compares (hours == 1). Those four dependencies name every gauge and state
	// the win condition, so they seed the model.
	for (auto &rec : records) {
		const Common::Array<Action::DependencyRecord> &deps = rec->_dependencies.children;
		if (deps.empty()) {
			continue;
		}

		bool allTableToTable = true;
		for (uint i = 0; i < deps.size(); ++i) {
			if (!isTableDep(deps[i]) || deps[i].hours != 1 || deps[i].condition != 0) {
				allTableToTable = false;
				break;
			}
		}

		if (!allTableToTable) {
			continue;
		}

		for (uint i = 0; i < deps.size(); ++i) {
			const uint16 a = (uint16)deps[i].label;
			const uint16 b = (uint16)deps[i].milliseconds;

			if (gaugeSlot(m, a) < 0) {
				m.gauges.push_back(a);
			}

			if (gaugeSlot(m, b) < 0) {
				m.gauges.push_back(b);
			}

			m.pairA.push_back(gaugeSlot(m, a));
			m.pairB.push_back(gaugeSlot(m, b));
		}

		break;
	}

	if (m.gauges.empty() || m.gauges.size() > kMaxGauges) {
		return m;
	}

	// 2. The fatal readings: the OR-chains of "some gauge equals L". Every such
	// literal is removed from the search space. The safe band is the widest run
	// of readings containing no fatal literal that the live readings sit inside;
	// with the shipped literals 0 and 10 that is 1..9.
	Common::Array<int16> fatal;
	for (auto &rec : records) {
		const Common::Array<Action::DependencyRecord> &deps = rec->_dependencies.children;
		if (deps.size() < 2) {
			continue;
		}

		bool orChainOfGauges = true;
		const int16 literal = depLiteral(deps[0]);
		for (uint i = 0; i < deps.size(); ++i) {
			if (!isTableDep(deps[i]) || deps[i].hours != 0 || deps[i].condition != 0 ||
					!deps[i].orFlag || depLiteral(deps[i]) != literal ||
					gaugeSlot(m, (uint16)deps[i].label) < 0) {
				orChainOfGauges = false;
				break;
			}
		}

		if (orChainOfGauges) {
			fatal.push_back(literal);
		}
	}

	if (fatal.empty()) {
		return m;
	}

	// 3. The ten arrows, and what each one does. The hotspot record is found by
	// the scratch flag it sets; the effect by the SetValue records gated on that
	// arrow's 1030+k.
	m.buttons.reserve(kNumArrows);
	for (int k = 0; k < kNumArrows; ++k) {
		Button b;
		b.arrow = k;

		for (auto &rec : records) {
			Action::EventFlagsMultiHS *hs = dynamic_cast<Action::EventFlagsMultiHS *>(rec);
			if (!hs || hs->_flags.descs.size() != 1 || hs->_hotspots.size() != 1) {
				continue;
			}

			if (hs->_flags.descs[0].label == kFirstPressFlag + k) {
				b.rec = rec;
				break;
			}
		}

		if (!b.rec) {
			continue;
		}

		bool sane = true;
		bool any = false;
		for (auto &rec : records) {
			Action::SetValue *sv = dynamic_cast<Action::SetValue *>(rec);
			if (!sv || sv->getShouldSet()) {
				continue;
			}

			bool gatedOnThisArrow = false;
			for (uint i = 0; i < sv->_dependencies.children.size(); ++i) {
				const Action::DependencyRecord &dep = sv->_dependencies.children[i];
				if (dep.type == Action::DependencyType::kEvent &&
						dep.label == kFirstEffectFlag + k && dep.condition == 1) {
					gatedOnThisArrow = true;
					break;
				}
			}

			if (!gatedOnThisArrow) {
				continue;
			}

			const int slot = gaugeSlot(m, sv->getTableIndex());
			const int16 operand = sv->getOperand();
			if (slot < 0 || operand < -100 || operand > 100) {
				sane = false;
				break;
			}

			b.delta[slot] = (int8)(b.delta[slot] + operand);
			any = true;
		}

		if (sane && any) {
			m.buttons.push_back(b);
		}
	}

	if (m.buttons.size() != kNumArrows) {
		return m;
	}

	// 4. The safe band, from the live readings outwards to the nearest fatal
	// reading on each side.
	TableData *table = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	if (!table) {
		return m;
	}

	int16 lowestLive = 0, highestLive = 0;
	for (uint i = 0; i < m.gauges.size(); ++i) {
		const int16 v = table->getValue(m.gauges[i]);
		if (v == kNoTableValue) {
			return m;
		}

		lowestLive = (i == 0 || v < lowestLive) ? v : lowestLive;
		highestLive = (i == 0 || v > highestLive) ? v : highestLive;
	}

	m.lo = lowestLive;
	m.hi = highestLive;
	for (;;) {
		bool grew = false;

		bool loSafe = true, hiSafe = true;
		for (uint i = 0; i < fatal.size(); ++i) {
			if (fatal[i] == (int16)(m.lo - 1)) {
				loSafe = false;
			}

			if (fatal[i] == (int16)(m.hi + 1)) {
				hiSafe = false;
			}
		}

		if (loSafe && m.lo > -1000) {
			--m.lo;
			grew = true;
		}

		if (hiSafe && m.hi < 1000) {
			++m.hi;
			grew = true;
		}

		if (!grew) {
			break;
		}
	}

	// A live reading that is itself fatal means the scene is already on its way
	// to the drowning branch; there is nothing to plan.
	for (uint i = 0; i < fatal.size(); ++i) {
		if (fatal[i] >= m.lo && fatal[i] <= m.hi) {
			return m;
		}
	}

	m.valid = true;
	return m;
}

static bool isGoal(const Model &m, const int16 *state) {
	for (uint i = 0; i < m.pairA.size(); ++i) {
		if (state[m.pairA[i]] != state[m.pairB[i]]) {
			return false;
		}
	}

	return true;
}

// Breadth-first search over the safe readings. Returns the index into
// m.buttons of the first click of a shortest safe solution, or -1.
// `outLength` receives the length of that solution.
static int planFirstClick(const Model &m, const int16 *start, uint &outLength) {
	const uint n = m.gauges.size();
	const uint32 span = (uint32)(m.hi - m.lo + 1);

	uint32 total = 1;
	for (uint i = 0; i < n; ++i) {
		if (total > kMaxSearchStates / span) {
			warning("Vault autoplay: %u gauges over %u readings is too large to search", n, (uint)span);
			return -1;
		}

		total *= span;
	}

	// Per state: which of the ten buttons the shortest path from `start` opens
	// with, plus a depth so the caller can report the plan length. 0 means
	// unvisited; a button index is stored as (index + 1).
	Common::Array<byte> firstMove(total, 0);
	Common::Array<uint16> depth(total, 0);

	int16 cur[kMaxGauges];
	uint32 startCode = 0;
	uint32 mul = 1;
	for (uint i = 0; i < n; ++i) {
		if (start[i] < m.lo || start[i] > m.hi) {
			return -1;
		}

		startCode += (uint32)(start[i] - m.lo) * mul;
		mul *= span;
	}

	if (isGoal(m, start)) {
		outLength = 0;
		return -1;
	}

	Common::Array<uint32> queue;
	queue.push_back(startCode);
	firstMove[startCode] = 0xFF;	// the start itself, distinguished from unvisited
	depth[startCode] = 0;

	for (uint head = 0; head < queue.size(); ++head) {
		const uint32 code = queue[head];

		uint32 rest = code;
		for (uint i = 0; i < n; ++i) {
			cur[i] = (int16)(m.lo + (int16)(rest % span));
			rest /= span;
		}

		for (uint b = 0; b < m.buttons.size(); ++b) {
			uint32 nextCode = 0;
			uint32 nmul = 1;
			bool safe = true;

			int16 next[kMaxGauges];
			for (uint i = 0; i < n; ++i) {
				next[i] = (int16)(cur[i] + m.buttons[b].delta[i]);
				if (next[i] < m.lo || next[i] > m.hi) {
					safe = false;
					break;
				}

				nextCode += (uint32)(next[i] - m.lo) * nmul;
				nmul *= span;
			}

			if (!safe || firstMove[nextCode] != 0) {
				continue;
			}

			firstMove[nextCode] = (code == startCode) ? (byte)(b + 1) : firstMove[code];
			depth[nextCode] = (uint16)(depth[code] + 1);

			if (isGoal(m, next)) {
				outLength = depth[nextCode];
				return (int)(firstMove[nextCode] - 1);
			}

			queue.push_back(nextCode);
		}
	}

	return -1;
}

bool vaultGaugeAutoPlayNextClick(Common::Point &clickAt) {
	if (!autoPlay() || !State::Scene::hasInstance() || g_nancy->getState() != NancyState::kScene) {
		return false;
	}

	const Model m = buildModel();
	if (!m.valid) {
		return false;
	}

	TableData *table = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	int16 state[kMaxGauges];
	for (uint i = 0; i < m.gauges.size(); ++i) {
		state[i] = table->getValue(m.gauges[i]);
	}

	// One click per distinct set of readings. Every button moves at least one
	// gauge, so the readings always change when a click is accepted; this stops
	// the same click being synthesised again in the polls between the click and
	// the record acting on it.
	static int16 lastClicked[kMaxGauges] = { 0 };
	static uint lastClickedCount = 0;
	bool sameAsLast = (lastClickedCount == m.gauges.size());
	for (uint i = 0; sameAsLast && i < m.gauges.size(); ++i) {
		sameAsLast = (lastClicked[i] == state[i]);
	}

	if (sameAsLast) {
		return false;
	}

	uint planLength = 0;
	const int choice = planFirstClick(m, state, planLength);
	if (choice < 0) {
		if (planLength == 0 && isGoal(m, state)) {
			// Solved. The scene's own record takes it from here; say so once so
			// an unattended run leaves the evidence in the log.
			static bool announced = false;
			if (!announced) {
				announced = true;
				debugC(1, kDebugScene, "Vault autoplay: gauges level, the hatch record has the scene");
			}
		}

		return false;
	}

	// The chosen arrow has to be one the engine would let a player click: its
	// record carries the "no other wheel is mid-turn" and "channel 10 still has
	// air" dependencies, so waiting for it to go live is waiting for exactly the
	// conditions the scene imposes.
	Action::ActionRecord *rec = m.buttons[choice].rec;
	if (!rec->_isActive || rec->_isDone || !rec->_hasHotspot || !rec->_hotspot.isValidRect()) {
		return false;
	}

	const Common::Rect screen = NancySceneState.getViewport().convertViewportToScreen(rec->_hotspot);
	if (screen.isEmpty()) {
		return false;
	}

	clickAt = Common::Point(screen.left + screen.width() / 2, screen.top + screen.height() / 2);

	for (uint i = 0; i < m.gauges.size(); ++i) {
		lastClicked[i] = state[i];
	}

	lastClickedCount = m.gauges.size();

	debugC(1, kDebugScene, "Vault autoplay: gauges %d/%d/%d/%d/%d, safe band %d..%d, "
			"%u clicks left, taking arrow %d at %d,%d",
			m.gauges.size() > 0 ? state[0] : -1, m.gauges.size() > 1 ? state[1] : -1,
			m.gauges.size() > 2 ? state[2] : -1, m.gauges.size() > 3 ? state[3] : -1,
			m.gauges.size() > 4 ? state[4] : -1, m.lo, m.hi,
			planLength, m.buttons[choice].arrow, clickAt.x, clickAt.y);
	return true;
}

} // End of namespace Nancy
