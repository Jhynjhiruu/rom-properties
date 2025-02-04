/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * ValveVTF.cpp: Valve VTF image reader.                                   *
 *                                                                         *
 * Copyright (c) 2017-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

/**
 * References:
 * - https://developer.valvesoftware.com/wiki/Valve_Texture_Format
 */

#include "ValveVTF.hpp"
#include "librpbase/RomData_p.hpp"

#include "vtf_structs.h"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/bitstuff.h"
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
#include <memory>
#include <string>
#include <vector>
using std::string;
using std::unique_ptr;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(ValveVTF)
ROMDATA_IMPL_IMG_TYPES(ValveVTF)

class ValveVTFPrivate : public RomDataPrivate
{
	public:
		ValveVTFPrivate(ValveVTF *q, IRpFile *file);
		~ValveVTFPrivate();

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(ValveVTFPrivate)

	public:
		// VTF header.
		VTFHEADER vtfHeader;

		// Texture data start address.
		unsigned int texDataStartAddr;

		// Decoded image.
		rp_image *img;

		/**
		 * Calculate an image size.
		 * @param format VTF image format.
		 * @param width Image width.
		 * @param height Image height.
		 * @return Image size, in bytes.
		 */
		static unsigned int calcImageSize(VTF_IMAGE_FORMAT format, unsigned int width, unsigned int height);

		/**
		 * Get the minimum block size for the specified format.
		 * @param format VTF image format.
		 * @return Minimum block size, or 0 if invalid.
		 */
		static unsigned int getMinBlockSize(VTF_IMAGE_FORMAT format);

		/**
		 * Load the image.
		 * @return Image, or nullptr on error.
		 */
		const rp_image *loadImage(void);

#if SYS_BYTEORDER == SYS_BIG_ENDIAN
		/**
		 * Byteswap a float. (TODO: Move to byteswap.h?)
		 * @param f Float to byteswap.
		 * @return Byteswapped flaot.
		 */
		static inline float __swabf(float f)
		{
			union {
				uint32_t u32;
				float f;
			} u32_f;
			u32_f.f = f;
			u32_f.u32 = __swab32(u32_f.u32);
			return u32_f.f;
		}
#endif
};

/** ValveVTFPrivate **/

ValveVTFPrivate::ValveVTFPrivate(ValveVTF *q, IRpFile *file)
	: super(q, file)
	, texDataStartAddr(0)
	, img(nullptr)
{
	// Clear the VTF header struct.
	memset(&vtfHeader, 0, sizeof(vtfHeader));
}

ValveVTFPrivate::~ValveVTFPrivate()
{
	delete img;
}

/**
 * Calculate an image size.
 * @param format VTF image format.
 * @param width Image width.
 * @param height Image height.
 * @return Image size, in bytes.
 */
unsigned int ValveVTFPrivate::calcImageSize(VTF_IMAGE_FORMAT format, unsigned int width, unsigned int height)
{
	unsigned int expected_size = width * height;

	enum OpCode {
		OP_UNKNOWN	= 0,
		OP_NONE,
		OP_MULTIPLY_2,
		OP_MULTIPLY_3,
		OP_MULTIPLY_4,
		OP_MULTIPLY_8,
		OP_DIVIDE_2,

		OP_MAX		= 7
	};

	static const uint8_t mul_tbl[] = {
		OP_MULTIPLY_4,	// VTF_IMAGE_FORMAT_RGBA8888
		OP_MULTIPLY_4,	// VTF_IMAGE_FORMAT_ABGR8888
		OP_MULTIPLY_3,	// VTF_IMAGE_FORMAT_RGB888
		OP_MULTIPLY_3,	// VTF_IMAGE_FORMAT_BGR888
		OP_MULTIPLY_2,	// VTF_IMAGE_FORMAT_RGB565
		OP_NONE,	// VTF_IMAGE_FORMAT_I8
		OP_MULTIPLY_2,	// VTF_IMAGE_FORMAT_IA88
		OP_NONE,	// VTF_IMAGE_FORMAT_P8
		OP_NONE,	// VTF_IMAGE_FORMAT_A8
		OP_MULTIPLY_3,	// VTF_IMAGE_FORMAT_RGB888_BLUESCREEN
		OP_MULTIPLY_3,	// VTF_IMAGE_FORMAT_BGR888_BLUESCREEN
		OP_MULTIPLY_4,	// VTF_IMAGE_FORMAT_ARGB8888
		OP_MULTIPLY_4,	// VTF_IMAGE_FORMAT_BGRA8888
		OP_DIVIDE_2,	// VTF_IMAGE_FORMAT_DXT1
		OP_NONE,	// VTF_IMAGE_FORMAT_DXT3
		OP_NONE,	// VTF_IMAGE_FORMAT_DXT5
		OP_MULTIPLY_4,	// VTF_IMAGE_FORMAT_BGRx8888
		OP_MULTIPLY_2,	// VTF_IMAGE_FORMAT_BGR565
		OP_MULTIPLY_2,	// VTF_IMAGE_FORMAT_BGRx5551
		OP_MULTIPLY_2,	// VTF_IMAGE_FORMAT_BGRA4444
		OP_DIVIDE_2,	// VTF_IMAGE_FORMAT_DXT1_ONEBITALPHA
		OP_MULTIPLY_2,	// VTF_IMAGE_FORMAT_BGRA5551
		OP_MULTIPLY_2,	// VTF_IMAGE_FORMAT_UV88
		OP_MULTIPLY_4,	// VTF_IMAGE_FORMAT_UVWQ8888
		OP_MULTIPLY_8,	// VTF_IMAGE_FORMAT_RGBA16161616F
		OP_MULTIPLY_8,	// VTF_IMAGE_FORMAT_RGBA16161616
		OP_MULTIPLY_4,	// VTF_IMAGE_FORMAT_UVLX8888
	};
	static_assert(ARRAY_SIZE(mul_tbl) == VTF_IMAGE_FORMAT_MAX,
		"mul_tbl[] is not the correct size.");

	assert(format >= 0 && format < ARRAY_SIZE(mul_tbl));
	if (format < 0 || format >= ARRAY_SIZE(mul_tbl)) {
		// Invalid format.
		return 0;
	}

	switch (mul_tbl[format]) {
		default:
		case OP_UNKNOWN:
			// Invalid opcode.
			return 0;

		case OP_NONE:					break;
		case OP_MULTIPLY_2:	expected_size *= 2;	break;
		case OP_MULTIPLY_3:	expected_size *= 3;	break;
		case OP_MULTIPLY_4:	expected_size *= 4;	break;
		case OP_MULTIPLY_8:	expected_size *= 8;	break;
		case OP_DIVIDE_2:	expected_size /= 2;	break;
	}

	return expected_size;
}

/**
* Get the minimum block size for the specified format.
* @param format VTF image format.
* @return Minimum block size, or 0 if invalid.
*/
unsigned int ValveVTFPrivate::getMinBlockSize(VTF_IMAGE_FORMAT format)
{
	static const uint8_t block_size_tbl[] = {
		4,	// VTF_IMAGE_FORMAT_RGBA8888
		4,	// VTF_IMAGE_FORMAT_ABGR8888
		3,	// VTF_IMAGE_FORMAT_RGB888
		3,	// VTF_IMAGE_FORMAT_BGR888
		2,	// VTF_IMAGE_FORMAT_RGB565
		1,	// VTF_IMAGE_FORMAT_I8
		2,	// VTF_IMAGE_FORMAT_IA88
		1,	// VTF_IMAGE_FORMAT_P8
		1,	// VTF_IMAGE_FORMAT_A8
		3,	// VTF_IMAGE_FORMAT_RGB888_BLUESCREEN
		3,	// VTF_IMAGE_FORMAT_BGR888_BLUESCREEN
		4,	// VTF_IMAGE_FORMAT_ARGB8888
		4,	// VTF_IMAGE_FORMAT_BGRA8888
		8,	// VTF_IMAGE_FORMAT_DXT1
		16,	// VTF_IMAGE_FORMAT_DXT3
		16,	// VTF_IMAGE_FORMAT_DXT5
		4,	// VTF_IMAGE_FORMAT_BGRx8888
		2,	// VTF_IMAGE_FORMAT_BGR565
		2,	// VTF_IMAGE_FORMAT_BGRx5551
		2,	// VTF_IMAGE_FORMAT_BGRA4444
		8,	// VTF_IMAGE_FORMAT_DXT1_ONEBITALPHA
		2,	// VTF_IMAGE_FORMAT_BGRA5551
		2,	// VTF_IMAGE_FORMAT_UV88
		4,	// VTF_IMAGE_FORMAT_UVWQ8888
		8,	// VTF_IMAGE_FORMAT_RGBA16161616F
		8,	// VTF_IMAGE_FORMAT_RGBA16161616
		4,	// VTF_IMAGE_FORMAT_UVLX8888
	};
	static_assert(ARRAY_SIZE(block_size_tbl) == VTF_IMAGE_FORMAT_MAX,
		"block_size_tbl[] is not the correct size.");

	assert(format >= 0 && format < ARRAY_SIZE(block_size_tbl));
	if (format < 0 || format >= ARRAY_SIZE(block_size_tbl)) {
		// Invalid format.
		return 0;
	}

	return block_size_tbl[format];
}

/**
 * Load the image.
 * @return Image, or nullptr on error.
 */
const rp_image *ValveVTFPrivate::loadImage(void)
{
	// TODO: Option to load the low-res image instead?

	if (img) {
		// Image has already been loaded.
		return img;
	} else if (!this->file || !this->isValid) {
		// Can't load the image.
		return nullptr;
	}

	// Sanity check: Maximum image dimensions of 32768x32768.
	// NOTE: `height == 0` is allowed here. (1D texture)
	assert(vtfHeader.width > 0);
	assert(vtfHeader.width <= 32768);
	assert(vtfHeader.height <= 32768);
	if (vtfHeader.width == 0 || vtfHeader.width > 32768 ||
	    vtfHeader.height > 32768)
	{
		// Invalid image dimensions.
		return nullptr;
	}

	if (file->size() > 128*1024*1024) {
		// Sanity check: VTF files shouldn't be more than 128 MB.
		return nullptr;
	}
	const uint32_t file_sz = static_cast<uint32_t>(file->size());

	// Handle a 1D texture as a "width x 1" 2D texture.
	// NOTE: Handling a 3D texture as a single 2D texture.
	const int height = (vtfHeader.height > 0 ? vtfHeader.height : 1);

	// NOTE: VTF specifications say the image size must be a power of two.
	// Some malformed images may have a smaller width in the header,
	// so calculate the row width here.
	int row_width = vtfHeader.width;
	if (!isPow2(row_width)) {
		// Adjust to the next power of two.
		// We need to calculate the actual stride in order to
		// prevent crashes in the SSE2 code.
		row_width = 1 << (uilog2(row_width) + 1);
	}

	// Calculate the expected size.
	unsigned int expected_size = calcImageSize(
		static_cast<VTF_IMAGE_FORMAT>(vtfHeader.highResImageFormat),
		row_width, height);
	if (expected_size == 0) {
		// Invalid image size.
		return nullptr;
	}

	// TODO: Handle environment maps (6-faced cube map) and volumetric textures.

	// Adjust for the number of mipmaps.
	// NOTE: Dimensions must be powers of two.
	unsigned int texDataStartAddr_adj = texDataStartAddr;
	unsigned int mipmap_size = expected_size;
	const unsigned int minBlockSize = getMinBlockSize(
		static_cast<VTF_IMAGE_FORMAT>(vtfHeader.highResImageFormat));
	for (unsigned int mipmapLevel = vtfHeader.mipmapCount; mipmapLevel > 1; mipmapLevel--) {
		mipmap_size /= 4;
		if (mipmap_size >= minBlockSize) {
			texDataStartAddr_adj += mipmap_size;
		} else {
			// Mipmap is smaller than the minimum block size
			// for this format.
			texDataStartAddr_adj += minBlockSize;
		}
	}

	// Low-resolution image size.
	texDataStartAddr_adj += calcImageSize(
		static_cast<VTF_IMAGE_FORMAT>(vtfHeader.lowResImageFormat),
		vtfHeader.lowResImageWidth,
		(vtfHeader.lowResImageHeight > 0 ? vtfHeader.lowResImageHeight : 1));

	// Verify file size.
	if (texDataStartAddr_adj + expected_size > file_sz) {
		// File is too small.
		return nullptr;
	}

	// Texture cannot start inside of the VTF header.
	assert(texDataStartAddr_adj >= sizeof(vtfHeader));
	if (texDataStartAddr_adj < sizeof(vtfHeader)) {
		// Invalid texture data start address.
		return nullptr;
	}

	// Seek to the start of the texture data.
	int ret = file->seek(texDataStartAddr_adj);
	if (ret != 0) {
		// Seek error.
		return nullptr;
	}

	// Read the texture data.
	auto buf = aligned_uptr<uint8_t>(16, expected_size);
	size_t size = file->read(buf.get(), expected_size);
	if (size != expected_size) {
		// Read error.
		return nullptr;
	}

	// Decode the image.
	// NOTE: VTF channel ordering does NOT match ImageDecoder channel ordering.
	// (The channels appear to be backwards.)
	// TODO: Lookup table to convert to PXF constants?
	// TODO: Verify on big-endian?
	switch (vtfHeader.highResImageFormat) {
		/* 32-bit */
		case VTF_IMAGE_FORMAT_RGBA8888:
		case VTF_IMAGE_FORMAT_UVWQ8888:	// handling as RGBA8888
		case VTF_IMAGE_FORMAT_UVLX8888:	// handling as RGBA8888
			img = ImageDecoder::fromLinear32(ImageDecoder::PXF_ABGR8888,
				vtfHeader.width, height,
				reinterpret_cast<const uint32_t*>(buf.get()), expected_size,
				row_width * sizeof(uint32_t));
			break;
		case VTF_IMAGE_FORMAT_ABGR8888:
			img = ImageDecoder::fromLinear32(ImageDecoder::PXF_RGBA8888,
				vtfHeader.width, height,
				reinterpret_cast<const uint32_t*>(buf.get()), expected_size,
				row_width * sizeof(uint32_t));
			break;
		case VTF_IMAGE_FORMAT_ARGB8888:
			// This is stored as RAGB for some reason...
			// FIXME: May be a bug in VTFEdit. (Tested versions: 1.2.5, 1.3.3)
			img = ImageDecoder::fromLinear32(ImageDecoder::PXF_RABG8888,
				vtfHeader.width, height,
				reinterpret_cast<const uint32_t*>(buf.get()), expected_size,
				row_width * sizeof(uint32_t));
			break;
		case VTF_IMAGE_FORMAT_BGRA8888:
			img = ImageDecoder::fromLinear32(ImageDecoder::PXF_ARGB8888,
				vtfHeader.width, height,
				reinterpret_cast<const uint32_t*>(buf.get()), expected_size,
				row_width * sizeof(uint32_t));
			break;
		case VTF_IMAGE_FORMAT_BGRx8888:
			img = ImageDecoder::fromLinear32(ImageDecoder::PXF_xRGB8888,
				vtfHeader.width, height,
				reinterpret_cast<const uint32_t*>(buf.get()), expected_size,
				row_width * sizeof(uint32_t));
			break;

		/* 24-bit */
		case VTF_IMAGE_FORMAT_RGB888:
			img = ImageDecoder::fromLinear24(ImageDecoder::PXF_BGR888,
				vtfHeader.width, height,
				buf.get(), expected_size,
				row_width * 3);
			break;
		case VTF_IMAGE_FORMAT_BGR888:
			img = ImageDecoder::fromLinear24(ImageDecoder::PXF_RGB888,
				vtfHeader.width, height,
				buf.get(), expected_size,
				row_width * 3);
			break;
		case VTF_IMAGE_FORMAT_RGB888_BLUESCREEN:
			img = ImageDecoder::fromLinear24(ImageDecoder::PXF_BGR888,
				vtfHeader.width, height,
				buf.get(), expected_size,
				row_width * 3);
			img->apply_chroma_key(0xFF0000FF);
			break;
		case VTF_IMAGE_FORMAT_BGR888_BLUESCREEN:
			img = ImageDecoder::fromLinear24(ImageDecoder::PXF_RGB888,
				vtfHeader.width, height,
				buf.get(), expected_size,
				row_width * 3);
			img->apply_chroma_key(0xFF0000FF);
			break;

		/* 16-bit */
		case VTF_IMAGE_FORMAT_RGB565:
			img = ImageDecoder::fromLinear16(ImageDecoder::PXF_BGR565,
				vtfHeader.width, height,
				reinterpret_cast<const uint16_t*>(buf.get()), expected_size,
				row_width * sizeof(uint16_t));
			break;
		case VTF_IMAGE_FORMAT_BGR565:
			img = ImageDecoder::fromLinear16(ImageDecoder::PXF_RGB565,
				vtfHeader.width, height,
				reinterpret_cast<const uint16_t*>(buf.get()), expected_size,
				row_width * sizeof(uint16_t));
			break;
		case VTF_IMAGE_FORMAT_BGRx5551:
			img = ImageDecoder::fromLinear16(ImageDecoder::PXF_RGB555,
				vtfHeader.width, height,
				reinterpret_cast<const uint16_t*>(buf.get()), expected_size,
				row_width * sizeof(uint16_t));
			break;
		case VTF_IMAGE_FORMAT_BGRA4444:
			img = ImageDecoder::fromLinear16(ImageDecoder::PXF_ARGB4444,
				vtfHeader.width, height,
				reinterpret_cast<const uint16_t*>(buf.get()), expected_size,
				row_width * sizeof(uint16_t));
			break;
		case VTF_IMAGE_FORMAT_BGRA5551:
			img = ImageDecoder::fromLinear16(ImageDecoder::PXF_ARGB1555,
				vtfHeader.width, height,
				reinterpret_cast<const uint16_t*>(buf.get()), expected_size,
				row_width * sizeof(uint16_t));
			break;
		case VTF_IMAGE_FORMAT_IA88:
			// FIXME: I8 might have the alpha channel set to the I channel,
			// whereas L8 has A=1.0.
			// https://www.opengl.org/discussion_boards/showthread.php/151701-GL_LUMINANCE-vs-GL_INTENSITY
			// NOTE: Using A8L8 format, not IA8, which is GameCube-specific.
			// (Channels are backwards.)
			// TODO: Add ImageDecoder::fromLinear16() support for IA8 later.
			img = ImageDecoder::fromLinear16(ImageDecoder::PXF_A8L8,
				vtfHeader.width, height,
				reinterpret_cast<const uint16_t*>(buf.get()), expected_size,
				row_width * sizeof(uint16_t));
			break;
		case VTF_IMAGE_FORMAT_UV88:
			// We're handling this as a GR88 texture.
			img = ImageDecoder::fromLinear16(ImageDecoder::PXF_GR88,
				vtfHeader.width, height,
				reinterpret_cast<const uint16_t*>(buf.get()), expected_size,
				row_width * sizeof(uint16_t));
			break;

		/* 8-bit */
		case VTF_IMAGE_FORMAT_I8:
			// FIXME: I8 might have the alpha channel set to the I channel,
			// whereas L8 has A=1.0.
			// https://www.opengl.org/discussion_boards/showthread.php/151701-GL_LUMINANCE-vs-GL_INTENSITY
			img = ImageDecoder::fromLinear8(ImageDecoder::PXF_L8,
				vtfHeader.width, height,
				buf.get(), expected_size,
				row_width);
			break;
		case VTF_IMAGE_FORMAT_A8:
			img = ImageDecoder::fromLinear8(ImageDecoder::PXF_A8,
				vtfHeader.width, height,
				buf.get(), expected_size,
				row_width);
			break;

		/* Compressed */
		case VTF_IMAGE_FORMAT_DXT1:
			img = ImageDecoder::fromDXT1(
				vtfHeader.width, height,
				buf.get(), expected_size);
			break;
		case VTF_IMAGE_FORMAT_DXT1_ONEBITALPHA:
			img = ImageDecoder::fromDXT1_A1(
				vtfHeader.width, height,
				buf.get(), expected_size);
			break;
		case VTF_IMAGE_FORMAT_DXT3:
			img = ImageDecoder::fromDXT3(
				vtfHeader.width, height,
				buf.get(), expected_size);
			break;
		case VTF_IMAGE_FORMAT_DXT5:
			img = ImageDecoder::fromDXT5(
				vtfHeader.width, height,
				buf.get(), expected_size);
			break;

		case VTF_IMAGE_FORMAT_P8:
		case VTF_IMAGE_FORMAT_RGBA16161616F:
		case VTF_IMAGE_FORMAT_RGBA16161616:
		default:
			// Not supported.
			break;
	}

	return img;
}

/** ValveVTF **/

/**
 * Read a Valve VTF image file.
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
ValveVTF::ValveVTF(IRpFile *file)
	: super(new ValveVTFPrivate(this, file))
{
	// This class handles texture files.
	RP_D(ValveVTF);
	d->className = "ValveVTF";
	d->fileType = FTYPE_TEXTURE_FILE;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the VTF header.
	d->file->rewind();
	size_t size = d->file->read(&d->vtfHeader, sizeof(d->vtfHeader));
	if (size != sizeof(d->vtfHeader)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this VTF texture is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = static_cast<uint32_t>(size);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->vtfHeader);
	info.ext = nullptr;	// Not needed for VTF.
	info.szFile = file->size();
	d->isValid = (isRomSupported_static(&info) >= 0);

	if (!d->isValid) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

#if SYS_BYTEORDER == SYS_BIG_ENDIAN
	// Header is stored in little-endian, so it always
	// needs to be byteswapped on big-endian.
	d->vtfHeader.signature		= le32_to_cpu(d->vtfHeader.signature);
	d->vtfHeader.version[0]		= le32_to_cpu(d->vtfHeader.version[0]);
	d->vtfHeader.version[1]		= le32_to_cpu(d->vtfHeader.version[1]);
	d->vtfHeader.headerSize		= le32_to_cpu(d->vtfHeader.headerSize);
	d->vtfHeader.width		= le16_to_cpu(d->vtfHeader.width);
	d->vtfHeader.height		= le16_to_cpu(d->vtfHeader.height);
	d->vtfHeader.flags		= le32_to_cpu(d->vtfHeader.flags);
	d->vtfHeader.frames		= le16_to_cpu(d->vtfHeader.frames);
	d->vtfHeader.firstFrame		= le16_to_cpu(d->vtfHeader.firstFrame);
	d->vtfHeader.reflectivity[0]	= d->__swabf(d->vtfHeader.reflectivity[0]);
	d->vtfHeader.reflectivity[1]	= d->__swabf(d->vtfHeader.reflectivity[1]);
	d->vtfHeader.reflectivity[2]	= d->__swabf(d->vtfHeader.reflectivity[2]);
	d->vtfHeader.bumpmapScale	= d->__swabf(d->vtfHeader.bumpmapScale);
	d->vtfHeader.highResImageFormat	= le32_to_cpu(d->vtfHeader.highResImageFormat);
	d->vtfHeader.lowResImageFormat	= le32_to_cpu(d->vtfHeader.lowResImageFormat);
	d->vtfHeader.depth		= le16_to_cpu(d->vtfHeader.depth);
	d->vtfHeader.numResources	= le32_to_cpu(d->vtfHeader.numResources);
#endif

	// Texture data start address.
	// Note that this is the start of *all* texture data,
	// including the low-res texture and mipmaps.
	// TODO: Should always be 16-byte aligned?
	// TODO: Verify header size against sizeof(VTFHEADER).
	// Test VTFs are 7.2 with 80-byte headers; sizeof(VTFHEADER) is 72...
	d->texDataStartAddr = d->vtfHeader.headerSize;
}

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int ValveVTF::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(VTFHEADER))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	// Verify the VTF signature.
	const VTFHEADER *const vtfHeader =
		reinterpret_cast<const VTFHEADER*>(info->header.pData);
	if (vtfHeader->signature == cpu_to_be32(VTF_SIGNATURE)) {
		// VTF signature is correct.
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
const char *ValveVTF::systemName(unsigned int type) const
{
	RP_D(const ValveVTF);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// Valve VTF has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"ValveVTF::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	static const char *const sysNames[4] = {
		"Valve VTF Texture", "Valve VTF", "VTF", nullptr
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
const char *const *ValveVTF::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".vtf",
		//".vtx",	// TODO: Some files might use the ".vtx" extension.
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
const char *const *ValveVTF::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Vendor-specific MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"image/vnd.valve.source.texture",

		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"image/x-vtf",

		nullptr
	};
	return mimeTypes;
}

/**
 * Get a bitfield of image types this class can retrieve.
 * @return Bitfield of supported image types. (ImageTypesBF)
 */
uint32_t ValveVTF::supportedImageTypes_static(void)
{
	return IMGBF_INT_IMAGE;
}

/**
 * Get a list of all available image sizes for the specified image type.
 * @param imageType Image type.
 * @return Vector of available image sizes, or empty vector if no images are available.
 */
vector<RomData::ImageSizeDef> ValveVTF::supportedImageSizes(ImageType imageType) const
{
	ASSERT_supportedImageSizes(imageType);

	RP_D(const ValveVTF);
	if (!d->isValid || imageType != IMG_INT_IMAGE) {
		return vector<ImageSizeDef>();
	}

	// Return the image's size.
	const ImageSizeDef imgsz[] = {{nullptr,
		d->vtfHeader.width,
		d->vtfHeader.height, 0}};
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
uint32_t ValveVTF::imgpf(ImageType imageType) const
{
	ASSERT_imgpf(imageType);

	RP_D(const ValveVTF);
	if (imageType != IMG_INT_IMAGE) {
		// Only IMG_INT_IMAGE is supported by DDS.
		return 0;
	}

	// If both dimensions of the texture are 64 or less,
	// specify nearest-neighbor scaling.
	uint32_t ret = 0;
	if (d->vtfHeader.width <= 64 && d->vtfHeader.height <= 64) {
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
int ValveVTF::loadFieldData(void)
{
	// TODO: Move to RomFields?
#ifdef _WIN32
	// Windows: 6 visible rows per RFT_LISTDATA.
	static const int rows_visible = 6;
#else
	// Linux: 4 visible rows per RFT_LISTDATA.
	static const int rows_visible = 4;
#endif

	RP_D(ValveVTF);
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

	// VTF header.
	const VTFHEADER *const vtfHeader = &d->vtfHeader;
	d->fields->reserve(12);	// Maximum of 12 fields.

	// VTF version.
	d->fields->addField_string(C_("ValveVTF", "VTF Version"),
		rp_sprintf("%u.%u", vtfHeader->version[0], vtfHeader->version[1]));

	// Texture size.
	// 7.2+ supports 3D textures.
	int depth = 0;
	if ((vtfHeader->version[0] > 7 || (vtfHeader->version[0] == 7 && vtfHeader->version[1] >= 2)) &&
	     vtfHeader->depth > 1)
	{
		depth = vtfHeader->depth;
	}
	d->fields->addField_dimensions(C_("ValveVTF", "Texture Size"),
		vtfHeader->width, vtfHeader->height, depth);

	// Flags.
	// TODO: Show "deprecated" flags for older versions.
	static const char *const flags_names[] = {
		// 0x1-0x8
		NOP_C_("ValveVTF|Flags", "Point Sampling"),
		NOP_C_("ValveVTF|Flags", "Trilinear Sampling"),
		NOP_C_("ValveVTF|Flags", "Clamp S"),
		NOP_C_("ValveVTF|Flags", "Clamp T"),
		// 0x10-0x80
		NOP_C_("ValveVTF|Flags", "Anisotropic Sampling"),
		NOP_C_("ValveVTF|Flags", "Hint DXT5"),
		NOP_C_("ValveVTF|Flags", "PWL Corrected"),	// "No Compress" (deprecated)
		NOP_C_("ValveVTF|Flags", "Normal Map"),
		// 0x100-0x800
		NOP_C_("ValveVTF|Flags", "No Mipmaps"),
		NOP_C_("ValveVTF|Flags", "No Level of Detail"),
		NOP_C_("ValveVTF|Flags", "No Minimum Mipmap"),
		NOP_C_("ValveVTF|Flags", "Procedural"),
		// 0x1000-0x8000
		NOP_C_("ValveVTF|Flags", "1-bit Alpha"),
		NOP_C_("ValveVTF|Flags", "8-bit Alpha"),
		NOP_C_("ValveVTF|Flags", "Environment Map"),
		NOP_C_("ValveVTF|Flags", "Render Target"),
		// 0x10000-0x80000
		NOP_C_("ValveVTF|Flags", "Depth Render Target"),
		NOP_C_("ValveVTF|Flags", "No Debug Override"),
		NOP_C_("ValveVTF|Flags", "Single Copy"),
		NOP_C_("ValveVTF|Flags", "Pre SRGB"),		// "One Over Mipmap Level in Alpha" (deprecated)
		// 0x100000-0x800000
		NOP_C_("ValveVTF|Flags", "Premult Color by 1/mipmap"),
		NOP_C_("ValveVTF|Flags", "Normal to DuDv"),
		NOP_C_("ValveVTF|Flags", "Alpha Test Mipmap Gen"),
		NOP_C_("ValveVTF|Flags", "No depth Buffer"),
		// 0x1000000-0x8000000
		NOP_C_("ValveVTF|Flags", "Nice Filtered"),
		NOP_C_("ValveVTF|Flags", "Clamp U"),
		NOP_C_("ValveVTF|Flags", "Vertex Texture"),
		NOP_C_("ValveVTF|Flags", "SSBump"),
		// 0x10000000-0x20000000
		nullptr,
		NOP_C_("ValveVTF|Flags", "Border"),
	};

	// Convert to vector<vector<string> > for RFT_LISTDATA.
	auto vv_flags = new vector<vector<string> >(ARRAY_SIZE(flags_names));
	for (int i = ARRAY_SIZE(flags_names)-1; i >= 0; i--) {
		auto &data_row = vv_flags->at(i);
		if (flags_names[i]) {
			data_row.push_back(dpgettext_expr(RP_I18N_DOMAIN, "ValveVTF|Flags", flags_names[i]));
		}
	}

	RomFields::AFLD_PARAMS params(RomFields::RFT_LISTDATA_CHECKBOXES, rows_visible);
	params.headers = nullptr;
	params.list_data = vv_flags;
	params.mxd.checkboxes = vtfHeader->flags;
	d->fields->addField_listData(C_("ValveVTF", "Flags"), &params);

	// Number of frames.
	d->fields->addField_string_numeric(C_("ValveVTF", "# of Frames"), vtfHeader->frames);
	if (vtfHeader->frames > 1) {
		d->fields->addField_string_numeric(C_("ValveVTF", "First Frame"), vtfHeader->firstFrame);
	}

	// Reflectivity vector.
	d->fields->addField_string(C_("ValveVTF", "Reflectivity Vector"),
		rp_sprintf("(%0.1f, %0.1f, %0.1f)",
			vtfHeader->reflectivity[0],
			vtfHeader->reflectivity[1],
			vtfHeader->reflectivity[2]));

	// Bumpmap scale.
	d->fields->addField_string(C_("ValveVTF", "Bumpmap Scale"),
		rp_sprintf("%0.1f", vtfHeader->bumpmapScale));

	// High-resolution image format.
	static const char *const img_format_tbl[] = {
		"RGBA8888",
		"ABGR8888",
		"RGB888",
		"BGR888",
		"RGB565",
		"I8",
		"IA88",
		"P8",
		"A8",
		NOP_C_("ValveVTF|ImageFormat", "RGB888 (Bluescreen)"),
		NOP_C_("ValveVTF|ImageFormat", "BGR888 (Bluescreen)"),
		"ARGB8888",
		"BGRA8888",
		"DXT1",
		"DXT3",
		"DXT5",
		"BGRx8888",
		"BGR565",
		"BGRx5551",
		"BGRA4444",
		"DXT1_A1",
		"BGRA5551",
		"UV88",
		"UVWQ8888",
		"RGBA16161616F",
		"RGBA16161616",
		"UVLX8888",
	};
	static_assert(ARRAY_SIZE(img_format_tbl) == VTF_IMAGE_FORMAT_MAX, "Missing VTF image formats.");

	const char *const high_res_image_format_title = C_("ValveVTF", "High-Res Image Format");
	const char *img_format = nullptr;
	if (vtfHeader->highResImageFormat < ARRAY_SIZE(img_format_tbl)) {
		img_format = img_format_tbl[vtfHeader->highResImageFormat];
	} else if (vtfHeader->highResImageFormat == static_cast<unsigned int>(-1)) {
		img_format = NOP_C_("ValveVTF|ImageFormat", "None");
	}

	if (img_format) {
		d->fields->addField_string(high_res_image_format_title,
			dpgettext_expr(RP_I18N_DOMAIN, "ValveVTF|ImageFormat", img_format));
	} else {
		d->fields->addField_string(high_res_image_format_title,
			rp_sprintf(C_("RomData", "Unknown (%d)"), vtfHeader->highResImageFormat));
	}

	// Mipmap count.
	d->fields->addField_string_numeric(C_("ValveVTF", "Mipmap Count"), vtfHeader->mipmapCount);

	// Low-resolution image format.
	const char *const low_res_image_format_title = C_("ValveVTF", "Low-Res Image Format");
	img_format = nullptr;
	if (vtfHeader->lowResImageFormat < ARRAY_SIZE(img_format_tbl)) {
		img_format = img_format_tbl[vtfHeader->lowResImageFormat];
	} else if (vtfHeader->lowResImageFormat == static_cast<unsigned int>(-1)) {
		img_format = NOP_C_("ValveVTF|ImageFormat", "None");
	}

	if (img_format) {
		d->fields->addField_string(low_res_image_format_title,
			dpgettext_expr(RP_I18N_DOMAIN, "ValveVTF|ImageFormat", img_format));
		// Low-res image size.
		d->fields->addField_dimensions(C_("ValveVTF", "Low-Res Size"),
			vtfHeader->lowResImageWidth,
			vtfHeader->lowResImageHeight);
	} else {
		d->fields->addField_string(low_res_image_format_title,
			rp_sprintf(C_("RomData", "Unknown (%d)"), vtfHeader->highResImageFormat));
	}

	if (vtfHeader->version[0] > 7 ||
	    (vtfHeader->version[0] == 7 && vtfHeader->version[1] >= 3))
	{
		// 7.3+: Resources.
		// TODO: Display the resources as RFT_LISTDATA?
		d->fields->addField_string_numeric(C_("ValveVTF", "# of Resources"),
			vtfHeader->numResources);
	}

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int ValveVTF::loadMetaData(void)
{
	RP_D(ValveVTF);
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

	// VTF header.
	const VTFHEADER *const vtfHeader = &d->vtfHeader;

	// Dimensions.
	// TODO: Don't add height for 1D textures?
	d->metaData->addMetaData_integer(Property::Width, vtfHeader->width);
	d->metaData->addMetaData_integer(Property::Height, vtfHeader->height);

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
int ValveVTF::loadInternalImage(ImageType imageType, const rp_image **pImage)
{
	ASSERT_loadInternalImage(imageType, pImage);

	RP_D(ValveVTF);
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
