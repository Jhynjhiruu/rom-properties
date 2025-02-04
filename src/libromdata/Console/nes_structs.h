/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * nes_structs.h: Nintendo Entertainment System/Famicom data structures.   *
 *                                                                         *
 * Copyright (c) 2016-2017 by David Korth.                                 *
 * Copyright (c) 2016-2017 by Egor.                                        *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

/**
 * References:
 * - https://wiki.nesdev.com/w/index.php/INES
 * - https://wiki.nesdev.com/w/index.php/NES_2.0
 * - https://wiki.nesdev.com/w/index.php/Family_Computer_Disk_System
 */

#ifndef __ROMPROPERTIES_LIBROMDATA_NES_STRUCTS_H__
#define __ROMPROPERTIES_LIBROMDATA_NES_STRUCTS_H__

#include "librpbase/common.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(1)

// Bank sizes for iNES.
#define INES_PRG_BANK_SIZE 16384
#define INES_CHR_BANK_SIZE 8192
#define INES_PRG_RAM_BANK_SIZE 8192

// Bank sizes for TNES.
#define TNES_PRG_BANK_SIZE 8192
#define TNES_CHR_BANK_SIZE 8192

/**
 * iNES ROM header.
 * This matches the ROM header format exactly.
 * References:
 * - https://wiki.nesdev.com/w/index.php/INES
 * - https://wiki.nesdev.com/w/index.php/NES_2.0
 *
 * All fields are in little-endian,
 * except for the magic number.
 *
 * NOTE: Strings are NOT null-terminated!
 */
#define INES_MAGIC		'NES\x1A'
#define INES_MAGIC_WIIU_VC	'NES\x00'
typedef struct PACKED _INES_RomHeader {
	uint32_t magic;		// [0x000] 'NES\x1A' (big-endian)
	uint8_t prg_banks;	// [0x004] # of 16 KB PRG ROM banks.
	uint8_t chr_banks;	// [0x005]# of 8 KB CHR ROM banks.

	// Mapper values. Each byte has one
	// nybble, plus HW information.
	uint8_t mapper_lo;	// [0x006]
	uint8_t mapper_hi;	// [0x007]

	union {
		struct {
			uint8_t prg_ram_size;	// 8 KB units
			uint8_t tv_mode;
			// TODO: Byte 10?
		} ines;
		struct {
			uint8_t mapper_hi2;
			uint8_t prg_banks_hi;
			uint8_t prg_ram_size;	// logarithmic
			uint8_t vram_size;	// logarithmic
			uint8_t tv_mode;	// 12
			uint8_t vs_hw;
		} nes2;
	};

	uint8_t reserved[2];
} INES_RomHeader;
ASSERT_STRUCT(INES_RomHeader, 16);

// mapper_lo flags.
typedef enum {
	// Mirroring.
	INES_F6_MIRROR_HORI = 0,
	INES_F6_MIRROR_VERT = (1 << 0),
	INES_F6_MIRROR_FOUR = (1 << 3),

	// Battery/trainer.
	INES_F6_BATTERY = (1 << 1),
	INES_F6_TRAINER = (1 << 2),

	// Mapper low nybble.
	INES_F6_MAPPER_MASK = 0xF0,
	INES_F6_MAPPER_SHIFT = 4,
} INES_Mapper_LO;

// mapper_hi flags.
typedef enum {
	// Hardware.
	INES_F7_SYSTEM_VS	= (1 << 0),
	INES_F7_SYSTEM_PC10	= (1 << 1),
	INES_F7_SYSTEM_MASK	= (INES_F7_SYSTEM_VS | INES_F7_SYSTEM_PC10),

	// NES 2.0 identification.
	INES_F7_NES2_MASK = (1 << 3) | (1 << 2),
	INES_F7_NES2_INES_VAL = 0,
	INES_F7_NES2_NES2_VAL = (1 << 3),

	// Mapper high nybble.
	INES_F7_MAPPER_MASK = 0xF0,
	INES_F7_MAPPER_SHIFT = 4,
} INES_Mapper_HI;

// NES 2.0 stuff
// Not gonna make enums for those:
// Byte 8 - Mapper variant
//   top nibble = submapper, bottom nibble = mapper plane
// Byte 9 - Rom size upper bits
//   top = CROM, bottom = PROM
// Byte 10 - pram
//   top = battery pram, bottom = normal pram
// Byte 11 - cram
//   top = battery cram, bottom = normal cram
// Byte 13 - vs unisystem
//   top = vs mode, bottom = ppu version
typedef enum {
	NES2_F12_NTSC = 0,
	NES2_F12_PAL = (1 << 0),
	NES2_F12_DUAL = (1 << 1),
	NES2_F12_REGION = (1 << 1) | (1 << 0),
} NES2_TV_Mode;

/**
 * Internal NES footer.
 * Located at the last 32 bytes of the last PRG bank in some ROMs.
 *
 * References:
 * - http://forums.no-intro.org/viewtopic.php?f=2&t=445
 * - https://github.com/GerbilSoft/rom-properties/issues/116
 *
 * TODO: Add enums?
 */
typedef struct PACKED _NES_IntFooter {
	char name[16];		// [0x000] Name. (May be right-aligned with 0xFF filler bytes.)
	uint16_t prg_checksum;	// [0x010] PRG checksum.
	uint16_t chr_checksum;	// [0x012] CHR checksum.
	uint8_t rom_size;	// [0x014] ROM sizes. Upper nybble == PRG, lower nybble == CHR
				//         2=32KB; 3=128KB; 4=256KB; 5=512KB
	uint8_t board_info;	// [0x015] Board information.
				//         Upper nybble: Mirroring (1=vertical, 0=horizontal)
				//         Lower nybble: Mapper (4=MMCx)
	uint8_t unknown1[2];	// [0x016]
	uint8_t publisher_code;	// [0x018] Old publisher code.
	uint8_t unknown2;	// [0x019]
	uint16_t nmi_vector;	// [0x01A] NMI vector.
	uint16_t reset_vector;	// [0x01C] Reset vector.
	uint16_t irq_vector;	// [0x01E] IRQ vector.
} NES_IntFooter;
ASSERT_STRUCT(NES_IntFooter, 32);

/**
 * TNES ROM header.
 * Used with Nintendo 3DS Virtual Console games.
 *
 * All fields are in little-endian,
 * except for the magic number.
 */
#define TNES_MAGIC 'TNES'
typedef struct PACKED _TNES_RomHeader {
	uint32_t magic;		// [0x000] 'TNES' (big-endian)
	uint8_t mapper;		// [0x004]
	uint8_t prg_banks;	// [0x005] # of 8 KB PRG ROM banks.
	uint8_t chr_banks;	// [0x006] # of 8 KB CHR ROM banks.
	uint8_t wram;		// [0x007] 00 == no; 01 == yes
	uint8_t mirroring;	// [0x008] 00 == none; 01 == horizontal; 02 == vertical
	uint8_t vram;		// [0x009] 00 == no; 01 == yes
	uint8_t reserved[6];	// [0x00A]
} TNES_RomHeader;
ASSERT_STRUCT(TNES_RomHeader, 16);

/**
 * TNES mappers.
 */
typedef enum {
	TNES_MAPPER_NROM	= 0,
	TNES_MAPPER_SxROM	= 1,
	TNES_MAPPER_PxROM	= 2,
	TNES_MAPPER_TxROM	= 3,
	TNES_MAPPER_FxROM	= 4,
	TNES_MAPPER_ExROM	= 5,
	TNES_MAPPER_UxROM	= 6,
	TNES_MAPPER_CNROM	= 7,
	TNES_MAPPER_AxROM	= 9,

	TNES_MAPPER_FDS		= 100,
} TNES_Mapper;

/**
 * TNES mirroring.
 */
typedef enum {
	TNES_MIRRORING_PROGRAMMABLE	= 0,	// Programmable
	TNES_MIRRORING_HORIZONTAL	= 1,	// Horizontal
	TNES_MIRRORING_VERTICAL		= 2,	// Vertical
} TNES_Mirroring;

/**
 * 3-byte BCD date stamp.
 */
typedef struct PACKED _FDS_BCD_DateStamp {
	uint8_t year;	// Add 1925 to this.
	uint8_t mon;	// 1-12
	uint8_t mday;	// 1-31
} FDS_BCD_DateStamp;
ASSERT_STRUCT(FDS_BCD_DateStamp, 3);

/**
 * Famicom Disk System header.
 */
typedef struct PACKED _FDS_DiskHeader {
	uint8_t block_code;	// 0x01
	uint8_t magic[14];	// "*NINTENDO-HVC*"
	uint8_t publisher_code;	// Old publisher code format
	char game_id[3];	// 3-character game ID.
	char game_type;		// Game type. (See FDS_Game_Type.)
	uint8_t revision;	// Revision.
	uint8_t side_number;	// Side number.
	uint8_t disk_number;	// Disk number.
	uint8_t disk_type;	// Disk type. (See FDS_Disk_Type.)
	uint8_t unknown1;
	uint8_t boot_read_file_code;	// File number to read on startup.
	uint8_t unknown2[5];		// 0xFF 0xFF 0xFF 0xFF 0xFF
	FDS_BCD_DateStamp mfr_date;	// Manufacturing date.
	uint8_t country_code;		// Country code. (0x49 == Japan)
	uint8_t unknown3[9];
	FDS_BCD_DateStamp rw_date;	// "Rewritten disk" date.
	uint8_t unknown4[2];
	uint16_t disk_writer_serial;	// Disk Writer serial number.
	uint8_t unknown5;
	uint8_t disk_rewrite_count;	// Stored in BCD format. $00 = original
	uint8_t actual_disk_side;
	uint8_t unknown6;
	uint8_t price;
	uint16_t crc;
} FDS_DiskHeader;
ASSERT_STRUCT(FDS_DiskHeader, 58);

typedef enum {
	FDS_GTYPE_NORMAL	= ' ',
	FDS_GTYPE_EVENT		= 'E',
	FDS_GTYPE_REDUCTION	= 'R',	// Sale!!!
} FDS_Game_Type;

typedef enum {
	FDS_DTYPE_FMC	= 0,	// FMC ("normal card")
	FDS_DTYPE_FSC	= 1,	// FSC ("card with shutter")
} FDS_Disk_type;

/**
 * fwNES FDS header.
 * If present, it's placed before the regular FDS header.
 *
 * All fields are in little-endian,
 * except for the magic number.
 */
#define fwNES_MAGIC 'FDS\x1A'
typedef struct PACKED _FDS_DiskHeader_fwNES {
	uint32_t magic;		// [0x000] 'FDS\x1A' (big-endian)
	uint8_t disk_sides;	// [0x004] Number of disk sides.
	uint8_t reserved[11];	// [0x005] Zero filled.
} FDS_DiskHeader_fwNES;
ASSERT_STRUCT(FDS_DiskHeader_fwNES, 16);

#pragma pack()

#ifdef __cplusplus
}
#endif

#endif /* __ROMPROPERTIES_LIBROMDATA_NES_STRUCTS_H__ */
