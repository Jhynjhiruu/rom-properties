/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * SemaphoreWin32.cpp: Win32 semaphore implementation.                     *
 *                                                                         *
 * Copyright (c) 2016-2018 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "Semaphore.hpp"
#include "libwin32common/RpWin32_sdk.h"

// C includes. (C++ namespace)
#include <cassert>
#include <cerrno>

namespace LibRpBase {

class Semaphore
{
	public:
		/**
		 * Create a semaphore.
		 * @param count Number of times the semaphore can be obtained before blocking.
		 */
		inline explicit Semaphore(int count);

		/**
		 * Delete the semaphore.
		 * WARNING: Semaphore MUST be fully released!
		 */
		inline ~Semaphore();

	private:
		RP_DISABLE_COPY(Semaphore)

	public:
		/**
		 * Obtain the semaphore.
		 * If the semaphore is at zero, this function will block
		 * until another thread releases the semaphore.
		 * @return 0 on success; non-zero on error.
		 */
		inline int obtain(void);

		/**
		 * Release a lock on the semaphore.
		 * @return 0 on success; non-zero on error.
		 */
		inline int release(void);

	private:
		HANDLE m_sem;
};

/**
 * Create a semaphore.
 * @param count Number of times the semaphore can be obtained before blocking.
 */
inline Semaphore::Semaphore(int count)
{
	m_sem = CreateSemaphore(nullptr, count, count, nullptr);
	assert(m_sem != nullptr);
	if (!m_sem) {
		// FIXME: Do something if an error occurred here...
	}
}

/**
 * Delete the semaphore.
 * WARNING: Semaphore MUST be fully released!
 */
inline Semaphore::~Semaphore()
{
	if (m_sem) {
		CloseHandle(m_sem);
	}
}

/**
 * Obtain the semaphore.
 * If the semaphore is at zero, this function will block
 * until another thread releases the semaphore.
 * @return 0 on success; non-zero on error.
 */
inline int Semaphore::obtain(void)
{
	if (!m_sem)
		return -EBADF;

	DWORD dwWaitResult = WaitForSingleObject(m_sem, INFINITE);
	if (dwWaitResult == WAIT_OBJECT_0)
		return 0;

	// TODO: What error to return?
	return -1;
}

/**
 * Release a lock on the semaphore.
 * @return 0 on success; non-zero on error.
 */
inline int Semaphore::release(void)
{
	if (!m_sem)
		return -EBADF;

	BOOL bRet = ReleaseSemaphore(m_sem, 1, nullptr);
	if (bRet != 0)
		return 0;

	// TODO: What error to return?
	return -1;
}

}
