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

#include "engines/nancy/cif.h"
#include "engines/nancy/decompress.h"
#include "engines/nancy/util.h"
#include "engines/nancy/nancy.h"

#include "common/memstream.h"
#include "common/substream.h"
#include "common/serializer.h"
#include "common/config-manager.h"

namespace Nancy {

// Reads the data common to standalone .cif files and the ones embedded in a ciftree
static void syncCifInfo(Common::Serializer &ser, CifInfo &info, bool tree) {
	// gross switch of what "version means"
	uint ver = ser.getVersion();
	ser.setVersion(g_nancy->getGameType());
	readRect(ser, info.src, kGameTypeNancy2);
	readRect(ser, info.dest, kGameTypeNancy2);
	ser.setVersion(ver);

	ser.syncAsUint16LE(info.width);
	ser.syncAsUint16LE(info.pitch);
	ser.syncAsUint16LE(info.height);
	ser.syncAsByte(info.depth);

	ser.syncAsByte(info.comp);

	if (tree) {
		ser.syncAsUint32LE(info.dataOffset, 0, 1);
	}

	ser.syncAsUint32LE(info.size);
	ser.skip(4); // A 2nd size for obsolete Cif type 1
	ser.syncAsUint32LE(info.compressedSize);

	ser.syncAsByte(info.type);

	if (!tree) {
		info.dataOffset = ser.bytesSynced();
	}

	// From Nancy4 on, the original decides compression from the resource type
	// (image and script resources are always LZSS-compressed) and ignores the
	// 'comp' byte, which isn't reliably written in the later games. Only Nancy2
	// and Nancy3 actually key off the 'comp' byte read above.
	if (g_nancy->getGameType() >= kGameTypeNancy4)
		info.comp = (info.type == CifInfo::kResTypeImage || info.type == CifInfo::kResTypeScript) ?
			CifInfo::kResCompression : CifInfo::kResCompressionNone;
}

// Reads the data for ciftree cif files
static void syncCiftreeInfo(Common::Serializer &ser, CifInfo &info) {
	uint nameSize = g_nancy->getGameType() <= kGameTypeNancy2 ? 9 : 33;
	byte name[34];
	if (ser.isSaving()) {
		memcpy(name, info.name.toString('/').c_str(), nameSize);
		name[nameSize] = 0;
	}

	ser.syncBytes(name, nameSize);
	name[nameSize] = 0;
	info.name = (char *)name;

	ser.skip(2); // Index of this block

	ser.syncAsUint32LE(info.dataOffset, 2);
	ser.skip(2, 2); // Next id in chain

	syncCifInfo(ser, info, true);

	ser.skip(2, 0, 1); // Next id in chain
}

enum {
	kHashMapSize = 1024
};

CifFile::CifFile(Common::SeekableReadStream *stream, const Common::Path &name) {
	assert(stream);
	_stream = stream;

	_info.name = name;
	Common::Serializer ser(stream, nullptr);
	if (!sync(ser)) {
		return;
	}
}

CifFile::~CifFile() {
	delete _stream;
}

Common::SeekableReadStream *CifFile::createReadStream() const {
	byte *buf = (byte *)malloc(_info.size);

	bool success = true;

	if (_info.comp == CifInfo::kResCompression) {
		// Decompress the data into the buffer
		if (_stream->seek(_info.dataOffset)) {
			Common::MemoryWriteStream write(buf, _info.size);
			Common::SeekableSubReadStream read(_stream, _info.dataOffset, _info.dataOffset + _info.compressedSize);
			Decompressor dec;
			success = dec.decompress(read, write);
		} else {
			success = false;
		}
	} else {
		if (!_stream->seek(_info.dataOffset) || _stream->read(buf, _info.size) < _info.size) {
			success = false;
		}
	}

	if (!success) {
		warning("Failed to read data for CifFile '%s'", _info.name.toString().c_str());
		free(buf);
		buf = nullptr;
		_stream->clearErr();
		return nullptr;
	}

	return new Common::MemoryReadStream(buf, _info.size, DisposeAfterUse::YES);
}

Common::SeekableReadStream *CifFile::createReadStreamRaw() const {
	uint size = (_info.comp == CifInfo::kResCompression ? _info.compressedSize : _info.size);
	byte *buf = new byte[size];

	if (!_stream->seek(_info.dataOffset) || _stream->read(buf, size) < size) {
		warning("Failed to read data for CifFile '%s'", _info.name.toString().c_str());
	}

	return new Common::MemoryReadStream(buf, size, DisposeAfterUse::YES);
}

bool CifFile::sync(Common::Serializer &ser) {
	if (g_nancy->getGameType() <= kGameTypeNancy11 && ser.matchBytes("CIF FILE WayneSikes", 20)) {
		ser.skip(4);	// 4 bytes unused
	} else if (g_nancy->getGameType() >= kGameTypeNancy12 && ser.matchBytes("CIF FILE HerInteractive", 24)) {
		// Nancy 12+
	} else {
		warning("Invalid id string found in CifFile '%s'", _info.name.toString().c_str());
		return false;
	}

	// Version high bytes. These do not change
	uint16 hi = 2;
	ser.syncAsUint16LE(hi);

	uint32 ver = (g_nancy->getGameType() <= kGameTypeNancy1) ? 0 : 1;
	ser.syncAsUint16LE(ver);

	if (ver != 0 && ver != 1 && ver != 2) {
		warning("Unsupported version %d found in CifFile '%s'", ver, _info.name.toString().c_str());
		return false;
	}

	if (g_nancy->getGameType() >= kGameTypeNancy6) {
		++ver; // nancy6 made changes to the CifTree structure, but didn't bump the file version
	}

	ser.setVersion(ver);

	syncCifInfo(ser, _info, false);
	return true;
}

CifTree::CifTree(Common::SeekableReadStream *stream, const Common::Path &name) :
		_stream(stream),
		_name(name) {}

CifTree::~CifTree() {
	delete _stream;
}

const CifInfo &CifTree::getCifInfo(const Common::Path &name) const {
	return _fileMap[name];
}

bool CifTree::hasFile(const Common::Path &path) const {
	return _fileMap.contains(path);
}

int CifTree::listMembers(Common::ArchiveMemberList &list) const {
	for (auto &f : _fileMap) {
		list.push_back(Common::ArchiveMemberPtr(new Common::GenericArchiveMember(f._key, *this)));
	}

	return list.size();
}

const Common::ArchiveMemberPtr CifTree::getMember(const Common::Path &path) const {
	if (!hasFile(path)) {
		return Common::ArchiveMemberPtr();
	}

	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(path, *this));
}

Common::SeekableReadStream *CifTree::createReadStreamForMember(const Common::Path &path) const {
	if (!hasFile(path)) {
		return nullptr;
	}

	const CifInfo &info = _fileMap[path];
	byte *buf = (byte *)malloc(info.size);

	bool success = true;

	if (info.comp == CifInfo::kResCompression) {
		// Decompress the data into the buffer
		if (_stream->seek(info.dataOffset)) {
			Common::MemoryWriteStream write(buf, info.size);
			Common::SeekableSubReadStream read(_stream, info.dataOffset, info.dataOffset + info.compressedSize);
			Decompressor dec;
			success = dec.decompress(read, write);
		} else {
			success = false;
		}
	} else {
		if (!_stream->seek(info.dataOffset) || _stream->read(buf, info.size) < info.size) {
			success = false;
		}
	}

	if (!success) {
		warning("Failed to read data for '%s' from CifTree '%s'", info.name.toString().c_str(), _name.toString().c_str());
		free(buf);
		buf = nullptr;
		_stream->clearErr();
		return nullptr;
	}

	return new Common::MemoryReadStream(buf, info.size, DisposeAfterUse::YES);
}

Common::SeekableReadStream *CifTree::createReadStreamRaw(const Common::Path &path) const {
	if (!hasFile(path)) {
		return nullptr;
	}

	const CifInfo &info = _fileMap[path];
	uint32 size = (info.comp == CifInfo::kResCompression ? info.compressedSize : info.size);
	byte *buf = new byte[size];

	if (!_stream->seek(info.dataOffset) || _stream->read(buf, size) < size) {
		warning("Failed to read data for '%s' from CifTree '%s'", info.name.toString().c_str(), _name.toString().c_str());
	}

	return new Common::MemoryReadStream(buf, size, DisposeAfterUse::YES);
}

CifTree *CifTree::makeCifTreeArchive(const Common::String &name, const Common::String &ext) {
	Common::Path path(name);
	path.appendInPlace('.' + ext);

	auto *stream = SearchMan.createReadStreamForMember(path);

	if (!stream) {
		return nullptr;
	}

	CifTree *ret = new CifTree(stream, path);

	// Nancy16 introduced a version 3 container tagged "CIF FILE" rather than "CIF TREE".
	// Probe for it up front: Serializer::matchBytes() consumes its bytes even when it
	// fails to match, so by the time sync() has tried the older tags the stream is no
	// longer positioned at the magic.
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		char magic[24];
		if (stream->read(magic, sizeof(magic)) == sizeof(magic) &&
				memcmp(magic, "CIF FILE HerInteractive", sizeof(magic)) == 0) {
			const uint16 hi = stream->readUint16LE();
			stream->readUint16LE(); // Version low bytes

			if (hi != 3 || !ret->syncV3()) {
				warning("Failed to read version %d CifTree '%s'", hi, path.toString().c_str());
				delete ret;
				return nullptr;
			}

			return ret;
		}

		stream->seek(0);
	}

	Common::Serializer ser(stream, nullptr);

	if (!ret->sync(ser)) {
		delete ret;
		return nullptr;
	}

	return ret;
}

bool CifTree::sync(Common::Serializer &ser) {
	if (g_nancy->getGameType() <= kGameTypeNancy11 && ser.matchBytes("CIF TREE WayneSikes", 20)) {
		// Nancy 1-11
		ser.skip(4); // 4 bytes unused
	} else if (g_nancy->getGameType() >= kGameTypeNancy12 && ser.matchBytes("CIF TREE HerInteractive", 24)) {
		// Nancy 12+
	} else {
		warning("Invalid id string found in CifTree '%s'", _name.toString().c_str());
		return false;
	}

	// Version high bytes. These do not change
	uint16 hi = 2;
	ser.syncAsUint16LE(hi);

	uint32 ver = (g_nancy->getGameType() <= kGameTypeNancy1) ? 0 : 1;
	ser.syncAsUint16LE(ver);

	// TODO: Nancy16 introduced version 3
	if (ver != 0 && ver != 1 && ver != 2) {
		warning("Unsupported version %d found in CifTree '%s'", ver, _name.toString().c_str());
		return false;
	}

	if (g_nancy->getGameType() >= kGameTypeNancy6) {
		++ver; // nancy6 made changes to the CifTree structure, but didn't bump the file version
	}

	ser.setVersion(ver);

	uint16 infoBlockCount = _writeFileMap.size();
	ser.syncAsUint16LE(infoBlockCount);
	ser.skip(2, 1);

	// We will be doing our own hashing, so skip the table built into the tree
	ser.skip(kHashMapSize * 2);

	CifInfo info;
	for (int i = 0; i < infoBlockCount; i++) {
		if (ser.isLoading()) {
			syncCiftreeInfo(ser, info);
			if (info.size && info.type != CifInfo::kResTypeEmpty) {
				_fileMap.setVal(info.name, info);
			}
		} else {
			syncCiftreeInfo(ser, _writeFileMap[i]);
		}
	}

	return true;
}

// Layout of the version 3 container introduced in Nancy16:
//
//   [28]  outer header ("CIF FILE HerInteractive\0", uint16 hi = 3, uint16 ver)
//         member records, each:
//           [24]  "CIF FILE HerInteractive\0"
//           [4]   uint16 hi, uint16 ver
//           [20]  uint32 type, width, height, comp, size
//           [size] payload
//   index:
//           [4]   uint32 member count
//           [37 * count]  char name[33], uint32 record offset
//   [4]   uint32 index size (not counting these trailing 4 bytes)
//
// Unlike earlier versions the payload is never run through Decompressor: images are
// stored as PNG and scripts as plain IFF, so `comp` is recorded but the bytes are read
// verbatim.
static const uint32 kV3NameFieldSize = 33;
static const uint32 kV3IndexEntrySize = kV3NameFieldSize + 4;
static const uint32 kV3RecordHeaderSize = 48;

bool CifTree::syncV3() {
	const int64 fileSize = _stream->size();
	if (fileSize < 4) {
		warning("CifTree '%s' is too small to contain a v3 index", _name.toString().c_str());
		return false;
	}

	if (!_stream->seek(fileSize - 4)) {
		return false;
	}

	const uint32 indexSize = _stream->readUint32LE();
	if (indexSize < 4 || (int64)indexSize + 4 > fileSize) {
		warning("CifTree '%s' has an out-of-range v3 index size of %d", _name.toString().c_str(), indexSize);
		return false;
	}

	const int64 indexOffset = fileSize - 4 - indexSize;
	if (!_stream->seek(indexOffset)) {
		return false;
	}

	// Check the entry count by dividing rather than multiplying: kV3IndexEntrySize is
	// odd and so invertible modulo 2^32, which would let a huge count pass a
	// multiply-based check and then be handed to reserve() below.
	if ((indexSize - 4) % kV3IndexEntrySize != 0) {
		warning("CifTree '%s' has a v3 index size of %d, which is not a whole number of entries",
			_name.toString().c_str(), indexSize);
		return false;
	}

	const uint32 numEntries = _stream->readUint32LE();
	if (numEntries != (indexSize - 4) / kV3IndexEntrySize) {
		warning("CifTree '%s' v3 index declares %d entries, which does not match its size of %d",
			_name.toString().c_str(), numEntries, indexSize);
		return false;
	}

	// Read the whole index first; each record header then needs a seek of its own.
	Common::Array<Common::Path> names;
	Common::Array<uint32> offsets;
	names.reserve(numEntries);
	offsets.reserve(numEntries);

	for (uint32 i = 0; i < numEntries; ++i) {
		char nameBuf[kV3NameFieldSize + 1];
		if (_stream->read(nameBuf, kV3NameFieldSize) != kV3NameFieldSize) {
			warning("CifTree '%s' ran out of data while reading its v3 index", _name.toString().c_str());
			return false;
		}

		nameBuf[kV3NameFieldSize] = '\0';
		names.push_back(Common::Path(nameBuf));
		offsets.push_back(_stream->readUint32LE());
	}

	for (uint32 i = 0; i < numEntries; ++i) {
		if ((int64)offsets[i] + kV3RecordHeaderSize > indexOffset || !_stream->seek(offsets[i])) {
			warning("CifTree '%s' member '%s' points outside the archive", _name.toString().c_str(),
				names[i].toString().c_str());
			continue;
		}

		char magic[24];
		if (_stream->read(magic, 24) != 24 || memcmp(magic, "CIF FILE HerInteractive", 24) != 0) {
			warning("CifTree '%s' member '%s' has no record header", _name.toString().c_str(),
				names[i].toString().c_str());
			continue;
		}

		_stream->skip(4); // Per-record version, matches the container's

		// These are 32-bit on disk, but CifInfo stores them narrower, so they need
		// checking instead of truncating
		const uint32 type = _stream->readUint32LE();
		const uint32 width = _stream->readUint32LE();
		const uint32 height = _stream->readUint32LE();

		if (type > 0xff || width > 0xffff || height > 0xffff) {
			warning("CifTree '%s' member '%s' has an out-of-range type %u or size %ux%u",
				_name.toString().c_str(), names[i].toString().c_str(), type, width, height);
			continue;
		}

		CifInfo info;
		info.name = names[i];
		info.type = (CifInfo::ResType)type;
		info.width = (uint16)width;
		info.height = (uint16)height;
		_stream->skip(4); // comp: set on some images, but the payload is stored verbatim regardless
		info.size = _stream->readUint32LE();
		info.comp = CifInfo::kResCompressionNone;
		info.compressedSize = info.size;
		info.pitch = info.width;
		info.dataOffset = offsets[i] + kV3RecordHeaderSize;

		if ((int64)info.dataOffset + info.size > indexOffset) {
			warning("CifTree '%s' member '%s' overruns the archive", _name.toString().c_str(),
				names[i].toString().c_str());
			continue;
		}

		if (info.size && info.type != CifInfo::kResTypeEmpty) {
			_fileMap.setVal(info.name, info);
		}
	}

	debugC(1, kDebugEngine, "Loaded v3 CifTree '%s' with %d members", _name.toString().c_str(), _fileMap.size());
	return true;
}

Common::Array<Common::Path> CifTree::getPathsForType(CifInfo::ResType type) const {
	Common::Array<Common::Path> pathList;

	for (auto &it : _fileMap) {
		if (type == CifInfo::kResTypeAny || it._value.type == type) {
			pathList.push_back(it._key);
		}
	}

	return pathList;
}

bool PatchTree::hasFile(const Common::Path &path) const {
	if (CifTree::hasFile(path)) {
		// An association is just a Pair of two StringArrays
		// The first member is an array of Pairs of Strings: a ConfMan property name, and the required value for that ConfMan property
		// The second member is an array with the names of the files to be enabled if all the ConfMan property requirements are satisfied
		for (auto &assoc : _associations) {
			auto &confManProps = assoc.first;
			auto &filenames = assoc.second;
			for (const Common::Path &s : filenames) {
				if (s == path) {
					bool satisfied = true;

					for (uint i = 0; i < confManProps.size(); ++i) {
						// Check all
						if (ConfMan.get(confManProps[i].first, ConfMan.getActiveDomainName()) != confManProps[i].second) {
							satisfied = false;
							break;
						}
					}

					return satisfied;
				}
			}
		}

		// Files without an associated ConfMan ID are always marked as present
		return true;
	}

	return false;
}

} // End of namespace Nancy
