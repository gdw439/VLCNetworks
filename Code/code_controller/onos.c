#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>           // close()
#include <string.h>           // strcpy, memset(), and memcpy()
#include <netdb.h>            // struct addrinfo
#include <sys/types.h>        // needed for socket(), uint8_t, uint16_t, uint32_t
#include <sys/socket.h>       // needed for socket()
#include <netinet/in.h>       // IPPROTO_ICMP, INET_ADDRSTRLEN
#include <netinet/ip.h>       // struct ip and IP_MAXPACKET (which is 65535)
#include <netinet/ip_icmp.h>  // struct icmp, ICMP_ECHO
#include <arpa/inet.h>        // inet_pton() and inet_ntop()
#include <sys/ioctl.h>        // macro ioctl is defined
#include <bits/ioctls.h>      // defines values for argument "request" of ioctl.
#include <net/if.h>           // struct ifreq
#include <linux/if_ether.h>   // ETH_P_IP = 0x0800, ETH_P_IPV6 = 0x86DD
#include <linux/if_packet.h>  // struct sockaddr_ll (see man 7 packet)
#include <net/ethernet.h>
#include <errno.h>            // errno, perror()
#include <pthread.h>

#define SLIP 2000       // the size of data package cache which come from server 
#define SLEN 1500       // the maxnum length of package

#define MAX_UE          4
#define MAC_MAXLEN     1288

#define FEEDBACK_PORT  5577   // wifi udp recv
#define DATABASE_PORT  2050   // use the port to receive the data from server

#define ETH_P_DEAN     0x2050 // 自己定义的以太网协议type
#define VLC_REGR_TYPE  0x00FF // user register type
#define VLC_DATA_TYPE  0xFF00 // data frame type

typedef struct sockaddr* saddrp;

typedef struct {
    unsigned short type;
    unsigned short lens;
    unsigned short ueid;
    unsigned short ledid;
    unsigned short power;
    unsigned char  uemac[6];
} feedback_head;

typedef struct {
    unsigned char  macaddr[6];
} mac_addr_table; 

int sockfd[MAX_UE];
int tomac_fd;
int feedback_fd;
uint8_t src_mac[6];
uint8_t dst_mac[6];
struct sockaddr_ll device;

typedef struct {
    unsigned short lock;
    unsigned short lens;
    unsigned short head;
    unsigned short tail;
    unsigned short size[SLIP];
    unsigned char  data[SLIP][SLEN];
} cache;

cache onos_cache[MAX_UE];
// cache onos_cache;

int threadid[MAX_UE];

pthread_t database_thread[MAX_UE];
pthread_t feedback_thread;

mac_addr_table ap_mac_table[4] = {
    {0x00, 0x0a, 0x35, 0x00, 0x00, 0x05},
    {0x00, 0x0a, 0x35, 0x00, 0x00, 0x08},
    {0x00, 0x0a, 0x35, 0x00, 0x00, 0x01},
    {0x00, 0x0a, 0x35, 0x00, 0x00, 0x04}
};

typedef struct {
    unsigned short UEID;
    unsigned char  UEMAC[6];
} UE_INFO;

UE_INFO ue_info[MAX_UE];
static

void * data_buff(void * arg);
void feedback_info(void);
void data_frame_package(unsigned short cur_led, unsigned short cur_ue, unsigned char * data, unsigned short datalen);

// initial the channel to network layer
void init_app(void){
    struct sockaddr_in rec_addr[MAX_UE];
    // initial the ethernet receive driver
    for(int i=0; i<MAX_UE; ++i){
        sockfd[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if(sockfd[i] < 0){
            printf("can't open ethernet!\n");
            return;
        }
        
        memset(&rec_addr[i], 0, sizeof(rec_addr[i]));
        rec_addr[i].sin_family = AF_INET;
        rec_addr[i].sin_addr.s_addr = htonl(INADDR_ANY); //IP地址，需要进行网络序转换，INADDR_ANY：本地地址
        rec_addr[i].sin_port = htons(DATABASE_PORT+i);     //端口号，需要网络序转换

        int ret = bind(sockfd[i], (struct sockaddr*)&rec_addr[i], sizeof(rec_addr[i]));
        if(ret < 0){
            printf("socket bind fail!\n");
            return;
        }
        // to create a thread to deal with socket data receive
        threadid[i] = i;
        ret = pthread_create(&database_thread[i],NULL, data_buff, &threadid[i]); 
        if(ret!=0) {
            printf ("create data_buff pthread error!\n");
            return;
        }
    }
    int ret2 = pthread_create(&feedback_thread,NULL,(void *) feedback_info, NULL); 
    if(ret2!=0) {
        printf ("create feedback_info pthread error!\n");
        return;
    }
}

// initial the channel to wifi feedback
void init_feedback(void){
    //创建socket
    feedback_fd = socket(AF_INET,SOCK_DGRAM,0);
    if (feedback_fd<0){
        perror("feedback_fd");
        return;
    }
    //准备地址
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;                  //ipv4
    addr.sin_port = htons(FEEDBACK_PORT);       //端口号
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   //我的ip地址

    //绑定
    int ret = bind(feedback_fd,(saddrp)&addr,sizeof(addr));
    if (0 > ret){
        perror("bind");
        return;
    }
}

// initial the channel to mac layer
void init_data_trans(void){
    char *interface="ens33";;
    struct ifreq ifr;

    // enempty the queue infomcation 
    bzero(&ue_info, sizeof(ue_info));
    for(int i=0; i<MAX_UE; i++){
        ue_info[i].UEID = i + 1;
    }

    bzero(&onos_cache, sizeof(onos_cache));

    // Submit request for a socket descriptor to look up interface.
    if ((tomac_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {//第一次创建socket是为了获取本地网卡信息
        perror("socket() failed to get socket descriptor for using ioctl() ");
        exit(EXIT_FAILURE);
    }

    // Use ioctl() to look up interface name and get its MAC address.
    memset (&ifr, 0, sizeof(ifr));
    snprintf (ifr.ifr_name, sizeof (ifr.ifr_name), "%s", interface);
    if (ioctl(tomac_fd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("ioctl() failed to get source MAC address ");
        return;
    }
    close (tomac_fd);

    // Copy source MAC address.
    memcpy(src_mac, ifr.ifr_hwaddr.sa_data, 6);

    // Report source MAC address to stdout.
    printf("MAC address for interface %s is ", interface);
    for(int i=0; i<5; i++){
        printf ("%02x:", src_mac[i]);
    }
    printf ("%02x\n", src_mac[5]);

    // Find interface index from interface name and store index in
    // struct sockaddr_ll device, which will be used as an argument of sendto().
    memset (&device, 0, sizeof (device));
    if ((device.sll_ifindex = if_nametoindex (interface)) == 0) {
        perror ("if_nametoindex() failed to obtain interface index ");
        exit (EXIT_FAILURE);
    }
    printf ("Index for interface %s is %i\n", interface, device.sll_ifindex);

    // Fill out sockaddr_ll.
    device.sll_family = AF_PACKET;
    memcpy (device.sll_addr, src_mac, 6);
    device.sll_halen = htons(6);
    int Index;

    // Submit request for a raw socket descriptor.
    if ((tomac_fd = socket(PF_PACKET, SOCK_RAW, htons (ETH_P_ALL))) < 0) {//创建正真发送的socket
        perror ("socket() failed ");
        exit (EXIT_FAILURE);
    }
}

void * data_buff(void * arg){
    int recv_num;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    unsigned char buff[2000];
    int *fd;
    fd = (int*)arg;
    int index =  *fd;
    printf("%d\n", index);
    while(1){
        recv_num = recvfrom(sockfd[index], &onos_cache[index].data[onos_cache[index].tail], 1500, 0, (struct sockaddr * )&addr, &addr_len);
        onos_cache[index].size[onos_cache[index].tail] = recv_num;
        if(onos_cache[index].lens < SLIP){
            onos_cache[index].tail = (onos_cache[index].tail + 1) % SLEN;
            onos_cache[index].lens = onos_cache[index].lens + 1;
        } else {
            printf("too many package rec\n");
        }
        // printf("recv %d, leisure %d\n", recv_num, onos_cache.lens);
    }
}

void regi_frame_package(unsigned short cur_led, unsigned short PreUEID, unsigned char * ue_mac){
    uint8_t ether_frame[MAC_MAXLEN];

    // fill up ip address according to the led feedback
    memcpy (ether_frame, ap_mac_table[(cur_led-1)/3].macaddr, 6);
    memcpy (ether_frame + 6, src_mac, 6);

    ether_frame[12] = ETH_P_DEAN / 256;
    ether_frame[13] = ETH_P_DEAN % 256;

    // timeslot
    ether_frame[14] = 0;
    ether_frame[15] = (PreUEID-1) % 3 + 1;                // here self think the maps satifitied this 
    printf("timeslot: %d %d\n", ether_frame[15], cur_led);
    // type
    ether_frame[16] = 0x00;
    ether_frame[17] = 0xFF;
    
    // length
    ether_frame[18] = 0; 
    ether_frame[19] = 6; 
    // led_id
    ether_frame[20] = 0;
    ether_frame[21] = cur_led; 
    // ue_id
    ether_frame[22] = PreUEID / 256;
    ether_frame[23] = PreUEID % 256; 

    // memcpy(ether_frame + 24, &ue_info[PreUEID-1].UEMAC, 6);
    memcpy(ether_frame + 24, ue_mac, 6);
    int frame_length = 30;
    
    // Send ethernet frame to socket.
    int bytes;
    if ((bytes = sendto(tomac_fd, ether_frame, frame_length, 0, (struct sockaddr *) &device, sizeof (device))) <= 0) {
        perror ("sendto() failed");
        exit (EXIT_FAILURE);
    }
}

void data_frame_package(unsigned short cur_led, unsigned short cur_ue, unsigned char * data, unsigned short datalen){
    uint8_t ether_frame[MAC_MAXLEN];
    
    // fill up ip address according to the led feedback
    memcpy (ether_frame, ap_mac_table[(cur_led-1)/3].macaddr, 6);
    memcpy (ether_frame + 6, src_mac, 6);

    ether_frame[12] = ETH_P_DEAN / 256;
    ether_frame[13] = ETH_P_DEAN % 256;

    // timeslot
    ether_frame[14] = 0;
    ether_frame[15] = (cur_ue-1) % 3 + 1;  
    // type
    ether_frame[16] = 0xFF;
    ether_frame[17] = 0x00;
    
    // length
    ether_frame[18] = datalen >> 8; 
    ether_frame[19] = datalen & 0x00FF; 
    // led_id
    ether_frame[20] = 0;
    ether_frame[21] = cur_led; 
    // ue_id
    ether_frame[22] = cur_ue >> 8;
    ether_frame[23] = cur_ue & 0x00FF; 

    memcpy(ether_frame + 24, data, datalen);
    int frame_length = 24 + datalen;
    
    // Send ethernet frame to socket.
    int bytes;
    if ((bytes = sendto (tomac_fd, ether_frame, frame_length, 0, (struct sockaddr *) &device, sizeof (device))) <= 0) {
        perror ("sendto() failed");
        exit (EXIT_FAILURE);
    }
}

void feedback_info(void){
    struct sockaddr_in src_addr ={};
    socklen_t addr_len = sizeof(struct sockaddr_in);
    while(1){
        unsigned char buf[200];
        //接收数据和来源的ip地址
        int data_lens = recvfrom(feedback_fd,buf,sizeof(buf),0,(saddrp)&src_addr,&addr_len);
        feedback_head * wifi_head = (feedback_head * )buf;

        unsigned short PreUEID = 0x00FF;
        
        if(wifi_head->ledid<=0 || wifi_head->ledid>12){
            printf("a error led_id(%d) abrupts.\n", wifi_head->ledid);
            continue;
        }else if(wifi_head->power<160){
            // In order to filter the wrong feedback
            printf("TYPE: %4X | LENS: %d | UEID: %4X | LEDID: %4X | POWER: %d\t\n",wifi_head->type, wifi_head->lens, wifi_head->ueid, wifi_head->ledid, wifi_head->power);
            continue;
        
        // deal with register feedback
        } else if(wifi_head->ueid==0x00FF){ 
            // printf("TYPE: %4X | LENS: %d | UEID: %4X | LEDID: %4X | POWER: %d\t\n",wifi_head->type, wifi_head->lens, wifi_head->ueid, wifi_head->ledid, wifi_head->power);
            for(char i=0; i<5; i++){
                if(wifi_head->uemac[5] == ue_info[i].UEMAC[5]){
                     PreUEID = ue_info[i].UEID;
                    printf("get repeated registration, maybe the indoor network architecture is unsuitable.\n");
                }
                // to assign a vaild ueid
                if(ue_info[i].UEMAC[5]==0 && PreUEID == 0x00FF){
                    PreUEID = ue_info[i].UEID;
                }
            }
            regi_frame_package(wifi_head->ledid, PreUEID, wifi_head->uemac);
            memcpy(ue_info[PreUEID-1].UEMAC, wifi_head->uemac, 6);

            printf("recvice the regiter frame\n");

        // deal with the data package feedback
        } else {
            if(wifi_head->ueid<=0 || wifi_head->ueid>MAX_UE){
                printf("ueid out of range\n");
                return;
            }
            char index = wifi_head->ueid - 1;
            // printf("TYPE: %4X | LENS: %d | UEID: %4X | LEDID: %4X | POWER: %d\t\n",wifi_head->type, wifi_head->lens, wifi_head->ueid, wifi_head->ledid, wifi_head->power);
            for(int i=0; i<8; i++){
                if(onos_cache[index].lens > 0){
                    data_frame_package(wifi_head->ledid, wifi_head->ueid, onos_cache[index].data[onos_cache[index].head], onos_cache[index].size[onos_cache[index].head]);
                    // printf("ID: %d, HEAD:%d\n",onos_cache.data[onos_cache.head][0]*256 + onos_cache.data[onos_cache.head][1], onos_cache.head);
                    onos_cache[index].lens = onos_cache[index].lens - 1;
                    onos_cache[index].head = (onos_cache[index].head + 1) % SLEN;
                }else{
                    break;
                }
                // printf("send frame\n");
            }
        }
    }  
    //关闭socket对象
    close(feedback_fd);
    return;
}

int main (int argc, char **argv){
    init_app();
    init_feedback();
    init_data_trans();
    
    for(int i=0; i<MAX_UE; i++)
        pthread_join(database_thread[i],NULL);
    pthread_join(feedback_thread,NULL);

    return (EXIT_SUCCESS);
    close(tomac_fd);
}