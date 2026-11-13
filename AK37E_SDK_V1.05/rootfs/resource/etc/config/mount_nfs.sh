#! /bin/sh

echo "killall ANYKA37E.BIN"
killall ANYKA37E.BIN

echo "killall wpa_supplicant"
killall wpa_supplicant

echo "killall udhcpc"
killall udhcpc

echo "wpa_supplicant -Dnl80211 -i wlan0 -c /etc/config/wpa_supplicant.conf -B"
wpa_supplicant -Dnl80211 -i wlan0 -c /etc/config/wpa_supplicant.conf -B

echo "udhcpc -i wlan0"
udhcpc udhcpc -i wlan0

echo "mkdir /mnt/nfs"
mkdir /mnt/nfs

echo "mount -t nfs -o nolock 192.168.32.24:/home/michael/share /mnt/nfs"
mount -t nfs -o nolock 192.168.32.24:/home/michael/share /mnt/nfs

echo "cd /mnt/nfs/two_wire_indoor/"
cd /mnt/nfs/two_wire_indoor/

echo "rm -rf /etc/config/two_wire_indoor.update"
rm -rf /etc/config/two_wire_indoor.update

echo "cp -rf two_wire_indoor.update /etc/config/"
cp -rf two_wire_indoor.update /etc/config/

echo "rm -rf /etc/config/user_data.cfg"
rm -rf /etc/config/user_data.cfg

echo "cp /mnt/nfs/two_wire_indoor/mount_nfs.sh /etc/config/"
cp /mnt/nfs/two_wire_indoor/mount_nfs.sh /etc/config/

echo "Okay!"

echo "--------------------copy------------------------S "
rm -rf /tmp/*
cp /etc/config/two_wire_indoor.update /tmp/
tar -xzvf /tmp/two_wire_indoor.update -C /tmp;
chmod -R 777 /tmp/*
echo "--------------------copy------------------------T "
