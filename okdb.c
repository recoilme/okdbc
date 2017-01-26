#include "http.h"
#include "sophia.h"

// define params here
#undef THREAD_COUNT // just in case
#define THREAD_COUNT 1 // depending on your use-case, you might want more threads.

#undef PROCESS_COUNT // just in case
#define PROCESS_COUNT 1 // leave one core for the kernel and the ab test tool

#define PORT "8888" // default port

#define LOG_LEVEL 1 // 1 print debug info

#define KEY_MAX_SIZE 1024 // max key size

#define VALUE_MAX_SIZE 1024 // max value size

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
static char err_key[] = "Error: key too large\n";
static char err_val[] = "Error: value too large\n";
static char err_noval[] = "Error: no value\n";
static char err_nokey[] = "Error: key not found\n";
static char ok[] = "OK\n";
static char err[] = "Error\n";

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
 Test with: ab -n 1000 -c 100 -k http://127.0.0.1:8888/
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
    // init response from request
    http_response_s response = http_response_init(request);
    
    // empty or "/" path
    if (request->path_len <= 1) {
        http_response_write_body(&response,
                           ok, sizeof(ok)-1);
        http_response_finish(&response);
        //sock_write(request->metadata.fd, msg_200, sizeof(msg_200) - 1);
        return;
    }
    
    // checks length of key
    if (request->path_len > KEY_MAX_SIZE + 1) {
        response.status = 400;
        http_response_write_body(&response,
                           err_key, sizeof(err_key)-1);
        //sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
        //ERR_OUT("key too large");
        http_response_finish(&response);
        return;
    }
    
    // set key from path and value from body
    if (!strcmp("PUT",request->method)) {
        if (request->content_length == 0) {
            //sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
            //ERR_OUT("no value");
            response.status = 400;
            http_response_write_body(&response,
                           err_noval, sizeof(err_noval)-1);
            http_response_finish(&response);
            return;
        }
        if (request->content_length > VALUE_MAX_SIZE) {
            //sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
            //ERR_OUT("value too large");
            response.status = 400;
            http_response_write_body(&response,
                           err_val, sizeof(err_val)-1);
            http_response_finish(&response);            
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
            //sock_write(request->metadata.fd, msg_200, sizeof(msg_200) - 1);
            http_response_write_body(&response,
                           ok, sizeof(ok)-1);
            http_response_finish(&response);            
            return;
        }
        else {
            //sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
            response.status = 400;
            //TODO write sophia error
            http_response_write_body(&response,
                           err, sizeof(err)-1);
            http_response_finish(&response);            
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
            
            //int resp_size = (strlen(msg_fmt) -2*2/* %s exclude */) + size + get_int_len(size) + 1/* \0*/;
            //char *resp = malloc(resp_size);
            //snprintf(resp, resp_size,msg_fmt,size,val);
            //INFO_OUT("resp:'%.*s'\n", resp_size,resp);
            
            //sock_write(request->metadata.fd, resp, resp_size - 1);
            
            http_response_write_body(&response,
                           val, size);
            http_response_finish(&response); 

            free(val);
            //free(resp);
            sp_destroy(o);
            return;
        }
        else {
            INFO_OUT("key not found");
            //sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
            response.status = 404;
            http_response_write_body(&response,
                           err_nokey, sizeof(err_nokey)-1);
            http_response_finish(&response);
            return;
        }
    }
    
    // not handled request
    //sock_write(request->metadata.fd, msg_400, sizeof(msg_400) - 1);
    response.status = 400;
    http_response_write_body(&response,
                    err, sizeof(err)-1);
    http_response_finish(&response);
    return;
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
