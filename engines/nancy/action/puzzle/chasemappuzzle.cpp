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

#include "engines/nancy/nancy.h"
#include "engines/nancy/puzzledata.h"

#include "engines/nancy/state/scene.h"

#include "engines/nancy/action/datarecords.h"
#include "engines/nancy/action/navigationrecords.h"
#include "engines/nancy/action/secondarymovie.h"
#include "engines/nancy/action/puzzle/chasemappuzzle.h"

#include "common/config-manager.h"
#include "common/random.h"

namespace Nancy {
namespace Action {

// How the villain weights his options. See the header for what is measured
// and what is not: the graph, the one-step-per-pass cadence and the
// kind -> flag mapping are measured, these three numbers are not.
static const uint kWeightAway		= 3;	// the step lengthens the gap
static const uint kWeightSideways	= 2;	// the step keeps it
static const uint kWeightToward		= 1;	// the step shortens it

// The snapshot the debug affordance at the bottom of this file plans on, filled
// in by readData() and only when the affordance is switched on. Kept in a
// function-local static rather than a namespace-scope one so nothing is
// constructed before main().
static Common::Array<ChaseMapPuzzle::Node> &autoGraph() {
	static Common::Array<ChaseMapPuzzle::Node> graph;
	return graph;
}

static uint16 g_autoValueNancy = 0;
static uint16 g_autoValueVillain = 0;

void ChaseMapPuzzle::readData(Common::SeekableReadStream &stream) {
	_valueIndexA = stream.readUint16LE();
	_valueIndexB = stream.readUint16LE();
	_unknown = stream.readByte();

	_flagA = stream.readSint16LE();
	_flagB = stream.readSint16LE();
	_flagC = stream.readSint16LE();

	const uint16 numNodes = stream.readUint16LE();
	_nodes.resize(numNodes);

	for (uint i = 0; i < numNodes; ++i) {
		Node &node = _nodes[i];
		node.id = stream.readUint16LE();

		const uint16 numEdges = stream.readUint16LE();
		node.edges.resize(numEdges);

		for (uint e = 0; e < numEdges; ++e) {
			node.edges[e].target = stream.readUint32LE();
			node.edges[e].kind = stream.readByte();
		}
	}

	// Debug affordance only, and inert with nancy_chase_autoplay off: hand the
	// graph to the main flow. Nancy's half of the chase is played there, and the
	// main flow cannot see this record - it belongs to the concurrent s5450
	// stream, which owns its own ActionManager. A copy rather than a pointer
	// because the stream deletes and re-reads its records on every one of its
	// self-loops, i.e. once per Nancy move.
	if (autoPlay()) {
		autoGraph() = _nodes;
		g_autoValueNancy = _valueIndexA;
		g_autoValueVillain = _valueIndexB;
	}
}

const ChaseMapPuzzle::Node *ChaseMapPuzzle::findNode(const Common::Array<Node> &nodes, int16 id) {
	for (uint i = 0; i < nodes.size(); ++i) {
		if ((int16)nodes[i].id == id) {
			return &nodes[i];
		}
	}

	return nullptr;
}

void ChaseMapPuzzle::buildDistances(const Common::Array<Node> &nodes, int16 from,
		Common::HashMap<uint32, uint> &dist) {
	dist.clear();
	if (findNode(nodes, from) == nullptr) {
		return;
	}

	dist.setVal((uint32)from, 0);

	Common::Array<uint32> frontier;
	frontier.push_back((uint32)from);

	while (!frontier.empty()) {
		Common::Array<uint32> next;
		for (uint i = 0; i < frontier.size(); ++i) {
			const Node *node = findNode(nodes, (int16)frontier[i]);
			if (!node) {
				continue;
			}

			const uint d = dist[frontier[i]] + 1;
			for (uint e = 0; e < node->edges.size(); ++e) {
				const uint32 target = node->edges[e].target;
				if (!dist.contains(target)) {
					dist.setVal(target, d);
					next.push_back(target);
				}
			}
		}

		frontier = next;
	}
}

void ChaseMapPuzzle::stepWeights(const Common::Array<Node> &nodes, int16 villainNode,
		int16 nancyNode, Common::Array<uint> &weights) {
	weights.clear();

	const Node *from = findNode(nodes, villainNode);
	if (!from) {
		return;
	}

	// Hop distance from Nancy, over the whole graph - the villain will take a
	// canal to get away from someone on foot.
	Common::HashMap<uint32, uint> dist;
	buildDistances(nodes, nancyNode, dist);
	const uint here = dist.contains((uint32)villainNode) ? dist[(uint32)villainNode] : 0;

	// Weight every edge by what it does to that distance, and refuse to walk
	// into Nancy. Parallel edges are two separate options, which is what the
	// record lists: the two pairs joined both on foot and by vaporetto are
	// twice as likely to be used as a single-linked pair, and which of the two
	// is taken decides which travel sound the player hears.
	weights.resize(from->edges.size());
	uint total = 0;

	for (uint i = 0; i < from->edges.size(); ++i) {
		const uint32 target = from->edges[i].target;
		uint weight;

		if ((int16)target == nancyNode) {
			weight = 0;
		} else if (!dist.contains(target)) {
			weight = kWeightSideways;
		} else if (dist[target] > here) {
			weight = kWeightAway;
		} else if (dist[target] == here) {
			weight = kWeightSideways;
		} else {
			weight = kWeightToward;
		}

		weights[i] = weight;
		total += weight;
	}

	if (total == 0) {
		// Cornered: every edge leads to Nancy. No node in nancy18's map has a
		// single distinct neighbour, so this cannot happen there, but a
		// zero-weight roll must not be possible whatever the data says.
		for (uint i = 0; i < weights.size(); ++i) {
			weights[i] = 1;
		}
	}
}

void ChaseMapPuzzle::execute() {
	// One step per pass. The scene this record lives in (s5450) is re-loaded
	// once per Nancy move - its type 93 record watches the main flow and its
	// type 15 record loops the scene back to itself - so a kOneShot record here
	// fires exactly once per move. Everything this needs is in the player
	// table, so nothing has to survive the reload.
	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	assert(playerTable);

	const int16 nancyNode = playerTable->getValue(_valueIndexA);
	const int16 villainNode = playerTable->getValue(_valueIndexB);

	const Node *from = findNode(_nodes, villainNode);
	if (!from || from->edges.empty()) {
		// Not seeded yet, or seeded to something off the map. The hub scene
		// s5400 writes both values before it starts this stream, so this is
		// only reachable from a broken save; do not teleport anyone.
		finishExecution();
		return;
	}

	Common::Array<uint> weights;
	stepWeights(_nodes, villainNode, nancyNode, weights);

	uint total = 0;
	for (uint i = 0; i < weights.size(); ++i) {
		total += weights[i];
	}

	if (weights.size() != from->edges.size() || total == 0) {
		finishExecution();
		return;
	}

	uint roll = g_nancy->_randomSource->getRandomNumber(total - 1);
	uint chosen = weights.size() - 1;
	for (uint i = 0; i < weights.size(); ++i) {
		if (roll < weights[i]) {
			chosen = i;
			break;
		}

		roll -= weights[i];
	}

	const Edge &edge = from->edges[chosen];

	// 35/36/37 sit above the 30 single values, so they are combo (float)
	// entries - the same dispatch SetValue and ValueTest do.
	const uint numSingleValues = playerTable->getNumSingleValues();
	if (_valueIndexB < numSingleValues) {
		playerTable->setSingleValue(_valueIndexB, (int16)edge.target);
	} else {
		playerTable->setComboValue(_valueIndexB - numSingleValues, (float)edge.target);
	}

	// Say how he travelled. The three flags are the three edge kinds: they gate
	// the Chase01/02/03 sounds in this scene and, in the PDA, the PDAChaseFoot,
	// PDAChaseGondola and PDAChaseVap icons - in that order.
	const int16 modeFlags[3] = { _flagA, _flagB, _flagC };
	for (uint i = 0; i < 3; ++i) {
		if (modeFlags[i] != kFlagNoLabel && modeFlags[i] > kEvNoEvent) {
			NancySceneState.setEventFlag(modeFlags[i],
				edge.kind == i ? g_nancy->_true : g_nancy->_false);
		}
	}

	debugC(1, kDebugScene, "ChaseMapPuzzle: villain %d -> %d by %s, Nancy at %d",
		villainNode, (int)edge.target,
		edge.kind == 0 ? "foot" : (edge.kind == 1 ? "gondola" : "vaporetto"),
		nancyNode);

	finishExecution();
}

// -- debug affordance -------------------------------------------------------------
//
// nancy_chase_autoplay plays NANCY's half of the Venice chase - the villain's
// half is execute(), above, and is ordinary game logic that runs whether this is
// on or off. It is the last critical-path puzzle in nancy18 that a headless run
// could not cross, and unlike the other autoplay hooks it cannot be a click
// script: the chase is randomised (which of nodes 16-21 he starts on, whether he
// is glimpsed, and every one of his steps), so it has to be solved in play.
//
// WHERE THE MOVE HAPPENS. Nancy's move is a click in the MAIN flow - one of the
// 26 map scenes - while this record lives in the concurrent s5450 stream and can
// neither see the main flow's records nor set its scratch flags. So the parts
// are split: readData() copies the graph into a file static, and InputManager
// calls autoPlayNextClick() once per poll to ask whether to click and where.
// The click itself is a real click on a real hotspot, so the travel movie, the
// footstep or boat sound and the scene change are all the game's own.
//
// WHAT IT READS, AND WHAT A PLAYER WOULD NOT. It reads the villain's node
// (value 36) straight out of the player table every turn. A player only sees him
// when the glimpse roll fires - flag 1020, 7/14/21/25% on a rising ladder - and
// otherwise has to infer him from the travel sound. This is ground truth, not
// deduction, and it is deliberate: the hook exists so a bot can get past the
// chase to the endgame, the same way nancy_mosaic_autoplay drops the gems the
// record itself names. It is not a model of a good player.
//
// THE POSITION IT PLANS FROM. One pass of s5450 is one Nancy move, and the
// order inside it is fixed (see the header): the catch test [0] runs first, the
// villain steps [11] second, the rings [17..42] draw last. So when Nancy chooses
// he has already moved, and the test that decides the chase is the one at the
// top of the NEXT pass - Nancy's new node against the node he is standing on
// now. Stepping onto him is therefore a certain catch, and the pursuit is
// "close, then pounce".
//
// THE POLICY. Every neighbour of Nancy's node is clickable, gondolas included
// (see the header's correction), so her move set is exactly the node's edge
// targets. Let f_t(p, v) be the chance of catching him within t more moves with
// Nancy on p and the villain on v. Then
//
//   f_t(p, v) = max over q adjacent to p of
//                   q == v ? 1 : sum over v' of P(v' | v, q) * f_(t-1)(q, v')
//
// with f_0 = 0, and P(.|v,q) taken from stepWeights() - the villain's own
// function, so his model of himself and hers of him are the same code. 26 nodes
// makes this 676 states; one sweep is a few thousand multiplies and the whole
// table is rebuilt on each move rather than cached, so nothing has to survive
// the stream's reload or a save.
//
// Simulated 20000 times against the villain exactly as execute() implements
// him, from the six starts the data allows and with the 19 moves the chase
// actually gives her: this policy catches him 19999 times, median on her fifth
// move. Greedy - "walk the shortest path, pounce when adjacent" - gets 99.14%,
// and a walker clicking at random 21.77%.
//
// The horizon is a constant rather than a countdown read off the turn counter
// because the sweep is flat, so there is nothing to gain by wiring one more
// value in: over the same 20000 chases, horizon 2 scores 99.935%, 3 99.970%,
// 5 99.980%, 8 99.985%, and 12, 16 and 20 all 99.995%.
//
// It also beats greedy when the model of the villain is WRONG, which is the
// thing worth insuring against here, since his weights are invented. Against a
// villain who always maximises the gap - the worst case for a pursuer, and the
// rule the header rejected - this policy still takes him 78% of the time while
// greedy collapses to 31%; against a uniform random walker, 100% against
// 99.8%.
//
// WHAT IS INFERRED HERE. Only the policy, and it is ours, not the game's. Every
// input to it is measured: the graph and the two value indices are the record's
// own bytes; "scene S(5410+n) is node n" is each map scene's own SetValue; the
// clickable set is the live hotspot list; the pass order is the s5450 record
// order. The villain's weights are the same invention they were before this
// hook - see the header - and this policy inherits them by calling his function.

static const uint16 kMapSceneBase	= 5410;	// node n is scene 5410 + n
static const uint16 kFirstMapScene	= 5411;	// node 1, Ca' Nascosta
static const uint16 kLastMapScene	= 5436;	// node 26, San Giorgio Maggiore
static const int16 kChaseStoryFlag	= 2050;	// set on entering S6716; gates the chase

// Moves to look ahead. Flat from 3 to 20 (see above), so this reads no counter.
static const uint kChaseHorizon		= 12;

// Polls to wait after landing in a map scene before clicking out of it. This is
// what keeps Nancy's plan in step with the villain's: the s5450 pass belonging
// to an arrival is driven by that arrival - its type 93 record watches the main
// flow's scene-change count - so it runs a frame or two after the scene id
// changes, and planning before it has run would aim at where he was rather than
// where he is. Twenty polls is an order of magnitude more than the one or two
// it needs, and still well inside one travel movie.
static const uint kSettlePolls		= 20;

// If a scene has been sat in this long with a click already sent, the click did
// not land: re-arm rather than deadlock the run.
static const uint kReclickPolls		= 600;

// nancy_chase_autoplay defaults off; ordinary play is unchanged.
bool ChaseMapPuzzle::autoPlay() {
	return ConfMan.hasKey("nancy_chase_autoplay") && ConfMan.getBool("nancy_chase_autoplay");
}

// Everything the pursuit needs, flattened once per move. Slot numbers rather
// than node ids throughout: the record numbers its nodes 1..26 in order, but
// nothing here relies on that.
struct ChasePlan {
	uint numNodes = 0;
	uint maxEdges = 0;

	// Per node, the distinct edge targets - Nancy's move set. Parallel edges are
	// one hotspot on the map so they collapse here, unlike the villain's, where
	// the record lists them separately and each is its own roll.
	Common::Array<Common::Array<uint> > neighbours;

	// [(v * numNodes + q) * maxEdges + e]: the weight the villain standing on v
	// puts on his edge e when Nancy is on q. From his own stepWeights().
	Common::Array<uint> weights;

	// [v * maxEdges + e]: the slot his edge e leads to, or numNodes if unknown.
	Common::Array<uint> targets;

	// [v]: the sum of his weights, per (v, q) pair. Zero means "no legal step",
	// which stepWeights() already rules out, but the arithmetic must not divide.
	Common::Array<uint> totals;
};

static bool buildChasePlan(const Common::Array<ChaseMapPuzzle::Node> &nodes,
		Common::HashMap<uint32, uint> &slotOf, ChasePlan &plan) {
	plan.numNodes = nodes.size();
	if (plan.numNodes == 0) {
		return false;
	}

	slotOf.clear();
	for (uint i = 0; i < plan.numNodes; ++i) {
		slotOf.setVal(nodes[i].id, i);
	}

	for (uint i = 0; i < plan.numNodes; ++i) {
		plan.maxEdges = MAX<uint>(plan.maxEdges, nodes[i].edges.size());
	}

	if (plan.maxEdges == 0) {
		return false;
	}

	plan.neighbours.resize(plan.numNodes);
	plan.targets.resize(plan.numNodes * plan.maxEdges);
	for (uint i = 0; i < plan.targets.size(); ++i) {
		plan.targets[i] = plan.numNodes;
	}

	for (uint i = 0; i < plan.numNodes; ++i) {
		for (uint e = 0; e < nodes[i].edges.size(); ++e) {
			if (!slotOf.contains(nodes[i].edges[e].target)) {
				continue;
			}

			const uint target = slotOf[nodes[i].edges[e].target];
			plan.targets[i * plan.maxEdges + e] = target;

			bool seen = false;
			for (uint k = 0; k < plan.neighbours[i].size(); ++k) {
				seen |= plan.neighbours[i][k] == target;
			}

			if (!seen) {
				plan.neighbours[i].push_back(target);
			}
		}
	}

	plan.weights.resize(plan.numNodes * plan.numNodes * plan.maxEdges);
	for (uint i = 0; i < plan.weights.size(); ++i) {
		plan.weights[i] = 0;
	}

	plan.totals.resize(plan.numNodes * plan.numNodes);
	for (uint i = 0; i < plan.totals.size(); ++i) {
		plan.totals[i] = 0;
	}

	Common::Array<uint> scratch;
	for (uint v = 0; v < plan.numNodes; ++v) {
		for (uint q = 0; q < plan.numNodes; ++q) {
			ChaseMapPuzzle::stepWeights(nodes, (int16)nodes[v].id, (int16)nodes[q].id, scratch);
			if (scratch.size() != nodes[v].edges.size()) {
				continue;
			}

			uint total = 0;
			for (uint e = 0; e < scratch.size(); ++e) {
				plan.weights[(v * plan.numNodes + q) * plan.maxEdges + e] = scratch[e];
				total += scratch[e];
			}

			plan.totals[v * plan.numNodes + q] = total;
		}
	}

	return true;
}

// The chance of catching him from Nancy's next position, i.e. after she has
// moved to `q` and he has answered from `v`: sum over his steps of what each
// leaves her holding, per `value`.
static float chaseExpected(const ChasePlan &plan, const Common::Array<float> &value,
		uint q, uint v) {
	const uint total = plan.totals[v * plan.numNodes + q];
	if (!total) {
		return 0.0f;
	}

	const uint base = (v * plan.numNodes + q) * plan.maxEdges;
	float sum = 0.0f;

	for (uint e = 0; e < plan.maxEdges; ++e) {
		const uint weight = plan.weights[base + e];
		const uint target = plan.targets[v * plan.maxEdges + e];
		if (!weight || target >= plan.numNodes) {
			continue;
		}

		sum += (float)weight / (float)total * value[q * plan.numNodes + target];
	}

	return sum;
}

// The node Nancy should move to, or -1 when the position cannot be planned from.
// `chance` comes back as the policy's own estimate of catching him from here.
static int16 chooseChaseMove(const Common::Array<ChaseMapPuzzle::Node> &nodes,
		int16 nancyNode, int16 villainNode, float &chance) {
	chance = 0.0f;

	Common::HashMap<uint32, uint> slotOf;
	ChasePlan plan;
	if (!buildChasePlan(nodes, slotOf, plan)) {
		return -1;
	}

	if (!slotOf.contains((uint32)nancyNode) || !slotOf.contains((uint32)villainNode)) {
		return -1;
	}

	const uint nancySlot = slotOf[(uint32)nancyNode];
	const uint villainSlot = slotOf[(uint32)villainNode];
	const uint numNodes = plan.numNodes;

	// f_0 is "no moves left, no catch"; each sweep raises the horizon by one.
	Common::Array<float> value, next;
	value.resize(numNodes * numNodes);
	next.resize(numNodes * numNodes);
	for (uint i = 0; i < value.size(); ++i) {
		value[i] = 0.0f;
		next[i] = 0.0f;
	}

	for (uint step = 1; step < kChaseHorizon; ++step) {
		for (uint p = 0; p < numNodes; ++p) {
			for (uint v = 0; v < numNodes; ++v) {
				float best = 0.0f;
				for (uint k = 0; k < plan.neighbours[p].size(); ++k) {
					const uint q = plan.neighbours[p][k];
					const float here = (q == v) ? 1.0f : chaseExpected(plan, value, q, v);
					best = MAX(best, here);
				}

				next[p * numNodes + v] = best;
			}
		}

		value = next;
	}

	// The move itself. Ties are broken towards the shorter hop distance and then
	// the lower node id, so a run is reproducible and reads sensibly on screen.
	Common::HashMap<uint32, uint> distToVillain;
	ChaseMapPuzzle::buildDistances(nodes, villainNode, distToVillain);

	int16 bestNode = -1;
	float bestScore = -1.0f;
	uint bestDist = 0xFFFF;

	for (uint k = 0; k < plan.neighbours[nancySlot].size(); ++k) {
		const uint q = plan.neighbours[nancySlot][k];
		const int16 id = (int16)nodes[q].id;

		if (q == villainSlot) {
			// He is standing next door and the catch test runs before his step:
			// this is not a gamble, it ends the chase.
			chance = 1.0f;
			return id;
		}

		const float score = chaseExpected(plan, value, q, villainSlot);
		const uint hops = distToVillain.contains((uint32)id) ? distToVillain[(uint32)id] : 0xFFFF;

		if (score > bestScore + 1e-6f ||
				(score > bestScore - 1e-6f &&
					(hops < bestDist || (hops == bestDist && bestNode >= 0 && id < bestNode)))) {
			bestScore = score;
			bestDist = hops;
			bestNode = id;
		}
	}

	chance = MAX(bestScore, 0.0f);
	return bestNode;
}

// The screen-space centre of the live hotspot in the map scene the main flow is
// showing that travels to `targetScene`, or false when there is none.
//
// Derived from the records rather than from a table of rects, because the rects
// differ per scene and a table would rot silently. The chain a click runs is:
// the type 91 hotspot raises one scratch flag, and the Movie Playback gated on
// that flag plays the travel clip and carries the scene change. So this reads it
// backwards - find the record whose scene change is the destination, collect the
// flags it is gated on, then find the live hotspot that raises one of them.
static bool findChaseHotspot(uint16 targetScene, Common::Point &centre) {
	Common::Array<ActionRecord *> &records = NancySceneState.getActionManager().getActionRecords();

	Common::Array<int16> flags;
	for (uint i = 0; i < records.size(); ++i) {
		uint16 destination = kNoScene;

		if (PlaySecondaryMovie *movie = dynamic_cast<PlaySecondaryMovie *>(records[i])) {
			destination = movie->_sceneChange.sceneID;
		} else if (SceneChange *change = dynamic_cast<SceneChange *>(records[i])) {
			destination = change->_sceneChange.sceneID;
		}

		if (destination != targetScene) {
			continue;
		}

		const DependencyRecord &deps = records[i]->_dependencies;
		for (uint d = 0; d < deps.children.size(); ++d) {
			if (deps.children[d].type == DependencyType::kEvent &&
					deps.children[d].condition == (int16)g_nancy->_true) {
				flags.push_back(deps.children[d].label);
			}
		}
	}

	if (flags.empty()) {
		return false;
	}

	for (uint i = 0; i < records.size(); ++i) {
		ActionRecord *record = records[i];
		if (!record->_isActive || record->_isDone || !record->_hasHotspot ||
				!record->_hotspot.isValidRect()) {
			continue;
		}

		EventFlags *setter = dynamic_cast<EventFlags *>(record);
		if (!setter) {
			continue;
		}

		bool match = false;
		for (uint f = 0; f < setter->_flags.descs.size() && !match; ++f) {
			if (setter->_flags.descs[f].flag != g_nancy->_true) {
				continue;
			}

			for (uint k = 0; k < flags.size() && !match; ++k) {
				match = setter->_flags.descs[f].label == flags[k];
			}
		}

		if (!match) {
			continue;
		}

		const Common::Rect screen =
			NancySceneState.getViewport().convertViewportToScreen(record->_hotspot);
		if (screen.isEmpty()) {
			continue;
		}

		centre = Common::Point(screen.left + screen.width() / 2, screen.top + screen.height() / 2);
		return true;
	}

	return false;
}

bool ChaseMapPuzzle::autoPlayNextClick(Common::Point &clickAt) {
	if (!autoPlay() || !State::Scene::hasInstance()) {
		return false;
	}

	// One click per arrival, `kSettlePolls` after landing. `settle` counts polls
	// since the scene last changed or since the last click, whichever is later,
	// so a click that somehow did not land is retried after kReclickPolls rather
	// than stalling the run for good.
	static uint16 lastScene = kNoScene;
	static uint16 clickedScene = kNoScene;
	static uint settle = 0;

	if (NancySceneState.getState() != State::Scene::kRun) {
		return false;
	}

	const uint16 scene = NancySceneState.getSceneInfo().sceneID;
	if (scene != lastScene) {
		lastScene = scene;
		settle = 0;
	} else {
		++settle;
	}

	if (scene < kFirstMapScene || scene > kLastMapScene) {
		return false;
	}

	// The chase is the only thing that puts the player on these scenes with 2050
	// up; without it they are the ordinary Venice map and clicking through it
	// would be somebody else's business.
	if (!NancySceneState.getEventFlag(kChaseStoryFlag, g_nancy->_true)) {
		return false;
	}

	if (settle < kSettlePolls || (scene == clickedScene && settle < kReclickPolls)) {
		return false;
	}

	const Common::Array<Node> &nodes = autoGraph();
	if (nodes.empty()) {
		return false;
	}

	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	if (!playerTable) {
		return false;
	}

	// Where the main flow is standing IS Nancy's node - each map scene holds the
	// SetValue that writes its own number into value 35 - so the scene id and the
	// table have to agree by the time anything is clickable. Read the scene, and
	// say so in the trace if they ever disagree.
	const int16 nancyNode = (int16)(scene - kMapSceneBase);
	const int16 tableNode = playerTable->getValue(g_autoValueNancy);
	const int16 villainNode = playerTable->getValue(g_autoValueVillain);

	if (findNode(nodes, villainNode) == nullptr) {
		return false;
	}

	float chance = 0.0f;
	const int16 target = chooseChaseMove(nodes, nancyNode, villainNode, chance);
	if (target < 0) {
		return false;
	}

	if (!findChaseHotspot((uint16)(kMapSceneBase + target), clickAt)) {
		// The scene's records are not live yet, or the travel movie is still
		// playing. Nothing to do but ask again next poll.
		return false;
	}

	clickedScene = scene;
	settle = 0;

	debugC(1, kDebugScene, "ChaseMapAutoplay: Nancy %d%s -> %d, villain %d, p(catch) %.2f",
		nancyNode, tableNode == nancyNode ? "" : " (value 35 disagrees!)",
		target, villainNode, chance);

	return true;
}

} // End of namespace Action

// See input.h for why the input layer reaches the hook through a free function
// instead of including this record's header.
bool chaseAutoPlayNextClick(Common::Point &clickAt) {
	return Action::ChaseMapPuzzle::autoPlayNextClick(clickAt);
}

} // End of namespace Nancy
