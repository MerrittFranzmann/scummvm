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

// The paper doll in Nancy16's armoire, one record per open drawer. Each drawer
// holds one category of garment. This record draws its own category on the
// rack and the doll; garments from other categories are drawn by the scene's
// static overlays, which watch the same "worn" event flags.
//
//   char[33]   garment atlas, then 182 bytes of unused names and metrics
//   RECT       where a worn garment of this category sits on the doll
//   byte[25]
//   uint16     garment count
//     per garment, 143 bytes:
//       RECT   source rect in the atlas
//       int16  unknown
//       RECT   destination on the rack
//       RECT   clickable part of that destination
//       RECT   the matching part of the source rect
//       RECT   the source rect again
//       byte[48] three unused RECTs
//       byte[5]
//       uint16 "has been worn" flag label
//       uint16 "is worn now" flag label
//       byte[4]
//   SoundGroup x2   rustle, on two channels
//   SoundGroup      unused
//   SoundGroup      rustle
//   byte[4], byte[7]
//   SoundGroup      unused
//   byte[7]
//   SoundGroup      unused
//   uint16     drawer count
//     per drawer, 23 bytes: RECT hotspot, uint16, uint16 scene, uint16 flag,
//                           byte flag value
//
// The drawer entries are the drawer fronts plus a strip along the bottom of
// the viewport that closes the armoire.
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
	// event flags, since those are what the overlays draw from and what a save restores.
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
