#include <unistd.h>
#include "vc_i2c_io_interface.h"
#include "icI2C.h"
//#include "hw_api.h"

#define I2CADDR (0x47 << 1)

/************************************************************************************
intermediate function
*************************************************************************************/
/*------------------------------------------------------------------------------
  Function: VCI2CWrite

  Purpose: 用户实现的接口函数,功能为向I2C总线按照固定的时序写数据,时序如下图：

  |START|0x8E(8bit)|ACK|register_address高8位|ACK|register_address低8位|ACK|buffer[0]|ACK|buffer[1]|ACK|...|buffer[size-1]|ACK|STOP|

  Inputs: register_address - 16bit的控制地址
          buffer - 写入I2C总线数据的存储地址
          size - 写入I2C总线数据的长度, 1 for Byte

  Outputs: return - succeed : 1;
                    failed  : 0;
------------------------------------------------------------------------------*/
int VCI2CWrite ( int register_address, unsigned char *buffer, int size )
{
  return icI2C_Send ( I2CADDR, register_address, buffer, size );
}


/*------------------------------------------------------------------------------
  Function: VCI2CRead

  Purpose: 用户实现的接口函数,功能为从I2C总线按照固定的时序读数据,时序如下图：

  |START|0x8E(8bit)|ACK|register_address高8位|ACK|register_address低8位|ACK|START|0x8F(8bit)|ACK|buffer[0]|ACK|buffer[1]|ACK|...|buffer[size-1]|ACK|STOP|

  Inputs: register_address - 16bit的控制地址
          buffer - 从I2C总线读取数据的存储地址
          size - 从I2C总线读取数据的长度, 1 for Byte

  Outputs: return - succeed : 1;
                    failed  : 0;
------------------------------------------------------------------------------*/
int VCI2CRead ( int register_address, unsigned char*buffer, int size )
{
  return icI2C_Recv ( I2CADDR, register_address, buffer, size );
}


/*------------------------------------------------------------------------------
  Function: VCI2CSleep

  Purpose: The I2C driver layer interface which user need to develop.This function
           just call the sleep fun on host.

  Inputs: intreval_ms - time for sleep, 1 for 1 millisecond

  Outputs: return - void
------------------------------------------------------------------------------*/
void VCI2CSleep ( int intreval_ms )
{
  usleep(intreval_ms*1000);
}
