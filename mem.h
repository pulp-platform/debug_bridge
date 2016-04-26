#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

bool sim_mem_open();
bool sim_mem_read(uint32_t addr, uint32_t *rdata);
bool sim_mem_write(uint32_t addr, uint8_t be, uint32_t wdata);

bool sim_mem_write_h(uint32_t addr, uint32_t wdata);
bool sim_mem_write_w(uint32_t addr, uint32_t wdata);
bool sim_mem_read_w(uint32_t addr, uint32_t* rdata);
