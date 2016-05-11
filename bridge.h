#ifndef BRIDGE_H
#define BRIDGE_H

#include "mem_zynq_spi.h"
#include "mem_zynq_apb_spi.h"
#include "sim.h"

#include "debug_if.h"
#include "cache.h"
#include "breakpoints.h"
#include "rsp.h"
#include "log.h"

enum Platforms { unknown, PULPino, PULP, GAP };

class Bridge : public LogIF {
  public:
    void initBridge(Platforms platform, int portNumber, MemIF *memIF, LogIF *log);
    Bridge(Platforms platform, int portNumber, LogIF *log=NULL);
    Bridge(Platforms platform, MemIF *memIF, LogIF *log=NULL);
    ~Bridge();
    void mainLoop();

    void user(char *str, ...);
    void debug(char *str, ...);

  private:
  MemIF* mem;
  std::list<DbgIF*> dbgifs;
  Cache* cache;
  Rsp* rsp;
  BreakPoints* bp;
  LogIF *log;
};

#endif
