#! /bin/sh

export PATH=$PATH:/usr/app/bin
export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/app/lib:/usr/app/wifi/lib

#echo /sbin/mdev > /proc/sys/kernel/hotplug

[ -d /var/run ] || mkdir /var/run

customdir=/tmp/etc/custom
configdir=/tmp/etc/config
[ -d ${configdir} ] || mkdir -p ${configdir}
mount -t jffs2 /dev/mtdblock3 ${configdir}

# format config patition if it is invalid
if [ ! -f ${configdir}/.tag ]; then
	echo "format config partition..."
	umount -f ${configdir}
	flash_erase /dev/mtd3 0 0
	mount -t jffs2 /dev/mtdblock3 ${configdir}
	touch ${configdir}/.tag
	mkdir ${configdir}/custom
fi

rm -f ${customdir}
ln -s ${configdir}/custom ${customdir}

import_custom

echo 512 > /proc/sys/vm/min_free_kbytes
ulimit -s 1024

echo 1048576 > /proc/sys/net/core/wmem_max

cd /usr/app/driver
./load_driver.sh

#network
/usr/app/setmac.sh
/usr/app/network.sh

if [ -f /usr/app/wifi/8188fu.ko ] ; then
	insmod /usr/app/wifi/8188fu.ko
	cp /usr/app/wifi/wpa_supplicant.conf /tmp/
elif [ -f /usr/app/wifi/hgics.ko ] ; then
	insmod /usr/app/wifi/mac80211.ko
	insmod /usr/app/wifi/hgics.ko fw_file=hgics.bin
	cp /usr/app/wifi/wpa_supplicant.conf /tmp/
elif [ -f /usr/app/wifi/ssv6x5x.ko ] ; then
	insmod /usr/app/wifi/mac80211.ko
	insmod /usr/app/wifi/ssv6x5x.ko stacfgpath=/usr/app/wifi/ssv6x5x-wifi.cfg
	cp /usr/app/wifi/wpa_supplicant.conf /tmp/
elif [ -f /usr/app/wifi/aic8800_fdrv.ko ] ; then
	insmod /usr/app/wifi/aic_load_fw.ko aic_fw_path=/usr/app/wifi
	insmod /usr/app/wifi/aic8800_fdrv.ko
	insmod /usr/app/wifi/aic_btusb.ko aic_fw_path=/usr/app/wifi
	cp /usr/app/wifi/wpa_supplicant.conf /tmp/
	mkdir -p /var/lib/misc/
	touch /var/lib/misc/udhcpd.lease
	sleep 5
elif [ -f /usr/app/wifi/atbm603x_comb_wifi.ko ] ; then
	#1 是 WiFi+BLE驱动
	#insmod /usr/app/wifi/mac80211.ko
	insmod /usr/app/wifi/atbm603x_comb_wifi.ko wifi_bt_comb=1
	cp /usr/app/wifi/wpa_supplicant.conf /tmp/
	sleep 1
fi

rm /dev/random
ln -s /dev/urandom /dev/random

telnetd -p 2360 &

cp /usr/app/bin/system_wapper /tmp/
/tmp/system_wapper &

#cp /usr/app/bin/upgrade_tool /tmp/upgrade_tool
#/tmp/upgrade_tool &

if [ -f ${configdir}/debug ] ; then
	exit
fi

if [ -f /usr/app/bin/ipcam ] ; then
	cp /usr/app/bin/ipcam /tmp
elif [ -f /usr/app/bin/ipcam.lzma ] ; then
	cp /usr/app/bin/ipcam.lzma /tmp
	lzma -d /tmp/ipcam.lzma
fi

chmod +x /tmp/ipcam
cd /tmp/
/tmp/ipcam

if [ -f /tmp/upgrade.flag ] ; then
	exit
fi

reboot

