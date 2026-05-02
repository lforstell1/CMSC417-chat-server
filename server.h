#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>


//constants 
#define MAXPEND 10
#define PASSMAXLEN 255
#define ROOMMAXLEN 255
#define NAMEMAXLEN 255
#define MSGMAXLEN  65536
#define MAXCLIENT 100
#define MAXTOKEN 512
#define MAXROOMS 100

typedef enum{
    
    CONNECTA  = 0x9b,
    CONNECTB = 0x29,
    LISTU = 0x0c,
    LISTR = 0x09,
    NICK = 0x0f,
    JOIN = 0x03,
    CHATMSG = 0x15,
    PRIVMSG = 0x12,
    LEAVE = 0x06,
    ALIVE = 0x13
} command_code;


//structs

struct server_arguments {
    int port;
    char *tokenlist;
};

struct client{
    int fd;
    char name[NAMEMAXLEN];
    int room;
    int authed;
};

struct room{
    char name[ROOMMAXLEN];
    char password[PASSMAXLEN];
    int active;
    int num_members;
};

//function prototypes
struct server_arguments server_parseopt(int argc, char *argv[]);
ssize_t recv_all(int sock, void *buf, size_t len);
ssize_t send_all(int sock, void *buf, size_t len);
void handle_client(int idx);
void handle_new_connection(int sock);
//command functions
void disconnect_client(int idx);
void handle_connect(int idx, uint8_t *payload, uint32_t plen);
void handle_nick(int idx, uint8_t *payload, uint32_t plen);
void handle_join(int idx, uint8_t *payload, uint32_t plen);
void handle_leave(int idx);
void handle_listu(int idx);
void handle_listr(int idx);
void handle_chat(int idx, uint8_t *payload, uint32_t plen);
void handle_privmsg(int idx, uint8_t *payload, uint32_t plen);
//name helpers
int name_taken(char *name, int curr_idx);
void assign_name(int idx);
//issue msgs to client
void send_error(int fd, uint8_t code, char *msg);
void send_response(int fd, uint8_t code, uint8_t *payload, uint32_t len);
void send_command(int fd, uint8_t cmd, uint8_t *payload, uint32_t plen);
//token helpers
void load_tokens(char* filename);
int valid_token(char *token);

#endif