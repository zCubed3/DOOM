//-----------------------------------------------------------------------------
//
// Copyright (C) 2024, Liam Reese
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
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdint.h>

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <SDL3/SDL.h>

#include "doomdefs.h"

// When doom starts the video subsystem (I_InitGraphics) it waits for us to connect
// We connect and then init our framebuffer
// Doom already has a FB init'ed it just sends it to us

typedef uint8_t byte;

static SOCKET g_Socket;

static struct sockaddr_in g_ClientAddr;
static struct sockaddr_in g_ServerAddr;

static byte* g_Palette;

#define DOOM_PORT 666
#define DOOM_PORT_STR "666"
#define DOOM_IP "192.168.120.1"

static SDL_Window* g_SDLWindow;
static SDL_Renderer* g_SDLRenderer;
static SDL_Texture* g_SDLTexture;

static int g_KeepRunning = 1;

static int g_MouseHistory;

const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 200;

// TODO: Actually use the sequencer
// Used to order packets in their arrival time
const unsigned short g_Sequence = 0;

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

typedef struct c2s_mouse_motion_event_packet_t
{
    packet_header_t header;
    int16_t x_motion;
    int16_t y_motion;
    byte packed_code;
} c2s_mouse_motion_event_packet_t;

typedef struct c2s_mouse_button_event_packet_t
{
    packet_header_t header;
    byte packed_code;
} c2s_mouse_button_event_packet_t;

typedef enum S2C_PACKET_TYPE
{

    /// Tells client we ack them and accept them
    S2C_HELLO_CLIENT    = 0x00,

    /// Tells the client we're still alive
    S2C_HEARTBEAT       = 0x01,

    /// Tells the client the game is shutting down
    S2C_SHUTDOWN        = 0x02,

    /// One rendered frame of the game
    S2C_SCREEN_FRAME    = 0x10,

    /// Palette has changed
    S2C_SET_PALETTE     = 0x11,

    /// Start of frame (used as a signal to tell the client when to send their inputs)
    S2C_START_FRAME     = 0x12,

} S2C_PACKET_TYPE;

typedef enum C2S_PACKET_TYPE
{

    /// Tells server we've connected
    C2S_HELLO_SERVER    = 0x00,

    /// Tells the server we're still alive
    C2S_HEARTBEAT       = 0x01,

    /// Tells the server about our keypress
    C2S_KEY_PRESS_EVENT = 0x11,

    /// Mouse was moved
    C2S_MOUSE_MOTION_EVENT = 0x12,

    /// Mouse button was pressed
    C2S_MOUSE_BUTTON_EVENT = 0x13,

} C2S_PACKET_TYPE;

#pragma pack(pop)

void SendClientACK()
{
    packet_header_t pkt_header;
    pkt_header.type = C2S_HELLO_SERVER;

    int result = sendto(g_Socket, &pkt_header, sizeof(pkt_header), 0, (struct sockaddr*)&g_ClientAddr, sizeof(g_ClientAddr));
}

int ConnectToDOOM()
{
    struct addrinfo hints;
    struct addrinfo *result;

    ZeroMemory(&hints, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    int iResult = getaddrinfo(DOOM_IP, DOOM_PORT_STR, &hints, &result);

    if (iResult != 0)
    {
        printf("getaddrinfo failed: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    g_Socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

    if (g_Socket == INVALID_SOCKET)
    {
        printf("socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 2;
    }

    memset(&g_ServerAddr, 0, sizeof(g_ServerAddr));

    g_ServerAddr.sin_family = AF_INET;
    g_ServerAddr.sin_addr.s_addr = INADDR_ANY;
    g_ServerAddr.sin_port = DOOM_PORT;

    iResult = bind(g_Socket, (struct sockaddr*)&g_ServerAddr, sizeof(g_ServerAddr));

    if (iResult == SOCKET_ERROR)
    {
        printf("bind() failed: %d\n", WSAGetLastError());
        closesocket(g_Socket);
        g_Socket = INVALID_SOCKET;
        return 4;
    }

    if (g_Socket == INVALID_SOCKET)
    {
        printf("socket connection failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 5;
    }

    // Send an ack
    g_ClientAddr.sin_family = AF_INET;
    g_ClientAddr.sin_port = htons(DOOM_PORT);
    inet_pton(AF_INET, DOOM_IP, &g_ClientAddr.sin_addr);

    SendClientACK();

    return 0;
}

void HandleMessages()
{
    fd_set readfds;
    struct timeval timeout;

    struct sockaddr_in client_addr;
    int client_addr_len;

    FD_ZERO(&readfds);
    FD_SET(g_Socket, &readfds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    client_addr_len = sizeof(client_addr);

    int ret = select(g_Socket + 1, &readfds, NULL, NULL, &timeout);

    // TODO: Make sure socket isn't closed
    if (ret == 0)
        return;

    static char bytes[65536];
    packet_header_t* pkt_header = (packet_header_t*)bytes;

    // Get messages from the server
    int iresult = recvfrom(g_Socket, bytes, 65536, 0, (struct sockaddr*)&client_addr, &client_addr_len);

    if (iresult == 0)
        return;

    if (iresult < 0)
        return;

    if (pkt_header->type == S2C_SET_PALETTE)
    {
        memcpy(g_Palette, bytes + sizeof(packet_header_t), 256 * 3);
    }

    if (pkt_header->type == S2C_SCREEN_FRAME)
    {
        s2c_frame_packet_t* pkt_f_header = bytes;

        if (pkt_f_header->width != SCREEN_WIDTH && pkt_f_header->height != SCREEN_HEIGHT)
        {
            printf("FRAME PACKET WAS MALFORMED, SIZE WAS %d, WE EXPECT %d!\n", pkt_f_header->width * pkt_f_header->height, SCREEN_WIDTH * SCREEN_HEIGHT);
            exit(1);
        }

        // TODO: Actually verify size
        byte* argb_pixels, *framebuffer;
        int pitch;

        SDL_LockTexture(g_SDLTexture, NULL, &argb_pixels, &pitch);
        framebuffer = bytes + sizeof(s2c_frame_packet_t);

        for (int r = 0; r < SCREEN_HEIGHT; r++) {

            for (int c = 0; c < SCREEN_WIDTH; c++) {

                int out_index = (r * pitch) + (c * 4);
                int in_index = (r * SCREEN_WIDTH) + (c);

                // Format is BGRA for some reason

                int palette_idx = framebuffer[in_index];
                int real_palette_idx = palette_idx * 3;

                argb_pixels[out_index + 2] = g_Palette[real_palette_idx];
                argb_pixels[out_index + 1] = g_Palette[real_palette_idx + 1];
                argb_pixels[out_index] = g_Palette[real_palette_idx + 2];
                argb_pixels[out_index + 3] = 0xFF;

            }

        }

        SDL_UnlockTexture(g_SDLTexture);
    }

    if (pkt_header->type == S2C_SHUTDOWN)
    {
        printf("Goodbye!\n");
        g_KeepRunning = 0;
    }
}

int ConvertScancode( SDL_Scancode sdl_code )
{
    switch (sdl_code)
    {

        case SDL_SCANCODE_RCTRL:
            return KEY_RCTRL;

        case SDL_SCANCODE_RSHIFT:
            return KEY_RSHIFT;

    }

    return -1;
}

int ConvertKeycode( SDL_Keycode sdl_code )
{
    switch (sdl_code)
    {

        case SDLK_LEFT:
            return KEY_LEFTARROW;

        case SDLK_RIGHT:
            return KEY_RIGHTARROW;

        case SDLK_UP:
            return KEY_UPARROW;

        case SDLK_DOWN:
            return KEY_DOWNARROW;

        case SDLK_RETURN:
            return KEY_ENTER;

        case SDLK_SPACE:
            return ' ';

        case SDLK_F1:
            return KEY_F1;

        case SDLK_F2:
            return KEY_F2;

        case SDLK_F3:
            return KEY_F3;

        case SDLK_F4:
            return KEY_F4;

        case SDLK_F5:
            return KEY_F5;

        case SDLK_F6:
            return KEY_F6;

        case SDLK_F7:
            return KEY_F7;

        case SDLK_F8:
            return KEY_F8;

        case SDLK_F9:
            return KEY_F9;

        case SDLK_F10:
            return KEY_F10;

        case SDLK_F11:
            return KEY_F11;

        case SDLK_F12:
            return KEY_F12;

        case SDLK_ESCAPE:
            return KEY_ESCAPE;

        case SDLK_TAB:
            return KEY_TAB;

    }

    return -1;
}

void PollEvents()
{
    SDL_Event sdl_event;

    memset(&sdl_event, 0, sizeof(sdl_event));

    while (SDL_PollEvent(&sdl_event)) {

        if (sdl_event.type == SDL_EVENT_QUIT) {
            g_KeepRunning = 0;
        }

        if (sdl_event.type == SDL_EVENT_KEY_DOWN
        || sdl_event.type == SDL_EVENT_KEY_UP) {

            if (sdl_event.key.key == SDLK_R && sdl_event.key.mod & SDL_KMOD_CTRL)
            {
                // Tells the server to reconnect
                SendClientACK();
                continue;
            }

            // Ignore repeats because doom input isn't filtered?
            if (sdl_event.key.repeat)
                continue;

            c2s_key_event_packet_t pkt_key_event;
            pkt_key_event.header.type = C2S_KEY_PRESS_EVENT;

            pkt_key_event.is_down = sdl_event.type == SDL_EVENT_KEY_DOWN;

            // TODO: Translate into doom keycodes
            int code = -1;

            if ((code = ConvertKeycode(sdl_event.key.key)) != -1) {
                pkt_key_event.key_code = code;
            } else if ((code = ConvertScancode(sdl_event.key.scancode)) != -1) {
                pkt_key_event.key_code = code;
            } else {
                pkt_key_event.key_code = sdl_event.key.key;
            }

            sendto(g_Socket, &pkt_key_event, sizeof(pkt_key_event), 0, (struct sockaddr*)&g_ClientAddr, sizeof(g_ClientAddr));
        }

        if (sdl_event.type == SDL_EVENT_MOUSE_MOTION) {
            c2s_mouse_motion_event_packet_t pkt_mouse_event;
            pkt_mouse_event.header.type = C2S_MOUSE_MOTION_EVENT;

            pkt_mouse_event.x_motion = sdl_event.motion.xrel;
            pkt_mouse_event.y_motion = -sdl_event.motion.yrel;
            pkt_mouse_event.packed_code = g_MouseHistory;

            sendto(g_Socket, &pkt_mouse_event, sizeof(pkt_mouse_event), 0, (struct sockaddr*)&g_ClientAddr, sizeof(g_ClientAddr));
        }

        if (sdl_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        || sdl_event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            c2s_mouse_button_event_packet_t pkt_mouse_event;
            pkt_mouse_event.header.type = C2S_MOUSE_BUTTON_EVENT;

            if (sdl_event.button.down)
                g_MouseHistory |= (1 << (sdl_event.button.button - 1));
            else
                g_MouseHistory &= ~(1 << (sdl_event.button.button - 1));

            //pkt_mouse_event.packed_code |= (sdl_event.button.down << 7);
            pkt_mouse_event.packed_code = g_MouseHistory;

            sendto(g_Socket, &pkt_mouse_event, sizeof(pkt_mouse_event), 0, (struct sockaddr*)&g_ClientAddr, sizeof(g_ClientAddr));
        }
    }

}

int main(void)
{
    SDL_Init(SDL_INIT_VIDEO);

    printf("Connecting to Doom...\n");

    int iResult;
    WSADATA wsaData;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }

    int result = ConnectToDOOM();

    if (result != 0) {
        return result;
    }

    // We pre-allocate the framebuffer here, anything larger we ignore
    printf("Connected!\n");

    g_SDLWindow = SDL_CreateWindow("DOOM CONNECTOR", 320 * 4, 200 * 4, SDL_WINDOW_RESIZABLE);
    g_SDLRenderer = SDL_CreateRenderer(g_SDLWindow, "Software");

    g_SDLTexture = SDL_CreateTexture(
            g_SDLRenderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            SCREEN_WIDTH,
            SCREEN_HEIGHT);

    SDL_SetTextureScaleMode(g_SDLTexture, SDL_SCALEMODE_NEAREST);

    SDL_SetWindowRelativeMouseMode(g_SDLWindow, 1);

    g_Palette = malloc(256 * 3);

    while (g_KeepRunning)
    {
        PollEvents();

        HandleMessages();

        SDL_RenderClear(g_SDLRenderer);
        SDL_RenderTexture(g_SDLRenderer, g_SDLTexture, NULL, NULL);
        SDL_RenderPresent(g_SDLRenderer);
    }

    return 0;
}
