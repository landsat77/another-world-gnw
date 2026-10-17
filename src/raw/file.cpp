/* Raw - Another World Interpreter
 * Copyright (C) 2004 Gregory Montoir
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef GNW
#include "zlib.h"
#endif
#include "file.h"


struct File_impl {
	bool _ioErr;
	File_impl() : _ioErr(false) {}
  virtual ~File_impl() {}
	virtual bool open(const char *path, const char *mode) = 0;
	virtual void close() = 0;
	virtual void seek(int32_t off) = 0;
	virtual void read(void *ptr, uint32_t size) = 0;
	virtual void write(void *ptr, uint32_t size) = 0;
};

struct stdFile : File_impl {
	FILE *_fp;
	stdFile() : _fp(0) {}
	bool open(const char *path, const char *mode) {
		_ioErr = false;
		_fp = fopen(path, mode);
		return (_fp != NULL);
	}
	void close() {
		if (_fp) {
			fclose(_fp);
			_fp = 0;
		}
	}
	void seek(int32_t off) {
		if (_fp) {
			fseek(_fp, off, SEEK_SET);
		}
	}
	void read(void *ptr, uint32_t size) {
		if (_fp) {
			uint32_t r = fread(ptr, 1, size, _fp);
			if (r != size) {
				_ioErr = true;
			}
		}
	}
	void write(void *ptr, uint32_t size) {
		if (_fp) {
			uint32_t r = fwrite(ptr, 1, size, _fp);
			if (r != size) {
				_ioErr = true;
			}
		}
	}
};

#ifdef GNW
/* GNW: savestates only (the game data is in the asset pack). The whole file
 * is kept in memory and read / written with a single call: the firmware's
 * file API is slow for the many small reads and writes of the serializer.
 * No compression. */
struct bufFile : File_impl {
	FILE *_fp;
	bool _write;
	uint8_t *_buf;
	uint32_t _size, _cap, _pos;
	bufFile() : _fp(0), _write(false), _buf(0), _size(0), _cap(0), _pos(0) {}
	bool open(const char *path, const char *mode) {
		close();
		_ioErr = false;
		_write = (mode[0] == 'w');
		_fp = fopen(path, _write ? "wb" : "rb");
		if (!_fp)
			return false;
		if (!_write) {
			fseek(_fp, 0, SEEK_END);
			long sz = ftell(_fp);
			fseek(_fp, 0, SEEK_SET);
			if (sz < 0 || sz > 1024 * 1024) {
				fclose(_fp);
				_fp = 0;
				return false;
			}
			_buf = (uint8_t *)malloc(sz ? sz : 1);
			if (!_buf || fread(_buf, 1, sz, _fp) != (size_t)sz)
				_ioErr = true;
			_size = _buf ? sz : 0;
			fclose(_fp);
			_fp = 0;
		}
		_pos = 0;
		return true;
	}
	void close() {
		if (_write && _fp) {
			if (_size && fwrite(_buf, 1, _size, _fp) != _size)
				_ioErr = true;
			fclose(_fp);
		} else if (_fp) {
			fclose(_fp);
		}
		_fp = 0;
		free(_buf);
		_buf = 0;
		_size = _cap = _pos = 0;
		_write = false;
	}
	void seek(int32_t off) {
		_pos = off < 0 ? 0 : off;
	}
	void read(void *ptr, uint32_t size) {
		if (_pos + size > _size) {
			memset(ptr, 0, size);
			_ioErr = true;
			return;
		}
		memcpy(ptr, _buf + _pos, size);
		_pos += size;
	}
	void write(void *ptr, uint32_t size) {
		if (!_write) {
			_ioErr = true;
			return;
		}
		if (_pos + size > _cap) {
			uint32_t cap = _cap ? _cap : 132 * 1024; // a savestate: ~127 KB
			while (cap < _pos + size)
				cap *= 2;
			uint8_t *n = (uint8_t *)realloc(_buf, cap);
			if (!n) {
				_ioErr = true;
				return;
			}
			_buf = n;
			_cap = cap;
		}
		memcpy(_buf + _pos, ptr, size);
		_pos += size;
		if (_pos > _size)
			_size = _pos;
	}
};

// operator new / delete of the SDK are a bump allocator without free():
// use the game heap.
File::File(bool gzipped) {
	(void)gzipped;
	void *mem = malloc(sizeof(bufFile));
	if (!mem)
		error("File: out of memory");
	_impl = new (mem) bufFile;
}

File::~File() {
	_impl->close();
	_impl->~File_impl();
	free(_impl);
}

bool File::openPath(const char *path, const char *mode) {
	return _impl->open(path, mode);
}

/* The flush happens in close(): report its I/O errors */
bool File::closeAndCheck() {
	_impl->close();
	return !_impl->_ioErr;
}
#else
struct zlibFile : File_impl {
	gzFile _fp;
	zlibFile() : _fp(0) {}
	bool open(const char *path, const char *mode) {
		_ioErr = false;
		_fp = gzopen(path, mode);
		return (_fp != NULL);
	}
	void close() {
		if (_fp) {
			gzclose(_fp);
			_fp = 0;
		}
	}
	void seek(int32_t off) {
		if (_fp) {
			gzseek(_fp, off, SEEK_SET);
		}
	}
	void read(void *ptr, uint32_t size) {
		if (_fp) {
			uint32_t r = gzread(_fp, ptr, size);
			if (r != size) {
				_ioErr = true;
			}
		}
	}
	void write(void *ptr, uint32_t size) {
		if (_fp) {
			uint32_t r = gzwrite(_fp, ptr, size);
			if (r != size) {
				_ioErr = true;
			}
		}
	}
};

File::File(bool gzipped) {
	if (gzipped) {
		_impl = new zlibFile;
	} else {
		_impl = new stdFile;
	}
}
#endif

#ifndef GNW
File::~File() {
	_impl->close();
	delete _impl;
}
#endif

bool File::open(const char *filename, const char *directory, const char *mode) {	
	_impl->close();
	char buf[512];
	sprintf(buf, "%s/%s", directory, filename);
	char *p = buf + strlen(directory) + 1;
	string_lower(p);
	bool opened = _impl->open(buf, mode);
	if (!opened) { // let's try uppercase
		string_upper(p);
		opened = _impl->open(buf, mode);
	}
	return opened;
}

void File::close() {
	_impl->close();
}

bool File::ioErr() const {
	return _impl->_ioErr;
}

void File::seek(int32_t off) {
	_impl->seek(off);
}

void File::read(void *ptr, uint32_t size) {
	_impl->read(ptr, size);
}

uint8_t File::readByte() {
	uint8_t b;
	read(&b, 1);
	return b;
}

uint16_t File::readUint16BE() {
	uint8_t hi = readByte();
	uint8_t lo = readByte();
	return (hi << 8) | lo;
}

uint32_t File::readUint32BE() {
	uint16_t hi = readUint16BE();
	uint16_t lo = readUint16BE();
	return (hi << 16) | lo;
}

void File::write(void *ptr, uint32_t size) {
	_impl->write(ptr, size);
}

void File::writeByte(uint8_t b) {
	write(&b, 1);
}

void File::writeUint16BE(uint16_t n) {
	writeByte(n >> 8);
	writeByte(n & 0xFF);
}

void File::writeUint32BE(uint32_t n) {
	writeUint16BE(n >> 16);
	writeUint16BE(n & 0xFFFF);
}
