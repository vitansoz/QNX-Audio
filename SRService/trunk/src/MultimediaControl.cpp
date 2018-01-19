#include "MultimediaControl.h"

#include <stdio.h>
#include <fcntl.h>
#include <iostream>
#include <errno.h>

#define VR_CMD_1 0x01 //Open Radio
#define VR_CMD_2 0x02 //Play Music
#define VR_CMD_3 0x03 //Play Video

MultimediaControl::MultimediaControl()
{
}

MultimediaControl::~MultimediaControl()
{
}

void MultimediaControl::operation(int cmd_type)
{
   int fd = -1;
   int length = 0;
   char buf[64] = {0};

   switch(cmd_type) {
     case VR_CMD_1:
       sprintf(buf, "multimedia::%s","radio");
       break;
     case VR_CMD_2:
       sprintf(buf, "multimedia::%s","music");
       break;
     case VR_CMD_3:
       sprintf(buf, "multimedia::%s","video");
       break;
     default:
       break;
   }

   length = strlen(buf);

   fd = open("/pps/hinge-tech/music_control", O_RDWR | O_CREAT, 0666);
   if (fd > 0)
   {
      if (write(fd, buf, length) == length)
      {
	 std::cout << "MultimediaControl::operation OK" << std::endl;
      }

      sprintf(buf, "multimedia::%s","none");
      length = strlen(buf);
      if (write(fd, buf, length) == length)
      {
	 std::cout << "MultimediaControl::operation OK" << std::endl;
      }

      close(fd);
   }
   else
   {
      std::cout << "MultimediaControl::operation error:" << strerror(errno) << std::endl;
   } 
}

bool MultimediaControl::match(std::string command)
{
   bool ret = false;
   int cmd = -1;
   int len = 0;
   int size;
   std::string dst;

   if (command.find("打开收音机") != std::string::npos)
   {
     cmd = VR_CMD_1;  
   } else if (command.find("播放音乐") != std::string::npos) {
     cmd = VR_CMD_2;
   } else if (command.find("播放视频") != std::string::npos) {
     cmd = VR_CMD_3;
   } 
   
   std::cout << "MultimediaControl::match state = " << cmd << std::endl;

   if (cmd != -1)
   {
      operation(cmd);
      ret = true;
   }

   return ret;
}
