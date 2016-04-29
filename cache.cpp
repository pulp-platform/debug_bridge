
#include "cache.h"

PulpCache::PulpCache(MemIF* mem, unsigned int addr) :
  Cache(mem) {
  m_addr = addr;
}

bool
PulpCache::flush() {
  uint32_t data = 0xFFFFFFFF;
  return m_mem->access(1, m_addr + 0x04, 4, (char*)&data);
}

GAPCache::GAPCache(MemIF* mem, unsigned int addr, unsigned int fc_addr) :
  PulpCache(mem, addr) {
  m_fc_addr = fc_addr;
}

bool
GAPCache::flush() {
  uint32_t data = 0xFFFFFFFF;
  bool retval = PulpCache::flush();
  return retval && m_mem->access(1, m_fc_addr + 0x0C, 4, (char*)&data);
}
