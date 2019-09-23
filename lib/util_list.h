//
//  util_list.h
//  libmosquitto
//
//  Created by drosdesd on 22/09/2019.
//

#ifndef UTIL_LIST_H
#define UTIL_LIST_H
#include <stdio.h>

#include "mosquitto.h"
#include "mosquitto_internal.h"
#ifdef WITH_BROKER
#  include "mosquitto_broker_internal.h"
#endif

#ifdef WITH_BROKER
int init_list(PORT_LIST** head, char *type);
PORT_LIST* add(PORT_LIST* node, BROKER broker);
PORT_LIST* delete_node(PORT_LIST* head, BROKER broker);
PORT_LIST* empty_list(PORT_LIST *head);
bool in_list(PORT_LIST* head, char *address, int port);
void print_list(PORT_LIST* head, char *type);
#endif

#endif /* util_list_h */
