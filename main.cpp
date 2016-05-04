
#include "mem_zynq_spi.h"
#include "mem_zynq_apb_spi.h"
#include "sim.h"

#include "debug_if.h"
#include "cache.h"
#include "breakpoints.h"
#include "rsp.h"

enum Platforms { unknown, PULPino, PULP, GAP };

Platforms platform_detect(MemIF* mem) {
  uint32_t info;

  mem->access(0, 0x10000000, 4, (char*)&info);

  if (info == 0xDEADBEEF) {
    printf ("Detected PULPino\n");
    return PULPino;
  } else {
    info = 1 << 16;
    mem->access(1, 0x1B220000 + 0x0, 4, (char*)&info);
    mem->access(0, 0x1B220000 + 0x4000 + 0xF10 * 4, 4, (char*)&info);
    if (info >> 5 == 32) {
      printf ("Detected GAP\n");
      return GAP;
    } else {
      printf ("Detected PULP\n");
      return PULP;
    }
  }
}

bool platform_pulp(MemIF* mem, std::list<DbgIF*>* p_list) {
  uint32_t info;
  unsigned int ncores;

  mem->access(0, 0x1A103010, 4, (char*)&info);

  ncores = info >> 16;

  for(int i = 0; i < ncores; i++) {
    p_list->push_back(new DbgIF(mem, 0x10300000 + i * 0x8000));
  }

  // set all-stop mode, so that all cores go to debug when one enters debug mode
  info = 0xFFFFFFFF;
  return mem->access(1, 0x10200038, 4, (char*)&info);
}

bool platform_gap(MemIF* mem, std::list<DbgIF*>* p_list) {
  platform_pulp(mem, p_list);
  p_list->push_back(new DbgIF(mem, 0x1B220000));

  return true;
}

bool platform_pulpino(MemIF* mem, std::list<DbgIF*>* p_list) {
  p_list->push_back(new DbgIF(mem, 0x1A110000));

  return true;
}

int main() {
  MemIF* mem;
  std::list<DbgIF*> dbgifs;
  Cache* cache;
  Platforms platform = unknown;

  // initialization
#ifdef FPGA
#ifdef PULPEMU
  mem = new ZynqAPBSPIIF();
#else
  mem = new FpgaIF();
#endif
#else
  mem = new SimIF("localhost", 4567);
#endif

  if (platform == unknown) {
    printf ("Unknown platform, trying auto-detect\n");
    platform = platform_detect(mem);
  }

  switch(platform) {
    case GAP:
      cache = new GAPCache(mem, 0x10201400, 0x1B200000);
      platform_gap(mem, &dbgifs);
      break;

    case PULP:
      cache = new PulpCache(mem, 0x10201400);
      platform_pulp(mem, &dbgifs);
      break;

    case PULPino:
      cache = new Cache(mem);
      platform_pulpino(mem, &dbgifs);
      break;

    default:
      printf ("ERROR: Unsupported platform found!\n");
      return 1;
  }


  BreakPoints* bp = new BreakPoints(mem, cache);

  Rsp* rsp = new Rsp(1234, mem, dbgifs, bp);

  // main loop
  while (1) {
    rsp->open();
    while(!rsp->wait_client());
    rsp->loop();
    rsp->close();
  }

  // cleanup
  delete rsp;

  for (std::list<DbgIF*>::iterator it = dbgifs.begin(); it != dbgifs.end(); it++) {
    delete (*it);
  }

  delete bp;
  delete cache;
  delete mem;

  return 0;
}
