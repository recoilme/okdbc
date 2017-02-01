#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char *ST_ERROR="ERROR\r\n";
const char *ST_STORED="STORED\r\n";
const char *ST_NOTSTORED="NOT_STORED\r\n";
const char *ST_END="END\r\n";
const char *ST_OK="OK\r\n";
const int LOGLEVEL = 0;

// str must have at least len bytes to copy
char *strndup_p(const char *str, size_t len)
{
	char *newstr;

	newstr = malloc(len + 1);
	if(newstr == NULL) {
        printf("ERROR ALOCATE%zu\n",len);
		return NULL;
	}

	memcpy(newstr, str, len);
	newstr[len] = 0;

	return newstr;
}

//must be positive
int get_int_len (int value)
{
  int l=1;
  while(value>9){ l++; value/=10; }
  return l;
}