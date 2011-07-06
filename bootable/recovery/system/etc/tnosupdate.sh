#!/sbin/sh
SERVER=210.75.5.230:8081
radiooptions 11
USERID=`getprop gsm.info.imsi null`
PACKAGE=metaosupdate.zip
mount -t vfat /dev/block/mmcblk0p1 /sdcard
sendmessage 0 #begin animation
sendmessage 1 0 0
sendmessage 2 "Downloading update package..."
cd /sdcard/
if [ -e $PACKAGE ] then
    sendmessage 2 "OS already cached"
else
    rm $PACKAGE
    wget -c http://$SERVER/$PACKAGE
    sendmessage 2 "Successfully downloaded."
fi
sendmessage 3 #endanimation
sendmessage 5 "SDCARD:$PACKAGE"
