#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/socket.h>
#include <linux/netfilter_ipv4.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/netpoll.h>
#include <linux/inet.h>
#include <net/ip.h>
#include <net/udp.h>
#include <linux/if_ether.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include "streambuf.h"

struct fec  *stream_fec;
struct user_stream_t **stream_buf;


void set_fec_size(__u16 pos,__u16 size)
{
	if (size > MTU_LEN || pos >=  STREAM_NUM || pos < 0 ) return;
	stream_fec[pos].size = size;
	stream_fec[pos].flag++;
}

void set_fec_srcnum(__u16 pos, __u16 num)
{
	if (num > MAX_NUM || pos >= STREAM_NUM || pos < 0 ) return;
	stream_fec[pos].srcnum = num;
	stream_fec[pos].flag++;
}

void set_fec_repairnum(__u16 pos, __u16 num)
{
	if (num > MAX_NUM || pos >= STREAM_NUM || pos < 0 ) return;
	stream_fec[pos].repairnum = num;
	stream_fec[pos].flag++;
}

void free_onebuf(struct user_stream_t *entry)
{
	int i, j;

	if (!entry) return;

	for (i = 0; i < entry->buflen; i++)
	{
		for (j = 0; j < entry->blklen; j++)
		{
			if(entry->streambuf[i].stream[j])
				kfree(entry->streambuf[i].stream[j]);
		}
		if(entry->streambuf[i].stream)
			kfree(entry->streambuf[i].stream);
		if(entry->streambuf[i].hash)
			kfree(entry->streambuf[i].hash);
	}
	if(entry->streambuf)	kfree(entry->streambuf);
	kfree(entry);
}

void del_onestreambuf(__u16 pos, __u32 id)
{
	struct user_stream_t *entry, *pre;

	if ( pos >= STREAM_NUM || pos < 0 ) return;

	entry = stream_buf[pos];
	pre = 0;
	while (entry)
	{
		if (entry->id == id)
		{
			if (pre) pre->next = entry->next;
			else stream_buf[pos] = entry->next;
			free_onebuf(entry);
			return;
		}
		if (entry->next != 0)
		{
			pre = entry;
			entry = entry->next;
		}
		else break;
	}
}


struct user_stream_t* find_streambuf(__u16 pos, __u32 id )
{
	struct user_stream_t *temp, *newbuf;
	int i;
	int total;
	int s_n;
	int pkglen;
	int srcnum;

	if (pos >= STREAM_NUM || pos < 0 ) return 0;
	if (stream_fec[pos].flag < 3) return  0;

	temp = stream_buf[pos];
	//pr_notice("%s  %d  %d\n", "s1 ", pos, id);
	while (temp != 0)
	{
		if (temp->id == id)
		{
			//pr_notice("%s  %d  %d\n", "find  streambuf ", pos, id);
			return temp;
		}
		if (temp->next != 0) temp = temp->next;
		else break;
	}
	//pr_notice("%s  %d  %d\n", "s2 ", pos, id);
	
	newbuf = (struct user_stream_t*)kzalloc(sizeof(struct user_stream_t), GFP_ATOMIC);
	if (!newbuf)
	{
		pr_notice("%s \n", "newbuf allocate error ");
		return 0;
	}

	if (id == 0)
	{
		total = stream_fec[pos].srcnum + stream_fec[pos].repairnum;
		s_n = HOST_BUF;
		pkglen = stream_fec[pos].size;
		srcnum = stream_fec[pos].srcnum;
	}
	else
	{
		total = VIP_FEC_SRC_NUM + VIP_FEC_REPAIR_NUM;
		s_n = GUEST_BUF;
		pkglen = VIP_FEC_BUF_LEN;
		srcnum = VIP_FEC_SRC_NUM;
	}

	newbuf->streambuf = (struct stream_buf_t*)kzalloc(sizeof(struct stream_buf_t) * s_n, GFP_ATOMIC);
	if (!newbuf->streambuf)
	{
		pr_notice("%s \n", "newbuf->streambuf allocate error ");
		return 0;
	}

	for (i = 0; i < s_n; i++)
	{
		int j;
		newbuf->streambuf[i].stream = (__u8**)kzalloc(sizeof(__u8*) * total, GFP_ATOMIC);
		if (!newbuf->streambuf[i].stream) return 0;
		for (j = 0; j < total; j++)
		{
			newbuf->streambuf[i].stream[j] = (__u8*)kzalloc(sizeof(__u8) * pkglen, GFP_ATOMIC);
			if (!newbuf->streambuf[i].stream[j]) return 0;
		}
		newbuf->streambuf[i].hash = (__u8*)kzalloc(sizeof(__u8) * total, GFP_ATOMIC);
		if (!newbuf->streambuf[i].hash) return 0;
		newbuf->streambuf[i].index = 0;
		//newbuf->streambuf[i].total = 0;
	}
	newbuf->start_pos = 0;
	newbuf->buflen = s_n;
	newbuf->blklen = total;
	newbuf->pkglen = pkglen;
	newbuf->srcnum = srcnum;
	newbuf->id = id;
	newbuf->next = 0;

	pr_notice("%s %d %d %d %d %d %d\n", "new buf", pos,id,newbuf->buflen, newbuf->blklen, newbuf->pkglen, newbuf->srcnum);
	if (!temp)  stream_buf[pos] = newbuf;
	else	temp->next = newbuf;
	return newbuf;
}

void add_streambuf(__u16 pos, __u32 id, const __s8 *buf, __u16 len)
{
	unsigned int seq;
	int  mod;
	int  start_seq;
	int end_pos;
	int i;
	struct user_stream_t*  m_buf;
	
	//pr_notice("%s  %d  %d %d \n", "ok ", id, stream_fec[pos].flag, pos);

	if (pos >= STREAM_NUM || pos < 0) return;

	if ( id == 0 && stream_fec[pos].flag < 3 ) return;
	if (!buf) return;

	m_buf = find_streambuf(pos, id);
	if (!m_buf) return;

	seq = *((unsigned int*)(buf + 4));
	mod = seq % m_buf->blklen;
	start_seq = seq - mod;
	end_pos = (m_buf->start_pos + m_buf->buflen - 1) % m_buf->buflen;
		
	if (start_seq > m_buf->streambuf[end_pos].index)
	{
		//pr_notice("%s %d %d\n", "k1", seq, m_buf->start_pos);
		m_buf->start_pos = (m_buf->start_pos + 1) % m_buf->buflen;
		end_pos = (end_pos + 1) % m_buf->buflen;
		m_buf->streambuf[end_pos].index = start_seq;
		memset(m_buf->streambuf[end_pos].hash, 0, sizeof(__s8)*m_buf->blklen);
		memcpy(m_buf->streambuf[end_pos].stream[mod], buf, sizeof(__u8)*m_buf->pkglen);

		//for (i = 0; i < m_buf->blklen; i++) m_buf->streambuf[end_pos].hash[i] = 0;
		//for (i = 0; i < m_buf->pkglen; i++) m_buf->streambuf[end_pos].stream[mod][i] = buf[i];
		m_buf->streambuf[end_pos].hash[mod] = 1;
		m_buf->streambuf[end_pos].total = 1;
	}
	else if (start_seq < m_buf->streambuf[m_buf->start_pos].index)
	{
		//pr_notice("%s %d\n", "k2", start_seq);
		//说明这流应该是最新的了，需要清除以前的buf
		if (start_seq + 2 * m_buf->blklen <= m_buf->streambuf[m_buf->start_pos].index)
		{
			m_buf->start_pos = 0;
			for (i = 0; i < m_buf->buflen; i++) m_buf->streambuf[i].index = 0;
		}
	}
	else 
	{		
		int endindex = m_buf->streambuf[end_pos].index;
		int temp = (endindex - start_seq) / m_buf->blklen;
		if (temp > m_buf->buflen - 1) return;
		int p = (end_pos - temp + m_buf->buflen) % m_buf->buflen;
		//pr_notice("%s %d\n", "k3", p);
		//for (i = 0; i < m_buf->pkglen; i++) m_buf->streambuf[p].stream[mod][i] = buf[i];
		memcpy(m_buf->streambuf[p].stream[mod], buf, sizeof(__u8)*m_buf->pkglen);
		m_buf->streambuf[p].hash[mod] = 1;
		m_buf->streambuf[p].total++;
		//pr_notice("%s %d  %d\n", "kkk2", p, m_buf->streambuf[p].total);
	}
}

//
__s8  find_streambufpos(__u16 pos, __u32 id, struct user_stream_t*  m_buf)
{
	__s8  num;
	int i;
	int j;
	__s8 sendflag = 0;
	
	if (!m_buf) return 0;
	if (pos >= STREAM_NUM || pos < 0) return 0;

	if ( id == 0 && stream_fec[pos].flag < 3 ) return 0;
	
	//pr_notice("%s %d %d %d %d %d\n", "check", m_buf->buflen, m_buf->streambuf[0].total, m_buf->streambuf[0].index, m_buf->streambuf[1].total,
		//m_buf->streambuf[0].index);

	num = m_buf->buflen;
	i = (m_buf->start_pos + m_buf->buflen - 1) % m_buf->buflen;
	while (num > 0)
	{
		//pr_notice("%s %d %d\n", "k3", i,m_buf->streambuf[i].total);
		if ( m_buf->streambuf[i].total >= m_buf->srcnum )
		{
			for (j = 0; j < m_buf->blklen; j++)
			{
				if (!m_buf->streambuf[i].hash[j]) continue;
				unsigned int seq = *((unsigned int*)(m_buf->streambuf[i].stream[j] + 4));
				if (seq % m_buf->blklen >= m_buf->srcnum ) continue;
				unsigned short flag = *((unsigned short*)(m_buf->streambuf[i].stream[j] + 8));
				if ((flag >> 12) == 2)
				{
					//pr_notice("%s %d  %d\n", "k4", sendflag, num);
					if(sendflag && (sendflag!=num) ) return num;
					sendflag = num;
				}
			}
		}
		i = (i - 1 + m_buf->buflen) % m_buf->buflen;
		num--;
	}
	return  sendflag;
}

int init_streambuf(void)
{
	int i;
	stream_fec = (struct fec*)kzalloc(sizeof(struct fec) * STREAM_NUM, GFP_ATOMIC );
	if (!stream_fec)
	{
		pr_notice("%s\n", "allocate stream_fec memory error!");
		return -1;
	}
	
	stream_buf = (struct user_stream_t **)kzalloc(sizeof(struct user_stream_t*) * STREAM_NUM, GFP_ATOMIC);
	if (!stream_buf)
	{
		kfree(stream_fec);
		pr_notice("%s\n", "allocate stream_buf memory error!");
		return -1;
	}

	for (i = 0; i < STREAM_NUM; i++)
	{
		stream_fec[i].flag = 0;
		stream_buf[i] = 0;
	}
	
	return 0;
}

void exit_streambuf(void)
{
	int i;
	struct user_stream_t *temp, *entry;

	kfree(stream_fec);

	for (i = 0; i < STREAM_NUM; i++)
	{
		entry = stream_buf[i];
		while(entry)
		{
			temp = entry;
			entry = entry->next;
			free_onebuf(temp);
		}
	}
	kfree(stream_buf);
}
