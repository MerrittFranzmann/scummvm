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

// "Scopa Card Game", AR type 211 in Nancy16. Nancy plays the Italian card game
// Scopa against Enrico in S4450.
//
// Images:
//   CAS_ScpaCrd_OVL      the two capture-pile tiles and the lit TAKE/DISCARD buttons
//   CAS_ScpaCrd-TXT_OVL  10x4 card atlas, rows denari/spade/coppe/bastoni and
//                        columns rank 1-10, plus a card back
//
// The record has no scene change; S4450's other records act on its flags:
//   winFlag (1010 / 1011)  that side won the match: play the outro and leave
//   1012                   reload S4450 for the next hand
//   1013                   Nancy's between-hands line
// 1010-1013 are per-scene flags cleared on every scene change, so the reload
// does not loop. The match score is kept in the "ScopaSave" puzzle data.
//
// The rules are standard Scopa; the record does not state them. Side A is
// Nancy. Who leads, the timing units (assumed to be ms) and the header's 6/1
// split are guesses, and the uint16 29 opening each side block is unknown.
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
		Common::Array<RandomSoundBlock> lines; // sweep variants, cards, coins, seven, primes
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
	// The runtime table is ten slots, one bit each in the capture masks
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
	// Picks and plays Enrico's move with a simple heuristic
	void runAutoTurn(int side);
	void endHand();
	void finishHand();
	void beginTallyStage();
	void drawTally();
	void tallyVoice(const Common::String &name);
	bool handInPlay() const;

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
	// Authored end-of-hand card and prime-value layouts.
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
		kTally,       // reveal cards and award each category before the next hand
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
	int _tallyRemaining[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	int _tallyStage = 0;
	int _tallySide = -1;
	int _tallyShown = 0;
	uint32 _tallyHoldUntil = 0;
	int _tallyAwards[4] = { -1, -1, -1, -1 };
	int _primeCards[2][4] = { { -1, -1, -1, -1 }, { -1, -1, -1, -1 } };
	int _primeTotals[2] = { 0, 0 };
	Common::Array<int> _tallyCards;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_SCOPAPUZZLE_H
