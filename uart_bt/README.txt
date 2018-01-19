execute command below to start uart_bt process
1.slay devc-seromap_hci
2.devc-seromap -E -F -b115200 -c48000000/16 0x48020000^2,106 -u3
3.mv read_messages /extra/Hinge_Apps/qtcar/bin/read_messages

then run "uart_bt"
