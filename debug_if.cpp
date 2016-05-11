
#include "debug_if.h"
#include "mem.h"

#include <stdio.h>

DbgIF::DbgIF(MemIF* mem, unsigned int base_addr, LogIF *log) {
  this->m_mem = mem;
  this->m_base_addr = base_addr;
  this->log = log;

  // let's discover core id and cluster id
  this->halt();
  this->csr_read(0xF10, &m_thread_id);
  log->debug("Found a core with id %X\n", m_thread_id);
}

void
DbgIF::flush() {
  // Write back the value of NPC so that it triggers a flush of the prefetch buffer
  uint32_t npc;
  read(DBG_NPC_REG, &npc);
  write(DBG_NPC_REG, npc);
}

bool
DbgIF::write(uint32_t addr, uint32_t wdata) {
  return m_mem->access(1, m_base_addr + addr, 4, (char*)&wdata);
}

bool
DbgIF::read(uint32_t addr, uint32_t* rdata) {
  return m_mem->access(0, m_base_addr + addr, 4, (char*)rdata);
}

bool
DbgIF::halt() {
  uint32_t data;
  if (!this->read(DBG_CTRL_REG, &data)) {
    fprintf(stderr, "debug_is_stopped: Reading from CTRL reg failed\n");
    return false;
  }

  data |= 0x1 << 16;
  return this->write(DBG_CTRL_REG, data);
}

bool
DbgIF::is_stopped() {
  uint32_t data;
  if (!this->read(DBG_CTRL_REG, &data)) {
    fprintf(stderr, "debug_is_stopped: Reading from CTRL reg failed\n");
    return false;
  }

  if (data & 0x10000)
    return true;
  else
    return false;
}

bool
DbgIF::gpr_read_all(uint32_t *data) {
  return m_mem->access(0, m_base_addr + 0x0400, 32 * 4, (char*)data);
}

bool
DbgIF::gpr_read(unsigned int i, uint32_t *data) {
  return this->read(0x0400 + i * 4, data);
}

bool
DbgIF::gpr_write(unsigned int i, uint32_t data) {
  return this->write(0x0400 + i * 4, data);
}

bool
DbgIF::csr_read(unsigned int i, uint32_t *data) {
  return this->read(0x4000 + i * 4, data);
}

bool
DbgIF::csr_write(unsigned int i, uint32_t data) {
  return this->write(0x4000 + i * 4, data);
}

void
DbgIF::get_name(char* str, size_t len) {
  snprintf(str, len, "Core %08X", this->m_thread_id);
}
