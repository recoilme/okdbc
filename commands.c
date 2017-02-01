#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "shared.h"
#include "sophia.h"

struct command {
	char *name;
	char *desc;
	int (*func)(struct command *command, char* data, int len, char** resp, int* resp_len);
};

int find_last_of(char * str, int str_len, char template) {
    int pos = str_len - 1;
    while (pos >= 0) {
        if (str[pos] == template) {
            return pos;
        }
        pos--;
    }
    return -1;
}


static int set_func(struct command *command, char* data, int len, char** resp, int* resp_len) {

    char *key;
    char *value;
    char *result = NULL;
    int res = -1;
    int header_len = 0;
    int value_len = 0;
    size_t endline;
    //INFO_OUT("pointer:%p\n",(void*)*data);
    char * data_cpy = data;
    data_cpy+=4;//'set '
    len-=4;
    endline = strcspn(data_cpy, " \r\n");
    if (endline < len) {
        char* key_pointer = data_cpy;
        int key_len = endline;
        endline = strcspn(data_cpy, "\r\n");
        header_len = endline;
        if (endline >= len) {
            return 0;// looks like incompleted header, try to wait closing NL
        }
        int last_ws_pos = find_last_of(data_cpy, endline, ' ');
        if (last_ws_pos < 0) {
            return -1;// looks like wrong header: it must contains sope whitespaces before NL
        }
        char value_len_str[20];
        last_ws_pos += 1;
        memcpy(value_len_str, data_cpy + sizeof(char) * last_ws_pos, endline - last_ws_pos);
        value_len_str[endline - last_ws_pos] = '\0';
        value_len = atoi(value_len_str);
        if (value_len < 0) {
            INFO_OUT("Command contains wrong data len: '%.*s'\n", len, data);
            return -1;
        }
        if (value_len <= (int)(len - endline - 4)) {
            data_cpy+=endline+2;
            len-=endline+2;
            endline = strcspn(data_cpy, "\r\n");
            if (endline < len) {
                key = strndup_p(key_pointer, key_len);
                if (strlen(key) > 64) {
                    INFO_OUT("wrong key = %s\n", key);
                    return -1;
                }
                value = strndup_p(data_cpy,endline);
                void *o = sp_document(db);
                sp_setstring(o, "key", &key[0], strlen(key));
                sp_setstring(o, "value", &value[0], strlen(value));
                res = sp_set(db, o);
                INFO_OUT("key:%s value:%s res:%d\n",key,value,res);
                free(value);
                free(key);
                if (res == 0) {
                    result = malloc(strlen(ST_STORED));
                    memcpy(result,ST_STORED,strlen(ST_STORED));
                    *resp_len = strlen(ST_STORED);
                }
            } else {
                return 0;
            }
        } else {
            return 0;
        }
    } else {
        return 0;
    }

    if (result == NULL) {
        result =  malloc(strlen(ST_NOTSTORED));
        memcpy(result,ST_NOTSTORED,strlen(ST_NOTSTORED));
        *resp_len = strlen(ST_NOTSTORED);
    }
    *resp = result;

    //success processed
    return header_len + value_len + 4 + 2+2;// 4 bytes 'set ', 2+2 bytes NL
}



static int get_func(struct command* command, char* data, int len, char ** resp, int* resp_size) {
    char *key = NULL;
    int key_len = 0;
    char *val = NULL;
    char *ptr;
    int size;
    int shift = 0;
    char * data_cpy = data;
    data_cpy+=4;//'get '
    size_t cmdend = strcspn(data_cpy, "\r\n");
    INFO_OUT("cmdend:%d, len = %d\n",(int)cmdend, len);
    if (cmdend < (len - 4)) {
        key = strndup_p(data_cpy,cmdend);
        key_len = strlen(key);
        INFO_OUT("key:'%.*s'\n", key_len,key);
        // get 
        
        void *o = sp_document(db);
        sp_setstring(o, "key", &key[0], key_len);
        o = sp_get(db, o);
        if (o) {
            ptr = sp_getstring(o, "value", &size);
            val = strndup_p((char*)ptr,size);
            if (!val) {
                goto error;
            }
            sp_destroy(o);
        }
        else {
            val = "";
            //TODO return empty string for val?
        }        
        INFO_OUT("val:'%.*s'\n", size,val);
        
        char *format = "VALUE %s 0 %d\r\n%s\r\nEND\r\n";
        *resp_size = (strlen(format) -3*2/* % exclude */) + key_len + size + get_int_len(size) + 1/* \0*/;
        *resp = malloc(*resp_size);
        snprintf(*resp, *resp_size,format,key,size,val);
        INFO_OUT("resp:'%.*s' %d\n", *resp_size,*resp,*resp_size);
        if (strcmp(val,"")) free(val);
        *resp_size -= 1; // do not send '\0' byte
        free(key);
        //success processed
        return 4 + key_len + 2;
    } else {
        return 0;
    }
error:;
    free(val);
    free(key);
    free(resp);
    return -1;
}

static struct command commands[] = {
	{ "set", "set key 0 0 5\r\nvalue\r\n", set_func },
    { "get", "get key\r\n", get_func }//,
    //{ "quit", "quit\r\n", quit_func }
};

//you must free data ater use
size_t handle_read(char* data, int len, char** resp, int* resp_len) {
    if (len < 5) { // minimal command: 'cmd' + NL
        return 0;
    }

    int processed = -1;

    size_t cmdend = strcspn(data, " \r\n");
    char *cmd = strndup_p(data, cmdend);
    INFO_OUT("Command:%s\n", cmd);
    // Execute the command, if it is valid
    int i;
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        if(!strcmp(cmd, commands[i].name)) {
            //INFO_OUT("Running command %s\n", commands[i].name);
            processed = commands[i].func(&commands[i], data, len, resp, resp_len);
            break;
        }
    }
    free(cmd);
    if (processed < 0) {
        //no commands
        INFO_OUT("data = '%.*s'\n", len, data);
        INFO_OUT("Command parsing exception, code: %d\n", processed);
        *resp_len = strlen(ST_ERROR);
        *resp = malloc(*resp_len);
        memcpy(*resp, ST_ERROR, *resp_len);
    }

    // TODO: in case of invalid protocol we drop all incoming requests (return len). It will be better to remove only invalid commands
    return processed < 0 ? len : processed;
}