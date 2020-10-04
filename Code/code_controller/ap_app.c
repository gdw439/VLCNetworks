#include <stdio.h>
#include <sys/socket.h> 
#include <sys/ioctl.h> 
#include <net/ethernet.h>
#include <netinet/in.h> 
#include <string.h> 
#include <sys/ioctl.h> 
#include <net/if.h> 
#include <pthread.h>

#include<stdlib.h>
#include <fcntl.h>
#include <unistd.h> 
#include <sys/mman.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>

#include "vlctype.h"

int data_frame_test(char user, char slot);
const unsigned short LEDID1 = 7;
const unsigned short LEDID2 = 8;
const unsigned short LEDID3 = 9;

// int debug_fd;
// struct sockaddr_in debug_addr = {};
// #define IP_ADDR "192.168.1.102"

int sockfd;
int regfd;
int ddrfd;
unsigned char *ddrtool;
pthread_t threadid;
tran_queue phy_queue[TIMESLOT];

void show_mac(eth_header * ethhead){
    unsigned char mac1, mac2, mac3, mac4, mac5, mac6;
    mac1 = ethhead->source_mac[0];
    mac2 = ethhead->source_mac[1];
    mac3 = ethhead->source_mac[2];
    mac4 = ethhead->source_mac[3];
    mac5 = ethhead->source_mac[4];
    mac6 = ethhead->source_mac[5];
    printf("source: %02x:%02x:%02x:%02x:%02x:%02x\n",mac1, mac2, mac3, mac4, mac5, mac6);

    mac1 = ethhead->target_mac[0];
    mac2 = ethhead->target_mac[1];
    mac3 = ethhead->target_mac[2];
    mac4 = ethhead->target_mac[3];
    mac5 = ethhead->target_mac[4];
    mac6 = ethhead->target_mac[5];
    printf("target: %02x:%02x:%02x:%02x:%02x:%02x\n",mac1, mac2, mac3, mac4, mac5, mac6);
}

// to receive the mac frame
int frame_rec(void){
    int recv_num;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    char frame_buf[2000];
    bzero(frame_buf, sizeof(frame_buf));

    while(1){
        recv_num = recvfrom(sockfd, (char *)frame_buf, sizeof(frame_buf), 0, (struct sockaddr * )&addr, &addr_len); 
        // printf("lens: %d\n", recv_num);

        eth_header * ethhead;
        ethhead = (eth_header *)frame_buf;
        char * pdata;
        pdata = frame_buf + sizeof(eth_header);
        int plens = recv_num - sizeof(eth_header) - sizeof(network_header);

        unsigned short frametype;
        frametype = ntohs(ethhead->frame_type);
        // printf("type :%04x\n", frametype);
    
        if(ethhead->target_mac[2] != 0xFF && frametype == 0x2050){
            // show_mac(ethhead);
            network_header * rec_head;
            rec_head = (network_header *)pdata;
            mac_header tran_head;
            int head_lens = sizeof(tran_head);
            // printf("LEDID: %d\n", rec_head->led_id);

            unsigned short testledid = ntohs(rec_head->led_id);
            unsigned short lednum  = ntohs(rec_head->led_id) - LEDID1;
            unsigned short slotnum = ntohs(rec_head->timeslot) - 1;
            printf("slot : %d----lednum: %d --> %d \n", slotnum, testledid, lednum);

            unsigned short byte_high,byte_low;
            tran_head.led_id             = 0x0101 << lednum;
            tran_head.timeslot           = 0x0101 << slotnum;
            byte_high = htons(rec_head->type);
            byte_low  = htons(rec_head->length);
            tran_head.type_and_length[0] = htonl((unsigned int)byte_high << 16 | byte_low); // int type conv happen when transmit in network
            tran_head.type_and_length[1] = tran_head.type_and_length[0];
            tran_head.type_and_length[2] = tran_head.type_and_length[0];
            tran_head.type_and_length[3] = tran_head.type_and_length[0];
            tran_head.type_and_length[4] = tran_head.type_and_length[0];

            byte_high = htons(rec_head->led_id);
            byte_low  = htons(rec_head->ue_id);
            tran_head.ledid_and_ueid[0]  = htonl((unsigned int)byte_high << 16 | byte_low);
            tran_head.ledid_and_ueid[1]  = tran_head.ledid_and_ueid[0];
            tran_head.ledid_and_ueid[2]  = tran_head.ledid_and_ueid[0];
            tran_head.ledid_and_ueid[3]  = tran_head.ledid_and_ueid[0];
            tran_head.ledid_and_ueid[4]  = tran_head.ledid_and_ueid[0];

            if (plens <= 1262 && slotnum<4 && slotnum>=0 && phy_queue[slotnum].lens < SLOTDEPTH){
                char * pslot_data = (char *)(phy_queue[slotnum].data[phy_queue[slotnum].tail]);
                // pslot_data = pslot_data + phy_queue[slotnum].lens;
                
                // copy the control bytes into queue
                memcpy(pslot_data, &tran_head, head_lens);

                pdata = pdata + sizeof(network_header);

                // copy the data bytes into queue
                memcpy(pslot_data+head_lens, pdata, plens);
                // phy_queue[slotnum].lens += 1306;

                phy_queue[slotnum].tail = (phy_queue[slotnum].tail + 1) % SLOTDEPTH;

                // avoid the double thread setting at the same time
                while (phy_queue[slotnum].lock);
                phy_queue[slotnum].lock = 1;
                phy_queue[slotnum].lens = phy_queue[slotnum].lens + 1;
                phy_queue[slotnum].lock = 0;

                printf("current queue %d: Head: %d, Tail: %d, Lens: %d\n", slotnum, phy_queue[slotnum].head, phy_queue[slotnum].tail, phy_queue[slotnum].lens);
                
            } else if(plens > 1262){
                printf("rec package length too large: %d \n", plens);
            } else {
                printf("error slot or queue is full\n");
            }
        }
    }
}

// timeslot data push into the ddr-memeory
void data_handout(unsigned timeslot){
    
    // avoid the double thread setting at the same time
    while (phy_queue[timeslot].lock);
    phy_queue[timeslot].lock = 1;
    int lens = phy_queue[timeslot].lens;
    phy_queue[timeslot].lock = 0;
    
    if (lens != 0){
        if(lens<8){
            // because the queue isn't a circle, so when the head is in tail, we must slice from tail to head.
            if(phy_queue[timeslot].head+lens <= SLOTDEPTH){
                memcpy(ddrtool, &(phy_queue[timeslot].data[phy_queue[timeslot].head]), SLOTLENGTH * lens);
                bzero(ddrtool + SLOTLENGTH * lens, SLOTLENGTH * (8-lens));
            } else {
                char cut_len = SLOTDEPTH - phy_queue[timeslot].head;
                memcpy(ddrtool, &(phy_queue[timeslot].data[phy_queue[timeslot].head]), SLOTLENGTH * cut_len);
                memcpy(ddrtool + SLOTLENGTH * cut_len, &(phy_queue[timeslot].data[0]), SLOTLENGTH * (lens - cut_len));
                bzero(ddrtool + SLOTLENGTH * lens, SLOTLENGTH * (8-lens));
            }
        } else {
            // Similar to blove
            if(phy_queue[timeslot].head + 8 <= SLOTDEPTH){
                memcpy(ddrtool, &(phy_queue[timeslot].data[phy_queue[timeslot].head]), 10448);
            } else {
                char cut_len = SLOTDEPTH - phy_queue[timeslot].head;
                memcpy(ddrtool, &(phy_queue[timeslot].data[phy_queue[timeslot].head]), SLOTLENGTH * cut_len);
                memcpy(ddrtool + SLOTLENGTH * cut_len, &(phy_queue[timeslot].data[0]), SLOTLENGTH * (8 - cut_len));
            }
        }
        
        // send settings
        int val = 4;
        write(regfd, &val, 4);
        val = 5;
        write(regfd, &val, 4);
            
        // queue parameter handle
        if(lens<8){
            while (phy_queue[timeslot].lock);
            phy_queue[timeslot].lock = 1;
            phy_queue[timeslot].lens = phy_queue[timeslot].lens - lens;
            phy_queue[timeslot].lock = 0;
            phy_queue[timeslot].head = (phy_queue[timeslot].head + lens) % SLOTDEPTH;
        } else {
            while (phy_queue[timeslot].lock);
            phy_queue[timeslot].lock = 1;
            phy_queue[timeslot].lens = phy_queue[timeslot].lens - 8;
            phy_queue[timeslot].lock = 0;
            phy_queue[timeslot].head = (phy_queue[timeslot].head + 8) % SLOTDEPTH;
        }
        
        // printf("send success in %d\n", timeslot);
    }
}

void my_signal_fun(int signum) {
	unsigned char key_val;
	read(regfd, &key_val, 1);

	switch(key_val){
		case 0 :  data_handout(1); break; 
		case 1 :  data_handout(2); break; 
		case 2 :  data_handout(3); break; 
		case 3 :  data_handout(0); break; 
        // case 3 :  data_frame_test(1,1); break; 
		default:  break;
	}
	// printf("key_val: 0x%x\n", key_val);
}

int init_ap(void){
    int Oflags;
    // initial the parameter 
    bzero(&phy_queue, sizeof(tran_queue)*4);

    // initial the reg and irq driver
	signal(SIGIO, my_signal_fun);
	regfd = open("/dev/irq_drv", O_RDWR);
	if (regfd < 0){
		printf("can't open irq_dev!\n");
        return -1;
	}

    fcntl(regfd, F_SETOWN, getpid());
	Oflags = fcntl(regfd, F_GETFL); 
	fcntl(regfd, F_SETFL, Oflags | FASYNC);

    // initial the ddr memory driver
    ddrfd = open ("/dev/mem", O_RDWR | O_SYNC);
	if (ddrfd < 0) {
		printf("can't open ddr_dev!\n");
		return -1;
	}
	ddrtool = (unsigned char *)mmap(NULL, 10496, PROT_READ | PROT_WRITE, MAP_SHARED, ddrfd, 0x20000000);
    
    // initial the ethernet receive driver
    if((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1){
        printf("can't open ethernet!\n");
        return -1;
    }

    // // for debug
    // debug_fd = socket(AF_INET, SOCK_DGRAM, 0);
    // if(debug_fd < 0){
    //     printf("can't open ethernet!\n");
    //     return;
    // }
    // debug_addr.sin_family = AF_INET;
	// debug_addr.sin_port = htons(6666);
	// debug_addr.sin_addr.s_addr = inet_addr(IP_ADDR);

	// socklen_t addr_len = sizeof(struct sockaddr_in);

    int i,ret;
    // to create a thread to deal with socket data receive
    ret = pthread_create(&threadid,NULL,(void *) frame_rec, NULL); 
    if(ret!=0) {
        printf ("create pthread error!\n");
        return -1;
    }

    // initial control reg of fpga
	int val[4];
	val[0] = 4;  // the rising edge of val[0][0] to tell fpga to read ddr; val[0][1] = 1 to using inner syn for debug, and 0 to using outside syn signal, and val[0][2] = 1 to enable TDMA  
    val[3] = 0;                                       // unused
    val[1] = (unsigned int)LEDID3 << 16 | LEDID2;     // tell fpga the ledid      LED3 | LED2
	val[2] = (unsigned int)LEDID1;                    // led3 id                  NoLED| LED1

    write(regfd, &val, 16);
}

int main(void){    
    init_ap();
    pthread_join(threadid,NULL);
    return 0;
}