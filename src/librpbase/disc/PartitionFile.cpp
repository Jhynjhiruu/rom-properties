/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * PartitionFile.hpp: IRpFile implementation for IPartition.               *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "PartitionFile.hpp"
#include "IDiscReader.hpp"

// C includes. (C++ namespace)
#include <cerrno>

// C++ includes.
#include <string>
using std::string;

namespace LibRpBase {

/**
 * Open a file from an IPartition.
 * NOTE: These files are read-only.
 * @param partition	[in] IPartition (or IDiscReader) object.
 * @param offset	[in] File starting offset.
 * @param size		[in] File size.
 */
PartitionFile::PartitionFile(IDiscReader *partition, int64_t offset, int64_t size)
	: super()
	, m_partition(partition)
	, m_offset(offset)
	, m_size(size)
	, m_pos(0)
{
	if (!partition) {
		m_lastError = EBADF;
	}

	// TODO: Reference counting?
}

/**
 * Is the file open?
 * This usually only returns false if an error occurred.
 * @return True if the file is open; false if it isn't.
 */
bool PartitionFile::isOpen(void) const
{
	return (m_partition != nullptr);
}

/**
 * Close the file.
 */
void PartitionFile::close(void)
{
	m_partition = nullptr;
}

/**
 * Read data from the file.
 * @param ptr Output data buffer.
 * @param size Amount of data to read, in bytes.
 * @return Number of bytes read.
 */
size_t PartitionFile::read(void *ptr, size_t size)
{
	if (!m_partition) {
		m_lastError = EBADF;
		return 0;
	}

	// Check if size is in bounds.
	if (m_pos > m_size - static_cast<int64_t>(size)) {
		// Not enough data.
		// Copy whatever's left in the file.
		size = static_cast<size_t>(m_size - m_pos);
		if (size == 0) {
			// Nothing left.
			// TODO: Set an error?
			return 0;
		}
	}

	m_partition->clearError();
	int iRet = m_partition->seek(m_offset + m_pos);
	if (iRet != 0) {
		m_lastError = m_partition->lastError();
		return 0;
	}

	size_t ret = 0;
	if (size > 0) {
		m_partition->clearError();
		ret = m_partition->read(ptr, size);
		m_pos += ret;
		m_lastError = m_partition->lastError();
	}

	return ret;
}

/**
 * Write data to the file.
 * (NOTE: Not valid for PartitionFile; this will always return 0.)
 * @param ptr Input data buffer.
 * @param size Amount of data to read, in bytes.
 * @return Number of bytes written.
 */
size_t PartitionFile::write(const void *ptr, size_t size)
{
	// Not a valid operation for PartitionFile.
	RP_UNUSED(ptr);
	RP_UNUSED(size);
	m_lastError = EBADF;
	return 0;
}

/**
 * Set the file position.
 * @param pos File position.
 * @return 0 on success; -1 on error.
 */
int PartitionFile::seek(int64_t pos)
{
	if (!m_partition) {
		m_lastError = EBADF;
		return -1;
	}

	if (pos <= 0) {
		m_pos = 0;
	} else if (pos >= m_size) {
		m_pos = m_size;
	} else {
		m_pos = pos;
	}

	return 0;
}

/**
 * Get the file position.
 * @return File position, or -1 on error.
 */
int64_t PartitionFile::tell(void)
{
	if (!m_partition) {
		m_lastError = EBADF;
		return -1;
	}

	return m_pos;
}

/**
 * Truncate the file.
 * @param size New size. (default is 0)
 * @return 0 on success; -1 on error.
 */
int PartitionFile::truncate(int64_t size)
{
	// Not supported.
	RP_UNUSED(size);
	m_lastError = ENOTSUP;
	return -m_lastError;
}

/** File properties. **/

/**
 * Get the file size.
 * @return File size, or negative on error.
 */
int64_t PartitionFile::size(void)
{
	if (!m_partition) {
		m_lastError = EBADF;
		return -1;
	}

	return m_size;
}

/**
 * Get the filename.
 * @return Filename. (May be empty if the filename is not available.)
 */
string PartitionFile::filename(void) const
{
	// TODO: Implement this.
	return string();
}

}
