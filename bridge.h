#ifndef BRIDGE_H
#define BRIDGE_H

#ifndef __SWIG__
#include "mem_zynq_spi.h"
#include "mem_zynq_apb_spi.h"
#include "sim.h"

#include "debug_if.h"
#include "cache.h"
#include "breakpoints.h"
#include "rsp.h"
#endif

enum Platforms { unknown, PULPino, PULP, GAP };

class Bridge {
  public:
    void initBridge(Platforms platform, int portNumber, MemIF *memIF);
    Bridge(Platforms platform, int portNumber);
    Bridge(Platforms platform, MemIF *memIF);
    ~Bridge();
    void mainLoop();

  private:
  MemIF* mem;
  std::list<DbgIF*> dbgifs;
  Cache* cache;
  Rsp* rsp;
  BreakPoints* bp;
};

#endif
