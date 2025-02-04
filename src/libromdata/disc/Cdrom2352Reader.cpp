/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * Cdrom2352Reader.hpp: CD-ROM reader for 2352-byte sector images.         *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

/**
 * References:
 * - https://github.com/qeedquan/ecm/blob/master/format.txt
 * - https://github.com/Karlson2k/libcdio-k2k/blob/master/include/cdio/sector.h
 */

#include "Cdrom2352Reader.hpp"
#include "librpbase/disc/SparseDiscReader_p.hpp"
#include "../cdrom_structs.h"

// librpbase
#include "librpbase/byteswap.h"
#include "librpbase/file/IRpFile.hpp"
using namespace LibRpBase;

// C includes.
#include <stdlib.h>

// C includes. (C++ namespace)
#include <cassert>
#include <cerrno>
#include <cstring>

namespace LibRomData {

class Cdrom2352ReaderPrivate : public SparseDiscReaderPrivate {
	public:
		Cdrom2352ReaderPrivate(Cdrom2352Reader *q);

	private:
		typedef SparseDiscReaderPrivate super;
		RP_DISABLE_COPY(Cdrom2352ReaderPrivate)

	public:
		// CD-ROM sync magic.
		static const uint8_t CDROM_2352_MAGIC[12];

		// Physical block size.
		static const unsigned int physBlockSize = 2352;

		// Number of 2352-byte blocks.
		unsigned int blockCount;
};

/** Cdrom2352ReaderPrivate **/

// CD-ROM sync magic magic.
const uint8_t Cdrom2352ReaderPrivate::CDROM_2352_MAGIC[12] =
	{0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00};

Cdrom2352ReaderPrivate::Cdrom2352ReaderPrivate(Cdrom2352Reader *q)
	: super(q)
	, blockCount(0)
{ }

/** Cdrom2352Reader **/

Cdrom2352Reader::Cdrom2352Reader(IRpFile *file)
	: super(new Cdrom2352ReaderPrivate(this), file)
{
	if (!m_file) {
		// File could not be ref()'d.
		return;
	}

	// Check the disc size.
	// Should be a multiple of 2352.
	int64_t fileSize = m_file->size();
	if (fileSize <= 0 || fileSize % 2352 != 0) {
		// Invalid disc size.
		m_file->unref();
		m_file = nullptr;
		m_lastError = EIO;
		return;
	}

	// Disc parameters.
	// TODO: 64-bit block count?
	RP_D(Cdrom2352Reader);
	d->blockCount = static_cast<unsigned int>(fileSize / 2352);
	d->block_size = 2048;
	d->disc_size = fileSize / 2352 * 2048;

	// Reset the disc position.
	d->pos = 0;
}

/**
 * Is a disc image supported by this class?
 * @param pHeader Disc image header.
 * @param szHeader Size of header.
 * @return Class-specific disc format ID (>= 0) if supported; -1 if not.
 */
int Cdrom2352Reader::isDiscSupported_static(const uint8_t *pHeader, size_t szHeader)
{
	if (szHeader < 2352) {
		// Not enough data to check.
		return -1;
	}

	// Check the CD-ROM sync magic.
	if (!memcmp(pHeader, Cdrom2352ReaderPrivate::CDROM_2352_MAGIC,
	     sizeof(Cdrom2352ReaderPrivate::CDROM_2352_MAGIC)))
	{
		// Valid CD-ROM sync magic.
		return 0;
	}

	// Not supported.
	return -1;
}

/**
 * Is a disc image supported by this object?
 * @param pHeader Disc image header.
 * @param szHeader Size of header.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int Cdrom2352Reader::isDiscSupported(const uint8_t *pHeader, size_t szHeader) const
{
	return isDiscSupported_static(pHeader, szHeader);
}

/** SparseDiscReader functions. **/

/**
 * Get the physical address of the specified logical block index.
 *
 * @param blockIdx	[in] Block index.
 * @return Physical address. (0 == empty block; -1 == invalid block index)
 */
int64_t Cdrom2352Reader::getPhysBlockAddr(uint32_t blockIdx) const
{
	// Make sure the block index is in range.
	RP_D(Cdrom2352Reader);
	assert(blockIdx < d->blockCount);
	if (blockIdx >= d->blockCount) {
		// Out of range.
		return -1;
	}

	// Convert to a physical block address and return.
	// FIXME: Currently only supports Mode 1.
	// Check for Mode 2 and handle it correctly.
	return (static_cast<int64_t>(blockIdx) * d->physBlockSize) + 16;
}

}
