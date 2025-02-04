/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * IsoPartition.cpp: ISO-9660 partition reader.                            *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "IsoPartition.hpp"

#include "iso_structs.h"

// librpbase
#include "librpbase/TextFuncs.hpp"
#include "librpbase/disc/PartitionFile.hpp"
using namespace LibRpBase;

// C includes. (C++ namespace)
#include <cassert>
#include <cerrno>

// C++ includes.
#include <string>
using std::string;

// Uninitialized vector class.
// Reference: http://andreoffringa.org/?q=uvector
#include "uvector.h"

namespace LibRomData {

class IsoPartitionPrivate
{
	public:
		IsoPartitionPrivate(IsoPartition *q,
			int64_t partition_offset, int iso_start_offset);
		~IsoPartitionPrivate();

	private:
		RP_DISABLE_COPY(IsoPartitionPrivate)
	protected:
		IsoPartition *const q_ptr;

	public:
		// Partition start offset. (in bytes)
		int64_t partition_offset;
		int64_t partition_size;		// Calculated partition size.

		// ISO start offset. (in blocks)
		// -1 == unknown
		int iso_start_offset;

		// ISO primary volume descriptor.
		ISO_Primary_Volume_Descriptor pvd;

		// Root directory.
		// NOTE: Directory entries are variable-length, so this
		// is a byte array, not an ISO_DirEntry array.
		ao::uvector<uint8_t> rootDir_data;

		/**
		 * Load the root directory.
		 * @return 0 on success; negative POSIX error code on error.
		 */
		int loadRootDirectory(void);
};

/** IsoPartitionPrivate **/

IsoPartitionPrivate::IsoPartitionPrivate(IsoPartition *q,
	int64_t partition_offset, int iso_start_offset)
	: q_ptr(q)
	, partition_offset(partition_offset)
	, partition_size(0)
	, iso_start_offset(iso_start_offset)
{
	// Clear the PVD struct.
	memset(&pvd, 0, sizeof(pvd));

	if (!q->m_discReader) {
		q->m_lastError = EIO;
		return;
	} else if (!q->m_discReader->isOpen()) {
		q->m_lastError = q->m_discReader->lastError();
		if (q->m_lastError == 0) {
			q->m_lastError = EIO;
		}
		q->m_discReader = nullptr;
		return;
	}

	// Calculated partition size.
	partition_size = q->m_discReader->size() - partition_offset;

	// Load the primary volume descriptor.
	// TODO: Assuming this is the first one.
	// Check for multiple?
	size_t size = q->m_discReader->seekAndRead(partition_offset + 0x8000, &pvd, sizeof(pvd));
	if (size != sizeof(pvd)) {
		// Seek and/or read error.
		q->m_discReader = nullptr;
		return;
	}

	// Verify the signature and volume descriptor type.
	if (pvd.header.type != ISO_VDT_PRIMARY ||
	    pvd.header.version != ISO_VD_VERSION ||
	    memcmp(pvd.header.identifier, ISO_VD_MAGIC, sizeof(pvd.header.identifier)) != 0)
	{
		// Invalid volume descriptor.
		q->m_discReader = nullptr;
		return;
	}

	// Load the root directory.
	loadRootDirectory();
}

IsoPartitionPrivate::~IsoPartitionPrivate()
{ }

/**
 * Load the root directory.
 * @return 0 on success; negative POSIX error code on error.
 */
int IsoPartitionPrivate::loadRootDirectory(void)
{
	RP_Q(IsoPartition);
	if (unlikely(!rootDir_data.empty())) {
		// Root directory is already loaded.
		return 0;
	} else if (unlikely(!q->m_discReader)) {
		// DiscReader isn't open.
		q->m_lastError = EIO;
		return -q->m_lastError;
	} else if (unlikely(pvd.header.type != ISO_VDT_PRIMARY)) {
		// PVD isn't loaded.
		q->m_lastError = EIO;
		return -q->m_lastError;
	}

	// Block size.
	// Should be 2048, but other values are possible.
	const unsigned int block_size = pvd.logical_block_size.he;

	// Check the root directory entry.
	const ISO_DirEntry *const rootdir = &pvd.dir_entry_root;
	if (rootdir->size.he > 16*1024*1024) {
		// Root directory is too big.
		q->m_lastError = EIO;
		return -q->m_lastError;
	}

	if (iso_start_offset >= 0) {
		// ISO start address was already determined.
		if (rootdir->block.he < ((unsigned int)iso_start_offset + 2)) {
			// Starting block is invalid.
			q->m_lastError = EIO;
			return -q->m_lastError;
		}
	} else {
		// We didn't find the ISO start address yet.
		// This might be a 2048-byte single-track image,
		// in which case, we'll need to assume that the
		// root directory starts at block 20.
		// TODO: Better heuristics.
		if (rootdir->block.he < 20) {
			// Starting block is invalid.
			q->m_lastError = EIO;
			return -q->m_lastError;
		}
		iso_start_offset = static_cast<int>(rootdir->block.he - 20);
	}

	// Load the root directory.
	// NOTE: Due to variable-length entries, we need to load
	// the entire root directory all at once.
	rootDir_data.resize(rootdir->size.he);
	const int64_t rootDir_addr = partition_offset +
		static_cast<int64_t>(rootdir->block.he - iso_start_offset) * block_size;
	size_t size = q->m_discReader->seekAndRead(rootDir_addr, rootDir_data.data(), rootDir_data.size());
	if (size != rootDir_data.size()) {
		// Seek and/or read error.
		rootDir_data.clear();
		q->m_lastError = q->m_discReader->lastError();
		if (q->m_lastError == 0) {
			q->m_lastError = EIO;
		}
		return -q->m_lastError;
	}

	// Root directory loaded.
	return 0;
}

/** IsoPartition **/

/**
 * Construct an IsoPartition with the specified IDiscReader.
 *
 * NOTE: The IDiscReader *must* remain valid while this
 * IsoPartition is open.
 *
 * @param discReader IDiscReader.
 * @param partition_offset Partition start offset.
 * @param iso_start_offset ISO start offset, in blocks. (If -1, uses heuristics.)
 */
IsoPartition::IsoPartition(IDiscReader *discReader, int64_t partition_offset, int iso_start_offset)
	: super(discReader)
	, d_ptr(new IsoPartitionPrivate(this, partition_offset, iso_start_offset))
{ }

IsoPartition::~IsoPartition()
{
	delete d_ptr;
}

/** IDiscReader **/

/**
 * Read data from the file.
 * @param ptr Output data buffer.
 * @param size Amount of data to read, in bytes.
 * @return Number of bytes read.
 */
size_t IsoPartition::read(void *ptr, size_t size)
{
	assert(m_discReader != nullptr);
	assert(m_discReader->isOpen());
	if (!m_discReader || !m_discReader->isOpen()) {
		m_lastError = EBADF;
		return 0;
	}

	// GCN partitions are stored as-is.
	// TODO: data_size checks?
	return m_discReader->read(ptr, size);
}

/**
 * Set the partition position.
 * @param pos Partition position.
 * @return 0 on success; -1 on error.
 */
int IsoPartition::seek(int64_t pos)
{
	RP_D(IsoPartition);
	assert(m_discReader != nullptr);
	assert(m_discReader->isOpen());
	if (!m_discReader ||  !m_discReader->isOpen()) {
		m_lastError = EBADF;
		return -1;
	}

	int ret = m_discReader->seek(d->partition_offset + pos);
	if (ret != 0) {
		m_lastError = m_discReader->lastError();
	}
	return ret;
}

/**
 * Get the partition position.
 * @return Partition position on success; -1 on error.
 */
int64_t IsoPartition::tell(void)
{
	RP_D(IsoPartition);
	assert(m_discReader != nullptr);
	assert(m_discReader->isOpen());
	if (!m_discReader ||  !m_discReader->isOpen()) {
		m_lastError = EBADF;
		return -1;
	}

	int64_t ret = m_discReader->tell() - d->partition_offset;
	if (ret < 0) {
		m_lastError = m_discReader->lastError();
	}
	return ret;
}

/**
 * Get the data size.
 * This size does not include the partition header,
 * and it's adjusted to exclude hashes.
 * @return Data size, or -1 on error.
 */
int64_t IsoPartition::size(void)
{
	// TODO: Restrict partition size?
	RP_D(const IsoPartition);
	if (!m_discReader)
		return -1;
	return d->partition_size;
}

/** Device file functions **/

/** IPartition **/

/**
 * Get the partition size.
 * This size includes the partition header and hashes.
 * @return Partition size, or -1 on error.
 */
int64_t IsoPartition::partition_size(void) const
{
	// TODO: Restrict partition size?
	RP_D(const IsoPartition);
	if (!m_discReader)
		return -1;
	return d->partition_size;
}

/**
 * Get the used partition size.
 * This size includes the partition header and hashes,
 * but does not include "empty" sectors.
 * @return Used partition size, or -1 on error.
 */
int64_t IsoPartition::partition_size_used(void) const
{
	// TODO: Implement for ISO?
	// For now, just use partition_size().
	return partition_size();
}

/** IsoPartition **/

/** GcnFst wrapper functions. **/

// TODO

#if 0
/**
 * Open a directory.
 * @param path	[in] Directory path.
 * @return IFst::Dir*, or nullptr on error.
 */
IFst::Dir *IsoPartition::opendir(const char *path)
{
	RP_D(IsoPartition);
	if (!d->fst) {
		// FST isn't loaded.
		if (d->loadFst() != 0) {
			// FST load failed.
			// TODO: Errors?
			return nullptr;
		}
	}

	return d->fst->opendir(path);
}

/**
 * Read a directory entry.
 * @param dirp FstDir pointer.
 * @return IFst::DirEnt*, or nullptr if end of directory or on error.
 * (TODO: Add lastError()?)
 */
IFst::DirEnt *IsoPartition::readdir(IFst::Dir *dirp)
{
	RP_D(IsoPartition);
	if (!d->fst) {
		// TODO: Errors?
		return nullptr;
	}

	return d->fst->readdir(dirp);
}

/**
 * Close an opened directory.
 * @param dirp FstDir pointer.
 * @return 0 on success; negative POSIX error code on error.
 */
int IsoPartition::closedir(IFst::Dir *dirp)
{
	RP_D(IsoPartition);
	if (!d->fst) {
		// TODO: Errors?
		return -EBADF;
	}

	return d->fst->closedir(dirp);
}
#endif

/**
 * Open a file. (read-only)
 * @param filename Filename.
 * @return IRpFile*, or nullptr on error.
 */
IRpFile *IsoPartition::open(const char *filename)
{
	RP_D(IsoPartition);
	assert(m_discReader != nullptr);
	assert(m_discReader->isOpen());
	if (!m_discReader ||  !m_discReader->isOpen()) {
		m_lastError = EBADF;
		return nullptr;
	}

	// TODO: File reference counter.
	// This might be difficult to do because PartitionFile is a separate class.

	// TODO: Support subdirectories.

	if (!filename || filename[0] == 0) {
		// No filename.
		m_lastError = EINVAL;
		return nullptr;
	}

	// Remove leading slashes.
	while (*filename == '/') {
		filename++;
	}
	if (filename[0] == 0) {
		// Nothing but slashes...
		return nullptr;
	}

	// TODO: Which encoding?
	// Assuming cp1252...
	string s_filename = utf8_to_cp1252(filename, -1);

	if (d->rootDir_data.empty()) {
		// Root directory isn't loaded.
		if (d->loadRootDirectory() != 0) {
			// Root directory load failed.
			m_lastError = EIO;
			return nullptr;
		}
	}

	// Find the file in the root directory.
	// NOTE: Filenames are case-insensitive.
	// NOTE: File might have a ";1" suffix.
	const unsigned int filename_len = static_cast<unsigned int>(s_filename.size());
	const ISO_DirEntry *dirEntry_found = nullptr;
	const uint8_t *p = d->rootDir_data.data();
	const uint8_t *const p_end = p + d->rootDir_data.size();
	while (p < p_end) {
		const ISO_DirEntry *dirEntry = reinterpret_cast<const ISO_DirEntry*>(p);
		if (dirEntry->entry_length < sizeof(*dirEntry)) {
			// End of directory.
			break;
		}

		const char *entry_filename = reinterpret_cast<const char*>(p) + sizeof(*dirEntry);
		if (entry_filename + dirEntry->filename_length > reinterpret_cast<const char*>(p_end)) {
			// Filename is out of bounds.
			break;
		}

		// Check the filename.
		// 1990s and early 2000s CD-ROM games usually have
		// ";1" filenames, so check for that first.
		if (dirEntry->filename_length == filename_len + 2) {
			// +2 length match.
			// This might have ";1".
			if (!strncasecmp(entry_filename, s_filename.c_str(), filename_len)) {
				// Check for ";1".
				if (entry_filename[filename_len]   == ';' &&
				    entry_filename[filename_len+1] == '1')
				{
					// Found it!
					dirEntry_found = dirEntry;
					break;
				}
			}
		} else if (dirEntry->filename_length == filename_len) {
			// Exact length match.
			if (!strncasecmp(entry_filename, s_filename.c_str(), filename_len)) {
				// Found it!
				dirEntry_found = dirEntry;
				break;
			}
		}

		// Next entry.
		p += dirEntry->entry_length;
	}

	if (!dirEntry_found) {
		// Not found.
		return nullptr;
	}

	// Make sure this is a regular file.
	// TODO: What is an "associated" file?
	if (dirEntry_found->flags & (ISO_FLAG_ASSOCIATED | ISO_FLAG_DIRECTORY)) {
		// Not a regular file.
		m_lastError = ((dirEntry_found->flags & ISO_FLAG_DIRECTORY) ? EISDIR : EPERM);
		return nullptr;
	}

	// Block size.
	// Should be 2048, but other values are possible.
	const unsigned int block_size = d->pvd.logical_block_size.he;

	// Make sure the file is in bounds.
	const int64_t file_addr = (static_cast<int64_t>(dirEntry_found->block.he) - d->iso_start_offset) * block_size;
	if (file_addr >= d->partition_size + d->partition_offset ||
	    file_addr > d->partition_size + d->partition_offset - dirEntry_found->size.he)
	{
		// File is out of bounds.
		m_lastError = EIO;
		return nullptr;
	}

	// Create the PartitionFile.
	// This is an IRpFile implementation that uses an
	// IPartition as the reader and takes an offset
	// and size as the file parameters.
	return new PartitionFile(this, file_addr, dirEntry_found->size.he);
}

}
