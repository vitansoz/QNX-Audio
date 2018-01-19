/*------------------------------------------------------------------------------

  File Name:  vc_i2c_io_interface.h

Description:  voice clean 的使用者需要在主控设备上实现的三个接口函数，包括i2c I/O函数
和sleep函数，由voice clean ctrl api调用，主要功能是按照固定的时序，读/写i2c总线和睡眠

------------------------------------------------------------------------------*/
#ifndef VOICE_CLEAN_IIC_IO_INTERFACE_H_
#define VOICE_CLEAN_IIC_IO_INTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif


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
int VCI2CWrite ( int register_address, unsigned char *buffer, int size );


/*------------------------------------------------------------------------------
  Function: VCI2CRead

  Purpose: 用户实现的接口函数,功能为从I2C总线按照固定的时序读数据,时序如下图：

  |START|0x8E(8bit)|ACK|register_address高8位|ACK|register_address低8位|ACK|START|0x8F(8bit)|ACK|register_address高8位|ACK|register_address低8位|ACK|buffer[0]|ACK|buffer[1]|ACK|...|buffer[size-1]|ACK|STOP|


  Inputs: register_address - 16bit的控制地址
          buffer - 从I2C总线读取数据的存储地址
          size - 从I2C总线读取数据的长度, 1 for Byte

  Outputs: return - succeed : 1;
                    failed  : 0;
------------------------------------------------------------------------------*/
int VCI2CRead ( int register_address, unsigned char*buffer, int size );


/*------------------------------------------------------------------------------
  Function: VCI2CSleep

  Purpose: The I2C driver layer interface which user need to develop.This function
           just call the sleep fun on host.

  Inputs: intreval_ms - time for sleep, 1 for 1 millisecond

  Outputs: return - void
------------------------------------------------------------------------------*/
void VCI2CSleep ( int intreval_ms );
#ifdef __cplusplus
}
#endif
#endif  // VOICE_CLEAN_IIC_IO_INTERFACE_H_

