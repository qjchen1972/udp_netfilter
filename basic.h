#pragma once
#ifndef BASIC__H
#define BASIC__H

#include "fwd.h"

#define  MAX_VIP 8
//#define  MAX_NODE 4
#define  MAX_GROUP_LIST 8

/* NOTE: The FWD_MAJOR symbol is only made available for kernel code.
* Userspace code has no business knowing about it.
*/
# define FWD_NAME        "FWD"

/* Version of FWD */
# define FWD_VERS        0x0001       /* 0.1 */ 

/* device 's status*/
#define  MOD_START        1
#define  MOD_STOP         0
#define  MOD_RESTART      2

/* 上下层node*/
#define  UP_NODE  -3      // 上一层node
#define  DOWN_NODE  -4    //下一层node

/* device name*/
#define  INTDEV_NAME  "INTFORWARD"
#define  EXTDEV_NAME  "EXTFORWARD"

// test  network speed
#define TEST_NETWORK_PORT  9999
//scope of stream's port
#define MIN_PORT    10000
#define MAX_PORT    11000
//heart packet ' max length 
#define MAX_HEART_LEN  200
//net device name length 
//#define NET_DEVICE_LEN   16
#define  STREAM_NUM  MAX_PORT-MIN_PORT

#define MAX_PARAM_LENGTH	256

#define MAX_QUEUE   5120
#define FLOW_MAX    50
#define FLOW_DELAY  HZ/50

#define GUEST_TIMEOUT 90
#define MTU_LEN    1472 
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#endif