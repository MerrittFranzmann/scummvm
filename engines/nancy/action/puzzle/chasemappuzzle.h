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

#ifndef NANCY_ACTION_CHASEMAPPUZZLE_H
#define NANCY_ACTION_CHASEMAPPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"

#include "common/hashmap.h"
#include "common/rect.h"

namespace Nancy {
namespace Action {

// AR type 213 in Nancy16, which reuses the type: in older games 213 is
// CollisionPuzzle/kTileMove.
//
// The Venice chase map in scene S5450. Each pass moves the villain one edge
// along a 26-node graph and raises a flag saying how he travelled.
//
// Record layout:
//   uint16  value index of Nancy's node (35)
//   uint16  value index of the villain's node (36)
//   byte    unknown, always 0
//   int16   3 event flags, one per edge kind: foot, gondola, vaporetto
//   uint16  node count
//     per node: uint16 id, uint16 edge count,
//               per edge: uint32 target node id, byte kind (0, 1 or 2)
//
// How the villain chooses among his edges is not stored in the record and is
// a guess; see stepWeights().
class ChaseMapPuzzle : public ActionRecord {
public:
	struct Edge {
		uint32 target = 0;
		byte kind = 0;
	};

	struct Node {
		uint16 id = 0;
		Common::Array<Edge> edges;
	};

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	// The villain's choice, as one weight per edge of villainNode, parallel to
	// that node's edge list.
	static void stepWeights(const Common::Array<Node> &nodes, int16 villainNode,
		int16 nancyNode, Common::Array<uint> &weights);

	// Hop count from `from` to every reachable node, over edges of every kind
	static void buildDistances(const Common::Array<Node> &nodes, int16 from,
		Common::HashMap<uint32, uint> &dist);

protected:
	Common::String getRecordTypeName() const override { return "ChaseMapPuzzle"; }

	static const Node *findNode(const Common::Array<Node> &nodes, int16 id);

	uint16 _valueIndexA = 0;	// 35, Nancy's node
	uint16 _valueIndexB = 0;	// 36, the villain's node
	byte _unknown = 0;			// Unknown, always 0

	int16 _flagA = -1;			// 2018, on foot
	int16 _flagB = -1;			// 2019, by gondola
	int16 _flagC = -1;			// 2020, by vaporetto

	Common::Array<Node> _nodes;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_CHASEMAPPUZZLE_H
