/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * NintendoDS.hpp: Nintendo DS(i) ROM reader.                              *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "librpbase/config.librpbase.h"

#include "NintendoDS.hpp"
#include "librpbase/RomData_p.hpp"

#include "nds_structs.h"
#include "data/NintendoPublishers.hpp"
#include "data/NintendoLanguage.hpp"

#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"

#include "librpbase/img/rp_image.hpp"
#include "librpbase/img/ImageDecoder.hpp"
#include "librpbase/img/IconAnimData.hpp"

#include "libi18n/i18n.h"
using namespace LibRpBase;

// C includes. (C++ namespace)
#include "librpbase/ctypex.h"
#include <cassert>
#include <cerrno>
#include <cstring>

// C++ includes.
#include <array>
#include <string>
#include <vector>
using std::array;
using std::string;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(NintendoDS)
ROMDATA_IMPL_IMG(NintendoDS)

class NintendoDSPrivate : public RomDataPrivate
{
	public:
		NintendoDSPrivate(NintendoDS *q, IRpFile *file, bool cia);
		virtual ~NintendoDSPrivate();

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(NintendoDSPrivate)

	public:
		// Animated icon data.
		// This class owns all of the icons in here, so we
		// must delete all of them.
		IconAnimData *iconAnimData;

		// Pointer to the first frame in iconAnimData.
		// Used when showing a static icon.
		const rp_image *icon_first_frame;

	public:
		/** RomFields **/

		// Hardware type. (RFT_BITFIELD)
		enum NDS_HWType {
			DS_HW_DS	= (1 << 0),
			DS_HW_DSi	= (1 << 1),
		};

		// DS region. (RFT_BITFIELD)
		enum NDS_Region {
			NDS_REGION_FREE		= (1 << 0),
			NDS_REGION_SKOREA	= (1 << 1),
			NDS_REGION_CHINA	= (1 << 2),
		};

	public:
		// ROM type.
		enum RomType {
			ROM_UNKNOWN	= -1,	// Unknown ROM type.
			ROM_NDS		= 0,	// Nintendo DS ROM.
			ROM_NDS_SLOT2	= 1,	// Nintendo DS ROM. (Slot-2)
			ROM_DSi_ENH	= 2,	// Nintendo DSi-enhanced ROM.
			ROM_DSi_ONLY	= 3,	// Nintendo DSi-only ROM.
		};

		// ROM type.
		int romType;

		// ROM header.
		// NOTE: Must be byteswapped on access.
		NDS_RomHeader romHeader;

		// Icon/title data from the ROM header.
		// NOTE: Must be byteswapped on access.
		NDS_IconTitleData nds_icon_title;
		bool nds_icon_title_loaded;

		// If true, this is an SRL in a 3DS CIA.
		// Some fields shouldn't be displayed.
		bool cia;

		/**
		 * Load the icon/title data.
		 * @return 0 on success; negative POSIX error code on error.
		 */
		int loadIconTitleData(void);

		/**
		 * Load the ROM image's icon.
		 * @return Icon, or nullptr on error.
		 */
		const rp_image *loadIcon(void);

		/**
		 * Get the title index.
		 * The title that most closely matches the
		 * host system language will be selected.
		 * @return Title index, or -1 on error.
		 */
		int getTitleIndex(void) const;

		/**
		 * Check the NDS security data and segment3 area.
		 *
		 * $1000-$3FFF is normally unreadable on hardware, so this
		 * area is usually blank in dumped ROMs. However, the official
		 * SDK has security data in here, which is required for official
		 * flash carts and mask ROM hardware, so DSiWare and Wii U VC
		 * SRLs will have non-zero data here.
		 *
		 * @return True if the security data area has non-zero data; false if not.
		 */
		bool checkNDSSecurityDataArea(void);

		/**
		 * Check the NDS Secure Area type.
		 * @return Secure area type.
		 */
		const char *checkNDSSecureArea(void);

		/**
		 * Convert a Nintendo DS(i) region value to a GameTDB region code.
		 * @param ndsRegion Nintendo DS region.
		 * @param dsiRegion Nintendo DSi region.
		 * @param idRegion Game ID region.
		 *
		 * NOTE: Mulitple GameTDB region codes may be returned, including:
		 * - User-specified fallback region. [TODO]
		 * - General fallback region.
		 *
		 * @return GameTDB region code(s), or empty vector if the region value is invalid.
		 */
		static vector<const char*> ndsRegionToGameTDB(
			uint8_t ndsRegion, uint32_t dsiRegion, char idRegion);

		/**
		 * Get the DSi flags string vector.
		 * @return DSi flags string vector.
		 */
		static vector<vector<string> > *getDSiFlagsStringVector(void);
};

/** NintendoDSPrivate **/

NintendoDSPrivate::NintendoDSPrivate(NintendoDS *q, IRpFile *file, bool cia)
	: super(q, file)
	, iconAnimData(nullptr)
	, icon_first_frame(nullptr)
	, romType(ROM_UNKNOWN)
	, nds_icon_title_loaded(false)
	, cia(cia)
{
	// Clear the various structs.
	memset(&romHeader, 0, sizeof(romHeader));
	memset(&nds_icon_title, 0, sizeof(nds_icon_title));
}

NintendoDSPrivate::~NintendoDSPrivate()
{
	if (iconAnimData) {
		for (int i = iconAnimData->count-1; i >= 0; i--) {
			delete iconAnimData->frames[i];
		}
		delete iconAnimData;
	}
}

/**
 * Load the icon/title data.
 * @return 0 on success; negative POSIX error code on error.
 */
int NintendoDSPrivate::loadIconTitleData(void)
{
	assert(this->file != nullptr);

	if (nds_icon_title_loaded) {
		// Icon/title data is already loaded.
		return 0;
	}

	// Get the address of the icon/title information.
	const uint32_t icon_offset = le32_to_cpu(romHeader.icon_offset);

	// Read the icon/title data.
	size_t size = this->file->seekAndRead(icon_offset, &nds_icon_title, sizeof(nds_icon_title));

	// Make sure we have the correct size based on the version.
	if (size < sizeof(nds_icon_title.version)) {
		// Couldn't even load the version number...
		return -EIO;
	}

	unsigned int req_size;
	switch (le16_to_cpu(nds_icon_title.version)) {
		case NDS_ICON_VERSION_ORIGINAL:
			req_size = NDS_ICON_SIZE_ORIGINAL;
			break;
		case NDS_ICON_VERSION_ZH:
			req_size = NDS_ICON_SIZE_ZH;
			break;
		case NDS_ICON_VERSION_ZH_KO:
			req_size = NDS_ICON_SIZE_ZH_KO;
			break;
		case NDS_ICON_VERSION_DSi:
			req_size = NDS_ICON_SIZE_DSi;
			break;
		default:
			// Invalid version number.
			return -EIO;
	}

	if (size < req_size) {
		// Error reading the icon data.
		return -EIO;
	}

	// Icon data loaded.
	nds_icon_title_loaded = true;
	return 0;
}

/**
 * Load the ROM image's icon.
 * @return Icon, or nullptr on error.
 */
const rp_image *NintendoDSPrivate::loadIcon(void)
{
	if (icon_first_frame) {
		// Icon has already been loaded.
		return icon_first_frame;
	} else if (!this->file || !this->isValid) {
		// Can't load the icon.
		return nullptr;
	}

	// Attempt to load the icon/title data.
	int ret = loadIconTitleData();
	if (ret != 0) {
		// Error loading the icon/title data.
		return nullptr;
	}

	// Load the icon data.
	// TODO: Only read the first frame unless specifically requested?
	this->iconAnimData = new IconAnimData();
	iconAnimData->count = 0;

	// Check if a DSi animated icon is present.
	// TODO: Some configuration option to return the standard
	// NDS icon for the standard icon instead of the first frame
	// of the animated DSi icon? (Except for DSiWare...)
	if (le16_to_cpu(nds_icon_title.version) < NDS_ICON_VERSION_DSi ||
	    (nds_icon_title.dsi_icon_seq[0] & cpu_to_le16(0xFF)) == 0)
	{
		// Either this isn't a DSi icon/title struct (pre-v0103),
		// or the animated icon sequence is invalid.

		// Convert the NDS icon to rp_image.
		iconAnimData->frames[0] = ImageDecoder::fromNDS_CI4(32, 32,
			nds_icon_title.icon_data, sizeof(nds_icon_title.icon_data),
			nds_icon_title.icon_pal,  sizeof(nds_icon_title.icon_pal));
		iconAnimData->count = 1;
	} else {
		// Animated icon is present.

		// Which bitmaps are used?
		array<bool, IconAnimData::MAX_FRAMES> bmp_used;
		bmp_used.fill(false);

		// Parse the icon sequence.
		int seq_idx;
		for (seq_idx = 0; seq_idx < ARRAY_SIZE(nds_icon_title.dsi_icon_seq); seq_idx++) {
			const uint16_t seq = le16_to_cpu(nds_icon_title.dsi_icon_seq[seq_idx]);
			const int delay = (seq & 0xFF);
			if (delay == 0) {
				// End of sequence.
				break;
			}

			// Token format: (bits)
			// - 15:    V flip (1=yes, 0=no) [TODO]
			// - 14:    H flip (1=yes, 0=no) [TODO]
			// - 13-11: Palette index.
			// - 10-8:  Bitmap index.
			// - 7-0:   Frame duration. (units of 60 Hz)

			// NOTE: IconAnimData doesn't support arbitrary combinations
			// of palette and bitmap. As a workaround, we'll make each
			// combination a unique bitmap, which means we have a maximum
			// of 64 bitmaps.
			uint8_t bmp_pal_idx = ((seq >> 8) & 0x3F);
			bmp_used[bmp_pal_idx] = true;
			iconAnimData->seq_index[seq_idx] = bmp_pal_idx;
			iconAnimData->delays[seq_idx].numer = static_cast<uint16_t>(delay);
			iconAnimData->delays[seq_idx].denom = 60;
			iconAnimData->delays[seq_idx].ms = delay * 1000 / 60;
		}
		iconAnimData->seq_count = seq_idx;

		// Convert the required bitmaps.
		for (int i = 0; i < static_cast<int>(bmp_used.size()); i++) {
			if (bmp_used[i]) {
				iconAnimData->count = i + 1;

				const uint8_t bmp = (i & 7);
				const uint8_t pal = (i >> 3) & 7;
				iconAnimData->frames[i] = ImageDecoder::fromNDS_CI4(32, 32,
					nds_icon_title.dsi_icon_data[bmp],
					sizeof(nds_icon_title.dsi_icon_data[bmp]),
					nds_icon_title.dsi_icon_pal[pal],
					sizeof(nds_icon_title.dsi_icon_pal[pal]));
			}
		}
	}

	// NOTE: We're not deleting iconAnimData even if we only have
	// a single icon because iconAnimData() will call loadIcon()
	// if iconAnimData is nullptr.

	// Return a pointer to the first frame.
	icon_first_frame = iconAnimData->frames[iconAnimData->seq_index[0]];
	return icon_first_frame;
}

/**
 * Get the title index.
 * The title that most closely matches the
 * host system language will be selected.
 * @return Title index, or -1 on error.
 */
int NintendoDSPrivate::getTitleIndex(void) const
{
	if (!nds_icon_title_loaded) {
		// Attempt to load the icon/title data.
		if (const_cast<NintendoDSPrivate*>(this)->loadIconTitleData() != 0) {
			// Error loading the icon/title data.
			return -1;
		}

		// Make sure it was actually loaded.
		if (!nds_icon_title_loaded) {
			// Icon/title data was not loaded.
			return -1;
		}
	}

	// Version number check is required for ZH and KO.
	const uint16_t version = le16_to_cpu(nds_icon_title.version);
	int lang = NintendoLanguage::getNDSLanguage(version);

	// Check that the field is valid.
	if (nds_icon_title.title[lang][0] == cpu_to_le16(0)) {
		// Not valid. Check English.
		if (nds_icon_title.title[NDS_LANG_ENGLISH][0] != cpu_to_le16(0)) {
			// English is valid.
			lang = NDS_LANG_ENGLISH;
		} else {
			// Not valid. Check Japanese.
			if (nds_icon_title.title[NDS_LANG_JAPANESE][0] != cpu_to_le16(0)) {
				// Japanese is valid.
				lang = NDS_LANG_JAPANESE;
			} else {
				// Not valid...
				// TODO: Check other languages?
				lang = -1;
			}
		}
	}

	return lang;
}

/**
 * Check the NDS security data and segment3 area.
 *
 * $1000-$3FFF is normally unreadable on hardware, so this
 * area is usually blank in dumped ROMs. However, the official
 * SDK has security data in here, which is required for official
 * flash carts and mask ROM hardware, so DSiWare and Wii U VC
 * SRLs will have non-zero data here.
 *
 * @return True if the security data area has non-zero data; false if not.
 */
bool NintendoDSPrivate::checkNDSSecurityDataArea(void)
{
	// Make sure 0x1000-0x3FFF is blank.
	// NOTE: ndstool checks 0x0200-0x0FFF, but this may
	// contain extra data for DSi-enhanced ROMs, or even
	// for regular DS games released after the DSi.
	uintptr_t blank_area[0x3000/sizeof(uintptr_t)];
	size_t size = file->seekAndRead(0x1000, blank_area, sizeof(blank_area));
	if (size != sizeof(blank_area)) {
		// Seek and/or read error.
		return false;
	}

	const uintptr_t *const end = &blank_area[ARRAY_SIZE(blank_area)-1];
	for (const uintptr_t *p = blank_area; p < end; p += 2) {
		if (p[0] != 0 || p[1] != 0) {
			// Not zero. This isn't a dumped ROM.
			return true;
		}
	}

	// The Mask ROM area is zero. This is a dumped ROM.
	return false;
}

/**
 * Check the NDS Secure Area type.
 * @return Secure area type, or nullptr if unknown.
 */
const char *NintendoDSPrivate::checkNDSSecureArea(void)
{
	// Read the start of the Secure Area.
	// NOTE: We only need to check the first two DWORDs, but
	// we're reading the first four because CIAReader only
	// supports multiples of 16 bytes right now.
	uint32_t secure_area[4];
	size_t size = file->seekAndRead(0x4000, secure_area, sizeof(secure_area));
	if (size != sizeof(secure_area)) {
		// Seek and/or read error.
		return nullptr;
	}

	// Reference: https://github.com/devkitPro/ndstool/blob/master/source/header.cpp#L39

	const char *secType = nullptr;
	//bool needs_encryption = false;	// TODO
	if (le32_to_cpu(romHeader.arm9.rom_offset) < 0x4000) {
		// ARM9 secure area is not present.
		// This is only valid for homebrew.
		secType = C_("NintendoDS|SecureArea", "Homebrew");
	} else if (secure_area[0] == cpu_to_le32(0x00000000) && secure_area[1] == cpu_to_le32(0x00000000)) {
		// Secure area is empty. (DS Download Play)
		secType = C_("NintendoDS|SecureArea", "Multiboot");
	} else if (secure_area[0] == cpu_to_le32(0xE7FFDEFF) && secure_area[1] == cpu_to_le32(0xE7FFDEFF)) {
		// Secure area is decrypted.
		// Probably dumped using wooddumper or Decrypt9WIP.
		secType = C_("NintendoDS|SecureArea", "Decrypted");
		//needs_encryption = true;	// CRC requires encryption.
	} else {
		// Secure area is encrypted.
		secType = C_("NintendoDS|SecureArea", "Encrypted");
	}

	// TODO: Verify the CRC?
	// For decrypted ROMs, this requires re-encrypting the secure area.
	return secType;
}

/**
 * Convert a Nintendo DS(i) region value to a GameTDB region code.
 * @param ndsRegion Nintendo DS region.
 * @param dsiRegion Nintendo DSi region.
 * @param idRegion Game ID region.
 *
 * NOTE: Mulitple GameTDB region codes may be returned, including:
 * - User-specified fallback region. [TODO]
 * - General fallback region.
 *
 * @return GameTDB region code(s), or empty vector if the region value is invalid.
 */
vector<const char*> NintendoDSPrivate::ndsRegionToGameTDB(
	uint8_t ndsRegion, uint32_t dsiRegion, char idRegion)
{
	/**
	 * There are up to three region codes for Nintendo DS games:
	 * - Game ID
	 * - NDS region (China/Korea only)
	 * - DSi region (DSi-enhanced/exclusive games only)
	 *
	 * Nintendo DS does not have region lock outside of
	 * China. (The Korea value isn't actually used.)
	 *
	 * Nintendo DSi does have region lock, but only for
	 * DSi-enhanced/exclusive games.
	 *
	 * If a DSi-enhanced/exclusive game has a single region
	 * code value set, that region will be displayed.
	 *
	 * If a DS-only game has China or Korea set, that region
	 * will be displayed.
	 *
	 * The game ID will always be used as a fallback.
	 *
	 * Game ID reference:
	 * - https://github.com/dolphin-emu/dolphin/blob/4c9c4568460df91a38d40ac3071d7646230a8d0f/Source/Core/DiscIO/Enums.cpp
	 */
	vector<const char*> ret;

	int fallback_region = 0;
	switch (dsiRegion) {
		case DSi_REGION_JAPAN:
			ret.push_back("JA");
			return ret;
		case DSi_REGION_USA:
			ret.push_back("US");
			return ret;
		case DSi_REGION_EUROPE:
		case DSi_REGION_EUROPE | DSi_REGION_AUSTRALIA:
			// Process the game ID and use "EN" as a fallback.
			fallback_region = 1;
			break;
		case DSi_REGION_AUSTRALIA:
			// Process the game ID and use "AU","EN" as fallbacks.
			fallback_region = 2;
			break;
		case DSi_REGION_CHINA:
			ret.push_back("ZHCN");
			ret.push_back("JA");
			ret.push_back("EN");
			return ret;
		case DSi_REGION_SKOREA:
			ret.push_back("KO");
			ret.push_back("JA");
			ret.push_back("EN");
			return ret;
		case 0:
		default:
			// No DSi region, or unsupported DSi region.
			break;
	}

	// TODO: If multiple DSi region bits are set,
	// compare each to the host system region.

	// Check for China/Korea.
	if (ndsRegion & NDS_REGION_CHINA) {
		ret.push_back("ZHCN");
		ret.push_back("JA");
		ret.push_back("EN");
		return ret;
	} else if (ndsRegion & NDS_REGION_SKOREA) {
		ret.push_back("KO");
		ret.push_back("JA");
		ret.push_back("EN");
		return ret;
	}

	// Check for region-specific game IDs.
	switch (idRegion) {
		case 'E':	// USA
			ret.push_back("US");
			break;
		case 'J':	// Japan
			ret.push_back("JA");
			break;
		case 'O':
			// TODO: US/EU.
			// Compare to host system region.
			// For now, assuming US.
			ret.push_back("US");
			break;
		case 'P':	// PAL
		case 'X':	// Multi-language release
		case 'Y':	// Multi-language release
		case 'L':	// Japanese import to PAL regions
		case 'M':	// Japanese import to PAL regions
		default:
			if (fallback_region == 0) {
				// Use the fallback region.
				fallback_region = 1;
			}
			break;

		// European regions.
		case 'D':	// Germany
			ret.push_back("DE");
			break;
		case 'F':	// France
			ret.push_back("FR");
			break;
		case 'H':	// Netherlands
			ret.push_back("NL");
			break;
		case 'I':	// Italy
			ret.push_back("NL");
			break;
		case 'R':	// Russia
			ret.push_back("RU");
			break;
		case 'S':	// Spain
			ret.push_back("ES");
			break;
		case 'U':	// Australia
			if (fallback_region == 0) {
				// Use the fallback region.
				fallback_region = 2;
			}
			break;
	}

	// Check for fallbacks.
	switch (fallback_region) {
		case 1:
			// Europe
			ret.push_back("EN");
			break;
		case 2:
			// Australia
			ret.push_back("AU");
			ret.push_back("EN");
			break;

		case 0:	// None
		default:
			break;
	}

	return ret;
}

/**
 * Get the DSi flags string vector.
 * @return DSi flags string vector.
 */
vector<vector<string> > *NintendoDSPrivate::getDSiFlagsStringVector(void)
{
	static const char *const dsi_flags_bitfield_names[] = {
		// tr: Uses the DSi-specific touchscreen protocol.
		NOP_C_("NintendoDS|DSi_Flags", "DSi Touchscreen"),
		// tr: Game requires agreeing to the Nintendo online services agreement.
		NOP_C_("NintendoDS|DSi_Flags", "Require EULA"),
		// tr: Custom icon is used from the save file.
		NOP_C_("NintendoDS|DSi_Flags", "Custom Icon"),
		// tr: Game supports Nintendo Wi-Fi Connection.
		NOP_C_("NintendoDS|DSi_Flags", "Nintendo WFC"),
		NOP_C_("NintendoDS|DSi_Flags", "DS Wireless"),
		NOP_C_("NintendoDS|DSi_Flags", "NDS Icon SHA-1"),
		NOP_C_("NintendoDS|DSi_Flags", "NDS Header RSA"),
		NOP_C_("NintendoDS|DSi_Flags", "Developer"),
	};

	// Convert to vector<vector<string> > for RFT_LISTDATA.
	auto vv_dsi_flags = new vector<vector<string> >(ARRAY_SIZE(dsi_flags_bitfield_names));
	for (int i = ARRAY_SIZE(dsi_flags_bitfield_names)-1; i >= 0; i--) {
		auto &data_row = vv_dsi_flags->at(i);
		data_row.push_back(
			dpgettext_expr(RP_I18N_DOMAIN, "NintendoDS|DSi_Flags",
				dsi_flags_bitfield_names[i]));
	}

	return vv_dsi_flags;
}

/** NintendoDS **/

/**
 * Read a Nintendo DS ROM image.
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
NintendoDS::NintendoDS(IRpFile *file)
	: super(new NintendoDSPrivate(this, file, false))
{
	RP_D(NintendoDS);
	d->className = "NintendoDS";

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	init();
}

/**
 * Read a Nintendo DS ROM image.
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
 * @param cia If true, hide fields that aren't relevant to DSiWare in 3DS CIA packages.
 */
NintendoDS::NintendoDS(IRpFile *file, bool cia)
	: super(new NintendoDSPrivate(this, file, cia))
{
	RP_D(NintendoDS);
	d->className = "NintendoDS";

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	init();
}

/**
 * Common initialization function for the constructors.
 */
void NintendoDS::init(void)
{
	RP_D(NintendoDS);

	// Read the ROM header.
	d->file->rewind();
	size_t size = d->file->read(&d->romHeader, sizeof(d->romHeader));
	if (size != sizeof(d->romHeader)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this ROM image is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(d->romHeader);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->romHeader);
	info.ext = nullptr;	// Not needed for NDS.
	info.szFile = 0;	// Not needed for NDS.
	d->romType = isRomSupported_static(&info);
	d->isValid = (d->romType >= 0);

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
int NintendoDS::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(NDS_RomHeader))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Check the first 16 bytes of the Nintendo logo.
	static const uint8_t nintendo_gba_logo[16] = {
		0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21,
		0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD
	};
	static const uint8_t nintendo_ds_logo_slot2[16] = {
		0xC8, 0x60, 0x4F, 0xE2, 0x01, 0x70, 0x8F, 0xE2,
		0x17, 0xFF, 0x2F, 0xE1, 0x12, 0x4F, 0x11, 0x48,
	};

	const NDS_RomHeader *const romHeader =
		reinterpret_cast<const NDS_RomHeader*>(info->header.pData);
	if (!memcmp(romHeader->nintendo_logo, nintendo_gba_logo, sizeof(nintendo_gba_logo)) &&
	    romHeader->nintendo_logo_checksum == cpu_to_le16(0xCF56)) {
		// Nintendo logo is valid. (Slot-1)
		static const uint8_t nds_romType[] = {
			NintendoDSPrivate::ROM_NDS,		// 0x00 == Nintendo DS
			NintendoDSPrivate::ROM_NDS,		// 0x01 == invalid (assuming DS)
			NintendoDSPrivate::ROM_DSi_ENH,		// 0x02 == DSi-enhanced
			NintendoDSPrivate::ROM_DSi_ONLY,	// 0x03 == DSi-only
		};
		return nds_romType[romHeader->unitcode & 3];
	} else if (!memcmp(romHeader->nintendo_logo, nintendo_ds_logo_slot2, sizeof(nintendo_ds_logo_slot2)) &&
		   romHeader->nintendo_logo_checksum == cpu_to_le16(0x9E1A)) {
		// Nintendo logo is valid. (Slot-2)
		// NOTE: Slot-2 is NDS only.
		return NintendoDSPrivate::ROM_NDS_SLOT2;
	}

	// Not supported.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *NintendoDS::systemName(unsigned int type) const
{
	RP_D(const NintendoDS);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// NDS/DSi are mostly the same worldwide, except for China.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"NintendoDS::systemName() array index optimization needs to be updated.");
	static_assert(SYSNAME_REGION_MASK == (1 << 2),
		"NintendoDS::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	// Bit 2: 0 for NDS, 1 for DSi-exclusive.
	// Bit 3: 0 for worldwide, 1 for China. (iQue DS)
	static const char *const sysNames[16] = {
		// Nintendo (worldwide)
		"Nintendo DS", "Nintendo DS", "NDS", nullptr,
		"Nintendo DSi", "Nintendo DSi", "DSi", nullptr,

		// iQue (China)
		"iQue DS", "iQue DS", "NDS", nullptr,
		"iQue DSi", "iQue DSi", "DSi", nullptr
	};

	unsigned int idx = (type & SYSNAME_TYPE_MASK);
	if (d->romType == NintendoDSPrivate::ROM_DSi_ONLY) {
		// DSi-exclusive game.
		idx |= (1 << 2);
		if ((type & SYSNAME_REGION_MASK) == SYSNAME_REGION_ROM_LOCAL) {
			if ((d->romHeader.dsi.region_code == cpu_to_le32(DSi_REGION_CHINA)) ||
			    (d->romHeader.nds_region & 0x80))
			{
				// iQue DSi.
				idx |= (1 << 3);
			}
		}
	} else {
		// NDS-only and/or DSi-enhanced game.
		if ((type & SYSNAME_REGION_MASK) == SYSNAME_REGION_ROM_LOCAL) {
			if (d->romHeader.nds_region & 0x80) {
				// iQue DS.
				idx |= (1 << 3);
			}
		}
	}

	return sysNames[idx];
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
const char *const *NintendoDS::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".nds",	// Nintendo DS
		".dsi",	// Nintendo DSi (devkitARM r46)
		".ids",	// iQue DS (no-intro)
		".srl",	// Official SDK extension

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
const char *const *NintendoDS::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types from FreeDesktop.org.
		"application/x-nintendo-ds-rom",

		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"application/x-nintendo-dsi-rom",

		nullptr
	};
	return mimeTypes;
}

/**
 * Get a bitfield of image types this class can retrieve.
 * @return Bitfield of supported image types. (ImageTypesBF)
 */
uint32_t NintendoDS::supportedImageTypes_static(void)
{
#ifdef HAVE_JPEG
	return IMGBF_INT_ICON | IMGBF_EXT_BOX |
	       IMGBF_EXT_COVER | IMGBF_EXT_COVER_FULL;
#else /* !HAVE_JPEG */
	return IMGBF_INT_ICON | IMGBF_EXT_BOX;
#endif /* HAVE_JPEG */
}

/**
 * Get a list of all available image sizes for the specified image type.
 * @param imageType Image type.
 * @return Vector of available image sizes, or empty vector if no images are available.
 */
vector<RomData::ImageSizeDef> NintendoDS::supportedImageSizes_static(ImageType imageType)
{
	ASSERT_supportedImageSizes(imageType);

	switch (imageType) {
		case IMG_INT_ICON: {
			static const ImageSizeDef sz_INT_ICON[] = {
				{nullptr, 32, 32, 0},
			};
			return vector<ImageSizeDef>(sz_INT_ICON,
				sz_INT_ICON + ARRAY_SIZE(sz_INT_ICON));
		}
#ifdef HAVE_JPEG
		case IMG_EXT_COVER: {
			static const ImageSizeDef sz_EXT_COVER[] = {
				{nullptr, 160, 144, 0},
				//{"S", 128, 115, 1},	// DISABLED; not needed.
				{"M", 400, 352, 2},
				{"HQ", 768, 680, 3},
			};
			return vector<ImageSizeDef>(sz_EXT_COVER,
				sz_EXT_COVER + ARRAY_SIZE(sz_EXT_COVER));
		}
		case IMG_EXT_COVER_FULL: {
			static const ImageSizeDef sz_EXT_COVER_FULL[] = {
				{nullptr, 340, 144, 0},
				//{"S", 272, 115, 1},	// Not currently present on GameTDB.
				{"M", 856, 352, 2},
				{"HQ", 1616, 680, 3},
			};
			return vector<ImageSizeDef>(sz_EXT_COVER_FULL,
				sz_EXT_COVER_FULL + ARRAY_SIZE(sz_EXT_COVER_FULL));
		}
#endif /* HAVE_JPEG */
		case IMG_EXT_BOX: {
			static const ImageSizeDef sz_EXT_BOX[] = {
				{nullptr, 240, 216, 0},
			};
			return vector<ImageSizeDef>(sz_EXT_BOX,
				sz_EXT_BOX + ARRAY_SIZE(sz_EXT_BOX));
		}
		default:
			break;
	}

	// Unsupported image type.
	return vector<ImageSizeDef>();
}

/**
 * Get image processing flags.
 *
 * These specify post-processing operations for images,
 * e.g. applying transparency masks.
 *
 * @param imageType Image type.
 * @return Bitfield of ImageProcessingBF operations to perform.
 */
uint32_t NintendoDS::imgpf(ImageType imageType) const
{
	ASSERT_imgpf(imageType);

	RP_D(const NintendoDS);
	uint32_t ret = 0;
	switch (imageType) {
		case IMG_INT_ICON:
			// Use nearest-neighbor scaling when resizing.
			// Also, need to check if this is an animated icon.
			const_cast<NintendoDSPrivate*>(d)->loadIcon();
			if (d->iconAnimData && d->iconAnimData->count > 1) {
				// Animated icon.
				ret = IMGPF_RESCALE_NEAREST | IMGPF_ICON_ANIMATED;
			} else {
				// Not animated.
				ret = IMGPF_RESCALE_NEAREST;
			}
			break;

		default:
			// GameTDB's Nintendo DS cover scans have alpha transparency.
			// Hence, no image processing is required.
			break;
	}
	return ret;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int NintendoDS::loadFieldData(void)
{
	RP_D(NintendoDS);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->romType < 0) {
		// ROM image isn't valid.
		return -EIO;
	}

#ifdef _WIN32
	// Windows: 6 visible rows per RFT_LISTDATA.
	static const int rows_visible = 6;
#else
	// Linux: 4 visible rows per RFT_LISTDATA.
	static const int rows_visible = 4;
#endif

	// Nintendo DS ROM header.
	const NDS_RomHeader *const romHeader = &d->romHeader;
	const bool hasDSi = !!(romHeader->unitcode & NintendoDSPrivate::DS_HW_DSi);
	if (hasDSi) {
		// DSi-enhanced or DSi-exclusive.
		d->fields->reserve(10+7);
	} else {
		// NDS only.
		d->fields->reserve(10);
	}

	// NDS common fields.
	d->fields->setTabName(0, "NDS");

	// Type.
	// TODO:
	// - Show PassMe fields?
	//   Reference: http://imrannazar.com/The-Smallest-NDS-File
	// - Show IR cart and/or other accessories? (NAND ROM, etc.)
	const char *nds_romType;
	if (d->cia || ((romHeader->unitcode & NintendoDSPrivate::DS_HW_DSi) &&
		romHeader->dsi.filetype != DSi_FTYPE_CARTRIDGE))
	{
		// DSiWare.
		// TODO: Verify games that are available as both
		// cartridge and DSiWare.
		if (romHeader->dsi.filetype == DSi_FTYPE_DSiWARE) {
			nds_romType = "DSiWare";
		} else {
			nds_romType = "DSi System Software";
		}
	} else {
		// TODO: Identify NDS Download Play titles.
		switch (d->romType) {
			case NintendoDSPrivate::ROM_NDS_SLOT2:
				nds_romType = "Slot-2 (PassMe)";
				break;
			default:
				nds_romType = "Slot-1";
				break;
		}
	}
	d->fields->addField_string(C_("NintendoDS", "Type"), nds_romType);

	// Title.
	d->fields->addField_string(C_("RomData", "Title"),
		latin1_to_utf8(romHeader->title, ARRAY_SIZE(romHeader->title)));

	// Full title.
	// TODO: Where should this go?
	int lang = d->getTitleIndex();
	if (lang >= 0 && lang < ARRAY_SIZE(d->nds_icon_title.title)) {
		d->fields->addField_string(C_("NintendoDS", "Full Title"),
			utf16le_to_utf8(d->nds_icon_title.title[lang],
				ARRAY_SIZE(d->nds_icon_title.title[lang])));
	}

	// Game ID.
	d->fields->addField_string(C_("NintendoDS", "Game ID"),
		latin1_to_utf8(romHeader->id6, ARRAY_SIZE(romHeader->id6)));

	// Publisher.
	const char *const publisher = NintendoPublishers::lookup(romHeader->company);
	d->fields->addField_string(C_("RomData", "Publisher"),
		publisher ? publisher :
			rp_sprintf(C_("NintendoDS", "Unknown (%.2s)"), romHeader->company));

	// ROM version.
	d->fields->addField_string_numeric(C_("RomData", "Revision"),
		romHeader->rom_version, RomFields::FB_DEC, 2);

	// Is the security data present?
	d->fields->addField_string(C_("NintendoDS", "Security Data"),
		d->checkNDSSecurityDataArea() ? "Present" : "Missing");

	// Secure Area.
	// TODO: Verify the CRC.
	const char *secure_area = d->checkNDSSecureArea();
	d->fields->addField_string(C_("NintendoDS", "Secure Area"),
		secure_area ? secure_area : C_("NintendoDS", "Unknown"));

	// Hardware type.
	// NOTE: DS_HW_DS is inverted bit0; DS_HW_DSi is normal bit1.
	uint32_t hw_type = (romHeader->unitcode & 3) ^ NintendoDSPrivate::DS_HW_DS;
	if (hw_type == 0) {
		// 0x01 is invalid. Assume DS.
		hw_type = NintendoDSPrivate::DS_HW_DS;
	}

	static const char *const hw_bitfield_names[] = {
		"Nintendo DS", "Nintendo DSi"
	};
	vector<string> *const v_hw_bitfield_names = RomFields::strArrayToVector(
		hw_bitfield_names, ARRAY_SIZE(hw_bitfield_names));
	d->fields->addField_bitfield(C_("NintendoDS", "Hardware"),
		v_hw_bitfield_names, 0, hw_type);

	// NDS Region.
	// Only used for region locking on Chinese iQue DS consoles.
	// Not displayed for DSiWare wrapped in 3DS CIA packages.
	uint32_t nds_region = 0;
	if (romHeader->nds_region & 0x80) {
		nds_region |= NintendoDSPrivate::NDS_REGION_CHINA;
	}
	if (romHeader->nds_region & 0x40) {
		nds_region |= NintendoDSPrivate::NDS_REGION_SKOREA;
	}
	if (nds_region == 0) {
		// No known region flags.
		// Note that the Sonic Colors demo has 0x02 here.
		nds_region = NintendoDSPrivate::NDS_REGION_FREE;
	}

	static const char *const nds_region_bitfield_names[] = {
		NOP_C_("Region", "Region-Free"),
		NOP_C_("Region", "South Korea"),
		NOP_C_("Region", "China"),
	};
	vector<string> *const v_nds_region_bitfield_names = RomFields::strArrayToVector_i18n(
		"Region", nds_region_bitfield_names, ARRAY_SIZE(nds_region_bitfield_names));
	d->fields->addField_bitfield(C_("NintendoDS", "DS Region Code"),
		v_nds_region_bitfield_names, 0, nds_region);

	if (!(hw_type & NintendoDSPrivate::DS_HW_DSi)) {
		// Not a DSi-enhanced or DSi-exclusive ROM image.
		if (romHeader->dsi.flags != 0) {
			// DSi flags.
			// NOTE: These are present in NDS games released after the DSi,
			// even if the game isn't DSi-enhanced.
			d->fields->addTab("DSi");
			auto vv_dsi_flags = d->getDSiFlagsStringVector();
			RomFields::AFLD_PARAMS params(RomFields::RFT_LISTDATA_CHECKBOXES, 8);
			params.headers = nullptr;
			params.list_data = vv_dsi_flags;
			params.mxd.checkboxes = romHeader->dsi.flags;
			d->fields->addField_listData(C_("NintendoDS", "Flags"), &params);
		}
		return static_cast<int>(d->fields->count());
	}

	/** DSi-specific fields. **/
	d->fields->addTab("DSi");

	// Title ID.
	const uint32_t tid_hi = le32_to_cpu(romHeader->dsi.title_id.hi);
	d->fields->addField_string(C_("NintendoDS", "Title ID"),
		rp_sprintf("%08X-%08X",
			tid_hi, le32_to_cpu(romHeader->dsi.title_id.lo)));

	// DSi filetype.
	static const struct {
		uint8_t dsi_filetype;
		const char *s_dsi_filetype;
	} dsi_filetype_lkup_tbl[] = {
		// tr: DSi-enhanced or DSi-exclusive cartridge.
		{DSi_FTYPE_CARTRIDGE,		NOP_C_("NintendoDS|DSiFileType", "Cartridge")},
		// tr: DSiWare (download-only title)
		{DSi_FTYPE_DSiWARE,		NOP_C_("NintendoDS|DSiFileType", "DSiWare")},
		// tr: DSi_FTYPE_SYSTEM_FUN_TOOL
		{DSi_FTYPE_SYSTEM_FUN_TOOL,	NOP_C_("NintendoDS|DSiFileType", "System Fun Tool")},
		// tr: Data file, e.g. DS cartridge whitelist.
		{DSi_FTYPE_NONEXEC_DATA,	NOP_C_("NintendoDS|DSiFileType", "Non-Executable Data File")},
		// tr: DSi_FTYPE_SYSTEM_BASE_TOOL
		{DSi_FTYPE_SYSTEM_BASE_TOOL,	NOP_C_("NintendoDS|DSiFileType", "System Base Tool")},
		// tr: System Menu
		{DSi_FTYPE_SYSTEM_MENU,		NOP_C_("NintendoDS|DSiFileType", "System Menu")},

		{0, nullptr}
	};

	const uint8_t dsi_filetype = romHeader->dsi.filetype;
	const char *s_dsi_filetype = nullptr;
	for (const auto *p = dsi_filetype_lkup_tbl; p->s_dsi_filetype != nullptr; p++) {
		if (p->dsi_filetype == dsi_filetype) {
			// Found a match.
			s_dsi_filetype = p->s_dsi_filetype;
			break;
		}
	}

	// TODO: Is the field name too long?
	const char *const dsi_rom_type_title = C_("NintendoDS", "DSi ROM Type");
	if (s_dsi_filetype) {
		d->fields->addField_string(dsi_rom_type_title,
			dpgettext_expr(RP_I18N_DOMAIN, "NintendoDS|DSiFileType", s_dsi_filetype));
	} else {
		// Invalid file type.
		d->fields->addField_string(dsi_rom_type_title,
			rp_sprintf(C_("RomData", "Unknown (0x%02X)"), dsi_filetype));
	}

	// Key index. Determined by title ID.
	int key_idx;
	if (tid_hi & 0x00000010) {
		// System application.
		key_idx = 2;
	} else if (tid_hi & 0x00000001) {
		// Applet.
		key_idx = 1;
	} else {
		// Cartridge and/or DSiWare.
		key_idx = 3;
	}

	// TODO: Keyset is determined by the system.
	// There might be some indicator in the cartridge header...
	d->fields->addField_string_numeric(C_("NintendoDS", "Key Index"), key_idx);

	const char *const region_code_name = (d->cia
			? C_("RomData", "Region Code")
			: C_("NintendoDS", "DSi Region Code"));

	// DSi Region.
	// Maps directly to the header field.
	static const char *const dsi_region_bitfield_names[] = {
		NOP_C_("Region", "Japan"),
		NOP_C_("Region", "USA"),
		NOP_C_("Region", "Europe"),
		NOP_C_("Region", "Australia"),
		NOP_C_("Region", "China"),
		NOP_C_("Region", "South Korea"),
	};
	vector<string> *const v_dsi_region_bitfield_names = RomFields::strArrayToVector_i18n(
		"Region", dsi_region_bitfield_names, ARRAY_SIZE(dsi_region_bitfield_names));
	d->fields->addField_bitfield(region_code_name,
		v_dsi_region_bitfield_names, 3, le32_to_cpu(romHeader->dsi.region_code));

	// Age rating(s).
	// Note that not all 16 fields are present on DSi,
	// though the fields do match exactly, so no
	// mapping is necessary.
	RomFields::age_ratings_t age_ratings;
	// Valid ratings: 0-1, 3-9
	// TODO: Not sure if Finland is valid for DSi.
	static const uint16_t valid_ratings = 0x3FB;

	for (int i = static_cast<int>(age_ratings.size())-1; i >= 0; i--) {
		if (!(valid_ratings & (1 << i))) {
			// Rating is not applicable for NintendoDS.
			age_ratings[i] = 0;
			continue;
		}

		// DSi ratings field:
		// - 0x1F: Age rating.
		// - 0x40: Prohibited in area. (TODO: Verify)
		// - 0x80: Rating is valid if set.
		const uint8_t dsi_rating = romHeader->dsi.age_ratings[i];
		if (!(dsi_rating & 0x80)) {
			// Rating is unused.
			age_ratings[i] = 0;
			continue;
		}

		// Set active | age value.
		age_ratings[i] = RomFields::AGEBF_ACTIVE | (dsi_rating & 0x1F);

		// Is the game prohibited?
		if (dsi_rating & 0x40) {
			age_ratings[i] |= RomFields::AGEBF_PROHIBITED;
		}
	}
	d->fields->addField_ageRatings(C_("RomData", "Age Ratings"), age_ratings);

	// Permissions and flags.
	d->fields->addTab("Permissions");

	// Permissions.
	static const char *const dsi_permissions_bitfield_names[] = {
		NOP_C_("NintendoDS|DSi_Permissions", "Common Key"),
		NOP_C_("NintendoDS|DSi_Permissions", "AES Slot B"),
		NOP_C_("NintendoDS|DSi_Permissions", "AES Slot C"),
		NOP_C_("NintendoDS|DSi_Permissions", "SD Card"),
		NOP_C_("NintendoDS|DSi_Permissions", "eMMC Access"),
		NOP_C_("NintendoDS|DSi_Permissions", "Game Card Power On"),
		NOP_C_("NintendoDS|DSi_Permissions", "Shared2 File"),
		NOP_C_("NintendoDS|DSi_Permissions", "Sign JPEG for Launcher"),
		NOP_C_("NintendoDS|DSi_Permissions", "Game Card NTR Mode"),
		NOP_C_("NintendoDS|DSi_Permissions", "SSL Client Cert"),
		NOP_C_("NintendoDS|DSi_Permissions", "Sign JPEG for User"),
		NOP_C_("NintendoDS|DSi_Permissions", "Photo Read Access"),
		NOP_C_("NintendoDS|DSi_Permissions", "Photo Write Access"),
		NOP_C_("NintendoDS|DSi_Permissions", "SD Card Read Access"),
		NOP_C_("NintendoDS|DSi_Permissions", "SD Card Write Access"),
		NOP_C_("NintendoDS|DSi_Permissions", "Game Card Save Read Access"),
		NOP_C_("NintendoDS|DSi_Permissions", "Game Card Save Write Access"),

		// FIXME: How to handle unused entries for RFT_LISTDATA?
		/*
		// Bits 17-30 are not used.
		nullptr, nullptr, nullptr,
		nullptr, nullptr, nullptr, nullptr,
		nullptr, nullptr, nullptr, nullptr,
		nullptr, nullptr, nullptr,

		NOP_C_("NintendoDS|DSi_Permissions", "Debug Key"),
		*/
	};

	// Convert to vector<vector<string> > for RFT_LISTDATA.
	auto vv_dsi_perm = new vector<vector<string> >(ARRAY_SIZE(dsi_permissions_bitfield_names));
	for (int i = ARRAY_SIZE(dsi_permissions_bitfield_names)-1; i >= 0; i--) {
		auto &data_row = vv_dsi_perm->at(i);
		data_row.push_back(
			dpgettext_expr(RP_I18N_DOMAIN, "NintendoDS|DSi_Permissions",
				dsi_permissions_bitfield_names[i]));
	}

	RomFields::AFLD_PARAMS params(RomFields::RFT_LISTDATA_CHECKBOXES, rows_visible);
	params.headers = nullptr;
	params.list_data = vv_dsi_perm;
	params.mxd.checkboxes = le32_to_cpu(romHeader->dsi.access_control);
	d->fields->addField_listData(C_("NintendoDS", "Permissions"), &params);

	// DSi flags.
	auto vv_dsi_flags = d->getDSiFlagsStringVector();
	params.headers = nullptr;
	params.list_data = vv_dsi_flags;
	params.mxd.checkboxes = romHeader->dsi.flags;
	d->fields->addField_listData(C_("NintendoDS", "Flags"), &params);

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int NintendoDS::loadMetaData(void)
{
	RP_D(NintendoDS);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->romType < 0) {
		// ROM image isn't valid.
		return -EIO;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(2);	// Maximum of 2 metadata properties.

	// ROM header is read in the constructor.
	const NDS_RomHeader *const romHeader = &d->romHeader;

	// Title.
	// TODO: Show the full title if it's present:
	// - Three lines: Concatenate the first two lines
	// - Two lines: Show the first line only.
	// - One line: Show that line.
	d->metaData->addMetaData_string(Property::Title,
		latin1_to_utf8(romHeader->title, ARRAY_SIZE(romHeader->title)));

	// Publisher.
	const char *const publisher = NintendoPublishers::lookup(romHeader->company);
	d->metaData->addMetaData_string(Property::Publisher,
		publisher ? publisher :
			rp_sprintf(C_("NintendoDS", "Unknown (%.2s)"), romHeader->company));

	// Finished reading the metadata.
	return static_cast<int>(d->metaData->count());
}

/**
 * Load an internal image.
 * Called by RomData::image().
 * @param imageType	[in] Image type to load.
 * @param pImage	[out] Pointer to const rp_image* to store the image in.
 * @return 0 on success; negative POSIX error code on error.
 */
int NintendoDS::loadInternalImage(ImageType imageType, const rp_image **pImage)
{
	ASSERT_loadInternalImage(imageType, pImage);

	RP_D(NintendoDS);
	if (imageType != IMG_INT_ICON) {
		// Only IMG_INT_ICON is supported by DS.
		*pImage = nullptr;
		return -ENOENT;
	} else if (d->icon_first_frame) {
		// Image has already been loaded.
		*pImage = d->icon_first_frame;
		return 0;
	} else if (!d->file) {
		// File isn't open.
		*pImage = nullptr;
		return -EBADF;
	} else if (!d->isValid || d->romType < 0) {
		// ROM image isn't valid.
		*pImage = nullptr;
		return -EIO;
	}

	// Load the icon.
	*pImage = d->loadIcon();
	return (*pImage != nullptr ? 0 : -EIO);
}

/**
 * Get the animated icon data.
 *
 * Check imgpf for IMGPF_ICON_ANIMATED first to see if this
 * object has an animated icon.
 *
 * @return Animated icon data, or nullptr if no animated icon is present.
 */
const IconAnimData *NintendoDS::iconAnimData(void) const
{
	RP_D(const NintendoDS);
	if (!d->iconAnimData) {
		// Load the icon.
		if (!const_cast<NintendoDSPrivate*>(d)->loadIcon()) {
			// Error loading the icon.
			return nullptr;
		}
		if (!d->iconAnimData) {
			// Still no icon...
			return nullptr;
		}
	}

	if (d->iconAnimData->count <= 1) {
		// Not an animated icon.
		return nullptr;
	}

	// Return the icon animation data.
	return d->iconAnimData;
}

/**
 * Get a list of URLs for an external image type.
 *
 * A thumbnail size may be requested from the shell.
 * If the subclass supports multiple sizes, it should
 * try to get the size that most closely matches the
 * requested size.
 *
 * @param imageType	[in]     Image type.
 * @param pExtURLs	[out]    Output vector.
 * @param size		[in,opt] Requested image size. This may be a requested
 *                               thumbnail size in pixels, or an ImageSizeType
 *                               enum value.
 * @return 0 on success; negative POSIX error code on error.
 */
int NintendoDS::extURLs(ImageType imageType, vector<ExtURL> *pExtURLs, int size) const
{
	ASSERT_extURLs(imageType, pExtURLs);
	pExtURLs->clear();

	// Check for DS ROMs that don't have boxart.
	RP_D(const NintendoDS);
	if (!d->isValid || d->romType < 0) {
		// ROM image isn't valid.
		return -EIO;
	} else if (!memcmp(d->romHeader.id4, "NTRJ", 4) ||
	           !memcmp(d->romHeader.id4, "####", 4))
	{
		// This is either a prototype, a download demo, or homebrew.
		// No external images are available.
		return -ENOENT;
	} else if ((d->romHeader.unitcode & NintendoDSPrivate::DS_HW_DSi) &&
		    d->romHeader.dsi.filetype != DSi_FTYPE_CARTRIDGE)
	{
		// This is a DSi SRL that isn't a cartridge dump.
		// No external images are available.
		return -ENOENT;
	}

	// Get the image sizes and sort them based on the
	// requested image size.
	vector<ImageSizeDef> sizeDefs = supportedImageSizes(imageType);
	if (sizeDefs.empty()) {
		// No image sizes.
		return -ENOENT;
	}

	// Select the best size.
	const ImageSizeDef *const sizeDef = d->selectBestSize(sizeDefs, size);
	if (!sizeDef) {
		// No size available...
		return -ENOENT;
	}

	// NOTE: Only downloading the first size as per the
	// sort order, since GameTDB basically guarantees that
	// all supported sizes for an image type are available.
	// TODO: Add cache keys for other sizes in case they're
	// downloaded and none of these are available?

	// Determine the image type name.
	const char *imageTypeName_base;
	const char *ext;
	switch (imageType) {
#ifdef HAVE_JPEG
		case IMG_EXT_COVER:
			imageTypeName_base = "cover";
			ext = ".jpg";
			break;
		case IMG_EXT_COVER_FULL:
			imageTypeName_base = "coverfull";
			ext = ".jpg";
			break;
#endif /* HAVE_JPEG */
		case IMG_EXT_BOX:
			imageTypeName_base = "box";
			ext = ".png";
			break;
		default:
			// Unsupported image type.
			return -ENOENT;
	}

	// Game ID. (GameTDB uses ID4 for Nintendo DS.)
	// The ID4 cannot have non-printable characters.
	char id4[5];
	for (int i = ARRAY_SIZE(d->romHeader.id4)-1; i >= 0; i--) {
		if (!ISPRINT(d->romHeader.id4[i])) {
			// Non-printable character found.
			return -ENOENT;
		}
		id4[i] = d->romHeader.id4[i];
	}
	// NULL-terminated ID4 is needed for the
	// GameTDB URL functions.
	id4[4] = 0;

	// Determine the GameTDB region code(s).
	vector<const char*> tdb_regions = d->ndsRegionToGameTDB(
		d->romHeader.nds_region,
		((d->romHeader.unitcode & NintendoDSPrivate::DS_HW_DSi)
			? le32_to_cpu(d->romHeader.dsi.region_code)
			: 0 /* not a DSi-enhanced/exclusive ROM */
			),
		d->romHeader.id4[3]);

	// If we're downloading a "high-resolution" image (M or higher),
	// also add the default image to ExtURLs in case the user has
	// high-resolution image downloads disabled.
	const ImageSizeDef *szdefs_dl[2];
	szdefs_dl[0] = sizeDef;
	unsigned int szdef_count;
	if (sizeDef->index >= 2) {
		// M or higher.
		szdefs_dl[1] = &sizeDefs[0];
		szdef_count = 2;
	} else {
		// Default or S.
		szdef_count = 1;
	}

	// Add the URLs.
	pExtURLs->resize(szdef_count * tdb_regions.size());
	auto extURL_iter = pExtURLs->begin();
	for (unsigned int i = 0; i < szdef_count; i++) {
		// Current image type.
		char imageTypeName[16];
		snprintf(imageTypeName, sizeof(imageTypeName), "%s%s",
			 imageTypeName_base, (szdefs_dl[i]->name ? szdefs_dl[i]->name : ""));

		// Add the images.
		for (auto tdb_iter = tdb_regions.cbegin();
		     tdb_iter != tdb_regions.cend(); ++tdb_iter, ++extURL_iter)
		{
			extURL_iter->url = d->getURL_GameTDB("ds", imageTypeName, *tdb_iter, id4, ext);
			extURL_iter->cache_key = d->getCacheKey_GameTDB("ds", imageTypeName, *tdb_iter, id4, ext);
			extURL_iter->width = szdefs_dl[i]->width;
			extURL_iter->height = szdefs_dl[i]->height;
			extURL_iter->high_res = (szdefs_dl[i]->index >= 2);
		}
	}

	// All URLs added.
	return 0;
}

/**
 * Does this ROM image have "dangerous" permissions?
 *
 * @return True if the ROM image has "dangerous" permissions; false if not.
 */
bool NintendoDS::hasDangerousPermissions(void) const
{
	// Load permissions.
	// TODO: If this is DSiWare, check DSiWare permissions?
	RP_D(const NintendoDS);

	// If Game Card Power On is set, eMMC Access and SD Card must be off.
	// This combination is normally not found in licensed games,
	// and is only found in the system menu. Some homebrew titles
	// might have this set, though.
	const uint32_t dsi_access_control = le32_to_cpu(d->romHeader.dsi.access_control);
	if (dsi_access_control & DSi_ACCESS_GAME_CARD_POWER_ON) {
		// Game Card Power On is set.
		if (dsi_access_control & (DSi_ACCESS_SD_CARD | DSi_ACCESS_eMMC_ACCESS)) {
			// SD and/or eMMC is set.
			// This combination is not allowed by Nintendo, and
			// usually indicates some sort of homebrew.
			return true;
		}
	}

	// Not dangerous.
	return false;
}

}
