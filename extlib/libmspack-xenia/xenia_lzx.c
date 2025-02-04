/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia_lzx.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "lzx.h"
#include "mspack.h"

// Xenia assertion macros.
// Originally defined in: src/xenia/base/assert.h
#include <assert.h>
#define assert_true(x) assert(x)

// std::min() replacement.
#ifndef min
# define min(x, y) ((x) < (y) ? (x) : (y))
#endif

typedef struct mspack_memory_file_t {
  struct mspack_system sys;
  void* buffer;
  off_t buffer_size;
  off_t offset;
} mspack_memory_file;
mspack_memory_file* mspack_memory_open(struct mspack_system* sys, void* buffer,
                                       const size_t buffer_size) {
  mspack_memory_file* memfile;
  ((void)sys);

  assert_true(buffer_size < INT_MAX);
  if (buffer_size >= INT_MAX) {
    return NULL;
  }
  memfile = (mspack_memory_file*)calloc(1, sizeof(mspack_memory_file));
  if (!memfile) {
    return NULL;
  }
  memfile->buffer = buffer;
  memfile->buffer_size = (off_t)buffer_size;
  memfile->offset = 0;
  return memfile;
}
void mspack_memory_close(mspack_memory_file* file) {
  mspack_memory_file* memfile = (mspack_memory_file*)file;
  free(memfile);
}
int mspack_memory_read(struct mspack_file* file, void* buffer, int chars) {
  mspack_memory_file* memfile = (mspack_memory_file*)file;
  const off_t remaining = memfile->buffer_size - memfile->offset;
  const off_t total = min((off_t)chars, remaining);
  memcpy(buffer, (uint8_t*)memfile->buffer + memfile->offset, total);
  memfile->offset += total;
  return (int)total;
}
int mspack_memory_write(struct mspack_file* file, void* buffer, int chars) {
  mspack_memory_file* memfile = (mspack_memory_file*)file;
  const off_t remaining = memfile->buffer_size - memfile->offset;
  const off_t total = min((off_t)chars, remaining);
  memcpy((uint8_t*)memfile->buffer + memfile->offset, buffer, total);
  memfile->offset += total;
  return (int)total;
}
void* mspack_memory_alloc(struct mspack_system* sys, size_t chars) {
  ((void)sys);
  return calloc(chars, 1);
}
void mspack_memory_free(void* ptr) { free(ptr); }
void mspack_memory_copy(void* src, void* dest, size_t chars) {
  memcpy(dest, src, chars);
}
struct mspack_system* mspack_memory_sys_create() {
  struct mspack_system* sys =
      (struct mspack_system*)calloc(1, sizeof(struct mspack_system));
  if (!sys) {
    return NULL;
  }
  sys->read = mspack_memory_read;
  sys->write = mspack_memory_write;
  sys->alloc = mspack_memory_alloc;
  sys->free = mspack_memory_free;
  sys->copy = mspack_memory_copy;
  return sys;
}
void mspack_memory_sys_destroy(struct mspack_system* sys) { free(sys); }

int lzx_decompress(const void* lzx_data, size_t lzx_len, void* dest,
                   size_t dest_len, uint32_t window_size, void* window_data,
                   size_t window_data_len) {
  uint32_t window_bits = 0;
  uint32_t temp_sz = window_size;
  size_t m;
  int result_code = 1;

  struct mspack_system* sys;
  mspack_memory_file* lzxsrc;
  mspack_memory_file* lzxdst;
  struct lzxd_stream* lzxd;

  for (m = 0; m < 32; m++, window_bits++) {
    temp_sz >>= 1;
    if (temp_sz == 0x00000000) {
      break;
    }
  }

  sys = mspack_memory_sys_create();
  lzxsrc = mspack_memory_open(sys, (void*)lzx_data, lzx_len);
  lzxdst = mspack_memory_open(sys, dest, dest_len);
  lzxd = lzxd_init(sys, (struct mspack_file*)lzxsrc, (struct mspack_file*)lzxdst,
                window_bits, 0, 0x8000, (off_t)dest_len, 0);

  if (lzxd) {
    if (window_data) {
      // zero the window and then copy window_data to the end of it
      memset(lzxd->window, 0, window_data_len);
      memcpy(lzxd->window + (window_size - window_data_len), window_data,
                  window_data_len);
      lzxd->ref_data_size = (uint32_t)window_data_len;
    }

    result_code = lzxd_decompress(lzxd, (off_t)dest_len);

    lzxd_free(lzxd);
    lzxd = NULL;
  }
  if (lzxsrc) {
    mspack_memory_close(lzxsrc);
    lzxsrc = NULL;
  }
  if (lzxdst) {
    mspack_memory_close(lzxdst);
    lzxdst = NULL;
  }
  if (sys) {
    mspack_memory_sys_destroy(sys);
    sys = NULL;
  }

  return result_code;
}
