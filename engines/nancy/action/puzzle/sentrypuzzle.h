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

// The "Robot Sentries" warehouse maze, AR types 210 and 168 in Nancy16.
// Scene descriptions call S3871..S3879 "Robot Sentries 1".."Robot Sentries 9".
//
// HOW THE TWO TYPES RELATE (measured, not assumed - see below for the evidence)
//
//   type 210, 9 records, one per room S3871..S3879. This is the whole playfield:
//            the walk mask, Nancy's sprite sheet, three patrolling sentries and
//            a list of trigger rectangles.
//   type 168, 30 records. NOT a per-sentry entry. It is a doorway hand-off:
//            "the next time level <name> loads, put Nancy on cell <rect>". Each
//            one sits immediately before a type 15 "Scene Change" record that
//            carries the actual destination scene, and both are gated on the
//            same event flag - the flag the 210 record raises when Nancy walks
//            into the matching doorway.
//
// The room graph falls straight out of the data and cross-checks perfectly.
// Reading the 210 triggers as arrows out of each room gives
//
//        71 72 73
//        74 75 76        right = +1, down = +3
//        77 78 79
//
// and every one of the thirteen doorways matches its partner: the 168 record
// that goes with room A's east arrow names room B and a cell on B's *west*
// edge, and B's own west arrow names A and the mirror cell on A's east edge.
// All 26 in-maze 168 records pair up this way; there is not one stray. The
// remaining four (S3890-S3893) have no dependency at all, so they fire on scene
// entry: those are the four corner-room keypad close-ups, and the hand-off puts
// Nancy back beside the keypad when the maze room reloads.
//
// 210 LAYOUT - 9/9 byte-exact
//
//   char[33]   level name, "SentryPuz71".."SentryPuz79"
//   char[33]   sprite sheet for Nancy, always "ZAT_RbtSntA_OVL" (221x63)
//   char[33]   walk mask, "ZAT_RbtSntBW01".."ZAT_RbtSntBW09" (640x385)
//   uint16     event flag raised when Nancy is caught, 1031 in all nine
//   RECT       default start cell
//   int32      180 in all nine                            (unidentified)
//   int32      75                                         (Nancy's speed)
//   int32      32, the cell size - the mask is exactly 20x12 cells of this
//   uint16 + n x RECT   Nancy's animation, 11 frames of 26x30
//   uint16 + n x sentry, always 3:
//       char[33]  sprite sheet, always "ZAT_RbtSntB_OVL" (206x235)
//       uint16 + n x RECT  facing frames, always 4 of 32x32
//       uint16 + n x RECT  alarm frames,  always 2 of 32x32
//       uint32 0, RECT, RECT      beam sprites, yellow (atlas x 143..146)
//       uint32 0, RECT, RECT      beam sprites, red    (atlas x 133..136)
//       byte      2 (1 on one sentry in S3875)           (unidentified)
//       uint16 + n x RECT  patrol waypoints, 4..15 of them, in playfield coords
//       int32     detection radius, 25 / 20 / 15
//       double    speed, 0.2 .. 0.5
//       int32     120 in all 27                           (beam length)
//   uint16 + n x trigger, 7..14 of them:
//       uint32    kind, 11 for all but one
//       RECT      trigger area
//       44 bytes  zero in all 85 triggers in the game
//       float     -272.0 in all 85                        (unidentified)
//       byte      64 in all 85                            (unidentified)
//       int16 + byte  condition flag, label -1 = none
//       6 bytes   zero in all 85, the size of a Nancy16 scene change
//       [kind 11] int16 + byte  flag to raise
//   uint16     0, an empty trailing array
//
// WHAT IS MEASURED AND CHECKABLE
//
//  * The 640x385 mask quantises exactly to 20x12 cells of 32px - every one of
//    the nine masks is uniform inside every cell, no mixed cell anywhere. White
//    is corridor, black is crate.
//  * All 27 patrol paths are axis-aligned leg by leg AND every cell they cross
//    is white in that room's mask. That is 27 independent checks against a
//    separate asset, and they all pass, which is what pins the waypoint array.
//  * Trigger pairs. Every doorway has two on-grid triggers: one on the cell
//    *inside* the door that raises a scratch flag (1040-1049), and one on the
//    door cell itself that requires that scratch flag and raises the exit flag.
//    That gate exists because Nancy arrives standing on the door cell - without
//    it she would bounce straight back through. Plus an off-grid trigger just
//    beyond the room edge with no condition, for walking clean off the board.
//  * The caught flag 1031 is the header's uint16 and it is exactly the flag two
//    sibling PlaySound records in every one of the nine scenes are gated on:
//    "Laser_Caught" on channel 15 with a scene change back to the same scene,
//    and "silence_1sec" which raises 1039, which in turn fires
//    "ZAT_Siren_Alarm2". So being caught restarts the room; this record only
//    has to raise 1031.
//
// WHAT IS GUESSED (and mirrored as comments at the point of use)
//
//  * Control scheme. Nothing in the record says how Nancy is driven. Click to
//    walk, with a breadth-first path through the mask, is the reading; the data
//    only fixes where she may stand.
//  * Speeds. Nancy's 75 is taken as pixels per second. The sentry double is
//    taken as pixels per 1/60s tick, so 0.5 is 30 px/s - Nancy is then about
//    2.5x faster than the fastest sentry, which is what makes the maze passable.
//    Neither unit is stated anywhere.
//  * Detection. The int32 25/20/15 is read as a contact radius in pixels and
//    the constant 120 as the beam's reach. Those are the only two length-like
//    numbers per sentry and they track the sprite colour, but the pairing is a
//    reading.
//  * The 11th Nancy frame is listed out of atlas order (it is the leftmost of
//    the top row, listed last), so it is taken as the standing frame and 0..9 as
//    the walk cycle.
//  * The four facing frames are indexed up/right/down/left. The green sentries
//    list them in exactly that atlas order; the blue and red ones list the same
//    four frames permuted, so on those two the lit lobe will not always agree
//    with the direction of travel. The permutation is in the file - it is not
//    something this code introduces.
//  * The single kind-20 trigger is read as a conditional wall rather than a
//    trigger, because it raises no flag and there is nothing else it could do.
//    That reading is checked, not merely assumed: it sits in S3875, the room with
//    the diamond, its condition is "the flag that means all four corner locks are
//    broken is still clear", and blocking its six cells leaves all four of that
//    room's doorways mutually reachable while making exactly the nine-cell
//    diamond alcove unreachable. That is what the four laser barriers drawn over
//    those cells by sibling Movie Playback records are for.
//
//    The flag is 1035, and what raises it is byte-exact: the sibling EventFlags
//    record in S3875 is gated on dependency (13, 25) with time 4, which Nancy16
//    reads as the player-table compare "index 25 == 4", and index 25 is written
//    by exactly four records in the game - one accumulating +1 in each of the
//    four keypad scenes S3890-S3893 when its Override Lock puzzle is solved.
//    Checked against the decoded mask: the six cells the wall covers are rows
//    5 and 6 of a 3x3 pocket at columns 8-10 whose only entrance is (9,4) from
//    above, so blocking them makes the bottom row - which is where the sapphire
//    trigger sits, at (9,7) - unreachable, and nothing else.

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
	int32 _unknown = 0;		// 180 in 28 of 30, 0 in S3871, 90 in S3874
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
		Common::Rect area;
		float unknownFloat = 0.0f;
		byte unknownByte = 0;
		FlagDescription condition;
		FlagDescription raise;
	};

	// Autoplay only: one sentry's predicted pose at one instant. See the .cpp.
	struct AutoPose {
		double x = 0.0, y = 0.0;
		Common::Rect beam;
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

	// -- autoplay: debug affordance, default off; see the block comment in the .cpp --
	static bool autoPlay();
	static bool autoTrace();
	int roomNumber() const;
	bool isExitTrigger(const Trigger &trigger) const;
	bool isObjectiveTrigger(const Trigger &trigger) const;
	void triggerCells(const Trigger &trigger, Common::Array<Common::Point> &out) const;
	bool autoChooseGoal(Common::Array<bool> &blocked, Common::Point &goal, bool &isObjective) const;
	bool autoSeenAt(const Common::Array<AutoPose> &poses, int slice, double px, double py) const;
	bool autoEdgeSafe(const Common::Array<AutoPose> &poses, int quantum,
		const Common::Point &from, const Common::Point &to) const;
	bool autoPlanStep(const Common::Point &goal, const Common::Array<bool> &blocked, Common::Point &next) const;
	void autoStep(uint32 now);

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
	Graphics::ManagedSurface _sentryImage;
	Graphics::ManagedSurface _mask;
	uint32 _wallColor = 0;
	int _numCols = 0;
	int _numRows = 0;

	double _playerX = 0.0, _playerY = 0.0;			// centre, playfield pixels
	Common::Array<Common::Point> _path;				// cells still to visit
	uint _playerFrame = 0;
	uint32 _playerFrameTime = 0;
	double _walkedSinceFrame = 0.0;

	State _playState = kWalking;
	uint32 _caughtTime = 0;
	uint32 _lastStepTime = 0;

	// Autoplay only: earliest time the planner may run again (it only needs to
	// run once per cell walked, or once a beat while it is standing still), and
	// the goal it last reported, so the trace prints one line per decision
	// rather than one per frame.
	uint32 _autoNextPlan = 0;
	Common::Point _autoGoal = Common::Point(-1, -1);
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_SENTRYPUZZLE_H
