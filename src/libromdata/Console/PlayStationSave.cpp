/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * PlayStationSave.hpp: Sony PlayStation save file reader.                 *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * Copyright (c) 2017-2018 by Egor.                                        *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

// References:
// - http://www.psdevwiki.com/ps3/Game_Saves#Game_Saves_PS1
// - http://problemkaputt.de/psx-spx.htm

#include "PlayStationSave.hpp"
#include "librpbase/RomData_p.hpp"

#include "ps1_structs.h"

// librpbase
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
#include <cassert>
#include <cerrno>
#include <cstring>

// C++ includes.
#include <vector>
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(PlayStationSave)
ROMDATA_IMPL_IMG(PlayStationSave)

class PlayStationSavePrivate : public RomDataPrivate
{
	public:
		PlayStationSavePrivate(PlayStationSave *q, IRpFile *file);
		virtual ~PlayStationSavePrivate();

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(PlayStationSavePrivate)

	public:
		// Animated icon data.
		IconAnimData *iconAnimData;

	public:
		// Save file type.
		enum SaveType {
			SAVE_TYPE_UNKNOWN = -1,	// Unknown save type.

			SAVE_TYPE_PSV = 0,	// PS1 on PS3 individual save file.
			SAVE_TYPE_RAW,		// Raw blocks without header information
			SAVE_TYPE_BLOCK,	// Prefixed by header of the first block (*.mcs, *.ps1)
			SAVE_TYPE_54		// Prefixed by 54-byte header (*.mcb, *.mcx, *.pda, *.psx)
		};
		int saveType;

	public:
		// Save file header.
		union {
			PS1_PSV_Header psvHeader;
			PS1_Block_Entry blockHeader;
			PS1_54_Header ps54Header;
		} mxh;
		PS1_SC_Struct scHeader;

		/**
		 * Load the save file's icons.
		 *
		 * This will load all of the animated icon frames,
		 * though only the first frame will be returned.
		 *
		 * @return Icon, or nullptr on error.
		 */
		const rp_image *loadIcon(void);
};

/** PlayStationSavePrivate **/

PlayStationSavePrivate::PlayStationSavePrivate(PlayStationSave *q, IRpFile *file)
	: super(q, file)
	, iconAnimData(nullptr)
	, saveType(SAVE_TYPE_UNKNOWN)
{
	// Clear the various headers.
	memset(&mxh, 0, sizeof(mxh));
	memset(&scHeader, 0, sizeof(scHeader));
}

PlayStationSavePrivate::~PlayStationSavePrivate()
{
	if (iconAnimData) {
		// This class owns all of the icons in iconAnimData, so we
		// must delete all of them.
		for (int i = iconAnimData->count-1; i >= 0; i--) {
			delete iconAnimData->frames[i];
		}
		delete iconAnimData;
	}
}

/**
 * Load the save file's icons.
 *
 * This will load all of the animated icon frames,
 * though only the first frame will be returned.
 *
 * @return Icon, or nullptr on error.
 */
const rp_image *PlayStationSavePrivate::loadIcon(void)
{
	if (iconAnimData) {
		// Icon has already been loaded.
		return iconAnimData->frames[0];
	}

	if (saveType == SAVE_TYPE_UNKNOWN) {
		return nullptr;
	}

	// Determine how many frames need to be decoded.
	int frames;
	int delay;	// in PAL frames
	switch (scHeader.icon_flag) {
		case PS1_SC_ICON_NONE:
		default:
			// No frames.
			return nullptr;

		case PS1_SC_ICON_STATIC:
		case PS1_SC_ICON_ALT_STATIC:
			// One frame.
			frames = 1;
			delay = 0;
			break;

		case PS1_SC_ICON_ANIM_2:
		case PS1_SC_ICON_ALT_ANIM_2:
			// Two frames.
			// Icon delay is 16 PAL frames.
			frames = 2;
			delay = 16;
			break;

		case PS1_SC_ICON_ANIM_3:
		case PS1_SC_ICON_ALT_ANIM_3:
			// Three frames.
			// Icon delay is 11 PAL frames.
			frames = 3;
			delay = 11;
			break;
	}

	this->iconAnimData = new IconAnimData();
	iconAnimData->count = frames;
	iconAnimData->seq_count = frames;

	// Decode the icon frames.
	for (int i = 0; i < frames; i++) {
		iconAnimData->delays[i].numer = delay;
		iconAnimData->delays[i].denom = 50;
		iconAnimData->delays[i].ms = (delay * 1000 / 50);
		iconAnimData->seq_index[i] = i;

		// Icon format is linear 16x16 4bpp with RGB555 palette.
		iconAnimData->frames[i] = ImageDecoder::fromLinearCI4<false>(
			ImageDecoder::PXF_BGR555_PS1, 16, 16,
			scHeader.icon_data[i], sizeof(scHeader.icon_data[i]),
			scHeader.icon_pal, sizeof(scHeader.icon_pal));
	}


	// Return the first frame.
	return iconAnimData->frames[0];
}

/** PlayStationSave **/

/**
 * Read a PlayStation save file.
 *
 * A save file must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the ROM image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open ROM image.
 */
PlayStationSave::PlayStationSave(IRpFile *file)
	: super(new PlayStationSavePrivate(this, file))
{
	// This class handles save files.
	RP_D(PlayStationSave);
	d->className = "PlayStationSave";
	d->fileType = FTYPE_SAVE_FILE;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the save file header.
	uint8_t header[1024];
	d->file->rewind();
	size_t size = d->file->read(&header, sizeof(header));
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
	info.ext = nullptr;	// Not needed for PS1.
	info.szFile = file->size();
	d->saveType = isRomSupported(&info);

	switch (d->saveType) {
		case PlayStationSavePrivate::SAVE_TYPE_PSV:
			// PSV (PS1 on PS3)
			memcpy(&d->mxh.psvHeader, header, sizeof(d->mxh.psvHeader));
			memcpy(&d->scHeader, &header[sizeof(d->mxh.psvHeader)], sizeof(d->scHeader));
			break;
		case PlayStationSavePrivate::SAVE_TYPE_RAW:
			memcpy(&d->scHeader, header, sizeof(d->scHeader));
			break;
		case PlayStationSavePrivate::SAVE_TYPE_BLOCK:
			memcpy(&d->mxh.blockHeader, header, sizeof(d->mxh.blockHeader));
			memcpy(&d->scHeader, &header[sizeof(d->mxh.blockHeader)], sizeof(d->scHeader));
			break;
		case PlayStationSavePrivate::SAVE_TYPE_54:
			memcpy(&d->mxh.ps54Header, header, sizeof(d->mxh.ps54Header));
			memcpy(&d->scHeader, &header[sizeof(d->mxh.ps54Header)], sizeof(d->scHeader));
			break;
		default:
			// Unknown save type.
			d->file->unref();
			d->file = nullptr;
			d->saveType = PlayStationSavePrivate::SAVE_TYPE_UNKNOWN;
			return;
	}

	d->isValid = true;
}

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int PlayStationSave::isRomSupported_static(const DetectInfo *info)
{
	// NOTE: Only PSV is supported right now.
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(PS1_SC_Struct))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Check for PSV+SC.
	if (info->header.size >= sizeof(PS1_PSV_Header) + sizeof(PS1_SC_Struct)) {
		// Check for SC magic.
		const PS1_SC_Struct *const scHeader =
			reinterpret_cast<const PS1_SC_Struct*>(
				&info->header.pData[sizeof(PS1_PSV_Header)]);
		if (scHeader->magic == cpu_to_be16(PS1_SC_MAGIC)) {
			// Check the PSV magic.
			const PS1_PSV_Header *const psvHeader =
				reinterpret_cast<const PS1_PSV_Header*>(info->header.pData);
			if (psvHeader->magic == cpu_to_be64(PS1_PSV_MAGIC)) {
				// This is a PSV (PS1 on PS3) save file.
				return PlayStationSavePrivate::SAVE_TYPE_PSV;
			}

			// PSV magic is incorrect.
			return -1;
		}
	}

	// Check for Block Entry + SC.
	if (info->header.size >= sizeof(PS1_Block_Entry) + sizeof(PS1_SC_Struct)) {
		// Check for SC magic.
		const PS1_SC_Struct *const scHeader =
			reinterpret_cast<const PS1_SC_Struct*>(
				&info->header.pData[sizeof(PS1_Block_Entry)]);
		if (scHeader->magic == cpu_to_be16(PS1_SC_MAGIC)) {
			const uint8_t *const header = info->header.pData;

			// Check the block magic.
			static const uint8_t block_magic[4] = {
				PS1_ENTRY_ALLOC_FIRST, 0x00, 0x00, 0x00,
			};
			if (memcmp(header, block_magic, sizeof(block_magic)) != 0) {
				// Block magic is incorrect.
				return -1;
			}

			// Check the checksum
			uint8_t checksum = 0;
			for (int i = sizeof(PS1_Block_Entry)-1; i >= 0; i--) {
				checksum ^= header[i];
			}
			if (checksum != 0) return -1;

			return PlayStationSavePrivate::SAVE_TYPE_BLOCK;
		}
	}

	// Check for PS1 54.
	if (info->header.size >= sizeof(PS1_54_Header) + sizeof(PS1_SC_Struct)) {
		// Check for SC magic.
		const PS1_SC_Struct *const scHeader =
			reinterpret_cast<const PS1_SC_Struct*>(
				&info->header.pData[sizeof(PS1_54_Header)]);
		if (scHeader->magic == cpu_to_be16(PS1_SC_MAGIC)) {
			// Extra filesize check to prevent false-positives
			if (info->szFile % 8192 != 54) return -1;
			return PlayStationSavePrivate::SAVE_TYPE_54;
		}
	}

	// Check for PS1 SC by itself.
	const PS1_SC_Struct *const scHeader =
		reinterpret_cast<const PS1_SC_Struct*>(info->header.pData);
	if (scHeader->magic == cpu_to_be16(PS1_SC_MAGIC)) {
		// Extra filesize check to prevent false-positives
		if (info->szFile % 8192 != 0) return -1;
		return PlayStationSavePrivate::SAVE_TYPE_RAW;
	}

	// Not supported.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *PlayStationSave::systemName(unsigned int type) const
{
	RP_D(const PlayStationSave);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// PlayStation has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"PlayStationSave::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	static const char *const sysNames[4] = {
		// TODO: PS1 or PSX?
		"Sony PlayStation", "PlayStation", "PS1", nullptr
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
const char *const *PlayStationSave::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".psv",
		".mcb", ".mcx", ".pda", ".psx",
		".mcs", ".ps1",

		// TODO: support RAW?
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
const char *const *PlayStationSave::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"application/x-ps1-save",

		nullptr
	};
	return mimeTypes;
}

/**
 * Get a bitfield of image types this class can retrieve.
 * @return Bitfield of supported image types. (ImageTypesBF)
 */
uint32_t PlayStationSave::supportedImageTypes_static(void)
{
	return IMGBF_INT_ICON;
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
vector<RomData::ImageSizeDef> PlayStationSave::supportedImageSizes_static(ImageType imageType)
{
	ASSERT_supportedImageSizes(imageType);

	if (imageType != IMG_INT_ICON) {
		// Only icons are supported.
		return vector<ImageSizeDef>();
	}

	// PlayStation save files have 16x16 icons.
	static const ImageSizeDef sz_INT_ICON[] = {
		{nullptr, 16, 16, 0},
	};
	return vector<ImageSizeDef>(sz_INT_ICON,
		sz_INT_ICON + ARRAY_SIZE(sz_INT_ICON));
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
uint32_t PlayStationSave::imgpf(ImageType imageType) const
{
	ASSERT_imgpf(imageType);

	uint32_t ret = 0;
	switch (imageType) {
		case IMG_INT_ICON: {
			// Use nearest-neighbor scaling when resizing.
			// Also, need to check if this is an animated icon.
			RP_D(const PlayStationSave);
			const_cast<PlayStationSavePrivate*>(d)->loadIcon();
			if (d->iconAnimData && d->iconAnimData->count > 1) {
				// Animated icon.
				ret = IMGPF_RESCALE_NEAREST | IMGPF_ICON_ANIMATED;
			} else {
				// Not animated.
				ret = IMGPF_RESCALE_NEAREST;
			}
			break;
		}

		default:
			break;
	}
	return ret;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int PlayStationSave::loadFieldData(void)
{
	RP_D(PlayStationSave);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// ROM image isn't valid.
		return -EIO;
	}

	// PSV (PS1 on PS3) save file header.
	const PS1_SC_Struct *const scHeader = &d->scHeader;
	d->fields->reserve(2);	// Maximum of 2 fields.

	// Filename.
	const char* filename = nullptr;
	switch(d->saveType) {
	case PlayStationSavePrivate::SAVE_TYPE_PSV:
		filename = d->mxh.psvHeader.filename;
		break;
	case PlayStationSavePrivate::SAVE_TYPE_BLOCK:
		filename = d->mxh.blockHeader.filename;
		break;
	case PlayStationSavePrivate::SAVE_TYPE_54:
		filename = d->mxh.ps54Header.filename;
		break;
	}

	if (filename) {
		d->fields->addField_string(C_("PlayStationSave", "Filename"),
			cp1252_sjis_to_utf8(filename, 20));
	}

	// Description.
	d->fields->addField_string(C_("PlayStationSave", "Description"),
		cp1252_sjis_to_utf8(scHeader->title, sizeof(scHeader->title)));

	// TODO: Moar fields.

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int PlayStationSave::loadMetaData(void)
{
	RP_D(PlayStationSave);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// Unknown file type.
		return -EIO;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(1);	// Maximum of 1 metadata property.

	// PSV (PS1 on PS3) save file header.
	const PS1_SC_Struct *const scHeader = &d->scHeader;

	// Title. (Description)
	d->metaData->addMetaData_string(Property::Title,
		cp1252_sjis_to_utf8(scHeader->title, sizeof(scHeader->title)));

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
int PlayStationSave::loadInternalImage(ImageType imageType, const rp_image **pImage)
{
	ASSERT_loadInternalImage(imageType, pImage);

	RP_D(PlayStationSave);
	if (imageType != IMG_INT_ICON) {
		// Only IMG_INT_ICON is supported by PS1.
		*pImage = nullptr;
		return -ENOENT;
	} else if (d->iconAnimData) {
		// Image has already been loaded.
		// NOTE: PS1 icon animations are always sequential,
		// so we can use a shortcut here.
		*pImage = d->iconAnimData->frames[0];
		return 0;
	} else if (!d->file) {
		// File isn't open.
		*pImage = nullptr;
		return -EBADF;
	} else if (!d->isValid) {
		// Save file isn't valid.
		*pImage = nullptr;
		return -EIO;
	}

	// Load the icon.
	// TODO: -ENOENT if the file doesn't actually have an icon.
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
const IconAnimData *PlayStationSave::iconAnimData(void) const
{
	RP_D(const PlayStationSave);
	if (!d->iconAnimData) {
		// Load the icon.
		if (!const_cast<PlayStationSavePrivate*>(d)->loadIcon()) {
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

}
