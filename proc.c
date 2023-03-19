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
#include <linux/jiffies.h>
#include <linux/workqueue.h> 
#include <linux/mutex.h>
#include "proc.h"
#include "vip.h"
#include "setup.h"
#include "streambuf.h"

//work队列需要处理的类型
#define TEST_SPEED_LIST   0
#define HEART_LIST        1
#define FORWARD_LIST      2
#define ADD_USER_LIST     3
#define DEL_USER_LIST     4
#define DEL_STREAM_LIST   5
#define ADD_PREVIEW_LIST  6
#define ADD_VIP_LIST      7

#define TRAN_LIST         8
#define PUSH_LIST         9 

#define SET_STOP_LIST     10 
//观众节点
struct udp_entry 
{
	__u32 ip;
	__u16 port;
	__u32 update;
	__s32 id;
	__s8  isext;
	__u8  mac[ETH_ALEN];	
	__s16 stoplist[MAX_VIP];	
	struct udp_entry* pre;
	struct udp_entry* next;
};

//统计的列表
struct stat_entry
{
	struct fwd_stat stat;
	__s8   flag; //0表示清零，重新开始统计
	struct stat_entry*  next;
};

//queue list
struct queue
{
	__u8  *data;
	__u16 len;
	__u32 src_ip;
	__u16 src_port;
	__u32 dst_ip;
	__u16 dst_port;
	__u8  proc_type;	
	__u8  mac[ETH_ALEN];
	__u32 stream_key;
	struct queue  *next;
};

//用于统计的信号量
static DEFINE_MUTEX(sta_mutex);
//用于统计的链表
static struct stat_entry **stat;

//用户链表
static struct udp_entry** udp_entrys;
static DEFINE_MUTEX(udp_mutex);

//内网的netpoll
static struct netpoll *int_np_t;
//外网的netpoll
static struct netpoll *ext_np_t;

//广播地址
static __u8 broadcast_mac[ETH_ALEN] = { 0xff,0xff,0xff,0xff,0xff,0xff };



//工作队列
//用于发送的工作队列
static struct delayed_work tx_work;
static struct workqueue_struct *tx_queue;
static struct queue  *tx_head, *tx_tail;
static struct queue  *pritx_head, *pritx_tail; //优先队列
static DEFINE_MUTEX(tx_mutex);

//用于事务处理的工作队列
static struct delayed_work logic_work;
static struct workqueue_struct *logic_queue;
static struct queue  *logic_head, *logic_tail;
static struct queue  *prilogic_head, *prilogic_tail; //优先队列
static DEFINE_SPINLOCK(logic_list_lock);


//服务器人数
__s32  user_num;
// 视频key
__u32   streamkey;
__u32   *streamkey_table;

//对Xen服务器体系做出的限流
static unsigned int  flow_num;
static unsigned long flow_tick;

//判断ip是否在同一局域网
int isinLan(__u32 ip, __u32 selfip, __u32 network_mask)
{
	return ((ip & network_mask) == (selfip&network_mask));
}

static int send_data(struct netpoll *np, const char *msg, int len)
{	
	netpoll_send_udp(np, msg, len);
	if (limit_speed >= 2 )
	{
		flow_num++;
		if (flow_num == 1) flow_tick = jiffies;
		unsigned long now_tick = jiffies;

		//pr_notice("%s  %d \n", "0  queue is ", flow_num);
		if (now_tick - flow_tick == 0)
		{
			if (flow_num >= FLOW_MAX)
			{				
				flow_num = 0;
				int tt = queue_delayed_work(tx_queue, &tx_work, FLOW_DELAY);
				if (tt == 0)
				{
					//msleep(FLOW_DELAY);
					pr_notice("%s \n", "spot speed is too fast.we will sleep! ");
				}
				return 0;
			}
		}
		else
		{
			int  num = flow_num / (now_tick - flow_tick);
			flow_num = 0;
			if (num >= FLOW_MAX)
			{
				int tt = queue_delayed_work(tx_queue, &tx_work, FLOW_DELAY);
				if (tt == 0)
				{
					//msleep(FLOW_DELAY);
					pr_notice("%s \n", "average speed is too fast! we will sleep");
				}
				return 0;
			}
		}
	}
	return 1;
}

static int is_extern_net(__u32 ip)
{
	return (ip == ext_self_ip);
}

static int is_inter_net(__u32 ip)
{
	return (ip == int_self_ip);
}

// debug use
static __s8 buffer[16];
__s8*  __inet_ntoa(__u32 in)
{
	__u8 *bytes = (__u8 *)&in;
	snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);
	return buffer;
}

static struct queue* get_logichead(void)
{
	unsigned long flags;
	struct queue *temp = 0;

	spin_lock_irqsave(&logic_list_lock, flags);
	if (prilogic_head)
	{
		temp = prilogic_head;
		prilogic_head = prilogic_head->next;
	}
	else
	{
		temp = logic_head;
		if (logic_head)		logic_head = logic_head->next;
		
	}
	spin_unlock_irqrestore(&logic_list_lock, flags);
	return temp;
}

static void add_logictail(struct queue* temp)
{
	unsigned long flags;

	spin_lock_irqsave(&logic_list_lock, flags);
	if (!logic_head)
	{
		logic_head = temp;
		logic_tail = temp;
	}
	else
	{
		logic_tail->next = temp;
		logic_tail = temp;
	}
	spin_unlock_irqrestore(&logic_list_lock, flags);
}

//添加优先队列
static void add_prilogictail(struct queue* temp)
{
	unsigned long flags;

	spin_lock_irqsave(&logic_list_lock, flags);
	if (!prilogic_head)
	{
		prilogic_head = temp;
		prilogic_tail = temp;
	}
	else
	{
		prilogic_tail->next = temp;
		prilogic_tail = temp;
	}
	spin_unlock_irqrestore(&logic_list_lock, flags);
}

static struct queue* get_txhead(void)
{	
	struct queue *temp = 0;
	
	mutex_lock(&tx_mutex);
	if (pritx_head)
	{
		temp = pritx_head;
		pritx_head = pritx_head->next;
	}
	else
	{
		temp = tx_head;
		if (tx_head) tx_head = tx_head->next;		
	}
	mutex_unlock(&tx_mutex);	
	return temp;
}

static void add_txtail(struct queue* temp)
{
	mutex_lock(&tx_mutex);
	if (!tx_head)
	{
		tx_head = temp;
		tx_tail = temp;
	}
	else
	{
		tx_tail->next = temp;
		tx_tail = temp;
	}
	mutex_unlock(&tx_mutex);
}

//添加优先队列
static void add_pritxtail(struct queue* temp)
{
	mutex_lock(&tx_mutex);
	if (!pritx_head)
	{
		pritx_head = temp;
		pritx_tail = temp;
	}
	else
	{
		pritx_tail->next = temp;
		pritx_tail = temp;
	}
	mutex_unlock(&tx_mutex);
}

static inline struct stat_entry*  find_stat(__u16 pos, __s32 id)
{
	struct stat_entry *entry;

	if (pos < 0 || pos >= STREAM_NUM) return 0;

	entry = stat[pos];
	//pr_notice("%s  %d\n", "start find", id, entry);
	while (entry != 0)
	{
		if (entry->stat.id == id)		return entry;
		if (entry->next != 0)	entry = entry->next;
		else break;
	}
	return 0;	
}

static inline struct stat_entry* add_stat(struct stat_entry *newstat, __u16 pos, __s32 id)
{
	struct stat_entry *entry;

	if (!newstat) return 0;

	if (pos < 0 || pos >= STREAM_NUM) return 0;

	entry = stat[pos];
	//pr_notice("%s  %d\n", "start find", id, entry);
	while (entry != 0)
	{
		if (entry->next != 0)	entry = entry->next;
		else break;
	}
	newstat->stat.id = id;
	newstat->next = 0;
	if (entry == 0) stat[pos] = newstat;
	else	entry->next = newstat;
	newstat->flag = 0;
	return newstat;
}

static int  can_send_stream(struct udp_entry *entry, __u16 pos, __s32  id)
{
	int i,j;

	if (group_mode[pos])
	{
		//pr_notice("%s  %d %d %d \n", " can send", entry->id, pos, id);
		if (id == 0)
		{
			//pr_notice("%s\n", " id = 0 in");
			for (i = 0; i < MAX_GROUP_LIST; i++)
			{
				if (host_table[pos].can_send_list[i] < 0) continue;
				//pr_notice("%s  %d\n", " id = 0 in", host_table[pos].can_send_list[i]);
				if (host_table[pos].can_send_list[i] == entry->id)
				{
					//pr_notice("%s  %d\n", " id = 0 find ", entry->id);
					return 1;
				}
			}
			return 0;
		}

		for (i = 0; i < MAX_VIP; i++)
		{
			if (vip_table[pos][i].id == id)
			{
				for (j = 0; j < MAX_GROUP_LIST; j++)
				{
					if (vip_table[pos][i].can_send_list[j] < 0) continue;
					//pr_notice("%s  %d\n", " vip is ", vip_table[pos][i].can_send_list[j] );
					if (vip_table[pos][i].can_send_list[j] == entry->id) return 1;
				}
				return 0;
			}
		}
		return 0;
	}
	else
	{
		if (id == 0) return 1;
		for (i = 0; i < MAX_VIP; i++)
		{
			if (id == entry->stoplist[i]) return 0;
		}
		return 1;
	}
	return 1;
}

// 把是id的streambuf push 给 enrty
static void push_buf(struct udp_entry *entry, __u16 pos, __s32  id)
{
	int j, i;
	struct user_stream_t*  m_buf;
	
	if (!entry) return;
	
	if (pos < 0 || pos >= STREAM_NUM) return;

	if (id == 0 && stream_fec[pos].flag < 3) return;

	if (!can_send_stream(entry, pos,id)) return;

	m_buf = find_streambuf(pos, id);
	if (!m_buf) return;

	// 得到的p,意味着需要发送m_buf->buflen - p+1 PACKET
	int p = find_streambufpos(pos, id, m_buf);
	//p = 1;
	pr_notice("%s %d %d\n", "start in", p, m_buf->start_pos);
	if (p > 0)
	{
		int s;
		// find end
		s = (m_buf->start_pos + m_buf->buflen - 1) % m_buf->buflen;
		// find start of send
		s = (s - (m_buf->buflen - p) + m_buf->buflen) % m_buf->buflen;
		while (p <= m_buf->buflen)
		{
			//pr_notice("%s %d \n", "haha ", stream_buf[num].streambuf[s].total);
			if (m_buf->streambuf[s].total >= m_buf->srcnum)
			{
				for (i = 0; i < m_buf->blklen; i++)
				{
					struct queue *tx_temp;
					if (!m_buf->streambuf[s].hash[i]) continue;

					tx_temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
					if (!tx_temp)
					{
						pr_notice("%s\n", "allocate tx_temp memory error!");
						return;
					}
					tx_temp->data = (__u8*)kzalloc(sizeof(__u8)*m_buf->pkglen, GFP_ATOMIC);
					if (!tx_temp->data)
					{
						kfree(tx_temp);
						pr_notice("%s\n", "allocate tx_temp->data memory error!");
						return;
					}
					tx_temp->stream_key = streamkey_table[pos];
					memcpy(tx_temp->data, m_buf->streambuf[s].stream[i], m_buf->pkglen);
					tx_temp->len = m_buf->pkglen;
					if (entry->isext)
					{
						tx_temp->src_ip = ext_self_ip;
					}
					else
					{
						tx_temp->src_ip = int_self_ip;
					}
					tx_temp->src_port = pos + MIN_PORT;
					tx_temp->dst_ip = entry->ip;
					tx_temp->dst_port = entry->port;
					tx_temp->proc_type = PUSH_LIST;
					tx_temp->next = 0;
					memcpy(tx_temp->mac, entry->mac, ETH_ALEN);
					add_pritxtail(tx_temp);
					if(limit_speed)
						queue_delayed_work(tx_queue, &tx_work, FLOW_DELAY);
					else
						queue_delayed_work(tx_queue, &tx_work, 0);
					//pr_notice("%s %d \n", "send queue ", t);
				}
			}
			s = (s + 1) % m_buf->buflen;
			p++;
		}
	}
}


static void del_entry(__u16 num, struct udp_entry* p)
{
	if (!p) return 0;
	if (num < 0 || num >= STREAM_NUM) return;

	if (p->pre) p->pre->next = p->next;
	else {
		udp_entrys[num] = p->next;
	}
	if (p->next) p->next->pre = p->pre;
	kfree(p);
}

static int proc_set_stoplist(struct queue  *temp)
{
	struct udp_entry *entry;
	__s32 id;
	__u16 pos = temp->src_port;

	memcpy(&id, &temp->src_ip, sizeof(__s32));
	
	pr_notice("%s %d %d  \n", "add_stop list input", pos, id);	

	mutex_lock(&udp_mutex);
	entry = udp_entrys[pos];
	while (entry != 0)
	{
		if (entry->id == id)
		{
			memcpy(entry->stoplist, temp->data, temp->len);
			mutex_unlock(&udp_mutex);
			pr_notice("%s %d %d   %d %d  %d %d  %d %d \n", "send list input",
				entry->stoplist[0], entry->stoplist[1], entry->stoplist[2], entry->stoplist[3],
				entry->stoplist[4], entry->stoplist[5], entry->stoplist[6], entry->stoplist[7] );
			return 1;
		}
		if (entry->next != 0)	entry = entry->next;
		else break;
	}
	mutex_unlock(&udp_mutex);
	return -1;
}


int  set_stoplist(__u16 pos, __s32 id, __s16 *sendlist)
{
	struct queue *temp;

	if (pos < 0 || pos >= STREAM_NUM) return -1;

	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}

	temp->data = (__u8*)kzalloc(sizeof(__s16)*MAX_VIP, GFP_ATOMIC);
	if (!temp->data)
	{
		kfree(temp);
		pr_notice("%s\n", "allocate tx_temp->data memory error!");
		return -1;
	}
	temp->len = sizeof(__s16)*MAX_VIP;
	memcpy(temp->data, sendlist, temp->len);
	temp->src_port = pos; // src_port 记录pos
	memcpy(&temp->src_ip, &id, sizeof(__s32)); // src_ip 记录id
	temp->proc_type = SET_STOP_LIST;
	temp->next = 0;

	add_prilogictail(temp);
	queue_delayed_work(logic_queue, &logic_work, 0);
	return  0;
}

static int proc_add_vip_list(struct queue  *temp)
{
	struct udp_entry *entry;
	int i;
	__s32 id;
	__u16 pos = temp->src_port;

	memcpy(&id, &temp->src_ip, sizeof(__s32));

	pr_notice("%s %d %d  \n", "add_vip input", pos, id);

	for (i = 0; i < MAX_VIP; i++)
	{
		if (vip_table[pos][i].id == -1)
		{
			vip_table[pos][i].id = id;

			//for (k = 0; k < MAX_GROUP_LIST; k++)
				//vip_table[pos][i].can_send_list[k] = -1;

			mutex_lock(&udp_mutex);
			entry = udp_entrys[pos];
			while (entry != 0)
			{
				if (entry->id == id)
				{
					vip_table[pos][i].ip = entry->ip;
					vip_table[pos][i].port = entry->port;
					mutex_unlock(&udp_mutex);
					return 1;
				}
				if (entry->next != 0)	entry = entry->next;
				else break;
			}
			mutex_unlock(&udp_mutex);
			return -1;
		}
	}
	return -1;
}

int  add_guest_vip( __u16 pos, __s32 id )
{
	struct queue *temp;

	if (pos < 0 || pos >= STREAM_NUM) return -1;

	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}

	temp->src_port = pos; // src_port 记录pos
	memcpy(&temp->src_ip, &id, sizeof(__s32)); // src_ip 记录id
	temp->proc_type = ADD_VIP_LIST;
	temp->next = 0;

	add_prilogictail(temp);
	queue_delayed_work(logic_queue, &logic_work, 0);
	return  0;
}


static int proc_add_preview_list(struct queue  *temp)
{
	struct udp_entry *entry;
	__u16 pos = temp->src_port;
	__s32 destid;
	__s32 streamid;
	int add_task = 0;
	int push_task = 0;

	memcpy(&destid, &temp->dst_ip, sizeof(__s32));
	memcpy(&streamid, &temp->src_ip, sizeof(__s32));

	pr_notice("%s %d %d %d \n", "add_preview input", pos, destid, streamid);

	mutex_lock(&udp_mutex);
	entry = udp_entrys[pos];
	while (entry)
	{
		if (entry->id == streamid && !add_task)
		{
			review_table[pos].ip = entry->ip;
			review_table[pos].port = entry->port;
			if (push_task)
			{
				mutex_unlock(&udp_mutex);
				return 1;
			}
			add_task = 1;
		}
		else if (entry->id == destid && !push_task)
		{
			push_buf(entry, pos, streamid);
			if (add_task)
			{
				mutex_unlock(&udp_mutex);
				return 1;
			}
			push_task = 1;
		}
		entry = entry->next;
	}
	if (!entry)
	{
		mutex_unlock(&udp_mutex);
		pr_notice("%s %d %d\n", "push buf to  %d %d error, no user exist", pos, destid);
		return -1;
	}
	mutex_unlock(&udp_mutex);
	return 1;
}

void push_bufwithid(__s32 destid, __u16 pos, __s32 streamid)
{
	struct queue *temp;
	
	if (pos < 0 || pos >= STREAM_NUM) return -1;

	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}

	temp->src_port = pos; // src_port 记录pos
	memcpy(&temp->src_ip, &streamid,sizeof(__s32)); // src_ip 记录streamid
	memcpy(&temp->dst_ip, &destid,sizeof(__s32)); //dst_ip 记录 destid
	temp->proc_type = ADD_PREVIEW_LIST;
	temp->next = 0;

	add_prilogictail(temp);
	queue_delayed_work(logic_queue, &logic_work, 0);
	return  0;
}


static int proc_del_stream_list(struct queue  *logic_temp)
{
	struct udp_entry *entry, *temp;
	struct stat_entry *s, *stemp;
	struct user_stream_t *buf, *btemp;
	

	__u16 pos = logic_temp->src_port;

	pr_notice("%s %d \n", "del stream input", pos);
	// clear user
	mutex_lock(&udp_mutex);
	entry = udp_entrys[pos];
	udp_entrys[pos] = 0;
	mutex_unlock(&udp_mutex);

	// clear statics
	mutex_lock(&sta_mutex);
	s = stat[pos];
	stat[pos] = 0;
	mutex_unlock(&sta_mutex);	

	while (entry != 0)
	{
		temp = entry;
		entry = entry->next;
		kfree(temp);
		user_num--;
	}	

	while (s != 0)
	{
		stemp = s;
		s = s->next;
		kfree(stemp);
	}

	// clear streambuf
	buf = stream_buf[pos];
	while (buf != 0)
	{
		btemp = buf;
		buf = buf->next;
		free_onebuf(btemp);
	}
	stream_buf[pos] = 0;

	// clear vip and some user
	reset_vip(pos);
	return 1;
}

void del_stream(__u16 pos)
{
	struct queue *temp;

	if (pos < 0 || pos >= STREAM_NUM) return -1;

	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}

	temp->src_port = pos; // src_port 记录pos
	temp->proc_type = DEL_STREAM_LIST;
	temp->next = 0;

	add_prilogictail(temp);
	queue_delayed_work(logic_queue, &logic_work, 0);
	return  0;
}


static int proc_del_user_list(struct queue  *temp)
{
	struct udp_entry *entry;
	__u16 pos = temp->src_port;
	__s32 id;
	__u32 ip = temp->dst_ip;

	memcpy(&id, &temp->src_ip, sizeof(__s32));

	pr_notice("%s %d %d %d \n", "del user input", pos, id, temp->dst_ip);

	mutex_lock(&udp_mutex);
	entry = udp_entrys[pos];
	while (entry)
	{
		//允许多个DOWN_NODE,所以删除时需要进行ip比较
		if ((id >= 0 && entry->id == id) || (entry->id == id&& entry->ip == ip))
		{
			if (entry->pre) entry->pre->next = entry->next;
			else
			{
				udp_entrys[pos] = entry->next;
			}
			if (entry->next) entry->next->pre = entry->pre;

			mutex_unlock(&udp_mutex);
			if (id > 0)
			{
				del_onestreambuf(pos, id);
			}
			if (id == 0) del_host(pos);
			user_num--;
			pr_notice("%s %d %d  %s\n", "del one user", pos, id, __inet_ntoa(ip));
			kfree(entry);			
			return 1;
		}
		if (entry->next != 0) entry = entry->next;
		else break;
	}
	mutex_unlock(&udp_mutex);

	pr_notice("%s %d %d\n", "del one user,but find error", pos, id);
	return -1;
}

int del_user(__u16 pos, __s32 id, __u32 ip)
{
	struct queue *temp;

	if (pos < 0 || pos >= STREAM_NUM) return -1;

	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}

	memcpy(&temp->src_ip,  &id,sizeof(__s32)); //src_ip 记录id
	temp->src_port = pos; // src_port 记录pos
	temp->dst_ip = ip;// dst_ip 记录 ip
	temp->proc_type = DEL_USER_LIST;
	temp->next = 0;

	add_prilogictail(temp);
	queue_delayed_work(logic_queue, &logic_work, 0);
	//schedule_delayed_work(&tx_work, 0);
	return  0;
}

static int proc_add_user_list(struct queue  *temp)
{
	struct udp_entry *new_entry, *entry;
	int j;
	__s32 id;
	__u16 pos = temp->src_port;

	memcpy(&id, &temp->src_ip, sizeof(__s32));


	pr_notice("%s %d %d %d %d\n", "add user input", pos, id, temp->dst_ip, temp->dst_port);

	new_entry = (struct udp_entry*)kzalloc(sizeof(struct udp_entry), GFP_ATOMIC);
	if (!new_entry)
	{
		pr_notice("%s\n", "allocate new_entry memory error!");
		return -1;
	}

	new_entry->ip = temp->dst_ip;
	new_entry->port = temp->dst_port;
	new_entry->update = get_seconds();
	new_entry->id = id;

	if (isinLan(temp->dst_ip, int_self_ip, int_mask))//内网目前暂时不考虑valn划分，以后:需要确定是否在同一vlan，然后来采用不同的发送方法
	{
		pr_notice("%s\n", "new user is int network");
		new_entry->isext = 0;
		memcpy(new_entry->mac, temp->mac, ETH_ALEN);
	}
	else if (isinLan(temp->dst_ip, ext_self_ip, ext_mask))
	{
		pr_notice("%s\n", "new user is in same lan,but it is ext  network");
		new_entry->isext = 1;
		memcpy(new_entry->mac, temp->mac, ETH_ALEN);
	}
	else
	{
		//广播包在Vm虚拟机系统下发不出去，尽量不能采用广播包发送
		new_entry->isext = 1;
		memcpy(new_entry->mac, gateway_mac, ETH_ALEN);
	}
	new_entry->next = 0;
	new_entry->pre = 0;

	mutex_lock(&udp_mutex);
	entry = udp_entrys[pos];
	while (entry)
	{
		if (entry->next != 0) entry = entry->next;
		else break;
	}
	if (entry == 0)
	{
		udp_entrys[pos] = new_entry;
		streamkey++;
		if (streamkey == 0) streamkey = 1;
		streamkey_table[pos] = streamkey;
	}
	else
	{
		entry->next = new_entry;
		new_entry->pre = entry;
	}

	user_num++;
	if (id == 0) add_host(pos, temp->dst_ip, temp->dst_port);

	if (id == DOWN_NODE)
	{
		int j;
		for (j = 0; j < MAX_VIP; j++)
		{
			if (vip_table[pos][j].id != -1)
			{
				push_buf(new_entry, pos, vip_table[pos][j].id);
			}
		}
		push_buf(new_entry, pos, 0);
	}
	mutex_unlock(&udp_mutex);

	pr_notice("%s %d %d %s %d\n", "add one user", pos, id, __inet_ntoa(temp->dst_ip), temp->dst_port);
	/*pr_notice("%s %d %d %d %d %d %d \n", "mac is", new_entry->mac[0], new_entry->mac[1], new_entry->mac[2],
	new_entry->mac[3], new_entry->mac[4], new_entry->mac[5]);*/
	return 1;
}


int add_user(__u16 pos, __s32 id, __u32 destip, __u16 destport, __u8 *mac)
{
	struct queue *temp;

	if (!mac) return -1;
	if (pos < 0 || pos >= STREAM_NUM) return -1;

	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}

	memcpy(&temp->src_ip,&id,sizeof(__s32)); //src_ip 记录id
	temp->src_port = pos; // src_port 记录pos
	temp->dst_ip = destip;
	temp->dst_port = destport;
	temp->proc_type = ADD_USER_LIST;
	temp->next = 0;
	memcpy(temp->mac, mac, ETH_ALEN);

	pr_notice("%s %d %d %d %d \n", "add user ", pos, id, destip,destport);
	
	add_prilogictail(temp);
	queue_delayed_work(logic_queue, &logic_work, 0);
	return  0;
}	



static int  proc_test_speed_list(struct queue  *temp)
{
	struct queue  *tx_temp;

	tx_temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!tx_temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}

	tx_temp->data = (__u8*)kzalloc(sizeof(__u8)*temp->len, GFP_ATOMIC);
	if (!tx_temp->data)
	{
		kfree(tx_temp);
		pr_notice("%s\n", "allocate tx_temp->data memory error!");
		return -1;
	}
	memcpy(tx_temp->data, temp->data, temp->len);
	tx_temp->len = temp->len;

	if (is_extern_net(temp->dst_ip))   //extern network
	{
		if (!isinLan(temp->src_ip, ext_self_ip, ext_mask))
			memcpy(tx_temp->mac, gateway_mac, ETH_ALEN);
		else
			memcpy(tx_temp->mac, temp->mac, ETH_ALEN);
	}
	else
		memcpy(tx_temp->mac, temp->mac, ETH_ALEN);
	tx_temp->src_ip = temp->dst_ip;
	tx_temp->dst_ip = temp->src_ip;
	tx_temp->src_port = temp->dst_port;
	tx_temp->dst_port = temp->src_port;
	tx_temp->proc_type = PUSH_LIST;
	add_pritxtail(tx_temp);
	queue_delayed_work(tx_queue, &tx_work, 0);
	return 0;
}	

int proc_test(struct sk_buff *skb,struct udphdr *udph, struct iphdr *iph, struct ethhdr *eth)
{	
	int data_len;
	struct queue *temp;
	int  offset = sizeof(struct udphdr) + sizeof(struct iphdr);

	if (!skb || !udph || !iph || !eth) return -1;

	if (user_num > max_user)
	{
		pr_notice("%s  %d\n", "maxuser limited ",user_num );
		return 0;
	}

	data_len = skb->len - offset;
	if (data_len > MTU_LEN) data_len = MTU_LEN;

	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate temp memory error!");
		return -1;
	}
	temp->data = (__u8*)kzalloc(sizeof(__u8)*data_len, GFP_ATOMIC);
	if (!temp->data)
	{
		kfree(temp);
		pr_notice("%s\n", "allocate temp->data memory error!");
		return -1;
	}
	if (skb_copy_bits(skb, offset, temp->data, data_len))
	{
		kfree(temp->data);
		kfree(temp);
		pr_notice("%s\n", "copy temp->data memory error!");
		return -1;
	}
	temp->len = data_len;
	temp->src_ip = iph->saddr;
	temp->src_port = htons(udph->source);
	temp->dst_ip = iph->daddr;
	temp->dst_port = htons(udph->dest);
	temp->proc_type = TEST_SPEED_LIST;
	temp->next = 0;
	memcpy(temp->mac, eth->h_source, ETH_ALEN);
	add_prilogictail(temp);

	queue_delayed_work(logic_queue, &logic_work, 0);
	return  0;
}


static int proc_heart_list(struct queue  *temp)
{
	struct udp_entry *new_entry, *entry;
	__s32 id;
	__u16 num;
	__u32 now;

	num = temp->dst_port - MIN_PORT;
	id = *((__u32*)(temp->data + sizeof(__u32)));

	if (id >= 0)
	{
		int len;
		int start;
		struct stat_entry *sta, *new_stat;

		len = temp->len - 2 * sizeof(__u32);
		start = 2 * sizeof(__u32);

		mutex_lock(&sta_mutex);
		sta = find_stat(num, id);
		if (sta == 0)
		{
			new_stat = (struct stat_entry*)kzalloc(sizeof(struct stat_entry), GFP_ATOMIC);
			if (!new_stat)
			{
				pr_notice("%s\n", "allocate new_stat memory error!");
				mutex_unlock(&sta_mutex);
				return -1;
			}
			sta = add_stat(new_stat, num, id);
			pr_notice("%s %d  %d\n", "heart: add new statics ", num, id);
		}

		sta->stat.stream_num = 0;
		if (sta->flag == 0)
		{
			sta->stat.up_bytes = 0;
			sta->stat.up_end_seq = 0;
			sta->stat.up_realpkgnum = 0;
			sta->stat.up_start_seq = 0;
		}

		while ((len >= 5 * sizeof(__u32)) && (sta->stat.stream_num < MAX_VIDEO))
		{
			sta->stat.stream[sta->stat.stream_num].id = *((__s32*)(temp->data + start));
			start += sizeof(__s32);
			sta->stat.stream[sta->stat.stream_num].down_recv_lost = *((__s32*)(temp->data + start));
			start += sizeof(__s32);
			sta->stat.stream[sta->stat.stream_num].down_fec_lost = *((__s32*)(temp->data + start));
			start += sizeof(__s32);
			sta->stat.stream[sta->stat.stream_num].down_fill_lost = *((__s32*)(temp->data + start));
			start += sizeof(__s32);
			sta->stat.stream[sta->stat.stream_num].down_save_data = *((__s32*)(temp->data + start));
			start += sizeof(__s32);
			len -= 5 * sizeof(__s32);
			sta->stat.stream_num++;
		}
		//pr_notice("%s %d %d\n", "heart: statis ", id, sta->stat.stream_num);
		mutex_unlock(&sta_mutex);
	}

	now = get_seconds();
	mutex_lock(&udp_mutex);
	entry = udp_entrys[num];
	while (entry != 0)
	{
		if (entry->id == id)
		{
			entry->update = now;
			entry->ip = temp->src_ip;
			entry->port = temp->src_port;
			if (id == 0) add_host(num, temp->src_ip, temp->src_port);
			mutex_unlock(&udp_mutex);
			return 1;
		}
		if (entry->next != 0) entry = entry->next;
		else break;
	}
	new_entry = (struct udp_entry*)kzalloc(sizeof(struct udp_entry), GFP_ATOMIC);
	if (!new_entry)
	{
		pr_notice("%s\n", "allocate new_entry memory error!");
		mutex_unlock(&udp_mutex);
		return -1;
	}
	new_entry->ip = temp->src_ip;
	new_entry->port = temp->src_port;
	new_entry->update = now;
	new_entry->id = id;
	new_entry->isext = is_extern_net(temp->dst_ip);
	if (new_entry->isext)
	{
		if (isinLan(temp->src_ip, ext_self_ip, ext_mask))
		{
			memcpy(new_entry->mac, temp->mac, ETH_ALEN);
			pr_notice("%s\n", "new user is in same lan,but it is ext  network");
		}
		else
			memcpy(new_entry->mac, gateway_mac, ETH_ALEN);
	}
	else
	{
		memcpy(new_entry->mac, temp->mac, ETH_ALEN);
	}
	new_entry->next = 0;
	new_entry->pre = 0;

	if (entry == 0)
	{
		udp_entrys[num] = new_entry;
		streamkey++;
		if (streamkey == 0) streamkey = 1;
		streamkey_table[num] = streamkey;
	}
	else
	{
		entry->next = new_entry;
		new_entry->pre = entry;
	}
	user_num++;
	if (id == 0)  add_host(num, temp->src_ip, temp->src_port);

	if (id >= 0)
	{
		int j;
		unsigned short p = temp->dst_port - MIN_PORT;
		for (j = 0; j < MAX_VIP; j++)
		{
			if (vip_table[num][j].id != -1 && vip_table[num][j].id != id)
			{
				pr_notice("%s %d\n", "send vip buf", vip_table[num][j].id);
				push_buf(new_entry, p, vip_table[num][j].id);
			}
		}
		if (id != 0)
		{
			push_buf(new_entry, p, 0);
		}
	}
	mutex_unlock(&udp_mutex);

	pr_notice("%s %d %d  %s %d\n", "heart add user ", num, id, __inet_ntoa(temp->src_ip), temp->src_port);
	pr_notice("%s %x %x %x %x %x %x\n", "src mac", temp->mac[0], temp->mac[1], temp->mac[2], temp->mac[3],
		temp->mac[4], temp->mac[5]);
	return  1;
}

int proc_heart(struct sk_buff *skb, struct udphdr *udph, struct iphdr *iph, struct ethhdr *eth)
{
	int  offset = sizeof(struct udphdr) + sizeof(struct iphdr);	
	struct queue *temp;
	int data_len;

	
	if (!skb || !udph || !iph || !eth) return -1;
	

	data_len = skb->len - offset;
	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}
	temp->data = (__u8*)kzalloc(sizeof(__u8)*data_len, GFP_ATOMIC);
	if (!temp->data)
	{
		kfree(temp);
		pr_notice("%s\n", "allocate tx_temp->data memory error!");
		return -1;
	}

	if (skb_copy_bits(skb, offset, temp->data, data_len))
	{
		kfree(temp->data);
		kfree(temp);
		pr_notice("%s\n", "copy tx_temp->data memory error!");
		return -1;
	}
	temp->len = data_len;
	temp->src_ip = iph->saddr;
	temp->src_port = htons(udph->source);
	temp->dst_ip = iph->daddr;
	temp->dst_port = htons(udph->dest);
	temp->proc_type = HEART_LIST;
	temp->next = 0;
	memcpy(temp->mac, eth->h_source, ETH_ALEN);

	//pr_notice("%s %d %d %d \n", "user ", temp->len, temp->src_port, temp->dst_port);

	add_prilogictail(temp);
	queue_delayed_work(logic_queue, &logic_work, 0);

	return  0;
}


int find_userstat(__u16 pos, __s32 id, struct fwd_stat *ret)
{
	struct stat_entry*  entry;
	int  i;

	if (!ret) return -1;
	if (pos < 0 || pos >= STREAM_NUM) return -1;
	
	//pr_notice("%s %d %d\n", "find user ", pos, id);
	mutex_lock(&sta_mutex);

	entry = find_stat(pos, id);
	if (entry == 0)
	{
		pr_notice("%s %d %d\n", "find user error", pos, id);
		mutex_unlock(&sta_mutex);
		return -1;

	}
	entry->flag = 0;
	//*ret = entry->stat;

	ret->id = entry->stat.id;
	ret->up_bytes = entry->stat.up_bytes;
	ret->up_start_seq = entry->stat.up_start_seq;
	ret->up_end_seq = entry->stat.up_end_seq;
	ret->up_realpkgnum = entry->stat.up_realpkgnum;
	ret->stream_num = entry->stat.stream_num;
	for (i = 0; i < entry->stat.stream_num; i++)
	{
		ret->stream[i].id = entry->stat.stream[i].id;
		ret->stream[i].down_fec_lost = entry->stat.stream[i].down_fec_lost;
		ret->stream[i].down_fill_lost = entry->stat.stream[i].down_fill_lost;
		ret->stream[i].down_recv_lost = entry->stat.stream[i].down_recv_lost;
		ret->stream[i].down_save_data = entry->stat.stream[i].down_save_data;
	}
	mutex_unlock(&sta_mutex);
	
	/*pr_notice("%s %d %d %d  %d %d %d %d %d \n", "data ",
		pos,id,
		entry->id, 
		entry->stat.id,
		entry->stat.up_bytes,
		entry->stat.up_realpkgnum,
		entry->stat.up_start_seq, 
		entry->stat.up_end_seq);*/
	
	return 0;
} 

static int proc_forward_list(struct queue  *logic_temp)
{
	struct stat_entry *new_stat, *sta;
	__u16 pos;
	__s32 id;
	__u32 seq;
	

	pos = logic_temp->dst_port - MIN_PORT;
	id = *((__s32*)logic_temp->data);
	seq = *((__s32*)(logic_temp->data + sizeof(__s32)));

	if ( streamkey_table[pos] != logic_temp->stream_key)
	{
		pr_notice("%s %d  %d \n", "forward stream is timeout ", streamkey_table[pos], logic_temp->stream_key);
		return -1;
	}
	if (!is_validstream(pos, id, logic_temp->src_ip, logic_temp->src_port))
	{
		pr_notice("%s  %d %s : %d\n", " reject ", id, __inet_ntoa(logic_temp->src_ip), logic_temp->src_port);
		return -1;
	}

	mutex_lock(&sta_mutex);
	sta = find_stat(pos, id);
	if (sta == 0)
	{
		new_stat = (struct stat_entry*)kzalloc(sizeof(struct stat_entry), GFP_ATOMIC);
		if (!new_stat)
		{
			pr_notice("%s\n", "allocate new_stat memory error!");
			mutex_unlock(&sta_mutex);
			return -1;
		}
		sta = add_stat(new_stat, pos, id);
		pr_notice("%s  %d %d\n", "create new statics buf ", pos, id);
	}
	if (!sta->flag)
	{
		//pr_notice("%s  %s : %d %d %d   %d  \n", "start stat", __inet_ntoa(iph->saddr), htons(udph->source),
		//host_addr[pos].ip, host_addr[pos].port, host_addr[pos].flag);
		sta->flag = 1;
		sta->stat.up_start_seq = seq;
		sta->stat.up_end_seq = seq;
		sta->stat.up_bytes = logic_temp->len;
		sta->stat.up_realpkgnum = 1;
	}
	else
	{
		sta->stat.up_end_seq = seq;
		sta->stat.up_bytes += logic_temp->len;
		sta->stat.up_realpkgnum++;
		//pr_notice("%s  %d %d   %d\n", "it stat", sta->stat.up_end_seq, sta->stat.up_bytes, sta->stat.up_realpkgnum);
	}
	mutex_unlock(&sta_mutex);

	add_streambuf(pos, id, logic_temp->data, logic_temp->len);

	struct queue  *tx_temp;
	tx_temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!tx_temp)
	{
		pr_notice("%s\n", "allocate tx_temp memory error!");
		return -1;
	}
	tx_temp->data = (__u8*)kzalloc(sizeof(__u8)*logic_temp->len, GFP_ATOMIC);
	if (!tx_temp->data)
	{
		kfree(tx_temp);
		pr_notice("%s\n", "allocate tx_temp->data memory error!");
		return -1;
	}
	tx_temp->stream_key = logic_temp->stream_key;
	memcpy(tx_temp->data, logic_temp->data, logic_temp->len);
	tx_temp->len = logic_temp->len;
	tx_temp->src_ip = logic_temp->dst_ip;
	tx_temp->src_port = logic_temp->dst_port;
	tx_temp->dst_ip = logic_temp->src_ip;
	tx_temp->dst_port = logic_temp->src_port;
	tx_temp->proc_type = TRAN_LIST;	
	add_pritxtail(tx_temp);
	queue_delayed_work(tx_queue, &tx_work, 0);
	return 1;
}

int proc_forward(struct sk_buff *skb,struct udphdr *udph, struct iphdr *iph  )
{	
	int  offset = sizeof(struct udphdr) + sizeof(struct iphdr);
	struct queue *temp;
	int data_len ;

	if (!skb || !udph || !iph ) return -1;

	data_len = skb->len - offset;
	if (data_len > MTU_LEN) data_len = MTU_LEN;

	temp = (struct queue*)kzalloc(sizeof(struct queue), GFP_ATOMIC);
	if (!temp)
	{
		pr_notice("%s\n", "allocate temp memory error!");
		return -1;
	}
	temp->data = (__u8*)kzalloc(sizeof(__u8)*data_len, GFP_ATOMIC);
	if (!temp->data)
	{
		kfree(temp);
		pr_notice("%s\n", "allocate temp->data memory error!");
		return -1;
	}
	if (skb_copy_bits(skb, offset, temp->data, data_len))
	{
		kfree(temp->data);
		kfree(temp);
		pr_notice("%s\n", "copy temp->data memory error!");
		return -1;
	}
	temp->len = data_len;	
	temp->src_ip = iph->saddr;
	temp->src_port = htons(udph->source);
	temp->dst_ip = iph->daddr;
	temp->dst_port = htons(udph->dest);
	temp->proc_type = FORWARD_LIST;
	temp->stream_key = streamkey_table[temp->dst_port - MIN_PORT];
	temp->next = 0;
	add_logictail(temp);
	queue_delayed_work(logic_queue, &logic_work, 0);	
	return 1;
}


static void tx_queue_process(struct work_struct *work)
{
	struct netpoll *np_t;
	struct netpoll_info *npinfo;
	int  dev_seq;

	struct queue  *temp = get_txhead();
	while (temp)
	{
		__u16 pos = temp->src_port - MIN_PORT;

		if (temp->stream_key != 0 && streamkey_table[pos] != temp->stream_key)
		{
			pr_notice("%s %d  %d \n", "stream is timeout ", streamkey_table[pos], temp->stream_key);
			goto clean;
		}
		if (temp->proc_type == PUSH_LIST)
		{
			//pr_notice("%s %d\n", "send video ", temp->dst_ip );
			if (is_inter_net(temp->src_ip))   // intern network
			{
				np_t = int_np_t;
			}
			else
			{
				np_t = ext_np_t;
			}
			if (!np_t) goto clean;
			//需要控制发送队列的个数，避免内存耗尽
			npinfo = rcu_dereference_bh(np_t->dev->npinfo);
			if (!npinfo || !netif_running(np_t->dev) || !netif_device_present(np_t->dev) || !(np_t->dev->flags & IFF_UP))
			{
				pr_notice("%s\n", " push get npinfot pointer error!");
				goto clean;
			}
			dev_seq = skb_queue_len(&npinfo->txq);
			if (dev_seq >= MAX_QUEUE)
			{
				pr_notice("%s  %d \n", "return, push send queue is ", dev_seq);
				goto clean;
			}
			if (dev_seq > 0)
				pr_notice("%s  %d \n", "push send queue is ", dev_seq);
			memcpy(np_t->remote_mac, temp->mac, ETH_ALEN);
			memcpy(&np_t->local_ip, &temp->src_ip, sizeof(__u32));
			memcpy(&np_t->remote_ip, &temp->dst_ip, sizeof(__u32));
			np_t->local_port = temp->src_port;
			np_t->remote_port = temp->dst_port;
			if (!send_data(np_t, temp->data, temp->len))
			{
				kfree(temp->data);
				kfree(temp);
				return;
			}
			//send_data(np_t, temp->data, temp->len);		
		}
		else if (temp->proc_type == TRAN_LIST)
		{
			__u32 now;
			__u16 pos;
			__s32 id;
			struct udp_entry *utemp, *entry;

			id = *((__s32*)temp->data);
			now = get_seconds();
			pos = temp->src_port - MIN_PORT;

			mutex_lock(&udp_mutex);
			entry = udp_entrys[pos];
			while (entry != 0)
			{
				// delete guest of timing out 
				/*if (now - entry->update > GUEST_TIMEOUT && entry->id > 0)
				{
					utemp = entry;
					entry = entry->next;
					pr_notice("%s %d\n", "del user", utemp->id);
					del_entry(pos, utemp);
					user_num--;
					continue;
				}*/
				//skip self 
				if ((entry->ip == temp->dst_ip && entry->port == temp->dst_port) || (id == entry->id))
				{
					entry = entry->next;
					continue;
				}
				// vip requesting forbid send 
				if (id != 0)
				{
					if (!(is_review(pos, id) && (entry->id == 0)) && (entry->id != UP_NODE) && !is_vip(pos, id))
					{
						entry = entry->next;
						continue;
					}
					/*if (!can_send_stream(entry, pos, id)) {
						entry = entry->next;
						continue;
					}*/
				}

				if (!can_send_stream(entry, pos, id)) {
					entry = entry->next;
					continue;
				}

				if (entry->isext)
				{
					np_t = ext_np_t;
				}
				else
				{
					np_t = int_np_t;
				}

				if (!np_t)
				{
					mutex_unlock(&udp_mutex);
					goto clean;
				}

				//需要控制发送队列的个数，避免内存耗尽
				npinfo = rcu_dereference_bh(np_t->dev->npinfo);
				if (!npinfo || !netif_running(np_t->dev) || !netif_device_present(np_t->dev) || !(np_t->dev->flags & IFF_UP))
				{
					pr_notice("%s\n", "trans get npinfot pointer error!");
					mutex_unlock(&udp_mutex);
					goto clean;
				}
				dev_seq = skb_queue_len(&npinfo->txq);
				if (dev_seq >= MAX_QUEUE)
				{
					mutex_unlock(&udp_mutex);
					pr_notice("%s  %d \n", "return,tran send queue is ", dev_seq);
					goto clean;
				}
				if (dev_seq > 0)
					pr_notice("%s  %d \n", "tran  send queue is ", dev_seq);

				memcpy(&np_t->local_ip, &temp->src_ip, sizeof(__u32));
				memcpy(&np_t->remote_ip, &entry->ip, sizeof(__u32));
				np_t->local_port = temp->src_port;
				np_t->remote_port = entry->port;
				memcpy(np_t->remote_mac, entry->mac, ETH_ALEN);
				entry = entry->next;
				if (!send_data(np_t, temp->data, temp->len))
				{
					kfree(temp->data);
					kfree(temp);
					mutex_unlock(&udp_mutex);
					return;
				}
			}
			mutex_unlock(&udp_mutex);
		}		
	clean:
		kfree(temp->data);
		kfree(temp);
		temp = get_txhead();
	}
	return;
}

static void logic_queue_process(struct work_struct *work)
{
	struct queue  *temp = get_logichead();	

	while (temp)
	{
		if (temp->proc_type == TEST_SPEED_LIST) // test speed
		{
			 proc_test_speed_list(temp);
		}
		else if (temp->proc_type == FORWARD_LIST) // general streams
		{
			 proc_forward_list(temp);
		}
		else if (temp->proc_type == HEART_LIST)
		{
			 proc_heart_list(temp);
		}
		else if (temp->proc_type == ADD_USER_LIST)
		{
			 proc_add_user_list(temp);
		}
		else if (temp->proc_type == DEL_USER_LIST)
		{
			 proc_del_user_list(temp);
		}
		else if (temp->proc_type == DEL_STREAM_LIST)
		{
			 proc_del_stream_list(temp);
		}
		else if (temp->proc_type == ADD_PREVIEW_LIST)
		{
			 proc_add_preview_list(temp);
		}
		else if (temp->proc_type == ADD_VIP_LIST)
		{
			 proc_add_vip_list(temp);
		}
		else if (temp->proc_type == SET_STOP_LIST)
		{
			proc_set_stoplist(temp);
		}		

		if(temp->data)  kfree(temp->data);
		kfree(temp);		
		temp = get_logichead();
	}
}

int init_proc(void)
{	
	//udp_entrys = (struct udp_entry**)kmalloc(sizeof(struct udp_entry*) * STREAM_NUM, 0);           //最多1000路
	udp_entrys = (struct udp_entry**)kzalloc(sizeof(struct udp_entry*) * STREAM_NUM, GFP_ATOMIC);    //最多1000路
	if (!udp_entrys)
	{
		pr_notice("%s\n", "allocate udp_entrys memory error!");
		return -1;
	}

	stat = (struct stat_entry**)kzalloc(sizeof(struct stat_entry*) * STREAM_NUM, GFP_ATOMIC);    //最多1000路
	if (!stat)
	{
		kfree(udp_entrys);
		pr_notice("%s\n", "allocate stat  memory error!");
		return -1;
	}
	
	//memset(udp_entrys, 0, sizeof(struct udp_entry*) * STREAM_NUM);
	struct netpoll *int_np_t;

	int_np_t = (struct netpoll*)kzalloc(sizeof(struct netpoll), GFP_ATOMIC);
	if (!int_np_t)
	{
		pr_notice("%s\n", "allocate int_np  memory error!");
		return -1;
	}
	ext_np_t = (struct netpoll*)kzalloc(sizeof(struct netpoll), GFP_ATOMIC);
	if (!ext_np_t)
	{
		pr_notice("%s\n", "allocate ext_np  memory error!");
		return -1;
	}

	streamkey_table = (__u32*)kzalloc(sizeof(__u32)*STREAM_NUM, GFP_ATOMIC);
	if (!streamkey_table)
	{
		pr_notice("%s\n", "allocate streamkey_table memory error!");
		return -1;
	}

	int_np_t->name = INTDEV_NAME;
	//pr_notice("%s\n", int_device);
	strlcpy(int_np_t->dev_name, int_device, IFNAMSIZ);
	if (netpoll_setup(int_np_t))
	{
		kfree(int_np_t);
		int_np_t = 0;
		pr_notice("%s\n", "int_device init error");
	}

	ext_np_t->name = EXTDEV_NAME;
	//pr_notice("%s\n", ext_device);
	strlcpy(ext_np_t->dev_name, ext_device, IFNAMSIZ);
	netpoll_setup(ext_np_t);
	if (netpoll_setup(ext_np_t))
	{
		kfree(ext_np_t);
		ext_np_t = 0;
		pr_notice("%s\n", "ext_device init error");
	}

	tx_head = 0;
	tx_tail = 0;
	pritx_head = 0;
	pritx_tail = 0;
	tx_queue = create_singlethread_workqueue("trans queue");
	if (!tx_queue) return -1;
	INIT_DELAYED_WORK(&tx_work, tx_queue_process);

	logic_head = 0;
	logic_tail = 0;
	prilogic_head = 0;
	prilogic_tail = 0;
	logic_queue = create_singlethread_workqueue("logic queue");
	if (!logic_queue) return -1;
	INIT_DELAYED_WORK(&logic_work, logic_queue_process);

	user_num = 0;
	streamkey = 0;

	//一些特别的服务器架构，需要控制流量
	flow_tick = jiffies;
	flow_num = 0;	
		
	return 0;
}

void restart_proc(void)
{
	int i;
	struct udp_entry *entry,*temp;
	struct stat_entry *s, *stemp;
	unsigned long flags;

	for (i = 0; i < STREAM_NUM; i++)
	{		
		entry = udp_entrys[i];
		while (entry)
		{
			temp = entry;
			entry = entry->next;
			kfree(temp);
		}
		udp_entrys[i] = 0;
	
		s = stat[i];
		while (s)
		{
			stemp = s;
			s = s->next;
			kfree(stemp);
		}
		stat[i] = 0;
	}
	user_num = 0;
}

void exit_proc(void)
{
	int i;
	struct udp_entry *entry, *temp;
	struct stat_entry *s, *stemp;
	struct queue *tx, *tx_temp;

	//first cancel
	cancel_delayed_work(&tx_work);
	cancel_delayed_work(&logic_work);

	for (i = 0; i < STREAM_NUM; i++)
	{
		entry = udp_entrys[i];
		while (entry)
		{
			temp = entry;
			entry = entry->next;
			kfree(temp);
		}
		s = stat[i];
		while (s)
		{
			stemp = s;
			s = s->next;
			kfree(stemp);
		}
	}
	kfree(udp_entrys);
	kfree(stat);
	
	
	//clean tx_queue
	tx = pritx_head;
	while (tx)
	{
		tx_temp = tx;
		tx = tx->next;
		kfree(tx_temp);
	}
	tx = tx_head;
	while (tx)
	{
		tx_temp = tx;
		tx = tx->next;
		kfree(tx_temp);
	}
	//now cancel it again
	cancel_delayed_work(&tx_work);
	cancel_delayed_work(&logic_work);

	//flush_workqueue(single_queue);
	destroy_workqueue(tx_queue);
	destroy_workqueue(logic_queue);
}

void test_fwd(void)
{
	add_user(0, 1, 3323232,12345, broadcast_mac);
	del_user(0, 1, 34623842);
	del_stream(0);
	push_bufwithid(0, 0,1);
	add_guest_vip(0, 1);
	__s16 sendlist[MAX_VIP] = {1,2,3,4,5,6,7,8};

	add_user(0, 3, 3323232, 12345, broadcast_mac);
	set_stoplist(0, 3, sendlist);
}
	
