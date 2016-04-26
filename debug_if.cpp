
#include "debug_if.h"
#include "mem.h"

bool debug_write(uint32_t addr, uint32_t wdata) {
  return sim_mem_access(1, DEBUG_BASE_ADDR + addr, 4, (char*)&wdata);
}

bool debug_read(uint32_t addr, uint32_t* rdata) {
  return sim_mem_access(0, DEBUG_BASE_ADDR + addr, 4, (char*)rdata);
}

bool debug_halt() {
  uint32_t data;
  if (!debug_read(DBG_CTRL_REG, &data))
    return false;

  data |= 0x1 << 16;
  return debug_write(DBG_CTRL_REG, data);
}

bool debug_is_stopped() {
  uint32_t data;
  if (!debug_read(DBG_CTRL_REG, &data))
    return false;

  if (data & 0x10000)
    return true;
  else
    return false;
}

bool debug_gpr_read(int i, uint32_t *data) {
  return debug_read(0x1000 + i * 4, data);
}

bool debug_gpr_write(int i, uint32_t data) {
  return debug_write(0x1000 + i * 4, data);
}

bool debug_csr_read(int i, uint32_t *data) {
  return debug_read(0x4000 + i * 4, data);
}

bool debug_csr_write(int i, uint32_t data) {
  return debug_write(0x4000 + i * 4, data);
}
