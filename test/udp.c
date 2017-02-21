#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event.h>
#include <event2/listener.h>
 
#define SVR_IP                          "127.0.0.1"
#define SVR_PORT                        10000
#define BUF_SIZE                        1024
 
void read_cb(int fd, short event, void *arg) {
    char                buf[BUF_SIZE];
    int                 len;
    int                 size = sizeof(struct sockaddr);
    struct sockaddr_in  client_addr;
 
    memset(buf, 0, sizeof(buf));
    len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &size);
 
    if (len == -1) {
        perror("recvfrom()");
    } else if (len == 0) {
        printf("Connection Closed\n");
    } else {
        printf("Read: len [%d] - content [%s]\n", len, buf);
         
        /* Echo */
        sendto(fd, buf, len, 0, (struct sockaddr *)&client_addr, size);
    }
}
 
int bind_socket(struct event *ev) {
    int                 sock_fd;
    int                 flag = 1;
    struct sockaddr_in  sin;
 
    /* Create endpoint */
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
 
    /* Set socket option */
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) < 0) {
        perror("setsockopt()");
        return 1;
    }
 
    /* Set IP, port */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(SVR_IP);
    sin.sin_port = htons(SVR_PORT);
 
    /* Bind */
    if (bind(sock_fd, (struct sockaddr *)&sin, sizeof(struct sockaddr)) < 0) {
        perror("bind()");
        return -1;
    } else {
        printf("bind() success - [%s] [%u]\n", SVR_IP, SVR_PORT);
    }
 
    /* Init one event and add to active events */
    event_set(ev, sock_fd, EV_READ | EV_PERSIST, &read_cb, NULL);
    if (event_add(ev, NULL) == -1) {
        printf("event_add() failed\n");
    }
 
    return 0;
}
 
int main(void) {
    struct event  ev;
 
    /* Init. event */
    if (event_init() == NULL) {
        printf("event_init() failed\n");
        return -1;
    }
 
    /* Bind socket */
    if (bind_socket(&ev) != 0) {
        printf("bind_socket() failed\n");
        return -1;
    }
 
    /* Enter event loop */
    event_dispatch();
 
    return 0;
}