#pragma once
#ifndef FWD__H
#define FWD__H

/* These are the IOCTL codes used for the control device */
#define FWD_GET_VERS         0xFEED0001     /* Get the version  */  
#define FWD_SET_ACTIVATE     0xFEED0002 
#define FWD_SET_STOP         0xFEED0003 
//#define FWD_SET_EXTIP        0xFEED0004
//#define FWD_SET_INTIP        0xFEED0005

#define FWD_ADD_VIP          0xFEED0006
#define FWD_DEL_VIP          0xFEED0007
#define FWD_SET_RESTART      0xFEED0008  
#define FWD_ADD_PREVIEW       0xFEED0009
#define FWD_DEL_PREVIEW       0xFEED000A
//#define FWD_SET_EXTMASK      0xFEED000B
//#define FWD_SET_INTMASK      0xFEED000C
//#define FWD_SET_EXTDEVNAME   0xFEED0010
//#define FWD_SET_INTDEVNAME   0xFEED0011
#define FWD_SEND_TICK        0xFEED0012
//#define FWD_SEND_BUF         0xFEED0013
#define FWD_DEL_STREAM       0xFEED0014
#define FWD_SET_STATID        0xFEED0015
#define FWD_GET_STAT          0xFEED0016
#define FWD_ADD_USER          0xFEED0017
#define FWD_DEL_USER          0xFEED0018
#define FWD_SET_FEC_SIZE      0xFEED0019
#define FWD_SET_FEC_SRCNUM    0xFEED0020
#define FWD_SET_FEC_RPRNUM    0xFEED0021
#define FWD_SET_STOPLIST      0xFEED0022
#define FWD_OPEN_GROUP_MODE    0xFEED0023
#define FWD_CLOSE_GROUP_MODE    0xFEED0024
#define FWD_SET_GROUP_MODE_SENDLIST  0xFEED0025


// statistic some  data
#define MAX_VIDEO  8
//#pragma pack(1)
struct down_stat
{
	int  id;
	int  down_recv_lost;
	int  down_fec_lost;
	int  down_fill_lost;
	int  down_save_data;
};

struct fwd_stat
{
	int  id;
	int  up_bytes;
	unsigned int  up_start_seq;
	unsigned int  up_end_seq;
	unsigned int  up_realpkgnum;
	unsigned char stream_num;
	struct down_stat stream[MAX_VIDEO];	
};
//#pragma   pack()

#endif