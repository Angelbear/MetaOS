radiooptions 5
sleep 10
if [ 0 == `getprop gsm.info.imsi 0` ]; then
    radiooptions 11
