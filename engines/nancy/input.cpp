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

#include "common/translation.h"
#include "common/system.h"

#include "backends/keymapper/action.h"
#include "backends/keymapper/keymap.h"
#include "backends/keymapper/keymapper.h"
#include "backends/keymapper/standard-actions.h"

#include "engines/nancy/nancy.h"
#include "common/config-manager.h"
#include "common/tokenizer.h"

#include "engines/nancy/action/puzzle/vaultgaugeautoplay.h"
#include "engines/nancy/action/puzzle/watermazeautoplay.h"
#include "engines/nancy/input.h"
#include "engines/nancy/util.h"
#include "engines/nancy/trace.h"

#include "engines/nancy/action/puzzle/microdotautoplay.h"

namespace Nancy {

const char *InputManager::_mazeKeymapID = "nancy-maze";

void InputManager::processEvents() {
	using namespace Common;
	Common::Event event;

	_inputs &= ~(NancyInput::kLeftMouseButtonDown | NancyInput::kLeftMouseButtonUp | NancyInput::kRightMouseButtonDown | NancyInput::kRightMouseButtonUp | NancyInput::kRaycastMap);
	_otherKbdInput.clear();

	// Debug affordance: a click list keyed on scene, "scene:x,y;scene:x,y;...".
	// nancy_autoclick_script fires on a fixed poll cadence, so one scene taking
	// a beat longer than expected shifts every later click onto the wrong screen
	// and the run silently diverges. This form only fires a step once the game is
	// actually in the scene that step names, so a whole playthrough stays in sync
	// however long each scene takes. Scene 0 in a step means "any scene".
	// nancy_scene_script_settle is the number of polls to wait after arriving
	// (or after the previous click) before firing, so records that only become
	// live a few frames into a scene are not clicked through.
	if (ConfMan.hasKey("nancy_scene_script") && g_nancy->getState() == NancyState::kScene) {
		static uint sstep = 0;
		static int settle = 0;
		static int lastScene = -1;
		const int settleFor = ConfMan.hasKey("nancy_scene_script_settle") ?
			ConfMan.getInt("nancy_scene_script_settle") : 45;

		const int nowScene = debugGetCurrentSceneID();
		if (nowScene != lastScene) {
			lastScene = nowScene;
			settle = 0;
		} else {
			++settle;
		}

		Common::StringTokenizer steps(ConfMan.get("nancy_scene_script"), ";");
		uint idx = 0;
		Common::String step;
		while (!steps.empty()) {
			const Common::String next = steps.nextToken();
			if (idx++ == sstep) {
				step = next;
				break;
			}
		}

		const uint colon = step.findFirstOf(':');
		const uint comma = step.findFirstOf(',');
		if (colon != Common::String::npos && comma != Common::String::npos && settle >= settleFor) {
			// "scene@frame:x,y" pans the panorama to `frame` before clicking. A
			// hotspot on a panoramic scene is only live while the viewport is on
			// its own frame, and panning is a hold-at-the-edge gesture no
			// synthetic click can aim.
			Common::String sceneField = step.substr(0, colon);
			int wantFrame = -1;
			const uint at = sceneField.findFirstOf('@');
			if (at != Common::String::npos) {
				wantFrame = atoi(sceneField.substr(at + 1).c_str());
				sceneField = sceneField.substr(0, at);
			}

			const int wantScene = atoi(sceneField.c_str());
			if (wantScene == 0 || wantScene == nowScene) {
				// Pan first and click on a later poll. A hotspot's _hasHotspot is
				// recomputed from the viewport frame during record processing, so
				// a click issued in the same poll as the pan is still tested
				// against the old frame's hotspots and silently does nothing.
				if (wantFrame >= 0 && debugGetViewportFrame() != wantFrame) {
					debugSetViewportFrame((uint)wantFrame);
					warning("SCENEPAN step %u scene %d to frame %d", sstep, nowScene, wantFrame);
						if (Trace::isOn()) {
							TraceEvent("pan")
								.num("step", sstep)
								.num("scene", nowScene)
								.num("frame", wantFrame)
								.emit();
						}
					settle = 0;
					return;
				}

				const int cx = atoi(step.substr(colon + 1, comma - colon - 1).c_str());
				const int cy = atoi(step.substr(comma + 1).c_str());
				setDebugMousePos(Common::Point(cx, cy));
				_inputs |= NancyInput::kLeftMouseButtonUp;
				_inputBeginState = g_nancy->getState();
				warning("SCENECLICK step %u scene %d frame %d at %d,%d", sstep, nowScene, wantFrame, cx, cy);
				if (Trace::isOn()) {
					TraceEvent("click")
						.num("step", sstep)
						.num("scene", nowScene)
						.num("frame", wantFrame)
						.num("x", cx)
						.num("y", cy)
						.emit();
				}
				++sstep;
				settle = 0;
			}
		}
	}

	// Debug affordance: synthesise one left-click at a given screen position after
	// a set number of polls. Headless test runs get no real input, and a lot of
	// the game gates on it - the splash, for one, waits on a click that sets an
	// event flag before it will advance.
	if (ConfMan.hasKey("nancy_autoclick_after")) {
		static int polls = 0;
		const int first = ConfMan.getInt("nancy_autoclick_after");
		const int every = ConfMan.hasKey("nancy_autoclick_every") ?
			ConfMan.getInt("nancy_autoclick_every") : 0;

		++polls;
		if (polls >= first && (polls == first || (every > 0 && (polls - first) % every == 0))) {
			int cx = ConfMan.hasKey("nancy_autoclick_x") ? ConfMan.getInt("nancy_autoclick_x") : 320;
			int cy = ConfMan.hasKey("nancy_autoclick_y") ? ConfMan.getInt("nancy_autoclick_y") : 200;

			// A scripted click list, "x,y;x,y;..." - one point per firing. Lets a
			// test drive an exact sequence (take the dictionary, then click the
			// ticket) rather than hoping a cycling explorer stumbles onto it.
			if (ConfMan.hasKey("nancy_autoclick_script")) {
				static uint step = 0;
				const Common::String script = ConfMan.get("nancy_autoclick_script");
				Common::StringTokenizer pts(script, ";");
				uint idx = 0;
				bool found = false;
				while (!pts.empty()) {
					const Common::String pt = pts.nextToken();
					if (idx++ != step) {
						continue;
					}

					const uint comma = pt.findFirstOf(',');
					if (comma != Common::String::npos) {
						cx = atoi(pt.substr(0, comma).c_str());
						cy = atoi(pt.substr(comma + 1).c_str());
						found = true;
					}

					break;
				}

				if (found) {
					++step;
				}
			}

			// Aim at a real hotspot rather than a fixed point when asked to. A
			// fixed click only clears scenes whose hotspot covers the viewport;
			// cycling through the live ones walks the game much further.
			if (ConfMan.getBool("nancy_autoclick_hotspots") && g_nancy->getState() == NancyState::kScene) {
				static uint which = 0;
				const uint n = which++;

				// Panoramic scenes scope each hotspot to one frame, so an explorer
				// that never pans only ever sees the slice of the room it happened
				// to arrive facing - in Nancy's bedroom that is 3 of the 9 exits.
				// Every nth click pans one frame on instead of clicking.
				const int panEvery = ConfMan.hasKey("nancy_autoclick_pan_every") ?
					ConfMan.getInt("nancy_autoclick_pan_every") : 0;
				if (panEvery > 0 && (n % (uint)panEvery) == (uint)(panEvery - 1)) {
					const int f = debugGetViewportFrame();
					if (f >= 0) {
						debugSetViewportFrame((uint)(f + 1));
						warning("AUTOPAN to frame %d", f + 1);
						return;
					}
				}

				// With nancy_autoclick_mix_fixed, every third click goes to the
				// fixed point instead, so a run can exercise the taskbar - which
				// sits outside the viewport and is never a scene hotspot. Off by
				// default, since it costs the explorer a third of its coverage.
				const bool mix = ConfMan.getBool("nancy_autoclick_mix_fixed");

				Common::Point target;
				if ((!mix || n % 3 != 2) && debugGetNthHotspotCentre(n, target)) {
					cx = target.x;
					cy = target.y;
				}
			}
			setDebugMousePos(Common::Point(cx, cy));
			_inputs |= NancyInput::kLeftMouseButtonUp;

			// getInput() drops anything whose begin-state does not match the
			// current one, so a synthesised click has to claim the state too.
			_inputBeginState = g_nancy->getState();
			warning("AUTOCLICK at %d,%d", cx, cy);

			// The explorer's clicks were the ONLY synthetic clicks not on the
			// trace - every other one (nancy_scene_script and all five autoplay
			// hooks) emits a "click" event, and this one emitted a warning and
			// nothing else. A driver reading the trace therefore saw the scene
			// loads the explorer caused but not the clicks that caused them, and
			// had to fall back on the log, which carries no scene and no rect.
			// That is what made observed-edge attribution guesswork: the same
			// blind (320,200) fallback appears in the log whether it landed on a
			// hotspot or not. Emitting the scene here is what lets the reader
			// test the click against the clickable set the trace already
			// publishes. Inert unless nancy_trace is on.
			if (Trace::isOn()) {
				TraceEvent("click")
					.str("hook", "explore")
					.num("scene", g_nancy->getState() == NancyState::kScene ?
						debugGetCurrentSceneID() : -1)
					.num("x", cx)
					.num("y", cy)
					.emit();
			}
		}
	}

	// Debug affordance, the drag twin of nancy_autoclick: walk a press-move-
	// release gesture along "x,y;x,y;..." - button down on the first point, held
	// on each one after it, released past the end. nancy_autoclick only ever
	// synthesises a button-up, so the records that read kLeftMouseButtonHeld
	// (the microdot viewer's pan, for one) cannot be driven headlessly with it.
	if (ConfMan.hasKey("nancy_autodrag")) {
		static int dpolls = 0;
		static uint dstep = 0;
		const int after = ConfMan.hasKey("nancy_autodrag_after") ?
			ConfMan.getInt("nancy_autodrag_after") : 120;
		const int every = ConfMan.hasKey("nancy_autodrag_every") ?
			ConfMan.getInt("nancy_autodrag_every") : 3;

		++dpolls;
		if (dpolls >= after && ((dpolls - after) % MAX(every, 1)) == 0) {
			Common::StringTokenizer pts(ConfMan.get("nancy_autodrag"), ";");
			uint idx = 0;
			Common::String pt;
			while (!pts.empty()) {
				const Common::String next = pts.nextToken();
				if (idx++ == dstep) {
					pt = next;
					break;
				}
			}

			if (!pt.empty()) {
				const uint comma = pt.findFirstOf(',');
				if (comma != Common::String::npos) {
					const int dx = atoi(pt.substr(0, comma).c_str());
					const int dy = atoi(pt.substr(comma + 1).c_str());
					setDebugMousePos(Common::Point(dx, dy));
					_inputs |= NancyInput::kLeftMouseButtonHeld;
					if (dstep == 0) {
						_inputs |= NancyInput::kLeftMouseButtonDown;
					}

					_inputBeginState = g_nancy->getState();
					warning("AUTODRAG %s at %d,%d", dstep == 0 ? "down" : "move", dx, dy);
				}

				++dstep;
			} else if (_inputs & NancyInput::kLeftMouseButtonHeld) {
				_inputs &= ~NancyInput::kLeftMouseButtonHeld;
				_inputs |= NancyInput::kLeftMouseButtonUp;
				_inputBeginState = g_nancy->getState();
				warning("AUTODRAG up");
			}
		}
	}

	// Debug affordance, the keyboard twin of nancy_autoclick: type a string one
	// character per Nth poll, then press Enter. The text-entry records (the
	// office laptop password box, for one) cannot be reached by a mouse-only
	// test at all. "_" in the string stands for a space, so the value survives
	// config parsing.
	if (ConfMan.hasKey("nancy_autotype")) {
		static int kpolls = 0;
		static uint typed = 0;
		const Common::String text = ConfMan.get("nancy_autotype");
		const int after = ConfMan.hasKey("nancy_autotype_after") ?
			ConfMan.getInt("nancy_autotype_after") : 120;
		const int every = ConfMan.hasKey("nancy_autotype_every") ?
			ConfMan.getInt("nancy_autotype_every") : 6;

		++kpolls;
		if (kpolls >= after && typed <= text.size() && ((kpolls - after) % every) == 0) {
			if (typed < text.size()) {
				char c = text[typed];
				if (c == '_') {
					c = ' ';
				}

				_otherKbdInput.push_back(KeyState((KeyCode)c, (uint16)c));
			} else {
				_otherKbdInput.push_back(KeyState(KEYCODE_RETURN, ASCII_RETURN));
			}

			++typed;
			_inputBeginState = g_nancy->getState();
		}
	}

	// Debug affordance: leave the microdot viewers (S3280/S3281) at the moment
	// their exit record's player-table gate opens. Like the chase below, this
	// puzzle is scene-scripted rather than a record class, so the decision is
	// made in microdotautoplay.cpp and only the click is synthesised here, on
	// the record's own hotspot. Inert unless nancy_microdot_autoplay is set and
	// the viewer's counter has arrived; see that file for the model.
	if ((_inputs & NancyInput::kLeftMouseButtonUp) == 0) {
		Common::Point microdotClick;
		if (microdotAutoPlayNextClick(microdotClick)) {
			setDebugMousePos(microdotClick);
			_inputs |= NancyInput::kLeftMouseButtonUp;
			_inputBeginState = g_nancy->getState();
			if (Trace::isOn()) {
				TraceEvent("click")
					.str("hook", "microdot")
					.num("x", microdotClick.x)
					.num("y", microdotClick.y)
					.emit();
			}
		}
	}

	// Debug affordance: play the Venice villain chase. Unlike the other autoplay
	// hooks this one cannot live inside the record it belongs to - the record
	// (AR 213, s5450) runs in a concurrent stream, while Nancy's move is a click
	// in the main flow - so the decision is made in chasemappuzzle.cpp and the
	// click is synthesised here, on the map hotspot a player would use. Inert
	// unless nancy_chase_autoplay is set AND the main flow is standing in one of
	// the 26 map scenes with the chase running; see that file for the model.
	//
	// Last of the click hooks, and skipped when one of them has already claimed
	// this poll, so a run can drive the approach with nancy_scene_script and
	// hand over cleanly when it arrives.
	if ((_inputs & NancyInput::kLeftMouseButtonUp) == 0) {
		Common::Point chaseClick;
		if (chaseAutoPlayNextClick(chaseClick)) {
			setDebugMousePos(chaseClick);
			_inputs |= NancyInput::kLeftMouseButtonUp;
			_inputBeginState = g_nancy->getState();
			if (Trace::isOn()) {
				TraceEvent("click")
					.str("hook", "chase")
					.num("x", chaseClick.x)
					.num("y", chaseClick.y)
					.emit();
			}
		}
	}

	// Debug affordance: work the flooded vault's five valve wheels (S6700) until
	// all five pressure gauges read the same. Scene-scripted like the microdot
	// viewers above, so the plan is made in vaultgaugeautoplay.cpp - a search
	// over the safe gauge readings - and only the click on the chosen arrow's own
	// hotspot is synthesised here. Inert unless nancy_vault_autoplay is set.
	if ((_inputs & NancyInput::kLeftMouseButtonUp) == 0) {
		Common::Point vaultClick;
		if (vaultGaugeAutoPlayNextClick(vaultClick)) {
			setDebugMousePos(vaultClick);
			_inputs |= NancyInput::kLeftMouseButtonUp;
			_inputBeginState = g_nancy->getState();
			if (Trace::isOn()) {
				TraceEvent("click")
					.str("hook", "vault")
					.num("x", vaultClick.x)
					.num("y", vaultClick.y)
					.emit();
			}
		}
	}

	// Debug affordance: walk the water-well tunnel maze until its own record
	// raises EV_Solved_Tunnel_Locks. Scene-scripted like the microdot viewers,
	// and spread over 223 scenes rather than one, so the map is built and
	// searched in watermazeautoplay.cpp and only the click on the chosen
	// record's own hotspot is synthesised here. Inert unless nancy_maze_autoplay
	// is set; see that file for the model.
	if ((_inputs & NancyInput::kLeftMouseButtonUp) == 0) {
		Common::Point mazeClick;
		if (waterMazeAutoPlayNextClick(mazeClick)) {
			setDebugMousePos(mazeClick);
			_inputs |= NancyInput::kLeftMouseButtonUp;
			_inputBeginState = g_nancy->getState();
			if (Trace::isOn()) {
				TraceEvent("click")
					.str("hook", "maze")
					.num("x", mazeClick.x)
					.num("y", mazeClick.y)
					.emit();
			}
		}
	}

	while (g_nancy->getEventManager()->pollEvent(event)) {
		switch (event.type) {
		case EVENT_KEYDOWN:
			// Push all keyboard events into an array and let getInput() callers handle them
			_otherKbdInput.push_back(event.kbd);
			_inputBeginState = g_nancy->getState();
			break;
		case EVENT_CUSTOM_ENGINE_ACTION_START:
			_inputBeginState = g_nancy->getState();

			switch (event.customType) {
			case kNancyActionLeftClick:
				_inputs |= NancyInput::kLeftMouseButtonDown;
				_inputs |= NancyInput::kLeftMouseButtonHeld;
				break;
			case kNancyActionRightClick:
				_inputs |= NancyInput::kRightMouseButtonDown;
				_inputs |= NancyInput::kRightMouseButtonHeld;
				break;
			case kNancyActionMoveUp:
				_inputs |= NancyInput::kMoveUp;
				break;
			case kNancyActionMoveDown:
				_inputs |= NancyInput::kMoveDown;
				break;
			case kNancyActionMoveLeft:
				_inputs |= NancyInput::kMoveLeft;
				break;
			case kNancyActionMoveRight:
				_inputs |= NancyInput::kMoveRight;
				break;
			case kNancyActionMoveFast:
				_inputs |= NancyInput::kMoveFastModifier;
				break;
			case kNancyActionOpenMainMenu:
				_inputs |= NancyInput::kOpenMainMenu;
				break;
			case kNancyActionShowRaycastMap:
				_inputs |= NancyInput::kRaycastMap;
				break;
			default:
				break;
			}

			break;
		case EVENT_CUSTOM_ENGINE_ACTION_END:
			switch (event.customType) {
			case kNancyActionLeftClick:
				_inputs |= NancyInput::kLeftMouseButtonUp;
				_inputs &= ~NancyInput::kLeftMouseButtonHeld;
				break;
			case kNancyActionRightClick:
				_inputs |= NancyInput::kRightMouseButtonUp;
				_inputs &= ~NancyInput::kRightMouseButtonHeld;
				break;
			case kNancyActionMoveUp:
				_inputs &= ~NancyInput::kMoveUp;
				break;
			case kNancyActionMoveDown:
				_inputs &= ~NancyInput::kMoveDown;
				break;
			case kNancyActionMoveLeft:
				_inputs &= ~NancyInput::kMoveLeft;
				break;
			case kNancyActionMoveRight:
				_inputs &= ~NancyInput::kMoveRight;
				break;
			case kNancyActionMoveFast:
				_inputs &= ~NancyInput::kMoveFastModifier;
				break;
			case kNancyActionOpenMainMenu:
				_inputs &= ~NancyInput::kOpenMainMenu;
				break;
			case kNancyActionShowRaycastMap:
				_inputs &= ~NancyInput::kRaycastMap;
				break;
			default:
				break;
			}

			break;
		default:
			break;
		}
	}

	if (_inputs == 0 && _otherKbdInput.size() == 0) {
		_inputBeginState = NancyState::kNone;
	}
}

NancyInput InputManager::getInput() const {
	NancyInput ret;

	// Filter out inputs that began in other states; e.g. if the mouse was pushed and held down
	// in a previous state, the button up event won't fire. Right now we simply block all events
	// until everything's clear, but if that causes problems the fix should be easy.
	if (_inputBeginState == g_nancy->getState()) {
		ret.input = _inputs;
		ret.otherKbdInput = _otherKbdInput;
	} else {
		ret.input = 0;
	}

	if (_mouseEnabled || g_nancy->getState() == NancyState::kCredits) {
		// A debug hook driving the game supplies its own cursor position; see
		// _debugMousePos. Without this the hooks had to warp the real pointer.
		ret.mousePos = _debugMousePosSet ? _debugMousePos
			: g_nancy->getEventManager()->getMousePos();
	} else {
		ret.eatMouseInput();
	}

	return ret;
}

void InputManager::forceCleanInput() {
	_inputs = 0;
	_otherKbdInput.clear();
}

void InputManager::setKeymapEnabled(Common::String keymapName, bool enabled) {
	Common::Keymapper *keymapper = g_nancy->getEventManager()->getKeymapper();
	Common::Keymap *keymap = keymapper->getKeymap(keymapName);
	if (keymap)
		keymap->setEnabled(enabled);
}

void InputManager::setVKEnabled(bool enabled) {
	g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, enabled);
}

void InputManager::initKeymaps(Common::KeymapArray &keymaps, const char *target) {
	using namespace Common;
	using namespace Nancy;

	Common::String gameId = ConfMan.get("gameid", target);
	Keymap *mainKeymap = new Keymap(Keymap::kKeymapTypeGame, "nancy-main", _("Nancy Drew"));
	Action *act;

	act = new Action(kStandardActionLeftClick, _("Left click"));
	act->setLeftClickEvent();
	act->setCustomEngineActionEvent(kNancyActionLeftClick);
	act->addDefaultInputMapping("MOUSE_LEFT");
	act->addDefaultInputMapping("JOY_A");
	mainKeymap->addAction(act);

	act = new Action(kStandardActionRightClick, _("Right click"));
	act->setRightClickEvent();
	act->setCustomEngineActionEvent(kNancyActionRightClick);
	act->addDefaultInputMapping("MOUSE_RIGHT");
	act->addDefaultInputMapping("JOY_B");
	mainKeymap->addAction(act);

	act = new Action(kStandardActionMoveUp, _("Move up"));
	act->setCustomEngineActionEvent(kNancyActionMoveUp);
	act->addDefaultInputMapping("UP");
	act->addDefaultInputMapping("JOY_UP");
	mainKeymap->addAction(act);

	act = new Action(kStandardActionMoveDown, _("Move down"));
	act->setCustomEngineActionEvent(kNancyActionMoveDown);
	act->addDefaultInputMapping("DOWN");
	act->addDefaultInputMapping("JOY_DOWN");
	mainKeymap->addAction(act);

	act = new Action(kStandardActionMoveLeft, _("Move left"));
	act->setCustomEngineActionEvent(kNancyActionMoveLeft);
	act->addDefaultInputMapping("LEFT");
	act->addDefaultInputMapping("JOY_LEFT");
	mainKeymap->addAction(act);

	act = new Action(kStandardActionMoveRight, _("Move right"));
	act->setCustomEngineActionEvent(kNancyActionMoveRight);
	act->addDefaultInputMapping("RIGHT");
	act->addDefaultInputMapping("JOY_RIGHT");
	mainKeymap->addAction(act);

	act = new Action("FASTM", _("Fast move modifier"));
	act->setCustomEngineActionEvent(kNancyActionMoveFast);
	act->addDefaultInputMapping("LCTRL");
	act->addDefaultInputMapping("JOY_LEFT_SHOULDER");
	mainKeymap->addAction(act);

	act = new Action("MMENU", _("Open main menu"));
	act->setCustomEngineActionEvent(kNancyActionOpenMainMenu);
	act->addDefaultInputMapping("ESCAPE");
	act->addDefaultInputMapping("JOY_START");
	mainKeymap->addAction(act);

	keymaps.push_back(mainKeymap);

	if (gameId == "nancy3" || gameId == "nancy6") {
		Keymap *mazeKeymap = new Keymap(Keymap::kKeymapTypeGame, _mazeKeymapID, _("Nancy Drew - Maze"));

		act = new Action("RAYCM", _("Show / hide maze map"));
		act->setCustomEngineActionEvent(kNancyActionShowRaycastMap);
		act->addDefaultInputMapping("m");
		act->addDefaultInputMapping("JOY_RIGHT_SHOULDER");
		mazeKeymap->addAction(act);
		mazeKeymap->setEnabled(false);

		keymaps.push_back(mazeKeymap);
	}
}

} // End of namespace Nancy
