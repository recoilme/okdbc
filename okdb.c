#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "sophia.h"

#define MAX_QUERY_PARAM_CNT 4
const int LOGENABLED = 0;

void *env;
void *db;

#define INFO(...) {\
	if (LOGENABLED == 1) printf("\n%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
	if (LOGENABLED == 1) printf(__VA_ARGS__);\
}

#define ERROR(...) {\
	printf("\n%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
	printf(__VA_ARGS__);\
}

static char 
*msg_ok =
    "HTTP/1.1 200 OK\r\n"
    "Connection: Keep-Alive\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "Content-Length: %d\r\n"
    "Keep-Alive: timeout=20, max=200\r\n"
    "Server: okdb/0.0.4\r\n"
    "\r\n%s";

static char 
*msg_notfound =
    "HTTP/1.1 404 Not Found\r\n"
    "Connection: close\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "Content-Length: %d\r\n"
    "Server: okdb/0.0.4\r\n"
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

/* A struct to hold the query string parameter values. */
struct query_param {
	char *key;
	char *val;
};

/* A struct to hold commands from query. */
struct command {
    char *type;
    char *prefix;
    char *command;
    char *key;
    char *value;
    int limit;
};

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
print_error()
{
    int size;
	char *error = sp_getstring(env, "sophia.error", &size);
	ERROR("%s",error);
    free(error);
}
/* 
stealed from here: https://github.com/jacketizer/libyuarel/blob/master/yuarel.c#L160
*/
static int
parse_query(char *query, char delimiter, struct query_param *params, int max_params)
{
	int i = 0;

	if (query == NULL || *query == '\0')
		return -1;

	params[i++].key = query;
	while (i < max_params && NULL != (query = strchr(query, delimiter))) {
		*query = '\0';
		params[i].key = ++query;
		params[i].val = NULL;
		
		/* Go back and split previous param */
		if (i > 0) {
			if ((params[i - 1].val = strchr(params[i - 1].key, '=')) != NULL)
				*(params[i - 1].val)++ = '\0';
		}
		i++;
	}

	/* Go back and split last param */
	if ((params[i - 1].val = strchr(params[i - 1].key, '=')) != NULL)
		*(params[i - 1].val)++ = '\0';

	return i;
}

static void
http_out(struct evbuffer *output, const char *format, const char *msg, int size)
{
    //INFO("msg:%s",msg);
    if (!msg) return;
    int resp_size = (strlen(format) -2*2/* % exclude */) + size + get_int_len(size)+1;
    //INFO("http_get_response size:%d", resp_size);        
    char *resp = malloc(resp_size);
    if (!resp) {
        perror("Out of memory");
        return;
    }
    resp[resp_size-1] = '\0';
    snprintf(resp, resp_size, format, size, msg);
    //INFO("http_get_response:'%s' size:%d",resp, resp_size);
    evbuffer_add(output, resp, resp_size-1);
    free(resp);
}


static void
memcache_out(struct evbuffer *output, const char *key,const char *value, int size)
{
    if (!value) {
        return;
    }
    char *format = "VALUE %s 0 %d\r\n%s\r\n";
    int resp_size = (strlen(format) -3*2) + strlen(key) + size + get_int_len(size) + 1;
    char *resp = malloc(resp_size);
    if (!resp) {
        perror("Out of memory");
        return;
    }
    resp[resp_size-1] = '\0';
    snprintf(resp, resp_size,format,key,size,value);
    INFO("resp:'%.*s' resp_size:%d\n", resp_size,resp, resp_size);
    evbuffer_add(output, resp, resp_size-1);//remove \0
    free(resp);
    return;
}


static void
get_val(struct evbuffer *output, const char *key, int is_http)
{
    char *query = strstr(key,"?");
	if (query) {
        /* Parse query like: ?type=cursor&limit=2 */
		query++;
		int p;
		struct query_param param[MAX_QUERY_PARAM_CNT] = {{0}};
        p = parse_query(query,'&', param, MAX_QUERY_PARAM_CNT);
        struct command cmd = {0};
        while (p-- > 0) {
            INFO("\t%s: %s\n", param[p].key, param[p].val);
            if (!strcmp("type",param[p].key)) cmd.type = param[p].val;
            if (!strcmp("prefix",param[p].key)) cmd.prefix = param[p].val; 
            if (!strcmp("limit",param[p].key)) {
                cmd.limit = (int) strtol(param[p].val,NULL,10);
            }
            if (!strcmp("command",param[p].key)) cmd.command = param[p].val;
            if (!strcmp("key",param[p].key)) cmd.key = param[p].val;
            if (!strcmp("value",param[p].key)) cmd.value = param[p].val;
        }
        if (!cmd.type) {
            /* Unknown type */
            if (is_http == 0) evbuffer_add(output, st_end, sizeof(st_end)-1);
            return;
        }
        INFO("processing");
        if (!strcmp("cursor",cmd.type)) {
            void *o = sp_document(db);
            char *prefix = NULL;
            //int res = sp_setstring(o, "order", ">=", 0);
            if (cmd.prefix){
                /* if prefix not set - will scan all db */
                sp_setstring(o, "order", ">=", 0);
                prefix = make_str(cmd.prefix,strlen(cmd.prefix));
                sp_setstring(o, "prefix", prefix, strlen(prefix));
                //free(prefix);
            }
            else {
                sp_setstring(o, "order", ">=", 0);
            }
            void *c = sp_cursor(env);
            /* Default limit - 100 */
            int limit = (cmd.limit==0)?100:cmd.limit;
            int value_size;
            int key_size;
            while( (o = sp_get(c, o)) ) {
                if (!limit) {
                    sp_destroy(o);
                    break;
                }
                char *ptr_key = (char*)sp_getstring(o, "key", &key_size);
                char *new_key = make_str(ptr_key,key_size);
                char *ptr_val = (char*)sp_getstring(o, "value", &value_size);
                char *value = make_str(ptr_val,value_size);
                if (is_http == 0) memcache_out(output,new_key,value,value_size);
                INFO("key:%s\tval:%s",new_key,value);
                free(new_key);
                free(value);
                limit--;
            }
            sp_destroy(c);
            if (prefix) free(prefix);
            if (is_http == 0) evbuffer_add(output, st_end, sizeof(st_end)-1);
            INFO("end cursor\n");
        }
        if (!strcmp("sophia",cmd.type) && cmd.command && cmd.key) {
            /* Process sophia command */

            INFO("Process %s sophia command",cmd.command);
            if (!strcmp("getstring",cmd.command)) {
                
                int value_size;
                char *new_key = make_str(cmd.key,strlen(cmd.key));
                if (new_key) {
                    char *ptr_key = (char*) sp_getstring(env,new_key,&value_size);
                    if (ptr_key) {
                        if (is_http == 0) memcache_out(output,new_key,ptr_key,value_size);
                        free(ptr_key);
                    }
                    free(new_key);
                }
            }

            if (!strcmp("getint",cmd.command)) {
                
                char *new_key = make_str(cmd.key,strlen(cmd.key));
                if (new_key) {
                    int64_t val = sp_getint(env,new_key);
                    char buf[128];
                    memset(buf, 0x00, 128);
                    sprintf(buf, "%lld", val);
                    if (is_http == 0) memcache_out(output,new_key,buf,strlen(buf));
                    free(new_key);
                }
            }

            if (!strcmp("setint",cmd.command)) {
                /* ?type=sophia&command=setint&key=backup.run&value=0 */
                char *new_key = make_str(cmd.key,strlen(cmd.key));
                if (new_key) {                      
                    int64_t val = cmd.value?strtol(cmd.value,NULL,10):0;
                    int result = sp_setint(env,new_key,val);
                    if (result == -1) print_error();
                    char buf[128];
                    memset(buf, 0x00, 128);
                    sprintf(buf, "%d", result);
                    if (is_http == 0) memcache_out(output,new_key,buf,strlen(buf));
                    free(new_key);
                }
            }

            if (!strcmp("setstring",cmd.command)) {
                
                char *new_key = make_str(cmd.key,strlen(cmd.key));
                if (new_key && cmd.value) { 
                    char *val = make_str(cmd.value,strlen(cmd.value)); 
                    int result = sp_setstring(env,new_key,val,strlen(val));
                    if (result == -1) print_error();
                    char buf[128];
                    memset(buf, 0x00, 128);
                    sprintf(buf, "%d", result);
                    if (is_http == 0) memcache_out(output,new_key,buf,strlen(buf));
                    free(new_key);
                    free(val);
                }
            }

            if (is_http == 0) evbuffer_add(output, st_end, sizeof(st_end)-1);
        }
        return;
	}
    /* get value from sophia */
    int value_size;

    void *o = sp_document(db);
    sp_setstring(o, "key", &key[0], strlen(key));
    o = sp_get(db, o);
    
    if (o) {
        char *ptr = sp_getstring(o, "value", &value_size);
        char *value = make_str((char*)ptr,value_size);
        INFO("Val:%s",value);
        sp_destroy(o);
        if (is_http == 0) {
            memcache_out(output,key,value,value_size);
        }
        else {
            http_out(output,msg_ok,value,value_size);
        }
        INFO("val:'%.*s', size:%d\n", value_size,value,value_size);
        free(value);
    }
    else {
        if (is_http == 1){
            http_out(output,msg_notfound,not_found,strlen(not_found));
        }
        INFO("key:'%s' not found\n",key);
    }
    if (is_http == 0) {
        evbuffer_add(output, st_end, sizeof(st_end)-1);
    }
    return;
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
        INFO("set key:'%s'\n", key);

        /* Check for noreply */
        char *noreply = strstr(data,"noreply");
        
        /* Lets find value size */
        int minus = sizeof(nl)-1;
        if (noreply) minus+=strlen("noreply")+1;
        char *command = make_str(data,cmd_len-minus);
        INFO("command:'%s'\n",command);
        char *val_sizestr = strrchr(command, ' ');
        val_sizestr+=1;//' '
        int val_size = (int) strtol(val_sizestr,NULL,10);
        INFO("val_size:%d\n",val_size);
        free(command);

        /* We know value size, check buffer */
        int total_size = cmd_len+val_size+(sizeof(nl)-1);
		if ((int)len<total_size) {
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
        if (!noreply) {
            if (res == 0) {
                evbuffer_add(output, st_stored, strlen(st_stored));
            }
            else {
                evbuffer_add(output, st_notstored, strlen(st_notstored));
            }
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
        get_val(output, key, 0);
        size_t lenout = evbuffer_get_length(output);
        INFO("out%d\n",(int)lenout);
        /* command catched - send response */
        if (bufferevent_write_buffer(bev, output)) {
            INFO("error send");
            free(key);
            free(data);
            close_connection(bev, ctx, "Error sending data to client");
            return;
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
            http_out(output,msg_ok,ok,strlen(ok));
        }
        else {
            /* We have key in request */
            if (!strcmp("favicon.ico",key)) {
                INFO("request:favicon.ico");
            }
            get_val(output,key,1);
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

    int port = 11213;

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
	printf("%s",ok);
    evconnlistener_set_error_cb(listener, accept_error_cb);

    event_base_dispatch(base);
    return 0;
}


static int 
init() {
	/* open or create environment and database */
    env = sp_env();
	sp_setstring(env, "sophia.path", "sophia", 0);
    sp_setstring(env, "backup.path", "sophia", 0);
	sp_setstring(env, "db", "db", 0);
    //sp_setstring(env, "db.db.scheme", "key", 0);
    //sp_setstring(env, "db.db.scheme.key", "string,key(0)", 0);
    //sp_setstring(env, "db.db.scheme", "value", 0);
    //sp_setstring(env, "db.db.scheme.value", "string", 0);
    /* set mmap mode */
	sp_setint(env, "db.db.mmap", 1);
	db = sp_getobject(env, "db.db");
    return sp_open(env);
}

static void
test()
{
	
}

int
main(int argc, char **argv)
{
	test();
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
