#include <stdio.h>
#include <sys/mman.h>
#include <sys/file.h>
#include "mem.h"

int
MemIF::mmap_gen(
  uint32_t mem_address,
  uint32_t mem_size,
  volatile uint32_t **return_ptr
) {
  int mem_dev = ::open("/dev/mem", O_RDWR | O_SYNC);
  if(mem_dev == -1) {
    perror("mmap_gen: Opening /dev/mem failed");
    return -1;
  }

  long ret = sysconf(_SC_PAGESIZE);
  if (ret == -1) {
    perror("mmap_gen: sysconf failed to get page size");
    return -1;
  }
  uint32_t page_size = (uint32_t)ret;

  uint32_t alloc_mem_size = (((mem_size / page_size) + 1) * page_size);
  uint32_t page_mask = (page_size - 1);

  volatile char *mem_ptr = (char*)::mmap(NULL,
                 alloc_mem_size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 mem_dev,
                 (mem_address & ~page_mask));

  close(mem_dev);

  if (mem_ptr == MAP_FAILED) {
    printf("mmap_gen: map failed\n");
    return -1;
  }

  *return_ptr = (volatile uint32_t *)(mem_ptr + (mem_address & page_mask));
  return 0;
}
