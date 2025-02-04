/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * DMG.hpp: Game Boy (DMG/CGB/SGB) ROM reader.                             *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * Copyright (c) 2016-2018 by Egor.                                        *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "DMG.hpp"
#include "librpbase/RomData_p.hpp"

#include "data/NintendoPublishers.hpp"
#include "dmg_structs.h"

// librpbase
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"
#include "libi18n/i18n.h"
using namespace LibRpBase;

// For sections delegated to other RomData subclasses.
#include "librpbase/disc/DiscReader.hpp"
#include "librpbase/disc/PartitionFile.hpp"
#include "Audio/GBS.hpp"
#include "Audio/gbs_structs.h"

// C includes. (C++ namespace)
#include "librpbase/ctypex.h"
#include <cassert>
#include <cerrno>
#include <cstring>

// C++ includes.
#include <string>
#include <vector>
using std::string;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(DMG)

class DMGPrivate : public RomDataPrivate
{
	public:
		DMGPrivate(DMG *q, IRpFile *file);

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(DMGPrivate)

	public:
		/** RomFields **/

		// System. (RFT_BITFIELD)
		enum DMG_System {
			DMG_SYSTEM_DMG		= (1 << 0),
			DMG_SYSTEM_SGB		= (1 << 1),
			DMG_SYSTEM_CGB		= (1 << 2),
		};

		// Cartridge hardware features. (RFT_BITFIELD)
		enum DMG_Feature {
			DMG_FEATURE_RAM		= (1 << 0),
			DMG_FEATURE_BATTERY	= (1 << 1),
			DMG_FEATURE_TIMER	= (1 << 2),
			DMG_FEATURE_RUMBLE	= (1 << 3),
		};

		/** Internal ROM data. **/

		// Cartridge hardware.
		enum DMG_Hardware {
			DMG_HW_UNK,
			DMG_HW_ROM,
			DMG_HW_MBC1,
			DMG_HW_MBC2,
			DMG_HW_MBC3,
			DMG_HW_MBC4,
			DMG_HW_MBC5,
			DMG_HW_MBC6,
			DMG_HW_MBC7,
			DMG_HW_MMM01,
			DMG_HW_HUC1,
			DMG_HW_HUC3,
			DMG_HW_TAMA5,
			DMG_HW_CAMERA
		};
		static const char *const dmg_hardware_names[];

		struct dmg_cart_type {
			uint8_t hardware;	// DMG_Hardware
			uint8_t features;	// DMG_Feature
		};

	private:
		// Sparse array setup:
		// - "start" starts at 0x00.
		// - "end" ends at 0xFF.
		static const dmg_cart_type dmg_cart_types_start[];
		static const dmg_cart_type dmg_cart_types_end[];

	public:
		/**
		 * Get a dmg_cart_type struct describing a cartridge type byte.
		 * @param type Cartridge type byte.
		 * @return dmg_cart_type struct.
		 */
		static inline const dmg_cart_type& CartType(uint8_t type);

		/**
		 * Convert the ROM size value to an actual size.
		 * @param type ROM size value.
		 * @return ROM size, in kilobytes. (-1 on error)
		 */
		static inline int RomSize(uint8_t type);

		/**
		 * Format ROM/RAM sizes, in KiB.
		 *
		 * This function expects the size to be a multiple of 1024,
		 * so it doesn't do any fractional rounding or printing.
		 *
		 * @param size File size.
		 * @return Formatted file size.
		 */
		inline string formatROMSizeKiB(unsigned int size);

	public:
		/**
		 * DMG RAM size array.
		 */
		static const uint8_t dmg_ram_size[];

		/**
		 * Nintendo's logo which is checked by bootrom.
		 * (Top half only.)
		 * 
		 * NOTE: CGB bootrom only checks the top half of the logo.
		 * (see 0x00D1 of CGB IPL)
		 */
		static const uint8_t dmg_nintendo[0x18];

	public:
		enum DMG_RomType {
			ROM_UNKNOWN	= -1,	// Unknown ROM type.
			ROM_DMG		= 0,	// Game Boy
			ROM_CGB		= 1,	// Game Boy Color

			ROM_MAX
		};

		// ROM type.
		int romType;

	public:
		// ROM header.
		DMG_RomHeader romHeader;

		// GBX footer.
		GBX_Footer gbxFooter;

		/**
		 * Get the title and game ID.
		 *
		 * NOTE: These have to be handled at the same time because
		 * later games take bytes away from the title field to use
		 * for the CGB flag and the game ID.
		 *
		 * @param s_title	[out] Title.
		 * @param s_gameID	[out] Game ID, or "Unknown" if not available.
		 */
		void getTitleAndGameID(string &s_title, string &s_gameID) const;

		/**
		 * Get the publisher.
		 * @return Publisher, or "Unknown (xxx)" if unknown.
		 */
		string getPublisher(void) const;
};

/** DMGPrivate **/

/** Internal ROM data. **/

// Cartrige hardware.
const char *const DMGPrivate::dmg_hardware_names[] = {
	"Unknown",
	"ROM",
	"MBC1",
	"MBC2",
	"MBC3",
	"MBC4",
	"MBC5",
	"MBC6",
	"MBC7",
	"MMM01",
	"HuC1",
	"HuC3",
	"TAMA5",
	"POCKET CAMERA", // ???
};

const DMGPrivate::dmg_cart_type DMGPrivate::dmg_cart_types_start[] = {
	{DMG_HW_ROM,	0},
	{DMG_HW_MBC1,	0},
	{DMG_HW_MBC1,	DMG_FEATURE_RAM},
	{DMG_HW_MBC1,	DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
	{DMG_HW_UNK,	0},
	{DMG_HW_MBC2,	0},
	{DMG_HW_MBC2,	DMG_FEATURE_BATTERY},
	{DMG_HW_UNK,	0},
	{DMG_HW_ROM,	DMG_FEATURE_RAM},
	{DMG_HW_ROM,	DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
	{DMG_HW_UNK,	0},
	{DMG_HW_MMM01,	0},
	{DMG_HW_MMM01,	DMG_FEATURE_RAM},
	{DMG_HW_MMM01,	DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
	{DMG_HW_UNK,	0},
	{DMG_HW_MBC3,	DMG_FEATURE_TIMER|DMG_FEATURE_BATTERY},
	{DMG_HW_MBC3,	DMG_FEATURE_TIMER|DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
	{DMG_HW_MBC3,	0},
	{DMG_HW_MBC3,	DMG_FEATURE_RAM},
	{DMG_HW_MBC3,	DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
	{DMG_HW_UNK,	0},
	{DMG_HW_MBC4,	0},
	{DMG_HW_MBC4,	DMG_FEATURE_RAM},
	{DMG_HW_MBC4,	DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
	{DMG_HW_UNK,	0},
	{DMG_HW_MBC5,	0},
	{DMG_HW_MBC5,	DMG_FEATURE_RAM},
	{DMG_HW_MBC5,	DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
	{DMG_HW_MBC5,	DMG_FEATURE_RUMBLE},
	{DMG_HW_MBC5,	DMG_FEATURE_RUMBLE|DMG_FEATURE_RAM},
	{DMG_HW_MBC5,	DMG_FEATURE_RUMBLE|DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
	{DMG_HW_UNK,	0},
	{DMG_HW_MBC6,	0},
	{DMG_HW_UNK,	0},
	{DMG_HW_MBC7,	DMG_FEATURE_RUMBLE|DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
};

const DMGPrivate::dmg_cart_type DMGPrivate::dmg_cart_types_end[] = {
	{DMG_HW_CAMERA, 0},
	{DMG_HW_TAMA5, 0},
	{DMG_HW_HUC3, 0},
	{DMG_HW_HUC1, DMG_FEATURE_RAM|DMG_FEATURE_BATTERY},
};

DMGPrivate::DMGPrivate(DMG *q, IRpFile *file)
	: super(q, file)
	, romType(ROM_UNKNOWN)
{
	// Clear the various structs.
	memset(&romHeader, 0, sizeof(romHeader));
	memset(&gbxFooter, 0, sizeof(gbxFooter));
}

/**
 * Get a dmg_cart_type struct describing a cartridge type byte.
 * @param type Cartridge type byte.
 * @return dmg_cart_type struct.
 */
inline const DMGPrivate::dmg_cart_type& DMGPrivate::CartType(uint8_t type)
{
	static const dmg_cart_type unk = {DMG_HW_UNK, 0};
	if (type < ARRAY_SIZE(dmg_cart_types_start)) {
		return dmg_cart_types_start[type];
	}
	const unsigned end_offset = 0x100u-ARRAY_SIZE(dmg_cart_types_end);
	if (type>=end_offset) {
		return dmg_cart_types_end[type-end_offset];
	}
	return unk;
}

/**
 * Convert the ROM size value to an actual size.
 * @param type ROM size value.
 * @return ROM size, in kilobytes. (-1 on error)
 */
inline int DMGPrivate::RomSize(uint8_t type)
{
	static const int rom_size[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
	static const int rom_size_52[] = {1152, 1280, 1536};
	if (type < ARRAY_SIZE(rom_size)) {
		return rom_size[type];
	} else if (type >= 0x52 && type < 0x52+ARRAY_SIZE(rom_size_52)) {
		return rom_size_52[type-0x52];
	}
	return -1;
}

/**
 * Format ROM/RAM sizes, in KiB.
 *
 * This function expects the size to be a multiple of 1024,
 * so it doesn't do any fractional rounding or printing.
 *
 * @param size File size.
 * @return Formatted file size.
 */
inline string DMGPrivate::formatROMSizeKiB(unsigned int size)
{
	return rp_sprintf("%u KiB", (size / 1024));
}

/**
 * DMG RAM size array.
 */
const uint8_t DMGPrivate::dmg_ram_size[] = {
	0, 2, 8, 32, 128, 64
};

/**
 * Nintendo's logo which is checked by bootrom.
 * (Top half only.)
 * 
 * NOTE: CGB bootrom only checks the top half of the logo.
 * (see 0x00D1 of CGB IPL)
 */
const uint8_t DMGPrivate::dmg_nintendo[0x18] = {
	0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
	0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
	0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E
};

/**
 * Get the title and game ID.
 *
 * NOTE: These have to be handled at the same time because
 * later games take bytes away from the title field to use
 * for the CGB flag and the game ID.
 *
 * @param s_title	[out] Title.
 * @param s_gameID	[out] Game ID, or "Unknown" if not available.
 */
void DMGPrivate::getTitleAndGameID(string &s_title, string &s_gameID) const
{
	/* NOTE: there are two approaches for doing this, when the 15 bytes are all used
	 * 1) prioritize id
	 * 2) prioritize title
	 * Both of those have counter examples:
	 * If you do the first, you will get "SUPER MARIO" and "LAND" on super mario land rom
	 * With the second one, you will get "MARIO DELUXAHYJ" and Unknown on super mario deluxe rom
	 *
	 * Current method is the first one.
	 */
	if (romHeader.cgbflag < 0x80) {
		// Assuming 16-character title for non-CGB.
		// Game ID is not present.
		s_title = latin1_to_utf8(romHeader.title16, sizeof(romHeader.title16));
		s_gameID = C_("RomData", "Unknown");
		return;
	}

	// Check if CGB flag is set.
	bool isGameID;
	if ((romHeader.cgbflag & 0x3F) == 0) {
		// CGB flag is set.
		// Check if a Game ID is present.
		isGameID = true;
		for (unsigned int i = 11; i < 15; i++) {
			if (!ISALNUM(romHeader.title15[i])) {
				// Not a Game ID.
				isGameID = false;
				break;
			}
		}
	} else {
		// Not CGB. No Game ID.
		isGameID = false;
	}

	if (isGameID) {
		// Game ID is present.
		s_title = latin1_to_utf8(romHeader.title11, sizeof(romHeader.title11));

		// Append the publisher code to make an ID6.
		s_gameID.clear();
		s_gameID.resize(6);
		s_gameID[0] = romHeader.id4[0];
		s_gameID[1] = romHeader.id4[1];
		s_gameID[2] = romHeader.id4[2];
		s_gameID[3] = romHeader.id4[3];
		if (romHeader.old_publisher_code == 0x33) {
			// New publisher code.
			s_gameID[4] = romHeader.new_publisher_code[0];
			s_gameID[5] = romHeader.new_publisher_code[1];
		} else {
			// Old publisher code.
			// FIXME: This probably won't ever happen,
			// since Game ID was added *after* CGB.
			static const char hex_lookup[16] = {
				'0','1','2','3','4','5','6','7',
				'8','9','A','B','C','D','E','F'
			};
			s_gameID[4] = hex_lookup[romHeader.old_publisher_code >> 4];
			s_gameID[5] = hex_lookup[romHeader.old_publisher_code & 0x0F];
		}
	} else {
		// Game ID is not present.
		s_title = latin1_to_utf8(romHeader.title15, sizeof(romHeader.title15));
		s_gameID = C_("RomData", "Unknown");
	}
}

/**
 * Get the publisher.
 * @return Publisher, or "Unknown (xxx)" if unknown.
 */
string DMGPrivate::getPublisher(void) const
{
	const char* publisher;
	string s_publisher;
	if (romHeader.old_publisher_code == 0x33) {
		// New publisher code.
		publisher = NintendoPublishers::lookup(romHeader.new_publisher_code);
		if (publisher) {
			s_publisher = publisher;
		} else {
			if (ISALNUM(romHeader.new_publisher_code[0]) &&
			    ISALNUM(romHeader.new_publisher_code[1]))
			{
				s_publisher = rp_sprintf(C_("DMG", "Unknown (%.2s)"),
					romHeader.new_publisher_code);
			} else {
				s_publisher = rp_sprintf(C_("DMG", "Unknown (%02X %02X)"),
					static_cast<uint8_t>(romHeader.new_publisher_code[0]),
					static_cast<uint8_t>(romHeader.new_publisher_code[1]));
			}
		}
	} else {
		// Old publisher code.
		publisher = NintendoPublishers::lookup_old(romHeader.old_publisher_code);
		if (publisher) {
			s_publisher = publisher;
		} else {
			s_publisher = rp_sprintf(C_("RomData", "Unknown (%02X)"),
				romHeader.old_publisher_code);
		}
	}

	return s_publisher;
}

/** DMG **/

/**
 * Read a Game Boy ROM.
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
DMG::DMG(IRpFile *file)
	: super(new DMGPrivate(this, file))
{
	RP_D(DMG);
	d->className = "DMG";

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Seek to the beginning of the header.
	d->file->rewind();

	// Read the ROM header. [0x150 bytes]
	uint8_t header[0x150];
	size_t size = d->file->read(header, sizeof(header));
	if (size != sizeof(header)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this ROM is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(header);
	info.header.pData = header;
	info.ext = nullptr;	// Not needed for DMG.
	info.szFile = 0;	// Not needed for DMG.
	d->romType = isRomSupported_static(&info);

	d->isValid = (d->romType >= 0);
	if (d->isValid) {
		// Save the header for later.
		// TODO: Save the RST table?
		memcpy(&d->romHeader, &header[0x100], sizeof(d->romHeader));
	} else {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Attempt to read the GBX footer.
	int64_t addr = file->size() - sizeof(GBX_Footer);
	if (addr >= (int64_t)sizeof(GBX_Footer)) {
		size = file->seekAndRead(addr, &d->gbxFooter, sizeof(d->gbxFooter));
		if (size != sizeof(d->gbxFooter)) {
			// Unable to read the footer.
			// Zero out the magic number just in case.
			d->gbxFooter.magic = 0;
		}
	}
}

/** ROM detection functions. **/

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int DMG::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < (0x100 + sizeof(DMG_RomHeader)))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Check the system name.
	const DMG_RomHeader *const romHeader =
		reinterpret_cast<const DMG_RomHeader*>(&info->header.pData[0x100]);
	if (!memcmp(romHeader->nintendo, DMGPrivate::dmg_nintendo, sizeof(DMGPrivate::dmg_nintendo))) {
		// Found a DMG ROM.
		if (romHeader->cgbflag & 0x80) {
			return DMGPrivate::ROM_CGB; // CGB supported
		}
		return DMGPrivate::ROM_DMG;
	}

	// Not supported.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *DMG::systemName(unsigned int type) const
{
	RP_D(const DMG);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// GB/GBC have the same names worldwide, so we can
	// ignore the region selection.
	// TODO: Abbreviation might be different... (Japan uses DMG/CGB?)
	static_assert(SYSNAME_TYPE_MASK == 3,
		"DMG::systemName() array index optimization needs to be updated.");
	static_assert(DMGPrivate::ROM_MAX == 2,
		"DMG::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	// Bit 2: Game Boy Color. (DMG-specific)
	static const char *const sysNames[2][4] = {
		{"Nintendo Game Boy", "Game Boy", "GB", nullptr},
		{"Nintendo Game Boy Color", "Game Boy Color", "GBC", nullptr}
	};

	// NOTE: This might return an incorrect system name if
	// d->romType is ROM_TYPE_UNKNOWN.
	return sysNames[d->romType & 1][type & SYSNAME_TYPE_MASK];
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
const char *const *DMG::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".gb",  ".sgb", ".sgb2",
		".gbc", ".cgb",

		// ROMs with GBX footer.
		".gbx",

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
const char *const *DMG::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types from FreeDesktop.org.
		"application/x-gameboy-rom",
		"application/x-gameboy-color-rom",

		nullptr
	};
	return mimeTypes;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int DMG::loadFieldData(void)
{
	RP_D(DMG);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file || !d->file->isOpen()) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->romType < 0) {
		// Unknown ROM image type.
		return -EIO;
	}

	// DMG ROM header, excluding the RST table.
	const DMG_RomHeader *const romHeader = &d->romHeader;

	// DMG ROM header:
	// - 12 regular fields.
	// - 5 fields for the GBX footer.
	d->fields->reserve(12+5);

	// Reserve at least 3 tabs:
	// DMG, GBX, GBS
	d->fields->reserveTabs(3);

	// Title and game ID
	// NOTE: These have to be handled at the same time because
	// later games take bytes away from the title field to use
	// for the CGB flag and the game ID.
	string s_title, s_gameID;
	d->getTitleAndGameID(s_title, s_gameID);
	d->fields->addField_string(C_("RomData", "Title"), s_title);
	d->fields->addField_string(C_("DMG", "Game ID"), s_gameID);

	// System
	uint32_t dmg_system = 0;
	if (romHeader->cgbflag & 0x80) {
		// Game supports CGB.
		dmg_system = DMGPrivate::DMG_SYSTEM_CGB;
		if (!(romHeader->cgbflag & 0x40)) {
			// Not CGB exclusive.
			dmg_system |= DMGPrivate::DMG_SYSTEM_DMG;
		}
	} else {
		// Game does not support CGB.
		dmg_system |= DMGPrivate::DMG_SYSTEM_DMG;
	}

	if (romHeader->old_publisher_code == 0x33 && romHeader->sgbflag==0x03) {
		// Game supports SGB.
		dmg_system |= DMGPrivate::DMG_SYSTEM_SGB;
	}

	static const char *const system_bitfield_names[] = {
		"DMG", "SGB", "CGB"
	};
	vector<string> *const v_system_bitfield_names = RomFields::strArrayToVector(
		system_bitfield_names, ARRAY_SIZE(system_bitfield_names));
	d->fields->addField_bitfield(C_("DMG", "System"),
		v_system_bitfield_names, 0, dmg_system);

	// Set the tab name based on the system.
	if (dmg_system & DMGPrivate::DMG_SYSTEM_CGB) {
		d->fields->setTabName(0, "CGB");
	} else if (dmg_system & DMGPrivate::DMG_SYSTEM_SGB) {
		d->fields->setTabName(0, "SGB");
	} else {
		d->fields->setTabName(0, "DMG");
	}

	// Entry Point
	const char *const entry_point_title = C_("DMG", "Entry Point");
	if ((romHeader->entry[0] == 0x00 ||	// NOP
	     romHeader->entry[0] == 0xF3 ||	// DI
	     romHeader->entry[0] == 0x7F ||	// LD A,A
	     romHeader->entry[0] == 0x3F) &&	// CCF
	    romHeader->entry[1] == 0xC3)	// JP nnnn
	{
		// NOP; JP nnnn
		// This is the "standard" way of doing the entry point.
		// NOTE: Some titles use a different opcode instead of NOP.
		const uint16_t entry_address = (romHeader->entry[2] | (romHeader->entry[3] << 8));
		d->fields->addField_string_numeric(entry_point_title,
			entry_address, RomFields::FB_HEX, 4, RomFields::STRF_MONOSPACE);
	} else if (romHeader->entry[0] == 0xC3) {
		// JP nnnn without a NOP.
		const uint16_t entry_address = (romHeader->entry[1] | (romHeader->entry[2] << 8));
		d->fields->addField_string_numeric(entry_point_title,
			entry_address, RomFields::FB_HEX, 4, RomFields::STRF_MONOSPACE);
	} else if (romHeader->entry[0] == 0x18) {
		// JR nnnn
		// Found in many homebrew ROMs.
		const int8_t disp = static_cast<int8_t>(romHeader->entry[1]);
		// Current PC: 0x100
		// Add displacement, plus 2.
		const uint16_t entry_address = 0x100 + disp + 2;
		d->fields->addField_string_numeric(entry_point_title,
			entry_address, RomFields::FB_HEX, 4, RomFields::STRF_MONOSPACE);
	} else {
		d->fields->addField_string_hexdump(entry_point_title,
			romHeader->entry, 4, RomFields::STRF_MONOSPACE);
	}

	// Publisher
	d->fields->addField_string(C_("RomData", "Publisher"),
		d->getPublisher());

	// Hardware
	d->fields->addField_string(C_("DMG", "Hardware"),
		DMGPrivate::dmg_hardware_names[DMGPrivate::CartType(romHeader->cart_type).hardware]);

	// Features
	static const char *const feature_bitfield_names[] = {
		NOP_C_("DMG|Features", "RAM"),
		NOP_C_("DMG|Features", "Battery"),
		NOP_C_("DMG|Features", "Timer"),
		NOP_C_("DMG|Features", "Rumble"),
	};
	vector<string> *const v_feature_bitfield_names = RomFields::strArrayToVector_i18n(
		"DMG|Features", feature_bitfield_names, ARRAY_SIZE(feature_bitfield_names));
	d->fields->addField_bitfield(C_("DMG", "Features"),
		v_feature_bitfield_names, 0, DMGPrivate::CartType(romHeader->cart_type).features);

	// ROM Size
	const char *const rom_size_title = C_("DMG", "ROM Size");
	const int rom_size = DMGPrivate::RomSize(romHeader->rom_size);
	if (rom_size < 0) {
		d->fields->addField_string(rom_size_title, C_("DMG", "Unknown"));
	} else {
		if (rom_size > 32) {
			const int banks = rom_size / 16;
			d->fields->addField_string(rom_size_title,
				rp_sprintf_p(NC_("DMG", "%1$u KiB (%2$u bank)", "%1$u KiB (%2$u banks)", banks),
					static_cast<unsigned int>(rom_size),
					static_cast<unsigned int>(banks)));
		} else {
			d->fields->addField_string(rom_size_title,
				rp_sprintf(C_("DMG", "%u KiB"), static_cast<unsigned int>(rom_size)));
		}
	}

	// RAM Size
	const char *const ram_size_title = C_("DMG", "RAM Size");
	if (romHeader->ram_size >= ARRAY_SIZE(DMGPrivate::dmg_ram_size)) {
		d->fields->addField_string(ram_size_title, C_("RomData", "Unknown"));
	} else {
		const uint8_t ram_size = DMGPrivate::dmg_ram_size[romHeader->ram_size];
		if (ram_size == 0 &&
		    DMGPrivate::CartType(romHeader->cart_type).hardware == DMGPrivate::DMG_HW_MBC2)
		{
			d->fields->addField_string(ram_size_title,
				// tr: MBC2 internal memory - Not really RAM, but whatever.
				C_("DMG", "512 x 4 bits"));
		} else if(ram_size == 0) {
			d->fields->addField_string(ram_size_title, C_("DMG", "No RAM"));
		} else {
			if (ram_size > 8) {
				const int banks = ram_size / 16;
				d->fields->addField_string(ram_size_title,
					rp_sprintf_p(NC_("DMG", "%1$u KiB (%2$u bank)", "%1$u KiB (%2$u banks)", banks),
						static_cast<unsigned int>(ram_size),
						static_cast<unsigned int>(banks)));
			} else {
				d->fields->addField_string(ram_size_title,
					rp_sprintf(C_("DMG", "%u KiB"), static_cast<unsigned int>(ram_size)));
			}
		}
	}

	// Region Code
	const char *const region_code_title = C_("RomData", "Region Code");
	switch (romHeader->region) {
		case 0:
			d->fields->addField_string(region_code_title,
				C_("Region|DMG", "Japanese"));
			break;
		case 1:
			d->fields->addField_string(region_code_title,
				C_("Region|DMG", "Non-Japanese"));
			break;
		default:
			// Invalid value.
			d->fields->addField_string(region_code_title,
				rp_sprintf(C_("DMG", "0x%02X (INVALID)"), romHeader->region));
			break;
	}

	// Revision
	d->fields->addField_string_numeric(C_("RomData", "Revision"),
		romHeader->version, RomFields::FB_DEC, 2);

	// Header checksum.
	// This is a checksum of ROM addresses 0x134-0x14D.
	// Note that romHeader is a copy of the ROM header
	// starting at 0x100, so the values are offset accordingly.
	uint8_t checksum = 0xE7; // -0x19
	const uint8_t *romHeader8 = reinterpret_cast<const uint8_t*>(romHeader);
	for (unsigned int i = 0x0034; i < 0x004D; i++){
		checksum -= romHeader8[i];
	}

	const char *const checksum_title = C_("RomData", "Checksum");
	if (checksum - romHeader->header_checksum != 0) {
		d->fields->addField_string(checksum_title,
			rp_sprintf_p(C_("DMG", "0x%1$02X (INVALID; should be 0x%2$02X)"),
				romHeader->header_checksum, checksum));
	} else {
		d->fields->addField_string(checksum_title,
			rp_sprintf(C_("DMG", "0x%02X (valid)"), checksum));
	}

	/** GBX footer. **/
	const GBX_Footer *const gbxFooter = &d->gbxFooter;
	if (gbxFooter->magic == cpu_to_be32(GBX_MAGIC)) {
		// GBX footer is present.
		d->fields->addTab("GBX");

		// GBX version.
		// TODO: Do things based on the version number?
		d->fields->addField_string(C_("DMG", "GBX Version"),
			rp_sprintf_p(C_("DMG", "%1$u.%2$u"),
				be32_to_cpu(gbxFooter->version.major),
				be32_to_cpu(gbxFooter->version.minor)));

		// Mapper.
		struct gbx_mapper_tbl_t {
			GBX_Mapper_e mapper_id;	// Host-endian
			const char *desc;
		};

		// TODO: Localization?
		// TODO: bsearch()?
		static const gbx_mapper_tbl_t gbx_mapper_tbl[] = {
			// Nintendo
			{GBX_MAPPER_ROM_ONLY,		"ROM only"},
			{GBX_MAPPER_MBC1,		"Nintendo MBC1"},
			{GBX_MAPPER_MBC2,		"Nintendo MBC2"},
			{GBX_MAPPER_MBC3,		"Nintendo MBC3"},
			{GBX_MAPPER_MBC5,		"Nintendo MBC5"},
			{GBX_MAPPER_MBC7,		"Nintendo MBC7 (tilt sensor)"},
			{GBX_MAPPER_MBC1_MULTICART,	"Nintendo MBC1 multicart"},
			{GBX_MAPPER_MMM01,		"Nintendo/Mani MMM01"},
			{GBX_MAPPER_POCKET_CAMERA,	"Nintendo Game Boy Camera"},

			// Licensed third-party
			{GBX_MAPPER_HuC1,		"Hudson HuC1"},
			{GBX_MAPPER_HuC3,		"Hudson HuC3"},
			{GBX_MAPPER_TAMA5,		"Bandai TAMA5"},

			// Unlicensed
			{GBX_MAPPER_BBD,		"BBD"},
			{GBX_MAPPER_HITEK,		"Hitek"},
			{GBX_MAPPER_SINTAX,		"Sintax"},
			{GBX_MAPPER_NT_OLDER_TYPE_1,	"NT older type 1"},
			{GBX_MAPPER_NT_OLDER_TYPE_2,	"NT older type 2"},
			{GBX_MAPPER_NT_NEWER,		"NT newer"},
			{GBX_MAPPER_LI_CHENG,		"Li Cheng"},
			{GBX_MAPPER_LAST_BIBLE,		"\"Last Bible\" multicart"},
			{GBX_MAPPER_LIEBAO,		"Liebao Technology"},

			{(GBX_Mapper_e)0, nullptr}
		};

		const char *s_mapper = nullptr;
		const uint32_t lkup = be32_to_cpu(gbxFooter->mapper_id);
		for (const gbx_mapper_tbl_t *pMapper = gbx_mapper_tbl; pMapper->mapper_id != 0; pMapper++) {
			if (pMapper->mapper_id == lkup) {
				// Found the mapper.
				s_mapper = pMapper->desc;
			}
		}

		if (s_mapper) {
			d->fields->addField_string(C_("DMG", "Mapper"), s_mapper);
		} else {
			// If the mapper ID is all printable characters, print the mapper as text.
			// Otherwise, print a hexdump.
			if (ISPRINT(gbxFooter->mapper[0]) &&
			    ISPRINT(gbxFooter->mapper[1]) &&
			    ISPRINT(gbxFooter->mapper[2]) &&
			    ISPRINT(gbxFooter->mapper[3]))
			{
				// All printable.
				d->fields->addField_string(C_("DMG", "Mapper"),
					latin1_to_utf8(gbxFooter->mapper, sizeof(gbxFooter->mapper)),
					RomFields::STRF_MONOSPACE);
			} else {
				// Not printable. Print a hexdump.
				d->fields->addField_string_hexdump(C_("DMG", "Mapper"),
					reinterpret_cast<const uint8_t*>(&gbxFooter->mapper[0]),
					sizeof(gbxFooter->mapper),
					RomFields::STRF_MONOSPACE);
			}
		}

		// Features.
		// NOTE: Same strings as the regular DMG header,
		// but the bitfield ordering is different.
		// NOTE: GBX spec says 00 = not present, 01 = present.
		// Assuming any non-zero value is present.
		uint32_t gbx_features = 0;
		if (gbxFooter->battery_flag) {
			gbx_features |= (1 << 0);
		}
		if (gbxFooter->rumble_flag) {
			gbx_features |= (1 << 1);
		}
		if (gbxFooter->timer_flag) {
			gbx_features |= (1 << 2);
		}

		static const char *const gbx_feature_bitfield_names[] = {
			NOP_C_("DMG|Features", "Battery"),
			NOP_C_("DMG|Features", "Rumble"),
			NOP_C_("DMG|Features", "Timer"),
		};
		vector<string> *const v_gbx_feature_bitfield_names = RomFields::strArrayToVector_i18n(
			"DMG|Features", gbx_feature_bitfield_names, ARRAY_SIZE(gbx_feature_bitfield_names));
		d->fields->addField_bitfield(C_("DMG", "Features"),
			v_gbx_feature_bitfield_names, 0, gbx_features);

		// ROM size, in bytes.
		// TODO: Use formatFileSize() instead?
		d->fields->addField_string(C_("DMG", "ROM Size"),
			d->formatROMSizeKiB(be32_to_cpu(gbxFooter->rom_size)));

		// RAM size, in bytes.
		// TODO: Use formatFileSize() instead?
		d->fields->addField_string(C_("DMG", "RAM Size"),
			d->formatROMSizeKiB(be32_to_cpu(gbxFooter->ram_size)));
	}

	/** GBS **/
	// Check for GBS.
	// NOTE: Loaded on demand, since GBS isn't used for metadata at the moment.
	// TODO: Maybe it should be?
	uint8_t gbs_jmp[3];
	size_t size = d->file->seekAndRead(0, gbs_jmp, sizeof(gbs_jmp));
	if (size == sizeof(gbs_jmp) && gbs_jmp[0] == 0xC3) {
		// Read the jump address.
		// GBS header is at the jump address minus sizeof(GBS_Header).
		uint16_t jp_addr = (gbs_jmp[2] << 8) | gbs_jmp[1];
		if (jp_addr >= sizeof(GBS_Header)) {
			jp_addr -= sizeof(GBS_Header);
			// Read the GBS magic number.
			uint32_t gbs_magic;
			size = d->file->seekAndRead(jp_addr, &gbs_magic, sizeof(gbs_magic));
			if (size == sizeof(gbs_magic) && gbs_magic == cpu_to_be32(GBS_MAGIC)) {
				// Found the GBS magic number.
				// Open the GBS.
				const int64_t fileSize = d->file->size();
				DiscReader *const reader = new DiscReader(d->file, 0, fileSize);
				if (reader->isOpen()) {
					// Create a PartitionFile.
					const int64_t length = fileSize - jp_addr;
					PartitionFile *const ptFile = new PartitionFile(reader, jp_addr, length);
					if (ptFile->isOpen()) {
						// Open the GBS.
						GBS *const gbs = new GBS(ptFile);
						if (gbs->isOpen()) {
							// Add the fields.
							const RomFields *const gbsFields = gbs->fields();
							assert(gbsFields != nullptr);
							assert(!gbsFields->empty());
							if (gbsFields && !gbsFields->empty()) {
								d->fields->addFields_romFields(gbsFields,
									RomFields::TabOffset_AddTabs);
							}
						}
						gbs->unref();
					}
					ptFile->unref();
				}
				delete reader;
			}
		}
	}

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int DMG::loadMetaData(void)
{
	RP_D(DMG);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->romType < 0) {
		// Unknown ROM image type.
		return -EIO;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(1);	// Maximum of 1 metadata property.

	// DMG ROM header
	//const DMG_RomHeader *const romHeader = &d->romHeader;

	// Title
	// NOTE: We don't actually need the game ID right now,
	// but the function retrieves both at the same time.
	string s_title, s_gameID;
	d->getTitleAndGameID(s_title, s_gameID);
	d->metaData->addMetaData_string(Property::Title,
		s_title, RomMetaData::STRF_TRIM_END);

	// Publisher
	d->metaData->addMetaData_string(Property::Publisher,
		d->getPublisher(), RomMetaData::STRF_TRIM_END);

	// Finished reading the metadata.
	return static_cast<int>(d->metaData->count());
}

}
