#ifndef MULTIMEDIACONTROL_H
#define MULTIMEDIACONTROL_H

#include "IControl.h"
#include <string>

class MultimediaControl : public IControl
{
   void operation(int cmd_type);
public:
   MultimediaControl();
   ~MultimediaControl();
   virtual bool match(std::string command);
};

#endif
