#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <pthread.h>
#include <iostream>
#include <vector>
#include <sys/time.h>

#include <iconv.h>
#include "BtApi.h"
#include "uart/uart.h"

int mLights;
int mAllWindows;

bool mInitStatus;
bool mA2DPStatus;
bool mAVRCPStatus;

const uint8_t GET_FIRMWARW_VERSION[] = "AT+B GVER\r";
const uint8_t START_DISCOVERY_DEVICE[] = "AT+B INQU 2\r";
const uint8_t STOP_DISCOVERY_DEVICE[] = "AT+B INQU 0\r";
const uint8_t START_PAIR_DEVICE[] = "AT+B PAIR ";
const uint8_t START_SCAN_DEVICE[] = "AT+B SCAN 3\r";
const uint8_t STOP_SCAN_DEVICE[] = "AT+B SCAN 0\r";
const uint8_t HFCONNECT_DEVICE[] = "AT+B HFCONN ";
const uint8_t HFDISCONNECT_DEVICE[] = "AT+B HFDISC";
const uint8_t PBCCONNECT_DEVICE[] = "AT+B PBCCONN ";
const uint8_t PBCDISCONNECT_DEVICE[] = "AT+B PBCDISC";
const uint8_t SET_DEVICE_NAME[] = "AT+B SLDN ";
const uint8_t QUERY_DEVICE_NAME[] = "AT+B GRDN ";
const uint8_t GET_PAIRED_DEVICE[] = "AT+B GPRD";
const uint8_t DELETE_PAIRED_DEVICE[] = "AT+B DPRD ";

const uint8_t PLAY_MUSIC[] = "AT+B AVRCPPLAY";
const uint8_t PAUSE_MUSIC[] = "AT+B AVRCPPAUSE";
const uint8_t STOP_MUSIC[] = "AT+B AVRCPSTOP";
const uint8_t PREV_MUSIC[] = "AT+B AVRCPBACKWARD";
const uint8_t NEXT_MUSIC[] = "AT+B AVRCPFORWARD";

const uint8_t ACCEPT_CALL[] = "AT+B HFANSW";
const uint8_t OUTGOING_CALL[] = "AT+B HFDIAL 0,";
const uint8_t HANGUP_CALL[] = "AT+B HFCHUP";
const uint8_t TRANSFER_AUDIO[] = "AT+B HFCTRS";
const uint8_t DTMF_CMD[] = "AT+B HFDTMF ";
const uint8_t SETPARSEPB[] = "AT+B PBCSETPARSE 1";
const uint8_t PBCPULLPB[] = "AT+B PBCPULLPB 1,1,65535,0";

//control
const uint8_t MAPCONNECT_DEVICE[] = "AT+B MAPCCONN ";
const uint8_t MAPDISCONNECT_DEVICE[] = "AT+B MAPCDISC ";
const uint8_t MAP_GET_LIST[] = "AT+B MAPCGETML ";
const uint8_t MAP_GET_CONT[] = "AT+B MAPCGETCONT\r";
const uint8_t MAP_GET_MSG[] = "AT+B MAPCGETMSG ";
const uint8_t MAP_GET_CRT[] = "AT+B MAPCGETCRT\r";
const uint8_t MAP_PUSH_MSG[] = "AT+B MAPCPUTMSG ";
const uint8_t MAP_TERM_OPT[] = "AT+B MAPCCMT\r";


int split(char dst[][32], const char* str, const char* spl)
{
    int n = 0;
    char *result = NULL;
    result = strtok((char *)str, spl);
    while( result != NULL )
    {
        strcpy(dst[n++], result);
        result = strtok(NULL, spl);
    }
    return n;
}

bool init_status()
{
    return mInitStatus;
}

bool get_firmware_version()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)GET_FIRMWARW_VERSION);

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

/**
 * @brief discoveryDevice
 * @param operation
 *        value : 0 / 1
 * @return
 */
bool discoveryDevice(int operation)
{
    int length = 0;
    char tmpCmd[64] = "";
    if(operation == 1) {
        strcat(tmpCmd, (char *)START_DISCOVERY_DEVICE);
    }
    else {
        strcat(tmpCmd, (char *)STOP_DISCOVERY_DEVICE);
    }
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

/**
 * @brief scanDevice
 * @param operation
 *        value : 0 / 1
 * @return
 */
bool scanDevice(int operation)
{
    int length = 0;
    char tmpCmd[64] = "";

    if(operation == 1) {
        strcat(tmpCmd, (char *)START_SCAN_DEVICE);
    }
    else {
        strcat(tmpCmd, (char *)STOP_SCAN_DEVICE);
    }
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

void pairDevice(char * deviceAddress)
{
    int length = 0;
    char tmpCmd[64] = "";

    strcat(tmpCmd, (char *)START_PAIR_DEVICE);
    strcat(tmpCmd, deviceAddress);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}

bool hfconnect2Device(char * deviceAddress)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)HFCONNECT_DEVICE);
    strcat(tmpCmd, deviceAddress);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool hfdisconnect2Device()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)HFDISCONNECT_DEVICE);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool setLocalDeviceName(char * deviceName)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)SET_DEVICE_NAME);
    strcat(tmpCmd, deviceName);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool pbcconnect2Device(char * deviceAddress)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)PBCCONNECT_DEVICE);
    strcat(tmpCmd, deviceAddress);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool pbcdisconnect2Device()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)PBCDISCONNECT_DEVICE);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool queryRemoteDeviceName(char * deviceAddress)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)QUERY_DEVICE_NAME);
    strcat(tmpCmd, deviceAddress);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool getPairedRecord()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)GET_PAIRED_DEVICE);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool deletePairedRecord(char * deviceAddress)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)DELETE_PAIRED_DEVICE);
    strcat(tmpCmd, deviceAddress);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

void connect2MapServer(char * deviceAddress)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)MAPCONNECT_DEVICE);
    strcat(tmpCmd, deviceAddress);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}

void mapClientdisconnect2Device()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)MAPDISCONNECT_DEVICE);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}

bool playMusic()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)PLAY_MUSIC);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool pauseMusic()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)PAUSE_MUSIC);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool stopMusic()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)STOP_MUSIC);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool previousMusic()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)PREV_MUSIC);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool nextMusic()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)NEXT_MUSIC);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool acceptCall()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)ACCEPT_CALL);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool outgoingCall(char *callNumber)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)OUTGOING_CALL);
    strcat(tmpCmd, callNumber);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool hangUpCall()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)HANGUP_CALL);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

/**
 * @brief transferAudio
 * When a call is ongoing, the audio can be transferred between
 * HFP-HF(the typical device is headset) device and HFP-AG(the typical device is mobile) device
 * @return
 */
bool transferAudio()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)TRANSFER_AUDIO);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

/**
 * @brief enterNumberDuringCall
 * @param number
 * During an ongoing call, the HF device can instruct the HFP-AG
 * device to transmit a specific DTMF code to its network connection
 * @return
 */
bool enterNumberDuringCall(char * number)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)DTMF_CMD);
    strcat(tmpCmd, number);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool setParsePhoneBook()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)SETPARSEPB);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

bool getPhoneBook()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)PBCPULLPB);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }

    return true;
}

//void getMessageList(int folder, int maxList, int startOffset)
void getMessageList()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)MAP_GET_LIST);
    strcat(tmpCmd, "0,100,0");
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}


void getMessageMoreData()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)MAP_GET_CONT);

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}

void getMessages(char *handle)
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)MAP_GET_MSG);
    strcat(tmpCmd, handle);
    strcat(tmpCmd, "\r");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}

void getMessagePrevIndication()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)MAP_GET_CRT);

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}

void pushMessages(bool moreData)
{
    int length = 0;
    char tmpCmd[156] = "";
    strcat(tmpCmd, (char *)MAP_PUSH_MSG);
    if(moreData) {
        strcat(tmpCmd, "TRUE,");
    }
    else {
        strcat(tmpCmd, "FALSE,");
    }
    strcat(tmpCmd, "128");
    strcat(tmpCmd, "xxxxxxxxxx");

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}

void terminateOperation()
{
    int length = 0;
    char tmpCmd[64] = "";
    strcat(tmpCmd, (char *)MAP_TERM_OPT);

    length = uart_send((const uint8_t *)tmpCmd, strlen(tmpCmd));

    if(length == 0)
    {
        printf("%s:%d cmd send failed\n", __FUNCTION__, __LINE__);
    }
}
