extern void* env;
extern void* db;
extern const char* ST_ERROR;
extern const char* ST_STORED;
extern const char* ST_NOTSTORED;
extern const char* ST_END;
extern const char* ST_OK;
extern const int LOGLEVEL;

char *strndup_p(const char *str, size_t len);

int get_int_len (int value);

// Size of array (Caution: references its parameter multiple times)
#define ARRAY_SIZE(array) (sizeof((array)) / sizeof((array)[0]))

/* Behaves similarly to fprintf(stderr, ...), but adds file, line, and function
 information. */
#define errorOut(...) {\
    fprintf(stderr, "%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
    fprintf(stderr, __VA_ARGS__);\
}

// Behaves similarly to printf(...), but adds file, line, and function
// information.  I omit do ... while(0) because I always use curly braces in my
// if statements.
#define INFO_OUT(...) {\
	if (LOGLEVEL == 1) printf("%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
	if (LOGLEVEL == 1) printf(__VA_ARGS__);\
}

// Behaves similarly to fprintf(stderr, ...), but adds file, line, and function
// information.
#define ERROR_OUT(...) {\
	fprintf(stderr, "\e[0;1m%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
	fprintf(stderr, __VA_ARGS__);\
	fprintf(stderr, "\e[0m");\
}

// Behaves similarly to perror(...), but supports printf formatting and prints
// file, line, and function information.
#define ERRNO_OUT(...) {\
	fprintf(stderr, "\e[0;1m%s:%d: %s():\t", __FILE__, __LINE__, __FUNCTION__);\
	fprintf(stderr, __VA_ARGS__);\
	fprintf(stderr, ": %d (%s)\e[0m\n", errno, strerror(errno));\
}

