//gcc -o simple_server_tst simple_server_tst.c -lpthread -levent -I/usr/local/include -L/usr/local/lib
//wrk -t 5 -c 1000 http://127.0.0.1:9876/
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

const int LOGENABLED = 1;

#define INFO(...) {\
	if (LOGENABLED == 1) printf("\n%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
	if (LOGENABLED == 1) printf(__VA_ARGS__);\
}

static char 
msg_ok[] =
        "HTTP/1.1 200 OK\r\n"
        "Connection: Keep-Alive\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Keep-Alive: timeout=10, max=20\r\n"
        "Server: okdb/0.0.1\r\n"
        "\r\n%s";

static char
http_get[] =
        "GET /";

static char
http_end[] =
        "\r\n\r\n";

static char
quit[] =
        "quit\r\n";

static char 
ok[] =
        "OK\r\n";


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
send_response(struct evbuffer *output, const char *msg)
{
    int resp_size = ((sizeof(msg_ok)-1) -2*2/* % exclude */) + strlen(msg) + get_int_len(strlen(msg));
                
    char *resp = malloc(resp_size+1);
    resp[resp_size] = '\0';
    snprintf(resp, resp_size,msg_ok, strlen(msg),msg);
    evbuffer_add(output, resp, resp_size);
    free(resp);
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
                send_response(output, ok);
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

int
main(int argc, char **argv)
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