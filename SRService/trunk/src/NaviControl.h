#ifndef NAVICONTROL_H
#define NAVICONTROL_H

#include "IControl.h"
#include <string>

class NaviControl : public IControl
{
   void operation(int cmd_type, std::string dst);
public:
   NaviControl();
   ~NaviControl();
   virtual bool match(std::string command);
};

#endif
