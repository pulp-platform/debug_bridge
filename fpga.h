#ifndef SIM_H
#define SIM_H

#include "mem.h"

#include <stdint.h>

class FpgaIF : public MemIF {
  public:
    FpgaIF();

    bool access(bool write, unsigned int addr, int size, char* buffer);

  private:
    bool mem_write(uint32_t addr, uint8_t be, uint32_t wdata);
    bool mem_read(uint32_t addr, uint32_t *rdata);

    int g_spi_fd;
};

#endif
