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
#include "common/hashmap.h"
#include "common/tokenizer.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/puzzledata.h"
#include "engines/nancy/util.h"

#include "engines/nancy/action/actionmanager.h"
#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/action/datarecords.h"
#include "engines/nancy/action/miscrecords.h"
#include "engines/nancy/action/navigationrecords.h"
#include "engines/nancy/action/puzzle/watermazeautoplay.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {

// -- The water-well tunnel maze ----------------------------------------------
//
// WHAT THE PUZZLE IS. Under Venice, a set of flooded shafts joined by tunnels.
// Three water registers - player table indices 20, 21 and 22, each reading 0..3
// and displayed by the twelve `TUN_ValveMeter{A,B,C}0n` overlays every junction
// carries - hold three units of water between them, and the tunnel doors are
// gated on those readings. Valve wheels move one unit from one register to
// another. Nancy solves it by reading the flow chart at the close-up in s6729
// and working out which shafts to drain and flood in which order; the puzzle
// completes at S6097, whose record [0] is a dependency-free AR 90 raising event
// flag 2531, EV_Solved_Tunnel_Locks.
//
// WHY THIS HOOK IS NOT SHAPED LIKE THE OTHER AUTOPLAY HOOKS. The propane lock is
// one record; the microdot exit is one record in the current scene; the flooded
// vault is 134 records, all in the one scene, so its planner can read the whole
// puzzle out of memory and search it. This puzzle spans 223 scenes, and the
// engine only ever holds the current scene's action records - Scene::load()
// hands one S<id> IFF's ACT chunks to ActionManager and clearActionRecords()
// destroys them on the next scene change. A planner standing in one tunnel
// cannot see the tunnel next door.
//
// The obvious repair - parse every S<id> at boot and evaluate dependencies
// against a hypothetical state - is the wrong shape, and it is worth saying why
// rather than leaving the next reader to find out. Parsing the scenes is not the
// hard part. The hard part is that a maze edge is not a record. It is a chain
// wired through the scratch flags, and the links in that chain are made by the
// runtime behaviour of several different record classes rather than by anything
// stated in the data:
//
//     click -> AR 91 EventFlagsCursorHS raises scratch flag F
//           -> AR 40 SpecialEffect (a fade) raises another flag when it finishes
//           -> AR 44 movie raises another at one of its frames
//           -> AR 145 sound raises another when it stops
//           -> AR 77 SetValue moves one unit of water
//           -> AR 16 SceneChange goes back to the junction
//
// A `satisfied(dependency, hypothetical state)` evaluator does not help with any
// of that. To predict one edge offline you would have to simulate SpecialEffect,
// PlaySecondaryMovie's frame flags and sound completion against a hypothetical
// clock - a scene simulator, not a dependency evaluator - and every place that
// simulation disagreed with the engine would put a wrong edge in the map with
// nothing to catch it.
//
// So this hook does the opposite. It never evaluates a dependency itself and
// never predicts an outcome it has not seen. It reads the scene it is standing
// in, asks the engine which of that scene's clicks are live, takes one, and
// watches where it ends up. The map is built a click at a time out of things
// that actually happened. Everything below is bookkeeping around that.
//
// WHAT IS BYTE-EXACT. Everything the hook uses is read from the shipped records
// at runtime; there is no table of scenes, valves, water levels or routes in
// this file, and no route is stored between runs.
//
//   * The water registers are the player-table indices named by the type-13/14
//     dependencies of the scenes the hook walks through, accumulated as it goes.
//     In the shipped data that set is exactly {20, 21, 22}: of the 1470 table
//     dependencies in the 244 scenes of the two tunnel id ranges, 435 name 20,
//     434 name 21 and 437 name 22, and the remaining 164 are the five pressure
//     gauges of the flooded vault (S6700/S6701/S6703), which is a separate
//     component this hook never reaches.
//   * A move is a live hotspot record that the scene itself wires to a scene
//     change: either a SceneChange subclass, which names its destination
//     outright, or an EventFlagsMultiHS whose scratch flag reaches a SceneChange
//     through the scene's own records. Nothing else is clicked - which is what
//     keeps the hook off the flow-chart close-up, whose hotspot raises 1025 into
//     an AR 26 "Begin a stream" and not into a scene change at all.
//   * Liveness is the engine's own: a record is clickable when the dependency
//     evaluator has made it `_isActive`, it is not `_isDone`, and its own
//     execute() has published a hotspot. The hook does not re-derive that.
//
// WHAT IS INFERRED. Three things, all of them the hook's own conduct rather than
// claims about the game, and each one is either checked at runtime or fails safe:
//
//   * That the two-hop flag closure below finds the destination of a valve
//     click. It follows EventFlags::_flags and SpecialEffect::_flagOnCompletion
//     only, so a chain running through a movie or a sound is not resolved. This
//     is used solely to order the search - "have I been to that scene?" - and a
//     move whose destination cannot be read is simply searched sooner rather
//     than later. The destination that goes into the map is always the observed
//     one.
//   * That the maze is the two scene id ranges 6000-6199 and 6600-6799. Measured:
//     the raw scene-change adjacency of those 244 scenes has one component of
//     223 that contains S6097, and exactly two records leave it - S6002 to S4720
//     (climbing out of the first well into the courtyard) and S6719 to S5400.
//     This is a leash on a debug hook, not part of the solve: when a door turns
//     out to lead out of the maze the hook steps back through it and marks it,
//     which is state-preserving because the water registers are global. Override
//     with nancy_maze_region if the reading is ever revised.
//   * That a scene has settled when its set of live moves has not changed for
//     nancy_maze_settle consecutive input polls. Poll counts, not wall clock:
//     this build's poll rate has measured anywhere from 30/s to 60/s, so a
//     hook that timed anything in milliseconds of its own would be wrong on one
//     of those runs.
//
// THE ALGORITHM. Frontier search over a map that is being built as it is walked.
// A node is (scene, water readings). At the node it is standing on the hook
// knows the live moves, because it just read them; for every node it has stood
// on before it remembers them, and for every move it has taken it remembers
// where that move came out. To pick the next click:
//
//   1. Breadth-first over the known edges for the nearest node holding a move
//      that has never been taken AND whose destination scene has never been
//      seen (or cannot be read). That is the frontier: it makes the walk cover
//      scenes rather than exhaust one junction's four valves at all ten water
//      distributions.
//   2. Failing that, the nearest node holding any move that has never been
//      taken. This is where the valves get turned: once every door the current
//      water opens has been walked through, the only untaken moves left are the
//      ones that move water, and turning them opens doors that were shut.
//   3. Failing that, the reachable part of the maze has been walked in full and
//      the hook stands down and says so.
//
// The click is the first step of that path, on the chosen record's own hotspot,
// so every move the hook makes is a move the engine had already decided a player
// could make. There is no timing in it: it clicks, waits to arrive, and reads
// the new scene.
//
// The evidence a run leaves is the game's own. Whenever the scene changes the
// hook diffs the engine's event-flag array and reports any flag that has been
// raised, so an unattended run records 2531 being set by S6097's record, in the
// log, in the middle of the scene chain that got there.
//
// nancy_maze_autoplay defaults off; ordinary play is unchanged.

// Room for more registers than the three the maze uses, so an unexpected fourth
// is reported rather than silently folded into the key.
static const uint kMaxRegisters = 6;

// Node key: scene id in the low 16 bits, then one 6-bit field per register, then
// the number of registers. 63 means "the table holds no value there", which is
// how register 22 reads until the first transfer writes it.
static const uint kUnsetReading = 63;

static const uint64 kEdgeUnknown = 0;			// never taken
static const uint64 kEdgeLeavesMaze = 1;		// taken, and it left the region
static const uint64 kEdgeDead = 2;				// taken, and nothing happened

static bool autoPlay() {
	return ConfMan.hasKey("nancy_maze_autoplay") && ConfMan.getBool("nancy_maze_autoplay");
}

struct Move {
	uint16 rec = 0;			// index into the scene's action records
	int destScene = -1;		// as named by the scene's own records, or -1
};

struct Maze {
	bool started = false;
	bool finished = false;

	// The registers, learned from the dependencies of the scenes walked through.
	Common::Array<uint16> registers;

	// The map. `moves` is what was live at a node; `edges` is where a move came
	// out, keyed by (node << 8 | record index).
	Common::HashMap<uint64, Common::Array<Move> > moves;
	Common::HashMap<uint64, uint64> edges;
	Common::HashMap<uint32, bool> seenScenes;

	// Where the hook is and how sure it is of that.
	int scene = -1;
	uint32 sceneChanges = 0;
	uint settle = 0;
	uint32 movesHash = 0;

	// The move in flight.
	bool pending = false;
	uint64 pendingNode = 0;
	uint16 pendingRec = 0;
	uint32 pendingAt = 0;
	uint pendingPolls = 0;

	// The last place inside the maze, so a door that leads out can be stepped
	// back through.
	bool haveAnchor = false;
	SceneChangeDescription anchor;
	uint outsidePolls = 0;

	uint32 clicks = 0;
	Common::String flagsSeen;
};

// Allocated on first use rather than at namespace scope: a global with a
// non-trivial constructor is a warning this tree does not otherwise carry, and
// the state is meant to live for the run anyway.
static Maze *g_maze = nullptr;

static Maze &maze() {
	if (!g_maze) {
		g_maze = new Maze();
	}

	return *g_maze;
}

// --- the region leash -------------------------------------------------------

static bool inRegion(int scene) {
	const Common::String spec = ConfMan.hasKey("nancy_maze_region") ?
		ConfMan.get("nancy_maze_region") : Common::String("6000-6199,6600-6799");

	Common::StringTokenizer ranges(spec, ",");
	while (!ranges.empty()) {
		const Common::String range = ranges.nextToken();
		const uint dash = range.findFirstOf('-');
		if (dash == Common::String::npos) {
			continue;
		}

		const int lo = atoi(range.substr(0, dash).c_str());
		const int hi = atoi(range.substr(dash + 1).c_str());
		if (scene >= lo && scene <= hi) {
			return true;
		}
	}

	return false;
}

// --- reading the scene ------------------------------------------------------

// The scratch flags, taken from the engine's own table rather than restated
// here: populateStaticData fills genericEventFlags contiguously, so its ends
// bound the band, which is what Scene::scratchFlagIndex uses to route a label to
// the running stream's scratch array instead of the persistent one.
static bool isScratchFlag(int16 label) {
	const Common::Array<uint16> &generic = g_nancy->getStaticData().genericEventFlags;
	if (generic.empty()) {
		return false;
	}

	return label >= (int16)generic[0] && label <= (int16)generic[generic.size() - 1];
}

// True when this record's dependency block waits on `label` being raised.
static bool waitsOnFlag(const Action::ActionRecord *rec, int16 label) {
	const Common::Array<Action::DependencyRecord> &deps = rec->_dependencies.children;
	for (uint i = 0; i < deps.size(); ++i) {
		if (deps[i].type == Action::DependencyType::kEvent &&
				deps[i].label == label && deps[i].condition == g_nancy->_true) {
			return true;
		}
	}

	return false;
}

// The scratch flags this record raises, for the two classes that state them
// plainly. Anything else contributes nothing and the chain stops there.
static void flagsRaisedBy(const Action::ActionRecord *rec, Common::Array<int16> &out) {
	if (const Action::EventFlags *ef = dynamic_cast<const Action::EventFlags *>(rec)) {
		for (uint i = 0; i < ef->_flags.descs.size(); ++i) {
			if (ef->_flags.descs[i].flag == g_nancy->_true && isScratchFlag(ef->_flags.descs[i].label)) {
				out.push_back(ef->_flags.descs[i].label);
			}
		}

		return;
	}

	if (const Action::SpecialEffect *fx = dynamic_cast<const Action::SpecialEffect *>(rec)) {
		if (fx->_flagOnCompletion.flag == g_nancy->_true && isScratchFlag(fx->_flagOnCompletion.label)) {
			out.push_back(fx->_flagOnCompletion.label);
		}
	}
}

// Where a click that raises `start` eventually sends the player, as far as this
// scene's own records say. Returns kNoScene when the chain does not reach a
// scene change - which is the test that keeps the flow-chart close-up, whose
// flag feeds an AR 26 "Begin a stream", out of the move list entirely.
//
// `resolved` distinguishes "reaches a scene change whose id I read" from
// "reaches a scene change" - the two are the same here, but the caller wants to
// know which moves it could not read so it can search them first.
static int destinationOfFlag(Common::Array<Action::ActionRecord *> &records, int16 start, uint depth) {
	Common::Array<int16> closed;
	closed.push_back(start);

	for (uint hop = 0; hop <= depth; ++hop) {
		Common::Array<int16> grown;

		for (uint f = 0; f < closed.size(); ++f) {
			for (uint i = 0; i < records.size(); ++i) {
				if (!waitsOnFlag(records[i], closed[f])) {
					continue;
				}

				if (const Action::SceneChange *sc = dynamic_cast<const Action::SceneChange *>(records[i])) {
					return (int)sc->_sceneChange.sceneID;
				}

				flagsRaisedBy(records[i], grown);
			}
		}

		bool anyNew = false;
		for (uint i = 0; i < grown.size(); ++i) {
			bool known = false;
			for (uint j = 0; j < closed.size(); ++j) {
				known |= (closed[j] == grown[i]);
			}

			if (!known) {
				closed.push_back(grown[i]);
				anyNew = true;
			}
		}

		if (!anyNew) {
			break;
		}
	}

	return kNoScene;
}

// The clicks this scene offers right now, in record order.
static void readMoves(Common::Array<Move> &out) {
	Common::Array<Action::ActionRecord *> &records = NancySceneState.getActionManager().getActionRecords();

	for (uint i = 0; i < records.size(); ++i) {
		Action::ActionRecord *rec = records[i];
		if (rec->_isDone || !rec->_isActive || !rec->_hasHotspot || !rec->_hotspot.isValidRect()) {
			continue;
		}

		Move m;
		m.rec = (uint16)i;

		if (const Action::SceneChange *sc = dynamic_cast<const Action::SceneChange *>(rec)) {
			m.destScene = (int)sc->_sceneChange.sceneID;
			out.push_back(m);
			continue;
		}

		const Action::EventFlagsMultiHS *hs = dynamic_cast<const Action::EventFlagsMultiHS *>(rec);
		if (!hs) {
			continue;
		}

		// A hotspot that raises no scratch flag cannot be wired to anything in
		// this scene, and one whose chain never reaches a scene change is not a
		// move - it is a close-up, a line of dialogue or a sound.
		int dest = kNoScene;
		for (uint f = 0; f < hs->_flags.descs.size() && dest == kNoScene; ++f) {
			if (hs->_flags.descs[f].flag != g_nancy->_true || !isScratchFlag(hs->_flags.descs[f].label)) {
				continue;
			}

			dest = destinationOfFlag(records, hs->_flags.descs[f].label, 2);
		}

		if (dest == kNoScene) {
			continue;
		}

		m.destScene = dest;
		out.push_back(m);
	}
}

// --- the node key -----------------------------------------------------------

// Grows the register set with any player-table index this scene compares
// against. The set only ever grows; the width goes into the key, so nodes keyed
// before a register was discovered simply fall out of use rather than aliasing
// onto nodes keyed after it.
static void learnRegisters() {
	Common::Array<Action::ActionRecord *> &records = NancySceneState.getActionManager().getActionRecords();

	for (uint i = 0; i < records.size(); ++i) {
		const Common::Array<Action::DependencyRecord> &deps = records[i]->_dependencies.children;
		for (uint d = 0; d < deps.size(); ++d) {
			if (deps[d].type != Action::DependencyType::kTimerLessThanDependencyTime &&
					deps[d].type != Action::DependencyType::kTimerGreaterThanDependencyTime) {
				continue;
			}

			// hours == 1 is the table-to-table compare; both sides name an index.
			for (uint side = 0; side < (deps[d].hours == 1 ? 2u : 1u); ++side) {
				const uint16 index = side == 0 ? (uint16)deps[d].label : (uint16)deps[d].milliseconds;

				bool known = false;
				for (uint r = 0; r < maze().registers.size(); ++r) {
					known |= (maze().registers[r] == index);
				}

				if (known) {
					continue;
				}

				if (maze().registers.size() >= kMaxRegisters) {
					warning("Maze autoplay: more than %u player-table registers in play, ignoring index %u",
							kMaxRegisters, index);
					continue;
				}

				// Kept sorted so the key does not depend on the order the
				// scenes happened to be walked in.
				uint at = 0;
				while (at < maze().registers.size() && maze().registers[at] < index) {
					++at;
				}

				maze().registers.insert_at(at, index);
			}
		}
	}
}

static uint64 nodeKey(int scene) {
	TableData *table = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());

	uint64 key = (uint64)(scene & 0xFFFF);
	for (uint i = 0; i < maze().registers.size() && i < kMaxRegisters; ++i) {
		uint reading = kUnsetReading;
		if (table) {
			const int16 v = table->getValue(maze().registers[i]);
			if (v != kNoTableValue && v >= 0 && v < (int16)kUnsetReading) {
				reading = (uint)v;
			}
		}

		key |= ((uint64)reading) << (16 + 6 * i);
	}

	key |= ((uint64)(maze().registers.size() & 0xF)) << 52;
	return key;
}

static uint64 edgeKey(uint64 node, uint16 rec) {
	return (node << 8) | (rec & 0xFF);
}

static Common::String readingsString() {
	TableData *table = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	Common::String out;

	for (uint i = 0; i < maze().registers.size(); ++i) {
		const int16 v = table ? table->getValue(maze().registers[i]) : kNoTableValue;
		out += Common::String::format("%s%u=", i ? "/" : "", maze().registers[i]);
		out += (v == kNoTableValue) ? Common::String("-") : Common::String::format("%d", v);
	}

	return out;
}

// --- the search -------------------------------------------------------------

static bool isFrontier(const Move &m, bool requireNewScene) {
	if (requireNewScene) {
		// A move whose destination cannot be read is treated as new, because it
		// is exactly the move whose outcome the map does not have.
		return m.destScene == kNoScene || m.destScene < 0 ||
				!maze().seenScenes.contains((uint32)m.destScene);
	}

	return true;
}

// Breadth-first over the known edges from `from` for the nearest node holding an
// untaken move. Returns the record index of the first click of that path, or -1.
static int planFirstClick(uint64 from, bool requireNewScene, uint &outDistance) {
	Common::Array<uint64> queue;
	Common::HashMap<uint64, int> firstMove;		// node -> record index at `from`
	Common::HashMap<uint64, uint> distance;

	queue.push_back(from);
	firstMove[from] = -1;
	distance[from] = 0;

	for (uint head = 0; head < queue.size(); ++head) {
		const uint64 node = queue[head];
		if (!maze().moves.contains(node)) {
			continue;
		}

		const Common::Array<Move> &here = maze().moves[node];
		for (uint i = 0; i < here.size(); ++i) {
			const uint64 ek = edgeKey(node, here[i].rec);
			const uint64 known = maze().edges.contains(ek) ? maze().edges[ek] : kEdgeUnknown;

			if (known == kEdgeUnknown) {
				if (!isFrontier(here[i], requireNewScene)) {
					continue;
				}

				outDistance = distance[node];
				return (node == from) ? (int)here[i].rec : firstMove[node];
			}

			if (known == kEdgeLeavesMaze || known == kEdgeDead) {
				continue;
			}

			if (firstMove.contains(known)) {
				continue;
			}

			firstMove[known] = (node == from) ? (int)here[i].rec : firstMove[node];
			distance[known] = distance[node] + 1;
			queue.push_back(known);
		}
	}

	return -1;
}

// --- evidence ---------------------------------------------------------------

// Reports every event flag the game has raised since the last look. The list
// comes from the engine's own array, so this is a record of what the shipped
// records did, not of what the hook expected them to do.
static void reportNewFlags(int scene) {
	const Common::String now = NancySceneState.debugAllEventFlags();
	if (now == maze().flagsSeen) {
		return;
	}

	Common::StringTokenizer fresh(now, ",");
	while (!fresh.empty()) {
		const Common::String token = fresh.nextToken();

		bool had = false;
		Common::StringTokenizer old(maze().flagsSeen, ",");
		while (!old.empty()) {
			if (old.nextToken() == token) {
				had = true;
				break;
			}
		}

		if (had) {
			continue;
		}

		// Scene::eventFlagToIndex subtracts 1000 from any label at or above it,
		// so an index at or above 1000 came from a 2xxx game flag.
		const int index = atoi(token.c_str());
		debugC(1, kDebugScene, "Maze autoplay: scene %d raised event flag %d",
				scene, index >= 1000 ? index + 1000 : index);
	}

	maze().flagsSeen = now;
}

// --- the poll ---------------------------------------------------------------

bool waterMazeAutoPlayNextClick(Common::Point &clickAt) {
	if (!autoPlay() || maze().finished) {
		return false;
	}

	if (!State::Scene::hasInstance() || g_nancy->getState() != NancyState::kScene ||
			NancySceneState.getState() != State::Scene::kRun) {
		maze().settle = 0;
		return false;
	}

	const int scene = debugGetCurrentSceneID();
	if (scene < 0) {
		return false;
	}

	if (scene != maze().scene) {
		maze().scene = scene;
		++maze().sceneChanges;
		maze().settle = 0;
		maze().seenScenes[(uint32)scene] = true;
		reportNewFlags(scene);

		if (!maze().started) {
			maze().started = true;
			debugC(1, kDebugScene, "Maze autoplay: walking the tunnels from scene %d", scene);
		}
	}

	// The leash. Two records in the maze lead out of it; when one of them is
	// taken, step back through it and never take it again. The water registers
	// are global, so nothing about the puzzle state is disturbed by going back.
	if (!inRegion(scene)) {
		if (++maze().outsidePolls < 30 || !maze().haveAnchor) {
			return false;
		}

		maze().outsidePolls = 0;
		if (maze().pending) {
			maze().edges[edgeKey(maze().pendingNode, maze().pendingRec)] = kEdgeLeavesMaze;
			maze().pending = false;
		}

		debugC(1, kDebugScene, "Maze autoplay: scene %d is outside the maze, going back to scene %d",
				scene, (int)maze().anchor.sceneID);
		NancySceneState.changeScene(maze().anchor);
		return false;
	}

	maze().outsidePolls = 0;

	const int goalFlag = ConfMan.hasKey("nancy_maze_goal_flag") ? ConfMan.getInt("nancy_maze_goal_flag") : -1;
	if (goalFlag > 0 && NancySceneState.getEventFlag((int16)goalFlag, g_nancy->_true)) {
		maze().finished = true;
		debugC(1, kDebugScene, "Maze autoplay: event flag %d is raised after %u clicks, standing down",
				goalFlag, maze().clicks);
		return false;
	}

	learnRegisters();

	Common::Array<Move> here;
	readMoves(here);

	// A scene has settled when its move list has stopped changing. Poll counts,
	// not milliseconds: the poll rate is not fixed on this build.
	uint32 hash = 0;
	for (uint i = 0; i < here.size(); ++i) {
		hash = hash * 131 + here[i].rec + 1;
	}

	if (hash != maze().movesHash) {
		maze().movesHash = hash;
		maze().settle = 0;
	} else {
		++maze().settle;
	}

	const uint settleFor = ConfMan.hasKey("nancy_maze_settle") ? (uint)ConfMan.getInt("nancy_maze_settle") : 12;
	if (maze().settle < settleFor) {
		return false;
	}

	// A move in flight resolves at the next scene that offers a choice. The
	// cutscenes in between - the valve turn, the ladder fade - have no moves of
	// their own and are simply walked through.
	if (maze().pending) {
		const uint stuckFor = ConfMan.hasKey("nancy_maze_stuck") ? (uint)ConfMan.getInt("nancy_maze_stuck") : 900;

		if (here.empty() || maze().sceneChanges == maze().pendingAt) {
			if (++maze().pendingPolls > stuckFor) {
				debugC(1, kDebugScene, "Maze autoplay: record %u in scene %d did nothing, dropping it",
						maze().pendingRec, (int)(maze().pendingNode & 0xFFFF));
				maze().edges[edgeKey(maze().pendingNode, maze().pendingRec)] = kEdgeDead;
				maze().pending = false;
			}

			return false;
		}
	}

	if (here.empty()) {
		return false;
	}

	const uint64 node = nodeKey(scene);

	if (maze().pending) {
		maze().edges[edgeKey(maze().pendingNode, maze().pendingRec)] = node;
		maze().pending = false;
		maze().pendingPolls = 0;
	}

	maze().moves[node] = here;

	maze().anchor = NancySceneState.getSceneInfo();
	maze().haveAnchor = true;

	const uint32 maxClicks = ConfMan.hasKey("nancy_maze_max_clicks") ?
		(uint32)ConfMan.getInt("nancy_maze_max_clicks") : 6000;
	if (maze().clicks >= maxClicks) {
		maze().finished = true;
		warning("Maze autoplay: %u clicks without finishing, standing down", maze().clicks);
		return false;
	}

	uint distance = 0;
	int choice = planFirstClick(node, true, distance);
	const char *why = "a scene I have not seen";
	if (choice < 0) {
		choice = planFirstClick(node, false, distance);
		why = "a move I have not taken";
	}

	if (choice < 0) {
		maze().finished = true;
		debugC(1, kDebugScene, "Maze autoplay: the reachable maze is walked out - %u nodes, %u scenes, "
				"%u clicks; nothing left to try",
				maze().moves.size(), maze().seenScenes.size(), maze().clicks);
		return false;
	}

	Common::Array<Action::ActionRecord *> &records = NancySceneState.getActionManager().getActionRecords();
	if ((uint)choice >= records.size()) {
		return false;
	}

	Action::ActionRecord *rec = records[choice];
	if (!rec->_isActive || rec->_isDone || !rec->_hasHotspot || !rec->_hotspot.isValidRect()) {
		return false;
	}

	const Common::Rect screen = NancySceneState.getViewport().convertViewportToScreen(rec->_hotspot);
	if (screen.isEmpty()) {
		return false;
	}

	clickAt = Common::Point(screen.left + screen.width() / 2, screen.top + screen.height() / 2);

	int dest = -1;
	for (uint i = 0; i < here.size(); ++i) {
		if (here[i].rec == (uint16)choice) {
			dest = here[i].destScene;
		}
	}

	++maze().clicks;
	maze().pending = true;
	maze().pendingNode = node;
	maze().pendingRec = (uint16)choice;
	maze().pendingAt = maze().sceneChanges;
	maze().pendingPolls = 0;

	debugC(1, kDebugScene, "Maze autoplay: scene %d water %s, %u moves, taking record %d "
			"(scene %d) at %d,%d - %u steps to %s [%u clicks, %u scenes]",
			scene, readingsString().c_str(), here.size(), choice,
			(dest == kNoScene) ? -1 : dest, clickAt.x, clickAt.y, distance, why,
			maze().clicks, maze().seenScenes.size());
	return true;
}

} // End of namespace Nancy
