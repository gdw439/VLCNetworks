#include<string.h>
#include<stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h> 
#include <sys/mman.h>
#include <time.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <sys/ioctl.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>

int reg_fd;
int ddr_fd;
int wifi_id;
int client_fd;

unsigned short UEID = 0x00FF;
unsigned char  UE_MAC[6];
unsigned char *start;
struct sockaddr_in addr = {};

#define IP_ADDR "192.168.1.108"
#define VLC_REGR_TYPE      0x00FF
#define VLC_DATA_TYPE      0xFF00

#define REQ_FRAME_TYPE      0x0907
#define ACK_FRAME_TYPE      0x0909
#define FEEDBACK_TYPE       0x090B


// some bias of address for test
#define MAC_ADDR_BIAS       40
#define DATA_BIAS           40


// video show setting
#define OUT_PORT 5588
#define BUFF_LEN 1302
struct sockaddr_in video_out_addr;
#define OUT_IP "192.168.2.100"

typedef struct sockaddr* saddrp;

typedef struct {
	unsigned short type;
	unsigned short length;
	unsigned short ueid;
	unsigned short LEDID;
	unsigned short RSS;
	unsigned char  UEMAC[6];
} BCFrame;

BCFrame boardcast_info;

// initial wifi modules
int init_wifi(void){

	wifi_id = socket(AF_INET,SOCK_DGRAM,0);
    if (0 > wifi_id){
        perror("socket");
        return -1;
    }

	// get wifi mac address
	char buf[2048];
	struct ifreq ifr;
	struct ifconf ifc;
	ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(wifi_id, SIOCGIFCONF, &ifc) == -1) {
        printf("ioctl error\n");
        return -1;
    }
	struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
    for (; it != end; ++it) {
        strcpy(ifr.ifr_name, it->ifr_name);
        if(ifr.ifr_name[0] == 'w' && ifr.ifr_name[1] == 'l' && ifr.ifr_name[2] == 'x'){
            if(ioctl(wifi_id, SIOCGIFFLAGS, &ifr) == 0) {
                if (ioctl(wifi_id, SIOCGIFHWADDR, &ifr) == 0) {
                    unsigned char * ptr ;
                    ptr = (unsigned char  *)&ifr.ifr_ifru.ifru_hwaddr.sa_data[0];
					for(char i=0;i<6;i++)
						UE_MAC[i] = ptr[i];
                    printf("Interface name : %s , Mac address : %2X:%2X:%2X:%2X:%2X:%2X \n",ifr.ifr_name,UE_MAC[0],UE_MAC[1],UE_MAC[2],UE_MAC[3],UE_MAC[4],UE_MAC[5]);
                }
            }
        }
    }

	addr.sin_family = AF_INET;
	addr.sin_port = htons(5577);
	addr.sin_addr.s_addr = inet_addr(IP_ADDR);

	socklen_t addr_len = sizeof(struct sockaddr_in);
}

void feedback(unsigned short feedback_type){
    unsigned int val[4];
	// read recvice broadcast data
    read(reg_fd, &val, 16);
	if(UEID==0x00FF){
		boardcast_info.length = 16;
		boardcast_info.type = REQ_FRAME_TYPE;
	} else {
		boardcast_info.length = 10;
		boardcast_info.type = feedback_type;
	}

	boardcast_info.ueid      = UEID;
	boardcast_info.LEDID     = 0;
	boardcast_info.RSS       = 0;

	unsigned short cur_LEDID, cur_RSS;

	// select a more bigger power led to feedback
	for(int i=0;i<3;i++){
		cur_RSS   = (val[i] & 0xFFFF0000) >> 16;
		cur_LEDID = (val[i] & 0x0000FFFF) >> 8;
		if(cur_RSS > boardcast_info.RSS){
			if(cur_LEDID<1 || cur_LEDID > 12){
				// printf("bad ledid\n");
				continue;
			}
			boardcast_info.RSS   = cur_RSS;
			boardcast_info.LEDID = cur_LEDID;
		}
		// printf("LED: %d : POWER: %d |\t", cur_LEDID, cur_RSS);
	}
	// printf("\n");
	if(UEID==0x00FF){
		memcpy(&boardcast_info.UEMAC, &UE_MAC, sizeof(UE_MAC));
		sendto(wifi_id, &boardcast_info, sizeof(boardcast_info), 0, (saddrp)&addr, sizeof(addr));
	} else {
		sendto(wifi_id, &boardcast_info, sizeof(boardcast_info) - 6, 0, (saddrp)&addr, sizeof(addr));
	}
} 

void frame_deal(void){
	unsigned short frame_type, frame_lens, led_id, ue_id;

	// unsigned char restype, cnt;
	// restype = 0;
	for(char i=0; i<8; i++){
		frame_type = (unsigned short)start[0 + i*1302] << 8 | start[1 + i*1302];
		frame_lens = (unsigned short)start[2 + i*1302] << 8 | start[3 + i*1302];

		led_id = (unsigned short)start[20 + i*1302] << 8 | start[21 + i*1302];
		ue_id  = (unsigned short)start[22 + i*1302] << 8 | start[23 + i*1302];

		// for(int i=0;i<40;i++)
		// 	printf("data %d: %x\n", i, start[i]);

		// Registration protocol frame
		if(frame_type==VLC_REGR_TYPE){
			printf("Rx:  TYPE: %4x | LENS: %4x | LEDID: %4x | UEID: %4x\n", frame_type, frame_lens, led_id, ue_id);
			if(UE_MAC[0]==start[MAC_ADDR_BIAS] && UE_MAC[1]==start[MAC_ADDR_BIAS+1] && UE_MAC[2]==start[MAC_ADDR_BIAS+2] && UE_MAC[3]==start[MAC_ADDR_BIAS+3] && UE_MAC[4]==start[MAC_ADDR_BIAS+4] && UE_MAC[5]==start[MAC_ADDR_BIAS+5]){
				UEID = ue_id;
				printf("register success!     current ue_id: %4x\n", ue_id);
			} else {
				printf("discard register frame: suitable mac address\n");
			}
		// Data protocol frame
		}else if(frame_type==VLC_DATA_TYPE && ue_id==UEID){
			sendto(client_fd, &start[DATA_BIAS + i*1302], frame_lens, 0, (struct sockaddr *) &video_out_addr, sizeof(video_out_addr));
			// printf("receive the data frame: %d bytes.\n", frame_lens);
		}
	}
}

// to deal with the fpga interrupt
void my_signal_fun(int signum) {
	unsigned char key_val;
	read(reg_fd, &key_val, 1);
	int i;
	switch(key_val){
		case 0 :  frame_deal(); break; 
		case 1 :  feedback(FEEDBACK_TYPE); break; 
		default:  break;
	}
	// printf("key_val: 0x%x\n", key_val);
}

int main(int argc, char **argv) {
	unsigned char key_val;
	int ret;
	int Oflags;

	init_wifi();

	signal(SIGIO, my_signal_fun);
	
	reg_fd = open("/dev/irq_drv", O_RDWR);
	if (reg_fd < 0){
		printf("can't open!\n");
	}

	ddr_fd = open ("/dev/mem", O_RDWR | O_SYNC);
	if (ddr_fd < 0) {
		printf("cannot open /dev/mem.");
		return -1;
	}
	start = (unsigned char *)mmap(NULL, 10496, PROT_READ | PROT_WRITE, MAP_SHARED, ddr_fd, 0x30000000);

	// parameter settings 
	// https://blog.csdn.net/h244259402/article/details/83993524
	fcntl(reg_fd, F_SETOWN, getpid());
	Oflags = fcntl(reg_fd, F_GETFL); 
	fcntl(reg_fd, F_SETFL, Oflags | FASYNC);
 
	// Forward data
	client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(client_fd < 0){
        printf("create socket fail!\n");
        return -1;
    }
	
    memset(&video_out_addr, 0, sizeof(video_out_addr));
    video_out_addr.sin_family = AF_INET;
    video_out_addr.sin_addr.s_addr = inet_addr(OUT_IP); //IP地址，需要进行网络序转换，INADDR_ANY：本地地址
    video_out_addr.sin_port = htons(OUT_PORT);          //端口号，需要网络序转换

	while (1);
}


