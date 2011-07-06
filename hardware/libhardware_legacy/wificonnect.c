/* //device/system/toolbox/resetradio.c
 **
 ** Copyright 2006, The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "string.h"
#include "hardware_legacy/wifi.h"
#include "cutils/log.h"
#include "cutils/properties.h"
#include <arpa/inet.h>

static int doCommand(const char *cmd, char *replybuf, int replybuflen)
{
	size_t reply_len = replybuflen - 1;

	if (wifi_command(cmd, replybuf, &reply_len) != 0)
		return -1;
	else {
		// Strip off trailing newline
		if (reply_len > 0 && replybuf[reply_len-1] == '\n')
			replybuf[reply_len-1] = '\0';
		else
			replybuf[reply_len] = '\0';
		return 0;
	}
}

static int doIntCommand(const char *cmd)
{
	char reply[256];

	if (doCommand(cmd, reply, sizeof(reply)) != 0) {
		return -1;
	} else {
		return atoi(reply);
	}
}

static int doBooleanCommand(const char *cmd, const char *expect)
{
	char reply[256];

	if (doCommand(cmd, reply, sizeof(reply)) != 0) {
		return 0;
	} else {
		return (strcmp(reply, expect) == 0);
	}
}

// Send a command to the supplicant, and return the reply as a String
static char* doStringCommand(const char *cmd)
{
	char reply[4096];

	if (doCommand(cmd, reply, sizeof(reply)) != 0) {
		return NULL;
	} else {
		return reply;
	}
}

int init_stage() {
	// load the wifi driver: insmod .ko
	int ret = wifi_load_driver();
	if(ret < 0) {
		perror("Failed to load Wi-Fi driver.");
		return -1;
	}

	// start wpa_supplicant
	ret =  wifi_start_supplicant();
	if(ret < 0) {
		perror("Failed to start supplicant daemon.");
		return -1;
	}

	ret = wifi_connect_to_supplicant();
	if(ret < 0) {
		perror("Failed to connect supplicant daemon.");
		return -1;
	}

	char ifname[256];
	property_get("wifi.interface", ifname ,"eth0");
	ret = ifc_enable(ifname);
	if(ret < 0) {
		perror("Failed to enable wifi interface eth0.");
		return -1;
	}

	return 0;
}

int scan_stage(){
	// XXX we don't need to really scan the wifi 
	return 0;
}

#define SSID_NAME "ssid"
#define BSSID_NAME "bssid"
#define KEY_MGMT "key_mgmt"
#define SSID "\"TServer\""
//#define BSSID "00:24:b2:c1:16:b6"
#define PASS "NONE"
//#define PSK_NAME "psk"
//#define PSK "\"pacman@chuangye606\""

int config_stage(){	
	// Add a network config to supplicant mode
	int networkId = doIntCommand("ADD_NETWORK"); // Add a new network id
	if(networkId < 0) {
		perror("Failed to add a network configuration.");
		return -1;
	}

	perror("Add a network");

	// set the ssid of the destination wifi adhoc
	char cmdstr[256];
	snprintf(cmdstr, sizeof(cmdstr), "SET_NETWORK %d %s %s",networkId, SSID_NAME, SSID);
	if(!doBooleanCommand(cmdstr,"OK")) {
		perror("Failed to set network configuration ssid.");
		return -1;
        }

        /*
        snprintf(cmdstr, sizeof(cmdstr), "SET_NETWORK %d %s %s", networkId, PSK_NAME, PSK);
	if(!doBooleanCommand(cmdstr,"OK")) {
		perror("Failed to set network configuration psk.");
		return -1;
	}
        */

	snprintf(cmdstr, sizeof(cmdstr), "SET_NETWORK %d %s %s", networkId, KEY_MGMT ,PASS);
	if(!doBooleanCommand(cmdstr,"OK")) {
		perror("Failed to set network  configuration key_mgmr.");
		return -1;
	}


	return networkId;
}

#define EVENT_PREFIX "CTRL-EVENT-"
#define CONNECTED "CONNECTED"
int connect_stage(int networkId) {
	char cmdstr[256];
	// enable the network
	snprintf(cmdstr, sizeof(cmdstr), "SELECT_NETWORK %d",networkId);
	if(!doBooleanCommand(cmdstr,"OK")) {
		perror("Failed to select network.");
		return -1;
	}

	// wait for connect
	char buf[256];
	while(1) {
		int nread = wifi_wait_for_event(buf, sizeof(buf));
		if(nread > 0) {
			if(strstr(buf,CONNECTED) > 0) {
				break;
			}
			// XXX danger of not going out of the loop!!!
		}
		continue;
	}
	return 0;
}

int dhcp_stage(){
	int result;
	in_addr_t ipaddr, gateway, mask, dns1, dns2, server;
	uint32_t lease;

	char ifname[64];
	char mDns1Name[64];
	char mDns2Name[64];
	char mIpAddress[64];
	char mGateway[64];
	char mMask[64];
	char mServer[64];
	property_get("wifi.interface", ifname ,"eth0");
	snprintf(mDns1Name, sizeof(mDns1Name), "dhcp.%s.dns1",ifname);
	snprintf(mDns2Name, sizeof(mDns2Name), "dhcp.%s.dns2",ifname);
	snprintf(mIpAddress, sizeof(mIpAddress), "dhcp.%s.ipaddress",ifname);
	snprintf(mGateway, sizeof(mGateway), "dhcp.%s.gateway",ifname);
	snprintf(mMask, sizeof(mMask), "dhcp.%s.mask",ifname);
	snprintf(mServer, sizeof(mServer), "dhcp.%s.server",ifname);

	result = dhcp_do_request(ifname, &ipaddr, &gateway, &mask, &dns1, &dns2, &server, &lease);
	if(result != 0) {
		perror("Failed to dhcp on interface eth0.");
		return -1;
	}
	struct in_addr dns_struct1, dns_struct2,dns_struct3, dns_struct4,dns_struct5, dns_struct6;
	dns_struct1.s_addr = dns1;
	dns_struct2.s_addr = dns2;
	dns_struct3.s_addr = ipaddr;
	dns_struct4.s_addr = gateway;
	dns_struct5.s_addr = mask;
	dns_struct6.s_addr = server;
	property_set(mDns1Name,inet_ntoa(dns_struct1));
	property_set(mDns2Name,inet_ntoa(dns_struct2));
	property_set(mIpAddress,inet_ntoa(dns_struct3));
	property_set(mGateway,inet_ntoa(dns_struct4));
	property_set(mMask,inet_ntoa(dns_struct5));
	property_set(mServer,inet_ntoa(dns_struct6));

	/*
	[dhcp.eth0.dns1]: [192.168.1.1]
	[dhcp.eth0.dns2]: []
	[dhcp.eth0.dns3]: []
	[dhcp.eth0.dns4]: []
	[dhcp.eth0.ipaddress]: [192.168.1.4]
	[dhcp.eth0.gateway]: [192.168.1.1]
	[dhcp.eth0.mask]: [255.255.255.0]
	[dhcp.eth0.leasetime]: [86400]
	[dhcp.eth0.server]: [192.168.1.1]
	*/
	return 0;
}

int main(int argc, char *argv[])
{
	perror("Enter wificonnect");
	int ret = init_stage();
	if(ret < 0) {
		perror("Failed init stage.");
		exit(-1);
	}

	perror("Finished init stage.");

	ret = config_stage();
	if(ret < 0) {
		perror("Failed config stage.");
		exit(-1);
	}
	perror("Finished config stage.");

	ret = connect_stage(ret);
	if(ret < 0) {
		perror("Failed connect stage.");
		exit(-1);
	}
	perror("Finished connect stage.");

	ret = dhcp_stage();
	if(ret < 0) {
		perror("Failed dhcp stage.");
		exit(-1);
	}
	perror("Finished dhcp stage.");
	return 0;
}
