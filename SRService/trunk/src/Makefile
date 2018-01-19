objects = SRService.o PCMRecorder.o PCMPlayer.o TTSPlayer.o AirConditionControl.o DoorControl.o LightControl.o WindowControl.o NaviControl.o MultimediaControl.o
libs = -lisssr -llsa -lissauth -lAECKernel -liFlyNLI -lmsc -ldicompile -lMAEKernel \
       -lisstts -lxml2 -lasound -lpps
includes = -Iinclude
libPath = -L../lib

CXXFLAGS += -std=gnu++11

all: $(objects) 
	QCC -Vgcc_ntoarmv7le -o SRService $(objects) $(libs) $(libPath)

SRService.o: SRService.cpp
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c SRService.cpp $(includes)

PCMRecorder.o: PCMRecorder.cpp PCMRecorder.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c PCMRecorder.cpp $(includes)

PCMPlayer.o: PCMPlayer.cpp PCMPlayer.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c PCMPlayer.cpp $(includes)

TTSPlayer.o: TTSPlayer.cpp TTSPlayer.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c TTSPlayer.cpp $(includes)

AirConditionControl.o: AirConditionControl.cpp AirConditionControl.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c AirConditionControl.cpp $(includes)

DoorControl.o: DoorControl.cpp DoorControl.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c DoorControl.cpp $(includes)

LightControl.o: LightControl.cpp LightControl.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c LightControl.cpp $(includes)

WindowControl.o: WindowControl.cpp WindowControl.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c WindowControl.cpp $(includes)

NaviControl.o: NaviControl.cpp NaviControl.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c NaviControl.cpp $(includes)

MultimediaControl.o: MultimediaControl.cpp MultimediaControl.h
	QCC -Vgcc_ntoarmv7le $(CXXFLAGS) -c MultimediaControl.cpp $(includes)

clean:
	rm SRService $(objects)
