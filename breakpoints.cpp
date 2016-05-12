
#include "breakpoints.h"
#include "mem.h"

#include <stdio.h>
#include <stdlib.h>

#define INSN_IS_COMPRESSED(instr) ((instr & 0x3) != 0x3)
#define INSN_BP_COMPRESSED   0x8002
#define INSN_BP              0x00100073

BreakPoints::BreakPoints(MemIF* mem, Cache* cache) {
  m_mem   = mem;
  m_cache = cache;
}

bool
BreakPoints::insert(unsigned int addr) {
  bool retval;
  uint32_t data_bp;
  struct bp_insn bp;

  bp.addr = addr;
  retval = m_mem->access(0, addr, 4, (char*)&bp.insn_orig);
  bp.is_compressed = INSN_IS_COMPRESSED(bp.insn_orig);

  m_bp_list.push_back(bp);

  if (bp.is_compressed) {
    data_bp = INSN_BP_COMPRESSED;
    retval = retval && m_mem->access(1, addr, 2, (char*)&data_bp);
  } else {
    data_bp = INSN_BP;
    retval = retval && m_mem->access(1, addr, 4, (char*)&data_bp);
  }

  return retval && m_cache->flush();
}

bool
BreakPoints::remove(unsigned int addr) {

  bool retval;
  bool is_compressed;
  uint32_t data;
  for (std::list<struct bp_insn>::iterator it = m_bp_list.begin(); it != m_bp_list.end(); it++) {
    if (it->addr == addr) {
      data = it->insn_orig;
      is_compressed = it->is_compressed;

      m_bp_list.erase(it);

      if (is_compressed)
        retval = m_mem->access(1, addr, 2, (char*)&data);
      else
        retval = m_mem->access(1, addr, 4, (char*)&data);

      return retval && m_cache->flush();
    }
  }

  return false;
}

bool
BreakPoints::clear() {

  bool retval = this->disable_all();

  m_bp_list.clear();

  return retval;
}


bool
BreakPoints::at_addr(unsigned int addr) {
  for (std::list<struct bp_insn>::iterator it = m_bp_list.begin(); it != m_bp_list.end(); it++) {
    if (it->addr == addr) {
      // we found our bp
      return true;
    }
  }

  return false;
}

bool
BreakPoints::enable(unsigned int addr) {
  bool retval;
  uint32_t data;

  for (std::list<struct bp_insn>::iterator it = m_bp_list.begin(); it != m_bp_list.end(); it++) {
    if (it->addr == addr) {
      if (it->is_compressed) {
        data = INSN_BP_COMPRESSED;
        retval = m_mem->access(1, addr, 2, (char*)&data);
      } else {
        data = INSN_BP;
        retval = m_mem->access(1, addr, 4, (char*)&data);
      }

      return retval && m_cache->flush();
    }
  }

  fprintf(stderr, "bp_enable: Did not find any bp at addr %08X\n", addr);

  return false;
}

bool
BreakPoints::disable(unsigned int addr) {
  bool retval;

  for (std::list<struct bp_insn>::iterator it = m_bp_list.begin(); it != m_bp_list.end(); it++) {
    if (it->addr == addr) {
      if (it->is_compressed)
        retval = m_mem->access(1, addr, 2, (char*)&it->insn_orig);
      else
        retval = m_mem->access(1, addr, 4, (char*)&it->insn_orig);

      return retval && m_cache->flush();
    }
  }

  fprintf(stderr, "bp_enable: Did not find any bp at addr %08X\n", addr);

  return false;
}

bool
BreakPoints::enable_all() {
  bool retval = true;

  for (std::list<struct bp_insn>::iterator it = m_bp_list.begin(); it != m_bp_list.end(); it++) {
    retval = retval && this->enable(it->addr);
  }

  return retval;
}

bool
BreakPoints::disable_all() {
  bool retval = true;

  for (std::list<struct bp_insn>::iterator it = m_bp_list.begin(); it != m_bp_list.end(); it++) {
    retval = retval && this->disable(it->addr);
  }

  return retval;
}
