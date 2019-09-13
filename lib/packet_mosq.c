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
#include <errno.h>
#include <string.h>

#ifdef WITH_BROKER
#  include "mosquitto_broker_internal.h"
#  ifdef WITH_WEBSOCKETS
#    include <libwebsockets.h>
#  endif
#else
#  include "read_handle.h"
#endif

#include "memory_mosq.h"
#include "mqtt_protocol.h"
#include "net_mosq.h"
#include "packet_mosq.h"
#include "read_handle.h"
#ifdef WITH_BROKER
#  include "sys_tree.h"
#  include "send_mosq.h"
#else
#  define G_BYTES_RECEIVED_INC(A)
#  define G_BYTES_SENT_INC(A)
#  define G_MSGS_SENT_INC(A)
#  define G_PUB_MSGS_SENT_INC(A)
#endif

char *convert_integer(int origin)
{
    char *destination;
    ssize_t bufsz;
    
    bufsz = snprintf(NULL, 0, "%d", origin);
    destination = malloc(bufsz+1);
    snprintf(destination, bufsz+1, "%d", origin);
    
    return destination;
}

int set__payloadlen(struct mosquitto__packet *packet)
{
    int length = 0;
    
    /* Source */
    if(packet->bpdu->origin_address){
        length = 2+strlen(packet->bpdu->origin_address);
    }else{
        length = 2;
    }
    
    if(packet->bpdu->origin_port){
        length += 2+strlen(packet->bpdu->origin_port);
    }else{
        length += 2;
    }
    
    if(packet->bpdu->origin_id){
        length += 2+strlen(packet->bpdu->origin_id);
    }else{
        length += 2;
    }
    
    /* Root */
    if(packet->bpdu->root_address){
        length += 2+strlen(packet->bpdu->root_address);
    }else{
        length += 2;
    }
    
    if(packet->bpdu->root_port){
        length += 2+strlen(packet->bpdu->root_port);
    }else{
        length += 2;
    }
    
    if(packet->bpdu->root_id){
        length += 2+strlen(packet->bpdu->root_id);
    }else{
        length += 2;
    }
    
    if(packet->bpdu->distance){
        length += 2+strlen(packet->bpdu->distance);
    }else{
        length += 2;
    }
    
    if(packet->bpdu->origin_pid){
        length += 2+strlen(packet->bpdu->origin_pid);
    }else{
        length += 2;
    }
    
    if(packet->bpdu->root_pid){
        length += 2+strlen(packet->bpdu->root_pid);
    }else{
        length += 2;
    }
    
    return length;
}

struct mosquitto__bpdu__packet *packet__write_bpdu(struct mosquitto__stp *stp)
{
    struct mosquitto__bpdu__packet *bpdu_pkt = NULL;
    
    bpdu_pkt = mosquitto__calloc(1, sizeof(struct mosquitto__bpdu__packet));
    if(!bpdu_pkt) return NULL;
    
    /* Source values */
    if(stp->my->port){
        bpdu_pkt->origin_port = convert_integer(stp->my->port);
    }
    if(stp->my->_id){
        bpdu_pkt->origin_id = stp->my->_id;
    }
    if(stp->my->address){
        bpdu_pkt->origin_address = stp->my->address;
    }
    
    /* Root values */
    if(stp->my_root->port){
        bpdu_pkt->root_port = convert_integer(stp->my_root->port);
    }
    if(stp->my_root->_id){
        bpdu_pkt->origin_id = stp->my_root->_id;
    }
    if(stp->my_root->address){
        bpdu_pkt->origin_address = stp->my_root->address;
    }
    
    /* Resources values */
    if(stp->my->res->pid){
        bpdu_pkt->origin_pid = convert_integer(stp->my->res->pid);
    }
    if(stp->my_root->res->pid){
        bpdu_pkt->root_pid = convert_integer(stp->my_root->res->pid);
    }
    
    if(stp->distance >= 0){
        bpdu_pkt->distance = convert_integer(stp->distance);
    }
    
    return bpdu_pkt;
}

int packet__alloc(struct mosquitto__packet *packet)
{
	uint8_t remaining_bytes[5], byte;
	uint32_t remaining_length;
	int i;

	assert(packet);

	remaining_length = packet->remaining_length;
	packet->payload = NULL;
	packet->remaining_count = 0;
	do{
		byte = remaining_length % 128;
		remaining_length = remaining_length / 128;
		/* If there are more digits to encode, set the top bit of this digit */
		if(remaining_length > 0){
			byte = byte | 0x80;
		}
		remaining_bytes[packet->remaining_count] = byte;
		packet->remaining_count++;
	}while(remaining_length > 0 && packet->remaining_count < 5);
	if(packet->remaining_count == 5) return MOSQ_ERR_PAYLOAD_SIZE;
	packet->packet_length = packet->remaining_length + 1 + packet->remaining_count;
#ifdef WITH_WEBSOCKETS
	packet->payload = mosquitto__malloc(sizeof(uint8_t)*packet->packet_length + LWS_SEND_BUFFER_PRE_PADDING + LWS_SEND_BUFFER_POST_PADDING);
#else
	packet->payload = mosquitto__malloc(sizeof(uint8_t)*packet->packet_length);
#endif
	if(!packet->payload) return MOSQ_ERR_NOMEM;

	packet->payload[0] = packet->command;
	for(i=0; i<packet->remaining_count; i++){
		packet->payload[i+1] = remaining_bytes[i];
	}
	packet->pos = 1 + packet->remaining_count;

	return MOSQ_ERR_SUCCESS;
}

void packet__cleanup(struct mosquitto__packet *packet)
{
	if(!packet) return;

	/* Free data and reset values */
	packet->command = 0;
	packet->remaining_count = 0;
	packet->remaining_mult = 1;
	packet->remaining_length = 0;
	mosquitto__free(packet->payload);
	packet->payload = NULL;
	packet->to_process = 0;
	packet->pos = 0;
}

int packet__queue(struct mosquitto *mosq, struct mosquitto__packet *packet)
{
#ifndef WITH_BROKER
	char sockpair_data = 0;
#endif
	assert(mosq);
	assert(packet);

	packet->pos = 0;
	packet->to_process = packet->packet_length;

	packet->next = NULL;
	pthread_mutex_lock(&mosq->out_packet_mutex);
	if(mosq->out_packet){
		mosq->out_packet_last->next = packet;
	}else{
		mosq->out_packet = packet;
	}
	mosq->out_packet_last = packet;
	pthread_mutex_unlock(&mosq->out_packet_mutex);
#ifdef WITH_BROKER
#  ifdef WITH_WEBSOCKETS
	if(mosq->wsi){
		libwebsocket_callback_on_writable(mosq->ws_context, mosq->wsi);
		return MOSQ_ERR_SUCCESS;
	}else{
		return packet__write(mosq);
	}
#  else
	return packet__write(mosq);
#  endif
#else

	/* Write a single byte to sockpairW (connected to sockpairR) to break out
	 * of select() if in threaded mode. */
	if(mosq->sockpairW != INVALID_SOCKET){
#ifndef WIN32
		if(write(mosq->sockpairW, &sockpair_data, 1)){
		}
#else
		send(mosq->sockpairW, &sockpair_data, 1, 0);
#endif
	}

	if(mosq->in_callback == false && mosq->threaded == mosq_ts_none){
		return packet__write(mosq);
	}else{
		return MOSQ_ERR_SUCCESS;
	}
#endif
}


int packet__check_oversize(struct mosquitto *mosq, uint32_t remaining_length)
{
	uint32_t len;

	if(mosq->maximum_packet_size == 0) return MOSQ_ERR_SUCCESS;

	len = remaining_length + packet__varint_bytes(remaining_length);
	if(len > mosq->maximum_packet_size){
		return MOSQ_ERR_OVERSIZE_PACKET;
	}else{
		return MOSQ_ERR_SUCCESS;
	}
}


int packet__write(struct mosquitto *mosq)
{
	ssize_t write_length;
	struct mosquitto__packet *packet;

	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;

	pthread_mutex_lock(&mosq->current_out_packet_mutex);
	pthread_mutex_lock(&mosq->out_packet_mutex);
	if(mosq->out_packet && !mosq->current_out_packet){
		mosq->current_out_packet = mosq->out_packet;
		mosq->out_packet = mosq->out_packet->next;
		if(!mosq->out_packet){
			mosq->out_packet_last = NULL;
		}
	}
	pthread_mutex_unlock(&mosq->out_packet_mutex);

#if defined(WITH_TLS) && !defined(WITH_BROKER)
	if((mosq->state == mosq_cs_connect_pending) || mosq->want_connect){
#else
	if(mosq->state == mosq_cs_connect_pending){
#endif
		pthread_mutex_unlock(&mosq->current_out_packet_mutex);
		return MOSQ_ERR_SUCCESS;
	}

	while(mosq->current_out_packet){
		packet = mosq->current_out_packet;

		while(packet->to_process > 0){
			write_length = net__write(mosq, &(packet->payload[packet->pos]), packet->to_process);
			if(write_length > 0){
				G_BYTES_SENT_INC(write_length);
				packet->to_process -= write_length;
				packet->pos += write_length;
			}else{
#ifdef WIN32
				errno = WSAGetLastError();
#endif
				if(errno == EAGAIN || errno == COMPAT_EWOULDBLOCK){
					pthread_mutex_unlock(&mosq->current_out_packet_mutex);
					return MOSQ_ERR_SUCCESS;
				}else{
					pthread_mutex_unlock(&mosq->current_out_packet_mutex);
					switch(errno){
						case COMPAT_ECONNRESET:
							return MOSQ_ERR_CONN_LOST;
						default:
							return MOSQ_ERR_ERRNO;
					}
				}
			}
		}

		G_MSGS_SENT_INC(1);
		if(((packet->command)&0xF6) == CMD_PUBLISH){
			G_PUB_MSGS_SENT_INC(1);
#ifndef WITH_BROKER
			pthread_mutex_lock(&mosq->callback_mutex);
			if(mosq->on_publish){
				/* This is a QoS=0 message */
				mosq->in_callback = true;
				mosq->on_publish(mosq, mosq->userdata, packet->mid);
				mosq->in_callback = false;
			}
			if(mosq->on_publish_v5){
				/* This is a QoS=0 message */
				mosq->in_callback = true;
				mosq->on_publish_v5(mosq, mosq->userdata, packet->mid, 0, NULL);
				mosq->in_callback = false;
			}
			pthread_mutex_unlock(&mosq->callback_mutex);
		}else if(((packet->command)&0xF0) == CMD_DISCONNECT){
			do_client_disconnect(mosq, MOSQ_ERR_SUCCESS, NULL);
			packet__cleanup(packet);
			mosquitto__free(packet);
			return MOSQ_ERR_SUCCESS;
#endif
		}

		/* Free data and reset values */
		pthread_mutex_lock(&mosq->out_packet_mutex);
		mosq->current_out_packet = mosq->out_packet;
		if(mosq->out_packet){
			mosq->out_packet = mosq->out_packet->next;
			if(!mosq->out_packet){
				mosq->out_packet_last = NULL;
			}
		}
		pthread_mutex_unlock(&mosq->out_packet_mutex);

		packet__cleanup(packet);
		mosquitto__free(packet);

		pthread_mutex_lock(&mosq->msgtime_mutex);
		mosq->next_msg_out = mosquitto_time() + mosq->keepalive;
		pthread_mutex_unlock(&mosq->msgtime_mutex);
	}
	pthread_mutex_unlock(&mosq->current_out_packet_mutex);
	return MOSQ_ERR_SUCCESS;
}


#ifdef WITH_BROKER
int packet__read(struct mosquitto_db *db, struct mosquitto *mosq)
#else
int packet__read(struct mosquitto *mosq)
#endif
{
	uint8_t byte;
	ssize_t read_length;
	int rc = 0;

	if(!mosq){
		return MOSQ_ERR_INVAL;
	}
	if(mosq->sock == INVALID_SOCKET){
		return MOSQ_ERR_NO_CONN;
	}
	if(mosq->state == mosq_cs_connect_pending){
		return MOSQ_ERR_SUCCESS;
	}

	/* This gets called if pselect() indicates that there is network data
	 * available - ie. at least one byte.  What we do depends on what data we
	 * already have.
	 * If we've not got a command, attempt to read one and save it. This should
	 * always work because it's only a single byte.
	 * Then try to read the remaining length. This may fail because it is may
	 * be more than one byte - will need to save data pending next read if it
	 * does fail.
	 * Then try to read the remaining payload, where 'payload' here means the
	 * combined variable header and actual payload. This is the most likely to
	 * fail due to longer length, so save current data and current position.
	 * After all data is read, send to mosquitto__handle_packet() to deal with.
	 * Finally, free the memory and reset everything to starting conditions.
	 */
	if(!mosq->in_packet.command){
		read_length = net__read(mosq, &byte, 1);
		if(read_length == 1){
			mosq->in_packet.command = byte;
#ifdef WITH_BROKER
			G_BYTES_RECEIVED_INC(1);
			/* Clients must send CONNECT as their first command. */
			if(!(mosq->bridge) && mosq->state == mosq_cs_new && (byte&0xF0) != CMD_CONNECT){
				return MOSQ_ERR_PROTOCOL;
			}
#endif
		}else{
			if(read_length == 0){
				return MOSQ_ERR_CONN_LOST; /* EOF */
			}
#ifdef WIN32
			errno = WSAGetLastError();
#endif
			if(errno == EAGAIN || errno == COMPAT_EWOULDBLOCK){
				return MOSQ_ERR_SUCCESS;
			}else{
				switch(errno){
					case COMPAT_ECONNRESET:
						return MOSQ_ERR_CONN_LOST;
					default:
						return MOSQ_ERR_ERRNO;
				}
			}
		}
	}
	/* remaining_count is the number of bytes that the remaining_length
	 * parameter occupied in this incoming packet. We don't use it here as such
	 * (it is used when allocating an outgoing packet), but we must be able to
	 * determine whether all of the remaining_length parameter has been read.
	 * remaining_count has three states here:
	 *   0 means that we haven't read any remaining_length bytes
	 *   <0 means we have read some remaining_length bytes but haven't finished
	 *   >0 means we have finished reading the remaining_length bytes.
	 */
	if(mosq->in_packet.remaining_count <= 0){
		do{
			read_length = net__read(mosq, &byte, 1);
			if(read_length == 1){
				mosq->in_packet.remaining_count--;
				/* Max 4 bytes length for remaining length as defined by protocol.
				 * Anything more likely means a broken/malicious client.
				 */
				if(mosq->in_packet.remaining_count < -4){
					return MOSQ_ERR_PROTOCOL;
				}

				G_BYTES_RECEIVED_INC(1);
				mosq->in_packet.remaining_length += (byte & 127) * mosq->in_packet.remaining_mult;
				mosq->in_packet.remaining_mult *= 128;
			}else{
				if(read_length == 0){
					return MOSQ_ERR_CONN_LOST; /* EOF */
				}
#ifdef WIN32
				errno = WSAGetLastError();
#endif
				if(errno == EAGAIN || errno == COMPAT_EWOULDBLOCK){
					return MOSQ_ERR_SUCCESS;
				}else{
					switch(errno){
						case COMPAT_ECONNRESET:
							return MOSQ_ERR_CONN_LOST;
						default:
							return MOSQ_ERR_ERRNO;
					}
				}
			}
		}while((byte & 128) != 0);
		/* We have finished reading remaining_length, so make remaining_count
		 * positive. */
		mosq->in_packet.remaining_count *= -1;

#ifdef WITH_BROKER
		if(db->config->max_packet_size > 0 && mosq->in_packet.remaining_length+1 > db->config->max_packet_size){
			log__printf(NULL, MOSQ_LOG_INFO, "Client %s sent too large packet %d, disconnecting.", mosq->id, mosq->in_packet.remaining_length+1);
			if(mosq->protocol == mosq_p_mqtt5){
				send__disconnect(mosq, MQTT_RC_PACKET_TOO_LARGE, NULL);
			}
			return MOSQ_ERR_OVERSIZE_PACKET;
		}
#else
		// FIXME - client case for incoming message received from broker too large
#endif
		if(mosq->in_packet.remaining_length > 0){
			mosq->in_packet.payload = mosquitto__malloc(mosq->in_packet.remaining_length*sizeof(uint8_t));
			if(!mosq->in_packet.payload){
				return MOSQ_ERR_NOMEM;
			}
			mosq->in_packet.to_process = mosq->in_packet.remaining_length;
		}
	}
	while(mosq->in_packet.to_process>0){
		read_length = net__read(mosq, &(mosq->in_packet.payload[mosq->in_packet.pos]), mosq->in_packet.to_process);
		if(read_length > 0){
			G_BYTES_RECEIVED_INC(read_length);
			mosq->in_packet.to_process -= read_length;
			mosq->in_packet.pos += read_length;
		}else{
#ifdef WIN32
			errno = WSAGetLastError();
#endif
			if(errno == EAGAIN || errno == COMPAT_EWOULDBLOCK){
				if(mosq->in_packet.to_process > 1000){
					/* Update last_msg_in time if more than 1000 bytes left to
					 * receive. Helps when receiving large messages.
					 * This is an arbitrary limit, but with some consideration.
					 * If a client can't send 1000 bytes in a second it
					 * probably shouldn't be using a 1 second keep alive. */
					pthread_mutex_lock(&mosq->msgtime_mutex);
					mosq->last_msg_in = mosquitto_time();
					pthread_mutex_unlock(&mosq->msgtime_mutex);
				}
				return MOSQ_ERR_SUCCESS;
			}else{
				switch(errno){
					case COMPAT_ECONNRESET:
						return MOSQ_ERR_CONN_LOST;
					default:
						return MOSQ_ERR_ERRNO;
				}
			}
		}
	}

	/* All data for this packet is read. */
	mosq->in_packet.pos = 0;
#ifdef WITH_BROKER
	G_MSGS_RECEIVED_INC(1);
	if(((mosq->in_packet.command)&0xF5) == CMD_PUBLISH){
		G_PUB_MSGS_RECEIVED_INC(1);
	}
	rc = handle__packet(db, mosq);
#else
	rc = handle__packet(mosq);
#endif

	/* Free data and reset values */
	packet__cleanup(&mosq->in_packet);

	pthread_mutex_lock(&mosq->msgtime_mutex);
	mosq->last_msg_in = mosquitto_time();
	pthread_mutex_unlock(&mosq->msgtime_mutex);
	return rc;
}
