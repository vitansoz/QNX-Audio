#ifndef UART_H
#define UART_H

#include <stdint.h>

#define READ_BUF_MAX_SIZE       1024

int uart_init(const char* uart_dev);
void uart_exit();
void open_pb_database();
void open_msg_database();
bool insert_record(char *name, char *number);
bool clear_table();
void* uart_read_thread_func(void*);
void* uart_bt_status_process_thread_func(void* bt_status_fd);
int uart_send(const uint8_t *data, int len);

#endif
