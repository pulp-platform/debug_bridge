#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

bool sim_mem_open();
bool sim_mem_access(bool write, unsigned int addr, int size, char* buffer);
