/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * WiiWAD.cpp: Nintendo Wii WAD file reader.                               *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "librpbase/config.librpbase.h"

#include "WiiWAD.hpp"
#include "librpbase/RomData_p.hpp"

#include "gcn_structs.h"
#include "wii_structs.h"
#include "wii_wad.h"
#include "wii_banner.h"
#include "data/NintendoLanguage.hpp"
#include "data/WiiSystemMenuVersion.hpp"
#include "GameCubeRegions.hpp"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"
using namespace LibRpBase;

#include "libi18n/i18n.h"

// Decryption.
#include "librpbase/crypto/KeyManager.hpp"
#include "disc/WiiPartition.hpp"	// for key information
#ifdef ENABLE_DECRYPTION
# include "librpbase/crypto/AesCipherFactory.hpp"
# include "librpbase/crypto/IAesCipher.hpp"
# include "librpbase/disc/CBCReader.hpp"
// For sections delegated to other RomData subclasses.
# include "librpbase/disc/PartitionFile.hpp"
# include "WiiWIBN.hpp"
#endif /* ENABLE_DECRYPTION */

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

ROMDATA_IMPL(WiiWAD)
ROMDATA_IMPL_IMG(WiiWAD)

class WiiWADPrivate : public RomDataPrivate
{
	public:
		WiiWADPrivate(WiiWAD *q, IRpFile *file);
		~WiiWADPrivate();

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(WiiWADPrivate)

	public:
		// WAD type.
		enum WadType {
			WAD_UNKNOWN	= -1,	// Unknown WAD type.
			WAD_STANDARD	= 0,	// Standard WAD.
			WAD_EARLY	= 1,	// Early WAD. (early devkits only)
		};

		// WAD type.
		int wadType;

		// WAD structs.
		union {
			Wii_WAD_Header wad;
			Wii_WAD_Header_EARLY wadE;
		} wadHeader;
		RVL_Ticket ticket;
		RVL_TMD_Header tmdHeader;

		// Data offset and size.
		uint32_t data_offset;
		uint32_t data_size;

		// Name. (Early WADs only)
		string wadName;

		/**
		 * Round a value to the next highest multiple of 64.
		 * @param value Value.
		 * @return Next highest multiple of 64.
		 */
		template<typename T>
		static inline T toNext64(T val)
		{
			return (val + (T)63) & ~((T)63);
		}

#ifdef ENABLE_DECRYPTION
		// CBC reader for the main data area.
		CBCReader *cbcReader;
		WiiWIBN *wibnData;

		// Main data headers.
		Wii_Content_Bin_Header contentHeader;
		Wii_IMET_t imet;	// NOTE: May be WIBN.
#endif /* ENABLE_DECRYPTION */

		// Key index.
		WiiPartition::EncryptionKeys key_idx;
		// Key status.
		KeyManager::VerifyResult key_status;

		/**
		 * Get the game information string from the banner.
		 * @return Game information string, or empty string on error.
		 */
		string getGameInfo(void);
};

/** WiiWADPrivate **/

WiiWADPrivate::WiiWADPrivate(WiiWAD *q, IRpFile *file)
	: super(q, file)
	, wadType(WAD_UNKNOWN)
	, data_offset(0)
	, data_size(0)
#ifdef ENABLE_DECRYPTION
	, cbcReader(nullptr)
	, wibnData(nullptr)
#endif /* ENABLE_DECRYPTION */
	, key_idx(WiiPartition::Key_Max)
	, key_status(KeyManager::VERIFY_UNKNOWN)
{
	// Clear the various structs.
	memset(&wadHeader, 0, sizeof(wadHeader));
	memset(&ticket, 0, sizeof(ticket));
	memset(&tmdHeader, 0, sizeof(tmdHeader));

#ifdef ENABLE_DECRYPTION
	memset(&contentHeader, 0, sizeof(contentHeader));
	memset(&imet, 0, sizeof(imet));
#endif /* ENABLE_DECRYPTION */
}

WiiWADPrivate::~WiiWADPrivate()
{
#ifdef ENABLE_DECRYPTION
	if (wibnData) {
		wibnData->unref();
	}
	delete cbcReader;
#endif /* ENABLE_DECRYPTION */
}

/**
 * Get the game information string from the banner.
 * @return Game information string, or empty string on error.
 */
string WiiWADPrivate::getGameInfo(void)
{
#ifdef ENABLE_DECRYPTION
	// IMET header.
	// TODO: Read on demand instead of always reading in the constructor.
	if (imet.magic != cpu_to_be32(WII_IMET_MAGIC)) {
		// Not valid.
		return string();
	}

	// TODO: Combine with GameCubePrivate::wii_getBannerName()?

	// Get the system language.
	// TODO: Verify against the region code somehow?
	int lang = NintendoLanguage::getWiiLanguage();

	// If the language-specific name is empty,
	// revert to English.
	if (imet.names[lang][0][0] == 0) {
		// Revert to English.
		lang = WII_LANG_ENGLISH;
	}

	// NOTE: The banner may have two lines.
	// Each line is a maximum of 21 characters.
	// Convert from UTF-16 BE and split into two lines at the same time.
	string info = utf16be_to_utf8(imet.names[lang][0], 21);
	if (imet.names[lang][1][0] != 0) {
		info += '\n';
		info += utf16be_to_utf8(imet.names[lang][1], 21);
	}

	return info;
#else /* !ENABLE_DECRYPTION */
	// Unable to decrypt the IMET header.
	return string();
#endif /* ENABLE_DECRYPTION */
}

/** WiiWAD **/

/**
 * Read a Nintendo Wii WAD file.
 *
 * A WAD file must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the WAD file.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open disc image.
 */
WiiWAD::WiiWAD(IRpFile *file)
	: super(new WiiWADPrivate(this, file))
{
	// This class handles application packages.
	RP_D(WiiWAD);
	d->className = "WiiWAD";
	d->fileType = FTYPE_APPLICATION_PACKAGE;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the WAD header.
	d->file->rewind();
	size_t size = d->file->read(&d->wadHeader, sizeof(d->wadHeader));
	if (size != sizeof(d->wadHeader)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this WAD file is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(d->wadHeader);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->wadHeader);
	info.ext = nullptr;	// Not needed for WiiWAD.
	info.szFile = d->file->size();
	d->wadType = isRomSupported_static(&info);
	d->isValid = (d->wadType >= 0);
	if (!d->isValid) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Determine the addresses.
	uint32_t ticket_addr, tmd_addr;
	switch (d->wadType) {
		case WiiWADPrivate::WAD_STANDARD:
			// Standard WAD.
			// Sections are 64-byte aligned.
			ticket_addr = WiiWADPrivate::toNext64(be32_to_cpu(d->wadHeader.wad.header_size)) +
				      WiiWADPrivate::toNext64(be32_to_cpu(d->wadHeader.wad.cert_chain_size));
			tmd_addr = ticket_addr +
				      WiiWADPrivate::toNext64(be32_to_cpu(d->wadHeader.wad.ticket_size));

			// Data offset is after the TMD.
			// Data size is taken from the header.
			d->data_size = be32_to_cpu(d->wadHeader.wad.data_size);
			d->data_offset = tmd_addr +
				      WiiWADPrivate::toNext64(be32_to_cpu(d->wadHeader.wad.tmd_size));
			break;

		case WiiWADPrivate::WAD_EARLY: {
			// Early devkit WADs.
			// Sections are NOT 64-byte aligned,
			// and there's an extra "name" section after the TMD.
			ticket_addr = be32_to_cpu(d->wadHeader.wadE.header_size) +
				      be32_to_cpu(d->wadHeader.wadE.cert_chain_size);
			tmd_addr = ticket_addr + be32_to_cpu(d->wadHeader.wadE.ticket_size);

			// Data offset is explicitly specified.
			// Data size is assumed to be the rest of the file.
			d->data_offset = be32_to_cpu(d->wadHeader.wadE.data_offset);
			d->data_size = (uint32_t)d->file->size() - d->data_offset;

			// Read the name here, since it's only present in early WADs.
			const uint32_t name_size = be32_to_cpu(d->wadHeader.wadE.name_size);
			if (name_size > 0 && name_size <= 1024) {
				const uint32_t name_addr = tmd_addr + be32_to_cpu(d->wadHeader.wadE.tmd_size);
				unique_ptr<char[]> namebuf(new char[name_size]);
				size = d->file->seekAndRead(name_addr, namebuf.get(), name_size);
				if (size == name_size) {
					// TODO: Trim NULLs?
					d->wadName = string(namebuf.get(), name_size);
				}
			}
			break;
		}

		default:
			assert(!"Should not get here...");
			d->file->unref();
			d->file = nullptr;
			return;
	}

	// Read the ticket and TMD.
	// TODO: Verify ticket/TMD sizes.
	size = d->file->seekAndRead(ticket_addr, &d->ticket, sizeof(d->ticket));
	if (size != sizeof(d->ticket)) {
		// Seek and/or read error.
		d->isValid = false;
		d->file->unref();
		d->file = nullptr;
		return;
	}
	size = d->file->seekAndRead(tmd_addr, &d->tmdHeader, sizeof(d->tmdHeader));
	if (size != sizeof(d->tmdHeader)) {
		// Seek and/or read error.
		d->isValid = false;
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Determine the key index and debug vs. retail.
	static const char issuer_rvt[] = "Root-CA00000002-XS00000006";
	if (!memcmp(d->ticket.signature_issuer, issuer_rvt, sizeof(issuer_rvt))) {
		// Debug encryption.
		d->key_idx = WiiPartition::Key_Rvt_Debug;
	} else {
		// Retail encryption.
		uint8_t idx = d->ticket.common_key_index;
		if (idx > 2) {
			// Out of range. Assume Wii common key.
			idx = 0;
		}
		d->key_idx = (WiiPartition::EncryptionKeys)idx;
	}

#ifdef ENABLE_DECRYPTION
	// Initialize the CBC reader for the main data area.

	// TODO: WiiVerifyKeys class.
	KeyManager *const keyManager = KeyManager::instance();
	assert(keyManager != nullptr);

	// Key verification data.
	// TODO: Move out of WiiPartition and into WiiVerifyKeys?
	const char *const keyName = WiiPartition::encryptionKeyName_static(d->key_idx);
	const uint8_t *const verifyData = WiiPartition::encryptionVerifyData_static(d->key_idx);
	assert(keyName != nullptr);
	assert(keyName[0] != '\0');
	assert(verifyData != nullptr);

	// Get and verify the key.
	KeyManager::KeyData_t keyData;
	d->key_status = keyManager->getAndVerify(keyName, &keyData, verifyData, 16);
	if (d->key_status != KeyManager::VERIFY_OK) {
		// Unable to get and verify the key.
		return;
	}

	// Create a cipher to decrypt the title key.
	IAesCipher *cipher = AesCipherFactory::create();

	// Initialize parameters for title key decryption.
	// TODO: Error checking.
	// Parameters:
	// - Chaining mode: CBC
	// - IV: Title ID (little-endian)
	cipher->setChainingMode(IAesCipher::CM_CBC);
	cipher->setKey(keyData.key, keyData.length);
	// Title key IV: High 8 bytes are the title ID (in big-endian), low 8 bytes are 0.
	uint8_t iv[16];
	memcpy(iv, &d->ticket.title_id.id, sizeof(d->ticket.title_id.id));
	memset(&iv[8], 0, 8);
	cipher->setIV(iv, sizeof(iv));
	
	// Decrypt the title key.
	uint8_t title_key[16];
	memcpy(title_key, d->ticket.enc_title_key, sizeof(d->ticket.enc_title_key));
	cipher->decrypt(title_key, sizeof(title_key));
	delete cipher;

	// Data area IV:
	// - First two bytes are the big-endian content index.
	// - Remaining bytes are zero.
	// - TODO: Read the TMD content table. For now, assuming index 0.
	memset(iv, 0, sizeof(iv));

	// Create a CBC reader to decrypt the data section.
	// TODO: Verify some known data?
	d->cbcReader = new CBCReader(d->file, d->data_offset, d->data_size, title_key, iv);

	// Read the content header.
	// NOTE: Continuing even if this fails, since we can show
	// other information from the ticket and TMD.
	size = d->cbcReader->read(&d->contentHeader, sizeof(d->contentHeader));
	if (size == sizeof(d->contentHeader)) {
		// Contents may be one of the following:
		// - IMET header: Most common.
		// - WIBN header: DLC titles.
		size = d->cbcReader->read(&d->imet, sizeof(d->imet));
		if (size == sizeof(d->imet)) {
			// TODO: Use the WiiWIBN subclass.
			// TODO: Create a WiiIMET subclass? (and also use it in GameCube)
			if (d->imet.magic == cpu_to_be32(WII_IMET_MAGIC)) {
				// This is an IMET header.
				// TODO: Do something here?
			} else if (d->imet.magic == cpu_to_be32(WII_WIBN_MAGIC)) {
				// This is a WIBN header.
				// Create the PartitionFile and WiiWIBN subclass.
				// NOTE: Not sure how big the WIBN data is, so we'll
				// allow it to read the rest of the file.
				PartitionFile *const ptFile = new PartitionFile(d->cbcReader,
					sizeof(d->contentHeader),
					d->data_size - sizeof(d->contentHeader));
				if (ptFile->isOpen()) {
					// Open the WiiWIBN.
					WiiWIBN *const wibn = new WiiWIBN(ptFile);
					if (wibn->isOpen()) {
						// Opened successfully.
						d->wibnData = wibn;
					} else {
						// Unable to open the WiiWIBN.
						wibn->unref();
					}
				}
				ptFile->unref();
			}
		}
	}
#else /* !ENABLE_DECRYPTION */
	// Cannot decrypt anything...
	d->key_status = KeyManager::VERIFY_NO_SUPPORT;
#endif /* ENABLE_DECRYPTION */
}

/**
 * Close the opened file.
 */
void WiiWAD::close(void)
{
#ifdef ENABLE_DECRYPTION
	RP_D(WiiWAD);

	// Close any child RomData subclasses.
	if (d->wibnData) {
		d->wibnData->close();
	}

	// Close associated files used with child RomData subclasses.
	delete d->cbcReader;
	d->cbcReader = nullptr;
#endif /* ENABLE_DECRYPTION */

	// Call the superclass function.
	super::close();
}

/** ROM detection functions. **/

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int WiiWAD::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(Wii_WAD_Header))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Check for the correct header fields.
	const Wii_WAD_Header *wadHeader = reinterpret_cast<const Wii_WAD_Header*>(info->header.pData);
	if (wadHeader->header_size != cpu_to_be32(sizeof(*wadHeader))) {
		// WAD header size is incorrect.
		return -1;
	}

	// Check WAD type.
	if (wadHeader->type != cpu_to_be32(WII_WAD_TYPE_Is) &&
	    wadHeader->type != cpu_to_be32(WII_WAD_TYPE_ib) &&
	    wadHeader->type != cpu_to_be32(WII_WAD_TYPE_Bk))
	{
		// WAD type is incorrect.
		// NOTE: This may be an early WAD.
		const Wii_WAD_Header_EARLY *wadE = reinterpret_cast<const Wii_WAD_Header_EARLY*>(wadHeader);
		if (wadE->ticket_size == cpu_to_be32(sizeof(RVL_Ticket))) {
			// Ticket size is correct.
			// This is probably an early WAD.
			return WiiWADPrivate::WAD_EARLY;
		}

		// Not supported.
		return -1;
	}

	// Verify the ticket size.
	// TODO: Also the TMD size.
	if (be32_to_cpu(wadHeader->ticket_size) < sizeof(RVL_Ticket)) {
		// Ticket is too small.
		return -1;
	}
	
	// Check the file size to ensure we have at least the IMET section.
	unsigned int expected_size = WiiWADPrivate::toNext64(be32_to_cpu(wadHeader->header_size)) +
				     WiiWADPrivate::toNext64(be32_to_cpu(wadHeader->cert_chain_size)) +
				     WiiWADPrivate::toNext64(be32_to_cpu(wadHeader->ticket_size)) +
				     WiiWADPrivate::toNext64(be32_to_cpu(wadHeader->tmd_size)) +
				     sizeof(Wii_Content_Bin_Header);
	if (expected_size > info->szFile) {
		// File is too small.
		return -1;
	}

	// This appears to be a Wii WAD file.
	return WiiWADPrivate::WAD_STANDARD;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *WiiWAD::systemName(unsigned int type) const
{
	RP_D(const WiiWAD);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// Wii has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"WiiWAD::systemName() array index optimization needs to be updated.");

	static const char *const sysNames[4] = {
		"Nintendo Wii", "Wii", "Wii", nullptr
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
const char *const *WiiWAD::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".wad",

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
const char *const *WiiWAD::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types from FreeDesktop.org.
		"application/x-wii-wad",

		nullptr
	};
	return mimeTypes;
}

/**
 * Get a bitfield of image types this class can retrieve.
 * @return Bitfield of supported image types. (ImageTypesBF)
 */
uint32_t WiiWAD::supportedImageTypes_static(void)
{
	// TODO: Only return IMG_INT_* if a WiiWIBN is available.
	return IMGBF_INT_ICON | IMGBF_INT_BANNER |
	       IMGBF_EXT_COVER | IMGBF_EXT_COVER_3D |
	       IMGBF_EXT_COVER_FULL |
	       IMGBF_EXT_TITLE_SCREEN;
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
vector<RomData::ImageSizeDef> WiiWAD::supportedImageSizes_static(ImageType imageType)
{
	ASSERT_supportedImageSizes(imageType);

	switch (imageType) {
		// TODO: Only return IMG_INT_* if a WiiWIBN is available.
		case IMG_INT_ICON: {
			static const ImageSizeDef sz_INT_ICON[] = {
				{nullptr, BANNER_WIBN_ICON_W, BANNER_WIBN_ICON_H, 0},
			};
			return vector<ImageSizeDef>(sz_INT_ICON,
				sz_INT_ICON + ARRAY_SIZE(sz_INT_ICON));
		}
		case IMG_INT_BANNER: {
			static const ImageSizeDef sz_INT_BANNER[] = {
				{nullptr, BANNER_WIBN_IMAGE_W, BANNER_WIBN_IMAGE_H, 0},
			};
			return vector<ImageSizeDef>(sz_INT_BANNER,
				sz_INT_BANNER + ARRAY_SIZE(sz_INT_BANNER));
		}

		case IMG_EXT_COVER: {
			static const ImageSizeDef sz_EXT_COVER[] = {
				{nullptr, 160, 224, 0},
			};
			return vector<ImageSizeDef>(sz_EXT_COVER,
				sz_EXT_COVER + ARRAY_SIZE(sz_EXT_COVER));
		}
		case IMG_EXT_COVER_3D: {
			static const ImageSizeDef sz_EXT_COVER_3D[] = {
				{nullptr, 176, 248, 0},
			};
			return vector<ImageSizeDef>(sz_EXT_COVER_3D,
				sz_EXT_COVER_3D + ARRAY_SIZE(sz_EXT_COVER_3D));
		}
		case IMG_EXT_COVER_FULL: {
			static const ImageSizeDef sz_EXT_COVER_FULL[] = {
				{nullptr, 512, 340, 0},
				{"HQ", 1024, 680, 1},
			};
			return vector<ImageSizeDef>(sz_EXT_COVER_FULL,
				sz_EXT_COVER_FULL + ARRAY_SIZE(sz_EXT_COVER_FULL));
		}
		case IMG_EXT_TITLE_SCREEN: {
			static const ImageSizeDef sz_EXT_TITLE_SCREEN[] = {
				{nullptr, 192, 112, 0},
			};
			return vector<ImageSizeDef>(sz_EXT_TITLE_SCREEN,
				sz_EXT_TITLE_SCREEN + ARRAY_SIZE(sz_EXT_TITLE_SCREEN));
		}
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
int WiiWAD::loadFieldData(void)
{
	RP_D(WiiWAD);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file || !d->file->isOpen()) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->wadType < 0) {
		// Unknown file type.
		return -EIO;
	}

	// WAD headers are read in the constructor.
	const RVL_TMD_Header *const tmdHeader = &d->tmdHeader;
	d->fields->reserve(11);	// Maximum of 11 fields.

	if (d->key_status != KeyManager::VERIFY_OK) {
		// Unable to get the decryption key.
		const char *err = KeyManager::verifyResultToString(d->key_status);
		if (!err) {
			err = C_("WiiWAD", "Unknown error. (THIS IS A BUG!)");
		}
		d->fields->addField_string(C_("WiiWAD", "Warning"),
			err, RomFields::STRF_WARNING);
	}

	// Type.
	string s_wadType;
	switch (d->wadType) {
		case WiiWADPrivate::WAD_STANDARD: {
			char buf[4];
			memcpy(buf, &d->wadHeader.wad.type, sizeof(buf));
			buf[2] = '\0';
			buf[3] = '\0';
			s_wadType = string(buf, 2);
			break;
		};

		case WiiWADPrivate::WAD_EARLY:
			s_wadType = C_("WiiWAD", "Early Devkit");
			break;

		default:
			s_wadType = C_("RomData", "Unknown");
			break;
	}
	d->fields->addField_string(C_("WiiWAD", "Type"), s_wadType);

	// Internal name. (Early WADs only)
	if (!d->wadName.empty()) {
		d->fields->addField_string(C_("RomData", "Name"), d->wadName);
	}

	// Title ID.
	// TODO: Make sure the ticket title ID matches the TMD title ID.
	d->fields->addField_string(C_("WiiWAD", "Title ID"),
		rp_sprintf("%08X-%08X",
			be32_to_cpu(tmdHeader->title_id.hi),
			be32_to_cpu(tmdHeader->title_id.lo)));

	// Game ID.
	// NOTE: Only displayed if TID lo is all alphanumeric characters.
	// TODO: Only for certain TID hi?
	if (ISALNUM(tmdHeader->title_id.u8[4]) &&
	    ISALNUM(tmdHeader->title_id.u8[5]) &&
	    ISALNUM(tmdHeader->title_id.u8[6]) &&
	    ISALNUM(tmdHeader->title_id.u8[7]))
	{
		// Print the game ID.
		// TODO: Is the publisher code available anywhere?
		d->fields->addField_string(C_("WiiWAD", "Game ID"),
			rp_sprintf("%.4s", reinterpret_cast<const char*>(&tmdHeader->title_id.u8[4])));
	}

	// Title version.
	const uint16_t title_version = be16_to_cpu(tmdHeader->title_version);
	d->fields->addField_string(C_("WiiWAD", "Title Version"),
		rp_sprintf("%u.%u (v%u)", title_version >> 8, title_version & 0xFF, title_version));

	// Title ID constants.
	const uint32_t tid_hi = be32_to_cpu(tmdHeader->title_id.hi);

	// Region code.
	unsigned int gcnRegion;
	if (tid_hi == 0x00000001) {
		// IOS and/or System Menu.
		if (tmdHeader->title_id.lo == cpu_to_be32(0x00000002)) {
			// System Menu.
			const char *ver = WiiSystemMenuVersion::lookup(title_version);
			if (ver) {
				switch (ver[3]) {
					case 'J':
						gcnRegion = GCN_REGION_JPN;
						break;
					case 'U':
						gcnRegion = GCN_REGION_USA;
						break;
					case 'E':
						gcnRegion = GCN_REGION_EUR;
						break;
					case 'K':
						gcnRegion = GCN_REGION_KOR;
						break;
					case 'C':
						gcnRegion = GCN_REGION_CHN;
						break;
					case 'T':
						gcnRegion = GCN_REGION_TWN;
						break;
					default:
						gcnRegion = 255;
						break;
				}
			} else {
				gcnRegion = 255;
			}
		} else {
			// IOS, BC, or MIOS. No region.
			gcnRegion = GCN_REGION_ALL;
		}
	} else {
		gcnRegion = be16_to_cpu(tmdHeader->region_code);
	}

	bool isDefault;
	const char id4_region = (char)tmdHeader->title_id.u8[7];
	const char *const region =
		GameCubeRegions::gcnRegionToString(gcnRegion, id4_region, &isDefault);
	const char *const region_code_title = C_("RomData", "Region Code");
	if (region) {
		// Append the GCN region name (USA/JPN/EUR/KOR) if
		// the ID4 value differs.
		const char *suffix = nullptr;
		if (!isDefault) {
			suffix = GameCubeRegions::gcnRegionToAbbrevString(gcnRegion);
		}

		string s_region;
		if (suffix) {
			// tr: %1%s == full region name, %2$s == abbreviation
			s_region = rp_sprintf_p(C_("WiiWAD", "%1$s (%2$s)"), region, suffix);
		} else {
			s_region = region;
		}

		d->fields->addField_string(region_code_title, s_region);
	} else {
		d->fields->addField_string(region_code_title,
			rp_sprintf(C_("RomData", "Unknown (0x%02X)"), gcnRegion));
	}

	// Required IOS version.
	const char *const ios_version_title = C_("WiiWAD", "IOS Version");
	const uint32_t ios_lo = be32_to_cpu(tmdHeader->sys_version.lo);
	if (tmdHeader->sys_version.hi == cpu_to_be32(0x00000001) &&
	    ios_lo > 2 && ios_lo < 0x300)
	{
		// Standard IOS slot.
		d->fields->addField_string(ios_version_title,
			rp_sprintf("IOS%u", ios_lo));
	} else if (tmdHeader->sys_version.id != 0) {
		// Non-standard IOS slot.
		// Print the full title ID.
		d->fields->addField_string(ios_version_title,
			rp_sprintf("%08X-%08X",
				be32_to_cpu(tmdHeader->sys_version.hi),
				be32_to_cpu(tmdHeader->sys_version.lo)));
	}

	// Access rights.
	vector<string> *const v_access_rights_hdr = new vector<string>();
	v_access_rights_hdr->reserve(2);
	v_access_rights_hdr->push_back("AHBPROT");
	v_access_rights_hdr->push_back(C_("WiiWAD", "DVD Video"));
	d->fields->addField_bitfield(C_("WiiWAD", "Access Rights"),
		v_access_rights_hdr, 0, be32_to_cpu(tmdHeader->access_rights));

	if (tid_hi >= 0x00010000) {
		// Get age rating(s).
		// TODO: Combine with GameCube::addFieldData()'s code.
		// Note that not all 16 fields are present on GCN,
		// though the fields do match exactly, so no
		// mapping is necessary.
		RomFields::age_ratings_t age_ratings;
		// Valid ratings: 0-1, 3-9
		static const uint16_t valid_ratings = 0x3FB;

		for (int i = static_cast<int>(age_ratings.size())-1; i >= 0; i--) {
			if (!(valid_ratings & (1 << i))) {
				// Rating is not applicable for GCN.
				age_ratings[i] = 0;
				continue;
			}

			// GCN ratings field:
			// - 0x1F: Age rating.
			// - 0x20: Has online play if set.
			// - 0x80: Unused if set.
			const uint8_t rvl_rating = tmdHeader->ratings[i];
			if (rvl_rating & 0x80) {
				// Rating is unused.
				age_ratings[i] = 0;
				continue;
			}
			// Set active | age value.
			age_ratings[i] = RomFields::AGEBF_ACTIVE | (rvl_rating & 0x1F);

			// Is "rating may change during online play" set?
			if (rvl_rating & 0x20) {
				age_ratings[i] |= RomFields::AGEBF_ONLINE_PLAY;
			}
		}
		d->fields->addField_ageRatings(C_("RomData", "Age Ratings"), age_ratings);
	}

	// Encryption key.
	// TODO: WiiPartition function to get a key's "display name"?
	static const char encKeyNames[][8] = {
		NOP_C_("WiiWAD|EncKey", "Retail"),
		NOP_C_("WiiWAD|EncKey", "Korean"),
		NOP_C_("WiiWAD|EncKey", "vWii"),
		NOP_C_("WiiWAD|EncKey", "SD AES"),
		NOP_C_("WiiWAD|EncKey", "SD IV"),
		NOP_C_("WiiWAD|EncKey", "SD MD5"),
		NOP_C_("WiiWAD|EncKey", "Debug"),
	};
	static_assert(ARRAY_SIZE(encKeyNames) == WiiPartition::Key_Max, "Update encKeyNames[]!");
	const char *keyName;
	if (d->key_idx >= 0 && d->key_idx < WiiPartition::Key_Max) {
		keyName = dpgettext_expr(RP_I18N_DOMAIN, "WiiWAD|EncKey", encKeyNames[d->key_idx]);
	} else {
		keyName = C_("WiiWAD", "Unknown");
	}
	d->fields->addField_string(C_("WiiWAD", "Encryption Key"), keyName);

#ifdef ENABLE_DECRYPTION
	// Do we have a WIBN header?
	// If so, we don't have IMET data.
	if (d->wibnData) {
		// Add the WIBN data.
		const RomFields *const wibnFields = d->wibnData->fields();
		assert(wibnFields != nullptr);
		if (wibnFields) {
			d->fields->addFields_romFields(wibnFields, 0);
		}
	} else
#endif /* ENABLE_DECRYPTION */
	{
		// No WIBN data.
		// Get the IMET data if it's available.
		string gameInfo = d->getGameInfo();
		if (!gameInfo.empty()) {
			d->fields->addField_string(C_("WiiWAD", "Game Info"), gameInfo);
		}
	}

	// TODO: Decrypt content.bin to get the actual data.

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int WiiWAD::loadMetaData(void)
{
	RP_D(WiiWAD);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->wadType < 0) {
		// Unknown file type.
		return -EIO;
	}

	// TODO: Game title from WIBN if it's available.

	// NOTE: We can only get the title if the encryption key is valid.
	// If we can't get the title, don't bother creating RomMetaData.
	string gameInfo = d->getGameInfo();
	if (gameInfo.empty()) {
		return -EIO;
	}
	const size_t nl_pos = gameInfo.find('\n');
	if (nl_pos != string::npos) {
		gameInfo.resize(nl_pos);
	}
	if (gameInfo.empty()) {
		return -EIO;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(1);	// Maximum of 1 metadata property.

	// Title. (first line of game info)
	d->metaData->addMetaData_string(Property::Title, gameInfo);

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
int WiiWAD::loadInternalImage(ImageType imageType, const rp_image **pImage)
{
	ASSERT_loadInternalImage(imageType, pImage);

	RP_D(WiiWAD);
	if (!d->isValid) {
		// Banner file isn't valid.
		*pImage = nullptr;
		return -EIO;
	}

#ifdef ENABLE_DECRYPTION
	// Forward this call to the WiiWIBN object.
	if (d->wibnData) {
		return d->wibnData->loadInternalImage(imageType, pImage);
	}
#endif /* ENABLE_DECRYPTION */

	// No WiiWIBN object.
	*pImage = nullptr;
	return -ENOENT;
}

/**
 * Get the animated icon data.
 *
 * Check imgpf for IMGPF_ICON_ANIMATED first to see if this
 * object has an animated icon.
 *
 * @return Animated icon data, or nullptr if no animated icon is present.
 */
const IconAnimData *WiiWAD::iconAnimData(void) const
{
#ifdef ENABLE_DECRYPTION
	// Forward this call to the WiiWIBN object.
	RP_D(const WiiWAD);
	if (d->wibnData) {
		return d->wibnData->iconAnimData();
	}
#endif /* ENABLE_DECRYPTION */

	// No WiiWIBN object.
	return nullptr;
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
int WiiWAD::extURLs(ImageType imageType, vector<ExtURL> *pExtURLs, int size) const
{
	ASSERT_extURLs(imageType, pExtURLs);
	pExtURLs->clear();

	// Check if a WiiWIBN is present.
	// If it is, this is a DLC WAD, so the title ID
	// won't match anything on GameTDB.
	RP_D(const WiiWAD);
	if (!d->isValid || d->wadType < 0) {
		// WAD isn't valid.
		return -EIO;
	} else
#ifdef ENABLE_DECRYPTION
	if (d->wibnData) {
		// WiiWIBN is present.
		// This means the boxart is not available on GameTDB,
		// since it's a DLC WAD.
		return -ENOENT;
	} else
#endif /* ENABLE_DECRYPTION */
	{
		// If the first letter of the ID4 is lowercase,
		// that means it's a DLC title. GameTDB doesn't
		// have artwork for DLC titles.
		char sysID = be32_to_cpu(d->tmdHeader.title_id.lo) >> 24;
		if (ISLOWER(sysID)) {
			// It's lowercase.
			return -ENOENT;
		}
	}

	// TMD Header.
	const RVL_TMD_Header *const tmdHeader = &d->tmdHeader;

	// Check for a valid TID hi.
	switch (be32_to_cpu(tmdHeader->title_id.hi)) {
		case 0x00010000:
		case 0x00010001:
		case 0x00010002:
		case 0x00010004:
		case 0x00010005:
		case 0x00010008:
			// TID hi is valid.
			break;

		default:
			// No GameTDB artwork is available.
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
		case IMG_EXT_COVER:
			imageTypeName_base = "cover";
			ext = ".png";
			break;
		case IMG_EXT_COVER_3D:
			imageTypeName_base = "cover3D";
			ext = ".png";
			break;
		case IMG_EXT_COVER_FULL:
			imageTypeName_base = "coverfull";
			ext = ".png";
			break;
		case IMG_EXT_TITLE_SCREEN:
			imageTypeName_base = "wwtitle";
			ext = ".png";
			break;
		default:
			// Unsupported image type.
			return -ENOENT;
	}

	// Game ID. (GameTDB uses ID4 for WiiWare.)
	// The ID4 cannot have non-printable characters.
	// NOTE: Must be NULL-terminated.
	char id4[5];
	memcpy(id4, &tmdHeader->title_id.u8[4], 4);
	id4[4] = 0;
	for (int i = 4-1; i >= 0; i--) {
		if (!ISPRINT(id4[i])) {
			// Non-printable character found.
			return -ENOENT;
		}
	}

	// Determine the GameTDB region code(s).
	const unsigned int gcnRegion = be16_to_cpu(tmdHeader->region_code);
	const char id4_region = (char)tmdHeader->title_id.u8[7];
	vector<const char*> tdb_regions =
		GameCubeRegions::gcnRegionToGameTDB(gcnRegion, id4_region);

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
			extURL_iter->url = d->getURL_GameTDB("wii", imageTypeName, *tdb_iter, id4, ext);
			extURL_iter->cache_key = d->getCacheKey_GameTDB("wii", imageTypeName, *tdb_iter, id4, ext);
			extURL_iter->width = szdefs_dl[i]->width;
			extURL_iter->height = szdefs_dl[i]->height;
			extURL_iter->high_res = (szdefs_dl[i]->index >= 2);
		}
	}

	// All URLs added.
	return 0;
}

}
