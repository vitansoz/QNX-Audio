#include "tef6638.h"
#include <math.h>
#define AUDIO_X_ADDR_SEG                              0xF20000UL
#define AUDIO_Y_ADDR_SEG                              0xF24000UL
#define AUDIO_ADDR_MASK                               0xFFC000UL
#define AUDIO_REL_ADDR(reg)                           (reg & ~AUDIO_ADDR_MASK)
#define MAX_PACKET_SIZE 200

static void order_1st_l_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_1st_h_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_1st_shlv_l_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_1st_shlv_h_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_2nd__peak_notch(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_2nd_shlv(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_2nd_peak(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_2nd_l_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_2nd_h_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_3rd_l_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_3rd_h_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_4th_l_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_4th_h_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_6th_l_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_6th_h_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_8th_l_pass(filter_param_t* param, filter_coeffients_t* coefficents);
static void order_8th_h_pass(filter_param_t* param, filter_coeffients_t* coefficents);

int 
audio_calc_x ( double val )
{
  int data =  val * 0x800000  + (  val < 0  ?  -0.5  : 0.5 );

  if ( data < -0x800000 )
    data = -0x800000;
  else if ( data > 0x7FFFFF )
    data = 0x7FFFFF;

  return data ;
}

short int 
audio_calc_y ( double val )
{
  int data =  val * 0x800  + (  val < 0  ?  -0.5  : 0.5 );

  if ( data < -0x800 )
    data = -0x800;
  else if ( data > 0x7FF )
    data = 0x7FF;

  return data & 0x00000FFF;
}

int 
tef6638_recv ( int fd,  uint8_t* data, int data_num, uint8_t*  buf, int size, uint8_t slave_addr)
{
  	struct {
  		i2c_sendrecv_t head;
  		char buf[MAX_PACKET_SIZE];
	}  sendrecv_data;
  	int err = 0;

  	sendrecv_data.head.slave.fmt = I2C_ADDRFMT_7BIT;
  	sendrecv_data.head.slave.addr = slave_addr;
  	sendrecv_data.head.send_len = data_num;
  	sendrecv_data.head.recv_len = data_num;
  	sendrecv_data.head.stop = 1;
  	memcpy(sendrecv_data.buf,data,data_num);

  	if ( (err = devctl( fd, DCMD_I2C_SENDRECV, &sendrecv_data, sizeof ( sendrecv_data ), NULL ) )) {
   	 	ado_error("Failed to read from codec: %s\n", strerror(errno));
   
  	}

  	memcpy(buf,sendrecv_data.buf,size);

  	return err;
}
int 
tef6638_send(int fd,  uint8_t* data, int data_num, uint8_t slave_addr)
{
	  int err = 0 ;

	  struct {
  		i2c_send_t head;
  		char buf[MAX_PACKET_SIZE];
	  }  send_data;

	  send_data.head.slave.fmt = I2C_ADDRFMT_7BIT;
	  send_data.head.slave.addr = slave_addr;
	  send_data.head.len = data_num;
	  send_data.head.stop = 1;
	  memcpy(send_data.buf,data,data_num);

	  if ((err = devctl( fd, DCMD_I2C_SEND, &send_data, sizeof ( send_data ), NULL ) )) 
	  {
    		 ado_error("Failed to write to codec: %s\n", strerror(errno));
      }

    return err;

}

int  
tef6638_write_x_reg(int fd, const int addr,const int *data,int data_num,uint8_t slave_addr)
{
	
	ado_error("tef6638_writex_reg reg=%x data=%x , slave_addr = %x", addr, *data, slave_addr);
	ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, *data, slave_addr);
	if ( ( ( addr & AUDIO_ADDR_MASK ) != AUDIO_X_ADDR_SEG ) || ( data_num < 1 ) || ( data_num > 8 ) || ( data == NULL ) ) 
	{
    	ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%d), slave_addr = %x", addr, data, data, slave_addr);
    	return -EINVAL;
  	}
 
  	unsigned char buf[data_num * 3 + 3], *buf_ptr = buf;

  	*buf_ptr++ = addr >> 16;
  	*buf_ptr++ = addr >> 8;
  	*buf_ptr++ = addr;

  	while ( data_num-- ) 
  	{
   	 	*buf_ptr++ = *data >> 16;
    	*buf_ptr++ = *data >> 8;
    	*buf_ptr++ = *data++;
  	}

  	return tef6638_send ( fd, buf, buf_ptr - buf,slave_addr) ;


}

int 
tef6638_read_x_reg ( int fd,const int addr, int *data, int data_num ,uint8_t slave_addr)
{
  	int err = 0;
  	if ( ( ( addr & AUDIO_ADDR_MASK ) != AUDIO_X_ADDR_SEG ) || ( data_num < 1 ) || ( data_num > 84 ) || ( data == NULL ) ) 
  	{
    	ado_debug(DB_LVL_MIXER, "tef6638_read_x_reg");
    	return -EINVAL;
  	}

  	int cnt = data_num * 3;
  	unsigned char sub_addr[3], buf[cnt], *buf_ptr = buf;

  	sub_addr[2] = addr;
  	sub_addr[1] = addr >> 8;
  	sub_addr[0] = addr >> 16;

  	if ( (err = tef6638_recv ( fd,sub_addr, 3, buf, cnt,slave_addr )) ) 
  	{
	 	return err;
  	} 
	else 
	{
  	  	while ( data_num-- ) 
	  	{
      		cnt = ( *buf_ptr++ ) << 16;
      		cnt += ( *buf_ptr++ ) << 8;
      		*data++ = cnt + ( *buf_ptr++ );
    	}
  	}

  	return err;
}

int 
tef6638_read_y_reg (int fd, const int addr, short int *data, int data_num ,uint8_t slave_addr )
{
	int err = 0;
	
  if ( ( ( addr & AUDIO_ADDR_MASK ) != AUDIO_Y_ADDR_SEG ) || ( data_num < 1 ) || ( data_num > 126 ) || ( data == NULL ) ) {
    ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, *data, slave_addr);
    return -EINVAL;
  }

  int cnt = data_num * 2;
  unsigned char sub_addr[3], buf[cnt], *buf_ptr = buf;

  sub_addr[2] = addr;
  sub_addr[1] = addr >> 8;
  sub_addr[0] = addr >> 16;

  if ( (err = tef6638_recv ( fd, sub_addr, 3, buf, cnt, slave_addr )) ) {  
    return err;
  } else {
	  while ( data_num-- ) {
			cnt = ( *buf_ptr++ ) << 8;
			*data++ = cnt + ( *buf_ptr++ );
	  }

  }

	return err;
}

int 
tef6638_write ( int fd , const char addr, const char *data, const int data_num, uint8_t slave_addr)
{
	
  if ( ( data_num < 1 ) || ( data == NULL ) ) {
    ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, *data, slave_addr);
    return -EINVAL;
  }

  unsigned char buf[data_num + 1];
  buf[0] = addr;
  memcpy ( &buf[1], data, data_num );

  return  tef6638_send (fd, buf, data_num + 1,slave_addr ) ;
}

int 
tef6638_write_y_reg ( int fd, const int addr, short int *data, int data_num , uint8_t slave_addr)
{
	ado_error("tef6638_writey_reg reg=%x data=%x, slave_addr = %x", addr, *data, slave_addr);
  if ( ( ( addr & AUDIO_ADDR_MASK ) != AUDIO_Y_ADDR_SEG ) || ( data_num < 1 ) || ( data_num > 12 ) || ( data == NULL ) ) {
    ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, *data, slave_addr);
    return -EINVAL;
  }

  unsigned char buf[data_num * 2 + 3], *buf_ptr = buf;

  *buf_ptr++ = addr >> 16;
  *buf_ptr++ = addr >> 8;
  *buf_ptr++ = addr;

  while ( data_num-- ) {
    *buf_ptr++ = *data >> 8;
    *buf_ptr++ = *data++;
  }

  return tef6638_send (fd, buf, buf_ptr - buf, slave_addr );
}

int 
tef6638_write_y_reg_scratch ( int fd, const int addr, short int *data, int data_num , uint8_t slave_addr)
{

  if ( ( ( addr & AUDIO_ADDR_MASK ) != AUDIO_Y_ADDR_SEG ) || ( data_num < 1 ) || ( data_num > 12 ) || ( data == NULL ) )
  {
    ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, data, slave_addr);
    return -EINVAL;
  }

  int val;
  if ( tef6638_read_x_reg ( fd,AUDIO_X_EasyP_Index, &val, 1 ,slave_addr) ) 
  {
    ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, data, slave_addr);
    return -EIO;
  }

  if ( val ) 
  {
    ado_error("not ready ");
    return -EAGAIN;
  }

  if ( tef6638_write_y_reg ( fd,AUDIO_Y_UpdatC_Coeff0, data, data_num,slave_addr ) ) 
  {
    ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, data, slave_addr);
    return -EIO;
  }

  val = addr & ( ~AUDIO_ADDR_MASK );
  if ( tef6638_write_x_reg ( fd,AUDIO_X_UpdatC_StartYaddr, &val, 1 ,slave_addr) ) 
  {
    ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, data, slave_addr);
    return -EIO;
  }

  if ( tef6638_write_x_reg ( fd,AUDIO_X_UpdatC_NrOfCoeff, &data_num, 1,slave_addr ) ) 
  {
    ado_debug(DB_LVL_MIXER, "DUMMY Codec write reg=%x data=%x (%x), slave_addr = %x", addr, data, data, slave_addr);
    return -EIO;
  }

  ado_debug( DB_LVL_MIXER, "\n!!!need wait a moment!!!\n" );
  return 0;
}

void 
filter_calculate(filter_param_t* param, filter_coeffients_t *coefficents)
{
	switch (param->type)
	{
	    case Flat_Response:
			coefficents->b0 = 0.5;
			coefficents->b1 = 0.0;
			coefficents->b2 = 0.0;
			coefficents->a1 = 0.0;
			coefficents->a2 = 0.0;
			break;
	    case First_Order_Low_Pass:
			order_1st_l_pass(param, coefficents);
			break;
	    case First_Order_High_Pass:
			order_1st_h_pass(param, coefficents);
			break;
	    case First_Order_Shelving_Low_Pass:
			order_1st_shlv_l_pass(param, coefficents);
			break;
	    case First_Order_Shelving_High_Pass:
			order_1st_shlv_h_pass(param, coefficents);
			break;
	    case Second_Order_Peak_Notch:
			order_2nd__peak_notch(param, coefficents);
			break;
	    case Second_Order_Shelving:
			order_2nd_shlv(param, coefficents);
			break;
	    case Second_Order_Peaking:
			order_2nd_peak(param, coefficents);
			break;
	    case Second_Order_Low_Pass:
			order_2nd_l_pass(param, coefficents);
			break;
	    case Second_Order_High_Pass:
			order_2nd_h_pass(param, coefficents);
			break;
	    case Third_Order_Low_Pass:
			order_3rd_l_pass(param, coefficents);
			break;
	    case Third_Order_High_Pass:
			order_3rd_h_pass(param, coefficents);
			break;
	    case Fourth_Order_Low_Pass:
			order_4th_l_pass(param, coefficents);
			break;
	    case Fourth_Order_High_Pass:
			order_4th_h_pass(param, coefficents);
			break;
	    case Sixth_Order_Low_Pass:
			order_6th_l_pass(param, coefficents);
			break;
	    case Sixth_Order_High_Pass:
			order_6th_h_pass(param, coefficents);
			break;
	    case Eighth_Order_Low_Pass:
			order_8th_l_pass(param, coefficents);
			break;
	    case Eighth_Order_High_Pass:
			order_8th_h_pass(param, coefficents);
			break;
		default:
			ado_error("***** filter type not support!!! *****\n");
			break;
	}
	return;
}


static void 
order_1st_l_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double g = 0.0, a1 = 0.0,a2 = 0.0,b0 = 0.0,b1 = 0.0,b2 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;
	if(fs == 0)
	{
		ado_error("invalid param input!!!\n");
		return;
	}
	if(G > 6)
		G = 6;
	else if(G < -12)
		G = -12;
	param->gain = G;
	g = pow(10,(G / (double)20));
	a1 = -0.5*tan(PI * ((double)fc/fs - 0.25));
    b0 = g * (0.25 - 0.5 * a1);
	b1 = b0;
	b2 = 0.0;
	a2 = 0.0;
	
	coefficents->a1 = a1;
	coefficents->a2 = a2;
	coefficents->b0 = b0;
	coefficents->b1 = b1;
	coefficents->b2 = b2;
	return;
}
static void
order_1st_h_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double g = 0.0, a1 = 0.0,a2 = 0.0,b0 = 0.0,b1 = 0.0,b2 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;
	if(fs == 0)
	{
		ado_error("invalid param input!!!\n");
		return;
	}
	if(G > 6)
		G = 6;
	else if(G < -12)
		G = -12;
	param->gain = G;
	g = pow(10,((double)G / 20.0));
	a1 = -0.5*tan(PI * ((double)fc/fs - 0.25));
    b0 = g * (0.25 + 0.5 * a1);
	b1 = -b0;
	b2 = 0.0;
	a2 = 0.0;
	
	coefficents->a1 = a1;
	coefficents->a2 = a2;
	coefficents->b0 = b0;
	coefficents->b0 = b0;
	coefficents->b1 = b1;
	coefficents->b2 = b2;
	return;


}
static void
order_1st_shlv_l_pass(filter_param_t *param, filter_coeffients_t *coefficents)
{
	double Gmax = 0.0, a1 = 0.0, a2 = 0.0,b0 = 0.0, b1 = 0.0, b2 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;
	if(fs == 0)
	{
		ado_error("invalid param input!!!\n");
		return;
	}

	Gmax = 20.0 * log(1.0 + 2.0/(1.0 + tan(PI * ((double)fc / fs - 0.25))));
	if(G > Gmax)
		G = Gmax;
	param->gain = G;
	
    a1 = -0.5 * tan(PI * ((double)fc/fs - 0.25));
	
		
	b0 = 0.25 - 0.5 * a1;
	b2 = 0.0;
	a2 = 0.0;
	b1 = b0;
	
	
	coefficents->a1 = a1;
	coefficents->a2 = a2;
	coefficents->b0 = b0;
	
	coefficents->b1 = b1;
	coefficents->b2 = b2;
	return;


}
static void
order_1st_shlv_h_pass(filter_param_t *param, filter_coeffients_t *coefficents)
{
    double Gmax = 0.0, g = 0.0, a1 = 0.0, a2 = 0.0,b0 = 0.0, b1 = 0.0, b2 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;
	if(fs == 0)
	{
		ado_error("invalid param input!!!\n");
		return;
	}

	Gmax = 20.0 * log(1.0 + 2.0/(1.0 + tan(PI * ((double)fc / fs - 0.25))));
	if( G > (int)Gmax)
	{
		G = (int)Gmax;
	    param->gain = G;
	}
	g = pow(10,((double)G / 20.0));
	if( G >= 0)
		a1 = -0.5 * tan(PI * ((double)fc/(double)fs - 0.25));
	else
		a1 = -0.5*tan(PI * (((double)(fc * g)/fs) - 0.25));
		
	b0 = 0.25 + 0.5 * a1;
	b1 = -b0;
	b2 = 0.0;
	a2 = 0.0;
	
	
	
	coefficents->a1 = a1;
	coefficents->a2 = a2;
	coefficents->b0 = b0;
	coefficents->b1 = b1;
	coefficents->b2 = b2;
	return;

}
static void
order_2nd__peak_notch(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double Gmax = 0.0, Q = param->q, beta = 0.0, t0= 0.0 , g = 0.0,Gb = 0.0, a1 = 0.0, a2 = 0.0,b0 = 0.0, b1 = 0.0, b2 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;

    
	t0 = 2.0 * PI * fc/(double)fs;
						
	/*gain setting cannot excess Gmax*/ 		
	Gmax = 20.0 * log( 2.0 + (2.0 * Q)/t0 );
			
	if( G > (int)Gmax)
	{
		G = (int)Gmax;
		param->gain = G;
	}
	g = pow(10,(double)G/20.0);
						
	if(g >= 1.0 )
	{
		beta = ((0.5 * t0) /Q);
	}
	else
	{
		beta=((0.5 * t0)/(g * Q));
	}
						
	a2 = (-0.5 * (1.0 - beta)/(1.0 + beta));
	a1 = (0.5 - a2) * cos(t0);
	b0 = (g - 1.0) * (0.25 + 0.5 * a2) + 0.5;
	b1 = -a1;
	b2 = -(g - 1.0)*(0.25 + 0.5 * a2) - a2;
	Gb = 0.25 * (g - 1.0);
    coefficents->a1 = a1;
	coefficents->a2 = a2;
	coefficents->b0 = b0;
	coefficents->b1 = b1;
	coefficents->b2 = b2;
	coefficents->Gb = Gb;
	return;

}
static void 
order_2nd_shlv(filter_param_t* param, filter_coeffients_t *coefficents)
{
	
     
   return;

	
}

static void
order_2nd_peak(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double Gmax = 0.0, Q = param->q, beta = 0.0, t0= 0.0 , a1 = 0.0, a2 = 0.0,b0 = 0.0, b1 = 0.0, b2 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;
	
		
		t0 = 2.0 * PI * fc/(double)fs;
							
		/*gain setting cannot excess Gmax*/ 		
		Gmax = 20 * log( 2.0 + (2.0 * Q)/t0 );
				
		if( G > (int)Gmax)
		{	
			G = (int)Gmax;
			param->gain = G;
		}	
							
		beta = ((0.5 * t0) /Q);
							
		a2 = (-0.5 * (1.0 - beta)/(1.0 + beta));
		a1 = (0.5 - a2) * cos(t0);
		b0 = (0.5 + a2) / 2.0;
		b1 = 0.0;
		b2 = -b0;
		coefficents->a1 = a1;
		coefficents->a2 = a2;
		coefficents->b0 = b0;
		coefficents->b1 = b1;
		coefficents->b2 = b2;
		return;

}
static void
order_2nd_l_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double   t0= 0.0 ,t1 = 0.0, g = 0.0, a1 = 0.0, a2 = 0.0,b0 = 0.0, b1 = 0.0, b2 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;

	if(G > 0)
		G = 0;
	else if(G < -12)
		G = -12;
	param->gain = G;
	g = pow(10,(double)G/20.0);
    t0 = tan(PI * fc / (double)fs);
	t1 = 1 + sqrt(2.0) * t0 + t0 * t0;
	
    a1 = (1.0 - t0 * t0) / t1;
	a2 = 0.5 * (sqrt(2.0) * t0 - 1.0 - t0*t0)/ t1;
    b0 = g * 0.5 * t0 * t0 / t1;
	b1 = 2.0 * b0;
	b2 = b0;
	coefficents->a1 = a1;
	coefficents->a2 = a2;
	coefficents->b0 = b0;
	coefficents->b1 = b1;
	coefficents->b2 = b2;
	return;

	
}
static void
order_2nd_h_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double   t0= 0.0 ,t1 = 0.0, g = 0.0, a1 = 0.0, a2 = 0.0,b0 = 0.0, b1 = 0.0, b2 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;

	if(G > 0)
		G = 0;
	else if(G < -12)
		G = -12;
	param->gain = G;

	g = pow(10,(double)G/20.0);
    t0 = tan(PI * fc / (double)fs);
	t1 = 1.0 + sqrt(2.0) * t0 + t0 * t0;
	
    a1 = (1.0 - t0 * t0) / t1;
	a2 = 0.5 * (sqrt(2.0) * t0 - 1.0 - t0*t0)/ t1;
    b0 = g * 0.5 / t1;
	b1 = -2.0 * b0;
	b2 = b0;
	coefficents->a1 = a1;
	coefficents->a2 = a2;
	coefficents->b0 = b0;
	coefficents->b1 = b1;
	coefficents->b2 = b2;
	return;	
	
}
static void
order_3rd_l_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double   t0= 0.0 ,t1 = 0.0, g = 0.0, a01 = 0.0, a02 = 0.0,b00 = 0.0, b01 = 0.0, b02 = 0.0,a11 = 0.0, a12 = 0.0,b10 = 0.0, b11 = 0.0, b12 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;

	if(G > 6)
		G = 6;
	if(G < -12)
		G = -12;
	param->gain = G;
	g = pow(10,(double)G/20.0);
    t0 = tan(PI * fc / (double)fs);
	t1 = 1.0 + t0 + t0 * t0;
	
    a01 = 0.5 * ( 1.0 - t0 ) / (1.0 + t0);
	a11 = (1.0- t0 * t0) / t1;
    a12 = 0.5 * (t0 - t0 * t0 -  1.0) /t1;
	b00 = 0.5 * g * t0/(t0 + 1.0);
	b01 = b00;
	b10 = 0.5 * t0 * t0 / t1;
	b11 = 2.0 * b10;
	b12 = b10;
	coefficents->a01 = a01;
	coefficents->a02 = a02;
	coefficents->a11 = a11;
	coefficents->a12 = a12;
	coefficents->b00 = b00;
	coefficents->b01 = b01;
	coefficents->b02 = b02;
	coefficents->b10 = b10;
	coefficents->b11 = b11;
	coefficents->b12 = b12;
	return;


}
static void
order_3rd_h_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double   t0= 0.0 ,t1 = 0.0, g = 0.0, a01 = 0.0, a02 = 0.0,b00 = 0.0, b01 = 0.0, b02 = 0.0,a11 = 0.0, a12 = 0.0,b10 = 0.0, b11 = 0.0, b12 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;

	if(G > 6)
		G = 6;
	if(G < -12)
		G = -12;
	param->gain = G;
	g = pow(10,(double)G/20.0);
    t0 = tan(PI * fc / (double)fs);
	t1 = 1.0 + t0 + t0 * t0;
	
    a01 = 0.5 * ( 1.0 - t0 ) / (1.0 + t0);
	a11 = (1.0- t0 * t0) / t1;
    a12 = 0.5 * (t0 - t0 * t0 -  1.0) /t1;
	b00 = 0.5 * g * t0/(t0 + 1.0);
	b01 = -b00;
	b10 = 0.5 / t1;
	b11 = -2.0 * b10;
	b12 = b10;
	coefficents->a01 = a01;
	coefficents->a02 = a02;
	coefficents->a11 = a11;
	coefficents->a12 = a12;
	coefficents->b00 = b00;
	coefficents->b01 = b01;
	coefficents->b02 = b02;
	coefficents->b10 = b10;
	coefficents->b11 = b11;
	coefficents->b12 = b12;
	return;


}
static void
order_4th_l_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double	t0= 0.0 ,t1 = 0.0,t2 = 0.0, g = 0.0, a01 = 0.0, a02 = 0.0,b00 = 0.0, b01 = 0.0, b02 = 0.0,a11 = 0.0, a12 = 0.0,b10 = 0.0, b11 = 0.0, b12 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;
	
		if(G > 0)
			G = 0;
		if(G < -12)
			G = -12;
		param->gain = G;
		g = pow(10,(double)G/20.0);
		t0 = tan(PI * fc / (double)fs);
		t1 = 1.0 + 2.0 * cos(0.125 * PI) * t0 + t0*t0;
		t2 = 1.0 + 2.0 * cos(0.375 * PI) * t0 + t0*t0;
		
		a01 =  (1.0 - t0 * t0) / t1;
		a02 = 0.5 * (t1 - 2.0 -  2.0 * t0 * t0) / t1;
        a11 = (1 - t0 * t0)/t2;
		a12 = 0.5 * (t2 - 2.0 - 2.0 * t0 * t0) / t1;
		b00 = 0.5 * t0 * t0 / t1;
		b01 = 2.0 * b00;
		b02 = b00;
		b10 = 0.5 * g * t0 * t0 / t2;
		b11 = 2.0 * b10;
		b12 = b10;
		
		coefficents->a01 = a01;
		coefficents->a02 = a02;
		coefficents->a11 = a11;
		coefficents->a12 = a12;
		coefficents->b00 = b00;
		coefficents->b01 = b01;
		coefficents->b02 = b02;
		coefficents->b10 = b10;
		coefficents->b11 = b11;
		coefficents->b12 = b12;
		return;



}
static void
order_4th_h_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	double	t0= 0.0 ,t1 = 0.0,t2 = 0.0, g = 0.0, a01 = 0.0, a02 = 0.0,b00 = 0.0, b01 = 0.0, b02 = 0.0,a11 = 0.0, a12 = 0.0,b10 = 0.0, b11 = 0.0, b12 = 0.0;
	int32_t fs = param->fs,fc = param->fc,G = param->gain;
		
	if(G > 0)
		G = 0;
	else if(G < -12)
		G = -12;
	param->gain = G;
	g = pow(10,(double)G/20.0);
	t0 = tan(PI * fc / (double)fs);
	t1 = 1.0 + 2.0 * cos(0.125 * PI) * t0 + t0*t0;
	t2 = 1.0 + 2.0 * cos(0.375 * PI) * t0 + t0*t0;
			
	a01 =  (1.0 - t0 * t0) / t1;
	a02 = 0.5 * (t1 - 2.0 -  2.0 * t0 * t0) / t1;
	a11 = (1 - t0 * t0)/t2;
	a12 = 0.5 * (t2 - 2.0 - 2.0 * t0 * t0) / t1;
	b00 = 0.5 / t1;
	b01 = -2.0 * b00;
	b02 = b00;
	b10 = 0.5 * g  / t2;
	b11 = -2.0 * b10;
	b12 = b10;
			
	coefficents->a01 = a01;
	coefficents->a02 = a02;
	coefficents->a11 = a11;
	coefficents->a12 = a12;
	coefficents->b00 = b00;
	coefficents->b01 = b01;
	coefficents->b02 = b02;
	coefficents->b10 = b10;
	coefficents->b11 = b11;
	coefficents->b12 = b12;
	return;



}

static void
order_6th_l_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{
	
	return;

}
static void
order_6th_h_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{

    return;

}
static void
order_8th_l_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{

	return;

}
static void
order_8th_h_pass(filter_param_t* param, filter_coeffients_t *coefficents)
{

    return;

}





