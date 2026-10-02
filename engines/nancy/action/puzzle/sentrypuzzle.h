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

#ifndef NANCY_ACTION_SENTRYPUZZLE_H
#define NANCY_ACTION_SENTRYPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/puzzledata.h"
#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// The "Robot Sentries" warehouse maze. Type 210 is one room of the playfield;
// type 168 tells the next room that loads which cell to start Nancy on.
//
// Type 210 layout:
//   char[33]   level name
//   char[33]   Nancy's sprite sheet
//   char[33]   walk mask; white is corridor, black is wall
//   uint16     event flag raised when Nancy is caught
//   RECT       default start cell
//   int32      unknown
//   int32      Nancy's speed
//   int32      cell size
//   uint16 + n x RECT   Nancy's animation frames
//   uint16 + n x sentry:
//       char[33]  sprite sheet
//       uint16 + n x RECT  facing frames
//       uint16 + n x RECT  alarm frames
//       uint32, RECT, RECT  beam sprites, normal
//       uint32, RECT, RECT  beam sprites, alarmed
//       byte      unknown
//       uint16 + n x RECT  patrol waypoints
//       int32     detection radius
//       double    speed
//       int32     beam length
//   uint16 + n x trigger:
//       uint32    kind; 11 raises a flag, 20 is a conditional wall
//       RECT      trigger area
//       44 bytes  unused
//       float     unknown
//       byte      unknown
//       int16 + byte  condition flag
//       6 bytes   unused
//       [kind 11] int16 + byte  flag to raise
//   uint16     empty trailing array
//
// The speed units are assumed: pixels per second for Nancy, pixels per
// 1/60s tick for the sentries.

// Type 168. Names a level and a cell in it, and hands that pair to the next
// SentryPuzzle that loads. The scene change itself belongs to the type 15
// record that follows it.
class SentryPuzzleExit : public ActionRecord {
public:
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "SentryPuzzleExit"; }

	Common::String _levelName;
	Common::Rect _cell;
	int32 _unknown = 0;		// Unknown, usually 180
};

// Type 210. The playfield.
class SentryPuzzle : public RenderActionRecord {
public:
	SentryPuzzle() : RenderActionRecord(8) {}
	virtual ~SentryPuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "SentryPuzzle"; }

	enum Direction { kUp = 0, kRight = 1, kDown = 2, kLeft = 3 };
	enum State { kWalking, kCaught, kLeaving };

	struct Sentry {
		Common::String imageName;
		Common::Array<Common::Rect> facingFrames;	// 4, indexed by Direction
		Common::Array<Common::Rect> alarmFrames;	// 2

		uint32 beamUnknown0 = 0;
		Common::Rect beamLine;						// 4x1, yellow
		Common::Rect beamTip;						// 4x4, yellow
		uint32 beamUnknown1 = 0;
		Common::Rect alarmBeamLine;					// 4x1, red
		Common::Rect alarmBeamTip;					// 4x4, red
		byte unknownByte = 0;

		Common::Array<Common::Rect> waypoints;		// playfield cells
		int32 detectRadius = 0;
		double speed = 0.0;
		int32 beamLength = 0;

		// Runtime
		double x = 0.0, y = 0.0;					// centre, playfield pixels
		int leg = 0;								// index of the waypoint just left
		int legDir = 1;								// +1 forward, -1 back
		Direction facing = kUp;
		bool alarmed = false;
		uint alarmFrame = 0;
	};

	struct Trigger {
		uint32 kind = 0;
		bool awaitingDeparture = false;
		Common::Rect area;
		float unknownFloat = 0.0f;
		byte unknownByte = 0;
		FlagDescription condition;
		FlagDescription raise;
	};

	SentryPuzzleData *getState();

	bool isOpen(int col, int row) const;
	bool isWalkable(int col, int row) const;
	bool triggerIsLive(const Trigger &trigger) const;
	Common::Point cellCentre(int col, int row) const;
	bool findPath(const Common::Point &from, const Common::Point &to);
	void stepPlayer(uint32 deltaMs);
	void stepSentry(Sentry &sentry, double deltaMs) const;
	void stepSentries(uint32 deltaMs);
	Common::Rect beamRect(const Sentry &sentry) const;
	bool caughtAt(double sentryX, double sentryY, int32 detectRadius,
		const Common::Rect &beam, double playerX, double playerY) const;
	bool sentrySeesPlayer(const Sentry &sentry) const;
	void checkTriggers();
	void drawBeam(const Sentry &sentry);
	void redraw();

	// -- File data --
	Common::String _levelName;
	Common::Path _playerImageName;
	Common::Path _maskImageName;
	uint16 _caughtFlag = 0;
	Common::Rect _startCell;
	int32 _unknown = 0;
	int32 _playerSpeed = 0;
	int32 _cellSize = 32;
	Common::Array<Common::Rect> _playerFrames;
	Common::Array<Sentry> _sentries;
	Common::Array<Trigger> _triggers;

	// -- Runtime state --
	Graphics::ManagedSurface _playerImage;
	Common::Array<Graphics::ManagedSurface> _playerFacingFrames[4];
	Graphics::ManagedSurface _sentryImage;
	Graphics::ManagedSurface _mask;
	uint32 _wallColor = 0;
	int _numCols = 0;
	int _numRows = 0;

	double _playerX = 0.0, _playerY = 0.0;			// centre, playfield pixels
	Common::Array<Common::Point> _path;				// cells still to visit
	uint _playerFrame = 0;
	Direction _playerDirection = kDown;
	uint32 _playerFrameTime = 0;
	double _walkedSinceFrame = 0.0;

	State _playState = kWalking;
	uint32 _caughtTime = 0;
	uint32 _lastStepTime = 0;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_SENTRYPUZZLE_H
