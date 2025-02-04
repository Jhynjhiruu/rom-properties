/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * Nintendo3DSFirm.hpp: Nintendo 3DS firmware reader.                      *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "Nintendo3DSFirm.hpp"
#include "librpbase/RomData_p.hpp"

#include "n3ds_firm_structs.h"
#include "data/Nintendo3DSFirmData.hpp"

#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"
#include "libi18n/i18n.h"
using namespace LibRpBase;

// C includes. (C++ namespace)
#include "librpbase/ctypex.h"
#include <cassert>
#include <cerrno>
#include <cstring>

// for memmem() if it's not available in <string.h>
#include "librpbase/TextFuncs_libc.h"

// C++ includes.
#include <memory>
#include <string>
#include <vector>
using std::string;
using std::unique_ptr;
using std::vector;

// zlib for crc32()
#include <zlib.h>

namespace LibRomData {

ROMDATA_IMPL(Nintendo3DSFirm)

class Nintendo3DSFirmPrivate : public RomDataPrivate
{
	public:
		Nintendo3DSFirmPrivate(Nintendo3DSFirm *q, IRpFile *file);

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(Nintendo3DSFirmPrivate)

	public:
		// Firmware header.
		// NOTE: Must be byteswapped on access.
		N3DS_FIRM_Header_t firmHeader;
};

/** Nintendo3DSFirmPrivate **/

Nintendo3DSFirmPrivate::Nintendo3DSFirmPrivate(Nintendo3DSFirm *q, IRpFile *file)
	: super(q, file)
{
	// Clear the various structs.
	memset(&firmHeader, 0, sizeof(firmHeader));
}

/** Nintendo3DSFirm **/

/**
 * Read a Nintendo 3DS firmware binary.
 *
 * A ROM image must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the ROM image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open ROM image.
 */
Nintendo3DSFirm::Nintendo3DSFirm(IRpFile *file)
	: super(new Nintendo3DSFirmPrivate(this, file))
{
	RP_D(Nintendo3DSFirm);
	d->className = "Nintendo3DSFirm";
	d->fileType = FTYPE_FIRMWARE_BINARY;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the firmware header.
	d->file->rewind();
	size_t size = d->file->read(&d->firmHeader, sizeof(d->firmHeader));
	if (size != sizeof(d->firmHeader)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this firmware binary is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(d->firmHeader);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->firmHeader);
	info.ext = nullptr;	// Not needed for N3DS FIRM.
	info.szFile = 0;	// Not needed for N3DS FIRM.
	d->isValid = (isRomSupported_static(&info) >= 0);

	if (!d->isValid) {
		d->file->unref();
		d->file = nullptr;
	}
}

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int Nintendo3DSFirm::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(N3DS_FIRM_Header_t))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Check the FIRM magic.
	// TODO: Other checks?
	const N3DS_FIRM_Header_t *const firmHeader =
		reinterpret_cast<const N3DS_FIRM_Header_t*>(info->header.pData);
	if (firmHeader->magic == cpu_to_be32(N3DS_FIRM_MAGIC)) {
		// This is a FIRM binary.
		return 0;
	}

	// Not supported.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *Nintendo3DSFirm::systemName(unsigned int type) const
{
	RP_D(const Nintendo3DSFirm);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// Nintendo 3DS has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"Nintendo3DSFirm::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	// TODO: *New* Nintendo 3DS for N3DS-exclusive titles.
	static const char *const sysNames[4] = {
		"Nintendo 3DS", "Nintendo 3DS", "3DS", nullptr
	};

	return sysNames[type & SYSNAME_TYPE_MASK];
}

/**
 * Get a list of all supported file extensions.
 * This is to be used for file type registration;
 * subclasses don't explicitly check the extension.
 *
 * NOTE: The extensions include the leading dot,
 * e.g. ".bin" instead of "bin".
 *
 * NOTE 2: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *Nintendo3DSFirm::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".firm",	// boot9strap
		".bin",		// older

		nullptr
	};
	return exts;
}

/**
 * Get a list of all supported MIME types.
 * This is to be used for metadata extractors that
 * must indicate which MIME types they support.
 *
 * NOTE: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *Nintendo3DSFirm::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"application/x-nintendo-3ds-firm",

		nullptr
	};
	return mimeTypes;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int Nintendo3DSFirm::loadFieldData(void)
{
	RP_D(Nintendo3DSFirm);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// Firmware binary isn't valid.
		return -EIO;
	}

	// Nintendo 3DS firmware binary header.
	const N3DS_FIRM_Header_t *const firmHeader = &d->firmHeader;
	d->fields->reserve(5);	// Maximum of 5 fields.

	// Read the firmware binary.
	unique_ptr<uint8_t[]> firmBuf;
	unsigned int szFile = 0;
	if (d->file->size() <= 4*1024*1024) {
		// Firmware binary is 4 MB or less.
		szFile = static_cast<unsigned int>(d->file->size());
		firmBuf.reset(new uint8_t[szFile]);
		d->file->rewind();
		size_t size = d->file->read(firmBuf.get(), szFile);
		if (size != szFile) {
			// Error reading the firmware binary.
			firmBuf.reset();
		}
	}

	// If both ARM11 and ARM9 entry points are non-zero,
	// check if this is an official 3DS firmware binary.
	const Nintendo3DSFirmData::FirmBin_t *firmBin = nullptr;
	const uint32_t arm11_entrypoint = le32_to_cpu(firmHeader->arm11_entrypoint);
	const uint32_t arm9_entrypoint = le32_to_cpu(firmHeader->arm9_entrypoint);
	const char *firmBinDesc = nullptr;
	bool checkCustomFIRM = false;	// Check for a custom FIRM, e.g. Boot9Strap.
	bool checkARM9 = false;		// Check for ARM9 homebrew.
	if (arm11_entrypoint != 0 && arm9_entrypoint != 0) {
		// Calculate the CRC32 and look it up.
		if (firmBuf) {
			const uint32_t crc = crc32(0, firmBuf.get(), static_cast<unsigned int>(szFile));
			firmBin = Nintendo3DSFirmData::lookup_firmBin(crc);
			if (firmBin != nullptr) {
				// Official firmware binary.
				firmBinDesc = (firmBin->isNew3DS ? "New3DS FIRM" : "Old3DS FIRM");
			} else {
				// Check for a custom FIRM.
				checkCustomFIRM = true;

				// NOTE: Luma3DS v9.1 has an ARM11 entry point set,
				// so we should check for ARM9 homebrew as well.
				checkARM9 = true;
			}
		}
	} else if (arm11_entrypoint == 0 && arm9_entrypoint != 0) {
		// ARM9 homebrew.
		firmBinDesc = C_("Nintendo3DSFirm", "ARM9 Homebrew");
		checkARM9 = true;
	} else if (arm11_entrypoint != 0 && arm9_entrypoint == 0) {
		// ARM11 homebrew. (Not a thing...)
		firmBinDesc = C_("Nintendo3DSFirm", "ARM11 Homebrew");
	}

	if (checkCustomFIRM) {
		// TODO: Split into a separate function?

		// Check for "B9S" at 0x3D.
		if (!memcmp(&firmHeader->reserved[0x2D], "B9S", 3)) {
			// This is Boot9Strap.
			firmBinDesc = "Boot9Strap";
		} else if (firmBuf) {
			// Check for sighax installer.
			// NOTE: String has a NULL terminator.
			static const char sighax_magic[] = "3DS BOOTHAX INS";
			if (!memcmp(&firmBuf[0x208], sighax_magic, sizeof(sighax_magic))) {
				// Found derrek's sighax installer.
				firmBinDesc = "sighax installer";
			}
		}
	}

	if (firmBin) {
		// Official firmware binary fields.
		d->fields->addField_string(C_("Nintendo3DSFirm", "Type"),
			(firmBinDesc ? firmBinDesc : C_("RomData", "Unknown")));

		// FIRM version.
		d->fields->addField_string(C_("Nintendo3DSFirm", "FIRM Version"),
			rp_sprintf("%u.%u.%u", firmBin->kernel.major,
				firmBin->kernel.minor, firmBin->kernel.revision));

		// System version.
		d->fields->addField_string(C_("Nintendo3DSFirm", "System Version"),
			rp_sprintf("%u.%u", firmBin->sys.major, firmBin->sys.minor));
	} else if (checkARM9) {
		// Check for ARM9 homebrew.

		// Version strings.
		struct Arm9VerStr_t {
			const char *title;	// Application title.
			const char *searchstr;	// Search string.
			unsigned int searchlen;	// Search string length, without the NULL terminator.
		};
		static const Arm9VerStr_t arm9VerStr[] = {
			{"Luma3DS", "Luma3DS v", 9},
			{"GodMode9", "GodMode9 Explorer v", 19},
			{"Decrypt9WIP", "Decrypt9WIP (", 13},
			{"Hourglass9", "Hourglass9 v", 12},
		};

		const char *arm9VerStr_title = nullptr;
		string s_verstr;
		for (unsigned int i = 0; i < ARRAY_SIZE(arm9VerStr); i++) {
			const char *verstr = static_cast<const char*>(memmem(
				firmBuf.get(), szFile, arm9VerStr[i].searchstr, arm9VerStr[i].searchlen));
			if (!verstr)
				continue;

			arm9VerStr_title = arm9VerStr[i].title;

			// Version does NOT include the 'v' character.
			verstr += arm9VerStr[i].searchlen;
			const char *end = (const char*)firmBuf.get() + szFile;
			int count = 0;
			while (verstr < end && verstr[count] != 0 &&
			       !ISSPACE(verstr[count]) && verstr[count] != ')' &&
			       count < 32)
			{
				count++;
			}
			if (count > 0) {
				// Make sure this is labeled as ARM9 homebrew.
				firmBinDesc = C_("Nintendo3DSFirm", "ARM9 Homebrew");

				// NOTE: Most ARM9 homebrew uses UTF-8 strings.
				s_verstr.assign(verstr, count);
			}

			// We're done here.
			break;
		}

		// Add the firmware type field.
		d->fields->addField_string(C_("Nintendo3DSFirm", "Type"),
			(firmBinDesc ? firmBinDesc : C_("RomData", "Unknown")));

		if (arm9VerStr_title) {
			d->fields->addField_string(C_("RomData", "Title"), arm9VerStr_title);
		}

		// If the version was found, add it.
		if (!s_verstr.empty()) {
			d->fields->addField_string(C_("RomData", "Version"), s_verstr);
		}
	} else {
		// Add the firmware type field.
		d->fields->addField_string(C_("Nintendo3DSFirm", "Type"),
			(firmBinDesc ? firmBinDesc : C_("RomData", "Unknown")));
	}

	// Entry Points
	if (arm11_entrypoint != 0) {
		d->fields->addField_string_numeric(C_("Nintendo3DSFirm", "ARM11 Entry Point"),
			arm11_entrypoint, RomFields::FB_HEX, 8, RomFields::STRF_MONOSPACE);
	}
	if (arm9_entrypoint != 0) {
		d->fields->addField_string_numeric(C_("Nintendo3DSFirm", "ARM9 Entry Point"),
			arm9_entrypoint, RomFields::FB_HEX, 8, RomFields::STRF_MONOSPACE);
	}

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

}
