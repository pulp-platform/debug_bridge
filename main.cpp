
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

int socket_in;
int socket_client;

struct packet {
  char raw[512];
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

struct bp_insn {
  uint32_t addr;
  uint32_t insn_orig;
  bool is_compressed;
};

std::list<struct bp_insn> g_bp_list;

#define INSN_IS_COMPRESSED(instr) ((instr & 0x3) != 0x3)
#define INSN_BP_COMPRESSED   0x8002
#define INSN_BP              0x00100073

#define DEBUG_BASE_ADDR 0x1A110000

#define DBG_CTRL_REG  0x0
#define DBG_HIT_REG   0x4
#define DBG_IE_REG    0x8
#define DBG_CAUSE_REG 0xC
#define DBG_NPC_REG   0x1200
#define DBG_PPC_REG   0x1204

bool sim_mem_open();
bool sim_mem_read(uint32_t addr, uint32_t *rdata);
bool sim_mem_write(uint32_t addr, uint8_t be, uint32_t wdata);

bool sim_mem_write_h(uint32_t addr, uint32_t wdata);
bool sim_mem_write_w(uint32_t addr, uint32_t wdata);
bool sim_mem_read_w(uint32_t addr, uint32_t* rdata);
bool debug_write(uint32_t addr, uint32_t wdata);
bool debug_read(uint32_t addr, uint32_t* rdata);
bool debug_halt();
bool debug_resume(bool step);
bool debug_gpr_read(int i, uint32_t *data);
bool debug_gpr_write(int i, uint32_t data);
bool debug_csr_read(int i, uint32_t *data);
bool debug_csr_write(int i, uint32_t data);
bool debug_is_stopped();

bool rsp_notify(char* data, size_t len);


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

  //ret = fcntl(socket_in, F_GETFL);
  //ret |= O_NONBLOCK;
  //fcntl(socket_in, F_SETFL, ret);

  fprintf(stderr, "Listening on port %d\n", socket_port);

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


  // Set the comm socket to non-blocking.
  //ret = fcntl(socket_client, F_GETFL);
  //ret |= O_NONBLOCK;
  //fcntl(socket_client, F_SETFL, ret);

  printf("RSP: Client connected!\n");
  return true;
}

bool rsp_get_packet(struct packet* packet) {
  int ret;
  // packets follow the format: $packet-data#checksum
  // checksum is two-digit

  // poison packet
  memset(packet->raw, 0, 512);
  packet->raw_len = 0;

  // first look for start bit
  do {
    ret = recv(socket_client, &packet->raw[0], 1, 0);

    if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
      fprintf(stderr, "RSP: Error receiving\n");
      return false;
    }

    // special case for 0x03 (asynchronous break)
    if (packet->raw[0] == 0x03) {
      packet->raw_len = 1;
      return true;
    }

    printf ("recv: %x\n", packet->raw[0] & 0xFF);
  } while(packet->raw[0] != '$');

  packet->raw_len = 1;

  // now store data as long as we don't see #
  do {
    if (packet->raw_len >= 512) {
      fprintf(stderr, "RSP: Too many characters received\n");
      return false;
    }

    ret = recv(socket_client, &packet->raw[packet->raw_len++], 1, 0);

    if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
      fprintf(stderr, "RSP: Error receiving\n");
      return false;
    }
  } while(packet->raw[packet->raw_len-1] != '#');

  // checksum, 2 bytes
  ret = recv(socket_client, &packet->raw[packet->raw_len++], 1, 0);
  if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
    fprintf(stderr, "RSP: Error receiving\n");
    return false;
  }

  ret = recv(socket_client, &packet->raw[packet->raw_len++], 1, 0);
  if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
    fprintf(stderr, "RSP: Error receiving\n");
    return false;
  }

  // check the checksum
  unsigned int checksum = 0;
  for(int i = 1; i < packet->raw_len-3; i++) {
    checksum += packet->raw[i];
  }

  checksum = checksum % 256;
  char checksum_str[3];
  snprintf(checksum_str, 3, "%02x", checksum);

  if (packet->raw[packet->raw_len-2] != checksum_str[0] || packet->raw[packet->raw_len-1] != checksum_str[1]) {
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
  packet->raw[packet->raw_len-3] = '\0';

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
    rsp_send_str("");
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

  for (i = 0; i < 32; i++) {
    debug_gpr_read(i, &gpr[i]);
  }

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

  sscanf(data, "%x", &addr);

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

  sscanf(data, "%x=%08x", &addr, &wdata);

  wdata = ntohl(wdata);

  if (addr < 32)
    debug_gpr_write(addr, wdata);
  else
    return rsp_send_str("E01");

  return rsp_send_str("OK");
}

bool rsp_write_regs(char* data, size_t len) {
  printf("REG WRITE IS STILL TODO\n");
  return false;
}

bool rsp_mem_read(char* data, size_t len) {
  char reply[512];
  uint32_t addr;
  uint32_t length;
  uint32_t rdata;
  int i;

  sscanf(data, "%x,%x", &addr, &length);

  for(i = 0; i < length; i++) {
    sim_mem_read(addr + i * 4, &rdata);
    rdata = htonl(rdata);
    snprintf(&reply[i * 8], 9, "%08x", rdata);
  }

  return rsp_send(reply, length*8);
}

bool rsp_mem_write_ascii(char* data, size_t len) {
  uint32_t addr;
  size_t length;
  uint32_t wdata;
  int i, j;

  if (sscanf(data, "%x,%d:", &addr, &length) != 2)
    return false;

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

  for(j = 0; j < len/8; j++) {
    wdata = 0;
    for(i = 0; i < 8; i++) {
      char c = data[j * 8 + i];
      uint32_t hex = 0;
      if (c >= '0' && c <= '9')
        hex = c - '0';
      else if (c >= 'a' && c <= 'f')
        hex = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        hex = c - 'A' + 10;

      wdata |= hex << (4 * i);
    }

    sim_mem_write_w(addr, wdata);
    addr += 4;
  }

  return rsp_send_str("OK");
}

bool rsp_mem_write(char* data, size_t len) {
  uint32_t addr;
  size_t length;
  uint32_t wdata;
  int i, j;

  if (sscanf(data, "%x,%d:", &addr, &length) != 2)
    return false;

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

  for(j = 0; j < len/4; j++) {
    wdata = *(int*)&data[j * 4];

    printf ("Addr %X, wdata %X\n", addr, wdata);
    sim_mem_write_w(addr, wdata);
    addr += 4;
  }

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

bool rsp_signal() {
  char str[4];
  size_t len;

  len = gdb_signal(str, 4);

  rsp_send(str, len);
  return true;
}

bool rsp_continue(char* data, size_t len) {
  uint32_t addr;
  uint32_t npc;

  // if (data[0] == 'C') {
  //   // strip signal first
  // }
  // sscanf(data, "%x", &addr);

  // debug_read(DBG_NPC_REG, &npc);

  // if (npc != addr)
  //   debug_write(DBG_NPC_REG, addr);

  return debug_resume(false);
}

bool rsp_step(char* data, size_t len) {
  uint32_t addr;
  uint32_t npc;

  // if (data[0] == 'C') {
  //   // strip signal first
  // }
  // sscanf(data, "%x", &addr);

  // debug_read(DBG_NPC_REG, &npc);

  // if (npc != addr)
  //   debug_write(DBG_NPC_REG, addr);

  return debug_resume(true);
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
  int bp_len;

  if (3 != sscanf(data, "Z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    fprintf(stderr, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    fprintf(stderr, "Not a memory bp\n");
    return false;
  }

  struct bp_insn bp;

  bp.addr = addr;
  sim_mem_read_w(addr, &bp.insn_orig);
  bp.is_compressed = INSN_IS_COMPRESSED(bp.insn_orig);

  g_bp_list.push_back(bp);

  if (bp.is_compressed)
    sim_mem_write_h(addr, INSN_BP_COMPRESSED);
  else
    sim_mem_write_w(addr, INSN_BP);

  return rsp_send_str("OK");
}

bool rsp_bp_remove(char* data, size_t len) {
  enum mp_type type;
  uint32_t addr;
  int bp_len;

  if (3 != sscanf(data, "z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    fprintf(stderr, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    fprintf(stderr, "Not a memory bp\n");
    return false;
  }

  for (std::list<struct bp_insn>::iterator it = g_bp_list.begin(); it != g_bp_list.end(); it++) {
    if (it->addr == addr) {
      if (it->is_compressed)
        sim_mem_write_h(it->addr, it->insn_orig);
      else
        sim_mem_write_w(it->addr, it->insn_orig);

      g_bp_list.erase(it);
      break;
    }
  }

  return rsp_send_str("OK");
}

bool rsp_loop() {
  struct packet packet;
  fd_set rfds;
  struct timeval tv;


  while(1) {
    if (rsp_get_packet(&packet)) {
      printf("Received %.*s\n", packet.raw_len, packet.raw);

      if (packet.raw[0] == 0x03) {
        rsp_signal();
        printf ("Received break\n");
        continue;
      }

      switch (packet.raw[1]) {
      case 'q':
        rsp_query(&packet.raw[1], packet.raw_len-4);
        break;

      case 'g':
        rsp_send_registers();
        break;

      case 'p':
        rsp_reg_read(&packet.raw[2], packet.raw_len-4);
        break;

      case 'P':
        rsp_reg_write(&packet.raw[2], packet.raw_len-4);
        break;

      case 'G':
        rsp_write_regs(&packet.raw[2], packet.raw_len-4);
        break;

      case 'c':
      case 'C':
        rsp_continue(&packet.raw[1], packet.raw_len-4);
        break;

      case 's':
      case 'S':
        rsp_step(&packet.raw[1], packet.raw_len-4);
        break;

      case 'H':
        rsp_send_str("OK");
        break;

      case 'm':
        rsp_mem_read(&packet.raw[2], packet.raw_len-4);
        break;

      case '?':
        rsp_signal();
        break;

      case 'v':
        rsp_v(&packet.raw[1], packet.raw_len-4);
        break;

      case 'M':
        rsp_mem_write_ascii(&packet.raw[2], packet.raw_len-5);
        break;

      case 'X':
        rsp_mem_write(&packet.raw[2], packet.raw_len-5);
        break;

      case 'z':
        rsp_bp_remove(&packet.raw[1], packet.raw_len-4);
        break;

      case 'Z':
        rsp_bp_insert(&packet.raw[1], packet.raw_len-4);
        break;

      default:
        fprintf(stderr, "Unknown packet: starts with %c\n", packet.raw[1]);
        break;
      }
    } else {
      return false;
    }
  }

  return true;
}


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

bool debug_write(uint32_t addr, uint32_t wdata) {
  return sim_mem_write_w(DEBUG_BASE_ADDR + addr, wdata);
}

bool debug_read(uint32_t addr, uint32_t* rdata) {
  return sim_mem_read_w(DEBUG_BASE_ADDR + addr, rdata);
}

bool debug_halt() {
  uint32_t data;
  if (!debug_read(DBG_CTRL_REG, &data))
    return false;

  data |= 0x1 << 16;
  return debug_write(DBG_CTRL_REG, data);
}

bool debug_resume(bool step) {
  int ret;
  char pkt;
  uint32_t hit;
  uint32_t ppc;


  // now let's handle software breakpoints
  // did we stop because of an ebreak?
  //  If yes, let's check if we inserted it (is in g_bp_list)
  debug_read(DBG_PPC_REG, &ppc);
  debug_read(DBG_HIT_REG, &hit);
  if (hit & 0x1) {
    for (std::list<struct bp_insn>::iterator it = g_bp_list.begin(); it != g_bp_list.end(); it++) {
      if (it->addr == ppc) {
        // we found our bp
        // This means we now have to replace it with its old value and
        // single-step once, then replace it with a bp again
        if (it->is_compressed)
          sim_mem_write_h(it->addr, it->insn_orig);
        else
          sim_mem_write_w(it->addr, it->insn_orig);

        debug_write(DBG_CTRL_REG, 0x1);
        if (it->is_compressed)
          sim_mem_write_h(it->addr, INSN_BP_COMPRESSED);
        else
          sim_mem_write_w(it->addr, INSN_BP);
      }
    }
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
        debug_halt();
      }
    }
  }
}

bool debug_is_stopped() {
  uint32_t data;
  if (!debug_read(DBG_CTRL_REG, &data))
    return false;

  if (data & 0x10000)
    return true;
  else
    return false;
}

bool debug_gpr_read(int i, uint32_t *data) {
  return debug_read(0x1000 + i * 4, data);
}

bool debug_gpr_write(int i, uint32_t data) {
  return debug_write(0x1000 + i * 4, data);
}

bool debug_csr_read(int i, uint32_t *data) {
  return debug_read(0x4000 + i * 4, data);
}

bool debug_csr_write(int i, uint32_t data) {
  return debug_write(0x4000 + i * 4, data);
}

int main() {
  rsp_open(1234);
  while(!rsp_wait_client());
  debug_halt();
  rsp_loop();
  rsp_close();
  return 0;
}
