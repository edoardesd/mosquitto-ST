//
//  util_list.c
//  libmosquitto
//
//  Created by drosdesd on 22/09/2019.
//

#include "config.h"

#include <assert.h>
#include <string.h>
#include "mosquitto_internal.h"
#include "mosquitto.h"
#include "util_mosq.h"
#include "logging_mosq.h"
#include "util_string.h"
#include "util_list.h"

#ifdef WITH_BROKER
#   include "mosquitto_broker_internal.h"
#endif


#ifdef WITH_BROKER
int init_list(PORT_LIST** head, char *type)
{
    *head = NULL;
    return MOSQ_ERR_SUCCESS;
}

void print_list(PORT_LIST* head, char *type)
{
    PORT_LIST * temp;
    fprintf(stdout, "List %s:", type);
    for (temp = head; temp; temp = temp->next){
        fprintf(stdout, " %s:%d,", temp->broker.address, temp->broker.port);
    }
    fprintf(stdout, "\n");
}

PORT_LIST* add(PORT_LIST* node, BROKER brk)
{
    PORT_LIST* temp = (PORT_LIST*) malloc(sizeof (PORT_LIST));
    if (temp == NULL) {
        exit(EXIT_FAILURE);
        //return MOSQ_ERR_NOMEM; // no memory available
    }
    temp->broker = brk;
    temp->next = node;
    node = temp;
    return node;
}

bool in_list(PORT_LIST* head, BROKER brk)
{
    PORT_LIST* current = head;
    while(current!= NULL){
        if(strcmp(current->broker.address, brk.address) == 0 && current->broker.port == brk.port){
            return true;
        }
        current = current->next;
    }
    return false;
}

PORT_LIST* delete_node(PORT_LIST* head, BROKER brk)
{
    PORT_LIST* temp, *prev;
    temp = head;
    if(temp != NULL){
        if(strcmp(temp->broker.address, brk.address) == 0 && temp->broker.port == brk.port){
            head = temp->next;
            free(temp);
            log__printf(NULL, MOSQ_LOG_INFO, "Broker %s:%d, deleted!", brk.address, brk.port);
            return head;
        }
    }
    
    while(temp!=NULL){
        if(temp->broker.port == brk.port && strcmp(temp->broker.address, brk.address) == 0){
            break;
        }
        prev = temp;
        temp = temp->next;
    }
    
    if(temp == NULL) return head;

    prev->next = temp->next;
    free(temp);
    return head;
}

PORT_LIST* empty_list(PORT_LIST *head)
{
    PORT_LIST* current = head;
    PORT_LIST* next;
    
    while(current!=NULL){
        next = current->next;
        free(current);
        current = next;
    }
    head = NULL;
    return head;
}

PORT_LIST* remove_from_blocked(PORT_LIST *head, BROKER broker)
{
    if(in_list(head, broker)){
        head = delete_node(head, broker);
    }
    
    return head;
}

PORT_LIST* find_and_delete(PORT_LIST *head, BROKER broker)
{
    if(in_list(head, broker)){
        head = delete_node(head, broker);
    }
    return head;
}

#endif
