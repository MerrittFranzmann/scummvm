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

#include "common/config-manager.h"
#include "common/queue.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/util.h"
#include "engines/nancy/input.h"
#include "engines/nancy/cursor.h"

#include "engines/nancy/action/puzzle/sentrypuzzle.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

// How long the alarm is held before the caught flag goes up. The record has
// no duration for it.
static const uint32 kCaughtHoldTime = 1200;

// How fast the alarm frames alternate while Nancy is being caught
static const uint32 kAlarmBlinkTime = 150;

// The sentry speed is assumed to be in pixels per 1/60s tick, which keeps
// Nancy comfortably faster than the sentries.
static const double kSentrySpeedToPixelsPerMs = 60.0 / 1000.0;

// Flags from here up only mark that Nancy reached the cell inside a doorway;
// the flags that leave the room are below this.
static const int16 kFirstScratchFlag = 1040;

// -- Type 168, the doorway hand-off --

void SentryPuzzleExit::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _levelName);
	readRect(stream, _cell);
	_unknown = stream.readSint32LE();
}

void SentryPuzzleExit::execute() {
	SentryPuzzleData *data = (SentryPuzzleData *)NancySceneState.getPuzzleData(SentryPuzzleData::getTag());
	if (data) {
		data->pendingLevel = _levelName;
		data->pendingCol = _cell.left / 32;
		data->pendingRow = _cell.top / 32;
	}

	_isDone = true;
}

// -- Type 210, the playfield --

void SentryPuzzle::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _levelName);
	readFilename(stream, _playerImageName);
	readFilename(stream, _maskImageName);

	_caughtFlag = stream.readUint16LE();
	readRect(stream, _startCell);
	_unknown = stream.readSint32LE();
	_playerSpeed = stream.readSint32LE();
	_cellSize = stream.readSint32LE();

	readRectArray(stream, _playerFrames, stream.readUint16LE());

	const uint16 numSentries = stream.readUint16LE();
	_sentries.resize(numSentries);
	for (uint i = 0; i < numSentries; ++i) {
		Sentry &sentry = _sentries[i];
		readFilename(stream, sentry.imageName);
		readRectArray(stream, sentry.facingFrames, stream.readUint16LE());
		readRectArray(stream, sentry.alarmFrames, stream.readUint16LE());

		// Two beam styles, normal and alarmed. Each is a 4x1 slice and a 4x4 fading
		// tip in the sentry sheet.
		sentry.beamUnknown0 = stream.readUint32LE();
		readRect(stream, sentry.beamLine);
		readRect(stream, sentry.beamTip);
		sentry.beamUnknown1 = stream.readUint32LE();
		readRect(stream, sentry.alarmBeamLine);
		readRect(stream, sentry.alarmBeamTip);
		sentry.unknownByte = stream.readByte();

		readRectArray(stream, sentry.waypoints, stream.readUint16LE());

		sentry.detectRadius = stream.readSint32LE();
		sentry.speed = stream.readDoubleLE();
		sentry.beamLength = stream.readSint32LE();
	}

	const uint16 numTriggers = stream.readUint16LE();
	_triggers.resize(numTriggers);
	for (uint i = 0; i < numTriggers; ++i) {
		Trigger &trigger = _triggers[i];
		trigger.kind = stream.readUint32LE();
		readRect(stream, trigger.area);
		stream.skip(44);								// unused
		trigger.unknownFloat = stream.readFloatLE();
		trigger.unknownByte = stream.readByte();
		trigger.condition.label = stream.readSint16LE();
		trigger.condition.flag = stream.readByte();
		stream.skip(6);									// unused

		if (trigger.kind == 11) {
			trigger.raise.label = stream.readSint16LE();
			trigger.raise.flag = stream.readByte();
		}
	}

	stream.skip(2);										// empty trailing array
}

SentryPuzzleData *SentryPuzzle::getState() {
	return (SentryPuzzleData *)NancySceneState.getPuzzleData(SentryPuzzleData::getTag());
}

void SentryPuzzle::init() {
	Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	g_nancy->_resource->loadImage(_playerImageName, _playerImage);
	_playerImage.setTransparentColor(_drawSurface.getTransparentColor());
	// The atlas carries one walking cycle facing down. Rotate those frames
	// by exact quarter turns so feet and shoulders follow the movement.
	for (uint direction = 0; direction < 4; ++direction) {
		_playerFacingFrames[direction].resize(_playerFrames.size());
		for (uint i = 0; i < _playerFrames.size(); ++i) {
			const Common::Rect &src = _playerFrames[i];
			const bool horizontal = direction == kLeft || direction == kRight;
			Graphics::ManagedSurface &dst = _playerFacingFrames[direction][i];
			dst.create(horizontal ? src.height() : src.width(),
				horizontal ? src.width() : src.height(), _playerImage.format);
			dst.setTransparentColor(_drawSurface.getTransparentColor());
			for (int y = 0; y < src.height(); ++y) {
				for (int x = 0; x < src.width(); ++x) {
					int tx = x, ty = y;
					if (direction == kUp) { tx = src.width() - 1 - x; ty = src.height() - 1 - y; }
					else if (direction == kRight) { tx = y; ty = src.width() - 1 - x; }
					else if (direction == kLeft) { tx = src.height() - 1 - y; ty = x; }
					dst.setPixel(tx, ty, _playerImage.getPixel(src.left + x, src.top + y));
				}
			}
		}
	}

	if (!_sentries.empty()) {
		g_nancy->_resource->loadImage(Common::Path(_sentries[0].imageName), _sentryImage);
		_sentryImage.setTransparentColor(_drawSurface.getTransparentColor());
	}

	g_nancy->_resource->loadImage(_maskImageName, _mask);

	if (_cellSize <= 0) {
		_cellSize = 32;
	}

	_numCols = _mask.w / _cellSize;
	_numRows = _mask.h / _cellSize;

	// The masks are black and white and aligned to the cell grid, so sampling a
	// cell's centre decides the whole cell. Cell (0,0) is always a wall.
	if (_numCols > 0 && _numRows > 0) {
		_wallColor = _mask.getPixel(_cellSize / 2, _cellSize / 2);
	}

	// Start on the cell named by a type 168 record in the previous scene, else
	// on this record's start rect, else on any open cell.
	int col = _startCell.left / _cellSize;
	int row = _startCell.top / _cellSize;

	SentryPuzzleData *data = getState();
	if (data && !data->pendingLevel.empty() && data->pendingLevel.equalsIgnoreCase(_levelName)) {
		col = data->pendingCol;
		row = data->pendingRow;
		data->pendingLevel.clear();
	}

	if (!isOpen(col, row)) {
		// Some start rects point at a wall; fall back to any open cell.
		for (int r = 0; r < _numRows && !isOpen(col, row); ++r) {
			for (int c = 0; c < _numCols; ++c) {
				if (isOpen(c, r)) {
					col = c;
					row = r;
					break;
				}
			}
		}
	}

	const Common::Point start = cellCentre(col, row);
	_playerX = start.x;
	_playerY = start.y;
	_playerFrame = _playerFrames.empty() ? 0 : _playerFrames.size() - 1;

	// Nancy may start inside an exit trigger, so a trigger containing the spawn
	// only arms once she has stepped outside it.
	for (auto &trigger : _triggers)
		trigger.awaitingDeparture = trigger.area.contains(start);

	for (uint i = 0; i < _sentries.size(); ++i) {
		Sentry &sentry = _sentries[i];
		if (sentry.waypoints.empty()) {
			continue;
		}

		sentry.x = sentry.waypoints[0].left + _cellSize / 2.0;
		sentry.y = sentry.waypoints[0].top + _cellSize / 2.0;
		sentry.leg = 0;
		sentry.legDir = 1;
	}

	_lastStepTime = g_nancy->getTotalPlayTime();

	redraw();
}

bool SentryPuzzle::isOpen(int col, int row) const {
	if (col < 0 || row < 0 || col >= _numCols || row >= _numRows) {
		return false;
	}

	return _mask.getPixel(col * _cellSize + _cellSize / 2, row * _cellSize + _cellSize / 2) != _wallColor;
}

// A trigger is live when it has no condition, or when its condition flag holds.
bool SentryPuzzle::triggerIsLive(const Trigger &trigger) const {
	return trigger.condition.label < 0 ||
		NancySceneState.getEventFlag(trigger.condition.label, trigger.condition.flag);
}

// Corridor, and not currently behind a laser barrier. Only the mask matters to a
// sentry's beam, so beamRect() uses isOpen() and only Nancy uses this.
bool SentryPuzzle::isWalkable(int col, int row) const {
	if (!isOpen(col, row)) {
		return false;
	}

	const Common::Point centre = cellCentre(col, row);
	for (uint i = 0; i < _triggers.size(); ++i) {
		if (_triggers[i].kind == 20 && triggerIsLive(_triggers[i]) &&
				_triggers[i].area.contains(centre)) {
			return false;
		}
	}

	return true;
}

Common::Point SentryPuzzle::cellCentre(int col, int row) const {
	return Common::Point(col * _cellSize + _cellSize / 2, row * _cellSize + _cellSize / 2);
}

// Breadth-first through the open cells. The corridors are one cell wide, so the
// shortest path is also the only sensible one.
bool SentryPuzzle::findPath(const Common::Point &from, const Common::Point &to) {
	_path.clear();

	if (from == to || !isWalkable(to.x, to.y) || !isWalkable(from.x, from.y)) {
		return false;
	}

	Common::Array<int> cameFrom;
	cameFrom.resize(_numCols * _numRows);
	for (uint i = 0; i < cameFrom.size(); ++i) {
		cameFrom[i] = -2;
	}

	const int startIdx = from.y * _numCols + from.x;
	const int goalIdx = to.y * _numCols + to.x;
	cameFrom[startIdx] = -1;

	Common::Queue<int> queue;
	queue.push(startIdx);

	static const int dx[4] = { 0, 1, 0, -1 };
	static const int dy[4] = { -1, 0, 1, 0 };

	while (!queue.empty()) {
		const int cur = queue.pop();
		if (cur == goalIdx) {
			break;
		}

		const int cx = cur % _numCols;
		const int cy = cur / _numCols;

		for (uint d = 0; d < 4; ++d) {
			const int nx = cx + dx[d];
			const int ny = cy + dy[d];
			if (!isWalkable(nx, ny)) {
				continue;
			}

			const int next = ny * _numCols + nx;
			if (cameFrom[next] != -2) {
				continue;
			}

			cameFrom[next] = cur;
			queue.push(next);
		}
	}

	if (cameFrom[goalIdx] == -2) {
		return false;
	}

	for (int cur = goalIdx; cur != startIdx; cur = cameFrom[cur]) {
		_path.insert_at(0, Common::Point(cur % _numCols, cur / _numCols));
	}

	return true;
}

void SentryPuzzle::stepPlayer(uint32 deltaMs) {
	if (_path.empty()) {
		if (!_playerFrames.empty()) {
			_playerFrame = _playerFrames.size() - 1;
		}

		return;
	}

	// Nancy's speed is assumed to be in pixels per second
	double remaining = (_playerSpeed * (double)deltaMs) / 1000.0;

	while (remaining > 0.0 && !_path.empty()) {
		const Common::Point target = cellCentre(_path[0].x, _path[0].y);
		const double dx = target.x - _playerX;
		const double dy = target.y - _playerY;
		const double dist = sqrt(dx * dx + dy * dy);
		if (dist > 0.001) {
			_playerDirection = fabs(dx) > fabs(dy) ? (dx > 0 ? kRight : kLeft) :
				(dy > 0 ? kDown : kUp);
		}

		if (dist <= remaining || dist < 0.001) {
			_playerX = target.x;
			_playerY = target.y;
			_walkedSinceFrame += dist;
			remaining -= dist;
			_path.remove_at(0);
		} else {
			_playerX += dx * remaining / dist;
			_playerY += dy * remaining / dist;
			_walkedSinceFrame += remaining;
			remaining = 0.0;
		}
	}

	// The last frame is the standing pose and the rest are the walk cycle.
	// The cycle is stepped by distance walked so her feet keep in step.
	if (_playerFrames.size() > 1) {
		const double perFrame = _cellSize / (double)(_playerFrames.size() - 1);
		while (perFrame > 0.0 && _walkedSinceFrame >= perFrame) {
			_walkedSinceFrame -= perFrame;
			_playerFrame = (_playerFrame + 1) % (_playerFrames.size() - 1);
		}
	}
}

void SentryPuzzle::stepSentry(Sentry &sentry, double deltaMs) const {
	if (sentry.waypoints.size() < 2) {
		return;
	}

	double remaining = sentry.speed * kSentrySpeedToPixelsPerMs * deltaMs;

	// The waypoints are a there-and-back patrol, not a loop
	for (uint guard = 0; remaining > 0.0 && guard < 64; ++guard) {
		int nextLeg = sentry.leg + sentry.legDir;
		if (nextLeg < 0 || nextLeg >= (int)sentry.waypoints.size()) {
			sentry.legDir = -sentry.legDir;
			nextLeg = sentry.leg + sentry.legDir;
			if (nextLeg < 0 || nextLeg >= (int)sentry.waypoints.size()) {
				break;
			}
		}

		const double tx = sentry.waypoints[nextLeg].left + _cellSize / 2.0;
		const double ty = sentry.waypoints[nextLeg].top + _cellSize / 2.0;
		const double dx = tx - sentry.x;
		const double dy = ty - sentry.y;
		const double dist = sqrt(dx * dx + dy * dy);

		if (dist < 0.001) {
			sentry.leg = nextLeg;
			continue;
		}

		if (ABS(dx) > ABS(dy)) {
			sentry.facing = dx > 0 ? kRight : kLeft;
		} else {
			sentry.facing = dy > 0 ? kDown : kUp;
		}

		if (dist <= remaining) {
			sentry.x = tx;
			sentry.y = ty;
			sentry.leg = nextLeg;
			remaining -= dist;
		} else {
			sentry.x += dx * remaining / dist;
			sentry.y += dy * remaining / dist;
			remaining = 0.0;
		}
	}
}

void SentryPuzzle::stepSentries(uint32 deltaMs) {
	for (uint i = 0; i < _sentries.size(); ++i) {
		stepSentry(_sentries[i], deltaMs);
	}
}

// The beam runs from the sentry's centre in its facing direction and is
// stopped by the first wall. Its width is the width of the beam sprite.
Common::Rect SentryPuzzle::beamRect(const Sentry &sentry) const {
	const int halfWidth = MAX<int>(1, sentry.beamLine.width() / 2);
	const int cx = (int)sentry.x;
	const int cy = (int)sentry.y;

	int reach = 0;
	while (reach < sentry.beamLength) {
		const int step = MIN<int>(4, sentry.beamLength - reach);
		int px = cx;
		int py = cy;
		switch (sentry.facing) {
		case kUp:		py -= reach + step;	break;
		case kRight:	px += reach + step;	break;
		case kDown:		py += reach + step;	break;
		default:		px -= reach + step;	break;
		}

		if (!isOpen(px / _cellSize, py / _cellSize)) {
			break;
		}

		reach += step;
	}

	switch (sentry.facing) {
	case kUp:		return Common::Rect(cx - halfWidth, cy - reach, cx + halfWidth, cy);
	case kRight:	return Common::Rect(cx, cy - halfWidth, cx + reach, cy + halfWidth);
	case kDown:		return Common::Rect(cx - halfWidth, cy, cx + halfWidth, cy + reach);
	default:		return Common::Rect(cx - reach, cy - halfWidth, cx, cy + halfWidth);
	}
}

// The two tests that make up "caught", written against a bare sentry pose
bool SentryPuzzle::caughtAt(double sentryX, double sentryY, int32 detectRadius,
		const Common::Rect &beam, double playerX, double playerY) const {
	const double dx = playerX - sentryX;
	const double dy = playerY - sentryY;

	// Walking into a sentry is caught regardless of where it is looking
	if (dx * dx + dy * dy <= (double)detectRadius * detectRadius) {
		return true;
	}

	if (_playerFrames.empty()) {
		return false;
	}

	const bool horizontal = _playerDirection == kLeft || _playerDirection == kRight;
	const int halfW = (horizontal ? _playerFrames[0].height() : _playerFrames[0].width()) / 2;
	const int halfH = (horizontal ? _playerFrames[0].width() : _playerFrames[0].height()) / 2;
	const Common::Rect player((int)playerX - halfW, (int)playerY - halfH,
		(int)playerX + halfW, (int)playerY + halfH);

	return beam.intersects(player);
}

bool SentryPuzzle::sentrySeesPlayer(const Sentry &sentry) const {
	return caughtAt(sentry.x, sentry.y, sentry.detectRadius, beamRect(sentry), _playerX, _playerY);
}

void SentryPuzzle::checkTriggers() {
	const Common::Point centre((int)_playerX, (int)_playerY);

	for (uint i = 0; i < _triggers.size(); ++i) {
		Trigger &trigger = _triggers[i];
		if (trigger.awaitingDeparture) {
			trigger.awaitingDeparture = trigger.area.contains(centre);
			continue;
		}

		// kind 20 raises no flag; it is handled as a wall in isWalkable().
		if (trigger.kind != 11 || trigger.raise.label < 0) {
			continue;
		}

		if (!trigger.area.contains(centre)) {
			continue;
		}

		if (!triggerIsLive(trigger)) {
			continue;
		}

		NancySceneState.setEventFlag(trigger.raise.label, trigger.raise.flag);

		// The flags below the scratch range have a sibling Scene Change record
		// waiting on them, so once one goes up the room is on its way out.
		if (trigger.raise.label < kFirstScratchFlag) {

			_path.clear();
			_playState = kLeaving;
			return;
		}
	}
}

void SentryPuzzle::drawBeam(const Sentry &sentry) {
	const Common::Rect &line = sentry.alarmed ? sentry.alarmBeamLine : sentry.beamLine;
	const Common::Rect &tip = sentry.alarmed ? sentry.alarmBeamTip : sentry.beamTip;
	if (line.isEmpty() || _sentryImage.empty()) {
		return;
	}

	const Common::Rect beam = beamRect(sentry);
	const bool vertical = sentry.facing == kUp || sentry.facing == kDown;
	const int length = vertical ? beam.height() : beam.width();
	if (length <= 0) {
		return;
	}

	const uint32 trans = _sentryImage.getTransparentColor();
	const int tipStart = length - tip.height();

	// The two beam sprites are drawn for a beam pointing down: the 4x1 is one
	// slice across the beam's width and the 4x4 is the fade at its far end.
	// Horizontal beams reuse the same pixels transposed.
	for (int along = 0; along < length; ++along) {
		const bool inTip = tip.height() > 0 && along >= tipStart;
		const Common::Rect &src = inTip ? tip : line;
		const int srcRow = src.top + (inTip ? MIN<int>(src.height() - 1, along - tipStart) : 0);

		for (int across = 0; across < src.width(); ++across) {
			const uint32 color = _sentryImage.getPixel(src.left + across, srcRow);
			if (color == trans) {
				continue;
			}

			int px, py;
			if (vertical) {
				px = beam.left + across;
				py = sentry.facing == kDown ? beam.top + along : beam.bottom - 1 - along;
			} else {
				px = sentry.facing == kRight ? beam.left + along : beam.right - 1 - along;
				py = beam.top + across;
			}

			if (px >= 0 && py >= 0 && px < (int)_drawSurface.w && py < (int)_drawSurface.h) {
				_drawSurface.setPixel(px, py, color);
			}
		}
	}
}

void SentryPuzzle::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	for (uint i = 0; i < _sentries.size(); ++i) {
		const Sentry &sentry = _sentries[i];
		drawBeam(sentry);

		const Common::Array<Common::Rect> &frames = sentry.alarmed ? sentry.alarmFrames : sentry.facingFrames;
		if (frames.empty() || _sentryImage.empty()) {
			continue;
		}

		const uint which = sentry.alarmed ? sentry.alarmFrame : (uint)sentry.facing;
		const Common::Rect &src = frames[MIN<uint>(which, frames.size() - 1)];
		_drawSurface.blitFrom(_sentryImage, src,
			Common::Point((int)sentry.x - src.width() / 2, (int)sentry.y - src.height() / 2));
	}

	if (!_playerFrames.empty() && !_playerImage.empty()) {
		const Graphics::ManagedSurface &frame = _playerFacingFrames[_playerDirection][
			MIN<uint>(_playerFrame, _playerFrames.size() - 1)];
		_drawSurface.blitFrom(frame, Common::Point((int)_playerX - frame.w / 2,
			(int)_playerY - frame.h / 2));
	}

	setNeedsRedraw(true);
}

void SentryPuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_state = kRun;
		// fall through
	case kRun: {
		const uint32 now = g_nancy->getTotalPlayTime();
		// Clamped so a stall cannot move anybody across the room in a single step
		const uint32 delta = MIN<uint32>(100, now - _lastStepTime);
		_lastStepTime = now;

		switch (_playState) {
		case kWalking: {
			stepPlayer(delta);
			stepSentries(delta);

			bool caught = false;
			for (uint i = 0; i < _sentries.size(); ++i) {
				if (sentrySeesPlayer(_sentries[i])) {
					_sentries[i].alarmed = true;
					caught = true;
				}
			}

			if (caught) {

				_playState = kCaught;
				_caughtTime = now;
				_path.clear();
			} else {
				checkTriggers();
			}

			redraw();
			break;
		}
		case kCaught: {
			// Hold the alarm for a moment, then raise the caught flag. Sibling records
			// wait on that flag and restart the room.
			for (uint i = 0; i < _sentries.size(); ++i) {
				if (_sentries[i].alarmed && !_sentries[i].alarmFrames.empty()) {
					_sentries[i].alarmFrame = ((now - _caughtTime) / kAlarmBlinkTime) %
						_sentries[i].alarmFrames.size();
				}
			}

			redraw();

			if (now - _caughtTime >= kCaughtHoldTime) {
				// The sibling records test this flag against 1.
				NancySceneState.setEventFlag((int16)_caughtFlag, 1);
				_playState = kLeaving;
			}

			break;
		}
		case kLeaving:
			// A doorway or the alarm has fired and a sibling record owns the scene
			// change from here. Keep the last frame on screen.
			break;
		}

		break;
	}
	case kActionTrigger:
		finishExecution();
		break;
	}
}

void SentryPuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _playState != kWalking) {
		return;
	}

	int dx = 0, dy = 0;
	if (input.input & NancyInput::kMoveUp) dy = -1;
	else if (input.input & NancyInput::kMoveDown) dy = 1;
	else if (input.input & NancyInput::kMoveLeft) dx = -1;
	else if (input.input & NancyInput::kMoveRight) dx = 1;
	for (uint i = 0; i < input.otherKbdInput.size(); ++i) {
		switch (input.otherKbdInput[i].keycode) {
		case Common::KEYCODE_UP: dy = -1; break;
		case Common::KEYCODE_DOWN: dy = 1; break;
		case Common::KEYCODE_LEFT: dx = -1; break;
		case Common::KEYCODE_RIGHT: dx = 1; break;
		default: continue;
		}
		input.otherKbdInput.remove_at(i--);
	}
	if (dx || dy) {
		input.input &= ~(NancyInput::kMoveUp | NancyInput::kMoveDown |
			NancyInput::kMoveLeft | NancyInput::kMoveRight);
		if (_path.empty()) {
			const Common::Point from((int)_playerX / _cellSize, (int)_playerY / _cellSize);
			findPath(from, Common::Point(from.x + dx, from.y + dy));
		}
		return;
	}

	const Common::Rect playArea(0, 0, _numCols * _cellSize, _numRows * _cellSize);
	if (!NancySceneState.getViewport().convertViewportToScreen(playArea).contains(input.mousePos)) {
		return;
	}

	// The viewport only converts rects, so go through a degenerate one.
	const Common::Rect asRect = NancySceneState.getViewport().convertScreenToViewport(
		Common::Rect(input.mousePos.x, input.mousePos.y, input.mousePos.x + 1, input.mousePos.y + 1));
	const int col = asRect.left / _cellSize;
	const int row = asRect.top / _cellSize;

	if (!isWalkable(col, row)) {
		return;
	}

	// Click to walk along a shortest path; arrow keys above move one cell.
	g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

	if (input.input & NancyInput::kLeftMouseButtonUp) {
		findPath(Common::Point((int)_playerX / _cellSize, (int)_playerY / _cellSize),
			Common::Point(col, row));
		input.eatMouseInput();
	}
}

} // End of namespace Action
} // End of namespace Nancy
