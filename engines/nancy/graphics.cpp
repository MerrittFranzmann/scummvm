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

#include "image/bmp.h"

#include "engines/util.h"

#include "engines/nancy/nancy.h"
#include "common/config-manager.h"
#include "common/system.h"
#include "common/tokenizer.h"

#include "engines/nancy/graphics.h"
#include "engines/nancy/renderobject.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/cursor.h"
#include "engines/nancy/state/scene.h"
#include "engines/nancy/puzzledata.h"

namespace Nancy {

GraphicsManager::GraphicsManager() :
	_objects(objectComparator),
	_inputPixelFormat16(2, 5, 5, 5, 0, 10, 5, 0, 0),
	_inputPixelFormat24(Graphics::PixelFormat::createFormatBGR24()),
	_inputPixelFormat32(Graphics::PixelFormat::createFormatBGRA32()),
	_screenPixelFormat16(2, 5, 6, 5, 0, 11, 5, 0, 0),
	_screenPixelFormat32(Graphics::PixelFormat::createFormatBGRA32()),
	_clut8Format(Graphics::PixelFormat::createFormatCLUT8()),
	_transparentPixelFormat(Graphics::PixelFormat::createFormatBGRA32()),
	_screen(640, 480, getScreenPixelFormat()),
	_isSuppressed(false){}
void GraphicsManager::init() {
	auto *bsum = GetEngineData(BSUM);
	assert(bsum);

	// Extract transparent color from the boot summary
	if (g_nancy->getGameType() == kGameTypeVampire) {
		_transColor = bsum->paletteTrans;
	} else {
		const Graphics::PixelFormat &format = getInputPixelFormat();
		_transColor = (bsum->rTrans << format.rShift) |
					  (bsum->gTrans << format.gShift) |
					  (bsum->bTrans << format.bShift);
	}

	initGraphics(640, 480, &getScreenPixelFormat());
	_screen.setTransparentColor(getTransColor());
	_screen.clear();

	// Nancy16+ dropped the OB0 chunk from BOOT along with the rest of the hardcoded UI
	// description, so there is no object0 surface to prime here.
	const ImageChunk *ob0 = (const ImageChunk *)g_nancy->getEngineData("OB0");
	assert(ob0 || g_nancy->getGameType() >= kGameTypeNancy16);

	if (ob0) {
		g_nancy->_resource->loadImage(ob0->imageName, _object0);
	}
}

void GraphicsManager::draw(bool updateScreen) {
	// Debug affordance: dump the framebuffer once, after a set number of drawn
	// frames. Screenshotting the host window needs an OS permission that a
	// headless test run does not have, and this is state-independent, so it also
	// captures states that never reach Scene::run().
	// Debug affordance: save to a slot after a set number of frames, so the
	// save/load round trip can be exercised without the GUI.
	if (ConfMan.hasKey("nancy_debug_save_after_frames")) {
		static int saveFrames = 0;
		static bool saved = false;
		if (!saved && ++saveFrames > ConfMan.getInt("nancy_debug_save_after_frames")) {
			// Keep trying rather than giving up on the first refusal. The engine
			// declines to save inside a conversation or a puzzle, and a long
			// exploratory run is very likely to be inside one at any given frame,
			// so a single attempt loses the whole run's progress.
			if (g_nancy->canSaveGameStateCurrently()) {
				saved = true;
				const Common::Error err = g_nancy->saveGameState(0, "debug round trip", false);
				warning("DEBUGSAVE slot 0: %s", err.getCode() == Common::kNoError ? "ok" : err.getDesc().c_str());
			} else {
				static int refusals = 0;
				if ((refusals++ % 300) == 0) {
					warning("DEBUGSAVE deferred: engine says it cannot save right now");
				}
			}
		}
	}

	// Debug affordance: load slot 0 back, in-session, after a set number of
	// frames. The twin of nancy_debug_save_after_frames, and not the same test:
	// booting with --save-slot restores into freshly built UI panels, while an
	// in-session load - which is what the second-chance AR 115 does - restores
	// over panels that still carry the current session's show/hide deltas.
	if (ConfMan.hasKey("nancy_debug_load_after_frames")) {
		static int loadFrames = 0;
		static bool loaded = false;
		if (!loaded && ++loadFrames > ConfMan.getInt("nancy_debug_load_after_frames")) {
			if (g_nancy->canLoadGameStateCurrently()) {
				loaded = true;
				const Common::Error err = g_nancy->loadGameState(0);
				warning("DEBUGLOAD slot 0: %s", err.getCode() == Common::kNoError ? "ok" : err.getDesc().c_str());
			}
		}
	}

	// Debug affordance: raise a list of event flags once, after a set number of
	// frames, as "label[,label...]". Several Nancy16 records are gated on a flag
	// that only a widget the engine does not draw yet can set - the death
	// screens' second-chance buttons are type 53 rollover overlays - so this is
	// how those records get exercised headlessly.
	if (ConfMan.hasKey("nancy_debug_set_flags")) {
		static int flagFrames = 0;
		static bool flagsSet = false;
		const int after = ConfMan.hasKey("nancy_debug_set_flags_after_frames") ?
			ConfMan.getInt("nancy_debug_set_flags_after_frames") : 60;

		if (!flagsSet && ++flagFrames > after && State::Scene::hasInstance() &&
				NancySceneState.getState() == State::Scene::kRun) {
			flagsSet = true;

			Common::StringTokenizer tok(ConfMan.get("nancy_debug_set_flags"), ", ");
			while (!tok.empty()) {
				const int16 label = (int16)atoi(tok.nextToken().c_str());
				warning("DEBUGSETFLAG %d", label);
				NancySceneState.setEventFlag(label, g_nancy->_true);
			}
		}
	}

	// Debug affordances that seed the rest of the player state, so a headless run
	// can start in the middle of the game instead of replaying from the top.
	// Flags alone are not enough: most mid-game hotspots are gated on inventory,
	// and the shops are gated on the player-value table, so a run that injected
	// only flags would see a mostly-dead scene and report blockers that are just
	// missing state. These fire on the same schedule as nancy_debug_set_flags.
	//
	//   nancy_debug_set_items  = "13,14,18"     inventory item ids
	//   nancy_debug_set_values = "5=200,38=50"  player table index=value
	if (ConfMan.hasKey("nancy_debug_set_items") || ConfMan.hasKey("nancy_debug_set_values")) {
		static int stateFrames = 0;
		static bool stateSet = false;
		const int after = ConfMan.hasKey("nancy_debug_set_flags_after_frames") ?
			ConfMan.getInt("nancy_debug_set_flags_after_frames") : 60;

		if (!stateSet && ++stateFrames > after && State::Scene::hasInstance() &&
				NancySceneState.getState() == State::Scene::kRun) {
			stateSet = true;

			if (ConfMan.hasKey("nancy_debug_set_items")) {
				Common::StringTokenizer tok(ConfMan.get("nancy_debug_set_items"), ", ");
				while (!tok.empty()) {
					const int16 id = (int16)atoi(tok.nextToken().c_str());
					warning("DEBUGSETITEM %d", id);
					NancySceneState.addItemToInventory(id);
				}
			}

			if (ConfMan.hasKey("nancy_debug_set_values")) {
				auto *table = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
				Common::StringTokenizer tok(ConfMan.get("nancy_debug_set_values"), ", ");
				while (!tok.empty()) {
					const Common::String pair = tok.nextToken();
					const uint eq = pair.findFirstOf('=');
					if (eq == Common::String::npos || !table) {
						continue;
					}

					const uint16 index = (uint16)atoi(pair.substr(0, eq).c_str());
					const int16 value = (int16)atoi(pair.substr(eq + 1).c_str());
					if (index < table->getNumSingleValues()) {
						warning("DEBUGSETVALUE %u=%d", index, value);
						table->setSingleValue(index, value);
					}
				}
			}
		}
	}

	// Debug affordance: print the scene's state fingerprint once, after a set
	// number of frames. Reading back what a load actually restored otherwise
	// means driving the console by hand, which a headless run cannot do.
	if (ConfMan.hasKey("nancy_debug_state_after_frames")) {
		static int stateFrames = 0;
		static bool dumped = false;
		if (!dumped && ++stateFrames > ConfMan.getInt("nancy_debug_state_after_frames") &&
				State::Scene::hasInstance()) {
			dumped = true;
			warning("DEBUGSTATE %s", NancySceneState.getStateFingerprint().c_str());
			warning("DEBUGFLAGS %s", NancySceneState.debugAllEventFlags().c_str());
		}
	}

	if (ConfMan.hasKey("nancy_screenshot_after_frames")) {
		// Several shots, spaced out, rather than one. The OpenGL backend often
		// hands back a blank buffer for a given frame, so a single capture is not
		// trustworthy evidence of what was drawn - pick the non-blank one.
		static int frames = 0;
		static int taken = 0;
		const int first = ConfMan.getInt("nancy_screenshot_after_frames");
		const int count = ConfMan.hasKey("nancy_screenshot_count") ?
			ConfMan.getInt("nancy_screenshot_count") : 1;

		// nancy_screenshot_every overrides the 150-frame spacing. At 30 fps that
		// is five seconds a shot, which is coarser than most things worth
		// watching change: a narration playlist swaps its caption several times
		// inside one interval, so the ghosting this file's dirty-rect reset fixes
		// could not be captured frame by frame without it.
		const int every = ConfMan.hasKey("nancy_screenshot_every") ?
			MAX(1, ConfMan.getInt("nancy_screenshot_every")) : 150;

		++frames;
		if (frames >= first && taken < count && (frames - first) % every == 0) {
			++taken;
			g_system->saveScreenshot();
		}
	}

	if (_isSuppressed && updateScreen) {
		_isSuppressed = false;
		return;
	}

	g_nancy->_cursor->applyCursor();

	// Update graphics for all RenderObjects and determine
	// the areas of the screen that need to be redrawn
	for (auto it : _objects) {
		RenderObject &current = *it;

		current.updateGraphics();

		if (current.needsRedraw()) {
			if (current.isVisible()) {
				if (current.hasMoved() && !current.getPreviousScreenPosition().isEmpty()) {
					// Object moved to a new location on screen, update the previous one
					_dirtyRects.push_back(current.getPreviousScreenPosition());
				}

				// Redraw the current location
				_dirtyRects.push_back(current.getScreenPosition());
			} else if (!current.getPreviousScreenPosition().isEmpty()) {
				// Object just turned invisible, redraw the last location
				_dirtyRects.push_back(current.getPreviousScreenPosition());
			}
		}

		current.setNeedsRedraw(false);
		current.setHasMoved(false);
		current.updatePreviousScreenPosition();
	}

	// Filter out dirty rects that are completely inside others to reduce overdraw
	for (auto outer = _dirtyRects.begin(); outer != _dirtyRects.end(); ++outer) {
		for (auto inner = _dirtyRects.begin(); inner != _dirtyRects.end(); ++inner) {
			if (inner != outer && (*outer).contains(*inner)) {
				_dirtyRects.erase(inner);
				break;
			}
		}
	}

	// Reset every dirty rect to the screen's base colour before recompositing it.
	//
	// The loop below rebuilds a dirty rect by blitting every object that
	// intersects it, bottom-up - but it never resets the pixels first, so the
	// rect is only truly rebuilt where some object paints opaquely. Anywhere the
	// stack is transparent (or absent), last frame's pixels survive underneath
	// the new ones. That is invisible in Nancy1-15, where a full-screen opaque
	// `Scene::_frame` sits at the bottom of every stack and repaints the base
	// layer for free; Nancy16 dropped the hardcoded UI chunks that built it (see
	// GraphicsManager::init - there is no OB0 to prime), so the strip of black
	// matte between the taskbar icon groups is painted by nothing at all. A
	// transparent panel over it - the LOWERMATTE caption band - therefore
	// accumulated every caption a narration playlist had ever shown.
	//
	// Restoring the missing base layer here fixes it for any transparent object
	// whose contents change in place, rather than per caller. Done as its own
	// pass because dirty rects may partially overlap, and filling one after
	// another had been composed would erase it.
	//
	// The fill is *opaque* black rather than the plain 0 the screen is cleared to
	// in init(), and that matters for translucent content. `_screen` is a
	// compositing destination, and ManagedSurface::blitFrom blends a partially
	// transparent source pixel against whatever it lands on - blitFromInner,
	// managed_surface.cpp, "Partially transparent, so calculate new pixel colors".
	// That blend has two branches keyed on the destination's own alpha:
	//
	//   aDest == 0xff  a true src-over, dst = src*a + dst*(1-a).
	//   otherwise      the "translucent target" branch, which divides the weights
	//                  back out: with aDest == 0 it collapses to dst = src, and
	//                  records the transparency only in the alpha channel - which
	//                  nothing downstream reads, since Screen::update hands the
	//                  buffer to the backend and the backend ignores it.
	//
	// Filling with 0 put every pixel that no opaque object covers into the second
	// branch, so anything translucent drawn over bare screen came out at full
	// strength. That is what made the nine translucent NDUI taskbar/HUD widgets
	// (textureColors[0] = 0x5affffff and 0x80ffffff) draw about three times too
	// bright: the top matte and the strip between the taskbar icon groups are
	// painted by nothing at all in Nancy16+, exactly where those widgets sit.
	//
	// Opaque black is the right base because black is what the game shows where
	// nothing is drawn, and it is visually identical for everything that was
	// already correct: an opaque source pixel is still a straight copy, a fully
	// transparent one is still skipped, and 0x00000000 and 0xff000000 present the
	// same to the backend.
	//
	// The colour is the game's own "Matte Color" setting rather than a constant;
	// black is only its default. setMatteColour() keeps the alpha at 0xff, which
	// is what the paragraph above depends on.
	const uint32 baseColor = _screen.format.ARGBToColor(0xff,
		(_matteColour >> 16) & 0xff, (_matteColour >> 8) & 0xff, _matteColour & 0xff);
	for (Common::Rect rect : _dirtyRects) {
		_screen.fillRect(rect, baseColor);
	}

	// Perform the actual drawing. This checks for cases where something would be fully obscured,
	// and skips them (e.g. redrawing the Viewport won't also redraw the background)
	for (Common::Rect rect : _dirtyRects) {
		for (RenderObject **it = _objects.begin(); it < _objects.end(); ++it) {
			RenderObject &current = **it;

			if (!current.isVisible() || current.getScreenPosition().isEmpty()) {
				continue;
			}

			bool shouldSkip = false;

			Common::Rect intersection = rect.findIntersectingRect(current.getScreenPosition());
			if (!intersection.isEmpty()) {
				// Found an intersecting RenderObject. Loop through the following
				// RenderObjects, and see if we have another that fully obscures the intersection
				for (auto it2 = it + 1; it2 < _objects.end(); ++it2) {
					RenderObject &other = **it2;

					if (!other.isVisible() || other.getScreenPosition().isEmpty()) {
						continue;
					}

					Common::Rect intersection2 = intersection.findIntersectingRect(other.getScreenPosition());
					if (intersection == intersection2) {
						// The entire area that would be drawn is obscured by another RenderObject.
						// If the obscuring RenderObject is not transparent, we skip drawing current

						if (!other._drawSurface.hasTransparentColor() && other._drawSurface.format != _transparentPixelFormat) {
							// No transparency, skip current
							shouldSkip = true;
							break;
						}
					}
				}

				if (shouldSkip) {
					continue;
				}

				blitToScreen(current, rect.findIntersectingRect(current.getScreenPosition()));
			}
		}
	}

	// Draw the screen
	if (updateScreen) {
		_screen.update();
	}

	// Remove all dirty rects for the next frame
	_dirtyRects.clear();
}

void GraphicsManager::loadFonts(Common::SeekableReadStream *chunkStream) {
	auto *bsum = GetEngineData(BSUM);
	assert(bsum);
	assert(chunkStream);

	chunkStream->seek(0);
	_fonts.resize(bsum->numFonts);
	for (uint i = 0; i < _fonts.size(); ++i) {
		_fonts[i].read(*chunkStream);
	}

	delete chunkStream;
}

void GraphicsManager::addObject(RenderObject *object) {
	for (auto &r : _objects) {
		if (r == object) {
			// Erase and re-add objects already in the array to make sure
			// any changes in the z depth are reflected correctly
			_objects.erase(&r);
		}
	}

	_objects.insert(object);
}

void GraphicsManager::removeObject(RenderObject *object) {
	for (auto &r : _objects) {
		if (r == object) {
			// Make sure the object gets properly cleared
			_dirtyRects.push_back(r->getPreviousScreenPosition());
			_objects.erase(&r);
			break;
		}
	}
}

void GraphicsManager::clearObjects() {
	_objects.clear();
}

void GraphicsManager::redrawAll() {
	for (auto &obj : _objects) {
		obj->setNeedsRedraw(true);
	}
}

void GraphicsManager::suppressNextDraw() {
	_isSuppressed = true;
}

void GraphicsManager::loadSurfacePalette(Graphics::ManagedSurface &inSurf, const Common::Path &paletteFilename, uint paletteStart, uint paletteSize) {
	Common::File f;
	if (f.open(paletteFilename.append(".bmp"))) {
		Image::BitmapDecoder dec;
		if (dec.loadStream(f)) {
			inSurf.setPalette(dec.getPalette().data(), paletteStart, paletteSize);
		}
	}
}

void GraphicsManager::copyToManaged(const Graphics::Surface &src, Graphics::ManagedSurface &dst, bool verticalFlip, bool doubleSize) {
	if (dst.w != (doubleSize ? src.w * 2 : src.w) || dst.h != (doubleSize ? src.h * 2 : src.h)) {
		uint8 palette[256 * 3];
		bool hasPalette = dst.hasPalette();
		bool hasTransColor = dst.hasTransparentColor();

		if (hasPalette && g_nancy->getGameType() == kGameTypeVampire) {
			dst.grabPalette(palette, 0, 256);
		}

		dst.create(doubleSize ? src.w * 2 : src.w, doubleSize ? src.h * 2 : src.h, src.format);

		if (hasPalette && g_nancy->getGameType() == kGameTypeVampire) {
			dst.setPalette(palette, 0, 256);
		}

		if (hasTransColor) {
			// Do the same trick with the transparent color
			dst.setTransparentColor(dst.getTransparentColor());
		}
	}

	if (!verticalFlip && !doubleSize) {
		dst.copyRectToSurface(src, 0, 0, Common::Rect(0, 0, src.w, src.h));
		return;
	}

	for (int y = 0; y < src.h; ++y) {
		if (!doubleSize) {
			// Copy single line bottom to top
			memcpy(dst.getBasePtr(0, y), src.getBasePtr(0, src.h - y - 1), src.w * src.format.bytesPerPixel);
		} else {
			// Make four copies of each source pixel
			for (int x = 0; x < src.w; ++x) {
				switch (src.format.bytesPerPixel) {
				case 1: {
					const byte *srcP = (const byte *)src.getBasePtr(x, y);
					uint dstX = x * 2;
					uint dstY = verticalFlip ? (src.h - y - 1) * 2 : src.h - y - 1;
					byte *dstP = ((byte *)dst.getBasePtr(dstX, dstY));
					*dstP = *srcP;
					*(dstP + 1) = *srcP;
					dstP += dst.w;
					*dstP = *srcP;
					*(dstP + 1) = *srcP;
					break;
				}
				case 2: {
					const uint16 *srcP = (const uint16 *)src.getBasePtr(x, y);
					uint dstX = x * 2;
					uint dstY = verticalFlip ? (src.h - y - 1) * 2 : src.h - y - 1;
					uint16 *dstP = ((uint16 *)dst.getBasePtr(dstX, dstY));
					*dstP = *srcP;
					*(dstP + 1) = *srcP;
					dstP += dst.w;
					*dstP = *srcP;
					*(dstP + 1) = *srcP;
					break;
				}
				case 4: {
					const uint32 *srcP = (const uint32 *)src.getBasePtr(x, y);
					uint dstX = x * 2;
					uint dstY = verticalFlip ? (src.h - y - 1) * 2 : src.h - y - 1;
					uint32 *dstP = ((uint32 *)dst.getBasePtr(dstX, dstY));
					*dstP = *srcP;
					*(dstP + 1) = *srcP;
					dstP += dst.w;
					*dstP = *srcP;
					*(dstP + 1) = *srcP;
					break;
				}
				default:
					return;
				}
			}
		}
	}
}

void GraphicsManager::copyToManaged(void *src, Graphics::ManagedSurface &dst, uint srcW, uint srcH, const Graphics::PixelFormat &format, bool verticalFlip, bool doubleSize) {
	// Do things the lazy way and simply create a Surface and pass it to the other overload
	// We do NOT free the surface since it's a temporary object and does not own the pixels
	Graphics::Surface surf;
	surf.w = srcW;
	surf.h = srcH;
	surf.format = format;
	surf.pitch = srcW * format.bytesPerPixel;
	surf.setPixels(src);

	copyToManaged(surf, dst, verticalFlip, doubleSize);
}

// Custom rotation code since Surface::rotoscale() produces incorrect results
// Only works on 16 bit surfaces and ignores transparency
// Rotation is a value between 0 and 3, corresponding to 0, 90, 180, or 270 degrees clockwise
void GraphicsManager::rotateBlit(const Graphics::ManagedSurface &src, Graphics::ManagedSurface &dest, byte rotation) {
	assert(!src.empty() && !dest.empty());
	assert(rotation <= 3);
	assert(src.format.bytesPerPixel == 2 && dest.format.bytesPerPixel == 2);

	uint srcW = src.w;
	uint srcH = src.h;
	const uint16 *s, *e;

	if (rotation % 2) {
		if (src.h != dest.w || src.w != dest.h) {
			// Dest surface is wrong size, destroy it and create an appropriate one
			dest.create(src.h, src.w, src.format);
		}
	} else {
		if (src.w != dest.w || src.h != dest.h) {
			// Dest surface is wrong size, destroy it and create an appropriate one
			dest.create(src.w, src.h, src.format);
		}
	}

	switch (rotation) {
	case 0 :
		// No rotation, just blit
		dest.rawBlitFrom(src, src.getBounds(), Common::Point());
		return;
	case 2 : {
		// 180 degrees
		uint16 *d;
		for (uint y = 0; y < srcH; ++y) {
			s = (const uint16 *)src.getBasePtr(0, y);
			e = (const uint16 *)src.getBasePtr(srcW, y);
			d = (uint16 *)dest.getBasePtr(srcW - 1, srcH - y - 1);
			for (; s < e; ++s, --d) {
				*d = *s;
			}
		}

		break;
	}
	case 1 :
		// 90 degrees
		for (uint y = 0; y < srcH; ++y) {
			s = (const uint16 *)src.getBasePtr(0, y);
			for (uint x = 0; x < srcW; ++x, ++s) {
				*((uint16 *)dest.getBasePtr(srcH - y - 1, x)) = *s;
			}
		}

		break;
	case 3 :
		// 270 degrees
		for (uint y = 0; y < srcH; ++y) {
			s = (const uint16 *)src.getBasePtr(0, y);
			for (uint x = 0; x < srcW; ++x, ++s) {
				*((uint16 *)dest.getBasePtr(y, srcW - x - 1)) = *s;
			}
		}

		break;
	}
}

void GraphicsManager::crossDissolve(const Graphics::ManagedSurface &from, const Graphics::ManagedSurface &to, byte alpha, const Common::Rect &rect, Graphics::ManagedSurface &inResult) {
	assert(from.getBounds() == to.getBounds());
	inResult.blitFrom(from, rect, Common::Point());
	inResult.transBlitFrom(to, rect, Common::Point(), (uint32)-1, false, alpha);
}

void GraphicsManager::debugDrawToScreen(const Graphics::ManagedSurface &surf) {
	_screen.blitFrom(surf, Common::Point());
	_screen.update();
}

const Graphics::PixelFormat &GraphicsManager::getInputPixelFormat(uint bpp) {
	if (g_nancy->getGameType() == kGameTypeVampire)
		return _clut8Format;

	switch (bpp) {
	case 0:
		return g_nancy->getGameType() >= kGameTypeNancy13 ? _inputPixelFormat32 : _inputPixelFormat16;
	case 16:
		return _inputPixelFormat16;	// RGB555
	case 24:
		return _inputPixelFormat24;
	case 32:
		return _inputPixelFormat32;
	default:
		error("Unsupported input pixel format with bpp %d", bpp);
	}
}

const Graphics::PixelFormat &GraphicsManager::getScreenPixelFormat() {
	return (g_nancy->getGameType() >= kGameTypeNancy13) ? _screenPixelFormat32 : _screenPixelFormat16;
}

const Graphics::PixelFormat &GraphicsManager::getTransparentPixelFormat() {
	return _transparentPixelFormat;
}

void GraphicsManager::grabViewportObjects(Common::Array<RenderObject *> &inArray) {
	// Add the viewport
	inArray.push_back(&(RenderObject &)NancySceneState.getViewport());

	// Add all viewport-relative (non-UI) objects
	for (RenderObject *obj : _objects) {
		if (obj->isViewportRelative()) {
			inArray.push_back(obj);
		}
	}
}

void GraphicsManager::screenshotScreen(Graphics::ManagedSurface &inSurf) {
	draw(false);
	inSurf.free();
	inSurf.copyFrom(_screen);
}

// Draw a given screen-space rectangle to the screen
void GraphicsManager::blitToScreen(const RenderObject &src, Common::Rect screenRect) {
	_screen.blitFrom(src._drawSurface, src._drawSurface.getBounds().findIntersectingRect(src.convertToLocal(screenRect)), screenRect);
}

int GraphicsManager::objectComparator(const void *a, const void *b) {
	if (((const RenderObject*)a)->getZOrder() < ((const RenderObject*)b)->getZOrder()) {
		return -1;
	} else if (((const RenderObject*)a)->getZOrder() > ((const RenderObject*)b)->getZOrder()) {
		return 1;
	} else {
		return 0;
	}
}

} // End of namespace Nancy
