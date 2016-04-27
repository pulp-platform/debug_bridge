#include <stdbool.h>

bool bp_insert(unsigned int addr);
bool bp_remove(unsigned int addr);

bool bp_clear(unsigned int addr);

bool bp_at_addr(unsigned int addr);

bool bp_enable_all();
bool bp_disable_all();

bool bp_disable(unsigned int addr);
bool bp_enable(unsigned int addr);
