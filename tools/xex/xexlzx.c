/* Standalone XEX LZX-stream decompressor (mspack lzxd.c + xenia's memory harness).
 * Usage: xexlzx <lzx_stream_file> <out_file> <window_size> <out_size>
 * The lzx_stream_file is the already-decrypted, de-blocked LZX chunk stream
 * (Python does the AES-CBC + SHA-block-strip). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "mspack.h"
#include "lzx.h"

void xenia_log(const char* fmt, ...) { (void)fmt; }

typedef struct { struct mspack_system sys; void* buffer; off_t buffer_size; off_t offset; } memfile;

static int mem_read(struct mspack_file* f, void* buf, int chars) {
  memfile* m = (memfile*)f; off_t rem = m->buffer_size - m->offset;
  off_t n = (off_t)chars < rem ? (off_t)chars : rem;
  if (n < 0) n = 0;
  memcpy(buf, (unsigned char*)m->buffer + m->offset, (size_t)n); m->offset += n; return (int)n;
}
static int mem_write(struct mspack_file* f, void* buf, int chars) {
  memfile* m = (memfile*)f; off_t rem = m->buffer_size - m->offset;
  off_t n = (off_t)chars < rem ? (off_t)chars : rem;
  if (n < 0) n = 0;
  memcpy((unsigned char*)m->buffer + m->offset, buf, (size_t)n); m->offset += n; return (int)n;
}
static void* mem_alloc(struct mspack_system* s, size_t n) { return calloc(n, 1); }
static void  mem_free(void* p) { free(p); }
static void  mem_copy(void* s, void* d, size_t n) { memcpy(d, s, n); }

static memfile* mem_open(struct mspack_system* sys, void* buffer, size_t sz) {
  memfile* m = (memfile*)calloc(1, sizeof(memfile));
  m->buffer = buffer; m->buffer_size = (off_t)sz; m->offset = 0; return m;
}

int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: xexlzx in out window_size out_size\n"); return 2; }
  const char* in_path = argv[1]; const char* out_path = argv[2];
  unsigned window_size = (unsigned)strtoul(argv[3], 0, 0);
  size_t out_size = (size_t)strtoull(argv[4], 0, 0);

  FILE* fi = fopen(in_path, "rb"); if (!fi) { perror("in"); return 1; }
  fseek(fi, 0, SEEK_END); long in_len = ftell(fi); fseek(fi, 0, SEEK_SET);
  unsigned char* in_buf = (unsigned char*)malloc(in_len);
  fread(in_buf, 1, in_len, fi); fclose(fi);

  unsigned char* dest = (unsigned char*)calloc(out_size, 1);

  unsigned window_bits = 0, ws = window_size; while (ws >>= 1) window_bits++;

  struct mspack_system sys; memset(&sys, 0, sizeof(sys));
  sys.read = mem_read; sys.write = mem_write; sys.alloc = mem_alloc; sys.free = mem_free; sys.copy = mem_copy;
  memfile* src = mem_open(&sys, in_buf, in_len);
  memfile* dst = mem_open(&sys, dest, out_size);
  src->sys = sys; dst->sys = sys;

  struct lzxd_stream* lzx = lzxd_init(&sys, (struct mspack_file*)src, (struct mspack_file*)dst,
                                      (int)window_bits, 0, 0x8000, (off_t)out_size, 0);
  int rc = lzx ? lzxd_decompress(lzx, (off_t)out_size) : -99;
  fprintf(stderr, "lzx window=%u bits=%u in=%ld out=%zu rc=%d wrote=%lld\n",
          window_size, window_bits, in_len, out_size, rc, (long long)dst->offset);

  FILE* fo = fopen(out_path, "wb"); fwrite(dest, 1, dst->offset > 0 ? (size_t)dst->offset : out_size, fo); fclose(fo);
  return (rc == 0 || rc == -1) ? 0 : 0; /* write whatever we got; rc!=0 may still be partial */
}
