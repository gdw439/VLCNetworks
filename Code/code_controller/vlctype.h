#ifndef _VLCTYPE_H_
#define _VLCTYPE_H_

#define TIMESLOT   4
#define RECLENGTH  1500
#define RECDEPTH   400

#define SLOTLENGTH 1306
#define SLOTDEPTH  400

typedef struct {
  unsigned char target_mac[6];
  unsigned char source_mac[6];
  unsigned short frame_type;         // self define for vlc network 
} eth_header;

// define header of frame receive from nework 
typedef struct {
  unsigned short timeslot;	
  unsigned short type;	
  unsigned short length;	
  unsigned short led_id;
  unsigned short ue_id;   
  } network_header;

// define header of frame transmit to fpga
typedef struct {
    unsigned short led_id;	
    unsigned short timeslot;	
    int   type_and_length[5];	
    int   ledid_and_ueid[5];	
}   mac_header;

typedef struct {
    char head;
    char tail;
    char full;
    char empty;
    char data[RECDEPTH][RECLENGTH];
    char lens[RECDEPTH][RECLENGTH];
} rec_queue;

typedef struct {
    unsigned short head;
    unsigned short tail;
    unsigned short lens;
    unsigned short lock;
    unsigned char data[SLOTDEPTH][SLOTLENGTH];
} tran_queue;

#endif 