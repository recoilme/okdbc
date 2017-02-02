#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "sophia/sophia.h"

const int LOGENABLED = 1;

void *env;
void *db;

#define INFO(...) {\
	if (LOGENABLED == 1) printf("\n%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
	if (LOGENABLED == 1) printf(__VA_ARGS__);\
}

static char 
*msg_ok =
    "HTTP/1.1 200 OK\r\n"
    "Connection: Keep-Alive\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "Content-Length: %d\r\n"
    "Keep-Alive: timeout=10, max=20\r\n"
    "Server: okdb/0.0.1\r\n"
    "\r\n%s";

static char 
*msg_notfound =
    "HTTP/1.1 404 Not Found\r\n"
    "Connection: Keep-Alive\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "Content-Length: %d\r\n"
    "Connection: close"
    "Server: okdb/0.0.1\r\n"
    "\r\n%s";

static char
http_get[] = "GET /";

static char
quit[] = "quit\r\n";

static char
cmd_get[] = "get ";

static char
cmd_set[] = "set ";

static char
st_stored[] = "STORED\r\n";

static char
st_notstored[] = "NOT_STORED\r\n";

static char
st_end[] = "END\r\n";

static char
http_end[] = "\r\n\r\n";

static char
nl[] = "\r\n";

static char 
ok[] = "OK\r\n";

static char 
not_found[] = "Not Found\r\n";

/*  Str must have at least len bytes to copy 
    Return NULL on error malloc
*/
static char *
make_str(const char *str, size_t len)
{
	char *newstr;

	newstr = malloc(len + 1);
	if(newstr == NULL) {
        perror("ERROR ALOCATE\n");
		return NULL;
	}

	memcpy(newstr, str, len);
	newstr[len] = 0;

	return newstr;
}

/* Must be positive */
int get_int_len (int value)
{
    int l=1;
    while(value>9){ l++; value/=10; }
    return l;
}

static void
close_connection(struct bufferevent *bev, void *ctx, const char *err)
{
    if (err) perror(err);
    free(ctx);
    bufferevent_free(bev);
}

static void
http_get_response(struct evbuffer *output, const char *format, const char *msg, int size)
{
    //INFO("msg:%s",msg);
    if (!msg) return;
    int resp_size = ((strlen(format)) -2*2/* % exclude */) + size + get_int_len(size) +1;
    //INFO("http_get_response size:%d", resp_size);        
    char *resp = malloc(resp_size);
    resp[resp_size] = '\0';
    snprintf(resp, resp_size, format, size, msg);
    //INFO("http_get_response:'%s' size:%d",resp, resp_size);
    evbuffer_add(output, resp, resp_size-1);
    free(resp);
}

static void
mc_get_response(struct evbuffer *output, const char *key,const char *value, int size)
{
    if (!value) {
        evbuffer_add(output, st_end, sizeof(st_end)-1);
        return;
    }
    char *format = "VALUE %s 0 %d\r\n%s\r\nEND\r\n";
    int resp_size = (strlen(format) -3*2) + strlen(key) + size + get_int_len(size) + 1;
    char *resp = malloc(resp_size);
    if (!resp) {
        perror("Out of memory");
        //TODO close connection or ignore?
        return;
    }
    snprintf(resp, resp_size,format,key,size,value);
    INFO("resp:'%.*s'\n", resp_size,resp);
    evbuffer_add(output, resp, resp_size-1);//remove \0
    return;
}

static char
*sophia_get(const char *key, int *value_size)
{
    /* get value from sophia */
    char *val = NULL;

    void *o = sp_document(db);
    sp_setstring(o, "key", &key[0], strlen(key));
    o = sp_get(db, o);
    
    if (o) {
        char *ptr = sp_getstring(o, "value", value_size);
        val = make_str((char*)ptr,*value_size);
        sp_destroy(o);

        INFO("val:'%.*s', size:%d\n", *value_size,val,*value_size);
    }
    else {
        INFO("key:'%s' not found\n",key);
    }
    return val;
}

static void
read_cb(struct bufferevent *bev, void *ctx)
{
    /* This callback is invoked when there is data to read on bev. */
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

CONTINUE_LOOP:;
    /* Try copy from evbuffer to buffer */
    size_t len = evbuffer_get_length(input);
    char *data = malloc(len+1);
    data[len] = '\0';

    /* Close connection if we dont may allocate buffer */
    if (!data) {
        close_connection(bev, ctx, "Out of memory");
        return;
    }

    evbuffer_copyout(input, data, len);

    /* Process quit (maybe anywhere in buffer) */
    if (strstr(data,quit)) {
        INFO("quit catched!\n");
        evbuffer_drain(input, len);
        free(data);
        close_connection(bev, ctx, NULL);
        return;
    }

    /* Check for set command */
    //TODO macros?
    char *cmdset_start = strstr(data,cmd_set);
    if (cmdset_start) {
        INFO("set catched!");
        int cmd_pos = (int)(cmdset_start - data);
        if (cmd_pos>0) {
            /* user may send some trash before command like:'trash GET /' - remove it */
            INFO("Drained:%d bytes",cmd_pos);
            evbuffer_drain(input, cmd_pos);
            free(data);
            goto CONTINUE_LOOP;
        }
        /* Find first nl in command */
        char *cmdset_end = strstr(data,nl);
        if (!cmdset_end) {
            /* no end in buffer - wait it in next packet */
            free(data);
            return; 
        }
        int cmd_len = (int)(cmdset_end - data) + (sizeof(nl)-1);

        /* extract key from buffer */
        data+=sizeof(cmd_set)-1;
        /* Find ' ' or /r/n in buffer */
        size_t key_end = strcspn(data, " \r\n");
        char *key = make_str(data, key_end);
        /* Move pointer back */
        data-=sizeof(cmd_set)-1;

        if (!key) {
            /* Error malloc */
            free(data);
            close_connection(bev, ctx, "Out of memory in key");
            return;
        }
        INFO("get key:'%s'\n", key);

        /* Lets find value size */
        char *command = make_str(data,cmd_len-(sizeof(nl)-1));
        INFO("command:'%s'\n",command);
        char *val_sizestr = strrchr(command, ' ');
        val_sizestr+=1;//' '
        int val_size = (int) strtol(val_sizestr,NULL,10);
        INFO("val_size:%d\n",val_size);
        free(command);

        /* We know value size, check buffer */
        int total_size = cmd_len+val_size+(sizeof(nl)-1);
        if (len<total_size) {
            /* buf not arrived, wait */
            free(data);
            free(key);
            return;
        }

        /* copy value from buffer */
        data+=cmd_len;//shift
        char *value = make_str(data,val_size);
        INFO("value:'%s'\n",value);

        /* Processing key/value */
        void *o = sp_document(db);
        sp_setstring(o, "key", &key[0], strlen(key));
        sp_setstring(o, "value", &value[0], val_size);
        int res = sp_set(db, o);
        if (res == 0) {
            evbuffer_add(output, st_stored, strlen(st_stored));
        }
        else {
            evbuffer_add(output, st_notstored, strlen(st_notstored));
        }

        /* command catched - send response */
        if (bufferevent_write_buffer(bev, output)) {
            /* error sending - closed? */
            free(value);
            free(key);
            data-=cmd_len;//unshift
            free(data);
            close_connection(bev, ctx, "Error sending data to client");
            return;
        }
        free(value);
        free(key);
        data-=cmd_len;//unshift

        /* free processed command buffer */
        evbuffer_drain(input, total_size);

        free(data);
        /* client may send multiple commands in one buffer so continue processing */
        goto CONTINUE_LOOP;
    }

    /* Check for get command */
    char *cmdget_start = strstr(data,cmd_get);
    if (cmdget_start) {
        INFO("get catched!");
        int cmd_pos = (int)(cmdget_start - data);
        if (cmd_pos>0) {
            /* user may send some trash before command like:'trash GET /' - remove it */
            INFO("Drained:%d bytes",cmd_pos);
            evbuffer_drain(input, cmd_pos);
            free(data);
            goto CONTINUE_LOOP;
        }
        /* Find end command */
        char *cmdget_end = strstr(data,nl);
        if (!cmdget_end) {
            /* no end in buffer - wait it in next packet */
            free(data);
            return; 
        }
        int cmd_len = (int)(cmdget_end - data) + (sizeof(nl)-1);

        /* free processed command buffer */
        evbuffer_drain(input, cmd_len);

        /* extract key from buffer */
        data+=sizeof(cmd_get)-1;
        /* Find ' ' or /r/n in buffer */
        size_t key_end = strcspn(data, " \r\n");
        char *key = make_str(data, key_end);
        /* Move pointer back */
        data-=sizeof(cmd_get)-1;

        if (!key) {
            /* Error malloc */
            free(data);
            close_connection(bev, ctx, "Out of memory in key");
            return;
        }
        INFO("key:'%s'\n", key);

        /* Processing key */
        int value_size;
        char *value = sophia_get(key,&value_size);
        
        mc_get_response(output,key,value,value_size);
        if (value) {
            free(value);
        }

        free(key);

        free(data);
        /* client may send multiple commands in one buffer so continue processing */
        goto CONTINUE_LOOP;
    }

    /* Check for http_get command */
    char *httpget_start = strstr(data,http_get);
    if (httpget_start) {
        INFO("http GET / catched!");
        int cmd_pos = (int)(httpget_start - data);
        if (cmd_pos>0) {
            /* user may send some trash before command like:'trash GET /' - remove it */
            INFO("Drained:%d bytes",cmd_pos);
            evbuffer_drain(input, cmd_pos);
            free(data);
            goto CONTINUE_LOOP;
        }
        /* Find end command */
        char *httpget_end = strstr(data,http_end);
        if (!httpget_end) {
            /* no end in buffer - wait it in next packet */
            free(data);
            return; 
        }
        int cmd_len = (int)(httpget_end - data) + (sizeof(http_end)-1);
        INFO("cmd_len:%d\n",cmd_len);

        /* free processed command buffer */
        evbuffer_drain(input, cmd_len);

        /* extract key from buffer */
        data+=sizeof(http_get)-1;
        /* Find ' ' or /r/n in buffer */
        size_t key_end = strcspn(data, " \r\n");
        char *key = make_str(data, key_end);
        /* Move pointer back */
        data-=sizeof(http_get)-1;

        if (!key) {
            /* Error malloc */
            free(data);
            close_connection(bev, ctx, "Out of memory in key");
            return;
        }
        INFO("key:'%s'\n", key);
        if (!strcmp("",key)) {
            INFO("Send 200 resp");
            http_get_response(output,msg_ok,ok,strlen(ok));
        }
        else {
            /* We have key in request */
            if (!strcmp("favicon.ico",key)) {
                INFO("request:favicon.ico");
            }
            int value_size;
            char *value = sophia_get(key,&value_size);
            INFO("Send value resp");
            if (value) {
                http_get_response(output,msg_ok,value,value_size);
                free(value);
            }
            else {
                //not found
                http_get_response(output,msg_notfound,not_found,strlen(not_found));
            }
        }
        free(key);

        /* command catched - send response */
        if (bufferevent_write_buffer(bev, output)) {
            free(data);
            close_connection(bev, ctx, "Error sending data to client");
            return;
        }
        free(data);
        /* client may send multiple commands in one buffer so continue processing */
        goto CONTINUE_LOOP;
    }
    free(data);
}

static void
event_cb(struct bufferevent *bev, short events, void *ctx)
{
    if (events & BEV_EVENT_ERROR)
            perror("Error from bufferevent");
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
            bufferevent_free(bev);
    }
}

static void
accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    /* We got a new connection! Set up a bufferevent for it. */
    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = bufferevent_socket_new(
            base, fd, BEV_OPT_CLOSE_ON_FREE);

    /* Minimal command len quit/r/n */
    bufferevent_setwatermark(bev, EV_READ, 6, 0);

    bufferevent_setcb(bev, read_cb, NULL, event_cb, NULL);

    bufferevent_enable(bev, EV_READ|EV_WRITE);
}

static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    fprintf(stderr, "Got an error %d (%s) on the listener. "
            "Shutting down.\n", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}

static int
run_server(int argc, char **argv)
{
    struct event_base *base;
    struct evconnlistener *listener;
    struct sockaddr_in sin;

    int port = 9876;

    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (port<=0 || port>65535) {
        puts("Invalid port");
        return 1;
    }

    base = event_base_new();
    if (!base) {
        puts("Couldn't open event base");
        return 1;
    }

    /* Clear the sockaddr before using it, in case there are extra
        * platform-specific fields that can mess us up. */
    memset(&sin, 0, sizeof(sin));
    /* This is an INET address */
    sin.sin_family = AF_INET;
    /* Listen on 0.0.0.0 */
    sin.sin_addr.s_addr = htonl(0);
    /* Listen on the given port. */
    sin.sin_port = htons(port);

    listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
        (struct sockaddr*)&sin, sizeof(sin));
    if (!listener) {
        perror("Couldn't create listener");
        return 1;
    }
    evconnlistener_set_error_cb(listener, accept_error_cb);

    event_base_dispatch(base);
    return 0;
}

static int 
init() {
	/* open or create environment and database */
    env = sp_env();
	sp_setstring(env, "sophia.path", "db", 0);
	sp_setstring(env, "db", "db", 0);
    /* set mmap mode */
	sp_setint(env, "db.db.mmap", 1);
	db = sp_getobject(env, "db.db");
    return sp_open(env);
}

int
main(int argc, char **argv)
{
	if (init() == -1)
		goto error;

    run_server(argc, argv);

    /* finish work */
	sp_destroy(env);
    return 0;
error:;
	int size;
	char *error = sp_getstring(env, "sophia.error", &size);
	printf("error: %s\n", error);
	free(error);
	sp_destroy(env);
	return 1;
}