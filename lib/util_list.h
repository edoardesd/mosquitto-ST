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
PORT_LIST* add(PORT_LIST* node, BROKER brk);
PORT_LIST* delete_node(PORT_LIST* head, BROKER brk);
PORT_LIST* empty_list(PORT_LIST *head);
bool in_list(PORT_LIST* head, BROKER brk);
void print_list(PORT_LIST* head, char *type);
PORT_LIST* find_and_delete(PORT_LIST *head, BROKER broker);
PORT_LIST* copy_list(PORT_LIST *start1);
bool are_identical(PORT_LIST *a, PORT_LIST *b);

#endif

#endif /* util_list_h */
