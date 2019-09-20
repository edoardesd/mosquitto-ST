//
//  stp_mosq.c
//  libmosquitto
//
//  Created by drosdesd on 20/09/2019.
//
#include "config.h"

#include "mosquitto.h"
#include "mosquitto_internal.h"
#include "mosquitto_broker_internal.h"

#include "stp_mosq.h"
#include "logging_mosq.h"


struct mosquitto__stp* stp__init(struct mosquitto__stp *stp, char *hostname, int port, int pid)
{
    if(stp){
        stp->distance = 0;
        stp->my_id = hostname;
        stp->root_id = hostname;
        stp->my->_id = NULL;
        stp->my->address = NULL;
        stp->my->port = port;
        stp->my->res->pid = pid;
        stp->my_root->_id = NULL;
        stp->my_root->address = NULL;
        stp->my_root->port = port;
        stp->my_root->res->pid = pid;
        return stp;
    }
    return stp;
}

void print_stp(struct mosquitto__stp *stp)
{
    log__printf(NULL, MOSQ_LOG_INFO, "d%d r(%d-%d) o(%d-%d)", stp->distance, stp->my_root->port, stp->my_root->res->pid, stp->my->port, stp->my->res->pid);
}

void ciao()
{
    log__printf(NULL, MOSQ_LOG_INFO, "CIAO");
}

char *create_full_hostname(char *address, int port)
{
    //char* hostname;
    char *port_c;
    strcat(address, ":");
    sprintf(port_c, "%d", port);
    strcat(address, port_c);
    log__printf(NULL, MOSQ_LOG_INFO, "%s", address);
    
    return address;
}
