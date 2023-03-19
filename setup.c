#include <linux/init.h>
#include <asm/uaccess.h>  
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/netpoll.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/inet.h>

#include"setup.h"
#include "vip.h"
#include "proc.h"
#include "udp_forward.h"
#include "streambuf.h"

__u32 ext_mask = 0;
__u32 int_mask = 0;
__u32 ext_self_ip = 0;
__u32 int_self_ip = 0;
__s8 ext_device[IFNAMSIZ];
__s8 int_device[IFNAMSIZ];
__u8 gateway_mac[ETH_ALEN];
__u8 active = MOD_STOP;
__u32 max_user = 0;
__s32 limit_speed = 0;

//设置统计的id
static __s32  stat_id = 0;
static __u16  stat_port = 0;

//设备初始化
static struct cdev cdev_m; 
static int ctrl_in_use = 0;
/* Control device major number */
static int major = 0;
static int d_major = 0;
static int d_minor = 0;
static  struct class *my_class = 0;

//用于ioclt
static DEFINE_MUTEX(ioctl_mutex);

//设置的buffer
#define MAX_INPUT_LEN 256
__s8 *buff = 0;

static __u32 inet_addr(__s8 *str)
{
	int a, b, c, d;
	__u8 arr[4];

	if (!str) return 0;
	sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d);
	arr[0] = a; arr[1] = b; arr[2] = c; arr[3] = d;
	return *(unsigned int*)arr;
}
static  int get_hexint(__u8 *str)
{
	int len; 
	int i;
	int total = 0;

	if (!str) return -1;

	len = strlen(str);
	for (i = 0; i < len; i++)
	{
		if (str[i] >= '0' && str[i] <= '9')
			total = total*16+ str[i] - '0';
		else
		{
			total = total * 16 +str[i] - 'a' + 10;
		}		
	}
	return total;
}

static  int get_decint(__u8 *str)
{
	int len;
	int i;
	int total = 0;

	if (!str) return -1;

	len = strlen(str);
	for (i = 0; i < len; i++)
	{
		if (str[i] >= '0' && str[i] <= '9')
			total = total * 10 + str[i] - '0';		
	}
	return total;
}

static int get_mac(__s8 *in, __u8 *mac)
{
	__s8 *target_config;
	__s8 *input; 
	int num = 0;

	if (!in || !mac) return -1;

	input = in;
	//pr_notice("%s\n", config);
	while ((target_config = strsep(&input, ":")))
	{
		pr_notice("%s\n", target_config);
		//if (sscanf(target_config, "%02x", &mac[num]) != 1) return -1;
		mac[num] = get_hexint(target_config);
		num++;
		if (num > 5) break;
	}
	pr_notice("%s  %d %d %d %d %d %d \n", "mac", 
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return 0;
}

static int read_param(void)
{
	__s8 *target_config;
	__s8 *input = config;

	int num = 0;
	pr_notice("%s\n", config);
	while ((target_config = strsep(&input, ":")))
	{
		pr_notice("%s\n", target_config);

		if(num == 0) int_self_ip = inet_addr(target_config);
		else if (num == 1) int_mask = inet_addr(target_config);
		else if (num == 2) strcpy(int_device, target_config);
		else if(num == 3 ) ext_self_ip = inet_addr(target_config);
		else if (num == 4) ext_mask = inet_addr(target_config);
		else if (num == 5) strcpy(ext_device, target_config);
		else if (num == 6) gateway_mac[num - 6] = get_hexint(target_config);
		else if (num == 7) gateway_mac[num - 6] = get_hexint(target_config);
		else if (num == 8) gateway_mac[num - 6] = get_hexint(target_config);
		else if (num == 9) gateway_mac[num - 6] = get_hexint(target_config);
		else if (num == 10) gateway_mac[num - 6] = get_hexint(target_config);
		else if (num == 11) gateway_mac[num - 6] = get_hexint(target_config);
		else if (num == 12) max_user = get_decint(target_config);
		else if (num == 13) limit_speed = get_decint(target_config);
		num++;
		
		pr_notice("%d %d %d %s %d %d %s\n", num, int_self_ip, int_mask, int_device, ext_self_ip, ext_mask, ext_device);

	}
	pr_notice("%s  %d %d %d %d %d %d %d %d \n", "mac", max_user, limit_speed,
		gateway_mac[0], gateway_mac[1], gateway_mac[2], gateway_mac[3], gateway_mac[4], gateway_mac[5]);

	return 0;
}




/*
* File operations functions for control device
*/

static int fwd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{	
	mutex_lock(&ioctl_mutex);
	switch (cmd)
	{
	case FWD_GET_VERS:
		return FWD_VERS;
	case FWD_SET_ACTIVATE:
		if (active == MOD_START)
		{
			active = MOD_STOP;
			pr_notice("FWD: restart.error,please del.sh\n");
		}
		else
		{
			active = MOD_START;
			pr_notice("FWD: Activated.\n");
		}
		break;
	case FWD_SET_STOP:
		active = MOD_STOP;
		pr_notice("FWD: Deactivated.\n");
		break;
	case FWD_SET_RESTART:
		active = MOD_RESTART;
		pr_notice("FWD: restart.\n");
		break;
	
	case FWD_DEL_VIP:
	{
		__u16 port;
		__s32 id;
		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("del vip , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %d", &port, &id) != 2)
		{
			pr_notice("del vip  , sscanf err!  \n");
			goto error;
		}
		del_vip(port - MIN_PORT, id);
		pr_notice("del vip  %d  %d \n", port, id);
		break;
	}
	case FWD_ADD_VIP:
	{
		__u16 port;
		__s32 id;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("add vip , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %d", &port, &id) != 2)
		{
			pr_notice("add vip  , sscanf err!  \n");
			goto error;
		}
		add_vip(port - MIN_PORT, id);
		pr_notice("add vip  %d  %d \n", port, id);
		break;
	}
	case FWD_ADD_PREVIEW:
	{
		__u16 port;
		__s32 id;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("add preview , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %d", &port, &id) != 2)
		{
			pr_notice("add preview , sscanf err!  \n");
			goto error;
		}
		add_review(port - MIN_PORT, id);
		pr_notice("add preview  %d  %d \n", port, id);
		break;
	}
	case FWD_DEL_PREVIEW:
	{
		__u16 port;
		__s32 id;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("del preview , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %d", &port, &id) != 2)
		{
			pr_notice("del preview , sscanf err!  \n");
			goto error;
		}
		del_review(port - MIN_PORT, id);
		pr_notice("del review  %d  %d \n", port, id);
		break;
	}
	case FWD_DEL_STREAM:
	{
		__u16 port;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("del  stream , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd", &port) != 1)
		{
			pr_notice("del stream , sscanf err!  \n");
			goto error;
		}
		pr_notice("del  stream %d\n", port);
		del_stream(port - MIN_PORT);
		break;
	}
	case FWD_SET_STATID:

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("set stat id , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %d", &stat_port, &stat_id) != 2)
		{
			pr_notice("set stat id , sscanf err!  \n");
			goto error;
		}
		//pr_notice("set stat id  %d, %d\n", stat_port,stat_id);
		break;
	case FWD_GET_STAT:
	{
		//struct fwd_stat  *stat = (struct fwd_stat*)arg;
		struct fwd_stat temp;
		if (find_userstat(stat_port - MIN_PORT, stat_id, &temp) < 0)
		{
			pr_notice("find_userstat err!  \n");
			goto error;
		}
		if ((struct fwd_stat*)arg == 0)
		{
			pr_notice("get stat,arg is null!  \n");
			goto error;
		}
		if (copy_to_user((struct fwd_stat*)arg, &temp, sizeof(struct fwd_stat)))
		{
			pr_notice("get stat,copy_to_user err!  \n");
			goto error;
		}
		//pr_notice("get stat %d, %d  \n", stat_id,temp.stream_num);
		break;
	}
	case FWD_ADD_USER:
	{
		__u16 port;
		__s32 id;
		__u32 destip;
		__u16 destport;
		__s8  strmac[24];
		__u8  mac[ETH_ALEN];

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("add user , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %d %d %hd %s", &port, &id,&destip,&destport, strmac) != 5 )
		{
			pr_notice("add user, sscanf err!  \n");
			goto error;
		}
		if (get_mac(strmac, mac) < 0)
		{
			pr_notice("add user, get mac err!  \n");
			goto error;
		}
		add_user(port - MIN_PORT, id, destip,destport,mac);
		pr_notice("add user  %d  %d %s %d, %02x:%02x:%02x:%02x:%02x:%02x\n", port, id, __inet_ntoa(destip),destport,
			mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
		break;
	}
	case FWD_DEL_USER:
	{
		__u16 port;
		__s32 id;
		__u32 ip;
		
		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("del user , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %d %d", &port, &id, &ip) != 3)
		{
			pr_notice("del user, sscanf err!  \n");
			goto error;
		}
		del_user(port - MIN_PORT, id, ip);
		pr_notice("del user  %d  %d  %s\n", port, id, __inet_ntoa(ip));
		break;
	}
	case FWD_SET_FEC_SIZE:
	{
		__u16 port;
		__u16 size;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("set_fec_size , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %hd", &port, &size) != 2)
		{
			pr_notice("set_fec_size , sscanf err!  \n");
			goto error;
		}
		set_fec_size(port - MIN_PORT, size);
		pr_notice("set_fec_size  %d  %d\n", port, size);		
		break;
	}
	case FWD_SET_FEC_SRCNUM:
	{
		__u16 port;
		__u16 num;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("set_fec_srcnum , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %hd", &port, &num) != 2)
		{
			pr_notice("set_fec_srcnum , sscanf err!  \n");
			goto error;
		}
		set_fec_srcnum(port - MIN_PORT, num);
		pr_notice("set_fec_srcnum  %d  %d\n", port, num);
		break;
	}
	case FWD_SET_FEC_RPRNUM:
	{
		__u16 port;
		__u16 num;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("set_fec_repairnum , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %hd", &port, &num) != 2)
		{
			pr_notice("set_fec_repairnum , sscanf err!  \n");
			goto error;
		}
		set_fec_repairnum(port - MIN_PORT, num);
		pr_notice("set_fec_repairnum  %d  %d\n", port, num);
		break;
	}
	case FWD_SET_STOPLIST:
	{
		__u16 port;
		__s32 id;
		__s16 stoplist[MAX_VIP];
		
		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("set stop list , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd %d  %hd %hd %hd %hd %hd %hd %hd %hd", &port,&id,
			&stoplist[0], &stoplist[1], &stoplist[2], &stoplist[3], &stoplist[4], &stoplist[5], &stoplist[6], &stoplist[7] ) != 10 )
		{
			pr_notice("set send list , sscanf err!  \n");
			goto error;
		}
		set_stoplist(port - MIN_PORT, id, stoplist);
		pr_notice("set stop list %d  %d  %d  %d %d %d %d  %d %d %d\n", port,id,
			stoplist[0], stoplist[1], stoplist[2], stoplist[3], stoplist[4], stoplist[5], stoplist[6], stoplist[7]);
		break;
	}
	case FWD_OPEN_GROUP_MODE:
	{
		__u16 port;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("open group mode , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd", &port) != 1)
		{
			pr_notice("open group mode , sscanf err!  \n");
			goto error;
		}
		pr_notice("open group mode %d\n", port);
		group_mode[port - MIN_PORT] = 1;
		break;
	}    
	case FWD_CLOSE_GROUP_MODE:
	{
		__u16 port;

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("close group mode , copy_from_user err!  \n");
			goto error;
		}
		if (sscanf(buff, "%hd", &port) != 1)
		{
			pr_notice("close group mode , sscanf err!  \n");
			goto error;
		}
		pr_notice("close group mode %d\n", port);
		group_mode[port - MIN_PORT] = 0;
		break;
	}
	case FWD_SET_GROUP_MODE_SENDLIST:
	{
		__u16 port;
		__s32 id;
		__s16 sendlist[MAX_VIP];

		if (copy_from_user(buff, (char *)arg, MAX_INPUT_LEN))
		{
			pr_notice("set group mode sendid , copy_from_user err!  \n");
			goto error;
		}

		if (sscanf(buff, "%hd %d  %hd %hd %hd %hd %hd %hd %hd %hd", &port, &id,
			&sendlist[0], &sendlist[1], &sendlist[2], &sendlist[3], &sendlist[4], &sendlist[5], &sendlist[6], &sendlist[7]) != 10)
		{
			pr_notice("set send list , sscanf err!  \n");
			goto error;
		}
		set_groupmode_sendlist(port - MIN_PORT, id, sendlist);
		pr_notice("set group mode send list %d  %d  %d  %d %d %d %d  %d %d %d\n", port, id,
			sendlist[0], sendlist[1], sendlist[2], sendlist[3], sendlist[4], sendlist[5], sendlist[6], sendlist[7]);
		break;
	}

	default:
		goto error;
	}

	mutex_unlock(&ioctl_mutex);
	return 0;

error:
	mutex_unlock(&ioctl_mutex);
	return -EINVAL;
}


/* Called whenever open() is called on the device file */
static int fwd_open(struct inode *inode, struct file *file)
{
	if (ctrl_in_use) {
		return -EBUSY;
	}
	else {
		ctrl_in_use++;
		return 0;
	}
	return 0;
}

/* Called whenever close() is called on the device file */
static int fwd_release(struct inode *inode, struct file *file)
{
	ctrl_in_use ^= ctrl_in_use;
	return 0;
}

/*
* This is the interface device's file_operations structure
*/
static struct file_operations  fwd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = fwd_ioctl,
	.open = fwd_open,
	.release = fwd_release,
};

int init_setup()
{	
	int result, err;

	dev_t devno, devno_m;

	/* Register the control device, /dev/fwd */

	result = alloc_chrdev_region(&devno, 0, 1, FWD_NAME);
	if (result < 0)
	{
		pr_err("register control error\s");
		return -1;
	}

	major = MAJOR(devno);
	devno_m = MKDEV(major, 0);

	d_major = MAJOR(devno_m);
	d_minor = MINOR(devno_m);

	pr_notice("major  %d\n", MAJOR(devno_m));
	pr_notice("minor  %d\n", MINOR(devno_m));
	cdev_init(&cdev_m, &fwd_fops);

	cdev_m.owner = THIS_MODULE;
	cdev_m.ops = &fwd_fops;
	err = cdev_add(&cdev_m, devno_m, 1);
	if (err != 0)
	{
		pr_err("cdev_add error\n");
		return -1;
	}
	//2.6.29之后可以使用以下创建设备/dev/FWD
	//create_dev(MAJOR(devno_m), MINOR(devno_m));
	/* create your own class under /sysfs */
	my_class = class_create(THIS_MODULE, FWD_NAME);
	if (IS_ERR(my_class)) {
		pr_err("Err: failed in creating class./n");
		return -1;
	}
	/* register your own device in sysfs, and this will cause udev to create corresponding device node */
	device_create(my_class, NULL, MKDEV(d_major, d_minor), NULL,FWD_NAME);



	/* Make sure the usage marker for the control device is cleared */
	ctrl_in_use ^= ctrl_in_use;
	pr_notice("\nFWD: Control device successfully registered.\n");
	active = MOD_STOP;
	ext_self_ip = 0;
	int_self_ip = 0;

	if (read_param() < 0)
	{
		pr_err("read param error\n");
		return -1;
	}

	buff = (__s8*)kzalloc(sizeof(__s8)*MAX_INPUT_LEN, GFP_ATOMIC);
	if (!buff)
	{
		pr_notice("%s\n", "allocate buff memory error!");
		return -1;
	}
	return 0;
}

void exit_setup()
{
	/* Now unregister control device */
	cdev_del(&cdev_m);
	unregister_chrdev_region(MKDEV(major, 0), 1);
	device_destroy(my_class, MKDEV(major, 0));         //delete device node under /dev  
	class_destroy(my_class);

}