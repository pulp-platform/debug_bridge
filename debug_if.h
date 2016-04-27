
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

// TODO: this should be a parameter in a struct or so
#define DEBUG_BASE_ADDR 0x1A110000

#define DBG_CTRL_REG  0x0
#define DBG_HIT_REG   0x4
#define DBG_IE_REG    0x8
#define DBG_CAUSE_REG 0xC
#define DBG_NPC_REG   0x1200
#define DBG_PPC_REG   0x1204

#define DBG_CAUSE_BP  0x3

bool debug_write(uint32_t addr, uint32_t wdata);
bool debug_read(uint32_t addr, uint32_t* rdata);
bool debug_halt();
bool debug_resume(bool step);
bool debug_gpr_read_all(uint32_t *data);
bool debug_gpr_read(int i, uint32_t *data);
bool debug_gpr_write(int i, uint32_t data);
bool debug_csr_read(int i, uint32_t *data);
bool debug_csr_write(int i, uint32_t data);
bool debug_is_stopped();

