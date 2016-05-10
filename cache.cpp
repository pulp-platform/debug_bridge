
#include "cache.h"
#include <stdio.h>

void Cache::flushCores() {
  for (std::list<DbgIF*>::iterator it = p_dbgIfList->begin(); it != p_dbgIfList->end(); it++) {
    (*it)->flush();
  }
}

PulpCache::PulpCache(MemIF* mem, std::list<DbgIF*>* p_dbgIfList, unsigned int addr) :
  Cache(mem, p_dbgIfList) {
  m_addr = addr;
  this->p_dbgIfList = p_dbgIfList;
}

bool
PulpCache::flush() {
  uint32_t data = 0xFFFFFFFF;
  flushCores();
  return m_mem->access(1, m_addr + 0x04, 4, (char*)&data);
}

GAPCache::GAPCache(MemIF* mem, std::list<DbgIF*>* p_dbgIfList, unsigned int addr, unsigned int fc_addr) :
  PulpCache(mem, p_dbgIfList, addr) {
  m_fc_addr = fc_addr;
  p_dbgIfList = p_dbgIfList;
}

bool
GAPCache::flush() {
  uint32_t data = 0xFFFFFFFF;
  bool retval = PulpCache::flush();
  return retval && m_mem->access(1, m_fc_addr + 0x0C, 4, (char*)&data);
}
