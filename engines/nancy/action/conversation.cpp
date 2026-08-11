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

#include "common/random.h"
#include "common/serializer.h"
#include "common/memstream.h"

#include "engines/nancy/nancy.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/input.h"
#include "engines/nancy/util.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/iff.h"

#include "common/config-manager.h"
#include "engines/nancy/action/conversation.h"
#include "engines/nancy/action/actionmanager.h"
#include "engines/nancy/action/secondarymovie.h"

#include "engines/nancy/state/scene.h"
#include "engines/nancy/nduipanel.h"

namespace Nancy {
namespace Action {

ConversationSound::ConversationSound() :
		RenderActionRecord(8),
		_noResponse(g_nancy->getGameType() <= kGameTypeNancy2 ||
					g_nancy->getGameType() == kGameTypeNancy10 ? 10 : 20),
		_hasDrawnTextbox(false),
		_pickedResponse(-1) {
	_conditionalResponseCharacterID = _noResponse;
	_goodbyeResponseCharacterID = _noResponse;
}

ConversationSound::~ConversationSound() {
	if (NancySceneState.getActiveConversation() == this) {
		NancySceneState.setActiveConversation(nullptr);
	}
}

void ConversationSound::init() {
	RenderObject::init();
}

void ConversationSound::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		readDataNancy16(stream);
		return;
	}

	if (g_nancy->getGameType() >= kGameTypeNancy13) {
		readDataNancy13(stream);
		return;
	}

	Common::Serializer ser(&stream, nullptr);
	ser.setVersion(g_nancy->getGameType());

	if (ser.getVersion() >= kGameTypeNancy2) {
		_sound.readNormal(stream);
	}

	readCaptionText(stream);

	if (ser.getVersion() <= kGameTypeNancy1) {
		_sound.readNormal(stream);
	}

	_responseGenericSound.readNormal(stream);
	ser.skip(1); // RESPONSE_STARTUP_CLEAR_ALL, RESPONSE_STARTUP_KEEP_OLD; never used (tested up to nancy5)
	ser.syncAsByte(_conditionalResponseCharacterID);
	ser.syncAsByte(_goodbyeResponseCharacterID);
	ser.syncAsByte(_defaultNextScene);
	ser.syncAsByte(_popNextScene);
	_sceneChange.readData(stream, ser.getVersion() == kGameTypeVampire);
	ser.skip(0x32, kGameTypeVampire, kGameTypeNancy1);
	ser.skip(2, kGameTypeNancy2);

	uint16 numResponses = 0;
	ser.syncAsUint16LE(numResponses);

	_responses.resize(numResponses);
	for (uint i = 0; i < numResponses; ++i) {
		ResponseStruct &response = _responses[i];
		response.conditionFlags.read(stream);
		readResponseText(stream, response);
		readFilename(stream, response.soundName);
		ser.syncAsByte(response.addRule);
		response.sceneChange.readData(stream, ser.getVersion() == kGameTypeVampire);
		ser.syncAsSint16LE(response.flagDesc.label);
		ser.syncAsByte(response.flagDesc.flag);
		ser.skip(0x32, kGameTypeVampire, kGameTypeNancy1);
		ser.skip(2, kGameTypeNancy2);
	}

	uint16 numSceneBranchStructs = stream.readUint16LE();
	_sceneBranchStructs.resize(numSceneBranchStructs);
	for (uint i = 0; i < numSceneBranchStructs; ++i) {
		_sceneBranchStructs[i].conditions.read(stream);
		_sceneBranchStructs[i].sceneChange.readData(stream, g_nancy->getGameType() == kGameTypeVampire);
		ser.skip(0x32, kGameTypeVampire, kGameTypeNancy1);
		ser.skip(2, kGameTypeNancy2);
	}

	uint16 numFlagsStructs = stream.readUint16LE();
	_flagsStructs.resize(numFlagsStructs);
	for (uint i = 0; i < numFlagsStructs; ++i) {
		FlagsStruct &flagsStruct = _flagsStructs[i];
		flagsStruct.conditions.read(stream);
		flagsStruct.flagToSet.type = stream.readByte();
		flagsStruct.flagToSet.flag.label = stream.readSint16LE();
		flagsStruct.flagToSet.flag.flag = stream.readByte();
	}
}

void ConversationSound::readTerseData(Common::SeekableReadStream &stream) {
	readFilename(stream, _sound.name);
	_sound.volume = stream.readUint16LE();
	_sound.channelID = 12; // hardcoded
	_sound.numLoops = 1;

	_responseGenericSound.volume = _sound.volume;
	_responseGenericSound.numLoops = 1;
	_responseGenericSound.channelID = 13; // hardcoded

	readTerseCaptionText(stream);

	_conditionalResponseCharacterID = stream.readByte();
	_goodbyeResponseCharacterID = stream.readByte();

	_defaultNextScene = stream.readByte();

	_sceneChange.sceneID = stream.readUint16LE();
	if (g_nancy->getGameType() >= kGameTypeNancy10)
		_sceneChange.frameID = stream.readUint16LE();
	_sceneChange.continueSceneSound = kContinueSceneSound;

	uint16 numResponses = stream.readUint16LE();
	_responses.resize(numResponses);
	for (uint i = 0; i < numResponses; ++i) {
		ResponseStruct &response = _responses[i];
		response.conditionFlags.read(stream);
		readTerseResponseText(stream, response);
		readFilename(stream, response.soundName);
		response.sceneChange.sceneID = stream.readUint16LE();
		response.sceneChange.continueSceneSound = kContinueSceneSound;
	}

	// No scene branches
	uint16 numFlagsStructs = stream.readUint16LE();
	_flagsStructs.resize(numFlagsStructs);
	for (uint i = 0; i < numFlagsStructs; ++i) {
		FlagsStruct &flagsStruct = _flagsStructs[i];
		flagsStruct.conditions.read(stream);
		flagsStruct.flagToSet.type = stream.readByte();
		flagsStruct.flagToSet.flag.label = stream.readSint16LE();
		flagsStruct.flagToSet.flag.flag = stream.readByte();
	}
}

void ConversationSound::readDataNancy13(Common::SeekableReadStream &stream) {
	readFilename(stream, _sound.name);
	_sound.channelID = 12;	// hardcoded, as in the terse variants
	_sound.numLoops = 1;

	readCelDataNancy13(stream);

	_conditionalResponseCharacterID = stream.readByte();
	_goodbyeResponseCharacterID = stream.readByte();

	_sceneChange.sceneID = stream.readUint16LE();
	_sceneChange.frameID = stream.readUint16LE();
	_sceneChange.continueSceneSound = kContinueSceneSound;

	// Caption and response texts are external, keyed by sound name in CONVO.
	const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
	assert(convo);
	_text = convo->texts.getValOrDefault(_sound.name, "");

	uint16 numResponses = stream.readUint16LE();
	_responses.resize(numResponses);
	for (uint i = 0; i < numResponses; ++i) {
		ResponseStruct &response = _responses[i];
		readFilename(stream, response.soundName);
		response.sceneChange.sceneID = stream.readUint16LE();
		response.sceneChange.continueSceneSound = kContinueSceneSound;
		response.conditionFlags.read(stream);
		response.text = convo->texts.getValOrDefault(response.soundName, "");
	}

	uint16 numFlagsStructs = stream.readUint16LE();
	_flagsStructs.resize(numFlagsStructs);
	for (uint i = 0; i < numFlagsStructs; ++i) {
		FlagsStruct &flagsStruct = _flagsStructs[i];
		flagsStruct.flagToSet.type = stream.readByte();
		flagsStruct.flagToSet.flag.label = stream.readSint16LE();
		flagsStruct.flagToSet.flag.flag = stream.readByte();
	}
}

// Nancy16's layout, established by exact byte consumption over all 702 type 57
// and 58 records in nancy18:
//
//   uint16   numNames
//   char[33] x numNames           sound names; [0] doubles as the XSheet name
//   byte[8]  0xff                 constant in 702/702
//   char[33] conditional response ("infocheck_<character>"), often empty
//   char[33] goodbye response     ("bye_<character>"), often empty
//   uint16   sceneID              a real scene in 481, the 32767 sentinel in 221
//   uint16   frameID
//   uint16   numResponses
//     char[33] name, uint16 sceneID (real in 197/197), uint16 nConds,
//     ConversationFlag[nConds]     5 bytes each, exactly ConversationFlag::read
//   uint16   numFlags
//     byte type, int16 label, byte flag   4 bytes, as in readDataNancy13
//
// The old Nancy13 reader took the leading count for the first two characters of
// the sound name, so every name came out empty and the XSheet never resolved.
void ConversationSound::readDataNancy16(Common::SeekableReadStream &stream) {
	const uint16 numNames = stream.readUint16LE();
	_nancy16Names.resize(numNames);
	for (uint i = 0; i < numNames; ++i) {
		readFilename(stream, _nancy16Names[i]);
	}

	if (numNames) {
		_sound.name = _nancy16Names[0];
	}

	_sound.channelID = 12;	// hardcoded, as in the terse variants
	_sound.numLoops = 1;

	stream.skip(8);	// 0xff run, constant across the corpus

	Common::String conditionalName, goodbyeName;
	readFilename(stream, conditionalName);
	readFilename(stream, goodbyeName);

	// Pre-Nancy16 these are character ids indexing S<800 + id> / S<900 + id>.
	// Here they are the *script names* of the same two tables, which ship in
	// the ciftree as their own members (INFOCHECK_MARG, BYE_MARG, ...); nancy18
	// has no scene in the 800-999 range at all. Keep the ids as a present /
	// absent switch only, and remember the names for the lookup.
	_nancy16ConditionalScript = conditionalName;
	_nancy16GoodbyeScript = goodbyeName;
	_conditionalResponseCharacterID = conditionalName.empty() ? _noResponse : 0;
	_goodbyeResponseCharacterID = goodbyeName.empty() ? _noResponse : 0;

	_sceneChange.sceneID = stream.readUint16LE();
	_sceneChange.frameID = stream.readUint16LE();
	_sceneChange.continueSceneSound = kContinueSceneSound;

	if (_sceneChange.sceneID == kNancy16NoScene) {
		_sceneChange.sceneID = kNoScene;
	}

	const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
	if (convo) {
		_text = convo->texts.getValOrDefault(_sound.name, "");
	}

	const uint16 numResponses = stream.readUint16LE();
	_responses.resize(numResponses);
	for (uint i = 0; i < numResponses; ++i) {
		ResponseStruct &response = _responses[i];
		readFilename(stream, response.soundName);
		response.sceneChange.sceneID = stream.readUint16LE();
		response.sceneChange.continueSceneSound = kContinueSceneSound;
		response.conditionFlags.read(stream);

		if (convo) {
			response.text = convo->texts.getValOrDefault(response.soundName, "");
		}
	}

	const uint16 numFlagsStructs = stream.readUint16LE();
	_flagsStructs.resize(numFlagsStructs);
	for (uint i = 0; i < numFlagsStructs; ++i) {
		FlagsStruct &flagsStruct = _flagsStructs[i];
		flagsStruct.flagToSet.type = stream.readByte();
		flagsStruct.flagToSet.flag.label = stream.readSint16LE();
		flagsStruct.flagToSet.flag.flag = stream.readByte();
	}
}

// The base conversation has no cels; skip the frame fields.
void ConversationSound::readCelDataNancy13(Common::SeekableReadStream &stream) {
	stream.skip(8); // firstFrame + lastFrame (int32)
}

void ConversationSound::readCaptionText(Common::SeekableReadStream &stream) {
	char *rawText = new char[1500];
	stream.read(rawText, 1500);
	assembleTextLine(rawText, _text, 1500);
	delete[] rawText;
}

void ConversationSound::readResponseText(Common::SeekableReadStream &stream, ResponseStruct &response) {
	char *rawText = new char[400];
	stream.read(rawText, 400);
	assembleTextLine(rawText, response.text, 400);
	delete[] rawText;
}

void ConversationSound::readTerseCaptionText(Common::SeekableReadStream &stream) {
	Common::String key;
	readFilename(stream, key);

	const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
	assert(convo);

	_text = convo->texts.getValOrDefault(key, "");
}

void ConversationSound::readTerseResponseText(Common::SeekableReadStream &stream, ResponseStruct &response) {
	Common::String key;
	readFilename(stream, key);

	const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
	assert(convo);

	response.text = convo->texts.getValOrDefault(key, "");
}

void ConversationSound::execute() {
	ConversationSound *activeConversation = NancySceneState.getActiveConversation();
	if (activeConversation != this && activeConversation != nullptr) {
		if (	!activeConversation->_isDone ||
				activeConversation->_defaultNextScene == kDefaultNextSceneEnabled ||
				activeConversation->_pickedResponse != -1	) {

			return;
		} else {
			// Chained videos, hide the previous one and start this
			activeConversation->setVisible(false);
			NancySceneState.setActiveConversation(this);
		}
	}

	switch (_state) {
	case kBegin: {
		init();
		g_nancy->_sound->loadSound(_sound);

		if (!ConfMan.getBool("speech_mute") && ConfMan.getBool("character_speech")) {
			g_nancy->_sound->playSound(_sound);
		}

		// Remove held item and re-add it to inventory
		NancySceneState.setNoHeldItem();

		// Move the mouse to the default position defined in CURS
		const Common::Point initialMousePos = g_nancy->_cursor->getPrimaryVideoInitialPos();
		const Common::Point cursorHotspot = g_nancy->_cursor->getCurrentCursorHotspot();
		Common::Point adjustedMousePos = g_nancy->_input->getInput().mousePos;
		adjustedMousePos.x -= cursorHotspot.x;
		adjustedMousePos.y -= cursorHotspot.y - 1;
		if (g_nancy->_cursor->getPrimaryVideoInactiveZone().bottom > adjustedMousePos.y) {
			g_nancy->_cursor->warpCursor(Common::Point(initialMousePos.x + cursorHotspot.x, initialMousePos.y + cursorHotspot.y));
			g_nancy->_cursor->setCursorType(CursorManager::kNormalArrow);
		}

		_state = kRun;
		NancySceneState.setActiveConversation(this);

		// Do not draw first frame since video won't be loaded yet
		g_nancy->_graphics->suppressNextDraw();

		if (g_nancy->getGameType() < kGameTypeNancy6) {
			// Do not fall through to give the execution one loop for event flag changes
			// This fixes TVD scene 750
			break;
		}

		// However, nancy6 scene 1299 requires us to fall through in order to get the correct caption.
		// By that point Conversation scenes weren't the tangled mess they were in earlier games,
		// so hopefully this won't break anything
	}
		// fall through
	case kRun:
		if (!_hasDrawnTextbox) {
			_hasDrawnTextbox = true;
			if (NDUIPanel *convo = NancySceneState.getConversationPanel()) {
				// Nancy16+: the NDUI CONVO panel stands in for ConversationPopup.
				convo->convoOpen();

				if (ConfMan.getBool("subtitles")) {
					convo->convoSetCaption(_text);
				}
			} else if (g_nancy->getGameType() >= kGameTypeNancy10) {
				NancySceneState.getConversationPopup().open();

				if (ConfMan.getBool("subtitles")) {
					NancySceneState.getConversationPopup().addTextLine(_text);
				}
			} else {
				auto *textboxData = GetEngineData(TBOX);
				assert(textboxData);
				NancySceneState.getTextbox().clear();
				NancySceneState.getTextbox().setOverrideFont(textboxData->conversationFontID);

				if (ConfMan.getBool("subtitles")) {
					NancySceneState.getTextbox().addTextLine(_text);
				}
			}

			Common::Array<uint> responsesToAdd;
			for (uint i = 0; i < _responses.size(); ++i) {
				auto &res = _responses[i];

				if (res.conditionFlags.isSatisfied()) {
					int foundIndex = -1;
					for (uint j = 0; j < responsesToAdd.size(); ++j) {
						if (_responses[responsesToAdd[j]].text.compareToIgnoreCase(res.text) == 0) {
							foundIndex = j;
							break;
						}
					}
					switch(res.addRule) {
					case ResponseStruct::kAddIfNotFound:
						if (foundIndex == -1) {
							responsesToAdd.push_back(i);
						}

						break;
					case ResponseStruct::kRemoveAndAddToEnd:
						if (foundIndex != -1) {
							responsesToAdd.remove_at(foundIndex);
						}

						responsesToAdd.push_back(i);
						break;
					case ResponseStruct::kRemove:
						if (foundIndex != -1) {
							responsesToAdd.remove_at(foundIndex);
						}

						break;
					}
				}
			}

			uint numRegularResponses = _responses.size();

			// Add responses when conditions have been satisfied
			if (_conditionalResponseCharacterID != _noResponse) {
				addConditionalDialogue();
			}

			if (_goodbyeResponseCharacterID != _noResponse) {
				addGoodbye();
			}

			// If conditionals/goodbyes added, make sure to send them to Textbox
			for (uint i = numRegularResponses; i < _responses.size(); ++i) {
				responsesToAdd.push_back(i);
			}

			NDUIPanel *convoPanel = NancySceneState.getConversationPanel();
			if (!convoPanel && g_nancy->getGameType() >= kGameTypeNancy10) {
				NancySceneState.getConversationPopup().setResponseStart();
			}

			for (uint i : responsesToAdd) {
				if (convoPanel) {
					convoPanel->convoAddResponse(_responses[i].text);
				} else if (g_nancy->getGameType() >= kGameTypeNancy10) {
					NancySceneState.getConversationPopup().addTextLine(_responses[i].text);
				} else {
					NancySceneState.getTextbox().addTextLine(_responses[i].text);
				}
				_responses[i].isOnScreen = true;
			}
		}

		if (!g_nancy->_sound->isSoundPlaying(_sound) && isVideoDonePlaying()) {
			g_nancy->_sound->stopSound(_sound);

			bool hasResponses = false;
			for (auto &res : _responses) {
				if (res.isOnScreen) {
					hasResponses = true;
					break;
				}
			}

			if (!hasResponses) {
				// NPC has finished talking with no responses available, auto-advance to next scene.
				//
				// CLOSE THE PANEL HERE TOO. kRun has two exits and only the
				// other one - the player picking a response, below - used to
				// close it, so a conversation that ends without offering a
				// choice left the box on screen with the last line still in it.
				// It looked intermittent because it only happens on this path,
				// and because it is cleared incidentally later by the next
				// ConcatSound narration that runs out (soundrecords.cpp) or by
				// a load (scene.cpp). Reaching here already required
				// !isSoundPlaying(_sound) && isVideoDonePlaying(), so the line
				// has finished playing and there is nothing left to read.
				if (NDUIPanel *convo = NancySceneState.getConversationPanel()) {
					convo->convoClose();
				}

				_state = kActionTrigger;
			} else {
				// NPC has finished talking, we have responses.
				// Nancy16+ hit-tests its responses inside the NDUI panel rather
				// than through the logic conditions the popup sets, and the index
				// it reports already counts only the on-screen ones.
				if (NDUIPanel *convo = NancySceneState.getConversationPanel()) {
					const int picked = convo->convoTakePickedResponse();
					if (picked >= 0) {
						int seen = -1;
						for (uint j = 0; j < _responses.size(); ++j) {
							if (!_responses[j].isOnScreen) {
								continue;
							}

							if (++seen == picked) {
								_pickedResponse = (int)j;
								break;
							}
						}
					}
				}

				for (uint i = 0; _pickedResponse == -1 && i < 30; ++i) {
					if (NancySceneState.getLogicCondition(i, g_nancy->_true)) {
						_pickedResponse = i;

						// Adjust to account for hidden responses
						for (uint j = 0; j < _responses.size(); ++j) {
							if (!_responses[j].isOnScreen) {
								++_pickedResponse;
							}

							if ((int)j == _pickedResponse) {
								break;
							}
						}

						break;
					}
				}

				if (_pickedResponse != -1) {
					if (NDUIPanel *convo = NancySceneState.getConversationPanel()) {
						convo->convoClose();
					}

					// Player has picked response, play sound file and change state
					_responseGenericSound.name = _responses[_pickedResponse].soundName;
					g_nancy->_sound->loadSound(_responseGenericSound);

					if (!ConfMan.getBool("speech_mute") && ConfMan.getBool("player_speech")) {
						g_nancy->_sound->playSound(_responseGenericSound);
					}

					// Nancy 11+: play a fresh random sequence as the character's response anim.
					if (PlaySecondaryMovie *active = NancySceneState.getActiveMovie()) {
						if (active->_isRandom) {
							active->playRandomSequence();
						}
					}

					_state = kActionTrigger;
				}
			}
		}
		break;
	case kActionTrigger:
		if (!g_nancy->_sound->isSoundPlaying(_responseGenericSound)) {
			// process flags structs
			for (auto &flags : _flagsStructs) {
				if (flags.conditions.isSatisfied()) {
					flags.flagToSet.set();
				}
			}

			if (_pickedResponse != -1) {
				// Set response's event flag, if any
				NancySceneState.setEventFlag(_responses[_pickedResponse].flagDesc);
			}

			g_nancy->_sound->stopSound(_responseGenericSound);

			if (_pickedResponse != -1) {
				NancySceneState.changeScene(_responses[_pickedResponse].sceneChange);
			} else {
				for (uint i = 0; i < _sceneBranchStructs.size(); ++i) {
					if (_sceneBranchStructs[i].conditions.isSatisfied()) {
						NancySceneState.changeScene(_sceneBranchStructs[i].sceneChange);
						break;
					}
				}

				if (_defaultNextScene == kDefaultNextSceneEnabled) {
					NancySceneState.changeScene(_sceneChange);
				} else if (_popNextScene == kPopNextScene) {
					// Exit dialogue
					NancySceneState.popScene();
				}
			}

			if (g_nancy->getGameType() >= kGameTypeNancy10) {
				NancySceneState.getConversationPopup().close();
			}
			
			finishExecution();
		}

		break;
	}
}

void ConversationSound::addConditionalDialogue() {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		if (!_nancy16ConditionalScript.empty()) {
			addConditionalDialogueFromScript(Common::Path(_nancy16ConditionalScript));
		}

		return;
	}

	if (g_nancy->getGameType() >= kGameTypeNancy12) {
		addConditionalDialogueNancy12();
		return;
	}

	// We adjust the base label for Nancy 11 event flags, which can be
	// larger than 1000.
	int16 baseLabel = g_nancy->getGameType() >= kGameTypeNancy11 ? 1000 : 0;

	for (const auto &res : g_nancy->getStaticData().conditionalDialogue[_conditionalResponseCharacterID]) {
		bool isSatisfied = true;

		for (const auto &cond : res.conditions) {
			switch (cond.type) {
			case (byte)StaticDataConditionType::kEvent :
				if (!NancySceneState.getEventFlag(baseLabel + cond.label, cond.flag)) {
					isSatisfied = false;
				}

				break;
			case (byte)StaticDataConditionType::kInventory :
				if (NancySceneState.hasItem(cond.label) != cond.flag) {
					isSatisfied = false;
				}

				break;
			case (byte)StaticDataConditionType::kDifficulty :
				if (	(NancySceneState.getDifficulty() != cond.label && cond.flag != 0) ||
						(NancySceneState.getDifficulty() == cond.label && cond.flag == 0) ) {
					isSatisfied = false;
				}

				break;
			}

			if (!isSatisfied) {
				break;
			}
		}

		if (isSatisfied) {
			_responses.push_back(ResponseStruct());
			ResponseStruct &newResponse = _responses.back();
			newResponse.soundName = res.soundID;

			if (g_nancy->getGameType() <= kGameTypeNancy5) {
				// String is also inside nancy.dat
				newResponse.text = g_nancy->getStaticData().conditionalDialogueTexts[res.textID];
			} else {
				// String is inside the CVTX chunk in the CONVO file. Sound ID doubles as string key
				const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
				assert(convo);

				newResponse.text = convo->texts[res.soundID];
			}

			newResponse.sceneChange.sceneID = res.sceneID;
			newResponse.sceneChange.continueSceneSound = kContinueSceneSound;
			newResponse.sceneChange.listenerFrontVector.set(0, 0, 1);

			// Check if the response is a repeat. This can happen when multiple condition combinations
			// trigger the same response.
			for (uint i = 0; i < _responses.size() - 1; ++i) {
				if (	_responses[i].soundName == newResponse.soundName &&
						_responses[i].text == newResponse.text &&
						_responses[i].sceneChange.sceneID == newResponse.sceneChange.sceneID) {
					_responses.pop_back();
					break;
				}
			}
		}
	}
}

void ConversationSound::addGoodbye() {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		if (!_nancy16GoodbyeScript.empty()) {
			addGoodbyeFromScript(Common::Path(_nancy16GoodbyeScript));
		}

		return;
	}

	if (g_nancy->getGameType() >= kGameTypeNancy12) {
		addGoodbyeNancy12();
		return;
	}

	// We adjust the base label for Nancy 11 event flags, which can be
	// larger than 1000.
	int16 baseLabel = g_nancy->getGameType() >= kGameTypeNancy11 ? 1000 : 0;

	auto &res = g_nancy->getStaticData().goodbyes[_goodbyeResponseCharacterID];
	_responses.push_back(ResponseStruct());
	ResponseStruct &newResponse = _responses.back();
	newResponse.soundName = res.soundID;

	if (g_nancy->getGameType() <= kGameTypeNancy5) {
		// String is also inside nancy.dat
		newResponse.text = g_nancy->getStaticData().goodbyeTexts[_goodbyeResponseCharacterID];
	} else {
		// String is inside the CVTX chunk in the CONVO file. Sound ID doubles as string key
		const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
		assert(convo);

		newResponse.text = convo->texts[res.soundID];
	}

	// Evaluate conditions to pick from the collection of replies
	uint sceneChangeID = 0;
	for (uint i = 0; i < res.sceneChanges.size(); ++i) {
		const GoodbyeSceneChange &sc = res.sceneChanges[i];
		if (sc.conditions.size() == 0) {
			// No conditions, default choice
			sceneChangeID = i;
			break;
		} else {
			bool isSatisfied = true;

			for (const auto &cond : sc.conditions) {
				switch (cond.type) {
				case (byte)StaticDataConditionType::kEvent :
					if (!NancySceneState.getEventFlag(baseLabel + cond.label, cond.flag)) {
						isSatisfied = false;
					}

					break;
				case (byte)StaticDataConditionType::kInventory :
					if (NancySceneState.hasItem(cond.label) != cond.flag) {
						isSatisfied = false;
					}

					break;
				case (byte)StaticDataConditionType::kDifficulty :
					if (	(NancySceneState.getDifficulty() != cond.label && cond.flag != 0) ||
							(NancySceneState.getDifficulty() == cond.label && cond.flag == 0) ) {
						isSatisfied = false;
					}

					break;
				}

				if (!isSatisfied) {
					break;
				}
			}

			if (isSatisfied) {
				sceneChangeID = i;
				break;
			}
		}
	}

	const GoodbyeSceneChange &sceneChange = res.sceneChanges[sceneChangeID];

	// The reply from the character is picked randomly
	newResponse.sceneChange.sceneID = sceneChange.sceneIDs[g_nancy->_randomSource->getRandomNumber(sceneChange.sceneIDs.size() - 1)];
	newResponse.sceneChange.continueSceneSound = kContinueSceneSound;
	newResponse.sceneChange.listenerFrontVector.set(0, 0, 1);

	// Set an event flag if applicable
	// Assumes flagToSet is an event flag
	NancySceneState.setEventFlag(sceneChange.flagToSet.label, sceneChange.flagToSet.flag);
}

void ConversationSound::addConditionalDialogueNancy12() {
	// Every action record in scene S<800 + charID> is one conditional response
	addConditionalDialogueFromScript(
		Common::Path(Common::String::format("S%u", 800 + _conditionalResponseCharacterID)));
}

void ConversationSound::addConditionalDialogueFromScript(const Common::Path &sceneName) {
	IFF *iff = g_nancy->_resource->loadIFF(sceneName);
	if (!iff) {
		warning("Could not load conditional dialogue script %s", sceneName.toString().c_str());
		return;
	}

	// Response text is in the CONVO file's CVTX chunk, keyed by sound ID
	const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
	assert(convo);

	Common::SeekableReadStream *chunk = nullptr;
	for (uint i = 0; (chunk = iff->getChunkStream("ACT", i)) != nullptr; ++i) {
		ActionRecord *record = ActionManager::createAndLoadNewRecord(*chunk);
		delete chunk;

		ConversationInfoCheck *infoCheck = dynamic_cast<ConversationInfoCheck *>(record);
		if (infoCheck) {
			NancySceneState.getActionManager().processDependency(infoCheck->_dependencies, *infoCheck, true);

			if (infoCheck->_dependencies.satisfied) {
				_responses.push_back(ResponseStruct());
				ResponseStruct &newResponse = _responses.back();
				newResponse.soundName = infoCheck->_soundID;
				newResponse.text = convo->texts[infoCheck->_soundID];
				newResponse.sceneChange.sceneID = infoCheck->_sceneID;
				newResponse.sceneChange.continueSceneSound = kContinueSceneSound;
				newResponse.sceneChange.listenerFrontVector.set(0, 0, 1);

				debugC(1, kDebugScene, "CONVOEXTRA cond %s snd %s -> scene %u",
					sceneName.toString().c_str(), infoCheck->_soundID.c_str(),
					(uint)infoCheck->_sceneID);

				// Drop the response if it's a repeat
				for (uint j = 0; j < _responses.size() - 1; ++j) {
					if (	_responses[j].soundName == newResponse.soundName &&
							_responses[j].text == newResponse.text &&
							_responses[j].sceneChange.sceneID == newResponse.sceneChange.sceneID) {
						_responses.pop_back();
						break;
					}
				}
			}
		}

		delete record;
	}

	delete iff;
}

void ConversationSound::addGoodbyeNancy12() {
	// Use the first satisfied goodbye record in scene S<900 + charID>
	addGoodbyeFromScript(
		Common::Path(Common::String::format("S%u", 900 + _goodbyeResponseCharacterID)));
}

void ConversationSound::addGoodbyeFromScript(const Common::Path &sceneName) {
	IFF *iff = g_nancy->_resource->loadIFF(sceneName);
	if (!iff) {
		warning("Could not load goodbye script %s", sceneName.toString().c_str());
		return;
	}

	// Response text is in the CONVO file's CVTX chunk, keyed by sound ID
	const CVTX *convo = (const CVTX *)g_nancy->getEngineData("CONVO");
	assert(convo);

	bool added = false;
	Common::SeekableReadStream *chunk = nullptr;
	for (uint i = 0; !added && (chunk = iff->getChunkStream("ACT", i)) != nullptr; ++i) {
		ActionRecord *record = ActionManager::createAndLoadNewRecord(*chunk);
		delete chunk;

		ConversationGoodbye *goodbye = dynamic_cast<ConversationGoodbye *>(record);
		if (goodbye && !goodbye->_soundIDs.empty() && !goodbye->_sceneIDs.empty()) {
			NancySceneState.getActionManager().processDependency(goodbye->_dependencies, *goodbye, true);

			if (goodbye->_dependencies.satisfied) {
				_responses.push_back(ResponseStruct());
				ResponseStruct &newResponse = _responses.back();

				const Common::String &soundID = goodbye->_soundIDs[g_nancy->_randomSource->getRandomNumber(goodbye->_soundIDs.size() - 1)];
				newResponse.soundName = soundID;
				newResponse.text = convo->texts[soundID];
				newResponse.sceneChange.sceneID = goodbye->_sceneIDs[g_nancy->_randomSource->getRandomNumber(goodbye->_sceneIDs.size() - 1)];
				newResponse.sceneChange.continueSceneSound = kContinueSceneSound;
				newResponse.sceneChange.listenerFrontVector.set(0, 0, 1);

				debugC(1, kDebugScene, "CONVOEXTRA bye %s snd %s -> scene %u (of %u candidates)",
					sceneName.toString().c_str(), soundID.c_str(),
					(uint)newResponse.sceneChange.sceneID, goodbye->_sceneIDs.size());

				added = true;
			}
		}

		delete record;
	}

	delete iff;
}

void ConversationInfoCheck::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _soundID);
	_sceneID = stream.readUint16LE();
}

void ConversationGoodbye::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() == kGameTypeNancy12) {
		// A single sound, then a count and a fixed set of 5 candidate scenes
		_soundIDs.resize(1);
		readFilename(stream, _soundIDs[0]);

		uint16 numScenes = stream.readUint16LE();
		_sceneIDs.resize(numScenes);
		for (uint i = 0; i < 5; ++i) {
			uint16 sceneID = stream.readUint16LE();
			if (i < numScenes) {
				_sceneIDs[i] = sceneID;
			}
		}

		return;
	}

	// Nancy13+: a count-prefixed list of sounds, then one of scenes
	uint16 numSounds = stream.readUint16LE();
	readFilenameArray(stream, _soundIDs, numSounds);

	uint16 numScenes = stream.readUint16LE();
	_sceneIDs.resize(numScenes);
	for (uint i = 0; i < numScenes; ++i) {
		_sceneIDs[i] = stream.readUint16LE();
	}
}

void ConversationSound::ConversationFlag::read(Common::SeekableReadStream &stream) {
	type = stream.readByte();
	flag.label = stream.readSint16LE();
	flag.flag = stream.readByte();
	orFlag = stream.readByte();
}

bool ConversationSound::ConversationFlag::isSatisfied() const {
	switch (type) {
	case kFlagEvent:
		return NancySceneState.getEventFlag(flag);
	case kFlagInventory:
		return NancySceneState.hasItem(flag.label) == flag.flag;
	default:
		return false;
	}
}

void ConversationSound::ConversationFlag::set() const {
	switch (type) {
	case kFlagEvent:
		NancySceneState.setEventFlag(flag);
		break;
	case kFlagInventory:
		if (flag.flag == g_nancy->_true) {
			NancySceneState.addItemToInventory(flag.label);
		} else {
			NancySceneState.removeItemFromInventory(flag.label);
		}

		break;
	default:
		break;
	}
}

void ConversationSound::ConversationFlags::read(Common::SeekableReadStream &stream) {
	uint16 numFlags = stream.readUint16LE();

	conditionFlags.resize(numFlags);
	for (uint i = 0; i < numFlags; ++i) {
		conditionFlags[i].read(stream);
	}
}

bool ConversationSound::ConversationFlags::isSatisfied() const {
	Common::Array<bool> conditionsMet(conditionFlags.size(), false);

	for (uint i = 0; i < conditionFlags.size(); ++i) {
		if (conditionFlags[i].isSatisfied()) {
			conditionsMet[i] = true;
		}
	}

	for (uint i = 0; i < conditionsMet.size(); ++i) {
		if (conditionFlags[i].orFlag) {
			bool foundSatisfied = false;
			for (uint j = 0; j < conditionFlags.size(); ++j) {
				if (conditionsMet[j]) {
					foundSatisfied = true;
					break;
				}

				// Found end of orFlag chain
				if (!conditionFlags[j].orFlag) {
					break;
				}
			}

			if (foundSatisfied) {
				for (; i < conditionsMet.size(); ++i) {
					conditionsMet[i] = true;
					if (!conditionFlags[i].orFlag) {
						// End of orFlag chain
						break;
					}
				}
			}
		}
	}

	for (uint i = 0; i < conditionsMet.size(); ++i) {
		if (conditionsMet[i] == false) {
			return false;
		}
	}

	return true;
}

void ConversationVideo::init() {
	if (!_decoder.loadFile(Common::Path(_videoName))) {
		error("Couldn't load video file %s", _videoName.c_str());
	}

	_decoder.seekToFrame(_firstFrame);

	if (!_paletteName.empty()) {
		GraphicsManager::loadSurfacePalette(_drawSurface, _paletteName);
		setTransparent(true);
	}

	ConversationSound::init();
	registerGraphics();
}

void ConversationVideo::readData(Common::SeekableReadStream &stream) {
	Common::Serializer ser(&stream, nullptr);
	ser.setVersion(g_nancy->getGameType());

	readFilename(stream, _videoName);
	readFilename(ser, _paletteName, kGameTypeVampire, kGameTypeVampire);

	ser.skip(2, kGameTypeVampire, kGameTypeNancy1);
	ser.syncAsUint16LE(_videoFormat);
	ser.skip(3); // Quality
	ser.skip(4, kGameTypeVampire, kGameTypeVampire); // paletteStart, paletteSize
	ser.syncAsUint16LE(_firstFrame);
	ser.syncAsUint16LE(_lastFrame);
	ser.skip(8, kGameTypeVampire, kGameTypeNancy1);
	ser.skip(6, kGameTypeNancy2);

	ser.skip(0x10); // Bounds
	readRect(stream, _screenPosition);

	ConversationSound::readData(stream);
}

void ConversationVideo::updateGraphics() {
	if (!_decoder.isVideoLoaded()) {
		return;
	}

	if (!_decoder.isPlaying()) {
		_decoder.start();
	}

	if (_decoder.getCurFrame() == _lastFrame) {
		_decoder.pauseVideo(true);
	}

	if (_decoder.needsUpdate()) {
		GraphicsManager::copyToManaged(*_decoder.decodeNextFrame(), _drawSurface, _videoFormat == kSmallVideoFormat);

		_needsRedraw = true;
	}

	RenderObject::updateGraphics();
}

void ConversationVideo::onPause(bool pause) {
	_decoder.pauseVideo(pause);
	RenderActionRecord::onPause(pause);
}

bool ConversationVideo::isVideoDonePlaying() {
	return _decoder.endOfVideo() || _decoder.getCurFrame() == _lastFrame;
}

Common::String ConversationVideo::getRecordTypeName() const {
	if (g_nancy->getGameType() <= kGameTypeNancy1) {
		return "PlayPrimaryVideo";
	} else {
		return "ConversationVideo";
	}
}

class ConversationCelLoader : public DeferredLoader {
public:
	ConversationCelLoader(ConversationCel &owner) : _owner(owner) {}

private:
	bool loadInner() override { return _owner.load(); }

	ConversationCel &_owner;
};

ConversationCel::~ConversationCel() {
	// Make sure there isn't a single-frame gap between conversation scenes where
	// the character is invisible
	g_nancy->_graphics->suppressNextDraw();
}

void ConversationCel::init() {
	_curFrame = _firstFrame;
	_nextFrameTime = g_nancy->getTotalPlayTime();

	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		ConversationSound::init();

		// The XSheet's layer 1: the head, one alpha video per sheet.
		_nancy16HasVideo = !_sound.name.empty() &&
			_nancy16Video.loadFile(Common::Path(_sound.name), false);

		if (_nancy16HasVideo) {
			_celRObjects.push_back(RenderedCel());	// kNancy16HeadCel
			_lastFrame = _nancy16Video.getFrameCount() ?
				(uint16)(_nancy16Video.getFrameCount() - 1) : 0;
			_frameTime = 66;	// ~15fps, the rate the shipped sheets imply

			// The XSheet's layer 0: the character's body. Only three of these
			// ship (Colin, Helena, Margherita); the STUB_* sheets name a
			// CHARLEENABODY that does not exist and carry an empty rect, so an
			// absent video is a normal, silent no-op. loadFile() resolves the
			// name through SearchMan, which is case-insensitive - the sheet says
			// COLINBODY and the file is ColinBody.bik.
			_nancy16BodyCurFrame = -1;
			_nancy16HasBody = !_nancy16BodyName.empty() && !_nancy16BodyDest.isEmpty() &&
				_nancy16BodyVideo.loadFile(Common::Path(_nancy16BodyName), false) &&
				_nancy16BodyVideo.getFrameCount() > 0;

			if (_nancy16HasBody) {
				_celRObjects.push_back(RenderedCel());	// kNancy16BodyCel
			}

			registerGraphics();
		}

		return;
	}

	ConversationSound::init();

	_loaderPtr.reset(new ConversationCelLoader(*this));
	auto castedPtr = _loaderPtr.dynamicCast<DeferredLoader>();
	g_nancy->addDeferredLoader(castedPtr);

	for (uint i = 0; i < _treeNames.size(); ++i) {
		if (_treeNames[i].size()) {
			_celRObjects.push_back(RenderedCel());
		} else break;
	}

	registerGraphics();
}

void ConversationCel::registerGraphics() {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		// No setTransparent() here: the Nancy16 cels are RGBA video frames and
		// the blit honours their alpha channel directly. See updateGraphics().
		// The body has to draw behind the head, so it takes a lower z than the
		// RenderedCel default of 9; both stay above the viewport (6) and the
		// taskbar (7).
		for (uint i = 0; i < _celRObjects.size(); ++i) {
			_celRObjects[i].setZOrder(i == kNancy16BodyCel ? 8 : 9);
			_celRObjects[i].setVisible(true);
			_celRObjects[i].registerGraphics();
		}

		RenderActionRecord::registerGraphics();
		return;
	}

	for (uint i = 0; i < _celRObjects.size(); ++i) {
		_celRObjects[i].setZOrder(9 + _drawingOrder[i]);
		_celRObjects[i].setVisible(true);
		_celRObjects[i].setTransparent(true);
		_celRObjects[i].registerGraphics();
	}

	RenderActionRecord::registerGraphics();
}

// Paste one XSheet layer's decoded video frame into its RenderedCel, clipped to
// that layer's destination rect and moved there.
//
// The video is drawn 1:1. The decoded frame runs a little past the rect -
// Helena's HBP sheets say 108x132 and the .bik decodes 112x144 - because Bink
// pads to a multiple of 16 in both axes; every one of the six videos is exactly
// its rect rounded up that way. The padding is what produces the seam: the
// character's alpha silhouette is opaque right up to the frame's bottom row and
// side columns, so those rows get pasted straight over whatever is behind, at a
// different exposure. Measured on scene 1100 frame 0 against the same frame
// rendered with the cel suppressed, the mean |dRGB| across the boundary drops
// from 77.5 to 26.7 when the frame is clipped to the rect instead of drawn whole.
void ConversationCel::drawNancy16Layer(RenderedCel &obj, const Graphics::Surface &frame, const Common::Rect &dest) {
	Common::Rect src(frame.w, frame.h);
	if (!dest.isEmpty()) {
		src.right = MIN<int16>(src.right, dest.width());
		src.bottom = MIN<int16>(src.bottom, dest.height());
	}

	obj._drawSurface.free();
	obj._drawSurface.copyFrom(frame.getSubArea(src));

	Common::Rect moved(src.width(), src.height());
	moved.moveTo(dest.left, dest.top);
	obj.moveTo(moved);

	// Deliberately no setTransparent(): these frames are RGBA and the blit
	// already honours the alpha channel (ManagedSurface::blitFrom ->
	// blitFromInner skips a == 0 and blends 0 < a < 255). Setting a transparent
	// *colour* on top of that only adds an RGB colour key that would punch holes
	// in the character wherever it happened to match, and nancy18's BSUM names
	// 000000 as its transparent colour.
	obj.setVisible(true);
}

// Which body frame goes with head frame `headFrame`, per the XSheet entry table.
int ConversationCel::nancy16BodyFrameFor(uint headFrame) const {
	const int frameCount = _nancy16BodyVideo.getFrameCount();
	int bodyFrame;

	if (headFrame < _nancy16BodyFrames.size()) {
		bodyFrame = _nancy16BodyFrames[headFrame];
	} else if (!_nancy16BodyFrames.empty()) {
		// 43 of the 255 real sheets are off by one against their head video's
		// frame count (23 short, 20 long); hold the last entry rather than
		// dropping the body for that frame.
		bodyFrame = _nancy16BodyFrames.back();
	} else {
		// No usable entry table: step the body along with the head, clamped.
		bodyFrame = (int)headFrame;
	}

	return CLIP<int>(bodyFrame, 0, frameCount - 1);
}

void ConversationCel::updateGraphics() {
	uint32 currentTime = g_nancy->getTotalPlayTime();

	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		if (!_nancy16HasVideo || _celRObjects.empty() || _state != kRun) {
			return;
		}

		if (currentTime <= _nextFrameTime || _curFrame > _lastFrame) {
			return;
		}

		const Graphics::Surface *frame = _nancy16Video.decodeNextFrame((int)_curFrame);
		if (frame) {
			drawNancy16Layer(_celRObjects[kNancy16HeadCel], *frame, _nancy16Dest);
		}

		// The body layer, beneath the head. Long stretches of the entry table
		// hold one body frame, so only decode when the index actually moves -
		// otherwise every hold would make the Bink decoder re-seek.
		if (_nancy16HasBody && _celRObjects.size() > kNancy16BodyCel) {
			const int bodyFrame = nancy16BodyFrameFor(_curFrame);
			if (bodyFrame != _nancy16BodyCurFrame) {
				const Graphics::Surface *bodySurf = _nancy16BodyVideo.decodeNextFrame(bodyFrame);
				if (bodySurf) {
					drawNancy16Layer(_celRObjects[kNancy16BodyCel], *bodySurf, _nancy16BodyDest);
					_nancy16BodyCurFrame = bodyFrame;
				}
			}
		}

		_nextFrameTime = currentTime + _frameTime;
		++_curFrame;
		return;
	}

	if (_state == kRun && currentTime > _nextFrameTime && _curFrame < MIN<uint>(_lastFrame + 1, _celNames[0].size())) {
		for (uint i = 0; i < _celRObjects.size(); ++i) {
			Cel &cel = loadCel(_celNames[i][_curFrame], _treeNames[i]);
			if (_overrideTreeRects[i] == kCelOverrideTreeRectsOn) {
				_celRObjects[i]._drawSurface.create(cel.surf, _overrideRectSrcs[i]);
				_celRObjects[i].moveTo(_overrideRectDests[i]);
			} else {
				_celRObjects[i]._drawSurface.create(cel.surf, cel.src);
				_celRObjects[i].moveTo(cel.dest);
			}
		}

		_nextFrameTime += _frameTime;
		++_curFrame;
	}
}

void ConversationCel::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		// Same record shape as the sound-only variant; the cels come from an
		// XSheet named by the record's first name rather than from the record.
		readDataNancy16(stream);

		if (!_sound.name.empty()) {
			readXSheetNancy16(_sound.name);
		}

		_drawingOrder = { 1, 0, 2, 3 };
		_overrideTreeRects.resize(4, kCelOverrideTreeRectsOff);
		_firstFrame = 0;
		_lastFrame = 0;
		return;
	}

	if (g_nancy->getGameType() >= kGameTypeNancy13) {
		readDataNancy13(stream);
		return;
	}

	Common::String xsheetName;
	readFilename(stream, xsheetName);

	readFilenameArray(stream, _treeNames, 4);
	readXSheet(stream, xsheetName);

	// Continue reading the AR stream

	// Something related to quality
	stream.skip(3);

	_firstFrame = stream.readUint16LE();
	_lastFrame = stream.readUint16LE();

	stream.skip(6);

	_drawingOrder.resize(4);
	for (uint i = 0; i < 4; ++i) {
		_drawingOrder[i] = stream.readByte();
	}

	_overrideTreeRects.resize(4);
	for (uint i = 0; i < 4; ++i) {
		_overrideTreeRects[i] = stream.readByte();
	}

	readRectArray(stream, _overrideRectSrcs, 4);
	readRectArray(stream, _overrideRectDests, 4);

	ConversationSound::readData(stream);
}

// The sound name (read by the base) is also the XSheet name. first/last frame
// are int32s with -1 sentinels.
void ConversationCel::readCelDataNancy13(Common::SeekableReadStream &stream) {
	readXSheet(stream, _sound.name);	// also fills _treeNames from the XSheet header

	_drawingOrder = { 1, 0, 2, 3 };
	_overrideTreeRects.resize(4, kCelOverrideTreeRectsOff);

	const int32 firstFrame = stream.readSint32LE();
	const int32 lastFrame = stream.readSint32LE();
	const uint numFrames = (!_celNames.empty() && !_celNames[0].empty()) ? _celNames[0].size() : 0;
	_firstFrame = firstFrame < 0 ? 0 : (uint16)firstFrame;
	_lastFrame = lastFrame < 0 ? (numFrames ? (uint16)(numFrames - 1) : 0) : (uint16)lastFrame;
}

// Nancy16's XSheet, verified against all 258 in the game:
//
//   [22]  "XSHEET HerInteractive\0"
//   0x1e  uint32 version, always 2
//   0x22  uint16 entryCount
//   0x24  uint16 layerCount, always 2
//   0x26  layerCount x char[33] layerName
//   170   RECT layer 0 destination      body
//   186   RECT layer 1 destination      head, named after the sheet itself
//   238   entryCount x 24-byte entry    int32 layer0 frame, int32 layer1 frame,
//                                       then 16 zero bytes
//
// len(payload) == 238 + entryCount * 24 for all 258 sheets, exactly.
//
// Both layers are real and both ship as videos. Layer 1 is the head, named
// after the sheet and played one frame per entry. Layer 0 is the body: one long
// video per character (COLINBODY -> ColinBody.bik and so on), shared by every
// sheet that character owns, with the sheet's entry table saying which body
// frame goes with each head frame.
//
// That the layer-0 index is a body *frame* index is not a guess:
//  * across all 258 sheets the largest layer-0 index is exactly frameCount - 1
//    for each of the three body videos (Colin 168/169, Helena 173/174,
//    Margherita 540/541), and no sheet ever over-indexes;
//  * the sequences read as gestures - a hold on the video's last frame (the
//    rest pose), then a contiguous run, then back to rest. CBP00A is
//    168 x6, 1..10, 7 x9, 22..32, 104 x12, 110..113, 142..147.
// The layer-1 index is -1 in every sheet with real videos, i.e. "no override,
// play sequentially"; it is only ever set in the three all-zero STUB_* sheets,
// which name CHARLEENABODY/CHARLEENAHEAD and have no videos at all.
//
// The destination rects are the true content size; the videos are Bink-padded
// up to a multiple of 16 in both axes. That holds for all six videos - Colin
// head 106x93 -> 112x96 and body 179x180 -> 192x192, Helena 108x132 -> 112x144
// and 80x87 -> 80x96, Margherita 108x108 -> 112x112 and 107x94 -> 112x96. The
// padding is what produces the seam, so both layers are clipped to their rect.
void ConversationCel::readXSheetNancy16(const Common::String &xsheetName) {
	Common::SeekableReadStream *xsheet = SearchMan.createReadStreamForMember(Common::Path(xsheetName));
	if (!xsheet) {
		return;
	}

	Common::String signature = xsheet->readString('\0', 22);
	if (signature != "XSHEET HerInteractive") {
		warning("XSHEET '%s' signature doesn't match!", xsheetName.c_str());
		delete xsheet;
		return;
	}

	xsheet->seek(0x22);
	const uint16 entryCount = xsheet->readUint16LE();
	const uint16 layerCount = xsheet->readUint16LE();

	if (layerCount < 2) {
		delete xsheet;
		return;
	}

	// 0x26: the layer names. Only layer 0's is needed - layer 1's is the sheet's
	// own name, which the caller already has.
	const uint kLayerNameSize = 33;
	char nameBuf[kLayerNameSize];
	xsheet->read(nameBuf, kLayerNameSize);
	nameBuf[kLayerNameSize - 1] = '\0';
	_nancy16BodyName = nameBuf;

	// 170 and 186, contiguous.
	xsheet->seek(170);
	readRect(*xsheet, _nancy16BodyDest);
	readRect(*xsheet, _nancy16Dest);

	// 238: the entry table. Keep only the layer-0 (body) frame index; see above
	// for why the layer-1 one is not worth carrying.
	const uint kEntryStart = 238;
	const uint kEntrySize = 24;
	if (entryCount && xsheet->size() >= (int64)(kEntryStart + (uint)entryCount * kEntrySize)) {
		xsheet->seek(kEntryStart);
		_nancy16BodyFrames.resize(entryCount);
		for (uint16 i = 0; i < entryCount; ++i) {
			_nancy16BodyFrames[i] = xsheet->readSint32LE();
			xsheet->skip(kEntrySize - 4);
		}

		if (xsheet->err()) {
			_nancy16BodyFrames.clear();
		}
	}

	delete xsheet;
}

void ConversationCel::readXSheet(Common::SeekableReadStream &stream, const Common::String &xsheetName) {
	Common::SeekableReadStream *xsheet = SearchMan.createReadStreamForMember(Common::Path(xsheetName));
	if (!xsheet) {
		// Not every conversation record names an XSheet that ships with the game.
		// Leaving _celNames empty is the "no cels" state the callers below check
		// for, so bail out rather than dereferencing a null stream.
		warning("Could not open XSheet '%s'", xsheetName.c_str());
		return;
	}

	const bool isNancy13 = g_nancy->getGameType() >= kGameTypeNancy13;
	const uint kNameSize = 33;
	const uint kFrameRecordSize = 140;

	// Read the xsheet and load all images into the arrays
	// Completely unoptimized, the original engine uses a buffer
	xsheet->seek(0);
	const uint16 sigLength = g_nancy->getGameType() <= kGameTypeNancy11 ? 18 : 22;
	Common::String signature = xsheet->readString('\0', sigLength);
	if (signature != "XSHEET WayneSikes" && signature != "XSHEET HerInteractive") {
		warning("XSHEET '%s' signature doesn't match!", xsheetName.c_str());
		delete xsheet;
		return;
	}

	xsheet->seek(0x22);
	const uint16 numFrames = xsheet->readUint16LE();
	uint16 numTrees = 4;

	if (isNancy13) {
		// Nancy13 moved the cel tree / .cal names into the XSheet header, which
		// reserves a fixed kMaxTrees name slots followed by a 32-bit frame time;
		// the frame data begins right after. Each frame is likewise a fixed-size
		// block of kMaxTrees name slots. Earlier games keep the tree names in the
		// AR data and use a 16-bit frame time.
		const uint kMaxTrees = 4;

		numTrees = xsheet->readUint16LE();

		_treeNames.resize(numTrees);
		for (uint j = 0; j < numTrees; ++j) {
			readFilename(*xsheet, _treeNames[j]);
		}

		// Skip any unused tree-name slots so the frame time is read from its fixed
		// offset regardless of numTrees.
		xsheet->skip((kMaxTrees - numTrees) * kNameSize);
		_frameTime = xsheet->readUint32LE();
	} else {
		xsheet->skip(2);
		_frameTime = xsheet->readUint16LE();
		xsheet->skip(2);
	}

	_celNames.resize(numTrees, Common::Array<Common::Path>(numFrames));
	for (uint i = 0; i < numFrames; ++i) {
		for (uint j = 0; j < numTrees; ++j) {
			readFilename(*xsheet, _celNames[j][i]);
		}
		xsheet->skip(kFrameRecordSize - numTrees * kNameSize);
	}

	delete xsheet;
}

bool ConversationCel::isVideoDonePlaying() {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		return !_nancy16HasVideo || _curFrame > _lastFrame;
	}

	// An XSheet that failed to load leaves no cels; such a record has no video to
	// wait on, so treat it as already finished.
	if (_celNames.empty()) {
		return true;
	}

	return _curFrame >= MIN<uint>(_lastFrame, _celNames[0].size()) && _nextFrameTime <= g_nancy->getTotalPlayTime();
}

ConversationCel::Cel &ConversationCel::loadCel(const Common::Path &name, const Common::String &treeName) {
	// Assumes head and body cels will be named differently
	if (_celCache.contains(name)) {
		return _celCache[name];
	}

	Cel &newCel = _celCache.getOrCreateVal(name);
	g_nancy->_resource->loadImage(name, newCel.surf, treeName, &newCel.src, &newCel.dest);
	return _celCache[name];
}

bool ConversationCel::load() {
	if (g_nancy->getGameType() >= kGameTypeNancy16 || _celNames.empty()) {
		return true;
	}

	for (uint i = _curFrame; i < _celNames[0].size(); ++i) {
		for (uint j = 0; j < _celRObjects.size(); ++j) {
			if (!_celCache.contains(_celNames[j][i])) {
				loadCel(_celNames[j][i], _treeNames[j]);
				return false;
			}
		}
	}

	return true;
}

void ConversationSoundTerse::readData(Common::SeekableReadStream &stream) {
	readTerseData(stream);
}

void ConversationCelTerse::readData(Common::SeekableReadStream &stream) {
	Common::String xsheetName;
	readFilename(stream, xsheetName);

	readFilenameArray(stream, _treeNames, 2); // Only 2
	readXSheet(stream, xsheetName);

	_lastFrame = stream.readUint16LE();
	_drawingOrder = { 1, 0, 2, 3 };
	_overrideTreeRects.resize(4, kCelOverrideTreeRectsOff);

	readTerseData(stream);

	// WORKAROUND: Fix the last frame for some videos, to prevent them from
	// running for too long, if the associated sound file is shorter than
	// the video
	if (g_nancy->getGameType() == kGameTypeNancy9 && xsheetName == "KFB28" && _lastFrame == 102 && _sound.name == "KFF28") {
		// Offerring to call the Sheriff for Katie - bug #16753
		_lastFrame = 70;
	}  else if (g_nancy->getGameType() == kGameTypeNancy9 && xsheetName == "StubAndy" && _lastFrame == 344 && _sound.name == "ACC03") {
		// Asking Andy for a whale watching keychain design - bug #16786
		_lastFrame = 30;
	}
}

} // End of namespace Action
} // End of namespace Nancy
