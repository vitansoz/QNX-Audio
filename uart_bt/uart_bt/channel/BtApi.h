#ifndef BTAPI_H
#define BTAPI_H

/** Global function */
/** splite string with special char */
int split(char dst[][32], const char* str, const char* spl);

bool init_status();
bool get_firmware_version();
/** Hardware connection/disconnection */
bool discoveryDevice(int operation);
bool scanDevice(int operation);
void pairDevice(char * deviceAddress);
bool hfconnect2Device(char * deviceAddress);
bool hfdisconnect2Device();
bool setLocalDeviceName(char * deviceName);
bool pbcconnect2Device(char * deviceAddress);
bool pbcdisconnect2Device();
bool queryRemoteDeviceName(char * deviceAddress);
bool getPairedRecord();
bool deletePairedRecord(char * deviceAddress);
void connect2MapServer(char * deviceAddress);
void mapClientdisconnect2Device();

/** Music control */
bool playMusic();
bool pauseMusic();
bool stopMusic();
bool previousMusic();
bool nextMusic();

/** Call control */
bool acceptCall();
bool outgoingCall(char *callNumber);
bool hangUpCall();
bool transferAudio();
bool enterNumberDuringCall(char * number);
bool setParsePhoneBook();
bool getPhoneBook();

/** Message control */
void getMessageList();
void getMessageMoreData();
void getMessages(char *handle);
void getMessagePrevIndication();
void pushMessages();
void terminateOperation();

#endif
