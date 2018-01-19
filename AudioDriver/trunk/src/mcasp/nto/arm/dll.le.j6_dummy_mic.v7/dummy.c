/*
 * Copyright 2016, Hinge-Tech.
 */
/*
 *    dummy_mic.c
 *      The primary interface into the dummy codec.
 */

struct dummy_context;
#define  MIXER_CONTEXT_T struct dummy_context

#include "mcasp.h"
#include <hw/i2c.h>


#include <math.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <hnm/pps.h>
#include "tef6638.h"
#include <pthread.h>


typedef struct dummy_context
{
	ado_mixer_t *mixer;
	HW_CONTEXT_T *hwc;
	int fd;

	int i2c_addr1;

	int sel_mcasp;
#define SEL_MCASP0 0
#define SEL_MCASP2 0
#define SEL_MCASP5 1

	int8_t	volume;
    int8_t  sec_volume;
	int8_t 	channel_gain[6];// FL,FR,RL,RR,SC,SW
	
    int8_t  fader;
	int8_t  balance;
	int8_t  sec_balance;
	 struct {
		int tc;
		int bass;
		int mid;
		int treble;
	} tone;  
	struct{
		int gain;
		int fc;
		float Q;
	}peq[7];

	int extend_thread_id;
	pthread_t extendthr;
	ado_mixer_delement_t *mic1_in;
	
}dummy_context_t;

static void pps_eq_handler(MIXER_CONTEXT_T *dummy,int fd );
#define FILE_AUDIO_EQ "/pps/hinge-tech/audio/equalizer?delta"

#define FILE_AUDIO_CHIME_GEN "/pps/hinge-tech/audio/chime?delta"
static void pps_chime_handler(MIXER_CONTEXT_T * dummy,int fd);

#define FILE_FM_STATUS "/pps/hinge-tech/radio/status?delta"

static void 
determine_volume(int32_t voldB,int32_t mainVolMax,short int *volMain)
{
   /*volMain is always positive.range of voldB is :[-80,+12]dB*/
   if(voldB>=0)
   {
		volMain[0] =  0x07FFUL;/*~+1*/
		volMain[1] = table_dB2Lin[mainVolMax - voldB];
   }
   else if (voldB >= -MaxLoudBoost)
   {
		volMain[0] = table_dB2Lin[-voldB];
		volMain[1] = table_dB2Lin[mainVolMax];
   }
   else if (voldB >= (mainVolMax - MaxLoudBoost - FixedBoost))
   	{
		volMain[0] = table_dB2Lin[MaxLoudBoost];
		volMain[1] = table_dB2Lin[mainVolMax - MaxLoudBoost -voldB];
   }
   else
   {
		volMain[0] = table_dB2Lin[mainVolMax - voldB -48];
		volMain[1] = 0x008UL;/*= 1/256*/

   }
	return;
}

/**/
static int32_t 
dummy_output_mainP_vol_control(MIXER_CONTEXT_T * dummy, ado_mixer_delement_t * element, uint8_t set,
						   uint32_t * vol, void *instance_data)
{
	int32_t altered = 0;
	int db;
	short int y_buf[2];
	if(set)
	{
		ado_debug(DB_LVL_MIXER, "dummy_output_mainP_vol_control will set volume=%d",vol[0]);
		
		altered = vol[0] != (dummy->volume & 0x7f);
		dummy->volume &=0x80;
		dummy->volume |= vol[0] & 0x7f;
		db = main_volume[(dummy->volume&0x7f)/4];
		determine_volume(db, MainVolMaxP, y_buf);
		tef6638_write_y_reg(dummy->fd,AUDIO_Y_Vol_Main1P, y_buf, 2, dummy->i2c_addr1);

		/*Loudness compensation*/
		if( db <= -25 )
		{
			y_buf[0] = table_Loudness_TrebleB_curve[25];
			y_buf[1] = table_Loudness_BassB_curve[25];
		}
		else if(db <= 0)  /*-25 < db <= 0*/
	    {
			y_buf[0] = table_Loudness_TrebleB_curve[-db];
			y_buf[1] = table_Loudness_BassB_curve[-db];
		}
		else if(db == 1)
		{
			y_buf[0] = LOUDNESS_TREBLE_BOOST_VOL_1dB;
			y_buf[1] = LOUDNESS_BASS_BOOST_VOL_1dB;
		}
		else /*db >=2,no boosting*/
		{
			y_buf[0] = LOUDNESS_TREBLE_BOOST_VOL_2dB;
			y_buf[1] = LOUDNESS_BASS_BOOST_VOL_2dB;
		}
		tef6638_write_y_reg(dummy->fd, AUDIO_Y_Loudf_StatLevT, y_buf,2,dummy->i2c_addr1);
		
	}
	else
	{
		vol[0] = (dummy->volume & 0x7f);
	}
    return altered;

}

static int32_t 
dummy_output_mainP_mute_control(MIXER_CONTEXT_T * dummy,ado_mixer_delement_t * element,uint8_t set,uint32_t * val,void * instance_data)
{
	int32_t altered = 0;
	short int y_buf[1] = {0};
	
	if(set)
	{	
		ado_debug(DB_LVL_MIXER,"dummy_output_mainP_mute_control will set mute=%x",val[0]);
		altered = val[0] != ((dummy->volume &0x80) >> 7);
		dummy->volume &= 0x7f;
		dummy->volume |= (val[0] & 0x01) << 7 ;
		
		y_buf[0] = ((dummy->volume & 0x80) ? 0 : 0x7FF);
		tef6638_write_y_reg(dummy->fd, AUDIO_Y_Mute_P, y_buf, 1, dummy->i2c_addr1);	
 	}
	else
	{
		val[0] = (dummy->volume & 0x80) >> 7;
	}

	return altered;
}
/*
 *primary channel volume control,used to build "PCM Volume" element
 *
 *
 *vol[0]and vol[1] for FL and FR,vol[3]and vol[4] for RL and RR,vol[2]and vol[5]for SWL(Center) and SWR
*/
static int32_t
dummy_output_channel_gain_control(MIXER_CONTEXT_T * dummy, ado_mixer_delement_t * element, uint8_t set,
						   uint32_t * vol, void *instance_data)
{
	int32_t altered = 0;
    uint16_t MaxBoost = ChanlGainMaxP;
	short int y_buf[6] ={0};
	if(set)
	{
		
		ado_debug(DB_LVL_MIXER, "dummy_output_vol_control FL=%d,FR=%d,RL=%d,RR=%d,SWL=%d,SWR=%d",
								vol[0],vol[1],vol[3],vol[4],vol[2],vol[5]);
        altered = (vol[0] !=  (dummy->channel_gain[0] & 0x7f)) 	||
				  (vol[1] != (dummy->channel_gain[1] & 0x7f))||
				  (vol[3] != (dummy->channel_gain[2] & 0x7f))	||
				  (vol[4] != (dummy->channel_gain[3] & 0x7f))||
			      (vol[2] != (dummy->channel_gain[4] & 0x7f))||
			      (vol[5] != (dummy->channel_gain[5] & 0x7f));
		           
		dummy->channel_gain[0] &= 0x80;
		dummy->channel_gain[0] |= (vol[0] & 0x7f);
		dummy->channel_gain[1] &= 0x80;
		dummy->channel_gain[1] |=  (vol[1] & 0x7f);
		dummy->channel_gain[2] &= 0x80;
		dummy->channel_gain[2] |=  (vol[3] & 0x7f);
		dummy->channel_gain[3] &= 0x80;
		dummy->channel_gain[3] |= (vol[4] & 0x7f);
		dummy->channel_gain[4] &= 0x80;
		dummy->channel_gain[4] |= (vol[2] & 0x7f);
		dummy->channel_gain[5] &= 0x80;
		dummy->channel_gain[5] |= (vol[5] & 0x7f);
		
			
        y_buf[0] = table_dB2Lin[MaxBoost - channel_gain[PRIMARY_CHANNEL][(dummy->channel_gain[0]&0x7f)/9]];
		y_buf[1] = table_dB2Lin[MaxBoost - channel_gain[PRIMARY_CHANNEL][(dummy->channel_gain[1]&0x7f)/9]];
		y_buf[2] = table_dB2Lin[MaxBoost - channel_gain[PRIMARY_CHANNEL][(dummy->channel_gain[2]&0x7f)/9]];
		y_buf[3] = table_dB2Lin[MaxBoost - channel_gain[PRIMARY_CHANNEL][(dummy->channel_gain[3]&0x7f)/9]];
		y_buf[4] = table_dB2Lin[MaxBoost - channel_gain[PRIMARY_CHANNEL][(dummy->channel_gain[4]&0x7f)/9]];
		y_buf[5] = table_dB2Lin[MaxBoost - channel_gain[PRIMARY_CHANNEL][(dummy->channel_gain[5]&0x7f)/9]];
		ado_debug(DB_LVL_MIXER, "dummy_output_gain_control will set FF=%X,FL=%X,RL=%X,RR=%X,SWL=%X,SWR=%X", y_buf[0], y_buf[1], y_buf[2], y_buf[3], y_buf[4], y_buf[5]);

		tef6638_write_y_reg( dummy->fd, AUDIO_Y_Vol_ChanGainPFL, y_buf, 6, dummy->i2c_addr1);
	    
		
	}
	else
	{
		
        vol[0] =  (dummy->channel_gain[0] & 0x7f);
		vol[1] =  (dummy->channel_gain[1] & 0x7f);
		vol[3] =  (dummy->channel_gain[2] & 0x7f);
		vol[4] =  (dummy->channel_gain[3] & 0x7f);
		vol[2] =  (dummy->channel_gain[4] & 0x7f);
		vol[5] =  (dummy->channel_gain[5] & 0x7f);
		
	}
	return (altered);
}


static int32_t
dummy_output_channel_mute_control(MIXER_CONTEXT_T * dummy, ado_mixer_delement_t * element, uint8_t set,
							uint32_t * val, void *instance_data)
{
	int32_t altered = 0;
	short int y_buf[6] = {0};
	if (set)
	{

		ado_debug(DB_LVL_MIXER, "dummy_output_channel_mute_control mute=%x", val[0]);
	    altered = val[0] !=  ( ((dummy->channel_gain[0] & 0x80)>>7) |
								   ((dummy->channel_gain[1] & 0x80)>>6)|
								   ((dummy->channel_gain[2] & 0x80)>>4) |
								   ((dummy->channel_gain[3] & 0x80)>>3)|
								   ((dummy->channel_gain[4] & 0x80)>>5) |
								   ((dummy->channel_gain[5] & 0x80)>>2) );
        dummy->channel_gain[0] &= 0x7f;
		dummy->channel_gain[0] |= (val[0] & SND_MIXER_CHN_MASK_FRONT_LEFT)<<7;
		dummy->channel_gain[1] &= 0x7f;
		dummy->channel_gain[1] |= (val[0] & SND_MIXER_CHN_MASK_FRONT_RIGHT)<<6;
		dummy->channel_gain[2] &= 0x7f;
		dummy->channel_gain[2] |= (val[0] & SND_MIXER_CHN_MASK_REAR_LEFT)<<4;
		dummy->channel_gain[3] &= 0x7f;
		dummy->channel_gain[3] |= (val[0] & SND_MIXER_CHN_MASK_REAR_RIGHT)<<3;
        dummy->channel_gain[4] &= 0x7f;
		dummy->channel_gain[4] |= (val[0] & SND_MIXER_CHN_MASK_FRONT_CENTER)<<5;
		dummy->channel_gain[5] &= 0x7f;
		dummy->channel_gain[5] |= (val[0] & SND_MIXER_CHN_MASK_WOOFER)<<2;	

		
		
		y_buf[0] = ((dummy->channel_gain[0] & 0x80) ? 0 : 0x7FF);
		y_buf[1] = ((dummy->channel_gain[1] & 0x80) ? 0 : 0x7FF);
		y_buf[2] = ((dummy->channel_gain[2] & 0x80) ? 0 : 0x7FF);
		y_buf[3] = ((dummy->channel_gain[3] & 0x80) ? 0 : 0x7FF);
	  	y_buf[4] = ((dummy->channel_gain[4] & 0x80) ? 0 : 0x7FF);
		y_buf[5] = ((dummy->channel_gain[5] & 0x80) ? 0 : 0x7FF);
		tef6638_write_y_reg( dummy->fd, AUDIO_Y_MuteSix_FL, y_buf, 6, dummy->i2c_addr1);
		
	}
	else
	{
		val[0] = ((dummy->channel_gain[0] & 0x80)>>7) |
				 ((dummy->channel_gain[1] & 0x80)>>6) |
				 ((dummy->channel_gain[2] & 0x80)>>4) |
				 ((dummy->channel_gain[3] & 0x80)>>3) |
				 ((dummy->channel_gain[4] & 0x80)>>5) |
				 ((dummy->channel_gain[5] & 0x80)>>2) ;			
	}
	return (altered);
}


static int32_t 
dummy_output_fader_control(MIXER_CONTEXT_T *dummy, ado_mixer_delement_t *element, uint8_t set, uint32_t *pan, void *instance_data)
{
	int32_t altered = 0,step = 0;
    short int y_buf[2];

	if(set)
	{
		ado_debug(DB_LVL_MIXER, "dummy_output_fader_control will set fader=%x", pan[0]);
		altered = pan[0] != ((dummy->fader));
		dummy->fader = pan[0];	
		if(dummy->fader > pan_range[PAN_FRONT_REAR].max)
			dummy->fader = pan_range[PAN_FRONT_REAR].max;
		if(dummy->fader < pan_range[PAN_FRONT_REAR].min)
			dummy->fader = pan_range[PAN_FRONT_REAR].min;
		step = (int)(dummy->fader/7);
		
		y_buf[0] = table_dB2Lin[audio_fade_gain[step].gain1];
		y_buf[1] = table_dB2Lin[audio_fade_gain[step].gain2];	
		
		tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_FadF, y_buf, 2, dummy->i2c_addr1);
	}
	else 
	{
		pan[0] = dummy->fader ;
	}

	return altered;
		
}

static int32_t 
dummy_output_balance_control(MIXER_CONTEXT_T *dummy, ado_mixer_delement_t *element, uint8_t set, uint32_t *pan, void *instance_data)
{
	int32_t altered = 0,step = 0;
    short int y_buf[2];
	

	if(set)
	{

        ado_debug(DB_LVL_MIXER, "dummy_output_balance_control will set balance=%x", pan[0]);

        altered = pan[0] != ((dummy->balance));
        dummy->balance = pan[0];
		if(dummy->balance > pan_range[PAN_LEFT_RIGHT].max)
			dummy->balance = pan_range[PAN_LEFT_RIGHT].max;
		if(dummy->balance < pan_range[PAN_LEFT_RIGHT].min)
			dummy->balance = pan_range[PAN_LEFT_RIGHT].min;

		step = (int)(dummy->balance/7);

		y_buf[0] = table_dB2Lin[audio_balance_gain[step].gain1];
		y_buf[1] = table_dB2Lin[audio_balance_gain[step].gain2];
		tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_BalPL, y_buf, 2, dummy->i2c_addr1);	
	}
	else
	{
		pan[0] = dummy->balance ;
	}

	return altered;
		
}

static int32_t 
dummy_output_pri_tone_control(MIXER_CONTEXT_T *dummy, ado_mixer_delement_t *element, uint8_t set, struct snd_mixer_element_tone_control1 *tc1, void *instance_data)
{
	int32_t altered = 0;
	 
	short int y_buf[10];
	int max_value;
	double value;
	double  Gmax;
	
	if(set)
	{
		ado_debug(DB_LVL_MIXER, "dummy_output_pri_tone_control will set type = %d,bass=%x,treble=%x\n", tc1->tc,tc1->bass, tc1->treble);

			altered = dummy->tone.bass != (tc1->bass & 0x7f) || 
					dummy->tone.treble != (tc1->treble & 0x7f);
			
            if(tc1->tc & SND_MIXER_TC1_BASS)
            {
            	int32_t gb;
				Gmax = 20.0 * log(1.0 + 2.0/(1.0 + tan(PI * (120.0/ 48000.0 - 0.25))));
				ado_debug(DB_LVL_MIXER, "dummy_output_pri_tone_control will set bass = %x\n", tc1->bass);
				dummy->tone.bass = tc1->bass & 0x7f;
				gb = tone_control_gain[(int)(dummy->tone.bass/9)];
				ado_debug(DB_LVL_MIXER, "dummy_output_pri_tone_control will set gb = %d\n", gb);
				if(gb > Gmax)
					gb = Gmax;
				if(gb >= 0)
					value = (pow(10,((double)gb / 20.0)) - 1.0) /16.0;
				else
					value = -(pow(10,((double)-gb / 20.0)) - 1.0) /16.0;
		         y_buf[0] = audio_calc_y(value);
				ado_debug(DB_LVL_MIXER, "dummy_output_pri_tone_control will set value=%f\n", value);	

				tef6638_write_y_reg(dummy->fd, AUDIO_Y_BMT_GbasP, y_buf, 1, dummy->i2c_addr1);
			}
			if(tc1->tc & SND_MIXER_TC1_TREBLE)
			{
				int32_t gt;
				filter_param_t param ;
				filter_coeffients_t coef ;
				
				dummy->tone.treble = tc1->treble & 0x7f;
				gt = tone_control_gain[(int)(dummy->tone.treble/9)];
    			memset(&param, 0, sizeof(filter_param_t));
				memset(&coef, 0, sizeof(filter_coeffients_t));
				param.type = TREBLE_FILTER_TYPE;
				param.fc = Tone_TREBLE_Fc;
				param.fs = SamplingFrequency;
				param.q  = Tone_Treble_Q;
				param.gain = gt;
				filter_calculate(&param, &coef);
				y_buf[0] = audio_calc_y (coef.a1);
				y_buf[1] = audio_calc_y (coef.a2);
				y_buf[2] = audio_calc_y (coef.b0);
				y_buf[3] = audio_calc_y (coef.b1);
				y_buf[4] = audio_calc_y (coef.b2);
			    ado_debug(DB_LVL_MIXER, "dummy_output_pri_tone_control a1=%f,a2=%f,b0=%f,b1=%f,b2=%f\n", coef.a1,coef.a2,coef.b0,coef.b1,coef.b2);

				gt = param.gain;
				value = (pow(10,((double)gt / 20.0)) - 1.0) /16.0;
				y_buf[5] = audio_calc_y(value);

				tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_BMT_a1tP, y_buf, 6, dummy->i2c_addr1);	
			}
			max_value= (dummy->tone.treble > dummy->tone.bass ? dummy->tone.treble : dummy->tone.bass);
			max_value = tone_control_gain[(int)(max_value/9)];
		    y_buf[0] = table_dB2Lin[ToneBoostMaxP - max_value];
			tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_DesScalBMTP, y_buf, 1, dummy->i2c_addr1);
		
	}
	else
	{
		tc1->bass = dummy->tone.bass;
		tc1->treble = dummy->tone.treble;
	}

	return altered;
}

/* Required for compatibility with Audioman
 * This switch is called by audio manager to ask deva to send the current HW status, i.e., whether headset is connected
 */
static int32_t
dummy_mixer_audioman_refresh_set(MIXER_CONTEXT_T * hw_ctx, ado_dswitch_t * dswitch, snd_switch_t * cswitch,
								void *instance_data)
{
	return (EOK);
}

static int32_t
dummy_mixer_audioman_refresh_get(MIXER_CONTEXT_T * hw_ctx, ado_dswitch_t * dswitch, snd_switch_t * cswitch,
								void *instance_data)
{
	/* Always return disabled as this switch does not maintain state */
	cswitch->type = SND_SW_TYPE_BOOLEAN;
	cswitch->value.enable = 0;
	return 0;
}



/***
*use secondary channel as input channel
***/
static int32_t
dummy_input_vol_control(MIXER_CONTEXT_T * dummy, ado_mixer_delement_t * element, uint8_t set,uint32_t * vol, void *instance_data)
{
	
	int32_t altered = 0;
	int db;
	short int y_buf[2];
	if(set)
	{
		ado_debug(DB_LVL_MIXER, "dummy_output_mainS_vol_control will set volume=%d",vol[0]);
		
		altered = vol[0] != (dummy->sec_volume & 0x7f);
		dummy->sec_volume &=0x80;
		dummy->sec_volume |= vol[0] & 0x7f;
		db = main_volume[(dummy->sec_volume&0x7f)/4];
		determine_volume(db, MainVolMaxS, y_buf);
		tef6638_write_y_reg(dummy->fd,AUDIO_Y_Vol_Main1S, y_buf, 2, dummy->i2c_addr1);	
	}
	else
	{
		vol[0] = (dummy->sec_volume & 0x7f);
	}
    return altered;
}

static int32_t
dummy_input_mute_control(MIXER_CONTEXT_T * dummy, ado_mixer_delement_t * element, uint8_t set,
						   uint32_t * val, void *instance_data)
{
	int32_t altered = 0;
	short int y_buf[1] = {0};
	
	if(set)
	{	
	    ado_debug(DB_LVL_MIXER,"dummy_output_mainS_mute_control will set mute=%x",val[0]);
		altered = val[0] != ((dummy->sec_volume &0x80) >> 7);
		dummy->sec_volume &= 0x7f;
		dummy->sec_volume |= (val[0] & 0x01) << 7 ;
		
		y_buf[0] = ((dummy->sec_volume & 0x80) ? 0 : 0x7FF);
		tef6638_write_y_reg(dummy->fd, AUDIO_Y_Mute_S, y_buf, 1, dummy->i2c_addr1);	
 	}
	else
	{
		val[0] = (dummy->sec_volume & 0x80) >> 7;
	}

	return altered;    
}




static int32_t build_dummy_mixer(MIXER_CONTEXT_T * dummy, ado_mixer_t * mixer)
{
	int error = 0;

	ado_mixer_delement_t *accu = NULL;
    //play--primary
	ado_mixer_delement_t *pri_channel_gain = NULL;
	ado_mixer_delement_t *pri_channel_mute = NULL;
	ado_mixer_delement_t *play_out = NULL;
	ado_mixer_delement_t *pri_main_vol = NULL;
	ado_mixer_delement_t *pri_main_mute = NULL;
	ado_mixer_delement_t * pri_tone_control = NULL;
	ado_mixer_delement_t *pri_balance = NULL;
	ado_mixer_delement_t *pri_fader = NULL;
	ado_mixer_dgroup_t *play_grp = NULL,*play_main_grp = NULL;
	ado_mixer_delement_t *pcm_out;

	ado_mixer_delement_t  *input_vol,*input_mute,*pcm_in;
	ado_mixer_dgroup_t * igain_grp;
	if(SEL_MCASP2 == dummy->sel_mcasp) //mic->i2s3
	{
		/* ################ */
		/* the primary OUTPUT GROUP */
		/* pcm_out >> tone_control >> main_volume >> channel_gain >> balance >> fader >> channel_mutes >> main_mute*/
		/* ################ */
        ado_debug(DB_LVL_MIXER, "enter Build dummy mixer");

		// pcm out
		if (!error && (pcm_out = ado_mixer_element_pcm1(mixer, SND_MIXER_ELEMENT_PLAYBACK,SND_MIXER_ETYPE_PLAYBACK1, 1, &pcm_devices[PRIMARY_CHANNEL])) == NULL)
			error++;
		
		//tone control
		if(!error && (pri_tone_control = ado_mixer_element_tone_control1(mixer, "Tone Control", &tone_info[PRIMARY_CHANNEL], dummy_output_pri_tone_control, (void *) NULL, NULL))==NULL)
			error++;
		
		//primary main volume
		if(!error && (pri_main_vol = ado_mixer_element_volume1( mixer, "PCM Volume", 1, &output_volume_range[PRIMARY_CHANNEL], dummy_output_mainP_vol_control,(void *) NULL,NULL)) == NULL)
		    error++;
	
		//primary main mute
		if (!error && (pri_main_mute = ado_mixer_element_sw1(mixer, "PCM Mute", 1,dummy_output_mainP_mute_control, (void *) NULL, NULL)) == NULL)
			error++;
	
		
		// channel gain  
		if (!error && (pri_channel_gain = ado_mixer_element_volume1(mixer, "Channel Gain", 6,&output_gain_range[PRIMARY_CHANNEL], dummy_output_channel_gain_control, (void *) NULL, NULL)) == NULL)
			error++;
	
		// channel mute
		if (!error && (pri_channel_mute = ado_mixer_element_sw1(mixer, "Channel Mute", 6,dummy_output_channel_mute_control, (void *) NULL, NULL)) == NULL)
			error++;
	
		//play fader
		if (!error &&(pri_fader = ado_mixer_element_pan_control1(mixer, "Playback Fader", 1, &pan_range[PAN_FRONT_REAR], dummy_output_fader_control, (void *)NULL, NULL)) == NULL)
			error++;
		
		//play balance
		if(!error && (pri_balance = ado_mixer_element_pan_control1(mixer, "Playback Balance", 1, &pan_range[PAN_LEFT_RIGHT],dummy_output_balance_control, (void *)NULL, NULL)) == NULL)
        	error++;
		
		//ACCU
        if(!error && (accu = ado_mixer_element_accu1(mixer, SND_MIXER_ELEMENT_DIGITAL_ACCU, 0)) == NULL)
			error++; 
		if (!error && (play_out = ado_mixer_element_io(mixer, "PCM Out", SND_MIXER_ETYPE_OUTPUT, 0, 6, stereo_voices)) == NULL)
		error++;

		//route pcm_out  to tone_control
		if (!error && ado_mixer_element_route_add(mixer, pcm_out, pri_tone_control) != 0)
			error++;
	
		//route tone_control to main volume 
		if (!error && ado_mixer_element_route_add(mixer, pri_tone_control, pri_main_vol) != 0)
			error++;
	
		// route main volume to channel gain 
		if (!error && ado_mixer_element_route_add(mixer, pri_main_vol, pri_channel_gain) != 0)
			error++;
	
		//route channel gain  to balance
		if(!error && ado_mixer_element_route_add(mixer, pri_channel_gain,  pri_balance) != 0)
			error++;
		
		//route play balance to fader
		if(!error && ado_mixer_element_route_add(mixer, pri_balance, pri_fader) != 0)
			error++;
	
		// route fader to channel mutes
		if (!error && ado_mixer_element_route_add(mixer, pri_fader, pri_channel_mute) != 0)
			error++;
	
		//route channel mutes to main mute
		if (!error && ado_mixer_element_route_add(mixer, pri_channel_mute, pri_main_mute) != 0)
			error++;

		// route main mute to accu        
		if (!error && ado_mixer_element_route_add(mixer, pri_main_mute, accu) != 0)
			error++;
		
		//route accu to output
		if (!error && ado_mixer_element_route_add(mixer, accu, play_out) != 0)
			error++;
	
		//volume control group
		if (!error && (play_grp = ado_mixer_playback_group_create(mixer, "Output Gain", SND_MIXER_CHN_MASK_5_1, pri_channel_gain, pri_channel_mute)) == NULL)
			error++;
		
		if (!error && (play_main_grp = ado_mixer_playback_group_create(mixer, "Pri Mixer", SND_MIXER_CHN_MASK_MONO, pri_main_vol, pri_main_mute)) == NULL)
			error++;

		
	
		// Input gain group
	/*	if(!error && (input_vol = ado_mixer_element_volume1(mixer, "Input Volume", 2, &input_volume_range[SECONDARY_CHANNEL], dummy_input_vol_control, (void*) NULL, NULL)) == NULL)
			error++;
		
		if(!error && (input_mute = ado_mixer_element_sw1(mixer, "Input Mute", 2,dummy_input_mute_control, NULL, NULL)) == NULL)
			error++;
		
		// route vol to mute
		if(!error && ado_mixer_element_route_add(mixer, input_vol, input_mute) != 0)
			error++;
		
		// create input gain group
		if(!error && (igain_grp = ado_mixer_capture_group_create(mixer, SND_MIXER_GRP_IGAIN, SND_MIXER_CHN_MASK_STEREO,input_vol, input_mute, NULL, NULL)) == NULL)
			error++;
 
		// Mic group
		if(!error && (dummy->mic1_in = ado_mixer_element_io(mixer, "MICIN", SND_MIXER_ETYPE_INPUT, 0, 2, stereo_voices)) == NULL)
			error++;
      
		// route mic io to input_vol
		if(!error && ado_mixer_element_route_add(mixer, dummy->mic1_in, input_vol) != 0)
			error++;

		// PCM component
		if(!error && (pcm_in = ado_mixer_element_pcm1(mixer, SND_MIXER_ELEMENT_CAPTURE, SND_MIXER_ETYPE_CAPTURE1, 1, &pcm_devices[SECONDARY_CHANNEL])) == NULL)
			error++;

		// route mute to pcm
		if(!error && ado_mixer_element_route_add(mixer, input_mute, pcm_in) != 0)
			error++;
         */
		// Audioman support
		if (!error && ado_mixer_switch_new(mixer, "Audioman Refresh", SND_SW_TYPE_BOOLEAN, 0, dummy_mixer_audioman_refresh_get,
											dummy_mixer_audioman_refresh_set, NULL, NULL) == NULL)
			error++;
        
		return (!error ? 0 : -1);
	}
	return (0);
}

#define DRA74X_GPIO_REGSIZE  0x400
#define DRA74X_GPIO3_BASE    0x48057000  // 64-95
#define GPIO_OE              0x134
#define GPIO_DATAOUT         0x13c
static inline void sr32(unsigned addr, unsigned start_bit, unsigned num_bits, unsigned value)
{
	unsigned tmp, msk = 0;
	msk = 1 << num_bits;
	--msk;
	tmp = in32(addr) & ~(msk << start_bit);
	tmp |= value << start_bit;
	out32(addr, tmp);
}


/*
gpio1_reglen = DRA74X_GPIO_REGSIZE;
	gpio1_physbase = DRA74X_GPIO1_BASE;
	gpio1_regbase = mmap_device_io(gpio1_reglen, gpio1_physbase);
	if (gpio1_regbase == (uintptr_t)MAP_FAILED) {
		printf("%s: mmap_device_io failed for GPIO1", __FUNCTION__);
		return -1;
	}
	gpio3_reglen = DRA74X_GPIO_REGSIZE;
	gpio3_physbase = DRA74X_GPIO3_BASE;
	gpio3_regbase = mmap_device_io(gpio3_reglen, gpio3_physbase);
	if (gpio3_regbase == (uintptr_t)MAP_FAILED) {
		printf("%s: mmap_device_io failed for GPIO3", __FUNCTION__);
		return -1;
	}
*/
	


static void reset_6638(void)
{
	/* Remap GPIO **/
		unsigned	gpio1_reglen, gpio3_reglen; /* GPIO regs access*/
		unsigned	gpio1_physbase, gpio3_physbase;
		uintptr_t	gpio1_regbase, gpio3_regbase;
    
//	usec_delay(10000);
	/* Enable IO capability.*/
		if (ThreadCtl(_NTO_TCTL_IO, NULL) == -1) {
			printf("ThreadCtl: error");
			return ;
		}
	gpio3_reglen = DRA74X_GPIO_REGSIZE;
	gpio3_physbase = DRA74X_GPIO3_BASE;
	gpio3_regbase = mmap_device_io(gpio3_reglen, gpio3_physbase);
	if (gpio3_regbase == (uintptr_t)MAP_FAILED) {
		printf("%s: mmap_device_io failed for GPIO3", __FUNCTION__);
		return ;
	}
	ado_debug(DB_LVL_MIXER, "init_6638\n");
	//AMP_STB_CPU
	/*
	sr32(gpio3_regbase + GPIO_OE, 19, 1, 1);
	usleep ( 100000 );
    sr32(gpio3_regbase + GPIO_DATAOUT, 19, 1, 0);
	usleep ( 100000 );
	sr32(gpio3_regbase + GPIO_OE, 19, 1, 0);
	usleep ( 100000 );
    sr32(gpio3_regbase + GPIO_DATAOUT, 19, 1, 1);
	usleep ( 100000 );
    */
	//RESET_TUNER
	
	sr32(gpio3_regbase + GPIO_OE, 25, 1, 0);
    usleep ( 100000 );

	sr32(gpio3_regbase + GPIO_DATAOUT, 25, 1, 0);

	usleep ( 100000 );
	sr32(gpio3_regbase + GPIO_DATAOUT, 25, 1, 1);
	usleep ( 100000 );
	
}


static int interface_configure(MIXER_CONTEXT_T * dummy)
{
	char buf[20] = {0};
	short int y_buf[11] = {0};
	int x_buf[10] = {0};
	
	 ado_debug(DB_LVL_MIXER, "enter interface_configure");
	/*Select Audio UseCase 4 and Samplerate*/
	buf[0] = ( USECASE << 4 )|SAMPLE_RATE ; 
    
	if(tef6638_write(dummy->fd, AUDIO_AUDIO_CFG_ADDR, buf, 1 , dummy->i2c_addr1))
	{
		return -EIO;
	}
	/*change tuning mode to standby mode*/
	buf[0] = 0x00;
	buf[1] = 0x2A;
    buf[2] = 0x12;
	if(tef6638_write(dummy->fd, AUDIO_MODE_CFG_ADDR,buf, 3, dummy->i2c_addr1 ))
	{
		return -EIO;
	}
	
	buf[0] = 0x10;
	buf[1] = 0x2A;
	buf[2] = 0x12;
	/*change mode to active mode (Preset Tuning)*/
	if(tef6638_write(dummy->fd, AUDIO_MODE_CFG_ADDR,buf, 3, dummy->i2c_addr1 ))
	{
		return -EINVAL;
	}

	 //check power on
	uint8_t sts, sts_addr = AUDIO_STS_ADDR;
    int cnt = 10;
    do {
      if ( cnt != 10 )
        usleep ( 5000 );
      if (tef6638_recv ( dummy->fd, &sts_addr,1, &sts, 1,dummy->i2c_addr1 ) )
      {
		  return -EIO;	
	  }

#ifdef SIMULATION
      sts = 0;
#endif

    } while ( ( cnt-- ) && ( sts & 0x01 ) );// end check power on

    /*set default ta tr time for mute*/
	x_buf[0] = 0x0076E3;
	x_buf[1] = 0x000BE3;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_Mute_StpAttP, x_buf, 2, dummy->i2c_addr1);
	tef6638_write_x_reg(dummy->fd, AUDIO_X_Mute_StpAttS, x_buf, 2, dummy->i2c_addr1);
	tef6638_write_x_reg(dummy->fd, AUDIO_X_Mute_StpAttT, x_buf, 2, dummy->i2c_addr1);
	tef6638_write_x_reg(dummy->fd, AUDIO_X_Mute_StpAttN, x_buf, 2, dummy->i2c_addr1);
	x_buf[0] = 0x00EDC6;
	x_buf[1] = 0x000EDC;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_MuteSix_StpAtt, x_buf, 2, dummy->i2c_addr1);		

	/*mute primary secondary phone navigation*/
	y_buf[0]=y_buf[1]=y_buf[2]=y_buf[3]=0x7FF;	
	tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_Mute_P, y_buf, 4, dummy->i2c_addr1);

	/*preset gain = 100 ,unmute FL,FR,RL,RR,SWL,SWR channel*/
    int i = 6;
	while(i--)
    {
	    dummy->channel_gain[i]=output_gain_range[PRIMARY_CHANNEL].max;
		y_buf[i] = 0x7FF;
	}
	tef6638_write_y_reg_scratch( dummy->fd, AUDIO_Y_Vol_ChanGainPFL, y_buf, 6, dummy->i2c_addr1);
	tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_MuteSix_FL, y_buf, 6, dummy->i2c_addr1);
	
    /*High level Power on ADs, DA01(Rear),DA23(Front*/
	buf[0] = AUDIO_PERIPHERAL_ADC;
	buf[1] = AUDIO_PERIPHERAL_ENABLE;
	if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2,dummy->i2c_addr1))
	{
		return -EIO;
	}
	buf[0] = AUDIO_PERIPHERAL_REAR_DAC;
	buf[1] = AUDIO_PERIPHERAL_ENABLE;
	if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2,dummy->i2c_addr1))
	{
		return -EIO;
	}
	buf[0] = AUDIO_PERIPHERAL_FRONT_DAC;
	buf[1] = AUDIO_PERIPHERAL_ENABLE;
    if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2,dummy->i2c_addr1))
	{
		return -EIO;
	}

	/*Peripheral config Slave-I2S(1,2)-input format*/
    buf[0] = SLAVE_I2S1_INPUT;
	buf[1] = I2S_FORMAT;
	if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2, dummy->i2c_addr1))
	{
		    return -EIO;
	}
	buf[0] = SLAVE_I2S2_INPUT;
	buf[1] = I2S_FORMAT;
	if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2, dummy->i2c_addr1))
	{
		    return -EIO;
	}
	/*Peripheral config Host-I2S-Output and set as i2s mode*/
	buf[0] = HOST_I2S0_OUTPUT;
	buf[1] = I2S_FORMAT;
	if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2, dummy->i2c_addr1))
	{
		    return -EIO;
	}
	buf[0] = HOST_I2S1_OUTPUT;
	buf[1] = I2S_FORMAT;
	if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2, dummy->i2c_addr1))
	{
		    return -EIO;
	}
	buf[0] = HOST_I2S2_OUTPUT;
	buf[1] = I2S_FORMAT;
	if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2, dummy->i2c_addr1))
	{
		    return -EIO;
	}
	buf[0] = HOST_I2S_INPUT;
	buf[1] = I2S_FORMAT;
	if(tef6638_write(dummy->fd, AUDIO_PERIPHERAL_CFG_ADDR, buf, 2, dummy->i2c_addr1))
	{
		    return -EIO;
	}
    ado_debug(DB_LVL_MIXER, "exit interface_configure");
	
	return  0;
	
}

static ado_mixer_reset_t dummy_reset;
static int primary_channel_init(MIXER_CONTEXT_T * dummy);
static int secondary_channel_init(MIXER_CONTEXT_T *dummy);
static int nav_channel_init(MIXER_CONTEXT_T *dummy);
static int phone_channel_init(MIXER_CONTEXT_T *dummy);

static int dummy_reset(MIXER_CONTEXT_T * dummy)
{
    ado_debug(DB_LVL_MIXER, "enter dummy_reset");
	if( interface_configure(dummy))
	{
		ado_error("dummy_reset: audio interface configure error!!!");
		return (-1);
	}
	
	if(primary_channel_init(dummy))
	{
		ado_error("dummy_reset: primary channel init fail!!!");
		return (-1);

	}
	if(secondary_channel_init(dummy))
	{
		ado_error("dummy_reset: secondary channel init fail!!!");
		return (-1);

	}
	if(phone_channel_init(dummy))
	{   
		ado_error("dummy_reset: phone channel init fail!!!");
		return (-1);

	}
	if(nav_channel_init(dummy))
	{
		ado_error("dummy_reset: navigation channel init fail!!!");
		return (-1);

	}
	 ado_debug(DB_LVL_MIXER, "exit dummy_reset");
	return (0);

}


static ado_mixer_destroy_t dummy_destroy;
static int dummy_destroy(MIXER_CONTEXT_T * dummy)
{
	short int y_buf[11] = {0};
	ado_error("destroying DUMMY Codec");
	y_buf[0]=y_buf[1]=y_buf[2]=y_buf[3]=y_buf[4]=y_buf[5]=y_buf[6]=y_buf[7]=y_buf[8]=y_buf[9]=0x00;
		
	tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_Mute_P, y_buf, 10, dummy->i2c_addr1);
    //if(EOK == pthread_kill(dummy->extend_thread_id, 0))//check thread exist or not
	//    if(-1 == ThreadDestroy(dummy->extend_thread_id,-1,NULL))
    //	{
	//		ado_error("ThreadDestroy:tid=%d, %s", dummy->extend_thread_id, strerror(errno));
	//	}
	pthread_abort(dummy->extendthr);
	close(dummy->fd);
	ado_free(dummy);

	return (0);
}



void * sound_extend_process_thread(void * arg);

int 
codec_mixer(ado_card_t * card, HW_CONTEXT_T * hwc)
{
	dummy_context_t *dummy;
	int32_t status;

	ado_error("%s %s %d :enter", __FILE__, __func__, __LINE__);
	if ((dummy = (dummy_context_t *) ado_calloc(1, sizeof (dummy_context_t))) == NULL)
	{
		ado_error("dummy: no memory %s", strerror(errno));
		return (-1);
	}
	if ((status = ado_mixer_create(card, "Primary", &hwc->mixer, dummy)) != EOK)
	{
		ado_free(dummy);
		return (status);
	}

	
	dummy->mixer = hwc->mixer;
	dummy->hwc = hwc;
	
		
	
	if ((dummy->fd = open(TEF6638_I2C_DEV, O_RDWR)) < 0)
	{
		ado_error("%s %d  %s",__func__, __LINE__, strerror(errno));
		ado_free(dummy);
		return (-1);
	}
			
	
	int speed = TEF6638_I2C_SPEED;
	if(devctl ( dummy->fd , DCMD_I2C_SET_BUS_SPEED, &speed, sizeof (speed), NULL ))
	{
		ado_error("Failed to write to codec:%s\n", strerror(errno));

	}
     
    hwc->i2c_dev = dummy->fd;
	dummy->i2c_addr1 = hwc->codec_i2c_addr;
	
     //reset 6638 to default setting only for dynamic load driver.  	
    reset_6638();
	
	// reset codec(s)
	if(dummy_reset(dummy))
    {
		ado_error("dummy: could not initialize codec");
		close(dummy->fd);
		ado_free(dummy);
		return (-1);
    }


	if (build_dummy_mixer(dummy, dummy->mixer))
	{
		ado_error("dummy: could not build Primary mixer");
		close(dummy->fd);
		ado_free(dummy);
		return (-1);
	}
     
	ado_mixer_set_reset_func(dummy->mixer, dummy_reset);
	ado_mixer_set_destroy_func(dummy->mixer, dummy_destroy);
	
	/* setup mixer controls for pcm  */
	ado_pcm_chn_mixer(hwc->pcm, ADO_PCM_CHANNEL_PLAYBACK,
					  hwc->mixer, ado_mixer_find_element(hwc->mixer, SND_MIXER_ETYPE_PLAYBACK1, SND_MIXER_ELEMENT_PLAYBACK, 0),
					  ado_mixer_find_group(hwc->mixer, "Pri Mixer", 0));
    
//	ado_pcm_chn_mixer(hwc->cap_aif.cap_strm[0].pcm, ADO_PCM_CHANNEL_CAPTURE,
//					  hwc->mixer, ado_mixer_find_element(hwc->mixer, SND_MIXER_ETYPE_CAPTURE1, SND_MIXER_ELEMENT_CAPTURE, 0),
//					  ado_mixer_find_group(hwc->mixer, SND_MIXER_GRP_IGAIN, 0));
	/* create thread for process eq feature*/
	

    ado_error("start to create pthread");
	if(EOK != pthread_create(&dummy->extendthr, NULL, sound_extend_process_thread, (void*)dummy))
	{
		ado_error("%s: %d %s", __func__,__LINE__,strerror(errno));	
		return (0);
	}
	pthread_setname_np(dummy->extendthr, "audio extend");
	ado_error("%s %d : exit", __func__,__LINE__);

	return (0);
}

static int init_pri_scaler(MIXER_CONTEXT_T * dummy)
{
	int x_buf[10];
	short int y_buf[10];

	ado_debug(DB_LVL_MIXER,"enter init_pri_scaler()");
	/*Source Scaling*/
	y_buf[0] = table_dB2Lin[6];
    if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_SrcScalP, y_buf, 1,dummy->i2c_addr1))
    {
		return -EINVAL;	
	}
	
	/*Graphic Equalizer pre-scaling  
   	 *AUDIO_X_Vol_OneOverMaxBoostP
	 *AUDIO_Y_Vol_DesScalGEq
	 */
	x_buf[0] = audio_calc_x(pow(10, (double)(-(GeqBoostMaxP+ ToneBoostMaxP))/20.0));
	
 	if(tef6638_write_x_reg(dummy->fd, AUDIO_X_Vol_OneOverMaxBoostP, x_buf, 1, dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	/*Down-Scaler for eq, preventing cliping due to GEQ band boost*/
	y_buf[0] = table_dB2Lin[GeqBoostMaxP];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_DesScalGEq, y_buf, 1, dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	/* Down-scaler for tone, preventing cliping due to bass,mid,treble boost*/
	
	y_buf[0] = table_dB2Lin[ToneBoostMaxP];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_DesScalBMTP, y_buf, 1, dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	/*Down-Scaler For Peq
	 *AUDIO_Y_Vol_ScalF
	 *AUDIO_Y_Vol_ScalR
	 *AUDIO_Y_Vol_ScalSwL
	 *AUDIO_Y_Vol_ScalSwR
	*/
	y_buf[0]=y_buf[1]=y_buf[2]=y_buf[3]= table_dB2Lin[PeqBoostMaxP];
   
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_ScalF, y_buf, 4, dummy->i2c_addr1))
	{
		return -EINVAL;

	}

	/*Post-Scaler For Peq
	 *AUDIO_Y_Vol_UpScalF
	 *AUDIO_Y_Vol_UpScalR
	 *AUDIO_Y_Vol_UpScalS
	 *AUDIO_Y_Vol_UpScalSwL
	 *AUDIO_Y_Vol_UpScalSwR
	*/
	
	y_buf[0]=y_buf[1]=y_buf[2]=y_buf[3]=y_buf[4] = table_dB2Lin[FixedBoost - MainVolMaxP - ChanlGainMaxP - PeqBoostMaxP - GeqBoostMaxP - ToneBoostMaxP];
	
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_UpScalF, y_buf, 4, dummy->i2c_addr1))
	{
		return -EINVAL;
	}  
	ado_debug(DB_LVL_MIXER,"exit init_pri_scaler()");
	return 0;
	/*Primary Navigation and phone volume setting*/
	
	
}

 
static int init_pri_path(MIXER_CONTEXT_T * dummy)
{
	int x_buf[10] = {0};
	char buf[10] = {0};
	
	ado_debug(DB_LVL_MIXER,"enter init_pri_path()");
	/*select i2s2 as  input source for primary input*/
	
	x_buf[0] = AUDIO_EASYP_PchannelStereo;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_EasyP_Index, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_EASYP_SrcSw_I2S1onA;//now Pchanselpntr connected to Src1nL

	tef6638_write_x_reg(dummy->fd, AUDIO_X_EasyP_Index, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_PChannelMode_OutL;

    tef6638_write_x_reg(dummy->fd, AUDIO_X_CompExp_InPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_CompExp_OutPL;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_GEqInPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_GEq_OutFL;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_PToneControl_InPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_ToneOutPL;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_PVol_InPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_Vol_OutPL;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_Loudf_InPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_Loudf_OutL;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_FInPntr, x_buf, 1,dummy->i2c_addr1);
	tef6638_write_x_reg(dummy->fd, AUDIO_X_RInPntr, x_buf, 1,dummy->i2c_addr1);
	tef6638_write_x_reg(dummy->fd, AUDIO_X_CenterInPntr, x_buf, 1,dummy->i2c_addr1);
	tef6638_write_x_reg(dummy->fd, AUDIO_X_SubwInPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_Eq_OutFL;
	x_buf[1] = AUDIO_X_Eq_OutRL;
	x_buf[2] = AUDIO_X_Eq_OutSwL;
	x_buf[3] = AUDIO_X_Eq_OutSwR;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_Delay_F_InPntr, x_buf, 4,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_Delay_FL_Out;
	x_buf[1] = AUDIO_X_Delay_RL_Out;
	x_buf[2] = AUDIO_X_Delay_SwL_Out;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_FVolInPntr, x_buf,3,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_Vol_OutFL;
	x_buf[1] = AUDIO_X_Vol_OutRL;
	x_buf[2] = AUDIO_X_Vol_OutSwL;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_FSupInPntr, x_buf, 3,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_FrontOutL;
	x_buf[1] = AUDIO_X_RearOutL;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_FDACpntr, x_buf, 2,dummy->i2c_addr1);
    x_buf[0] = AUDIO_X_SwOutL;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_HIIS0Outpntr, x_buf, 1,dummy->i2c_addr1);
    x_buf[0] = AUDIO_X_Chime_Cl_Out;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_Sup_ExtInPntr, x_buf, 1,dummy->i2c_addr1);
	ado_debug(DB_LVL_MIXER,"exit init_pri_path()");
    return 0;
 
}

static int primary_channel_init(MIXER_CONTEXT_T * dummy)
{
    short int y_buf[20] = {0};
	int x_buf[10] = {0};
	double temp;

	ado_debug(DB_LVL_MIXER,"enter primary_channel_init()");
	
    if(init_pri_scaler(dummy))
	{
		ado_error("dummy_reset: prescale init error!!!");
		return (-1);
	}  
	if(init_pri_path(dummy))
	{
	   ado_error("dummy_reset:init audio path error!!!");
	   return (-1);
    }
	
	/*DC Filter off,on*/
	y_buf[0] = 0x000;
	y_buf[1] = 0x800;
	y_buf[2] = 0x000;
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_DCfilt_a1A, y_buf, 3, dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	temp = -tan(PI *(-0.25 + 20.0/(double)SamplingFrequency)); 
	y_buf[0] = audio_calc_y(temp);//a1
	y_buf[1] = audio_calc_y(-0.5 - 0.5 * temp);//b1
	y_buf[2] = y_buf[1];//b0
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_DCfilt_a1A, y_buf, 3, dummy->i2c_addr1))
	{
		return -EINVAL;

	}
	

	/*set GEQ flat response*/
	y_buf[0] = 0;
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_GEq_Gc1, y_buf, 1, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_GEq_Gc2, y_buf, 1, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_GEq_Gc3, y_buf, 1, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_GEq_Gc4, y_buf, 1, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_GEq_Gc5, y_buf, 1, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	
	/*tone control init*/
	filter_param_t param ;
	filter_coeffients_t coef ;
    memset(&param, 0, sizeof(filter_param_t));
	memset(&coef, 0, sizeof(filter_coeffients_t));
	param.type = BASS_FILTER_TYPE;
	param.fc = Tone_Bass_Fc;
	param.fs = SamplingFrequency;
	param.q  = Tone_Bass_Q;
	filter_calculate(&param, &coef);
    y_buf[0] = (0xFFF000 & audio_calc_x (coef.a1))>>12;
	y_buf[1] = (0x000FFF & audio_calc_x (coef.a1));
	y_buf[2] = (0xFFF000 & audio_calc_x (coef.a2))>>12;
	y_buf[3] = (0x000FFF & audio_calc_x (coef.a2));
	y_buf[4] = (0xFFF000 & audio_calc_x (coef.b0))>>12;
	y_buf[5] = (0x000FFF & audio_calc_x (coef.b0));
	y_buf[6] = (0xFFF000 & audio_calc_x (coef.b1))>>12;
	y_buf[7] = (0x000FFF & audio_calc_x (coef.b1));
	y_buf[8] = (0xFFF000 & audio_calc_x (coef.b2))>>12;
	y_buf[9] = (0x000FFF & audio_calc_x (coef.b2));
	y_buf[10] = audio_calc_y(0);
    if(tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_BMT_a1bHP, y_buf, 11, dummy->i2c_addr1))
    {
		return -EINVAL;
	}
	//Mid set as flat response	
	memset(&param, 0, sizeof(filter_param_t));
	memset(&coef, 0, sizeof(filter_coeffients_t));
    param.type = MID_FILTER_TYPE;
	filter_calculate(&param, &coef);
	y_buf[0] = (0xFFF000 & audio_calc_x (coef.a1))>>12;
	y_buf[1] = (0x000FFF & audio_calc_x (coef.a1));
	y_buf[2] = (0xFFF000 & audio_calc_x (coef.a2))>>12;
	y_buf[3] = (0x000FFF & audio_calc_x (coef.a2));
	y_buf[4] = (0xFFF000 & audio_calc_x (coef.b0))>>12;
	y_buf[5] = (0x000FFF & audio_calc_x (coef.b0));
	y_buf[6] = audio_calc_y(0);
	if(tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_BMT_a1mHP, y_buf, 7, dummy->i2c_addr1))
    {
		return -EINVAL;
	}
	//treble set as 1st filter shelving
	memset(&param, 0, sizeof(filter_param_t));
	memset(&coef, 0, sizeof(filter_coeffients_t));
    param.type = TREBLE_FILTER_TYPE;
	param.fc = Tone_TREBLE_Fc;
	param.fs = SamplingFrequency;
	param.q  = Tone_Treble_Q;
	param.gain = Tone_Gain_Default;
	filter_calculate(&param, &coef);
	y_buf[0] = audio_calc_y (coef.a1);
	y_buf[1] = audio_calc_y (coef.a2);
	y_buf[2] = audio_calc_y (coef.b0);
	y_buf[3] = audio_calc_y (coef.b1);
	y_buf[4] = audio_calc_y (coef.b2);
	
	int gt = param.gain;
	double value=0.0;
	value = (pow(10,((double)gt / 20.0)) - 1.0) /16.0;
	y_buf[5] = audio_calc_y(value);
	if(tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_BMT_a1tP, y_buf, 6, dummy->i2c_addr1))
    {
		return -EINVAL;
	}
	/*lodad Loudness filter coeffects samplerate 48k , 150 Hz for BASS ,4KHZ for TREBLE*/
	x_buf[0] = AUDIO_EASYP_Loudf_StaticIndep;
	if(tef6638_write_x_reg(dummy->fd, AUDIO_X_EasyP_Index, x_buf, 1, dummy->i2c_addr1 ))
	{
		return -EINVAL;
	}
	/*set max loudness*/
	y_buf[0] = audio_calc_y((pow(10, (double)MaxLoudBoostB/20.0)-1.0)/8.0);
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Loudf_MaxBstB, y_buf, 1, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	y_buf[0] = audio_calc_y((pow(10, (double)MaxLoudBoostT/20.0)-1.0)/4.0);
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Loudf_MaxBstT, y_buf, 1, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	int i;
	for(i=7; i >= 0; i--)
		y_buf[i] = table_Loudness_BassCoeffs[16 + i];
		
	if(tef6638_write_y_reg_scratch(dummy->fd,  AUDIO_Y_Loudf_a1bL, y_buf, 8, dummy->i2c_addr1))
	{
		return -EINVAL;	
	}
    for(i=4; i >= 0; i--)
	    y_buf[i] = table_Loudness_TrebleCoeffs[5 + i];
	if(tef6638_write_y_reg_scratch(dummy->fd,  AUDIO_Y_Loudf_a1t, y_buf, 5, dummy->i2c_addr1))
	{
		return -EINVAL;	
	}
    ado_debug(DB_LVL_MIXER,"exit primary_channel_init()");
    return 0;

}
	


static int init_secondary_path(MIXER_CONTEXT_T *dummy)
{
	//select Ain2 as secondary channel input source
	int x_buf[10] = {0};
	
	ado_debug(DB_LVL_MIXER,"enter init_secondary_path()");
	
//	x_buf[0] = AUDIO_EASYP_SrcSw_I2S1onB;
//	tef6638_write_x_reg(dummy->fd, AUDIO_X_EasyP_Index, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_Vol_OutScalSL;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_SVolInPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_Vol_OutSL;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_SSupInPntr, x_buf, 1,dummy->i2c_addr1);
//	x_buf[0] = AUDIO_X_SecOutL;
//	tef6638_write_x_reg(dummy->fd, AUDIO_X_HIIS0Outpntr, x_buf, 1,dummy->i2c_addr1);
	
	ado_debug(DB_LVL_MIXER,"exit init_secondary_path()");
	return 0;
}
static int init_secondary_scaler(MIXER_CONTEXT_T *dummy)
{
	int x_buf[10] = {0};
	short int y_buf[10] = {0};
	y_buf[0] = table_dB2Lin[6];// +6 dB

	ado_debug(DB_LVL_MIXER,"enter init_secondary_scaler()");
	/*source scaler for secondary*/
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_SrcScalS, y_buf, 1,dummy->i2c_addr1))
	{
		return -EINVAL;
	}

	x_buf[0] = audio_calc_x(pow(10, (double)(-ToneBoostMaxS)/20.0));
		
	if(tef6638_write_x_reg(dummy->fd, AUDIO_X_Vol_OneOverMaxBoostS, x_buf, 1, dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	/* Down-scaler for tone, preventing cliping due to bass,mid,treble boost*/
	
	y_buf[0] = table_dB2Lin[ToneBoostMaxS];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_DesScalBMTS, y_buf, 1, dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	/**
	*Down-Scaler for optional secondary equalizer,preventing clipping due to secondary EQ BAND boost
	*AUDIO_Y_Vol_ScalS
	**/
	y_buf[0] = table_dB2Lin[PeqBoostMaxP];
   
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_ScalS, y_buf, 1, dummy->i2c_addr1))
	{
		return -EINVAL;

	}

	/*Post-Scaler for secondary
	 *AUDIO_Y_Vol_UpScalS 
	*/
	
	y_buf[0]= table_dB2Lin[FixedBoost - MainVolMaxS - ChanlGainMaxS - ToneBoostMaxS];
	
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_UpScalS, y_buf, 1, dummy->i2c_addr1))
	{
		return -EINVAL;
	}  

	ado_debug(DB_LVL_MIXER,"enter init_secondary_scaler()");
	return 0;
}

static int secondary_channel_init(MIXER_CONTEXT_T *dummy)
{
	short int y_buf[20] = {0};
//	int x_buf[10] = {0};
	double temp;

	ado_debug(DB_LVL_MIXER,"enter secondary_channel_init()");
	
	if(init_secondary_scaler(dummy))
	{
		ado_error("dummy_reset: prescale init error!!!");
		return (-1);
	}  
	if(init_secondary_path(dummy))
	{
	   ado_error("dummy_reset:init audio path error!!!");
	   return (-1);
    }

	/*DC Filter off,on*/
	y_buf[0] = 0x000;
	y_buf[1] = 0x800;
	y_buf[2] = 0x000;
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_DCfilt_a1B, y_buf, 3, dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	temp = -tan(PI *(-0.25 + 20.0/(double)SamplingFrequency)); 
	y_buf[0] = audio_calc_y(temp);//a1
	y_buf[1] = audio_calc_y(-0.5 - 0.5 * temp);//b1
	y_buf[2] = y_buf[1];//b0
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_DCfilt_a1B, y_buf, 3, dummy->i2c_addr1))
	{
		return -EINVAL;

	}
	/*tone control init*/
		filter_param_t param ;
		filter_coeffients_t coef ;
		memset(&param, 0, sizeof(filter_param_t));
		memset(&coef, 0, sizeof(filter_coeffients_t));
		param.type = BASS_FILTER_TYPE;
		param.fc = Tone_Bass_Fc;
		param.fs = SamplingFrequency;
		param.q  = Tone_Bass_Q;
		filter_calculate(&param, &coef);
		y_buf[0] = (0xFFF000 & audio_calc_x (coef.a1))>>12;
		y_buf[1] = (0x000FFF & audio_calc_x (coef.a1));
		y_buf[2] = (0xFFF000 & audio_calc_x (coef.a2))>>12;
		y_buf[3] = (0x000FFF & audio_calc_x (coef.a2));
		y_buf[4] = (0xFFF000 & audio_calc_x (coef.b0))>>12;
		y_buf[5] = (0x000FFF & audio_calc_x (coef.b0));
		y_buf[6] = (0xFFF000 & audio_calc_x (coef.b1))>>12;
		y_buf[7] = (0x000FFF & audio_calc_x (coef.b1));
		y_buf[8] = (0xFFF000 & audio_calc_x (coef.b2))>>12;
		y_buf[9] = (0x000FFF & audio_calc_x (coef.b2));
		y_buf[10] = audio_calc_y(0);
		if(tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_BMT_a1bHS, y_buf, 11, dummy->i2c_addr1))
		{
			return -EINVAL;
		}
		//Mid set as flat response	
		memset(&param, 0, sizeof(filter_param_t));
		memset(&coef, 0, sizeof(filter_coeffients_t));
		param.type = MID_FILTER_TYPE;
		filter_calculate(&param, &coef);
		y_buf[0] = (0xFFF000 & audio_calc_x (coef.a1))>>12;
		y_buf[1] = (0x000FFF & audio_calc_x (coef.a1));
		y_buf[2] = (0xFFF000 & audio_calc_x (coef.a2))>>12;
		y_buf[3] = (0x000FFF & audio_calc_x (coef.a2));
		y_buf[4] = (0xFFF000 & audio_calc_x (coef.b0))>>12;
		y_buf[5] = (0x000FFF & audio_calc_x (coef.b0));
		y_buf[6] = audio_calc_y(0);
		if(tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_BMT_a1mHS, y_buf, 7, dummy->i2c_addr1))
		{
			return -EINVAL;
		}
		//treble set as 1st filter shelving
		memset(&param, 0, sizeof(filter_param_t));
		memset(&coef, 0, sizeof(filter_coeffients_t));
		param.type = TREBLE_FILTER_TYPE;
		param.fc = Tone_TREBLE_Fc;
		param.fs = SamplingFrequency;
		param.q  = Tone_Treble_Q;
		param.gain = Tone_Gain_Default;
		filter_calculate(&param, &coef);
		y_buf[0] = audio_calc_y (coef.a1);
		y_buf[1] = audio_calc_y (coef.a2);
		y_buf[2] = audio_calc_y (coef.b0);
		y_buf[3] = audio_calc_y (coef.b1);
		y_buf[4] = audio_calc_y (coef.b2);
		
		int gt = param.gain;
		double value=0.0;
		value = (pow(10,((double)gt / 20.0)) - 1.0) /16.0;
		y_buf[5] = audio_calc_y(value);
		if(tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_BMT_a1tS, y_buf, 6, dummy->i2c_addr1))
		{
			return -EINVAL;
		}
        /*unmute secondary channel*/
		y_buf[0] = 0x07FF;	
		tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_Mute_S, y_buf, 1, dummy->i2c_addr1);

		ado_debug(DB_LVL_MIXER,"exit secondary_channel_init()");
		
		return 0;
}
static int init_nav_path(MIXER_CONTEXT_T *dummy)
{
    //select i2s1 as nav channel input source
    int x_buf[10] = {0};
	
	x_buf[0] = AUDIO_X_InputFlagIIS2;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_pSRCInputFlag2, x_buf, 1,dummy->i2c_addr1); 
    x_buf[0] = AUDIO_X_Navb4EQ;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_NavBackInPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_NavOut;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_Sup_NInPntr, x_buf, 1,dummy->i2c_addr1);
	return 0;
}
static int init_nav_scaler(MIXER_CONTEXT_T *dummy)
{
	short int y_buf[10] = {0};
	y_buf[0] = table_dB2Lin[6];// +6 dB

	//srcscalT
	
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_SrcScalN, y_buf, 1,dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	y_buf[0] = table_dB2Lin[0];	
		  tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_Nav, y_buf, 1, dummy->i2c_addr1);
	return 0;

}



static int nav_channel_init(MIXER_CONTEXT_T *dummy)
{
	short int y_buf[10] = {0};
	int32_t voice_filter_coeffs[2][10] = {
		{0x496,0x03a,0x12c,0x075,0x496,0x03a,0x10f,0x4e0,0x499,0xe35},
		{0x41c,0x3e1,0x7c7,0x83c,0x41c,0x3e1,0x0f5,0x7c2,0x084,0xc3c}
	} ;/*Nav filter as a band pass frequency,with cut off frequencies of 300HZ and 4kHz*/
	int i = 0;
	if(init_nav_scaler(dummy))
	{
		ado_error("dummy_reset: prescale init error!!!");
		return (-1);
	}  
	if(init_nav_path(dummy))
	{
	   ado_error("dummy_reset:init audio path error!!!");
	   return (-1);
    }
	/*allow nav only on FL channel*/
	y_buf[0]=y_buf[1]=y_buf[2]=y_buf[3]=y_buf[4]=y_buf[5] = table_dB2Lin[0];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Sup_NonFL, y_buf, 6, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	//load filter coeffients for NAV channel filter
    for(i = 0;i < 10;i++)
	    y_buf[i] = voice_filter_coeffs[0][i];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_EqN_b00L, y_buf, 10, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	for(i = 0;i < 10;i++)
	    y_buf[i] = voice_filter_coeffs[1][i];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_EqN_b10L, y_buf, 10, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	return 0;
}
//When Select usecase7, SRC3 is for phone ,SRC2 is for NAV,SRC1 is always for primary
static int init_phone_path(MIXER_CONTEXT_T *dummy)
{
    int x_buf[10] = {0};
	//init phone path selelct i2s-1 as phone channel input source
//	x_buf[0] = AUDIO_X_InputFlagIIS1;
	
//	tef6638_write_x_reg(dummy->fd, AUDIO_X_pSRCInputFlag3, x_buf, 1,dummy->i2c_addr1);
	/*enable phone on sup*/
    x_buf[0] = AUDIO_X_Phonb4EQ;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_PhonBackInPntr, x_buf, 1,dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_PhonOut;

	tef6638_write_x_reg(dummy->fd, AUDIO_X_Sup_TInPntr, x_buf, 1,dummy->i2c_addr1);
	return 0;
      
}
static int init_phone_scaler(MIXER_CONTEXT_T *dummy)
{
    short int y_buf[10] = {0};
	y_buf[0] = table_dB2Lin[6];// +6 dB

	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_SrcScalT, y_buf, 1,dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	
    return 0; 
}

static int phone_channel_init(MIXER_CONTEXT_T *dummy)
{
	short int y_buf[10] = {0};
	int32_t voice_filter_coeffs[2][10] = {
		{0x496,0x03a,0x12c,0x075,0x496,0x03a,0x10f,0x4e0,0x499,0xe35},
		{0x41c,0x3e1,0x7c7,0x83c,0x41c,0x3e1,0x0f5,0x7c2,0x084,0xc3c}
	} ;/*phone filter as a band pass frequency,with cut off frequencies of 300HZ and 4kHz*/
	int i = 0;
	if(init_phone_scaler(dummy))
	{
		ado_error("dummy_reset: prescale init error!!!");
		return (-1);
	}  
	if(init_phone_path(dummy))
	{
	   ado_error("dummy_reset:init audio path error!!!");
	   return (-1);
    }
	//load filter coeffients for phone channel filter
    for(i = 0;i < 10;i++)
	    y_buf[i] = voice_filter_coeffs[0][i];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_EqT_b00L, y_buf,10, dummy->i2c_addr1))	
	{
		return -EINVAL;
	}
	for(i = 0;i < 10;i++)
	    y_buf[i] = voice_filter_coeffs[1][i];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_EqT_b10L, y_buf,10, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	//enable phone on sups
	y_buf[0]=y_buf[1]=y_buf[2]=y_buf[3]=y_buf[4]=y_buf[5]=table_dB2Lin[0];
	if(tef6638_write_y_reg(dummy->fd, AUDIO_Y_Sup_TonFL, y_buf, 6, dummy->i2c_addr1))	
	{
		return -EINVAL;

	}
	
	return 0;
}





static int init_chime(MIXER_CONTEXT_T * dummy);
#ifdef STREAM_CONTROL
static void pps_fm_status_handler(MIXER_CONTEXT_T *dummy, int fd );
#endif
void *  sound_extend_process_thread(void * arg)
{
	dummy_context_t *dummy = (dummy_context_t*)arg;
	fd_set rfds;
	int pps_audio_eq_fd;
	int pps_audio_chime_fd;
#ifdef STREAM_CONTROL
	int pps_fm_status_fd;
#endif
	int max_fd;	
    ado_error("%s:enter %d",__func__,__LINE__);
	
    while(access(FILE_AUDIO_EQ, 0))
    {
        ado_debug(DB_LVL_MIXER,"waiting %s\n", FILE_AUDIO_EQ);
        sleep(10);
    }
	
    while(access(FILE_AUDIO_CHIME_GEN, 0))
    {
        ado_debug(DB_LVL_MIXER,"waiting %s\n", FILE_AUDIO_CHIME_GEN);
        sleep(10);
    }
#ifdef STREAM_CONTROL	
    while(access(FILE_FM_STATUS, 0))
    {
        ado_debug(DB_LVL_MIXER,"waiting %s\n", FILE_FM_STATUS);
        sleep(10);
    }
#endif
	if (( pps_audio_eq_fd = open ( FILE_AUDIO_EQ, O_RDONLY ) ) <= 0 ) 
	{
    	ado_error("open pps_audio_eq_fd fail");	
		return NULL;
	}
	
	if ( ( pps_audio_chime_fd = open ( FILE_AUDIO_CHIME_GEN, O_RDONLY ) ) <= 0 ) 
	{
		ado_error("open pps_audio_chime_fd fail");
		return NULL;
	}
#ifdef STREAM_CONTROL
	if(( pps_fm_status_fd = open ( FILE_FM_STATUS, O_RDONLY ) ) <= 0)
	{
		ado_error("open pps_fm_status_fd fail");	
		return NULL;
	}
#endif	
	if(init_chime(dummy))
	{
		ado_error("failed to  initlize chime function!!!");
		return NULL;
	}
	/*init eq as last setting*/
	max_fd = ( pps_audio_eq_fd > pps_audio_chime_fd ? pps_audio_eq_fd : pps_audio_chime_fd );
#ifdef STREAM_CONTROL
	max_fd = max_fd > pps_fm_status_fd ? max_fd : pps_fm_status_fd;
#endif
	while ( 1 )
	{
		FD_ZERO ( &rfds );
		FD_SET ( pps_audio_eq_fd, &rfds );
		FD_SET ( pps_audio_chime_fd, &rfds);
#ifdef STREAM_CONTROL
		FD_SET ( pps_fm_status_fd, &rfds );
#endif
		if ( select ( max_fd + 1, &rfds, NULL, NULL, NULL ) < 0 ) 
		{
		  if ( errno == EINTR ) 
		  {
			continue;
		  }
		}
	
		if ( FD_ISSET ( pps_audio_eq_fd, &rfds ) ) 
		{
		  ado_debug(DB_LVL_MIXER, "sound_extend_process_thread: start setting eq !!!");
		  pps_eq_handler(dummy, pps_audio_eq_fd );
		}
		if ( FD_ISSET ( pps_audio_chime_fd, &rfds ) ) 
		{
		  ado_debug(DB_LVL_MIXER, "sound_extend_process_thread: start setting chime generator !!!");
		  pps_chime_handler(dummy, pps_audio_chime_fd );
		}
#ifdef STREAM_CONTROL		
		if ( FD_ISSET ( pps_fm_status_fd, &rfds ) ) 
		{
		  ado_debug(DB_LVL_MIXER, "sound_extend_process_thread: start setting out fm status !!!");
		  pps_fm_status_handler(dummy, pps_fm_status_fd );
		}
#endif
	}	
	return NULL;
}

#ifdef STREAM_CONTROL
static void pps_fm_status_handler(MIXER_CONTEXT_T *dummy, int fd )
{
	char buf[512] = {0};
    const char *fm;
	pps_decoder_t decoder;
	short int y_buf[2] = {0};

	read(fd, buf, sizeof(buf) - 1);
	
	if(PPS_DECODER_OK != pps_decoder_initialize ( &decoder, NULL ))
	{
		ado_debug(DB_LVL_MIXER,"%s: %d chime pps_decoder_initialize fail !!!", __func__, __LINE__);
		return;
	}
	if(PPS_DECODER_OK != pps_decoder_parse_pps_str ( &decoder, buf ))
	{	
		ado_debug(DB_LVL_MIXER,"%s: %d chime pps_decoder_parse_pps_str !!!", __func__, __LINE__);
		return; 
	}
	
	pps_decoder_push ( &decoder, NULL );
	
	if( PPS_DECODER_OK == pps_decoder_get_string ( &decoder, "audio", &fm))
	{
		    if(!strncmp(fm,"off",3))
		    {
		    	y_buf[0] =  0x7FF;
				
				delay(500);
			//	ado_mutex_lock(&dummy->hwc->cmd_lock);
				ado_error("%s: %d  dummy->hwc->cmd = %d", __func__, __LINE__ , dummy->hwc->cmd);
				if( 1 == dummy->hwc->cmd)
				{
					tef6638_write_y_reg(dummy->fd, AUDIO_Y_Mute_P, y_buf, 1, dummy->i2c_addr1);	
				}
			//	ado_mutex_unlock(&dummy->hwc->cmd_lock);
			
		    }				
	}
	pps_decoder_pop( &decoder);
	pps_decoder_cleanup ( &decoder );
	return;
}
#endif

static void pps_chime_handler(MIXER_CONTEXT_T * dummy,int fd)
{
	int x_buf[2] = {0};
	pps_decoder_t decoder;
	char buf[512] = {0};
	const char *chime;
	static bool first_time_enter=true;
	
	read ( fd ,buf, sizeof ( buf ) - 1 );
	if(first_time_enter)
	{
		first_time_enter = false;
		return;
	}
		
	if(PPS_DECODER_OK != pps_decoder_initialize ( &decoder, NULL ))
	{
		ado_debug(DB_LVL_MIXER,"%s: %d chime pps_decoder_initialize fail !!!", __func__, __LINE__);
		return;
	}
	
	if(PPS_DECODER_OK != pps_decoder_parse_pps_str ( &decoder, buf ))
	{	
		ado_debug(DB_LVL_MIXER,"%s: %d chime pps_decoder_parse_pps_str !!!", __func__, __LINE__);
		return; 
	}
	
	pps_decoder_push ( &decoder, NULL );
	
	if( PPS_DECODER_OK == pps_decoder_get_string ( &decoder, "Chime", &chime))
	{
		    if(!strncmp(chime,"click",5))
		    {
		    	ado_debug(DB_LVL_MIXER,"Generate Click Sound");
				x_buf[0] = AUDIO_X_WavTab_Control_Click;	
				tef6638_write_x_reg(dummy->fd,AUDIO_X_WavTab_Control , x_buf, 1,dummy->i2c_addr1);
			}
			else if(!strncmp(chime,"clack",5))
			{
				ado_debug(DB_LVL_MIXER,"Generate Clack Sound");
				x_buf[0] = AUDIO_X_WavTab_Control_Clack;	
				tef6638_write_x_reg(dummy->fd,AUDIO_X_WavTab_Control , x_buf, 1,dummy->i2c_addr1);
			}
				
	}
	
	pps_decoder_pop( &decoder);
	pps_decoder_cleanup ( &decoder );
	return;
}
enum {
	PEQ_BAND1,
	PEQ_BAND2,
	PEQ_BAND3,
	PEQ_BAND4,
	PEQ_BAND5,
	PEQ_BAND6,
	PEQ_BAND7,	
};

static void set_eq_param(MIXER_CONTEXT_T *dummy ,pps_decoder_t *decoder,int band)
{
	const int addr[7][5]={
			{AUDIO_Y_EqFL_b10L, AUDIO_Y_EqFR_b10L, AUDIO_Y_EqRL_b10L, AUDIO_Y_EqRR_b10L, 7},
			{AUDIO_Y_EqFL_b20L, AUDIO_Y_EqFR_b20L, AUDIO_Y_EqRL_b20L, AUDIO_Y_EqRR_b20L, 7},
			{AUDIO_Y_EqFL_b30L, AUDIO_Y_EqFR_b30L, AUDIO_Y_EqRL_b30L, AUDIO_Y_EqRR_b30L, 7},
			{AUDIO_Y_EqFL_b40L, AUDIO_Y_EqFR_b40L, AUDIO_Y_EqRL_b40L, AUDIO_Y_EqRR_b40L, 7},
			{AUDIO_Y_EqFL_b50L, AUDIO_Y_EqFR_b50L, AUDIO_Y_EqRL_b50L, AUDIO_Y_EqRR_b50L, 10},
			{AUDIO_Y_EqFL_b60L, AUDIO_Y_EqFR_b60L, AUDIO_Y_EqRL_b60L, AUDIO_Y_EqRR_b60L, 10},
			{AUDIO_Y_EqFL_b70L, AUDIO_Y_EqFR_b70L, AUDIO_Y_EqRL_b70L, AUDIO_Y_EqRR_b70L, 10},
		};

	double beta = 0.0, t0 = 0.0, Gmax = 0.0,g = 0.0,G = 0.0;
	double a1 = 0.0, a2 = 0.0, b0 = 0.0,b1 = 0.0, b2 = 0.0;
	int gain = 0;
	int fc = 0;
	double Q = 1.0;
	short int y_buf[11] = {0};
	
		
	int fs = SamplingFrequency;
	int i = 0;
	
	if (PPS_DECODER_OK != pps_decoder_get_int ( decoder, "gain", &gain)||
				PPS_DECODER_OK != pps_decoder_get_double ( decoder, "Q", &Q)||
				PPS_DECODER_OK != pps_decoder_get_int ( decoder, "fc", &fc))
	{
		ado_debug(DB_LVL_MIXER,"invalid value setting !!!\nband=%d\ngain=%d\nQ=%f\nfc=%d\n", band + 1, gain, Q, fc);
		return; 		
	}

	t0 = 2.0 * PI * fc/(double)fs;
	if( gain > PeqBoostMaxP)
	   gain = PeqBoostMaxP;
	if(gain < PeqBoostMinP)
	   gain = PeqBoostMinP;			
	/*gain setting cannot excess Gmax*/ 		
	Gmax = 20 * log( 2.0 + (2 * Q)/t0 );

	if(fc > 20000 || fc < 20 || gain > PeqBoostMaxP|| gain < -14 ||Q <= 0 || Q > 10)
	{
		ado_error("value not allowed !!!\n band=%d\n gain=%d\n Q=%f\n fc=%d\n\n", band + 1, gain, Q, fc);
		return;
	}
	
	if( gain  > Gmax)
	{
		gain = (int)Gmax;
	}
	dummy->peq[band].gain = gain;
	dummy->peq[band].Q = Q;
	dummy->peq[band].fc = fc;
	
	
	
	ado_debug(DB_LVL_MIXER,"band = %d, gain=%d, Q=%f, fc=%d\n", band + 1, dummy->peq[band].gain, dummy->peq[band].Q, dummy->peq[band].fc);

	g = pow(10,dummy->peq[band].gain/(double)20.0);
				
	if(g >=1)
	{
		beta = ((0.5 * t0) /dummy->peq[band].Q);
	}
	else
	{
		beta=((0.5 * t0)/(g * dummy->peq[band].Q));
	}
				
	a2 = (-0.5 * (1 - beta)/(1 + beta));
	a1 = (0.5 - a2) * cos(t0);
	b0 = (g - 1) * (0.25 + 0.5 * a2) + 0.5;
	G = 0.25 * (g - 1);
	ado_debug(DB_LVL_MIXER,"\nwill set band=%d\n gain=%d\n Q=%f\n fc=%d\n", band + 1, gain, Q, fc);
	 
	switch (band)
	{
		case PEQ_BAND1:
		case PEQ_BAND2:
		case PEQ_BAND3:
		case PEQ_BAND4:
			y_buf[0] = y_buf[1] = audio_calc_y(b0);
			y_buf[2] = y_buf[3] = audio_calc_y(a2);
			y_buf[4] = y_buf[5] = audio_calc_y(a1);
			y_buf[6] = audio_calc_y(G);
			for( i = 0;i < 4; i++)
			   tef6638_write_y_reg_scratch(dummy->fd,addr[band][i], y_buf, 7, dummy->i2c_addr1);
			break;
		case PEQ_BAND5:
		case PEQ_BAND6:
		case PEQ_BAND7:
			b1 = -a1;
			b2 = -(g - 1)*(0.25 + 0.5 * a2) - a2;
			y_buf[0] = y_buf[1] = audio_calc_y(b0);
			y_buf[2] = y_buf[3] = audio_calc_y(b1);
			y_buf[4] = y_buf[5] = audio_calc_y(b2);
			y_buf[6] = y_buf[7] = audio_calc_y(a1);
			y_buf[8] = y_buf[9] = audio_calc_y(a2);
			for( i = 0;i < 4; i++)
				tef6638_write_y_reg_scratch(dummy->fd,addr[band][i], y_buf, 10, dummy->i2c_addr1);
			break;
		default:
			break;
	}
	ado_debug(DB_LVL_MIXER,"a1 = %f, a2 = %f, b0 = %f, b1 = %f, b2 = %f, G = %f\n", a1, a2, b0, b1, b2, G);
	return;	   
}
static void pps_eq_handler(MIXER_CONTEXT_T *dummy,int fd )
{
  		
  		
	/*
	 * band1:json:{"gain": 0,"Q":1.0,"fc":60}
	 * band2:json:{"gain": 0,"Q":1.0,"fc":150}
	 * band3:json:{"gain": 0,"Q":1.0,"fc":400}
	 * band4:json:{"gain": 0,"Q":1.0,"fc":1000}
	 * band5:json:{"gain": 0,"Q":1.0,"fc":3000}
	 * band6:json:{"gain": 0,"Q":1.0,"fc":7000}
	 * band7:json:{"gain": 0,"Q":1.0,"fc":15000}
	*/
	pps_decoder_t decoder;
	
	char buf[512] = {0};
	
	memset ( buf, 0, sizeof ( buf ) );
	read ( fd ,buf, sizeof ( buf ) - 1 );	

	ado_debug (DB_LVL_MIXER,"%s\n*********\n", buf );

	if(PPS_DECODER_OK != pps_decoder_initialize ( &decoder, NULL ))
	{
		ado_debug(DB_LVL_MIXER,"eq pps_decoder_initialize fail !!!");
		return;
	}
	if(PPS_DECODER_OK != pps_decoder_parse_pps_str ( &decoder, buf ))
	{   
		ado_debug(DB_LVL_MIXER,"eq pps_decoder_parse_pps_str !!!");
		return;	
	}

	pps_decoder_push ( &decoder, NULL );
	if(PPS_DECODER_OK == pps_decoder_push ( &decoder, "band1" ))
	{	
		ado_debug(DB_LVL_MIXER,"\n*****will set band1*****\n");	
		set_eq_param(dummy,&decoder, PEQ_BAND1);
		pps_decoder_pop(&decoder);
	}
	if(PPS_DECODER_OK == pps_decoder_push ( &decoder, "band2" ))
	{	
		ado_debug(DB_LVL_MIXER,"\n*****will set band2*****\n");
		set_eq_param(dummy, &decoder, PEQ_BAND2);
		pps_decoder_pop(&decoder);
	}
	if(PPS_DECODER_OK == pps_decoder_push ( &decoder, "band3" ))
	{
		ado_debug(DB_LVL_MIXER,"\n*****will set band3*****\n");
		set_eq_param(dummy, &decoder, PEQ_BAND3);	
		pps_decoder_pop(&decoder);
	}
	if(PPS_DECODER_OK == pps_decoder_push ( &decoder, "band4" ))
	{
		ado_debug(DB_LVL_MIXER,"\n*****will set band4*****\n");
		set_eq_param(dummy, &decoder, PEQ_BAND4);
		pps_decoder_pop(&decoder);
	}
	if(PPS_DECODER_OK == pps_decoder_push ( &decoder, "band5" ))
	{
		ado_debug(DB_LVL_MIXER,"\n*****will set band5*****\n");
		set_eq_param(dummy, &decoder, PEQ_BAND5);
		pps_decoder_pop(&decoder);
	}
	if(PPS_DECODER_OK == pps_decoder_push ( &decoder, "band6" ))
	{
		ado_debug(DB_LVL_MIXER,"\n*****will set band6*****\n");
		set_eq_param(dummy, &decoder, PEQ_BAND6);
		pps_decoder_pop(&decoder);
	}
	if(PPS_DECODER_OK == pps_decoder_push ( &decoder, "band7" ))
	{
		ado_debug(DB_LVL_MIXER,"\n*****will set band7*****\n");
		set_eq_param(dummy, &decoder, PEQ_BAND7);
		pps_decoder_pop(&decoder);
	}
	pps_decoder_pop(&decoder);
	pps_decoder_cleanup ( &decoder );	
	return;
}




static int init_chime(MIXER_CONTEXT_T * dummy)
{
	short int y_buf[11] = {0};
	int x_buf[10] = {0};
	int cnt = 0;
	
    static const char audio_chime_data[] = {
  (char)(3 * 61),
  (char)0xF2, (char)0x07, (char)0x61,  (char)0x08, (char)0x89, (char)0x86,  (char)0x07, (char)0x88, (char)0x87,  (char)0x08, (char)0x88, (char)0x86,  (char)0x07, (char)0x87, (char)0x86,
  (char)0x0A, (char)0x8A, (char)0x87,  (char)0x0A, (char)0x89, (char)0x89,  (char)0x08, (char)0x88, (char)0x89,  (char)0x08, (char)0x88, (char)0x88,  (char)0x0A, (char)0x89, (char)0x8A,
  (char)0x0C, (char)0x8C, (char)0x8B,  (char)0x07, (char)0x87, (char)0x89,  (char)0x0F, (char)0x8D, (char)0x89,  (char)0x05, (char)0x89, (char)0x8D,  (char)0x0E, (char)0x88, (char)0x84,
  (char)0x0A, (char)0x8E, (char)0x8E,  (char)0x0A, (char)0x89, (char)0x87,  (char)0x19, (char)0x94, (char)0x90,  (char)0x0C, (char)0x9A, (char)0x9F,  (char)0xF7, (char)0x6E, (char)0x7A,
  (char)0x27, (char)0xA4, (char)0x8E,  (char)0xF0, (char)0x75, (char)0x91,  (char)0x39, (char)0xA2, (char)0x84,  (char)0xEB, (char)0x86, (char)0xAC,  (char)0x08, (char)0x79, (char)0x67,
  (char)0x0C, (char)0x95, (char)0x91,  (char)0x13, (char)0xA6, (char)0x99,  (char)0xDE, (char)0x56, (char)0x71,  (char)0x48, (char)0xB5, (char)0x87,  (char)0xBD, (char)0x55, (char)0x9F,
  (char)0x77, (char)0xBA, (char)0x6D,  (char)0xD4, (char)0xAE, (char)0xF8,  (char)0x10, (char)0x57, (char)0x33,  (char)0x1C, (char)0xB8, (char)0xB0,  (char)0xE3, (char)0x62, (char)0x69,
  (char)0x13, (char)0x75, (char)0x63,  (char)0x03, (char)0xAA, (char)0xB5,  (char)0xC5, (char)0x35, (char)0x5D,  (char)0x50, (char)0xC9, (char)0x91,  (char)0xDF, (char)0x84, (char)0xB1,
  (char)0x26, (char)0x8C, (char)0x66,  (char)0x34, (char)0xB5, (char)0xB2,  (char)0xDC, (char)0x71, (char)0x97,  (char)0x28, (char)0x69, (char)0x4B,  (char)0xF4, (char)0xB9, (char)0xCE,
  (char)0xC8, (char)0x2E, (char)0x40,  (char)0x36, (char)0xD1, (char)0x9F,  (char)0xAC, (char)0x48, (char)0x80,  (char)0x21, (char)0x6A, (char)0x35,  (char)0x11, (char)0xA6, (char)0xAF,
  (char)0xF6, (char)0x5F, (char)0x74,  (char)0x41, (char)0xAC, (char)0x9A,  (char)0x07, (char)0xA4, (char)0xC3,  (char)0x0E, (char)0x82, (char)0x79,  (char)0x1E, (char)0xB8, (char)0xA0,
  (char)0xBE, (char)0x56, (char)0x72,  (char)0xEE, (char)0x4D, (char)0x3B,  (char)0xDB, (char)0x68, (char)0x7A,  (char)0xD7, (char)0x3D, (char)0x45,  (char)0x2E, (char)0xB2, (char)0x8B,
  (char)0xE9, (char)0x7C, (char)0x9B,
  (char)(3 * 61),
  (char)0xF2, (char)0x07, (char)0x9D,  (char)0x4B, (char)0xB3, (char)0x87,  (char)0x07, (char)0x9F, (char)0xC1,  (char)0x20, (char)0x8B, (char)0x7F,  (char)0xF0, (char)0x8A, (char)0xA1,
  (char)0xEF, (char)0x6C, (char)0x69,  (char)0xE2, (char)0x74, (char)0x7A,  (char)0xE5, (char)0x59, (char)0x57,  (char)0xED, (char)0x6C, (char)0x68,  (char)0xE6, (char)0x5E, (char)0x5F,
  (char)0x0C, (char)0x7D, (char)0x6F,  (char)0x0F, (char)0x98, (char)0x98,  (char)0x10, (char)0x8A, (char)0x8E,  (char)0x29, (char)0xAE, (char)0xA3,  (char)0xFB, (char)0x88, (char)0x95,
  (char)0x19, (char)0x8C, (char)0x79,  (char)0xEC, (char)0x81, (char)0x94,  (char)0xE2, (char)0x54, (char)0x5A,  (char)0xE1, (char)0x71, (char)0x74,  (char)0xDB, (char)0x52, (char)0x54,
  (char)0xFC, (char)0x80, (char)0x6F,  (char)0xF9, (char)0x73, (char)0x76,  (char)0x21, (char)0x9C, (char)0x8D,  (char)0x0F, (char)0x97, (char)0xA0,  (char)0x20, (char)0x91, (char)0x8C,
  (char)0x18, (char)0x9F, (char)0xA4,  (char)0xF4, (char)0x74, (char)0x85,  (char)0x02, (char)0x84, (char)0x7F,  (char)0xD8, (char)0x68, (char)0x79,  (char)0xEC, (char)0x5E, (char)0x52,
  (char)0xEC, (char)0x73, (char)0x6F,  (char)0xF2, (char)0x69, (char)0x65,  (char)0x00, (char)0x7F, (char)0x7C,  (char)0x12, (char)0x87, (char)0x82,  (char)0x1D, (char)0x9E, (char)0x9D,
  (char)0x18, (char)0x96, (char)0x9A,  (char)0x17, (char)0x9B, (char)0x9B,  (char)0xF9, (char)0x83, (char)0x8D,  (char)0xF6, (char)0x6F, (char)0x71,  (char)0xEF, (char)0x76, (char)0x79,
  (char)0xDB, (char)0x5B, (char)0x65,  (char)0xF1, (char)0x6B, (char)0x63,  (char)0xF8, (char)0x78, (char)0x75,  (char)0x07, (char)0x83, (char)0x7D,  (char)0x16, (char)0x90, (char)0x8A,
  (char)0x13, (char)0x96, (char)0x97,  (char)0x0C, (char)0x8A, (char)0x8D,  (char)0x10, (char)0x94, (char)0x92,  (char)0xF6, (char)0x7D, (char)0x87,  (char)0xF8, (char)0x77, (char)0x74,
  (char)0xF0, (char)0x72, (char)0x75,  (char)0xED, (char)0x6D, (char)0x6F,  (char)0xF5, (char)0x70, (char)0x6E,  (char)0xFB, (char)0x7C, (char)0x7B,  (char)0x01, (char)0x7B, (char)0x7A,
  (char)0x0F, (char)0x8D, (char)0x87,  (char)0x0A, (char)0x8B, (char)0x8D,  (char)0x0E, (char)0x8B, (char)0x89,  (char)0x05, (char)0x89, (char)0x8E,  (char)0x04, (char)0x82, (char)0x81,
  (char)0xFA, (char)0x82, (char)0x85,
  (char)(3 * 61),
  (char)0xF2, (char)0x07, (char)0xD9,  (char)0xF0, (char)0x6F, (char)0x73,  (char)0xF5, (char)0x75, (char)0x73,  (char)0xEE, (char)0x6F, (char)0x73,  (char)0xFD, (char)0x76, (char)0x72,
  (char)0xFF, (char)0x82, (char)0x81,  (char)0x02, (char)0x7F, (char)0x7D,  (char)0x0A, (char)0x8A, (char)0x87,  (char)0x07, (char)0x88, (char)0x89,  (char)0x05, (char)0x86, (char)0x87,
  (char)0xFD, (char)0x7F, (char)0x83,  (char)0xF8, (char)0x78, (char)0x7B,  (char)0xF8, (char)0x78, (char)0x78,  (char)0xF6, (char)0x78, (char)0x79,  (char)0xF8, (char)0x76, (char)0x75,
  (char)0xFE, (char)0x7D, (char)0x7A,  (char)0xFE, (char)0x7D, (char)0x7D,  (char)0x02, (char)0x82, (char)0x80,  (char)0x01, (char)0x81, (char)0x81,  (char)0x00, (char)0x81, (char)0x81,
  (char)0x00, (char)0x7F, (char)0x7F,  (char)0x02, (char)0x83, (char)0x81,  (char)0xFD, (char)0x7F, (char)0x81,  (char)0xFE, (char)0x7D, (char)0x7D,  (char)0xFC, (char)0x7C, (char)0x7E,
  (char)0xFC, (char)0x7B, (char)0x7B,  (char)0xFE, (char)0x7E, (char)0x7D,  (char)0xFC, (char)0x7D, (char)0x7E,  (char)0xFF, (char)0x7D, (char)0x7C,  (char)0x00, (char)0x80, (char)0x80,
  (char)0x00, (char)0x7F, (char)0x7F,  (char)0x02, (char)0x83, (char)0x82,  (char)0x00, (char)0x81, (char)0x82,  (char)0x00, (char)0x7F, (char)0x7F,  (char)0x00, (char)0x81, (char)0x81,
  (char)0xFE, (char)0x7D, (char)0x7F,  (char)0xFF, (char)0x7F, (char)0x7E,  (char)0xFD, (char)0x7D, (char)0x7E,  (char)0xFF, (char)0x7F, (char)0x7D,  (char)0xFE, (char)0x7F, (char)0x7F,
  (char)0xFF, (char)0x7E, (char)0x7D,  (char)0x00, (char)0x80, (char)0x7F,  (char)0x00, (char)0x80, (char)0x80,  (char)0x01, (char)0x81, (char)0x81,  (char)0x02, (char)0x82, (char)0x82,
  (char)0x01, (char)0x81, (char)0x82,  (char)0x01, (char)0x81, (char)0x81,  (char)0xFF, (char)0x80, (char)0x81,  (char)0xFF, (char)0x7E, (char)0x7E,  (char)0xFF, (char)0x7F, (char)0x7F,
  (char)0xFE, (char)0x7F, (char)0x7E,  (char)0x00, (char)0x7F, (char)0x7F,  (char)0x02, (char)0x82, (char)0x82,  (char)0x07, (char)0x83, (char)0x84,  (char)0x06, (char)0x84, (char)0x85,
  (char)0x04, (char)0x86, (char)0x84,  (char)0x04, (char)0x85, (char)0x84,  (char)0x04, (char)0x83, (char)0x82,  (char)0x03, (char)0x83, (char)0x84,  (char)0x02, (char)0x83, (char)0x82,
  (char)0x03, (char)0x84, (char)0x84,
  (char)(3 * 61),
  (char)0xF2, (char)0x08, (char)0x15,  (char)0x01, (char)0x80, (char)0x84,  (char)0x05, (char)0x83, (char)0x82,  (char)0x04, (char)0x86, (char)0x85,  (char)0xF6, (char)0x79, (char)0x7F,
  (char)0x0D, (char)0x85, (char)0x7A,  (char)0xFC, (char)0x85, (char)0x8E,  (char)0x02, (char)0x7B, (char)0x7A,  (char)0xFC, (char)0x7D, (char)0x82,  (char)0x02, (char)0x83, (char)0x7B,
  (char)0x0A, (char)0x83, (char)0x82,  (char)0x06, (char)0x9F, (char)0xA2,  (char)0xD3, (char)0x40, (char)0x5C,  (char)0x2A, (char)0xB0, (char)0x80,  (char)0xD2, (char)0x52, (char)0x7E,
  (char)0x4B, (char)0xB8, (char)0x89,  (char)0xCA, (char)0x66, (char)0xA5,  (char)0xF0, (char)0x76, (char)0x5C,  (char)0x05, (char)0x81, (char)0x77,  (char)0xD6, (char)0x95, (char)0x9D,
  (char)0xF8, (char)0x36, (char)0x23,  (char)0x22, (char)0xD2, (char)0xB8,  (char)0xDA, (char)0x3F, (char)0x59,  (char)0x37, (char)0xC1, (char)0x99,  (char)0x0E, (char)0x9E, (char)0xA7,
  (char)0xDD, (char)0x63, (char)0x73,  (char)0x45, (char)0xA5, (char)0x6A,  (char)0x9B, (char)0x58, (char)0x95,  (char)0x1D, (char)0x4F, (char)0x0A,  (char)0xE0, (char)0x82, (char)0xA8,
  (char)0xEC, (char)0x30, (char)0x38,  (char)0x35, (char)0x9B, (char)0x90,  (char)0xDD, (char)0x89, (char)0xAE,  (char)0x0F, (char)0x55, (char)0x46,  (char)0x58, (char)0xF1, (char)0xD7,
  (char)0xBF, (char)0x4A, (char)0x94,  (char)0x30, (char)0x78, (char)0x4B,  (char)0xE6, (char)0xA0, (char)0xBE,  (char)0xB0, (char)0x0C, (char)0x2F,  (char)0x37, (char)0xB3, (char)0x85,
  (char)0x83, (char)0x3B, (char)0x91,  (char)0x10, (char)0x4B, (char)0x0F,  (char)0x1B, (char)0xC7, (char)0xB8,  (char)0xDB, (char)0x5E, (char)0x74,  (char)0x56, (char)0xBF, (char)0x90,
  (char)0x1B, (char)0xC6, (char)0xE2,  (char)0xFE, (char)0x6A, (char)0x71,  (char)0x1D, (char)0xAC, (char)0x8F,  (char)0x9A, (char)0x46, (char)0x76,  (char)0xD3, (char)0x29, (char)0x17,
  (char)0xF3, (char)0x6F, (char)0x70,  (char)0xE2, (char)0x54, (char)0x62,  (char)0x33, (char)0x9A, (char)0x78,  (char)0x28, (char)0xC2, (char)0xC4,  (char)0x14, (char)0x84, (char)0x98,
  (char)0x5F, (char)0xD1, (char)0xC1,  (char)0xE6, (char)0x87, (char)0xC2,  (char)0x0B, (char)0x72, (char)0x5E,  (char)0xE6, (char)0x8C, (char)0x99,  (char)0x94, (char)0x2A, (char)0x4E,
  (char)0x00, (char)0x63, (char)0x3E,
  (char)(3 * 61),
  (char)0xF2, (char)0x08, (char)0x51,  (char)0xC8, (char)0x77, (char)0x8F,  (char)0x08, (char)0x4E, (char)0x26,  (char)0x4C, (char)0xE7, (char)0xC1,  (char)0x0F, (char)0x9A, (char)0xAF,
  (char)0x45, (char)0xB3, (char)0x9F,  (char)0x41, (char)0xC8, (char)0xC8,  (char)0xDC, (char)0x69, (char)0x8F,  (char)0x06, (char)0x7E, (char)0x5F,  (char)0xC6, (char)0x6D, (char)0x85,
  (char)0xD5, (char)0x3F, (char)0x3B,  (char)0x0A, (char)0x77, (char)0x68,  (char)0xF2, (char)0x73, (char)0x81,  (char)0x05, (char)0x76, (char)0x6C,  (char)0x29, (char)0xA7, (char)0x98,
  (char)0x26, (char)0xA6, (char)0xAC,  (char)0x24, (char)0xA5, (char)0xAB,  (char)0x19, (char)0x96, (char)0x9F,  (char)0x1F, (char)0xA7, (char)0x9F,  (char)0xDD, (char)0x6E, (char)0x8A,
  (char)0xE2, (char)0x60, (char)0x61,  (char)0xD2, (char)0x5F, (char)0x6B,  (char)0xD6, (char)0x58, (char)0x55,  (char)0xFF, (char)0x71, (char)0x5D,  (char)0x15, (char)0x99, (char)0x8C,
  (char)0x11, (char)0x92, (char)0x93,  (char)0x2F, (char)0xAC, (char)0x9D,  (char)0x2C, (char)0xAE, (char)0xAD,  (char)0x1C, (char)0x9F, (char)0xA3,  (char)0x07, (char)0x89, (char)0x8E,
  (char)0xEA, (char)0x7B, (char)0x7F,  (char)0xDE, (char)0x56, (char)0x59,  (char)0xE5, (char)0x64, (char)0x62,  (char)0xD7, (char)0x50, (char)0x59,  (char)0x01, (char)0x72, (char)0x65,
  (char)0x19, (char)0x98, (char)0x90,  (char)0x0C, (char)0x8B, (char)0x93,  (char)0x2D, (char)0xA0, (char)0x95,  (char)0x1B, (char)0xA5, (char)0xAF,  (char)0xFD, (char)0x85, (char)0x90,
  (char)0xFD, (char)0x7C, (char)0x7C,  (char)0xE6, (char)0x73, (char)0x7D,  (char)0xD7, (char)0x56, (char)0x5D,  (char)0xE6, (char)0x5F, (char)0x5B,  (char)0x00, (char)0x7E, (char)0x72,
  (char)0xEE, (char)0x77, (char)0x7E,  (char)0x0D, (char)0x80, (char)0x71,  (char)0x1E, (char)0xA0, (char)0x98,  (char)0x07, (char)0x8E, (char)0x94,  (char)0x0A, (char)0x83, (char)0x81,
  (char)0x0D, (char)0x91, (char)0x91,  (char)0xE8, (char)0x73, (char)0x81,  (char)0xF0, (char)0x69, (char)0x64,  (char)0xF4, (char)0x76, (char)0x74,  (char)0xE7, (char)0x6A, (char)0x6E,
  (char)0xF5, (char)0x6E, (char)0x68,  (char)0x03, (char)0x7F, (char)0x7A,  (char)0x08, (char)0x84, (char)0x82,  (char)0x09, (char)0x89, (char)0x8A,  (char)0x10, (char)0x8A, (char)0x88,
  (char)0x12, (char)0x94, (char)0x93,
  (char)(3 * 43),
  (char)0xF2, (char)0x08, (char)0x8D,  (char)0x02, (char)0x86, (char)0x8C,  (char)0xF8, (char)0x78, (char)0x7E,  (char)0xFA, (char)0x79, (char)0x78,  (char)0xED, (char)0x73, (char)0x78,
  (char)0xEE, (char)0x6A, (char)0x6B,  (char)0x01, (char)0x7D, (char)0x76,  (char)0xFE, (char)0x80, (char)0x81,  (char)0x04, (char)0x81, (char)0x7E,  (char)0x11, (char)0x8E, (char)0x89,
  (char)0x05, (char)0x8C, (char)0x91,  (char)0x03, (char)0x7F, (char)0x80,  (char)0x02, (char)0x84, (char)0x84,  (char)0xF9, (char)0x7B, (char)0x7C,  (char)0xF6, (char)0x77, (char)0x77,
  (char)0xFC, (char)0x79, (char)0x77,  (char)0xFE, (char)0x7E, (char)0x7D,  (char)0xFE, (char)0x7E, (char)0x7E,  (char)0x04, (char)0x80, (char)0x7E,  (char)0x08, (char)0x89, (char)0x87,
  (char)0xFF, (char)0x81, (char)0x84,  (char)0x03, (char)0x81, (char)0x80,  (char)0x02, (char)0x83, (char)0x84,  (char)0xFE, (char)0x7E, (char)0x81,  (char)0x00, (char)0x7E, (char)0x7D,
  (char)0xFF, (char)0x80, (char)0x80,  (char)0x00, (char)0x80, (char)0x7F,  (char)0x00, (char)0x80, (char)0x81,  (char)0x01, (char)0x80, (char)0x7F,  (char)0x01, (char)0x82, (char)0x82,
  (char)0xFF, (char)0x80, (char)0x80,  (char)0x02, (char)0x81, (char)0x80,  (char)0x00, (char)0x82, (char)0x82,  (char)0x00, (char)0x80, (char)0x80,  (char)0x02, (char)0x82, (char)0x81,
  (char)0x01, (char)0x81, (char)0x81,  (char)0xFF, (char)0x80, (char)0x80,  (char)0x02, (char)0x81, (char)0x80,  (char)0x00, (char)0x81, (char)0x82,  (char)0xFF, (char)0x7F, (char)0x7F,
  (char)0x00, (char)0x80, (char)0x80,  (char)0xFE, (char)0x7F, (char)0x80,  (char)0x00, (char)0x7F, (char)0x7E,
  (char)0
};

	ado_error("%s:enter %d",__func__,__LINE__);

	unsigned char *data = ( unsigned char * ) audio_chime_data;
#define NUM_OF_CLICK_LOCATIONS 171
#define	NUM_OF_CLACK_LOCATIONS 171
		/*config click-clack chime as i2c control mode*/
	x_buf[0] = AUDIO_X_WavTab_UselIO_I2C_MODE;
	
	if(tef6638_write_x_reg(dummy->fd, AUDIO_X_WavTab_UseIOFlag, x_buf, 1, dummy->i2c_addr1))
    {
		return -EINVAL;
	}

    /*disable click-clack chime and load click-clack table wav data*/
	x_buf[0] = AUDIO_EASYP_ClickClack_Disable;
	if(tef6638_write_x_reg(dummy->fd, AUDIO_X_EasyP_Index, x_buf, 1, dummy->i2c_addr1))
	{
		return -EINVAL;
	}
	while ( 1 ) 
	{
		cnt = *data++;
		if ( cnt )
		{   
			if ( tef6638_send ( dummy->fd, data, cnt, dummy->i2c_addr1) ) 
			{
				return -EIO;
			} 
			else 
				break;
		}   
		data += cnt;
	}

	
	
	x_buf[0] = 0x000001;//use xram click clack wave table
	tef6638_write_x_reg(dummy->fd, AUDIO_X_WavTab_UseRamFlag, x_buf, 1, dummy->i2c_addr1);
	x_buf[0] = 0x00000;//reinitialize wave table sample pointer
	tef6638_write_x_reg(dummy->fd, AUDIO_X_WavTab_Pointer, x_buf, 1, dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_WavTab_Ram_BuffStart;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_WavTab_TicStartPntr, x_buf, 1, dummy->i2c_addr1);
	x_buf[0] += NUM_OF_CLICK_LOCATIONS -1;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_WavTab_TicEndPntr, x_buf, 1, dummy->i2c_addr1);
	x_buf[0] = AUDIO_X_WavTab_Ram_BuffStart + NUM_OF_CLICK_LOCATIONS;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_WavTab_TacStartPntr, x_buf, 1, dummy->i2c_addr1);
	x_buf[0] += NUM_OF_CLACK_LOCATIONS - 1;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_WavTab_TacEndPntr, x_buf, 1, dummy->i2c_addr1);
	
	//enable smoothing sup
	x_buf[0] = AUDIO_EASYP_SupPos_Smooth1;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_EasyP_Index, x_buf, 1,dummy->i2c_addr1);
	y_buf[0]=y_buf[1]=y_buf[2]=y_buf[3]=y_buf[4]=y_buf[5] = table_dB2Lin[20];
	tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_Sup_sExtonFL, y_buf, 6, dummy->i2c_addr1);
	y_buf[0] = 0x7F2;
	y_buf[1] = 0x00E;
	tef6638_write_y_reg(dummy->fd, AUDIO_Y_Sup_Filta, y_buf, 2, dummy->i2c_addr1);
	x_buf[0] = AUDIO_EASYP_ClickClack_Enable;
	tef6638_write_x_reg(dummy->fd, AUDIO_X_EasyP_Index, x_buf, 1, dummy->i2c_addr1);//enable or disable click-clack chime generator
	ado_error("%s: exit %d",__func__,__LINE__);
	return 0;
}




#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://svn/product/branches/6.6.0/trunk/hardware/deva/ctrl/mcasp/nto/arm/dll.le.j6_dummy.v7/dummy.c $ $Rev: 759887 $")
#endif
