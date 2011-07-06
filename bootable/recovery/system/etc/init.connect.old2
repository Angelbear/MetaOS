#!/sbin/sh
sendmessage 3
if [ 0 == `getprop init.connect.runned 0` ]; then
	mount -t vfat /dev/block/mmcblk0p1 /sdcard/
else
	sendmessage 2 "sdcard already mounted"
fi
sendmessage 1 10 1
if [ 1 == `getprop net.ccinet0.gw 1` ]; then
	if [ 0 == `getprop init.connect.runned 0` ]; then
		sendmessage 2 "begin to connect to network"
	else
		sendmessage 2 "retry to connect to network"
		radiooptions 1
		sleep 5
	fi
	sendmessage 1 40 20
	radiooptions 5
	sleep 20
	sendmessage 2 "network registed, begin to dial..."
	sendmessage 1 50 10
	radiooptions 6 1 2 cmnet none none 3
	sleep 10
	if [ 1 == `getprop net.ccinet0.gw 1` ]; then
		sendmessage 2 "connected failed"
		setprop init.connect.retry 1
	else
		sendmessage 2 "successfully obtained ip"
		setprop init.connect.retry 0
	fi
else
	sendmessage 2 "already connected"
	sendmessage 1 90 1
fi
setprop init.connect.runned 1
