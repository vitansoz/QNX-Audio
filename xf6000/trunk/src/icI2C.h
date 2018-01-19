#ifndef ICI2C_H_
#define ICI2C_H_

int icI2C_Init ( void );
int icI2C_Send ( unsigned char ucDevAddr, int register_address, const unsigned char*buffer, int size );
int icI2C_Recv ( unsigned char ucDevAddr, int register_address, unsigned char*buffer, int size );

#endif