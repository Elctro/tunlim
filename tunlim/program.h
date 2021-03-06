#pragma once
 
#include <fcntl.h> 

#include "thread_shared.h"

//entropy calc
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int ftruncate(int fd, off_t length);

int create(void **args);
int destroy();
int init();

void* threadproc(void *arg);
int increment(const char *address, const char *domain);
int search(const char * querieddomain, struct ip_addr * userIpAddress, const char * userIpAddressString, int rrtype, char * originaldomain, char * logmessage);
int explode(char * domainToFind, struct ip_addr * userIpAddress, const char * userIpAddressString, int rrtype);
int makehist(char *S, int *hist, int len);
double entropy(int *hist, int histlen, int len);
double calc_entropy(const char * string);