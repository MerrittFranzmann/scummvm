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

#ifndef NANCY_ACTION_CHESSCODEPUZZLE_H
#define NANCY_ACTION_CHESSCODEPUZZLE_H

#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/time.h"

namespace Nancy {
namespace Action {

// "Chess IM Puzzle", action record type 188. A single instance, in nancy18
// scene 3010 (the laptop showing "Gina's Chessboard Server").
//
// The player is chatting with Scaramuccia over an instant messenger that only
// accepts chess moves. Each move is <piece><file><rank>, e.g. "KH3", and the
// file/rank pair indexes an 8x8 grid of letters; the piece letter is decoration
// and does not affect the decode. Successive moves therefore spell a word, and
// sending a word Scaramuccia recognises gets a canned reply.
//
// The grid is the eight Chess_GridN strings, one per rank:
//
//     rank 8  654321ZY        rank 4  ABCDEFGH
//     rank 7  XWVUTSRQ        rank 3  IJKLMNOP
//     rank 6  PONMLKJI        rank 2  QRSTUVWX
//     rank 5  HGFEDCBA        rank 1  YZ123456
//
// so "KH3" -> rank 3, file H -> 'P'. This is confirmed against the chess logs
// the game ships as readable documents: the Il Capitano moves in Dec29_ChessLog
// A/B/C decode to PALAZZOFOGLIO, those in Jan21_ChessLog to FENICE, and those in
// Jan24_ChessLogA to GERVASE - all three of which appear verbatim in this
// record's word list.
class ChessCodePuzzle : public RenderActionRecord {
public:
	// One accepted word and the reply it draws.
	struct Exchange {
		Common::String wordKey;			// AUTOTEXT key whose text is the word
		Common::String responseKey;		// AUTOTEXT key of Scaramuccia's reply
		int16 outFlag = -1;				// generic event flag the answer raises
	};

	ChessCodePuzzle() : RenderActionRecord(7) {}
	virtual ~ChessCodePuzzle();

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "ChessCodePuzzle"; }

	// Resolves an AUTOTEXT key to its string, or returns the key when it is not
	// one (the data only ever uses keys, but a missing table must not crash).
	Common::String text(const Common::String &key) const;

	// Recomputes the object's screen rect from the two viewport rects.
	void syncBounds();

	Common::String speech(const Common::String &prefixKey, const Common::String &body) const;
	void appendLog(const Common::String &line);
	void redraw();

	// Turns a completed move into the letter it codes for. Returns 0 when the
	// move falls outside the grid.
	char decodeMove(const Common::String &move) const;

	// Scaramuccia's own move: one character picked at random out of each of the
	// three alphabet strings.
	Common::String randomMove() const;

	void commitMove();
	void deliverReply();

	SoundDescription playSoundBlock(const RandomSoundBlock &block);
	void speakLetter(char letter);

	// -- Data ------------------------------------------------------------

	Common::String _imageName;			// always empty; the terminal chrome is
										// part of the scene's background video

	Common::Rect _logDest;				// viewport-relative, the chat transcript
	Common::Rect _inputDest;			// viewport-relative, the compose line

	RandomSoundBlock _keySound;			// ComputerBeep16
	RandomSoundBlock _sendSound;		// ComputerBeep09
	RandomSoundBlock _replySound;		// ComputerBeep15
	RandomSoundBlock _letterSound;		// name is the prefix "Chess_"; the decoded
										// letter is appended to get Chess_A ... Chess_6

	uint16 _numSpinnerFrames = 0;		// 4, and _spinnerChars is 4 characters long
	uint16 _moveLength = 0;				// 3, and there are 3 alphabets
	uint32 _spinnerInterval = 0;		// 600 ms
	uint32 _replyDelay = 0;				// 1400 ms
	Common::String _spinnerChars;		// "/-\|"
	uint32 _fontID = 0;					// 10, FONT registry id "Typewriter"
	uint32 _maxLetters = 0;				// 15; the longest word here is 14 characters

	Common::String _selfPrefixKey;		// ChessIM08, "<n><b4><c3>Il Capitano: "
	Common::String _otherPrefixKey;		// ChessIM07, "<n><b4><c6>Scaramuccia: "
	Common::String _introKey;			// ChessIM21, Scaramuccia's opening line

	Common::Array<Common::String> _alphabetKeys;	// pieces, files, ranks
	Common::Array<Common::String> _unknownKeys;		// ChessIM01, "? ? ?"
	Common::Array<Common::String> _gridKeys;		// Chess_Grid1 .. Chess_Grid8
	Common::Array<Common::String> _endWordKeys;		// ChessIM06, " DISCONNECT"
	Common::Array<Exchange> _exchanges;

	// -- Unknown fields, preserved so the record consumes byte-exactly -----
	//
	// GUESSED/UNKNOWN: a uint16 863 followed by three zero bytes sits between the
	// (empty) image name and the first sound block, and a uint16 1015 sits between
	// the font id and the message prefixes. 1015 is a valid event flag label and
	// is also the condition on two of the exchanges, but nothing here pins down
	// what it selects, so both are read and ignored rather than guessed at.
	uint16 _unknown21 = 0;
	uint16 _unknownF8 = 0;

	// -- Runtime ----------------------------------------------------------

	Common::Array<Common::String> _log;	// composed markup lines, oldest first
	Common::String _pendingMove;		// what the player has typed of this move
	Common::String _word;				// letters decoded so far

	bool _awaitingReply = false;
	bool _finished = false;
	Time _replyTime;
	Time _nextSpinnerTime;
	uint _spinnerFrame = 0;

	Graphics::ManagedSurface _logSurface;
	bool _needsTextRedraw = true;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_CHESSCODEPUZZLE_H
