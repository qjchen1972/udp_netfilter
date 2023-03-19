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
#include <linux/moduleparam.h>

#include "udp_forward.h"
#include "vip.h"
#include "setup.h"
#include "proc.h"
#include "streambuf.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chenqqianjiang");
MODULE_DESCRIPTION("udp forward in network interfaces");

__s8 config[MAX_PARAM_LENGTH];
module_param_string(net, config, MAX_PARAM_LENGTH, 0);
MODULE_PARM_DESC(net, "net=[int-ip]:[int-mask]:[int-net-name]:[ext-ip]:[ext-mask]:[ext-net-name]:[gatemac]:[maxuser]");


static struct nf_hook_ops nfho;


static unsigned int udp_hook(void* priv, struct sk_buff* __skb, const struct nf_hook_state* state)
/*static unsigned udp_hook(int hooknum, struct sk_buff* __skb,const struct net_device *in,
                         const struct net_device *out,int(*okfn)(struct sk_buff *))*/

{

    struct iphdr *iph;
    struct udphdr *udph;
	struct ethhdr *eth;
	__u16 port;

	if(active == MOD_STOP) return NF_ACCEPT;
	else if(active == MOD_RESTART)
	{
		restart_proc();
		restart_vip();
	}
	if (!__skb) return NF_ACCEPT;
    iph = ip_hdr(__skb);
	if(!iph) return NF_ACCEPT;
	skb_set_transport_header(__skb, sizeof(struct iphdr));
    udph = (struct udphdr *)udp_hdr(__skb);/*(__skb->data + (ip_hdr(__skb)->ihl * 4));*/
	if(!udph) return NF_ACCEPT;
	eth = (struct ethhdr *)skb_mac_header(__skb);
	if(!eth) return NF_ACCEPT;

	/*if (isinLan(iph->saddr,ext_self_ip,ext_mask))
		printk("packet from extern network %s:%d\n", __inet_ntoa(iph->saddr), htons(udph->source));
	else
		printk("packet from inter network %s:%d \n", __inet_ntoa(iph->saddr), htons(udph->source));
    */
	//pr_notice("%s %s  %d \n", "packet's destip is ", __inet_ntoa(iph->daddr), port);

	if(iph->protocol == IPPROTO_UDP)
	{	
		if (ext_self_ip == iph->daddr || int_self_ip == iph->daddr)
		{
			port = ntohs(udph->dest);
			if (port == TEST_NETWORK_PORT)
			{
				proc_test(__skb, udph, iph, eth);
				return NF_DROP;
			}
			else if (port >= MIN_PORT && port < MAX_PORT)
			{
				//__u16 len = ntohs(udph->len);
				//__s8 *temp = (__s8*)udph + sizeof(struct udphdr);
				__s32 key = *((__s32*)((__s8*)udph + sizeof(struct udphdr)));
				//pr_notice("%s   %d \n", "packet's destip is ", key);
				if (key == 0x0000ffff /*len < MAX_HEART_LEN*/)
				{
					proc_heart(__skb, udph, iph, eth);
				}
				else
				{					
					proc_forward(__skb, udph, iph);
				}
				return NF_DROP;
			}
		}
    }
    return NF_ACCEPT;
}

 

static int __init filter_init(void)
{
	int ret;

	ret = init_setup();
	if (ret < 0)
	{
		pr_err("%s\n", "can't init io device!");
		return ret;
	}
	/* Now register the network hooks */
    nfho.hook = udp_hook;
    nfho.pf = AF_INET;
	nfho.hooknum = NF_INET_PRE_ROUTING;
    nfho.priority = NF_IP_PRI_FIRST;
    
    ret = nf_register_hook(&nfho);
    if(ret < 0)
	{
		pr_err("%s\n", "can't modify skb hook!");
        return ret;
    }
	pr_notice("%s\n", "init kernel module");

    if(init_proc() < 0 ) return -1;
	if(init_vip() < 0 ) return -1;
	if (init_streambuf() < 0) return -1;

	//test_fwd();

    return 0;
}

static void filter_fini(void) {
    nf_unregister_hook(&nfho);    
	exit_setup();
	exit_vip();
	exit_proc();
	exit_streambuf();
}

//module_init(filter_init);
late_initcall(filter_init);
module_exit(filter_fini);
