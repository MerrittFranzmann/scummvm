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

#ifndef NANCY_H
#define NANCY_H

#include "common/file.h"
#include "common/str.h"
#include "common/ptr.h"

#include "engines/engine.h"

#include "nancy/detection.h"
#include "nancy/time.h"
#include "nancy/commontypes.h"
#include "nancy/enginedata.h"
#include "nancy/ttffont.h"
#include "nancy/trace.h"

namespace Common {
class RandomSource;
class Serializer;
}

/**
 * This is the namespace of the Nancy engine.
 *
 * Status of this engine:
 * The Vampire Diaries and all Nancy Drew games up to and including
 * Nancy Drew: Danger on Deception Island are fully completable.
 * Every other game is untested but definitely unplayable.
 *
 * Games using this engine:
 *	- The Vampire Diaries (1996)
 *	- Almost every mainline Nancy Drew game by HeR Interactive,
 *		beginning with Nancy Drew: Secrets can Kill (1998)
 *		up to and including Nancy Drew: Sea of Darkness (2015)
 */
namespace Nancy {

// 8 adds the Nancy16 stream table (see streams.h) to the Scene block.
// 9 adds the Nancy16 NDUI show/hide deltas (see nduipanel.h) to the Scene block.
static const int kSavegameVersion = 9;

// The three options-screen settings ScummVM has no key of its own for, kept in
// the game's config domain beside the ones it does (music_volume, sfx_volume,
// speech_volume, subtitles). Each is stored in the same units the game's own
// PhantomOfVenice.INI uses, so a value can be read straight across:
//
//   nancy_matte_colour  a COLR chunk id. The INI stores the resolved ARGB
//                       (BackgroundColor=4278190080 = 0xff000000 = COLR 4), but
//                       the id is what the options screen sends, and it survives
//                       a change to the palette.
//   nancy_font_scale    percent. The INI's FontScale is this x10 (1270 = 127%).
//   nancy_window_mode   0 standard/CRT, 1 windowed, 2 widescreen/LCD - the INI's
//                       WindowMode, whose own comment gives those three names.
extern const char *const kConfMatteColour;
extern const char *const kConfFontScale;
extern const char *const kConfWindowMode;

// COLR chunk 4, black: what the retail INI ships as BackgroundColor.
static const int kDefaultMatteColourID = 4;

// The port renders text at the authored FONT heights, which is the 100% option.
// The retail INI ships 127, i.e. the game's default is "Large Text"; adopting
// that as the port's default would change every text metric in the game, so it
// is offered rather than imposed.
static const int kDefaultFontScalePercent = 100;

struct NancyGameDescription;

class ResourceManager;
class IFF;
class InputManager;
class SoundManager;
class GraphicsManager;
class CursorManager;
class NancyConsole;
class DeferredLoader;

namespace State {
class State;
}

class NancyEngine : public Engine {
public:
	NancyEngine(OSystem *syst, const NancyGameDescription *gd);
	~NancyEngine();

	static NancyEngine *create(GameType type, OSystem *syst, const NancyGameDescription *gd);

	void errorString(const char *buf_input, char *buf_output, int buf_output_size) override;
	bool hasFeature(EngineFeature f) const override;

	Common::Error loadGameState(int slot) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;

	void secondChance();

	// Nancy16 action record 114, "Save a Named Game", asks for a checkpoint save
	// under a script-authored name such as "Start_Game". ScummVM save slots are
	// integers, so the name has to be mapped onto one; see nancy.cpp for the
	// rules. Silently does nothing if the engine cannot save right now.
	void saveNamedGame(const Common::String &name);

	// The slot a named save resolves to, or -1 if the reserved band is full.
	// Exposed for the console/tests; saveNamedGame is the normal entry point.
	int getNamedSaveSlot(const Common::String &name) const;

	// The slot a named save already occupies, or -1 if no save by that name
	// exists. Distinct from getNamedSaveSlot(), which falls back to the first
	// free slot - a fine answer for "where should this be written", and exactly
	// the wrong one for "what should be read back".
	int findNamedSaveSlot(const Common::String &name) const;

	// Nancy16 action record 115, "Load a saved Game", is the other half of 114:
	// it restores the checkpoint that 114 wrote under the same name. The load is
	// queued rather than performed, because it tears down the scene - including
	// the record array ActionManager is in the middle of iterating - and runs
	// from the top of the main loop instead. See runPendingNamedLoad().
	void requestNamedLoad(const Common::String &name);

	// Nancy16 action record 108, "Clear Saved Data", drops a named save.
	void deleteNamedGame(const Common::String &name);

	// The NDUI load dialog's own Load button. Deferred for the same reason
	// requestNamedLoad() is: the load tears down the Scene whose input handler
	// asked for it, so it cannot happen on the stack of that handler.
	void requestSlotLoad(int slot);

	// Size of the reserved band of slots that named saves live in. It sits
	// directly below the second chance slot, which is getMaximumSaveSlot().
	static const int kNumNamedSaveSlots = 20;

	// The first slot of that band. Slots below it are the player's own, which is
	// what the NDUI save dialog offers and the NDUI load dialog lists.
	int firstNamedSaveSlot() const;


	const char *getCopyrightString() const;
	uint32 getGameFlags() const;
	const char *getGameId() const;
	GameType getGameType() const;
	Common::Language getGameLanguage() const;
	Common::Platform getPlatform() const;

	const StaticData &getStaticData() const;
	const EngineData *getEngineData(const Common::String &name) const;
	const Common::String getEventFlagName(uint flagID) const;

	void setState(NancyState::NancyState state, NancyState::NancyState overridePrevious = NancyState::kNone);
	NancyState::NancyState getState() { return _gameFlow.curState; }
	void setToPreviousState();

	void setMouseEnabled(bool enabled);

	void addDeferredLoader(Common::SharedPtr<DeferredLoader> &loaderPtr);

	// The first few games used 1/2 for false/true in
	// inventory, logic conditions, and event flags
	const byte _true;
	const byte _false;

	// Managers
	ResourceManager *_resource;

	// Nancy16+ only: substitutes for the real typefaces the FONT registry names
	// but the game does not ship. Empty for earlier games, which use glyph atlases.
	TTFFontProvider _ttfFonts;
	GraphicsManager *_graphics;
	CursorManager *_cursor;
	InputManager *_input;
	SoundManager *_sound;

	NancyRandomSource *_randomSource;

	// Used to check whether we need to show the SaveDialog
	bool _hasJustSaved;

protected:
	Common::Error run() override;
	void pauseEngineIntern(bool pause) override;

private:
	struct GameFlow {
		NancyState::NancyState curState = NancyState::kNone;
		NancyState::NancyState prevState = NancyState::kNone;
		NancyState::NancyState nextState = NancyState::kNone;
		bool changingState = true;
	};

	void bootGameEngine();

	State::State *getStateObject(NancyState::NancyState state) const;
	void destroyState(NancyState::NancyState state) const;

	void preloadCals();
	void readDatFile();
	// Nancy12 onwards no longer ship their static data in nancy.dat; the values
	// the engine still needs are provided here instead (see also the EVNT chunk).
	void populateStaticData();

	Common::Error synchronize(Common::Serializer &serializer);

	// Performs a load queued by requestNamedLoad(). Called from the top of the
	// main loop, at the same point in the frame that a load from ScummVM's own
	// menu happens, so it goes through exactly the sequence that path already
	// exercises.
	void runPendingNamedLoad();

	// Name of a checkpoint save an action record asked to restore, empty when
	// there is nothing pending. A pending load is retried every frame until the
	// engine will take it, so this can outlive the frame that set it.
	Common::String _pendingNamedLoad;

	// Whether the pending load has already been refused once, so the "waiting"
	// note is logged on the first frame rather than on all of them.
	bool _namedLoadWasDeferred = false;

	// Slot the NDUI load dialog asked for, -1 when there is nothing pending.
	// Runs from the same place, and before the named load: a player who clicks
	// Load has overridden whatever a record queued on an earlier frame.
	int _pendingSlotLoad = -1;

	bool isCompressed();

	StaticData _staticData;
	Common::HashMap<Common::String, EngineData *> _engineData;

	const byte _datFileMajorVersion;
	const byte _datFileMinorVersion;

	GameFlow _gameFlow;
	OSystem *_system;

	const NancyGameDescription *_gameDescription;

	Common::Array<Common::WeakPtr<DeferredLoader>> _deferredLoaderObjects;
};

extern NancyEngine *g_nancy;
#define GetEngineData(s) (const s*)g_nancy->getEngineData(#s);

} // End of namespace Nancy

#endif // NANCY_H
