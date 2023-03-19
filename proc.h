#pragma once
#ifndef PROC__H
#define PROC__H

#include"basic.h"


__s8*  __inet_ntoa(__u32 in);

int  add_guest_vip(__u16 pos, __s32 id);

int del_user(__u16 pos, __s32 id, __u32 ip);
int add_user(__u16 pos, __s32 id, __u32 destip, __u16 destport, __u8 *mac);

void push_bufwithid(__s32 destid, __u16 port, __s32 streamid);
int find_userstat(__u16 pos, __s32 id, struct fwd_stat *ret);
int  set_stoplist(__u16 pos,__s32 id, __s16 *sendlist);
void del_stream(__u16 pos);
int isinLan(__u32 ip, __u32 selfip, __u32 network_mask);
int proc_test(struct sk_buff *skb, struct udphdr *udph, struct iphdr *iph, struct ethhdr *eth);
int proc_heart(struct sk_buff *skb, struct udphdr *udph, struct iphdr *iph, struct ethhdr *eth);
int proc_forward(struct sk_buff *skb,struct udphdr *udph, struct iphdr *iph);
void test_fwd(void);

int  init_proc(void);
void exit_proc(void);
void restart_proc(void);

#endif