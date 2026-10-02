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

// Nancy18's chess instant messenger. Each move is <piece><file><rank>, and the
// file and rank index an 8x8 grid of letters given by the Chess_GridN strings.
// The piece letter is ignored, so successive moves spell a word, and a word
// from the record's list gets a canned reply.
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
	RandomSoundBlock _letterSound;		// prefix; the decoded letter is appended


	uint16 _numSpinnerFrames = 0;
	uint16 _moveLength = 0;
	uint32 _spinnerInterval = 0;		// ms
	uint32 _replyDelay = 0;				// ms
	Common::String _spinnerChars;		// not a filename
	uint32 _fontID = 0;
	uint32 _maxLetters = 0;

	Common::String _selfPrefixKey;		// speaker prefix for Il Capitano
	Common::String _otherPrefixKey;		// speaker prefix for Scaramuccia
	Common::String _introKey;			// Scaramuccia's opening line

	Common::Array<Common::String> _alphabetKeys;	// pieces, files, ranks
	Common::Array<Common::String> _unknownKeys;		// "? ? ?"
	Common::Array<Common::String> _gridKeys;
	Common::Array<Common::String> _endWordKeys;		// words that end the session
	Common::Array<Exchange> _exchanges;

	// -- Unknown fields ---------------------------------------------------
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
