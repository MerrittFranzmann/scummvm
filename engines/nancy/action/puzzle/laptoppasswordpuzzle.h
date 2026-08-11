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

#ifndef NANCY_ACTION_LAPTOPPASSWORDPUZZLE_H
#define NANCY_ACTION_LAPTOPPASSWORDPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// AR type 189, Nancy16 only, and there is exactly one record of it in the whole
// game: scene S2929, whose background is OFF_LaptopPassword. The record calls
// itself "Quiz Puzzle", which is the same family as the Nancy8/Nancy9
// QuizPuzzle (typed text checked against a list of accepted answers), but the
// Nancy16 layout is different enough to need its own reader. In-game it is the
// password box on Enrico Tazza's office laptop; the accepted answers are five
// spellings of "Il Capitano".
//
// Because there is only one record, there is no second sample to diff against
// and several scalars below could not be identified. They are named _unknownN
// and read purely so the record consumes exactly.
//
// Record layout, 1/1 byte-exact with nothing left over:
//
//   char[33]   empty (no atlas: this record draws only text)
//   uint32     255                        -- unidentified
//   char[33]   font alias ("UIFontLarge" -> FONT id 6, Tahoma 22 bold)
//   uint16     11                         -- unidentified; note FONT id 11 is
//                                            "UIFontSmall" (Tahoma 12), which
//                                            would fit the 18px box better
//   uint16     500                        -- used below as the cursor blink ms
//   uint16     380                        -- unidentified
//   byte       1, byte 1                  -- unidentified pair
//   char[33]   empty, char[33] empty      -- unidentified (allowed-chars?)
//   uint16     29                         -- kNancy13PuzzleArrow, box hover cursor
//   char[33]   empty                      -- unidentified
//   SoundGroup keypress sound ("Silence", channel 14)
//   byte[7]    solve: uint16 scene (2930), uint16 frame, int16 flag (2504), byte 1
//   SoundGroup solve sound ("Beep_Computer_Multi", channel 14)
//   byte[23]   exit: RECT (empty here), uint16 cursor (29), uint16 scene (3815),
//              int16 flag (-1), byte
//   byte       pad
//   uint16     2                          -- unidentified
//   RECT       text box (244,174,421,192), viewport space
//   uint16     5, then 5 char[33]         accepted answers
//   int16      -1  answered-correctly event flag (none here)
//   int16      -1                         -- unidentified
//   SoundGroup correct sound ("silence")
//   SoundGroup wrong sound   ("silence")
//
// The exit scene, 3815, is not a scene that exists in this game; dangling scene
// references do occur elsewhere in the corpus (11 of 938 scene-change records
// point at missing scenes). The exit RECT is empty too, so nothing can trigger
// that path -- scene S2929 carries its own "Scene Change with Hotspot" record
// for the bottom-bar back button, and that is what actually leaves the puzzle.
class LaptopPasswordPuzzle : public RenderActionRecord {
public:
	LaptopPasswordPuzzle() : RenderActionRecord(7) {}
	virtual ~LaptopPasswordPuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "LaptopPasswordPuzzle"; }

	enum SolveState { kTyping, kWaitForCorrect, kWaitForSolve };

	// GUESS: nothing in the record states a maximum input length. Capped so a
	// player holding a key cannot grow the string without bound; comfortably
	// longer than every accepted answer.
	static const uint kMaxLength = 32;

	bool isAnswerCorrect() const;
	void redraw();
	SoundDescription playSoundBlock(const RandomSoundBlock &block);

	// -- File data --
	uint32 _unknownA = 0;				// 255
	Common::String _fontName;
	uint16 _unknownB = 0;				// 11
	uint16 _cursorBlinkTime = 500;
	uint16 _unknownC = 0;				// 380
	byte _unknownD = 0;					// 1
	byte _unknownE = 0;					// 1
	Common::String _unknownStrA;		// empty
	Common::String _unknownStrB;		// empty
	uint16 _boxCursorType = 0;			// 29
	Common::String _unknownStrC;		// empty
	uint16 _unknownF = 0;				// 2
	int16 _unknownG = 0;				// -1

	RandomSoundBlock _keypressSound;
	RandomSoundBlock _solveSound;
	RandomSoundBlock _correctSound;
	RandomSoundBlock _wrongSound;

	SceneChangeDescription _solveScene;
	FlagDescription _solveFlag;

	Common::Rect _exitHotspot;
	uint16 _exitCursorType = 0;
	SceneChangeDescription _exitScene;
	FlagDescription _exitFlag;

	Common::Rect _boxRect;
	Common::Array<Common::String> _answers;
	FlagDescription _answerFlag;

	// -- Runtime state --
	Common::String _typed;
	bool _cursorVisible = true;
	uint32 _nextBlink = 0;
	SolveState _solveState = kTyping;
	SoundDescription _playingSound;
	bool _exitRequested = false;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_LAPTOPPASSWORDPUZZLE_H
