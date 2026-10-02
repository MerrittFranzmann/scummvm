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

// How the villain weights his options. These values are a guess.
static const uint kWeightAway		= 3;	// the step lengthens the gap
static const uint kWeightSideways	= 2;	// the step keeps it
static const uint kWeightToward		= 1;	// the step shortens it

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

	// Hop distance from Nancy, over edges of every kind
	Common::HashMap<uint32, uint> dist;
	buildDistances(nodes, nancyNode, dist);
	const uint here = dist.contains((uint32)villainNode) ? dist[(uint32)villainNode] : 0;

	// Weight every edge by what it does to that distance, and never step onto
	// Nancy. Parallel edges of different kinds count as separate options.
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
		// Cornered: every edge leads to Nancy. Never roll on zero weights.
		for (uint i = 0; i < weights.size(); ++i) {
			weights[i] = 1;
		}
	}
}

void ChaseMapPuzzle::execute() {
	// The scene reloads itself once per Nancy move, so this steps once per move.
	// All state lives in the player table.
	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	assert(playerTable);

	const int16 nancyNode = playerTable->getValue(_valueIndexA);
	const int16 villainNode = playerTable->getValue(_valueIndexB);

	const Node *from = findNode(_nodes, villainNode);
	if (!from || from->edges.empty()) {
		// Not seeded yet, or seeded to a node off the map
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

	// Indices past the single values are combo (float) entries, as in SetValue
	const uint numSingleValues = playerTable->getNumSingleValues();
	if (_valueIndexB < numSingleValues) {
		playerTable->setSingleValue(_valueIndexB, (int16)edge.target);
	} else {
		playerTable->setComboValue(_valueIndexB - numSingleValues, (float)edge.target);
	}

	// Raise the flag for how he travelled. They gate the Chase01/02/03 sounds
	// and the PDA's foot, gondola and vaporetto icons.
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

} // End of namespace Action

} // End of namespace Nancy
