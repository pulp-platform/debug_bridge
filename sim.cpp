#include "mem.h"

#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <list>

int mem_socket;
int mem_socket_port = 4567;
const char* mem_server = "localhost";

bool sim_mem_open() {
  struct sockaddr_in addr;
  struct hostent *he;

  if((mem_socket = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf(stderr, "Unable to create socket (%s)\n", strerror(errno));
    return false;
  }

  if((he = gethostbyname(mem_server)) == NULL) {
    perror("gethostbyname");
    return false;
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(mem_socket_port);
  addr.sin_addr = *((struct in_addr *)he->h_addr_list[0]);
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

  if(connect(mem_socket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    fprintf(stderr, "Unable to connect to %s port %d (%s)\n", mem_server, mem_socket_port,
            strerror(errno));
    return false;
  }

  printf("Mem connected!");

  return true;
}

bool sim_mem_write(uint32_t addr, uint8_t be, uint32_t wdata) {
  int ret;
  char data[9];

  data[0] = (1 << 7) | (be & 0xF);
  *((int*)&data[1]) = htonl(addr);
  *((int*)&data[5]) = htonl(wdata);

  ret = send(mem_socket, data, 9, 0);
  if (ret != 9) {
    fprintf(stderr, "Unable to send to simulator: %s\n", strerror(errno));
    return false;
  }

  // now wait for response
  ret = recv(mem_socket, data, 4, 0);
  if (ret != 4) {
    fprintf(stderr, "Unable to ack for write to simulator: %s\n", strerror(errno));
    return false;
  }

  return true;
}

bool sim_mem_read(uint32_t addr, uint32_t *rdata) {
  int ret;
  char data[9];
  uint32_t rdata_int;

  data[0] = 0xF;
  *((int*)&data[1]) = htonl(addr);
  *((int*)&data[5]) = 0;

  ret = send(mem_socket, data, 9, 0);
  // check for connection abort
  if((ret == -1) || (ret == 0)) {
    fprintf(stderr, "Mem: no data received: %s\n", strerror(errno));
    // try to reopen connection
    return sim_mem_open() && sim_mem_read(addr, rdata);
  }

  if (ret != 9) {
    fprintf(stderr, "Unable to send to simulator: %s\n", strerror(errno));
    return false;
  }

  // now wait for response
  ret = recv(mem_socket, &rdata_int, 4, 0);
  // check for connection abort
  if((ret == -1) || (ret == 0)) {
    fprintf(stderr, "Mem: no data received: %s\n", strerror(errno));
    // try to reopen connection
    return sim_mem_open() && sim_mem_read(addr, rdata);
  }

  if (ret != 4) {
    fprintf(stderr, "Unable to get ack for read to simulator: %s\n", strerror(errno));
    return false;
  }
  *rdata = ntohl(rdata_int);

  return true;
}

bool sim_mem_write_w(uint32_t addr, uint32_t wdata) {
  return sim_mem_write(addr, 0xF, wdata);
}

bool sim_mem_write_h(uint32_t addr, uint32_t wdata) {
  if (addr & 0x2)
    return sim_mem_write(addr, 0xC, wdata << 16);
  else
    return sim_mem_write(addr, 0x3, wdata & 0xFFFF);
}

bool sim_mem_read_w(uint32_t addr, uint32_t* rdata) {
  return sim_mem_read(addr, rdata);
}

