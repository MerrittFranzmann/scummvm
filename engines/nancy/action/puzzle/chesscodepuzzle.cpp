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

#include "engines/nancy/nancy.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/input.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/util.h"

#include "engines/nancy/action/puzzle/chesscodepuzzle.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

// COLR 6 is 00FF00. The transcript's own lines set their colour from the
// speaker prefix (<c6> green for Scaramuccia, <c3> white for Il Capitano); this
// is only the default for what the record composes for itself, i.e. the compose
// line and the spinner, which the retail recording shows in green.
static const int kTerminalTextColour = 6;

// The record has no exit of its own - the player closes the laptop through the
// scene's own hotspots - so the virtual keyboard is turned back off here rather
// than in kActionTrigger, which is never reached.
ChessCodePuzzle::~ChessCodePuzzle() {
	g_nancy->_input->setVKEnabled(false);
}

void ChessCodePuzzle::init() {
	syncBounds();
	setTransparent(true);

	RenderObject::init();
}

// The record carries no rect for itself, only the two areas it writes into, so
// the object covers their union. A viewport-relative RenderObject keeps
// _screenPosition in viewport coordinates - getScreenPosition() and
// convertToLocal() both do the conversion themselves - so the union goes in
// unconverted.
void ChessCodePuzzle::syncBounds() {
	Common::Rect bounds = _logDest;
	bounds.extend(_inputDest);

	if (bounds == _screenPosition && (int)_drawSurface.w == bounds.width()) {
		return;
	}

	moveTo(bounds);
	_drawSurface.create(bounds.width(), bounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
}

void ChessCodePuzzle::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);	// always empty in the shipped record

	_unknown21 = stream.readUint16LE();	// 863, purpose unknown
	stream.skip(3);						// always zero

	_keySound.readData(stream);
	_sendSound.readData(stream);
	_replySound.readData(stream);

	readRect(stream, _logDest);
	readRect(stream, _inputDest);

	_numSpinnerFrames = stream.readUint16LE();
	_moveLength = stream.readUint16LE();
	_spinnerInterval = stream.readUint32LE();
	_replyDelay = stream.readUint32LE();
	readFilename(stream, _spinnerChars);	// not a filename, the glyphs "/-\|"

	_fontID = stream.readUint32LE();
	_unknownF8 = stream.readUint16LE();		// 1015, purpose unknown

	readFilename(stream, _selfPrefixKey);
	readFilename(stream, _otherPrefixKey);
	readFilename(stream, _introKey);

	uint16 num = stream.readUint16LE();
	readFilenameArray(stream, _alphabetKeys, num);

	num = stream.readUint16LE();
	readFilenameArray(stream, _unknownKeys, num);

	_letterSound.readData(stream);

	num = stream.readUint16LE();
	readFilenameArray(stream, _gridKeys, num);

	_maxLetters = stream.readUint32LE();

	num = stream.readUint16LE();
	readFilenameArray(stream, _endWordKeys, num);

	num = stream.readUint16LE();
	_exchanges.resize(num);
	for (uint i = 0; i < num; ++i) {
		readFilename(stream, _exchanges[i].wordKey);
		readFilename(stream, _exchanges[i].responseKey);
		_exchanges[i].outFlag = stream.readSint16LE();
	}
}

Common::String ChessCodePuzzle::text(const Common::String &key) const {
	if (key.empty()) {
		return Common::String();
	}

	// Every string this record names lives in the AUTOTEXT table, not CONVO.
	return resolveSubtitleText(key, key, "AUTOTEXT");
}

// Joins a speaker prefix to the message it introduces. The prefixes end in
// "Name: " in the data but CVTX trims trailing whitespace off every string it
// reads, so the separator has to be put back.
Common::String ChessCodePuzzle::speech(const Common::String &prefixKey, const Common::String &body) const {
	Common::String line = text(prefixKey);
	if (!line.empty() && line.lastChar() != ' ' && !body.empty()) {
		line += ' ';
	}

	return line + body;
}

void ChessCodePuzzle::appendLog(const Common::String &line) {
	_log.push_back(line);
	_needsTextRedraw = true;
}

SoundDescription ChessCodePuzzle::playSoundBlock(const RandomSoundBlock &block) {
	SoundDescription desc;
	if (block.names.empty()) {
		return desc;
	}

	uint idx = block.names.size() == 1 ? 0 : g_nancy->_randomSource->getRandomNumber(block.names.size() - 1);
	const Common::String &name = block.names[idx];
	if (name.empty() || name == "NO SOUND") {
		return desc;
	}

	desc.name = name;
	desc.channelID = block.channel;
	desc.numLoops = block.numLoops > 0 ? block.numLoops : 1;
	desc.volume = block.volume;

	g_nancy->_sound->loadSound(desc);
	g_nancy->_sound->playSound(desc);
	return desc;
}

void ChessCodePuzzle::speakLetter(char letter) {
	// The block's single "name" is the prefix "Chess_"; the game ships one voice
	// clip per grid symbol, Chess_A .. Chess_Z and Chess_1 .. Chess_6 - exactly
	// the alphabet the eight grid rows are built from, which is what identifies
	// this block as the read-back of the decoded letter rather than of a keypress.
	if (_letterSound.names.empty() || !Common::isAlnum(letter)) {
		return;
	}

	RandomSoundBlock block = _letterSound;
	block.names[0] += letter;
	playSoundBlock(block);
}

char ChessCodePuzzle::decodeMove(const Common::String &move) const {
	// alphabet 0 is the piece letters, 1 the files, 2 the ranks. Only the last
	// two take part in the decode.
	if (_alphabetKeys.size() < 3 || move.size() < 3 || _gridKeys.empty()) {
		return 0;
	}

	Common::String files = text(_alphabetKeys[1]);
	Common::String ranks = text(_alphabetKeys[2]);

	// size_t, not uint: Common::String::npos truncates on a 64-bit build, which
	// made the npos test below dead. Harmless in practice - handleInput() cannot
	// produce an out-of-alphabet character and the row-size guard catches it
	// anyway - but the comparison should actually mean something.
	size_t fileIdx = files.findFirstOf(move[1]);
	size_t rankIdx = ranks.findFirstOf(move[2]);
	if (fileIdx == Common::String::npos || rankIdx == Common::String::npos ||
			rankIdx >= _gridKeys.size()) {
		return 0;
	}

	Common::String row = text(_gridKeys[rankIdx]);
	if (fileIdx >= row.size()) {
		return 0;
	}

	return row[fileIdx];
}

Common::String ChessCodePuzzle::randomMove() const {
	Common::String move;
	for (uint i = 0; i < _alphabetKeys.size(); ++i) {
		Common::String alphabet = text(_alphabetKeys[i]);
		if (alphabet.empty()) {
			continue;
		}

		move += alphabet[g_nancy->_randomSource->getRandomNumber(alphabet.size() - 1)];
	}

	return move;
}

void ChessCodePuzzle::commitMove() {
	appendLog(speech(_selfPrefixKey, _pendingMove));
	playSoundBlock(_sendSound);

	char letter = decodeMove(_pendingMove);
	if (letter) {
		_word += letter;
		speakLetter(letter);
	}

	_pendingMove.clear();

	Time now = g_nancy->getTotalPlayTime();
	_awaitingReply = true;
	_replyTime = now + _replyDelay;
	_nextSpinnerTime = now + _spinnerInterval;
	_spinnerFrame = 0;
}

void ChessCodePuzzle::deliverReply() {
	_awaitingReply = false;
	playSoundBlock(_replySound);

	Common::String word = _word;
	word.toUppercase();

	// GUESSED: the one-entry list holding ChessIM06 (" DISCONNECT") is read as the
	// set of words that hang up. Every chess log the game ships ends on a
	// DISCONNECTED line, and the grid alphabet can spell the word, so a typed
	// DISCONNECT ending the session is the reading that uses the field for
	// something. Nothing in the record proves it.
	for (uint i = 0; i < _endWordKeys.size(); ++i) {
		Common::String endWord = text(_endWordKeys[i]);
		endWord.trim();
		endWord.toUppercase();

		if (!endWord.empty() && endWord == word) {
			appendLog(speech(_otherPrefixKey, text(_endWordKeys[i])));
			_word.clear();
			_finished = true;
			return;
		}
	}

	for (uint i = 0; i < _exchanges.size(); ++i) {
		Common::String candidate = text(_exchanges[i].wordKey);
		candidate.toUppercase();

		if (candidate.empty() || candidate != word) {
			continue;
		}

		// The bare int16 on each entry is the generic event flag the answer
		// raises. All three labels used here - 1011, 1014, 1015 - fall in the
		// 1010-1040 band the engine clears on every scene change, so they are
		// scene-local signals rather than story state, and scene 3010 carries a
		// plain Event Flags record whose only dependency is generic flag 1014
		// (the label on the two winning words) and whose only effect is to set
		// the persistent flag 2503. That companion record is what turns the
		// answer into progress; this one only has to raise the signal.
		if (_exchanges[i].outFlag >= 0) {
			NancySceneState.setEventFlag(_exchanges[i].outFlag, g_nancy->_true);
		}

		appendLog(speech(_otherPrefixKey, text(_exchanges[i].responseKey)));
		_word.clear();
		return;
	}

	// Nothing matched. Scaramuccia answers with a move of her own until the word
	// runs past the length limit, at which point she gives up on it.
	if (_maxLetters && _word.size() >= _maxLetters) {
		appendLog(speech(_otherPrefixKey, _unknownKeys.empty() ? Common::String() : text(_unknownKeys[0])));
		_word.clear();
		return;
	}

	appendLog(speech(_otherPrefixKey, randomMove()));
}

void ChessCodePuzzle::redraw() {
	_needsTextRedraw = false;
	syncBounds();
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	UI::Viewport &viewport = NancySceneState.getViewport();
	Common::Rect logBounds = convertToLocal(viewport.convertViewportToScreen(_logDest));
	Common::Rect inputBounds = convertToLocal(viewport.convertViewportToScreen(_inputDest));

	// The transcript is laid out in one pass so that the face and colour a line's
	// prefix sets carry into the message body, which is how the data is authored:
	// ChessIM07/ChessIM08 hold the "<n><b4><cN>Name: " prefixes - <b4> is FONT 4,
	// UIFont, at bold weight - and the message strings themselves are untagged.
	Common::String transcript;
	for (uint i = 0; i < _log.size(); ++i) {
		transcript += _log[i];
	}

	// Every prefix opens with a <n>, so drop the very first one or the transcript
	// starts one blank line down.
	if (transcript.hasPrefix("<n>")) {
		transcript.erase(0, 3);
	}

	if (logBounds.width() > 0 && logBounds.height() > 0) {
		// Draw the whole transcript into a tall scratch surface, then show its
		// bottom - the terminal always sits at the newest line.
		const int surfaceHeight = MAX<int>(logBounds.height() * 8, 512);
		if ((int)_logSurface.w != logBounds.width() || (int)_logSurface.h != surfaceHeight) {
			_logSurface.create(logBounds.width(), surfaceHeight, g_nancy->_graphics->getInputPixelFormat());
		}

		_logSurface.clear(g_nancy->_graphics->getTransColor());

		int height = drawStyledTextToSurface(_logSurface, transcript, _fontID,
			kTerminalTextColour, 0, 0, logBounds.width(), Graphics::kTextAlignLeft);

		// Once the transcript is longer than the scratch surface, retire the
		// oldest lines rather than letting it draw off the top.
		while (height > surfaceHeight && _log.size() > 1) {
			_log.remove_at(0);
			redraw();
			return;
		}

		// The transcript grows UPWARD from the bottom of the log box, the way a
		// terminal scrolls: a short log sits low in the window rather than at its
		// top. Measured on the retail recording, which pins both halves of this -
		// in `chess_mid` the log is 12 lines and in `chess_end` it is 14, and the
		// last line's cell ends at the same y (the box's own bottom, screen 354)
		// in both, so the first line starts 12 and 14 line heights above it.
		int top = MAX(0, height - logBounds.height());
		int dstTop = logBounds.top + MAX(0, logBounds.height() - height);
		Common::Rect src(0, top, _logSurface.w, MIN<int>(surfaceHeight, top + logBounds.height()));
		_drawSurface.blitFrom(_logSurface, src, Common::Point(logBounds.left, dstTop));
	}

	Common::String inputLine;
	if (_finished) {
		inputLine.clear();
	} else if (_awaitingReply && _pendingMove.empty()) {
		if (!_spinnerChars.empty()) {
			inputLine += _spinnerChars[_spinnerFrame % _spinnerChars.size()];
		}
	} else {
		// ChessIM01 is "? ? ?": one slot per character of a move, which is what
		// makes it the compose-line placeholder as well as the reply Scaramuccia
		// gives to a word she does not know.
		inputLine = _unknownKeys.empty() ? Common::String() : text(_unknownKeys[0]);
		if (inputLine.empty()) {
			for (uint i = 0; i < _moveLength; ++i) {
				inputLine += (i ? " ?" : "?");
			}
		}

		uint slot = 0;
		for (uint i = 0; i < inputLine.size() && slot < _pendingMove.size(); ++i) {
			if (inputLine[i] == '?') {
				inputLine.setChar(_pendingMove[slot++], i);
			}
		}
	}

	if (!inputLine.empty() && inputBounds.width() > 0) {
		drawStyledTextToSurface(_drawSurface, inputLine, _fontID,
			kTerminalTextColour,
			inputBounds.left, inputBounds.top, inputBounds.width(), Graphics::kTextAlignLeft);
	}

	_needsRedraw = true;
}

void ChessCodePuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		g_nancy->_input->setVKEnabled(true);
		appendLog(speech(_otherPrefixKey, text(_introKey)));
		_state = kRun;
		// fall through
	case kRun: {
		Time now = g_nancy->getTotalPlayTime();

		if (_awaitingReply) {
			if (_spinnerInterval && now >= _nextSpinnerTime) {
				++_spinnerFrame;
				_nextSpinnerTime = now + _spinnerInterval;
				_needsTextRedraw = true;
			}

			if (now >= _replyTime) {
				deliverReply();
			}
		}

		if (!_awaitingReply && !_finished && _moveLength && _pendingMove.size() >= _moveLength) {
			commitMove();
		}

		if (_needsTextRedraw) {
			redraw();
		}

		break;
	}
	case kActionTrigger:
		// The record carries no scene change of its own - the player leaves the
		// terminal through the scene's own exit hotspots - so this is only
		// reached if something else stops the record.
		g_nancy->_input->setVKEnabled(false);
		finishExecution();
		break;
	}
}

void ChessCodePuzzle::handleInput(NancyInput &input) {
	if (_finished) {
		return;
	}

	for (uint i = 0; i < input.otherKbdInput.size(); ++i) {
		const Common::KeyState &key = input.otherKbdInput[i];

		if (key.keycode == Common::KEYCODE_BACKSPACE) {
			if (!_pendingMove.empty()) {
				_pendingMove.deleteLastChar();
				_needsTextRedraw = true;
			}

			continue;
		}

		if (_pendingMove.size() >= _moveLength || _alphabetKeys.empty()) {
			continue;
		}

		// Each character of a move comes from its own alphabet: piece, then file,
		// then rank. Anything else is simply not accepted.
		const uint slot = MIN<uint>(_pendingMove.size(), _alphabetKeys.size() - 1);
		Common::String alphabet = text(_alphabetKeys[slot]);
		alphabet.toUppercase();

		const char typed = Common::isLower(key.ascii) ? (char)(key.ascii - 'a' + 'A') : (char)key.ascii;
		if (!typed || alphabet.findFirstOf(typed) == Common::String::npos) {
			continue;
		}

		_pendingMove += typed;
		playSoundBlock(_keySound);
		_needsTextRedraw = true;

		// A finished move is sent from execute(), not from here, so that keys
		// typed while the last reply is still coming in are composed rather than
		// thrown away.
	}
}

} // End of namespace Action
} // End of namespace Nancy
