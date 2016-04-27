
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

#include "mem.h"
#include "debug_if.h"
#include "breakpoints.h"

int socket_in;
int socket_client;

#define PACKET_MAX_LEN 4096

struct packet {
  char raw[PACKET_MAX_LEN];
  size_t raw_len;
};

enum mp_type {
  BP_MEMORY   = 0,
  BP_HARDWARE = 1,
  WP_WRITE    = 2,
  WP_READ     = 3,
  WP_ACCESS   = 4
};

enum target_signal {
  TARGET_SIGNAL_NONE =  0,
  TARGET_SIGNAL_INT  =  2,
  TARGET_SIGNAL_ILL  =  4,
  TARGET_SIGNAL_TRAP =  5,
  TARGET_SIGNAL_FPE  =  8,
  TARGET_SIGNAL_BUS  = 10,
  TARGET_SIGNAL_SEGV = 11,
  TARGET_SIGNAL_ALRM = 14,
  TARGET_SIGNAL_STOP = 17,
  TARGET_SIGNAL_USR2 = 31,
  TARGET_SIGNAL_PWR  = 32
};


bool rsp_notify(char* data, size_t len);
bool rsp_signal();


bool rsp_open(int socket_port) {
  struct sockaddr_in addr;
  int ret;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(socket_port);
  addr.sin_addr.s_addr = INADDR_ANY;
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

  socket_in = socket(PF_INET, SOCK_STREAM, 0);
  if(socket_in < 0)
  {
    fprintf(stderr, "Unable to create comm socket: %s\n", strerror(errno));
    return false;
  }

  int yes = 1;
  if(setsockopt(socket_in, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    fprintf(stderr, "Unable to setsockopt on the socket: %s\n", strerror(errno));
    return false;
  }

  if(bind(socket_in, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    fprintf(stderr, "Unable to bind the socket: %s\n", strerror(errno));
    return false;
  }

  if(listen(socket_in, 1) == -1) {
    fprintf(stderr, "Unable to listen: %s\n", strerror(errno));
    return false;
  }

  fprintf(stderr, "Listening on port %d\n", socket_port);

  // now clear resources
  bp_clear();
  debug_halt();

  return true;
}

void rsp_close() {
  close(socket_in);
}

bool rsp_wait_client() {
  int ret;
  if((socket_client = accept(socket_in, NULL, NULL)) == -1) {
    if(errno == EAGAIN)
      return false;

    fprintf(stderr, "Unable to accept connection: %s\n", strerror(errno));
    return false;
  }

  printf("RSP: Client connected!\n");
  return true;
}

bool rsp_get_packet(struct packet* packet) {
  char c;
  char check_chars[2];
  char buffer[PACKET_MAX_LEN];
  int  buffer_len = 0;
  bool escaped = false;
  int ret;
  // packets follow the format: $packet-data#checksum
  // checksum is two-digit

  // poison packet
  memset(packet->raw, 0, PACKET_MAX_LEN);
  packet->raw_len = 0;

  // first look for start bit
  do {
    ret = recv(socket_client, &c, 1, 0);

    if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
      fprintf(stderr, "RSP: Error receiving\n");
      return false;
    }

    if(ret == -1 && errno == EWOULDBLOCK) {
      // no data available
      continue;
    }

    // special case for 0x03 (asynchronous break)
    if (c == 0x03) {
      packet->raw[0]  = c;
      packet->raw_len = 1;
      return true;
    }
  } while(c != '$');

  buffer[0] = c;

  // now store data as long as we don't see #
  do {
    if (buffer_len >= PACKET_MAX_LEN) {
      fprintf(stderr, "RSP: Too many characters received\n");
      return false;
    }

    ret = recv(socket_client, &c, 1, 0);

    if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
      fprintf(stderr, "RSP: Error receiving\n");
      return false;
    }

    if(ret == -1 && errno == EWOULDBLOCK) {
      // no data available
      continue;
    }

    buffer[buffer_len++] = c;

    // check for 0x7d = '}'
    if (c == 0x7d) {
      escaped = true;
      continue;
    }

    if (escaped)
      packet->raw[packet->raw_len++] = c ^ 0x20;
    else
      packet->raw[packet->raw_len++] = c;

    escaped = false;
  } while(c != '#');

  buffer_len--;
  packet->raw_len--;

  // checksum, 2 bytes
  ret = recv(socket_client, &check_chars[0], 1, 0);
  if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
    fprintf(stderr, "RSP: Error receiving\n");
    return false;
  }

  ret = recv(socket_client, &check_chars[1], 1, 0);
  if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
    fprintf(stderr, "RSP: Error receiving\n");
    return false;
  }

  // check the checksum
  unsigned int checksum = 0;
  for(int i = 0; i < buffer_len; i++) {
    checksum += buffer[i];
  }

  checksum = checksum % 256;
  char checksum_str[3];
  snprintf(checksum_str, 3, "%02x", checksum);

  if (check_chars[0] != checksum_str[0] || check_chars[1] != checksum_str[1]) {
    fprintf(stderr, "RSP: Checksum failed; received %.*s; checksum should be %02x\n", packet->raw_len, packet->raw, checksum);
    return false;
  }

  // now send ACK
  char ack = '+';
  if (send(socket_client, &ack, 1, 0) != 1) {
    fprintf(stderr, "RSP: Sending ACK failed\n");
    return false;
  }

  // NULL terminate the string
  packet->raw[packet->raw_len] = '\0';

  return true;
}

bool rsp_send(const char* data, size_t len) {
  int ret;
  size_t raw_len = len + 4;
  char* raw = (char*)malloc(raw_len);
  unsigned int checksum = 0;

  raw[0] = '$';
  raw[raw_len - 3] = '#';

  if (len > 0) {
    memcpy(raw + 1, data, len);
    for(int i = 0; i < len; i++) {
      checksum += data[i];
    }
  }

  // add checksum
  checksum = checksum % 256;
  char checksum_str[3];
  snprintf(checksum_str, 3, "%02x", checksum);

  raw[raw_len - 2] = checksum_str[0];
  raw[raw_len - 1] = checksum_str[1];

  char ack;
  do {
    printf("Sending %.*s\n", raw_len, raw);

    if (send(socket_client, raw, raw_len, 0) != raw_len) {
      free(raw);
      fprintf(stderr, "Unable to send data to client\n");
      return false;
    }

    ret = recv(socket_client, &ack, 1, 0);
    if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
      free(raw);
      fprintf(stderr, "RSP: Error receiving\n");
      return false;
    }

    if(ret == -1 && errno == EWOULDBLOCK) {
      // no data available
      continue;
    }
  } while (ack != '+');

  free(raw);
  return true;
}

bool rsp_send_str(const char* data) {
  return rsp_send(data, strlen(data));
}

bool rsp_query(char* data, size_t len) {
  if (strncmp ("qSupported", data, strlen ("qSupported")) == 0)
  {
    const char* support_string = "PacketSize=256";

    rsp_send(support_string, strlen(support_string));
    return true;
  }
  else if (strncmp ("qTStatus", data, strlen ("qTStatus")) == 0)
  {
    // not supported, send empty packet
    rsp_send(NULL, 0);
    return true;
  }
  else if (strncmp ("qT", data, strlen ("qT")) == 0)
  {
    // not supported, send empty packet
    rsp_send(NULL, 0);
    return true;
  }
  else if (strncmp ("qfThreadInfo", data, strlen ("qfThreadInfo")) == 0)
  {
    const char* threadinfo_str = "m0";

    rsp_send(threadinfo_str, strlen(threadinfo_str));
    return true;
  }
  else if (strncmp ("qsThreadInfo", data, strlen ("qsThreadInfo")) == 0)
  {
    const char* sthreadinfo_str = "l";

    rsp_send(sthreadinfo_str, strlen(sthreadinfo_str));
    return true;
  }
  else if (strncmp ("qAttached", data, strlen ("qAttached")) == 0)
  {
    rsp_send_str("1");
    return true;
  }
  else if (strncmp ("qC", data, strlen ("qC")) == 0)
  {
    rsp_send_str("");
    return true;
  }
  else if (strncmp ("qSymbol", data, strlen ("qSymbol")) == 0)
  {
    rsp_send_str("OK");
    return true;
  }
  else if (strncmp ("qOffsets", data, strlen ("qOffsets")) == 0)
  {
    const char* offsets_str = "Text=0;Data=0;Bss=0";

    rsp_send(offsets_str, strlen(offsets_str));
    return true;
  }

  fprintf(stderr, "Unknown query packet\n");

  return false;
}

uint32_t rsp_pc_read() {
  uint32_t npc;
  uint32_t ppc;
  uint32_t cause;
  uint32_t hit;

  debug_read(DBG_PPC_REG, &ppc);
  debug_read(DBG_NPC_REG, &npc);

  debug_read(DBG_HIT_REG, &hit);
  debug_read(DBG_CAUSE_REG, &cause);

  if (hit & 0x1)
    return npc;
  else if(cause & (1 << 31)) // interrupt
    return npc;
  else if(cause == 3)  // breakpoint
    return ppc;
  else if(cause == 2)
    return ppc;
  else if(cause == 5)
    return ppc;
  else
    return npc;
}

bool rsp_send_registers() {
  uint32_t gpr[32];
  uint32_t npc;
  uint32_t ppc;
  char regs_str[512];
  int i;

  debug_gpr_read_all(gpr);

  // now build the string to send back
  for(i = 0; i < 32; i++) {
    snprintf(&regs_str[i * 8], 9, "%08x", htonl(gpr[i]));
  }

  npc = rsp_pc_read();
  snprintf(&regs_str[32 * 8 + 0 * 8], 9, "%08x", htonl(npc));

  return rsp_send_str(regs_str);
}

bool rsp_reg_read(char* data, size_t len) {
  uint32_t addr;
  uint32_t rdata;
  char data_str[10];

  if (sscanf(data, "%x", &addr) != 1) {
    fprintf(stderr, "Could not parse packet\n");
    return false;
  }

  if (addr < 32)
    debug_gpr_read(addr, &rdata);
  else if (addr == 0x20)
    rdata = rsp_pc_read();
  else
    return rsp_send_str("");

  rdata = htonl(rdata);
  snprintf(data_str, 9, "%08x", rdata);

  return rsp_send_str(data_str);
}

bool rsp_reg_write(char* data, size_t len) {
  uint32_t addr;
  uint32_t wdata;
  char data_str[10];

  if (sscanf(data, "%x=%08x", &addr, &wdata) != 2) {
    fprintf(stderr, "Could not parse packet\n");
    return false;
  }

  wdata = ntohl(wdata);

  if (addr < 32)
    debug_gpr_write(addr, wdata);
  else if (addr == 32)
    debug_write(DBG_NPC_REG, wdata);
  else
    return rsp_send_str("E01");

  return rsp_send_str("OK");
}

bool rsp_mem_read(char* data, size_t len) {
  char buffer[512];
  char reply[512];
  uint32_t addr;
  uint32_t length;
  uint32_t rdata;
  int i;

  if (sscanf(data, "%x,%x", &addr, &length) != 2) {
    fprintf(stderr, "Could not parse packet\n");
    return false;
  }

  sim_mem_access(0, addr, length, buffer);

  for(i = 0; i < length; i++) {
    rdata = buffer[i];
    snprintf(&reply[i * 2], 3, "%02x", rdata);
  }

  return rsp_send(reply, length*2);
}

bool rsp_mem_write_ascii(char* data, size_t len) {
  uint32_t addr;
  size_t length;
  uint32_t wdata;
  int i, j;

  char* buffer;
  int buffer_len;

  if (sscanf(data, "%x,%d:", &addr, &length) != 2) {
    fprintf(stderr, "Could not parse packet\n");
    return false;
  }

  for(i = 0; i < len; i++) {
    if (data[i] == ':') {
      break;
    }
  }

  if (i == len)
    return false;

  // align to hex data
  data = &data[i+1];
  len = len - i - 1;

  buffer_len = len/2;
  buffer = (char*)malloc(buffer_len);
  if (buffer == NULL) {
    fprintf(stderr, "Failed to allocate buffer\n");
    return false;
  }

  for(j = 0; j < len/2; j++) {
    wdata = 0;
    for(i = 0; i < 2; i++) {
      char c = data[j * 2 + i];
      uint32_t hex = 0;
      if (c >= '0' && c <= '9')
        hex = c - '0';
      else if (c >= 'a' && c <= 'f')
        hex = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        hex = c - 'A' + 10;

      wdata |= hex << (4 * i);
    }

    buffer[j] = wdata;
  }

  sim_mem_access(1, addr, buffer_len, buffer);

  free(buffer);

  return rsp_send_str("OK");
}

bool rsp_mem_write(char* data, size_t len) {
  uint32_t addr;
  size_t length;
  uint32_t wdata;
  int i, j;

  char* buffer;
  int buffer_len;

  if (sscanf(data, "%x,%x:", &addr, &length) != 2) {
    fprintf(stderr, "Could not parse packet\n");
    return false;
  }

  for(i = 0; i < len; i++) {
    if (data[i] == ':') {
      break;
    }
  }

  if (i == len)
    return false;

  // align to hex data
  data = &data[i+1];
  len = len - i - 1;

  sim_mem_access(1, addr, len, data);

  return rsp_send_str("OK");
}

bool rsp_v(char* data, size_t len) {
  if (strncmp ("vKill", data, strlen ("vKill")) == 0)
  {
    rsp_send_str("OK");
    return true;
  }
  if (strncmp ("vCont?", data, strlen ("vCont?")) == 0)
  {
    rsp_send_str("");
    return true;
  }

  return false;
}

int gdb_signal(char* str, size_t len) {
  uint32_t cause;
  uint32_t hit;
  int signal;

  debug_write(DBG_IE_REG, 0xFFFF);

  // figure out why we are stopped
  if (debug_is_stopped()) {
    if (!debug_read(DBG_HIT_REG, &hit))
      return false;
    if (!debug_read(DBG_CAUSE_REG, &cause))
      return false;

    if (hit & 0x1)
      signal = TARGET_SIGNAL_TRAP;
    else if(cause & (1 << 31))
      signal = TARGET_SIGNAL_INT;
    else if(cause & (1 << 3))
      signal = TARGET_SIGNAL_TRAP;
    else if(cause & (1 << 2))
      signal = TARGET_SIGNAL_ILL;
    else if(cause & (1 << 5))
      signal = TARGET_SIGNAL_BUS;
    else
      signal = TARGET_SIGNAL_STOP;
  } else {
    signal = TARGET_SIGNAL_NONE;
  }

  return snprintf(str, 4, "S%02x", signal);
}

bool rsp_resume(bool step) {
  int ret;
  char pkt;
  uint32_t hit;
  uint32_t cause;
  uint32_t ppc;
  uint32_t npc;
  uint32_t data;


  // now let's handle software breakpoints
  debug_read(DBG_PPC_REG, &ppc);
  debug_read(DBG_NPC_REG, &npc);
  debug_read(DBG_HIT_REG, &hit);
  debug_read(DBG_CAUSE_REG, &cause);

  // if there is a breakpoint at this address, let's remove it and single-step over it
  printf("In resume, PPC is %08X, NPC is %08X, HIT is %08X, CAUSE %08X\n", ppc, npc, hit, cause);
  printf("There is a BP at PPC here: %d\n", bp_at_addr(ppc));
  printf("There is a BP at NPC here: %d\n", bp_at_addr(npc));
  if (bp_at_addr(ppc)) {
    bp_disable(ppc);
    debug_write(DBG_NPC_REG, ppc); // re-execute this instruction
    debug_write(DBG_CTRL_REG, 0x1); // single-step
    bp_enable(ppc);
  }

  // clear hit register, has to be done before CTRL
  debug_write(DBG_HIT_REG, 0);

  if (step)
    debug_write(DBG_CTRL_REG, 0x1);
  else
    debug_write(DBG_CTRL_REG, 0);

  fd_set rfds;
  struct timeval tv;


  while(1) {
    FD_ZERO(&rfds);
    FD_SET(socket_client, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 100 * 1000;

    if (select(socket_client+1, &rfds, NULL, NULL, &tv) == 0) {
      if (debug_is_stopped()) {
        return rsp_signal();
      }
    } else {
      ret = recv(socket_client, &pkt, 1, 0);
      if (ret == 1 && pkt == 0x3) {
        printf("Stopping core\n");
        debug_halt();
      }
    }
  }

  return true;
}

bool rsp_signal() {
  char str[4];
  size_t len;

  len = gdb_signal(str, 4);

  rsp_send(str, len);
  return true;
}

bool rsp_continue(char* data, size_t len) {
  uint32_t sig;
  uint32_t addr;
  uint32_t npc;
  int i;
  bool npc_found = false;

  // strip signal first
  if (data[0] == 'C') {
    if (sscanf(data, "C%X;%X", &sig, &addr) == 2)
      npc_found = true;
  } else {
    if (sscanf(data, "c%X", &addr) == 1)
      npc_found = true;
  }

  if (npc_found) {
    // only when we have received an address
    debug_read(DBG_NPC_REG, &npc);

    printf ("New NPC is %08X\n", addr);
    if (npc != addr)
      debug_write(DBG_NPC_REG, addr);
  }

  return rsp_resume(false);
}

bool rsp_step(char* data, size_t len) {
  uint32_t addr;
  uint32_t npc;
  int i;

  // strip signal first
  if (data[0] == 'S') {
    for (i = 0; i < len; i++) {
      if (data[i] == ';') {
        data = &data[i+1];
        break;
      }
    }
  }

  if (sscanf(data, "%x", &addr) == 1) {
    // only when we have received an address
    debug_read(DBG_NPC_REG, &npc);

    if (npc != addr)
      debug_write(DBG_NPC_REG, addr);
  }

  return rsp_resume(true);
}

bool rsp_notify_signal() {
  char str[20];
  char sigstr[4];
  size_t len;

  len = gdb_signal(sigstr, 4);

  len = snprintf(str, 20, "Stop:%s", sigstr);

  return rsp_notify(str, len);
}

bool rsp_notify(char* data, size_t len) {
  int ret;
  size_t raw_len = len + 4;
  char* raw = (char*)malloc(raw_len);
  unsigned int checksum = 0;

  raw[0] = '%';
  raw[raw_len - 3] = '#';

  if (len > 0) {
    memcpy(raw + 1, data, len);
    for(int i = 0; i < len; i++) {
      checksum += data[i];
    }
  }

  // add checksum
  checksum = checksum % 256;
  char checksum_str[3];
  snprintf(checksum_str, 3, "%02x", checksum);

  raw[raw_len - 2] = checksum_str[0];
  raw[raw_len - 1] = checksum_str[1];

  printf("Sending %.*s\n", raw_len, raw);

  if (send(socket_client, raw, raw_len, 0) != raw_len) {
    free(raw);
    fprintf(stderr, "Unable to send data to client\n");
    return false;
  }

  free(raw);
  return true;
}

bool rsp_bp_insert(char* data, size_t len) {
  enum mp_type type;
  uint32_t addr;
  uint32_t data_bp;
  int bp_len;

  if (3 != sscanf(data, "Z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    fprintf(stderr, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    fprintf(stderr, "Not a memory bp\n");
    return false;
  }

  bp_insert(addr);

  return rsp_send_str("OK");
}

bool rsp_bp_remove(char* data, size_t len) {
  enum mp_type type;
  uint32_t addr;
  uint32_t ppc;
  int bp_len;

  if (3 != sscanf(data, "z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    fprintf(stderr, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    fprintf(stderr, "Not a memory bp\n");
    return false;
  }

  bp_remove(addr);

  // check if we are currently on this bp that is removed
  debug_read(DBG_PPC_REG, &ppc);

  if (addr == ppc) {
    debug_write(DBG_NPC_REG, ppc); // re-execute this instruction
    debug_write(DBG_CTRL_REG, 0x1); // single-step
  }

  return rsp_send_str("OK");
}

bool rsp_loop() {
  struct packet packet;
  fd_set rfds;
  struct timeval tv;


  while (rsp_get_packet(&packet)) {
    printf("Received $%.*s\n", packet.raw_len, packet.raw);

    if (packet.raw[0] == 0x03) {
      rsp_signal();
      printf ("Received break\n");
      continue;
    }

    switch (packet.raw[0]) {
    case 'q':
      rsp_query(&packet.raw[0], packet.raw_len);
      break;

    case 'g':
      rsp_send_registers();
      break;

    case 'p':
      rsp_reg_read(&packet.raw[1], packet.raw_len-1);
      break;

    case 'P':
      rsp_reg_write(&packet.raw[1], packet.raw_len-1);
      break;

    case 'c':
    case 'C':
      rsp_continue(&packet.raw[0], packet.raw_len);
      break;

    case 's':
    case 'S':
      rsp_step(&packet.raw[0], packet.raw_len);
      break;

    case 'H':
      rsp_send_str("OK");
      break;

    case 'm':
      rsp_mem_read(&packet.raw[1], packet.raw_len-1);
      break;

    case '?':
      rsp_signal();
      break;

    case 'v':
      rsp_v(&packet.raw[0], packet.raw_len);
      break;

    case 'M':
      rsp_mem_write_ascii(&packet.raw[1], packet.raw_len-1);
      break;

    case 'X':
      rsp_mem_write(&packet.raw[1], packet.raw_len-1);
      break;

    case 'z':
      rsp_bp_remove(&packet.raw[0], packet.raw_len);
      break;

    case 'Z':
      rsp_bp_insert(&packet.raw[0], packet.raw_len);
      break;

    default:
      fprintf(stderr, "Unknown packet: starts with %c\n", packet.raw[0]);
      break;
    }
  }

  return true;
}

int main() {
  sim_mem_open(4567);

  while (1) {
    rsp_open(1234);
    while(!rsp_wait_client());
    rsp_loop();
    rsp_close();
  }
  return 0;
}
