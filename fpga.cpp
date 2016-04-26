#include "mem.h"

#include <byteswap.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#define SPIDEV               "/dev/spidev32766.0"

int g_spi_fd;


bool sim_mem_open() {
  bool retval = true;
  // open spidev
  g_spi_fd = open(SPIDEV, O_RDWR);
  if (g_spi_fd <= 0) {
    perror("Device not found\n");

    retval = false;
    goto fail;
  }

  printf("Mem connected!");

  return true;

fail:
  // close spidev
  close(g_spi_fd);

  return retval;
}

bool sim_mem_write(uint32_t addr, uint8_t be, uint32_t wdata) {
  char wr_buf[9];

  wr_buf[0] = 0x02; // write command
  // address
  wr_buf[1] = addr >> 24;
  wr_buf[2] = addr >> 16;
  wr_buf[3] = addr >> 8;
  wr_buf[4] = addr;
  wr_buf[5] = wdata >> 24;
  wr_buf[6] = wdata >> 16;
  wr_buf[7] = wdata >> 8;
  wr_buf[8] = wdata;

  // write to spidev
  if (write(g_spi_fd, wr_buf, 9) != 9) {
    perror("Write Error");

    return false;
  }

  return true;
}

bool sim_mem_read(uint32_t addr, uint32_t *rdata) {
  char wr_buf[256];
  char rd_buf[256];
  int i;

  struct spi_ioc_transfer* transfer = (struct spi_ioc_transfer*)malloc(sizeof(struct spi_ioc_transfer));

  memset(transfer, 0, sizeof(struct spi_ioc_transfer));

  transfer->tx_buf = (unsigned long)wr_buf;
  transfer->rx_buf = (unsigned long)rd_buf;
  transfer->len    = 14;

  memset(wr_buf, 0, transfer->len);
  memset(rd_buf, 0, transfer->len);

  wr_buf[0] = 0x0B; // read command
  // address
  wr_buf[1] = addr >> 24;
  wr_buf[2] = addr >> 16;
  wr_buf[3] = addr >> 8;
  wr_buf[4] = addr;

  // check if write was sucessful
  if (ioctl(g_spi_fd, SPI_IOC_MESSAGE(1), transfer) < 0) {
    perror("SPI_IOC_MESSAGE");
    return false;
  }

  // shift everything by one bit
  for(i = 0; i < transfer->len-1; i++) {
    rd_buf[i] = (rd_buf[i] << 1) | ((rd_buf[i+1] & 0x80) >> 7);
  }

  *rdata = htonl(*((int*)&rd_buf[9]));

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


