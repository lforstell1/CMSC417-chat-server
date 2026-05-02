#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <argp.h>
#include <poll.h>

//msg
#define CONNECTMSG "What in the blue blazes? Crazy hotrodder!"
#define NAMETAKENMSG "That name's already taken, hot rod."
#define TOKENAUTHFAIL "It hurts my feelings when you talk jibbersmack to me."
#define WRONGROOMPASS "I knew you couldn't drive. I didn’t know you couldn't type!"
#define NOROOMMSG "You keep talkin' to yourself, people will think you're crazy."
#define NOUSERMSG "McQueen reported missing. Maybe try a different town?"
#define JOINROOMALREADYIN "You're already in Radiator Sprints, the cutest little town in Carburetor County."

//not sure what causes this one yet
#define ERR "You think I quit? They quit on me."

//response codes
#define RSPOK 0x00
#define RSPERR 0x01
#define RSPCTXERR 0x03

//globals
struct client clients[MAXCLIENT];
struct room rooms[MAXROOMS];
int roomnum = 0;
char tokens[MAXTOKEN][MAXTOKEN];
int tokennum = 0;

//parser stuff i modified from old projects
error_t server_parser(int key, char *arg, struct argp_state *state) {
	struct server_arguments *args = state->input;
	error_t ret = 0;
    int len;
	switch(key) {
	case 'p':
		/* Validate that port is correct and a number, etc!! */
		args->port = atoi(arg);
		break;
	case 's':
		len = strlen(arg);
		args->tokenlist = malloc(len + 1);
		strcpy(args->tokenlist, arg);
		break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

struct server_arguments server_parseopt(int argc, char *argv[]) {
	struct server_arguments args;

	/* bzero ensures that "default" parameters are all zeroed out */
	bzero(&args, sizeof(args));

	struct argp_option options[] = {
		{ "port", 'p', "port", 0, "The port to be used for the server" ,0},
		{ "token list", 's', "tokenlist", 0, "The list of valid tokens if not provided default is tokens.txt", 0},
		{0}
	};
	struct argp argp_settings = { options, server_parser, 0, 0, 0, 0, 0 };
	if (argp_parse(&argp_settings, argc, argv, 0, NULL, &args) != 0) {
		printf("Got an error condition when parsing\n");
	}

	/* What happens if you don't pass in parameters? Check args values
	 * for sanity and required parameters being filled in */

    if(args.port == 0 ){
         perror("Missing Argument");
        exit(1);
    } 
	/* If they don't pass in all required settings, you should detect
	 * this and return a non-zero value from main */
    if(!args.tokenlist ){
		args.tokenlist = malloc(strlen("tokens.txt") + 1);
		strcpy(args.tokenlist, "tokens.txt");
        printf("Got port %d and default token list\n", args.port);
    }else{
        printf("Got port %d and token list %s\n", args.port, args.tokenlist);
    }
	
    return args;
}



ssize_t recv_all (int sock, void *buf, size_t len){
    size_t total = 0;
    while(total < len){
        ssize_t n = recv(sock, (char*)buf + total, len - total, 0);
        if (n <= 0) return n; 
        total += n;
    }
    return total;
}

ssize_t send_all (int sock, void *buf, size_t len){
    size_t total = 0;
    while(total < len){
        ssize_t n = send(sock, (char*)buf + total, len - total, 0);
        if (n <= 0) return n; 
        total += n;
    }
    return total;
}

void send_response(int fd, uint8_t status, uint8_t *payload, uint32_t plen) {
    uint32_t total = htonl(1 + plen);
    uint8_t  buf[MSGMAXLEN + 16];
    memcpy(buf, &total, 4);
    buf[4] = 0x04;
    buf[5] = 0x17;
    buf[6] = 0x9a;
    buf[7] = status;
    if (plen > 0 && payload) memcpy(buf + 8, payload, plen);
    send_all(fd, buf, 8 + plen);
}

//deliver a msg to a client chat/privmsg
void send_command(int fd, uint8_t cmd, uint8_t *payload, uint32_t plen) {
    uint32_t total = htonl(plen);  
    uint8_t  buf[MSGMAXLEN + 16];
    memcpy(buf, &total, 4);
    buf[4] = 0x04;
    buf[5] = 0x17;
    buf[6] = cmd;
    if (plen > 0 && payload) memcpy(buf + 7, payload, plen);
    send_all(fd, buf, 7 + plen);
}
 
void send_error(int fd, uint8_t status, char *msg) {
    send_response(fd, status, (uint8_t*)msg, strlen(msg));
}
 

//handles \nick command
void handle_nick(int idx, uint8_t *payload, uint32_t plen){
    int fd = clients[idx].fd;
 
    if (plen < 2) {
        send_error(fd, RSPERR, ERR);
        return;
    }
 
    uint8_t namelen = payload[0];
    if (namelen == 0 || plen < (uint32_t)(1 + namelen)) {
        send_error(fd, RSPERR, ERR);
        return;
    }
 
    char newname[NAMEMAXLEN];
    memcpy(newname, payload + 1, namelen);
    newname[namelen] = '\0';
 
    if (name_taken(newname, idx)) {
        send_error(fd, RSPERR, NAMETAKENMSG);
        return;
    }
 
    strncpy(clients[idx].name, newname, NAMEMAXLEN - 1);
    send_response(fd, RSPOK, NULL, 0);
}

//nickname helpers

//checks if desired name is in use if it is returns 1 else 0
int name_taken(char *name, int curr_idx){
    for(int i = 0; i < MAXCLIENT; i++){
        if(i == curr_idx){
            continue;
        }
        if(clients[i].fd >=0 && strcmp(clients[i].name,name) == 0){
            return 1;
        }
    }
    return 0;
}

//this is the default name generator it starts with rand0 checks 
//if that is available and increases n till randn is available
void assign_name(int idx){
    char name[NAMEMAXLEN] = "rand0";
    int n = 0;

    while(name_taken(name,idx)){
        snprintf(name,sizeof(name),"rand%d",++n);
    }

    strncpy(clients[idx].name, name, NAMEMAXLEN - 1);
    clients[idx].name[NAMEMAXLEN - 1] = '\0';
}

//handles \list users
void handle_listu(int idx) {
    int fd = clients[idx].fd;
    int room = clients[idx].room;

    uint8_t  payload[MAXCLIENT * (NAMEMAXLEN + 1)];
    uint32_t plen = 0;

    for(int i = 0; i < MAXCLIENT; i++){
        //if not active user or nor authed user skip
        if (clients[i].fd < 0 || !clients[i].authed) {
            continue;
        }
        //if in a room skip users not in current room
        if (room >= 0 && clients[i].room != room) {
            continue;
        }

        //add name sizes and names to buffer
        uint8_t nlen = strlen(clients[i].name);
        payload[plen++] = nlen;
        memcpy(payload + plen, clients[i].name, nlen);
        plen += nlen;
       
    }

    send_response(fd, RSPOK, payload, plen);
}

//handles \list rooms
void handle_listr(int idx){
    int fd = clients[idx].fd;
 
    uint8_t  payload[MAXROOMS * (ROOMMAXLEN + 1)];
    uint32_t plen = 0;
 
    for(int i = 0; i < roomnum; i++) {
        //if the room is not currently active skip it 
        if (!rooms[i].active) {
            continue;
        } 
        uint8_t nlen = strlen(rooms[i].name);
        payload[plen++] = nlen;
        memcpy(payload + plen, rooms[i].name, nlen);
        plen += nlen;
    }
 
    send_response(fd, RSPOK, payload, plen);

}

//handles \join
void handle_join(int idx, uint8_t *payload, uint32_t plen){
    int fd = clients[idx].fd;
 
    if (plen < 1) {
        send_error(fd, RSPCTXERR, ERR);
        return;
    }
 
    uint8_t roomlen = payload[0];
    if (plen < (uint32_t)(1 + roomlen)) {
        send_error(fd, RSPCTXERR, ERR);
        return;
    }
 
    char roomname[ROOMMAXLEN];
    memcpy(roomname, payload + 1, roomlen);
    roomname[roomlen] = '\0';
 
    char password[PASSMAXLEN] = "";
    uint32_t off = 1 + roomlen;
    if (plen > off) {
        uint8_t passlen = payload[off++];
        if (passlen > 0 && plen >= off + passlen) {
            memcpy(password, payload + off, passlen);
            password[passlen] = '\0';
        }
    }
 
    
    //see if a room exists if no create it
    int room_idx = -1;
    for (int i = 0; i < roomnum; i++) {
        if (rooms[i].active && strcmp(rooms[i].name, roomname) == 0) {
            room_idx = i;
            break;
        }
    }
 
    if (room_idx < 0) {
        //create a new room
        
        // try to reuse inactive slot
        for (int i = 0; i < roomnum; i++) {
            if (!rooms[i].active) {
                room_idx = i;
                break;
            }
        }

        if (room_idx < 0) {
            if (roomnum >= MAXROOMS) {
                send_error(fd, RSPERR, ERR);
                return;
            }
        room_idx = roomnum++;
        }
      
        strncpy(rooms[room_idx].name, roomname, ROOMMAXLEN - 1);
        strncpy(rooms[room_idx].password, password, PASSMAXLEN - 1);
        rooms[room_idx].active = 1;
    } else {
        //check if password matches
        if (strcmp(rooms[room_idx].password, password) != 0) {
            send_error(fd, RSPERR, WRONGROOMPASS);
            return;
        }

    }

    //see if you are already in the room 
    //if so throw error
    if (clients[idx].room == room_idx) {
        send_error(fd, RSPERR, JOINROOMALREADYIN);
        return;
    }
    
    //if already in a room leave it first
    if (clients[idx].room >= 0) {
        int old = clients[idx].room;

        rooms[old].num_members--;

        if (rooms[old].num_members == 0) {
            rooms[old].active = 0;
            rooms[old].name[0] = '\0';
            rooms[old].password[0] = '\0';
        }
     }
 
    clients[idx].room = room_idx;
    rooms[room_idx].num_members++;
    send_response(fd, RSPOK, NULL, 0);
}

//handles chat msgs from within rooms
void handle_chat(int idx, uint8_t *payload, uint32_t plen){
    int fd = clients[idx].fd;
 
    if (clients[idx].room < 0) {
        send_error(fd, RSPERR, NOROOMMSG);
        return;
    }
    if (plen < 1) {
        send_error(fd, RSPCTXERR, ERR);
        return;
    }
 
    uint8_t  in_roomlen = payload[0];
    uint32_t off = 1 + in_roomlen + 1; 
 
    if (plen < off + 1) {
        send_error(fd, RSPCTXERR, ERR);
        return;
    }
 
    uint16_t msglen = payload[off];
    off += 1;
 
    if (plen < off + msglen) {
        send_error(fd, RSPCTXERR, ERR);
        return;
    }
 
    uint8_t *msg  = payload + off;
    int room = clients[idx].room;
 
    //build packet
    uint8_t  outbuf[MSGMAXLEN + NAMEMAXLEN + ROOMMAXLEN + 16];
    uint32_t olen = 0;

    
    uint8_t roomlen = strlen(rooms[room].name);
    outbuf[olen++] = roomlen;
    memcpy(outbuf + olen, rooms[room].name, roomlen);
    olen += roomlen;

  
    uint8_t userlen = strlen(clients[idx].name);
    outbuf[olen++] = userlen;
    memcpy(outbuf + olen, clients[idx].name, userlen);
    olen += userlen;

   
    outbuf[olen++] = 0x00;
    outbuf[olen++] = msglen;

    memcpy(outbuf + olen, msg, msglen);
    olen += msglen;
 
    //send to everyone in room
    for (int i = 0; i < MAXCLIENT; i++) {
        if (clients[i].fd >= 0 && clients[i].authed && clients[i].room == room && i !=idx)
            send_command(clients[i].fd, CHATMSG, outbuf, olen);
    }
 
    send_response(fd, RSPOK, NULL, 0);
}

//handles private messages 
void handle_privmsg(int idx, uint8_t *payload, uint32_t plen){
    int fd = clients[idx].fd;
 
    if (plen < 1) {
        send_error(fd, RSPCTXERR, ERR);
        return;
    }
 
    uint8_t  userlen = payload[0];
    uint32_t off = 1 + userlen + 1; 
 
    if (plen < off + 1) {
        send_error(fd, RSPCTXERR, ERR);
        return;
    }
 
    char target[NAMEMAXLEN];
    memcpy(target, payload + 1, userlen);
    target[userlen] = '\0';
 
    uint8_t msglen = payload[off++];

    if (plen < off + msglen) {
        send_error(fd, RSPCTXERR, ERR);
        return;
    }
    uint8_t *msg = payload + off;
 
    //find targeted client
    int target_idx = -1;
    for (int i = 0; i < MAXCLIENT; i++) {
        if (clients[i].fd >= 0 && clients[i].authed &&
            strcmp(clients[i].name, target) == 0) {
            target_idx = i;
            break;
        }
    }
 
    if (target_idx < 0) {
        send_error(fd, RSPERR, NOUSERMSG);
        return;
    }
 
    //send msg
    uint8_t  outbuf[NAMEMAXLEN + MSGMAXLEN + 8];
    uint32_t olen = 0;
    uint8_t  senderlen = strlen(clients[idx].name);
 
    outbuf[olen++] = senderlen;
    memcpy(outbuf + olen, clients[idx].name, senderlen);
    olen += senderlen;
    outbuf[olen++] = 0x00;
    outbuf[olen++] = msglen;
    memcpy(outbuf + olen, msg, msglen);
    olen += msglen;
 
    send_command(clients[target_idx].fd, PRIVMSG, outbuf, olen);
    send_response(fd, RSPOK, NULL, 0);
}

//handles TCP part of connection
void handle_new_connection(int sock){
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int cfd = accept(sock, (struct sockaddr*)&caddr, &clen);
    if (cfd < 0) { 
        perror("accept failed"); 
        return; 
    }
 
    
    int slot = -1;
    for (int i = 0; i < MAXCLIENT; i++) {
        if (clients[i].fd < 0) { slot = i; break; }
    }
    if (slot < 0) { close(cfd); return; }
 
    clients[slot].fd = cfd;
    clients[slot].room = -1;
    clients[slot].authed = 0;
    clients[slot].name[0] = '\0';
}

//handles \disconnect command
void disconnect_client(int idx){
    
    //if somehow already disconnected do nothing
    if (clients[idx].fd < 0) {
        return;
    }
        
    //close connection set all settings to zero/default
    close(clients[idx].fd);

    

    //if you are only person in the room shut it down
    if(clients[idx].room >= 0){
        int room = clients[idx].room;
        if(rooms[room].num_members == 1){
            rooms[room].active = 0;
            rooms[room].name[0] = '\0';
            rooms[room].password[0] = '\0';
            rooms[room].num_members = 0;
        } else{
            rooms[room].num_members--;
        }
    }

    clients[idx].fd = -1;
    clients[idx].authed =  0;
    clients[idx].room = -1;
    clients[idx].name[0] = '\0';
}

//handles \connect command
void handle_connect(int idx, uint8_t *payload, uint32_t plen){
    int fd = clients[idx].fd;
 
    if (plen < 1 || payload[0] != CONNECTB) {
        send_error(fd, RSPERR, TOKENAUTHFAIL);
        disconnect_client(idx);
        return;
    }
 
    //know that after 0x29 is payload with CONNECTMSG + auth token
    char    *data = (char*)(payload + 1);
    uint32_t dlen = plen - 1;
 
    //find +
    uint32_t challenge_len = strlen(CONNECTMSG);
    if (dlen < challenge_len + 1) {
        send_error(fd, RSPERR, TOKENAUTHFAIL);
        disconnect_client(idx);
        return;
    }

    /* byte right after challenge is the token length */
    uint8_t token_len = (uint8_t)data[challenge_len];
    char *token_start = data + challenge_len + 1;

    if (challenge_len + 1 + token_len > dlen || token_len >= MAXTOKEN) {
        send_error(fd, RSPERR, TOKENAUTHFAIL);
        disconnect_client(idx);
        return;
    }
 
    char token_buf[MAXTOKEN];
    if (token_len >= MAXTOKEN) {
        send_error(fd, RSPERR, TOKENAUTHFAIL);
        disconnect_client(idx);
        return;
    }
    memcpy(token_buf, token_start, token_len);
    token_buf[token_len] = '\0';
 
    if (!valid_token(token_buf)) {
        send_error(fd, RSPERR, TOKENAUTHFAIL);
        disconnect_client(idx);
        return;
    }
 
    //given valid token and msg give default name
    clients[idx].authed = 1;
    assign_name(idx);

    uint8_t  resp[NAMEMAXLEN + 2];
    uint32_t rlen = 0;
    resp[rlen++] = RSPOK;
    uint8_t namelen = strlen(clients[idx].name);
    memcpy(resp + rlen, clients[idx].name, namelen);
    rlen += namelen;

    send_command(fd, 0x9a, resp, rlen);
}
//handles client commands 
void handle_client(int idx){
    int fd = clients[idx].fd;
 
    //read prefix
    uint8_t lenbuf[4];
    if (recv_all(fd, lenbuf, 4) <= 0) {
        disconnect_client(idx);
        return;
    }
    uint32_t msglen = ((uint32_t)lenbuf[0] << 24) | ((uint32_t)lenbuf[1] << 16) |
                      ((uint32_t)lenbuf[2] <<  8) |  (uint32_t)lenbuf[3];
 
    if (msglen > MSGMAXLEN + 10) {
        disconnect_client(idx);
        return;
    }
 
    uint8_t buf[MSGMAXLEN + 16];
    if (recv_all(fd, buf, msglen+3) <= 0) {
        disconnect_client(idx);
        return;
    }
 
 
    //every msg seesm to start with 0417
    if (buf[0] != 0x04 || buf[1] != 0x17) {
        disconnect_client(idx);
        return;
    }
 
    uint8_t  cmd = buf[2];
    uint8_t *payload = buf + 3;
    uint32_t plen = msglen;
 
    //used to ensure unauthed clients only send \connect
    if (!clients[idx].authed) {
        if (cmd == CONNECTA)
            handle_connect(idx, payload, plen);
        else
            disconnect_client(idx);
        return;
    }
 
    switch (cmd) {
        case NICK:    
            handle_nick(idx, payload, plen);    
            break;
        case JOIN:    
            handle_join(idx, payload, plen);    
            break;
        case LEAVE:   
            handle_leave(idx);                  
            break;
        case LISTU:    
            handle_listu(idx);             
            break;
        case LISTR:   
            handle_listr(idx);             
            break;
        case CHATMSG: 
            handle_chat(idx, payload, plen);    
            break;
        case PRIVMSG: 
            handle_privmsg(idx, payload, plen); 
            break;
        case ALIVE:   
            //timer already reset at the begining of the func  
            break;
        default:
            send_error(fd, RSPCTXERR, ERR);
            break;
    }
}


//handles \leave command
void handle_leave(int idx){
    int fd  = clients[idx].fd;

    //if not in room leave server
    if (clients[idx].room < 0){
        send_response(fd, RSPOK, NULL, 0);
        disconnect_client(idx);
        return;
    }

    //if in room leave room
    int room = clients[idx].room;

    //if you are only person in the room shut it down
    if(rooms[room].num_members == 1){
        rooms[room].active = 0;
        rooms[room].name[0] = '\0';
        rooms[room].password[0] = '\0';
    }
    
    clients[idx].room = -1;
    rooms[room].num_members--;
    send_response(fd, RSPOK, NULL, 0);
}



void load_tokens(char* filename){
    FILE* fptr = fopen(filename,"r");

    if (fptr == NULL) {
        perror("Could not open file");
        return;
    }

    char curr_line[NAMEMAXLEN];
    while (fgets(curr_line, sizeof(curr_line), fptr) && tokennum < MAXTOKEN) {
        curr_line[strcspn(curr_line, "\r\n")] = '\0';
        if (strlen(curr_line) > 0){
            strcpy(tokens[tokennum++], curr_line);
        }
    }
            
    fclose(fptr);
}

int valid_token(char *token){
    for(int i = 0; i < tokennum; i++){
    if(strcmp(tokens[i],token) == 0){
        return 1;
    }
}
return 0;
}


int main (int argc, char *argv[]){
    int sock;
    struct server_arguments args = server_parseopt(argc, argv);

    load_tokens(args.tokenlist);

    for(int i = 0; i < MAXCLIENT; i++){
        clients[i].fd = -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0){
        perror("Socket failed");
        exit(1);
    }

    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(args.port);

    if (bind(sock, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0){
         perror("Bind failed");
        exit(1);
    }

    if(listen(sock,MAXPEND) < 0){
        perror("listen failed");
        exit(1);
    }
    
    while (1) {
        struct pollfd fds[MAXCLIENT + 1];
        fds[0].fd = sock;
        fds[0].events = POLLIN;
        int nfds = 1;
 
        for (int i = 0; i < MAXCLIENT; i++) {
            if (clients[i].fd >= 0) {
                fds[nfds].fd = clients[i].fd;
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }
 
        
        int ready = poll(fds, nfds, 1000);
        if (ready < 0) { 
            perror("poll"); 
            break; 
        }

        if (ready == 0) continue;
 
        //check for new connection
        if (fds[0].revents & POLLIN)
            handle_new_connection(sock);
 
        //check for data from exisiting clients 
        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                for (int j = 0; j < MAXCLIENT; j++) {
                    if (clients[j].fd == fds[i].fd) {
                        handle_client(j);
                        break;
                    }
                }
            }
        }
    }
 
    close(sock);
    free(args.tokenlist);
    return 0;
}