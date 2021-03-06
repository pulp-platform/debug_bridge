#ifndef RSP_H
#define RSP_H

#include "mem.h"
#include "debug_if.h"
#include "breakpoints.h"

#include <list>
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


class Rsp {
  public:
    Rsp(int socket_port, MemIF* mem, LogIF *log, std::list<DbgIF*> list_dbgif, BreakPoints* bp);

    bool open();
    void close();

    bool wait_client();
    bool loop();

  private:
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
      TARGET_SIGNAL_PWR  = 32,

      TARGET_SIGNAL_LAST,
    };

    bool decode(char* data, size_t len);

    bool multithread(char* data, size_t len);

    bool cont(char* data, size_t len); // continue, reserved keyword, thus not used as function name
    bool step(char* data, size_t len);

    bool query(char* data, size_t len);
    bool monitor(char* data, size_t len);
    bool v_packet(char* data, size_t len);
    bool ctrlc(void); // break, reserved keyword, thus not used as function name

    bool regs_send();
    bool reg_read(char* data, size_t len);
    bool reg_write(char* data, size_t len);

    bool get_packet(char* data, size_t* len);


    bool send(const char* data, size_t len);
    bool send_stop_reason();
    bool send_signal(enum target_signal signal);
    bool send_str(const char* data);
    // internal helper functions
    bool pc_read(unsigned int* pc);

    bool waitStop(DbgIF* dbgif);
    bool resume(bool step);
    bool resume(int tid, bool step);
    void resumeCore(DbgIF* dbgif, bool step);
    void resumeCoresPrepare(DbgIF *dbgif, bool step);
    void resumeCores();

    bool mem_read(char* data, size_t len);
    bool mem_write_ascii(char* data, size_t len);
    bool mem_write(char* data, size_t len);

    bool bp_insert(char* data, size_t len);
    bool bp_remove(char* data, size_t len);

    bool reset(bool halt);

    bool monitor_help(char *str, size_t len);

    bool encode_hex(const char *in, char *out, size_t out_len);

    DbgIF* get_dbgif(unsigned int thread_id);

    int m_socket_port;
    int m_socket_in;
    int m_socket_client;

    int m_thread_sel;
    MemIF* m_mem;
    LogIF *log;
    BreakPoints* m_bp;
    std::list<DbgIF*> m_dbgifs;
};

#endif
