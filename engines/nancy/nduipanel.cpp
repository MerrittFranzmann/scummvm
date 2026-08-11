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

#include "engines/nancy/nduipanel.h"
#include "engines/nancy/util.h"
#include "engines/nancy/ndui.h"
#include "engines/nancy/enginedata.h"
#include "engines/nancy/nancy.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/graphics.h"

#include "graphics/managed_surface.h"
#include "graphics/font.h"
#include "graphics/surface.h"

#include "engines/metaengine.h"
#include "engines/savestate.h"

#include "common/config-manager.h"

#include "engines/nancy/input.h"
#include "engines/nancy/cursor.h"
#include "engines/nancy/puzzledata.h"
#include "engines/nancy/state/scene.h"

namespace Nancy {

// A Var::Static (class 14) has no caption of its own; it displays a live number
// pulled out of the game state. Which number is named by a binding action:
// event 18 (VALUE_CHANGED), command 12 (INVOKE), target "Engine_Index", with the
// index in paramID. Nancy18 authors exactly two of these bindings and both are
// player-table indices that the action records already write - CoinPurse is
// index 5 (Nancy's money, seeded 200 by S1) and the dance puzzle's DanceMeter
// slider is index 38 - so "Engine_Index paramID N" is TableData index N.
//
// Returns false for a control with no such binding, which is every other class.
static bool nduiBoundTableIndex(const NDUIControl &control, uint16 &outIndex) {
	for (uint i = 0; i < control.actions.size(); ++i) {
		const NDUIAction &action = control.actions[i];
		if (action.paramID != NDUIAction::kNoParamID && action.paramID >= 0 &&
				action.target.equalsIgnoreCase("Engine_Index")) {
			outIndex = (uint16)action.paramID;
			return true;
		}
	}

	return false;
}

// Reads the value a Var::Static is bound to. False when the control carries no
// binding or the player table has not been created yet.
static bool nduiBoundValue(const NDUIControl &control, int16 &outValue) {
	uint16 index = 0;
	if (!nduiBoundTableIndex(control, index)) {
		return false;
	}

	if (!State::Scene::hasInstance()) {
		return false;
	}

	auto *table = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	if (!table) {
		return false;
	}

	outValue = table->getValue(index);

	// A slot nothing has written yet reads back the engine's kNoTableValue
	// sentinel. In normal play the purse is only shown after S3552, well after
	// S1 has set index 5, but a debug boot straight into a later scene would
	// otherwise put a literal "9999" in the purse.
	return outValue != kNoTableValue;
}

// --- Element texture modulation ----------------------------------------
//
// Every NDUIElement carries four ARGB values in `textureColors`, one per control
// state. Ndui.dll is a DXUT derivative, and DXUT's CDXUTDialog::DrawSprite hands
// the current state's colour straight to ID3DXSprite::Draw, which multiplies the
// texture by it. The colour is therefore not decoration: it is how the author
// dims a widget. Nine taskbar/HUD widgets are authored translucent white -
// alpha 0x5a on eight of them and 0x80 on ShowOptions - and this port blitted
// the atlas raw, so they drew at 1/0.353 ~= 2.8x their intended strength.
//
// Only the alpha channel is applied. The RGB channels are deliberately left
// alone; see the note over kNDUIModulateRGB below for exactly what that skips
// and why.
//
// This has to happen per element rather than at the panel-composite step: one
// panel's elements do not agree on a value (LOWERMATTE mixes 0x5a buttons with
// an opaque Static and an 0x80 ShowOptions), so there is no single alpha a
// whole-panel blend could use. Multiplying it into the element's own pixels puts
// the modulation exactly where the data puts it, and the alpha then rides the
// surface's own alpha channel through the downscale and out to the screen.
//
// That works because ManagedSurface::blitFrom is not the pure colour-key copy it
// looks like. It only takes the colour-key path when the *source* has a
// transparent colour set (managed_surface.cpp:571); none of the three surfaces
// on this path does, so all three hops run blitFromInner, which splits any pixel
// that is neither fully opaque nor fully transparent and blends it against the
// destination. On the two intermediate hops the destination is cleared to 0, and
// blitFromInner's "translucent target" branch divides the weights back out, so
// 0x5a arrives at the panel surface unchanged. The final hop onto GraphicsManager
//::_screen is where it is actually resolved - which is why that destination has
// to be opaque; see the fill in GraphicsManager::draw.
//
// blendBlitFrom would be the obvious alternative and is not usable here:
// BlendBlit::getSupportedPixelFormat is RGBA-in-a-uint32 while this engine works
// in BGRA32, so every call would take the "only accepts RGBA32!" path and draw
// nothing.

// Whether to honour the RGB channels of textureColors as well as the alpha.
//
// Off, on purpose. In nancy18 twenty-seven widgets with artwork carry an opaque
// but non-white texture colour: twenty-six at 0xff4b7880 (every dialog's Close
// button, the Load/Save action buttons, the cheat console's tabs) and
// PhoneBookEntry at 0xffc8c8c8. Honouring RGB would darken all of them by about
// half, and unlike the alpha case there is nothing to check that against - none
// of those dialogs is open in any frame of the reference recording, so the change
// would be asserted rather than measured. The alpha case is measured: the
// paper-doll box goes 56.97 -> 20.14 against a reference of 18.84 while the
// opaque coin purse beside it stays put.
static const bool kNDUIModulateRGB = false;

static const uint32 kNDUINoModulation = 0xffffffff;

// Multiplies `rect` of `surf` by `argb`, component-wise. `surf` is always the
// caller's own freshly decoded copy of an atlas - ResourceManager::loadImage
// copyFrom()s into the caller's surface and keeps no cache - so this never
// touches shared pixels.
static void modulateSurface(Graphics::ManagedSurface &surf, const Common::Rect &rect, uint32 argb) {
	const uint32 mulA = (argb >> 24) & 0xff;
	const uint32 mulR = (argb >> 16) & 0xff;
	const uint32 mulG = (argb >> 8) & 0xff;
	const uint32 mulB = argb & 0xff;

	for (int y = rect.top; y < rect.bottom; ++y) {
		uint32 *p = (uint32 *)surf.getBasePtr(rect.left, y);
		for (int x = rect.left; x < rect.right; ++x, ++p) {
			byte a, r, g, b;
			surf.format.colorToARGB(*p, a, r, g, b);

			// A pixel the atlas already calls transparent stays transparent;
			// scaling zero by anything is still zero, and skipping it keeps the
			// soft edges of the round buttons exactly as authored.
			if (a == 0) {
				continue;
			}

			*p = surf.format.ARGBToColor((a * mulA) / 255, (r * mulR) / 255,
										 (g * mulG) / 255, (b * mulB) / 255);
		}
	}
}

// The UI is authored at 800x600; the engine renders 640x480.
static const int kNDUIScreenNum = 4;
static const int kNDUIScreenDen = 5;

// Authored coordinate -> output coordinate. Everything the file describes is in
// the 800x600 space; the panel surface, the fonts and the hit-test rects all
// live at output scale.
static int toScreen(int authored) {
	return (authored * kNDUIScreenNum) / kNDUIScreenDen;
}

static Common::Rect toScreen(const Common::Rect &authored) {
	return Common::Rect(toScreen(authored.left), toScreen(authored.top),
						toScreen(authored.right), toScreen(authored.bottom));
}

// Output coordinate -> the smallest authored coordinate that reaches it. The
// ceiling matters: this exists so a scroll thumb computed in output pixels can
// be turned back into a crop of its authored sprite, and toScreen(fromScreen(s))
// == s exactly (the standard inverse of a monotone floor), so the thumb that is
// painted is the thumb the hit test was built from. Rounding down instead would
// lose a pixel off the bottom of the thumb and put the paint and the hit rect a
// pixel apart - which is the whole class of bug this widget is fixing.
static int fromScreen(int screen) {
	return (screen * kNDUIScreenDen + kNDUIScreenNum - 1) / kNDUIScreenNum;
}

// DXUT's SCROLLBAR_MINTHUMBSIZE, 8 authored pixels, in output pixels. A thumb
// for a very long list would otherwise round to nothing and leave the player
// with a track that looks empty and cannot be grabbed.
static const int kMinThumbScreen = 6;

// Collapses the authored-size artwork onto the output-size surface by giving
// every source pixel its share of every destination pixel it covers.
//
// A scaling ManagedSurface::blitFrom is a *point* sample, and 4/5 point sampling
// reads only four source columns in five: floor(d * 5/4) never lands on a column
// congruent to 4 (mod 5). A feature exactly one pixel wide therefore survives or
// disappears according to nothing but where it sits mod 5 - and the taskbar
// buttons are exactly that case. Every button sprite in UI_Buttons_Source is a
// 42x42 cell whose columns 1 and 40 are the teal frame (measured: rgb 88,146,145
// at alpha ~246, against a ~34,80,82 interior), and the two groups sit at
// different phases, because LOWERMATTE[3] flows from padding-left 10 while
// LOWERMATTE[4] flows from padding-right 10 in reverse:
//
//   left  group sprite origins  10,  60, 110, 160  -> frame column at 11, 61, ...
//   right group sprite origins  18,  68, 118, 168  -> frame column at 19, 69, ...
//
// 11 mod 5 = 1, kept. 19 mod 5 = 4, dropped. That is the whole of the bug: the
// gear, load, save and quit buttons lost their left edge while the bag, journal
// and tasklist buttons kept theirs, from one sampling rule and two padding
// values, with no difference in the artwork at all.
//
// Averaged in premultiplied alpha: straight per-channel averaging of RGBA drags
// the (meaningless) colour of fully transparent pixels into their neighbours,
// which would fringe every soft-edged sprite in the UI.
// Redraw the one-pixel frame lines the downscale is mathematically obliged to
// lose.
//
// The UI is authored at 800x600 and the screen is 640x480, so an authored pixel
// is 0.8 of a screen pixel and a 1px line cannot land on a pixel boundary. The
// area-weighted collapse above is CORRECT - it spreads that line over two
// destination pixels at partial strength - and the result is a frame that fades
// out. Measured on the conversation container (Convo_Source 1,1-459,109), whose
// art is a double frame: authored x=1..2 an opaque teal outer line, x=3..12 a
// fully transparent ring, x=13 a ONE PIXEL opaque teal inner line, then the
// alpha-128 interior. After the collapse the outer line survives because it is
// two pixels wide, and the inner line goes from a crisp (75,119,127) to a
// smeared (68,95,93) spread across two pixels - which on screen reads as the
// inner box having no outline at all.
//
// So the lines are found in the AUTHORED art, where they are still exact, and
// re-stroked on the output at the rounded position. Deliberately narrow:
//
//   * only fully opaque source pixels count, and only where BOTH neighbours are
//     not opaque - that is a line, not the edge of a filled region, so nothing
//     with a soft or anti-aliased boundary is touched;
//   * only lines spanning most of the panel qualify (kSpan), which is what makes
//     this a frame rather than a coincidence of interior detail;
//   * the stroke reuses the source pixel's own colour, so nothing is invented.
//
// Text is unaffected: it is drawn after this, at output scale, by the loop below.
static void restoreThinFrames(const Graphics::ManagedSurface &src, Graphics::ManagedSurface &dst) {
	const int sw = src.w, sh = src.h, dw = dst.w, dh = dst.h;
	if (sw <= 2 || sh <= 2 || dw <= 0 || dh <= 0 || (sw == dw && sh == dh)) {
		return;
	}

	// A frame line has to run most of the way across to be a frame.
	const int kSpanNum = 3, kSpanDen = 4;

	const Graphics::PixelFormat &sf = src.format;
	const Graphics::PixelFormat &df = dst.format;

	auto alphaAt = [&](int x, int y) -> byte {
		byte a, r, g, b;
		sf.colorToARGB(*(const uint32 *)src.getBasePtr(x, y), a, r, g, b);
		return a;
	};

	// Vertical lines: one opaque column between two that are not.
	for (int x = 1; x < sw - 1; ++x) {
		int run = 0, firstY = -1, lastY = -1;
		for (int y = 0; y < sh; ++y) {
			if (alphaAt(x, y) == 0xff && alphaAt(x - 1, y) != 0xff && alphaAt(x + 1, y) != 0xff) {
				++run;
				if (firstY < 0) {
					firstY = y;
				}
				lastY = y;
			}
		}

		if (run * kSpanDen < sh * kSpanNum) {
			continue;
		}

		const int dx = CLIP((x * dw + dw / 2) / sw, 0, dw - 1);
		for (int y = firstY; y <= lastY; ++y) {
			if (alphaAt(x, y) != 0xff) {
				continue;
			}

			byte a, r, g, b;
			sf.colorToARGB(*(const uint32 *)src.getBasePtr(x, y), a, r, g, b);
			const int dy = CLIP((y * dh + dh / 2) / sh, 0, dh - 1);
			*(uint32 *)dst.getBasePtr(dx, dy) = df.ARGBToColor(a, r, g, b);
		}
	}

	// Horizontal lines: one opaque row between two that are not.
	for (int y = 1; y < sh - 1; ++y) {
		int run = 0, firstX = -1, lastX = -1;
		for (int x = 0; x < sw; ++x) {
			if (alphaAt(x, y) == 0xff && alphaAt(x, y - 1) != 0xff && alphaAt(x, y + 1) != 0xff) {
				++run;
				if (firstX < 0) {
					firstX = x;
				}
				lastX = x;
			}
		}

		if (run * kSpanDen < sw * kSpanNum) {
			continue;
		}

		const int dy = CLIP((y * dh + dh / 2) / sh, 0, dh - 1);
		for (int x = firstX; x <= lastX; ++x) {
			if (alphaAt(x, y) != 0xff) {
				continue;
			}

			byte a, r, g, b;
			sf.colorToARGB(*(const uint32 *)src.getBasePtr(x, y), a, r, g, b);
			const int dx = CLIP((x * dw + dw / 2) / sw, 0, dw - 1);
			*(uint32 *)dst.getBasePtr(dx, dy) = df.ARGBToColor(a, r, g, b);
		}
	}
}

static void downscaleAuthoredArt(const Graphics::ManagedSurface &src, Graphics::ManagedSurface &dst) {
	const int sw = src.w, sh = src.h;
	const int dw = dst.w, dh = dst.h;

	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
		return;
	}

	// Nothing to resample, and nothing to lose by not resampling.
	if (sw == dw && sh == dh) {
		dst.blitFrom(src, Common::Rect(0, 0, sw, sh), Common::Point(0, 0));
		return;
	}

	// Both grids are expressed in a common unit of 1/(sw*dw) of the width, so a
	// source pixel is `dw` units wide and a destination pixel `sw`; the overlap
	// of the two is the weight, and no rounding happens until the final divide.
	for (int dy = 0; dy < dh; ++dy) {
		const int sy0 = (dy * sh) / dh;
		const int sy1 = MIN(sh - 1, ((dy + 1) * sh - 1) / dh);
		byte *outRow = (byte *)dst.getBasePtr(0, dy);

		for (int dx = 0; dx < dw; ++dx) {
			const int sx0 = (dx * sw) / dw;
			const int sx1 = MIN(sw - 1, ((dx + 1) * sw - 1) / dw);

			uint64 weightSum = 0, aAcc = 0, rAcc = 0, gAcc = 0, bAcc = 0;

			for (int sy = sy0; sy <= sy1; ++sy) {
				const uint64 wy = (uint64)(MIN((sy + 1) * dh, (dy + 1) * sh) - MAX(sy * dh, dy * sh));
				const uint32 *in = (const uint32 *)src.getBasePtr(sx0, sy);

				for (int sx = sx0; sx <= sx1; ++sx, ++in) {
					const uint64 wx = (uint64)(MIN((sx + 1) * dw, (dx + 1) * sw) - MAX(sx * dw, dx * sw));
					const uint64 w = wx * wy;

					byte a, r, g, b;
					src.format.colorToARGB(*in, a, r, g, b);

					const uint64 wa = w * a;
					weightSum += w;
					aAcc += wa;
					rAcc += wa * r;
					gAcc += wa * g;
					bAcc += wa * b;
				}
			}

			byte a = 0, r = 0, g = 0, b = 0;
			if (weightSum) {
				a = (byte)((aAcc + weightSum / 2) / weightSum);
			}

			// Un-premultiply. aAcc is the sum of the weights that carried any
			// colour at all, so a fully transparent neighbourhood contributes
			// nothing and leaves the pixel at zero rather than dividing by zero.
			if (aAcc) {
				r = (byte)MIN<uint64>(255, (rAcc + aAcc / 2) / aAcc);
				g = (byte)MIN<uint64>(255, (gAcc + aAcc / 2) / aAcc);
				b = (byte)MIN<uint64>(255, (bAcc + aAcc / 2) / aAcc);
			}

			*((uint32 *)outRow + dx) = dst.format.ARGBToColor(a, r, g, b);
		}
	}
}

NDUIPanel::~NDUIPanel() {
	delete _chunk;
	delete _rowTemplate;
}

void NDUIPanel::init(NDUI *chunk) {
	if (_chunk != chunk) {
		// _barDrag points into the tree about to be freed. This is the only place
		// that tree is deleted, which is what makes a bare pointer safe across the
		// recomposition a drag causes on nearly every frame of the gesture.
		_barDrag = nullptr;

		delete _chunk;
		_chunk = chunk;
	}

	_blitCount = 0;
	_textCount = 0;

	// Dropped here rather than beside _barDrag above, because it has to go on a
	// same-pointer redraw() too: every entry names what THIS compose resolved, and
	// drawScrollBarThumbs refills it at the end. Placing it after the delete also
	// covers the new-chunk case, and nothing between the two reads it.
	//
	// Note the early returns below: a panel that resolves no surface leaves this
	// empty, which is exactly right - it painted no thumb, so it has none to check.
	_thumbWatch.clear();

	if (!chunk || !chunk->hasPanel) {
		return;
	}

	// The embedded Static's bounds are the panel's own rect, and every child
	// rect is relative to it. Panels do not clip their children - CARCAT's box
	// is zero-width and its cards still draw - so the surface has to cover the
	// union of the box and everything inside it.
	Common::Rect extent = chunk->panelStatic.bounds;
	const Common::Point origin(extent.left, extent.top);

	for (uint i = 0; i < chunk->children.size(); ++i) {
		const NDUIControl &c = chunk->children[i];
		if (c.bounds.isEmpty()) {
			continue;
		}

		Common::Rect abs = c.bounds;
		abs.translate(origin.x, origin.y);
		extent.extend(abs);
	}

	if (extent.isEmpty()) {
		return;
	}

	// NDUI is authored in the game's 800x600 space while the engine renders
	// 640x480, an exact factor of 1.25.
	//
	// The artwork is composed at the authored size, into a scratch surface, and
	// scaled down once when it is copied into the panel's own surface - which is
	// exactly what RenderObject's scale-on-blit used to do for the whole panel,
	// so widget artwork comes out pixel for pixel as before.
	//
	// Text is deliberately *not* part of that pass. Rasterising a glyph at the
	// authored cell height and then resampling it to 4/5 is what made the
	// conversation text mushy; the text pass below runs after the downscale, at
	// output scale, against fonts rasterised at the matching size (see
	// ttffont.cpp). The authored layout is preserved because both the font cell
	// height and every rect are converted by the same factor.
	const Common::Rect onScreen = toScreen(extent);

	Graphics::ManagedSurface authoredArt(extent.width(), extent.height(),
		g_nancy->_graphics->getInputPixelFormat(32));
	authoredArt.clear(authoredArt.getTransparentColor());
	_artSurface = &authoredArt;
	_pendingText.clear();

	_drawSurface.create(onScreen.width(), onScreen.height(), g_nancy->_graphics->getInputPixelFormat(32));
	_drawSurface.clear(_drawSurface.getTransparentColor());
	moveTo(onScreen);

	// Seed runtime visibility from the authored state, once. Later Show/Hide
	// commands change this map rather than the parsed data.
	if (_visible.empty()) {
		Common::Array<const NDUIControl *> stack;
		stack.push_back(&chunk->panelStatic);
		for (uint i = 0; i < chunk->children.size(); ++i) {
			stack.push_back(&chunk->children[i]);
		}

		while (!stack.empty()) {
			const NDUIControl *c = stack.back();
			stack.pop_back();
			if (!c->name.empty()) {
				// state is an enum (0 normal, 1 disabled, 2 hidden), never a
				// bitfield - Control::LoadCommon branches on exact equality.
				_visible[c->name] = (c->state != NDUIControl::kNDUIStateHidden);
			}

			for (uint i = 0; i < c->children.size(); ++i) {
				stack.push_back(&c->children[i]);
			}
		}

		_baselineVisible = _visible;
	}

	// The panel's own backdrop, then its children in file order. Depth is
	// implicit in that order; NDUI carries no explicit z per widget.
	//
	// The panelStatic's bounds are absolute while every child's are relative to
	// it, so the two need different origins to land in the same surface space.
	const Common::Point surfaceTopLeft(extent.left, extent.top);
	_surfaceTopLeft = surfaceTopLeft;
	drawPanelFill(chunk, Common::Point(-surfaceTopLeft.x, -surfaceTopLeft.y));
	drawControl(chunk->panelStatic, Common::Point(-surfaceTopLeft.x, -surfaceTopLeft.y));

	const Common::Point childOrigin(origin.x - surfaceTopLeft.x, origin.y - surfaceTopLeft.y);
	layOutFlowChildren(chunk);

	for (uint i = 0; i < chunk->children.size(); ++i) {
		drawControl(chunk->children[i], childOrigin);
	}

	drawInventory();

	// Collapse the authored-size artwork onto the output-size surface, then draw
	// every string on top of it at output scale.
	downscaleAuthoredArt(authoredArt, _drawSurface);
	restoreThinFrames(authoredArt, _drawSurface);
	_artSurface = nullptr;

	for (uint i = 0; i < _pendingText.size(); ++i) {
		drawControlText(*_pendingText[i].control, _pendingText[i].origin);
	}

	_pendingText.clear();

	drawConversation();
	drawNarration();
	drawStakeout();
	drawList();
	drawSaveLoad();

	// Last, and deliberately: a thumb's length and position are the page size and
	// the scroll offset, and only the two draws above know how many rows fit the
	// box. Drawing it up in the artwork pass with the rest of the bar would show
	// the previous composition's numbers, and since every scroll goes through
	// redraw() the thumb would lag one click behind the list it describes.
	drawScrollBarThumbs(chunk->children, Common::Point());

	// A re-compose must not change whether the panel is on screen: the
	// conversation chunks are hidden between conversations and the scene decides
	// when they come back. _wasEverVisible starts false so the first init() of a
	// normal panel still shows it.
	setVisible(_wasEverVisible || !_startsHidden);
	_wasEverVisible = _isVisible;

	// A re-compose builds a whole new surface, so the object has to go back on
	// screen. Restoring what is underneath is the compositor's job and it now
	// does it (GraphicsManager::draw resets each dirty rect before rebuilding
	// it); moveTo() to the object's own bounds used to stand in for that and
	// never could have worked - RenderObject::hasMoved() compares the previous
	// screen position with the current one, not the `_hasMoved` latch moveTo()
	// sets, and even had it been true the extra dirty rect would have been the
	// identical rect setNeedsRedraw already pushes.
	setNeedsRedraw(true);
}

// The dialog's own background wash, under everything else it draws.
//
// `panelHeader[9]` is CDXUTDialog::SetBackgroundColors' argument, and it is the
// one field of the 42-byte header stored big-endian, so the little-endian read in
// ndui.cpp leaves the bytes the wrong way round: 0x00000064 on disk is ARGB
// 0x64000000, black at 100/255. Read the other way it would be alpha 0, i.e. no
// panel in the game would have a background at all, which is the tell.
//
// Measured on the reference recording rather than asserted. The conversation
// container's art (Convo_Source 1,1-459,109) is a double frame: an opaque teal
// rounded rect, a fully transparent ring inside it, an inner opaque line, then a
// 458x108 interior at a uniform alpha of 128. Solving the composite against the
// scene's own background video frame, over 39 pillarboxed frames of the reference
// that show the box, gives an effective coverage of
//
//   the transparent ring   0.44   <- art contributes nothing here, so this *is*
//                                    the fill, against an authored 100/255 = 0.392
//   the alpha-128 interior 0.73   <- fill then art: 1-(1-0.392)(1-0.502) = 0.697
//   outside the panel box  0.00   <- and it stops exactly at the box
//
// and the transitions land on authored y 424/436/520/532 to within a pixel. Our
// build measured 0.00 and 0.49 for the same two bands: the ring was see-through
// and the interior was carrying the art alone.
//
// Eight panels author a fill: the conversation, inventory, journal, tasklist and
// cellphone containers at 39% black, the stakeout at 55%, MODAL's ModalDialog at
// 63% black - the veil that makes a modal dialog modal - and its ErrorBgDialog at
// 63% red behind the warning panel.
void NDUIPanel::drawPanelFill(const NDUI *chunk, const Common::Point &origin) {
	if (!_artSurface || !chunk || !chunk->hasPanel) {
		return;
	}

	const uint32 fill = SWAP_BYTES_32(chunk->panelHeader[9]);
	const byte a = (fill >> 24) & 0xff;
	if (!a) {
		return;
	}

	Common::Rect box = chunk->panelStatic.bounds;
	box.translate(origin.x, origin.y);
	box.clip(Common::Rect(_artSurface->w, _artSurface->h));
	if (box.isEmpty()) {
		return;
	}

	// A plain fill, not a blend: this is the first thing drawn into a surface that
	// was just cleared to transparent, and the artwork blitted over it afterwards
	// composites against it per-pixel in ManagedSurface's translucent-target path.
	_artSurface->fillRect(box, _artSurface->format.ARGBToColor(a,
		(fill >> 16) & 0xff, (fill >> 8) & 0xff, fill & 0xff));
}

void NDUIPanel::drawControl(const NDUIControl &control, const Common::Point &origin) {
	// state is an enum, never a bitfield - Control::LoadCommon branches on exact
	// equality. Hidden controls take no space and draw nothing. Visibility is
	// read from the runtime map so Show/Hide bindings take effect.
	if (!control.name.empty() && _visible.contains(control.name)) {
		if (!_visible[control.name]) {
			return;
		}
	} else if (control.state == NDUIControl::kNDUIStateHidden) {
		return;
	}

	// A scroll bar's four elements are four *places*, not four layers; the flat
	// element pass below would stack them all in the top-left corner.
	const bool isScrollBar = (control.classID == kNDUIClassScrollBar);
	if (isScrollBar) {
		drawScrollBar(control, origin);
	}

	for (uint i = 0; i < control.elements.size() && !isScrollBar; ++i) {
		const NDUIElement &el = control.elements[i];

		if (el.sourceName.empty() || el.sourceRect.isEmpty()) {
			// A text-only element: it has a font and a caption but no artwork.
			// Rendering those is the next stage; skipping keeps this pass to
			// pure atlas blits.
			continue;
		}

		// Not a real member: the save/load screens name a surface the engine is
		// expected to generate at runtime from the savegame thumbnail.
		if (el.sourceName.equalsIgnoreCase("Engine_LoadSave")) {
			continue;
		}

		// `origin` converts this control's bounds into surface space: it is the
		// negated surface top-left for the panelStatic, whose bounds are absolute,
		// and the panel top-left relative to the surface for its children.
		Common::Point dest(control.bounds.left + origin.x, control.bounds.top + origin.y);

		// Flow-laid children carry no authored position, so use the slot the
		// layout pass assigned them.
		if (!control.name.empty() && _flowPos.contains(control.name)) {
			dest = _flowPos[control.name];
			dest.x += origin.x;
			dest.y += origin.y;
		}

		// The authored per-state texture colour. Only the NORMAL slot is consumed:
		// the other three need the mouse-over / pressed / disabled tracking the
		// panel does not do yet, and the disabled slot is 0xc8808080 on nearly
		// every widget, so wiring it up would change how every greyed-out widget
		// looks - a separate question from this one.
		//
		// Masking the RGB back to white when kNDUIModulateRGB is off is not just
		// tidiness: it makes the twenty-seven opaque-but-tinted widgets compare
		// equal to kNDUINoModulation below, so they take the plain blit and never
		// pay for a pixel pass that would compute x*255/255.
		uint32 mod = el.textureColors[kNDUIColorNormal];
		if (!kNDUIModulateRGB) {
			mod |= 0x00ffffff;
		}

		if ((mod >> 24) == 0) {
			// Authored fully transparent at rest. Eight of the options dialog's
			// radio buttons and its CCOption checkbox are like this: element[0] is
			// only their hit box - the frame and the swatches are painted into the
			// dialog's own backdrop art - and what marks the current choice is the
			// class-specific "checked" sprite, drawn by drawSetMark() below.
			continue;
		}

		blitAtlasRect(el.sourceName, el.sourceRect, dest, mod, control.name);
	}

	// The two classes whose artwork is not in an element. Both run after the
	// element pass so the mark and the thumb land on top of the track/hit box.
	if (control.classID == kNDUIClassSlider) {
		drawSlider(control, origin);
	} else if (control.classID == kNDUIClassCheckBox || control.classID == kNDUIClassRadioButton) {
		drawSetMark(control, origin);
	}

	// Deferred: strings are drawn after the artwork has been scaled down, so they
	// are never resampled. Widget artwork and widget text never overlap within a
	// panel, so the change in order is not observable.
	//
	// A Var::Static's string is the number it is bound to, not a caption id, so
	// it has no captionTextID to gate on. Its artwork *is* under its text - the
	// coin purse's digits sit on the purse - which is exactly why the text pass
	// has to come after the artwork pass, as it already does.
	if ((!control.captionTextID.empty() || control.classID == kNDUIClassVarStatic) &&
			!control.elements.empty()) {
		PendingText pending;
		pending.control = &control;
		pending.origin = origin;
		_pendingText.push_back(pending);
	}

	// An owned control's bounds are measured from the *panel*, not from its owner,
	// so the origin does not descend. There are twelve owner/owned pairs in the
	// whole game and they settle it between them:
	//
	//   * seven RadioButtons in three RadioGroups, and every group's own bounds
	//     are (0,0)-(0,0), so for those the question does not arise;
	//   * five ScrollBars owned by a ListBox, and all five are authored to start
	//     exactly 9 above their list's top and end exactly 9 below its bottom,
	//     with x running from the list's right edge minus 6. One convention,
	//     five bars, three different owning-list origins - which is a coincidence
	//     only if the bounds are panel-relative.
	//
	// The clincher is the journal, whose two bars are authored as a matched pair
	// in one column: JournalItemsScrollBar (165,34)-(181,199) is owned by the
	// list, JournalDetailsScrollBar (165,195)-(181,360) is a direct panel child,
	// and they share an x and abut in y. Descending the owner's offset would move
	// the first of them 14px right and 43px down, out of the column.
	for (uint i = 0; i < control.children.size(); ++i) {
		drawControl(control.children[i], origin);
	}
}

bool NDUIPanel::blitAtlasRect(const Common::String &atlasName, const Common::Rect &srcRect,
		const Common::Point &dest, uint32 modulate, const Common::String &controlName,
		const Common::Rect *clipTo) {
	if (!_artSurface || atlasName.empty() || srcRect.isEmpty()) {
		return false;
	}

	// Debug affordance: report which atlas and sub-rect each control draws
	// from. This is the manifest an artist needs in order to replace a texture:
	// the atlases are PNG members and can be carved straight out of the data,
	// but nothing outside the engine knows which rect belongs to which widget.
	if (ConfMan.hasKey("nancy_dump_ndui_art")) {
		warning("NDUIART %s|%s|%d,%d,%d,%d", controlName.c_str(),
			atlasName.c_str(), srcRect.left, srcRect.top,
			srcRect.right, srcRect.bottom);
	}

	Graphics::ManagedSurface atlas;
	if (!g_nancy->_resource->loadImage(Common::Path(atlasName), atlas)) {
		warning("NDUI: could not load atlas '%s' for control '%s'",
			atlasName.c_str(), controlName.c_str());
		return false;
	}

	Common::Rect src = srcRect;
	if (src.right > atlas.w || src.bottom > atlas.h) {
		warning("NDUI: source rect for '%s' lies outside atlas '%s'",
			controlName.c_str(), atlasName.c_str());
		return false;
	}

	// A slider's filled bar is the same sprite as its track, shown only up to the
	// thumb, so the caller can ask for a right-hand crop.
	if (clipTo) {
		src.right = MIN(src.right, (int16)(src.left + clipTo->width()));
		src.bottom = MIN(src.bottom, (int16)(src.top + clipTo->height()));
		if (src.isEmpty()) {
			return false;
		}
	}

	if (modulate == kNDUINoModulation) {
		_artSurface->blitFrom(atlas, src, dest);
	} else {
		// `atlas` is this call's own decoded copy, so the sub-rect can be
		// modulated in place; no second surface, no second allocation.
		modulateSurface(atlas, src, modulate);
		_artSurface->blitFrom(atlas, src, dest);
	}

	++_blitCount;
	return true;
}

// --- The options screen's settings widgets -----------------------------
//
// Which setting a widget drives is read off its own bindings, never off its name.
// Each of the twelve carries the pair the toolkit raises for a value change and
// for a commit - `event 18 command 30` and `event -1 command 13` - both aimed at
// an "Engine_*" pseudo-target, and that target is the setting.

Common::String NDUIPanel::settingTarget(const NDUIControl &control) {
	for (uint i = 0; i < control.actions.size(); ++i) {
		const NDUIAction &action = control.actions[i];
		if (action.commandID != kNDUICommandSetValue && action.commandID != kNDUICommandNotify) {
			continue;
		}

		if (action.target.hasPrefixIgnoreCase("Engine_")) {
			return action.target;
		}
	}

	return Common::String();
}

int NDUIPanel::sliderValue(const NDUIControl &control) const {
	if (!control.name.empty() && _sliderValues.contains(control.name)) {
		return _sliderValues[control.name];
	}

	// Nothing has moved it this session: show the setting if there is one, and
	// the authored value otherwise (which is what a slider driving nothing is).
	double setting = 0.0;
	if (NancySceneState.getNDUISettingValue(settingTarget(control), setting)) {
		return (int)(setting + 0.5);
	}

	return (int)control.sliderValue;
}

bool NDUIPanel::controlIsSet(const NDUIControl &control) const {
	double setting = 0.0;
	if (!NancySceneState.getNDUISettingValue(settingTarget(control), setting)) {
		return false;
	}

	// A radio button names the value it stands for in the parameter its own
	// bindings carry - Engine_Matte 4.0 and 5.0, Engine_GameWindow 0.0/1.0/2.0,
	// Engine_Font 100.0 and 127.0 - so it is the current one when the setting has
	// that value. A check box carries no parameter; it is simply on or off.
	if (control.classID == kNDUIClassRadioButton) {
		for (uint i = 0; i < control.actions.size(); ++i) {
			const NDUIAction &action = control.actions[i];
			if (action.hasParam && action.target.hasPrefixIgnoreCase("Engine_")) {
				return ABS(setting - action.param) < 0.5;
			}
		}

		return false;
	}

	return setting != 0.0;
}

// DXUT's CDXUTSlider, which is the widget NDUI's class 3 is a description of:
// the thumb is *centred* on the value's position along the whole track, so it
// hangs half off each end at the extremes.
//
//   UpdateRect(): m_nButtonX = (nValue - nMin) * RectWidth(bounding) / (nMax - nMin)
//   ValueFromPos(x): nMin + (nMax - nMin) * (x - left) / RectWidth(bounding)
//
// The thumb sprite is 8x16 against a 204x16 track, so unlike DXUT - which makes
// the thumb a square of the control's height - the authored size is used.
void NDUIPanel::drawSlider(const NDUIControl &control, const Common::Point &origin) {
	const Common::Rect bounds = control.bounds;
	if (bounds.isEmpty() || control.sliderMax <= control.sliderMin) {
		return;
	}

	const Common::Point dest(bounds.left + origin.x, bounds.top + origin.y);
	const int range = (int)control.sliderMax - (int)control.sliderMin;
	const int value = CLIP<int>(sliderValue(control), (int)control.sliderMin, (int)control.sliderMax);
	const int buttonX = (value - (int)control.sliderMin) * bounds.width() / range;

	// The filled part of the track, up to the thumb. NDUI carries this as a
	// second sprite the same size as the track; DXUT has no such element, so
	// "the bar is the track shown up to the thumb" is a reading rather than a
	// transcription - but a full-width second copy of the track would be
	// invisible under it, which is the only other thing it could mean.
	if (!control.sliderBarImageName.empty() && buttonX > 0) {
		const Common::Rect crop(buttonX, control.sliderBarRect.height());
		blitAtlasRect(control.sliderBarImageName, control.sliderBarRect, dest,
			kNDUINoModulation, control.name, &crop);
	}

	if (control.sliderThumbImageName.empty()) {
		return;
	}

	Common::Point thumb(dest.x + buttonX - control.sliderThumbRect.width() / 2, dest.y);
	thumb.y += (bounds.height() - control.sliderThumbRect.height()) / 2;
	blitAtlasRect(control.sliderThumbImageName, control.sliderThumbRect, thumb,
		kNDUINoModulation, control.name);
}

// DXUT's CDXUTScrollBar::UpdateRect puts a button at each end of the bar and
// leaves the track between them:
//
//   m_rcUpButton   = (x, y,           x + w, y + w)
//   m_rcDownButton = (x, y2 - w,      x + w, y2)
//   m_rcTrack      = (x, y + w,       x + w, y2 - w)
//
// NDUI ships a real sprite for each - element 0 the track, 1 the up button, 2 the
// down button, 3 the thumb - where DXUT stretches one element into each computed
// rect. That the sprites are meant at their authored size, and in DXUT's places,
// is settled by the artwork itself: on eight of the game's nine scroll bars
//
//     elements[1].height + elements[0].height + elements[2].height
//
// is *exactly* the control's height. Convo 17 + 72 + 17 = 106; Journal (both)
// 16 + 133 + 16 = 165; Tasklist 16 + 294 + 16 = 326; Inventory and CellPhone
// 16 + 141 + 16 = 173; Load 17 + 125 + 17 = 159; Save, CheatMain and CheatInv
// 17 + 105 + 17 = 139. Six different atlases agree; the odd one out is
// StakeOutScrollBar, which borrows Convo's 18px art for a 15x84 box, and is the
// only reason the placement below clips.
//
// The thumb is not drawn *here*; drawScrollBarThumbs draws it, after the rows it
// has to measure itself against exist. This function paints elements 0, 1 and 2
// and nothing else, which is why it is untouched.
//
// The claim this comment used to make - "no NDUI list in this port scrolls, so a
// thumb would have no position to take" - stopped being true when listScrollBy
// was wired up. Three lists scroll now (the save list, the load list and the
// journal's headings), and the cost of the claim outliving its truth was
// measured: the up and down arrows are 13 output pixels each on a LOADGAME bar
// whose track is about 100, elements[3] was never blitted at all, and three
// clicks at the middle of the track produced no change in the rendered list. A
// player reaching for the bar grabs the thumb, finds nothing there, clicks the
// track, and the game does not respond.
//
// elements[3] is authored on all eleven of the game's bars and is a *full
// track-length* strip - LOADGAME's track is Load_Source 18x125 and its thumb is
// Load_Source (427,63)-(445,188), also 18x125 - i.e. a sprite meant to be
// cropped to a proportional length, structurally the same as the slider's
// sliderBarRect that drawSlider crops to buttonX above. Its normal-state texture
// colour is 0xffffffff on every bar, so drawControl's fully-transparent skip
// never applies to it.
//
// DXUT's UpdateThumbRect() still governs the case where there is nothing to
// scroll: it collapses the thumb to nothing whenever the content fits the page
// (`m_rcThumb.bottom = m_rcThumb.top`), and so does scrollBarGeom. That keeps
// faith with the only rendering evidence there is - on the reference recording,
// the 39 frames that show the conversation box have a bright block at each end
// of the bar (screen y 346-353 and 411-417 of a bar spanning 340-425) and a
// flat, dim track between them. The conversation bar has no readable list behind
// it either way, so those 39 frames must still come out pixel for pixel.
void NDUIPanel::drawScrollBar(const NDUIControl &control, const Common::Point &origin) {
	if (control.elements.size() < 3 || control.bounds.isEmpty()) {
		return;
	}

	const NDUIElement &track = control.elements[0];
	const NDUIElement &up = control.elements[1];
	const NDUIElement &down = control.elements[2];

	const Common::Point dest(control.bounds.left + origin.x, control.bounds.top + origin.y);
	const int barHeight = control.bounds.height();
	const int upHeight = MIN<int>(up.sourceRect.height(), barHeight);
	const int downHeight = MIN<int>(down.sourceRect.height(), barHeight - upHeight);
	const int trackHeight = MAX<int>(0, barHeight - upHeight - downHeight);

	// Cropped rather than stretched, so a bar whose box is shorter than its art
	// loses the far end of the track instead of overrunning the panel.
	if (trackHeight > 0 && !track.sourceName.empty()) {
		const Common::Rect crop(track.sourceRect.width(), (int16)trackHeight);
		blitAtlasRect(track.sourceName, track.sourceRect,
			Common::Point(dest.x, dest.y + upHeight), kNDUINoModulation, control.name, &crop);
	}

	if (upHeight > 0 && !up.sourceName.empty()) {
		const Common::Rect crop(up.sourceRect.width(), (int16)upHeight);
		blitAtlasRect(up.sourceName, up.sourceRect, dest, kNDUINoModulation, control.name, &crop);
	}

	if (downHeight > 0 && !down.sourceName.empty()) {
		const Common::Rect crop(down.sourceRect.width(), (int16)downHeight);
		blitAtlasRect(down.sourceName, down.sourceRect,
			Common::Point(dest.x, dest.y + barHeight - downHeight), kNDUINoModulation,
			control.name, &crop);
	}
}

// How far through its list this panel is, in that list's own units. The two
// runtime lists count different things - the save list counts rows, the journal
// and task list count scroll stops - and no panel owns both, so this dispatches
// the same way listScrollBy does and hands back numbers already in the units the
// matching delta has to be expressed in.
//
// The page size is *read back off the last draw* rather than recomputed. That is
// not a shortcut, it is the only honest way to get it: how many rows fit is a
// function of the wrap width and of getFontHeight(), which returns the substitute
// font's hhea height and does not track the requested size, so any count worked
// out here would be a guess dressed up as a measurement. Both draws already
// record what they laid out, so the page is reported by the thing that did the
// laying out.
//
//   _savePageRows - the rows on the LAST page, which is the one page a list of
//                   variable-height rows has a fixed answer for: a wrapped save
//                   name makes "how many fit" depend on where the list starts, so
//                   the count on screen right now is not a property of the list at
//                   all. drawSaveLoad works it out from the heights it measured -
//                   size minus the top row of the last page - which is also the
//                   value it clamps _saveScroll to, so maxPos below (range - page)
//                   is that clamp exactly. The array of hit rects is no use for
//                   this any more: it is as long as the current page, and a page
//                   that moved with the position would restretch the thumb and
//                   rescale a drag under the pointer.
//   _listRowRects - drawList pushes one rect per drawn *segment*. The first is
//                   stop _listScroll by construction; every later one is drawn
//                   only if its whole height fits the box, which is exactly the
//                   condition under which the stop table gave that row a single
//                   stop. So the count is the number of stops this page covers.
//                   (Deriving it as 1 would be wrong whenever several short rows
//                   share a screenful, which is every task list the game has.)
//
// A page of zero means no draw has happened yet, and is reported as "no model" so
// a bar cannot be given a thumb before the list behind it has been laid out once.
bool NDUIPanel::scrollModel(int &position, int &page, int &range) {
	if (saveListControl()) {
		position = _saveScroll;
		page = _savePageRows;
		range = (int)_saveRows.size();
		return page > 0;
	}

	if (!_listAnchor.empty() && _listStops > 0) {
		position = _listScroll;
		page = (int)_listRowRects.size();
		range = _listStops;
		return page > 0;
	}

	// The item grid, third and last so the two authored lists keep their exact
	// priority: this is reachable only by a panel that has neither, which is the
	// single panel that owns InvDialog. ROWS of the grid, not items - drawInventory
	// skips _invScroll * cols items per row, so a page counted in items would make
	// the thumb three times too short and the track page three times too far. Both
	// numbers are read back off the last draw for the reason above, and here it is
	// not even the font that forces it: recomputing them needs INVD and
	// UI_INVENTORY, i.e. two image loads from inside a compose.
	if (isInventoryPanel()) {
		position = _invScroll;
		page = _invPageRows;
		range = _invRangeRows;
		return page > 0;
	}

	return false;
}

// THE LATCH, and it is the load-bearing part of the whole routing scheme rather
// than any particular lookup: a bar carrying any event-15 action is answered
// through its targets and NEVER falls back to this panel's own scrollModel, even
// when every target it names resolves to nothing.
//
// The measured reason it has to be that way round is JournalDetailsScrollBar,
// which lives in JOURNAL chunk[2] - the same panel as the JournalItems ListBox -
// and names VENJH1..VENJH4, the roots of chunks 4..7. "Resolve the list this
// panel owns" would hand the details bar the heading list's model, and its track
// would page the wrong pane. (Checked for all six routed bars: no panel in
// PUI_Default.dat owns both a routed bar and that bar's target -
// work/scrollbar/dump_scrollbars.py.)
//
// Six of the eleven bars answer true here. The other five carry no binding at
// all and are answered by this panel's own list exactly as they were before any
// of this existed.
bool NDUIPanel::barIsRouted(const NDUIControl &control) {
	for (uint a = 0; a < control.actions.size(); ++a) {
		if (control.actions[a].eventID == kNDUIEventScroll) {
			return true;
		}
	}

	return false;
}

// Which model a bar describes. InvScrollBar sits in INVENTORY chunk[2] and names
// InvDialog, the root of chunk[4], so the panel holding the bar cannot answer out
// of its own state and has to go through the scene.
//
// Read and write are deliberately the same shape, over the same latch. If they
// ever disagree about which list a bar describes, the thumb is painted from one
// position while the drag writes against another, which is a thumb that walks out
// from under the pointer. Keep them parallel.
bool NDUIPanel::barScrollModel(const NDUIControl &control, int &position, int &page, int &range) {
	if (!barIsRouted(control)) {
		return scrollModel(position, page, range);
	}

	for (uint a = 0; a < control.actions.size(); ++a) {
		if (control.actions[a].eventID != kNDUIEventScroll) {
			continue;
		}

		if (NancySceneState.nduiScrollModel(control.actions[a].target, position, page, range)) {
			return true;
		}
	}

	return false;
}

// The write twin, lifted out of handleScrollBarInput unchanged so there is one
// copy of the walk rather than two that can drift. Unlike the read it does not
// stop at the first target: the fan-out - JournalDetailsScrollBar scrolls all
// four VENJH pages, including the three that are hidden - is pre-existing
// behaviour and is left exactly as it was.
void NDUIPanel::barScrollBy(const NDUIControl &control, int delta) {
	if (!barIsRouted(control)) {
		listScrollBy(delta);
		return;
	}

	for (uint a = 0; a < control.actions.size(); ++a) {
		if (control.actions[a].eventID != kNDUIEventScroll) {
			continue;
		}

		NancySceneState.applyNDUIScroll(control.actions[a].target, delta);
	}
}

// The single source of truth for where a bar's parts are. The paint and the hit
// test both come through here, because "you aim at the thumb and the click lands
// somewhere else" is the same defect as "there is no thumb at all" one step on.
//
// Returns false only for a control that is not a usable bar. Otherwise it always
// fills in the three button rects, and leaves `thumb` empty - DXUT's collapsed
// m_rcThumb - in every case where dragging or paging must not happen:
//
//   * no model. barScrollModel resolves one by the bar's own event-15 target or
//     not at all - see barIsRouted. This used to bail on the mere presence of the
//     binding; the latch is what took that over, and it is the latch, not the
//     bail, that keeps JournalDetailsScrollBar off the JournalItems list it
//     shares a panel with. A routed bar whose target is hidden, absent or
//     modelless lands here, which is the whole of ConvoScrollBar (its text is
//     measured in output pixels, so there is no row model to report) and the
//     whole of StakeOutScrollBar (drawStakeout paints at no offset), both
//     unchanged.
//   * the panel has no scrollable list (CheatInvList's bar, StreamList's).
//   * the content fits, range <= page. UpdateThumbRect's rule, and the one that
//     stops a thumb from advertising a scrollability that does not exist.
//   * the track is too short to hold a minimum thumb, or elements[3] is missing.
//
// Every division below is guarded by one of those: range > page >= 1 makes
// `range` at least 2, `maxPos` at least 1, and a track of at least
// kMinThumbScreen + 1 output pixels forces thumbHeight < trackHeight, so `travel`
// is at least 1. There is no path to a divide by zero and none to an unbounded
// position - the drag maps into [0, maxPos] by construction. That argument rests
// on the range > page test alone, so it is untouched by where the three numbers
// now come from.
bool NDUIPanel::scrollBarGeom(const NDUIControl &control, const Common::Point &origin,
		ScrollBarGeom &out) {
	out = ScrollBarGeom();

	if (control.classID != kNDUIClassScrollBar || control.elements.size() < 3 ||
			control.bounds.isEmpty()) {
		return false;
	}

	out.bar = controlScreenRect(control, origin);

	// Composed exactly as handleScrollBarInput composed them before this existed,
	// so the arrow hit boxes - already verified against the rendered output - come
	// out bit for bit the same.
	const int barHeight = control.bounds.height();
	const int upHeight = MIN<int>(control.elements[1].sourceRect.height(), barHeight);
	const int downHeight = MIN<int>(control.elements[2].sourceRect.height(), barHeight - upHeight);

	out.up = Common::Rect(out.bar.left, out.bar.top, out.bar.right,
		out.bar.top + toScreen(upHeight));
	out.down = Common::Rect(out.bar.left, out.bar.bottom - toScreen(downHeight),
		out.bar.right, out.bar.bottom);

	if (out.down.top <= out.up.bottom) {
		// Defensive only - no authored bar reaches this, and StakeOutScrollBar,
		// which this comment used to name, is not the case. Measured from
		// PUI_Default.dat: its box is 15 wide by 84 tall (the 18 is the sprite
		// WIDTH) with 17-tall arrows, which through controlScreenRect gives bar
		// (304,360)-(316,427), up.bottom 373, down.top 414 - a 41-output-pixel
		// track, and the tightest of the eleven. Firing this would take an authored
		// height of about 32 or less. Kept because a bar with no track has nothing
		// to grab and the track rect below would come out inverted.
		return true;
	}

	out.track = Common::Rect(out.bar.left, out.up.bottom, out.bar.right, out.down.top);

	// One test where there were two. The bail on the binding is gone: a routed bar
	// is resolved through its target now instead of refused, and a target that
	// answers nothing falls out of the same exit it would have taken anyway, so
	// every bar that shows no thumb today still shows none.
	if (!barScrollModel(control, out.position, out.page, out.range) ||
			out.range <= out.page) {
		return true;
	}

	if (control.elements.size() < 4 || control.elements[3].sourceName.empty()) {
		return true;
	}

	const int trackHeight = out.track.height();
	const int artHeight = MIN<int>(control.elements[3].sourceRect.height(),
		barHeight - upHeight - downHeight);
	if (artHeight <= 0 || trackHeight < kMinThumbScreen + 1) {
		return true;
	}

	// Length is the fraction of the list on screen, floored at DXUT's minimum and
	// capped by both the track and the sprite. Save_Source's and Journal_Source's
	// thumb strips are two authored pixels shorter than their tracks, so a thumb
	// that should span the whole track falls two pixels short there rather than
	// sampling outside the sprite.
	const int maxSrc = MIN(artHeight, (trackHeight * kNDUIScreenDen) / kNDUIScreenNum);
	if (maxSrc <= 0) {
		return true;
	}

	int thumbHeight = MAX(trackHeight * out.page / out.range,
		MIN(kMinThumbScreen, trackHeight));

	// Round the length to one the crop can actually reproduce, then take that back
	// as the length, so the rect the player aims at is the rect that gets painted.
	out.srcHeight = CLIP(fromScreen(thumbHeight), 1, maxSrc);
	thumbHeight = toScreen(out.srcHeight);
	if (thumbHeight <= 0 || thumbHeight >= trackHeight) {
		out.srcHeight = 0;
		return true;
	}

	const int maxPos = out.range - out.page;
	const int travel = trackHeight - thumbHeight;
	const int pos = CLIP(out.position, 0, maxPos);
	const int top = out.track.top + pos * travel / maxPos;

	// Full bar width, like the arrows: the sprite is blitted at its own authored
	// width from the same left edge, exactly as drawScrollBar blits the track, but
	// a couple of pixels of art must not decide how hard the thumb is to hit.
	out.thumb = Common::Rect(out.track.left, top, out.track.right, top + thumbHeight);
	return true;
}

void NDUIPanel::drawScrollBarThumbs(const Common::Array<NDUIControl> &children,
		const Common::Point &origin) {
	for (uint i = 0; i < children.size(); ++i) {
		const NDUIControl &c = children[i];

		// drawControl's visibility test, repeated rather than shared because this
		// pass walks the tree in controlScreenRect's origin space rather than the
		// artwork pass's. Skipping the children too is the point: LOADGAME's bar is
		// nested inside its ListBox, and hiding the list has to take the thumb with
		// it or a dismissed dialog leaves one floating.
		if (!c.name.empty() && _visible.contains(c.name)) {
			if (!_visible[c.name]) {
				continue;
			}
		} else if (c.state == NDUIControl::kNDUIStateHidden) {
			continue;
		}

		ScrollBarGeom geom;
		const bool isBar = (c.classID == kNDUIClassScrollBar &&
			scrollBarGeom(c, origin, geom));

		// Record what this compose resolved for every ROUTED bar that got this far,
		// so updateGraphics() can tell later whether the thumb it painted still
		// describes the list - see there for why that is the coupling rather than a
		// refresh call. Only the routed ones: an unrouted bar reads a list in this
		// same panel, which this same compose has just laid out, so it is correct by
		// construction and stays byte-for-byte what it was. That exclusion is what
		// keeps LoadList's, SaveList's, JournalItems', CheatInvList's and
		// StreamList's bars provably untouched by any of this.
		//
		// Resolved a second time rather than taken out of `geom`, because
		// scrollBarGeom is shared with the hit test and would have to grow an
		// out-param that only the compose ever reads; and because geom's three
		// numbers are left at 0/0/0 on several of its exits, which would make "the
		// target answered nothing" indistinguishable from "the target answered
		// zero". Three ints, at most six bars, once per compose.
		if (isBar && barIsRouted(c)) {
			ThumbWatch watch;
			watch.control = &c;
			watch.resolved = barScrollModel(c, watch.position, watch.page, watch.range);
			_thumbWatch.push_back(watch);
		}

		if (isBar && !geom.thumb.isEmpty()) {
			// Cropped from the top of the strip, the way drawScrollBar crops the
			// track. This goes to the output surface rather than the artwork
			// scratch because the artwork pass has already been downscaled by the
			// time the rows that size this thumb exist.
			//
			// Positioned off geom.thumb, which is the rect the hit test uses, so
			// the two cannot drift apart. That means the thumb is placed in
			// controlScreenRect's space rather than the artwork pass's, and the two
			// floor the 4/5 conversion at different points, so the thumb can sit a
			// pixel off the track sprite it rides in. A pixel of art is the right
			// thing to trade for a thumb that is exactly where it looks.
			Common::Rect src = c.elements[3].sourceRect;
			src.bottom = src.top + (int16)geom.srcHeight;

			blitAtlasRectToOutput(c.elements[3].sourceName, src,
				Common::Point(geom.thumb.left - _screenPosition.left,
							  geom.thumb.top - _screenPosition.top), c.name);
		}

		if (!c.children.empty()) {
			drawScrollBarThumbs(c.children, origin);
		}
	}
}

// THE COUPLING between a routed bar and the list it describes, and the only one.
// A thumb is painted by a compose of the panel holding the BAR, out of state that
// lives in the panel holding the LIST, and nothing about a compose of the second
// panel touches the first. So the thumb is a cache of another object's state, and
// this is the check that it is still valid, run once a frame, immediately before
// the frame is composited.
//
// WHY A CHECK AT DRAW TIME RATHER THAN A REFRESH AT EACH WRITE. A refresh has to
// be called, and every caller that forgets is a defect that looks exactly like
// this one. Three were already sitting in the tree when this was written:
//
//   * RECOMPOSE ORDER. _nduiPanels is built in chunk order (Scene::init pushes
//     inside the per-chunk loop), which puts every routed bar's panel at a LOWER
//     index than its target's: INVENTORY[2] InvDialogContainer before
//     INVENTORY[4] InvDialog, TASKLIST[2] before TASKLIST[4], JOURNAL[2] before
//     JOURNAL[4..7]. Both loops that recompose everything - the Engine_Font
//     rebuild and synchronizeNDUI's restore - therefore compose each thumb from
//     its target's PRE-LOOP state and never compose it again. Measured: carry ten
//     or more items, open the inventory, save, load that save. On the way back in
//     INVENTORY[2] composes while INVENTORY[4] is still hidden, nduiScrollModel
//     skips it on isVisible(), and the thumb comes back EMPTY - with the track
//     inert too, since handleScrollBarInput returns false on a bare track.
//   * selectJournalPage() shows one VENJH page and hides three, by NDUI command
//     on the page panels. It never touches JOURNAL[2], where
//     JournalDetailsScrollBar lives, so picking a heading produced a page with no
//     thumb beside it.
//   * drawInventory() re-clamps _invScroll against the row count it just
//     measured. That is a write to the position from inside the target's own
//     compose, which no caller-side refresh could ever be placed after.
//
// A check keyed on the state itself has none of those holes, and - the reason to
// prefer it over a second pass bolted onto the two recompose loops - it cannot
// acquire new ones. A future third recompose loop, or any future writer of any of
// the three list models, is covered without knowing this exists.
//
// It is also the engine's own idiom for this shape of problem: refreshBoundValues
// polls the player table for the coin purse for the same reason, that the writer
// (a SetValue record) has no idea a widget is showing its value.
//
// COST. Only visible panels, and within one only the routed bars the thumb pass
// actually reached - six such bars exist in the whole game, and they are spread
// over six container panels that the player opens one at a time. Each is a
// nduiScrollModel lookup, i.e. a findControl walk per panel; the same order of
// work refreshBoundValues already does across every panel every frame.
//
// TERMINATION. Composing the panel that holds a bar cannot change the model that
// bar reads - the model is the target's, and this compose does not run the
// target's draws - so one redraw() makes the recorded and live values agree and
// the next frame does nothing. There is no ping-pong, and no cycle to have one:
// no panel holds both a routed bar and that bar's target.
//
// MID-DRAG. This fires on nearly every frame of a thumb drag, which is safe for
// one reason and only one: init() drops _barDrag when the chunk POINTER changes,
// and redraw() hands it back the same one, so the control the drag points at
// outlives any number of recompositions. The grab anchor (_barDragGrabY,
// _barDragGrabPos) is a member and is not touched by a compose, so the position
// is still measured from the press and the thumb cannot drift under the pointer.
// If the parsed tree ever becomes mutable this is a use-after-free rather than
// one redraw too many.
void NDUIPanel::updateGraphics() {
	// Empty on every panel whose last compose reached no routed bar, which is most
	// of the twelve; they pay a size check. _isVisible is tested as well because a
	// hidden panel is composed but not shown, and re-composing one on a model
	// change nothing can see is work for no pixels - it will be re-checked on the
	// first frame it is visible, since the check is against live state rather than
	// against a change it might have missed.
	if (!_isVisible || _thumbWatch.empty()) {
		return;
	}

	for (uint i = 0; i < _thumbWatch.size(); ++i) {
		const ThumbWatch &watch = _thumbWatch[i];

		int position = 0;
		int page = 0;
		int range = 0;
		const bool resolved = barScrollModel(*watch.control, position, page, range);

		// `resolved` is compared first and on its own: false leaves the three ints
		// untouched, so comparing them across a resolved/unresolved change would be
		// comparing to whatever the caller happened to initialise. It is also the
		// interesting transition - "no thumb" to "a thumb" is the load bug above.
		if (resolved != watch.resolved || (resolved && (position != watch.position ||
				page != watch.page || range != watch.range))) {
			// RETURN, not break or continue: redraw() re-enters init(), which clears
			// _thumbWatch and lets drawScrollBarThumbs refill it, so `watch` and the
			// loop index both refer to an array that no longer exists. One recompose
			// repaints every thumb in the panel anyway, so there is nothing left to
			// check.
			redraw();
			return;
		}
	}
}

void NDUIPanel::drawSetMark(const NDUIControl &control, const Common::Point &origin) {
	if (control.checkedImageName.empty() || control.checkedImageRect.isEmpty()) {
		return;
	}

	if (!controlIsSet(control)) {
		return;
	}

	// DXUT puts the box at the left of the bounding rect, vertically centred, and
	// starts the caption past it. The sprite is drawn at its authored size rather
	// than stretched into a square of the control's height, for the same reason
	// the slider thumb is.
	Common::Point dest(control.bounds.left + origin.x, control.bounds.top + origin.y);
	dest.y += (control.bounds.height() - control.checkedImageRect.height()) / 2;

	blitAtlasRect(control.checkedImageName, control.checkedImageRect, dest,
		kNDUINoModulation, control.name);
}

// --- Inline text markup ------------------------------------------------
//
// The CONVO, UITEXT and journal string tables carry markup inline, in the same
// angle-bracket form the game's Hypertext widgets parse. Every code that occurs
// in nancy18 more often than chance (a three-byte pattern turns up about three
// times by accident in a 50MB ciftree) is handled here:
//
//   <cN>  996 + 1466 + ...  foreground colour, COLR chunk with id N
//   <bN>  3 (+2 in UITEXT)  typeface at BOLD weight, FONT chunk with id N
//   <fN>  11 (<f8>, <f16>)  typeface, FONT chunk with id N
//   <n>   531               hard line break
//   <u>   43                emphasis toggle, drawn as an underline
//
// Deliberately *not* implemented, and still stripped:
//
//   <cN> for N with no COLR chunk. nancy18 ships ids 0..8 but the Italian
//        stakeout responses use <c9> 36 times; there is no tenth entry anywhere
//        in the data, so there is no colour to resolve it to and the run keeps
//        whatever colour it had.
//   <sN> size and <i>/<e> emphasis. These appear in the published notes for the
//        format but nancy18's data contains no <s0> at all, and the bare <s>,
//        <i> and <e> hits (6, 1 and 2) are at the noise floor for a pattern of
//        that length. There is nothing to check an implementation against, so
//        guessing a size ladder or an italic face would be inventing behaviour.
//
// The id mapping is direct - <cN> is the COLR chunk whose id field is N - and is
// pinned by three independent checks:
//   * COLR 0 is the only entry whose four state slots differ (64A0DC normal,
//     646464 disabled, FFFFFF over/pressed) and <c0> is used on exactly one kind
//     of string: the player's clickable conversation responses.
//   * <f16> tags the ATM keypad strings, and FONT id 16 is the "ATM" alias,
//     Courier New at weight 700 - a monospaced bold face for a cash machine.
//   * <c1> opens every NPC conversation line and COLR 1 is EBCE54; the retail
//     screen shot of the first Helena conversation draws that caption in gold.
//
// <bN> names a FONT chunk, not a COLR chunk, and adds the bold weight. This
// used to be read as "background colour behind the run", which drew a filled
// black box behind every line of the chess terminal. Three things rule that out
// and pin the reading above:
//
//   * There are five <b> uses in the whole game and they are two authorings of
//     the same two strings: AUTOTEXT ChessIM07/ChessIM08 are "<n><b4><c6>" and
//     "<n><b4><c3>", and UITEXT Chess_ResponsePrefix/Chess_PlayerPrefix are the
//     same lines with <b10>. 4 and 10 are both FONT ids (UIFont and
//     Typewriter) and 10 is the very id the chess record carries as its own
//     font, whereas COLR only goes up to 8 - as a colour <b10> would resolve to
//     nothing while <b4> painted black, so the two authorings of one line would
//     not look alike. As fonts they are simply two choices of face.
//   * The retail recording of the terminal (w_video chess_mid/chess_end) has no
//     box behind the text at all: the glyphs sit straight on the blue field.
//   * Its lines step 16.0px and "Il Capitano: KD2" measures 101px there. FONT 4
//     is Tahoma authored 16 - a 16px advance - and Tahoma Bold at that cell is
//     110px against the regular weight's 96px. Bold, and font 4, both measured.
//     (The third AUTOTEXT use, ChessIM04, is the terminal's "closed for
//     maintenance" line - the same screen, the same <b4>.)

struct TextStyle {
	const Graphics::Font *font = nullptr;
	uint32 colour = 0;
	bool underline = false;
};

// One stretch of text sharing a single style. `lineBreak` marks a run that a <n>
// pushed onto a new line.
struct StyledRun {
	Common::String text;
	TextStyle style;
	bool lineBreak = false;
};

static uint32 argbToSurface(const Graphics::ManagedSurface &surf, uint32 argb) {
	return surf.format.ARGBToColor((argb >> 24) & 0xff, (argb >> 16) & 0xff,
								   (argb >> 8) & 0xff, argb & 0xff);
}

// Reads "<x12>" at `pos`. Returns false unless the whole thing is one letter
// followed by nothing or by decimal digits and then '>', which is what keeps
// literal angle brackets in prose from being eaten. Declared in util.h; AR 209
// shares it so its character count matches what this file draws.
bool parseMarkupCode(const Common::String &in, uint pos, char &letter, int &number, uint &endPos) {
	if (pos >= in.size() || in[pos] != '<') {
		return false;
	}

	uint i = pos + 1;
	if (i >= in.size() || !Common::isAlpha(in[i])) {
		return false;
	}

	letter = in[i++];
	number = -1;

	int value = 0;
	bool hasDigits = false;
	while (i < in.size() && Common::isDigit(in[i])) {
		value = value * 10 + (in[i] - '0');
		hasDigits = true;
		++i;
		if (value > 9999) {
			return false;
		}
	}

	if (i >= in.size() || in[i] != '>') {
		return false;
	}

	if (hasDigits) {
		number = value;
	}

	endPos = i;	// index of '>'
	return true;
}

// Splits `in` into styled runs. Unknown or unresolvable codes are dropped
// without changing the style, which is what the old stripTextMarkup() did to
// every code.
static void parseStyledText(const Common::String &in, const TextStyle &base,
		const Graphics::ManagedSurface &surf, Common::Array<StyledRun> &out) {
	auto *fonts = (const FontRegistry *)g_nancy->getEngineData("FONTREG");

	TextStyle style = base;
	Common::String pending;
	bool pendingBreak = false;

	auto flush = [&]() {
		if (pending.empty()) {
			return;
		}

		StyledRun run;
		run.text = pending;
		run.style = style;
		run.lineBreak = pendingBreak;
		out.push_back(run);
		pending.clear();
		pendingBreak = false;
	};

	for (uint i = 0; i < in.size(); ++i) {
		char letter = 0;
		int number = -1;
		uint end = 0;

		if (!parseMarkupCode(in, i, letter, number, end)) {
			pending += in[i];
			continue;
		}

		const char lower = Common::isUpper(letter) ? (char)(letter - 'A' + 'a') : letter;

		switch (lower) {
		case 'c':
			if (number >= 0 && fonts) {
				const FontRegistry::ColourEntry *entry = fonts->findColourByID((uint32)number);
				if (entry) {
					flush();
					// Slot 0 is NORMAL. The three other slots only ever differ on
					// COLR 0, and nothing here is in a hover or pressed state at
					// draw time, so NORMAL is the one to take.
					style.colour = argbToSurface(surf, entry->argb[0]);
				}
			}
			break;
		case 'b':
		case 'f':
			// Same argument, same table; <b> asks for the bold weight of it.
			if (number >= 0) {
				const Graphics::Font *font = g_nancy->_ttfFonts.getByID((uint32)number, lower == 'b');
				if (font) {
					flush();
					style.font = font;
				}
			}
			break;
		case 'n':
			if (number < 0) {
				flush();
				pendingBreak = true;
			}
			break;
		case 'u':
			if (number < 0) {
				flush();
				style.underline = !style.underline;
			}
			break;
		default:
			break;
		}

		i = end;
	}

	flush();

	// A trailing <n> with nothing after it still has to survive as an empty run,
	// or a caption that ends in one loses its blank line.
	if (pendingBreak) {
		StyledRun run;
		run.style = style;
		run.lineBreak = true;
		out.push_back(run);
	}
}

// One laid-out piece of a line: a maximal stretch of one style at a known x.
struct LayoutPiece {
	Common::String text;
	TextStyle style;
	int x = 0;
	int width = 0;
};

struct LayoutLine {
	Common::Array<LayoutPiece> pieces;
	int width = 0;
	int height = 0;
};

// Greedy word wrap across style boundaries. Widths are measured with each
// piece's own font, so a <f16> in the middle of a sentence still wraps correctly.
static void layOutStyledText(const Common::Array<StyledRun> &runs, int wrapWidth,
		Common::Array<LayoutLine> &lines) {
	lines.push_back(LayoutLine());

	auto lineIsEmpty = [&]() {
		return lines.back().pieces.empty();
	};

	for (uint r = 0; r < runs.size(); ++r) {
		const StyledRun &run = runs[r];

		if (run.lineBreak) {
			lines.push_back(LayoutLine());
		}

		if (!run.style.font || run.text.empty()) {
			continue;
		}

		const Graphics::Font *font = run.style.font;
		// The authored line height, not the substitute face's hhea height; see
		// TTFFontProvider::lineHeight. A line mixing fonts still takes the tallest.
		const int fontHeight = (int)g_nancy->_ttfFonts.lineHeight(font);

		uint pos = 0;
		while (pos < run.text.size()) {
			// A token is one word plus the spaces that follow it, so a wrap never
			// leaves a dangling space at the end of a line.
			uint wordEnd = pos;
			while (wordEnd < run.text.size() && run.text[wordEnd] != ' ') {
				++wordEnd;
			}

			uint tokenEnd = wordEnd;
			while (tokenEnd < run.text.size() && run.text[tokenEnd] == ' ') {
				++tokenEnd;
			}

			Common::String word(run.text.c_str() + pos, wordEnd - pos);
			Common::String token(run.text.c_str() + pos, tokenEnd - pos);
			const int wordWidth = font->getStringWidth(word);

			if (!lineIsEmpty() && lines.back().width + wordWidth > wrapWidth) {
				lines.push_back(LayoutLine());
				// The token keeps the spaces that follow its word even when it
				// starts a wrapped line. Dropping them here used to weld the word
				// onto the next one ("about a nice" wrapped as "about" / "anice"),
				// because a following token merges straight into the same piece.
			}

			if (lineIsEmpty() && word.empty()) {
				// A run of spaces at the very start of a line: nothing to show
				pos = tokenEnd;
				continue;
			}

			LayoutLine &line = lines.back();
			const int tokenWidth = font->getStringWidth(token);

			// Merge into the previous piece when the style has not changed, so a
			// line usually ends up as a single drawString call.
			bool merged = false;
			if (!line.pieces.empty()) {
				LayoutPiece &prev = line.pieces.back();
				if (prev.style.font == run.style.font && prev.style.colour == run.style.colour &&
						prev.style.underline == run.style.underline) {
					prev.text += token;
					prev.width = font->getStringWidth(prev.text);
					merged = true;
				}
			}

			if (!merged) {
				LayoutPiece piece;
				piece.text = token;
				piece.style = run.style;
				piece.x = line.width;
				piece.width = tokenWidth;
				line.pieces.push_back(piece);
			}

			line.width += tokenWidth;
			line.height = MAX(line.height, fontHeight);
			pos = tokenEnd;
		}
	}
}

// The surface-taking core of the two drawStyledText entry points. Kept separate
// so callers that are not NDUI panels (the chess terminal action record) can lay
// out the same markup into a surface of their own.
static int drawStyledTextImpl(Graphics::ManagedSurface &surf, const Common::String &text,
		const TextStyle &base, int x, int y, int wrapWidth, Graphics::TextAlign align,
		uint *piecesDrawnOut, uint skipLines = 0, uint maxLines = 0) {
	if (!base.font) {
		return 0;
	}

	Common::Array<StyledRun> runs;
	parseStyledText(text, base, surf, runs);

	Common::Array<LayoutLine> lines;
	layOutStyledText(runs, wrapWidth, lines);

	const uint firstLine = MIN<uint>(skipLines, lines.size());
	const uint lastLine = maxLines ? MIN<uint>(firstLine + maxLines, lines.size()) : lines.size();

	const int defaultHeight = (int)g_nancy->_ttfFonts.lineHeight(base.font);
	int cursorY = y;
	uint piecesDrawn = 0;

	for (uint l = firstLine; l < lastLine; ++l) {
		const LayoutLine &line = lines[l];
		const int lineHeight = line.height ? line.height : defaultHeight;

		int offset = 0;
		if (align == Graphics::kTextAlignRight) {
			offset = wrapWidth - line.width;
		} else if (align == Graphics::kTextAlignCenter) {
			offset = (wrapWidth - line.width) / 2;
		}

		for (uint p = 0; p < line.pieces.size(); ++p) {
			const LayoutPiece &piece = line.pieces[p];
			++piecesDrawn;
			const int px = x + offset + piece.x;
			// Font::drawString takes the top of the cell, so this sits a shorter
			// piece on the line's bottom edge. Clamped at 0 because the line box
			// is now the authored height, which a substitute's own cell can
			// exceed - without the clamp a tall substitute would draw above the
			// line and, on the first line, outside the panel.
			const int baseline = cursorY + MAX(0, lineHeight - piece.style.font->getFontHeight());

			piece.style.font->drawString(&surf, piece.text, px, baseline,
				MAX(0, wrapWidth - offset - piece.x), piece.style.colour);

			if (piece.style.underline) {
				const int uy = baseline + piece.style.font->getFontHeight() - 1;
				Common::Rect rule(px, uy, px + piece.style.font->getStringWidth(piece.text), uy + 1);
				rule.clip(Common::Rect(surf.w, surf.h));
				if (!rule.isEmpty()) {
					surf.fillRect(rule, piece.style.colour);
				}
			}
		}

		// No leading. Tahoma's hhea lineGap is 0 and so is Courier New's, so GDI's
		// tmExternalLeading was 0 and the original advanced by the cell height
		// alone. The +1 that used to be here was what turned an authored 12 into
		// an 11px pitch against the recording's measured 12.0.
		cursorY += lineHeight;
	}

	if (piecesDrawnOut) {
		*piecesDrawnOut = piecesDrawn;
	}

	return cursorY - y;
}

int NDUIPanel::drawStyledText(const Common::String &text, const TextStyle &base,
		int x, int y, int wrapWidth, Graphics::TextAlign align,
		uint skipLines, uint maxLines) {
	uint piecesDrawn = 0;
	const int height = drawStyledTextImpl(_drawSurface, text, base, x, y, wrapWidth, align,
		&piecesDrawn, skipLines, maxLines);

	if (piecesDrawn) {
		++_textCount;
	}

	return height;
}

int drawStyledTextToSurface(Graphics::ManagedSurface &surf, const Common::String &text,
		int baseFontID, int baseColourID,
		int x, int y, int wrapWidth, Graphics::TextAlign align) {
	auto *fonts = (const FontRegistry *)g_nancy->getEngineData("FONTREG");

	TextStyle base;
	base.font = g_nancy->_ttfFonts.getByID((uint32)baseFontID);

	if (fonts && baseColourID >= 0) {
		const FontRegistry::ColourEntry *entry = fonts->findColourByID((uint32)baseColourID);
		if (entry) {
			base.colour = argbToSurface(surf, entry->argb[0]);
		}
	}

	return drawStyledTextImpl(surf, text, base, x, y, wrapWidth, align, nullptr);
}

void NDUIPanel::drawControlText(const NDUIControl &control, const Common::Point &origin) {
	if (control.elements.empty()) {
		return;
	}

	// CCText carries an authored placeholder caption in UITEXT. While a live VO
	// line is showing, drawNarration() owns this control - drawing both put the
	// placeholder and the real line on top of each other.
	if (!_narrationCaption.empty() && control.name == "CCText") {
		return;
	}

	Common::String caption;
	if (control.classID == kNDUIClassVarStatic) {
		// The whole point of the class: the string is the bound value. Plain
		// decimal, no padding and no unit - the coin purse in the recording
		// reads "47", not "47L" or "047".
		int16 value = 0;
		if (!nduiBoundValue(control, value)) {
			return;
		}

		caption = Common::String::format("%d", value);
	} else {
		if (control.captionTextID.empty()) {
			return;
		}

		auto *texts = (const CVTX *)g_nancy->getEngineData("UITEXT");
		if (!texts || !texts->texts.contains(control.captionTextID)) {
			// The CarCat card slots reference ids that exist in no table; the game
			// fills them at runtime. Nothing to draw yet, and not an error.
			return;
		}

		caption = texts->texts[control.captionTextID];
	}

	const NDUIElement &el = control.elements[0];

	TextStyle style;
	style.font = g_nancy->_ttfFonts.get(el.fontName, el.boldOrRuntimeTemplate != 0);
	if (!style.font) {
		return;
	}

	// fontColors[0] is the NORMAL slot, stored big-endian ARGB. Inline <cN> codes
	// in the string override it per run.
	style.colour = argbToSurface(_drawSurface, el.fontColors[0]);

	// The three alignment fields are one Text::Align triple; only the horizontal
	// one affects layout here. 0 LEFT, 1 RIGHT, 2 CENTER.
	Graphics::TextAlign align = Graphics::kTextAlignLeft;
	if (el.hAlign == 1) {
		align = Graphics::kTextAlignRight;
	} else if (el.hAlign == 2) {
		align = Graphics::kTextAlignCenter;
	}

	// Vertically centre within the control, which is what the authored heights
	// imply - a 16px font in a 16px box. Everything here is in output space; the
	// bounds are authored, so they convert on the way in.
	Common::Rect authored(
		control.bounds.left + origin.x, control.bounds.top + origin.y,
		control.bounds.right + origin.x, control.bounds.bottom + origin.y);

	// A check box or radio button shares its rect with its mark: DXUT's
	// CDXUTCheckBox::UpdateRect puts a square of the control's height at the left
	// and starts the caption at 1.25 times that width. Without the offset the tick
	// drawn by drawSetMark() lands on the first letter of "Fullscreen 1".
	if (control.classID == kNDUIClassCheckBox || control.classID == kNDUIClassRadioButton) {
		if (!control.checkedImageName.empty()) {
			authored.left += (authored.height() * 5) / 4;
		}
	}

	const Common::Rect box = toScreen(authored);

	const int y = box.top + (box.height() - style.font->getFontHeight()) / 2;

	if (el.wordWrap == 0) {
		// Word wrap is authored off on every fixed-size label, and off means
		// "one line, overflowing the box if it has to" - the box then only says
		// where that line is anchored. Wrapping them anyway is what stacked the
		// options screen's three volume labels on top of each other: "Voice
		// Volume" does not fit its 94px box, so "Volume" fell onto the next
		// label's row.
		const int width = style.font->getStringWidth(caption);
		int x = box.left;
		if (align == Graphics::kTextAlignRight) {
			x = box.right - width;
		} else if (align == Graphics::kTextAlignCenter) {
			x = box.left + (box.width() - width) / 2;
		}

		// Overflowing the *box* is authored and is what the original did too:
		// measured out of the real Tahoma, "Effects Volume" at the authored cell
		// 16 is 87px wide and its box is 75, and five more captions on this one
		// screen are between 4 and 15px too long for their rects. Overflowing the
		// *surface* is ours, and it is destructive: a panel composes into one
		// surface sized to the union of its widget rects, so once a caption ran
		// past the panel it lost whole glyphs at the edge - the options screen
		// read "ffects Volume" the moment the player picked Large Text, which is
		// the very radio button sitting under it. Anchor a caption that cannot
		// fit inside the panel rather than cutting it in half.
		x = CLIP<int>(x, 0, MAX(0, (int)_drawSurface.w - width));

		// Wrap width is the measured line plus slack rather than the box, so the
		// layout pass has no reason to break it. The alignment has already been
		// resolved into x, so the text itself is laid out flush left.
		drawStyledText(caption, style, x, y, width + 8, Graphics::kTextAlignLeft);
		return;
	}

	drawStyledText(caption, style, box.left, y, box.width(), align);
}

void NDUIPanel::layOutFlowChildren(const NDUI *chunk) {
	_flowPos.clear();

	// The taskbar groups author every button at 0,0 and let the panel space them
	// out - four buttons in each group all claim the same rect. Which way they
	// flow, and by how much, is authored in the panel's own 42-byte header
	// rather than being a property of the engine:
	//
	//   panelHeader[1] layoutMode  0 = no layout pass, 1 = forward, 2 = reverse
	//   panelHeader[3] gapX  [4] gapY
	//   panelHeader[5..8]    padding (left, top, right, bottom)
	//
	// Twelve of the game's forty panels set layoutMode; exactly one of them,
	// LOWERMATTE[4] `LowerMatteRight`, sets mode 2. Its children are authored
	// ShowQuitGame, ShowSave, ShowLoad, ShowOptions and the shipped game places
	// them from the panel's *right* edge, so they read options/load/save/quit
	// left to right - the mirror image of LOWERMATTE[3] `LowerMatteLeft`.
	//
	// Flowing that group forward mirrored the whole strip against the retail
	// game. Art and hit rect both come from _flowPos, so an icon never lied
	// about its own action; what went wrong is that the button in each screen
	// position was the wrong one, and the position a player reaches for to open
	// Options is the position that quit the game.
	//
	// The rule also fixes where the buttons sit. The previous pass invented an
	// even spread across the panel box; the authored one anchors to the padding
	// edge, which is why the left group used to slide sideways whenever ShowPDA
	// was hidden. Against the retail recording the authored rule is exact:
	// screen x 8/48/88/128 on the left, 478/518/558/598 on the right, y 437.
	if (!chunk->hasPanel) {
		return;
	}

	const uint32 layoutMode = chunk->panelHeader[1];
	if (layoutMode != kNDUILayoutForward && layoutMode != kNDUILayoutReverse) {
		// Mode 0 panels position every child from its own authored bounds. All
		// 28 of them also carry a zero gap and zero padding, i.e. the fields are
		// only ever set when the layout pass is meant to run.
		return;
	}

	Common::Array<const NDUIControl *> flow;
	for (uint i = 0; i < chunk->children.size(); ++i) {
		const NDUIControl &c = chunk->children[i];
		if (c.name.empty() || c.elements.empty() || c.bounds.left != 0 || c.bounds.top != 0) {
			continue;
		}

		// The pass walks visible children only, which is what lets eight buttons
		// share four slots: ShowInv and HideInv swap, and the survivors close up.
		if (_visible.contains(c.name) && !_visible[c.name]) {
			continue;
		}

		if (!c.elements[0].sourceRect.isEmpty()) {
			flow.push_back(&c);
		}
	}

	if (flow.empty()) {
		return;
	}

	const Common::Rect &box = chunk->panelStatic.bounds;
	const int gapX = (int)chunk->panelHeader[3];
	const int gapY = (int)chunk->panelHeader[4];
	const int padLeft = (int)chunk->panelHeader[5];
	const int padTop = (int)chunk->panelHeader[6];
	const int padRight = (int)chunk->panelHeader[7];

	const int rowLeft = padLeft;
	const int rowRight = MAX(rowLeft, box.width() - padRight);
	const bool reverse = (layoutMode == kNDUILayoutReverse);

	// One cursor along the row, running from whichever edge the mode names, and
	// wrapping to a new row when the next child would overhang the far one.
	int cursor = reverse ? rowRight : rowLeft;
	int rowTop = padTop;
	int rowHeight = 0;

	for (uint i = 0; i < flow.size(); ++i) {
		const Common::Rect &art = flow[i]->elements[0].sourceRect;
		const int w = art.width();
		const int h = art.height();

		const bool overhangs = reverse ? (cursor - w < rowLeft) : (cursor + w > rowRight);
		if (overhangs && rowHeight > 0) {
			cursor = reverse ? rowRight : rowLeft;
			rowTop += rowHeight + gapY;
			rowHeight = 0;
		}

		const int x = reverse ? cursor - w : cursor;
		_flowPos[flow[i]->name] = Common::Point(x, rowTop);

		cursor = reverse ? (x - gapX) : (x + w + gapX);
		rowHeight = MAX(rowHeight, h);
	}
}

void NDUIPanel::setStartsHidden() {
	_startsHidden = true;
	_wasEverVisible = false;

	if (_chunk && !_chunk->panelStatic.name.empty()) {
		_visible[_chunk->panelStatic.name] = false;
		// Part of how the panel was built, not something the player did, so it
		// belongs in the baseline too - otherwise "restore to baseline" would
		// quietly put the inventory and conversation panels back on screen.
		_baselineVisible[_chunk->panelStatic.name] = false;
	}
}

bool NDUIPanel::isInventoryPanel() const {
	return _chunk && const_cast<NDUIPanel *>(this)->findControl("InvDialog") != nullptr;
}

void NDUIPanel::refreshInventory() {
	redraw();
}

void NDUIPanel::refreshBoundValues() {
	if (!_chunk || !_chunk->hasPanel) {
		return;
	}

	// Only the widgets that are actually on screen matter: a hidden coin purse
	// is re-read the moment it is shown, because init() draws from the live
	// value rather than from this cache.
	bool changed = false;
	for (uint i = 0; i < _chunk->children.size(); ++i) {
		const NDUIControl &c = _chunk->children[i];
		if (c.classID != kNDUIClassVarStatic || c.name.empty()) {
			continue;
		}

		if (_visible.contains(c.name) && !_visible[c.name]) {
			continue;
		}

		int16 value = 0;
		if (!nduiBoundValue(c, value)) {
			continue;
		}

		if (!_boundValues.contains(c.name) || _boundValues[c.name] != value) {
			_boundValues[c.name] = value;
			changed = true;
		}
	}

	if (changed) {
		redraw();
	}
}

// The authored OnShow/OnHide lists are how one widget drives another - the
// taskbar's ShowInv swaps itself for HideInv this way, and InvDialog pulls its
// own backdrop up. They cross panel boundaries, so they go through the scene.
void NDUIPanel::fireVisibilityBindings(const Common::String &target, bool shown) {
	NDUIControl *control = findControl(target);
	if (!control) {
		return;
	}

	const int32 wanted = shown ? kNDUIEventOnShow : kNDUIEventOnHide;
	for (uint i = 0; i < control->actions.size(); ++i) {
		const NDUIAction &action = control->actions[i];
		if (action.eventID != wanted || action.target.empty()) {
			continue;
		}

		if (action.commandID == kNDUICommandShow || action.commandID == kNDUICommandHide) {
			NancySceneState.applyNDUICommand(action.target, action.commandID);
		}
	}
}

bool NDUIPanel::applyCommand(const Common::String &target, uint32 commandID) {
	if (!findControl(target)) {
		return false;
	}

	switch (commandID) {
	case kNDUICommandShow:
	case kNDUICommandHide: {
		const bool show = (commandID == kNDUICommandShow);
		if (_visible.contains(target) && _visible[target] == show) {
			return true;	// already in that state; do not re-fire the bindings
		}

		_visible[target] = show;

		// A panel whose own root is being shown or hidden comes on and off
		// screen with it, rather than merely re-composing.
		if (_chunk && target.equalsIgnoreCase(_chunk->panelStatic.name)) {
			_wasEverVisible = show;
			setVisible(show);

			// A settings screen opening shows what is configured *now*, not what
			// it was left showing. Nothing else can move these - ScummVM's own
			// options dialog writes the same keys - so they are re-read here
			// rather than kept in step.
			if (show) {
				_sliderValues.clear();
				_sliderDrag = -1;
			}
		}

		redraw();
		fireVisibilityBindings(target, show);
		return true;
	}
	case kNDUICommandSet:
	case kNDUICommandClear:
		// Against a widget these enable and disable rather than show and hide.
		// Nothing renders a disabled state yet, so this is recorded and not drawn.
		_enabled[target] = (commandID == kNDUICommandSet);
		return true;
	default:
		return false;
	}
}

Common::String NDUIPanel::getPanelName() const {
	return (_chunk && _chunk->hasPanel) ? _chunk->panelStatic.name : Common::String();
}

void NDUIPanel::getStateDelta(Common::StringArray &shownNames, Common::Array<byte> &shownValues,
		Common::StringArray &enabledNames, Common::Array<byte> &enabledValues) const {
	for (auto &v : _visible) {
		// A name absent from the baseline cannot happen - _visible only ever
		// gains entries through applyCommand, against controls the seed already
		// walked - but treating "absent" as the authored-visible default keeps
		// this total rather than relying on that.
		const bool base = _baselineVisible.contains(v._key) ? _baselineVisible.getVal(v._key) : true;
		if (v._value != base) {
			shownNames.push_back(v._key);
			shownValues.push_back(v._value ? 1 : 0);
		}
	}

	// _enabled starts empty and is only ever written by NDUI command 8/9, so
	// every entry in it is already a delta.
	for (auto &e : _enabled) {
		enabledNames.push_back(e._key);
		enabledValues.push_back(e._value ? 1 : 0);
	}
}

void NDUIPanel::applyStateDelta(const Common::StringArray &shownNames, const Common::Array<byte> &shownValues,
		const Common::StringArray &enabledNames, const Common::Array<byte> &enabledValues) {
	if (!_chunk || !_chunk->hasPanel || _baselineVisible.empty()) {
		return;
	}

	// Reset first. An in-session load (the second-chance AR 115, or the Load
	// button) runs against panels that already carry the pre-load session's
	// deltas, and a delta-only save cannot express "this one went back to
	// authored" without a known starting point.
	_visible = _baselineVisible;
	_enabled.clear();

	for (uint i = 0; i < shownNames.size() && i < shownValues.size(); ++i) {
		if (findControl(shownNames[i])) {
			_visible[shownNames[i]] = (shownValues[i] != 0);
		}
	}

	for (uint i = 0; i < enabledNames.size() && i < enabledValues.size(); ++i) {
		if (findControl(enabledNames[i])) {
			_enabled[enabledNames[i]] = (enabledValues[i] != 0);
		}
	}

	// Re-compose, then settle the panel's own on-screen flag - in that order.
	// init() ends with setVisible(_wasEverVisible || !_startsHidden), so a
	// redraw would undo a hide applied before it on any panel that does not
	// start hidden. Both halves have to agree: _visible[root] is what a later
	// Show is compared against ("already in that state; do not re-fire the
	// bindings"), so a root hidden on screen but still true in the map would
	// swallow the Show that is meant to bring it back.
	redraw();

	const Common::String &root = _chunk->panelStatic.name;
	if (!root.empty() && _visible.contains(root)) {
		_wasEverVisible = _visible[root];
		setVisible(_visible[root]);
	}
}

bool NDUIPanel::isConversationPanel() const {
	return _chunk && const_cast<NDUIPanel *>(this)->findControl("ConvoCCText") != nullptr;
}

void NDUIPanel::convoOpen() {
	_convoCaption.clear();
	_convoResponses.clear();
	_convoResponseRects.clear();
	_convoPicked = -1;
	_wasEverVisible = true;
	redraw();
	NancySceneState.setConversationUIVisible(true);
}

void NDUIPanel::convoClose() {
	_convoCaption.clear();
	_convoResponses.clear();
	_convoResponseRects.clear();
	_convoPicked = -1;
	_wasEverVisible = false;
	NancySceneState.setConversationUIVisible(false);
}

void NDUIPanel::convoSetCaption(const Common::String &text) {
	_convoCaption = text;
	redraw();
}

uint NDUIPanel::convoAddResponse(const Common::String &text) {
	_convoResponses.push_back(text);
	redraw();
	return _convoResponses.size() - 1;
}

int NDUIPanel::convoTakePickedResponse() {
	const int picked = _convoPicked;
	_convoPicked = -1;
	return picked;
}

void NDUIPanel::debugGetConvoResponseRects(Common::Array<Common::Rect> &out) const {
	if (!_isVisible) {
		return;
	}

	// Same translation handleInput applies: the rects are recorded relative to
	// the panel surface, which is already at output scale.
	for (uint i = 0; i < _convoResponseRects.size(); ++i) {
		Common::Rect r = _convoResponseRects[i];
		r.translate(_screenPosition.left, _screenPosition.top);
		out.push_back(r);
	}
}

void NDUIPanel::debugGetInvItemRects(Common::Array<DebugInvItem> &out) const {
	if (!_isVisible) {
		return;
	}

	// Same translation handleInput applies to _invRects, and the same order, so
	// out[i] is the rect whose click sets held item _invItems[i].
	for (uint i = 0; i < _invRects.size() && i < _invItems.size(); ++i) {
		DebugInvItem d;
		d.itemID = (int)_invItems[i];
		d.rect = _invRects[i];
		d.rect.translate(_screenPosition.left, _screenPosition.top);
		out.push_back(d);
	}
}

// Debug affordance, mirroring debugGetConvoResponseRects: report every widget
// handleInput() would currently accept a click on, with the same visibility,
// enablement and rect arithmetic, so a headless probe can drive the NDUI. The
// PDA (and with it the whole chase interface) is authored entirely as NDUI
// widgets, so without this there is no way to know what is clickable.
void NDUIPanel::debugGetClickableWidgets(Common::Array<DebugWidget> &out) const {
	if (!_chunk || !_chunk->hasPanel || !_isVisible) {
		return;
	}

	const Common::Point origin(_chunk->panelStatic.bounds.left, _chunk->panelStatic.bounds.top);

	for (int i = (int)_chunk->children.size() - 1; i >= 0; --i) {
		const NDUIControl &c = _chunk->children[i];

		if (c.bounds.isEmpty() || c.actions.empty()) {
			continue;
		}

		if (_visible.contains(c.name) && !_visible[c.name]) {
			continue;
		}

		if (_enabled.contains(c.name) && !_enabled[c.name]) {
			continue;
		}

		bool hasClick = false;
		Common::String acts;
		for (uint a = 0; a < c.actions.size(); ++a) {
			const NDUIAction &action = c.actions[a];
			if (action.eventID != kNDUIEventClick) {
				continue;
			}

			hasClick = true;
			acts += Common::String::format("%s%s(%u,%s,%d)", acts.empty() ? "" : "+",
				action.commandID == kNDUICommandShow ? "show" :
				action.commandID == kNDUICommandHide ? "hide" :
				action.commandID == kNDUICommandSet ? "set" :
				action.commandID == kNDUICommandClear ? "clear" :
				action.commandID == kNDUICommandInvoke ? "invoke" :
				action.commandID == kNDUICommandNotify ? "notify" :
				action.commandID == kNDUICommandSetValue ? "setvalue" : "cmd",
				action.commandID, action.target.c_str(), (int)action.paramID);
		}

		if (!hasClick) {
			continue;
		}

		Common::Rect screenRect = c.bounds;
		if (_flowPos.contains(c.name)) {
			const Common::Rect &art = c.elements[0].sourceRect;
			screenRect = Common::Rect(art.width(), art.height());
			screenRect.translate(_flowPos[c.name].x, _flowPos[c.name].y);
		}

		screenRect.translate(origin.x, origin.y);
		screenRect = toScreen(screenRect);

		DebugWidget w;
		w.name = c.name;
		w.rect = screenRect;
		w.actions = acts;
		out.push_back(w);
	}
}

// Lays the NPC line and the player's responses into the panel. ConvoCCText and
// ConvoResponses are both authored zero-height: the game grows them at runtime,
// so the heights here come from the wrapped text rather than from the file.
//
// The CONVO strings carry inline markup - captions open with <c1> and responses
// with <c0> - which drawStyledText resolves against the COLR table. The widget
// colours below are only the fallback for a string that carries no code at all:
// ConvoCCText declares FF879BFF and the ConvoResponse template FF8F8FC8.
// The blank the original leaves between the caption and the first response, and
// between successive responses. Measured on the recording as the amount by which
// consecutive response starts exceed a plain line step: S1544 356/373/389 and
// S1250, S1205, S1249, S1224, S1261, S1127 all put it at 16-17px against a step
// of 12, i.e. a blank of 4-5px.
//
// That is half the character height, not half the cell: half the 12px cell would
// be 6 and would space the responses 18 apart. getFontAscent() is the closest
// thing Graphics::Font exposes to the em box - it is 10 for the Convo face - so
// half of it gives 5, and 12 + 5 = 17.
static int convoResponseGap(const Graphics::Font *font) {
	return font->getFontAscent() / 2;
}

void NDUIPanel::drawConversation() {
	_convoResponseRects.clear();

	if (!_chunk || (_convoCaption.empty() && _convoResponses.empty())) {
		return;
	}

	NDUIControl *anchor = findControl("ConvoCCText");
	if (!anchor) {
		return;
	}

	// The anchor is a child of the panel's Static, so its bounds are relative to
	// the panel box, which is where the surface starts. Everything below is in
	// output space.
	const Common::Rect &box = _chunk->panelStatic.bounds;
	const int left = toScreen(anchor->bounds.left);
	const int wrapWidth = toScreen(anchor->bounds.width() ? anchor->bounds.width() : box.width());

	const Common::String fontName = anchor->elements.empty() ? Common::String("Convo")
		: anchor->elements[0].fontName;

	TextStyle captionStyle;
	captionStyle.font = g_nancy->_ttfFonts.get(fontName, false);
	if (!captionStyle.font) {
		return;
	}

	captionStyle.colour = anchor->elements.empty()
		? _drawSurface.format.RGBToColor(0xff, 0xff, 0xff)
		: argbToSurface(_drawSurface, anchor->elements[0].fontColors[0]);

	TextStyle responseStyle = captionStyle;
	NDUIControl *responseTemplate = findControl("ConvoResponse");
	if (responseTemplate && !responseTemplate->elements.empty()) {
		const NDUIElement &el = responseTemplate->elements[0];
		if (const Graphics::Font *f = g_nancy->_ttfFonts.get(el.fontName, false)) {
			responseStyle.font = f;
		}

		responseStyle.colour = argbToSurface(_drawSurface, el.fontColors[0]);
	}

	int y = toScreen(anchor->bounds.top);

	if (!_convoCaption.empty()) {
		y += drawStyledText(_convoCaption, captionStyle, left, y, wrapWidth, Graphics::kTextAlignLeft);
		y += convoResponseGap(captionStyle.font);	// a blank half-line before the responses
	}

	for (uint r = 0; r < _convoResponses.size(); ++r) {
		if (r) {
			// The original separates successive choices with the same blank
			// half-line it puts after the caption, not just the caption from the
			// first choice. Measured against the retail recording, which spaces
			// the starts of consecutive response lines 16-17px apart where a
			// plain line step is 12: nancy18 S1544 (three choices, gaps 17 and
			// 16) and S1338 (two choices, gap 16). Without this we stack them a
			// bare line step apart.
			//
			// Deliberately added before `top` so the gap stays outside both
			// hotspots: widening a response's rect could move which choice a
			// click lands on, and nothing here needs that.
			y += convoResponseGap(responseStyle.font);
		}

		const int top = y;
		y += drawStyledText(_convoResponses[r], responseStyle, left, y, wrapWidth, Graphics::kTextAlignLeft);

		// Recorded relative to the panel surface, which is already at output
		// scale; handleInput only has to add the surface's screen position.
		_convoResponseRects.push_back(Common::Rect(left, top, left + wrapWidth, y));
	}
}

bool NDUIPanel::isStakeoutPanel() const {
	return _chunk && const_cast<NDUIPanel *>(this)->findControl("StakeOutDialog") != nullptr;
}

void NDUIPanel::stakeoutSetLines(const Common::StringArray &lines) {
	if (_stakeoutLines.size() == lines.size()) {
		bool same = true;
		for (uint i = 0; i < lines.size(); ++i) {
			if (_stakeoutLines[i] != lines[i]) {
				same = false;
				break;
			}
		}

		if (same) {
			return;
		}
	}

	_stakeoutLines = lines;
	redraw();
}

// The rows are laid out at runtime, exactly like the conversation replies: the
// STAKEOUT member ships one row template rather than N authored rows, so there
// is nothing in the file to position.
//
// `StakeOutDialog` is its chunk's panel root, so its bounds are absolute and the
// composed surface already starts at them - unlike ConvoCCText, which is a child
// of the CONVO panel and carries its own offset. Rebasing against
// _surfaceTopLeft is what makes both cases come out at 0.
void NDUIPanel::drawStakeout() {
	if (!_chunk || _stakeoutLines.empty()) {
		return;
	}

	NDUIControl *anchor = findControl("StakeOutDialog");
	if (!anchor) {
		return;
	}

	if (_visible.contains(anchor->name) && !_visible[anchor->name]) {
		return;
	}

	// The row template lives in a 0x28 chunk with no panel of its own, so the
	// scene drops it and findControl cannot see it - the same situation
	// drawConversation is in with the ConvoResponse template. Fall back to the
	// dialog's own element, which names the same Convo font family; every line
	// carries its own <cN> colour code anyway, so the element colour below is
	// only the fallback for a string with no code at all.
	const Common::String fontName = anchor->elements.empty() ? Common::String("Convo")
		: anchor->elements[0].fontName;

	TextStyle style;
	style.font = g_nancy->_ttfFonts.get(fontName, false);
	if (!style.font) {
		return;
	}

	style.colour = anchor->elements.empty()
		? _drawSurface.format.RGBToColor(0xff, 0xff, 0xff)
		: argbToSurface(_drawSurface, anchor->elements[0].fontColors[0]);

	const int left = toScreen(anchor->bounds.left) - toScreen(_surfaceTopLeft.x);
	const int wrapWidth = toScreen(anchor->bounds.width());
	int y = toScreen(anchor->bounds.top) - toScreen(_surfaceTopLeft.y);

	for (uint i = 0; i < _stakeoutLines.size(); ++i) {
		y += drawStyledText(_stakeoutLines[i], style, left, y, wrapWidth, Graphics::kTextAlignLeft);
	}
}

// --- Journal and task-list rows ----------------------------------------
//
// The same runtime list drawStakeout does, with three additions the two lists
// need and it does not: a real row template (so the task list's tick box has a
// sprite and both lists have their authored font rather than the box's), a
// scroll offset, and per-row hit rects.

void NDUIPanel::setRowTemplate(const NDUIControl &tmpl) {
	delete _rowTemplate;
	_rowTemplate = new NDUIControl(tmpl);
}

void NDUIPanel::setListRows(const Common::String &anchorName, const Common::Array<ListRow> &rows) {
	bool same = _listAnchor.equalsIgnoreCase(anchorName) && _listRows.size() == rows.size();
	for (uint i = 0; same && i < rows.size(); ++i) {
		same = (_listRows[i].text == rows[i].text) && (_listRows[i].checked == rows[i].checked);
	}

	if (same) {
		return;
	}

	// A list that changed under the player starts at the top again; there is no
	// row identity to hold a position against.
	_listAnchor = anchorName;
	_listRows = rows;
	_listScroll = 0;
	_listStops = 0;
	redraw();
}

void NDUIPanel::setListSelection(int index) {
	if (_listSel == index) {
		return;
	}

	_listSel = index;
	redraw();
}

int NDUIPanel::listTakePicked() {
	const int picked = _listPicked;
	_listPicked = -1;
	return picked;
}

// THE SINGLE WRITER of all three list positions. Every requested scroll in the
// game arrives here: an arrow click and a track page through
// handleScrollBarInput, a thumb drag through the same, a routed bar's delta
// through Scene::applyNDUIScroll, and the inventory's kMoveUp/kMoveDown through
// handleInput. Nothing else assigns _saveScroll, _listScroll or _invScroll except
// the two draws, which only re-clamp what this put there.
//
// That matters most for the grid. The keyboard branch used to nudge _invScroll
// itself, with its own half of the clamp (it floored at zero and left the top end
// to the next draw), so the keys and the authored InvScrollBar were two writers
// applying two different limits to one offset.
void NDUIPanel::listScrollBy(int delta) {
	if (!delta) {
		return;
	}

	// Three runtime lists answer to this and no panel owns two of them, so dispatch
	// here rather than teach the callers - the unbound-bar fall-through in
	// handleScrollBarInput, Scene::applyNDUIScroll, and the movement keys - which
	// kind of list a given panel has. Giving the item grid an entry point of its
	// own would push that choice up into applyNDUIScroll, which would then need an
	// isInventoryPanel() test to pick a writer while Scene::nduiScrollModel needed
	// the identical test to pick a reader: two copies of one decision, in the two
	// places this design most needs to agree with itself.
	//
	// LOADGAME's and SAVEGAME's bars arrive by that fall-through: both are
	// authored unnamed, nested inside their own ListBox and carrying no bindings
	// at all (NDUI-LAYOUTS.md:465 and :825), exactly like JournalItemsScrollBar.
	// So the bar was found, hit-tested and its click eaten, and then the delta
	// died on the _listAnchor guard below - the save list is a second list
	// implementation, fed by saveLoadRefresh rather than setListRows, that this
	// fall-through was written without. That is the whole reason the Load
	// dialog's scroll bar did nothing: every save past the first boxful was
	// unreachable by any input the game had. (How many rows fit is deliberately
	// not quoted here - drawSaveLoad measures each row, since a long name wraps,
	// and the pitch it floors them at is getFontHeight(), which returns the
	// substitute font's hhea height and does not track the requested size, so a
	// fixed count would be a guess dressed up as a measurement.)
	//
	// Nothing here tells anyone that the position moved, and nothing needs to: a
	// bar in another panel notices by checking, in NDUIPanel::updateGraphics. See
	// there for why that direction is the one that does not rot.
	if (saveListControl()) {
		// Loose on purpose, the same contract _invScroll relies on: only the draw
		// knows how many rows fit the box, and drawSaveLoad re-clamps against
		// that, so an offset past the end settles back on the next draw.
		const int wanted = CLIP(_saveScroll + delta, 0, MAX(0, (int)_saveRows.size() - 1));
		if (wanted != _saveScroll) {
			_saveScroll = wanted;
			redraw();
		}

		return;
	}

	if (_listAnchor.empty()) {
		// The item grid, third and last, so the two authored lists keep their exact
		// priority and this is reached only by a panel that has neither. This
		// branch IS the InvScrollBar arrow fix: the click already got this far -
		// handleScrollBarInput routed it, applyNDUIScroll matched InvDialog and
		// called in - and then died on this guard, because the grid was a third
		// list that nothing here dispatched. drawInventory's clamp stays as the
		// backstop; clamping here as well only saves a pointless redraw on every
		// click at the bottom of the grid.
		if (isInventoryPanel()) {
			const int wanted = CLIP(_invScroll + delta, 0,
				MAX(0, _invRangeRows - _invPageRows));
			if (wanted != _invScroll) {
				_invScroll = wanted;
				redraw();
			}
		}

		return;
	}

	// Clamped against the stop count drawList worked out, not the row count: a
	// row taller than the box is worth several stops, and drawList re-clamps
	// anyway, so a scroll that arrives before the first draw cannot run away.
	const int limit = MAX(_listStops, (int)_listRows.size());
	const int wanted = CLIP(_listScroll + delta, 0, MAX(0, limit - 1));
	if (wanted == _listScroll) {
		return;
	}

	_listScroll = wanted;
	redraw();
}

// One sub-rect of an atlas, downscaled 4/5 into the output surface. Same
// resampler the artwork pass uses, so a tick box drawn here matches one drawn
// there; it just cannot go through the artwork pass, because a row's position is
// only known once its text has wrapped and that happens after the downscale.
bool NDUIPanel::blitAtlasRectToOutput(const Common::String &atlasName, const Common::Rect &srcRect,
		const Common::Point &dest, const Common::String &controlName) {
	if (atlasName.empty() || srcRect.isEmpty()) {
		return false;
	}

	Graphics::ManagedSurface atlas;
	if (!g_nancy->_resource->loadImage(Common::Path(atlasName), atlas)) {
		warning("NDUI: could not load atlas '%s' for control '%s'",
			atlasName.c_str(), controlName.c_str());
		return false;
	}

	if (srcRect.right > atlas.w || srcRect.bottom > atlas.h) {
		warning("NDUI: source rect for '%s' lies outside atlas '%s'",
			controlName.c_str(), atlasName.c_str());
		return false;
	}

	const int dw = toScreen(srcRect.width());
	const int dh = toScreen(srcRect.height());
	if (dw <= 0 || dh <= 0) {
		return false;
	}

	Graphics::ManagedSurface piece(srcRect.width(), srcRect.height(), _drawSurface.format);
	piece.clear(piece.getTransparentColor());
	piece.blitFrom(atlas, srcRect, Common::Point(0, 0));

	Graphics::ManagedSurface scaled(dw, dh, _drawSurface.format);
	scaled.clear(scaled.getTransparentColor());
	downscaleAuthoredArt(piece, scaled);

	_drawSurface.blitFrom(scaled, Common::Rect(dw, dh), dest);
	++_blitCount;
	return true;
}

// Wrap-only pass. drawSaveLoad can step by a constant font height because a save
// name is one line; a task or a journal note is a wrapped paragraph, so the row
// height has to be known before the row is committed to. The per-line heights
// are what drawList needs to scroll inside a paragraph that is taller than its
// box, so they are handed back rather than summed here.
// `contentLines` is one past the last line that actually has something on it.
// Every string in these two tables ends with a trailing <n>, which lays out as an
// empty line and is what puts the gap between two task rows - so it counts
// towards the row's height, but a scroll stop that would show only those empty
// lines is a blank page and must not be made.
static void measureStyledLines(const Graphics::ManagedSurface &surf, const Common::String &text,
		const TextStyle &base, int wrapWidth, Common::Array<int> &heights, uint *contentLines = nullptr) {
	heights.clear();
	if (contentLines) {
		*contentLines = 0;
	}

	if (!base.font) {
		return;
	}

	Common::Array<StyledRun> runs;
	parseStyledText(text, base, surf, runs);

	Common::Array<LayoutLine> lines;
	layOutStyledText(runs, wrapWidth, lines);

	const int defaultHeight = (int)g_nancy->_ttfFonts.lineHeight(base.font);
	for (uint l = 0; l < lines.size(); ++l) {
		heights.push_back(lines[l].height ? lines[l].height : defaultHeight);
		if (contentLines && !lines[l].pieces.empty()) {
			*contentLines = l + 1;
		}
	}
}

// How many of `heights` from `first` onwards fit in `avail`, and how tall that
// is. Always takes at least one line: a single line taller than the whole box
// still has to be drawn, or the row would show nothing at all.
static uint linesThatFit(const Common::Array<int> &heights, uint first, int avail, int &heightOut) {
	uint n = 0;
	heightOut = 0;
	while (first + n < heights.size()) {
		const int h = heights[first + n];
		if (n && heightOut + h > avail) {
			break;
		}

		heightOut += h;
		++n;
	}

	return n;
}

void NDUIPanel::drawList() {
	_listRowRects.clear();
	_listRowIndices.clear();

	if (!_chunk || _listAnchor.empty() || _listRows.empty()) {
		return;
	}

	NDUIControl *anchor = findControl(_listAnchor);
	if (!anchor) {
		return;
	}

	if (_visible.contains(anchor->name) && !_visible[anchor->name]) {
		return;
	}

	// The anchor is either the panel's own root, whose bounds are absolute, or a
	// child of it, whose bounds are relative to the panel box. Both are put into
	// authored absolute space first and then rebased onto the surface, which is
	// the one form that is right for both - drawStakeout and drawConversation are
	// the two halves of this.
	Common::Rect authored = anchor->bounds;
	if (anchor != &_chunk->panelStatic) {
		authored.translate(_chunk->panelStatic.bounds.left, _chunk->panelStatic.bounds.top);
	}

	authored.translate(-_surfaceTopLeft.x, -_surfaceTopLeft.y);
	const Common::Rect box = toScreen(authored);
	if (box.isEmpty()) {
		return;
	}

	// The row template's descriptor, when the member ships one: its font, its
	// colours, its own left inset and width, and - for the task list - the
	// checked sprite. Falls back to the anchor's element, which is what
	// drawStakeout has to do without a template.
	const NDUIElement *rowEl = nullptr;
	if (_rowTemplate && !_rowTemplate->elements.empty()) {
		rowEl = &_rowTemplate->elements[0];
	} else if (!anchor->elements.empty()) {
		rowEl = &anchor->elements[0];
	}

	TextStyle style;
	style.font = g_nancy->_ttfFonts.get(rowEl ? rowEl->fontName : Common::String("Convo"), false);
	if (!style.font) {
		return;
	}

	const uint32 restARGB = rowEl ? rowEl->fontColors[kNDUIColorNormal] : 0xffffffff;
	const uint32 selARGB = rowEl ? rowEl->fontColors[kNDUIColorMouseOver] : 0xffffffff;

	// A CheckBox template means every row carries a tick box, drawn at the left
	// of the row with the caption starting past it - which is where DXUT puts it,
	// and where drawSetMark already puts the options screen's.
	const bool hasCheckbox = _rowTemplate &&
		_rowTemplate->classID == kNDUIClassCheckBox &&
		!_rowTemplate->checkedImageName.empty() &&
		!_rowTemplate->checkedImageRect.isEmpty();

	const int gutter = hasCheckbox ? toScreen(_rowTemplate->checkedImageRect.width() + 3) : 0;
	const int inset = (_rowTemplate && !_rowTemplate->bounds.isEmpty())
		? toScreen(_rowTemplate->bounds.left) : 0;

	int wrapWidth = box.width() - inset - gutter;
	if (_rowTemplate && _rowTemplate->bounds.width() > 0) {
		wrapWidth = MIN(wrapWidth, toScreen(_rowTemplate->bounds.width()) - gutter);
	}

	if (wrapWidth <= 0) {
		return;
	}

	const int left = box.left + inset + gutter;

	// The scroll stops. A row short enough to be shown whole is one stop, which is
	// what a row has always been and is every row the task list has. A row taller
	// than the box gets one stop per boxful of its own lines, because otherwise
	// its tail has no scroll position at all: the details pane draws one journal
	// note at a time, most notes are longer than the box, and the next click used
	// to jump to the next note rather than to the rest of this one - so the end of
	// nine of the seventeen Observations entries could not be read.
	Common::Array<int> lineHeights;
	Common::Array<uint> stopRow;
	Common::Array<uint> stopLine;
	for (uint i = 0; i < _listRows.size(); ++i) {
		uint contentLines = 0;
		measureStyledLines(_drawSurface, _listRows[i].text, style, wrapWidth, lineHeights, &contentLines);
		if (lineHeights.empty()) {
			continue;
		}

		uint first = 0;
		while (true) {
			stopRow.push_back(i);
			stopLine.push_back(first);

			int taken = 0;
			const uint n = linesThatFit(lineHeights, first, box.height(), taken);
			if (first + n >= contentLines) {
				break;
			}

			first += n;
		}
	}

	_listStops = (int)stopRow.size();
	if (stopRow.empty()) {
		return;
	}

	_listScroll = CLIP(_listScroll, 0, (int)stopRow.size() - 1);

	int y = box.top;
	uint skip = stopLine[_listScroll];
	for (uint i = stopRow[_listScroll]; i < _listRows.size(); ++i) {
		measureStyledLines(_drawSurface, _listRows[i].text, style, wrapWidth, lineHeights);
		if (skip >= lineHeights.size()) {
			skip = 0;
			continue;
		}

		int rowHeight = 0;
		for (uint l = skip; l < lineHeights.size(); ++l) {
			rowHeight += lineHeights[l];
		}

		uint drawLines = 0;
		if (rowHeight <= box.height()) {
			// A row that does not fit is left for the next scroll step rather than
			// clipped in half - except when it is the only one on screen, which is
			// the one case where refusing to draw would show nothing at all.
			if (y + rowHeight > box.bottom && y > box.top) {
				break;
			}
		} else {
			// Taller than the whole box, so no scroll position can ever show it
			// whole. It only starts at the top of the box - the stop that reaches
			// it puts it there - and it takes the boxful of lines that fit.
			if (y > box.top) {
				break;
			}

			drawLines = linesThatFit(lineHeights, skip, box.height(), rowHeight);
		}

		style.colour = argbToSurface(_drawSurface,
			(int)i == _listSel ? selARGB : restARGB);

		if ((int)i == _listSel) {
			// A rule under the picked heading, exactly as drawSaveLoad puts one
			// under the picked save: the element's four colour slots are the only
			// thing the descriptor offers, and on this template MOUSEOVER equals
			// NORMAL, so the colour alone would show nothing.
			Common::Rect underline(box.left, y + rowHeight - 1, box.right, y + rowHeight);
			underline.clip(Common::Rect(_drawSurface.w, _drawSurface.h));
			if (!underline.isEmpty()) {
				_drawSurface.fillRect(underline, style.colour);
			}
		}

		// Only on the row's own first stop: a continuation is the rest of one
		// entry, not a second entry, and must not grow a second tick box.
		if (hasCheckbox && _listRows[i].checked && skip == 0) {
			const int markH = toScreen(_rowTemplate->checkedImageRect.height());
			blitAtlasRectToOutput(_rowTemplate->checkedImageName, _rowTemplate->checkedImageRect,
				Common::Point(box.left + inset,
					y + MAX(0, ((int)g_nancy->_ttfFonts.lineHeight(style.font) - markH) / 2)),
				_rowTemplate->name);
		}

		drawStyledText(_listRows[i].text, style, left, y, wrapWidth, Graphics::kTextAlignLeft,
			skip, drawLines);

		_listRowRects.push_back(Common::Rect(box.left, y, box.right,
			MIN<int>(box.bottom, y + rowHeight)));
		_listRowIndices.push_back(i);
		y += rowHeight;
		skip = 0;
	}
}

// Lays the held items into InvDialog as a grid of 38x38 cells on a 39px stride,
// which is what INVD's source rects and the InvButton template both describe.

bool NDUIPanel::isNarrationPanel() const {
	return _chunk && const_cast<NDUIPanel *>(this)->findControl("CCText") != nullptr;
}

void NDUIPanel::narrationSetCaption(const Common::String &text) {
	if (_narrationCaption == text) {
		return;
	}

	_narrationCaption = text;
	redraw();
}

void NDUIPanel::narrationClear() {
	if (_narrationCaption.empty()) {
		return;
	}

	_narrationCaption.clear();
	redraw();
}

// The VO caption: one centred line near the bottom of the screen, drawn into
// LOWERMATTE's "CCText" control. Its authored box is 360x50 at 220,540 in the
// 800x600 UI space, i.e. horizontally centred, which is exactly where the
// original game puts Nancy's narration. No frame, no scrollbar, no responses -
// those belong to the conversation panel.
void NDUIPanel::drawNarration() {
	if (!_chunk || _narrationCaption.empty()) {
		return;
	}

	NDUIControl *anchor = findControl("CCText");
	if (!anchor) {
		return;
	}

	const Common::Rect &box = _chunk->panelStatic.bounds;
	const int left = toScreen(anchor->bounds.left);
	const int wrapWidth = toScreen(anchor->bounds.width() ? anchor->bounds.width() : box.width());

	TextStyle style;
	style.font = g_nancy->_ttfFonts.get(anchor->elements.empty() ? Common::String("Convo")
		: anchor->elements[0].fontName, false);
	if (!style.font) {
		return;
	}

	style.colour = anchor->elements.empty()
		? _drawSurface.format.RGBToColor(0xff, 0xff, 0xff)
		: argbToSurface(_drawSurface, anchor->elements[0].fontColors[0]);

	// No opaque band behind the text: the panel stays transparent and the
	// compositor resets what is underneath (GraphicsManager::draw), so a new
	// caption no longer lands on top of the previous one.
	const int top = toScreen(anchor->bounds.top);

	drawStyledText(_narrationCaption, style, left, top, wrapWidth,
		Graphics::kTextAlignCenter);
}

void NDUIPanel::drawInventory() {
	_invItems.clear();
	_invRects.clear();

	// Zeroed ahead of the three early returns below, so a frame that resolves no
	// grid reports "no model" to InvScrollBar rather than last frame's numbers.
	_invPageRows = 0;
	_invRangeRows = 0;

	NDUIControl *box = _chunk ? findControl("InvDialog") : nullptr;
	if (!box) {
		return;
	}

	auto *invData = (const INVD *)g_nancy->getEngineData("INVD");
	if (!invData) {
		return;
	}

	Graphics::ManagedSurface icons;
	if (!g_nancy->_resource->loadImage("UI_INVENTORY", icons)) {
		return;
	}

	// InvDialog is this panel's root, so its bounds are absolute; the surface
	// starts at _surfaceTopLeft. Rebase before laying anything out.
	const Common::Rect grid(box->bounds.left - _surfaceTopLeft.x,
							box->bounds.top - _surfaceTopLeft.y,
							box->bounds.right - _surfaceTopLeft.x,
							box->bounds.bottom - _surfaceTopLeft.y);

	const int cell = 39;
	const int cols = MAX(1, grid.width() / cell);
	const int rows = MAX(1, grid.height() / cell);

	// SCROLLING. Without it the panel shows the first `cols * rows` items and
	// silently drops the rest: measured at S6713, the player holds 26 items and
	// nine cells are drawn, so seventeen items are unreachable and the late
	// game cannot be completed - by a bot or by a person. Two ways in, and the
	// comment that used to be here was stale about both: the authored InvScrollBar
	// IS read now - the panel data names it, and it reaches this offset through
	// its event-15 binding on InvDialog - and the keyboard route is
	// kMoveUp/kMoveDown, not the mouse wheel, which the keymapper has no action
	// for (see handleInput).
	//
	// Count what is actually displayable first, so the offset can be clamped to
	// something real rather than to the raw inventory size.
	uint shown = 0;
	for (uint i = 0; i < invData->items.size(); ++i) {
		const INVD::Item &it = invData->items[i];
		if (it.isPseudoItem() || NancySceneState.hasItem(it.id) != g_nancy->_true) {
			continue;
		}
		if (it.sourceRect.isEmpty() ||
				it.sourceRect.right > icons.w || it.sourceRect.bottom > icons.h) {
			continue;
		}
		++shown;
	}

	// Recorded for scrollModel(), not recomputed there. The bar that displays this
	// is in another panel - InvDialogContainer, INVENTORY chunk[2] - and asks
	// during its own compose, where re-deriving these would mean re-loading INVD
	// and UI_INVENTORY once a frame. Rows, which is the unit _invScroll counts in.
	_invPageRows = rows;
	_invRangeRows = (int)((shown + cols - 1) / cols);

	const int maxRow = MAX(0, _invRangeRows - _invPageRows);

	// Debug affordance: PIN the starting row, so the scroll can be tested
	// without synthesising arrow keys (the autotype harness types characters
	// only). Applied on every draw, not once: the first drawInventory() runs
	// before the panel holds anything, so maxRow is 0 there and a one-shot
	// assignment is immediately clamped back to 0 and never reapplied.
	if (ConfMan.hasKey("nancy_inv_scroll")) {
		_invScroll = ConfMan.getInt("nancy_inv_scroll");
	}

	_invScroll = CLIP<int>(_invScroll, 0, maxRow);
	const uint skip = (uint)_invScroll * cols;

	uint seen = 0;
	uint slot = 0;
	for (uint i = 0; i < invData->items.size(); ++i) {
		const INVD::Item &item = invData->items[i];
		if (item.isPseudoItem() || NancySceneState.hasItem(item.id) != g_nancy->_true) {
			continue;
		}

		if (item.sourceRect.isEmpty() ||
				item.sourceRect.right > icons.w || item.sourceRect.bottom > icons.h) {
			continue;
		}

		// Scrolled off the top
		if (seen++ < skip) {
			continue;
		}

		const Common::Point dest(grid.left + (int)(slot % cols) * cell,
								 grid.top + (int)(slot / cols) * cell);
		if (dest.y + cell > grid.bottom) {
			break;	// scrolled off the bottom; the wheel brings these up
		}

		_artSurface->blitFrom(icons, item.sourceRect, dest);
		_invItems.push_back(item.id);

		// Recorded at output scale, like the conversation rects, so hit-testing
		// only has to offset by the panel's screen position.
		_invRects.push_back(toScreen(Common::Rect(dest.x, dest.y,
			dest.x + item.sourceRect.width(), dest.y + item.sourceRect.height())));
		++slot;
	}
}

NDUIControl *NDUIPanel::findControl(const Common::String &name) {
	if (!_chunk || name.empty()) {
		return nullptr;
	}

	Common::Array<const NDUIControl *> stack;
	stack.push_back(&_chunk->panelStatic);
	for (uint i = 0; i < _chunk->children.size(); ++i) {
		stack.push_back(&_chunk->children[i]);
	}

	while (!stack.empty()) {
		const NDUIControl *c = stack.back();
		stack.pop_back();
		if (c->name.equalsIgnoreCase(name)) {
			return const_cast<NDUIControl *>(c);
		}

		for (uint i = 0; i < c->children.size(); ++i) {
			stack.push_back(&c->children[i]);
		}
	}

	return nullptr;
}

// --- Save / load dialogs ------------------------------------------------
//
// The LOADGAME and SAVEGAME chunks author a frame, a scrollbar, a Close button
// and an OK button, and then three widgets whose contents the file does not
// carry: a ListBox, an EditBox, and a Static whose element sourceName is
// "Engine_LoadSave". Those three are the engine's half of the screen, so they
// are filled in here, in the same post-downscale pass the conversation text
// uses - the rows are text, and rasterising them at authored size and then
// resampling is what made the dialogue box mushy.

NDUIControl *NDUIPanel::saveListControl() {
	if (!_chunk) {
		return nullptr;
	}

	NDUIControl *list = findControl("LoadList");
	return list ? list : findControl("SaveList");
}

bool NDUIPanel::ownsControl(const Common::String &name) {
	return findControl(name) != nullptr;
}

int NDUIPanel::saveLoadSelectedSlot() const {
	if (_saveSel < 0 || _saveSel >= (int)_saveRows.size()) {
		return -1;
	}

	return _saveRows[_saveSel].slot;
}

void NDUIPanel::saveLoadSetName(const Common::String &name) {
	if (_saveName == name) {
		return;
	}

	_saveName = name;
	redraw();
}

void NDUIPanel::saveLoadRefresh() {
	if (!saveListControl()) {
		return;
	}

	const Common::String previous = saveLoadSelectedSlot() >= 0 ? _saveRows[_saveSel].name : Common::String();
	_saveRows.clear();

	// Only the player's own slots. The band at the top of the range holds the
	// checkpoints action record 114 writes under script-authored names
	// ("Start_Game", ...) plus the second chance slot; those are engine
	// bookkeeping that the game restores by name on its own, and listing them
	// would bury the player's saves under a dozen entries they never made.
	//
	// The autosave slot is left out for a sharper reason than tidiness: this
	// same list is the overwrite picker on the save screen - clicking a row
	// copies its name into the name field, and Save then writes over whatever
	// that name resolves to. A listed autosave could therefore be destroyed by
	// two clicks, and it is the one save the player never chose to make.
	const int firstReserved = g_nancy->firstNamedSaveSlot();
	const int autosaveSlot = g_nancy->getAutosaveSlot();
	SaveStateList saves = g_nancy->getMetaEngine()->listSaves(ConfMan.getActiveDomainName().c_str());
	for (const SaveStateDescriptor &save : saves) {
		if (save.getSaveSlot() < 0 || save.getSaveSlot() >= firstReserved ||
				save.getSaveSlot() == autosaveSlot) {
			continue;
		}

		SaveRow row;
		row.slot = save.getSaveSlot();
		row.name = save.getDescription();
		_saveRows.push_back(row);
	}

	// Keep pointing at the same save across a refresh - after a save is written
	// the list grows, and an index-based selection would slide onto its
	// neighbour.
	_saveSel = -1;
	for (uint i = 0; i < _saveRows.size(); ++i) {
		if (!previous.empty() && _saveRows[i].name.equalsIgnoreCase(previous)) {
			_saveSel = (int)i;
			break;
		}
	}

	// Top of the list, then let the next draw bring the restored selection into
	// view - it is the only place that knows how many rows fit the box, and the
	// row matched by name above can be anywhere in the list.
	_saveScroll = 0;
	_saveScrollToSel = true;
	redraw();
}

void NDUIPanel::drawSaveLoad() {
	_saveRowRects.clear();
	_savePageRows = 0;

	NDUIControl *list = saveListControl();
	if (!list) {
		return;
	}

	const NDUIElement *listEl = list->elements.empty() ? nullptr : &list->elements[0];
	const Graphics::Font *font = listEl ? g_nancy->_ttfFonts.get(listEl->fontName, false) : nullptr;
	if (!font) {
		font = g_nancy->_ttfFonts.get("UIFont", false);
	}

	if (!font) {
		return;
	}

	// Children of the panel root are authored relative to it, and the root's own
	// bounds are absolute, so both need rebasing onto the surface before the
	// 800x600 -> 640x480 conversion.
	const Common::Point base(_chunk->panelStatic.bounds.left - _surfaceTopLeft.x,
							 _chunk->panelStatic.bounds.top - _surfaceTopLeft.y);

	const int minRowHeight = font->getFontHeight();

	// fontColors[] is the same four-slot group the artwork uses: NORMAL for a
	// row at rest, MOUSEOVER for the selected one. The authored pair is
	// 0xffc8c8c8 / 0xffffffff, i.e. grey rows with the picked one in white,
	// which is what the retail screens show.
	const uint32 restARGB = listEl ? listEl->fontColors[kNDUIColorNormal] : 0xffc8c8c8;
	const uint32 selARGB = listEl ? listEl->fontColors[kNDUIColorMouseOver] : 0xffffffff;

	Common::Rect box = toScreen(Common::Rect(
		list->bounds.left + base.x, list->bounds.top + base.y,
		list->bounds.right + base.x, list->bounds.bottom + base.y));

	// THE ROW HEIGHTS, measured rather than assumed. drawStyledText wraps a save
	// name at box.width() like any other styled text, so a long description takes
	// two lines - "02 - Waking up in Venice" on slot 135 is one - while the y
	// cursor stepped by exactly one font height per entry, which drew the second
	// line straight over the row below it ("03 - Rialto Market"). Measured with the
	// helper drawList already uses, at the same wrap width the draw below passes,
	// so the step, the hit rect and the paint cannot disagree about where a row
	// ends.
	//
	// Floored at the old constant pitch. That pitch is getFontHeight(), the
	// substitute face's own hhea height, which normally sits a pixel or two above
	// the authored lineHeight the layout advances by; keeping it as the minimum
	// leaves a list of one-line names laid out exactly as it is today, and leaves
	// how many of them fit unchanged. Only a row that really wraps grows.
	TextStyle measure;
	measure.font = font;

	Common::Array<int> lineHeights;
	Common::Array<int> rowHeights;
	rowHeights.reserve(_saveRows.size());
	for (uint i = 0; i < _saveRows.size(); ++i) {
		measureStyledLines(_drawSurface, _saveRows[i].name, measure, box.width(), lineHeights);

		int height = 0;
		for (uint l = 0; l < lineHeights.size(); ++l) {
			height += lineHeights[l];
		}

		rowHeights.push_back(MAX(height, minRowHeight));
	}

	// THE PAGE, and why it is no longer a division. With variable row heights "how
	// many fit" is a function of WHERE you start, so there is no single count - but
	// the scroll bar does not actually need the count on screen, it needs the one
	// page that fixes the ends of the travel: the LAST one. lastPageTop is the
	// smallest first row whose remaining rows all still fit, i.e. the largest
	// scroll position the list has, worked out by walking back from the end the
	// same way drawList's stop table walks forward through a paragraph.
	//
	// Reporting page as size - lastPageTop makes scrollBarGeom's maxPos
	// (range - page) come out exactly equal to that clamp, so the thumb reaches the
	// bottom of the track precisely when the list reaches its last row, and never
	// offers a position the next draw would snap back from. It is still read back
	// off the layout, as the old page was and for the same reason - getFontHeight()
	// does not track the requested size, so no row count worked out anywhere else
	// would be a measurement. It just cannot be read off _saveRowRects any more:
	// that array is as long as the CURRENT page, which now breathes by a row as
	// wrapped entries scroll through the box, and a page that moves with the
	// position would change both the thumb's length and the drag's pixels-to-rows
	// scale in the middle of a gesture.
	int lastPageTop = 0;
	int lastPageUsed = 0;
	for (int i = (int)_saveRows.size() - 1; i >= 0; --i) {
		if (lastPageUsed && lastPageUsed + rowHeights[i] > box.height()) {
			break;
		}

		lastPageUsed += rowHeights[i];
		lastPageTop = i;
	}

	_savePageRows = (int)_saveRows.size() - lastPageTop;

	// Bring the selection on screen once per refresh, not on every draw. The
	// authored ScrollBar is wired up now (listScrollBy), and this clamp used to
	// run unconditionally, which re-pinned _saveScroll to the row the player last
	// clicked: the bar would move the list for a single frame and then snap
	// straight back. Since a selection is set as soon as anyone clicks a row, a
	// routed bar would still have looked as dead as an unrouted one.
	//
	// drawList and drawInventory both clamp to range only and leave the position
	// to whoever owns the delta. This was the odd one out because, until the bar
	// was routed, it was the only thing that scrolled the list.
	if (_saveScrollToSel) {
		_saveScrollToSel = false;
		if (_saveSel >= 0 && _saveSel < (int)_saveRows.size()) {
			// The variable-height form of "_saveSel - visibleRows + 1": walk back
			// from the selected row until one more row would not fit. That row is
			// the smallest scroll position that still shows the selection whole, and
			// _saveSel itself is the largest.
			int firstVisible = _saveSel;
			int used = 0;
			for (int i = _saveSel; i >= 0; --i) {
				if (used && used + rowHeights[i] > box.height()) {
					break;
				}

				used += rowHeights[i];
				firstVisible = i;
			}

			_saveScroll = CLIP(_saveScroll, firstVisible, _saveSel);
		}
	}

	_saveScroll = CLIP(_saveScroll, 0, lastPageTop);

	int y = box.top;
	for (uint i = (uint)_saveScroll; i < _saveRows.size(); ++i) {
		const int rowHeight = rowHeights[i];

		// A row that does not fit whole is left for the next scroll position rather
		// than clipped across the bottom edge - except at the top of the box, where
		// refusing to draw would show nothing at all. Both are drawList's rules, and
		// the second is why _saveScroll is still clamped above: the exception must
		// stay a last resort rather than a way to park an unreadable row on screen.
		if (y + rowHeight > box.bottom && y > box.top) {
			break;
		}

		const int rowBottom = MIN<int>(box.bottom, y + rowHeight);

		uint drawLines = 0;
		if (rowHeight > box.height()) {
			// A name too tall for its own box: no scroll position can ever show it
			// whole, so it takes the lines that fit and the rest is dropped instead
			// of painted over the buttons below. Re-measured here rather than kept
			// in a second table for the whole list, because no save name the game
			// can produce comes near a boxful of lines.
			measureStyledLines(_drawSurface, _saveRows[i].name, measure, box.width(), lineHeights);
			int taken = 0;
			drawLines = linesThatFit(lineHeights, 0, box.height(), taken);
		}

		TextStyle style;
		style.font = font;
		style.colour = argbToSurface(_drawSurface, (int)i == _saveSel ? selARGB : restARGB);

		if ((int)i == _saveSel) {
			// A rule under the picked row. The original draws a filled highlight
			// bar; nothing in the descriptor names its colour, so this uses the
			// selected text colour rather than inventing one.
			Common::Rect underline(box.left, rowBottom - 1, box.right, rowBottom);
			underline.clip(Common::Rect(_drawSurface.w, _drawSurface.h));
			if (!underline.isEmpty()) {
				_drawSurface.fillRect(underline, style.colour);
			}
		}

		drawStyledText(_saveRows[i].name, style, box.left, y, box.width(),
			Graphics::kTextAlignLeft, 0, drawLines);

		// One rect per row, still pushed consecutively from _saveScroll and never
		// skipped, because the hit test recovers the row as i + _saveScroll.
		_saveRowRects.push_back(Common::Rect(box.left, y, box.right, rowBottom));
		y += rowHeight;
	}

	// The name field, on the save screen only.
	if (NDUIControl *edit = findControl("SaveName")) {
		const Common::Rect editBox = toScreen(Common::Rect(
			edit->bounds.left + base.x, edit->bounds.top + base.y,
			edit->bounds.right + base.x, edit->bounds.bottom + base.y));

		TextStyle style;
		const NDUIElement *editEl = edit->elements.empty() ? nullptr : &edit->elements[0];
		style.font = editEl ? g_nancy->_ttfFonts.get(editEl->fontName, false) : font;
		if (!style.font) {
			style.font = font;
		}

		style.colour = argbToSurface(_drawSurface,
			editEl ? editEl->fontColors[kNDUIColorMouseOver] : 0xffffffff);

		// A trailing caret, so it reads as a field being typed into rather than
		// a label. There is no focus model here - the box is always the target.
		const Common::String shown = _saveName + "_";
		const int ty = editBox.top + (editBox.height() - style.font->getFontHeight()) / 2;
		drawStyledText(shown, style, editBox.left + 2, ty, editBox.width() - 4, Graphics::kTextAlignLeft);
	}

	// The thumbnail. "Engine_LoadSave" is not a member of any tree - drawControl
	// skips it by name - so this is the surface it stands for: the screenshot
	// ScummVM already stores in every save.
	NDUIControl *shot = findControl("LoadScreenshot");
	if (!shot) {
		shot = findControl("SaveScreenshot");
	}

	if (shot && saveLoadSelectedSlot() >= 0) {
		SaveStateDescriptor desc = g_nancy->getMetaEngine()->querySaveMetaInfos(
			ConfMan.getActiveDomainName().c_str(), saveLoadSelectedSlot());

		if (const Graphics::Surface *thumb = desc.getThumbnail()) {
			const Common::Rect dst = toScreen(Common::Rect(
				shot->bounds.left + base.x, shot->bounds.top + base.y,
				shot->bounds.right + base.x, shot->bounds.bottom + base.y));

			Graphics::ManagedSurface converted;
			converted.copyFrom(*thumb);
			converted.convertToInPlace(_drawSurface.format);
			_drawSurface.blitFrom(converted, Common::Rect(converted.w, converted.h), dst);
		}
	}
}

bool NDUIPanel::runAction(const NDUIAction &action, const NDUIControl &source) {
	// The "Engine_*" targets are subsystems rather than widgets, and the same
	// command id means something different against each - Hide against a widget
	// takes it off screen, Hide against Engine_GameWindow quits the game. They
	// are dispatched before the widget vocabulary below so that reading is never
	// applied to them by accident, and they all live in Scene because the widget
	// they act on is in a different panel from the button that was clicked.
	// The number the command carries. A radio button names the value it stands for
	// in its own parameter; a slider has none, and means the position it was just
	// dragged to; a check box has none either, and means "the other one" - its
	// state is the setting, so there is nowhere else for the toggle to live.
	double value = action.param;
	if (!action.hasParam) {
		if (source.classID == kNDUIClassSlider) {
			value = sliderValue(source);
		} else if (source.classID == kNDUIClassCheckBox) {
			value = controlIsSet(source) ? 0.0 : 1.0;
		}
	}

	if (action.target.hasPrefixIgnoreCase("Engine_") &&
			NancySceneState.applyNDUIEngineCommand(action.target, action.commandID,
				action.paramString, value)) {
		// A settings command changes what the widgets show - the mark moves to
		// another radio button, the tick appears - and nothing else will notice.
		if (action.commandID == kNDUICommandSetValue || action.commandID == kNDUICommandNotify) {
			return !settingTarget(source).empty();
		}

		return false;
	}

	switch (action.commandID) {
	case kNDUICommandInvoke:
		// "invoke" is not a toggle. Against Engine_Inv it means "activate the
		// inventory item this InvButton stands for", and the widget's own name
		// is what names the item: INVD item 45 is called "ShowPDA" and item 46
		// "ShowPaperDoll", which are exactly the two class-12 InvButtons in the
		// taskbar that carry this action. (The third, the generic "InvButton"
		// template in the INVENTORY chunk, is the per-held-item button; no INVD
		// item is named "InvButton", so it falls through harmlessly until the
		// template instancer exists.)
		//
		// The proof that it is the *open* and nothing else: its partner button
		// HidePDA does not carry an invoke at all. It only sets event flag 2052
		// EV_Close_PDA, which the PDA's own scenes watch to route to their close
		// screen. Closing is data-driven; only opening needs the engine.
		if (action.target.equalsIgnoreCase("Engine_Inv") &&
				source.classID == kNDUIClassInvButton && !source.name.empty()) {
			const INVD *invd = (const INVD *)g_nancy->getEngineData("INVD");
			if (invd) {
				for (uint i = 0; i < invd->items.size(); ++i) {
					if (invd->items[i].name.equalsIgnoreCase(source.name)) {
						NancySceneState.openItemViewScene((int16)invd->items[i].id);
						break;
					}
				}
			}
		}

		return false;
	case kNDUICommandShow:
	case kNDUICommandHide: {
		// Against a widget these show and hide it. Against Engine_Flags they
		// would be set/clear, but that pairing uses commands 8 and 9 instead.
		// The target usually lives in another panel - the taskbar's ShowInv
		// points at InvDialog over in the INVENTORY chunks - so this goes
		// through the scene rather than looking only at our own controls.
		NancySceneState.applyNDUICommand(action.target, action.commandID);
		return findControl(action.target) != nullptr;
	}
	case kNDUICommandSet:
	case kNDUICommandClear:
		// Overloaded: against Engine_Flags these set and clear an event flag,
		// against a widget they enable and disable it. Only the flag half is
		// wired up, since nothing renders a disabled state yet.
		if (action.target.equalsIgnoreCase("Engine_Flags") && action.paramID != NDUIAction::kNoParamID) {
			FlagDescription desc;
			desc.label = action.paramID;
			desc.flag = (action.commandID == kNDUICommandSet) ? g_nancy->_true : g_nancy->_false;
			NancySceneState.setEventFlag(desc);
		}

		return false;
	default:
		// Everything else - invoke a subsystem, set a value, notify - needs the
		// Engine_* hooks, which are not built yet.
		return false;
	}
}

// A slider is the one widget here that needs the whole press-drag-release
// gesture rather than a click: its value follows the mouse, and it keeps
// following it after the pointer has left the track. The authored bindings say
// as much - `event 18 command 30` (value changed) is raised repeatedly while the
// thumb moves, and `event -1 command 13` (commit) once at the end.
bool NDUIPanel::handleSliderInput(NancyInput &input) {
	if (!_chunk || !_chunk->hasPanel || !_isVisible) {
		return false;
	}

	const bool down = (input.input & NancyInput::kLeftMouseButtonDown) != 0;
	const bool held = (input.input & NancyInput::kLeftMouseButtonHeld) != 0;
	const bool up = (input.input & NancyInput::kLeftMouseButtonUp) != 0;

	if (_sliderDrag < 0) {
		if (!down || input.mousePos.x < 0) {
			return false;
		}

		for (int i = (int)_chunk->children.size() - 1; i >= 0; --i) {
			const NDUIControl &c = _chunk->children[i];
			if (c.classID != kNDUIClassSlider || c.bounds.isEmpty()) {
				continue;
			}

			if ((_visible.contains(c.name) && !_visible[c.name]) ||
					(_enabled.contains(c.name) && !_enabled[c.name])) {
				continue;
			}

			if (controlScreenRect(c).contains(input.mousePos)) {
				_sliderDrag = i;
				break;
			}
		}

		if (_sliderDrag < 0) {
			return false;
		}
	} else if (!held && !up) {
		// The release was eaten elsewhere, or the button came up in a frame this
		// panel never saw. Drop the drag rather than latching onto the mouse.
		_sliderDrag = -1;
		return false;
	}

	const NDUIControl &control = _chunk->children[_sliderDrag];
	const Common::Rect screenRect = controlScreenRect(control);

	if (screenRect.width() > 0 && control.sliderMax > control.sliderMin && input.mousePos.x >= 0) {
		const int range = (int)control.sliderMax - (int)control.sliderMin;
		const int offset = CLIP<int>(input.mousePos.x - screenRect.left, 0, screenRect.width());
		const int value = (int)control.sliderMin +
			(offset * range + screenRect.width() / 2) / screenRect.width();

		if (sliderValue(control) != value) {
			_sliderValues[control.name] = value;
			redraw();

			// The live half of the pair: this is what makes the volume follow the
			// thumb instead of jumping when it is let go.
			for (uint a = 0; a < control.actions.size(); ++a) {
				if (control.actions[a].eventID == kNDUIEventValueChanged) {
					runAction(control.actions[a], control);
				}
			}
		}
	}

	if (up) {
		_sliderDrag = -1;

		// The commit half. Runs even when the value did not move, because a click
		// that lands on the thumb where it already is still ends the gesture.
		for (uint a = 0; a < control.actions.size(); ++a) {
			if (control.actions[a].eventID == kNDUIEventClick) {
				runAction(control.actions[a], control);
			}
		}
	}

	g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);
	input.eatMouseInput();
	return true;
}

// DXUT gives a scroll bar four live places: an up button of the up sprite's
// height at the top, a down button of the down sprite's height at the bottom, a
// thumb, and the rest of the track. All four act here now.
//
// Only the two buttons used to, and the measured cost of that was a bar that
// looked interactive and was not: 13 output pixels of arrow at each end of a
// LOADGAME track about 100 pixels long, no thumb drawn at all, and three clicks
// at the middle of the track producing no change in the rendered list. The click
// did not merely miss - the track returned false, and the fall-through reached no
// handler either, because Scene's findControlHit skips a control with no bindings
// and LOADGAME's bar is authored unnamed with none. So it went nowhere. That is
// the reported "the scroll bar is not working".
//
// The track pages by a screenful toward the click, as DXUT does, and the thumb
// takes the press-drag-release gesture the slider already establishes below. Both
// resolve through scrollBarGeom, so the rect the player aims at is the rect the
// click is measured against.
//
// What a bar scrolls is *not* the panel it lives in. Six of nancy18's eleven list
// bars name their target in an event-15 binding that crosses chunks -
// TasklistScrollBar sits in the container while TasklistDialog is the ScrollPanel
// inside it, and JournalDetailsScrollBar names all four VENJH pages at once - so
// this routes through the scene exactly as a Show does.
//
// The other five are each nested inside their own ListBox and carry no bindings
// at all - JournalItems', LOADGAME's, SAVEGAME's, CheatInvList's and
// StreamList's - and those are the case that scrolls the panel's own list, via
// the fall-through below. This comment used to count only JournalItems', and the
// two save bars quietly inherited a fall-through written without them in mind;
// listScrollBy is where that was finally paid for.
bool NDUIPanel::handleScrollBarInput(NancyInput &input) {
	if (!_chunk || !_chunk->hasPanel || !_isVisible) {
		_barDrag = nullptr;
		return false;
	}

	const bool held = (input.input & NancyInput::kLeftMouseButtonHeld) != 0;
	const bool released = (input.input & NancyInput::kLeftMouseButtonUp) != 0;

	// A drag in progress owns the mouse until it is let go, wherever the pointer
	// has wandered to - the slider's rule, for the slider's reason, and it has to
	// be settled before the hit test below or sweeping off the bar would drop the
	// gesture halfway.
	if (_barDrag) {
		if (!held && !released) {
			// The release was eaten elsewhere, or the button came up in a frame this
			// panel never saw. Drop the drag rather than latching onto the mouse.
			_barDrag = nullptr;
			return false;
		}

		ScrollBarGeom geom;
		if (!scrollBarGeom(*_barDrag, _barDragOrigin, geom) || geom.thumb.isEmpty()) {
			// The list emptied or shrank to fit under the pointer. There is nothing
			// left to scroll, so end the gesture instead of scrolling to a position
			// that no longer exists.
			_barDrag = nullptr;
			return false;
		}

		const int maxPos = geom.range - geom.page;
		const int travel = geom.track.height() - geom.thumb.height();
		if (travel > 0 && maxPos > 0 && input.mousePos.x >= 0) {
			// The position follows how far the pointer has MOVED since the grab,
			// not the thumb's painted top.
			//
			// Deriving it from the top instead is the obvious form and it drifts,
			// because paint and hit test are not inverses: the thumb is placed by
			// truncation (pos * travel / maxPos) and recovering pos from that top
			// by rounding does not always give pos back. Worked case on LOADGAME
			// with 53 saves and a 6-row box - travel 90, maxPos 47 - at pos 12 the
			// thumb paints at top 22 (22.97 truncated), and round(22 * 47 / 90) is
			// 11. So merely PRESSING the thumb and holding still scrolled the list
			// up one row under a motionless cursor. It first bites at 43 saves on
			// SAVEGAME and 51 on LOADGAME, so a full save folder reaches it.
			//
			// Anchoring to the grab makes a zero mouse delta a zero scroll by
			// construction, whatever the rounding does, and keeps the thumb under
			// the finger for the whole gesture.
			const int moved = input.mousePos.y - _barDragGrabY;
			const int wanted = CLIP(_barDragGrabPos +
				(moved * maxPos + (moved >= 0 ? travel / 2 : -travel / 2)) / travel,
				0, maxPos);

			// Through barScrollBy and not listScrollBy, which matters the moment a
			// routed bar has a thumb to drag: geom.position came from whichever
			// panel barScrollModel resolved, and for the routed six that is not this
			// one, so writing the delta to `this` would move nothing at all while
			// the thumb tracked the pointer. barScrollBy reaches the list by the
			// same path the arrows take, so a drag stays a new way to ASK for a
			// scroll rather than a second way to perform one.
			barScrollBy(*_barDrag, wanted - geom.position);
		}

		if (released) {
			_barDrag = nullptr;
		}

		g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);
		input.eatMouseInput();
		return true;
	}

	Common::Array<HitCandidate> candidates;
	collectControls(_chunk->children, Common::Point(), candidates);

	for (uint i = 0; i < candidates.size(); ++i) {
		const NDUIControl &c = *candidates[i].control;
		if (c.classID != kNDUIClassScrollBar || c.elements.size() < 3 || c.bounds.isEmpty()) {
			continue;
		}

		if (_visible.contains(c.name) && !_visible[c.name]) {
			continue;
		}

		const Common::Rect r = controlScreenRect(c, candidates[i].origin);
		if (!r.contains(input.mousePos)) {
			continue;
		}

		// Only now, so a bar the pointer is nowhere near costs no list lookup.
		ScrollBarGeom geom;
		if (!scrollBarGeom(c, candidates[i].origin, geom)) {
			continue;
		}

		int delta = 0;
		if (input.mousePos.y < geom.up.bottom) {
			delta = -1;
		} else if (input.mousePos.y >= geom.down.top) {
			delta = 1;
		} else if (geom.thumb.isEmpty()) {
			// Unchanged, and the case that keeps every other bar exactly as it is: a
			// bar that scrolls another panel, one with no list behind it, and one
			// whose content already fits all land here. The bare track goes back to
			// whatever is under it.
			return false;
		} else if (input.mousePos.y >= geom.thumb.top && input.mousePos.y < geom.thumb.bottom) {
			if (input.input & NancyInput::kLeftMouseButtonDown) {
				_barDrag = &c;
				_barDragOrigin = candidates[i].origin;
				_barDragGrabY = input.mousePos.y;
				_barDragGrabPos = geom.position;

				// A press and a release in one frame is a click on the thumb, which
				// asks for no movement at all; take the gesture and end it rather
				// than leaving a drag armed for the next frame to cancel.
				if (released) {
					_barDrag = nullptr;
				}

				input.eatMouseInput();
			}

			g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);
			return true;
		} else {
			// A screenful toward the click, less one for context, which is DXUT's
			// m_nPageSize - 1. The page is in the same units as the delta because
			// scrollModel reported both, so this needs no conversion for either
			// list; on the stop model a single-stop page degenerates to one stop,
			// which is right, because there one stop is one screenful.
			delta = MAX(1, geom.page - 1);
			if (input.mousePos.y < geom.thumb.top) {
				delta = -delta;
			}
		}

		g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);

		if (!(input.input & NancyInput::kLeftMouseButtonUp)) {
			return true;
		}

		input.eatMouseInput();

		// The same latch and the same walk as barScrollModel, deliberately - see
		// barIsRouted.
		barScrollBy(c, delta);

		return true;
	}

	return false;
}

void NDUIPanel::handleInput(NancyInput &input) {
	if (!_chunk || !_chunk->hasPanel || !_isVisible) {
		// A panel stops being driven the moment it is hidden, so a drag that was
		// live when the dialog closed would otherwise still be armed and resume on
		// the next frame the dialog is opened.
		_barDrag = nullptr;
		return;
	}

	// Before everything else: a drag in progress owns the mouse until it is let
	// go, wherever the pointer has wandered to.
	if (handleSliderInput(input)) {
		return;
	}

	// Then the scroll bars, which have to be caught here rather than left to the
	// Scene's control dispatch: their only bindings are on event 15, and
	// activateControl runs event -1, so a click on one used to be swallowed and
	// do nothing.
	if (handleScrollBarInput(input)) {
		return;
	}

	// Journal / task-list rows, laid out at runtime like the three below.
	for (uint i = 0; i < _listRowRects.size(); ++i) {
		Common::Rect r = _listRowRects[i];
		r.translate(_screenPosition.left, _screenPosition.top);

		if (!r.contains(input.mousePos)) {
			continue;
		}

		g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			input.eatMouseInput();
			_listPicked = (int)_listRowIndices[i];
		}

		return;
	}

	const Common::Point origin(_chunk->panelStatic.bounds.left, _chunk->panelStatic.bounds.top);

	// Inventory items, then conversation responses, sit on top of the authored
	// controls: both are laid out at runtime rather than by the file. Both are
	// recorded relative to the panel surface, which is at output scale, so they
	// only need the surface's own screen position added.
	// Scroll the item grid with the movement keys while the panel is open.
	//
	// The mouse wheel would be the natural control and is NOT used: Nancy's
	// input manager is driven by the keymapper rather than raw SDL events and
	// has no wheel action, so wiring one means a new keymapper binding as well.
	// kMoveUp/kMoveDown are already plumbed, do nothing else while a dialog is
	// up, and arrow keys to scroll a list is an ordinary affordance. If a wheel
	// action is ever added, point it here.
	if (isInventoryPanel() &&
			(input.input & (NancyInput::kMoveUp | NancyInput::kMoveDown))) {
		// Straight to the single writer, which owns the grid's clamp and its redraw
		// exactly as it owns the two lists', so the keys and the authored
		// InvScrollBar cannot apply different limits to one offset. This branch used
		// to nudge _invScroll itself and floor it at zero, which was the second
		// writer and the wrong clamp.
		//
		// No coupling call beside it, and that absence is the design: InvScrollBar
		// is in another panel, and the thumb catches up because that panel checks
		// its own model every frame (NDUIPanel::updateGraphics). A refresh call here
		// would have been a third place to remember, and forgetting it is precisely
		// the defect this branch shipped with.
		listScrollBy((input.input & NancyInput::kMoveUp) ? -1 : 1);
		return;
	}

	// NO kMoveUp/kMoveDown FALLBACK FOR THE SAVE LIST, deliberately. One was
	// written and removed, for two reasons found by reviewing it:
	//
	//   * its stated justification was false in both directions. The autotype
	//     harness cannot press an arrow - `nancy_autotype` pushes KeyStates into
	//     `_otherKbdInput` (input.cpp) and never sets the movement bits, which
	//     come only from EVENT_CUSTOM_ENGINE_ACTION_START - while the harness
	//     CAN click, so it drives this list through the bar like a player does.
	//   * it swallowed keystrokes. Returning here on a held movement bit means
	//     the SaveName handler further down never runs, and `_otherKbdInput` is
	//     cleared every frame, so a character typed while Down was held - or the
	//     Return that commits the save - was lost, not deferred.
	//
	// The authored scroll bar is the control; listScrollBy now serves it.

	for (uint i = 0; i < _invRects.size(); ++i) {
		Common::Rect r = _invRects[i];
		r.translate(_screenPosition.left, _screenPosition.top);

		if (!r.contains(input.mousePos)) {
			continue;
		}

		g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			input.eatMouseInput();
			NancySceneState.setHeldItem((int16)_invItems[i]);
		}

		return;
	}

	// Conversation responses sit on top of everything the file authored.
	for (uint i = 0; i < _convoResponseRects.size(); ++i) {
		Common::Rect r = _convoResponseRects[i];
		r.translate(_screenPosition.left, _screenPosition.top);

		if (!r.contains(input.mousePos)) {
			continue;
		}

		g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			input.eatMouseInput();
			_convoPicked = (int)i;
		}

		return;
	}

	// Save-list rows, laid out at runtime like the two above.
	for (uint i = 0; i < _saveRowRects.size(); ++i) {
		Common::Rect r = _saveRowRects[i];
		r.translate(_screenPosition.left, _screenPosition.top);

		if (!r.contains(input.mousePos)) {
			continue;
		}

		g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			input.eatMouseInput();
			_saveSel = (int)(i + (uint)_saveScroll);

			// The authored binding for a click on SaveList is a pair: Invoke to
			// Engine_Saver, and Invoke to SaveName. The second one is the list
			// copying the row it just picked into the name field, which is what
			// makes "click a save, press Save" overwrite it.
			if (findControl("SaveName") && _saveSel < (int)_saveRows.size()) {
				_saveName = _saveRows[_saveSel].name;
			}

			redraw();
		}

		return;
	}

	// Typing into the SaveName EditBox. NDUI has a focus model - the EditBox
	// carries an Invoke binding for the click that focuses it - but there is
	// exactly one edit box in the whole game and it is only on screen while its
	// own dialog is up, so the box takes every key while it is visible rather
	// than tracking focus that can only ever have one value.
	if (!input.otherKbdInput.empty() && findControl("SaveName")) {
		bool changed = false;
		bool commit = false;
		for (uint i = 0; i < input.otherKbdInput.size(); ++i) {
			const Common::KeyState &key = input.otherKbdInput[i];

			if (key.keycode == Common::KEYCODE_BACKSPACE) {
				if (!_saveName.empty()) {
					_saveName.deleteLastChar();
					changed = true;
				}
			} else if (key.keycode == Common::KEYCODE_RETURN ||
					key.keycode == Common::KEYCODE_KP_ENTER) {
				// The EditBox's own event 8 binding is Notify -> Engine_Saver,
				// the same command the Save button sends. Return commits.
				commit = true;
				changed = true;
			} else if (key.ascii >= 32 && key.ascii < 127 && _saveName.size() < 32) {
				_saveName += (char)key.ascii;
				changed = true;
			}
		}

		if (changed) {
			input.otherKbdInput.clear();
			redraw();
		}

		if (commit) {
			NancySceneState.applyNDUIEngineCommand("Engine_Saver", kNDUICommandNotify, Common::String());
		}
	}

	// Authored controls are dispatched by Scene via findControlHit/activateControl.
}

Common::Rect NDUIPanel::controlScreenRect(const NDUIControl &c, const Common::Point &parentOrigin) const {
	Common::Point origin(_chunk->panelStatic.bounds.left, _chunk->panelStatic.bounds.top);
	origin += parentOrigin;

	Common::Rect screenRect = c.bounds;

	if (_flowPos.contains(c.name) && !c.elements.empty()) {
		const Common::Rect &art = c.elements[0].sourceRect;
		screenRect = Common::Rect(art.width(), art.height());
		screenRect.translate(_flowPos[c.name].x, _flowPos[c.name].y);
	}

	screenRect.translate(origin.x, origin.y);
	return toScreen(screenRect);
}

// Every control a click can reach, in the depth-first order drawControl composes
// them, each with the origin its own bounds are measured from.
//
// Descending matters: the options screen's eight radio buttons are children of a
// RadioGroup rather than of the panel, so a hit test over the panel's direct
// children alone finds none of them - which is why VidMode, FontSize and the two
// matte swatches took no clicks while CCOption, a panel-level CheckBox, did.
void NDUIPanel::collectControls(const Common::Array<NDUIControl> &children,
		const Common::Point &origin, Common::Array<HitCandidate> &out) const {
	for (uint i = 0; i < children.size(); ++i) {
		const NDUIControl &c = children[i];

		HitCandidate candidate;
		candidate.control = &c;
		candidate.origin = origin;
		out.push_back(candidate);

		// The same origin drawControl uses, so a hit rect is the rect that was
		// drawn: an owned control measures from the panel, not from its owner.
		if (!c.children.empty()) {
			collectControls(c.children, origin, out);
		}
	}
}

bool NDUIPanel::findControlHit(const Common::Point &mouse, int &outIndex, int &outArea) const {
	if (!_chunk || !_chunk->hasPanel || !_isVisible) {
		return false;
	}

	Common::Array<HitCandidate> candidates;
	collectControls(_chunk->children, Common::Point(), candidates);

	bool found = false;
	for (int i = (int)candidates.size() - 1; i >= 0; --i) {
		const NDUIControl &c = *candidates[i].control;

		if (c.bounds.isEmpty() || c.actions.empty()) {
			continue;
		}

		// Sliders take the whole press-drag-release gesture in handleInput, which
		// runs first. Offering them here as well would fire the commit binding a
		// second time on the release.
		if (c.classID == kNDUIClassSlider) {
			continue;
		}

		if ((_visible.contains(c.name) && !_visible[c.name]) ||
				(_enabled.contains(c.name) && !_enabled[c.name])) {
			continue;
		}

		const Common::Rect r = controlScreenRect(c, candidates[i].origin);
		if (!r.contains(mouse)) {
			continue;
		}

		const int area = r.width() * r.height();
		if (!found || area < outArea) {
			found = true;
			outArea = area;
			outIndex = i;
		}
	}

	return found;
}

void NDUIPanel::activateControl(int index, NancyInput &input) {
	if (!_chunk) {
		return;
	}

	Common::Array<HitCandidate> candidates;
	collectControls(_chunk->children, Common::Point(), candidates);

	if (index < 0 || index >= (int)candidates.size()) {
		return;
	}

	const NDUIControl &c = *candidates[index].control;
	const Common::Point candidateOrigin = candidates[index].origin;
	g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);

	if (input.input & NancyInput::kLeftMouseButtonUp) {
		// Captured before eatMouseInput(), which sets mousePos.x to -1.
		const Common::Point clickedAt = input.mousePos;
		input.eatMouseInput();

		// Debug affordance: which widget a click actually reached, logged at the
		// dispatch site rather than inferred from where an icon was drawn. Three
		// of the four right-hand taskbar buttons aim at Engine_Loader /
		// Engine_Saver / Engine_GameWindow, none of which is wired up yet, so
		// they have no downstream effect to watch and this is the only way to
		// tell a correctly routed click from a mirrored one.
		if (ConfMan.getBool("nancy_debug_hotspots")) {
			Common::String acts;
			for (uint a = 0; a < c.actions.size(); ++a) {
				if (c.actions[a].eventID != kNDUIEventClick) {
					continue;
				}

				acts += Common::String::format(" cmd%u->%s",
					c.actions[a].commandID, c.actions[a].target.c_str());
			}

			const Common::Rect r = controlScreenRect(c, candidateOrigin);
			warning("NDUICLICK at %d,%d -> panel '%s' control '%s' rect %d,%d %dx%d%s",
				clickedAt.x, clickedAt.y,
				_chunk->panelStatic.name.c_str(), c.name.c_str(),
				r.left, r.top, r.width(), r.height(), acts.c_str());
		}

		bool dirty = false;
		for (uint a = 0; a < c.actions.size(); ++a) {
			if (c.actions[a].eventID == kNDUIEventClick) {
				dirty |= runAction(c.actions[a], c);
			}
		}

		if (dirty) {
			redraw();
		}
	}
}

void NDUIPanel::redraw() {
	if (_chunk) {
		init(_chunk);
	}
}

} // End of namespace Nancy
