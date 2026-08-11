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

#ifndef NANCY_ACTION_OVERLAY_H
#define NANCY_ACTION_OVERLAY_H

#include "engines/nancy/action/actionrecord.h"

namespace Nancy {
namespace Action {

// Places a static image or a looping animation on top of the background
// Can move along with the scene's background frame, however:
// - in animation mode, the animation is the same for every background frame
// - in static mode, every background frame gets its own static image
// Also supports:
// - playing a sound;
// - playing backwards;
// - looping (non-looping animated overlays are very rare);
// - getting interrupted by an event flag;
// - changing the scene/setting flags when clicked/interrupted
// Originally introduced in nancy1, where it was split into two different types:
// PlayStaticBitmapAnimation and PlayIntStaticBitmapAnimation (the latter was interruptible)
// In nancy2, the two got merged inside the newly-renamed Overlay;
// that was also when static mode got introduced.
class Overlay : public RenderActionRecord {
public:
	Overlay(bool interruptible) : RenderActionRecord(7), _isInterruptible(interruptible), _usesAutotext(false) {}
	virtual ~Overlay() { _fullSurface.free(); }

	void init() override;
	void handleInput(NancyInput &input) override;
	void updateGraphics() override;

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	Common::Path _imageName;

	uint16 _transparency = kPlayOverlayPlain;
	uint16 _hasSceneChange = kPlayOverlaySceneChange;
	uint16 _enableHotspotNancy2 = kPlayOverlayNoHotspot;
	uint16 _overlayType = kPlayOverlayAnimated;
	uint16 _playDirection = kPlayOverlayForward;
	uint16 _loop = kPlayOverlayOnce;
	uint16 _firstFrame = 0;
	uint16 _loopFirstFrame = 0;
	uint16 _loopLastFrame = 0;
	uint32 _frameTime = 0;
	FlagDescription _interruptCondition;
	SceneChangeDescription _sceneChange;
	MultiEventFlagDescription _flagsOnTrigger;

	SoundDescription _sound;

	// Describes a single frame in this animation
	Common::Array<Common::Rect> _srcRects;
	// Describes how the animation will be displayed on a single
	// frame of the viewport
	Common::Array<FrameBlitDescription> _blitDescriptions;

	int16 _currentFrame = -1;
	int16 _currentViewportFrame = -1;
	uint32 _nextFrameTime = 0;
	// Whether the viewport frame we are on right now carries a blit description
	// for this record. updateGraphics() needs it: it may only re-assert a static
	// overlay's visibility where execute() has actually placed the overlay.
	bool _hasBlitForCurrentFrame = false;
	bool _isInterruptible;
	bool _usesAutotext;

	bool canHaveHotspot() const override { return true; }
	bool isViewportRelative() const override { return true; }
	bool survivesSceneChange(bool nextSceneIsNoArt) const override { return nextSceneIsNoArt; }
	Common::String getRecordExtraInfo() const override { return Common::String::format("Scene %d", _sceneChange.sceneID); }

protected:
	Common::String getRecordTypeName() const override;

	Graphics::ManagedSurface _fullSurface;
};

// Short version of a static overlay; assumes scene background doesn't move
class OverlayStaticTerse : public Overlay {
public:
	OverlayStaticTerse() : Overlay(true) {}
	virtual ~OverlayStaticTerse() {}

	void readData(Common::SeekableReadStream &stream) override;

protected:
	Common::String getRecordTypeName() const override { return "OverlayStaticTerse"; }
};

// Short version of an animated overlay; assumes scene background doesn't move
class OverlayAnimTerse : public Overlay {
public:
	OverlayAnimTerse() : Overlay(true) {}
	virtual ~OverlayAnimTerse() {}

	void readData(Common::SeekableReadStream &stream) override;

protected:
	Common::String getRecordTypeName() const override { return "OverlayAnimTerse"; }
};

// Nancy16 AR 53, "rollover overlay". A hotspot that shows a piece of a still
// image while the cursor is inside it, and on click plays a sound, sets an
// event flag and (optionally) changes scene. It is _not_ a movie: the number
// used to map to PlaySecondaryMovie, which fed the image name to the video
// loader and error()ed on the miss.
//
// The hover half is what makes the record worth having: the costume shop
// (S4712) pairs every rollover with a "Text Scroll" record gated on the hover
// flag, so hovering an item pops up its price tag _and_ the price text, and
// the click flag is what the "buy it" records key off.
class RolloverOverlay : public RenderActionRecord {
public:
	RolloverOverlay() : RenderActionRecord(10) {}
	virtual ~RolloverOverlay() { _fullSurface.free(); }

	void init() override;
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void handleInput(NancyInput &input) override;

	bool canHaveHotspot() const override { return true; }
	bool isViewportRelative() const override { return true; }

	// GUESS: the third uint16 of the header is read as a cursor type. It is 14
	// on every UI-ish rollover (the PDA keys, the handbook contents, the guide
	// page links, the Second Chance yes/no buttons, the laptop page arrows) and
	// 0 or 1 on the price tags, which lines up with Nancy13's system cursor
	// table (14 = kNancy13Arrow, 0 = eyeglass, 1 = its hotspot variant). No
	// other reading of the field has been found.
	CursorManager::CursorType getHoverCursor() const override { return (CursorManager::CursorType)_cursorType; }
	bool cursorSetFromScript() const override { return true; }

	Common::String getRecordExtraInfo() const override;

protected:
	Common::String getRecordTypeName() const override { return "RolloverOverlay"; }

	Common::Path _imageName;
	uint16 _cursorType = 0;
	Common::Rect _srcRect;
	Common::Rect _destRect;

	FlagDescription _hoverFlag;
	RandomSoundBlock _hoverSounds;
	FlagDescription _clickFlag;
	RandomSoundBlock _clickSounds;
	SceneChangeDescription _sceneChange;

	bool _isHovered = false;
	SoundDescription _playingSound;
	Graphics::ManagedSurface _fullSurface;
};

// Nancy16 AR 54, "Static Overlay Image". The same still-image blit as the terse
// overlay on AR 52 (same z / source / destination triple), plus a table of
// 32-bit colours, each tagged with an event flag label.
//
// The pairing in the data says what the table is for: in the Scopa scenes every
// AR 54 has an AR 52 twin with the identical image and rects, the 52 gated on a
// flag being clear and the 54 on the next flag being set - i.e. the same card,
// drawn plain and drawn marked. The one colour those records carry is a single
// light blue. The microscope slides (S3280/S3281) carry ten colours tagged
// 1011..1020, one per dye, over a greyscale fibre texture, and each of those
// flags also gates a spoken line about that dye.
//
// GUESS: the colour is applied as a multiply tint over the blitted image - the
// reading that makes both uses come out right (a blue-marked card; a greyscale
// fibre rendered in the chosen dye). Which entry wins is the first one whose
// flag is currently set; with none set the image is drawn untinted.
class OverlayStaticTinted : public RenderActionRecord {
public:
	OverlayStaticTinted() : RenderActionRecord(10) {}
	virtual ~OverlayStaticTinted() { _fullSurface.free(); }

	void init() override;
	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;
	void updateGraphics() override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "OverlayStaticTinted"; }

	// Rebuild _drawSurface from _fullSurface, multiplied by the tint at
	// _tints[tintID] (or untinted when tintID is -1).
	void applyTint(int tintID);
	int pickTint() const;

	struct TintEntry {
		uint32 color = 0;		// 0xRRGGBBAA; the alpha byte is 0xff in all 21 records
		int16 flagLabel = -1;
	};

	Common::Path _imageName;
	Common::Rect _srcRect;
	Common::Rect _destRect;
	Common::Array<TintEntry> _tints;
	int _currentTint = -2;		// -2 = never drawn, -1 = drawn untinted

	Graphics::ManagedSurface _fullSurface;
};

class TableIndexOverlay : public Overlay {
public:
	TableIndexOverlay() : Overlay(true) {}
	virtual ~TableIndexOverlay() {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

protected:
	Common::String getRecordTypeName() const override { return "TableIndexOverlay"; }

	uint16 _tableIndex = 0;
	int16 _lastIndexVal = -1;
};

// Draws a single line of text on top of the scene background. The text is a
// value looked up from the player-data table (used by the nancy12 minigolf
// scorecard, where each hole's score is a separate record).
class TextLineOverlay : public RenderActionRecord {
public:
	TextLineOverlay() : RenderActionRecord(8) {}
	virtual ~TextLineOverlay() {}

	void readData(Common::SeekableReadStream &stream) override;
	void execute() override;

	bool isViewportRelative() const override { return true; }

protected:
	Common::String getRecordTypeName() const override { return "TextLineOverlay"; }

	uint16 _fontID = 0;
	uint16 _textColor = 0;
	Common::Point _position;
	Common::String _textKey;
	int16 _tableIndex = 0;
};

} // End of namespace Action
} // End of namespace Nancy

#endif // NANCY_ACTION_OVERLAY_H
