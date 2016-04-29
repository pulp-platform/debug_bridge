
#include "rsp.h"

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

#define PACKET_MAX_LEN 4096

Rsp::Rsp(int socket_port, MemIF* mem, std::list<DbgIF*> list_dbgif, BreakPoints* bp) {
  m_socket_port = socket_port;
  m_mem = mem;
  m_dbgifs = list_dbgif;
  m_bp = bp;

  // select one dbg if at random
  if (m_dbgifs.size() == 0) {
    fprintf(stderr, "No debug interface available! Exiting now\n");
    exit(1);
  }

  m_thread_sel = m_dbgifs.front()->get_thread_id();
}

bool
Rsp::open() {
  struct sockaddr_in addr;
  int yes = 1;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(m_socket_port);
  addr.sin_addr.s_addr = INADDR_ANY;
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

  m_socket_in = socket(PF_INET, SOCK_STREAM, 0);
  if(m_socket_in < 0)
  {
    fprintf(stderr, "Unable to create comm socket: %s\n", strerror(errno));
    return false;
  }

  if(setsockopt(m_socket_in, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    fprintf(stderr, "Unable to setsockopt on the socket: %s\n", strerror(errno));
    return false;
  }

  if(bind(m_socket_in, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    fprintf(stderr, "Unable to bind the socket: %s\n", strerror(errno));
    return false;
  }

  if(listen(m_socket_in, 1) == -1) {
    fprintf(stderr, "Unable to listen: %s\n", strerror(errno));
    return false;
  }

  fprintf(stderr, "Listening on port %d\n", m_socket_port);

  // now clear resources
  for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
    (*it)->halt();
  }

  return true;
}

void
Rsp::close() {
  m_bp->clear();
  ::close(m_socket_in);
}

bool
Rsp::wait_client() {
  if((m_socket_client = accept(m_socket_in, NULL, NULL)) == -1) {
    if(errno == EAGAIN)
      return false;

    fprintf(stderr, "Unable to accept connection: %s\n", strerror(errno));
    return false;
  }

  printf("RSP: Client connected!\n");
  return true;
}

bool
Rsp::loop() {
  char pkt[PACKET_MAX_LEN];
  size_t len;

  fd_set rfds;
  struct timeval tv;

  while (this->get_packet(pkt, &len)) {
    printf("Received $%.*s\n", len, pkt);
    if (!this->decode(pkt, len))
      return false;
  }

  return true;
}

bool
Rsp::decode(char* data, size_t len) {
  if (data[0] == 0x03) {
    printf ("Received break\n");
    return this->signal();
  }

  switch (data[0]) {
  case 'q':
    return this->query(&data[0], len);

  case 'g':
    return this->regs_send();

  case 'p':
    return this->reg_read(&data[1], len-1);

  case 'P':
    return this->reg_write(&data[1], len-1);

  case 'c':
  case 'C':
    return this->cont(&data[0], len);

  case 's':
  case 'S':
    return this->step(&data[0], len);

  case 'H':
    return this->multithread(&data[1], len-1);

  case 'm':
    return this->mem_read(&data[1], len-1);

  case '?':
    return this->signal();

  case 'v':
    return this->v_packet(&data[0], len);

  case 'M':
    return this->mem_write_ascii(&data[1], len-1);

  case 'X':
    return this->mem_write(&data[1], len-1);

  case 'z':
    return this->bp_remove(&data[0], len);

  case 'Z':
    return this->bp_insert(&data[0], len);

  case 'T':
    return this->send_str("OK"); // threads are always alive

  case 'D':
    this->send_str("OK");
    return false;

  default:
    fprintf(stderr, "Unknown packet: starts with %c\n", data[0]);
    break;
  }

  return false;
}

bool
Rsp::cont(char* data, size_t len) {
  uint32_t sig;
  uint32_t addr;
  uint32_t npc;
  int i;
  bool npc_found = false;
  DbgIF* dbgif;

  // strip signal first
  if (data[0] == 'C') {
    if (sscanf(data, "C%X;%X", &sig, &addr) == 2)
      npc_found = true;
  } else {
    if (sscanf(data, "c%X", &addr) == 1)
      npc_found = true;
  }

  if (npc_found) {
    dbgif = this->get_dbgif(m_thread_sel);
    // only when we have received an address
    dbgif->read(DBG_NPC_REG, &npc);

    if (npc != addr)
      dbgif->write(DBG_NPC_REG, addr);
  }

  return this->resume(false);
}

bool
Rsp::step(char* data, size_t len) {
  uint32_t addr;
  uint32_t npc;
  int i;
  DbgIF* dbgif;

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
    dbgif = this->get_dbgif(m_thread_sel);
    // only when we have received an address
    dbgif->read(DBG_NPC_REG, &npc);

    if (npc != addr)
      dbgif->write(DBG_NPC_REG, addr);
  }

  return this->resume(true);
}

bool
Rsp::multithread(char* data, size_t len) {
  int thread_id;

  switch (data[0]) {
    case 'c':
    case 'g':
      if (sscanf(&data[1], "%d", &thread_id) != 1)
        return false;

      if (thread_id == -1) // affects all threads
        return this->send_str("OK");

      // we got the thread id, now let's look for this thread in our list
      if (this->get_dbgif(thread_id) != NULL) {
        m_thread_sel = thread_id;
        return this->send_str("OK");
      }

      return this->send_str("E01");
      return true;
  }

  return false;
}

bool
Rsp::query(char* data, size_t len) {
  int ret;
  char reply[256];

  if (strncmp ("qSupported", data, strlen ("qSupported")) == 0)
  {
    return this->send_str("PacketSize=256");
  }
  else if (strncmp ("qTStatus", data, strlen ("qTStatus")) == 0)
  {
    // not supported, send empty packet
    return this->send_str("");
  }
  else if (strncmp ("qfThreadInfo", data, strlen ("qfThreadInfo")) == 0)
  {
    reply[0] = 'm';
    ret = 1;

    for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
      ret += snprintf(&reply[ret], 256 - ret, "%u,", (*it)->get_thread_id());
    }

    return this->send(reply, ret-1);
  }
  else if (strncmp ("qsThreadInfo", data, strlen ("qsThreadInfo")) == 0)
  {
    return this->send_str("l");
  }
  else if (strncmp ("qThreadExtraInfo", data, strlen ("qThreadExtraInfo")) == 0)
  {
    const char* str_default = "Unknown Core";
    char str[256];
    unsigned int thread_id;
    if (sscanf(data, "qThreadExtraInfo,%d", &thread_id) != 1) {
      fprintf(stderr, "Could not parse qThreadExtraInfo packet\n");
      return this->send_str("");
    }

    DbgIF* dbgif = this->get_dbgif(thread_id);

    if (dbgif != NULL)
      dbgif->get_name(str, 256);
    else
      strcpy(str, str_default);

    ret = 0;
    for(int i = 0; i < strlen(str); i++)
      ret += snprintf(&reply[ret], 256 - ret, "%02X", str[i]);

    return this->send(reply, ret);
  }
  else if (strncmp ("qAttached", data, strlen ("qAttached")) == 0)
  {
    return this->send_str("1");
  }
  else if (strncmp ("qC", data, strlen ("qC")) == 0)
  {
    snprintf(reply, 64, "0.%u", this->get_dbgif(m_thread_sel)->get_thread_id());
    return this->send_str(reply);
  }
  else if (strncmp ("qSymbol", data, strlen ("qSymbol")) == 0)
  {
    return this->send_str("OK");
  }
  else if (strncmp ("qOffsets", data, strlen ("qOffsets")) == 0)
  {
    return this->send_str("Text=0;Data=0;Bss=0");
  }
  else if (strncmp ("qT", data, strlen ("qT")) == 0)
  {
    // not supported, send empty packet
    return this->send_str("");
  }

  fprintf(stderr, "Unknown query packet\n");

  return false;
}

bool
Rsp::v_packet(char* data, size_t len) {
  if (strncmp ("vKill", data, strlen ("vKill")) == 0)
  {
    this->send_str("OK");
    return false;
  }
  else if (strncmp ("vCont?", data, strlen ("vCont?")) == 0)
  {
    return this->send_str("");
  }

  fprintf(stderr, "Unknown v packet\n");

  return false;
}

bool
Rsp::regs_send() {
  uint32_t gpr[32];
  uint32_t npc;
  uint32_t ppc;
  char regs_str[512];
  int i;

  this->get_dbgif(m_thread_sel)->gpr_read_all(gpr);

  // now build the string to send back
  for(i = 0; i < 32; i++) {
    snprintf(&regs_str[i * 8], 9, "%08x", htonl(gpr[i]));
  }

  this->pc_read(&npc);
  snprintf(&regs_str[32 * 8 + 0 * 8], 9, "%08x", htonl(npc));

  return this->send_str(regs_str);
}

bool
Rsp::reg_read(char* data, size_t len) {
  uint32_t addr;
  uint32_t rdata;
  char data_str[10];

  if (sscanf(data, "%x", &addr) != 1) {
    fprintf(stderr, "Could not parse packet\n");
    return false;
  }

  if (addr < 32)
    this->get_dbgif(m_thread_sel)->gpr_read(addr, &rdata);
  else if (addr == 0x20)
    this->pc_read(&rdata);
  else
    return this->send_str("");

  rdata = htonl(rdata);
  snprintf(data_str, 9, "%08x", rdata);

  return this->send_str(data_str);
}

bool
Rsp::reg_write(char* data, size_t len) {
  uint32_t addr;
  uint32_t wdata;
  char data_str[10];
  DbgIF* dbgif;

  if (sscanf(data, "%x=%08x", &addr, &wdata) != 2) {
    fprintf(stderr, "Could not parse packet\n");
    return false;
  }

  wdata = ntohl(wdata);

  dbgif = this->get_dbgif(m_thread_sel);
  if (addr < 32)
    dbgif->gpr_write(addr, wdata);
  else if (addr == 32)
    dbgif->write(DBG_NPC_REG, wdata);
  else
    return this->send_str("E01");

  return this->send_str("OK");
}

bool
Rsp::get_packet(char* pkt, size_t* p_pkt_len) {
  char c;
  char check_chars[2];
  char buffer[PACKET_MAX_LEN];
  int  buffer_len = 0;
  int  pkt_len;
  bool escaped = false;
  int ret;
  // packets follow the format: $packet-data#checksum
  // checksum is two-digit

  // poison packet
  memset(pkt, 0, PACKET_MAX_LEN);
  pkt_len = 0;

  // first look for start bit
  do {
    ret = recv(m_socket_client, &c, 1, 0);

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
      pkt[0]  = c;
      *p_pkt_len = 1;
      return true;
    }
  } while(c != '$');

  buffer[0] = c;

  // now store data as long as we don't see #
  do {
    if (buffer_len >= PACKET_MAX_LEN || pkt_len >= PACKET_MAX_LEN) {
      fprintf(stderr, "RSP: Too many characters received\n");
      return false;
    }

    ret = recv(m_socket_client, &c, 1, 0);

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
      pkt[pkt_len++] = c ^ 0x20;
    else
      pkt[pkt_len++] = c;

    escaped = false;
  } while(c != '#');

  buffer_len--;
  pkt_len--;

  // checksum, 2 bytes
  ret = recv(m_socket_client, &check_chars[0], 1, 0);
  if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
    fprintf(stderr, "RSP: Error receiving\n");
    return false;
  }

  ret = recv(m_socket_client, &check_chars[1], 1, 0);
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
    fprintf(stderr, "RSP: Checksum failed; received %.*s; checksum should be %02x\n", pkt_len, pkt, checksum);
    return false;
  }

  // now send ACK
  char ack = '+';
  if (::send(m_socket_client, &ack, 1, 0) != 1) {
    fprintf(stderr, "RSP: Sending ACK failed\n");
    return false;
  }

  // NULL terminate the string
  pkt[pkt_len] = '\0';
  *p_pkt_len = pkt_len;

  return true;
}

bool
Rsp::signal() {
  uint32_t cause;
  uint32_t hit;
  int signal;
  char str[4];
  int len;
  DbgIF* dbgif;

  dbgif = this->get_dbgif(m_thread_sel);

  dbgif->write(DBG_IE_REG, 0xFFFF);

  // figure out why we are stopped
  if (dbgif->is_stopped()) {
    if (!dbgif->read(DBG_HIT_REG, &hit))
      return false;
    if (!dbgif->read(DBG_CAUSE_REG, &cause))
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

  len = snprintf(str, 4, "S%02x", signal);

  return this->send(str, len);
}

bool
Rsp::send(const char* data, size_t len) {
  int ret;
  size_t raw_len = len + 4;
  char* raw = (char*)malloc(raw_len);
  unsigned int checksum = 0;

  raw[0] = '$';
  raw[raw_len - 3] = '#';

  // TODO: ESCAPING IS REQUIRED HERE!!!!
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

    if (::send(m_socket_client, raw, raw_len, 0) != raw_len) {
      free(raw);
      fprintf(stderr, "Unable to send data to client\n");
      return false;
    }

    ret = recv(m_socket_client, &ack, 1, 0);
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

bool
Rsp::send_str(const char* data) {
  return this->send(data, strlen(data));
}

// internal helper functions
bool
Rsp::pc_read(unsigned int* pc) {
  uint32_t npc;
  uint32_t ppc;
  uint32_t cause;
  uint32_t hit;
  DbgIF* dbgif;

  dbgif = this->get_dbgif(m_thread_sel);

  dbgif->read(DBG_PPC_REG, &ppc);
  dbgif->read(DBG_NPC_REG, &npc);

  dbgif->read(DBG_HIT_REG, &hit);
  dbgif->read(DBG_CAUSE_REG, &cause);

  if (hit & 0x1)
    *pc = npc;
  else if(cause & (1 << 31)) // interrupt
    *pc = npc;
  else if(cause == 3)  // breakpoint
    *pc = ppc;
  else if(cause == 2)
    *pc = ppc;
  else if(cause == 5)
    *pc = ppc;
  else
    *pc = npc;

  return true;
}

bool
Rsp::resume(bool step) {
  int ret;
  char pkt;
  uint32_t hit;
  uint32_t cause;
  uint32_t ppc;
  uint32_t npc;
  uint32_t data;
  DbgIF* dbgif;

  dbgif = this->get_dbgif(m_thread_sel);

  // now let's handle software breakpoints
  dbgif->read(DBG_PPC_REG, &ppc);
  dbgif->read(DBG_NPC_REG, &npc);
  dbgif->read(DBG_HIT_REG, &hit);
  dbgif->read(DBG_CAUSE_REG, &cause);

  // if there is a breakpoint at this address, let's remove it and single-step over it
  if (m_bp->at_addr(ppc)) {
    m_bp->disable(ppc);
    dbgif->write(DBG_NPC_REG, ppc); // re-execute this instruction
    dbgif->write(DBG_CTRL_REG, 0x1); // single-step
    m_bp->enable(ppc);
  }

  // clear hit register, has to be done before CTRL
  dbgif->write(DBG_HIT_REG, 0);

  if (step)
    dbgif->write(DBG_CTRL_REG, 0x1);
  else
    dbgif->write(DBG_CTRL_REG, 0);

  fd_set rfds;
  struct timeval tv;


  while(1) {
    FD_ZERO(&rfds);
    FD_SET(m_socket_client, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 100 * 1000;

    if (select(m_socket_client+1, &rfds, NULL, NULL, &tv) == 0) {
      if (dbgif->is_stopped()) {
        return this->signal();
      }
    } else {
      ret = recv(m_socket_client, &pkt, 1, 0);
      if (ret == 1 && pkt == 0x3) {
        printf("Stopping core\n");
        dbgif->halt();
      }
    }
  }

  return true;
}

bool
Rsp::mem_read(char* data, size_t len) {
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

  m_mem->access(0, addr, length, buffer);

  for(i = 0; i < length; i++) {
    rdata = buffer[i];
    snprintf(&reply[i * 2], 3, "%02x", rdata);
  }

  return this->send(reply, length*2);
}

bool
Rsp::mem_write_ascii(char* data, size_t len) {
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

  m_mem->access(1, addr, buffer_len, buffer);

  free(buffer);

  return this->send_str("OK");
}

bool
Rsp::mem_write(char* data, size_t len) {
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

  m_mem->access(1, addr, len, data);

  return this->send_str("OK");
}

bool
Rsp::bp_insert(char* data, size_t len) {
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

  m_bp->insert(addr);

  return this->send_str("OK");
}

bool
Rsp::bp_remove(char* data, size_t len) {
  enum mp_type type;
  uint32_t addr;
  uint32_t ppc;
  int bp_len;
  DbgIF* dbgif;

  dbgif = this->get_dbgif(m_thread_sel);

  if (3 != sscanf(data, "z%1d,%x,%1d", (int *)&type, &addr, &bp_len)) {
    fprintf(stderr, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    fprintf(stderr, "Not a memory bp\n");
    return false;
  }

  m_bp->remove(addr);

  // check if we are currently on this bp that is removed
  dbgif->read(DBG_PPC_REG, &ppc);

  if (addr == ppc) {
    dbgif->write(DBG_NPC_REG, ppc); // re-execute this instruction
    dbgif->write(DBG_CTRL_REG, 0x1); // single-step
  }

  return this->send_str("OK");
}

DbgIF*
Rsp::get_dbgif(int thread_id) {
  for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
    if ((*it)->get_thread_id() == thread_id)
      return *it;
  }

  return NULL;
}
