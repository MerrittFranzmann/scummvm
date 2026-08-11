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

#include "engines/nancy/nancy.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/util.h"
#include "engines/nancy/input.h"
#include "engines/nancy/cursor.h"
#include "engines/nancy/graphics.h"

#include "engines/nancy/action/overlay.h"

#include "engines/nancy/state/scene.h"

#include "common/random.h"
#include "common/serializer.h"

#include "graphics/font.h"

namespace Nancy {
namespace Action {

void Overlay::init() {
	// Autotext overlays need special handling when blitting
	if (_imageName.baseName().hasPrefix("USE_")) {
		_usesAutotext = true;
	}

	g_nancy->_resource->loadImage(_imageName, _fullSurface);

	_currentFrame = _firstFrame;

	RenderObject::init();
}

void Overlay::handleInput(NancyInput &input) {
	// For no apparent reason, from nancy3 on the original engine handles Overlay input as a special case,
	// rather than simply set the general hotspot inside the ActionRecord struct. Special cases
	// (a.k.a puzzle types) get handled before regular ActionRecords, which means an Overlay
	// must take precedence when handling the mouse. Thus, out ActionManager class first iterates
	// through all records and calls their handleInput() function just to make sure this special
	// case is handled. This fixes nancy3 scene 7081.
	if (g_nancy->getGameType() >= kGameTypeNancy3) {
		if (_hasHotspot) {
			if (NancySceneState.getViewport().convertViewportToScreen(_hotspot).contains(input.mousePos)) {
				g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

				if (input.input & NancyInput::kLeftMouseButtonUp) {
					_state = kActionTrigger;

					// Make sure nothing else gets triggered
					// This is nancy3 and up, since we actually want to trigger other records in nancy2 (e.g. scene 2541)
					input.eatMouseInput();
				}
			}
		}
	}
}

void Overlay::updateGraphics() {
	// A static overlay gated by a dynamic dependency - e.g. a Nancy12 gas/tire gauge,
	// which is one of a set of sprites each shown for a different resource range - must be
	// visible only while its dependency currently holds. (With the usual set-once event
	// flags _isActive never drops back to false, so this is a no-op there.)
	//
	// It must ALSO still be on a viewport frame that has a blit for it. Without
	// the second term this line re-shows a static overlay the moment execute()
	// has correctly hidden it on a viewport frame it does not belong to, and it
	// is repainted with the _drawSurface and _screenPosition of whichever frame
	// it last matched. In a panoramic node that means every one of the node's
	// overlays piles onto every viewpoint, each a patch of the room as seen from
	// somewhere else. Nancy18 scenes 2910, 3210 and 3510 are the game's three
	// 20-frame nodes carrying static overlays, and all three showed it.
	if (g_nancy->getGameType() >= kGameTypeNancy12 && _state == kRun && _overlayType == kPlayOverlayStatic) {
		setVisible(_isActive && _hasBlitForCurrentFrame);
	}

	// Update inactive animated overlays
	if (!_isActive && _state == kRun && !_blitDescriptions.empty() && _overlayType == kPlayOverlayAnimated) {
		uint16 newFrame = NancySceneState.getSceneInfo().frameID;
		if (_currentViewportFrame == newFrame)
			return;

		_currentViewportFrame = (int16)newFrame;
		setVisible(false);

		for (auto &blit : _blitDescriptions) {
			if (_currentViewportFrame == blit.frameID) {
				moveTo(blit.dest);
				setVisible(true);
				break;
			}
		}
	}
}

void Overlay::readData(Common::SeekableReadStream &stream) {
	Common::Serializer ser(&stream, nullptr);
	ser.setVersion(g_nancy->getGameType());

	uint16 numSrcRects = 0;

	readFilename(ser, _imageName);
	ser.skip(2); // VIDEO_STOP_RENDERING or VIDEO_CONTINUE_RENDERING
	ser.syncAsUint16LE(_transparency);
	ser.syncAsUint16LE(_hasSceneChange);
	ser.syncAsUint16LE(_enableHotspotNancy2, kGameTypeNancy2, kGameTypeNancy2);
	ser.syncAsUint16LE(_z, kGameTypeNancy2);
	ser.syncAsUint16LE(_overlayType, kGameTypeNancy2);
	ser.syncAsUint16LE(numSrcRects, kGameTypeNancy2);

	ser.syncAsUint16LE(_playDirection);
	ser.syncAsUint16LE(_loop);
	ser.syncAsUint16LE(_firstFrame);
	ser.syncAsUint16LE(_loopFirstFrame);
	ser.syncAsUint16LE(_loopLastFrame);
	uint16 framesPerSec = stream.readUint16LE();

	// Avoid divide by 0
	if (framesPerSec) {
		_frameTime = Common::Rational(1000, framesPerSec).toInt();
	}

	ser.syncAsUint16LE(_z, kGameTypeNancy1, kGameTypeNancy1);

	if (_isInterruptible) {
			ser.syncAsSint16LE(_interruptCondition.label);
			ser.syncAsUint16LE(_interruptCondition.flag);
		} else {
			_interruptCondition.label = kEvNoEvent;
			_interruptCondition.flag = g_nancy->_false;
		}

	_sceneChange.readData(stream);
	_flagsOnTrigger.readData(stream);
	_sound.readNormal(stream);

	uint numViewportFrames = stream.readUint16LE();

	if (_overlayType == kPlayOverlayAnimated) {
		numSrcRects = _loopLastFrame - _firstFrame + 1;
	}

	readRectArray(ser, _srcRects, numSrcRects);

	_blitDescriptions.resize(numViewportFrames);
	for (auto &bm : _blitDescriptions) {
		bm.readData(stream, ser.getVersion() >= kGameTypeNancy2);
	}
}

void Overlay::execute() {
	uint32 _currentFrameTime = g_nancy->getTotalPlayTime();
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		g_nancy->_sound->loadSound(_sound);
		g_nancy->_sound->playSound(_sound);
		_state = kRun;
		// fall through
	case kRun: {
		// Check the timer to see if we need to draw the next animation frame
		if (_overlayType == kPlayOverlayAnimated && _nextFrameTime <= _currentFrameTime) {
			bool shouldTrigger = false;

			// Check for interrupt flag
			if (NancySceneState.getEventFlag(_interruptCondition)) {
				shouldTrigger = true;
			}

			// Wait until sound stops (if present)
			if (!g_nancy->_sound->isSoundPlaying(_sound)) {
				// Check if we're at the last frame
				if (_currentFrame == _loopLastFrame && _playDirection == kPlayOverlayForward && _loop == kPlayOverlayOnce) {
					shouldTrigger = true;
				} else if (_currentFrame == _loopFirstFrame && _playDirection == kPlayOverlayReverse && _loop == kPlayOverlayOnce) {
					shouldTrigger = true;
				}
			}

			if (shouldTrigger) {
				_state = kActionTrigger;
			} else {
				// Check if we've moved the viewport
				uint16 newFrame = NancySceneState.getSceneInfo().frameID;

				if (_currentViewportFrame != newFrame) {
					_currentViewportFrame = newFrame;

					setVisible(false);
					_hasHotspot = false;

					for (uint i = 0; i < _blitDescriptions.size(); ++i) {
						if (_currentViewportFrame == _blitDescriptions[i].frameID) {
							moveTo(_blitDescriptions[i].dest);
							setVisible(true);

							if (g_nancy->getGameType() <= kGameTypeNancy2) {
								if (_enableHotspotNancy2 == kPlayOverlayWithHotspot) {
									_hotspot = _screenPosition;
									_hasHotspot = true;
								}
							} else {
								if (_blitDescriptions[i].hasHotspot == kPlayOverlayWithHotspot) {
									_hotspot = _screenPosition;
									_hasHotspot = true;
								}
							}

							break;
						}
					}
				}

				uint16 frameDiff = 1;
				uint16 nextFrame = _currentFrame;

				if (_nextFrameTime == 0) {
					_nextFrameTime = _currentFrameTime + _frameTime;
				} else {
					uint32 timeDiff = _currentFrameTime - _nextFrameTime;
					frameDiff = timeDiff / MAX<uint32>(_frameTime, 1); // Fix for nancy2 scene 1090, where _frameTime is 0
					_nextFrameTime += _frameTime * frameDiff;
				}

				if (_playDirection == kPlayOverlayReverse) {
					if (nextFrame - frameDiff < _loopFirstFrame) {
						// We keep looping if sound is present (nancy1/2 only)
						if (_loop == kPlayOverlayLoop || (_sound.name != "NO SOUND" && g_nancy->getGameType() <= kGameTypeNancy2)) {
							nextFrame = _loopLastFrame - (frameDiff % (_loopLastFrame - _loopFirstFrame + 1));
						}
					} else {
						nextFrame -= frameDiff;
					}
				} else {
					if (nextFrame + frameDiff > _loopLastFrame) {
						if (_loop == kPlayOverlayLoop || (_sound.name != "NO SOUND" && g_nancy->getGameType() <= kGameTypeNancy2)) {
							nextFrame = _loopFirstFrame + (frameDiff % (_loopLastFrame - _loopFirstFrame + 1));
						}
					} else {
						nextFrame += frameDiff;
					}
				}

				// Workaround for:
				// - the arcade machine in nancy1 scene 833
				// - the fireplace in nancy2 scene 2491, where one of the rects is invalid.
				// - the ball thing in nancy2 scene 1562, where one of the rects is twice as tall as it should be
				// Assumes all rects in a single animation have the same dimensions
				Common::Rect srcRect = _srcRects[nextFrame];
				if (!srcRect.isValidRect() || srcRect.width() != _srcRects[0].width() || srcRect.height() != _srcRects[0].height()) {
					srcRect.setWidth(_srcRects[0].width());
					srcRect.setHeight(_srcRects[0].height());
				}

				_drawSurface.create(_fullSurface, srcRect);
				setTransparent(_transparency >= kPlayOverlayTransparent);

				_currentFrame = nextFrame;
				_needsRedraw = true;
			}
		} else {
			// Check if we've moved the viewport
			uint16 newFrame = NancySceneState.getSceneInfo().frameID;

			if (_currentViewportFrame != newFrame) {
				_currentViewportFrame = newFrame;

				setVisible(false);
				_hasHotspot = false;
				_hasBlitForCurrentFrame = false;

				// First, check if there's more than one blit description for the current viewport frame.
				// This happens in nancy7 scene 3600
				Common::Array<uint16> blitsForThisFrame;
				Common::Rect destRect;
				for (uint i = 0; i < _blitDescriptions.size(); ++i) {
					if (_currentViewportFrame == _blitDescriptions[i].frameID) {
						blitsForThisFrame.push_back(i);
						if (destRect.isEmpty()) {
							destRect = _blitDescriptions[i].dest;
						} else {
							destRect.extend(_blitDescriptions[i].dest);
						}
					}
				}

				if (_overlayType == kPlayOverlayStatic && blitsForThisFrame.size()) {
					moveTo(destRect);
					setVisible(true);
					_hasBlitForCurrentFrame = true;

					if (blitsForThisFrame.size() != 1) {
						_drawSurface.create(destRect.width(), destRect.height(), _fullSurface.format);
						setTransparent(true); // Force transparency. This shouldn't break anything. Hopefully.
						_drawSurface.clear(_drawSurface.getTransparentColor());
					}

					for (uint i = 0; i < blitsForThisFrame.size(); ++i) {
						// In static mode every "animation" frame corresponds to a viewport frame
						// Static mode overlays use both the general source rects (_srcRects),
						// and the ones inside the blit description struct corresponding to the current scene background.

						// BlitDescriptions contain the id of the source rect to actually use
						Common::Rect srcRect = _srcRects[_blitDescriptions[blitsForThisFrame[i]].staticRectID];
						Common::Rect staticBounds = _blitDescriptions[blitsForThisFrame[i]].src;

						if (_usesAutotext) {
							// For autotext overlays, the srcRect is junk data
							srcRect = staticBounds;
						} else {
							// Lastly, the general source rect we just got may also be completely empty (nancy5 scenes 2056, 2057),
							// or have coordinates other than (0, 0) (nancy3 scene 3070, nancy5 scene 2000). Presumably,
							// the general source rect was used for blitting to an (optional) intermediate surface, while the ones
							// inside the blit description below were used for blitting from that intermediate surface to the screen.
							// We can achieve the same results by doung the calculations below
							srcRect.translate(staticBounds.left, staticBounds.top);

							if (srcRect.isEmpty()) {
								srcRect.setWidth(staticBounds.width());
								srcRect.setHeight(staticBounds.height());
							} else {
								// Grab whichever dimensions are smaller. Fixes the book in nancy5 scene 3000
								srcRect.setWidth(MIN<int>(staticBounds.width(), srcRect.width()));
								srcRect.setHeight(MIN<int>(staticBounds.height(), srcRect.height()));
							}
						}

						Common::Rect blitDest = _blitDescriptions[blitsForThisFrame[i]].dest;

						// Make sure the srcRect doesn't extend beyond the image.
						// This fixes nancy7 scene 4228
						Common::Rect clippedSrc = srcRect;
						clippedSrc.clip(_fullSurface.getBounds());

						if (clippedSrc != srcRect) {
							// Clipping the source but not the destination makes the blit
							// *stretch* the surviving part of the image over the full authored
							// destination, because RenderObject scales whenever _drawSurface and
							// _screenPosition disagree. The original engine simply drops the part
							// of the blit that falls outside the image, so move the destination
							// edges by the same amounts the source edges moved.
							// This fixes nancy18 scene 4460, whose scopa tutorial pages author a
							// 640-wide source rect against a 591-wide sheet and were being
							// stretched horizontally by 8%.
							blitDest.left += clippedSrc.left - srcRect.left;
							blitDest.top += clippedSrc.top - srcRect.top;
							blitDest.right += clippedSrc.right - srcRect.right;
							blitDest.bottom += clippedSrc.bottom - srcRect.bottom;
							srcRect = clippedSrc;
						}

						if (blitsForThisFrame.size() == 1) {
							moveTo(blitDest);
							_drawSurface.create(_fullSurface, srcRect);
							setTransparent(_transparency >= kPlayOverlayTransparent);
						} else {
							Common::Rect d = blitDest;
							d.translate(-destRect.left, -destRect.top);
							_drawSurface.blitFrom(_fullSurface, srcRect, d);
						}

						_needsRedraw = true;

						if (g_nancy->getGameType() <= kGameTypeNancy2) {
							// In nancy2, the presence of a hotspot relies on whether the Overlay has a scene change
							if (_enableHotspotNancy2 == kPlayOverlayWithHotspot) {
								_hotspot = _screenPosition;
								_hasHotspot = true;
							}
						} else {
							// nancy3 added a per-frame flag for hotspots. This allows the overlay to be clickable
							// even without a scene change (useful for setting flags).
							if (_blitDescriptions[blitsForThisFrame[i]].hasHotspot == kPlayOverlayWithHotspot) {
								_hotspot = _screenPosition;
								_hasHotspot = true;
							}
						}
					}
				}
			}
		}

		break;
	}
	case kActionTrigger:
		if (g_nancy->getGameType() <= kGameTypeNancy9) {
			// This isn't done by the original engine, but it's here
			// to fix Nancy1's safe lock light not turning off. Removing
			// it for Nancy 10, to fix the animated label showing correctly,
			// when using the ring at the slot machine.
			setVisible(false);
		}

		g_nancy->_sound->stopSound(_sound);

		_flagsOnTrigger.execute();
		if (_hasSceneChange == kPlayOverlaySceneChange) {
			NancySceneState.changeScene(_sceneChange);
		}

		finishExecution();

		break;
	}
}

Common::String Overlay::getRecordTypeName() const {
	if (g_nancy->getGameType() <= kGameTypeNancy1) {
		if (_isInterruptible) {
			return "PlayIntStaticBitmapAnimation";
		} else {
			return "PlayStaticBitmapAnimation";
		}
	} else {
		return "Overlay";
	}
}

void OverlayStaticTerse::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);

	Common::Rect dest, src;
	uint16 frameID = 0;

	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		// Nancy16 swapped z and transparency, inserted a frame ID, and stores the
		// source rect before the destination - the reverse of the older order.
		// 33 + 2 + 2 + 2 + 16 + 16 = 71, exact on all 3349 records in nancy18.
		_z = stream.readUint16LE();
		_transparency = stream.readUint16LE();

		// The panorama frame this overlay belongs to. Nonzero on 20 of the game's
		// 3349 records, and on all 20 it equals the node number in the record's
		// own image name (PIA_node0013B_OVL -> 13). Skipping it left every
		// overlay claiming frame 0, so a node drew all of its overlays at once,
		// each at the destination belonging to a different viewpoint.
		frameID = stream.readUint16LE();
		readRect(stream, src);
		readRect(stream, dest);
	} else {
		_transparency = stream.readUint16LE();
		_z = stream.readUint16LE();
		readRect(stream, dest);
		readRect(stream, src);
	}

	// The source rect only supplies the top-left offset into the image; the overlay
	// is blitted 1:1, so the source region takes the destination's dimensions. Using
	// the source rect's own (smaller) size here would scale the image to fit the destination.
	Common::Rect srcRect(dest.width(), dest.height());
	srcRect.moveTo(src.left, src.top);

	_srcRects.push_back(srcRect);
	_blitDescriptions.resize(1);
	_blitDescriptions[0].frameID = frameID;
	_blitDescriptions[0].src = Common::Rect(dest.width(), dest.height());
	_blitDescriptions[0].dest = dest;

	_overlayType = kPlayOverlayStatic;
}

// The {event flag, sound list} block that appears twice in a Nancy16 AR 53: once
// for the hover, once for the click. The sound settings are only stored when the
// list is not empty.
static void readRolloverBlock(Common::SeekableReadStream &stream, FlagDescription &flag, RandomSoundBlock &sounds) {
	flag.label = stream.readSint16LE();
	flag.flag = stream.readByte();
	sounds.readData(stream);
}

// Pick one of a rollover block's interchangeable sound names and start it.
static SoundDescription playRolloverSounds(const RandomSoundBlock &block) {
	SoundDescription desc;
	if (block.names.empty()) {
		return desc;
	}

	const uint idx = block.names.size() == 1 ? 0 :
		g_nancy->_randomSource->getRandomNumber(block.names.size() - 1);
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

void RolloverOverlay::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);

	_z = stream.readUint16LE();
	stream.skip(2);		// 0 in all 75 records; the frame ID slot AR 52 has here
	_cursorType = stream.readUint16LE();

	readRect(stream, _hotspot);
	readRect(stream, _srcRect);
	readRect(stream, _destRect);

	readRolloverBlock(stream, _hoverFlag, _hoverSounds);

	stream.skip(1);		// 0 in 74/75 records

	_sceneChange.sceneID = stream.readUint16LE();
	_sceneChange.frameID = stream.readUint16LE();	// 0 in all 75 records
	_sceneChange.continueSceneSound = kContinueSceneSound;
	_sceneChange.listenerFrontVector.set(0, 0, 1);

	if (_sceneChange.sceneID == kNancy16NoScene) {
		_sceneChange.sceneID = kNoScene;
	}

	readRolloverBlock(stream, _clickFlag, _clickSounds);

	// 33 + 6 + 48 + block + 5 + block, exact on all 75 records.
}

void RolloverOverlay::init() {
	g_nancy->_resource->loadImage(_imageName, _fullSurface);

	// Like the terse overlay, the source rect supplies the top-left corner and
	// the destination supplies the size; every AR 53 has them the same size
	// anyway, so this only guards against a bad rect.
	Common::Rect src(_destRect.width(), _destRect.height());
	src.moveTo(_srcRect.left, _srcRect.top);
	src.clip(Common::Rect(_fullSurface.w, _fullSurface.h));

	_drawSurface.create(_fullSurface, src);
	setTransparent(true);
	moveTo(_destRect);
	setVisible(false);

	RenderObject::init();
}

void RolloverOverlay::handleInput(NancyInput &input) {
	// Clicking is left to ActionManager's regular hotspot handling (which also
	// checks the record's dependencies); this only tracks the hover.
	const bool nowHovered = _hasHotspot &&
		NancySceneState.getViewport().convertViewportToScreen(_hotspot).contains(input.mousePos);

	if (nowHovered == _isHovered) {
		return;
	}

	_isHovered = nowHovered;
	setVisible(nowHovered);

	if (_hoverFlag.label != kEvNoEvent) {
		// The hover flag is what the paired records watch, so it has to go back
		// down when the cursor leaves - otherwise a price tag's text would stay
		// up for the rest of the scene. Several of these labels are in the
		// persistent 1041+ range, which makes leaving them set worse still.
		NancySceneState.setEventFlag(_hoverFlag.label,
			nowHovered ? _hoverFlag.flag :
				(_hoverFlag.flag == g_nancy->_true ? g_nancy->_false : g_nancy->_true));
	}

	if (nowHovered) {
		_playingSound = playRolloverSounds(_hoverSounds);
	}
}

void RolloverOverlay::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_hasHotspot = true;
		_state = kRun;
		// fall through
	case kRun:
		break;
	case kActionTrigger:
		_playingSound = playRolloverSounds(_clickSounds);
		NancySceneState.setEventFlag(_clickFlag);

		if (_sceneChange.sceneID != kNoScene) {
			setVisible(false);
			_isHovered = false;

			if (_hoverFlag.label != kEvNoEvent) {
				NancySceneState.setEventFlag(_hoverFlag.label,
					_hoverFlag.flag == g_nancy->_true ? g_nancy->_false : g_nancy->_true);
			}

			NancySceneState.changeScene(_sceneChange);
			finishExecution();
			break;
		}

		// No scene change: the rollover has to keep working. Retiring it the
		// way a one-shot hotspot normally would means the costume shop's price
		// tag stops appearing the moment it is clicked once, and the four
		// rollovers in S5210 that set no flag at all would be killed by a
		// single stray click. What the click does (its flag) has already
		// happened, and the records that watch that flag are one-shot
		// themselves, so re-arming is safe.
		_state = kRun;
		break;
	}
}

Common::String RolloverOverlay::getRecordExtraInfo() const {
	return Common::String::format("%s, scene %d", _imageName.baseName().c_str(), _sceneChange.sceneID);
}

void OverlayStaticTinted::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);

	_z = stream.readUint16LE();

	const uint16 numTints = stream.readUint16LE();
	_tints.resize(numTints);
	for (uint i = 0; i < numTints; ++i) {
		_tints[i].color = stream.readUint32LE();
		_tints[i].flagLabel = stream.readSint16LE();
	}

	stream.skip(4);		// 1 in all 21 records

	readRect(stream, _srcRect);
	readRect(stream, _destRect);

	// 33 + 2 + 2 + 6 * numTints + 4 + 32, exact on all 21 records.
}

int OverlayStaticTinted::pickTint() const {
	for (uint i = 0; i < _tints.size(); ++i) {
		if (_tints[i].flagLabel != kEvNoEvent &&
				NancySceneState.getEventFlag(_tints[i].flagLabel, g_nancy->_true)) {
			return (int)i;
		}
	}

	return -1;
}

void OverlayStaticTinted::applyTint(int tintID) {
	Common::Rect src(_destRect.width(), _destRect.height());
	src.moveTo(_srcRect.left, _srcRect.top);
	src.clip(Common::Rect(_fullSurface.w, _fullSurface.h));

	if (src.isEmpty()) {
		return;
	}

	// An owned copy, not a view into _fullSurface: the tint is destructive and
	// the same image is shared by several records.
	_drawSurface.free();
	_drawSurface.create(src.width(), src.height(), _fullSurface.format);
	_drawSurface.rawBlitFrom(_fullSurface, src, Common::Point());

	if (tintID >= 0 && tintID < (int)_tints.size() && _drawSurface.format.bytesPerPixel == 4) {
		const uint32 color = _tints[tintID].color;
		const uint32 tintR = (color >> 24) & 0xff;
		const uint32 tintG = (color >> 16) & 0xff;
		const uint32 tintB = (color >> 8) & 0xff;
		const uint32 transColor = g_nancy->_graphics->getTransColor();
		const Graphics::PixelFormat &format = _drawSurface.format;

		for (int y = 0; y < _drawSurface.h; ++y) {
			uint32 *pixel = (uint32 *)_drawSurface.getBasePtr(0, y);
			for (int x = 0; x < _drawSurface.w; ++x, ++pixel) {
				if (*pixel == transColor) {
					continue;
				}

				byte r, g, b, a;
				format.colorToARGB(*pixel, a, r, g, b);
				*pixel = format.ARGBToColor(a, (r * tintR) / 255, (g * tintG) / 255, (b * tintB) / 255);
			}
		}
	}

	setTransparent(true);
	moveTo(_destRect);
	_needsRedraw = true;
	_currentTint = tintID;
}

void OverlayStaticTinted::init() {
	g_nancy->_resource->loadImage(_imageName, _fullSurface);
	applyTint(pickTint());
	RenderObject::init();
}

void OverlayStaticTinted::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_state = kRun;
		// fall through
	case kRun:
		break;
	default:
		finishExecution();
		break;
	}
}

void OverlayStaticTinted::updateGraphics() {
	if (_state != kRun) {
		return;
	}

	// Same rule as the static Overlay: a record gated on a dependency that can
	// go back off must disappear with it.
	setVisible(_isActive);

	const int tint = pickTint();
	if (tint != _currentTint) {
		applyTint(tint);
	}
}

void OverlayAnimTerse::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);
	stream.skip(2); // VIDEO_STOP_RENDERING, VIDEO_CONTINUE_RENDERING
	_transparency = stream.readUint16LE();
	_hasSceneChange = stream.readUint16LE();
	_z = stream.readUint16LE();
	_playDirection = stream.readUint16LE();
	_loop = stream.readUint16LE();

	_sceneChange.sceneID = stream.readUint16LE();
	_sceneChange.continueSceneSound = kContinueSceneSound;
	_sceneChange.listenerFrontVector.set(0, 0, 1);
	_flagsOnTrigger.descs[0].label = stream.readSint16LE();
	_flagsOnTrigger.descs[0].flag = stream.readUint16LE();

	_firstFrame = _loopFirstFrame = stream.readUint16LE();
	_loopLastFrame = stream.readUint16LE();

	_blitDescriptions.resize(1);
	readRect(stream, _blitDescriptions[0].dest);

	readRectArray(stream, _srcRects, _loopLastFrame - _loopFirstFrame + 1);

	_overlayType = kPlayOverlayAnimated;
	_frameTime = Common::Rational(1000, 15).toInt(); // Always set to 15 fps
}

void TableIndexOverlay::readData(Common::SeekableReadStream &stream) {
	_tableIndex = stream.readUint16LE();
	Overlay::readData(stream);
}

void TableIndexOverlay::execute() {
	if (_state == kBegin) {
		Overlay::execute();
	}

	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	assert(playerTable);
	auto *tabl = GetEngineData(TABL);
	assert(tabl);

	if (_lastIndexVal != playerTable->singleValues[_tableIndex - 1]) {
		_lastIndexVal = playerTable->singleValues[_tableIndex - 1];
		_srcRects.clear();
		_srcRects.push_back(tabl->srcRects[_lastIndexVal - 1]);
		_currentViewportFrame = -1; // Force redraw
	}

	if (_state != kBegin) {
		Overlay::execute();
	}
}

void TextLineOverlay::readData(Common::SeekableReadStream &stream) {
	_fontID = stream.readUint16LE();
	_textColor = stream.readUint16LE();
	_position.x = stream.readSint16LE();
	stream.skip(2);
	_position.y = stream.readSint16LE();
	stream.skip(2);
	readFilename(stream, _textKey);
	_tableIndex = stream.readSint16LE();
}

void TextLineOverlay::execute() {
	if (_isDone) {
		return;
	}

	const Graphics::Font *font = g_nancy->_graphics->getFont(_fontID);
	if (!font) {
		return;
	}

	Common::String text;
	if (!_textKey.empty()) {
		text = _textKey;
	} else {
		TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
		assert(playerTable);

		int16 value = playerTable->getValue(_tableIndex);

		// An unset value is displayed as zero
		if (value == kNoTableValue) {
			value = 0;
		}

		text = Common::String::format("%d", value);
	}

	uint width = font->getStringWidth(text);
	uint height = font->getFontHeight();
	if (!width || !height) {
		_isDone = true;
		return;
	}

	_drawSurface.create(width, height, g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	font->drawString(&_drawSurface, text, 0, 0, width, _textColor);

	// The stored y is the baseline (bottom) of the text, so anchor the surface's
	// bottom edge there rather than its top
	moveTo(Common::Rect(_position.x, _position.y - (int16)height, _position.x + (int16)width, _position.y));
	setTransparent(true);
	setVisible(true);
	registerGraphics();

	_isDone = true;
}

} // End of namespace Action
} // End of namespace Nancy
