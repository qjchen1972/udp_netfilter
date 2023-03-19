#pragma once
#ifndef STREAM_BUF__H
#define STREAM_BUF__H

#include"basic.h"


#define VIP_FEC_SRC_NUM  50
#define VIP_FEC_REPAIR_NUM 5
#define VIP_FEC_BUF_LEN  548

//#define MAX_UDP_PAYLOAD 1472
#define MAX_NUM  255

//one block
struct stream_buf_t
{
	__u8  **stream;
	__s8  *hash;
	__u32  index;
	__u16 total;
};

#define HOST_BUF  5
#define GUEST_BUF 3

struct user_stream_t
{
	struct stream_buf_t *streambuf;
	__u8 start_pos;
	__s32 id;
	__s8   buflen;
	__u16  blklen;
	__u16  pkglen;
	__u16  srcnum;
	struct user_stream_t*  next;
};


struct fec
{
	__u16  size;
	__u16  srcnum;
	__u16  repairnum;
	__u8   flag;
};

extern struct fec  *stream_fec;
extern struct user_stream_t **stream_buf;

void add_streambuf(__u16 pos, __u32 id, const __s8 *buf, __u16 len);
__s8  find_streambufpos(__u16 pos, __u32 id, struct user_stream_t*  m_buf);
struct user_stream_t* find_streambuf(__u16 pos, __u32 id);
void free_onebuf(struct user_stream_t *entry);
void del_onestreambuf(__u16 pos, __u32 id);

void set_fec_size(__u16 pos,__u16 size);
void set_fec_srcnum(__u16 pos,__u16 num);
void set_fec_repairnum(__u16 pos,__u16 num);

int init_streambuf(void);
void exit_streambuf(void);

#endif


