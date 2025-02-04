/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * RpFile_stdio.cpp: Standard file object. (stdio implementation)          *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "RpFile.hpp"
#include "RpFile_stdio_p.hpp"

#ifdef _WIN32
# error RpFile_stdio is not supported on Windows, use RpFile_win32.
#endif /* _WIN32 */

// librpbase
#include "byteswap.h"

// C includes.
#include <sys/stat.h>

namespace LibRpBase {

/** RpFilePrivate **/

RpFilePrivate::~RpFilePrivate()
{
	if (gzfd) {
		gzclose_r(gzfd);
	}
	if (file) {
		fclose(file);
	}
}

/**
 * Convert an RpFile::FileMode to an fopen() mode string.
 * @param mode	[in] FileMode
 * @return fopen() mode string.
 */
inline const char *RpFilePrivate::mode_to_str(RpFile::FileMode mode)
{
	switch (mode & RpFile::FM_MODE_MASK) {
		case RpFile::FM_OPEN_READ:
			return "rb";
		case RpFile::FM_OPEN_WRITE:
			return "rb+";
		case RpFile::FM_CREATE|RpFile::FM_READ /*RpFile::FM_CREATE_READ*/ :
		case RpFile::FM_CREATE_WRITE:
			return "wb+";
		default:
			// Invalid mode.
			return nullptr;
	}
}

/**
 * (Re-)Open the main file.
 *
 * INTERNAL FUNCTION. This does NOT affect gzfd.
 * NOTE: This function sets q->m_lastError.
 *
 * Uses parameters stored in this->filename and this->mode.
 * @return 0 on success; non-zero on error.
 */
int RpFilePrivate::reOpenFile(void)
{
	RP_Q(RpFile);
	const char *const mode_str = mode_to_str(mode);

	// Linux: Use UTF-8 filenames directly.
	if (file) {
		fclose(file);
	}
	file = fopen(filename.c_str(), mode_str);

	// Return 0 if it's *not* nullptr.
	if (!file) {
		q->m_lastError = errno;
		if (q->m_lastError == 0) {
			q->m_lastError = EIO;
		}
		return q->m_lastError;
	}

	// Check if this is a device.
	struct stat sb;
	int ret = fstat(fileno(file), &sb);
	if (ret == 0) {
		// fstat() succeeded.
		if (S_ISDIR(sb.st_mode)) {
			// This is a directory.
			fclose(file);
			file = nullptr;
			q->m_lastError = EISDIR;
			return -EISDIR;
		}
		isDevice = (S_ISBLK(sb.st_mode) || S_ISCHR(sb.st_mode));
	} else {
		// Unable to fstat().
		// Assume this is not a device.
		isDevice = false;
	}

	// NOTE: Opening certain device files can cause crashes
	// and/or hangs (e.g. stdin). Only allow device files
	// that match certain patterns.
	// TODO: May need updates for *BSD, Mac OS X, etc.
	// TODO: Check if a block device is a CD-ROM or something else.
	if (isDevice) {
		// NOTE: Some Unix systems use character devices for "raw"
		// block devices. Linux does not, so we're only checking
		// for block devices for now.
		// TODO: Add checks for other Unix systems.
		if (S_ISCHR(sb.st_mode)) {
			// Character device. Not supported.
			fclose(file);
			file = nullptr;
			isDevice = false;
			q->m_lastError = ENOTSUP;
			return -ENOTSUP;
		}

		// Check the filename pattern.
		// TODO: Use an array?
		// TODO: More systems.
		if (strncmp(filename.c_str(), "/dev/sr", 7) != 0 &&
		    strncmp(filename.c_str(), "/dev/scd", 8) != 0 &&
		    strncmp(filename.c_str(), "/dev/disk/", 10) != 0 &&
		    strncmp(filename.c_str(), "/dev/block/", 11) != 0)
		{
			// Not a match.
			fclose(file);
			file = nullptr;
			isDevice = false;
			q->m_lastError = ENOTSUP;
			return -ENOTSUP;
		}
	}

	return 0;
}

/** RpFile **/

/**
 * Open a file.
 * NOTE: Files are always opened in binary mode.
 * @param filename Filename.
 * @param mode File mode.
 */
RpFile::RpFile(const char *filename, FileMode mode)
	: super()
	, d_ptr(new RpFilePrivate(this, filename, mode))
{
	init();
}

/**
 * Open a file.
 * NOTE: Files are always opened in binary mode.
 * @param filename Filename.
 * @param mode File mode.
 */
RpFile::RpFile(const string &filename, FileMode mode)
	: super()
	, d_ptr(new RpFilePrivate(this, filename, mode))
{
	init();
}

/**
 * Common initialization function for RpFile's constructors.
 * Filename must be set in d->filename.
 */
void RpFile::init(void)
{
	RP_D(RpFile);

	// Cannot use decompression with writing.
	// FIXME: Proper assert statement...
	//assert((d->mode & FM_MODE_MASK != RpFile::FM_READ) || (d->mode & RpFile::FM_GZIP_DECOMPRESS));

	// Open the file.
	if (d->reOpenFile() != 0) {
		// An error occurred while opening the file.
		return;
	}

	// Check if this is a gzipped file.
	// If it is, use transparent decompression.
	// Reference: https://www.forensicswiki.org/wiki/Gzip
	if (d->mode == FM_OPEN_READ_GZ) {
		uint16_t gzmagic;
		size_t size = fread(&gzmagic, 1, sizeof(gzmagic), d->file);
		if (size == sizeof(gzmagic) && gzmagic == be16_to_cpu(0x1F8B)) {
			// This is a gzipped file.
			// Get the uncompressed size at the end of the file.
			fseeko(d->file, 0, SEEK_END);
			int64_t real_sz = ftello(d->file);
			if (real_sz > 10+8) {
				int ret = fseeko(d->file, real_sz-4, SEEK_SET);
				if (!ret) {
					uint32_t uncomp_sz;
					size = fread(&uncomp_sz, 1, sizeof(uncomp_sz), d->file);
					uncomp_sz = le32_to_cpu(uncomp_sz);
					if (size == sizeof(uncomp_sz) && uncomp_sz >= real_sz-(10+8)) {
						// Uncompressed size looks valid.
						d->gzsz = (int64_t)uncomp_sz;

						// Make sure the CRC32 table is initialized.
						get_crc_table();

						// Open the file with gzdopen().
						::rewind(d->file);
						::fflush(d->file);
						int gzfd_dup = ::dup(fileno(d->file));
						if (gzfd_dup >= 0) {
							d->gzfd = gzdopen(gzfd_dup, "r");
							if (!d->gzfd) {
								// gzdopen() failed.
								// Close the dup()'d handle to prevent a leak.
								::close(gzfd_dup);
							}
						}
					}
				}
			}
		}

		if (!d->gzfd) {
			// Not a gzipped file.
			// Rewind and flush the file.
			::rewind(d->file);
			::fflush(d->file);
		}
	}
}

RpFile::~RpFile()
{
	delete d_ptr;
}

/**
 * Is the file open?
 * This usually only returns false if an error occurred.
 * @return True if the file is open; false if it isn't.
 */
bool RpFile::isOpen(void) const
{
	RP_D(const RpFile);
	return (d->file != nullptr);
}

/**
 * Close the file.
 */
void RpFile::close(void)
{
	RP_D(RpFile);
	if (d->gzfd) {
		gzclose_r(d->gzfd);
		d->gzfd = nullptr;
	}
	if (d->file) {
		fclose(d->file);
		d->file = nullptr;
	}
}

/**
 * Read data from the file.
 * @param ptr Output data buffer.
 * @param size Amount of data to read, in bytes.
 * @return Number of bytes read.
 */
size_t RpFile::read(void *ptr, size_t size)
{
	RP_D(RpFile);
	if (!d->file) {
		m_lastError = EBADF;
		return 0;
	}

	size_t ret;
	if (d->gzfd) {
		int iret = gzread(d->gzfd, ptr, size);
		if (iret >= 0) {
			ret = (size_t)iret;
		} else {
			// An error occurred.
			ret = 0;
			m_lastError = errno;
		}
	} else {
		ret = fread(ptr, 1, size, d->file);
		if (ferror(d->file)) {
			// An error occurred.
			m_lastError = errno;
		}
	}
	return ret;
}

/**
 * Write data to the file.
 * @param ptr Input data buffer.
 * @param size Amount of data to read, in bytes.
 * @return Number of bytes written.
 */
size_t RpFile::write(const void *ptr, size_t size)
{
	RP_D(RpFile);
	if (!d->file || !(d->mode & FM_WRITE)) {
		// Either the file isn't open,
		// or it's read-only.
		m_lastError = EBADF;
		return 0;
	}

	size_t ret = fwrite(ptr, 1, size, d->file);
	if (ferror(d->file)) {
		// An error occurred.
		m_lastError = errno;
	}
	return ret;
}

/**
 * Set the file position.
 * @param pos File position.
 * @return 0 on success; -1 on error.
 */
int RpFile::seek(int64_t pos)
{
	RP_D(RpFile);
	if (!d->file) {
		m_lastError = EBADF;
		return -1;
	}

	int ret;
	if (d->gzfd) {
		z_off_t zret = gzseek(d->gzfd, pos, SEEK_SET);
		if (zret >= 0) {
			ret = 0;
		} else {
			// TODO: Does gzseek() set errno?
			ret = -1;
			m_lastError = -EIO;
		}
	} else {
		ret = fseeko(d->file, pos, SEEK_SET);
		if (ret != 0) {
			m_lastError = errno;
		}
	}
	::fflush(d->file);	// needed for some things like gzip
	return ret;
}

/**
 * Get the file position.
 * @return File position, or -1 on error.
 */
int64_t RpFile::tell(void)
{
	RP_D(RpFile);
	if (!d->file) {
		m_lastError = EBADF;
		return -1;
	}

	if (d->gzfd) {
		return (int64_t)gztell(d->gzfd);
	}
	return ftello(d->file);
}

/**
 * Truncate the file.
 * @param size New size. (default is 0)
 * @return 0 on success; -1 on error.
 */
int RpFile::truncate(int64_t size)
{
	RP_D(RpFile);
	if (!d->file || !(d->mode & FM_WRITE)) {
		// Either the file isn't open,
		// or it's read-only.
		m_lastError = EBADF;
		return -1;
	} else if (size < 0) {
		m_lastError = EINVAL;
		return -1;
	}

	// Get the current position.
	int64_t pos = ftello(d->file);
	if (pos < 0) {
		m_lastError = errno;
		return -1;
	}

	// Truncate the file.
	fflush(d->file);
	int ret = ftruncate(fileno(d->file), size);
	if (ret != 0) {
		m_lastError = errno;
		return -1;
	}

	// If the previous position was past the new
	// file size, reset the pointer.
	if (pos > size) {
		ret = fseeko(d->file, size, SEEK_SET);
		if (ret != 0) {
			m_lastError = errno;
			return -1;
		}
	}

	// File truncated.
	return 0;
}

/** File properties **/

/**
 * Get the file size.
 * @return File size, or negative on error.
 */
int64_t RpFile::size(void)
{
	RP_D(RpFile);
	if (!d->file) {
		m_lastError = EBADF;
		return -1;
	}

	// TODO: Error checking?

	if (d->gzfd) {
		// gzipped files have the uncompressed size stored
		// at the end of the stream.
		return d->gzsz;
	}

	// Save the current position.
	int64_t cur_pos = ftello(d->file);

	// Seek to the end of the file and record its position.
	fseeko(d->file, 0, SEEK_END);
	int64_t end_pos = ftello(d->file);

	// Go back to the previous position.
	fseeko(d->file, cur_pos, SEEK_SET);

	// Return the file size.
	return end_pos;
}

/**
 * Get the filename.
 * @return Filename. (May be empty if the filename is not available.)
 */
string RpFile::filename(void) const
{
	RP_D(const RpFile);
	return d->filename;
}

/** Device file functions **/

/**
 * Is this a device file?
 * @return True if this is a device file; false if not.
 */
bool RpFile::isDevice(void) const
{
	RP_D(const RpFile);
	return d->isDevice;
}

}
