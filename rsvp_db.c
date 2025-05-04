/*
 * Copyright (c) 2025, Spanidea. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 **/

#include "rsvp_db.h"
#include "rsvp_msg.h"
#include "timer-event.h"
#include "log.h"

struct session* sess = NULL;
time_t now = 0;
char nhip[16];
char source_ip[16];
char destination_ip[16];
char next_hop_ip[16];

struct session* path_head = NULL;
struct session* resv_head = NULL;
db_node *path_tree = NULL;
db_node *resv_tree = NULL;

pthread_mutex_t path_tree_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t resv_tree_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t path_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t resv_list_mutex = PTHREAD_MUTEX_INITIALIZER;

struct session* search_session(struct session* sess, uint16_t tunnel_id) {
    now = time(NULL);
    struct session *temp = sess;
    while(temp != NULL) {
        if( temp->tunnel_id == tunnel_id) {
            temp->last_path_time = now;
            return temp;
        }
        temp=temp->next;
    }
    return NULL;	
}

struct session* insert_session(struct session* sess, uint16_t t_id, char sender[], char receiver[], uint8_t dest) {
    now = time(NULL);
    log_message("insert session\n");
    if(sess == NULL) {
        struct session *temp = (struct session*)malloc(sizeof(struct session));
        if(temp == NULL){
            log_message("cannot allocate dynamic memory]n");
            return NULL;
        }

        temp->last_path_time = now;
        strcpy(temp->sender, sender);
        strcpy(temp->receiver, receiver);
        temp->dest = dest;
        temp->del = 0;
        temp->tunnel_id = t_id;
        temp->next = NULL;
        return temp;
    } else {
        struct session *temp = (struct session*)malloc(sizeof(struct session));
        if(sess == NULL)
            log_message("cannot allocate dynamic memory\n");

        temp->last_path_time = now;
        strcpy(temp->sender, sender);
        strcpy(temp->receiver, receiver);
        temp->dest = dest;
        temp->tunnel_id = t_id;
        temp->del = 0;
        temp->next = sess;

        return temp;
    }
}


struct session* delete_session(struct session* head, struct session** sess, struct session** prev) { 

    struct session *temp = NULL;

    if( head == NULL)
        return NULL;

    if(head == *sess) {
        temp = head;
        head = head->next;
        free(temp);
        *sess = NULL;
        return head;
    } else {
        temp = (*sess)->next;
        if(temp == NULL) {
            print_session(head);
            (*prev)->next = NULL;
            free(sess);
            *sess = NULL;
            return head;
        }
        (*prev)->next = temp;
        *sess = NULL;
        free(temp);
        return head;
    }
}

void print_session (struct session* head) {
    if(head == NULL)
        return;

    struct session *temp = head;
    while(temp != NULL) {
        log_message("t_id %d dest = %d dst ip = %s\n", temp->tunnel_id, temp->dest, temp->receiver);
        temp=temp->next;
    }
} 

void delete_session_state(struct session** head, uint16_t tunnel_id ) {
    struct session* temp = *head;
    struct session* prev = NULL;

    while (temp != NULL){
        if (temp->tunnel_id == tunnel_id) {
            if (prev == NULL) {
                //Deleting head
                *head = temp->next;
            } else {
                prev->next = temp->next;
            }
            free(temp);
            return;
        }
        prev = temp;
        temp = temp->next;
    }
    log_message("Delete:No session state for tunnel:%d\n", tunnel_id);
}

void insert(char buffer[], uint8_t type) {
    char sender_ip[16], receiver_ip[16];
    uint16_t tunnel_id;
    int reached = 0;
    struct session *temp = NULL, *head = NULL;

    get_ip(buffer, sender_ip, receiver_ip, &tunnel_id);
    if((reached = dst_reached(sender_ip)) == -1) {
        log_message(" No route to destiantion %s\n",sender_ip);
        return;
    }


    pthread_mutex_lock(&resv_list_mutex);
    temp = search_session(resv_head, tunnel_id);
    pthread_mutex_unlock(&resv_list_mutex);	
    if(temp == NULL) {
        pthread_mutex_lock(&resv_list_mutex);
        resv_head = insert_session(resv_head, tunnel_id, sender_ip, receiver_ip, reached);
        pthread_mutex_unlock(&resv_list_mutex);
        if(resv_head == NULL) {
            log_message("insert for tunnel %d failed", tunnel_id);
            return;
        } else {
            log_message("insertion of tunnel_id %d sucessful",tunnel_id);
        } 
    }
    temp = NULL;
}



//AVL for Path adn Resv table
//*****************************************

int compare_path_insert(const void *a, const void *b) {
    return (((path_msg*) a)->tunnel_id - ((path_msg*) b)->tunnel_id);
}

// Comparison function for Resv messages during insertion
int compare_resv_insert(const void *a, const void *b) {
    return (((resv_msg*) a)->tunnel_id - ((resv_msg*) b)->tunnel_id);
}

// Comparison function for Path messages during search
int compare_path_del(uint16_t tunnel_id, const void *b) {
    return (tunnel_id - ((path_msg*) b)->tunnel_id);
}

// Comparison function for Resv messages during search
int compare_resv_del(uint16_t tunnel_id, const void *b) {
    return (tunnel_id - ((resv_msg*) b)->tunnel_id);
}

/* Right rotation */
db_node* right_rotate(db_node *y) {
    db_node *x = y->left;
    db_node *T2 = x->right;
    x->right = y;
    y->left = T2;
    y->height = max(get_height(y->left), get_height(y->right)) + 1;
    x->height = max(get_height(x->left), get_height(x->right)) + 1;
    return x;
}

/* Left rotation */
db_node* left_rotate(db_node *x) {
    db_node *y = x->right;
    db_node *T2 = y->left;
    y->left = x;
    x->right = T2;
    x->height = max(get_height(x->left), get_height(x->right)) + 1;
    y->height = max(get_height(y->left), get_height(y->right)) + 1;
    return y;
}


/* Create a new AVL Node for path_msg */
db_node* create_node(void *data) {
    db_node *node = (db_node*)malloc(sizeof(db_node));
    if (!node) {
        log_message("Memory allocation failed!\n");
        return NULL;
    }
    node->data = data;
    node->left = node->right = NULL;
    node->height = 1;
    return node;
}

/* Insert a path_msg node */
db_node* insert_node(db_node *node, void *data, int (*cmp1)(const void *, const void *), uint8_t msg_type) {
    if (!node) return create_node(data);

    int cmp_result = cmp1(data, node->data);

    if (cmp_result < 0) {
        node->left = insert_node(node->left, data, cmp1, msg_type);
    } else if (cmp_result > 0) {
        node->right = insert_node(node->right, data, cmp1, msg_type);
    } else {
        return node;
        // Tunnel IDs match, compare destination IPs
        /*struct in_addr new_dest_ip = {0};
          struct in_addr old_dest_ip = {0};
          uint16_t tunnel_id = 0;

          if (msg_type == 1) { // path_msg
          path_msg *new_path = (path_msg*)data;
          path_msg *old_path = (path_msg*)node->data;
          new_dest_ip = new_path->dest_ip;
          old_dest_ip = old_path->dest_ip;
          tunnel_id = old_path->tunnel_id;
          } else {
          resv_msg *new_resv = (resv_msg*)data;
          resv_msg *old_resv = (resv_msg*)node->data;
          new_dest_ip = new_resv->dest_ip;
          old_dest_ip = old_resv->dest_ip;
          tunnel_id = old_resv->tunnel_id;
          }

          if (new_dest_ip.s_addr == old_dest_ip.s_addr) {
        // Destination IPs match, update the node
        log_message("Tunnel ID %d exists with same destination. Updating node.", tunnel_id);
        free(node->data); // Free the old data structure
        node->data = data; // Assign the new data structure
                           // Node structure (left/right pointers, height) remains the same
                           return node; // Return the updated node
                           } else {
        // Destination IPs differ, log conflict and discard new data
        char new_ip_str[INET_ADDRSTRLEN];
        char old_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &new_dest_ip, new_ip_str, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &old_dest_ip, old_ip_str, INET_ADDRSTRLEN);

        log_message("Error: Tunnel ID %d already exists but with different destination IP (%s vs new %s).",
        tunnel_id, old_ip_str, new_ip_str);
        log_message("Delete the existing entry first before adding the new one.");

        free(data); // Free the new data that won't be inserted
        return node; // Return the original node without modification
        }*/
    }

    // Update height and rebalance (only if insertion happened)
    node->height = 1 + max(get_height(node->left), get_height(node->right));
    int balance = get_balance(node);

    // Perform rotations if unbalanced (standard AVL logic)
    if (balance > 1 && cmp1(data, node->left->data) < 0)
        return right_rotate(node);
    if (balance < -1 && cmp1(data, node->right->data) > 0)
        return left_rotate(node);
    if (balance > 1 && cmp1(data, node->left->data) > 0) {
        node->left = left_rotate(node->left);
        return right_rotate(node);
    }
    if (balance < -1 && cmp1(data, node->right->data) < 0) {
        node->right = right_rotate(node->right);
        return left_rotate(node);
    }

    return node;
}

/* Utility function to get the minimum value node */
db_node* min_node(db_node* node) {
    db_node* current = node;
    while (current->left != NULL)
        current = current->left;
    return current;
}

/* Delete a node from path_msg AVL tree */
db_node* delete_node(db_node* node, uint16_t tunnel_id, int (*cmp)(uint16_t , const void *), uint8_t msg) {
    if (node == NULL) return NULL;

    if (cmp(tunnel_id, node->data) < 0) {
        node->left = delete_node(node->left, tunnel_id, cmp, msg);
    } else if (cmp(tunnel_id, node->data) > 0) {
        node->right = delete_node(node->right, tunnel_id, cmp, msg);
    } else {
        // Node with only one child or no child
        if ((node->left == NULL) || (node->right == NULL)) {
            db_node* temp = node->left ? node->left : node->right;
            db_node* node_to_free = node;
            node = temp;

            if (node_to_free != NULL) {
                if (node_to_free->data != NULL) {
                    if(msg) {
                        free((path_msg*) node_to_free->data);
                    } else {
                        free((resv_msg*) node_to_free->data);
                    }
                    node_to_free->data = NULL;
                }
                free(node_to_free);
            }

        } else {
            db_node* temp = min_node(node->right);
            void* data_keep = temp->data;
            void* data_remove = node->data;
            node->data = data_keep;
            temp->data = data_remove;

            if (msg && data_remove != NULL) {
                node->right = delete_node(node->right, ((path_msg *)data_remove)->tunnel_id, cmp, msg);
            } else if (!msg && data_remove != NULL) {
                node->right = delete_node(node->right, ((resv_msg *)data_remove)->tunnel_id, cmp, msg);
            }
        }
    }

    if (node == NULL) return node;

    node->height = 1 + max(get_height(node->left), get_height(node->right));
    int balance = get_balance(node);

    // Perform rotations if needed
    if (balance > 1 && get_balance(node->left) >= 0)
        return right_rotate(node);
    if (balance > 1 && get_balance(node->left) < 0) {
        node->left = left_rotate(node->left);
        return right_rotate(node);
    }
    if (balance < -1 && get_balance(node->right) <= 0)
        return left_rotate(node);
    if (balance < -1 && get_balance(node->right) > 0) {
        node->right = right_rotate(node->right);
        return left_rotate(node);
    }

    return node;
}

/* Search for a path_msg node */
db_node* search_node(db_node *node, uint16_t data, int (*cmp)(uint16_t, const void *)) {
    if (node == NULL) {
        return node;

    }
    if (cmp(data, node->data) == 0)
        return node;

    if (cmp(data, node->data) < 0) { 
        return search_node(node->left, data, cmp);
    } else {
        return search_node(node->right, data, cmp);
    }
}


void update_tables(void *arg) {
    ThreadArgs *p = (ThreadArgs*)arg;
    char d_ip[16], n_ip[16], command[200];

    //resv_msg *p = (resv_msg*)resv_node->data;
    
    struct in_addr net, mask;
    char network[16];

    mask.s_addr = htonl(~((1 << (32 - p->prefix_len)) - 1));
    net.s_addr = p->dest_ip.s_addr & mask.s_addr; 

    inet_ntop(AF_INET, &net, network, 16);
    inet_ntop(AF_INET, &p->p_srcip, n_ip, 16);

    //update LFIB table
    if(p->in_label == -1 && (p->out_label >= BASE_LABEL)) {
        //push label delete
        snprintf(command, sizeof(command), "ip route del %s/%d encap mpls %d via %s dev %s",
                network, p->prefix_len, (p->out_label), n_ip, p->dev);
        log_message(" ========== 1 %s \n", command);
	system(command);
    } else if(p->in_label >= BASE_LABEL && p->out_label >= BASE_LABEL) {
        //swap label delete
        snprintf(command, sizeof(command), "ip -M route del %d as %d via inet %s",
                (p->in_label), (p->out_label), n_ip);
        log_message(" ========== 3 %s - ", command);
        system(command);
    } else if(p->in_label >= BASE_LABEL && (p->out_label == IMPLICIT_NULL || p->out_label == EXPLICIT_NULL)) {
        //explicit label =  3 delete
        snprintf(command, sizeof(command), "ip -M route del %d via inet %s dev %s",
                (p->in_label), n_ip, p->dev);
        log_message(" ========== 2 %s - ", command);
        system(command);
    } else {
        //not a valid label
    }

    free(p);

    //update labels
    free_label(p->in_label);
}

/* Free a path tree */
void free_tree(db_node *node) {
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    free(node->data);
    free(node);
}

void display_tree(db_node *node, uint8_t msg, char *buffer, size_t buffer_size) {
    if (node == NULL) return;

    // In-order traversal: left, root, right
    display_tree(node->left, msg, buffer, buffer_size);

    char temp[256];
    size_t current_len = strlen(buffer);
    size_t remaining_size = buffer_size - current_len;

    if (remaining_size <= 1) return; // No space left (leave room for null terminator)

    if (msg) { // PATH tree (msg == 1)
        path_msg *p = (path_msg*)node->data;
        //log_message("display tree dest ip %s", inet_ntoa(p->dest_ip));
        inet_ntop(AF_INET, &p->src_ip, source_ip, 16);
        inet_ntop(AF_INET, &p->dest_ip, destination_ip, 16);
        inet_ntop(AF_INET, &p->nexthop_ip, next_hop_ip, 16);
        snprintf(temp, sizeof(temp), 
                "Tunnel ID: %d, Src: %s, Dst: %s, NextHop: %s, Name: %s\n",
                p->tunnel_id, source_ip, destination_ip,
                next_hop_ip, p->name);
    } else { // RESV tree (msg == 0)
        resv_msg *r = (resv_msg*)node->data;
        inet_ntop(AF_INET, &r->src_ip, source_ip, 16);
        inet_ntop(AF_INET, &r->dest_ip, destination_ip, 16);
        inet_ntop(AF_INET, &r->nexthop_ip, next_hop_ip, 16);
        snprintf(temp, sizeof(temp),
                "Tunnel ID: %u, Src: %s, Dest: %s, Next Hop: %s, In_label: %d, Out_label: %d\n",
                r->tunnel_id, source_ip, destination_ip, next_hop_ip, r->in_label,
                r->out_label);
    }

    // Append to buffer, ensuring we don't overflow
    strncat(buffer, temp, remaining_size - 1);
    buffer[buffer_size - 1] = '\0'; // Ensure null termination

    display_tree(node->right, msg, buffer, buffer_size);
}

/* Display path tree (inorder traversal) */
void display_tree_debug(db_node *node, uint8_t msg) {
    char Psrcip[16], Esrcip[16];
    if (node == NULL){ 
        log_message("No nodes in tree");
        return;
    }
    display_tree_debug(node->left, msg);
    if(msg) {
        path_msg* p = node->data;
        inet_ntop(AF_INET, &p->src_ip, source_ip, 16);
        inet_ntop(AF_INET, &p->dest_ip, destination_ip, 16);
        inet_ntop(AF_INET, &p->nexthop_ip, next_hop_ip, 16);
        inet_ntop(AF_INET, &p->p_srcip, Psrcip, 16);
        inet_ntop(AF_INET, &p->e_srcip, Esrcip, 16);
        log_message("Tunnel ID: %u, Src: %s, Dest: %s, Next Hop: %s Psrcip = %s Esrcip = %s\n",
                p->tunnel_id,
                source_ip,
                destination_ip,
                next_hop_ip,
                Psrcip,
                Esrcip);
    } else {
        resv_msg* r = node->data;
        inet_ntop(AF_INET, &r->src_ip, source_ip, 16);
        inet_ntop(AF_INET, &r->dest_ip, destination_ip, 16);
        inet_ntop(AF_INET, &r->nexthop_ip, next_hop_ip, 16);
        inet_ntop(AF_INET, &r->p_srcip, Psrcip, 16);
        inet_ntop(AF_INET, &r->e_srcip, Esrcip, 16);
        log_message("Tunnel ID: %u, Src: %s, Dest: %s, Next Hop: %s, Psrcip = %s, Esrcip = %s, prefix_len: %d, In_label: %d, Out_label: %d\n",
                r->tunnel_id,
                source_ip,
                destination_ip,
                next_hop_ip,
                Psrcip,
                Esrcip,
                r->prefix_len,
                (r->in_label),
                (r->out_label));
    }
    display_tree_debug(node->right, msg);
}

//Fetch information from receive buffer
//-------------------------------------

db_node* path_tree_insert(db_node* path_tree, char buffer[]) {
    uint32_t ifh = 0;
    uint8_t prefix_len = 0;
    char dev[16];
    char srcip[16];

    struct session_object *session_obj = (struct session_object*)(buffer + START_RECV_SESSION_OBJ);
    struct hop_object *hop_obj = (struct hop_object*)(buffer + START_RECV_HOP_OBJ);
    struct time_object *time_obj = (struct time_object*)(buffer + START_RECV_TIME_OBJ);
    struct session_attr_object *session_attr_obj = (struct session_attr_object*)(buffer + START_RECV_SESSION_ATTR_OBJ);

    path_msg *p = malloc(sizeof(path_msg));

    p->tunnel_id = htons(session_obj->tunnel_id);
    p->src_ip = (session_obj->src_ip);
    p->dest_ip = (session_obj->dst_ip);
    p->p_srcip = hop_obj->next_hop;
    p->interval = time_obj->interval;
    p->setup_priority = session_attr_obj->setup_prio;
    p->hold_priority = session_attr_obj->hold_prio;
    p->flags = session_attr_obj->flags;
    p->lsp_id = 1;
    strncpy(p->name, session_attr_obj->Name, sizeof(session_attr_obj->Name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';

    if(get_nexthop(inet_ntoa(p->dest_ip), nhip, &prefix_len, dev, &ifh)) {
        strcpy(p->dev, dev);
        //p->IFH = ifh;
        if(strcmp(nhip, " ") == 0) {
            inet_pton(AF_INET, "0.0.0.0", &p->nexthop_ip);
            inet_pton(AF_INET, "0.0.0.0", &p->e_srcip);
            p->IFH = 0;
            p->prefix_len = prefix_len;
        }
        else {
            inet_pton(AF_INET, nhip, &p->nexthop_ip);
            p->prefix_len = prefix_len;
            if(get_srcip(nhip, srcip, &ifh)) {
                inet_pton(AF_INET, srcip, &p->e_srcip);
                p->IFH = ifh;
            }
        }
    } else {
        log_message("No route to destination\n");
        free(p);
        return NULL;
    }

    return insert_node(path_tree, p, compare_path_insert, 1);
}

db_node* resv_tree_insert(db_node* resv_tree, char buffer[], struct in_addr p_nhip, uint8_t path_dst_reach) {

    uint32_t ifh = 0;
    char srcip[16], nhip[16];

    struct session_object *session_obj = (struct session_object*)(buffer + START_RECV_SESSION_OBJ);
    struct hop_object *hop_obj = (struct hop_object*)(buffer + START_RECV_HOP_OBJ);
    struct time_object *time_obj = (struct time_object*)(buffer + START_RECV_TIME_OBJ);
    struct label_object *label_obj = (struct label_object*)(buffer + START_RECV_LABEL);

    resv_msg *p = (resv_msg*)malloc(sizeof(resv_msg));

    p->tunnel_id = ntohs(session_obj->tunnel_id);
    p->src_ip = (session_obj->src_ip);
    p->dest_ip = (session_obj->dst_ip);
    p->nexthop_ip = p_nhip; 
    p->interval = time_obj->interval;

    inet_ntop(AF_INET, &p->nexthop_ip, nhip, INET_ADDRSTRLEN);
    if(path_dst_reach) {
        p->in_label = (3);
        p->out_label = (-1);
        inet_pton(AF_INET, "0.0.0.0", &p->p_srcip);
        if(get_srcip(nhip, srcip, &ifh)) {
            inet_pton(AF_INET, srcip, &p->e_srcip);
            p->IFH = ifh;
        }
    }

    if(!path_dst_reach) {
        p->out_label = ntohl(label_obj->label);
        p->p_srcip = hop_obj->next_hop;

        if(strcmp(inet_ntoa(p->nexthop_ip), "0.0.0.0") == 0) {
            p->in_label = (-1);
            inet_pton(AF_INET,"0.0.0.0", &p->e_srcip);
            p->IFH = 0;
        } else {
            p->in_label = allocate_label();
            if(get_srcip(nhip, srcip, &ifh)) {
                inet_pton(AF_INET, srcip, &p->e_srcip);
                p->IFH = ifh;
            }
        }
    }
    return insert_node(resv_tree, p, compare_resv_insert, 0);
}
