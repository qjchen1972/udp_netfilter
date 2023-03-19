#include <stdio.h>
#include <stdlib.h> 
//#include <unistd.h> 
//#include "basic.h"
#define LED_DEVICE  "/dev/FWD" 

int main(int argc, char **argv)
{
	int fd;
	char buf[64];
	char mac[] = {0x00,0x50,0x56,0xf0,0xea,0xc9};

	fd = open(LED_DEVICE, 0);

	if (fd < 0)
	{
		printf("can't open/dev/FWD!\n");
		return 0;
	}
	//ioctl(fd, FWD_SET_ACTIVATE);
	
	sprintf(buf, "%ud", inet_addr("192.168.75.130")); //192.168.75.130
	//ioctl(fd, FWD_ADD_VIP,"10020 32");
	ioctl(fd, FWD_SET_EXTIP, buf);
	sprintf(buf, "%ud", inet_addr("255.255.255.0"));
	ioctl(fd, FWD_SET_EXTMASK, buf);
	//sprintf(buf, "%c %c %c %c %c %c", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	//ioctl(fd, FWD_SET_GATEMAC, buf);
	ioctl(fd, FWD_SET_EXTDEVNAME, "eth0");
	ioctl(fd, FWD_SET_ACTIVATE);

	return 0;
	
}