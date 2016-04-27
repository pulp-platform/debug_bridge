#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

bool sim_mem_open(int port);
bool sim_mem_access(bool write, unsigned int addr, int size, char* buffer);
