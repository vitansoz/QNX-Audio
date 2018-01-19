#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <hw/i2c.h>
#include "icI2C.h"

#define MSG(x...) printf(x)

#define I2C_DEV_BUS   "/dev/i2c4"
#define I2C_DEV_ADR   0x47
#define I2C_DEV_SPEED 100000

#define MAX_PACKET_SIZE 96

typedef struct {
  i2c_send_t head;
  unsigned char buf[MAX_PACKET_SIZE];
} xf_send_t;

typedef struct {
  i2c_sendrecv_t head;
  unsigned char buf[MAX_PACKET_SIZE];
} xf_sendrecv_t;


static int xf_fd;

int icI2C_Init ( void )
{
  int ret;
  int speed = I2C_DEV_SPEED;

  if ( ( xf_fd = open ( I2C_DEV_BUS, O_RDWR ) ) < 0 ) {
    MSG ( "open fail %d\n", xf_fd );
    return -1;
  } else {
    MSG ( "%s open success with fd:%d\n", I2C_DEV_BUS, xf_fd );
  }

  if ( ret = devctl ( xf_fd, DCMD_I2C_SET_BUS_SPEED, &speed, sizeof ( speed ), NULL ) ) {
    MSG ( "Set bus speed fail %d\n", ret );
    return -1;
  }

  return 0;
}

int icI2C_Send ( unsigned char ucDevAddr, int register_address, const unsigned char*buffer, int size )
{
  int ret;
  xf_send_t send_data;

  send_data.head.slave.fmt = I2C_ADDRFMT_7BIT;
  send_data.head.slave.addr = I2C_DEV_ADR;
  send_data.head.len = size + 2;
  send_data.head.stop = 1;
  send_data.buf[0] = ( unsigned char ) ( ( register_address >> 8 ) & 0xff );
  send_data.buf[1] = ( unsigned char ) ( ( register_address ) & 0xff );

  memcpy ( &send_data.buf[2], buffer, size );

  if ( ret = devctl ( xf_fd, DCMD_I2C_SEND, &send_data, sizeof ( send_data ), NULL ) ) {
    MSG ( "Fail to ioctl for DCMD_I2C_SEND, errno=%d\n", ret );
    return ret;
  }

  return 1;
}

int icI2C_Recv ( unsigned char ucDevAddr, int register_address, unsigned char*buffer, int size )
{

  int i;
  int ret;
  xf_sendrecv_t sendrecv_data;

  sendrecv_data.head.slave.fmt = I2C_ADDRFMT_7BIT;
  sendrecv_data.head.slave.addr = I2C_DEV_ADR;
  sendrecv_data.head.send_len = 2;
  sendrecv_data.head.recv_len = size;
  sendrecv_data.head.stop = 1;
  sendrecv_data.buf[0] = ( unsigned char ) ( ( register_address >> 8 ) & 0xff );
  sendrecv_data.buf[1] = ( unsigned char ) ( ( register_address ) & 0xff );

  if ( ret = devctl ( xf_fd, DCMD_I2C_SENDRECV, &sendrecv_data, sizeof ( sendrecv_data ), NULL ) ) {
    MSG ( "Fail to ioctl for DCMD_I2C_SENDRECV, errno=%d\n", ret );
    return ret;
  }

  for ( i = 0; i < size; i++ ) {
    buffer[i] = sendrecv_data.buf[i] ;
  }

  return 1;
}
