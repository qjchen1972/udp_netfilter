#pragma once
#ifndef SETUP__H
#define SETUP__H

#include"basic.h"

extern __s32 limit_speed;
extern __u32 max_user;
extern __u8  active;
extern __u32 ext_mask;
extern __u32 int_mask;
extern __u32 ext_self_ip;
extern __u32 int_self_ip;
extern __s8 ext_device[IFNAMSIZ];
extern __s8 int_device[IFNAMSIZ];
extern __u8 gateway_mac[ETH_ALEN];
int init_setup(void);
void exit_setup(void);

#endif