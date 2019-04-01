#ifndef MEM_H
#define MEM_H

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

class MemIF {
  public:
    virtual ~MemIF(){};
    virtual bool access(bool write, unsigned int addr, int size, char* buffer) = 0;
    static int mmap_gen(uint32_t mem_address, uint32_t mem_size, volatile uint32_t **return_ptr);
};

#endif
