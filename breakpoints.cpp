
#include "breakpoints.h"
#include "mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <list>


struct bp_insn {
  uint32_t addr;
  uint32_t insn_orig;
  bool is_compressed;
};

std::list<struct bp_insn> g_bp_list;

#define INSN_IS_COMPRESSED(instr) ((instr & 0x3) != 0x3)
#define INSN_BP_COMPRESSED   0x8002
#define INSN_BP              0x00100073


bool bp_insert(unsigned int addr) {
  uint32_t data_bp;
  struct bp_insn bp;

  bp.addr = addr;
  sim_mem_access(0, addr, 4, (char*)&bp.insn_orig);
  bp.is_compressed = INSN_IS_COMPRESSED(bp.insn_orig);

  g_bp_list.push_back(bp);

  if (bp.is_compressed) {
    data_bp = INSN_BP_COMPRESSED;
    return sim_mem_access(1, addr, 2, (char*)&data_bp);
  } else {
    data_bp = INSN_BP;
    return sim_mem_access(1, addr, 4, (char*)&data_bp);
  }
}

bool bp_remove(unsigned int addr) {
  bool is_compressed;
  uint32_t data;
  for (std::list<struct bp_insn>::iterator it = g_bp_list.begin(); it != g_bp_list.end(); it++) {
    if (it->addr == addr) {
      data = it->insn_orig;
      is_compressed = it->is_compressed;

      g_bp_list.erase(it);

      if (is_compressed)
        return sim_mem_access(1, addr, 2, (char*)&data);
      else
        return sim_mem_access(1, addr, 4, (char*)&data);
    }
  }

  return false;
}

bool bp_clear() {
  bool retval = bp_disable_all();

  g_bp_list.clear();

  return retval;
}


bool bp_at_addr(unsigned int addr) {
  for (std::list<struct bp_insn>::iterator it = g_bp_list.begin(); it != g_bp_list.end(); it++) {
    if (it->addr == addr) {
      // we found our bp
      return true;
    }
  }

  return false;
}

bool bp_enable(unsigned int addr) {
  uint32_t data;

  for (std::list<struct bp_insn>::iterator it = g_bp_list.begin(); it != g_bp_list.end(); it++) {
    if (it->addr == addr) {
      if (it->is_compressed) {
        data = INSN_BP_COMPRESSED;
        return sim_mem_access(1, addr, 2, (char*)&data);
      } else {
        data = INSN_BP;
        return sim_mem_access(1, addr, 4, (char*)&data);
      }
    }
  }

  fprintf(stderr, "bp_enable: Did not find any bp at addr %08X\n", addr);

  return false;
}

bool bp_disable(unsigned int addr) {
  uint32_t data;

  for (std::list<struct bp_insn>::iterator it = g_bp_list.begin(); it != g_bp_list.end(); it++) {
    if (it->addr == addr) {
      if (it->is_compressed)
        return sim_mem_access(1, addr, 2, (char*)&it->insn_orig);
      else
        return sim_mem_access(1, addr, 4, (char*)&it->insn_orig);
    }
  }

  fprintf(stderr, "bp_enable: Did not find any bp at addr %08X\n", addr);

  return false;
}

bool bp_enable_all() {
  bool retval = true;

  for (std::list<struct bp_insn>::iterator it = g_bp_list.begin(); it != g_bp_list.end(); it++) {
    retval = retval && bp_enable(it->addr);
  }

  return retval;
}

bool bp_disable_all() {
  bool retval = true;

  for (std::list<struct bp_insn>::iterator it = g_bp_list.begin(); it != g_bp_list.end(); it++) {
    retval = retval && bp_disable(it->addr);
  }

  return retval;
}
