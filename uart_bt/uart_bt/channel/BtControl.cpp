#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <hnm/pps.h>

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <string.h>

#include "BtControl.h"
#include "BtApi.h"
#include "uart/uart.h"

#define PPS_BT_SETTINGS_FILE        "/pps/hinge-tech/bt/settings?delta"

extern uint8_t RxdBuffer[READ_BUF_MAX_SIZE];
extern int uart_update_flag;

const uint8_t DISCOVERY_DEVICE[15] = "AT+B INQU 1\r";
const uint8_t CONNECT_DEVICE[15] = "AT+B A2DPCONN ";

BtControl::BtControl(int channel)
   : IChannel(channel)
{

}

BtControl::~BtControl()
{
}

bool BtControl::process(int pps_fd)
{
    char buf[512];
    int nread = 0;
    pps_decoder_t decoder;
    pps_decoder_error_t err;
    const char *tmp_cmd = NULL;

    if (pps_fd < 0) {
        return false;
    }
	
    memset(buf, 0, 512);
	
    if ((nread = read(pps_fd, buf, 512)) <= 0) {
        std::cout << __FUNCTION__ << "read error: " << strerror(errno) << std::endl;
        return false;
    }

    printf("Bt PPS Send\nbuf: %s\n", buf);

    pps_decoder_initialize(&decoder, NULL);
    pps_decoder_parse_pps_str(&decoder, buf);
    pps_decoder_push(&decoder, NULL);

    /** Decode bt set local device name command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "set_name", &tmp_cmd);
    if(err == PPS_DECODER_OK) {
        printf("Command: set_name\n");
        /** Write new device name into pps */
        int settings_fd = -1;
        if((settings_fd = open(PPS_BT_SETTINGS_FILE, O_RDWR)) <= 0) {
            printf("pps file open failed!\n\n");
        }
        else {
            char tmp_buf[32];
            sprintf(tmp_buf, "name::%s\n", tmp_cmd);

            int length = strlen(tmp_buf);

            if (write(settings_fd, tmp_buf, length) != length) {
                printf("set_name, length = %d\n", length);
                printf("wirte failed!\n\n");
            }

            close(settings_fd);
            setLocalDeviceName((char *)tmp_cmd);
        }
    }

    /** Decode bt discovery command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "discovery", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: discovery\n");

        if(!strcmp(tmp_cmd, "start")) {
            discoveryDevice(1);
        }
        else {
            discoveryDevice(0);
        }
    }

    /** Decode bt pair command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "pair", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: pair\n");

        char dst[2][32];
        split(dst, tmp_cmd, ",");
        if(!strcmp(dst[0], "start")) {
            pairDevice(dst[1]);
        }
    }

    /** Decode bt pair command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "get_paired_devices", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: get_paired_devices\n");

        if(!strcmp(tmp_cmd, "start")) {
            getPairedRecord();
        }
    }

    /** Decode bt scan command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "scan", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: scan\n");

        if(!strcmp(tmp_cmd, "start")) {
            scanDevice(1);
        }
        else {
            scanDevice(0);
        }
    }

    /** Decode bt connection command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "connect_cmd", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: connect_cmd\n");

        if(!strcmp(tmp_cmd, "disconnect")) {
            hfdisconnect2Device();
            pbcdisconnect2Device();
            mapClientdisconnect2Device();
        }
        else {
            char dst[2][32];
            split(dst, tmp_cmd, ",");
            hfconnect2Device(dst[1]);
            pbcconnect2Device(dst[1]);
            connect2MapServer(dst[1]);
            //get_firmware_version();
            //getPairedRecord();
        }
    }

    /** Decode bt delete command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "delete_paired_device", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: delete_paired_device\n");

        deletePairedRecord((char *)tmp_cmd);
    }

    /** Decode music play command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "music_cmd", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: music_cmd\n");

        if(!strcmp(tmp_cmd, "play")) {
            playMusic();
        }
        else if(!strcmp(tmp_cmd, "pause")) {
            pauseMusic();
        }
        else if(!strcmp(tmp_cmd, "stop")) {
            stopMusic();
        }
        else if(!strcmp(tmp_cmd, "prev")) {
            previousMusic();
        }
        else if(!strcmp(tmp_cmd, "next")) {
            nextMusic();
        }
    }

    /** Decode call control command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "call_cmd", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: call_cmd\n");

        if(!strcmp(tmp_cmd, "accept")) {
            acceptCall();
        }
        else if(!strcmp(tmp_cmd, "hangup")) {
            hangUpCall();
        }
//        else if(!strcmp(tmp_cmd, "transfer")) {
//            transferAudio();
//        }
        else if(strstr(tmp_cmd, "dialout") - tmp_cmd == 0) {
            char dst[2][32];
            split(dst, tmp_cmd, ",");
            outgoingCall(dst[1]);
        }
        else if(strstr(tmp_cmd, "dial_num") - tmp_cmd == 0) {
            char dst[2][32];
            split(dst, tmp_cmd, ",");
            enterNumberDuringCall(dst[1]);
        }
    }

    /** Decode message control command */
    tmp_cmd = NULL;
    err = pps_decoder_get_string(&decoder, "map_cmd", &tmp_cmd);
    if (err == PPS_DECODER_OK) {
        printf("Command: map_cmd\n");

        if(!strcmp(tmp_cmd, "get_list")) {
            getMessageList();
        }
        else if(!strcmp(tmp_cmd, "get_more_data")) {
            getMessageMoreData();
        }
        else if(strstr(tmp_cmd, "get_message") - tmp_cmd == 0) {
            char dst[2][32];
            split(dst, tmp_cmd, " ");
            getMessages(dst[1]);
        }
        else if(!strcmp(tmp_cmd, "get_prev_indicate")) {
            getMessagePrevIndication();
        }
        else if(strstr(tmp_cmd, "push_message") - tmp_cmd == 0) {
            // TUDO
        }
        else if(!strcmp(tmp_cmd, "terminate_opt")) {
            terminateOperation();
        }
    }

    pps_decoder_pop(&decoder);
    pps_decoder_cleanup(&decoder);

    return true;
}

