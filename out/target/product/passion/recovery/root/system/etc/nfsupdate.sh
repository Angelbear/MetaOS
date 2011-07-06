#!/system/bin/sh
SERVER=182.18.10.43:10080
sendmessage 0 #begin animation
sendmessage 1 0 0
sendmessage 2 "Downloading update package..."
cd /sdcard/
rm nfsupdate.zip
wget http://$SERVER/nfsupdate.zip
sendmessage 2 "Successfully downloaded."
sendmessage 3 #endanimation
sendmessage 5 "SDCARD:nfsupdate.zip"

