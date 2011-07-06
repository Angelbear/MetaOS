/******************************************************************************
 * *(C) Copyright 2008 Marvell International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include "ioctl.h"
#include "marvell-ril.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cutils/properties.h>
#define LOG_TAG "RIL"
#include <utils/Log.h>

#if PLATFORM_SDK_VERSION >= 8
#define PPP0_PID_FILE   "/mnt/asec/ppp0.pid"
#else
#define PPP0_PID_FILE    "/sqlite_stmt_journals/ppp0.pid"
#endif


int ifc_init(void);
int ifc_close(void);
int ifc_set_addr(const char *name, unsigned addr);
int ifc_create_default_route(const char *name, unsigned addr);
int ifc_remove_default_route(const char *name);
int ifc_up(const char *name);
int ifc_down(const char *name);
int ifc_get_info(const char *name, unsigned *addr, unsigned *mask, unsigned *flags);

int cinetdevfd = 0;
int cidatadevfd = 0;
#define CCINET_MAJOR 241
#define CCINET_IOC_MAGIC CCINET_MAJOR
#define CCINET_IP_ENABLE  _IOW(CCINET_IOC_MAGIC, 1, int)
#define CCINET_IP_DISABLE  _IOW(CCINET_IOC_MAGIC, 2, int)
#define TIOPPPON _IOW('T', 208, int)
#define TIOPPPOFF _IOW('T', 209, int)

#ifdef DKB_CP
#define LOCK_FILE_PATH "/var/lock/LCK..cidatatty1"
#define DEVICE_FILE_PATH "/dev/cidatatty1"
#elif defined BROWNSTONE_CP
#define LOCK_FILE_PATH "/var/lock/LCK..ttyACM0"
#define DEVICE_FILE_PATH "/dev/ttyACM0"
#endif

extern void setDNS(char* dns)
{
	unsigned char *buf, *packetEnd, *ipcpEnd;

	buf = alloca(strlen(dns) / 2 + 1);
	int len = strlen(dns);
	int i = 0;
	unsigned int tmp;
	char tmpbuf[3];
	char primaryDNS[20] = { '\0' };
	char secondaryDNS[20] = { '\0' };

	while (i < len)
	{
		memcpy(tmpbuf, &dns[i], 2);
		tmpbuf[2] = '\0';
		sscanf(tmpbuf, "%x", &tmp);
		buf[i / 2] = tmp;
		i += 2;
	}
	buf[len / 2] = '\0';
	packetEnd = buf + strlen((char*)buf);
	LOGV("dns length:%d", strlen((char*)buf));
	while (buf < packetEnd)
	{

		unsigned short type = buf[0] << 8 | buf[1];
		unsigned char len = buf[2]; //  this length field includes only what follows it
		switch (type)
		{
		// we are currently interested only on one type
		// but will specify some more for fute
		case 0x23C0:
			// PAP - not used  - ignore
			buf += len + 3;
			break;

		case 0x8021:
			// IPCP option for IP configuration - we are looking for DNS parameters.
			// it seem that this option may appear more than once!
			LOGD("parse DNS");
			ipcpEnd = buf + len + 3;
			buf += 3;
			// Ido : I guess that this must be a Nak because of the conf-request structure ???
			if (*buf == 0x03)
			{
				LOGV("parse DNS 1:buf:%d %d %d %d", buf[1], buf[2], buf[3], buf[4]);
				// Config-Nak found, advance to IPCP data start
				buf += 4;
				// parse any configuration
				while (buf < ipcpEnd)
				{
					LOGV("parse DNS 11:buf:%d, buf1:%d", *buf, buf[1]);
					if (*buf == 129)
					{
						// Primary DNS address
						buf += 2;
						LOGI("Primary DNS %d.%d.%d.%d\n", buf[0], buf[1], buf[2], buf[3]);
						sprintf(primaryDNS, "%d.%d.%d.%d", buf[0], buf[1], buf[2], buf[3]);
						buf += 4;
						continue;
					}
					if (*buf == 131)
					{
						// Secondary DNS address
						buf += 2;
						LOGI("secondary DNS %d.%d.%d.%d\r\n", buf[0], buf[1], buf[2], buf[3]);
						sprintf(secondaryDNS, "%d.%d.%d.%d", buf[0], buf[1], buf[2], buf[3]);
						//pNode->directIpAddress.ipv4.inSecondaryDNS=((buf[0]<<24) | (buf[1]<<16) | (buf[2]<<8) | buf[3]);
						buf += 4;
						continue;
					}
					// buf[0] includes the ipcp type, buf[1] the length of this field includes the whole TLV
					buf += buf[1];
				}
			}
			else
			{
				LOGW("parse DNS 2");
				buf += len;
			}
			break;

		default:
			LOGW("parse default");
			buf += len + 3;
			break;
		}
	}

	if (primaryDNS[0] != '\0')
	{
		property_set("net.ccinet0.dns1", primaryDNS);
	}
	if (secondaryDNS[0] != '\0')
	{
		property_set("net.ccinet0.dns2", secondaryDNS);
	}
	//LOGI("---cid:%d,type:%s\n,apn:%s\n,address:%s\n",cid,type,apn,address);
}

int configureInterface(char* address)
{
	char gw[20];
	int ret;
	unsigned addr, netmask, gateway, dns1, dns2;
	int tmp1, tmp2, tmp3, tmp4;

	sscanf(address, "%d.%d.%d.%d", &tmp1, &tmp2, &tmp3, &tmp4);
	sprintf(gw, "%d.%d.%d.%d", tmp1, tmp2, tmp3, tmp4 ^ 0xFF);
	LOGI("gw:%s", gw);
	property_set("net.ccinet0.gw", gw);

	sscanf(address, "%d.%d.%d.%d", &tmp1, &tmp2, &tmp3, &tmp4);
	addr = tmp1 | (tmp2 << 8) | (tmp3 << 16) | (tmp4 << 24);
	sscanf(gw, "%d.%d.%d.%d", &tmp1, &tmp2, &tmp3, &tmp4);
	gateway = tmp1 | (tmp2 << 8) | (tmp3 << 16) | (tmp4 << 24);

	ifc_init();
	if (ifc_up("ccinet0"))
	{
		LOGW("failed to turn on interface");
		ifc_close();
		return -1;
	}
	if (ifc_set_addr("ccinet0", addr))
	{
		LOGW("failed to set ipaddr");
		ifc_close();
		return -1;
	}

	if (ifc_create_default_route("ccinet0", gateway))
	{
		LOGW("failed to set default route");
		ifc_close();
		return -1;
	}
	ifc_close();
	return 0;
}

void deconfigureInterface()
{

	property_set("net.ccinet0.gw", "");
	ifc_init();

	if (ifc_remove_default_route("ccinet0"))
	{
		LOGW("failed to remove default route");
		ifc_close();
		return;
	}

	if (ifc_down("ccinet0"))
	{
		LOGW("failed to turn off interface");
		ifc_close();
		return;
	}
	ifc_close();

}

void enableInterface(int cid)
{
	if (cid < 1)
	{
		LOGE("%s: invalid cid:%d\n", __FUNCTION__, cid);
		return;
	}
	if (cinetdevfd <= 0)
	{
		LOGD("open ccichar");
		cinetdevfd = open("/dev/ccichar", O_RDWR);
	}

	if (cinetdevfd <= 0)
		LOGW("open ccichar fail");
	else
		LOGI("open ccichar success");
	ioctl(cinetdevfd, CCINET_IP_ENABLE, cid - 1);
}

void disableInterface(int cid)
{
	if (cinetdevfd <= 0)
		return;
	LOGD("disable ccinet0");

	deconfigureInterface();

	ioctl(cinetdevfd, CCINET_IP_DISABLE, cid - 1);
	close(cinetdevfd);

	cinetdevfd = -1;
}

int enablePPPInterface(int cid, const char* user, const char* passwd, char* ipaddress)
{
	char shell[256];
	int ret = -1;

	if (cid < 1)
	{
		LOGE("%s: invalid cid:%d\n", __FUNCTION__, cid);
		return -1;
	}
#ifdef DKB_CP
	if (cidatadevfd <= 0)
	{
		LOGD("open /dev/cctdatadev1");
		cidatadevfd = open("/dev/cctdatadev1", O_RDWR);
	}

	if (cidatadevfd <= 0)
	{
		LOGE("%s: Error open /dev/cctdatadev1!\n", __FUNCTION__);
		return -1;
	}
	else
		LOGI("open /dev/cctdatadev1 success");
	ioctl(cidatadevfd, TIOPPPON, cid - 1);
#endif

	// Kill PPPD if it exists
	FILE* fp = fopen(PPP0_PID_FILE, "r");
	if ( fp )
	{
		int pid = -1;
		fscanf(fp, "%d", &pid);
		if ( pid != -1 )
		{
			char cmd[256];
			sprintf(cmd, "kill %d", pid);
			ret = system(cmd);
			LOGD("exec cmd: %s and ret is: %d!", cmd, ret);
			//Wait pppd exit
			if ( ret == 0 ) sleep(2);
		}
		fclose(fp);
	}
	unlink(LOCK_FILE_PATH);

	sprintf(shell, "pppd unit 0 %s 115200 -detach modem lock defaultroute usepeerdns user %s password %s &",
		DEVICE_FILE_PATH, user[0] ? user : "any", passwd[0] ? passwd : "any");
	ret = system(shell);
	LOGD("Launch: %s and ret is: %d!\n", shell, ret);
	if ( ret == -1)
	{
		LOGE("Launch: %s failed!\n", shell);
		return -1;
	}

	//Wait to get the IP address
	int count = 20;
	unsigned myaddr = 0;
	ret = -1;
	ifc_init();
	while ( count > 0 )
	{
		sleep(1);
		ifc_get_info("ppp0", &myaddr, NULL, NULL);
		if ( myaddr )
		{
			ret = 0;
			sprintf(ipaddress, "%d.%d.%d.%d", myaddr & 0xFF, (myaddr >> 8) & 0xFF, (myaddr >> 16) & 0xFF, (myaddr >> 24) & 0xFF);
			LOGD("ppp0 IP address is: %s!\n", ipaddress);
			break;
		}
		--count;
	}
	ifc_close();

	return ret;
}

void disablePPPInterface(int cid)
{
#ifdef DKB_CP
	if (cidatadevfd <= 0)
		return;

	LOGD("%s: close /dev/cctdatadev1!\n", __FUNCTION__);
	ioctl(cidatadevfd, TIOPPPOFF, cid - 1);
	close(cidatadevfd);
#elif defined BROWNSTONE_CP
	if (descriptions[CHANNEL_DAT].fd <= 0)
		return;

	LOGD("%s: close /dev/ttyACM0\n", __FUNCTION__);
	close(descriptions[CHANNEL_DAT].fd);
#endif

	// Kill PPPD if it exists
	FILE* fp = fopen(PPP0_PID_FILE, "r");
	if ( fp )
	{
		int ret;
		int pid = -1;
		fscanf(fp, "%d", &pid);
		if ( pid != -1 )
		{
			char cmd[256];
			sprintf(cmd, "kill %d", pid);
			ret = system(cmd);
			LOGD("exec cmd: %s and ret is: %d!", cmd, ret);
		}
		fclose(fp);
	}
	sleep(3); //Wait pppd exit
#ifdef DKB_CP
	cidatadevfd = -1;
#elif defined BROWNSTONE_CP
	descriptions[CHANNEL_DAT].fd = -1;
#endif
}

int getInterfaceAddr(int cid, const char* ifname, char* ipaddress)
{
	int ret = -1;
	unsigned myaddr = 0;

	ifc_init();
	ifc_get_info(ifname, &myaddr, NULL, NULL);
	if ( myaddr )
	{
		ret = 0;
		sprintf(ipaddress, "%d.%d.%d.%d", myaddr & 0xFF, (myaddr >> 8) & 0xFF, (myaddr >> 16) & 0xFF, (myaddr >> 24) & 0xFF);
		LOGD("ppp0 IP address is: %s!\n", ipaddress);
	}
	ifc_close();
	return ret;
}
