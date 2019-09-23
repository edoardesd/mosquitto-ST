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
    log__printf(NULL, MOSQ_LOG_INFO, "d%d r(%d-%d) o(%d-%d)", stp->distance, stp->my_root->port, stp->my_root->res->pid, stp->my->port, stp->my->res->pid);
}

#ifdef WITH_BROKER
struct mosquitto__bpdu__packet* find_bridge(struct mosquitto_db *db, struct mosquitto__bpdu__packet *packet, int origin_port, int i)
{
    struct mosquitto__bpdu__packet *stored_bpdu;
    
    if(strcmp(db->config->bridges[i].addresses->address, packet->origin_address) == 0 && (db->config->bridges[i].addresses->port == origin_port)){
        log__printf(NULL, MOSQ_LOG_DEBUG, "Received msg from %s:%s and i found it in the bridges", packet->origin_address, packet->origin_port);
        stored_bpdu = db->config->bridges[i].last_bpdu;
        log__printf(NULL, MOSQ_LOG_DEBUG, "Reading the stored bpdu... r%s:%s, d%s, o:%s:%s", stored_bpdu->root_address, stored_bpdu->root_port, stored_bpdu->distance, stored_bpdu->origin_address, stored_bpdu->origin_port);
        
        return stored_bpdu;
    }
    return NULL;
}

bool check_repeated(struct mosquitto__bpdu__packet *stored_bpdu, struct mosquitto__bpdu__packet *packet){
    if(strcmp(stored_bpdu->root_address, packet->root_address) == 0 && strcmp(stored_bpdu->root_port, packet->root_port) == 0 && strcmp(stored_bpdu->root_pid, packet->root_pid) == 0){
        if(strcmp(stored_bpdu->origin_address, packet->origin_address) == 0 && strcmp(stored_bpdu->origin_port, packet->origin_port) == 0 && strcmp(stored_bpdu->origin_pid, packet->origin_pid)==0){
            if(strcmp(stored_bpdu->distance, packet->distance) == 0){
                log__printf(NULL, MOSQ_LOG_DEBUG, "Repeated information");
                return true;
            }
        }
    }
    return false;
}

#endif

char *create_full_hostname(char *address, int port)
{
    char *port_c = convert_integer(port);
    char *name = (char *) malloc(sizeof(char) * 3);
    
    strcpy(name, address);
    strcat(name, ":");
    strcat(name, port_c);
    
    return name;
}
