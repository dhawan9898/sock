#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <sys/time.h>
#include "socket.h"
#include <time.h>
#include <signal.h>

int sock = 0;

extern struct session* path_head;
extern struct session* resv_head;
extern db_node *path_tree;
extern db_node *resv_tree;

extern pthread_mutex_t path_tree_mutex;
extern pthread_mutex_t resv_tree_mutex;
extern pthread_mutex_t path_list_mutex;
extern pthread_mutex_t resv_list_mutex;

uint32_t ip_to_int(const char* ip_str) {
    struct in_addr ip_addr;
    inet_aton(ip_str, &ip_addr);
    return ntohl(ip_addr.s_addr);
}

int main() {

    char buffer[512];
    char sender_ip[16], receiver_ip[16];
    uint16_t tunnel_id;
    uint8_t reached = 0;

    struct sockaddr_in addr;
    sock = socket(AF_INET, SOCK_RAW, RSVP_PROTOCOL);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("binding failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    // Initialize the mutexes
    if (pthread_mutex_init(&path_tree_mutex, NULL) != 0 ||
        pthread_mutex_init(&resv_tree_mutex, NULL) != 0 ||
        pthread_mutex_init(&path_list_mutex, NULL) != 0 ||
        pthread_mutex_init(&resv_list_mutex, NULL) != 0) {
        perror("Failed to initialize mutex");
        close(sock);
        return 1;
    }

    while(1) {
        printf("Waiting to receive mesgae\n");
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recvfrom(sock, buffer, sizeof(buffer), 0,
                (struct sockaddr*)&sender_addr, &addr_len);
        if (bytes_received < 0) {
            perror("Receive failed");
            continue;
        }

        struct rsvp_header *rsvp = (struct rsvp_header*)(buffer+20);

        //printf("---- %d\n",rsvp->msg_type);
        switch(rsvp->msg_type) {

            case PATH_MSG_TYPE: 

                //Receive PATH Message

                resv_event_handler();
                // get ip from the received path packet
                get_ip(buffer, sender_ip, receiver_ip,&tunnel_id);
                reached = dst_reached(receiver_ip);

                pthread_mutex_lock(&path_list_mutex);
                printf("insert_path_session\n");
                if(path_head == NULL) {
                    path_head = insert_session(path_head, tunnel_id, sender_ip, receiver_ip, reached);
                } else {
                    insert_session(path_head, tunnel_id, sender_ip, receiver_ip,reached);
                }
                pthread_mutex_unlock(&path_list_mutex);

                receive_path_message(sock,buffer,sender_addr);	

                break;

            case RESV_MSG_TYPE:

                // Receive RSVP-TE RESV Message

                path_event_handler();
                //get ip from the received resv msg
                get_ip(buffer, sender_ip, receiver_ip, &tunnel_id);
                reached = dst_reached(sender_ip);

                printf("insert_resv_session\n");
                pthread_mutex_lock(&resv_list_mutex);
                if(resv_head == NULL) {
                    resv_head = insert_session(resv_head, tunnel_id, sender_ip, receiver_ip, reached);
                } else {
                    insert_session(resv_head, tunnel_id, sender_ip, receiver_ip, reached);
                }
                pthread_mutex_unlock(&resv_list_mutex);

                receive_resv_message(sock,buffer,sender_addr);

                break;

        }
    }
    close(sock);
    pthread_mutex_destroy(&path_tree_mutex);
    pthread_mutex_destroy(&resv_tree_mutex);
    pthread_mutex_destroy(&path_list_mutex);
    pthread_mutex_destroy(&resv_list_mutex);
    return 0;
}

