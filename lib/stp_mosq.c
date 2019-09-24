//
//  stp_mosq.c
//  libmosquitto
//
//  Created by drosdesd on 20/09/2019.
//
#include <stdio.h>
#include <string.h>
#include "config.h"

#include "stp_mosq.h"
#include "util_string.h"
#include "logging_mosq.h"
#include "packet_mosq.h"


struct mosquitto__stp* stp__init(struct mosquitto__stp *stp, char* hostname, int port, int pid)
{
    if(stp){
        stp->distance = 0;
        stp->my->address = hostname;
        stp->my->port = port;
        stp->my->res->pid = pid;
        
        stp->my_root->address = hostname;
        stp->my_root->port = port;
        stp->my_root->res->pid = pid;
        return stp;
    }
    return stp;
}

void print_stp(struct mosquitto__stp *stp)
{
    log__printf(NULL, MOSQ_LOG_NOTICE, "d%d r(%d-%d) o(%d-%d)", stp->distance, stp->my_root->port, stp->my_root->res->pid, stp->my->port, stp->my->res->pid);
}

int update_bpdu(struct mosquitto__bpdu__packet *stored_bpdu, struct mosquitto__bpdu__packet *packet)
{
    if(stored_bpdu && packet){
        stored_bpdu->root_pid = packet->root_pid;
        stored_bpdu->root_address = packet->root_address;
        stored_bpdu->root_port = packet->root_port;
        stored_bpdu->distance = packet->distance;
        stored_bpdu->origin_pid = packet->origin_pid;
        stored_bpdu->origin_address = packet->origin_address;
        stored_bpdu->origin_port = packet->origin_port;
                
        return 1;
    }
    return 0;
}

int update_stp(struct mosquitto__stp *stp, struct mosquitto__bpdu__packet *packet)
{
    if(stp && packet){
        stp->distance = strint(packet->distance) +1;
        stp->my_root->address = packet->root_address;
        stp->my_root->port = strint(packet->root_port);
        stp->my_root->res->pid = strint(packet->root_pid);
        
        return 1;
    }
    return 0;
}

void superior_update(struct mosquitto__bpdu__packet *stored_bpdu, struct mosquitto__bpdu__packet *packet, struct mosquitto__stp *stp)
{
    log__printf(NULL, MOSQ_LOG_DEBUG, "Superior information, UPDATE");
    //update_bpdu(stored_bpdu, packet);
    
    update_stp(stp, packet);
}

char *create_full_hostname(char *address, int port)
{
    char *port_c = convert_integer(port);
    char *name = (char *) malloc(sizeof(char) * 3);
    
    strcpy(name, address);
    strcat(name, ":");
    strcat(name, port_c);
    
    return name;
}
