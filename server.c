/**
 * Multithreaded, libevent 2.x-based socket server.
 * Copyright (c) 2012 Qi Huang
 * This software is licensed under the BSD license.
 * See the accompanying LICENSE.txt for details.
 *
 * To compile: ./make
 * To run: ./echoserver_threaded
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <signal.h>

#include "workqueue.h"
#include "shared.h"
#include "sophia.h"


/* Port to listen on. */
#define SERVER_PORT 11211
/* Connection backlog (# of backlogged connections to accept). */
#define CONNECTION_BACKLOG 120
/* Number of worker threads.  Should match number of CPU cores reported in 
 * /proc/cpuinfo. */
#define NUM_THREADS 4

/**
 * Struct to carry around connection (client)-specific data.
 */
typedef struct client {
    /* The client's socket. */
    int fd;

    /* The event_base for this client. */
    struct event_base *evbase;

    /* The bufferedevent for this client. */
    struct bufferevent *buf_ev;

    /* The output buffer for this client. */
    struct evbuffer *output_buffer;

    /* Here you can add your own application-specific attributes which
     * are connection-specific. */
    int command;
} client_t;

static struct event_base *evbase_accept;
static workqueue_t workqueue;

/* Signal handler function (defined below). */
static void sighandler(int signal);

static void closeClient(client_t *client) {
    if (client != NULL) {
        if (client->fd >= 0) {
            close(client->fd);
            client->fd = -1;
        }
    }
}

static void closeAndFreeClient(client_t *client) {
    if (client != NULL) {
        closeClient(client);
        if (client->buf_ev != NULL) {
            bufferevent_free(client->buf_ev);
            client->buf_ev = NULL;
        }
        if (client->evbase != NULL) {
            event_base_free(client->evbase);
            client->evbase = NULL;
        }
        if (client->output_buffer != NULL) {
            evbuffer_free(client->output_buffer);
            client->output_buffer = NULL;
        }
        free(client);
    }
}


/* Count the total occurrences of 'str' in 'buf'. */
int count_instances(struct evbuffer *buf, const char *str)
{
    size_t len = strlen(str);
    int total = 0;
    struct evbuffer_ptr p;

    if (!len)
        /* Don't try to count the occurrences of a 0-length string. */
        return -1;

    evbuffer_ptr_set(buf, &p, 0, EVBUFFER_PTR_SET);

    while (1) {
         p = evbuffer_search(buf, str, len, &p);
         if (p.pos < 0)
             break;
         total++;
         evbuffer_ptr_set(buf, &p, 1, EVBUFFER_PTR_ADD);
    }

    return total;
}

size_t str_firstpos(struct evbuffer *buf, const char *str)
{
    size_t len = strlen(str);
    struct evbuffer_ptr p;

    if (!len)
        /* Don't try to count the occurrences of a 0-length string. */
        return -1;

    p = evbuffer_search(buf, str, len, NULL);
    return p.pos;
}

void write_msg(struct bufferevent *buf_event, struct client *client, const char *msg) {
    evbuffer_add(client->output_buffer, msg, sizeof(msg));
    //flush buffer
    if (bufferevent_write_buffer(buf_event, client->output_buffer)) {
        errorOut("Error sending data to client on fd %d\n", client->fd);
        closeClient(client);
    }
}


void buffered_on_read_new(struct bufferevent *bev, void *arg) {
    client_t *client = (client_t *)arg;
    //char resp[8] = {'S','T','O','R','E','D',13,10};
    static char msg_fmt[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Connection: keep-alive\r\n"
        "Keep-Alive: 1;timeout=9\r\n"
        "\r\n"
        "%s";
    //char *response;
    struct evbuffer *input;

    input = bufferevent_get_input(bev);
    int len = evbuffer_get_length(input);
    char *val = "OK";
    int size = strlen(val);

    int resp_size = (strlen(msg_fmt) -2*2/* %s exclude */) + size + get_int_len(size) + 1/* \0*/;
    char *resp = malloc(resp_size);
    snprintf(resp, resp_size,msg_fmt,size,val);
    //INFO_OUT("resp:'%.*s'\n", resp_size,resp);
    evbuffer_drain(input, len);

    evbuffer_add(client->output_buffer, resp, resp_size -1);
    if (bufferevent_write_buffer(bev, client->output_buffer)) {
        errorOut("Error sending data to client on fd %d\n", client->fd);
        closeClient(client);
    }
    //bufferevent_write_buffer(bev, client->output_buffer);

    /*
    //char data[len];
    char *data;
    data = malloc(len + 1);
    data[len] = '\0';

    if (!data) {
        //out of memory error
        closeClient(client);
        return;
    }

    evbuffer_copyout(input, data, len);
    
    INFO_OUT("request:'%.*s'\n", (int)len,data);

    int all_handled_len = 0;
    char *data_ptr = data;
    char* resp_ptr;
    int resp_len;
    int handled_len = handle_read(data_ptr, len, &resp_ptr, &resp_len);
    while (handled_len > 0) {
        all_handled_len += handled_len;
        len -= handled_len;
        INFO_OUT("response:'%.*s' strlen:%d\n", resp_len, resp_ptr, resp_len);
    
        //evbuffer_add(client->output_buffer, resp, sizeof(resp));
        evbuffer_add(client->output_buffer, resp_ptr, resp_len);

        // Send the results to the client.  This actually only queues the results
        // for sending. Sending will occur asynchronously, handled by libevent. 
        if (bufferevent_write_buffer(bev, client->output_buffer)) {
            errorOut("Error sending data to client on fd %d\n", client->fd);
            closeClient(client);
            break;
        }
        free(resp_ptr);

        if (len > 0) {
            handled_len = handle_read(data + all_handled_len * sizeof(char), len, &resp_ptr, &resp_len);
        } else {
            // no more data in inpud buffer
            break;
        }
    }

    if (all_handled_len) {
        evbuffer_drain(input, all_handled_len);
    }

    free(data);
    */
}

/**
 * Called by libevent when there is data to read.
 */
void buffered_on_read_old(struct bufferevent *bev, void *arg) {
    client_t *client = (client_t *)arg;
    char data[4096];
    char resp[8] = {'S','T','O','R','E','D',13,10};    
    int nbytes;

    /* If we have input data, the number of bytes we have is contained in
     * bev->input->off. Copy the data from the input buffer to the output
     * buffer in 4096-byte chunks. There is a one-liner to do the whole thing
     * in one shot, but the purpose of this server is to show actual real-world
     * reading and writing of the input and output buffers, so we won't take
     * that shortcut here. */
    struct evbuffer *input;
    input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) > 0) {
        /* Remove a chunk of data from the input buffer, copying it into our
         * local array (data). */
        nbytes = evbuffer_remove(input, data, 4096); 
        //printf("data:%.*s\n",nbytes,data);
        //printf("resp:%.*s\n",8,resp);
        /* Add the chunk of data from our local array (data) to the client's
         * output buffer. */
        //evbuffer_add(client->output_buffer, data, nbytes);
        evbuffer_add(client->output_buffer, resp, 8);
    }

    /* Send the results to the client.  This actually only queues the results
     * for sending. Sending will occur asynchronously, handled by libevent. */
    if (bufferevent_write_buffer(bev, client->output_buffer)) {
        errorOut("Error sending data to client on fd %d\n", client->fd);
        closeClient(client);
    }
}

/**
 * Called by libevent when there is data to read.
 */
void read_cb(struct bufferevent *bev, void *arg)
{
    client_t *client = (client_t *)arg;
    struct evbuffer *input;
    input = bufferevent_get_input(bev);
    INFO_OUT("read cb\n");
Start:;
    INFO_OUT("start\n");
    int len = evbuffer_get_length(input);
    if (len < 6) return;// minimal command: 'quit\r\n'
    char *data;
    data = malloc(len + 1);
    data[len] = '\0';

    if (!data) {
        //out of memory error
        ERROR_OUT("out of memory error\n")
        closeClient(client);
        return;
    }

    evbuffer_copyout(input, data, len);
    //quit\r\n
    if (strstr(data,"quit\r\n")) {
        ERROR_OUT("quit\n")
        free(data);
        evbuffer_drain(input, len);
        //closeClient(client);
        return;
    }
    INFO_OUT("fd=%u, input:'%s'\n",client->fd, data);
    //set key 0 0 1
    if (!memcmp("set ",data,4)) {
        INFO_OUT("set parse\n");
        //int shift = 0;//shift data pointer
        //check on \n
        char *nl = strstr(data,"\r\n");
        if (!nl) {
            free(data);
            return; 
        }
        int command_len = (int)(nl - data);
        INFO_OUT("command_len:%d\n",command_len);
        //check on second \n
        data+=command_len+2;
        if (!strstr(data,"\r\n")) {
            //no second new line
            data-=command_len+2;
            INFO_OUT("terminated set\n");
            free(data);
            return;
        }
        data-=command_len+2;
        INFO_OUT("lets parse data:'%s'\n",data);
        data+=4;//'set '
        char *space = (char*) memchr(data,' ',len);
        if (!space) {
            data-=4;
            evbuffer_drain(input, len);
            ERROR_OUT("Error parsing key\n");
            free(data);
            return;
        }
        int key_len = (space-data);
        char *key;
        key = malloc(key_len);
        memcpy(key,data,key_len);
        INFO_OUT("key:'%.*s'\n",key_len,key);
        data-=4;

        //find val size
        char *command = malloc(command_len+1);
        command[command_len] = '\0';
        memcpy(command,data,command_len);
        INFO_OUT("command:'%.*s'\n",command_len,command);
        char *val_sizestr = strrchr(command, ' ');
        val_sizestr+=1;//' '
        INFO_OUT("val_sizestr:'%.*s' %d\n",(int)(command - val_sizestr),val_sizestr,(int)(command - val_sizestr));
        int val_size = strtol(val_sizestr,NULL,10);
        INFO_OUT("val_size:%d\n",val_size);
        free(command);

        //check parsing
        int drain_size = val_size+2+command_len+2;
        if (len<drain_size) {
            //not all value
            free(key);
            free(data);
            INFO_OUT("terminated set (value)\n");
            return;
        }

        if (val_size == 0) {
            evbuffer_add(client->output_buffer, ST_NOTSTORED, strlen(ST_NOTSTORED));
            ERROR_OUT("Error parsing value\n");
            drain_size = len;
        }
        else {
            char *val = malloc(val_size);
            if (!val) {
                ERROR_OUT("Error out of memory\n");
                free(key);
                free(data);
                closeClient(client);
                return;
            }
            data+=command_len+2;
            memcpy(val,data,val_size);
            data-=command_len+2;
            void *o = sp_document(db);
            sp_setstring(o, "key", &key[0], key_len);
            sp_setstring(o, "value", &val[0], val_size);
            int res = sp_set(db, o);
            free(val);
            if (res == 0) {
                evbuffer_add(client->output_buffer, ST_STORED, strlen(ST_STORED));
            }
            else {
                evbuffer_add(client->output_buffer, ST_NOTSTORED, strlen(ST_NOTSTORED));
            }
        }
        
        free(key);
        free(data);

        evbuffer_drain(input, drain_size);
        if (bufferevent_write_buffer(bev, client->output_buffer)) {
            errorOut("Error sending data to client on fd %d\n", client->fd);
            closeClient(client);
        }
        
        goto Start;
    }

    //get key\r\n
    if (!memcmp("get ",data,4)) {
        int shift = 0;//shift data pointer
        //check on \n
        char *nl = strstr(data,"\r\n");
        if (!nl) {
            ERROR_OUT("terminated get\n")
            free(data);
            return; 
        }
        int drain_size = (int)(nl - data) + 2;//"\r\n\r\n"
        INFO_OUT("lets parse get\r");
        shift = 4;//'get '
        data+=shift;
        char *rchar = (char*) memchr(data,'\r',len);
        if (!rchar) {
            data-=shift;
            ERROR_OUT("terminated get no r\n")
            free(data);
            return;
        }
        int key_len = (rchar-data);
        char *key;
        key = malloc(key_len+1);
        key[key_len] = '\0';
        memcpy(key,data,key_len);
        INFO_OUT("key:'%.*s' %d\n",key_len,key,key_len);

        void *o = sp_document(db);
        sp_setstring(o, "key", &key[0], key_len);
        o = sp_get(db, o);
        char *val;
        int size;
        if (o) {
            char *ptr = sp_getstring(o, "value", &size);
            val = strndup_p((char*)ptr,size);
            sp_destroy(o);
            INFO_OUT("val:'%.*s'\n", size,val);
            char *format = "VALUE %s 0 %d\r\n%s\r\nEND\r\n";
            int resp_size = (strlen(format) -3*2/* % exclude */) + key_len + size + get_int_len(size) + 1/* \0*/;
            char *resp = malloc(resp_size);
            snprintf(resp, resp_size,format,key,size,val);
            INFO_OUT("resp:'%.*s' % d\n", resp_size,resp,resp_size);
            evbuffer_add(client->output_buffer, resp, strlen(resp));
            free(val);
            free(resp);
        }
        else {
            evbuffer_add(client->output_buffer, ST_END, strlen(ST_END));
            ERROR_OUT("no key\n")
        }
        free(key);
        
        data-=shift;
        free(data);
        evbuffer_drain(input, drain_size);
        if (bufferevent_write_buffer(bev, client->output_buffer)) {
            ERROR_OUT("Error sending data to client on fd %d\n", client->fd);
            closeClient(client);
        }
        
        goto Start;
    }
    if (!memcmp("GET /",data,5)) {
        char *double_nl = strstr(data,"\r\n\r\n");
        if (!double_nl) {
            free(data);
            return; 
        }
        int drain_size = (int)(double_nl - data) + 4;//"\r\n\r\n"
        static char msg_ok2[] = "123";
        static char msg_ok[] =
        "HTTP/1.1 200 OK\r\n"
        "Connection: Keep-Alive\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Keep-Alive: timeout=10, max=20\r\n"
        "Server: okdb/0.0.1\r\n"
        "\r\n%s";
        int shift = 5;//'GET /'
        data+=shift;

        size_t cmdend = strcspn(data, " \r\n");
        char *key = strndup_p(data, cmdend);
        INFO_OUT("key:%s\n", key);

        char *resp;
        int key_len = strlen(key);
        if (!key_len) {
            //empty key
            int resp_size = ((sizeof(msg_ok)) -2*2/* % exclude */) + strlen(ST_OK) + get_int_len(strlen(ST_OK));
            resp = malloc(resp_size);
            //resp[resp_size] = '\0';
            snprintf(resp, resp_size,msg_ok, strlen(ST_OK),ST_OK);
            INFO_OUT("resp:'%.*s'\n", resp_size-1,resp);
            evbuffer_add(client->output_buffer, resp, resp_size-1);
            free(resp);
        }
        else {
            void *o = sp_document(db);
            sp_setstring(o, "key", &key[0], key_len);
            o = sp_get(db, o);
            char *val;
            int size;
            if (o) {
                char *ptr = sp_getstring(o, "value", &size);
                val = strndup_p((char*)ptr,size);
                sp_destroy(o);

                INFO_OUT("val:'%.*s'\n", size,val);
                int resp_size = ((sizeof(msg_ok)) -2*2/* % exclude */) + size + get_int_len(size);
                char *resp = malloc(resp_size);
                //resp[resp_size] = '\0';
                snprintf(resp, resp_size,msg_ok,size,val);
                INFO_OUT("resp:'%.*s'\n", resp_size-1,resp);
                evbuffer_add(client->output_buffer, resp, resp_size-1);
                free(val);
                free(resp);
            }
        }
        free(key);
        data-=shift;
        free(data);

        evbuffer_drain(input, drain_size);
        if (bufferevent_write_buffer(bev, client->output_buffer)) {
            errorOut("Error sending data to client on fd %d\n", client->fd);
            closeClient(client);
        }
        goto Start;
    }
    free(data);
    evbuffer_drain(input, len);
    evbuffer_add(client->output_buffer, ST_ERROR, strlen(ST_ERROR));
    if (bufferevent_write_buffer(bev, client->output_buffer)) {
        errorOut("Error sending data to client on fd %d\n", client->fd);
        closeClient(client);
    }
}

void buffered_on_read(struct bufferevent *bev, void *arg) {
    client_t *client = (client_t *)arg;
    char data[4096];
    char resp[8] = {'S','T','O','R','E','D',13,10};
    static char msg_ok[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "Connection: keep-alive\r\n"
        "Keep-Alive: 1;timeout=9\r\n"
        "\r\n"
        "OK";    
    int nbytes;

    /* If we have input data, the number of bytes we have is contained in
     * bev->input->off. Copy the data from the input buffer to the output
     * buffer in 4096-byte chunks. There is a one-liner to do the whole thing
     * in one shot, but the purpose of this server is to show actual real-world
     * reading and writing of the input and output buffers, so we won't take
     * that shortcut here. */
    struct evbuffer *input;
    input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) > 0) {
        /* Remove a chunk of data from the input buffer, copying it into our
         * local array (data). */
        nbytes = evbuffer_remove(input, data, 4096); 
        //printf("data:%.*s\n",nbytes,data);
        //printf("resp:%.*s\n",8,resp);
        /* Add the chunk of data from our local array (data) to the client's
         * output buffer. */
        //evbuffer_add(client->output_buffer, data, nbytes);
        evbuffer_add(client->output_buffer, msg_ok, sizeof(msg_ok));
    }

    /* Send the results to the client.  This actually only queues the results
     * for sending. Sending will occur asynchronously, handled by libevent. */
    if (bufferevent_write_buffer(bev, client->output_buffer)) {
        errorOut("Error sending data to client on fd %d\n", client->fd);
        closeClient(client);
    }
}

/**
 * Called by libevent when the write buffer reaches 0.  We only
 * provide this because libevent expects it, but we don't use it.
 */
void buffered_on_write(struct bufferevent *bev, void *arg) {
}

/**
 * Called by libevent when there is an error on the underlying socket
 * descriptor.
 */
void buffered_on_error(struct bufferevent *bev, short what, void *arg) {
    closeClient((client_t *)arg);
}

static void server_job_function(struct job *job) {
    client_t *client = (client_t *)job->user_data;

    event_base_dispatch(client->evbase);
    closeAndFreeClient(client);
    free(job);
}

/**
 * This function will be called by libevent when there is a connection
 * ready to be accepted.
 */
void on_accept(evutil_socket_t fd, short ev, void *arg) {
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    workqueue_t *workqueue = (workqueue_t *)arg;
    client_t *client;
    job_t *job;

    client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        warn("accept failed");
        ERROR_OUT("accept failed");
        return;
    }

    /* Set the client socket to non-blocking mode. */
    if (evutil_make_socket_nonblocking(client_fd) < 0) {
        warn("failed to set client socket to non-blocking");

        close(client_fd);
        return;
    }

    /* Create a client object. */
    if ((client = malloc(sizeof(*client))) == NULL) {
        warn("failed to allocate memory for client state");
        close(client_fd);
        return;
    }
    memset(client, 0, sizeof(*client));
    client->fd = client_fd;
    

    /* Add any custom code anywhere from here to the end of this function
     * to initialize your application-specific attributes in the client struct.
     */

     //client->command = NULL;

    if ((client->output_buffer = evbuffer_new()) == NULL) {
        warn("client output buffer allocation failed");
        closeAndFreeClient(client);
        return;
    }

    if ((client->evbase = event_base_new()) == NULL) {
        warn("client event_base creation failed");
        closeAndFreeClient(client);
        return;
    }

    /* Create the buffered event.
     *
     * The first argument is the file descriptor that will trigger
     * the events, in this case the clients socket.
     *
     * The second argument is the callback that will be called
     * when data has been read from the socket and is available to
     * the application.
     *
     * The third argument is a callback to a function that will be
     * called when the write buffer has reached a low watermark.
     * That usually means that when the write buffer is 0 length,
     * this callback will be called.  It must be defined, but you
     * don't actually have to do anything in this callback.
     *
     * The fourth argument is a callback that will be called when
     * there is a socket error.  This is where you will detect
     * that the client disconnected or other socket errors.
     *
     * The fifth and final argument is to store an argument in
     * that will be passed to the callbacks.  We store the client
     * object here.
     */
    client->buf_ev = bufferevent_socket_new(client->evbase, client_fd,
                                            BEV_OPT_CLOSE_ON_FREE);
    if ((client->buf_ev) == NULL) {
        warn("client bufferevent creation failed");
        closeAndFreeClient(client);
        return;
    }
    bufferevent_setcb(client->buf_ev, read_cb, buffered_on_write,
                      buffered_on_error, client);

    /* We have to enable it before our callbacks will be
     * called. */
    bufferevent_enable(client->buf_ev, EV_READ);

    /* Create a job object and add it to the work queue. */
    if ((job = malloc(sizeof(*job))) == NULL) {
        warn("failed to allocate memory for job state");
        closeAndFreeClient(client);
        return;
    }
    job->job_function = server_job_function;
    job->user_data = client;

    workqueue_add_job(workqueue, job);
}

/**
 * Run the server.  This function blocks, only returning when the server has 
 * terminated.
 */
int runServer(void) {
    evutil_socket_t listenfd;
    struct sockaddr_in listen_addr;
    struct event *ev_accept;
    int reuseaddr_on;

    /* Set signal handlers */
    sigset_t sigset;
    sigemptyset(&sigset);
    struct sigaction siginfo = {
        .sa_handler = sighandler,
        .sa_mask = sigset,
        .sa_flags = SA_RESTART,
    };
    sigaction(SIGINT, &siginfo, NULL);
    sigaction(SIGTERM, &siginfo, NULL);

    /* Create our listening socket. */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        err(1, "listen failed");
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(SERVER_PORT);
    reuseaddr_on = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on));
    if (bind(listenfd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) 
        < 0) {
        err(1, "bind failed");
        ERROR_OUT("bind failed\n");
    }
    if (listen(listenfd, CONNECTION_BACKLOG) < 0) {
        err(1, "listen failed");
        ERROR_OUT("listen failed\n");
    }
    /*
    
    struct evconnlistener *listener;
    listener = evconnlistener_new_bind(
        evbase, acceptcb, app_ctx, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
        16, rp->ai_addr, (int)rp->ai_addrlen);
    if (listener) {
      freeaddrinfo(res);

      return;
    }
    */
    

    /* Set the socket to non-blocking, this is essential in event
     * based programming with libevent. */
    if (evutil_make_socket_nonblocking(listenfd) < 0) {
        err(1, "failed to set server socket to non-blocking");
    }

    if ((evbase_accept = event_base_new()) == NULL) {
        perror("Unable to create socket accept event base");
        close(listenfd);
        return 1;
    }

    /* Initialize work queue. */
    if (workqueue_init(&workqueue, NUM_THREADS)) {
        perror("Failed to create work queue");
        close(listenfd);
        workqueue_shutdown(&workqueue);
        return 1;
    }

    /* We now have a listening socket, we create a read event to
     * be notified when a client connects. */
    ev_accept = event_new(evbase_accept, listenfd, EV_READ|EV_PERSIST,
                          on_accept, (void *)&workqueue);
    event_add(ev_accept, NULL);

    printf("Server running.\n");

    /* Start the event loop. */
    event_base_dispatch(evbase_accept);

    event_base_free(evbase_accept);
    evbase_accept = NULL;

    close(listenfd);

    printf("Server shutdown.\n");

    return 0;
}

/**
 * Kill the server.  This function can be called from another thread to kill
 * the server, causing runServer() to return.
 */
void killServer(void) {
    fprintf(stdout, "Stopping socket listener event loop.\n");
    if (event_base_loopexit(evbase_accept, NULL)) {
        perror("Error shutting down server");
    }
    fprintf(stdout, "Stopping workers.\n");
    workqueue_shutdown(&workqueue);
}

static void sighandler(int signal) {
    fprintf(stdout, "Received signal %d: %s.  Shutting down.\n", signal,
            strsignal(signal));
    killServer();
}

/* Main function for demonstrating the echo server.
 * You can remove this and simply call runServer() from your application. */
//int main(int argc, char *argv[]) {
//    return runServer();
//}
