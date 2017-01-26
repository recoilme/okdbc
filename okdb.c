#include "http.h"
#include "sophia.h"

// define params here
#undef THREAD_COUNT // just in case
#define THREAD_COUNT 1 // depending on your use-case, you might want more threads.

#undef PROCESS_COUNT // just in case
#define PROCESS_COUNT 4 // leave one core for the kernel and the ab test tool

#define PORT "8888" // default port

#define LOG_LEVEL 0 // 1 print debug info

#define KEY_MAX_SIZE 1024 // max key size

#define VALUE_MAX_SIZE 4096 // max value size

// Behaves similarly to printf(...), but adds file, line, and function
// information.
#define INFO_OUT(...) {\
if (LOG_LEVEL) printf("%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
if (LOG_LEVEL) printf(__VA_ARGS__);\
}

// Behaves similarly to fprintf(stderr, ...), but adds file, line, and function
// information.
#define ERR_OUT(...) {\
printf("%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
printf(__VA_ARGS__);\
}

// Constants
static char msg_200[] =
"HTTP/1.1 200 OK\r\n"
"Content-Length: 2\r\n"
"Connection: keep-alive\r\n"
"Keep-Alive: 1;timeout=5\r\n"
"\r\n"
"OK";

static char msg_400[] =
"HTTP/1.1 400 Bad Request\r\n"
"Content-Length: 6\r\n"
"Connection: keep-alive\r\n"
"Keep-Alive: 1;timeout=5\r\n"
"\r\n"
"NOT OK";

static char msg_fmt[] =
"HTTP/1.1 200 OK\r\n"
"Content-Length: %d\r\n"
"Connection: keep-alive\r\n"
"Keep-Alive: 1;timeout=5\r\n"
"\r\n"
"%s";

// Global vars
void *env;
void *db;

/*
 Length of int
 must be positive
 */
int get_int_len (int value)
{
    int l=1;
    while(value>9){ l++; value/=10; }
    return l;
}

/*
 Request parser
 Test with: ab -n 1000000 -c 200 -k http://127.0.0.1:3000/
 */
static void on_request(http_request_s* request) {
    
    if (LOG_LEVEL) {
        INFO_OUT("\nHeaders.\n");
        INFO_OUT("method:%s\n",request->method);
        INFO_OUT("path:%s\n",request->path);
        INFO_OUT("path len:%d\n",(int) request->path_len);
        INFO_OUT("query:%s\n",request->query);
        INFO_OUT("version:%s\n",request->version);
        INFO_OUT("host:%s\n",request->host);
        INFO_OUT("content_type:%s\n",request->content_type);
        INFO_OUT("connection:%s\n",request->connection);
        INFO_OUT("body_str:%s\n",request->body_str);
        INFO_OUT("content_length:%d\n",(int)request->content_length);
    }
    
    // empty or "/" path
    if (request->path_len <= 1) {
        sock_write(request->metadata.fd, msg_200, sizeof(msg_200) - 1);
        return;
    }
    
    // checks length of key
    if (request->path_len > KEY_MAX_SIZE ) {
        sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
        ERR_OUT("key too large");
        return;
    }
    
    // set key from path and value from body
    if (!strcmp("PUT",request->method)) {
        if (request->content_length == 0) {
            sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
            ERR_OUT("no value");
            return;
        }
        if (request->content_length > VALUE_MAX_SIZE) {
            sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
            ERR_OUT("value too large");
            return;
        }
        // delete leading / from path ? is it always present?
        request->path++;
        
        void *o = sp_document(db);
        sp_setstring(o, "key", &request->path[0], (int) (request->path_len - 1));
        sp_setstring(o, "value", &request->body_str[0], (int)request->content_length);
        //set key value in simple trunsaction
        int result = sp_set(db, o);
        // return pointer back
        request->path--;
        if (result == 0) {
            // succesfuly stored
            sock_write(request->metadata.fd, msg_200, sizeof(msg_200) - 1);
            return;
        }
        else {
            sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
            return;
        }
    }
    // get value by key from path
    if (!strcmp("GET",request->method)) {
        // delete leading / from path ? is it always present?
        request->path++;
        
        int size;
        char *val;
        void *o = sp_document(db);
        sp_setstring(o, "key", &request->path[0], (int) (request->path_len - 1));
        o = sp_get(db, o);
        // return pointer
        request->path--;
        if (o) {
            void *ptr = sp_getstring(o, "value", &size);
            val = malloc(size);
            memcpy(val,ptr,size);
            INFO_OUT("val:'%.*s'\n", size,val);
            
            int resp_size = (strlen(msg_fmt) -2*2/* %s exclude */) + size + get_int_len(size) + 1/* \0*/;
            char *resp = malloc(resp_size);
            snprintf(resp, resp_size,msg_fmt,size,val);
            INFO_OUT("resp:'%.*s'\n", resp_size,resp);
            
            sock_write(request->metadata.fd, resp, resp_size - 1);
            free(val);
            free(resp);
            sp_destroy(o);
            return;
        }
        else {
            INFO_OUT("key not found");
            sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
        }
    }
    
    // not handled request
    sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
}

/*
 Open or create environment and database
 */
int init() {
    env = sp_env();
    sp_setstring(env, "sophia.path", "_test", 0);
    sp_setstring(env, "db", "test", 0);
    sp_setint(env, "db.test.mmap", 1);
    db = sp_getobject(env, "db.test");
    return sp_open(env);
}

/*****************************
The main function
*/
int main(void) {

    if (init() == -1)
        goto error;
    
    if (http1_listen(PORT, NULL, .on_request = on_request,
                     .log_static = 1)) {
        sp_destroy(env);
        perror("Couldn't initiate HTTP service"), exit(1);
    }
    server_run(.threads = THREAD_COUNT, .processes = PROCESS_COUNT);
    
    sp_destroy(env);
    return 0;
    
error:;
    int size;
    char *error = sp_getstring(env, "sophia.error", &size);
    ERR_OUT("error: %s\n", error);
    free(error);
    sp_destroy(env);
    return 1;
}
