objects = main.o uart.o BtControl.o BtApi.o
libs = -lpps -lsqlite3 -lz #-liconv
includes = -I.
libPath = 

all: $(objects) 
	QCC -Vgcc_ntoarmv7le -Wall -o uart_bt $(objects) $(libs) $(libPath)
	rm $(objects)

main.o: main.cpp
	QCC -Vgcc_ntoarmv7le -Wall -c main.cpp $(includes)

uart.o: uart/uart.cpp uart/uart.h
	QCC -Vgcc_ntoarmv7le -Wall -c uart/uart.cpp $(includes)

BtControl.o: channel/BtControl.cpp channel/BtControl.h
	QCC -Vgcc_ntoarmv7le -Wall -c channel/BtControl.cpp $(includes)

BtApi.o: channel/BtApi.cpp channel/BtApi.h
	QCC -Vgcc_ntoarmv7le -Wall -c channel/BtApi.cpp $(includes)

clean:
	rm uart_ipc $(objects)
