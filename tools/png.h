/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Minimal PNG writer: 8 bit RGB, no filtering, uncompressed deflate blocks.
// Written here so the tools have no dependency at all.


#ifndef ARKANOID_PNG_H
#define ARKANOID_PNG_H

#include <stdint.h>
#include <stdio.h>
#include <vector>

namespace png {


inline uint32_t
Crc32Update(uint32_t crc, const uint8_t* data, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			if ((crc & 1) != 0)
				crc = (crc >> 1) ^ 0xedb88320u;
			else
				crc >>= 1;
		}
	}
	return crc;
}


inline uint32_t
Adler32(const uint8_t* data, size_t length)
{
	uint32_t a = 1;
	uint32_t b = 0;
	for (size_t i = 0; i < length; i++) {
		a = (a + data[i]) % 65521;
		b = (b + a) % 65521;
	}
	return (b << 16) | a;
}


inline void
WriteU32(FILE* file, uint32_t value)
{
	fputc((value >> 24) & 0xff, file);
	fputc((value >> 16) & 0xff, file);
	fputc((value >> 8) & 0xff, file);
	fputc(value & 0xff, file);
}


inline void
WriteChunk(FILE* file, const char* type, const uint8_t* data, uint32_t length)
{
	WriteU32(file, length);
	fwrite(type, 1, 4, file);
	if (length != 0)
		fwrite(data, 1, length, file);
	uint32_t crc = Crc32Update(0xffffffffu, (const uint8_t*)type, 4);
	crc = Crc32Update(crc, data, length);
	WriteU32(file, ~crc);
}


// Writes 'width' x 'height' pixels, 3 bytes per pixel, top row first.
inline bool
WritePng(const char* path, const uint8_t* rgb, int width, int height)
{
	if (width <= 0 || height <= 0)
		return false;

	std::vector<uint8_t> raw;
	raw.reserve(static_cast<size_t>(height) * (1 + width * 3));
	for (int y = 0; y < height; y++) {
		raw.push_back(0);	// filter: none
		size_t offset = static_cast<size_t>(y) * width * 3;
		raw.insert(raw.end(), rgb + offset, rgb + offset + width * 3);
	}

	// The zlib stream: header, stored deflate blocks, Adler-32.
	std::vector<uint8_t> zlib;
	zlib.push_back(0x78);
	zlib.push_back(0x01);
	size_t position = 0;
	while (position < raw.size()) {
		size_t count = raw.size() - position;
		if (count > 65535)
			count = 65535;
		bool last = position + count == raw.size();
		zlib.push_back(last ? 1 : 0);
		zlib.push_back(count & 0xff);
		zlib.push_back((count >> 8) & 0xff);
		uint16_t inverse = static_cast<uint16_t>(~count);
		zlib.push_back(inverse & 0xff);
		zlib.push_back((inverse >> 8) & 0xff);
		zlib.insert(zlib.end(), raw.begin() + position,
			raw.begin() + position + count);
		position += count;
	}
	uint32_t adler = Adler32(raw.data(), raw.size());
	zlib.push_back((adler >> 24) & 0xff);
	zlib.push_back((adler >> 16) & 0xff);
	zlib.push_back((adler >> 8) & 0xff);
	zlib.push_back(adler & 0xff);

	FILE* file = fopen(path, "wb");
	if (file == NULL)
		return false;
	static const uint8_t kSignature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a,
		0x1a, 0x0a };
	fwrite(kSignature, 1, sizeof(kSignature), file);

	uint8_t header[13];
	header[0] = (width >> 24) & 0xff;
	header[1] = (width >> 16) & 0xff;
	header[2] = (width >> 8) & 0xff;
	header[3] = width & 0xff;
	header[4] = (height >> 24) & 0xff;
	header[5] = (height >> 16) & 0xff;
	header[6] = (height >> 8) & 0xff;
	header[7] = height & 0xff;
	header[8] = 8;		// bit depth
	header[9] = 2;		// colour type: RGB
	header[10] = 0;		// compression
	header[11] = 0;		// filter
	header[12] = 0;		// interlace
	WriteChunk(file, "IHDR", header, sizeof(header));
	WriteChunk(file, "IDAT", zlib.data(), static_cast<uint32_t>(zlib.size()));
	WriteChunk(file, "IEND", NULL, 0);
	fclose(file);
	return true;
}


}	// namespace png


#endif	// ARKANOID_PNG_H
