#include "NaviControl.h"

#include <stdio.h>
#include <fcntl.h>
#include <iostream>
#include <errno.h>

#define VR_CMD_1 0x01 //Start Navi
#define VR_CMD_2 0x02 //Stop Navi
#define VR_CMD_3 0x03 //Navi to home
#define VR_CMD_4 0x04 //Navi to company
#define VR_CMD_5 0x05 //Navi to someplace
#define VR_CMD_6 0x06 //Traffic query

NaviControl::NaviControl()
{
}

NaviControl::~NaviControl()
{
}

void NaviControl::operation(int cmd_type, std::string dst)
{
   int fd = -1;
   int length = 0;
   char buf[64] = {0};

   switch(cmd_type) {
     case VR_CMD_1:
       //sprintf(buf, "cmd:n:%d",0x01);
       break;
     case VR_CMD_2:
       sprintf(buf, "cmd:n:%d",0x02);
       break;
     case VR_CMD_3:
       sprintf(buf, "cmd:n:%d",0x03);
       break;
     case VR_CMD_4:
       sprintf(buf, "cmd:n:%d",0x04);
       break;
     case VR_CMD_5:
       sprintf(buf, "NavTo::%s",dst.c_str());
       break;
     case VR_CMD_6:
       sprintf(buf, "TrafficQuery::%s",dst.c_str());
       break;
     default:
       break;
   }

   length = strlen(buf);

   fd = open("/pps/services/geolocation/vr_control", O_RDWR | O_CREAT, 0666);
   if (fd > 0)
   {
      if (write(fd, buf, length) == length)
      {
	 std::cout << "NaviControl::operation OK" << std::endl;
      }

      if ( cmd_type == VR_CMD_1 || cmd_type == VR_CMD_3 || cmd_type == VR_CMD_4 || cmd_type == VR_CMD_5 || cmd_type == VR_CMD_6 )
      {
        sprintf(buf, "cmd:n:%d",0x01);
        length = strlen(buf);
        if (write(fd, buf, length) == length)
        {
	  std::cout << "NaviControl::Open navi OK" << std::endl;
        }

        sprintf(buf, "cmd:n:%d",0x00);
        length = strlen(buf);
        if (write(fd, buf, length) == length)
        {
	  std::cout << "NaviControl::Open navi OK" << std::endl;
        }
      }

      close(fd);
   }
   else
   {
      std::cout << "NaviControl::operation open error:" << strerror(errno) << std::endl;
   } 
}

bool NaviControl::match(std::string command)
{
   bool ret = false;
   int cmd = -1;
   int len = 0;
   int size;
   std::string dst;

   if (command.find("打开导航") != std::string::npos || command.find("开启导航") != std::string::npos)
   {
     cmd = VR_CMD_1;  
   } else if (command.find("停止导航") != std::string::npos || command.find("结束导航") != std::string::npos) {
     cmd = VR_CMD_2;
   } else if (command.find("导航到家") != std::string::npos || command.find("回家") != std::string::npos) {
     cmd = VR_CMD_3;
   } else if (command.find("导航到公司") != std::string::npos || command.find("去公司") != std::string::npos) {
     cmd = VR_CMD_4;
   } else if (command.find("导航到") != std::string::npos) {
     /*
     const char *ptr = command.c_str();
     for (int i = 0; i < strlen(ptr); i++)
     {
         printf("%02X", ptr[i]);
     }
     printf("\n\n");
     */
     size = command.find("。");
     printf("\n***size:%d\n",size);
     if (size == std::string::npos) {
        size = command.size();
     }
     printf("\n***command.size:%d\n",command.size());
     len = size - strlen("导航到");
     printf("\n***len:%d\n",len);
     if (len > 0) {
       cmd = VR_CMD_5;
       dst = command.substr(strlen("导航到"), len);
     }
   } else if (command.find("查询") != std::string::npos && command.find("路况") != std::string::npos) {
     len = command.size() - strlen("查询路况") - 3;
     if ( len > 0 ) {
       cmd = VR_CMD_6;
       dst = command.substr(strlen("查询"), len);
     }
   } 

   std::cout << "NaviControl::match state = " << cmd << std::endl;
   std::cout << "Destination:" << dst << std::endl;

   if (cmd != -1)
   {
      operation(cmd,dst);
      ret = true;
   }

   return ret;
}
