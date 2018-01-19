#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <pthread.h>
#include <iostream>
#include <vector>
#include <sys/time.h>

#include "uart.h"
#include "channel/BtApi.h"
#include <sqlite3.h>

const char DEVICE_FOUND[10] = "AT-B INQR";
const char A2DPCONN[14] = "AT-B A2DPCONN";

class Data
{
public:
    explicit Data(const uint8_t *data,  int size)
    {
        mData = new uint8_t[size];
        memcpy(mData, data, size);
        mSize = size;
    }

    ~Data()
    {
        delete mData;
    }

    uint8_t *mData;
    int mSize;
};

static uint8_t btRxdBuffer[READ_BUF_MAX_SIZE];
int m_index = 0;

static int s_uart_fd = -1;

std::vector<class Data*> sData(0);
static pthread_t send_tid = -1;

static pthread_mutex_t bt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bt_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t send_cond = PTHREAD_COND_INITIALIZER;

struct timeval tv;
sqlite3 * pb_db = 0;
sqlite3 * msg_db = 0;

void* uart_send_thread_func(void* arg)
{
    class Data *data;
    int ret = 0;

    while (1) {
        while (sData.size() > 0) {
            data = sData[0];
            ret = write(s_uart_fd, data->mData, data->mSize);

            if (ret < 0) {
                printf("%s:%d write error: %s\n", __FUNCTION__, __LINE__, strerror(errno));
            }

            printf("uart_send data:");
            for (int i = 0; i < data->mSize; i++) {
                printf("%c", data->mData[i]);
            }
            printf("\n\n");
            delete data;
            sData.erase(sData.begin());
            usleep(10000);
        }

        pthread_mutex_lock(&send_mutex);
        pthread_cond_wait(&send_cond, &send_mutex);
        pthread_mutex_unlock(&send_mutex);
    }

    pthread_exit(0);
}

int uart_init(const char* uart_dev)
{
    s_uart_fd = open(uart_dev, O_RDWR | O_NOCTTY);
    if (s_uart_fd < 0) {
        printf("open %s error: %s\n", uart_dev, strerror(errno));
    }

    pthread_create(&send_tid, NULL, &uart_send_thread_func, NULL);

    return 0;
}

void uart_exit()
{
    pthread_join(send_tid, NULL);

    if (s_uart_fd) {
        close(s_uart_fd);
    }
}

void open_pb_database()
{
    const char * sSQL1 = "create table phonebook(username text, phone_number varchar(32));";
    char * pErrMsg = 0;
    //connect to database
    int ret = sqlite3_open("/db/phonebook.db", &pb_db);
    if(ret != SQLITE_OK) {
        fprintf(stderr, "could not open database: %s", sqlite3_errmsg(pb_db));
        return;
    }
    printf("database connect succeeded!\n");

    //exec sql command
    sqlite3_exec(pb_db, sSQL1, 0, 0, &pErrMsg);
    if(ret != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", pErrMsg);
        sqlite3_free(pErrMsg);
    }
}

void open_msg_database()
{
    const char * sSQL1 = "create table android_msg(sender text, msg_text text);";
    char * pErrMsg = 0;
    //connect to database
    int ret = sqlite3_open("/db/messages.db", &msg_db);
    if(ret != SQLITE_OK) {
        fprintf(stderr, "could not open database: %s", sqlite3_errmsg(msg_db));
        return;
    }
    printf("database connect succeeded!\n");

    //exec sql command
    sqlite3_exec(msg_db, sSQL1, 0, 0, &pErrMsg);
    if(ret != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", pErrMsg);
        sqlite3_free(pErrMsg);
    }
}

bool insert_record(char *name, char *number)
{
    char * pErrMsg = 0;
    char tmpbuf[128];
    sprintf(tmpbuf, "insert into phonebook values('%s','%s');", name, number);
    int result = sqlite3_exec(pb_db, tmpbuf, 0, 0, &pErrMsg);
    if(result != SQLITE_OK){
        printf("err no = %d\n", result);
        printf("Record insert failed\n");
        return false;
    }

    return true;
}

bool clear_table()
{
    char * pErrMsg = 0;
    char tmpbuf[512];
    sprintf(tmpbuf, "delete from phonebook;");
    int result = sqlite3_exec(pb_db, tmpbuf, 0, 0, &pErrMsg);
    if(result != SQLITE_OK){
        printf("Record clear failed\n");
        return false;
    }
    return true;
}

void* uart_read_thread_func(void*)
{
    int size = 0;
    uint8_t buf[READ_BUF_MAX_SIZE];

    memset(buf, 0, READ_BUF_MAX_SIZE);

    while (1) {
        size = read(s_uart_fd, buf, READ_BUF_MAX_SIZE);
        if(size <= 0) {
            //printf("%s:%d read error: %s\n", __FUNCTION__, __LINE__, strerror(errno));
            continue;
        }

        if(buf[size - 1] == '\r') {
            strncpy((char *)btRxdBuffer + m_index, (char *)buf, size);
            m_index += size;
            btRxdBuffer[m_index] = '\0';

            pthread_mutex_lock(&bt_mutex);
            pthread_cond_signal(&bt_cond);
            pthread_mutex_unlock(&bt_mutex);
        }
        else {
            strncpy((char *)btRxdBuffer + m_index, (char *)buf, size);
            m_index += size;
        }

        memset(buf, 0, size);
    }

    return NULL;
}

void* uart_bt_status_process_thread_func(void* bt_status_fd)
{
    int pps_fd = *((int*)bt_status_fd);
    int length = 0;
    int pbab_connected = 0;
    int a2dp_avrcp_connected = 0;
    bool isActiveCall = false;
    char buf[128];
    char tmp[8][32];
    char dst[8][32];
    int remote_device_num = 0;
    char remote_device_addr[32][32];
    char remote_device_name[32][32];

    int paired_device_num = 0;
    char paired_device_addr[32][32];
    char paired_device_name[32][32];
    FILE *fp;

    memset(buf, 0, sizeof(buf));
    memset(dst, 0, sizeof(dst));
    memset(btRxdBuffer, 0, READ_BUF_MAX_SIZE);

    while (1) {
        pthread_mutex_lock(&bt_mutex);
        pthread_cond_wait(&bt_cond, &bt_mutex);
        pthread_mutex_unlock(&bt_mutex);

        if(strstr((char *)btRxdBuffer, "MAPCGETDATAIND") == NULL) {
            int tmpIndex = strstr((char *)btRxdBuffer, "AT-B") - (char *)btRxdBuffer;

            if(tmpIndex >= 0 && tmpIndex <= 10) {
                split(dst, (const char *)btRxdBuffer, " ");
            }
            else {
                m_index = 0;
                continue;
            }
            m_index = 0;
        }
        else {
            /** Get list object command's status */
            /** Save xml */
            char * packet_ptr = NULL;
            if(btRxdBuffer[27] == ',') {
                packet_ptr = (char *)(btRxdBuffer + 28);
            }
            else if(btRxdBuffer[28] == ',') {
                packet_ptr = (char *)(btRxdBuffer + 29);
            }
            else if(btRxdBuffer[29] == ',') {
                packet_ptr = (char *)(btRxdBuffer + 30);
            }
            //printf("%s\n", btRxdBuffer);

            fwrite(packet_ptr, strlen(packet_ptr) - 1, 1, fp);

            /** Has more message data to be read */
            if(btRxdBuffer[24] == '1') {
                sprintf(buf, "map_msg_more_data::1\n");
            }
            else {
                /** Parse xml */
                fclose(fp);
                system("/extra/Hinge_Apps/bin/read_messages &");
            }
        }

        if(!strcmp(dst[1], "INQR")) {
            /** A device detected */
            int address_exist = 0;
            split(tmp, (const char *)dst[2], ",");

            for(int i=0;i<remote_device_num;i++) {
                if(!strcmp(remote_device_addr[i], tmp[0])) {
                    address_exist = 1;
                    break;
                }
            }

            if(!address_exist) {
                sprintf(buf, "discovery_status::start\n");
                strcpy(remote_device_addr[remote_device_num], tmp[0]);
                strcpy(remote_device_name[remote_device_num], tmp[2]);
                sprintf(buf, "remote_device%d::%s,%s", remote_device_num,
                        remote_device_addr[remote_device_num], remote_device_name[remote_device_num]);
                remote_device_num ++;
            }
        }
        else if(!strcmp(dst[1], "INQC\r")) {
            /** The device discovery procedure completes */
            printf("Device discovery stopped\n");
            sprintf(buf, "discovery_status::stop\n");
            remote_device_num = 0;
            memset(remote_device_addr, 0, sizeof(remote_device_addr));
            memset(remote_device_name, 0, sizeof(remote_device_name));
        }
        else if(!strcmp(dst[1], "GPRD")) {
            /** Get paired device */
            printf("Receive paired device info\n");
            split(tmp, (const char *)dst[2], ",");
            if(!strcmp(tmp[0], "0")) {
                printf("No paired device record\n");
                remote_device_num = 0;
            }
            else {
                char tmp2[8][32];
                split(tmp2, (const char *)tmp[2], "\r");
                printf("Get a paired device record\n");
                strcpy(paired_device_addr[paired_device_num], tmp2[0]);
                strcpy(paired_device_name[paired_device_num], tmp2[0]);
                sprintf(buf, "paired_device%d::%s,%s", paired_device_num,
                        paired_device_addr[paired_device_num], paired_device_name[paired_device_num]);
                remote_device_num ++;

                if(remote_device_num >= 31) {
                    remote_device_num = 0;
                }
            }



            remote_device_num = 0;
            memset(remote_device_addr, 0, sizeof(remote_device_addr));
            memset(remote_device_name, 0, sizeof(remote_device_name));
        }
        else if(!strcmp(dst[1], "PAIR")) {
            /** Pairing succeeded */
            printf("Pair status\n");
            split(tmp, (const char *)dst[2], ",");
            if(!strcmp(tmp[0], "0")) {
                sprintf(buf, "pair::succeeded\n");
            }
            else {
                sprintf(buf, "pair::failed\n");
            }
        }
        else if(!strcmp(dst[1], "HFDISC")) {
            /** The HFP connection is closed */
            printf("HFP disconnected\n");
            sprintf(buf, "connect_status::disconnected\n");
            clear_table();
        }
        else if(!strcmp(dst[1], "HFSTAT")) {
            /** Module status */
            if(dst[2][0] == '1') {
                /** Module is in ready status(disconnected) */
                printf("Device ready\n");
                sprintf(buf, "call_status::ready\n");
                isActiveCall = false;
            }
            else if(dst[2][0] == '2') {
                /** Module is in connecting status */
                printf("Device connecting\n");
                sprintf(buf, "connect_status::connecting\n");
                isActiveCall = false;
            }
            else if(dst[2][0] == '3') {
                if(!isActiveCall) {
                    /** Module is in connected status, no active call */
                    printf("Device connected\n");
                    sprintf(buf, "connect_status::connected\n");
                }
                else {
                    /** Hang up an active call */
                    printf("Hang up an active call\n");
                    sprintf(buf, "call_status::ready\n");
                    isActiveCall = false;
                }
            }
            else if(dst[2][0] == '4') {
                /** An incoming call arrives */
                printf("Incoming call\n");
                sprintf(buf, "call_status::incoming_call\n");
            }
            else if(dst[2][0] == '5') {
                /** An outgoing call is established */
                printf("Outgoing call\n");
                sprintf(buf, "call_status::outgoing_call\n");
            }
            else if(dst[2][0] == '6') {
                /** An active call is established */
                printf("Active call\n");
                sprintf(buf, "call_status::active_call\n");
                isActiveCall = true;
            }
            else {
                printf("===========Get unknown HFSTAT\n");
            }
        }
        else if(!strcmp(dst[1], "HFCHUP")) {
            /** Hang up a call */
            if(dst[2][0] == '0') {
                /** A call is successfully hung up */
                sprintf(buf, "call_status::ready\n");
            }
        }
//        else if(!strcmp(dst[1], "HFAUDIO")) {
//            /** AG Hang up a call */
//            if(dst[2][0] == '0') {
//                /** A call is successfully hung up */
//                sprintf(buf, "call_status::ready\n");
//            }
//        }
        else if(!strcmp(dst[1], "SLDN")) {
            /** Set local device name */
            if(dst[2][0] == '0') {;
                /** Module is in ready status(disconnected) */
                printf("Set device name succeeded\n");
            }
        }
        else if(!strcmp(dst[1], "HFCCIN")) {
            /** Inform the caller id(phone number) of the incoming call */
            printf("call number");
            split(tmp, (const char *)dst[2], ",");
            sprintf(buf, "call_num::%s\n", tmp[6]);
        }
        else if(!strcmp(dst[1], "HFCLIP")) {
            /** Inform the caller id(phone number) of the incoming call */
            printf("Incoming call number");
            sprintf(buf, "incoming_call_num::%s\n", dst[2]);
        }
        else if(!strcmp(dst[1], "PBCSTAT")) {
            /** PBAP status */
            if(dst[2][0] == '1') {
                pbab_connected = 0;
                printf("PBAP ready\n");
            }
            else if(dst[2][0] == '3') {
                if(pbab_connected == 0) {
                    pbab_connected = 1;
                    printf("PBAP connected\n");
                    setParsePhoneBook();
                    sprintf(buf, "pbap_connect_status::connected\n");
                }
            }
        }
        else if(!strcmp(dst[1], "PBCSETPARSE")) {
            /** PhoneBook parsed */
            //printf("PhoneBook parsed\n");
            getPhoneBook();
        }
        else if(!strcmp(dst[1], "PBCPARSEDATAIND")) {
            /** Get a phonebook record from the mobile phone storage */
            /** All phonebook record should be writen into sql database, int the database
             *  table, records should be unique
             */
            //printf("Get phone book record\n");
            split(tmp, (const char *)dst[2], ",");
            insert_record(tmp[3], tmp[2]);
        }
        else if(!strcmp(dst[1], "PBCPULLCMTIND\r")) {
            /** phonebook sync finished */
            sprintf(buf, "pbap_sync_status::synced\n");
        }
        else if(!strcmp(dst[1], "AVRCPSTAT")) {
            /** A2DP_AVRCP status */
            if(dst[2][0] == '1') {
                a2dp_avrcp_connected = 0;
                printf("A2DP_AVRCP ready\n");
            }
            else if(dst[2][0] == '3') {
                if(a2dp_avrcp_connected == 0) {
                    a2dp_avrcp_connected = 1;
                    printf("A2DP_AVRCP connected\n");
                    sprintf(buf, "a2dp_avrcp_connect_status::connected\n");
                }
            }
        }
        else if(!strcmp(dst[1], "AVRCPPOS")) {
            /** Music playing position */
            printf("Music pos update\n");
            sprintf(buf, "music_pos:n:%s\n", dst[2]);
        }
        else if(!strcmp(dst[1], "PLAYSTATUS")) {
            /** Music status */
            if(!strcmp(dst[1], "0")) {
                /** Music is stopped */
                printf("Music stopped\n");
                sprintf(buf, "music_status::stopped\n");
            }
            else if(!strcmp(dst[1], "1")) {
                /** Music is playing */
                printf("Music playing\n");
                sprintf(buf, "music_status::playing\n");
            }
            else if(!strcmp(dst[1], "2")) {
                /** Music is paused */
                printf("Music paused\n");
                sprintf(buf, "music_status::paused\n");
            }
        }
        else if(!strcmp(dst[1], "AVRCPTITLE")) {
            /** Music Title */
            printf("Music title\n");
            sprintf(buf, "music_title::%s\n", btRxdBuffer + 16);
        }
        else if(!strcmp(dst[1], "AVRCPARTIST")) {
            /** Music artist */
            printf("Music artist\n");
            sprintf(buf, "music_artist::%s\n", btRxdBuffer + 17);
        }
        else if(!strcmp(dst[1], "AVRCPALBUM")) {
            /** Music album */
            printf("Music album\n");
            sprintf(buf, "music_album::%s\n", btRxdBuffer + 16);
        }
        else if(!strcmp(dst[1], "AVRCPTIME")) {
            /** Music total time */
            printf("Music total time\n");
            sprintf(buf, "music_during:n:%s\n", dst[2]);
        }
        else if(!strcmp(dst[1], "GVER")) {
            /** Get firmware version */
            printf("Get firmware version : %s\n", dst[2]);
            //sprintf(buf, "Firmware version:n:%s\n", dst[2]);
        }
        else if(!strcmp(dst[1], "MAPCCONN")) {
            /** MAP status */
            /** Get map connect command's status */
            split(tmp, (const char *)dst[2], ",");
            if(!strcmp(tmp[0], "1")) {
                sprintf(buf, "map_connect_status::connected\n");
                fp = fopen("/tmp/messages.xml", "w");
            }
            else{
                sprintf(buf, "map_connect_status::disconnected\n");
            }
        }

        if(buf[0] != 0) {
            length = strlen(buf);

            if (write(pps_fd, buf, length) != length) {
                printf("generic, length = %d\n", length);
                printf("wirte failed!\n\n");
            }
        }

        memset(buf, 0, sizeof(buf));
        memset(dst, 0, sizeof(dst));
        memset(btRxdBuffer, 0, READ_BUF_MAX_SIZE);
    }

    return NULL;
}

int uart_send(const uint8_t *data, int len)
{
    class Data *d = NULL;
    try {
       d = new  Data(data, len);
    }
    catch(...) {
        return 0;
    }

    sData.push_back(d);

    if (sData.size() == 1) {
        pthread_mutex_lock(&send_mutex);
        pthread_cond_signal(&send_cond);
        pthread_mutex_unlock(&send_mutex);
    }

    return len;
}
