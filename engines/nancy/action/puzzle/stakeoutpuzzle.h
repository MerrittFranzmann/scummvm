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

#ifndef NANCY_ACTION_STAKEOUTPUZZLE_H
#define NANCY_ACTION_STAKEOUTPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// AR type 212 in Nancy16, which reuses the type: in older games 212 is
// OrderingPuzzle/kOrderItems.
//
// The Venice night stake-out in scene S5610. Four agents hide in the courtyard
// and radio in where they are; Nancy must click the hiding place Nico peeks out
// of without giving away any of theirs.
//
// Record layout:
//   uint16  value index of the catch count (69)
//   uint16  value index of the miss count (70)
//   str33   NDUI panel name ("StakeOutDialog")
//   str33   list row template name ("TextEntry")
//   uint16  unknown (29)
//   double  5 tuning values (0.75, 1.0, 0.5, 4.0, 12.0)
//   double  3 (min, max) ranges, all (0, 2)
//   uint16  silhouette count (11)
//     per silhouette: str33 label, Rect dest, str33 image name,
//                     uint16 n1, n1 x Rect, uint16 n2, n2 x Rect,
//                     RandomSoundBlock (Nancy naming the place)
//   uint16  suspect count (5)
//     per suspect: str33 name, byte, uint16, RandomSoundBlock x 3
//
// A silhouette's two frame arrays are the two halves of one peek. A suspect's
// three sound groups answer a click on that agent's own place, on an empty
// place, and on Nico; Nico's own groups are empty.
//
// S5610's scene changes compare 69 and 70 against targets that depend on the
// difficulty, so this record only counts.
//
// Assumed, not stated by the record: only Nico moves after a click, _tuning[4]
// is the frame rate, and the ranges are delays in seconds around a peek.
// _tuning[0..3] are unknown and unused.
class StakeOutPuzzle : public RenderActionRecord {
public:
	StakeOutPuzzle() : RenderActionRecord(7) {}
	virtual ~StakeOutPuzzle();

	// A hiding place: a dest rect and the two halves of one peek, in one sheet
	struct Silhouette {
		Common::String label;			// "A" .. "K"
		Common::Rect dest;
		Common::Path imageName;			// ORP_Shadow?ANIM_OVL
		Common::Array<Common::Rect> framesA;	// coming out
		Common::Array<Common::Rect> framesB;	// going back in
		RandomSoundBlock line;			// NDStakeOut01ENGnn, channel 25

		// Runtime
		Graphics::ManagedSurface image;
		enum PeekState { kHidden, kEmerging, kOut, kWithdrawing };
		PeekState peek = kHidden;
		uint frame = 0;
		uint32 nextStepTime = 0;
		bool occupied = false;
	};

	struct Suspect {
		Common::String name;			// Nico, Bruno, Eva, Gino, Sabina
		byte unknownByte = 0;			// 1 for Nico, 0 for the other four
		uint16 unknownValue = 0;		// 14, 24, 9, 10, 11
		RandomSoundBlock lines[3];		// empty for Nico

		// Runtime
		int position = -1;				// index into _silhouettes, -1 if unplaced

		// Nico is the only suspect with no lines
		bool isTarget() const { return lines[0].names.empty() && lines[2].names.empty(); }
	};

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "StakeOutPuzzle"; }

	static void readCountedRects(Common::SeekableReadStream &stream, Common::Array<Common::Rect> &out);

	// kReporting: the agents radio in one at a time. kJudging: the reply to a
	// click is playing and the score has not been applied yet.
	enum Phase { kReporting, kWatching, kJudging };

	void placeAgents();
	void placeNico();
	void stepSilhouettes(uint32 now);
	void stepReports(uint32 now);
	void judgeClick(uint index, uint32 now);
	void applyScore();
	void redrawSilhouettes();
	void publishList();
	void say(const Common::String &line);

	// Adds delta to a player-table value the same way AR 108 SetValue does
	void addToValue(uint16 index, int16 delta);

	uint32 frameDelayMs() const;
	uint32 randomRangeMs(uint which) const;
	SoundDescription playSoundBlock(const RandomSoundBlock &block);
	uint pickResponder() const;

	uint16 _valueIndexA = 0;			// 69, catches
	uint16 _valueIndexB = 0;			// 70, misses
	Common::String _dialogName;			// "StakeOutDialog"
	Common::String _entryName;			// "TextEntry"
	uint16 _unknown29 = 0;

	double _tuning[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
	double _ranges[3][2] = {{ 0.0, 0.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }};

	Common::Array<Silhouette> _silhouettes;
	Common::Array<Suspect> _suspects;

	Phase _phase = kReporting;
	Common::Array<uint> _agents;		// indices into _suspects, in record order
	int _nico = -1;						// index into _suspects

	uint _reportsDone = 0;
	uint32 _reportWaitUntil = 0;

	// Taken from the report PlaySound records in S5611-S5614
	static const int16 kReportChannel = 24;
	static const int16 kReportVolume = 85;

	int _pendingValueIndex = -1;
	int16 _pendingDelta = 0;
	int _pendingResponder = -1;			// index into _suspects
	int _pendingGroup = 0;				// which of that suspect's three groups
	bool _responsePlayed = false;
	int16 _nancyChannel = -1;
	int16 _responseChannel = -1;
	uint32 _judgeWaitUntil = 0;

};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_STAKEOUTPUZZLE_H
