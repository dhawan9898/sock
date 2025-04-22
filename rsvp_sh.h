#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdint.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<time.h>
#include<pthread.h>
#include<unistd.h>

// Show API functions 
void get_path_tree_info(char * , size_t );
void get_resv_tree_info(char * , size_t );

// Config API functions
int rsvp_add_config(const char * , char * , size_t);
int rsvp_delete_config(const char * , char * , size_t);

path_msg* create_path(const char *, char *, size_t);
int rsvpsh_main();
