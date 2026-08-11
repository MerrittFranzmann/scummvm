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

#ifndef NANCY_ACTION_STAKEOUTPUZZLE_H
#define NANCY_ACTION_STAKEOUTPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// AR type 212 in Nancy16, where the type is reused: in the older games 212 is
// OrderingPuzzle/kOrderItems, which is why the factory keeps that branch.
//
// There is exactly one Nancy16 record of this type, 8797 payload bytes, in
// scene S5610 ("ORP stake out puzzle", background ORP_ExteriorA). Its own
// description string is "UI Control", which is a copy-paste: "UI Control" is the
// description AR type 34 carries (S5615's ShowPDA record, for one), and nothing
// in this record's payload has anything to do with a UI toggle. Descriptions in
// this game are not evidence - AR 171's twelve records all call themselves
// "Turning puzzle" while being lock picks.
//
// WHAT IT IS: the Venice night stake-out. Four GdiF agents hide in the ORP
// courtyard along with Nico; each radios Nancy, in Italian, to say where he or
// she is; the eleven possible hiding places peek out from time to time; and
// Nancy answers by clicking the one that is Nico and none of theirs. The game
// states the rules itself, in CVTX `Stake_IntroA` / `Stake_IntroB`, which S5617
// plays as a ConcatSound on the way in:
//
//   "Nancy, the four of us agents will be hiding in the courtyard waiting for
//    Nico. We'll call over the radio to tell you where we are, and peek out
//    from time to time."
//   "If you see Nico, let us know where he is and we will try to catch him.
//    But don't give away our hiding places."
//
// ===========================================================================
// RECORD LAYOUT - 1/1 record, 8797/8797 payload bytes, nothing left over
// ===========================================================================
//
//   uint16  69, uint16 70    two player-table value slots (see SCORING below)
//   str33   "StakeOutDialog" \  the NDUI panel and the list-row template the
//   str33   "TextEntry"      /  report list is built from (see THE UI below)
//   uint16  29               unidentified; the same value AR 211's two side
//                            blocks open with, and the same value S4450's
//                            hotspot records use as a cursor id
//   double  0.75, 1.0, 0.5, 4.0, 12.0        five tuning scalars
//   double  (0,2), (0,2), (0,2)              three ranges
//   uint16  11               silhouette count
//     per silhouette: str33 label ("A".."K"), Rect dest, str33 image name,
//                     uint16 n1, n1 x Rect, uint16 n2, n2 x Rect,
//                     RandomSoundBlock (one NDStakeOut01ENGnn line, channel 25)
//   uint16  5                suspect count
//     per suspect: str33 name, byte, uint16, RandomSoundBlock x 3
//
// The eleven doubles are pinned as a block by what follows them parsing at all;
// the split into "five scalars then three (min,max) pairs" is a reading of that
// block, not a fact about it.
//
// ===========================================================================
// WHAT IS MEASURED
// ===========================================================================
//
//  * The eleven silhouette blocks name ORP_ShadowAANIM_OVL .. ORP_ShadowKANIM_OVL
//    and each carries two arrays of exactly 17 source rects. The two arrays
//    together fill their image exactly, which is what makes the alignment
//    checkable rather than merely fitted: every one of the eleven sheets is a
//    7x5 grid of cells at the block's own cell size with a one-pixel gutter,
//    framesA walks cells 0..16 in reading order and framesB walks 18..34, and
//    the union of them is the whole image to the pixel. ORP_ShadowA is 232x211
//    and 7*33+1 = 232, 5*42+1 = 211; ORP_ShadowF is 316x486 and 7*45+1 = 316,
//    5*97+1 = 486; and so on, 11/11.
//  * The one grid cell the two arrays skip is the same one every time, index 17,
//    the cell that would join them. So this is a peek in two halves - framesA
//    coming out, framesB going back in - and not one 34-frame loop.
//  * Each block's dest rect is exactly its own cell size - 32x41 for A, 47x50
//    for B, 44x96 for F - so the frames are blitted 1:1, not scaled. The eleven
//    dests are disjoint and spread across the frame, five along the upper
//    storeys and six at ground level.
//  * The five suspects are Nico, Bruno, Eva, Gino and Sabina. Four of them carry
//    three sound groups each in their own variant of the same set -
//    Stake29/30/31, then Stake26/27/28, then Stake32 - suffixed A for Bruno,
//    C for Eva, B for Gino, D for Sabina, one dedicated channel each (Bruno
//    15/14, Eva 16, Gino 17, Sabina 18). Nico's three groups are empty: the
//    record stores a bare uint16 0 for each, which is exactly what
//    RandomSoundBlock::readData already does with a zero count, so the structure
//    is the engine's own idiom rather than a special case invented here. Nico is
//    also the only suspect whose leading byte is 1 rather than 0.
//  * What the three groups are is settled by their CVTX captions:
//      group 0  Stake29/30/31  "That's me, not Nico!" / "That's not Nico." /
//                              "That's me!"
//      group 1  Stake26/27/28  "Non e la. (He's not there.)" /
//                              "Non riesco a trovarlo. (I can't find him.)" /
//                              "E andato via. (He's gone.)"
//      group 2  Stake32        "Fermati!"  ("Stop!")
//    So group 0 answers a click on that agent's own hiding place, group 1 a
//    click on a place nobody is at, and group 2 is the catch.
//  * Each silhouette's own sound is Nancy's half of the exchange, not an agent's:
//    NDStakeOut01ENGnn resolves in CVTX to "He's behind the tree.", "He's next to
//    the gondolas.", and so on - Nancy naming the place she is pointing at. The
//    retail frame shows exactly that line in the centred VO caption after a
//    click, which is why it is routed to the narration panel here.
//  * The letter -> place mapping is pinned by a complement argument, not by
//    matching strings by eye. CVTX holds two disjoint families:
//      - <SuspectName><Letter>, Bruno/Eva/Gino/Sabina x A..K = 44/44 keys, and
//        0/11 for Nico, who never radios;
//      - Stake<NN><Suffix>, NN an index into the 25-entry Italian NDStakeOut
//        location list, Suffix A..D the same four agents.
//    The eleven letters use Italian indices {1,2,4,5,7,9,12,14,17,18,20} and the
//    Stake<NN> family covers exactly the other fourteen, {3,6,8,10,11,13,15,16,
//    19,21,22,23,24,25}. 11 + 14 = 25 with no overlap: the per-letter family is
//    the set this record actually uses and the numbered family is the leftovers.
//    Reading: A red umbrella, B blue umbrella, C right of the red flowers,
//    D left of the white flowers, E cat, F tree, G statue, H gondolas,
//    I fountain, J car, K wall.
//  * The apparent English/Italian disagreement on B is not one. The English list
//    is the Italian list with "blue umbrella" removed, so ENG04..ENG25 are
//    Italian 05..25 shifted by one - and B's sound is NDStakeOut01ENG03a, the
//    only one of the eleven with a letter suffix, which is the blue-umbrella line
//    slotted back in between ENG03 and ENG04. (It is the one silhouette with no
//    CVTX caption of its own, so B's click draws no subtitle. That is shipped
//    data, not a gap here.)
//  * The four reports play on channel 24, numLoops 1, volume 85. That is not in
//    this record - it is what S5611-S5614's four PlaySound records use, 4/4
//    identical, and those four scenes are the same four reports played out as the
//    scripted tutorial round (BrunoD, GinoI, EvaF, SabinaJ over
//    ORP_ShadowWhiteFlowers/Fountain/Tree/Auto_ANIM).
//
// ===========================================================================
// SCORING - from S5610's other four records, byte-exact
// ===========================================================================
//
// S5610 holds this record and four "Scene Change with Frame" records, each with
// two dependencies: a type 13 player-table compare (condition 0, so `==`, with
// the value in timeData) and a type 15 difficulty test.
//
//    PlayerTable[69] == 4  &&  difficulty 0 (Junior)  -> S5620
//    PlayerTable[69] == 5  &&  difficulty 2 (Senior)  -> S5620
//    PlayerTable[70] == 5  &&  difficulty 0 (Junior)  -> S5625
//    PlayerTable[70] == 3  &&  difficulty 2 (Senior)  -> S5625
//
// S5620 raises flag 2530 EV_Solved_Stakeout and plays ORP_Win_ANIM with
// Siren_European05 and PoliceWhistle01..05: the arrest. S5625 leads to S5626,
// which picks one of three Typewriter Text records at random, and those three
// are CVTX Stake_Lose01/02/03 - "The Bad News: No one got caught during the
// stakeout." - then S5627 and S5628, a "Load a saved Game" screen. So 69 counts
// catches and 70 counts misses, and the difficulty scaling reads the right way
// round: Senior needs one more catch and allows two fewer misses.
//
// Nothing else in the game's 14143 records writes value 69, and the only other
// writers of value 70 are S6543-S6550, an unrelated block. So this record is
// what must be writing them, and until it did, S5610 had no live exit at all.
//
// ===========================================================================
// THE UI - and why nothing is typed
// ===========================================================================
//
// "StakeOutDialog" and "TextEntry" are NDUI widget names, and the STAKEOUT
// member holds exactly three chunks:
//
//   [0] Panel  StakeOutDialogContainer (60,450)-(395,534) + StakeOutScrollBar
//   [1] 0x28   Static TextEntry (0,0)-(320,0), font Convo, wrap on
//   [2] Panel  StakeOutDialog (60,450)-(380,534), OnShow/OnHide -> container
//
// `TextEntry` is a class-2 Static, not a class-7 EditBox - a list-row template,
// structurally identical to JOURNAL's `JournalEntry` (also a 317-byte 0x28
// chunk) and TASKLIST's `TasklistEntry`. nancy18's only two real EditBoxes are
// `SceneName` in the cheat panel and `SaveName` in the save dialog. The name is
// just what the artist called the row: there is no text entry in this puzzle and
// no text-entry widget was needed to finish it.
//
// The retail frame confirms the reading: four lines in the list, "Dietro il
// gatto. / Dietro l'albero. / Dietro la parete. / Dietro la statua." - letters
// E, F, K, G - in four different colours, the colours coming from the <c6>..<c9>
// codes those CVTX strings carry, which drawStyledText already honours.
//
// ===========================================================================
// WHAT IS INFERRED - the parts below are a reading, not a fact
// ===========================================================================
//
//  * That the four agents take four distinct silhouettes, Nico a fifth, and the
//    other six stay empty. The record does not say how many places are occupied.
//    It is what makes group 1 ("He's not there") reachable at all and what makes
//    the four reports worth listening to.
//  * That the agents are placed and announced once, and that only Nico moves -
//    re-placed after every judged click, onto a place no agent holds. Three
//    things point this way and none of them is decisive: the intro says "we'll
//    call over the radio to tell you where we are", singular; the three retail
//    frames of S5610 show the same four reports before and after a click; and
//    the walkthroughs describe having to find Nico again for each of the three
//    or four catches, which needs him to move and needs nothing else to.
//  * Which agent answers a click on Nico or on an empty place. A click on an
//    agent is answered by that agent - that is what group 0's text means - but
//    for the other two cases the record carries four equivalent sound sets and
//    no rule, so one is picked at random.
//  * The animation rate. `_tuning[4]` is 12.0 and is used here as frames per
//    second, which puts a 17-frame half-peek at 1.4s. Nothing names it.
//  * The three (0, 2) ranges, used as the random second-delays around a peek:
//    before it, held out at the apex, and after it. They are the only ranges in
//    the record and 0-2 seconds is the right order of magnitude for "peek out
//    from time to time", but no unit is stated and nothing says which is which.
//
// ===========================================================================
// WHAT IS STILL UNKNOWN, and is therefore not used at all
// ===========================================================================
//
//  * `_tuning[0..3]` = 0.75, 1.0, 0.5, 4.0. 4.0 is the number of agents, which
//    is suggestive and nothing more; the other three are unattached. They are
//    read and stored and deliberately not wired to anything.
//  * The uint16 29 in the header.
//  * The per-suspect byte (1 for Nico, 0 for the rest). It reads as "is the
//    target", and this class derives that from the empty sound groups instead,
//    because that is the same fact stated twice and the sound groups are the
//    half corroborated by the sound files on disk.
//  * The per-suspect uint16: Nico 14, Bruno 24, Eva 9, Gino 10, Sabina 11.
//    Bruno's 24 is the channel S5611-S5614 play the reports on, but Eva's 9,
//    Gino's 10 and Sabina's 11 are not their channels (16, 17, 18), so that is a
//    coincidence and not a reading. Not a silhouette index, a flag label or a
//    player-table slot either - none of those sets match.
class StakeOutPuzzle : public RenderActionRecord {
public:
	StakeOutPuzzle() : RenderActionRecord(7) {}
	virtual ~StakeOutPuzzle();

	// One of the eleven silhouettes: a dest on the courtyard and the two halves
	// of one peek, packed into a single sheet.
	struct Silhouette {
		Common::String label;			// "A" .. "K"
		Common::Rect dest;
		Common::Path imageName;			// ORP_Shadow?ANIM_OVL
		Common::Array<Common::Rect> framesA;	// coming out
		Common::Array<Common::Rect> framesB;	// going back in
		RandomSoundBlock line;			// NDStakeOut01ENGnn, channel 25

		// Runtime
		Graphics::ManagedSurface image;
		enum PeekState { kHidden, kEmerging, kOut, kWithdrawing };
		PeekState peek = kHidden;
		uint frame = 0;
		uint32 nextStepTime = 0;
		bool occupied = false;
	};

	struct Suspect {
		Common::String name;			// Nico, Bruno, Eva, Gino, Sabina
		byte unknownByte = 0;			// 1 for Nico, 0 for the other four
		uint16 unknownValue = 0;		// 14, 24, 9, 10, 11
		RandomSoundBlock lines[3];		// empty for Nico

		// Runtime
		int position = -1;				// index into _silhouettes, -1 if unplaced

		// Nico is the one suspect with no recorded lines at all; see the header.
		bool isTarget() const { return lines[0].names.empty() && lines[2].names.empty(); }
	};

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "StakeOutPuzzle"; }

	static void readCountedRects(Common::SeekableReadStream &stream, Common::Array<Common::Rect> &out);

	// kReporting is the opening pass in which the four agents radio in, one at a
	// time; kWatching is the stake-out proper; kJudging holds off the score until
	// the answer to a click has been heard out.
	enum Phase { kReporting, kWatching, kJudging };

	void placeAgents();
	void placeNico();
	void stepSilhouettes(uint32 now);
	void stepReports(uint32 now);
	void judgeClick(uint index, uint32 now);
	void applyScore();
	void redrawSilhouettes();
	void publishList();
	void say(const Common::String &line);

	// Adds `delta` to a player-table value the same way AR 108 SetValue does, so
	// 69 and 70 are written through exactly the path the dependencies read.
	void addToValue(uint16 index, int16 delta);

	uint32 frameDelayMs() const;
	uint32 randomRangeMs(uint which) const;
	SoundDescription playSoundBlock(const RandomSoundBlock &block);
	uint pickResponder() const;

	// Debug affordances; both default off. See the .cpp.
	bool autoPlay() const;
	bool autoMiss() const;
	int autoTarget() const;

	uint16 _valueIndexA = 0;			// 69, catches
	uint16 _valueIndexB = 0;			// 70, misses
	Common::String _dialogName;			// "StakeOutDialog"
	Common::String _entryName;			// "TextEntry"
	uint16 _unknown29 = 0;

	double _tuning[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
	double _ranges[3][2] = {{ 0.0, 0.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }};

	Common::Array<Silhouette> _silhouettes;
	Common::Array<Suspect> _suspects;

	Phase _phase = kReporting;
	Common::Array<uint> _agents;		// indices into _suspects, in record order
	int _nico = -1;						// index into _suspects

	uint _reportsDone = 0;
	uint32 _reportWaitUntil = 0;

	// Channel 24 / volume 85, from S5611-S5614's four PlaySound records, 4/4.
	static const int16 kReportChannel = 24;
	static const int16 kReportVolume = 85;

	int _pendingValueIndex = -1;
	int16 _pendingDelta = 0;
	int _pendingResponder = -1;			// index into _suspects
	int _pendingGroup = 0;				// which of that suspect's three groups
	bool _responsePlayed = false;
	int16 _nancyChannel = -1;
	int16 _responseChannel = -1;
	uint32 _judgeWaitUntil = 0;

	uint32 _autoNextClick = 0;
	uint _clicksJudged = 0;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_STAKEOUTPUZZLE_H
