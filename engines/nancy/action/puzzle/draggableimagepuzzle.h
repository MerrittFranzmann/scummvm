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

#ifndef NANCY_ACTION_DRAGGABLEIMAGEPUZZLE_H
#define NANCY_ACTION_DRAGGABLEIMAGEPUZZLE_H

#include "engines/nancy/action/actionrecord.h"
#include "engines/nancy/commontypes.h"

namespace Nancy {
namespace Action {

// AR 207, "Draggable Image with Action Zones" - the microdot viewer, the two
// records in S3263 (microdot A) and S3285 (microdot B).
//
// The record names an image far larger than the viewport (both microdots are
// 1600x1200 PNGs) and a window into it the same size as the destination
// rectangle, 640x385. The player drags the image around under a fixed circular
// eyepiece to read it; the eyepiece itself is not this record's work - the
// scene background draws the microscope and the type 52 overlay in the same
// scene, PIA_MicrodotPUZ_OVL, is the dark mask with the transparent circle that
// is drawn on top of us (z-order 7 against our 6).
//
// Layout, 129/129 bytes exact on both records:
//
//   char[33]   oversize image
//   RECT       initial window into that image, always 640x385
//   RECT       destination in the viewport, always (0,0)-(640,385)
//   char[33]   puzzle name, "MicrodotA" / "MicrodotB"
//   uint16     always 1
//   int32      always 4
//   uint16     action zone count (1 in both records)
//     per zone, 23 bytes, the same shape as the type 166 drawer table:
//       RECT   hotspot, viewport space
//       uint16 cursor type
//       uint16 scene, 0x7fff for "stay here"
//       int16  event flag, -1 for none
//       byte   flag value
//
// The single zone in each record is the full-width strip along the bottom of
// the viewport that leaves the microscope: cursor 10, scene 3261 (PIA_MicroOn),
// and it also raises scratch flag 1015.
class DraggableImagePuzzle : public RenderActionRecord {
public:
	DraggableImagePuzzle() : RenderActionRecord(6) {}
	virtual ~DraggableImagePuzzle() {}

	void init() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "DraggableImagePuzzle"; }
	Common::String getRecordExtraInfo() const override;

	struct Zone {
		Common::Rect hotspot;
		uint16 cursorType = 0;
		SceneChangeDescription scene;
		FlagDescription flag;
	};

	int zoneAtCursor(const Common::Point &mousePos) const;
	void redraw();

	// Moves the window by (dx, dy) in image space, clamped so it never leaves
	// the image. Returns true when it actually moved.
	bool pan(int dx, int dy);

	// The pan offset lives in CollectionData under the record's own name so it
	// survives leaving the scene and a save/load, the same way the collections
	// subsystem keeps the keypad digits. Nothing declares "MicrodotA" with an
	// AR 37 creation record, so the collection is made on first use.
	void loadOffset();
	void storeOffset();

	// -- File data --
	Common::Path _imageName;
	Common::Rect _initialWindow;
	Common::Rect _dest;
	Common::String _puzzleName;
	Common::Array<Zone> _zones;

	// -- Runtime state --
	Graphics::ManagedSurface _image;
	Common::Rect _window;
	bool _dragging = false;
	Common::Point _lastDragPos;
	bool _zoneRequested = false;
	bool _flagRaised = false;
	SceneChangeDescription _pendingScene;
	FlagDescription _pendingFlag;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_DRAGGABLEIMAGEPUZZLE_H
