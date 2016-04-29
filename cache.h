#ifndef CACHE_H
#define CACHE_H

#include "mem.h"

class Cache {
  public:
    Cache(MemIF* mem) { m_mem = mem; }

    virtual bool flush() { return true; }

  protected:
    MemIF* m_mem;
};

class PulpCache : public Cache {
  public:
    PulpCache(MemIF* mem, unsigned int addr);

    virtual bool flush();

  protected:
    unsigned int m_addr;
};

class GAPCache : public PulpCache {
  public:
    GAPCache(MemIF* mem, unsigned int addr, unsigned int fc_addr);

    virtual bool flush();

  protected:
    unsigned int m_fc_addr;
};

#endif
