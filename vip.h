#pragma once
#ifndef VIP__H
#define VIP__H

#include"basic.h"

struct guest_t
{
	__u32 ip;
	__u16 port;
	__s32 id;	
	__s16 can_send_list[MAX_GROUP_LIST];//在组模式逻辑下，能发送的id
};

extern struct guest_t  **vip_table;
extern struct guest_t  *review_table;
extern struct guest_t  *host_table;
extern __s8 *group_mode; // 用于开启组模式逻辑，就是只能同组的人互相发送视频
int init_vip(void);
void exit_vip(void);
void restart_vip(void);
void reset_vip(__u16 pos);


__u8 is_validstream(__u16 pos, __s32 id, __u32 ip, __u16 port);

__s8 add_host(__u16 pos, __u32 ip, __u16 port);
__s8 del_host(__u16 pos);

__s8 add_vip(__u16 pos, __s32 id);
__s8 del_vip(__u16 pos, __s32 id);
__u8 is_vip(__u16 pos, __s32 id);
__s8 add_review(__u16 pos, __s32 id);
__s8 del_review(__u16 pos, __s32 id);
__u8 is_review(__u16 pos, __s32 id);
void set_groupmode_sendlist(__u16 pos, __s32 id, __s16 *sendlist);

#endif