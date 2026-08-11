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

#include "common/random.h"
#include "common/config-manager.h"

#include "graphics/font.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/ttffont.h"
#include "engines/nancy/puzzledata.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/util.h"
#include "engines/nancy/input.h"
#include "engines/nancy/cursor.h"

#include "engines/nancy/action/puzzle/scopapuzzle.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

// -- debug affordances ---------------------------------------------------------
//
// Reaching the match-win branch means playing several hands to 11 points, and a
// hand is 36 moves half of which wait on a human. nancy_scopa_autoplay hands
// Nancy's turn to the same move picker Enrico already uses, so a whole match can
// be run headlessly; nancy_scopa_fast shrinks the record's own delays so it runs
// in seconds; nancy_scopa_trace prints the shuffled deck, every move and every
// hand's scoring, which is what lets the run be replayed and checked offline.
// All three default off, so ordinary play is byte-for-byte what it was.

bool ScopaPuzzle::autoPlay() {
	return ConfMan.hasKey("nancy_scopa_autoplay") && ConfMan.getBool("nancy_scopa_autoplay");
}

bool ScopaPuzzle::trace() {
	return ConfMan.hasKey("nancy_scopa_trace") && ConfMan.getBool("nancy_scopa_trace");
}

uint32 ScopaPuzzle::pace(uint32 ms) {
	if (ConfMan.hasKey("nancy_scopa_fast") && ConfMan.getBool("nancy_scopa_fast")) {
		return MIN<uint32>(ms, 20);
	}

	return ms;
}

void ScopaPuzzle::readCountedRects(Common::SeekableReadStream &stream, Common::Array<Common::Rect> &out) {
	const uint16 count = stream.readUint16LE();
	readRectArray(stream, out, count);
}

void ScopaPuzzle::readSide(Common::SeekableReadStream &stream, Side &side) {
	side.unknown29 = stream.readUint16LE();
	side.winFlag = stream.readUint16LE();
	side.winFlag2 = stream.readUint16LE();

	readRect(stream, side.pileSrc);
	readRect(stream, side.pileDest);
	readRect(stream, side.scoreBox);

	// Both counts describe the sound groups that follow: six "line" groups, then
	// a bank group whose own count repeats this number (8 for side A, 7 for
	// side B). The repetition is what identifies them.
	const uint32 numLines = stream.readUint32LE();
	stream.readUint32LE(); // number of bank sounds; the group carries it again

	side.intro.readData(stream);

	side.lines.resize(numLines);
	for (uint32 i = 0; i < numLines; ++i) {
		side.lines[i].readData(stream);
	}

	side.ven.readData(stream);
	side.bank.readData(stream);
}

void ScopaPuzzle::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _saveName);
	_targetScore = stream.readUint32LE();
	_nextHandFlag = stream.readUint16LE();
	_handOverFlag = stream.readUint16LE();
	readFilename(stream, _overlayImageName);

	// 6 and 1. Read as the split of the seven trailing "misc" sound groups; only
	// the total is measured. See the header.
	_headerCounts[0] = stream.readUint32LE();
	_headerCounts[1] = stream.readUint32LE();

	for (int i = 0; i < kNumSides; ++i) {
		readSide(stream, _side[i]);
	}

	readRect(stream, _takeSrc);
	readRect(stream, _takeDest);
	readRect(stream, _discardSrc);
	readRect(stream, _discardDest);

	for (int i = 0; i < 3; ++i) {
		_timing[i] = stream.readUint32LE();
	}

	_dealSound.readData(stream);
	_placeSound.readData(stream);
	_flipSound.readData(stream);

	_miscSounds.resize(_headerCounts[0] + _headerCounts[1]);
	for (uint i = 0; i < _miscSounds.size(); ++i) {
		_miscSounds[i].readData(stream);
	}

	readCountedRects(stream, _handDest[kSideNancy]);
	readCountedRects(stream, _handDest[kSideEnrico]);
	readCountedRects(stream, _tableDest);
	readCountedRects(stream, _gridDest);
	readCountedRects(stream, _grid8);
	readCountedRects(stream, _grid10);

	readFilename(stream, _cardImageName);
	_tailValues[0] = stream.readUint32LE();
	_tailValues[1] = stream.readUint32LE();
	stream.read(_highlight, 4);
	readRect(stream, _cardBackSrc);
	readCountedRects(stream, _cardSrc);

	_trailing = stream.readUint16LE();
}

void ScopaPuzzle::init() {
	Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	g_nancy->_resource->loadImage(_overlayImageName, _overlayImage);
	g_nancy->_resource->loadImage(_cardImageName, _cardImage);
	_overlayImage.setTransparentColor(_drawSurface.getTransparentColor());
	_cardImage.setTransparentColor(_drawSurface.getTransparentColor());

	// The match score survives the scene reloading itself between hands.
	ScopaPuzzleData *data = (ScopaPuzzleData *)NancySceneState.getPuzzleData(ScopaPuzzleData::getTag());
	if (data) {
		_matchScore[0] = data->score[0];
		_matchScore[1] = data->score[1];
	}

	startHand();
}

// -- rules --------------------------------------------------------------------

uint ScopaPuzzle::numTableSlots() const {
	return MIN<uint>(_tableDest.size(), ARRAYSIZE(_table));
}

int ScopaPuzzle::tableCount() const {
	int n = 0;
	for (uint i = 0; i < numTableSlots(); ++i) {
		if (_table[i] >= 0) {
			++n;
		}
	}

	return n;
}

int ScopaPuzzle::freeTableSlot() const {
	for (uint i = 0; i < numTableSlots(); ++i) {
		if (_table[i] < 0) {
			return (int)i;
		}
	}

	return -1;
}

// Standard Scopa: a card takes an equal-ranked table card if one is there, and
// only otherwise may it take a set that sums to its rank. Nothing in the record
// states this; see the header.
void ScopaPuzzle::findCaptures(int card, Common::Array<uint32> &out) const {
	out.clear();

	const int rank = cardRank(card);
	const uint numSlots = numTableSlots();

	for (uint i = 0; i < numSlots; ++i) {
		if (_table[i] >= 0 && cardRank(_table[i]) == rank) {
			out.push_back(1u << i);
		}
	}

	if (!out.empty()) {
		return;
	}

	// Every subset that sums to the rank. Ten slots, so brute force is fine.
	for (uint32 mask = 1; mask < (1u << numSlots); ++mask) {
		int sum = 0;
		bool valid = true;

		for (uint i = 0; i < numSlots; ++i) {
			if (!(mask & (1u << i))) {
				continue;
			}

			if (_table[i] < 0) {
				valid = false;
				break;
			}

			sum += cardRank(_table[i]);
			if (sum > rank) {
				valid = false;
				break;
			}
		}

		if (valid && sum == rank) {
			out.push_back(mask);
		}
	}
}

bool ScopaPuzzle::isLegalCapture(int card, uint32 mask) const {
	if (mask == 0) {
		return false;
	}

	Common::Array<uint32> options;
	findCaptures(card, options);

	for (uint i = 0; i < options.size(); ++i) {
		if (options[i] == mask) {
			return true;
		}
	}

	return false;
}

bool ScopaPuzzle::handInPlay() const {
	return !_hand[kSideNancy].empty() || !_hand[kSideEnrico].empty() || _deckPos < kNumCards;
}

void ScopaPuzzle::applyMove(int side, int handIdx, uint32 captureMask) {
	if (handIdx < 0 || handIdx >= (int)_hand[side].size()) {
		return;
	}

	const int card = _hand[side][handIdx];
	_hand[side].remove_at(handIdx);

	if (trace()) {
		warning("SCOPA move side=%d card=%d mask=0x%x table=%d", side, card, captureMask, tableCount());
	}

	if (captureMask == 0) {
		const int slot = freeTableSlot();
		if (slot >= 0) {
			_table[slot] = card;
		} else {
			// Ten slots is more than a Scopa table realistically reaches, but a
			// dropped card would corrupt the count, so it goes to the pile of
			// whoever played it rather than vanishing.
			_captured[side].push_back(card);
		}

		playBlock(_placeSound);
	} else {
		_captured[side].push_back(card);

		for (uint i = 0; i < numTableSlots(); ++i) {
			if ((captureMask & (1u << i)) && _table[i] >= 0) {
				_captured[side].push_back(_table[i]);
				_table[i] = -1;
			}
		}

		_lastCapturer = side;
		playBlock(_flipSound);

		// Clearing the table is a scopa, except on the very last card of the
		// hand - the standard exception, not something the record states.
		if (tableCount() == 0 && handInPlay()) {
			++_scope[side];
		}

		// The six lines per side are Enrico's commentary on that side's move -
		// all twelve groups are Enrico voice files on his channel, paired across
		// the sides. Which of the six goes with which event is not recoverable
		// from one record, so one is picked at random rather than inventing a
		// mapping.
		if (!_side[side].lines.empty()) {
			const uint pick = g_nancy->_randomSource->getRandomNumber(_side[side].lines.size() - 1);
			playVoice(_side[side].lines[pick]);
		}
	}

	_selectedHandCard = -1;
	_selectedTable = 0;
	redraw();
}

// Primiera values, the standard table. 7 is worth most, then 6, then the ace.
int ScopaPuzzle::primieraValue(int card) {
	static const int values[10] = { 16, 12, 13, 14, 15, 18, 21, 10, 10, 10 };
	return values[cardRank(card) - 1];
}

void ScopaPuzzle::scoreHand() {
	int cards[kNumSides] = { 0, 0 };
	int denari[kNumSides] = { 0, 0 };
	int settebello = -1;
	int primiera[kNumSides] = { 0, 0 };

	for (int side = 0; side < kNumSides; ++side) {
		int best[4] = { 0, 0, 0, 0 };

		for (uint i = 0; i < _captured[side].size(); ++i) {
			const int card = _captured[side][i];
			++cards[side];

			if (cardSuit(card) == kSuitDenari) {
				++denari[side];
			}

			if (card == kSettebello) {
				settebello = side;
			}

			best[cardSuit(card)] = MAX(best[cardSuit(card)], primieraValue(card));
		}

		for (int s = 0; s < 4; ++s) {
			primiera[side] += best[s];
		}

		_matchScore[side] += _scope[side];
	}

	if (cards[0] != cards[1]) {
		++_matchScore[cards[0] > cards[1] ? 0 : 1];
	}

	if (denari[0] != denari[1]) {
		++_matchScore[denari[0] > denari[1] ? 0 : 1];
	}

	if (settebello >= 0) {
		++_matchScore[settebello];
	}

	if (primiera[0] != primiera[1]) {
		++_matchScore[primiera[0] > primiera[1] ? 0 : 1];
	}
}

// -- flow ---------------------------------------------------------------------

void ScopaPuzzle::startHand() {
	for (int i = 0; i < kNumCards; ++i) {
		_deck[i] = i;
	}

	for (int i = kNumCards - 1; i > 0; --i) {
		const int j = g_nancy->_randomSource->getRandomNumber(i);
		SWAP(_deck[i], _deck[j]);
	}

	_deckPos = 0;
	_lastCapturer = -1;
	_selectedHandCard = -1;
	_selectedTable = 0;

	for (int side = 0; side < kNumSides; ++side) {
		_hand[side].clear();
		_captured[side].clear();
		_scope[side] = 0;
	}

	for (uint i = 0; i < ARRAYSIZE(_table); ++i) {
		_table[i] = -1;
	}

	if (trace()) {
		Common::String deck;
		for (int i = 0; i < kNumCards; ++i) {
			deck += Common::String::format("%d ", _deck[i]);
		}

		warning("SCOPA hand-start score %d-%d deck %s", _matchScore[0], _matchScore[1], deck.c_str());
	}

	// Nancy leads. Nothing in the record says who deals or who plays first.
	_turn = kSideNancy;
	playVoice(_side[_turn].intro);

	_dealTarget = 3 * kNumSides + 4;
	_dealtSoFar = 0;
	_phase = kDealing;
	_nextStep = g_nancy->getTotalPlayTime();
	redraw();
}

void ScopaPuzzle::dealRound() {
	_dealTarget = 3 * kNumSides;
	_dealtSoFar = 0;
	_phase = kDealing;
	_nextStep = g_nancy->getTotalPlayTime();
}

void ScopaPuzzle::beginTurn(int side) {
	_selectedHandCard = -1;
	_selectedTable = 0;

	if (_hand[kSideNancy].empty() && _hand[kSideEnrico].empty()) {
		if (_deckPos >= kNumCards) {
			endHand();
			return;
		}

		_turn = side;
		dealRound();
		return;
	}

	// Both hands are dealt together, so a side is only ever empty at the end of
	// a round; guard anyway rather than letting an empty side stall the game.
	if (_hand[side].empty()) {
		side ^= 1;
	}

	_turn = side;

	if (side == kSideNancy && !autoPlay()) {
		_phase = kPlayerInput;
	} else {
		_phase = kEnemyThink;
		// 2000 ms, the first of the three timings, used as Enrico's think time.
		_nextStep = g_nancy->getTotalPlayTime() + pace(_timing[0]);
	}

	redraw();
}

void ScopaPuzzle::runAutoTurn(int side) {
	int bestHandIdx = -1;
	uint32 bestMask = 0;
	int bestValue = -1;

	for (uint h = 0; h < _hand[side].size(); ++h) {
		const int card = _hand[side][h];
		Common::Array<uint32> options;
		findCaptures(card, options);

		for (uint o = 0; o < options.size(); ++o) {
			const uint32 mask = options[o];
			int taken = 1;
			int value = 0;

			for (uint i = 0; i < numTableSlots(); ++i) {
				if (!(mask & (1u << i)) || _table[i] < 0) {
					continue;
				}

				++taken;
				value += 2;

				if (cardSuit(_table[i]) == kSuitDenari) {
					value += 3;
				}

				if (_table[i] == kSettebello) {
					value += 12;
				}
			}

			if (cardSuit(card) == kSuitDenari) {
				value += 3;
			}

			if (card == kSettebello) {
				value += 12;
			}

			if (taken == tableCount() + 1) {
				value += 20; // clears the table: a scopa
			}

			if (value > bestValue) {
				bestValue = value;
				bestHandIdx = (int)h;
				bestMask = mask;
			}
		}
	}

	if (bestHandIdx < 0) {
		// No capture available. Shed the lowest card, and prefer not to hand over
		// a denaro; a plain heuristic, not something the record specifies.
		int worst = -1;
		for (uint h = 0; h < _hand[side].size(); ++h) {
			const int card = _hand[side][h];
			const int cost = cardRank(card) + (cardSuit(card) == kSuitDenari ? 4 : 0)
				+ (card == kSettebello ? 20 : 0);

			if (worst < 0 || cost < worst) {
				worst = cost;
				bestHandIdx = (int)h;
			}
		}

		bestMask = 0;
	}

	applyMove(side, bestHandIdx, bestMask);

	_phase = kSettle;
	// 400 ms, the first of the pair by the card image name, as the beat between
	// a move landing and the turn passing.
	_nextStep = g_nancy->getTotalPlayTime() + pace(_tailValues[0]);
}

void ScopaPuzzle::endHand() {
	// Whoever captured last sweeps what is left on the table.
	if (_lastCapturer >= 0) {
		for (uint i = 0; i < numTableSlots(); ++i) {
			if (_table[i] >= 0) {
				_captured[_lastCapturer].push_back(_table[i]);
				_table[i] = -1;
			}
		}
	}

	if (trace()) {
		Common::String pileA, pileB;
		for (uint i = 0; i < _captured[0].size(); ++i) {
			pileA += Common::String::format("%d ", _captured[0][i]);
		}

		for (uint i = 0; i < _captured[1].size(); ++i) {
			pileB += Common::String::format("%d ", _captured[1][i]);
		}

		warning("SCOPA hand-end scope %d/%d pileA %s| pileB %s", _scope[0], _scope[1], pileA.c_str(), pileB.c_str());
	}

	scoreHand();

	ScopaPuzzleData *data = (ScopaPuzzleData *)NancySceneState.getPuzzleData(ScopaPuzzleData::getTag());

	// A level score at or past the target is not a win; Scopa plays another hand.
	int winner = -1;
	if ((_matchScore[0] >= (int)_targetScore || _matchScore[1] >= (int)_targetScore)
			&& _matchScore[0] != _matchScore[1]) {
		winner = _matchScore[0] > _matchScore[1] ? kSideNancy : kSideEnrico;
	}

	if (winner >= 0) {
		// The match is over. AR 7013/7023 play the winning side's "bank" lines off
		// these flags and the scene-change records follow, so the bank sounds are
		// deliberately not played here as well.
		if (trace()) {
			warning("SCOPA MATCH OVER score %d-%d target %d winner %d setting flags %d and %d",
				_matchScore[0], _matchScore[1], _targetScore, winner,
				_side[winner].winFlag, _side[winner].winFlag2);
		}

		NancySceneState.setEventFlag(_side[winner].winFlag, g_nancy->_true);
		NancySceneState.setEventFlag(_side[winner].winFlag2, g_nancy->_true);

		// AR 108, which is what clears "ScopaSave" in the original, is consume-only
		// in Nancy16, so the store is reset here instead of being left to it. The
		// original's AR 7012 also clears it when the player walks out mid-match
		// (its flag 1058/1059 branch); that path is still AR 108's, so leaving and
		// coming back resumes the match rather than starting a new one.
		if (data) {
			data->score[0] = 0;
			data->score[1] = 0;
		}
	} else {
		if (data) {
			data->score[0] = (int16)_matchScore[0];
			data->score[1] = (int16)_matchScore[1];
		}

		if (trace()) {
			warning("SCOPA hand over, no winner yet, stored score %d-%d", _matchScore[0], _matchScore[1]);
		}

		// AR 7030 plays Nancy's between-hands line off 1013, AR 7031 reloads S4450
		// off 1012, and the next hand starts from the score just stored.
		NancySceneState.setEventFlag(_handOverFlag, g_nancy->_true);
		NancySceneState.setEventFlag(_nextHandFlag, g_nancy->_true);
	}

	_phase = kHandOver;
	redraw();
}

// -- presentation -------------------------------------------------------------

void ScopaPuzzle::playBlock(const RandomSoundBlock &block) {
	if (block.names.empty()) {
		return;
	}

	const uint idx = block.names.size() == 1 ? 0 :
		g_nancy->_randomSource->getRandomNumber(block.names.size() - 1);
	const Common::String &name = block.names[idx];
	if (name.empty() || name == "NO SOUND") {
		return;
	}

	SoundDescription desc;
	desc.name = name;
	desc.channelID = block.channel;
	desc.numLoops = block.numLoops > 0 ? block.numLoops : 1;
	desc.volume = block.volume;

	g_nancy->_sound->loadSound(desc);
	g_nancy->_sound->playSound(desc);
}

void ScopaPuzzle::playVoice(const RandomSoundBlock &block) {
	// One voice at a time. Every one of these groups is a spoken line on the
	// same pair of channels, and a card game generates plenty of events.
	if (!_voice.name.empty() && g_nancy->_sound->isSoundPlaying(_voice)) {
		return;
	}

	if (block.names.empty()) {
		return;
	}

	const uint idx = block.names.size() == 1 ? 0 :
		g_nancy->_randomSource->getRandomNumber(block.names.size() - 1);
	const Common::String &name = block.names[idx];
	if (name.empty() || name == "NO SOUND") {
		return;
	}

	_voice.name = name;
	_voice.channelID = block.channel;
	_voice.numLoops = block.numLoops > 0 ? block.numLoops : 1;
	_voice.volume = block.volume;

	g_nancy->_sound->loadSound(_voice);
	g_nancy->_sound->playSound(_voice);
}

void ScopaPuzzle::outline(const Common::Rect &dest) {
	// The record's single RGB triple, used as a selection outline. That it is a
	// colour is measured; that this is what it is for is a guess.
	const uint32 colour = _drawSurface.format.RGBToColor(_highlight[0], _highlight[1], _highlight[2]);
	_drawSurface.frameRect(dest, colour);

	Common::Rect inner = dest;
	inner.grow(-1);
	if (!inner.isEmpty()) {
		_drawSurface.frameRect(inner, colour);
	}
}

void ScopaPuzzle::drawCard(int card, const Common::Rect &dest, bool highlight) {
	const Common::Rect &src = (card >= 0 && card < (int)_cardSrc.size()) ? _cardSrc[card] : _cardBackSrc;
	_drawSurface.blitFrom(_cardImage, src, Common::Point(dest.left, dest.top));

	if (highlight) {
		outline(dest);
	}
}

void ScopaPuzzle::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	// Table.
	for (uint i = 0; i < numTableSlots(); ++i) {
		if (_table[i] >= 0) {
			drawCard(_table[i], _tableDest[i], (_selectedTable & (1u << i)) != 0);
		}
	}

	// Nancy's hand face up, Enrico's face down.
	for (uint i = 0; i < _hand[kSideNancy].size() && i < _handDest[kSideNancy].size(); ++i) {
		drawCard(_hand[kSideNancy][i], _handDest[kSideNancy][i], (int)i == _selectedHandCard);
	}

	for (uint i = 0; i < _hand[kSideEnrico].size() && i < _handDest[kSideEnrico].size(); ++i) {
		drawCard(-1, _handDest[kSideEnrico][i], false);
	}

	// Capture piles, once there is anything in them.
	for (int side = 0; side < kNumSides; ++side) {
		if (!_captured[side].empty()) {
			_drawSurface.blitFrom(_overlayImage, _side[side].pileSrc,
				Common::Point(_side[side].pileDest.left, _side[side].pileDest.top));
		}
	}

	// The lit TAKE / DISCARD tiles, drawn over the dim ones in the background
	// only while that action is the legal one.
	if (_phase == kPlayerInput && _selectedHandCard >= 0
			&& _selectedHandCard < (int)_hand[kSideNancy].size()) {
		const int card = _hand[kSideNancy][_selectedHandCard];
		Common::Array<uint32> options;
		findCaptures(card, options);

		if (isLegalCapture(card, _selectedTable)) {
			_drawSurface.blitFrom(_overlayImage, _takeSrc, Common::Point(_takeDest.left, _takeDest.top));
		}

		if (options.empty()) {
			_drawSurface.blitFrom(_overlayImage, _discardSrc,
				Common::Point(_discardDest.left, _discardDest.top));
		}
	}

	// Match score, drawn into the empty bars the background already provides.
	// The record carries no font id, but the game's own Nancy16 font registry has
	// an entry aliased "ScopaTest" (id 17, Courier New 12) that exists for no
	// other puzzle, so that is the face used, in the record's own colour.
	const Graphics::Font *font = g_nancy->_ttfFonts.get("ScopaTest");
	if (!font) {
		font = g_nancy->_ttfFonts.get("UIFontMedium");
	}

	if (font) {
		const uint32 colour = _drawSurface.format.RGBToColor(_highlight[0], _highlight[1], _highlight[2]);

		for (int side = 0; side < kNumSides; ++side) {
			const Common::Rect &box = _side[side].scoreBox;
			if (box.isEmpty()) {
				continue;
			}

			const Common::String text = Common::String::format("%d", _matchScore[side]);
			const int y = box.top + (box.height() - font->getFontHeight()) / 2;
			font->drawString(&_drawSurface, text, box.left, y, box.width(), colour,
				Graphics::kTextAlignCenter);
		}
	}

	setNeedsRedraw(true);
}

int ScopaPuzzle::handSlotAt(const Common::Point &pos) const {
	for (uint i = 0; i < _hand[kSideNancy].size() && i < _handDest[kSideNancy].size(); ++i) {
		if (_handDest[kSideNancy][i].contains(pos)) {
			return (int)i;
		}
	}

	return -1;
}

int ScopaPuzzle::tableSlotAt(const Common::Point &pos) const {
	for (uint i = 0; i < numTableSlots(); ++i) {
		if (_table[i] >= 0 && _tableDest[i].contains(pos)) {
			return (int)i;
		}
	}

	return -1;
}

// -- driving ------------------------------------------------------------------

void ScopaPuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_state = kRun;
		// fall through
	case kRun: {
		const uint32 now = g_nancy->getTotalPlayTime();

		switch (_phase) {
		case kDealing:
			// 200 ms, the second timing, as the gap between dealt cards.
			while (_dealtSoFar < _dealTarget && now >= _nextStep) {
				if (_deckPos >= kNumCards) {
					_dealtSoFar = _dealTarget;
					break;
				}

				const int card = _deck[_deckPos++];

				if (_dealtSoFar < kHandSize) {
					_hand[kSideNancy].push_back(card);
				} else if (_dealtSoFar < 2 * kHandSize) {
					_hand[kSideEnrico].push_back(card);
				} else {
					const int slot = freeTableSlot();
					if (slot >= 0) {
						_table[slot] = card;
					}
				}

				playBlock(_dealSound);
				++_dealtSoFar;
				_nextStep = now + pace(_timing[1]);
				redraw();
			}

			if (_dealtSoFar >= _dealTarget) {
				beginTurn(_turn);
			}

			break;
		case kPlayerInput:
			break;
		case kEnemyThink:
			if (now >= _nextStep) {
				runAutoTurn(_turn);
			}

			break;
		case kSettle:
			if (now >= _nextStep) {
				beginTurn(_turn ^ 1);
			}

			break;
		case kHandOver:
			// The flags are set; the scene's own records take it from here, either
			// reloading S4450 for the next hand or leaving for the outro. The final
			// board stays on screen until they do.
			break;
		}

		break;
	}
	case kActionTrigger:
		finishExecution();
		break;
	}
}

void ScopaPuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _phase != kPlayerInput) {
		return;
	}

	Common::Point pos = input.mousePos;
	const Common::Rect vpPos = NancySceneState.getViewport().getScreenPosition();
	pos -= Common::Point(vpPos.left, vpPos.top);

	const int handSlot = handSlotAt(pos);
	const int tableSlot = tableSlotAt(pos);
	const bool overTake = _takeDest.contains(pos);
	const bool overDiscard = _discardDest.contains(pos);

	if (handSlot < 0 && tableSlot < 0 && !overTake && !overDiscard) {
		return;
	}

	g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

	if (!(input.input & NancyInput::kLeftMouseButtonUp)) {
		return;
	}

	input.eatMouseInput();

	if (handSlot >= 0) {
		_selectedHandCard = (_selectedHandCard == handSlot) ? -1 : handSlot;
		_selectedTable = 0;
		redraw();
		return;
	}

	if (_selectedHandCard < 0 || _selectedHandCard >= (int)_hand[kSideNancy].size()) {
		return;
	}

	const int card = _hand[kSideNancy][_selectedHandCard];

	if (tableSlot >= 0) {
		_selectedTable ^= (1u << tableSlot);
		redraw();
		return;
	}

	if (overTake) {
		if (!isLegalCapture(card, _selectedTable)) {
			return;
		}

		applyMove(kSideNancy, _selectedHandCard, _selectedTable);
	} else {
		// Discarding is only offered when the card cannot take anything, which is
		// the standard rule rather than anything the record states.
		Common::Array<uint32> options;
		findCaptures(card, options);
		if (!options.empty()) {
			return;
		}

		applyMove(kSideNancy, _selectedHandCard, 0);
	}

	_phase = kSettle;
	_nextStep = g_nancy->getTotalPlayTime() + pace(_tailValues[0]);
}

} // End of namespace Action
} // End of namespace Nancy
