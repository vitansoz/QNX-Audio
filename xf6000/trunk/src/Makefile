xf6000_driver:main.o vc_ctrl_api.o vc_i2c_command.o vc_i2c_io_interface.o icI2C.o
	qcc -Vgcc_ntoarmv7le -o xf6000_driver main.o vc_ctrl_api.o vc_i2c_command.o vc_i2c_io_interface.o icI2C.o
	cp xf6000_driver ../bin/
main.o:main.c
	qcc -Vgcc_ntoarmv7le -c main.c
vc_ctrl_api.o:vc_ctrl_api.c
	qcc -Vgcc_ntoarmv7le -c vc_ctrl_api.c
vc_i2c_command.o:vc_i2c_command.c
	qcc -Vgcc_ntoarmv7le -c vc_i2c_command.c
vc_i2c_io_interface.o:vc_i2c_io_interface.c
	qcc -Vgcc_ntoarmv7le -c vc_i2c_io_interface.c
icI2C.o:icI2C.c
	qcc -Vgcc_ntoarmv7le -c icI2C.c
clean:
	rm xf6000_driver main.o vc_ctrl_api.o vc_i2c_command.o vc_i2c_io_interface.o icI2C.o

