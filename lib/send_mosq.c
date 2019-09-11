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
#  include "sys_tree.h"
#else
#  define G_PUB_BYTES_SENT_INC(A)
#endif

#include "mosquitto.h"
#include "mosquitto_internal.h"
#include "logging_mosq.h"
#include "mqtt_protocol.h"
#include "memory_mosq.h"
#include "net_mosq.h"
#include "packet_mosq.h"
#include "property_mosq.h"
#include "send_mosq.h"
#include "time_mosq.h"
#include "util_mosq.h"


#ifdef WITH_BROKER
int send__pingreq(struct mosquitto_db *db, struct mosquitto *mosq)
#else
int send__pingreq(struct mosquitto *mosq)
#endif
{
	int rc;
	assert(mosq);
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PINGREQ to %s", mosq->id);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PINGREQ", mosq->id);
#endif
    
#ifdef WITH_BROKER
    rc = send__complex_command(db, mosq, CMD_PINGREQ);
#else
    rc = send__simple_command(mosq, CMD_PINGREQ);
#endif
	if(rc == MOSQ_ERR_SUCCESS){
		mosq->ping_t = mosquitto_time();
	}
	return rc;
}

int send__pingresp(struct mosquitto *mosq)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PINGRESP to %s", mosq->id);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PINGRESP", mosq->id);
#endif
	return send__simple_command(mosq, CMD_PINGRESP);
}

int send__puback(struct mosquitto *mosq, uint16_t mid, uint8_t reason_code)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PUBACK to %s (m%d, rc%d)", mosq->id, mid, reason_code);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PUBACK (m%d, rc%d)", mosq->id, mid, reason_code);
#endif
	util__increment_receive_quota(mosq);
	/* We don't use Reason String or User Property yet. */
	return send__command_with_mid(mosq, CMD_PUBACK, mid, false, reason_code, NULL);
}

int send__pubcomp(struct mosquitto *mosq, uint16_t mid)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PUBCOMP to %s (m%d)", mosq->id, mid);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PUBCOMP (m%d)", mosq->id, mid);
#endif
	util__increment_receive_quota(mosq);
	/* We don't use Reason String or User Property yet. */
	return send__command_with_mid(mosq, CMD_PUBCOMP, mid, false, 0, NULL);
}


int send__pubrec(struct mosquitto *mosq, uint16_t mid, uint8_t reason_code)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PUBREC to %s (m%d, rc%d)", mosq->id, mid, reason_code);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PUBREC (m%d, rc%d)", mosq->id, mid, reason_code);
#endif
	if(reason_code >= 0x80 && mosq->protocol == mosq_p_mqtt5){
		util__increment_receive_quota(mosq);
	}
	/* We don't use Reason String or User Property yet. */
	return send__command_with_mid(mosq, CMD_PUBREC, mid, false, reason_code, NULL);
}

int send__pubrel(struct mosquitto *mosq, uint16_t mid)
{
#ifdef WITH_BROKER
	log__printf(NULL, MOSQ_LOG_DEBUG, "Sending PUBREL to %s (m%d)", mosq->id, mid);
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending PUBREL (m%d)", mosq->id, mid);
#endif
	/* We don't use Reason String or User Property yet. */
	return send__command_with_mid(mosq, CMD_PUBREL|2, mid, false, 0, NULL);
}

/* For PUBACK, PUBCOMP, PUBREC, and PUBREL */
int send__command_with_mid(struct mosquitto *mosq, uint8_t command, uint16_t mid, bool dup, uint8_t reason_code, const mosquitto_property *properties)
{
	struct mosquitto__packet *packet = NULL;
	int rc;
	int proplen, varbytes;

	assert(mosq);
	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	packet->command = command;
	if(dup){
		packet->command |= 8;
	}
	packet->remaining_length = 2;

	if(mosq->protocol == mosq_p_mqtt5){
		if(reason_code != 0 || properties){
			packet->remaining_length += 1;
		}

		if(properties){
			proplen = property__get_length_all(properties);
			varbytes = packet__varint_bytes(proplen);
			packet->remaining_length += varbytes + proplen;
		}
	}

	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}

	packet__write_uint16(packet, mid);

	if(mosq->protocol == mosq_p_mqtt5){
		if(reason_code != 0 || properties){
			packet__write_byte(packet, reason_code);
		}
		if(properties){
			property__write_all(packet, properties, true);
		}
	}

	return packet__queue(mosq, packet);
}

/* For DISCONNECT, PINGREQ and PINGRESP */
int send__simple_command(struct mosquitto *mosq, uint8_t command)
{
	struct mosquitto__packet *packet = NULL;
	int rc;

	assert(mosq);
	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	packet->command = command;
	packet->remaining_length = 0;

	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}

	return packet__queue(mosq, packet);
}

/* For custom PINGREQ */
#ifdef WITH_BROKER
int send__complex_command(struct mosquitto_db *db, struct mosquitto *mosq, uint8_t command)
{
    struct mosquitto__packet *packet = NULL;
    int payloadlen;
    uint8_t byte;
    int rc;
    uint8_t version;
    char *broker_id, *root_dist, *root_id;
    int res_pid;
    
    int headerlen;
    int proplen = 0, varbytes;
    mosquitto_property *local_props = NULL;
    uint16_t receive_maximum;
    
    assert(mosq);
    
    broker_id = "";//db->stp.broker_id;
    root_dist = ""; //(char *) db->stp.root_distance;
    root_id = ""; //(char*) db->stp.root_id;
    
    res_pid = db->stp.res.pid;
    
    if(mosq->protocol == mosq_p_mqtt5){
        /* Generate properties from options */
        if(!mosquitto_property_read_int16(NULL, MQTT_PROP_RECEIVE_MAXIMUM, &receive_maximum, false)){
            rc = mosquitto_property_add_int16(&local_props, MQTT_PROP_RECEIVE_MAXIMUM, mosq->msgs_in.inflight_maximum);
            if(rc) return rc;
        }else{
            mosq->msgs_in.inflight_maximum = receive_maximum;
            mosq->msgs_in.inflight_quota = receive_maximum;
        }
        
        version = MQTT_PROTOCOL_V5;
        headerlen = 10;
        proplen = 0;
        proplen += property__get_length_all(NULL);
        proplen += property__get_length_all(local_props);
        varbytes = packet__varint_bytes(proplen);
        headerlen += proplen + varbytes;
    }else if(mosq->protocol == mosq_p_mqtt311){
        version = MQTT_PROTOCOL_V311;
        headerlen = 10;
    }else if(mosq->protocol == mosq_p_mqtt31){
        version = MQTT_PROTOCOL_V31;
        headerlen = 12;
    }else{
        return MOSQ_ERR_INVAL;
    }
    
    packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
    if(!packet) return MOSQ_ERR_NOMEM;
    
    /* Set payload length */
    if(broker_id){
        payloadlen = 2+strlen(broker_id);
    }else{
        payloadlen = 2;
    }
    
    if(root_dist){
        payloadlen += 2+strlen(root_dist);
    }
    
    packet->command = command;
    packet->remaining_length = headerlen + payloadlen;
    
    /* Memory allocation */
    rc = packet__alloc(packet);
    if(rc){
        mosquitto__free(packet);
        return rc;
    }
    
    /* Variable header */
    if(version == MQTT_PROTOCOL_V31){
        packet__write_string(packet, PROTOCOL_NAME_v31, strlen(PROTOCOL_NAME_v31));
    }else{
        packet__write_string(packet, PROTOCOL_NAME, strlen(PROTOCOL_NAME));
    }
    
    /* Check better */
    byte = (1&0x1)<<1; //different (clean_session&0x1)<<1;
    
    if(root_dist){
        byte = byte | 0x1<<2;
    }
    
    packet__write_byte(packet, byte);
    packet__write_uint16(packet, 6); //6 is keepalive
    
    if(mosq->protocol == mosq_p_mqtt5){
        /* Write properties */
        packet__write_varint(packet, proplen);
        property__write_all(packet, NULL, false);
        property__write_all(packet, local_props, false);
    }
    
    /* Payload */
    if(broker_id){
        packet__write_string(packet, broker_id, strlen(broker_id));
    }else{
        packet__write_uint16(packet, 0);
    }
    
    if(root_dist){
        packet__write_string(packet, root_dist, strlen(root_dist));
    }
    
    log__printf(mosq, MOSQ_LOG_DEBUG, "Sending COMPLEX PING");
    return packet__queue(mosq, packet);
}
#endif

