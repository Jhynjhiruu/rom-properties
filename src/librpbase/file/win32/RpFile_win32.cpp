/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * RpFile_win32.cpp: Standard file object. (Win32 implementation)          *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "config.librpbase.h"

#include "../RpFile.hpp"
#include "RpFile_win32_p.hpp"

// librpbase
#include "bitstuff.h"
#include "byteswap.h"
#include "TextFuncs.hpp"
#include "TextFuncs_wchar.hpp"

// libwin32common
#include "libwin32common/w32err.h"

// C includes.
#include <fcntl.h>

// C includes. (C++ namespace)
#include "librpbase/ctypex.h"
#include <cassert>

// C++ includes.
#include <memory>
#include <string>
using std::string;
using std::unique_ptr;
using std::wstring;

#ifdef _MSC_VER
// MSVC: Exception handling for /DELAYLOAD.
#include "libwin32common/DelayLoadHelper.h"
#endif /* _MSC_VER */

namespace LibRpBase {

#ifdef _MSC_VER
// DelayLoad test implementation.
DELAYLOAD_TEST_FUNCTION_IMPL0(zlibVersion);
#endif /* _MSC_VER */

/** RpFilePrivate **/

RpFilePrivate::~RpFilePrivate()
{
	if (gzfd) {
		gzclose_r(gzfd);
	}
	if (file && file != INVALID_HANDLE_VALUE) {
		CloseHandle(file);
	}
}

/**
 * Convert an RpFile::FileMode to Win32 CreateFile() parameters.
 * @param mode				[in] FileMode
 * @param pdwDesiredAccess		[out] dwDesiredAccess
 * @param pdwShareMode			[out] dwShareMode
 * @param pdwCreationDisposition	[out] dwCreationDisposition
 * @return 0 on success; non-zero on error.
 */
inline int RpFilePrivate::mode_to_win32(RpFile::FileMode mode,
	DWORD *pdwDesiredAccess,
	DWORD *pdwShareMode,
	DWORD *pdwCreationDisposition)
{
	switch (mode & RpFile::FM_MODE_MASK) {
		case RpFile::FM_OPEN_READ:
			*pdwDesiredAccess = GENERIC_READ;
			*pdwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
			*pdwCreationDisposition = OPEN_EXISTING;
			break;
		case RpFile::FM_OPEN_WRITE:
			*pdwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
			*pdwShareMode = FILE_SHARE_READ;
			*pdwCreationDisposition = OPEN_EXISTING;
			break;
		case RpFile::FM_CREATE|RpFile::FM_READ /*RpFile::FM_CREATE_READ*/ :
		case RpFile::FM_CREATE_WRITE:
			*pdwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
			*pdwShareMode = FILE_SHARE_READ;
			*pdwCreationDisposition = CREATE_ALWAYS;
			break;
		default:
			// Invalid mode.
			return -1;
	}

	// Mode converted successfully.
	return 0;
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

	// Determine the file mode.
	DWORD dwDesiredAccess, dwShareMode, dwCreationDisposition;
	if (mode_to_win32(mode, &dwDesiredAccess, &dwShareMode, &dwCreationDisposition) != 0) {
		// Invalid mode.
		q->m_lastError = EINVAL;
		return -EINVAL;
	}

	// Converted filename for Windows.
	tstring tfilename;

	// If the filename is "X:", change it to "X:\\".
	if (filename.size() == 2 &&
	    ISASCII(filename[0]) && ISALPHA(filename[0]) &&
	    filename[1] == ':')
	{
		// Drive letter. Append '\\'.
		filename += '\\';
	}

	// Check if the path starts with a drive letter.
	if (filename.size() >= 3 &&
	    ISASCII(filename[0]) && ISALPHA(filename[0]) &&
	    filename[1] == ':' && filename[2] == '\\')
	{
		// Is it only a drive letter?
		if (filename.size() == 3) {
			// This is a drive letter.
			// Only CD-ROM (and similar) drives are supported.
			// TODO: Verify if opening by drive letter works,
			// or if we have to resolve the physical device name.
			// NOTE: filename is UTF-8, but we can use it as if
			// it's ANSI for a drive letter.
			const UINT driveType = GetDriveTypeA(filename.c_str());
			switch (driveType) {
				case DRIVE_CDROM:
					// CD-ROM works.
					break;
				case DRIVE_UNKNOWN:
				case DRIVE_NO_ROOT_DIR:
					// No drive.
					isDevice = false;
					q->m_lastError = ENODEV;
					return -ENODEV;
				default:
					// Not a CD-ROM drive.
					isDevice = false;
					q->m_lastError = ENOTSUP;
					return -ENOTSUP;
			}

			// Create a raw device filename.
			// Reference: https://support.microsoft.com/en-us/help/138434/how-win32-based-applications-read-cd-rom-sectors-in-windows-nt
			tfilename = _T("\\\\.\\X:");
			tfilename[4] = filename[0];
			isDevice = true;
		} else {
			// Absolute path.
			isDevice = false;
#ifdef UNICODE
			// Unicode only: Prepend "\\?\" in order to support filenames longer than MAX_PATH.
			tfilename = _T("\\\\?\\");
			tfilename += U82T_s(filename);
#else /* !UNICODE */
			// ANSI: Use the filename directly.
			tfilename = U82T_s(filename);
#endif /* UNICODE */
		}
	} else {
		// Not an absolute path, or "\\?\" is already
		// prepended. Use it as-is.
		isDevice = false;
		tfilename = U82T_s(filename);
	}

	if (!isDevice) {
		// Make sure this isn't a directory.
		// TODO: Other checks?
		DWORD dwAttr = GetFileAttributes(tfilename.c_str());
		if (dwAttr == INVALID_FILE_ATTRIBUTES) {
			// File cannot be opened.
			RP_Q(RpFile);
			q->m_lastError = EIO;
			return -EIO;
		} else if (dwAttr & FILE_ATTRIBUTE_DIRECTORY) {
			// File is a directory.
			RP_Q(RpFile);
			q->m_lastError = EISDIR;
			return -EISDIR;
		}
	}

	if (isDevice) {
		if (mode & RpFile::FM_WRITE) {
			// Writing to block devices is not allowed.
			q->m_lastError = EINVAL;
			return -EINVAL;
		}
		// NOTE: We need WRITE permission for
		// DeviceIoControl() to function properly.
		dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
		dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	}

	// Open the file.
	if (file && file != INVALID_HANDLE_VALUE) {
		CloseHandle(file);
	}
	file = CreateFile(tfilename.c_str(),
			dwDesiredAccess,
			dwShareMode,
			nullptr,
			dwCreationDisposition,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
	if (isDevice) {
		if (!file || file == INVALID_HANDLE_VALUE) {
			// Try again without WRITE permission.
			file = CreateFile(tfilename.c_str(),
					GENERIC_READ,
					FILE_SHARE_READ,
					nullptr,
					dwCreationDisposition,
					FILE_ATTRIBUTE_NORMAL,
					nullptr);
		}
	}
	if (!file || file == INVALID_HANDLE_VALUE) {
		// Error opening the file.
		q->m_lastError = w32err_to_posix(GetLastError());
		return -q->m_lastError;
	}

	if (isDevice) {
		// Get the disk space.
		// NOTE: IOCTL_DISK_GET_DRIVE_GEOMETRY_EX seems to report 512-byte sectors
		// for certain emulated CD-ROM device, e.g. the Verizon LG G2.
		// GetDiskFreeSpace() reports the correct value (2048).
		DWORD dwSectorsPerCluster, dwBytesPerSector;
		DWORD dwNumberOfFreeClusters, dwTotalNumberOfClusters;
		DWORD w32err = 0;
		// NOTE: tfilename doesn't work here.
		// Use the original UTF-8 filename as if it's ANSI.
		BOOL bRet = GetDiskFreeSpaceA(filename.c_str(),
			&dwSectorsPerCluster, &dwBytesPerSector,
			&dwNumberOfFreeClusters, &dwTotalNumberOfClusters);
		if (bRet && dwBytesPerSector >= 512 && dwTotalNumberOfClusters > 0) {
			// TODO: Make sure the sector size is a power of 2
			// and isn't a ridiculous value.

			// Save the device size and sector size.
			// NOTE: GetDiskFreeSpaceEx() eliminates the need for multiplications,
			// but it doesn't provide dwBytesPerSector.
			device_size = static_cast<int64_t>(dwBytesPerSector) *
				      static_cast<int64_t>(dwSectorsPerCluster) *
				      static_cast<int64_t>(dwTotalNumberOfClusters);
			sector_size = dwBytesPerSector;
		} else {
			// GetDiskFreeSpace() failed.
			w32err = GetLastError();
			if (w32err == ERROR_INVALID_PARAMETER) {
				// The disk may use some file system that
				// Windows doesn't recognize.
				// Try IOCTL_DISK_GET_DRIVE_GEOMETRY_EX instead.
				DISK_GEOMETRY_EX dg;
				DWORD dwBytesReturned;  // TODO: Check this?
				if (DeviceIoControl(file, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
				    NULL, 0, &dg, sizeof(dg), &dwBytesReturned, NULL) != 0)
				{
					// Device geometry retrieved.
					w32err = 0;
					device_size = dg.DiskSize.QuadPart;
					sector_size = dg.Geometry.BytesPerSector;
				} else {
					// IOCTL failed.
					w32err = GetLastError();
					if (w32err == 0) {
						w32err = ERROR_INVALID_PARAMETER;
					}
				}
			}
		}

		if (w32err != 0) {
			// An error occurred...
			q->m_lastError = w32err_to_posix(GetLastError());
			if (q->m_lastError == 0) {
				q->m_lastError = EIO;
			}
			CloseHandle(file);
			file = INVALID_HANDLE_VALUE;
			return -q->m_lastError;
		}
	}

	// Return 0 if it's *not* nullptr or INVALID_HANDLE_VALUE.
	return (!file || file == INVALID_HANDLE_VALUE);
}

/**
 * Read using block reads.
 * Required for block devices.
 * @param ptr Output data buffer.
 * @param size Amount of data to read, in bytes.
 * @return Number of bytes read.
 */
size_t RpFilePrivate::readUsingBlocks(void *ptr, size_t size)
{
	assert(device_size > 0);
	assert(sector_size >= 512);
	if (device_size <= 0 || sector_size < 512) {
		// Not a block device...
		return 0;
	}

	uint8_t *ptr8 = static_cast<uint8_t*>(ptr);
	size_t ret = 0;

	RP_Q(RpFile);
	int64_t pos = q->tell();

	// Are we already at the end of the block device?
	if (pos >= device_size) {
		// End of the block device.
		return 0;
	}

	// Make sure d->pos + size <= d->device_size.
	// If it isn't, we'll do a short read.
	if (pos + static_cast<int64_t>(size) >= device_size) {
		size = static_cast<size_t>(device_size - pos);
	}

	// Seek to the beginning of the first block.
	// TODO: Make sure sector_size is a power of 2.
	int seek_ret = q->seek(pos & ~(static_cast<int64_t>(sector_size) - 1));
	if (seek_ret != 0) {
		// Seek error.
		return 0;
	}

	// Sector buffer.
	unique_ptr<uint8_t[]> sector_buffer;

	// Check if we're not starting on a block boundary.
	const uint32_t blockStartOffset = pos % sector_size;
	if (blockStartOffset != 0) {
		// Not a block boundary.
		// Read the end of the first block.
		if (!sector_buffer) {
			sector_buffer.reset(new uint8_t[sector_size]);
		}

		// Read the first block.
		DWORD bytesRead;
		BOOL bRet = ReadFile(file, sector_buffer.get(), sector_size, &bytesRead, nullptr);
		if (bRet == 0 || bytesRead != sector_size) {
			// Read error.
			q->m_lastError = w32err_to_posix(GetLastError());
			return bytesRead;
		}

		// Copy the data from the sector buffer.
		uint32_t read_sz = sector_size - blockStartOffset;
		if (size < static_cast<size_t>(read_sz)) {
			read_sz = static_cast<uint32_t>(size);
		}
		memcpy(ptr8, &sector_buffer[blockStartOffset], read_sz);

		// Starting block read.
		size -= read_sz;
		ptr8 += read_sz;
		ret += read_sz;
	} else {
		// Seek to the beginning of the first block.
		seek_ret = q->seek(pos);
		if (seek_ret != 0) {
			// Seek error.
			return 0;
		}
	}

	// Must be on a sector boundary now.
	assert(q->tell() % sector_size == 0);

	// Read contiguous blocks.
	// NOTE: sector_size must be a power of two.
	assert(isPow2(sector_size));
	size_t contig_size = size & ~(static_cast<size_t>(sector_size) - 1);
	DWORD bytesRead;
	BOOL bRet = ReadFile(file, ptr8, contig_size, &bytesRead, nullptr);
	if (bRet == 0 || bytesRead != contig_size) {
		// Read error.
		q->m_lastError = w32err_to_posix(GetLastError());
		return ret + bytesRead;
	}
	size -= contig_size;
	ptr8 += contig_size;
	ret += contig_size;

	// Check if we still have data left. (not a full block)
	if (size > 0) {
		if (!sector_buffer) {
			sector_buffer.reset(new uint8_t[sector_size]);
		}

		// Read the last block.
		pos = q->tell();
		assert(pos % sector_size == 0);
		DWORD bytesRead;
		BOOL bRet = ReadFile(file, sector_buffer.get(), sector_size, &bytesRead, nullptr);
		if (bRet == 0 || bytesRead != sector_size) {
			// Read error.
			q->m_lastError = w32err_to_posix(GetLastError());
			return ret + bytesRead;
		}

		// Copy the data from the sector buffer.
		memcpy(ptr8, sector_buffer.get(), size);

		ret += size;
	}

	// Finished reading the data.
	return ret;
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

	// Cannot use decompression with writing, or when reading block devices.
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
	if (d->sector_size == 0 && d->mode == FM_OPEN_READ_GZ) {
#if defined(_MSC_VER) && defined(ZLIB_IS_DLL)
		// Delay load verification.
		// TODO: Only if linked with /DELAYLOAD?
		if (DelayLoad_test_zlibVersion() != 0) {
			// Delay load failed.
			// Don't do any gzip checking.
			return;
		}
#endif /* defined(_MSC_VER) && defined(ZLIB_IS_DLL) */

		DWORD bytesRead;
		BOOL bRet;

		uint16_t gzmagic;
		bRet = ReadFile(d->file, &gzmagic, sizeof(gzmagic), &bytesRead, nullptr);
		if (bRet && bytesRead == sizeof(gzmagic) && gzmagic == be16_to_cpu(0x1F8B)) {
			// This is a gzipped file.
			// Get the uncompressed size at the end of the file.
			LARGE_INTEGER liFileSize;
			bRet = GetFileSizeEx(d->file, &liFileSize);
			if (bRet && liFileSize.QuadPart > 10+8) {
				LARGE_INTEGER liSeekPos;
				liSeekPos.QuadPart = liFileSize.QuadPart - 4;
				bRet = SetFilePointerEx(d->file, liSeekPos, nullptr, FILE_BEGIN);
				if (bRet) {
					uint32_t uncomp_sz;
					bRet = ReadFile(d->file, &uncomp_sz, sizeof(uncomp_sz), &bytesRead, nullptr);
					uncomp_sz = le32_to_cpu(uncomp_sz);
					if (bRet && bytesRead == sizeof(uncomp_sz) && uncomp_sz >= liFileSize.QuadPart-(10+8)) {
						// Uncompressed size looks valid.
						d->gzsz = (int64_t)uncomp_sz;

						liSeekPos.QuadPart = 0;
						SetFilePointerEx(d->file, liSeekPos, nullptr, FILE_BEGIN);
						// NOTE: Not sure if this is needed on Windows.
						FlushFileBuffers(d->file);

						// Open the file with gzdopen().
						HANDLE hGzDup;
						BOOL bRet = DuplicateHandle(
							GetCurrentProcess(),	// hSourceProcessHandle
							d->file,		// hSourceHandle
							GetCurrentProcess(),	// hTargetProcessHandle
							&hGzDup,		// lpTargetHandle
							0,			// dwDesiredAccess
							FALSE,			// bInheritHandle
							DUPLICATE_SAME_ACCESS);	// dwOptions
						if (bRet) {
							// NOTE: close() on gzfd_dup() will close the
							// underlying Windows handle.
							int gzfd_dup = _open_osfhandle((intptr_t)hGzDup, _O_RDONLY);
							if (gzfd_dup >= 0) {
								// Make sure the CRC32 table is initialized.
								get_crc_table();

								d->gzfd = gzdopen(gzfd_dup, "r");
								if (!d->gzfd) {
									// gzdopen() failed.
									// Close the dup()'d handle to prevent a leak.
									_close(gzfd_dup);
								}
							} else {
								// Unable to open an fd.
								CloseHandle(hGzDup);
							}
						}
					}
				}
			}
		}

		if (!d->gzfd) {
			// Not a gzipped file.
			// Rewind and flush the file.
			LARGE_INTEGER liSeekPos;
			liSeekPos.QuadPart = 0;
			SetFilePointerEx(d->file, liSeekPos, nullptr, FILE_BEGIN);
			// NOTE: Not sure if this is needed on Windows.
			FlushFileBuffers(d->file);
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
	return (d->file != nullptr && d->file != INVALID_HANDLE_VALUE);
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
	if (d->file && d->file != INVALID_HANDLE_VALUE) {
		CloseHandle(d->file);
		d->file = INVALID_HANDLE_VALUE;
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
	if (!d->file || d->file == INVALID_HANDLE_VALUE) {
		m_lastError = EBADF;
		return 0;
	} else if (size == 0) {
		// Nothing to read.
		return 0;
	}

	if (d->sector_size != 0) {
		// Block device. Need to read in multiples of the block size.
		return d->readUsingBlocks(ptr, size);
	}

	DWORD bytesRead;
	if (d->gzfd) {
		int iret = gzread(d->gzfd, ptr, (unsigned int)size);
		if (iret >= 0) {
			bytesRead = (DWORD)iret;
		} else {
			// An error occurred.
			bytesRead = 0;
			m_lastError = errno;
		}
	} else {
		BOOL bRet = ReadFile(d->file, ptr, static_cast<DWORD>(size), &bytesRead, nullptr);
		if (!bRet) {
			// An error occurred.
			m_lastError = w32err_to_posix(GetLastError());
			bytesRead = 0;
		}
	}

	return bytesRead;
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
	if (!d->file || d->file == INVALID_HANDLE_VALUE || !(d->mode & FM_WRITE)) {
		// Either the file isn't open,
		// or it's read-only.
		m_lastError = EBADF;
		return 0;
	}

	if (d->sector_size != 0) {
		// Writing to block devices is not allowed.
		m_lastError = EBADF;
		return 0;
	}

	DWORD bytesWritten;
	BOOL bRet = WriteFile(d->file, ptr, static_cast<DWORD>(size), &bytesWritten, nullptr);
	if (!bRet) {
		// An error occurred.
		m_lastError = w32err_to_posix(GetLastError());
		return 0;
	}

	return bytesWritten;
}

/**
 * Set the file position.
 * @param pos File position.
 * @return 0 on success; -1 on error.
 */
int RpFile::seek(int64_t pos)
{
	RP_D(RpFile);
	if (!d->file || d->file == INVALID_HANDLE_VALUE) {
		m_lastError = EBADF;
		return -1;
	}

	int ret;
	if (d->gzfd) {
		// FIXME: Might not work with >2GB files...
		z_off_t zret = gzseek(d->gzfd, (long)pos, SEEK_SET);
		if (zret >= 0) {
			ret = 0;
		} else {
			// TODO: Does gzseek() set errno?
			ret = -1;
			m_lastError = -EIO;
		}
	} else {
		LARGE_INTEGER liSeekPos;
		liSeekPos.QuadPart = pos;
		BOOL bRet = SetFilePointerEx(d->file, liSeekPos, nullptr, FILE_BEGIN);
		if (bRet) {
			ret = 0;
		} else {
			ret = -1;
			m_lastError = w32err_to_posix(GetLastError());
		}
	}

	return ret;
}

/**
 * Get the file position.
 * @return File position, or -1 on error.
 */
int64_t RpFile::tell(void)
{
	RP_D(RpFile);
	if (!d->file || d->file == INVALID_HANDLE_VALUE) {
		m_lastError = EBADF;
		return -1;
	}

	if (d->gzfd) {
		return (int64_t)gztell(d->gzfd);
	}

	LARGE_INTEGER liSeekPos, liSeekRet;
	liSeekPos.QuadPart = 0;
	BOOL bRet = SetFilePointerEx(d->file, liSeekPos, &liSeekRet, FILE_CURRENT);
	if (!bRet) {
		m_lastError = w32err_to_posix(GetLastError());
		return -1;
	}

	return liSeekRet.QuadPart;
}

/**
 * Truncate the file.
 * @param size New size. (default is 0)
 * @return 0 on success; -1 on error.
 */
int RpFile::truncate(int64_t size)
{
	RP_D(RpFile);
	if (!d->file || d->file == INVALID_HANDLE_VALUE || !(d->mode & FM_WRITE)) {
		// Either the file isn't open,
		// or it's read-only.
		m_lastError = EBADF;
		return -1;
	} else if (size < 0) {
		m_lastError = EINVAL;
		return -1;
	}

	// Set the requested end of file, and
	// get the current file position.
	LARGE_INTEGER liSeekPos, liSeekRet;
	liSeekPos.QuadPart = size;
	BOOL bRet = SetFilePointerEx(d->file, liSeekPos, &liSeekRet, FILE_CURRENT);
	if (!bRet) {
		m_lastError = w32err_to_posix(GetLastError());
		return -1;
	}

	// Truncate the file.
	bRet = SetEndOfFile(d->file);
	if (!bRet) {
		m_lastError = w32err_to_posix(GetLastError());
		return -1;
	}

	// Restore the original position if it was
	// less than the new size.
	if (liSeekRet.QuadPart < size) {
		bRet = SetFilePointerEx(d->file, liSeekRet, nullptr, FILE_BEGIN);
		if (!bRet) {
			m_lastError = w32err_to_posix(GetLastError());
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
	if (!d->file || d->file == INVALID_HANDLE_VALUE) {
		m_lastError = EBADF;
		return -1;
	}

	// TODO: Error checking?

	if (d->sector_size != 0) {
		// Block device. Use the cached device size.
		return d->device_size;
	} else if (d->gzfd) {
		// gzipped files have the uncompressed size stored
		// at the end of the stream.
		return d->gzsz;
	}

	// Regular file.
	LARGE_INTEGER liFileSize;
	if (!GetFileSizeEx(d->file, &liFileSize)) {
		// Could not get the file size.
		m_lastError = w32err_to_posix(GetLastError());
		return -1;
	}

	// Return the file/device size.
	return liFileSize.QuadPart;
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
