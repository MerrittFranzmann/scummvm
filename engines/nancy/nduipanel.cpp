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

// A Var::Static displays a live value from the player table. The index comes from
// a VALUE_CHANGED/INVOKE binding targeting "Engine_Index", with the index in paramID.
// Returns false for a control with no such binding.
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

	// A slot nothing has written yet reads back kNoTableValue; don't display it.
	return outValue != kNoTableValue;
}

// Element texture modulation. Each NDUIElement carries one ARGB colour per control
// state, which DXUT multiplies into the texture when drawing. Only the alpha is
// applied, per element, since elements of one panel use different values. The alpha
// is resolved by the final blit onto the opaque screen surface.

// Whether to also honour the RGB channels of textureColors. Off for now, since it
// would darken many opaque widgets, such as every dialog's Close button.
static const bool kNDUIModulateRGB = false;

static const uint32 kNDUINoModulation = 0xffffffff;

// Multiplies `rect` of `surf` by `argb`, component-wise. `surf` is always the
// caller's own decoded copy of an atlas, so no shared pixels are touched.
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

			// Fully transparent pixels stay transparent
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

// Authored 800x600 coordinate to output coordinate
static int toScreen(int authored) {
	return (authored * kNDUIScreenNum) / kNDUIScreenDen;
}

static Common::Rect toScreen(const Common::Rect &authored) {
	return Common::Rect(toScreen(authored.left), toScreen(authored.top),
						toScreen(authored.right), toScreen(authored.bottom));
}

// Output coordinate to the smallest authored coordinate that reaches it. Rounds
// up so that toScreen(fromScreen(s)) == s, which keeps a painted scroll thumb
// aligned with its hit rect.
static int fromScreen(int screen) {
	return (screen * kNDUIScreenDen + kNDUIScreenNum - 1) / kNDUIScreenNum;
}

// DXUT's SCROLLBAR_MINTHUMBSIZE, 8 authored pixels, in output pixels. A thumb
// for a very long list would otherwise round to nothing and leave the player
// with a track that looks empty and cannot be grabbed.
static const int kMinThumbScreen = 6;

// Re-strokes one pixel wide frame lines that the downscale smears. A line is a run
// of fully opaque pixels with non-opaque neighbours on both sides that spans most
// of the surface; it is redrawn at the rounded output position in its own colour.
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

// Downscales authored art by area averaging in premultiplied alpha. A scaling
// blitFrom point-samples, which at 4/5 skips every fifth source column and makes
// one pixel wide frame lines vanish depending on their position.
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

void NDUIPanel::setVisible(bool visible) {
	_wasEverVisible = visible;
	if (_chunk && _chunk->hasPanel && !_chunk->panelStatic.name.empty()) {
		_visible[_chunk->panelStatic.name] = visible;
	}
	RenderObject::setVisible(visible);
}

void NDUIPanel::init(NDUI *chunk) {
	if (_chunk != chunk) {
		// _barDrag points into the tree about to be freed. This is the only place the
		// tree is deleted, so the bare pointer is otherwise safe.
		_barDrag = nullptr;

		delete _chunk;
		_chunk = chunk;
	}

	_blitCount = 0;
	_textCount = 0;

	// Cleared on every compose, including same-chunk redraws; drawScrollBarThumbs
	// refills it with the thumbs this compose paints.
	_thumbWatch.clear();

	if (!chunk || !chunk->hasPanel) {
		return;
	}

	// The embedded Static's bounds are the panel's own rect, and child rects are
	// relative to it. Panels do not clip their children, so the surface covers the
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
		// Artwork is drawn at its source size, which can exceed the control's
		// layout box (CoinPurse has 42-pixel art in a 40-pixel box). Include
		// those pixels in the backing surface instead of clipping its edge.
		for (uint e = 0; e < c.elements.size(); ++e) {
			const NDUIElement &element = c.elements[e];
			if (!element.sourceName.empty() && !element.sourceRect.isEmpty()) {
				Common::Rect art(abs.left, abs.top,
					abs.left + element.sourceRect.width(),
					abs.top + element.sourceRect.height());
				extent.extend(art);
			}
		}
	}

	if (extent.isEmpty()) {
		return;
	}

	// NDUI is authored at 800x600 while the engine renders 640x480. Artwork is
	// composed at the authored size and scaled down once; text is drawn afterwards
	// at output scale so glyphs are never resampled.
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
				// state is an enum (0 normal, 1 disabled, 2 hidden), not a bitfield
				_visible[c->name] = (c->state != NDUIControl::kNDUIStateHidden);
			}

			for (uint i = 0; i < c->children.size(); ++i) {
				stack.push_back(&c->children[i]);
			}
		}

		_baselineVisible = _visible;
	}

	// The panel's own backdrop, then its children in file order. The panelStatic's
	// bounds are absolute while every child's are relative to it.
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

	// Last, since the thumb size and position depend on the row counts computed by
	// the list draws above.
	drawScrollBarThumbs(chunk->children, Common::Point());

	// A re-compose must not change whether the panel is on screen. _wasEverVisible
	// starts false so the first init() of a normal panel still shows it.
	setVisible(_wasEverVisible || !_startsHidden);
	_wasEverVisible = _isVisible;

	// A re-compose builds a whole new surface, so the object has to be redrawn.
	setNeedsRedraw(true);
}

// The dialog's own background wash, under everything else it draws.
// panelHeader[9] is CDXUTDialog::SetBackgroundColors' argument and, unlike the
// rest of the header, is stored big-endian: 0x00000064 on disk is ARGB 0x64000000.
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

	// A plain fill, since the surface was just cleared; the artwork drawn afterwards
	// composites against it per pixel.
	_artSurface->fillRect(box, _artSurface->format.ARGBToColor(a,
		(fill >> 16) & 0xff, (fill >> 8) & 0xff, fill & 0xff));
}

void NDUIPanel::drawControl(const NDUIControl &control, const Common::Point &origin) {
	// Hidden controls take no space and draw nothing. Visibility is read from the
	// runtime map so Show/Hide bindings take effect.
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
			// A text-only element, with a font and caption but no artwork
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

		// The authored per-state texture colour. Only the normal state is used, since the
		// panel does not track hover, pressed or disabled states yet. Masking RGB to white
		// lets opaque tinted widgets take the plain blit.
		uint32 mod = el.textureColors[kNDUIColorNormal];
		if (!kNDUIModulateRGB) {
			mod |= 0x00ffffff;
		}

		if ((mod >> 24) == 0) {
			// Authored fully transparent at rest: the element is only a hit box, and the
			// checked state is drawn by drawSetMark() below.
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

	// Deferred: strings are drawn after the artwork has been scaled down. A
	// Var::Static's string is its bound value, so it has no captionTextID.
	if ((!control.captionTextID.empty() || control.classID == kNDUIClassVarStatic) &&
			!control.elements.empty()) {
		PendingText pending;
		pending.control = &control;
		pending.origin = origin;
		_pendingText.push_back(pending);
	}

	// An owned control's bounds are relative to the panel, not to its owner, so the
	// origin does not descend.
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
		// `atlas` is this call's own copy, so the sub-rect can be modulated in place
		modulateSurface(atlas, src, modulate);
		_artSurface->blitFrom(atlas, src, dest);
	}

	++_blitCount;
	return true;
}

// The options screen's settings widgets. The setting a widget drives is the
// "Engine_*" target of its SetValue or Notify binding, never its name.

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
	int16 boundValue = 0;
	if (nduiBoundValue(control, boundValue)) {
		return boundValue;
	}

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

	// A radio button stands for the value in its binding's parameter, and is set
	// when the setting has that value. A check box is simply on or off.
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

// As in DXUT's CDXUTSlider, the thumb is centred on the value's position along
// the whole track. The thumb keeps its authored size rather than being square.
void NDUIPanel::drawSlider(const NDUIControl &control, const Common::Point &origin) {
	const Common::Rect bounds = control.bounds;
	if (bounds.isEmpty() || control.sliderMax <= control.sliderMin) {
		return;
	}

	const Common::Point dest(bounds.left + origin.x, bounds.top + origin.y);
	const int range = (int)control.sliderMax - (int)control.sliderMin;
	const int value = CLIP<int>(sliderValue(control), (int)control.sliderMin, (int)control.sliderMax);
	const int buttonX = (value - (int)control.sliderMin) * bounds.width() / range;

	// The filled part of the track, up to the thumb. NDUI carries it as a second
	// sprite the same size as the track.
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

// As in DXUT's CDXUTScrollBar, a button sits at each end of the bar with the
// track between them. The elements are the track, up button, down button and
// thumb, drawn at their authored size; the thumb is drawn by drawScrollBarThumbs.
void NDUIPanel::drawScrollBar(const NDUIControl &control, const Common::Point &origin) {
	if (control.elements.size() < 3 || control.bounds.isEmpty()) {
		return;
	}

	const NDUIElement &track = control.elements[0];
	const NDUIElement &up = control.elements[1];
	const NDUIElement &down = control.elements[2];
	// A conversation bar whose content fits is drawn in its authored disabled state
	int position = 0, page = 0, range = 0;
	const bool convoBar = control.name.equalsIgnoreCase("ConvoScrollBar");
	const bool disabled = convoBar && (!barScrollModel(control, position, page, range) || range <= page);
	const uint state = disabled ? kNDUIColorDisabled : kNDUIColorNormal;

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
			Common::Point(dest.x, dest.y + upHeight),
			convoBar ? track.textureColors[state] : kNDUINoModulation, control.name, &crop);
	}

	if (upHeight > 0 && !up.sourceName.empty()) {
		const Common::Rect crop(up.sourceRect.width(), (int16)upHeight);
		blitAtlasRect(up.sourceName, up.sourceRect, dest,
			convoBar ? up.textureColors[state] : kNDUINoModulation, control.name, &crop);
	}

	if (downHeight > 0 && !down.sourceName.empty()) {
		const Common::Rect crop(down.sourceRect.width(), (int16)downHeight);
		blitAtlasRect(down.sourceName, down.sourceRect,
			Common::Point(dest.x, dest.y + barHeight - downHeight),
			convoBar ? down.textureColors[state] : kNDUINoModulation,
			control.name, &crop);
	}
}

// How far through its list this panel is, in that list's own units: rows for the
// save list, scroll stops for the journal and task list, grid rows for inventory.
// The page size is read back from the last draw, so a page of zero means the list
// has not been laid out yet and is reported as no model.
bool NDUIPanel::scrollModel(int &position, int &page, int &range) {
	if (isConversationPanel()) {
		position = _convoScroll;
		page = _convoPage;
		range = _convoRange;
		return true;
	}
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

	// The item grid, counted in rows rather than items
	if (isInventoryPanel()) {
		position = _invScroll;
		page = _invPageRows;
		range = _invRangeRows;
		return page > 0;
	}

	return false;
}

// A bar carrying any scroll binding is answered through its targets and never
// falls back to this panel's own list, even if no target resolves. This keeps
// JournalDetailsScrollBar off the JournalItems list that shares its panel.
bool NDUIPanel::barIsRouted(const NDUIControl &control) {
	for (uint a = 0; a < control.actions.size(); ++a) {
		if (control.actions[a].eventID == kNDUIEventScroll) {
			return true;
		}
	}

	return false;
}

// Which model a bar describes. A routed bar goes through the scene, since its
// target can live in another panel. Keep this parallel with barScrollBy().
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

// The write counterpart of barScrollModel(). Unlike the read it applies to every
// target, so JournalDetailsScrollBar scrolls all four journal pages.
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

// Where a bar's parts are, shared by the paint and the hit test. Returns false
// only for a control that is not a usable bar. `thumb` is left empty when there is
// no model, the content fits, or the track is too short for a minimum thumb; those
// checks also rule out a divide by zero below.
bool NDUIPanel::scrollBarGeom(const NDUIControl &control, const Common::Point &origin,
		ScrollBarGeom &out) {
	out = ScrollBarGeom();

	if (control.classID != kNDUIClassScrollBar || control.elements.size() < 3 ||
			control.bounds.isEmpty()) {
		return false;
	}

	out.bar = controlScreenRect(control, origin);

	// Arrow hit boxes match the painted button heights
	const int barHeight = control.bounds.height();
	const int upHeight = MIN<int>(control.elements[1].sourceRect.height(), barHeight);
	const int downHeight = MIN<int>(control.elements[2].sourceRect.height(), barHeight - upHeight);

	out.up = Common::Rect(out.bar.left, out.bar.top, out.bar.right,
		out.bar.top + toScreen(upHeight));
	out.down = Common::Rect(out.bar.left, out.bar.bottom - toScreen(downHeight),
		out.bar.right, out.bar.bottom);

	if (out.down.top <= out.up.bottom) {
		// Defensive only: a bar with no track has nothing to grab
		return true;
	}

	out.track = Common::Rect(out.bar.left, out.up.bottom, out.bar.right, out.down.top);

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
	// capped by both the track and the sprite.
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

	// Full bar width, like the arrows, so the art's width does not decide how hard
	// the thumb is to hit.
	out.thumb = Common::Rect(out.track.left, top, out.track.right, top + thumbHeight);
	return true;
}

void NDUIPanel::drawScrollBarThumbs(const Common::Array<NDUIControl> &children,
		const Common::Point &origin) {
	for (uint i = 0; i < children.size(); ++i) {
		const NDUIControl &c = children[i];

		// drawControl's visibility test. Hidden controls skip their children too, so a
		// hidden list takes its nested bar's thumb with it.
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

		// Record what each routed bar resolved, so updateGraphics() can tell when the
		// target list has changed. Unrouted bars read a list in this panel, which this
		// compose has just laid out.
		if (isBar && barIsRouted(c)) {
			ThumbWatch watch;
			watch.control = &c;
			watch.resolved = barScrollModel(c, watch.position, watch.page, watch.range);
			_thumbWatch.push_back(watch);
		}

		if (isBar && !geom.thumb.isEmpty()) {
			// Cropped from the top of the strip, and drawn to the output surface since the
			// artwork pass has already been downscaled. Positioned off geom.thumb, the hit
			// test rect, so the paint and the hit test cannot drift apart.
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

// A routed bar's thumb is painted from another panel's state, so once a frame it
// is checked against the live model and the panel is recomposed if it changed.
// This covers every writer, including recompose loops that reach the bar before
// its target. Safe mid-drag, since redraw() keeps the chunk and so _barDrag.
void NDUIPanel::updateGraphics() {
	// Hidden panels are re-checked once they become visible
	if (!_isVisible || _thumbWatch.empty()) {
		return;
	}

	for (uint i = 0; i < _thumbWatch.size(); ++i) {
		const ThumbWatch &watch = _thumbWatch[i];

		int position = 0;
		int page = 0;
		int range = 0;
		const bool resolved = barScrollModel(*watch.control, position, page, range);

		// `resolved` is compared first, since an unresolved model leaves the ints unset
		if (resolved != watch.resolved || (resolved && (position != watch.position ||
				page != watch.page || range != watch.range))) {
			// Return, since redraw() clears and refills _thumbWatch
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

	// DXUT puts the box at the left of the bounding rect, vertically centred. The
	// sprite is drawn at its authored size.
	Common::Point dest(control.bounds.left + origin.x, control.bounds.top + origin.y);
	dest.y += (control.bounds.height() - control.checkedImageRect.height()) / 2;

	blitAtlasRect(control.checkedImageName, control.checkedImageRect, dest,
		kNDUINoModulation, control.name);
}

// Inline text markup used by the CONVO, UITEXT and journal string tables:
//
//   <cN>  foreground colour, COLR chunk with id N
//   <bN>  typeface at bold weight, FONT chunk with id N
//   <fN>  typeface, FONT chunk with id N
//   <n>   hard line break
//   <u>   emphasis toggle, drawn as an underline
//
// Other codes, and <cN> with no matching COLR chunk, are stripped.

struct TextStyle {
	const Graphics::Font *font = nullptr;
	uint32 colour = 0;
	bool underline = false;
	uint colourState = kNDUIColorNormal;
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

// Reads "<x12>" at `pos`: one letter, optional decimal digits, then '>'. Anything
// else stays literal text. Declared in util.h, since AR 209 shares it.
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

// Splits `in` into styled runs. Unknown or unresolvable codes are dropped without
// changing the style.
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
					// Responses use COLR's hover slot; captions retain NORMAL.
					style.colour = argbToSurface(surf, entry->argb[base.colourState]);
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
				// The token keeps its trailing spaces even when it starts a wrapped line,
				// or it would merge with the next word.
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
			// Font::drawString takes the top of the cell, so this sits a shorter piece on
			// the line's bottom edge. Clamped for a substitute font taller than the line.
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

		// No leading: the original advanced by the cell height alone
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

	// CCText's authored caption is a placeholder; drawNarration() owns this control
	if (control.name == "CCText") {
		return;
	}

	Common::String caption;
	if (control.classID == kNDUIClassVarStatic) {
		// The string is the bound value, as plain decimal
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

	// Vertically centre within the control. Bounds are authored, so they convert to
	// output space on the way in.
	Common::Rect authored(
		control.bounds.left + origin.x, control.bounds.top + origin.y,
		control.bounds.right + origin.x, control.bounds.bottom + origin.y);

	// A check box or radio button shares its rect with its mark. As in DXUT's
	// CDXUTCheckBox::UpdateRect, the caption starts at 1.25 times the control's height.
	if (control.classID == kNDUIClassCheckBox || control.classID == kNDUIClassRadioButton) {
		if (!control.checkedImageName.empty()) {
			authored.left += (authored.height() * 5) / 4;
		}
	}

	const Common::Rect box = toScreen(authored);

	const int y = box.top + (box.height() - style.font->getFontHeight()) / 2;

	if (el.wordWrap == 0) {
		// With word wrap off the caption is one line, overflowing the box if it has
		// to; the box only says where the line is anchored.
		const int width = style.font->getStringWidth(caption);
		int x = box.left;
		if (align == Graphics::kTextAlignRight) {
			x = box.right - width;
		} else if (align == Graphics::kTextAlignCenter) {
			x = box.left + (box.width() - width) / 2;
		}

		// Overflowing the box is authored, but the caption must stay inside the panel
		// surface or it loses glyphs at the edge.
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
	// out, as described by the panel's 42-byte header:
	//   panelHeader[1] layoutMode  0 = no layout pass, 1 = forward, 2 = reverse
	//   panelHeader[3] gapX  [4] gapY
	//   panelHeader[5..8]    padding (left, top, right, bottom)
	//
	// A reverse layout places children from the panel's right padding edge.
	if (!chunk->hasPanel) {
		return;
	}

	const uint32 layoutMode = chunk->panelHeader[1];
	if (layoutMode != kNDUILayoutForward && layoutMode != kNDUILayoutReverse) {
		// Mode 0 panels position every child from its own authored bounds
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
		// Part of how the panel was built, so it belongs in the baseline too
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

	// Hidden widgets are skipped; init() reads the live value when they are shown.
	bool changed = false;
	for (uint i = 0; i < _chunk->children.size(); ++i) {
		const NDUIControl &c = _chunk->children[i];
		if ((c.classID != kNDUIClassVarStatic && c.classID != kNDUIClassSlider) || c.name.empty()) {
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

			// A settings screen re-reads its values each time it opens, since ScummVM's
			// own options dialog writes the same keys.
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
		// Treat a name missing from the baseline as authored visible
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

	// Reset first, since an in-session load runs against panels that already carry
	// the previous session's deltas.
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

	// Re-compose first, since init() resets the panel's visibility, then make the
	// root's on-screen state match _visible.
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
	_convoHovered = -1;
	_convoScroll = 0;
	_convoPage = _convoRange = 1;
	_wasEverVisible = true;
	redraw();
	NancySceneState.setConversationUIVisible(true);
}

void NDUIPanel::convoClose() {
	_convoCaption.clear();
	_convoResponses.clear();
	_convoResponseRects.clear();
	_convoPicked = -1;
	_convoHovered = -1;
	_convoScroll = 0;
	_convoPage = _convoRange = 1;
	_wasEverVisible = false;
	NancySceneState.setConversationUIVisible(false);
}

void NDUIPanel::convoSetCaption(const Common::String &text) {
	_convoCaption = text;
	_convoScroll = 0;
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

// The gap the original leaves between the caption and the first response, and
// between responses: half the font's ascent.
static int convoResponseGap(const Graphics::Font *font) {
	return font->getFontAscent() / 2;
}

// Lays the NPC line and the player's responses into the panel. ConvoCCText and
// ConvoResponses are authored zero-height, so heights come from the wrapped text.
// The widget colours are only a fallback for strings with no inline <cN> code.
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
	if (!responseTemplate && _rowTemplate && _rowTemplate->name.equalsIgnoreCase("ConvoResponse")) {
		responseTemplate = _rowTemplate;
	}
	if (responseTemplate && !responseTemplate->elements.empty()) {
		const NDUIElement &el = responseTemplate->elements[0];
		if (const Graphics::Font *f = g_nancy->_ttfFonts.get(el.fontName, false)) {
			responseStyle.font = f;
		}

		responseStyle.colour = argbToSurface(_drawSurface, el.fontColors[0]);
	}

	// This ScrollPanel is the text viewport, separate from the larger framed
	// container. Lay out the entire caption/choice group before clipping it.
	const Common::Rect viewport(left, toScreen(anchor->bounds.top),
		MIN<int>(_drawSurface.w, left + wrapWidth), _drawSurface.h);
	if (viewport.isEmpty()) {
		return;
	}

	auto textHeight = [&](const Common::String &text, const TextStyle &style) {
		Common::Array<StyledRun> runs;
		parseStyledText(text, style, _drawSurface, runs);
		Common::Array<LayoutLine> lines;
		layOutStyledText(runs, wrapWidth, lines);
		int height = 0;
		for (const auto &line : lines) {
			height += line.height ? line.height : (int)g_nancy->_ttfFonts.lineHeight(style.font);
		}
		return height;
	};

	const int captionHeight = _convoCaption.empty() ? 0 : textHeight(_convoCaption, captionStyle);
	const int responseStart = captionHeight +
		(!_convoCaption.empty() && !_convoResponses.empty() ? convoResponseGap(captionStyle.font) : 0);
	const int responseGap = convoResponseGap(responseStyle.font);
	Common::Array<int> responseHeights;
	int contentHeight = responseStart;
	for (uint r = 0; r < _convoResponses.size(); ++r) {
		const int height = textHeight(_convoResponses[r], responseStyle);
		responseHeights.push_back(height);
		contentHeight += height + (r ? responseGap : 0);
	}

	// ScrollPanel's step is authored pixels (6 in CONVO). Use steps throughout
	// the shared scrollbar model, and convert their offsets only when drawing.
	const int step = MAX<int>(1, _chunk->scrollStep);
	const int excess = MAX(0, contentHeight - viewport.height());
	const int lastStep = (fromScreen(excess) + step - 1) / step;
	_convoPage = MAX(1, fromScreen(viewport.height()) / step);
	_convoRange = _convoPage + lastStep;

	_convoScroll = CLIP(_convoScroll, 0, lastStep);
	const int offset = toScreen(_convoScroll * step);

	Graphics::ManagedSurface textSurface(_drawSurface.w, _drawSurface.h, _drawSurface.format);
	textSurface.clear(0);
	int y = viewport.top - offset;
	uint pieces = 0;
	if (!_convoCaption.empty()) {
		drawStyledTextImpl(textSurface, _convoCaption, captionStyle, left, y,
			wrapWidth, Graphics::kTextAlignLeft, &pieces);
	}
	y += responseStart;
	for (uint r = 0; r < _convoResponses.size(); ++r) {
		if (r) {
			y += responseGap;
		}
		responseStyle.colourState = (int)r == _convoHovered ? kNDUIColorMouseOver : kNDUIColorNormal;
		if (responseTemplate && !responseTemplate->elements.empty()) {
			responseStyle.colour = argbToSurface(textSurface,
				responseTemplate->elements[0].fontColors[responseStyle.colourState]);
		}
		drawStyledTextImpl(textSurface, _convoResponses[r], responseStyle, left, y,
			wrapWidth, Graphics::kTextAlignLeft, &pieces);
		Common::Rect hit(left, y, left + wrapWidth, y + responseHeights[r]);
		hit.clip(viewport);
		// Keep one entry per response, including invisible entries, so picking a
		// scrolled response still reports its original conversation index.
		_convoResponseRects.push_back(hit);
		y += responseHeights[r];
	}
	_drawSurface.blitFrom(textSurface, viewport, Common::Point(viewport.left, viewport.top));
	if (pieces) {
		++_textCount;
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

// The rows are laid out at runtime, since STAKEOUT ships one row template rather
// than authored rows. StakeOutDialog is its chunk's panel root, so its bounds are
// absolute and are rebased against _surfaceTopLeft.
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

	// The row template lives in a chunk with no panel, so findControl cannot see it.
	// Fall back to the dialog's own element, which names the same Convo font.
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

// Journal and task-list rows. Like drawStakeout, plus a row template, a scroll
// offset and per-row hit rects.

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

// The single writer of the list positions; the draws only re-clamp them. Bar
// input, routed bar deltas and the movement keys all scroll through here.
void NDUIPanel::listScrollBy(int delta) {
	if (!delta) {
		return;
	}
	if (isConversationPanel()) {
		const int wanted = CLIP(_convoScroll + delta, 0, MAX(0, _convoRange - _convoPage));
		if (wanted != _convoScroll) {
			_convoScroll = wanted;
			_convoHovered = -1;
			redraw();
		}
		return;
	}

	// No panel owns more than one runtime list, so dispatch here rather than in the
	// callers. The save and load bars carry no bindings and arrive unrouted.
	if (saveListControl()) {
		// Loosely clamped; drawSaveLoad re-clamps against the rows that actually fit.
		const int wanted = CLIP(_saveScroll + delta, 0, MAX(0, (int)_saveRows.size() - 1));
		if (wanted != _saveScroll) {
			_saveScroll = wanted;
			redraw();
		}

		return;
	}

	if (_listAnchor.empty()) {
		// The item grid, reached only by a panel with neither authored list.
		// drawInventory re-clamps as well.
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

// One sub-rect of an atlas, downscaled into the output surface with the same
// resampler as the artwork pass. Used for sprites placed after text layout.
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

// Wrap-only pass, returning per-line heights so drawList can scroll inside a
// paragraph taller than its box. `contentLines` is one past the last non-empty
// line; trailing <n> lines count towards the height but not as scroll stops.
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

	// The anchor is either the panel root, with absolute bounds, or a child relative
	// to the panel box. Convert to authored absolute space, then rebase onto the surface.
	Common::Rect authored = anchor->bounds;
	if (anchor != &_chunk->panelStatic) {
		authored.translate(_chunk->panelStatic.bounds.left, _chunk->panelStatic.bounds.top);
	}

	authored.translate(-_surfaceTopLeft.x, -_surfaceTopLeft.y);
	const Common::Rect box = toScreen(authored);
	if (box.isEmpty()) {
		return;
	}

	// The row template's element when the member ships one, otherwise the anchor's
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

	// A CheckBox template gives every row a tick box at its left, as in DXUT
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

	// The scroll stops. A row that fits the box is one stop; a taller row gets one
	// stop per boxful of its lines, so its tail can be scrolled to.
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
			// A row that does not fit is left for the next scroll step rather than clipped,
			// unless it is the only one on screen.
			if (y + rowHeight > box.bottom && y > box.top) {
				break;
			}
		} else {
			// Taller than the whole box: it starts at the top and takes the lines that fit
			if (y > box.top) {
				break;
			}

			drawLines = linesThatFit(lineHeights, skip, box.height(), rowHeight);
		}

		style.colour = argbToSurface(_drawSurface,
			(int)i == _listSel ? selARGB : restARGB);

		if ((int)i == _listSel) {
			// Underline the selected row, as drawSaveLoad does, since the template's hover
			// colour equals its normal colour.
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

// The VO caption: one centred line in LOWERMATTE's CCText control, near the
// bottom of the screen.
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

	// No backing band: the compositor restores what is underneath the panel.
	const int top = toScreen(anchor->bounds.top);

	drawStyledText(_narrationCaption, style, left, top, wrapWidth,
		Graphics::kTextAlignCenter);
}

// Lays the held items into InvDialog as a grid of 38x38 cells on a 39px stride.
void NDUIPanel::drawInventory() {
	_invItems.clear();
	_invRects.clear();
	_invViewRects.clear();

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

	// The grid scrolls by rows, through InvScrollBar or kMoveUp/kMoveDown. Count the
	// displayable items first so the offset is clamped to something real.
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

	// Recorded for scrollModel(), since the bar is in another panel and recomputing
	// would reload INVD and UI_INVENTORY. Counted in rows, like _invScroll.
	_invPageRows = rows;
	_invRangeRows = (int)((shown + cols - 1) / cols);

	const int maxRow = MAX(0, _invRangeRows - _invPageRows);

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
			break;	// scrolled off the bottom
		}

		_artSurface->blitFrom(icons, item.sourceRect, dest);
		_invItems.push_back(item.id);

		// Recorded at output scale, like the conversation rects, so hit-testing
		// only has to offset by the panel's screen position.
		_invRects.push_back(toScreen(Common::Rect(dest.x, dest.y,
			dest.x + item.sourceRect.width(), dest.y + item.sourceRect.height())));
		// The eye is baked into the lower-right corner of viewable item icons.
		// Its click opens the INVD view scene; the rest of the icon picks up.
		_invViewRects.push_back(item.hasViewScene() ? toScreen(Common::Rect(
			dest.x + item.sourceRect.width() - 16, dest.y + item.sourceRect.height() - 16,
			dest.x + item.sourceRect.width(), dest.y + item.sourceRect.height())) : Common::Rect());
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

// Save/load dialogs. LOADGAME and SAVEGAME author the frame and buttons; the
// ListBox, EditBox and "Engine_LoadSave" thumbnail Static are filled in by the
// engine, in the post-downscale text pass.

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

	// Only the player's own slots: the reserved slots hold named checkpoints and the
	// second chance save. The autosave is left out, since clicking a row selects it
	// to be overwritten.
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

	// Keep pointing at the same save, by name, across a refresh
	_saveSel = -1;
	for (uint i = 0; i < _saveRows.size(); ++i) {
		if (!previous.empty() && _saveRows[i].name.equalsIgnoreCase(previous)) {
			_saveSel = (int)i;
			break;
		}
	}

	// Let the next draw bring the restored selection into view, since only it knows
	// how many rows fit.
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

	// fontColors[] holds the same four state slots as the artwork: NORMAL for a row
	// at rest, MOUSEOVER for the selected one.
	const uint32 restARGB = listEl ? listEl->fontColors[kNDUIColorNormal] : 0xffc8c8c8;
	const uint32 selARGB = listEl ? listEl->fontColors[kNDUIColorMouseOver] : 0xffffffff;

	Common::Rect box = toScreen(Common::Rect(
		list->bounds.left + base.x, list->bounds.top + base.y,
		list->bounds.right + base.x, list->bounds.bottom + base.y));

	// Row heights are measured, since a long save name wraps onto a second line.
	// They are floored at the font height so one-line names keep their pitch.
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

	// With variable row heights, the page reported to the scroll bar is the last
	// page, so maxPos (range - page) equals the largest scroll position, lastPageTop.
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

	// Bring the selection on screen once per refresh rather than on every draw, or
	// the scroll bar could never move the list away from it.
	if (_saveScrollToSel) {
		_saveScrollToSel = false;
		if (_saveSel >= 0 && _saveSel < (int)_saveRows.size()) {
			// Walk back from the selection to the smallest scroll position that still
			// shows it whole.
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

		// A row that does not fit whole is left for the next scroll position, unless
		// it is at the top of the box, as in drawList.
		if (y + rowHeight > box.bottom && y > box.top) {
			break;
		}

		const int rowBottom = MIN<int>(box.bottom, y + rowHeight);

		uint drawLines = 0;
		if (rowHeight > box.height()) {
			// A name too tall for the box takes only the lines that fit
			measureStyledLines(_drawSurface, _saveRows[i].name, measure, box.width(), lineHeights);
			int taken = 0;
			drawLines = linesThatFit(lineHeights, 0, box.height(), taken);
		}

		TextStyle style;
		style.font = font;
		style.colour = argbToSurface(_drawSurface, (int)i == _saveSel ? selARGB : restARGB);

		if ((int)i == _saveSel) {
			// A rule under the selected row. The original draws a filled bar, but nothing
			// in the descriptor names its colour.
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
	// The number the command carries: a radio button's own parameter, a slider's
	// current position, or the toggled state of a check box.
	double value = action.param;
	if (!action.hasParam) {
		if (source.classID == kNDUIClassSlider) {
			value = sliderValue(source);
		} else if (source.classID == kNDUIClassCheckBox) {
			value = controlIsSet(source) ? 0.0 : 1.0;
		}
	}

	// "Engine_*" targets are subsystems rather than widgets, and the same command
	// means something different against each, so they are dispatched first.
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
		// Against Engine_Inv, "invoke" activates the inventory item this InvButton
		// stands for, which is named after the widget (e.g. ShowPDA).
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
		// Against a widget these show and hide it. The target usually lives in another
		// panel, so this goes through the scene.
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

// A slider follows the mouse for the whole press-drag-release gesture, raising
// its value-changed binding while it moves and its commit binding on release.
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

			// Engine_Index sliders display puzzle state; they are meters,
			// rather than adjustable settings.
			uint16 boundIndex = 0;
			if (nduiBoundTableIndex(c, boundIndex)) {
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

// As in DXUT, a scroll bar has an up button, a down button, a thumb and the rest
// of the track, which pages toward the click. Bars with a scroll binding scroll a
// list in another panel through the scene; the others scroll this panel's list.
bool NDUIPanel::handleScrollBarInput(NancyInput &input) {
	if (!_chunk || !_chunk->hasPanel || !_isVisible) {
		_barDrag = nullptr;
		return false;
	}

	const bool held = (input.input & NancyInput::kLeftMouseButtonHeld) != 0;
	const bool released = (input.input & NancyInput::kLeftMouseButtonUp) != 0;

	// A drag in progress owns the mouse until it is let go, wherever the pointer
	// has wandered to.
	if (_barDrag) {
		if (!held && !released) {
			// The release was eaten elsewhere, or the button came up in a frame this
			// panel never saw. Drop the drag rather than latching onto the mouse.
			_barDrag = nullptr;
			return false;
		}

		ScrollBarGeom geom;
		if (!scrollBarGeom(*_barDrag, _barDragOrigin, geom) || geom.thumb.isEmpty()) {
			// The list emptied or now fits, so there is nothing left to scroll
			_barDrag = nullptr;
			return false;
		}

		const int maxPos = geom.range - geom.page;
		const int travel = geom.track.height() - geom.thumb.height();
		if (travel > 0 && maxPos > 0 && input.mousePos.x >= 0) {
			// The position follows how far the pointer has moved since the grab, not the
			// thumb's painted top, which is truncated and would drift on a motionless press.
			const int moved = input.mousePos.y - _barDragGrabY;
			const int wanted = CLIP(_barDragGrabPos +
				(moved * maxPos + (moved >= 0 ? travel / 2 : -travel / 2)) / travel,
				0, maxPos);

			// Through barScrollBy(), since a routed bar's list lives in another panel
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
			// No thumb: the bar has no list behind it or its content fits. The bare
			// track goes back to whatever is under it.
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
			// A screenful toward the click, less one for context, as in DXUT
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

		// Same routing as barScrollModel(); see barIsRouted()
		barScrollBy(c, delta);

		return true;
	}

	return false;
}

void NDUIPanel::handleInput(NancyInput &input) {
	if (!_chunk || !_chunk->hasPanel || !_isVisible) {
		// Drop any drag when the panel is hidden, so it does not resume on reopening
		_barDrag = nullptr;
		return;
	}

	if (isConversationPanel()) {
		int hovered = -1;
		for (uint i = 0; i < _convoResponseRects.size(); ++i) {
			Common::Rect r = _convoResponseRects[i];
			r.translate(_screenPosition.left, _screenPosition.top);
			if (r.contains(input.mousePos)) {
				hovered = (int)i;
				break;
			}
		}
		if (hovered != _convoHovered) {
			_convoHovered = hovered;
			redraw();
		}
	}

	// Before everything else: a drag in progress owns the mouse until it is let
	// go, wherever the pointer has wandered to.
	if (handleSliderInput(input)) {
		return;
	}

	// Then the scroll bars, whose bindings are on event 15, which the Scene's
	// control dispatch does not run.
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
	// controls. Both are recorded relative to the panel surface, at output scale.
	// The movement keys scroll the item grid, since there is no mouse wheel action.
	if (isInventoryPanel() &&
			(input.input & (NancyInput::kMoveUp | NancyInput::kMoveDown))) {
		// Through the single writer; InvScrollBar's thumb catches up in updateGraphics()
		listScrollBy((input.input & NancyInput::kMoveUp) ? -1 : 1);
		return;
	}

	// No movement key fallback for the save list, since it would swallow keys typed
	// into the SaveName field.

	for (uint i = 0; i < _invRects.size(); ++i) {
		Common::Rect r = _invRects[i];
		r.translate(_screenPosition.left, _screenPosition.top);

		if (!r.contains(input.mousePos)) {
			continue;
		}

		Common::Rect viewRect = _invViewRects[i];
		if (!viewRect.isEmpty()) {
			viewRect.translate(_screenPosition.left, _screenPosition.top);
		}
		const bool view = viewRect.contains(input.mousePos);
		g_nancy->_cursor->setCursorType(view ? CursorManager::kHotspot : CursorManager::kHotspotArrow);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			input.eatMouseInput();
			const int16 itemID = (int16)_invItems[i];
			if (view) {
				if (NancySceneState.openItemViewScene(itemID)) {
					NancySceneState.applyNDUICommand("InvDialog", kNDUICommandHide);
				}
			} else {
				// Selecting the carried item again puts it down. Nancy 16+'s
				// NDUI inventory keeps the item in its grid while selected, so
				// the older popup's drop path is not used here.
				NancySceneState.setHeldItem(
					NancySceneState.getHeldItem() == itemID ? -1 : itemID);
			}
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

			// A click on SaveList also copies the row's name into the name field, so
			// "click a save, press Save" overwrites it.
			if (findControl("SaveName") && _saveSel < (int)_saveRows.size()) {
				_saveName = _saveRows[_saveSel].name;
			}

			redraw();
		}

		return;
	}

	// Typing into the SaveName EditBox. There is only one edit box in the game, so
	// it takes every key while visible rather than tracking focus.
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

// Every control a click can reach, in drawControl's depth-first order, each with
// the origin its bounds are measured from. Descending reaches controls nested in
// a RadioGroup.
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
	g_nancy->_cursor->setCursorType(CursorManager::kHotspotArrow);

	if (input.input & NancyInput::kLeftMouseButtonUp) {
		input.eatMouseInput();

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
