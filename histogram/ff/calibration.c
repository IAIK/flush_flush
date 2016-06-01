#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include "../cacheutils.h"

size_t array[5*1024];

size_t hit_histogram[600];
size_t miss_histogram[600];

size_t onlyreload(size_t* addr)
{
  size_t time = rdtsc();
  flush(addr);
  size_t delta = rdtsc() - time;
  maccess(addr);
  maccess(addr);
  return delta;
}

size_t flushandreload(size_t* addr)
{
  size_t time = rdtsc();
  flush(addr);
  size_t delta = rdtsc() - time;
  flush(addr);
  return delta;
}

int main(int argc, char** argv)
{
  memset(array,-1,5*1024*sizeof(size_t));
  maccess(array + 2*1024);
  sched_yield();
  for (int i = 0; i < 1*1024*1024; ++i)
  {
    size_t d = onlyreload(array+2*1024);
    hit_histogram[MIN(599,d)]++;
    for (size_t i = 0; i < 30; ++i)
      sched_yield();
  }
  flush(array+1024);
  for (int i = 0; i < 1*1024*1024; ++i)
  {
    size_t d = flushandreload(array+2*1024);
    miss_histogram[MIN(599,d)]++;
    for (size_t i = 0; i < 30; ++i)
      sched_yield();
  }
  printf(".\n");
  size_t hit_max = 0;
  size_t hit_max_i = 0;
  size_t miss_min_i = 0;
  for (size_t i = 0; i < 600; ++i)
  {
    printf("%3zu: %10zu %10zu\n",i,hit_histogram[i],miss_histogram[i]);
  }
  return 0;
}
