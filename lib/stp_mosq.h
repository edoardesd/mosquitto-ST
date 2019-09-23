//
//  stp_mosq.h
//  libmosquitto
//
//  Created by drosdesd on 20/09/2019.
//

#ifndef STP_MOSQ_H
#define STP_MOSQ_H

#include "mosquitto.h"
#include "mosquitto_internal.h"
#ifdef WITH_BROKER
#include "mosquitto_broker_internal.h"
#endif


struct mosquitto__stp* stp__init(struct mosquitto__stp *stp, char* hostname, int port, int pid);
void print_stp(struct mosquitto__stp *stp);
#ifdef WITH_BRIDGE
struct mosquitto__bpdu__packet *find_bridge(struct mosquitto_db *db, struct mosquitto__bpdu__packet *packet, int origin_port, int i);
bool check_repeated(struct mosquitto__bpdu__packet *stored_bpdu, struct mosquitto__bpdu__packet *packet);
#endif
char *create_full_hostname(char *address, int port);
#endif
