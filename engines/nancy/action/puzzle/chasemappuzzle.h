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

// AR type 213 in Nancy16, where the type is reused: in the older games 213 is
// CollisionPuzzle/kTileMove, which is why the factory keeps that branch.
//
// There is exactly one Nancy16 record of this type, in scene S5450 ("MAP",
// background "NO_BG"). The record calls itself "Graph Puzzle" and it is the
// road/canal network behind the Venice chase sequence: 26 numbered map nodes
// and, for each, the nodes it connects to.
//
// This record IS the villain. Once per pass it moves him one edge along the
// graph and says how he travelled. Everything about the plumbing - which values
// it reads and writes, how often it runs, what the three flags mean - is
// measured; the one thing that is not is how he picks among his options, and
// that is called out at the bottom.
//
// Record layout, 1/1 byte-exact with nothing left over:
//
//   uint16  35        value index (see below)
//   uint16  36        value index (see below)
//   byte    0         unidentified
//   uint16  2018, uint16 2019, uint16 2020   event flag labels
//   uint16  26        node count
//     per node: uint16 id (1..26, sequential), uint16 edge count,
//               per edge: uint32 target node id, byte kind (0, 1 or 2)
//
// WHAT THE NODES ARE. Measured, and it is not a guess: scenes S5411 to S5436
// are the twenty-six named places on the Venice map (background
// MAP_VeniceMap-TXT), and each one holds a single SetValue record that writes
// its own number into value 35:
//
//    1 Ca' Nascosta            14 Santo Stefano
//    2 Rialto Market           15 La Fenice
//    3 Campo Dei Frari         16 Accademia
//    4 Campo San Polo          17 Palazzo Genovese
//    5 San Silvestro           18 Santa Maria dei Giglio
//    6 Rialto Bridge           19 Salute
//    7 Campo Santa Maria Formosa  20 Punta della Dogana
//    8 Scuola Grande Dei Carmini  21 Piazza San Marco
//    9 Campo Santa Margharita  22 San Zaccaria
//   10 San Toma                23 Pieta
//   11 Sant' Angelo            24 Zattere
//   12 Ca' Rezzonica           25 Spirito Santo
//   13 San Samuele             26 San Giorgio Maggiore
//
// The numbering is cross-checked by the movies: 24 of S5450's 26 chase movies
// are the same generic MAP_ChaseRings_ANIM, and the only two that are not are
// node 2's MAP_ChaseRialtoMkt_ANIM and node 25's MAP_ChaseSpirito_ANIM -
// Rialto Market and Spirito Santo, exactly the two places the table above puts
// at 2 and 25.
//
// WHAT THE TWO VALUES ARE. Also measured, by exhaustion over all 14143 records:
//   * Value 35 is where Nancy is. Its only writers in the whole game are the
//     twenty-six map scenes above plus S5400, the map hub, which seeds it to 4
//     (Campo San Polo).
//   * Value 36 is where the villain is. Its only writers are six SetValue
//     records in S5400 that seed it to 16, 17, 18, 19, 20 or 21 - Accademia
//     through Piazza San Marco, the south-eastern end of the map - and nothing
//     else in the game writes it at all. S5450 holds 26 Movie Playback records
//     gated on "value 36 == N", so value 36 chooses which chase animation
//     plays. Since 35 is spoken for and 36 has no other writer, this record is
//     what must be stepping 36 along the graph.
//   * Value 37 is the step counter: S5450's SetValue record is index 37 with
//     shouldSet = 0 and value 1, i.e. an increment, and the scene loops back to
//     itself through a "Detecting main stream scene changes" record that raises
//     flag 1010.
//
// HOW THE CHASE ENDS. S5450's two ValueTest records and its two stream scene
// changes spell it out:
//   * ValueTest(35, kTestSome, ==, [36]) -> flag 1030, and flag 1030 plays
//     VEN010 and moves MainStream to scene 5490. Catching the villain is
//     standing on the same node they are.
//   * A kSceneCount dependency on scene 5450 with count > 20 moves MainStream
//     to 5480 (background VEN_SecChaChase, the second-chance branch). Twenty
//     loops and they get away.
//   * ValueTest(37, kTestActualValue, <, 21) -> flag 1031, the same twenty-step
//     budget expressed on the counter.
//   * The counter also drives an escalating chance of being spotted: five
//     "Event Flags" records all raise flag 1020, gated on value-37 bands
//     (== 1, 3-5, 6-10, 11-15, >= 16) paired with kRandom dependencies whose
//     thresholds climb 7, 14, 21, 25. Flag 1020 gates every chase movie and
//     every per-node Chase## sound.
//   * Flags 2018/2019/2020 gate the Chase01/02/03 sounds in S5450 and three
//     records in S6432 and S6437. Nothing in the game sets 2019 or 2020, and
//     only S6719 sets 2018, so these three are this record's outputs too.
//
// WHAT THE THREE FLAGS ARE - the outputs, and they name the three edge kinds.
// This is measured, from the widgets they drive:
//
//     2018 -> NDUIControl Show/Hide "PDAChaseFoot"      (S6432[6,7], S6437[0,1])
//     2019 -> NDUIControl Show/Hide "PDAChaseGondola"   (S6432[10,11], S6437[4,5])
//     2020 -> NDUIControl Show/Hide "PDAChaseVap"       (S6432[8,9],  S6437[2,3])
//
// Foot, gondola, vaporetto - three ways to cross Venice, in the same order as
// the record's three flag fields, and the record's edges carry exactly three
// kinds. Each flag also gates one of the Chase01/02/03 sounds in S5450, played
// with no other condition, so the player hears how the villain just travelled
// on every pass whether or not he is seen; the PDA shows the same thing as an
// icon. That is the deduction the chase runs on: the rings only appear
// occasionally, but the travel sound always does, and it narrows the edge.
//
// And the map itself names each kind a second time, independently of the PDA
// widgets. Every travel hotspot in the 26 map scenes S5411-S5436 fires a Movie
// Playback whose clip is named after the mode:
//
//     kind 0  MAP_G01..G32(+A/B)  Footsteps_ConcreteHeels01   42 edges, on foot
//     kind 1  MAP_B01..B08        Boat_RowShort01              8 edges, rowed
//     kind 2  MAP_R01..R14        Boat_StartShort01           14 edges, motor
//
// Prefix B appears on kind-1 edges and on nothing else, and there are exactly 8
// of those clips for the 8 kind-1 edges; prefix R appears on kind-2 edges and
// there are exactly 14 of them for the 14 kind-2 edges. A rowed boat is a
// gondola and a motor starting is a vaporetto, so "1 gondola, 2 vaporetto"
// holds without appealing to the PDA at all.
//
// NANCY CAN USE ALL THREE, AND ONLY DURING THE CHASE. Each of the 16 directed
// kind-1 hotspots is a *pair* of records on one rect, split on story flag 2050:
// the copy gated on 2050 == 0 leads to one of the nine gondola-stand scenes
// S5460-S5468 and sets story flags, and the copy gated on 2050 == 1 - i.e. only
// while the chase is running - plays the MAP_B## clip and moves her along the
// gondola edge. Counting both, the clickable set in each of the 26 map scenes
// is exactly that node's full neighbour list, all three kinds, 26 of 26 an
// exact set match. So during the chase Nancy is exactly as mobile as the
// villain; the PDA icon tells her which of three modes he took, not which of
// them she is barred from.
//
// (An earlier reading of this file said the opposite - "not one kind-1 edge
// offered anywhere", the gondola his alone. That came from a record scan that
// skipped any record whose first dependency had the OR flag set, and every
// gondola movie is gated on "scratch flag OR story flag", so all 16 of them
// were dropped before the comparison ran. Confirmed in the engine as well as in
// the data: under nancy_debug_hotspots, S5417 (node 7) lists seven live
// hotspots for its seven neighbours, three of them its kind-1 edges, and the
// autoplay below takes gondola edges in ordinary play.)
//
// The geography agrees with the ordering independently. Kind 0 (42 edges) is
// the one subgraph that spans nodes 1-25 and cannot reach the island 26: on
// foot. Kind 2 (14 edges) is Rialto - San Silvestro - Sant'Angelo - San Toma -
// Ca' Rezzonica - San Samuele - Accademia - S.M. del Giglio - San Marco, plus
// Salute, Zattere, San Zaccaria and San Giorgio: that is a vaporetto line's
// stop order down the Grand Canal and across the Bacino, and it is why 5<->6
// and 21<->22 are joined twice - those pairs are both a short walk and two
// adjacent boat stops. Kind 1 (8 edges) is the leftover set of long hops
// through the back canals, including the two edges to Ca' Nascosta, the
// villain's own palazzo, which no vaporetto would call at.
//
// WHAT THE EDGE KINDS LOOK LIKE. Measured:
//   * The graph is exactly undirected, kind included: every (source, target,
//     kind) triple has its mirror, with no exceptions.
//   * 42 undirected edges are kind 0, 14 are kind 2, 8 are kind 1.
//   * The kind-0 subgraph spans nodes 1 to 25 in one connected piece and leaves
//     node 26 - San Giorgio Maggiore, the island across the Bacino - completely
//     isolated. 26 is the only node with no kind-0 edge at all; its four links
//     are kind 1 to Santa Maria Formosa and Punta della Dogana and kind 2 to
//     Piazza San Marco and San Zaccaria.
//   * The two node pairs joined twice with different kinds are 5<->6
//     (San Silvestro / Rialto Bridge) and 21<->22 (Piazza San Marco /
//     San Zaccaria), each joined once by kind 0 and once by kind 2.
//   * The kind-2 edges are dominated by crossings that are boat hops in the
//     real city: San Toma / Sant' Angelo, San Toma / San Samuele, Salute /
//     Piazza San Marco, San Zaccaria / San Giorgio Maggiore.
//   Which kind is which is settled twice over - by the PDA widgets above and by
//   the travel clip names: 0 foot, 1 gondola, 2 vaporetto.
//
// WHERE IT SITS IN THE PASS. S5450 is not a screen - it is a second scene flow
// started by S5400's type 26 record, running underneath the Venice map the
// player is looking at and drawing its MAP_Chase*_ANIM overlays on top. Its
// type 93 record watches the main flow for a scene change and its type 15
// record loops the scene back to itself, so the whole script re-runs once per
// Nancy move, in record order:
//
//   [0]      ValueTest 35 == 36           -> flag 1030 -> s5490, caught
//   [5]      SetValue 37 += 1             -> the turn counter
//   [7..10]  kSceneCount(5450) > 20       -> s5480, he gets away
//   [11]     THIS RECORD                  -> the villain steps
//   [12..16] flag 1020, rising odds       -> whether he is seen this turn
//   [17..42] MAP_Chase*_ANIM at value 36  -> the rings, gated on 1020
//   [43..45] Chase01/02/03                -> the travel sound, gated on the
//                                            flag this record just set
//
// So the catch test runs before the step: Nancy catches him by arriving where
// he still is, and only then does he slip away. The rings drawn at the end of a
// pass show where he stands when she next chooses, which makes a glimpse while
// adjacent a certain catch - that is the whole game. Nancy gets 20 moves; the
// scratch flags (1010-1040, which is 1020 and 1030) are per-stream and are
// cleared by every one of those self-loops, so each pass rolls fresh.
//
// WHAT IS STILL GUESSED - the only thing left, and it is the interesting one.
// How he picks among his options is not in the record and is not recoverable
// from it. Two things do constrain it. The record carries value index 35,
// Nancy's node, which it would not need if he wandered blindly - so his step
// depends on where she is. And the record lists no self-loops, so he always
// moves. Beyond that the numbers below are invented, and were chosen by
// simulating the pass above:
//
//   he never steps onto Nancy's node, and among the rest an edge that
//   lengthens the hop distance to her is weighted 3, one that keeps it 2, one
//   that shortens it 1.
//
// A flat random walk (1/1/1) and a strict always-flee (only ever lengthen) are
// both worse: the first is not a chase, and the second is close to uncatchable,
// because with equal speed a villain who always increases the gap holds it
// forever - simulated, it drops even a perfect pursuer to under 30%, and it
// punishes knowing the rule rather than rewarding it. 3/2/1 over 3000
// simulated chases: 93% caught by a player who tracks him and uses the travel
// sound, 84% without the sound, 69% by one who just walks to where the rings
// last appeared, 19% by one clicking at random. The 20-move timeout ends the
// rest. In the engine, a fixed patrol that ignores the rings entirely - Nancy
// looping Rialto - Sant'Angelo - Santo Stefano - La Fenice - San Marco - Santa
// Maria Formosa - caught him in 8 of 23 runs and timed out into s5480 in the
// other 15.
//
// (Those four percentages were simulated with Nancy restricted to the kind-0
// and kind-2 edges, which the correction above shows she is not. Re-simulated
// with her real neighbour set, a perfect tracker takes him 99.3% of the time
// rather than 93%. The ordering of the four players is unchanged.)
//
// NO PURSUIT WINS EVERY TIME, whatever the villain's rule is. Solving the
// perfect-information game on this graph backwards - Nancy must move, he must
// move, and he never moves onto her - only 151 of the 676 (Nancy, villain)
// positions are a forced catch, and not one of the six the chase can start from
// is among them. So the puzzle is genuinely probabilistic and a hook for it has
// to be a policy, not a route. See the debug affordance in the .cpp.
//
// Also still unknown: what the leading byte (0) means.
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

	// -- debug affordance, nancy_chase_autoplay ------------------------------
	// Plays Nancy's half of the chase. Both default off; see the .cpp for the
	// pursuit model and for what these read that a player only glimpses.
	static bool autoPlay();

	// True when the poll this is called on should synthesise a left click at
	// `clickAt`, which is the screen-space centre of the map hotspot Nancy
	// should take next. Called once per input poll from InputManager, and
	// inert unless the main flow is standing in a map scene with the chase
	// running.
	static bool autoPlayNextClick(Common::Point &clickAt);

	// The villain's choice, as a weight per edge of `villainNode`, parallel to
	// that node's edge list. execute() rolls on it; the autoplay pursuit sums
	// over it. Sharing one function is the point: the pursuit's model of him
	// cannot drift from what he actually does, however the weights are revised.
	static void stepWeights(const Common::Array<Node> &nodes, int16 villainNode,
		int16 nancyNode, Common::Array<uint> &weights);

	// Hop count from `from` to every node it can reach, over edges of every
	// kind. 26 nodes, so this is cheap enough to redo on each step and saves
	// having to keep anything alive across the scene's self-loop.
	static void buildDistances(const Common::Array<Node> &nodes, int16 from,
		Common::HashMap<uint32, uint> &dist);

protected:
	Common::String getRecordTypeName() const override { return "ChaseMapPuzzle"; }

	static const Node *findNode(const Common::Array<Node> &nodes, int16 id);

	uint16 _valueIndexA = 0;	// 35, where Nancy is; written by the map scenes
	uint16 _valueIndexB = 0;	// 36, where the villain is; the movies switch on it
	byte _unknown = 0;			// 0

	int16 _flagA = -1;			// 2018
	int16 _flagB = -1;			// 2019
	int16 _flagC = -1;			// 2020

	Common::Array<Node> _nodes;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_CHASEMAPPUZZLE_H
