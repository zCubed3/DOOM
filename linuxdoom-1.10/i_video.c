// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <errno.h>
#include <signal.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include <termios.h>

#include <netdb.h>
#include <sys/socket.h>

#include "doomdef.h"	

#define FBUF_SPAN (SCREENWIDTH + 1)
#define FBUF_SIZE (SCREENHEIGHT * FBUF_SPAN)

static char OUTPUT_FBUF[FBUF_SIZE];
static char ASCII_MAP[9] = { '.', ':', '-', '=', '+', '*', '#', '%', '@' };

#define NB_DISABLE 1
#define NB_ENABLE 0

static int KEY_HISTORY[256];

//#define INPUT_ONLY_DEBUGGING

static int sockfd;
static struct sockaddr_in active_client_addr;
static int have_active_client;
static int client_need_palette;

#define PORT 666

#pragma pack(push, 1)

typedef struct packet_header_t 
{

    uint8_t type;
    uint16_t sequence;

} packet_header_t;

typedef struct s2c_frame_packet_t 
{

    packet_header_t header;
    uint16_t width;
	uint16_t height;

} s2c_frame_packet_t;

typedef struct c2s_key_event_packet_t 
{

    packet_header_t header;
    byte is_down;
    byte key_code;

} c2s_key_event_packet_t;

typedef enum S2C_PACKET_TYPE {

    /// Tells client we ack them and accept them
    S2C_HELLO_CLIENT    = 0x00,

    /// Tells the client we're still alive
    S2C_HEARTBEAT       = 0x01,

    /// One rendered frame of the game
    S2C_SCREEN_FRAME    = 0x10,

    /// Palette has changed
    S2C_SET_PALETTE     = 0x11,

    /// Start of frame (used as a signal to tell the client when to send their inputs)
    S2C_START_FRAME     = 0x12,

} S2C_PACKET_TYPE;

typedef enum C2S_PACKET_TYPE {

    /// Tells the server we're still alive
    C2S_HEARTBEAT       = 0x01,

    /// Tells server we've connected
    C2S_HELLO_SERVER    = 0x10,

    /// Tells the server about our keypress
    C2S_KEY_PRESS_EVENT = 0x11,

} C2S_PACKET_TYPE;

#pragma pack(pop)

int khbit() 
{

	struct timeval tv;
	fd_set fds;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);

	select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
	
	return FD_ISSET(STDIN_FILENO, &fds);

}

void nonblock(int state)
{
	
    struct termios ttystate;

    //get the terminal state
    tcgetattr(STDIN_FILENO, &ttystate);

    if (state==NB_ENABLE)
    {
        //turn off canonical mode
        ttystate.c_lflag &= ~ICANON;
		ttystate.c_lflag &= ~ECHO;

        //minimum of number input read.
        ttystate.c_cc[VMIN] = 1;
    }
    else if (state==NB_DISABLE)
    {
        //turn on canonical mode
        ttystate.c_lflag |= ICANON;
		ttystate.c_lflag |= ECHO;
    }
    //set the terminal attributes.
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);

}


void I_ShutdownGraphics(void)
{
	
	// TODO: Tell client bye-bye!
	I_ShutdownTCPServer();

	free(screens[0]);

}

void I_SetupTCPServer (void)
{

	int len, iresult;
	struct sockaddr_in servaddr, cli;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (sockfd == -1) {
		printf("Socket creation failed!\n");
		exit(1);
	} 

	bzero(&servaddr, sizeof(servaddr));

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT);

	iresult = bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if (iresult != 0) {
		printf("Socket bind failed with code, %d!\n", errno);
		exit(1);
	}

/*
	iresult = listen(sockfd, 5);
	if (iresult != 0) {
		printf("Listen failed!\n");
		exit(1);
	}

	printf("Socket created, waiting for client to join...\n");

	len = sizeof(cli);
	connfd = accept(sockfd, (struct sockaddr*)&cli, &len);
	if (connfd < 0)
 	{
		printf("Accept failed with error code, %d!\n", errno);
		exit(1);
	}

	printf("Client joined!\n");
*/
}

void I_ShutdownTCPServer (void)
{

	close(sockfd);

}


//
// I_StartFrame
//
void I_StartFrame (void)
{

	// TODO

}

int ConvertKey(char key)
{

	// Most likely an arrow key
	if (key == 91 && khbit()) 
	{
		char event1;
		char event2;

		event1 = fgetc(stdin);
		event2 = fgetc(stdin);

		switch (event1) 
		{
			case 'A':
				return KEY_UPARROW;

			case 'B':
				return KEY_DOWNARROW;

			case 'D':
				return KEY_LEFTARROW;

			case 'C':
				return KEY_RIGHTARROW;
		}
	}

	if (key == 10) 
	{
		return KEY_ENTER;
	}

	return key;

}

int I_ScanNetwork (void)
{

	fd_set readfds;
    struct timeval timeout;

	packet_header_t pkt_header;
	struct sockaddr_in client_addr;
	int client_addr_len;

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

	client_addr_len = sizeof(client_addr);

    int ret = select(sockfd + 1, &readfds, NULL, NULL, &timeout);

	if (ret == 0)
		return 0;

	// If we see an ACK from a client, say hello
	recvfrom(sockfd, &pkt_header, sizeof(pkt_header), MSG_PEEK, (struct sockaddr*)&client_addr, &client_addr_len);

	//printf("GOT PACKET, READING IT\n");

	if (pkt_header.type == C2S_HELLO_SERVER)
	{
		// Receive it again to flush buffer
		recvfrom(sockfd, &pkt_header, sizeof(pkt_header), 0, NULL, NULL);

		printf("HELLO CLIENT!\n");
		printf("SENDING HELLO BACK TO %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		// Tell the client hello back and store their addr
		pkt_header.type = S2C_HELLO_CLIENT;
		sendto(sockfd, &pkt_header, sizeof(pkt_header), 0, (struct sockaddr*)&client_addr, &client_addr_len);

		active_client_addr = client_addr;
		have_active_client = 1;
		client_need_palette = 1;

	}

	if (pkt_header.type == C2S_KEY_PRESS_EVENT)
	{
		c2s_key_event_packet_t pkt_key_event;
		event_t doom_event;

		recvfrom(sockfd, &pkt_key_event, sizeof(pkt_key_event), 0, NULL, NULL);

		doom_event.type = pkt_key_event.is_down ? ev_keydown : ev_keyup;
		doom_event.data1 = pkt_key_event.key_code;

		D_PostEvent(&doom_event);
	}

	return 1;

}

void I_GetEvent(void)
{

	while (I_ScanNetwork())
	{
		// TODO: Do something here?
	}

}

//
// I_StartTic
//
void I_StartTic (void)
{

	I_GetEvent();

}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{

	int w;
	int h;
	s2c_frame_packet_t* pkt_f_header;
	byte* pkt_frame_burst;

	static byte* p_burst_space;
	static int burst_size;
	static const char* MESSAGE = "WELCOME TO HELL!";

	// Dont render frames
	#ifdef INPUT_ONLY_DEBUGGING
	return;
	#endif

	#ifdef RENDER_TO_TERMINAL
	puts("\033[1;1H");

	//sprintf(OUTPUT_FBUF, "HELLO, WORLD!\n");
	for (h = 0; h < SCREENHEIGHT; h++) {
		for (w = 0; w < SCREENWIDTH; w++) {
			OUTPUT_FBUF[w + (h * FBUF_SPAN)] = ASCII_MAP[screens[0][w + (h * SCREENWIDTH)] % 9];
		}
	}
	#endif

	if (p_burst_space == NULL) {
		burst_size = sizeof(s2c_frame_packet_t) + (SCREENWIDTH * SCREENHEIGHT);
		p_burst_space = malloc(burst_size);
	}

	pkt_f_header = (s2c_frame_packet_t*)p_burst_space;
	pkt_frame_burst = p_burst_space + sizeof(s2c_frame_packet_t);

	//write(STDOUT_FILENO, OUTPUT_FBUF, FBUF_SIZE);

	// Send the client our screen (assuming we have a client)
	if (!have_active_client)
		return;

	// If need palette, send that first
	if (client_need_palette)
	{
		client_need_palette = 0;
		I_SetPalette(NULL);
	}

	pkt_f_header->header.type = (uint8_t)S2C_SCREEN_FRAME;
	pkt_f_header->width = SCREENWIDTH;
	pkt_f_header->height = SCREENHEIGHT;
	memcpy(pkt_frame_burst, screens[0], pkt_f_header->width * pkt_f_header->height);

	//printf("SENDING BURST TO %s:%d\n" inet_ntoa(active_client_addr.sin_addr), ntohs(active_client_addr.sin_port));
	sendto(sockfd, p_burst_space, burst_size, 0, (struct sockaddr*)&active_client_addr, sizeof(active_client_addr));

}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
	
	memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);

}

//
// I_SetPalette
//
void I_SetPalette (byte* palette)
{
	
	#define PALETTE_MSG_SIZE ((256 * 3) + sizeof(packet_header_t))
	
	static byte palette_scratch[PALETTE_MSG_SIZE];
	
	packet_header_t* pkt_header;
	byte* pkt_palette_burst;

	pkt_palette_burst = palette_scratch + sizeof(packet_header_t);

	if (palette != NULL)
		memcpy(pkt_palette_burst, palette, 256 * 3);

	if (!have_active_client)
		return;

	pkt_header = (packet_header_t*)palette_scratch;
	pkt_header->type = (uint8_t)S2C_SET_PALETTE;

	//printf("SENDING BURST TO %s:%d\n" inet_ntoa(active_client_addr.sin_addr), ntohs(active_client_addr.sin_port));
	sendto(sockfd, palette_scratch, PALETTE_MSG_SIZE, 0, (struct sockaddr*)&active_client_addr, sizeof(active_client_addr));

}

void I_InitGraphics(void)
{

	int w = 0;
	int h = 0;

	screens[0] = (unsigned char *) malloc (SCREENWIDTH * SCREENHEIGHT);

	for (h = 0; h < SCREENHEIGHT; h++) {
		for (w = 0; w < SCREENWIDTH; w++) {
			OUTPUT_FBUF[w + (h * FBUF_SPAN)] = '\0';
		}

		OUTPUT_FBUF[SCREENWIDTH + (h * FBUF_SPAN)] = '\n';
	}

	I_SetupTCPServer();

}