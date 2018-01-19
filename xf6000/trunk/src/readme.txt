main.c 调用参考
vc_main_pc_demo.cpp  调用参考
vc_ctrl_api.h    接口定义
vc_ctrl_api.c 接口实现

vc_i2c_command.h I2C命令发送
vc_i2c_command.c I2C命令发送


vc_i2c_io_interface.h 用户需要实现的接口定义。跟据平台的I2C驱动实现此文件中的接口
vc_i2c_io_interface.c 用户需要实现的接口示例
icI2C.c 某平台I2C实现参考。用户需要按照自己平台的I2C模块实现这些函数。
vc_i2c_io_sample/vc_i2c_io_sample_gpio.c  GPIO模块I2C实现示例。


补充说明VCChangeWorkMode接口的工作模式的选择：
如果麦克风输入的电压的有效值在0.05v左右，选择外挂模式WORK_MODE_PERIPHERAL；
如果麦克风输入的电压的有效值在0.5v左右，选择顶灯模式 WORK_MODE_TOPLIGHT；



