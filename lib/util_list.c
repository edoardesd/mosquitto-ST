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
#include "mosquitto_broker_internal.h"
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
        fprintf(stdout, " %d,", temp->broker.port);
    }
    fprintf(stdout, "\n");
}

PORT_LIST* add(PORT_LIST* node, BROKER broker)
{
    PORT_LIST* temp = (PORT_LIST*) malloc(sizeof (PORT_LIST));
    if (temp == NULL) {
        exit(EXIT_FAILURE);
        //return MOSQ_ERR_NOMEM; // no memory available
    }
    temp->broker = broker;
    temp->next = node;
    node = temp;
    log__printf(NULL, MOSQ_LOG_INFO, "Broker %d, added!", broker.port);
    return node;
}

bool in_list(PORT_LIST* head, char *address, int port)
{
    PORT_LIST* current = head;
    while(current!= NULL){
        if(current->broker.port == port) return true;
        current = current->next;
    }
    return false;
}

PORT_LIST* delete_node(PORT_LIST* head, BROKER broker)
{
    PORT_LIST* temp, *prev;
    temp = head;
    if(temp != NULL && temp->broker.port == broker.port){
        head = temp->next;
        free(temp);
        return head;
    }
    
    while(temp!=NULL && temp->broker.port != broker.port){
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
#endif
