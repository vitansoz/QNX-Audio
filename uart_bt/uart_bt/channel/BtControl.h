#ifndef BTCONTROL_H
#define BTCONTROL_H

#include "IChannel.h"

class BtControl : public IChannel
{
public:
        BtControl(int channel);
        virtual ~BtControl();

	bool process(int pps_fd);
};

#endif
