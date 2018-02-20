#include "bridge.h"
#include <stdarg.h>

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

bool platform_pulp(MemIF* mem, std::list<DbgIF*>* p_list, LogIF *log) {
  uint32_t info;
  unsigned int ncores;

  mem->access(0, 0x1A103010, 4, (char*)&info);

  ncores = info >> 16;

  for(int i = 0; i < ncores; i++) {
    p_list->push_back(new DbgIF(mem, 0x10300000 + i * 0x8000, log));
  }

  // set all-stop mode, so that all cores go to debug when one enters debug mode
  info = 0xFFFFFFFF;
  return mem->access(1, 0x10200038, 4, (char*)&info);
}

bool platform_gap(MemIF* mem, std::list<DbgIF*>* p_list, LogIF *log) {
  platform_pulp(mem, p_list, log);
  p_list->push_back(new DbgIF(mem, 0x1B220000, log));

  return true;
}

bool platform_pulpino(MemIF* mem, std::list<DbgIF*>* p_list, LogIF *log) {
  p_list->push_back(new DbgIF(mem, 0x1A110000, log));

  return true;
}

Bridge::Bridge(Platforms platform, int portNumber, LogIF *log) {
  initBridge(platform, portNumber, NULL, log);
}

Bridge::Bridge(Platforms platform, MemIF *memIF, LogIF *log) {
  initBridge(platform, -1, memIF, log);
}

void Bridge::initBridge(Platforms platform, int portNumber, MemIF *memIF, LogIF *log) {

  // initialization
  if (log == NULL)
    this->log = this;
  else
    this->log = log;

#ifdef FPGA
#ifdef PULPEMU
  mem = new ZynqAPBSPIIF();
#else
  mem = new FpgaIF();
#endif
#else
  if (portNumber != -1) mem = new SimIF("localhost", portNumber);
  else if (memIF != NULL) mem = memIF;
  else {
    fprintf(stderr, "Either a memory interface or a port number must be provided\n");
    exit (-1);
  }
#endif

  if (platform == unknown) {
    printf ("Unknown platform, trying auto-detect\n");
    platform = platform_detect(mem);
  }

  switch(platform) {
    case GAP:
      platform_gap(mem, &dbgifs, this->log);
      cache = new GAPCache(mem, &dbgifs, 0x10201400, 0x1B200000);
      break;

    case PULP:
      platform_pulp(mem, &dbgifs, this->log);
      cache = new PulpCache(mem, &dbgifs, 0x10201400);
      break;

    case PULPino:
      platform_pulpino(mem, &dbgifs, this->log);
      cache = new Cache(mem, &dbgifs);
      break;

    default:
      printf ("ERROR: Unsupported platform found!\n");
      return;
  }

  bp = new BreakPoints(mem, cache);

  rsp = new Rsp(1234, mem, this->log, dbgifs, bp);
}

void Bridge::mainLoop()
{
  // main loop
  while (1) {
    rsp->open();
    while(!rsp->wait_client());
    rsp->loop();
    rsp->close();
  }
}

Bridge::~Bridge()
{
  // cleanup
  delete rsp;

  for (std::list<DbgIF*>::iterator it = dbgifs.begin(); it != dbgifs.end(); it++) {
    delete (*it);
  }

  delete bp;
  delete cache;
  delete mem;
}

void Bridge::user(const char *str, ...)
{
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}

void Bridge::debug(const char *str, ...)
{
  va_list va;
  va_start(va, str);
  vprintf(str, va);
  va_end(va);
}
