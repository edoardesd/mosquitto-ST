/*
Copyright (c) 2009-2019 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

Contributors:
   Roger Light - initial implementation and documentation.
*/

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef WITH_BROKER
#  include "mosquitto_broker_internal.h"
#endif

#include "mosquitto.h"
#include "logging_mosq.h"
#include "memory_mosq.h"
#include "messages_mosq.h"
#include "mqtt_protocol.h"
#include "net_mosq.h"
#include "packet_mosq.h"
#include "read_handle.h"
#include "send_mosq.h"
#include "util_mosq.h"

int handle__pingreq(struct mosquitto *mosq)
{
	assert(mosq);

	if(mosq->state != mosq_cs_connected){
		return MOSQ_ERR_PROTOCOL;
	}

#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Received PINGREQ from %s", mosq->id);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s received PINGREQ", mosq->id);
#endif
	return send__pingresp(mosq);
}

int handle__pingresp(struct mosquitto *mosq)
{
	assert(mosq);

	if(mosq->state != mosq_cs_connected){
		return MOSQ_ERR_PROTOCOL;
	}

	mosq->ping_t = 0; /* No longer waiting for a PINGRESP. */
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Received PINGRESP from %s", mosq->id);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s received PINGRESP", mosq->id);
#endif
	return MOSQ_ERR_SUCCESS;
}

int handle__pingreqcomp(struct mosquitto_db *db, struct mosquitto *mosq)
{
    char protocol_name[7];
    mosquitto_property *properties = NULL;
    uint8_t protocol_version;
    uint8_t connect_flags;
    int rc = 0;
    int slen;
    uint16_t slen16;
    uint16_t keepalive = 0;
    struct mosquitto__bpdu__packet *recv_packet;
    
    assert(mosq);
    
    if(mosq->state != mosq_cs_connected){
        return MOSQ_ERR_PROTOCOL;
    }
    
    recv_packet = mosquitto__calloc(1, sizeof(struct mosquitto__bpdu__packet));
    if(!recv_packet) return MOSQ_ERR_NOMEM;
    
    if(packet__read_uint16(&mosq->in_packet, &slen16)){
        rc = 1;
    }
    slen = slen16;
    
    if(slen != 4 /* MQTT */ && slen != 6 /* MQIsdp */){
        if(rc){
            log__printf(NULL, MOSQ_LOG_DEBUG, "Received PINGREQ from client %s", mosq->id);
            rc = MOSQ_ERR_PROTOCOL;
        
            return send__pingresp(mosq);
        }
    }else{
        /* Now we are sure that the request is from a bridged broker */
        log__printf(NULL, MOSQ_LOG_DEBUG, "Received complex PINGREQ from %s", mosq->id);
    }
    
    
    if(packet__read_bytes(&mosq->in_packet, protocol_name, slen)){
        rc = MOSQ_ERR_PROTOCOL;
        goto handle_connect_error;
    }
    protocol_name[slen] = '\0';
    
    if(packet__read_byte(&mosq->in_packet, &protocol_version)){
        rc = 1;
        goto handle_connect_error;
    }
    
    //TODO check protocols routine
    if((mosq->in_packet.command&0x0F) != 0x00){
        log__printf(NULL, MOSQ_LOG_DEBUG, "Reserved flags not set to 0, must disconnect.");
        rc = MOSQ_ERR_PROTOCOL;
        goto handle_connect_error;
    }
    
    if(packet__read_byte(&mosq->in_packet, &connect_flags)){
        rc = 1;
        goto handle_connect_error;
    }
    
    if(mosq->protocol == mosq_p_mqtt311 || mosq->protocol == mosq_p_mqtt5){
        if((connect_flags & 0x01) != 0x00){
            rc = MOSQ_ERR_PROTOCOL;
            goto handle_connect_error;
        }
    }
    
    if(packet__read_uint16(&mosq->in_packet, &keepalive)){
        rc = 1;
        goto handle_connect_error;
    }
    if(protocol_version == PROTOCOL_VERSION_v5){
        rc = property__read_all(CMD_CONNECT, &mosq->in_packet, &properties);
        if(rc) {
            log__printf(NULL, MOSQ_LOG_ERR, "Error properties");
            goto handle_connect_error;
        }
    }
    
    /* Source properties */
    if(packet__read_string(&mosq->in_packet, &recv_packet->origin_address, &slen)){
        rc = 1;
        goto handle_connect_error;
    }
    if(packet__read_string(&mosq->in_packet, &recv_packet->origin_port, &slen)){
        rc = 1;
        goto handle_connect_error;
    }
    if(packet__read_string(&mosq->in_packet, &recv_packet->origin_id, &slen)){
        rc = 1;
        goto handle_connect_error;
    }
    
    /* Root properties */
    if(packet__read_string(&mosq->in_packet, &recv_packet->root_address, &slen)){
        rc = 1;
        goto handle_connect_error;
    }
    if(packet__read_string(&mosq->in_packet, &recv_packet->root_port, &slen)){
        rc = 1;
        goto handle_connect_error;
    }
    if(packet__read_string(&mosq->in_packet, &recv_packet->root_id, &slen)){
        rc = 1;
        goto handle_connect_error;
    }
    
    /* Other */
    if(packet__read_string(&mosq->in_packet, &recv_packet->distance, &slen)){
        rc = 1;
        goto handle_connect_error;
    }
    if(packet__read_string(&mosq->in_packet, &recv_packet->origin_pid, &slen)){
        rc = 1;
        goto handle_connect_error;
    }
    
    log__printf(NULL, MOSQ_LOG_DEBUG, "[PING] Source_address: %s, source_port: %s, source_id: %s", recv_packet->origin_address, recv_packet->origin_port, recv_packet->origin_id);
    log__printf(NULL, MOSQ_LOG_DEBUG, "[PING] Root_address: %s, root_port: %s, root_id: %s", recv_packet->root_address, recv_packet->root_port, recv_packet->root_id);
    log__printf(NULL, MOSQ_LOG_DEBUG, "[PING] Source_pid: %s, root_distance: %s", recv_packet->origin_pid, recv_packet->distance);

     /* Store packet fields */
#ifdef WITH_BROKER
    if(update__stp_properties(db, recv_packet)){
        log__printf(NULL, MOSQ_LOG_ERR, "Impossible to update STP fields.");
    }
#endif

    return send__pingresp(mosq);
    
    
//TODO handle connect herror
handle_connect_error:
    /*
    mosquitto__free(auth_data);
    mosquitto__free(client_id);
    mosquitto__free(username);
    mosquitto__free(password);
    if(will_struct){
        mosquitto_property_free_all(&will_struct->properties);
        mosquitto__free(will_struct->msg.payload);
        mosquitto__free(will_struct->msg.topic);
        mosquitto__free(will_struct);
    }
    */
#ifdef WITH_TLS
    if(client_cert) X509_free(client_cert);
#endif
    /* We return an error here which means the client is freed later on. */
    return rc;
}
