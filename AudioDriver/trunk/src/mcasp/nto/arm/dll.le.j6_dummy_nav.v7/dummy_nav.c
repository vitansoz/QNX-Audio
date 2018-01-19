/*
 * $QNXLicenseC:
 * Copyright 2014, QNX Software Systems.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You
 * may not reproduce, modify or distribute this software except in
 * compliance with the License. You may obtain a copy of the License
 * at: http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */



/*
 *    dummy.c
 *      The primary interface into the dummy codec.
 */

struct dummy_context;
#define  MIXER_CONTEXT_T struct dummy_context

#include "mcasp.h"
#include <hw/i2c.h>
#include "tef6638.h"

typedef struct dummy_context
{
	ado_mixer_t *mixer;
	HW_CONTEXT_T *hwc;
	int fd;
	
	int     i2c_addr;
	int8_t  volume;
	int8_t  gain;	
	int8_t  sec_volume;
}
dummy_context_t;




static int32_t dummy_output_nav_vol_control(MIXER_CONTEXT_T * dummy, ado_mixer_delement_t * element, uint8_t set,
						   uint32_t * vol, void *instance_data)
{
	int32_t altered = 0,db = 0;
	short int y_buf[1]={0};
	if(set)
	{
		  altered = vol[0] != (dummy->volume & 0x7f);
		
		  dummy->volume &= 0x80;
		  dummy->volume |= (vol[0] & 0x7f);

		  db = phone_nav_volume[(dummy->volume&0x7F)/3];
		    
		  y_buf[0] = table_dB2Lin[NvPhBoostMax - db];	
		  tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_Nav, y_buf, 1, dummy->i2c_addr);	
	}
	else
	{	
		vol[0] = (dummy->volume & 0x7f);
	
	}
	return altered;

}

static int32_t dummy_output_nav_gain_control(MIXER_CONTEXT_T * dummy, ado_mixer_delement_t * element, uint8_t set,
						   uint32_t * vol, void *instance_data)
{
    int32_t altered = 0,db = 0,MaxBoost;
	short int y_buf[1]={0};
	MaxBoost = NvPhBoostMax;
	if(set)
	{
		  altered = vol[0] != (dummy->gain & 0x7f);
		
		  dummy->gain &= 0x80;
		  dummy->gain |= (vol[0] & 0x7f);
		  db = phone_nav_gain[(dummy->gain&0x7F)/3];
		
		  y_buf[0] = table_dB2Lin[MaxBoost - db];	
		  tef6638_write_y_reg(dummy->fd, AUDIO_Y_Vol_ChanGainN, y_buf, 1, dummy->i2c_addr);	
	}
	else
	{	
		vol[0] = (dummy->gain & 0x7f);
	
	}
	return altered;

	


}

static int32_t dummy_output_nav_mute_control(MIXER_CONTEXT_T * dummy, ado_mixer_delement_t * element, uint8_t set,
						   uint32_t * val, void *instance_data)
{
	int32_t altered = 0;
	short int y_buf[1] = {0};
	if(set)
	{
		  altered = val[0] != ((dummy->volume & 0x80)>>7);  
		  dummy->volume &= 0x7f;
		  dummy->volume |= (val[0]&0x01 )<<7; 	 
		  y_buf[0] = ((dummy->volume & 0x80) ? 0x000 : 0x7FF);
		  tef6638_write_y_reg( dummy->fd, AUDIO_Y_Mute_N , y_buf, 1, dummy->i2c_addr);	
	}
	else
	{	
		val[0] = ((dummy->volume&0x80)>>7);
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

static int32_t build_dummy_mixer(MIXER_CONTEXT_T * dummy, ado_mixer_t * mixer)
{
	//navigation 
	ado_mixer_delement_t *nav_pcm_out = NULL;
	ado_mixer_delement_t *nav_vol = NULL;
	ado_mixer_delement_t *nav_gain = NULL;
	ado_mixer_delement_t *nav_mute = NULL;
	ado_mixer_delement_t *accu = NULL;
	ado_mixer_delement_t *nav_out = NULL;
	
	ado_mixer_dgroup_t *nav_grp = NULL;
	int error = 0;

	ado_error("%s %d: enter",__func__,__LINE__);

	//Nav out
	if (!error && (nav_pcm_out = ado_mixer_element_pcm1(mixer, "Navigation Playback", SND_MIXER_ETYPE_PLAYBACK1, 1, &pcm_devices[NAV_CHANNEL])) == NULL)
		error++;	
		//nav volume

	if(!error && (nav_vol = ado_mixer_element_volume1(mixer, "Navigation Volume", 1, &output_volume_range[NAV_CHANNEL], dummy_output_nav_vol_control,(void *) NULL,NULL)) == NULL)
		error++;

	//if(!error && (nav_gain = ado_mixer_element_volume1(mixer, "Navigation Gain", 1, &output_gain_range[NAV_CHANNEL], dummy_output_nav_gain_control,(void *) NULL,NULL)) == NULL)
	//	error++;

		//nav mute
	if(!error && (nav_mute = ado_mixer_element_sw1(mixer, "Navigation Mute", 1, dummy_output_nav_mute_control,(void *) NULL,NULL)) == NULL)
		error++;
	//route nav_pcm_out to nav volume
	if (!error && ado_mixer_element_route_add(mixer, nav_pcm_out, nav_vol) != 0)
		error++;
	
	    //route nav vol to nav gain
	//if(!error && ado_mixer_element_route_add(mixer, nav_vol , nav_gain) != 0)
	//	error++;
	
		//route nav gain to nav mute
	if(!error && ado_mixer_element_route_add(mixer, nav_vol, nav_mute) != 0)
		error++;
	
	//if(!error && (accu = ado_mixer_element_accu1(mixer, SND_MIXER_ELEMENT_DIGITAL_ACCU, 0)) == NULL)
	//		error++; 
	
	//if(!error && ado_mixer_element_route_add(mixer, nav_mute, accu) != 0)
	//	error++;
	
		
	
//	if(!error && (nav_grp = ado_mixer_playback_group_create(mixer, "Nav Gain", SND_MIXER_CHN_MASK_MONO, nav_gain, NULL)) == NULL)
//		error++;
	

	// ouput
	if (!error && (nav_out = ado_mixer_element_io(mixer, "Nav Ouput", SND_MIXER_ETYPE_OUTPUT, 0, 1, stereo_voices)) == NULL)
		error++;
	
	if (!error && ado_mixer_element_route_add(mixer, nav_mute, nav_out) != 0)
		error++;
	//nav group 
	if(!error && (nav_grp = ado_mixer_playback_group_create(mixer, "Nav Mixer", SND_MIXER_CHN_MASK_MONO, nav_vol, nav_mute)) == NULL)
		error++;
	
	if (!error && ado_mixer_switch_new(mixer, "Audioman Refresh", SND_SW_TYPE_BOOLEAN, 0, dummy_mixer_audioman_refresh_get,
											dummy_mixer_audioman_refresh_set, NULL, NULL) == NULL)
		error++;
	ado_error("%s %d: exit with error = %d",__func__,__LINE__, error);
    return (!error ? 0 : -1);
}

static ado_mixer_reset_t dummy_reset;
static int dummy_reset(MIXER_CONTEXT_T * dummy)
{
	return 0;		
}


static ado_mixer_destroy_t dummy_destroy;
static int dummy_destroy(MIXER_CONTEXT_T * dummy)
{
	short int y_buf[1] = {0};
	//mute channel navigation 
	tef6638_write_y_reg_scratch(dummy->fd, AUDIO_Y_Mute_N, y_buf, 1, dummy->i2c_addr);
	close(dummy->fd);
	ado_free(dummy);
	return (0);
}




int
codec_mixer(ado_card_t * card, HW_CONTEXT_T * hwc)
{
	dummy_context_t *dummy;
	int32_t status;

	ado_error("%s %d :enter", __func__,__LINE__);

	if ((dummy = (dummy_context_t *) ado_calloc(1, sizeof (dummy_context_t))) == NULL)
	{
		ado_error("%s %d :%s", __func__,__LINE__, strerror(errno));
		return (-1);
	}
	if ((status = ado_mixer_create(card, "Navigation", &hwc->mixer, dummy)) != EOK)
	{
		ado_free(dummy);
		return (status);
	}
	dummy->mixer = hwc->mixer;
	dummy->hwc = hwc;
	dummy->i2c_addr = hwc->codec_i2c_addr;
	
	if ((dummy->fd = open(TEF6638_I2C_DEV, O_RDWR)) < 0)
	{
		ado_error("%s %d %s",__func__,__LINE__, strerror(errno));
		ado_free(dummy);
		return (-1);
	}
	hwc->i2c_dev = dummy->fd;

	int speed = TEF6638_I2C_SPEED;
	if(devctl ( dummy->fd , DCMD_I2C_SET_BUS_SPEED, &speed, sizeof (speed), NULL ))
	{
		ado_error("%s %d %s",__func__,__LINE__, strerror(errno));
		ado_free(dummy);
		return (-1);
	}

	// reset codec(s)
	if(dummy_reset(dummy) == -1)
    {
		ado_error("%s %d",__func__,__LINE__);
		close(dummy->fd);
		ado_free(dummy);
		return (-1);
    }


	if (build_dummy_mixer(dummy, dummy->mixer))
	{
		ado_error("%s %d",__func__,__LINE__);
		close(dummy->fd);
		ado_free(dummy);
		return (-1);
	}
	
	ado_mixer_set_reset_func(dummy->mixer, dummy_reset);
	ado_mixer_set_destroy_func(dummy->mixer, dummy_destroy);
	ado_error("%s %d",__func__,__LINE__);

	/* setup mixer controls for pcm  */
	ado_pcm_chn_mixer(hwc->pcm, ADO_PCM_CHANNEL_PLAYBACK,
					  hwc->mixer, ado_mixer_find_element(hwc->mixer, SND_MIXER_ETYPE_PLAYBACK1, "Navigation Playback", 0),
					  ado_mixer_find_group(hwc->mixer, "Nav Mixer", 0));
	ado_error("%s %d",__func__,__LINE__);

	//ado_pcm_chn_mixer(hwc->cap_aif.cap_strm[0].pcm, ADO_PCM_CHANNEL_CAPTURE,
	//				  hwc->mixer, ado_mixer_find_element(hwc->mixer, SND_MIXER_ETYPE_CAPTURE1, SND_MIXER_ELEMENT_CAPTURE, 0),
	//				  ado_mixer_find_group(hwc->mixer, SND_MIXER_GRP_IGAIN, 0));

	ado_error("%s %d: exit",__func__,__LINE__);
	return (0);
}

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://svn/product/branches/6.6.0/trunk/hardware/deva/ctrl/mcasp/nto/arm/dll.le.j6_dummy.v7/dummy.c $ $Rev: 759887 $")
#endif


