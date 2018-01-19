#include <iostream>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <pthread.h>
#include <stdio.h>

#include "uart/uart.h"
#include "channel/BtControl.h"

#define PPS_BT_CONTROL_FILE "/pps/hinge-tech/bt/control?delta"
#define PPS_BT_STATUS_FILE "/pps/hinge-tech/bt/status?delta"

#define UART_DEV_FILE "/dev/ser2"

int main(int argc, char** argv)
{
    BtControl btControl(1);

    int bt_ctl_fd = -1;
    int bt_sts_fd = -1;

    fd_set rfds;
    int max_fd = 0;

    pthread_t readpthr;
    pthread_t btpthr;

    if (argc > 1)
    {
        uart_init(argv[argc - 1]);
    }
    else
    {
        while(access(UART_DEV_FILE, 0));
        uart_init(UART_DEV_FILE);
    }

    /** Open database */
    open_pb_database();
    open_msg_database();

    if ((bt_ctl_fd = open(PPS_BT_CONTROL_FILE, O_RDWR)) < 0)
    {
        std::cout << "open " << PPS_BT_CONTROL_FILE << " error: " << strerror(errno) << std::endl;
    }

    if ((bt_sts_fd = open(PPS_BT_STATUS_FILE, O_RDWR)) < 0)
    {
        std::cout << "open " << PPS_BT_STATUS_FILE << " error: " << strerror(errno) << std::endl;
    }

    max_fd = max_fd > bt_ctl_fd ? max_fd : bt_ctl_fd;
    max_fd = max_fd > bt_sts_fd ? max_fd : bt_sts_fd;

    pthread_create(&btpthr, NULL, &uart_bt_status_process_thread_func, &bt_sts_fd);
    pthread_create(&readpthr, NULL, &uart_read_thread_func, NULL);

    while (1)
    {
        FD_ZERO(&rfds);
        FD_SET(bt_ctl_fd, &rfds);

        if (select(max_fd + 1, &rfds, NULL, NULL, NULL) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
        }

        if (FD_ISSET(bt_ctl_fd, &rfds))
        {
            printf("FD_ISSET bt_ctl_fd\n");
            btControl.process(bt_ctl_fd);
        }
    }

    pthread_join(readpthr, NULL);
    pthread_join(btpthr, NULL);

    if (bt_ctl_fd != -1)
    {
        close(bt_ctl_fd);
    }

    if (bt_sts_fd != -1)
    {
        close(bt_sts_fd);
    }

    uart_exit();

    return 0;
}
