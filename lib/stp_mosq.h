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

void superior_update(struct mosquitto__bpdu__packet *stored_bpdu, struct mosquitto__bpdu__packet *packet, struct mosquitto__stp *stp);
int update_bpdu(struct mosquitto__bpdu__packet *stored_bpdu, struct mosquitto__bpdu__packet *packet);
#ifdef WITH_BROKER
void check_old_info(struct mosquitto__stp *stp, struct mosquitto__bpdu__packet *packet,  struct mosquitto_db *db, int bridge);
#endif
int update_stp(struct mosquitto__stp *stp, struct mosquitto__bpdu__packet *packet);
char *create_full_hostname(char *address, int port);
#endif
