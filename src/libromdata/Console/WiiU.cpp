/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * WiiU.cpp: Nintendo Wii U disc image reader.                             *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "librpbase/config.librpbase.h"

#include "WiiU.hpp"
#include "librpbase/RomData_p.hpp"

#include "data/NintendoPublishers.hpp"
#include "wiiu_structs.h"
#include "gcn_structs.h"
#include "data/WiiUData.hpp"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"
#include "libi18n/i18n.h"
using namespace LibRpBase;

// DiscReader
#include "librpbase/disc/DiscReader.hpp"
#include "disc/WuxReader.hpp"
#include "disc/wux_structs.h"

// C includes. (C++ namespace)
#include "librpbase/ctypex.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>

// C++ includes.
#include <memory>
#include <string>
#include <vector>
using std::string;
using std::unique_ptr;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(WiiU)
ROMDATA_IMPL_IMG(WiiU)

class WiiUPrivate : public RomDataPrivate
{
	public:
		WiiUPrivate(WiiU *q, IRpFile *file);
		virtual ~WiiUPrivate();

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(WiiUPrivate)

	public:
		enum DiscType {
			DISC_UNKNOWN = -1,	// Unknown disc type

			DISC_FORMAT_WUD = 0,	// Wii U disc image (uncompressed)
			DISC_FORMAT_WUX = 1,	// WUX (compressed)
		};

		// Disc type and reader.
		int discType;
		IDiscReader *discReader;

		// Disc header.
		WiiU_DiscHeader discHeader;
};

/** WiiUPrivate **/

WiiUPrivate::WiiUPrivate(WiiU *q, IRpFile *file)
	: super(q, file)
	, discType(DISC_UNKNOWN)
	, discReader(nullptr)
{
	// Clear the discHeader struct.
	memset(&discHeader, 0, sizeof(discHeader));
}

WiiUPrivate::~WiiUPrivate()
{
	delete discReader;
}

/** WiiU **/

/**
 * Read a Nintendo Wii U disc image.
 *
 * A disc image must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the disc image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open disc image.
 */
WiiU::WiiU(IRpFile *file)
	: super(new WiiUPrivate(this, file))
{
	// This class handles disc images.
	RP_D(WiiU);
	d->className = "WiiU";
	d->fileType = FTYPE_DISC_IMAGE;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the disc header.
	// NOTE: Using sizeof(GCN_DiscHeader) so we can verify that
	// GCN/Wii magic numbers are not present.
	static_assert(sizeof(GCN_DiscHeader) >= sizeof(WiiU_DiscHeader),
		"GCN_DiscHeader is smaller than WiiU_DiscHeader.");
	uint8_t header[sizeof(GCN_DiscHeader)];
	d->file->rewind();
	size_t size = d->file->read(header, sizeof(header));
	if (size != sizeof(header)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this disc image is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(header);
	info.header.pData = header;
	info.ext = nullptr;	// Not needed for Wii U.
	info.szFile = d->file->size();
	d->discType = isRomSupported_static(&info);
	if (d->discType < 0) {
		// Disc image is invalid.
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Create an IDiscReader.
	switch (d->discType) {
		case WiiUPrivate::DISC_FORMAT_WUD:
			d->discReader = new DiscReader(d->file);
			break;
		case WiiUPrivate::DISC_FORMAT_WUX:
			d->discReader = new WuxReader(d->file);
			break;
		case WiiUPrivate::DISC_UNKNOWN:
		default:
			d->fileType = FTYPE_UNKNOWN;
			d->discType = WiiUPrivate::DISC_UNKNOWN;
			break;
	}

	if (!d->discReader->isOpen()) {
		// Error opening the DiscReader.
		delete d->discReader;
		d->discReader = nullptr;
		d->fileType = FTYPE_UNKNOWN;
		d->discType = WiiUPrivate::DISC_UNKNOWN;
		return;
	}

	if (d->discType < 0) {
		// Nothing else to do here.
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Re-read the disc header for WUX.
	if (d->discType > WiiUPrivate::DISC_FORMAT_WUD) {
		size = d->discReader->seekAndRead(0, header, sizeof(header));
		if (size != sizeof(header)) {
			// Seek and/or read error.
			delete d->discReader;
			d->file->unref();
			d->discReader = nullptr;
			d->file = nullptr;
			d->discType = WiiUPrivate::DISC_UNKNOWN;
			return;
		}
	}

	// Verify the secondary magic number at 0x10000.
	uint32_t disc_magic;
	size = d->discReader->seekAndRead(0x10000, &disc_magic, sizeof(disc_magic));
	if (size != sizeof(disc_magic)) {
		// Seek and/or read error.
		delete d->discReader;
		d->file->unref();
		d->discReader = nullptr;
		d->file = nullptr;
		d->discType = WiiUPrivate::DISC_UNKNOWN;
		return;
	}

	if (disc_magic == cpu_to_be32(WIIU_SECONDARY_MAGIC)) {
		// Secondary magic matches.
		d->isValid = true;

		// Save the disc header.
		memcpy(&d->discHeader, header, sizeof(d->discHeader));
	} else {
		// No match.
		delete d->discReader;
		d->file->unref();
		d->discReader = nullptr;
		d->file = nullptr;
		d->discType = WiiUPrivate::DISC_UNKNOWN;
		return;
	}
}

/** ROM detection functions. **/

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int WiiU::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(GCN_DiscHeader) ||
	    info->szFile < 0x20000)
	{
		// Either no detection information was specified,
		// or the header is too small.
		// szFile: Partition table is at 0x18000, so we
		// need to have at least 0x20000.
		return -1;
	}

	// Check for WUX magic numbers.
	const wuxHeader_t *const wuxHeader = reinterpret_cast<const wuxHeader_t*>(info->header.pData);
	if (wuxHeader->magic[0] == cpu_to_le32(WUX_MAGIC_0) &&
	    wuxHeader->magic[1] == cpu_to_le32(WUX_MAGIC_1))
	{
		// WUX header detected.
		// TODO: Also check for other Wii U magic numbers if WUX is found.
		// TODO: Verify block size?
		return WiiUPrivate::DISC_FORMAT_WUX;
	}

	// Game ID must start with "WUP-".
	// NOTE: There's also a secondary magic number at 0x10000,
	// but we can't check it here.
	const WiiU_DiscHeader *const wiiu_header = reinterpret_cast<const WiiU_DiscHeader*>(info->header.pData);
	if (memcmp(wiiu_header->id, "WUP-", 4) != 0) {
		// Not Wii U.
		return -1;
	}

	// Check hyphens.
	// TODO: Verify version numbers and region code.
	if (wiiu_header->hyphen1 != '-' ||
	    wiiu_header->hyphen2 != '-' ||
	    wiiu_header->hyphen3 != '-' ||
	    wiiu_header->hyphen4 != '-' ||
	    wiiu_header->hyphen5 != '-')
	{
		// Missing hyphen.
		return -1;
	}

	// Check for GCN/Wii magic numbers.
	const GCN_DiscHeader *gcn_header = reinterpret_cast<const GCN_DiscHeader*>(info->header.pData);
	if (be32_to_cpu(gcn_header->magic_wii) == WII_MAGIC ||
	    be32_to_cpu(gcn_header->magic_gcn) == GCN_MAGIC)
	{
		// GameCube and/or Wii magic is present.
		// This is not a Wii U disc image.
		return -1;
	}

	// Disc header is valid.
	return WiiUPrivate::DISC_FORMAT_WUD;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *WiiU::systemName(unsigned int type) const
{
	RP_D(const WiiU);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// Wii U has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"WiiU::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	static const char *const sysNames[4] = {
		"Nintendo Wii U", "Wii U", "Wii U", nullptr
	};

	return sysNames[type & SYSNAME_TYPE_MASK];
}

/**
 * Get a list of all supported file extensions.
 * This is to be used for file type registration;
 * subclasses don't explicitly check the extension.
 *
 * NOTE: The extensions do not include the leading dot,
 * e.g. "bin" instead of ".bin".
 *
 * NOTE 2: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *WiiU::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".wud", ".wux",

		// NOTE: May cause conflicts on Windows
		// if fallback handling isn't working.
		".iso",

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
const char *const *WiiU::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"application/x-wii-u-rom",

		nullptr
	};
	return mimeTypes;
}

/**
 * Get a bitfield of image types this class can retrieve.
 * @return Bitfield of supported image types. (ImageTypesBF)
 */
uint32_t WiiU::supportedImageTypes_static(void)
{
#ifdef HAVE_JPEG
	return IMGBF_EXT_MEDIA |
	       IMGBF_EXT_COVER | IMGBF_EXT_COVER_3D |
	       IMGBF_EXT_COVER_FULL;
#else /* !HAVE_JPEG */
	return IMGBF_EXT_MEDIA | IMGBF_EXT_COVER_3D;
#endif /* HAVE_JPEG */
}

/**
 * Get a list of all available image sizes for the specified image type.
 *
 * The first item in the returned vector is the "default" size.
 * If the width/height is 0, then an image exists, but the size is unknown.
 *
 * @param imageType Image type.
 * @return Vector of available image sizes, or empty vector if no images are available.
 */
vector<RomData::ImageSizeDef> WiiU::supportedImageSizes_static(ImageType imageType)
{
	ASSERT_supportedImageSizes(imageType);

	switch (imageType) {
		case IMG_EXT_MEDIA: {
			static const ImageSizeDef sz_EXT_MEDIA[] = {
				{nullptr, 160, 160, 0},
				{"M", 500, 500, 1},
			};
			return vector<ImageSizeDef>(sz_EXT_MEDIA,
				sz_EXT_MEDIA + ARRAY_SIZE(sz_EXT_MEDIA));
		}
#ifdef HAVE_JPEG
		case IMG_EXT_COVER: {
			static const ImageSizeDef sz_EXT_COVER[] = {
				{nullptr, 160, 224, 0},
				{"M", 350, 500, 1},
				{"HQ", 768, 1080, 2},
			};
			return vector<ImageSizeDef>(sz_EXT_COVER,
				sz_EXT_COVER + ARRAY_SIZE(sz_EXT_COVER));
		}
#endif /* HAVE_JPEG */
		case IMG_EXT_COVER_3D: {
			static const ImageSizeDef sz_EXT_COVER_3D[] = {
				{nullptr, 176, 248, 0},
			};
			return vector<ImageSizeDef>(sz_EXT_COVER_3D,
				sz_EXT_COVER_3D + ARRAY_SIZE(sz_EXT_COVER_3D));
		}
#ifdef HAVE_JPEG
		case IMG_EXT_COVER_FULL: {
			static const ImageSizeDef sz_EXT_COVER[] = {
				{nullptr, 340, 224, 0},
				{"M", 752, 500, 1},
				{"HQ", 1632, 1080, 2},
			};
			return vector<ImageSizeDef>(sz_EXT_COVER,
				sz_EXT_COVER + ARRAY_SIZE(sz_EXT_COVER));
		}
#endif /* HAVE_JPEG */
		default:
			break;
	}

	// Unsupported image type.
	return vector<ImageSizeDef>();
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int WiiU::loadFieldData(void)
{
	RP_D(WiiU);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file || !d->file->isOpen()) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// Disc image isn't valid.
		return -EIO;
	}

	// Disc header is read in the constructor.
	d->fields->reserve(4);	// Maximum of 4 fields.

	// Game ID.
	d->fields->addField_string(C_("WiiU", "Game ID"),
		latin1_to_utf8(d->discHeader.id, sizeof(d->discHeader.id)));

	// Publisher.
	// Look up the publisher ID.
	char publisher_code[5];
	const char *publisher = nullptr;
	string s_publisher;

	const uint32_t publisher_id = WiiUData::lookup_disc_publisher(d->discHeader.id4);
	publisher_code[0] = (publisher_id >> 24) & 0xFF;
	publisher_code[1] = (publisher_id >> 16) & 0xFF;
	publisher_code[2] = (publisher_id >>  8) & 0xFF;
	publisher_code[3] =  publisher_id & 0xFF;
	publisher_code[4] = 0;
	if (publisher_id != 0 && (publisher_id & 0xFFFF0000) == 0x30300000) {
		// Publisher ID is a valid two-character ID.
		publisher = NintendoPublishers::lookup(&publisher_code[2]);
	}
	if (publisher) {
		s_publisher = publisher;
	} else {
		if (ISALNUM(publisher_code[0]) && ISALNUM(publisher_code[1]) &&
		    ISALNUM(publisher_code[2]) && ISALNUM(publisher_code[3]))
		{
			s_publisher = rp_sprintf(C_("WiiU", "Unknown (%.4s)"), publisher_code);
		} else {
			s_publisher = rp_sprintf(C_("WiiU", "Unknown (%02X %02X %02X %02X)"),
				static_cast<uint8_t>(publisher_code[0]),
				static_cast<uint8_t>(publisher_code[1]),
				static_cast<uint8_t>(publisher_code[2]),
				static_cast<uint8_t>(publisher_code[3]));
		}
	}
	d->fields->addField_string(C_("RomData", "Publisher"), s_publisher);

	// Game version.
	// TODO: Validate the version characters.
	d->fields->addField_string(C_("RomData", "Version"),
		latin1_to_utf8(d->discHeader.version, sizeof(d->discHeader.version)));

	// OS version.
	// TODO: Validate the version characters.
	d->fields->addField_string(C_("WiiU", "OS Version"),
		rp_sprintf("%c.%c.%c",
			d->discHeader.os_version[0],
			d->discHeader.os_version[1],
			d->discHeader.os_version[2]));

	// Region.
	// TODO: Compare against list of regions and show the fancy name.
	d->fields->addField_string(C_("RomData", "Region Code"),
		latin1_to_utf8(d->discHeader.region, sizeof(d->discHeader.region)));

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
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
int WiiU::extURLs(ImageType imageType, vector<ExtURL> *pExtURLs, int size) const
{
	ASSERT_extURLs(imageType, pExtURLs);
	pExtURLs->clear();

	RP_D(const WiiU);
	if (!d->isValid) {
		// Disc image isn't valid.
		return -EIO;
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
		case IMG_EXT_MEDIA:
			imageTypeName_base = "disc";
			ext = ".png";
			break;
#ifdef HAVE_JPEG
		case IMG_EXT_COVER:
			imageTypeName_base = "cover";
			ext = ".jpg";
			break;
#endif /* HAVE_JPEG */
		case IMG_EXT_COVER_3D:
			imageTypeName_base = "cover3D";
			ext = ".png";
			break;
#ifdef HAVE_JPEG
		case IMG_EXT_COVER_FULL:
			imageTypeName_base = "coverfull";
			ext = ".jpg";
			break;
#endif /* HAVE_JPEG */
		default:
			// Unsupported image type.
			return -ENOENT;
	}

	// Look up the publisher ID.
	uint32_t publisher_id = WiiUData::lookup_disc_publisher(d->discHeader.id4);
	if (publisher_id == 0 || (publisher_id & 0xFFFF0000) != 0x30300000) {
		// Either the publisher ID is unknown, or it's a
		// 4-character ID, which isn't supported by
		// GameTDB at the moment.
		return -ENOENT;
	}

	// Determine the GameTDB region code(s).
	// TODO: Wii U version. (Figure out the region code field...)
	//vector<const char*> tdb_regions = d->gcnRegionToGameTDB(d->gcnRegion, d->discHeader.id4[3]);
	vector<const char*> tdb_regions;
	tdb_regions.push_back("US");

	// Game ID.
	// Replace any non-printable characters with underscores.
	// (GameCube NDDEMO has ID6 "00\0E01".)
	char id6[7];
	for (int i = 0; i < 4; i++) {
		id6[i] = (ISPRINT(d->discHeader.id4[i])
			? d->discHeader.id4[i]
			: '_');
	}

	// Publisher ID.
	id6[4] = (publisher_id >> 8) & 0xFF;
	id6[5] = publisher_id & 0xFF;
	id6[6] = 0;

	// If we're downloading a "high-resolution" image (M or higher),
	// also add the default image to ExtURLs in case the user has
	// high-resolution image downloads disabled.
	const ImageSizeDef *szdefs_dl[2];
	szdefs_dl[0] = sizeDef;
	unsigned int szdef_count;
	if (sizeDef->index > 0) {
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
			extURL_iter->url = d->getURL_GameTDB("wiiu", imageTypeName, *tdb_iter, id6, ext);
			extURL_iter->cache_key = d->getCacheKey_GameTDB("wiiu", imageTypeName, *tdb_iter, id6, ext);
			extURL_iter->width = szdefs_dl[i]->width;
			extURL_iter->height = szdefs_dl[i]->height;
			extURL_iter->high_res = (szdefs_dl[i]->index > 0);
		}
	}

	// All URLs added.
	return 0;
}

}
