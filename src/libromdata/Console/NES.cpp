/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * NES.cpp: Nintendo Entertainment System/Famicom ROM reader.              *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * Copyright (c) 2016-2018 by Egor.                                        *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "NES.hpp"
#include "librpbase/RomData_p.hpp"

#include "data/NintendoPublishers.hpp"
#include "data/NESMappers.hpp"
#include "nes_structs.h"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/SystemRegion.hpp"
#include "librpbase/file/IRpFile.hpp"
#include "libi18n/i18n.h"
using namespace LibRpBase;

 // C includes. (C++ namespace)
#include <cassert>
#include <cerrno>
#include <cstring>
#include <ctime>

 // C++ includes.
#include <string>
#include <vector>
using std::string;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(NES)

class NESPrivate : public RomDataPrivate
{
	public:
		NESPrivate(NES *q, IRpFile *file);

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(NESPrivate)

	public:
		/** RomFields **/

		// ROM image type.
		enum RomType {
			ROM_UNKNOWN = -1,		// Unknown ROM type.

			ROM_FORMAT_OLD_INES = 0,	// Archaic iNES format
			ROM_FORMAT_INES = 1,		// iNES format
			ROM_FORMAT_NES2 = 2,		// NES 2.0 format
			ROM_FORMAT_TNES = 3,		// TNES (Nintendo 3DS Virtual Console)
			ROM_FORMAT_FDS = 4,		// Famicom Disk System
			ROM_FORMAT_FDS_FWNES = 5,	// Famicom Disk System (with fwNES header)
			ROM_FORMAT_FDS_TNES = 6,	// Famicom Disk System (TNES / TDS)
			ROM_FORMAT_UNKNOWN = 0xFF,
			ROM_FORMAT_MASK = 0xFF,

			ROM_SYSTEM_NES = (0 << 8),	// NES / Famicom
			ROM_SYSTEM_FDS = (1 << 8),	// Famicom Disk System
			ROM_SYSTEM_VS = (2 << 8),	// VS. System
			ROM_SYSTEM_PC10 = (3 << 8),	// PlayChoice-10
			ROM_SYSTEM_UNKNOWN = (0xFF << 8),
			ROM_SYSTEM_MASK = (0xFF << 8),

			// Special flags. (bitfield)
			ROM_SPECIAL_WIIU_VC = (1 << 16),	// Wii U VC (modified iNES)
			// TODO: Other VC formats, maybe UNIF?
		};
		int romType;

	public:
		// ROM header.
		struct {
			// iNES and FDS are mutually exclusive.
			// TNES + FDS is possible, though.
			union {
				INES_RomHeader ines;
				struct {
					FDS_DiskHeader_fwNES fds_fwNES;
					FDS_DiskHeader fds;
				};
			};
			TNES_RomHeader tnes;
		} header;

		/**
		 * Format PRG/CHR ROM bank sizes, in KiB.
		 *
		 * This function expects the size to be a multiple of 1024,
		 * so it doesn't do any fractional rounding or printing.
		 *
		 * @param size File size.
		 * @return Formatted file size.
		 */
		static inline string formatBankSizeKiB(unsigned int size);

		/**
		 * Convert an FDS BCD datestamp to Unix time.
		 * @param fds_bcd_ds FDS BCD datestamp.
		 * @return Unix time, or -1 if an error occurred.
		 *
		 * NOTE: -1 is a valid Unix timestamp (1970/01/01), but is
		 * not likely to be valid for NES/Famicom, since the Famicom
		 * was released in 1983.
		 */
		static time_t fds_bcd_datestamp_to_unix_time(const FDS_BCD_DateStamp *fds_bcd_ds);
};

/** NESPrivate **/

NESPrivate::NESPrivate(NES *q, IRpFile *file)
	: super(q, file)
	, romType(ROM_UNKNOWN)
{
	// Clear the ROM header structs.
	memset(&header, 0, sizeof(header));
}

/**
 * Format PRG/CHR ROM bank sizes, in KiB.
 *
 * This function expects the size to be a multiple of 1024,
 * so it doesn't do any fractional rounding or printing.
 *
 * @param size File size.
 * @return Formatted file size.
 */
inline string NESPrivate::formatBankSizeKiB(unsigned int size)
{
	return rp_sprintf("%u KiB", (size / 1024));
}

/**
 * Convert an FDS BCD datestamp to Unix time.
 * @param fds_bcd_ds FDS BCD datestamp.
 * @return Unix time, or -1 if an error occurred.
 *
 * NOTE: -1 is a valid Unix timestamp (1970/01/01), but is
 * not likely to be valid for NES/Famicom, since the Famicom
 * was released in 1983.
 */
time_t NESPrivate::fds_bcd_datestamp_to_unix_time(const FDS_BCD_DateStamp *fds_bcd_ds)
{
	// Convert the FDS time to Unix time.
	// NOTE: struct tm has some oddities:
	// - tm_year: year - 1900
	// - tm_mon: 0 == January
	if ((fds_bcd_ds->year == 0 &&
	     fds_bcd_ds->mon == 0 &&
	     fds_bcd_ds->mday == 0) ||
	    (fds_bcd_ds->year == 0xFF &&
	     fds_bcd_ds->mon == 0xFF &&
	     fds_bcd_ds->mday == 0xFF))
	{
		// Invalid date.
		return -1;
	}

	struct tm fdstime;

	// TODO: Check for invalid BCD values.
	fdstime.tm_year = ((fds_bcd_ds->year >> 4) * 10) +
			   (fds_bcd_ds->year & 0x0F) + 1925 - 1900;
	fdstime.tm_mon  = ((fds_bcd_ds->mon >> 4) * 10) +
			   (fds_bcd_ds->mon & 0x0F) - 1;
	fdstime.tm_mday = ((fds_bcd_ds->mday >> 4) * 10) +
			   (fds_bcd_ds->mday & 0x0F);

	// Time portion is empty.
	fdstime.tm_hour = 0;
	fdstime.tm_min = 0;
	fdstime.tm_sec = 0;

	// tm_wday and tm_yday are output variables.
	fdstime.tm_wday = 0;
	fdstime.tm_yday = 0;
	fdstime.tm_isdst = 0;

	// If conversion fails, d->ctime will be set to -1.
	return timegm(&fdstime);
}

/** NES **/

/**
 * Read an NES ROM.
 *
 * A ROM file must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the ROM.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open ROM file.
 */
NES::NES(IRpFile *file)
	: super(new NESPrivate(this, file))
{
	RP_D(NES);
	d->className = "NES";

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}
	
	// Seek to the beginning of the header.
	d->file->rewind();

	// Read the ROM header. [128 bytes]
	uint8_t header[128];
	size_t size = d->file->read(header, sizeof(header));
	if (size != sizeof(header)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this ROM image is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(header);
	info.header.pData = header;
	info.ext = nullptr;	// Not needed for NES.
	info.szFile = d->file->size();
	d->romType = isRomSupported_static(&info);

	switch (d->romType & NESPrivate::ROM_FORMAT_MASK) {
		case NESPrivate::ROM_FORMAT_OLD_INES:
		case NESPrivate::ROM_FORMAT_INES:
		case NESPrivate::ROM_FORMAT_NES2:
			// iNES-style ROM header.
			d->fileType = FTYPE_ROM_IMAGE;
			memcpy(&d->header.ines, header, sizeof(d->header.ines));
			break;

		case NESPrivate::ROM_FORMAT_TNES:
			// TNES ROM header.
			d->fileType = FTYPE_ROM_IMAGE;
			memcpy(&d->header.tnes, header, sizeof(d->header.tnes));
			break;

		case NESPrivate::ROM_FORMAT_FDS:
			// FDS disk image.
			d->fileType = FTYPE_DISK_IMAGE;
			memcpy(&d->header.fds, header, sizeof(d->header.fds));
			break;

		case NESPrivate::ROM_FORMAT_FDS_FWNES:
			// FDS disk image, with fwNES header.
			d->fileType = FTYPE_DISK_IMAGE;
			memcpy(&d->header.fds_fwNES, header, sizeof(d->header.fds_fwNES));
			memcpy(&d->header.fds, &header[16], sizeof(d->header.fds));
			break;

		case NESPrivate::ROM_FORMAT_FDS_TNES: {
			// FDS disk image. (TNES/TDS format)
			size_t szret = d->file->seekAndRead(0x2010, &d->header.fds, sizeof(d->header.fds));
			if (szret != sizeof(d->header.fds)) {
				// Seek and/or read error.
				d->file->unref();
				d->file = nullptr;
				d->fileType = FTYPE_UNKNOWN;
				d->romType = NESPrivate::ROM_FORMAT_UNKNOWN;
				return;
			}

			d->fileType = FTYPE_DISK_IMAGE;
			break;
		}

		default:
			// Unknown ROM type.
			d->file->unref();
			d->file = nullptr;
			d->fileType = FTYPE_UNKNOWN;
			d->romType = NESPrivate::ROM_FORMAT_UNKNOWN;
			return;
	}

	d->isValid = true;
}

/** ROM detection functions. **/

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int NES::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < 16)
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Check for iNES.
	const INES_RomHeader *inesHeader =
		reinterpret_cast<const INES_RomHeader*>(info->header.pData);
	if (inesHeader->magic == cpu_to_be32(INES_MAGIC) ||
	    inesHeader->magic == cpu_to_be32(INES_MAGIC_WIIU_VC))
	{
		// Found an iNES ROM header.
		// If the fourth byte is 0x00, it's Wii U VC.
		int romType = (inesHeader->magic == cpu_to_be32(INES_MAGIC_WIIU_VC))
				? NESPrivate::ROM_SPECIAL_WIIU_VC
				: 0;

		// Check for NES 2.0.
		if ((inesHeader->mapper_hi & 0x0C) == 0x08) {
			// May be NES 2.0
			// Verify the ROM size.
			int64_t size = sizeof(INES_RomHeader) +
				(inesHeader->prg_banks * INES_PRG_BANK_SIZE) +
				(inesHeader->chr_banks * INES_CHR_BANK_SIZE) +
				((inesHeader->nes2.prg_banks_hi << 8) * INES_PRG_BANK_SIZE);
			if (size <= info->szFile) {
				// This is an NES 2.0 header.
				switch (inesHeader->mapper_hi & INES_F7_SYSTEM_MASK) {
					case INES_F7_SYSTEM_VS:
						romType |= NESPrivate::ROM_FORMAT_NES2 |
							   NESPrivate::ROM_SYSTEM_VS;
						break;
					case INES_F7_SYSTEM_PC10:
						romType |= NESPrivate::ROM_FORMAT_NES2 |
							   NESPrivate::ROM_SYSTEM_PC10;
						break;
					default:
						// TODO: What if both are set?
						romType |= NESPrivate::ROM_FORMAT_NES2 |
							   NESPrivate::ROM_SYSTEM_NES;
						break;
				}
				return romType;
			}
		}

		// Not NES 2.0.
		if ((inesHeader->mapper_hi & 0x0C) == 0x00) {
			// May be iNES.
			// TODO: Optimize this check?
			if (info->header.pData[12] == 0 &&
			    info->header.pData[13] == 0 &&
			    info->header.pData[14] == 0 &&
			    info->header.pData[15] == 0)
			{
				// Definitely iNES.
				switch (inesHeader->mapper_hi & INES_F7_SYSTEM_MASK) {
					case INES_F7_SYSTEM_VS:
						romType |= NESPrivate::ROM_FORMAT_INES |
							   NESPrivate::ROM_SYSTEM_VS;
						break;
					case INES_F7_SYSTEM_PC10:
						romType |= NESPrivate::ROM_FORMAT_INES |
							   NESPrivate::ROM_SYSTEM_PC10;
						break;
					default:
						// TODO: What if both are set?
						romType |= NESPrivate::ROM_FORMAT_INES |
							   NESPrivate::ROM_SYSTEM_NES;
						break;
				}
				return romType;
			}
		}

		// Archaic iNES format.
		romType |= NESPrivate::ROM_FORMAT_OLD_INES |
			   NESPrivate::ROM_SYSTEM_NES;
		return romType;
	}

	// Check for TNES.
	const TNES_RomHeader *tnesHeader =
		reinterpret_cast<const TNES_RomHeader*>(info->header.pData);
	if (tnesHeader->magic == cpu_to_be32(TNES_MAGIC)) {
		// Found a TNES ROM header.
		if (tnesHeader->mapper == TNES_MAPPER_FDS) {
			// This is an FDS game.
			// TODO: Validate the FDS header?
			return NESPrivate::ROM_FORMAT_FDS_TNES |
			       NESPrivate::ROM_SYSTEM_FDS;
		}

		// TODO: Verify ROM size?
		return NESPrivate::ROM_FORMAT_TNES |
		       NESPrivate::ROM_SYSTEM_NES;
	}

	// Check for FDS.
	// TODO: Check that the block code is 0x01?
	static const uint8_t fds_magic[] = "*NINTENDO-HVC*";

	// Check for headered FDS.
	const FDS_DiskHeader_fwNES *fwNESHeader =
		reinterpret_cast<const FDS_DiskHeader_fwNES*>(info->header.pData);
	if (fwNESHeader->magic == cpu_to_be32(fwNES_MAGIC)) {
		// fwNES header is present.
		// TODO: Check required NULL bytes.
		// For now, assume this is correct.
		const FDS_DiskHeader *fdsHeader =
			reinterpret_cast<const FDS_DiskHeader*>(&info->header.pData[16]);
		if (fdsHeader->block_code == 0x01 &&
		    !memcmp(fdsHeader->magic, fds_magic, sizeof(fdsHeader->magic)-1))
		{
			// This is an FDS disk image.
			return NESPrivate::ROM_FORMAT_FDS_FWNES |
			       NESPrivate::ROM_SYSTEM_FDS;
		}
	} else {
		// fwNES header is not present.
		const FDS_DiskHeader *fdsHeader =
			reinterpret_cast<const FDS_DiskHeader*>(info->header.pData);
		if (fdsHeader->block_code == 0x01 &&
		    !memcmp(fdsHeader->magic, fds_magic, sizeof(fdsHeader->magic)-1))
		{
			// This is an FDS disk image.
			return NESPrivate::ROM_FORMAT_FDS |
			       NESPrivate::ROM_SYSTEM_FDS;
		}
	}

	// Not supported.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *NES::systemName(unsigned int type) const
{
	RP_D(const NES);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// TODO: Regional variant, e.g. Famicom.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"NES::systemName() array index optimization needs to be updated.");

	const unsigned int idx = (type & SYSNAME_TYPE_MASK);
	switch (d->romType & NESPrivate::ROM_SYSTEM_MASK) {
		case NESPrivate::ROM_SYSTEM_NES:
		default: {
			static const char *const sysNames_NES[3][4] = {
				// NES (International)
				{"Nintendo Entertainment System",
				 "Nintendo Entertainment System",
				 "NES", nullptr},

				// Famicom (Japan)
				{"Nintendo Famicom",
				 "Famicom",
				 "FC", nullptr},

				// Hyundai Comboy (South Korea)
				{"Hyundai Comboy",
				 "Comboy",
				 "CB", nullptr},
			};

			if ((type & SYSNAME_REGION_MASK) == SYSNAME_REGION_GENERIC) {
				// Use the international name.
				return sysNames_NES[0][idx];
			}

			// Get the system region.
			switch (SystemRegion::getCountryCode()) {
				case 'JP':
					return sysNames_NES[1][idx];
				case 'KR':
					return sysNames_NES[2][idx];
				default:
					return sysNames_NES[0][idx];
			}
		}

		case NESPrivate::ROM_SYSTEM_FDS: {
			static const char *const sysNames_FDS[] = {
				"Nintendo Famicom Disk System",
				"Famicom Disk System",
				"FDS", nullptr
			};
			return sysNames_FDS[idx];
		}

		case NESPrivate::ROM_SYSTEM_VS: {
			static const char *const sysNames_VS[] = {
				"Nintendo VS. System",
				"VS. System",
				"VS", nullptr
			};
			return sysNames_VS[idx];
		}

		case NESPrivate::ROM_SYSTEM_PC10: {
			static const char *const sysNames_PC10[] = {
				"Nintendo PlayChoice-10",
				"PlayChoice-10",
				"PC10", nullptr
			};
			return sysNames_PC10[idx];
		}
	};

	// Should not get here...
	return nullptr;
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
const char *const *NES::supportedFileExtensions_static(void)
{
	// NOTE: .fds is missing block checksums.
	// .qd has block checksums, as does .tds (which is basically
	// a 16-byte header, FDS BIOS, and a .qd file).

	// This isn't too important right now because we're only
	// reading the header, but we'll need to take it into
	// account if file access is added.

	static const char *const exts[] = {
		".nes",	// iNES
		".nez",	// Compressed iNES?
		".fds",	// Famicom Disk System
		".qd",	// FDS (Animal Crossing)
		".tds",	// FDS (3DS Virtual Console)

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
const char *const *NES::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types from FreeDesktop.org.
		"application/x-nes-rom",
		"application/x-fds-disk",

		nullptr
	};
	return mimeTypes;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int NES::loadFieldData(void)
{
	RP_D(NES);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file || !d->file->isOpen()) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// ROM image isn't valid.
		return -EIO;
	}

	// NES ROM header:
	// - 15 regular fields. (iNES, NES 2.0, FDS)
	// - 8 fields for the internal NES header.
	d->fields->reserve(15+8);

	// Reserve at least 2 tabs:
	// iNES, Internal Footer
	d->fields->reserveTabs(2);

	// Determine stuff based on the ROM format.
	const char *rom_format;
	bool romOK = true;
	int mapper = -1;
	int submapper = -1;
	int tnes_mapper = -1;
	bool has_trainer = false;
	uint8_t tv_mode = 0xFF;			// NES2_TV_Mode (0xFF if unknown)
	unsigned int prg_rom_size = 0;
	unsigned int chr_rom_size = 0;
	unsigned int chr_ram_size = 0;		// CHR RAM
	unsigned int chr_ram_battery_size = 0;	// CHR RAM with battery (rare but it exists)
	unsigned int prg_ram_size = 0;		// PRG RAM ($6000-$7FFF)
	unsigned int prg_ram_battery_size = 0;	// PRG RAM with battery (save RAM)

	switch (d->romType & NESPrivate::ROM_FORMAT_MASK) {
		case NESPrivate::ROM_FORMAT_OLD_INES:
			d->fields->setTabName(0, "iNES");
			rom_format = C_("NES|Format", "Archaic iNES");
			mapper = (d->header.ines.mapper_lo >> 4);
			has_trainer = !!(d->header.ines.mapper_lo & INES_F6_TRAINER);
			prg_rom_size = d->header.ines.prg_banks * INES_PRG_BANK_SIZE;
			chr_rom_size = d->header.ines.chr_banks * INES_CHR_BANK_SIZE;
			if (chr_rom_size == 0) {
				chr_ram_size = 8192;
			}
			if (d->header.ines.mapper_lo & INES_F6_BATTERY) {
				prg_ram_battery_size = 8192;
			}
			break;

		case NESPrivate::ROM_FORMAT_INES:
			d->fields->setTabName(0, "iNES");
			rom_format = "iNES";	// NOT translatable!
			mapper = (d->header.ines.mapper_lo >> 4) |
				 (d->header.ines.mapper_hi & 0xF0);
			has_trainer = !!(d->header.ines.mapper_lo & INES_F6_TRAINER);
			// NOTE: Very few iNES ROMs have this set correctly,
			// so we're ignoring it for now.
			//tv_mode = (d->header.ines.ines.tv_mode & 1);
			prg_rom_size = d->header.ines.prg_banks * INES_PRG_BANK_SIZE;
			chr_rom_size = d->header.ines.chr_banks * INES_CHR_BANK_SIZE;
			if (chr_rom_size == 0) {
				chr_ram_size = 8192;
			}
			if (d->header.ines.mapper_lo & INES_F6_BATTERY) {
				if (d->header.ines.ines.prg_ram_size == 0) {
					prg_ram_battery_size = 8192;
				} else {
					prg_ram_battery_size = d->header.ines.ines.prg_ram_size * INES_PRG_RAM_BANK_SIZE;
				}
			} else {
				// FIXME: Is this correct?
				if (d->header.ines.ines.prg_ram_size > 0) {
					prg_ram_size = d->header.ines.ines.prg_ram_size * INES_PRG_RAM_BANK_SIZE;
				}
			}
			break;

		case NESPrivate::ROM_FORMAT_NES2:
			d->fields->setTabName(0, "NES 2.0");
			rom_format = "NES 2.0";	// NOT translatable!
			mapper = (d->header.ines.mapper_lo >> 4) |
				 (d->header.ines.mapper_hi & 0xF0) |
				 ((d->header.ines.nes2.mapper_hi2 & 0x0F) << 8);
			submapper = (d->header.ines.nes2.mapper_hi2 >> 4);
			has_trainer = !!(d->header.ines.mapper_lo & INES_F6_TRAINER);
			tv_mode = (d->header.ines.nes2.tv_mode & 3);
			prg_rom_size = ((d->header.ines.prg_banks +
					(d->header.ines.nes2.prg_banks_hi << 8))
					* INES_PRG_BANK_SIZE);
			chr_rom_size = d->header.ines.chr_banks * INES_CHR_BANK_SIZE;
			// CHR RAM size. (TODO: Needs testing.)
			if (d->header.ines.nes2.vram_size & 0x0F) {
				chr_ram_size = 128 << ((d->header.ines.nes2.vram_size & 0x0F) - 1);
			}
			if ((d->header.ines.mapper_lo & INES_F6_BATTERY) &&
			    (d->header.ines.nes2.vram_size & 0xF0))
			{
				chr_ram_battery_size = 128 << ((d->header.ines.nes2.vram_size >> 4) - 1);
			}
			// PRG RAM size. (TODO: Needs testing.)
			if (d->header.ines.nes2.prg_ram_size & 0x0F) {
				prg_ram_size = 128 << ((d->header.ines.nes2.prg_ram_size >> 4) - 1);
			}
			// TODO: Require INES_F6_BATTERY?
			if (d->header.ines.nes2.prg_ram_size & 0xF0) {
				prg_ram_battery_size = 128 << ((d->header.ines.nes2.prg_ram_size >> 4) - 1);
			}
			break;

		case NESPrivate::ROM_FORMAT_TNES:
			d->fields->setTabName(0, "TNES");
			rom_format = C_("NES|Format", "TNES (Nintendo 3DS Virtual Console)");
			tnes_mapper = d->header.tnes.mapper;
			mapper = NESMappers::tnesMapperToInesMapper(tnes_mapper);
			prg_rom_size = d->header.tnes.prg_banks * TNES_PRG_BANK_SIZE;
			chr_rom_size = d->header.tnes.chr_banks * TNES_CHR_BANK_SIZE;
			// FIXME: Check Zelda TNES to see where 8K CHR RAM is.
			break;

		// NOTE: FDS fields are handled later.
		// We're just obtaining the ROM format name here.
		case NESPrivate::ROM_FORMAT_FDS:
			d->fields->setTabName(0, "FDS");
			rom_format = C_("NES|Format", "FDS disk image");
			break;
		case NESPrivate::ROM_FORMAT_FDS_FWNES:
			d->fields->setTabName(0, "FDS");
			rom_format = C_("NES|Format", "FDS disk image (with fwNES header)");
			break;
		case NESPrivate::ROM_FORMAT_FDS_TNES:
			d->fields->setTabName(0, "FDS");
			rom_format = C_("NES|Format", "TDS (Nintendo 3DS Virtual Console)");
			break;

		default:
			d->fields->setTabName(0, "NES");
			rom_format = C_("RomData", "Unknown");
			romOK = false;
			break;
	}

	const char *const format_title = C_("NES", "Format");
	if (d->romType & NESPrivate::ROM_SPECIAL_WIIU_VC) {
		// Wii U Virtual Console.
		const int romFormat = (d->romType & NESPrivate::ROM_FORMAT_MASK);
		assert(romFormat >= NESPrivate::ROM_FORMAT_OLD_INES);
		assert(romFormat <= NESPrivate::ROM_FORMAT_NES2);
		if (romFormat >= NESPrivate::ROM_FORMAT_OLD_INES &&
		    romFormat <= NESPrivate::ROM_FORMAT_NES2)
		{
			d->fields->addField_string(format_title,
				// tr: ROM format, e.g. iNES or FDS disk image.
				rp_sprintf(C_("NES|Format", "%s (Wii U Virtual Console)"), rom_format));
		} else {
			d->fields->addField_string(format_title, rom_format);
		}
	} else {
		d->fields->addField_string(format_title, rom_format);
	}

	// Display the fields.
	if (!romOK) {
		// Invalid mapper.
		return static_cast<int>(d->fields->count());
	}

	const char *const mapper_title = C_("NES", "Mapper");
	if (mapper >= 0) {
		// Look up the mapper name.
		string s_mapper;

		const char *const mapper_name = NESMappers::lookup_ines(mapper);
		if (mapper_name) {
			// tr: Print the mapper ID followed by the mapper name.
			s_mapper = rp_sprintf_p(C_("NES|Mapper", "%1$u - %2$s"),
				static_cast<unsigned int>(mapper), mapper_name);
		} else {
			// tr: Print only the mapper ID.
			s_mapper = rp_sprintf("%u", static_cast<unsigned int>(mapper));
		}
		d->fields->addField_string(mapper_title, s_mapper);
	} else {
		// No mapper. If this isn't TNES,
		// then it's probably an FDS image.
		if (tnes_mapper >= 0) {
			// This has a TNES mapper.
			// It *should* map to an iNES mapper...
			d->fields->addField_string(mapper_title,
				C_("NES", "[Missing TNES mapping!]"));
		}
	}

	if (submapper >= 0) {
		// Submapper. (NES 2.0 only)
		string s_submapper;

		// Look up the submapper name.
		// TODO: Needs testing.
		const char *const submapper_name = NESMappers::lookup_nes2_submapper(mapper, submapper);
		if (submapper_name) {
			// tr: Print the submapper ID followed by the submapper name.
			s_submapper = rp_sprintf_p(C_("NES|Mapper", "%1$u - %2$s"),
				static_cast<unsigned int>(submapper), submapper_name);
		} else {
			// tr: Print only the submapper ID.
			s_submapper = rp_sprintf("%u", static_cast<unsigned int>(submapper));
		}
		d->fields->addField_string(C_("NES", "Submapper"), s_submapper);
	}

	if (tnes_mapper >= 0) {
		// TNES mapper.
		// TODO: Look up the name.
		d->fields->addField_string_numeric(C_("NES", "TNES Mapper"),
			tnes_mapper, RomFields::FB_DEC);
	}

	// TV mode
	// NOTE: Dendy PAL isn't supported in any headers at the moment.
	static const char *const tv_mode_tbl[] = {
		NOP_C_("NES|TVMode", "NTSC"),
		NOP_C_("NES|TVMode", "PAL"),
		NOP_C_("NES|TVMode", "Dual (NTSC/PAL)"),
		NOP_C_("NES|TVMode", "Dual (NTSC/PAL)"),
	};
	if (tv_mode < ARRAY_SIZE(tv_mode_tbl)) {
		d->fields->addField_string(C_("NES", "TV Mode"),
			dpgettext_expr(RP_I18N_DOMAIN, "NES|TVMode", tv_mode_tbl[tv_mode]));
	}

	// ROM features
	const char *rom_features = nullptr;
	if (prg_ram_battery_size > 0 && has_trainer) {
		rom_features = C_("NES|Features", "Save RAM, Trainer");
	} else if (prg_ram_battery_size > 0 && !has_trainer) {
		rom_features = C_("NES|Features", "Save RAM");
	} else if (prg_ram_battery_size == 0 && has_trainer) {
		rom_features = C_("NES|Features", "Trainer");
	}
	if (rom_features) {
		d->fields->addField_string(C_("NES", "Features"), rom_features);
	}

	// ROM sizes
	if (prg_rom_size > 0) {
		d->fields->addField_string(C_("NES", "PRG ROM Size"),
			d->formatBankSizeKiB(prg_rom_size));
	}
	if (chr_rom_size > 0) {
		d->fields->addField_string(C_("NES", "CHR ROM Size"),
			d->formatBankSizeKiB(chr_rom_size));
	}

	// RAM sizes
	if (chr_ram_size > 0) {
		d->fields->addField_string(C_("NES", "CHR RAM Size"),
			d->formatBankSizeKiB(chr_ram_size));
	}
	if (chr_ram_battery_size > 0) {
		// tr: CHR RAM with a battery backup.
		d->fields->addField_string(C_("NES", "CHR RAM (backed up)"),
			d->formatBankSizeKiB(chr_ram_battery_size));
	}
	if (prg_ram_size > 0) {
		d->fields->addField_string(C_("NES", "PRG RAM Size"),
			d->formatBankSizeKiB(prg_ram_size));
	}
	if (prg_ram_battery_size > 0) {
		// tr: Save RAM with a battery backup.
		d->fields->addField_string(C_("NES", "Save RAM (backed up)"),
			d->formatBankSizeKiB(prg_ram_battery_size));
	}

	// Check for FDS fields.
	if ((d->romType & NESPrivate::ROM_SYSTEM_MASK) == NESPrivate::ROM_SYSTEM_FDS) {
		// Game ID
		// TODO: Check for invalid characters?
		d->fields->addField_string(C_("NES", "Game ID"),
			rp_sprintf("%s-%.3s",
				(d->header.fds.disk_type == FDS_DTYPE_FSC ? "FSC" : "FMC"),
				d->header.fds.game_id));

		// Publisher
		const char *const publisher_title = C_("RomData", "Publisher");
		const char *const publisher =
			NintendoPublishers::lookup_fds(d->header.fds.publisher_code);
		if (publisher) {
			d->fields->addField_string(publisher_title, publisher);
		} else {
			d->fields->addField_string(publisher_title,
				rp_sprintf(C_("RomData", "Unknown (0x%02X)"), d->header.fds.publisher_code));
		}

		// Revision
		d->fields->addField_string_numeric(C_("RomData", "Revision"),
			d->header.fds.revision, RomFields::FB_DEC, 2);

		// Manufacturing Date.
		time_t mfr_date = d->fds_bcd_datestamp_to_unix_time(&d->header.fds.mfr_date);
		d->fields->addField_dateTime(C_("NES", "Manufacturing Date"), mfr_date,
			RomFields::RFT_DATETIME_HAS_DATE |
			RomFields::RFT_DATETIME_IS_UTC  // Date only.
		);

		// TODO: Disk Writer fields.
	} else {
		// Add non-FDS fields.
		const char *s_mirroring = nullptr;
		const char *s_vs_ppu = nullptr;
		switch (d->romType & NESPrivate::ROM_FORMAT_MASK) {
			case NESPrivate::ROM_FORMAT_OLD_INES:
			case NESPrivate::ROM_FORMAT_INES:
			case NESPrivate::ROM_FORMAT_NES2:
				// Mirroring
				// TODO: Detect mappers that have programmable mirroring.
				// TODO: Also One Screen, e.g. AxROM.
				if (d->header.ines.mapper_lo & INES_F6_MIRROR_FOUR) {
					// Four screens using extra VRAM.
					s_mirroring = C_("NES|Mirroring", "Four Screens");
				} else {
					// TODO: There should be a "one screen" option...
					if (d->header.ines.mapper_lo & INES_F6_MIRROR_VERT) {
						s_mirroring = C_("NES|Mirroring", "Vertical");
					} else {
						s_mirroring = C_("NES|Mirroring", "Horizontal");
					}
				}

				if ((d->romType & (NESPrivate::ROM_FORMAT_MASK | NESPrivate::ROM_SYSTEM_MASK)) ==
				    (NESPrivate::ROM_FORMAT_NES2 | NESPrivate::ROM_SYSTEM_VS))
				{
					// Check the VS. PPU type.
					// NOTE: Not translatable!
					static const char vs_ppu_types[13][12] = {
						"RP2C03B",     "RP2C03G",
						"RP2C04-0001", "RP2C04-0002",
						"RP2C04-0003", "RP2C04-0004",
						"RP2C03B",     "RP2C03C",
						"RP2C05-01",   "RP2C05-02",
						"RP2C05-03",   "RP2C05-04",
						"RP2C05-05"
					};
					const unsigned int vs_ppu = (d->header.ines.nes2.vs_hw & 0x0F);
					if (vs_ppu < ARRAY_SIZE(vs_ppu_types)) {
						s_vs_ppu = vs_ppu_types[vs_ppu];
					}

					// TODO: VS. copy-protection hardware?
				}
				break;

			case NESPrivate::ROM_FORMAT_TNES:
				// Mirroring
				switch (d->header.tnes.mirroring) {
					case TNES_MIRRORING_PROGRAMMABLE:
						// For all mappers except AxROM, this is programmable.
						// For AxROM, this is One Screen.
						if (tnes_mapper == TNES_MAPPER_AxROM) {
							s_mirroring = C_("NES|Mirroring", "One Screen");
						} else {
							s_mirroring = C_("NES|Mirroring", "Programmable");
						}
						break;
					case TNES_MIRRORING_HORIZONTAL:
						s_mirroring = C_("NES|Mirroring", "Horizontal");
						break;
					case TNES_MIRRORING_VERTICAL:
						s_mirroring = C_("NES|Mirroring", "Vertical");
						break;
					default:
						s_mirroring = C_("RomData", "Unknown");
						break;
				}
				break;

			default:
				break;
		}

		const char *const mirroring_title = C_("NES", "Mirroring");
		if (s_mirroring) {
			d->fields->addField_string(mirroring_title, s_mirroring);
		}
		if (s_vs_ppu) {
			d->fields->addField_string(C_("NES", "VS. PPU"), s_vs_ppu);
		}

		// Check for the internal NES footer.
		// This is located at the last 32 bytes of the last PRG bank in some ROMs.
		// We'll verify that it's valid by looking for an
		// all-ASCII title, possibly right-aligned with
		// 0xFF filler bytes.
		// NOTE: +16 to skip the iNES header.
		NES_IntFooter intFooter;
		bool hasFooter = false;
		unsigned int firstNonFF = (unsigned int)sizeof(intFooter.name);
		unsigned int addr = prg_rom_size - sizeof(intFooter) + 16;

		do {	// to break out early
			size_t size = d->file->seekAndRead(addr, &intFooter, sizeof(intFooter));
			if (size != sizeof(intFooter)) {
				// Seek and/or read error.
				// Assume we don't have an internal footer.
				break;
			}

			// Check the mirroring value.
			if ((intFooter.board_info >> 4) > 1) {
				// Incorrect mirroring value.
				break;
			}

			// Check if the name looks right.
			firstNonFF = (unsigned int)sizeof(intFooter.name);
			bool foundNonFF = false;
			bool foundInvalid = false;
			for (unsigned int i = 0; i < (unsigned int)sizeof(intFooter.name); i++) {
				char chr = intFooter.name[i];

				// Skip leading 0xFF.
				if (chr == (char)0xFF) {
					if (foundNonFF) {
						// Cannot have 0xFF here.
						foundInvalid = true;
						break;
					}
					continue;
				}
				// Also skip leading spaces.
				if (!foundNonFF && chr == ' ') {
					continue;
				}

				if (!foundNonFF) {
					foundNonFF = true;
					firstNonFF = i;
				}
				if (chr < 32 || chr >= 127) {
					// Invalid character.
					foundInvalid = true;
					break;
				}
			}

			if (!foundInvalid && firstNonFF < (unsigned int)sizeof(intFooter.name)) {
				// Name looks valid.
				hasFooter = true;
				break;
			}
		} while (0);

		if (hasFooter) {
			// Found the internal footer.
			d->fields->addTab("Internal Footer");

			// Internal name (Assuming ASCII)
			if (firstNonFF < (unsigned int)sizeof(intFooter.name)) {
				d->fields->addField_string(C_("NES", "Internal Name"),
					latin1_to_utf8(&intFooter.name[firstNonFF], sizeof(intFooter.name)-firstNonFF),
					RomFields::STRF_TRIM_END);
			}

			// PRG checksum
			d->fields->addField_string_numeric(C_("NES", "PRG Checksum"),
				le16_to_cpu(intFooter.prg_checksum),
				RomFields::FB_HEX, 4, RomFields::STRF_MONOSPACE);

			// CHR checksum
			d->fields->addField_string_numeric(C_("NES", "CHR Checksum"),
				le16_to_cpu(intFooter.chr_checksum),
				RomFields::FB_HEX, 4, RomFields::STRF_MONOSPACE);

			// ROM sizes
			static const uint32_t sz_lookup[] = {
				0,		// 0
				0,		// 1
				32*1024,	// 2
				128*1024,	// 3
				256*1024,	// 4
				512*1024,	// 5
			};

			const uint8_t prg_sz_idx = intFooter.rom_size >> 4;
			const uint8_t chr_sz_idx = intFooter.rom_size & 0x0F;
			unsigned int prg_size = 0, chr_size = 0;
			if (prg_sz_idx < ARRAY_SIZE(sz_lookup)) {
				prg_size = sz_lookup[prg_sz_idx];
			}
			if (chr_sz_idx < ARRAY_SIZE(sz_lookup)) {
				chr_size = sz_lookup[chr_sz_idx];
			}

			// PRG ROM size
			string s_prg_size;
			if (prg_sz_idx == 0) {
				s_prg_size = C_("NES", "Not set");
			} else if (prg_size != 0) {
				s_prg_size = d->formatBankSizeKiB(prg_size);
			} else {
				s_prg_size = rp_sprintf(C_("RomData", "Unknown (0x%02X)"), prg_sz_idx);
			}
			d->fields->addField_string(C_("NES", "PRG ROM Size"), s_prg_size);

			// CHR ROM size
			string s_chr_size;
			if (chr_sz_idx == 0) {
				s_chr_size = C_("NES", "Not set");
			} else if (chr_size != 0) {
				s_chr_size = d->formatBankSizeKiB(chr_size);
			} else {
				s_chr_size = rp_sprintf(C_("RomData", "Unknown (0x%02X)"), chr_sz_idx);
			}
			d->fields->addField_string(C_("NES", "CHR ROM Size"), s_chr_size);

			// Mirroring
			switch (intFooter.board_info >> 4) {
				case 0:
					s_mirroring = C_("NES|Mirroring", "Horizontal");
					break;
				case 1:
					s_mirroring = C_("NES|Mirroring", "Vertical");
					break;
				default:
					s_mirroring = nullptr;
					break;
			}
			if (s_mirroring) {
				d->fields->addField_string(mirroring_title, s_mirroring);
			} else {
				d->fields->addField_string(mirroring_title,
					rp_sprintf(C_("RomData", "Unknown (0x%02X)"), intFooter.board_info >> 4));
			}

			// Board type
			// TODO: Lookup table.
			d->fields->addField_string_numeric(C_("NES", "Board Type"),
				intFooter.board_info & 0x0F);

			// Publisher
			const char *const publisher_title = C_("RomData", "Publisher");
			const char *const publisher =
				NintendoPublishers::lookup_old(intFooter.publisher_code);
			if (publisher) {
				d->fields->addField_string(publisher_title, publisher);
			} else {
				d->fields->addField_string(publisher_title,
					rp_sprintf(C_("RomData", "Unknown (0x%02X)"), intFooter.publisher_code));
			}
		}
	}

	// TODO: More fields.
	return static_cast<int>(d->fields->count());
}

}
