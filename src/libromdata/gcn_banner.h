/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * gcn_structs.h: Nintendo GameCube and Wii banner structures.             *
 *                                                                         *
 * Copyright (c) 2016 by David Korth.                                      *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published by the   *
 * Free Software Foundation; either version 2 of the License, or (at your  *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.           *
 ***************************************************************************/

/**
 * Reference:
 * - http://hitmen.c02.at/files/yagcd/yagcd/chap14.html
 */

#ifndef __ROMPROPERTIES_LIBROMDATA_GCN_BANNER_H__
#define __ROMPROPERTIES_LIBROMDATA_GCN_BANNER_H__

#include <stdint.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Magic numbers.
#define BANNER_MAGIC_BNR1 0x424E5231	/* 'BNR1' */
#define BANNER_MAGIC_BNR2 0x424E5232	/* 'BNR2' */

// Banner size.
#define BANNER_IMAGE_W 96
#define BANNER_IMAGE_H 32

// NOTE: Strings are encoded in either cp1252 or Shift-JIS,
// depending on the game region.

// Banner comment.
#pragma pack(1)
#define GCN_BANNER_COMMENT_SIZE 0x140
typedef struct PACKED _banner_comment_t
{
	char gamename[0x20];
	char company[0x20];
	char gamename_full[0x40];
	char company_full[0x40];
	char gamedesc[0x80];
} banner_comment_t;

// BNR1
#pragma pack(1)
#define GCN_BANNER_BNR1_SIZE (0x1820 + GCN_BANNER_COMMENT_SIZE)
typedef struct PACKED _banner_bnr1_t
{
	uint32_t magic;			// BANNER_MAGIC_BNR1
	uint8_t reserved[0x1C];
	uint16_t banner[0x1800>>1];	// Banner image. (96x32, RGB5A3)
	banner_comment_t comment;
} banner_bnr1_t;
#pragma pack()

// BNR2
#pragma pack(1)
#define GCN_BANNER_BNR2_SIZE (0x1820 + (GCN_BANNER_COMMENT_SIZE * 6))
typedef struct PACKED _banner_bnr2_t
{
	uint32_t magic;			// BANNER_MAGIC_BNR2
	uint8_t reserved[0x1C];
	uint16_t banner[0x1800>>1];	// Banner image. (96x32, RGB5A3)
	banner_comment_t comments[6];
} banner_bnr2_t;
#pragma pack()

// BNR2 languages. (Maps to GameCube language setting.)
typedef enum {
	GCN_PAL_LANG_ENGLISH	= 0,
	GCN_PAL_LANG_GERMAN	= 1,
	GCN_PAL_LANG_FRENCH	= 2,
	GCN_PAL_LANG_SPANISH	= 3,
	GCN_PAL_LANG_ITALIAN	= 4,
	GCN_PAL_LANG_DUTCH	= 5,
} GCN_PAL_Language;

/**
 * WIBN (Wii Banner)
 * Reference: http://wiibrew.org/wiki/Savegame_Files
 * NOTE: This may be located at one of two places:
 * - 0x0000: banner.bin extracted via SaveGame Manager GX
 * - 0x0020: Savegame extracted via Wii System Menu
 */

// Magic numbers.
#define BANNER_WIBN_MAGIC		0x5749424E	/* 'WIBN' */
#define BANNER_WIBN_ADDRESS_RAW		0x0000		/* banner.bin from SaveGame Manager GX */
#define BANNER_WIBN_ADDRESS_ENCRYPTED	0x0020		/* extracted from Wii System Menu */

// Flags.
#define BANNER_WIBN_FLAGS_NOCOPY	0x01
#define BANNER_WIBN_FLAGS_ICON_BOUNCE	0x10

// Banner size.
#define BANNER_WIBN_IMAGE_W 192
#define BANNER_WIBN_IMAGE_H 64

// Icon size.
#define BANNER_WIBN_ICON_W 48
#define BANNER_WIBN_ICON_H 48

// Struct size.
#define BANNER_WIBN_ICON_SIZE 0x1200
#define BANNER_WIBN_STRUCT_SIZE 24736
#define BANNER_WIBN_STRUCT_SIZE_ICONS(icons) \
	(BANNER_WIBN_STRUCT_SIZE + ((icons)*BANNER_WIBN_ICON_SIZE))

#pragma pack(1)
typedef struct PACKED _banner_wibn_t
{
	uint32_t magic;			// BANNER_MAGIC_WIBN
	uint32_t flags;
	uint16_t iconDelay;		// Similar to GCN.
	uint8_t reserved[22];
	uint16_t gameTitle[32];		// Game title. (UTF-16 BE)
	uint16_t gameSubTitle[32];	// Game subtitle. (UTF-16 BE)
	uint16_t banner[0x6000>>1];	// Banner image. (192x64, RGB5A3)
	uint16_t icon[8][0x1200>>1];	// Icons. (48x48, RGB5A3) [optional]
} banner_wibn_t;
#pragma pack()

#ifdef __cplusplus
}
#endif

#endif /* __ROMPROPERTIES_LIBROMDATA_GCN_BANNER_H__ */
