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

bool sim_mem_access(bool write, unsigned int addr, int size, char* buffer) {
  int ret;
  char data[2048];
  // packet header looks like this:
  // Write (in LSB)
  // ADDR[31:0]
  // SIZE[31:0]
  //
  // after that follows the data in the buffer (for a write), starting at the lowest byte
  // for a read, the packet is finished with SIZE

  memset(data, 0, sizeof(data));

  data[0] = write ? 1 : 0;
  *((int*)&data[1]) = addr;
  *((int*)&data[5]) = size;

  ret = send(mem_socket, data, 9, 0);
  if (ret != 9) {
    fprintf(stderr, "Unable to send header to simulator: %s\n", strerror(errno));
    return false;
  }

  if (write) {
    // write
    ret = send(mem_socket, buffer, size, 0);
    if (ret != size) {
      fprintf(stderr, "Unable to send buffer to simulator: %s\n", strerror(errno));
      return false;
    }

    // check response
    ret = recv(mem_socket, data, 5, 0);
    if (ret == -1 || ret == 0) {
      fprintf(stderr, "Unable to get a response from simulator: %s\n", strerror(errno));
      return false;
    }

    if (ret != 5) {
      fprintf(stderr, "Unable to get all write response, only get %d from %d\n", ret, 5);
      return false;
    }

    uint8_t ok    = *((uint8_t*)&data[0]);
    uint32_t size = *((uint32_t*)&data[1]);

    if (ok == -1) {
      fprintf(stderr, "Write failed on simulator\n");
      return false;
    }
  } else {
    // read
    ret = recv(mem_socket, data, 5, 0);
    if (ret == -1 || ret == 0) {
      fprintf(stderr, "Unable to get a response from simulator: %s\n", strerror(errno));
      return false;
    }

    uint8_t ok    = *((uint8_t*)&data[0]);
    uint32_t size = *((uint32_t*)&data[1]);

    if (ok == -1) {
      fprintf(stderr, "Read failed on simulator\n");
      return false;
    }

    ret = recv(mem_socket, buffer, size, 0);
    if (ret == -1 || ret == 0) {
      fprintf(stderr, "Unable to get a response from simulator: %s\n", strerror(errno));
      return false;
    }

    if (ret != size) {
      fprintf(stderr, "Unable to get all data only get %d from %d\n", ret, size);
      return false;
    }
  }

  return true;
}
