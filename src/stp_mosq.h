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

struct mosquitto__stp* stp__init(struct mosquitto__stp *stp, char *hostname, int port, int pid);
void print_stp(struct mosquitto__stp *stp);
char *create_full_hostname(char *address, int port);
void ciao();
#endif
