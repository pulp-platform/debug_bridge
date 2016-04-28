
#ifdef FPGA
#include "fpga.h"
#else
#include "sim.h"
#endif

#include "debug_if.h"
#include "breakpoints.h"
#include "rsp.h"

int main() {
  MemIF* mem;
  std::list<DbgIF*> dbgifs;
  DbgIF* dbgif;

  // initialization
#ifdef FPGA
  mem = new FpgaIF();
#else
  mem = new SimIF("localhost", 4567);
#endif

  BreakPoints* bp = new BreakPoints(mem);

  dbgif = new DbgIF(mem, 0x1A110000);
  dbgifs.push_back(dbgif);

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
  delete mem;

  return 0;
}
