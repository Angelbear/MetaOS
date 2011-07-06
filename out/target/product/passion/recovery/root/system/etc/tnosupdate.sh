#!/sbin/sh
SERVER=`getprop ro.tserver.address`
mount -t vfat /dev/block/mmcblk0p1 /sdcard
sendmessage 0 #begin animation
sendmessage 1 0 0
sendmessage 2 "Downloading update package..."
cd /sdcard/
#sleep 150
rm tnosupdate.zip
wget http://$SERVER/tnosupdate.zip
sendmessage 2 "Successfully downloaded."
sendmessage 3 #endanimation
sendmessage 5 "SDCARD:tnosupdate.zip"

