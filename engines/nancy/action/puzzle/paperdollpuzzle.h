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

#ifndef NANCY_ACTION_PAPERDOLLPUZZLE_H
#define NANCY_ACTION_PAPERDOLLPUZZLE_H

#include "engines/nancy/commontypes.h"
#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// "Nancy Doll Dressup Accessories", AR type 166 - OneBuildPuzzle in Nancy12-15.
// The paper doll in the RED bedroom armoire. One record per open drawer: twelve
// of them over S3601-S3606 and S3608-S3613, the second six byte-identical to the
// first six except for the record's own scene.
//
// The armoire has six drawers, each holding one category of garment (necklaces,
// glasses, hats, tops, trousers, shoes). Opening a drawer changes to that
// drawer's scene, and that scene's type 166 record is the drawer's contents:
// the garments laid out on the right-hand rack, plus whatever this category's
// doll is already wearing.
//
// The rest of the screen is not this record's work. The armoire, the drawers and
// the dashed doll outline are the scene's background art, and the garments worn
// in the *other* five categories are drawn by that scene's ~36 type 52 static
// overlays, each gated on one of the 40 "worn" event flags. So all this record
// has to draw is its own category, and all it has to do on a click is set the
// flag that the type 52 records are already watching.
//
// Record layout, 12/12 byte-exact:
//
//   char[33]   garment atlas ("RED_Armoire_PUZ_OVL"), then 182 bytes holding
//              two empty names and, at offset 203, uint16 28 / 29 / 28 - the
//              same grid metrics MosaicPuzzle carries - a per-drawer id, 0, 2
//   RECT       where a worn garment of this category sits on the doll
//   byte[25]
//   uint16     garment count (4, 6, 7 or 8)
//     per garment, 143 bytes:
//       RECT   source rect in the atlas
//       int16  -1
//       RECT   destination on the rack
//       RECT   clickable part of that destination
//       RECT   the matching part of the source rect
//       RECT   the source rect again
//       byte[48] three unused RECTs, zero in every garment
//       byte[5]
//       uint16 "has been worn" flag label, 2138-2207, unique per garment
//       uint16 "is worn now" flag label, 2063-2104, unique per garment
//       byte[4]
//   SoundGroup x2   Paper_Doll01..08 on channels 14 and 15 - the rustle
//   SoundGroup      "NO SOUND"
//   SoundGroup      Paper_Doll01..08 on channel 8
//   byte[4], byte[7]                (a 7-byte scene-and-flag block, all no-ops)
//   SoundGroup      "NO SOUND"
//   byte[7]
//   SoundGroup      "NO SOUND"
//   uint16     drawer count (7 in every record)
//     per drawer, 23 bytes: RECT hotspot, uint16, uint16 scene, uint16 flag,
//                           byte flag value
//
// The count sits at offset 256, past the atlas name and the metrics, which is
// why scanning the first 200 bytes for it comes up empty. Confirmed rather than
// merely fitted: on the smallest record the garment array ends at byte 830,
// exactly on the uint16 8 that opens the first SoundGroup, with "Paper_Doll01"
// immediately after it - and the drawer table then lands exactly on the end of
// the record in all twelve.
//
// The seven drawer entries are the six drawer fronts plus a full-width strip
// across the bottom of the viewport; the drawer you are standing in front of
// targets S3600, the closed armoire, so clicking it shuts the drawer again.
class PaperDollPuzzle : public RenderActionRecord {
public:
	PaperDollPuzzle() : RenderActionRecord(7) {}
	virtual ~PaperDollPuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "PaperDollPuzzle"; }

	struct Garment {
		Common::Rect src;			// in the atlas
		Common::Rect rackDest;		// where it hangs, viewport space
		Common::Rect hotspot;		// the part of rackDest that can be clicked
		int16 wornFlag = kEvNoEvent;
		int16 everWornFlag = kEvNoEvent;
	};

	struct Drawer {
		Common::Rect hotspot;
		SceneChangeDescription scene;
		FlagDescription flag;
	};

	// Index of the garment currently worn in this category, or -1. Read from the
	// event flags rather than kept locally: the flags are what the type 52
	// overlays draw from and what a save restores, so they are the state, and a
	// second copy here would be the one that goes stale.
	int wornGarment() const;

	int garmentAtCursor(const Common::Point &mousePos) const;
	int drawerAtCursor(const Common::Point &mousePos) const;

	void redraw();
	void playRustle();

	// -- File data --
	Common::Path _imageName;
	Common::Rect _dollDest;
	Common::Array<Garment> _garments;
	Common::Array<Drawer> _drawers;
	RandomSoundBlock _rustleSound;

	// -- Runtime state --
	Graphics::ManagedSurface _image;
	int _lastDrawnWorn = -2;	// forces the first redraw, since -1 is "nothing worn"
	SceneChangeDescription _pendingScene;
	FlagDescription _pendingFlag;
	bool _drawerRequested = false;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_PAPERDOLLPUZZLE_H
