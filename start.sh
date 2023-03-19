#!/bin/sh

intip=10.116.80.13
intmask=255.255.248.0
intdev=eth0
extip=120.25.212.29
extmask=255.255.252.0
extdev=eth1
gatemac=70:f9:6d:da:8f:81
maxuser=90
limit_speed=0
dir=`pwd`
cd $dir

depmod $dir/forward.ko

dmesg -c
modprobe forward net=$intip:$intmask:$intdev:$extip:$extmask:$extdev:$gatemac:$maxuser:$limit_speed

