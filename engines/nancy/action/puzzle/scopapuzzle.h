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

#ifndef NANCY_ACTION_SCOPAPUZZLE_H
#define NANCY_ACTION_SCOPAPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// "Scopa Card Game", AR type 211 in Nancy16. A single record, in S4450, where
// Nancy plays the Italian fishing game Scopa against Enrico. 4311 payload bytes,
// read byte-exactly by readData() below.
//
// WHAT IS MEASURED
//
// The two images the record names both fit their rect sets exactly, which is
// what makes the layout below checkable rather than merely fitted:
//
//   CAS_ScpaCrd_OVL      211x141. Holds exactly four tiles, and the four
//                        source rects in the record address exactly those:
//                        (1,1,105,105)   a stack of card backs on teal
//                        (106,1,210,105) the same stack on brown
//                        (1,106,91,140)  a green "TAKE" button
//                        (92,106,182,140) a green "DISCARD" button
//   CAS_ScpaCrd-TXT_OVL  591x456. A 10x4 atlas of Italian playing cards plus
//                        one card back tucked under the last column. The
//                        record's 40 source rects tile rows y=1/92/183/274 and
//                        columns x=1..532 with no gaps and nothing left over,
//                        and the separate 41st rect (532,365,590,455) is the
//                        next slot down, which is where the back sits.
//                        Row order in the image is denari, spade, coppe,
//                        bastoni; column order is rank 1..10. So card index
//                        i = suit * 10 + (rank - 1), read straight off the art.
//
// The destination rects land on the scene's Bink background (S4450,
// CAS_Scpa-TXT_PUZ) exactly where the artwork already draws slots for them:
// three 58x90 slots along the brown top edge (Enrico's hand), three along the
// blue bottom edge (Nancy's hand), a 5x2 green grid in the middle (the table),
// a blue diamond bottom-left and a brown diamond top-right (the two capture
// piles, matching the teal/brown backgrounds of the two pile tiles), an empty
// bar under "ENRICO" top-left and under "NANCY" bottom-right (the two score
// boxes), and dim "TAKE"/"DISCARD" labels on the right edge that the two green
// button tiles are the lit versions of.
//
// The record carries no scene change. Its outputs are event flags, and the
// other records in S4450 are the consumers - which is how the flags below are
// identified, since each one is the dependency of a neighbouring record:
//
//   flag 1010 -> AR 7013 plays Enrico24..68 (this record's side-A "bank"
//                sound list, name for name) and then AR 7015/7017/7019/7022
//                leave the scene. Side A winning the match.
//   flag 1011 -> AR 7023 plays Enrico28..74 (side B's "bank" list) and then
//                AR 7025/7027/7029 leave the scene. Side B winning the match.
//   flag 1012 -> AR 7031, a scene change back to 4450. Reload for another hand.
//   flag 1013 -> AR 7030, a Nancy voice line (VEN148/149/150).
//   flags 2332 / 1022 are the second flag in each side block; 2332 is cleared
//                again in scenes 1314 and 1315, so it outlives the puzzle.
//
// 1010-1013 and 1022 all fall inside the engine's generic per-scene flag range,
// labels 1010-1040, which Scene::clearSceneData() wipes on every scene change.
// That is what keeps the self-reload one-shot: the record sets 1012, S4450
// reloads, and the flag is already clear by the time the fresh copy of AR 7031
// is evaluated. AR 7031 also carries two kSound dependencies - channels 25 and
// 24, the two voice channels, both silent - so the reload waits for AR 7030's
// line to finish rather than cutting it off. Measured: playing a hand out to
// the end produces exactly one extra "Loading new scene 4450" and no loop, and
// three hands in a row each produce exactly one.
//
// The 11-point match-win branch is measured, not inferred. ScummVM's
// RandomSource is a seedable xorshift32, so the shuffle is reproducible: with
// random_seed=12345 the engine's first deck is exactly what the same
// Fisher-Yates loop produces offline after four leading draws. Running whole
// matches under the nancy_scopa_autoplay debug affordance (see the .cpp) and
// replaying every logged hand through an independent scorer reproduces the
// engine's capture piles, scopa counts and per-hand points exactly, and the
// running score survives each self-reload - seed 12345 goes 0-0, 0-3, 2-4, 5-4,
// 6-8, 10-12 over five hands. Both ends then fire for real:
//
//   seed 101  Nancy 11 - 7  -> flags 1010 + 2332 -> AR 7013 (Enrico24..68, which
//             also raises 1014) -> the 1015 block -> scene change to 1314, where
//             Enrico says "because you beat me, we can finally talk business".
//   seed 12345 / 7 / 2024, Enrico wins -> flags 1011 + 1022 -> AR 7023
//             (Enrico28..74, which also raises 1018) -> the 1019 block -> scene
//             change to 1315.
//
// Together with the leading str33 "ScopaSave" - which AR 7012, "Clear Saved
// Data", names as the thing it wipes once the game is over or abandoned - and
// the header's uint32 11, that fixes the shape of the game: a match played to
// 11 points over however many hands it takes, with the running score living in
// named save data and the scene reloading itself between hands.
//
// WHAT IS INFERRED. Called out again at each use site.
//
//  - The rules. Nothing in the record states them; they are standard Scopa
//    (capture by equal rank or by a summing subset, equal rank takes priority,
//    clearing the table is a scopa, and the hand scores a point each for most
//    cards, most denari, the settebello and primiera, plus one per scopa).
//    The record only supports that reading rather than establishing it: 40
//    cards, three-card hands, a ten-slot table, two capture piles, a TAKE and
//    a DISCARD button and a target of 11 are all what Scopa needs.
//  - Nancy plays first. That side A is Nancy is measured, not guessed: side A's
//    intro group is VEN019-023 on channel 25 and side B's is Enrico19-23 on
//    channel 24, and side A owns the bottom-left pile and the bottom-right score
//    box, the player's half of the screen. Who deals, and so who leads, is not
//    stated anywhere.
//  - The three uint32 timings (2000, 200, 80) and the two after the card image
//    name (400, 200) are used as millisecond delays. They read like durations
//    and are in the right range, but no unit is stated.
//  - The RGB triple (255,156,42) is used to outline selected cards.
//  - The six per-side "line" sound groups. All twelve are Enrico voice files on
//    channel 24, his channel, and they pair off across the two sides
//    (44/45, 46/47, 58/59, 54/55, 56/57, 61/62), so a block belongs to the side
//    whose move Enrico is remarking on rather than to a speaker. Which of the
//    six goes with which event is not recoverable from one record, so a capture
//    plays one of that side's six at random rather than pretending to a mapping.
//  - The uint32 pair 6/1 in the header is read as the split of the seven
//    trailing "misc" sound groups. Only their total, seven, is measured.
//
// WHAT IS NOT DECODED
//
//  - The uint16 29 that opens each side block. Both sides carry it. It is the
//    same value the neighbouring hotspot records (AR 7007/7009) use as a
//    cursor id, but that is a coincidence of value, not evidence.
//  - Three rect arrays, 40 + 8 + 10, that describe a full-screen 10x4 card
//    layout and two centred subsets of it. They are read and kept, and nothing
//    draws them. They cannot be the table or either hand - those have their own
//    arrays, and these overlap them - so they belong to some second view of the
//    cards (an end-of-hand tally is the obvious guess, and the two 58x20 strips
//    at the foot of the 10-array do look like count fields), but which view,
//    and when it is shown, is not determinable from this one record.
class ScopaPuzzle : public RenderActionRecord {
public:
	ScopaPuzzle() : RenderActionRecord(7) {}
	virtual ~ScopaPuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "ScopaPuzzle"; }

	static const int kNumCards = 40;
	static const int kHandSize = 3;
	static const int kNumSides = 2;
	static const int kSideNancy = 0;
	static const int kSideEnrico = 1;
	// Card index -> suit and rank, read off the atlas art.
	static const int kSuitDenari = 0;
	static const int kSettebello = kSuitDenari * 10 + 6; // seven of coins

	static int cardSuit(int card) { return card / 10; }
	static int cardRank(int card) { return card % 10 + 1; }

	// One player's half of the record. The two blocks are byte-identical in
	// shape; only the values differ.
	struct Side {
		uint16 unknown29 = 0;
		uint16 winFlag = 0;      // set when this side reaches the target score
		uint16 winFlag2 = 0;     // set alongside it; outlives the puzzle for side A
		Common::Rect pileSrc;    // capture-pile tile in the overlay image
		Common::Rect pileDest;   // where that pile is drawn
		Common::Rect scoreBox;   // the empty bar the background already draws
		RandomSoundBlock intro;
		Common::Array<RandomSoundBlock> lines; // six; see the header note
		RandomSoundBlock ven;
		RandomSoundBlock bank;   // duplicated by AR 7013/7023, so not played here
	};

	// -- reading --
	void readSide(Common::SeekableReadStream &stream, Side &side);
	static void readCountedRects(Common::SeekableReadStream &stream, Common::Array<Common::Rect> &out);

	// -- rules --
	// Every subset of the table that the given card may legally capture, as
	// bitmasks over _table. Equal rank beats summing, per the standard rule, so
	// when any single equal-rank card is present only those are returned.
	void findCaptures(int card, Common::Array<uint32> &out) const;
	bool isLegalCapture(int card, uint32 mask) const;
	// The runtime table is a fixed ten slots and the capture masks are bits in a
	// uint32; the array from the file is exactly ten, but never index past it.
	uint numTableSlots() const;
	int tableCount() const;
	int freeTableSlot() const;
	void applyMove(int side, int handIdx, uint32 captureMask);
	void scoreHand();
	static int primieraValue(int card);

	// -- flow --
	void startHand();
	void dealRound();
	void beginTurn(int side);
	// Picks and plays a move for one side with the heuristic below. Enrico always
	// uses it; Nancy only does under the nancy_scopa_autoplay debug affordance
	// (see the .cpp), which is what makes a whole 11-point match reachable in a
	// headless run.
	void runAutoTurn(int side);
	void endHand();
	bool handInPlay() const;

	// -- debug affordances, read once from the config --
	static bool autoPlay();
	static bool trace();
	// Shrinks the record's own delays under autoplay so a multi-hand match runs
	// in seconds rather than minutes. Off by default, so normal play is unchanged.
	static uint32 pace(uint32 ms);

	// -- presentation --
	void redraw();
	void drawCard(int card, const Common::Rect &dest, bool highlight);
	void outline(const Common::Rect &dest);
	void playBlock(const RandomSoundBlock &block);
	void playVoice(const RandomSoundBlock &block);
	int handSlotAt(const Common::Point &pos) const;
	int tableSlotAt(const Common::Point &pos) const;

	// -- file data --
	Common::String _saveName;
	uint32 _targetScore = 11;
	uint16 _nextHandFlag = 0;  // 1012: reload S4450 for the next hand
	uint16 _handOverFlag = 0;  // 1013: Nancy's between-hands line
	Common::Path _overlayImageName;
	uint32 _headerCounts[2] = { 0, 0 }; // 6 and 1; read as the misc-sound split

	Side _side[kNumSides];

	Common::Rect _takeSrc, _takeDest;
	Common::Rect _discardSrc, _discardDest;
	uint32 _timing[3] = { 0, 0, 0 };    // 2000, 200, 80

	RandomSoundBlock _dealSound;
	RandomSoundBlock _placeSound;
	RandomSoundBlock _flipSound;
	Common::Array<RandomSoundBlock> _miscSounds;

	Common::Array<Common::Rect> _handDest[kNumSides]; // [0] Nancy (bottom), [1] Enrico (top)
	Common::Array<Common::Rect> _tableDest;           // ten slots, in the data's own order
	// Undecoded; see the header note. Kept so the reading stays visible.
	Common::Array<Common::Rect> _gridDest;
	Common::Array<Common::Rect> _grid8;
	Common::Array<Common::Rect> _grid10;

	Common::Path _cardImageName;
	uint32 _tailValues[2] = { 0, 0 };   // 400, 200
	byte _highlight[4] = { 0, 0, 0, 0 }; // 255,156,42,0
	Common::Rect _cardBackSrc;
	Common::Array<Common::Rect> _cardSrc; // forty, indexed suit * 10 + rank - 1
	uint16 _trailing = 0;

	// -- runtime --
	enum Phase {
		kDealing,     // cards going out one at a time
		kPlayerInput, // Nancy picks a card, then table cards, then TAKE/DISCARD
		kEnemyThink,  // a beat before Enrico plays
		kSettle,      // a played move is on screen; wait, then pass the turn
		kHandOver     // flags set, nothing more to do
	};

	Graphics::ManagedSurface _overlayImage;
	Graphics::ManagedSurface _cardImage;

	int _deck[kNumCards] = { 0 };
	int _deckPos = 0;
	Common::Array<int> _hand[kNumSides];
	int _table[10] = { 0 };            // card index, or -1
	Common::Array<int> _captured[kNumSides];
	int _scope[kNumSides] = { 0, 0 };  // scope scored this hand
	int _matchScore[kNumSides] = { 0, 0 };
	int _lastCapturer = -1;
	int _turn = kSideNancy;

	Phase _phase = kDealing;
	uint32 _nextStep = 0;
	int _dealtSoFar = 0;
	int _dealTarget = 0;
	int _selectedHandCard = -1;   // index into _hand[kSideNancy]
	uint32 _selectedTable = 0;    // bitmask over _table
	SoundDescription _voice;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_SCOPAPUZZLE_H
