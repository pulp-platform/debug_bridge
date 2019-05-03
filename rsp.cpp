#include <inttypes.h>
#include "rsp.h"

enum mp_type {
  BP_MEMORY   = 0,
  BP_HARDWARE = 1,
  WP_WRITE    = 2,
  WP_READ     = 3,
  WP_ACCESS   = 4
};

// Cf. riscv_defines.sv and riscv_controller.sv (and RISCV debug draft 0.4)
enum debug_causes {
  CAUSE_ILLEGAL_INSN = 0x02, // Illegal instruction
  CAUSE_BREAKPOINT   = 0x03, // Break point
  CAUSE_ECALL_UMODE  = 0x08, // ECALL from User Mode
  CAUSE_ECALL_MMODE  = 0x0B, // ECALL from Machine Mode
};

#define PACKET_MAX_LEN 4096

Rsp::Rsp(int socket_port, MemIF* mem, LogIF *log, std::list<DbgIF*> list_dbgif, BreakPoints* bp) {
  m_socket_port = socket_port;
  m_mem = mem;
  m_dbgifs = list_dbgif;
  m_bp = bp;
  this->log = log;

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
  bool ret = 0;

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

  fprintf(stderr, "Debug bridge listening on port %d\n", m_socket_port);

  // now clear resources
  for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
    if (!(*it)->halt()) {
      printf("ERROR: failed sending halt\n");
      ret = 1;
    }
  }

  return ret;
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

  log->debug("RSP: Client connected!\n");
  return true;
}

bool
Rsp::loop() {
  char pkt[PACKET_MAX_LEN];
  size_t len;

  while (this->get_packet(pkt, &len)) {
    log->debug("Received $%.*s\n", len, pkt);
    if (!this->decode(pkt, len))
      return false;
  }

  return true;
}

bool
Rsp::ctrlc() {
  // Cf. https://sourceware.org/gdb/onlinedocs/gdb/Interrupts.html
  log->debug ("Received break\n");

  DbgIF* dbgif = this->get_dbgif(m_thread_sel);
  if (!dbgif->halt() || !dbgif->is_stopped()) {
    printf("ERROR: could not halt.\n");
    return false;
  }

  return this->send_signal(TARGET_SIGNAL_INT);
}

bool
Rsp::decode(char* data, size_t len) {
  if (data[0] == 0x03) {
    return this->ctrlc();
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

  case '?': {
    DbgIF* dbgif = this->get_dbgif(m_thread_sel);
    if (dbgif->is_stopped())
      return this->send_stop_reason();
    else
      return this->send_str("OK");
  }
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
    // The proper response to unsupported packets is the empty string, cf.
    // https://sourceware.org/gdb/onlinedocs/gdb/Packets.html
    fprintf(stderr, "Unknown packet: %.*s\n", (int)len, data);
    return this->send_str("");
  }

  return false; // Never reached
}

bool
Rsp::cont(char* data, size_t len) {
  uint32_t sig;
  uint32_t addr;
  uint32_t npc;
  bool npc_found = false;
  DbgIF* dbgif;

  // strip signal first
  if (data[0] == 'C') {
    if (sscanf(data, "C%" SCNx32 ";%" SCNx32 "", &sig, &addr) == 2)
      npc_found = true;
  } else {
    if (sscanf(data, "c%" SCNx32 "", &addr) == 1)
      npc_found = true;
  }

  if (npc_found) {
    dbgif = this->get_dbgif(m_thread_sel);
    // only when we have received an address
    dbgif->read(DBG_NPC_REG, &npc);

    if (npc != addr)
      dbgif->write(DBG_NPC_REG, addr);
  }

  m_thread_sel = 0;

  return this->resume(false);
}

bool
Rsp::step(char* data, size_t len) {
  uint32_t addr;
  uint32_t npc;
  DbgIF* dbgif;

  // strip signal first
  if (data[0] == 'S') {
    for (size_t i = 0; i < len; i++) {
      if (data[i] == ';') {
        data = &data[i+1];
        break;
      }
    }
  }

  if (sscanf(data, "%" SCNx32, &addr) == 1) {
    dbgif = this->get_dbgif(m_thread_sel);
    // only when we have received an address
    dbgif->read(DBG_NPC_REG, &npc);

    if (npc != addr)
      dbgif->write(DBG_NPC_REG, addr);
  }

  m_thread_sel = 0;

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
  }

  return false;
}

#define PULP_CTRL_AXI_ADDR   0x51000000
#include "mem_zynq_apb_spi.h"

int pulp_ctrl(int fetch_en, int reset) {
  static volatile uint32_t* ctrl_base = NULL;
    if (MemIF::mmap_gen(PULP_CTRL_AXI_ADDR, 0x1000, &ctrl_base) != 0) {
      fprintf(stderr, "Could not map CTRL interface\n");
      return 1;
    }

  volatile uint32_t* gpio = ctrl_base + 0x0; // FIXME: name
  volatile uint32_t* dir  = ctrl_base + 0x1;

  // now we can actually write to the peripheral
  uint32_t val = 0x0;
  if (reset == 0)
    val |= (1 << 31); // reset is active low

  if (fetch_en)
    val |= (1 << 0);

  *dir  = 0x0; // configure as output
  *gpio = val;

  return 0;
}

#define SPIDEV "/dev/spidev32766.0"
#include <linux/types.h>
#include <linux/spi/spidev.h>
int set_boot_addr(uint32_t boot_addr) {
  int fd;
  uint8_t wr_buf[9];
  int retval = 0;

  const uint32_t reg_addr = 0x1A107008;

  wr_buf[0] = 0x02; // write command
  wr_buf[1] = (reg_addr >> 24) & 0xFF;
  wr_buf[2] = (reg_addr >> 16) & 0xFF;
  wr_buf[3] = (reg_addr >>  8) & 0xFF;
  wr_buf[4] = (reg_addr >>  0) & 0xFF;
  // address
  wr_buf[5] = (boot_addr >> 24) & 0xFF;
  wr_buf[6] = (boot_addr >> 16) & 0xFF;
  wr_buf[7] = (boot_addr >>  8) & 0xFF;
  wr_buf[8] = (boot_addr >>  0) & 0xFF;

  fd = open(SPIDEV, O_RDWR);
  if (fd <= 0) {
    perror(SPIDEV " not found");

    retval = -1;
    goto fail;
  }

  if (write(fd, wr_buf, 9) != 9) {
    perror("Could not write to " SPIDEV);

    retval = -1;
    goto fail;
  }

fail:
  close(fd);

  return retval;
}

bool
Rsp::monitor_help(char *str, size_t len) {
  const char *text;
  if (strncmp ("reset", str, strlen("reset")) == 0)
  {
    static const char text_reset[] = 
      "Help for reset:\n"
      "	run  -- Reset and restart the target core\n"
      "	halt -- Reset the target core and hold its execution\n"
    ;
    text = text_reset;
  }
  else 
  {
    static const char text_general[] = 
      "General commands:\n"
      "	help  -- Display help for monitor commands\n"
      "	reset -- Reset the target core\n"
    ;
    text = text_general;
  }
  char out[512];
  if (!encode_hex(text, out, sizeof(out)))
    return this->send_str("E00");

  return this->send_str(out);
}

bool
Rsp::reset(bool halt) {
    pulp_ctrl(0, 1);
    pulp_ctrl(0, 0);
    
    set_boot_addr(0);

    if (!halt) {
      pulp_ctrl(1, 0);
    } else {
      return this->send_str("E00");
      // FIXME: the following does not really work, neither does simply 
      // not re-enabling the fetch enable
      // DbgIF* dbgif = this->get_dbgif(m_thread_sel);
      // dbgif->write(DBG_IE_REG, 0xFFFF);
    }

    return this->send_str("OK");
}

bool
Rsp::monitor(char *str, size_t len) {
  // Each two input characters translate to one output character + \0.
  // gdb uses a default maximum of a little under 200 characters but might
  // grow that when receiving bigger inputs (sic)...
  // Currently there is no need for any really big payloads thus less than
  // 200 is used below.
  char buf[64];
  if (len/2 >= sizeof(buf)) {
    fprintf(stderr, "Insufficient buffer for complete monitor packet payload.\n");
    return this->send_str("");
  }

  // Decode hex string
  size_t i;
  for(i = 0; i < len; i+=2)
    sscanf(&str[i], "%2hhx", &buf[i/2]);
  buf[i/2] = '\0';

  size_t help_len = strlen("help");
  size_t reset_len = strlen("reset");
  if (strncmp(buf, "help", help_len) == 0) {
    help_len += strspn(&buf[help_len], " \t");
    return monitor_help(&buf[help_len], len-help_len);
  }
  else if (strncmp(buf, "reset", reset_len) == 0) 
  {
    bool halt = 0;
    reset_len += strspn(&buf[reset_len], " \t");
    if (strncmp(&buf[reset_len], "halt", strlen("halt")) == 0) {
      halt = 1;
    }
    return this->reset(halt);
  } 

  // Default to not supported
  return this->send_str("");
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
    if (sscanf(data, "qThreadExtraInfo,%u", &thread_id) != 1) {
      fprintf(stderr, "Could not parse qThreadExtraInfo packet\n");
      return this->send_str("");
    }

    DbgIF* dbgif = this->get_dbgif(thread_id);

    if (dbgif != NULL)
      dbgif->get_name(str, 256);
    else
      strcpy(str, str_default);

    ret = 0;
    for(size_t i = 0; i < strlen(str); i++)
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
  else if (strncmp ("qRcmd,", data, strlen ("qRcmd,")) == 0)
  {
    return this->monitor(&data[6], len-6);
  }

  // The proper response to an unknown query packet is the empty string, cf.
  // https://sourceware.org/gdb/onlinedocs/gdb/Packets.html
  fprintf(stderr, "Unknown query packet: %.*s\n", (int)len, data);
  return this->send_str("");
}

bool
Rsp::v_packet(char* data, size_t len) {
  if (strncmp ("vKill", data, strlen ("vKill")) == 0)
  {
    return reset(0);
  }
  else if (strncmp ("vCont?", data, strlen ("vCont?")) == 0)
  {
    return this->send_str("");
  }
  else if (strncmp ("vCont", data, strlen ("vCont")) == 0)
  {
    bool threadsCmd[m_dbgifs.size()];
    for (std::size_t i=0; i<m_dbgifs.size(); i++) 
      threadsCmd[i] = false;

    // vCont can contains several commands, handle them in sequence
    char *str = strtok(&data[6], ";");
    while(str != NULL) {
      // Extract command and thread ID
      char *delim = index(str, ':');
      int tid = -1;
      if (delim != NULL) {
        tid = atoi(delim+1);
        *delim = 0;
      }

      bool cont = false;
      bool step = false;

      if (str[0] == 'C' || str[0] == 'c') {
        cont = true;
        step = false;
      } else if (str[0] == 'S' || str[0] == 's') {
        cont = true;
        step = true;
      } else {
        fprintf(stderr, "Unsupported command in vCont packet: %s\n", str);
        exit(-1);
      }

      if (cont) {
        if (tid == -1) {
          for (std::size_t i=0; i<m_dbgifs.size(); i++) {
            if (!threadsCmd[i]) resumeCoresPrepare(this->get_dbgif(i), step);
          }
        } else {
          if (!threadsCmd[tid]) this->resumeCoresPrepare(this->get_dbgif(tid), step);
          threadsCmd[tid] = true;
        }
      }

      str = strtok(NULL, ";");
    }

    this->resumeCores();

    return this->waitStop(NULL);
  }

  // The proper response to an unknown v packet is the empty string, cf.
  // https://sourceware.org/gdb/onlinedocs/gdb/Packets.html
  if (strncmp("vMustReplyEmpty", data, strlen("vMustReplyEmpty")) != 0)
    fprintf(stderr, "Unknown v packet: %.*s\n", (int)len, data);
  return this->send_str("");
}

bool
Rsp::regs_send() {
  uint32_t gpr[32];
  uint32_t npc;
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

  if (sscanf(data, "%" SCNx32, &addr) != 1) {
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
  DbgIF* dbgif;

  if (sscanf(data, "%" SCNx32 "=%08" SCNx32, &addr, &wdata) != 2) {
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
      fprintf(stderr, "RSP: Error receiving start bit: %s\n",
              ret == 0 ? "Connection reset by peer" : strerror(errno));
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
      fprintf(stderr, "RSP: Error receiving payload: %s\n",
              ret == 0 ? "Connection reset by peer" : strerror(errno));
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
    fprintf(stderr, "RSP: Error receiving checksum[0]: %s\n",
            ret == 0 ? "Connection reset by peer" : strerror(errno));
    return false;
  }

  ret = recv(m_socket_client, &check_chars[1], 1, 0);
  if((ret == -1 && errno != EWOULDBLOCK) || (ret == 0)) {
    fprintf(stderr, "RSP: Error receiving checksum[1]: %s\n",
            ret == 0 ? "Connection reset by peer" : strerror(errno));
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
Rsp::send_signal(enum target_signal signal) {
  if (signal >= TARGET_SIGNAL_LAST)
    return false;

  char str[4];
  int len = snprintf(str, 4, "S%02x", signal);
  return (len == 3) && this->send(str, len);
}

bool
Rsp::send_stop_reason() {
  uint32_t cause;
  uint32_t hit;
  enum target_signal signal;
  DbgIF* dbgif = this->get_dbgif(m_thread_sel);

  // FIXME: why, and why here?
  dbgif->write(DBG_IE_REG, 0xFFFF); // Make all debug interrupts cause traps

  // Figure out why we are stopped
  if (!dbgif->read(DBG_HIT_REG, &hit))
    return false;
  if (!dbgif->read(DBG_CAUSE_REG, &cause))
    return false;

  bool irq = cause & (1 << 31);
  cause &= 0x1F;

  if (irq) // Interrupt
    signal = TARGET_SIGNAL_INT;
  else if (hit & 0x01) // Single step
    signal = TARGET_SIGNAL_TRAP;
  else if (cause == CAUSE_ILLEGAL_INSN)
    signal = TARGET_SIGNAL_ILL;
  else if (cause == CAUSE_BREAKPOINT)
    signal = TARGET_SIGNAL_TRAP;
  else if (cause == CAUSE_ECALL_UMODE)
    signal = TARGET_SIGNAL_TRAP;
  else if (cause == CAUSE_ECALL_MMODE)
    signal = TARGET_SIGNAL_TRAP;
  else {
    printf("ERROR: could not determine the reason why we are stopped.\n");
    signal = TARGET_SIGNAL_NONE;
  }

  return this->send_signal(signal);
}

bool
Rsp::send(const char* data, size_t len) {
  int ret;
  size_t raw_len = 0;
  char* raw = (char*)malloc(len * 2 + 4);
  unsigned int checksum = 0;

  raw[raw_len++] = '$';

  for (size_t i = 0; i < len; i++) {
    char c = data[i];

    // check if escaping needed
    if (c == '#' || c == '%' || c == '}' || c == '*') {
      raw[raw_len++] = '}';
      raw[raw_len++] = c;
      checksum += '}';
      checksum += c;
    } else {
      raw[raw_len++] = c;
      checksum += c;
    }
  }

  // add checksum
  checksum = checksum % 256;
  char checksum_str[3];
  snprintf(checksum_str, 3, "%02x", checksum);

  raw[raw_len++] = '#';
  raw[raw_len++] = checksum_str[0];
  raw[raw_len++] = checksum_str[1];

  char ack;
  do {
    log->debug("Sending %.*s\n", raw_len, raw);

    if ((size_t)::send(m_socket_client, raw, raw_len, 0) != raw_len) {
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
  bool irq = cause & (1 << 31);
  cause &= 0x1F;

  // Correct PC for SW interrupts etc. FIXME: document
  if (hit & 0x1)
    *pc = npc;
  else if (irq)
    *pc = npc;
  else if (cause == CAUSE_BREAKPOINT)
    *pc = ppc;
  else if (cause == CAUSE_ILLEGAL_INSN)
    *pc = ppc;
  else
    *pc = npc;

  return true;
}

bool
Rsp::waitStop(DbgIF* dbgif) {
  int ret;
  char pkt;

  fd_set rfds;
  struct timeval tv;

  while(1) {

    //First check if one core has stopped
    if (dbgif) {
      if (dbgif->is_stopped()) {
        return this->send_stop_reason();
      }
    } else {
      for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
        if ((*it)->is_stopped()) {
          return this->send_stop_reason();
        }
      }
    }

    // Otherwise wait for a stop request from gdb side for a while

    FD_ZERO(&rfds);
    FD_SET(m_socket_client, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 100 * 1000;

    if (select(m_socket_client+1, &rfds, NULL, NULL, &tv)) {
      ret = recv(m_socket_client, &pkt, 1, 0);
      if (ret == 1 && pkt == 0x3) {
        if (dbgif) {
          if (!dbgif->halt()) {
            printf("ERROR: failed sending halt\n");
          }

          if (!dbgif->is_stopped()) {
            printf("ERROR: failed to stop core\n");
            return false;
          }

          return this->send_signal(TARGET_SIGNAL_INT);
        } else {
          for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
            if (!(*it)->halt()) {
              printf("ERROR: failed sending halt\n");
            }

            if (!(*it)->is_stopped()) {
              printf("ERROR: failed to stop core\n");
              return false;
            }
          }
          return this->send_signal(TARGET_SIGNAL_INT);
        }
      }
    }
  }

  return true;
}

void
Rsp::resumeCore(DbgIF* dbgif, bool step) {
  uint32_t hit;
  uint32_t cause;
  uint32_t ppc;
  uint32_t npc;

  // now let's handle software breakpoints

  dbgif->read(DBG_PPC_REG, &ppc);
  dbgif->read(DBG_NPC_REG, &npc);
  dbgif->read(DBG_HIT_REG, &hit);
  dbgif->read(DBG_CAUSE_REG, &cause);


  // Reset single step trace hit flag before any further steps via CTRL
  dbgif->write(DBG_HIT_REG, 0);
  dbgif->write(DBG_CTRL_REG, step); // Exit debug mode
}

void
Rsp::resumeCoresPrepare(DbgIF *dbgif, bool step) {

  uint32_t hit;
  uint32_t cause;
  uint32_t ppc;
  uint32_t npc;

  // now let's handle software breakpoints

  dbgif->read(DBG_PPC_REG, &ppc);
  dbgif->read(DBG_NPC_REG, &npc);
  dbgif->read(DBG_HIT_REG, &hit);
  dbgif->read(DBG_CAUSE_REG, &cause);

  // if there is a breakpoint at this address, let's remove it and single-step over it
  bool hasStepped = false;

  log->debug("Preparing core to resume (step: %d, ppc: 0x%x)\n", step, ppc);

  if (m_bp->at_addr(ppc)) {
    log->debug("Core is stopped on a breakpoint, stepping to go over (addr: 0x%x)\n", ppc);

    m_bp->disable(ppc);
    dbgif->write(DBG_NPC_REG, ppc); // re-execute this instruction
    dbgif->write(DBG_CTRL_REG, 0x1); // single-step
    while (1) {
      uint32_t value;
      dbgif->read(DBG_CTRL_REG, &value);
      if ((value >> 16) & 1) break;
    }
    m_bp->enable(ppc);
    hasStepped = true;
  }

  if (!step || !hasStepped) {
    // clear hit register, has to be done before CTRL
    dbgif->write(DBG_HIT_REG, 0);

    if (step)
      dbgif->write(DBG_CTRL_REG, (1<<16) | 0x1);
    else
      dbgif->write(DBG_CTRL_REG, (1<<16) | 0);
  }
}

void
Rsp::resumeCores() {
  if (m_dbgifs.size() == 1) {
    uint32_t value;
    this->get_dbgif(0)->read(DBG_CTRL_REG, &value);
    this->get_dbgif(0)->write(DBG_CTRL_REG, value & ~(1<<16));
  } else {
    uint32_t info = 0xFFFFFFFF;
    m_mem->access(1, 0x10200028, 4, (char*)&info);
  }
}

bool
Rsp::resume(bool step) {
  if (m_dbgifs.size() == 1) {
    DbgIF *dbgif = this->get_dbgif(m_thread_sel);

    resumeCore(dbgif, step);

    return waitStop(dbgif);
  } else {
    for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
      resumeCoresPrepare(*it, step);
    }
    resumeCores();
    return waitStop(NULL);
  }
}

bool
Rsp::resume(int tid, bool step) {
  DbgIF *dbgif = this->get_dbgif(tid);

  resumeCore(dbgif, step);

  return waitStop(dbgif);
}

bool
Rsp::mem_read(char* data, size_t len) {
  char buffer[512];
  char reply[512];
  uint32_t addr;
  unsigned int length;
  uint32_t rdata;

  if (sscanf(data, "%" SCNx32 ",%" SCNx32, &addr, &length) != 2) {
    fprintf(stderr, "Could not parse packet\n");
    return false;
  }

  m_mem->access(0, addr, length, buffer);

  for(unsigned int i = 0; i < length; i++) {
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
  unsigned int i, j;

  char* buffer;
  int buffer_len;

  if (sscanf(data, "%" SCNx32 ",%zu:", &addr, &length) != 2) {
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
  unsigned int i;

  if (sscanf(data, "%" SCNx32 ",%zx:", &addr, &length) != 2) {
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
  int bp_len;

  if (3 != sscanf(data, "Z%1d,%" SCNx32 ",%1d", (int *)&type, &addr, &bp_len)) {
    fprintf(stderr, "Could not get three arguments\n");
    return false;
  }

  if (type != BP_MEMORY) {
    fprintf(stderr, "Error: tried to insert an unsupported breakpoint of type %d\n", type);
    // The proper response to an unsupported break point is the empty string, cf.
    // https://sourceware.org/gdb/onlinedocs/gdb/Packets.html
    return this->send_str("");
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
    fprintf(stderr, "Error: tried to remove an unsupported breakpoint of type %d\n", type);
    // The proper response to an unsupported break point is the empty string, cf.
    // https://sourceware.org/gdb/onlinedocs/gdb/Packets.html
    return this->send_str("");
  }

  m_bp->remove(addr);

  // check if we are currently on this bp that is removed
  dbgif->read(DBG_PPC_REG, &ppc);
  if (addr == ppc) {
    dbgif->write(DBG_NPC_REG, ppc); // re-execute the original instruction next
  }

  return this->send_str("OK");
}

DbgIF*
Rsp::get_dbgif(unsigned int thread_id) {
  for (std::list<DbgIF*>::iterator it = m_dbgifs.begin(); it != m_dbgifs.end(); it++) {
    if ((*it)->get_thread_id() == thread_id)
      return *it;
  }

  return NULL;
}

bool
Rsp::encode_hex(const char *in, char *out, size_t out_len) {
  size_t in_len = strlen(in);
  // This check guarantees that for every non-\0 character in the input 
  // there are two bytes available in the out buffer + one for trailing \0.
  if (2*in_len - 1 > out_len) {
    fprintf(stderr, "%s: output buffer too small (need %zu, have %zu)\n", 
      __func__, 2*in_len - 1, out_len);
    return false;
  }
  size_t i = 0;
  while (i < in_len && in[i] != '\0') {
    sprintf(&out[2*i],"%02x", in[i]);
    i++;
  }
  out[2*i] = '\0';
  return true;
}
