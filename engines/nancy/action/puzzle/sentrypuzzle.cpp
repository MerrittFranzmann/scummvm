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
#include "engines/nancy/trace.h"

#include "engines/nancy/action/puzzle/sentrypuzzle.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

// How long the alarm is held before the caught flag goes up and the sibling
// records restart the room. Ours - the record carries no duration for it.
static const uint32 kCaughtHoldTime = 1200;

// How fast the alarm frames alternate while Nancy is being caught. Ours.
static const uint32 kAlarmBlinkTime = 150;

// The sentry speed in the file is a bare double, 0.2 to 0.5. Read as pixels per
// 1/60s tick, so the fastest sentry covers about one cell a second and Nancy, at
// her stated 75 pixels a second, is about 2.5x quicker. Neither unit is stated
// in the data; this is the ratio that makes the maze passable.
static const double kSentrySpeedToPixelsPerMs = 60.0 / 1000.0;

// The scratch flags a doorway's inner cell raises are all 1040 and up; the flags
// that actually leave the room are all below that. See the header.
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

		// Two beam styles, 73 bytes. Each is a 4x1 slice and a 4x4 fading tip in
		// the sentry sheet; the first pair is the yellow one at atlas x 143, the
		// second the red one at x 133. The leading uint32 is zero in all 27.
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
		stream.skip(44);								// zero in all 85 triggers
		trigger.unknownFloat = stream.readFloatLE();	// -272.0 in all 85
		trigger.unknownByte = stream.readByte();		// 64 in all 85
		trigger.condition.label = stream.readSint16LE();
		trigger.condition.flag = stream.readByte();
		stream.skip(6);									// zero in all 85; a scene change would fit

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

	// The masks are pure black and white and quantised exactly to the cell grid -
	// checked on all nine, not one cell of any of them is mixed - so a single
	// sample at a cell's centre decides the whole cell. Cell (0,0) is a crate in
	// all nine, so that pixel is the wall colour and anything else is corridor.
	if (_numCols > 0 && _numRows > 0) {
		_wallColor = _mask.getPixel(_cellSize / 2, _cellSize / 2);
	}

	// Where Nancy comes in. A type 168 record in the scene we just left names
	// this level and the cell to arrive on; failing that, the record's own start
	// rect, and failing that any open cell.
	int col = _startCell.left / _cellSize;
	int row = _startCell.top / _cellSize;

	SentryPuzzleData *data = getState();
	if (data && !data->pendingLevel.empty() && data->pendingLevel.equalsIgnoreCase(_levelName)) {
		col = data->pendingCol;
		row = data->pendingRow;
		data->pendingLevel.clear();
	}

	if (!isOpen(col, row)) {
		// Six of the nine start rects point at a crate, so they are a leftover
		// authoring default rather than a real spawn. Fall back to any open cell
		// so the room is never unplayable.
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

	// Autoplay bookkeeping only: which rooms the run has already been through.
	// A caught Nancy reloads the scene, so this counts arrivals, not rooms.
	if (autoPlay()) {
		const int room = roomNumber();
		if (data && room >= 71) {
			++data->autoVisits[room - 71];

			// A caught Nancy reloads the same room, which is not a move.
			if (data->autoRoom != (int16)room) {
				data->autoPrevRoom = data->autoRoom;
				data->autoRoom = (int16)room;
			}
		}

		if (autoTrace()) {
			TraceEvent("sentry")
				.str("what", "enter")
				.str("level", _levelName)
				.num("room", room)
				.num("col", col)
				.num("row", row)
				.num("visits", (data && room >= 71) ? data->autoVisits[room - 71] : 0)
				.emit();
		}
	}

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

	// Nancy's 75 is read as pixels per second.
	double remaining = (_playerSpeed * (double)deltaMs) / 1000.0;

	while (remaining > 0.0 && !_path.empty()) {
		const Common::Point target = cellCentre(_path[0].x, _path[0].y);
		const double dx = target.x - _playerX;
		const double dy = target.y - _playerY;
		const double dist = sqrt(dx * dx + dy * dy);

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

	// The eleventh frame is listed out of atlas order - it is the leftmost cell of
	// the sheet's top row but comes last in the array - so it is taken as the
	// standing pose and 0..9 as the walk cycle. Stepping the cycle by distance
	// walked rather than by a timer keeps her feet in step at any speed.
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

	// The waypoint list is a there-and-back patrol, not a loop: the first and
	// last waypoints of all 27 paths differ, so walking off the end and
	// reversing is the only reading that keeps a sentry on its own corridor.
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

// The beam runs from the sentry's centre in its facing direction, stopped by the
// first crate. Its reach is the per-sentry constant 120 and its width the width
// of the beam sprite - both are readings, see the header.
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

// The two tests that make up "caught", written against a bare pose so that the
// autoplay planner can ask the same question about a predicted sentry position
// as the live check asks about the real one. There is exactly one copy of the
// rule, so the two cannot drift apart.
bool SentryPuzzle::caughtAt(double sentryX, double sentryY, int32 detectRadius,
		const Common::Rect &beam, double playerX, double playerY) const {
	const double dx = playerX - sentryX;
	const double dy = playerY - sentryY;

	// Walking into a sentry is caught regardless of where it happens to be
	// looking. 25/20/15 is read as that contact radius.
	if (dx * dx + dy * dy <= (double)detectRadius * detectRadius) {
		return true;
	}

	if (_playerFrames.empty()) {
		return false;
	}

	const int halfW = _playerFrames[0].width() / 2;
	const int halfH = _playerFrames[0].height() / 2;
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
		const Trigger &trigger = _triggers[i];

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

		// The scratch flags only mark that Nancy reached the cell inside a
		// doorway; the flags below them have a sibling type 15 "Scene Change"
		// waiting on them, so once one of those goes up the room is on its way
		// out and there is nothing left to drive.
		if (trigger.raise.label < kFirstScratchFlag) {
			if (autoPlay()) {
				const int room = roomNumber();
				SentryPuzzleData *data = getState();
				if (data && room >= 71 && isObjectiveTrigger(trigger)) {
					data->autoTriedObjective[room - 71] = 1;
				}

				if (autoTrace()) {
					TraceEvent("sentry")
						.str("what", "leave")
						.num("room", room)
						.num("flag", trigger.raise.label)
						.num("col", (int)_playerX / _cellSize)
						.num("row", (int)_playerY / _cellSize)
						.emit();
				}
			}

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
		const Common::Rect &src = _playerFrames[MIN<uint>(_playerFrame, _playerFrames.size() - 1)];
		_drawSurface.blitFrom(_playerImage, src,
			Common::Point((int)_playerX - src.width() / 2, (int)_playerY - src.height() / 2));
	}

	setNeedsRedraw(true);
}

// -- autoplay -----------------------------------------------------------------
//
// nancy_sentry_autoplay drives Nancy across the maze unattended; it defaults
// off and ordinary play is untouched. nancy_sentry_trace prints one line per
// decision so a run can be replayed and checked offline.
//
// WHY A HOOK AND NOT A CLICK SCRIPT. The record's own interface is click-to-
// walk (see handleInput), so in principle a driver could script it - but the
// crossing is hundreds of clicks over nine scene loads, and nancy_autoclick_
// script advances its index per firing rather than per accepted click, so one
// dropped click desynchronises everything after it. Everything the crossing
// needs is already in the record; the only thing missing was somebody to press
// the buttons.
//
// WHAT IT AIMS AT, and what that costs in assumptions.
//
//   The room graph is not a table in this file. It is read off the trigger
//   geometry: every doorway trigger sits on the board's outer edge, so the edge
//   it sits on IS the direction, and the level name ends in the room's number,
//   71..79, laid out
//
//        71 72 73
//        74 75 76      right = +1, down = +3
//        77 78 79
//
//   That is checked, not assumed: all 26 in-maze doorway triggers land on an
//   edge, all 26 name a neighbour that exists in the grid, and the one that
//   names a room off the grid is room 72's north edge - which is the way out of
//   the maze to S3805. Every trigger not on an edge is an objective: the four
//   keypads and the sapphire.
//
//   So the goal for a room is, in order: a live objective in it, else the
//   doorway to the least-visited neighbour. That covers the nine rooms without
//   a route table and stops as soon as a room has something to do in it. The
//   sapphire needs no special case at all - its cell only becomes reachable on
//   the mask once the kind-20 wall goes down, and an unreachable objective is
//   skipped, so it starts being a goal exactly when the fourth keypad is done.
//
// HOW IT WALKS. Nothing here is random: the patrols are fixed polylines at a
// fixed speed from a fixed start, so given the sentries' current state the next
// twenty seconds are exactly predictable. The planner
//
//   * copies the live sentries and steps them forward with the same
//     stepSentry() the game uses, storing a pose every quarter-cell of time;
//   * builds, for every (cell, time-quantum), which of the five actions - wait,
//     up, right, down, left - can be taken without being seen at any sample
//     along the way, using the same caughtAt() the live check uses;
//   * marks a state survivable by backward induction from the horizon, so it
//     will not walk into a corridor it cannot get out of;
//   * breadth-first searches survivable states for the goal, and plays only the
//     FIRST move of the answer, then re-plans from the real state one cell
//     later. Re-planning per cell is what makes it immune to the frame-time
//     drift that would otherwise pull a pre-computed plan out of step.
//
// Being caught is not fatal - the sibling records reload the room - so when no
// safe plan exists the planner falls back to the survivable move that gets
// closest to the goal, and failing that stands still.
//
// The one quantity that is ours rather than the record's is the horizon.

// A quantum is the time Nancy takes to cross one cell, cellSize / playerSpeed,
// 426.67 ms with the record's own 32 and 75. Forty of them is 17 seconds, which
// is comfortably longer than the widest sentry-free gap the fastest patrol
// leaves and short enough to re-plan inside one cell of walking.
static const int kAutoHorizon = 40;

// Samples per quantum. At 75 px/s a quarter quantum moves Nancy 8 px, and the
// narrowest thing she can be caught by is a 4 px beam against her own 26 px
// sprite, so nothing can be stepped over between samples.
static const int kAutoSubSteps = 4;

// How long to stand still before looking again, when the plan says to wait.
static const uint32 kAutoWaitPace = 200;

// The maze is three rooms by three.
static const int kAutoGridSide = 3;

bool SentryPuzzle::autoPlay() {
	return ConfMan.hasKey("nancy_sentry_autoplay") && ConfMan.getBool("nancy_sentry_autoplay");
}

bool SentryPuzzle::autoTrace() {
	return ConfMan.hasKey("nancy_sentry_trace") && ConfMan.getBool("nancy_sentry_trace");
}

// 71..79 out of "SentryPuz71".."SentryPuz79"; -1 for anything else, which turns
// the room graph off and leaves the in-room planner running.
int SentryPuzzle::roomNumber() const {
	if (_levelName.size() < 2) {
		return -1;
	}

	const char tens = _levelName[_levelName.size() - 2];
	const char units = _levelName[_levelName.size() - 1];
	if (tens < '0' || tens > '9' || units < '0' || units > '9') {
		return -1;
	}

	const int number = (tens - '0') * 10 + (units - '0');
	return (number >= 71 && number <= 79) ? number : -1;
}

// A trigger that takes Nancy out of the room: the scratch flags 1040 and up only
// mark that she reached the cell inside a doorway.
bool SentryPuzzle::isExitTrigger(const Trigger &trigger) const {
	return trigger.kind == 11 && trigger.raise.label >= 0 && trigger.raise.label < kFirstScratchFlag;
}

// Every on-board cell whose centre lies inside the trigger, which is the same
// test checkTriggers() makes against Nancy's centre.
void SentryPuzzle::triggerCells(const Trigger &trigger, Common::Array<Common::Point> &out) const {
	for (int row = 0; row < _numRows; ++row) {
		for (int col = 0; col < _numCols; ++col) {
			if (trigger.area.contains(cellCentre(col, row))) {
				out.push_back(Common::Point(col, row));
			}
		}
	}
}

// An exit trigger that is not on the board's outer edge. Every doorway is on an
// edge - all 26 of them, checked - so what is left is the four keypads and the
// sapphire.
bool SentryPuzzle::isObjectiveTrigger(const Trigger &trigger) const {
	if (!isExitTrigger(trigger)) {
		return false;
	}

	Common::Array<Common::Point> cells;
	triggerCells(trigger, cells);
	if (cells.empty()) {
		return false;
	}

	for (uint i = 0; i < cells.size(); ++i) {
		if (cells[i].x == 0 || cells[i].y == 0 ||
				cells[i].x == _numCols - 1 || cells[i].y == _numRows - 1) {
			return false;
		}
	}

	return true;
}

bool SentryPuzzle::autoChooseGoal(Common::Array<bool> &blocked, Common::Point &goal, bool &isObjective) const {
	const int numCells = _numCols * _numRows;
	if (numCells <= 0) {
		return false;
	}

	blocked.clear();
	blocked.resize(numCells);

	// Shut every door. They are all leaf cells - each doorway cell has exactly
	// one open neighbour, and each objective cell sits in a pocket or beside a
	// parallel corridor - so this cannot cut the room in two, and it stops the
	// planner strolling through a door it did not choose.
	for (uint i = 0; i < _triggers.size(); ++i) {
		if (!isExitTrigger(_triggers[i])) {
			continue;
		}

		Common::Array<Common::Point> cells;
		triggerCells(_triggers[i], cells);
		for (uint j = 0; j < cells.size(); ++j) {
			blocked[cells[j].y * _numCols + cells[j].x] = true;
		}
	}

	const Common::Point cur((int)_playerX / _cellSize, (int)_playerY / _cellSize);
	if (cur.x < 0 || cur.y < 0 || cur.x >= _numCols || cur.y >= _numRows) {
		return false;
	}

	const int curIdx = cur.y * _numCols + cur.x;

	// Distances with the doors shut. Nancy's own cell is always allowed: she
	// arrives standing on one.
	Common::Array<int> dist;
	dist.resize(numCells);
	for (int i = 0; i < numCells; ++i) {
		dist[i] = -1;
	}

	static const int dx[4] = { 0, 1, 0, -1 };
	static const int dy[4] = { -1, 0, 1, 0 };

	Common::Queue<int> queue;
	dist[curIdx] = 0;
	queue.push(curIdx);

	while (!queue.empty()) {
		const int at = queue.pop();
		const int atX = at % _numCols;
		const int atY = at / _numCols;

		for (uint d = 0; d < 4; ++d) {
			const int nx = atX + dx[d];
			const int ny = atY + dy[d];
			if (nx < 0 || ny < 0 || nx >= _numCols || ny >= _numRows) {
				continue;
			}

			const int next = ny * _numCols + nx;
			if (dist[next] >= 0 || blocked[next] || !isWalkable(nx, ny)) {
				continue;
			}

			dist[next] = dist[at] + 1;
			queue.push(next);
		}
	}

	const int room = roomNumber();
	const SentryPuzzleData *state = (const SentryPuzzleData *)
		NancySceneState.getPuzzleData(SentryPuzzleData::getTag());

	int bestObjDist = -1;
	Common::Point bestObj;
	int bestDoorVisits = -1, bestDoorDist = -1, bestDoorRoom = -1;
	Common::Point bestDoor;

	for (uint i = 0; i < _triggers.size(); ++i) {
		const Trigger &trigger = _triggers[i];
		if (!isExitTrigger(trigger)) {
			continue;
		}

		const bool objective = isObjectiveTrigger(trigger);

		// One attempt per objective. If Nancy has already walked into this room's
		// keypad and it is still offering itself, whatever was supposed to happen
		// did not, and walking in again will not change that - so fall through to
		// exploring instead of shuttling in and out of the same close-up forever.
		if (objective && state && room >= 71 && state->autoTriedObjective[room - 71]) {
			continue;
		}

		Common::Array<Common::Point> cells;
		triggerCells(trigger, cells);

		for (uint j = 0; j < cells.size(); ++j) {
			const Common::Point &cell = cells[j];
			if (!isWalkable(cell.x, cell.y)) {
				continue;
			}

			// A door cell is not in the distance field - it is blocked - so it is
			// one step past whichever neighbour is nearest.
			int reach = (cell == cur) ? 0 : -1;
			for (uint d = 0; d < 4 && reach != 0; ++d) {
				const int nx = cell.x + dx[d];
				const int ny = cell.y + dy[d];
				if (nx < 0 || ny < 0 || nx >= _numCols || ny >= _numRows) {
					continue;
				}

				const int at = dist[ny * _numCols + nx];
				if (at >= 0 && (reach < 0 || at + 1 < reach)) {
					reach = at + 1;
				}
			}

			// reach 0 means Nancy is standing on it, and the only exit cell she can
			// be standing on is the doorway she just arrived through. Aiming at it
			// would mean walking straight back, or - since she is already there -
			// standing still for ever waiting to arrive.
			if (reach <= 0) {
				continue;
			}

			if (objective) {
				// A keypad or the sapphire. Its condition is a story flag, so once
				// it is done it stops being a goal.
				if (!triggerIsLive(trigger)) {
					continue;
				}

				if (bestObjDist < 0 || reach < bestObjDist) {
					bestObjDist = reach;
					bestObj = cell;
				}

				continue;
			}

			if (room < 0 || !state) {
				continue;
			}

			// The edge the doorway sits on is the direction it leads.
			int roomCol = (room - 71) % kAutoGridSide;
			int roomRow = (room - 71) / kAutoGridSide;
			if (cell.y == 0) {
				--roomRow;
			} else if (cell.y == _numRows - 1) {
				++roomRow;
			} else if (cell.x == 0) {
				--roomCol;
			} else {
				++roomCol;
			}

			if (roomCol < 0 || roomRow < 0 || roomCol >= kAutoGridSide || roomRow >= kAutoGridSide) {
				// Off the grid: room 72's north edge, the way out of the maze. Never
				// an exploration target, or the run would wander out of the puzzle.
				continue;
			}

			const int nextRoom = 71 + roomRow * kAutoGridSide + roomCol;

			// Fewest visits wins, then the nearest door, then the lowest room
			// number so the walk is reproducible. Turning straight round into the
			// room Nancy just left counts as one extra visit: without that, two
			// equally-visited neighbours make her oscillate in the doorway, since
			// the one behind her is always the closest.
			int visits = state->autoVisits[nextRoom - 71];
			if (nextRoom == state->autoPrevRoom) {
				++visits;
			}

			if (bestDoorVisits < 0 || visits < bestDoorVisits ||
					(visits == bestDoorVisits && reach < bestDoorDist) ||
					(visits == bestDoorVisits && reach == bestDoorDist && nextRoom < bestDoorRoom)) {
				bestDoorVisits = visits;
				bestDoorDist = reach;
				bestDoorRoom = nextRoom;
				bestDoor = cell;
			}
		}
	}

	if (bestObjDist >= 0) {
		goal = bestObj;
		isObjective = true;
		return true;
	}

	if (bestDoorVisits >= 0) {
		goal = bestDoor;
		isObjective = false;
		return true;
	}

	return false;
}

bool SentryPuzzle::autoSeenAt(const Common::Array<AutoPose> &poses, int slice, double px, double py) const {
	const uint numSentries = _sentries.size();

	for (uint i = 0; i < numSentries; ++i) {
		const AutoPose &pose = poses[slice * numSentries + i];
		if (caughtAt(pose.x, pose.y, _sentries[i].detectRadius, pose.beam, px, py)) {
			return true;
		}
	}

	return false;
}

bool SentryPuzzle::autoEdgeSafe(const Common::Array<AutoPose> &poses, int quantum,
		const Common::Point &from, const Common::Point &to) const {
	const Common::Point a = cellCentre(from.x, from.y);
	const Common::Point b = cellCentre(to.x, to.y);

	for (int j = 0; j <= kAutoSubSteps; ++j) {
		const double f = (double)j / kAutoSubSteps;
		if (autoSeenAt(poses, quantum * kAutoSubSteps + j,
				a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f)) {
			return false;
		}
	}

	return true;
}

bool SentryPuzzle::autoPlanStep(const Common::Point &goal, const Common::Array<bool> &blocked,
		Common::Point &next) const {
	const int numCells = _numCols * _numRows;
	const Common::Point cur((int)_playerX / _cellSize, (int)_playerY / _cellSize);
	const int curIdx = cur.y * _numCols + cur.x;
	const int goalIdx = goal.y * _numCols + goal.x;

	next = cur;

	if (numCells <= 0 || _playerSpeed <= 0) {
		return false;
	}

	// Wait, up, right, down, left - the same four directions findPath() uses,
	// with standing still first so it is the tie-break.
	static const int dx[5] = { 0, 0, 1, 0, -1 };
	static const int dy[5] = { 0, -1, 0, 1, 0 };

	Common::Array<bool> allowed;
	allowed.resize(numCells);
	for (int row = 0; row < _numRows; ++row) {
		for (int col = 0; col < _numCols; ++col) {
			const int at = row * _numCols + col;
			allowed[at] = isWalkable(col, row) && (!blocked[at] || at == goalIdx || at == curIdx);
		}
	}

	if (!allowed[curIdx]) {
		return false;
	}

	// Every sentry pose the horizon covers, from the live state forward.
	const double quantumMs = (_cellSize * 1000.0) / _playerSpeed;
	const int numSlices = kAutoHorizon * kAutoSubSteps + 1;
	const uint numSentries = _sentries.size();

	Common::Array<Sentry> sim = _sentries;
	Common::Array<AutoPose> poses;
	poses.resize(numSlices * numSentries);

	for (int slice = 0; slice < numSlices; ++slice) {
		for (uint i = 0; i < numSentries; ++i) {
			AutoPose &pose = poses[slice * numSentries + i];
			pose.x = sim[i].x;
			pose.y = sim[i].y;
			pose.beam = beamRect(sim[i]);
		}

		if (slice + 1 < numSlices) {
			for (uint i = 0; i < numSentries; ++i) {
				stepSentry(sim[i], quantumMs / kAutoSubSteps);
			}
		}
	}

	// Which actions are safe for the whole of each quantum, one bit each.
	Common::Array<byte> safe;
	safe.resize(kAutoHorizon * numCells);

	for (int quantum = 0; quantum < kAutoHorizon; ++quantum) {
		for (int at = 0; at < numCells; ++at) {
			if (!allowed[at]) {
				continue;
			}

			const Common::Point from(at % _numCols, at / _numCols);
			byte bits = 0;
			for (int a = 0; a < 5; ++a) {
				const Common::Point to(from.x + dx[a], from.y + dy[a]);
				if (to.x < 0 || to.y < 0 || to.x >= _numCols || to.y >= _numRows) {
					continue;
				}

				if (!allowed[to.y * _numCols + to.x]) {
					continue;
				}

				if (autoEdgeSafe(poses, quantum, from, to)) {
					bits |= (1 << a);
				}
			}

			safe[quantum * numCells + at] = bits;
		}
	}

	// Backward induction: a state is survivable when some safe action from it
	// leads to a survivable state. At the horizon everything standing is.
	Common::Array<byte> survivable;
	survivable.resize((kAutoHorizon + 1) * numCells);
	for (int at = 0; at < numCells; ++at) {
		survivable[kAutoHorizon * numCells + at] = allowed[at] ? 1 : 0;
	}

	for (int quantum = kAutoHorizon - 1; quantum >= 0; --quantum) {
		for (int at = 0; at < numCells; ++at) {
			if (!allowed[at]) {
				continue;
			}

			const byte bits = safe[quantum * numCells + at];
			for (int a = 0; a < 5; ++a) {
				if (!(bits & (1 << a))) {
					continue;
				}

				const int to = (at / _numCols + dy[a]) * _numCols + (at % _numCols + dx[a]);
				if (survivable[(quantum + 1) * numCells + to]) {
					survivable[quantum * numCells + at] = 1;
					break;
				}
			}
		}
	}

	// Breadth-first over survivable (cell, quantum) states. Only the first move
	// of the answer is ever played.
	Common::Array<int8> firstMove;
	firstMove.resize((kAutoHorizon + 1) * numCells);
	for (uint i = 0; i < firstMove.size(); ++i) {
		firstMove[i] = -1;
	}

	int chosen = -1;

	if (survivable[curIdx]) {
		Common::Queue<int> queue;
		firstMove[curIdx] = 5;			// the root: no move made yet
		queue.push(curIdx);

		while (!queue.empty()) {
			const int state = queue.pop();
			const int quantum = state / numCells;
			const int at = state % numCells;

			if (at == goalIdx) {
				chosen = firstMove[state] == 5 ? 0 : firstMove[state];
				break;
			}

			if (quantum >= kAutoHorizon) {
				continue;
			}

			const byte bits = safe[quantum * numCells + at];
			for (int a = 0; a < 5; ++a) {
				if (!(bits & (1 << a))) {
					continue;
				}

				const int to = (at / _numCols + dy[a]) * _numCols + (at % _numCols + dx[a]);
				const int nextState = (quantum + 1) * numCells + to;
				if (firstMove[nextState] != -1 || !survivable[nextState]) {
					continue;
				}

				firstMove[nextState] = firstMove[state] == 5 ? (int8)a : firstMove[state];
				queue.push(nextState);
			}
		}
	}

	if (chosen < 0) {
		// No safe route to the goal inside the horizon, or nowhere safe to stand.
		// Fall back to the survivable move that gets closest to the goal on the
		// mask alone, preferring to stand still.
		Common::Array<int> toGoal;
		toGoal.resize(numCells);
		for (int i = 0; i < numCells; ++i) {
			toGoal[i] = -1;
		}

		Common::Queue<int> queue;
		if (allowed[goalIdx]) {
			toGoal[goalIdx] = 0;
			queue.push(goalIdx);
		}

		while (!queue.empty()) {
			const int at = queue.pop();
			for (uint d = 1; d < 5; ++d) {
				const int nx = at % _numCols + dx[d];
				const int ny = at / _numCols + dy[d];
				if (nx < 0 || ny < 0 || nx >= _numCols || ny >= _numRows) {
					continue;
				}

				const int to = ny * _numCols + nx;
				if (toGoal[to] >= 0 || !allowed[to]) {
					continue;
				}

				toGoal[to] = toGoal[at] + 1;
				queue.push(to);
			}
		}

		// Take the survivable move that gets closest; standing still is action 0,
		// so it wins every tie.
		const byte bits = safe[curIdx];
		int bestDist = -1;

		for (int a = 0; a < 5; ++a) {
			if (!(bits & (1 << a))) {
				continue;
			}

			const int to = (cur.y + dy[a]) * _numCols + (cur.x + dx[a]);
			if (!survivable[numCells + to] || toGoal[to] < 0) {
				continue;
			}

			if (bestDist < 0 || toGoal[to] < bestDist) {
				bestDist = toGoal[to];
				chosen = a;
			}
		}

		// Nothing survivable and nothing that makes progress: take any safe move
		// at all. Being caught only reloads the room, so a bad guess costs time
		// rather than the run.
		for (int a = 0; a < 5 && chosen < 0; ++a) {
			if (bits & (1 << a)) {
				chosen = a;
			}
		}

		if (chosen < 0) {
			return false;
		}
	}

	next = Common::Point(cur.x + dx[chosen], cur.y + dy[chosen]);
	return chosen != 0;
}

void SentryPuzzle::autoStep(uint32 now) {
	if (now < _autoNextPlan) {
		return;
	}

	Common::Array<bool> blocked;
	Common::Point goal;
	bool isObjective = false;

	if (!autoChooseGoal(blocked, goal, isObjective)) {
		_autoNextPlan = now + kAutoWaitPace;
		return;
	}

	if (autoTrace() && goal != _autoGoal) {
		TraceEvent("sentry")
			.str("what", "goal")
			.num("room", roomNumber())
			.num("col", goal.x)
			.num("row", goal.y)
			.str("kind", isObjective ? "objective" : "door")
			.num("atcol", (int)_playerX / _cellSize)
			.num("atrow", (int)_playerY / _cellSize)
			.emit();
	}

	_autoGoal = goal;

	Common::Point next;
	if (autoPlanStep(goal, blocked, next)) {
		_path.clear();
		_path.push_back(next);
	} else {
		_autoNextPlan = now + kAutoWaitPace;
	}
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
		// Clamped so a stall (a loading hitch, the pause menu) cannot teleport
		// anybody across the room in a single step.
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
				if (autoPlay() && autoTrace()) {
					TraceEvent("sentry")
						.str("what", "caught")
						.num("room", roomNumber())
						.num("col", (int)_playerX / _cellSize)
						.num("row", (int)_playerY / _cellSize)
						.emit();
				}

				_playState = kCaught;
				_caughtTime = now;
				_path.clear();
			} else {
				checkTriggers();
			}

			// The planner only ever has to decide what to do next when Nancy has
			// finished a cell, or while she is deliberately standing still.
			if (autoPlay() && _playState == kWalking && _path.empty()) {
				autoStep(now);
			}

			redraw();
			break;
		}
		case kCaught: {
			// Hold the alarm for a beat so the player sees what got them, then
			// raise the caught flag. Two sibling PlaySound records in every one of
			// the nine scenes wait on that flag: one plays "Laser_Caught" and
			// changes the scene back to itself, which is what restarts the room.
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
			// A doorway or the alarm has fired; a sibling record owns the scene
			// change from here. Keep the last frame on screen and stop stepping.
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

	// Click to walk. Nothing in the record says how Nancy is driven; the data
	// only fixes which cells she may stand on, so a click and a shortest path
	// through the mask is the reading.
	g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

	if (input.input & NancyInput::kLeftMouseButtonUp) {
		findPath(Common::Point((int)_playerX / _cellSize, (int)_playerY / _cellSize),
			Common::Point(col, row));
		input.eatMouseInput();
	}
}

} // End of namespace Action
} // End of namespace Nancy
