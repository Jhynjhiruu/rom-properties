/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * KhronosKTX.cpp: Khronos KTX image reader.                               *
 *                                                                         *
 * Copyright (c) 2017-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

/**
 * References:
 * - https://www.khronos.org/opengles/sdk/tools/KTX/file_format_spec/
 */

#include "KhronosKTX.hpp"
#include "librpbase/RomData_p.hpp"

#include "ktx_structs.h"
#include "data/GLenumStrings.hpp"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/aligned_malloc.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"

#include "librpbase/img/rp_image.hpp"
#include "librpbase/img/ImageDecoder.hpp"

#include "libi18n/i18n.h"
using namespace LibRpBase;

// C includes. (C++ namespace)
#include <cassert>
#include <cerrno>
#include <cstring>

// C++ includes.
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using std::string;
using std::unique_ptr;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(KhronosKTX)
ROMDATA_IMPL_IMG_TYPES(KhronosKTX)

class KhronosKTXPrivate : public RomDataPrivate
{
	public:
		KhronosKTXPrivate(KhronosKTX *q, IRpFile *file);
		~KhronosKTXPrivate();

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(KhronosKTXPrivate)

	public:
		// KTX header.
		KTX_Header ktxHeader;

		// Is byteswapping needed?
		// (KTX file has the opposite endianness.)
		bool isByteswapNeeded;

		// Is HFlip/VFlip needed?
		// Some textures may be stored upside-down due to
		// the way GL texture coordinates are interpreted.
		// Default without KTXorientation is HFlip=false, VFlip=true
		uint8_t isFlipNeeded;
		enum FlipBits : uint8_t {
			FLIP_NONE	= 0,
			FLIP_V		= (1 << 0),
			FLIP_H		= (1 << 1),
			FLIP_HV		= FLIP_H | FLIP_V,
		};

		// Texture data start address.
		unsigned int texDataStartAddr;

		// Decoded image.
		rp_image *img;

		// Key/Value data.
		// NOTE: Stored as vector<vector<string> > instead of
		// vector<pair<string, string> > for compatibility with
		// RFT_LISTDATA.
		vector<vector<string> > kv_data;

		/**
		 * Load the image.
		 * @return Image, or nullptr on error.
		 */
		const rp_image *loadImage(void);

		/**
		 * Load key/value data.
		 */
		void loadKeyValueData(void);
};

/** KhronosKTXPrivate **/

KhronosKTXPrivate::KhronosKTXPrivate(KhronosKTX *q, IRpFile *file)
	: super(q, file)
	, isByteswapNeeded(false)
	, isFlipNeeded(FLIP_V)
	, texDataStartAddr(0)
	, img(nullptr)
{
	// Clear the KTX header struct.
	memset(&ktxHeader, 0, sizeof(ktxHeader));
}

KhronosKTXPrivate::~KhronosKTXPrivate()
{
	delete img;
}

/**
 * Load the image.
 * @return Image, or nullptr on error.
 */
const rp_image *KhronosKTXPrivate::loadImage(void)
{
	if (img) {
		// Image has already been loaded.
		return img;
	} else if (!this->file || !this->isValid) {
		// Can't load the image.
		return nullptr;
	}

	// Sanity check: Maximum image dimensions of 32768x32768.
	// NOTE: `pixelHeight == 0` is allowed here. (1D texture)
	assert(ktxHeader.pixelWidth > 0);
	assert(ktxHeader.pixelWidth <= 32768);
	assert(ktxHeader.pixelHeight <= 32768);
	if (ktxHeader.pixelWidth == 0 || ktxHeader.pixelWidth > 32768 ||
	    ktxHeader.pixelHeight > 32768)
	{
		// Invalid image dimensions.
		return nullptr;
	}

	// Texture cannot start inside of the KTX header.
	assert(texDataStartAddr >= sizeof(ktxHeader));
	if (texDataStartAddr < sizeof(ktxHeader)) {
		// Invalid texture data start address.
		return nullptr;
	}

	if (file->size() > 128*1024*1024) {
		// Sanity check: KTX files shouldn't be more than 128 MB.
		return nullptr;
	}
	const uint32_t file_sz = static_cast<uint32_t>(file->size());

	// Seek to the start of the texture data.
	int ret = file->seek(texDataStartAddr);
	if (ret != 0) {
		// Seek error.
		return nullptr;
	}

	// NOTE: Mipmaps are stored *after* the main image.
	// Hence, no mipmap processing is necessary.

	// Handle a 1D texture as a "width x 1" 2D texture.
	// NOTE: Handling a 3D texture as a single 2D texture.
	const int height = (ktxHeader.pixelHeight > 0 ? ktxHeader.pixelHeight : 1);

	// Calculate the expected size.
	// NOTE: Scanlines are 4-byte aligned.
	uint32_t expected_size;
	int stride = 0;
	switch (ktxHeader.glFormat) {
		case GL_RGB:
			// 24-bit RGB.
			stride = ALIGN(4, ktxHeader.pixelWidth * 3);
			expected_size = static_cast<unsigned int>(stride * height);
			break;

		case GL_RGBA:
			// 32-bit RGBA.
			stride = ktxHeader.pixelWidth * 4;
			expected_size = static_cast<unsigned int>(stride * height);
			break;

		case GL_LUMINANCE:
			// 8-bit luminance.
			stride = ALIGN(4, ktxHeader.pixelWidth);
			expected_size = static_cast<unsigned int>(stride * height);
			break;

		case 0:
		default:
			// May be a compressed format.
			// TODO: Stride calculations?
			switch (ktxHeader.glInternalFormat) {
				case GL_RGB_S3TC:
				case GL_RGB4_S3TC:
				case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
				case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
				case GL_ETC1_RGB8_OES:
				case GL_COMPRESSED_R11_EAC:
				case GL_COMPRESSED_SIGNED_R11_EAC:
				case GL_COMPRESSED_RGB8_ETC2:
				case GL_COMPRESSED_SRGB8_ETC2:
				case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
				case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
				case GL_COMPRESSED_RED_RGTC1:
				case GL_COMPRESSED_SIGNED_RED_RGTC1:
				case GL_COMPRESSED_LUMINANCE_LATC1_EXT:
				case GL_COMPRESSED_SIGNED_LUMINANCE_LATC1_EXT:
					// 16 pixels compressed into 64 bits. (4bpp)
					expected_size = (ktxHeader.pixelWidth * height) / 2;
					break;

				//case GL_RGBA_S3TC:	// TODO
				//case GL_RGBA4_S3TC:	// TODO
				case GL_RGBA_DXT5_S3TC:
				case GL_RGBA4_DXT5_S3TC:
				case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
				case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
				case GL_COMPRESSED_RG11_EAC:
				case GL_COMPRESSED_SIGNED_RG11_EAC:
				case GL_COMPRESSED_RGBA8_ETC2_EAC:
				case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
				case GL_COMPRESSED_RG_RGTC2:
				case GL_COMPRESSED_SIGNED_RG_RGTC2:
				case GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT:
				case GL_COMPRESSED_SIGNED_LUMINANCE_ALPHA_LATC2_EXT:
					// 16 pixels compressed into 128 bits. (8bpp)
					expected_size = ktxHeader.pixelWidth * height;
					break;

				default:
					// Not supported.
					return nullptr;
			}
			break;
	}

	// Verify file size.
	if (texDataStartAddr + expected_size > file_sz) {
		// File is too small.
		return nullptr;
	}

	// Read the image size field.
	uint32_t imageSize;
	size_t size = file->read(&imageSize, sizeof(imageSize));
	if (size != sizeof(imageSize)) {
		// Unable to read the image size field.
		return nullptr;
	}
	if (isByteswapNeeded) {
		imageSize = __swab32(imageSize);
	}
	if (imageSize != expected_size) {
		// Size is incorrect.
		return nullptr;
	}

	// Read the texture data.
	auto buf = aligned_uptr<uint8_t>(16, expected_size);
	size = file->read(buf.get(), expected_size);
	if (size != expected_size) {
		// Read error.
		return nullptr;
	}

	// TODO: Byteswapping.
	// TODO: Handle variants. Check for channel sizes in glInternalFormat?
	// TODO: Handle sRGB post-processing? (for e.g. GL_SRGB8)
	switch (ktxHeader.glFormat) {
		case GL_RGB:
			// 24-bit RGB.
			img = ImageDecoder::fromLinear24(ImageDecoder::PXF_BGR888,
				ktxHeader.pixelWidth, height,
				buf.get(), expected_size, stride);
			break;

		case GL_RGBA:
			// 32-bit RGBA.
			img = ImageDecoder::fromLinear32(ImageDecoder::PXF_ABGR8888,
				ktxHeader.pixelWidth, height,
				reinterpret_cast<const uint32_t*>(buf.get()), expected_size, stride);
			break;

		case GL_LUMINANCE:
			// 8-bit Luminance.
			img = ImageDecoder::fromLinear8(ImageDecoder::PXF_L8,
				ktxHeader.pixelWidth, height,
				buf.get(), expected_size, stride);
			break;

		case 0:
		default:
			// May be a compressed format.
			// TODO: sRGB post-processing for sRGB formats?
			switch (ktxHeader.glInternalFormat) {
				case GL_RGB_S3TC:
				case GL_RGB4_S3TC:
				case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
					// DXT1-compressed texture.
					img = ImageDecoder::fromDXT1(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
					// DXT1-compressed texture with 1-bit alpha.
					img = ImageDecoder::fromDXT1_A1(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
					// DXT3-compressed texture.
					img = ImageDecoder::fromDXT3(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_RGBA_DXT5_S3TC:
				case GL_RGBA4_DXT5_S3TC:
				case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
					// DXT5-compressed texture.
					img = ImageDecoder::fromDXT5(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_ETC1_RGB8_OES:
					// ETC1-compressed texture.
					img = ImageDecoder::fromETC1(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_COMPRESSED_RGB8_ETC2:
				case GL_COMPRESSED_SRGB8_ETC2:
					// ETC2-compressed RGB texture.
					// TODO: Handle sRGB.
					img = ImageDecoder::fromETC2_RGB(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
				case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
					// ETC2-compressed RGB texture
					// with punchthrough alpha.
					// TODO: Handle sRGB.
					img = ImageDecoder::fromETC2_RGB_A1(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_COMPRESSED_RGBA8_ETC2_EAC:
				case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
					// ETC2-compressed RGB texture
					// with EAC-compressed alpha channel.
					// TODO: Handle sRGB.
					img = ImageDecoder::fromETC2_RGBA(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_COMPRESSED_RED_RGTC1:
				case GL_COMPRESSED_SIGNED_RED_RGTC1:
					// RGTC, one component. (BC4)
					// TODO: Handle signed properly.
					img = ImageDecoder::fromBC4(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_COMPRESSED_RG_RGTC2:
				case GL_COMPRESSED_SIGNED_RG_RGTC2:
					// RGTC, two components. (BC5)
					// TODO: Handle signed properly.
					img = ImageDecoder::fromBC5(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					break;

				case GL_COMPRESSED_LUMINANCE_LATC1_EXT:
				case GL_COMPRESSED_SIGNED_LUMINANCE_LATC1_EXT:
					// LATC, one component. (BC4)
					// TODO: Handle signed properly.
					img = ImageDecoder::fromBC4(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					// TODO: If this fails, return it anyway or return nullptr?
					ImageDecoder::fromRed8ToL8(img);
					break;

				case GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT:
				case GL_COMPRESSED_SIGNED_LUMINANCE_ALPHA_LATC2_EXT:
					// LATC, two components. (BC5)
					// TODO: Handle signed properly.
					img = ImageDecoder::fromBC5(
						ktxHeader.pixelWidth, height,
						buf.get(), expected_size);
					// TODO: If this fails, return it anyway or return nullptr?
					ImageDecoder::fromRG8ToLA8(img);
					break;

				default:
					// Not supported.
					break;
			}
			break;
	}

	// Post-processing: Check if VFlip is needed.
	// TODO: Handle HFlip too?
	if (img && (isFlipNeeded & FLIP_V) && height > 1) {
		// TODO: Assert that img dimensions match ktxHeader?
		rp_image *flipimg = img->vflip();
		if (flipimg) {
			// Swap the images.
			std::swap(img, flipimg);
			// Delete the original image.
			delete flipimg;
		}
	}

	return img;
}

/**
 * Load key/value data.
 */
void KhronosKTXPrivate::loadKeyValueData(void)
{
	if (!kv_data.empty()) {
		// Key/value data is already loaded.
		return;
	} else if (ktxHeader.bytesOfKeyValueData == 0) {
		// No key/value data is present.
		return;
	} else if (ktxHeader.bytesOfKeyValueData > 512*1024) {
		// Sanity check: More than 512 KB is usually wrong.
		return;
	}

	// Load the data.
	unique_ptr<char[]> buf(new char[ktxHeader.bytesOfKeyValueData]);
	size_t size = file->seekAndRead(sizeof(ktxHeader), buf.get(), ktxHeader.bytesOfKeyValueData);
	if (size != ktxHeader.bytesOfKeyValueData) {
		// Seek and/or read error.
		return;
	}

	// Key/value data format:
	// - uint32_t: keyAndValueByteSize
	// - Byte: keyAndValue[keyAndValueByteSize] (UTF-8)
	// - Byte: valuePadding (4-byte alignment)
	const char *p = buf.get();
	const char *const p_end = p + ktxHeader.bytesOfKeyValueData;
	bool hasKTXorientation = false;

	while (p < p_end) {
		// Check the next key/value size.
		uint32_t sz = *((const uint32_t*)p);
		if (isByteswapNeeded) {
			sz = __swab32(sz);
		}
		if (p + 4 + sz > p_end) {
			// Out of range.
			// TODO: Show an error?
			break;
		}

		p += 4;

		// keyAndValue consists of two sections:
		// - key: UTF-8 string terminated by a NUL byte.
		// - value: Arbitrary data terminated by a NUL byte. (usually UTF-8)

		// kv_end: Points past the end of the string.
		const char *const kv_end = p + sz;

		// Find the key.
		const char *const k_end = static_cast<const char*>(memchr(p, 0, kv_end - p));
		if (!k_end) {
			// NUL byte not found.
			// TODO: Show an error?
			break;
		}

		// Make sure the value ends at kv_end - 1.
		const char *const v_end = static_cast<const char*>(memchr(k_end + 1, 0, kv_end - k_end - 1));
		if (v_end != kv_end - 1) {
			// Either the NUL byte was not found,
			// or it's not at the end of the value.
			// TODO: Show an error?
			break;
		}

		vector<string> data_row;
		data_row.reserve(2);
		data_row.push_back(string(p, k_end - p));
		data_row.push_back(string(k_end + 1, kv_end - k_end - 2));
		kv_data.push_back(data_row);

		// Check if this is KTXorientation.
		// NOTE: Only the first instance is used.
		if (!hasKTXorientation && !strcmp(p, "KTXorientation")) {
			hasKTXorientation = true;
			// Check for known values.
			// NOTE: Ignoring the R component.
			// NOTE: str[7] does NOT have a NULL terminator.
			const char *const v = k_end + 1;

			static const struct {
				char str[7];
				uint8_t flip;
			} orientation_lkup_tbl[] = {
				{{'S','=','r',',','T','=','d'}, FLIP_NONE},
				{{'S','=','r',',','T','=','u'}, FLIP_V},
				{{'S','=','l',',','T','=','d'}, FLIP_H},
				{{'S','=','l',',','T','=','u'}, FLIP_HV},

				{"", 0}
			};

			for (const auto *p = orientation_lkup_tbl; p->str[0] != '\0'; p++) {
				if (!strncmp(v, p->str, 7)) {
					// Found a match.
					isFlipNeeded = p->flip;
					break;
				}
			}
		}

		// Next key/value pair.
		p += ALIGN(4, sz);
	}
}

/** KhronosKTX **/

/**
 * Read a Khronos KTX image file.
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
KhronosKTX::KhronosKTX(IRpFile *file)
	: super(new KhronosKTXPrivate(this, file))
{
	// This class handles texture files.
	RP_D(KhronosKTX);
	d->className = "KhronosKTX";
	d->fileType = FTYPE_TEXTURE_FILE;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the KTX header.
	d->file->rewind();
	size_t size = d->file->read(&d->ktxHeader, sizeof(d->ktxHeader));
	if (size != sizeof(d->ktxHeader)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this KTX texture is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(d->ktxHeader);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->ktxHeader);
	info.ext = nullptr;	// Not needed for KTX.
	info.szFile = file->size();
	d->isValid = (isRomSupported_static(&info) >= 0);

	if (!d->isValid) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if the header needs to be byteswapped.
	if (d->ktxHeader.endianness != KTX_ENDIAN_MAGIC) {
		// Byteswapping is required.
		// NOTE: Keeping `endianness` unswapped in case
		// the actual image data needs to be byteswapped.
		d->ktxHeader.glType			= __swab32(d->ktxHeader.glType);
		d->ktxHeader.glTypeSize			= __swab32(d->ktxHeader.glTypeSize);
		d->ktxHeader.glFormat			= __swab32(d->ktxHeader.glFormat);
		d->ktxHeader.glInternalFormat		= __swab32(d->ktxHeader.glInternalFormat);
		d->ktxHeader.glBaseInternalFormat	= __swab32(d->ktxHeader.glBaseInternalFormat);
		d->ktxHeader.pixelWidth			= __swab32(d->ktxHeader.pixelWidth);
		d->ktxHeader.pixelHeight		= __swab32(d->ktxHeader.pixelHeight);
		d->ktxHeader.pixelDepth			= __swab32(d->ktxHeader.pixelDepth);
		d->ktxHeader.numberOfArrayElements	= __swab32(d->ktxHeader.numberOfArrayElements);
		d->ktxHeader.numberOfFaces		= __swab32(d->ktxHeader.numberOfFaces);
		d->ktxHeader.numberOfMipmapLevels	= __swab32(d->ktxHeader.numberOfMipmapLevels);
		d->ktxHeader.bytesOfKeyValueData	= __swab32(d->ktxHeader.bytesOfKeyValueData);

		// Convenience flag.
		d->isByteswapNeeded = true;
	}

	// Texture data start address.
	// NOTE: Always 4-byte aligned.
	d->texDataStartAddr = ALIGN(4, sizeof(d->ktxHeader) + d->ktxHeader.bytesOfKeyValueData);

	// Load key/value data.
	// This function also checks for KTXorientation
	// and sets the HFlip/VFlip values as necessary.
	d->loadKeyValueData();
}

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int KhronosKTX::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(KTX_Header))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Verify the KTX magic.
	const KTX_Header *const ktxHeader =
		reinterpret_cast<const KTX_Header*>(info->header.pData);
	if (!memcmp(ktxHeader->identifier, KTX_IDENTIFIER, sizeof(KTX_IDENTIFIER)-1)) {
		// KTX magic is present.
		// Check the endianness value.
		if (ktxHeader->endianness == KTX_ENDIAN_MAGIC ||
		    ktxHeader->endianness == __swab32(KTX_ENDIAN_MAGIC))
		{
			// Endianness value is either correct for this architecture
			// or correct for byteswapped.
			return 0;
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
const char *KhronosKTX::systemName(unsigned int type) const
{
	RP_D(const KhronosKTX);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// Khronos KTX has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"KhronosKTX::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	static const char *const sysNames[4] = {
		"Khronos KTX Texture", "Khronos KTX", "KTX", nullptr
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
const char *const *KhronosKTX::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".ktx",
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
const char *const *KhronosKTX::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Official MIME types.
		"image/ktx",

		nullptr
	};
	return mimeTypes;
}

/**
 * Get a bitfield of image types this class can retrieve.
 * @return Bitfield of supported image types. (ImageTypesBF)
 */
uint32_t KhronosKTX::supportedImageTypes_static(void)
{
	return IMGBF_INT_IMAGE;
}

/**
 * Get a list of all available image sizes for the specified image type.
 * @param imageType Image type.
 * @return Vector of available image sizes, or empty vector if no images are available.
 */
vector<RomData::ImageSizeDef> KhronosKTX::supportedImageSizes(ImageType imageType) const
{
	ASSERT_supportedImageSizes(imageType);

	RP_D(const KhronosKTX);
	if (!d->isValid || imageType != IMG_INT_IMAGE) {
		return vector<ImageSizeDef>();
	}

	// Return the image's size.
	const ImageSizeDef imgsz[] = {{nullptr,
		static_cast<uint16_t>(d->ktxHeader.pixelWidth),
		static_cast<uint16_t>(d->ktxHeader.pixelHeight), 0}};
	return vector<ImageSizeDef>(imgsz, imgsz + 1);
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
uint32_t KhronosKTX::imgpf(ImageType imageType) const
{
	ASSERT_imgpf(imageType);

	RP_D(const KhronosKTX);
	if (imageType != IMG_INT_IMAGE) {
		// Only IMG_INT_IMAGE is supported by DDS.
		return 0;
	}

	// If both dimensions of the texture are 64 or less,
	// specify nearest-neighbor scaling.
	uint32_t ret = 0;
	if (d->ktxHeader.pixelWidth <= 64 && d->ktxHeader.pixelHeight <= 64) {
		// 64x64 or smaller.
		ret = IMGPF_RESCALE_NEAREST;
	}
	return ret;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int KhronosKTX::loadFieldData(void)
{
	RP_D(KhronosKTX);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// Unknown file type.
		return -EIO;
	}

	// KTX header.
	const KTX_Header *const ktxHeader = &d->ktxHeader;
	d->fields->reserve(10);	// Maximum of 10 fields.

	// Texture size.
	d->fields->addField_dimensions(C_("KhronosKTX", "Texture Size"),
		ktxHeader->pixelWidth, ktxHeader->pixelHeight,
		ktxHeader->pixelDepth);

	// Endianness.
	// TODO: Save big vs. little in the constructor instead of just "needs byteswapping"?
	const char *endian_str;
	if (ktxHeader->endianness == KTX_ENDIAN_MAGIC) {
		// Matches host-endian.
#if SYS_BYTEORDER == SYS_LIL_ENDIAN
		endian_str = C_("RomData", "Little-Endian");
#else /* SYS_BYTEORDER == SYS_BIG_ENDIAN */
		endian_str = C_("RomData", "Big-Endian");
#endif
	} else {
		// Does not match host-endian.
#if SYS_BYTEORDER == SYS_LIL_ENDIAN
		endian_str = C_("RomData", "Big-Endian");
#else /* SYS_BYTEORDER == SYS_BIG_ENDIAN */
		endian_str = C_("RomData", "Little-Endian");
#endif
	}
	d->fields->addField_string(C_("RomData", "Endianness"), endian_str);

	// NOTE: GL field names should not be localized.

	// glType
	const char *glType_str = GLenumStrings::lookup_glEnum(ktxHeader->glType);
	if (glType_str) {
		d->fields->addField_string("glType", glType_str);
	} else {
		d->fields->addField_string_numeric("glType", ktxHeader->glType, RomFields::FB_HEX);
	}

	// glFormat
	const char *glFormat_str = GLenumStrings::lookup_glEnum(ktxHeader->glFormat);
	if (glFormat_str) {
		d->fields->addField_string("glFormat", glFormat_str);
	} else {
		d->fields->addField_string_numeric("glFormat", ktxHeader->glFormat, RomFields::FB_HEX);
	}

	// glInternalFormat
	const char *glInternalFormat_str = GLenumStrings::lookup_glEnum(ktxHeader->glInternalFormat);
	if (glInternalFormat_str) {
		d->fields->addField_string("glInternalFormat", glInternalFormat_str);
	} else {
		d->fields->addField_string_numeric("glInternalFormat",
			ktxHeader->glInternalFormat, RomFields::FB_HEX);
	}

	// glBaseInternalFormat (only if != glFormat)
	if (ktxHeader->glBaseInternalFormat != ktxHeader->glFormat) {
		const char *glBaseInternalFormat_str =
			GLenumStrings::lookup_glEnum(ktxHeader->glBaseInternalFormat);
		if (glBaseInternalFormat_str) {
			d->fields->addField_string("glBaseInternalFormat", glBaseInternalFormat_str);
		} else {
			d->fields->addField_string_numeric("glBaseInternalFormat",
				ktxHeader->glBaseInternalFormat, RomFields::FB_HEX);
		}
	}

	// # of array elements (for texture arrays)
	if (ktxHeader->numberOfArrayElements > 0) {
		d->fields->addField_string_numeric(C_("KhronosKTX", "# of Array Elements"),
			ktxHeader->numberOfArrayElements);
	}

	// # of faces (for cubemaps)
	if (ktxHeader->numberOfFaces > 1) {
		d->fields->addField_string_numeric(C_("KhronosKTX", "# of Faces"),
			ktxHeader->numberOfFaces);
	}

	// # of mipmap levels
	d->fields->addField_string_numeric(C_("KhronosKTX", "# of Mipmap Levels"),
		ktxHeader->numberOfMipmapLevels);

	// Key/Value data.
	d->loadKeyValueData();
	if (!d->kv_data.empty()) {
		static const char *const kv_field_names[] = {
			NOP_C_("KhronosKTX|KeyValue", "Key"),
			NOP_C_("KhronosKTX|KeyValue", "Value"),
		};

		// NOTE: Making a copy.
		vector<vector<string> > *const p_kv_data = new vector<vector<string> >(d->kv_data);
		vector<string> *const v_kv_field_names = RomFields::strArrayToVector_i18n(
			"KhronosKTX|KeyValue", kv_field_names, ARRAY_SIZE(kv_field_names));

		RomFields::AFLD_PARAMS params;
		params.headers = v_kv_field_names;
		params.list_data = p_kv_data;
		d->fields->addField_listData(C_("KhronosKTX", "Key/Value Data"), &params);
	}

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int KhronosKTX::loadMetaData(void)
{
	RP_D(KhronosKTX);
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
	d->metaData->reserve(2);	// Maximum of 2 metadata properties.

	// KTX header.
	const KTX_Header *const ktxHeader = &d->ktxHeader;

	// Dimensions.
	// TODO: Don't add pixelHeight for 1D textures?
	d->metaData->addMetaData_integer(Property::Width, ktxHeader->pixelWidth);
	d->metaData->addMetaData_integer(Property::Height, ktxHeader->pixelHeight);

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
int KhronosKTX::loadInternalImage(ImageType imageType, const rp_image **pImage)
{
	ASSERT_loadInternalImage(imageType, pImage);

	RP_D(KhronosKTX);
	if (imageType != IMG_INT_IMAGE) {
		// Only IMG_INT_IMAGE is supported by DDS.
		*pImage = nullptr;
		return -ENOENT;
	} else if (!d->file) {
		// File isn't open.
		*pImage = nullptr;
		return -EBADF;
	} else if (!d->isValid) {
		// DDS texture isn't valid.
		*pImage = nullptr;
		return -EIO;
	}

	// Load the image.
	*pImage = d->loadImage();
	return (*pImage != nullptr ? 0 : -EIO);
}

}
