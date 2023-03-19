#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include "vip.h"
#include "proc.h"

struct guest_t  **vip_table;
struct guest_t  *review_table;
struct guest_t  *host_table;
__s8  *group_mode;
//struct guest_t  **node_table;

//__s32  **vip_table;
//static __s32  *review_table;

/*__s8 add_node(__u16 pos, __s32 id, __u32 ip, __u16 port)
{
	__s32  i;
	//pr_notice("add vip %hd  %d\n", pos, id);

	for (i = 0; i < MAX_NODE; i++)
	{
		if (node_table[pos][i].id == -1)
		{
			node_table[pos][i].id = id;
			node_table[pos][i].ip = ip;
			node_table[pos][i].port = port;		
			return 0;
		}
	}
	return  -1;
}

__s8 del_node(__u16 pos, __s32 id,__u32 ip)
{
	__s32  i;

	//pr_notice("del vip %hd  %d\n", pos, id);

	for (i = 0; i < MAX_NODE; i++)
	{
		if (node_table[pos][i].id == id && node_table[pos][i].ip == ip )
		{
			node_table[pos][i].id = -1;
			node_table[pos][i].ip = 0;
			node_table[pos][i].port = 0;
			return 0;
		}
	}
	return  -1;
}*/


__s8 add_host(__u16 pos, __u32 ip, __u16 port)
{
	int k;

	if (pos < 0 || pos >= STREAM_NUM) return -1;

	host_table[pos].id = 0;
	host_table[pos].ip = ip;
	host_table[pos].port = port;

	//for (k = 0; k < MAX_GROUP_LIST; k++)
		//host_table[pos].can_send_list[k] = -1;
	return  0;
}

__s8 del_host(__u16 pos)
{
	int k;
	if (pos < 0 || pos >= STREAM_NUM) return -1;

	host_table[pos].id = -1;
	host_table[pos].ip = 0;
	host_table[pos].port = 0;
	for (k = 0; k < MAX_GROUP_LIST; k++)
		host_table[pos].can_send_list[k] = -1;
	return  0;
}

__s8 add_vip(__u16 pos, __s32 id)
{	
	//__s32  k;
	//pr_notice("add vip %hd  %d\n", pos, id);

	if (pos < 0 || pos >= STREAM_NUM) return -1;
	return add_guest_vip(pos,id);	
}

__s8 del_vip(__u16 pos, __s32 id)
{
	__s32  i,k;

	//pr_notice("del vip %hd  %d\n", pos, id);

	if (pos < 0 || pos >= STREAM_NUM) return -1;

	for (i = 0; i < MAX_VIP; i++)
	{
		if (vip_table[pos][i].id == id)
		{
			vip_table[pos][i].id = -1;
			vip_table[pos][i].ip = 0;
			vip_table[pos][i].port = 0;
			for (k = 0; k < MAX_GROUP_LIST; k++)
				vip_table[pos][i].can_send_list[k] = -1;
			return 0;
		}
	}
	return  -1;
}

__u8 is_vip(__u16 pos, __s32 id)
{
	__s32  i;

	if (pos < 0 || pos >= STREAM_NUM) return 0;

	for (i = 0; i < MAX_VIP; i++)
	{
		if (vip_table[pos][i].id == id)
		{
			return 1;
		}
	}
	return 0;
}

__u8 is_validstream(__u16 pos, __s32 id,__u32 ip, __u16 port)
{
	__s32  i;

	if (pos < 0 || pos >= STREAM_NUM) return 0;

	if (id == 0)
	{
		if (host_table[pos].id == -1) return 1;
		if (host_table[pos].ip == ip && host_table[pos].port == port) return 1;
		return 0;
	}

	if (review_table[pos].id == id)
	{
		if (review_table[pos].ip == 0 ) return 1;
		if ( review_table[pos].ip == ip && review_table[pos].port == port) return 1;		
		return 0;
	}

	for (i = 0; i < MAX_VIP; i++)
	{
		if (vip_table[pos][i].id == id)
		{
			if (vip_table[pos][i].ip == 0) return 1;
			if (vip_table[pos][i].ip == ip && vip_table[pos][i].port == port)	return 1;
			return 0;
		}
	}
	return 1;
}

__s8 add_review(__u16 pos, __s32 id)
{
	__u32 ip;
	__u16 port;

	if (pos < 0 || pos >= STREAM_NUM) return -1;

	review_table[pos].id = id;
	push_bufwithid(0, pos, id);
	return  0;
}

__s8 del_review(__u16 pos, __s32 id)
{
	if (pos < 0 || pos >= STREAM_NUM) return -1;

	review_table[pos].id = -1;
	review_table[pos].ip = 0;
	review_table[pos].port = 0;
	return  0;
}


__u8 is_review(__u16 pos, __s32 id)
{
	if (pos < 0 || pos >= STREAM_NUM) return 0;

	if (review_table[pos].id == id)
	{
		return 1;
	}	
	return 0;
}

void set_groupmode_sendlist(__u16 pos, __s32 id, __s16 *sendlist )
{
	int i;

	if (pos < 0 || pos >= STREAM_NUM) return ;
	if (id == 0)
	{
		if ( host_table[pos].id == -1) return ;
		memcpy( host_table[pos].can_send_list, sendlist, sizeof(__s16) * MAX_GROUP_LIST);
	}
	else
	{
		for (i = 0; i < MAX_GROUP_LIST; i++)
		{
			if (vip_table[pos][i].id == id)
			{
				memcpy(vip_table[pos][i].can_send_list, sendlist, sizeof(__s16) * MAX_GROUP_LIST);
				return;
			}
		}
	}
}

void reset_vip(__u16 pos)
{
	int  i,k;

	if (pos < 0 || pos >= STREAM_NUM) return ;

	for (i = 0; i < MAX_VIP; i++)
	{
		vip_table[pos][i].id = -1;
		vip_table[pos][i].ip = 0;
		vip_table[pos][i].port = 0;
		for (k = 0; k < MAX_GROUP_LIST; k++)
			vip_table[pos][i].can_send_list[k] = -1;
	}
	review_table[pos].id = -1;
	review_table[pos].ip = 0;
	review_table[pos].port = 0;
	/*for (i = 0; i < MAX_NODE; i++)
	{
		node_table[pos][i].id = -1;
		node_table[pos][i].ip = 0;
		node_table[pos][i].port = 0;
	}*/
	host_table[pos].id = -1;
	host_table[pos].ip = 0;
	host_table[pos].port = 0;

	for (i = 0; i < MAX_GROUP_LIST; i++)
		host_table[pos].can_send_list[i] = -1;
}

void restart_vip(void)
{
	int i;
	for (i = 0; i < STREAM_NUM; i++)
		reset_vip(i);	
}

int init_vip(void)
{
	int  i;
	int  j;
	int k;
	
	review_table = (struct guest_t*)kzalloc(sizeof(struct guest_t) * STREAM_NUM, GFP_ATOMIC);
	if (!review_table)
	{
		pr_notice("%s\n", "allocate review_table memory error!");
		return -1;
	}
	
	vip_table = (struct guest_t**)kzalloc(sizeof(struct guest_t*) * STREAM_NUM, GFP_ATOMIC);
	if (!vip_table)
	{
		kfree(review_table);
		pr_notice("%s\n", "allocate vip_table memory error!");
		return -1;
	}

	host_table = (struct guest_t*)kzalloc(sizeof(struct guest_t) * STREAM_NUM, GFP_ATOMIC);
	if (!host_table)
	{
		kfree(review_table);
		kfree(vip_table);
		pr_notice("%s\n", "allocate host_table memory error!");
		return -1;
	}

	group_mode = (__s8*)kzalloc(sizeof(__s8) * STREAM_NUM, GFP_ATOMIC);
	if (!group_mode)
	{
		kfree(review_table);
		kfree(vip_table);
		kfree(host_table);
		pr_notice("%s\n", "allocate group_mode memory error!");
		return -1;
	}
	/*node_table = (struct guest_t**)kzalloc(sizeof(struct guest_t*) * STREAM_NUM, GFP_KERNEL);
	if (!node_table)
	{
		kfree(review_table);
		kfree(vip_table);
		kfree(host_table);
		pr_notice("%s\n", "allocate node_table memory error!");
		return -1;
	}*/

	for (i = 0; i < STREAM_NUM; i++)
	{
		vip_table[i] = (struct guest_t*)kzalloc(sizeof(struct guest_t) * MAX_VIP, GFP_ATOMIC);
		if (!vip_table[i])
		{
			pr_notice("%s  %d\n", "allocate vip_table memory error!",i);
			return -1;
		}

		//node_table[i] = (struct guest_t*)kzalloc(sizeof(struct guest_t) * MAX_NODE, GFP_KERNEL);
		//if (!node_table[i])			return -1;

		for (j = 0; j < MAX_VIP; j++)
		{
			vip_table[i][j].id = -1;
			vip_table[i][j].ip = 0;
			vip_table[i][j].port = 0;
			for (k = 0; k < MAX_GROUP_LIST; k++)
				vip_table[i][j].can_send_list[k] = -1;
		}
		/*for (j = 0; j < MAX_NODE; j++)
		{
			node_table[i][j].id = -1;
			node_table[i][j].ip = 0;
			node_table[i][j].port = 0;
		}*/
		review_table[i].id = -1;
		review_table[i].ip = 0;
		review_table[i].port = 0;

		host_table[i].id = -1;
		host_table[i].ip = 0;
		host_table[i].port = 0;
		for (k = 0; k < MAX_GROUP_LIST; k++)
			host_table[i].can_send_list[k] = -1;
	}
	return 0;
	//spin_lock_init(&lock);
}

void exit_vip(void)
{
	int i;

	for (i = 0; i < STREAM_NUM; i++)
	{	
		kfree(vip_table[i]);
		//kfree(node_table[i]);
	}
	kfree(vip_table);
	kfree(review_table);
	//kfree(node_table);
	kfree(host_table);
}
