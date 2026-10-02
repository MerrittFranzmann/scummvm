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

// Nancy16's laptop password box. The record calls itself "Quiz Puzzle", like
// the Nancy8/9 QuizPuzzle, but the layout is different. Only one record of
// this type exists, so several fields are unidentified.
//
//   char[33]   empty, no atlas
//   uint32     unknown
//   char[33]   font alias
//   uint16     unknown
//   uint16     cursor blink time in ms
//   uint16     unknown
//   byte       unknown, byte unknown
//   char[33]   unknown, char[33] unknown
//   uint16     cursor type over the text box
//   char[33]   unknown
//   SoundGroup keypress sound
//   byte[7]    solve: uint16 scene, uint16 frame, int16 flag, byte
//   SoundGroup solve sound
//   byte[23]   exit: RECT, uint16 cursor, uint16 scene, int16 flag, byte
//   byte       pad
//   uint16     unknown
//   RECT       text box, viewport space
//   uint16 + n x char[33]  accepted answers
//   int16      answered-correctly event flag
//   int16      unknown
//   SoundGroup correct sound
//   SoundGroup wrong sound
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

	// The record has no maximum input length; this is longer than every answer
	static const uint kMaxLength = 32;

	bool isAnswerCorrect() const;
	void redraw();
	SoundDescription playSoundBlock(const RandomSoundBlock &block);

	// -- File data --
	uint32 _unknownA = 0;
	Common::String _fontName;
	uint16 _unknownB = 0;
	uint16 _cursorBlinkTime = 500;
	uint16 _unknownC = 0;
	byte _unknownD = 0;
	byte _unknownE = 0;
	Common::String _unknownStrA;
	Common::String _unknownStrB;
	uint16 _boxCursorType = 0;
	Common::String _unknownStrC;
	uint16 _unknownF = 0;
	int16 _unknownG = 0;

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
