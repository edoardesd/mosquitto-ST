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
#include <string.h>

#ifdef WIN32
#  include <winsock2.h>
#  include <aclapi.h>
#  include <io.h>
#  include <lmcons.h>
#else
#  include <sys/stat.h>
#endif

#if !defined(WITH_TLS) && defined(__linux__) && defined(__GLIBC__)
#  if __GLIBC_PREREQ(2, 25)
#    include <sys/random.h>
#    define HAVE_GETRANDOM 1
#  endif
#endif

#ifdef WITH_TLS
#  include <openssl/bn.h>
#  include <openssl/rand.h>
#endif

#ifdef WITH_BROKER
#include "mosquitto_broker_internal.h"
#endif

#include "mosquitto.h"
#include "memory_mosq.h"
#include "net_mosq.h"
#include "send_mosq.h"
#include "time_mosq.h"
#include "tls_mosq.h"
#include "util_mosq.h"
#include "util_string.h"
#include "util_list.h"
#include "stp_mosq.h"

#ifdef WITH_WEBSOCKETS
#include <libwebsockets.h>
#endif


#ifdef WITH_BROKER
struct mosquitto__bpdu__packet* init__bpdu(struct mosquitto_db *db, struct mosquitto__bpdu__packet *bpdu)
{
    bpdu->distance = "0";
    bpdu->origin_address = db->ip_address;
    bpdu->origin_port = convert_integer(db->stp->my->port);
    bpdu->origin_pid = convert_integer(db->stp->my->res->pid);
    bpdu->root_address = db->ip_address;
    bpdu->root_port = convert_integer(db->stp->my->port);
    bpdu->root_pid = convert_integer(db->stp->my->res->pid);
    
    return bpdu;
}


struct mosquitto__bpdu__packet* find_bridge(struct mosquitto_db *db, struct mosquitto__bpdu__packet *packet, int origin_port, int i)
{
    struct mosquitto__bpdu__packet *stored_bpdu;
    if(strcmp(db->config->bridges[i].addresses->address, packet->origin_address) == 0 && (db->config->bridges[i].addresses->port == origin_port)){
        stored_bpdu = db->config->bridges[i].last_bpdu;
        return stored_bpdu;
    }
    return NULL;
}

bool check_repeated(struct mosquitto__bpdu__packet *stored_bpdu, struct mosquitto__bpdu__packet *packet){
    if(strcmp(stored_bpdu->root_address, packet->root_address) == 0 && strcmp(stored_bpdu->root_port, packet->root_port) == 0 && strcmp(stored_bpdu->root_pid, packet->root_pid) == 0){
        if(strcmp(stored_bpdu->origin_address, packet->origin_address) == 0 && strcmp(stored_bpdu->origin_port, packet->origin_port) == 0 && strcmp(stored_bpdu->origin_pid, packet->origin_pid)==0){
            if(strcmp(stored_bpdu->distance, packet->distance) == 0){
                log__printf(NULL, MOSQ_LOG_DEBUG, "INFO repeated");
                return true;
            }
        }
    }
    return false;
}

//void check_old_info(struct mosquitto__stp *stp, struct mosquitto__bpdu__packet *packet,  struct mosquitto_db *db, int bridge){
//
//    if(strcmp(stp->my_root->address, packet->root_address) == 0 && stp->my_root->port == strint(packet->root_port) && stp->my_root->res->pid == strint(packet->root_pid)){
//        if(stp->distance == strint(packet->distance)+1){
//            log__printf(NULL, MOSQ_LOG_DEBUG, "same info of before!!!");
//            db->config->bridges[bridge].convergence = true;
//        }
//    }
//
//}

int ping_everyone_except(struct mosquitto_db *db)
{
    struct mosquitto *context;
    int i;
    
    for(i=0; i<db->bridge_count; i++){
        if(!db->bridges[i]) continue;
        context = db->bridges[i];
        
        if(strcmp(context->bridge->addresses[context->bridge->cur_address].address, db->king_port.address) != 0 || context->bridge->addresses[context->bridge->cur_address].port != db->king_port.port){
            log__printf(NULL, MOSQ_LOG_NOTICE, "Sending NEW ROOT info to address %s:%d", context->bridge->addresses[context->bridge->cur_address].address, context->bridge->addresses[context->bridge->cur_address].port);
            send__pingreq(db, context);
        }
    }
    return MOSQ_ERR_SUCCESS;
}

int stp__algorithm(struct mosquitto_db *db, struct mosquitto__stp *stp, struct mosquitto__bridge *context, struct mosquitto__bpdu__packet *packet)
{
    
    BROKER broker_root;
    BROKER broker_origin;
    int recv_root_port;
    int recv_origin_port;
    int recv_root_pid;
    int recv_distance;
    int recv_origin_pid;
    int ret;
    recv_origin_port = strint(packet->origin_port);
    recv_root_pid = strint(packet->root_pid);
    recv_root_port = strint(packet->root_port);
    recv_distance = strint(packet->distance);
    recv_origin_pid = strint(packet->origin_pid);
    broker_root.address = packet->root_address;
    broker_root.port = recv_root_port;
    broker_origin.address = packet->origin_address;
    broker_origin.port = recv_origin_port;
    
    /* Beginning of STP algorithm */
    if(stp->my_root->res->pid < recv_root_pid){
        log__printf(NULL, MOSQ_LOG_DEBUG, "Inferior information");
        /* Set as designated port */
        db->blocked_ports = find_and_delete(db->blocked_ports, broker_origin);
        
        if(!in_list(db->designated_ports, broker_origin)){
            db->designated_ports = add(db->designated_ports, broker_origin);
        }
        context->port_status = DESIGNATED_PORT;
        return MOSQ_ERR_SUCCESS;
    }
    
    if(stp->my_root->res->pid > recv_root_pid){
        log__printf(NULL, MOSQ_LOG_DEBUG, "BETTER PID");
        update_stp(stp, packet);
        //set root port
        //WHAT TO DO WITH OLD ROOT? DP?
        db->designated_ports = find_and_delete(db->designated_ports, broker_root);
        db->blocked_ports = find_and_delete(db->blocked_ports, broker_root);
        
        db->king_port = broker_root;
        context->port_status = ROOT_PORT;
        
        //TODO: send new ping to designated ports?
        ping_everyone_except(db);
        return MOSQ_ERR_SUCCESS;
    }
    
    /* SAME PID */
    /* -> Lower level */
    if (stp->my_root->res->pid == recv_root_pid){
        log__printf(NULL, MOSQ_LOG_DEBUG, "Same pid");
        if(stp->distance < recv_distance){
            log__printf(NULL, MOSQ_LOG_DEBUG, "Same PID but Inferior information");
            /* Set as designated port */
            db->blocked_ports = find_and_delete(db->blocked_ports, broker_origin);
            
            if(!in_list(db->designated_ports, broker_origin)){
                db->designated_ports = add(db->designated_ports, broker_origin);
            }
            context->port_status = DESIGNATED_PORT;
            
            return MOSQ_ERR_SUCCESS;
        }
        if(stp->distance > recv_distance){
            log__printf(NULL, MOSQ_LOG_DEBUG, "Found a better distance to the root");
            //WHAT TO DO WITH OLD ROOT? DP?
            update_stp(stp, packet);
            //set root port
            db->designated_ports = find_and_delete(db->designated_ports, broker_root);
            db->blocked_ports = find_and_delete(db->blocked_ports, broker_root);
            
            db->king_port = broker_root;
            context->port_status = ROOT_PORT;
            
            //TODO: send new ping to designated ports?
            ping_everyone_except(db);
            return MOSQ_ERR_SUCCESS;
            
        }
        /* SAME DISTANCE */
        /* -> Lower level */
        if (stp->distance == recv_distance){
            //another tie, check own pid first
            if(stp->my->res->pid < recv_origin_pid){
                /* Set as designated port */
                db->blocked_ports = find_and_delete(db->blocked_ports, broker_origin);
                if(!in_list(db->designated_ports, broker_origin)){
                    db->designated_ports = add(db->designated_ports, broker_origin);
                }
                context->port_status = DESIGNATED_PORT;
                return MOSQ_ERR_SUCCESS;
            }
            
            if(stp->my->res->pid > recv_origin_pid){
                log__printf(NULL, MOSQ_LOG_DEBUG, "we tie but my PID is greater, i've to block");
                
                db->designated_ports = find_and_delete(db->designated_ports, broker_origin);
                
                if(!in_list(db->blocked_ports, broker_origin)){
                    db->blocked_ports = add(db->blocked_ports, broker_origin);
                }
                
                context->port_status = BLOCKED_PORT;
                return MOSQ_ERR_SUCCESS;
            }
            /* SAME OWN PID (almost impossible to have*/
            /* -> Lower level */
            if (stp->my->res->pid == recv_origin_pid){
                ret = strcmp(stp->my->address, packet->origin_address);
                if(ret < 0){
                    /* Set as designated port */
                    db->blocked_ports = find_and_delete(db->blocked_ports, broker_origin);
                    if(!in_list(db->designated_ports, broker_origin)){
                        db->designated_ports = add(db->designated_ports, broker_origin);
                    }
                    context->port_status = DESIGNATED_PORT;
                    return MOSQ_ERR_SUCCESS;
                }else if (ret == 0){
                    //another tie
                    if(stp->my->port < recv_origin_port){
                        /* Set as designated port */
                        db->blocked_ports = find_and_delete(db->blocked_ports, broker_origin);
                        if(!in_list(db->designated_ports, broker_origin)){
                            db->designated_ports = add(db->designated_ports, broker_origin);
                        }
                        context->port_status = DESIGNATED_PORT;
                        return MOSQ_ERR_SUCCESS;
                    }else if(stp->my->port > recv_origin_port){
                        log__printf(NULL, MOSQ_LOG_DEBUG, "we tie but i'm greater, i've to block");
                        //TODO set block
                        db->designated_ports = find_and_delete(db->designated_ports, broker_origin);
                        
                        if(!in_list(db->blocked_ports, broker_origin)){
                            db->blocked_ports = add(db->blocked_ports, broker_origin);
                        }
                        context->port_status = BLOCKED_PORT;
                        return MOSQ_ERR_SUCCESS;
                    }else{
                        return MOSQ_ERR_STP;
                    }
                }else{
                    log__printf(NULL, MOSQ_LOG_DEBUG, "we tie again but i'm greater address, i've to block");
                    db->designated_ports = find_and_delete(db->designated_ports, broker_origin);
                    
                    if(!in_list(db->blocked_ports, broker_origin)){
                        db->blocked_ports = add(db->blocked_ports, broker_origin);
                    }
                    
                    context->port_status = BLOCKED_PORT;
                    return MOSQ_ERR_SUCCESS;
                }
            }
        }
    }
    return MOSQ_ERR_STP;
}

bool check_convergence(struct mosquitto_db *db, PORT_LIST *old_des, PORT_LIST *old_block, BROKER old_k)
{
    if(are_identical(db->designated_ports, old_des) && are_identical(db->blocked_ports, old_block)){
        if(strcmp(db->king_port.address, old_k.address) == 0 && db->king_port.port == old_k.port){
            return true;
        }
    }
    
    return false;
}

int update__stp_properties(struct mosquitto_db *db, struct mosquitto__stp *stp, struct mosquitto__bridge *bridge, struct mosquitto__bpdu__packet *packet)
{
    struct mosquitto__bpdu__packet *stored_bpdu = NULL;
    struct mosquitto__bridge *context = NULL;
    bool old_convergence = false;
    int recv_origin_port;
    bool conv_reached = false;
    
    recv_origin_port = strint(packet->origin_port);
    
    log__printf(NULL, MOSQ_LOG_NOTICE, "r%s:%s o%s:%d", packet->root_address, packet->root_port, packet->origin_address, recv_origin_port);
    
    PORT_LIST *old_designated;
    PORT_LIST *old_blocked;
    BROKER old_king;
    
    init_list(&old_designated, "Desisgnated");
    init_list(&old_blocked, "blocked");
    old_king.address = db->king_port.address;
    old_king.port = db->king_port.port;
    
    old_designated = copy_list(db->designated_ports);
    old_blocked = copy_list(db->blocked_ports);
    old_convergence = db->convergence;
    
    for(int i=0; i<db->config->bridge_count; i++){
        stored_bpdu = find_bridge(db, packet, recv_origin_port, i);
        
        context = &db->config->bridges[i];
        if(stored_bpdu){
            
            if(stp__algorithm(db, stp, context, packet) == MOSQ_ERR_SUCCESS){
                update_bpdu(stored_bpdu, packet);
                log__printf(NULL, MOSQ_LOG_DEBUG, "----- NEW LISTS -----");
                log__printf(NULL, MOSQ_LOG_DEBUG, "\nList ROOT: %s:%d", db->king_port.address, db->king_port.port);
                print_list(db->designated_ports, "DESIGNATED");
                print_list(db->blocked_ports, "BLOCK");
                
                if(check_convergence(db, old_designated, old_blocked, old_king)){
                    db->convergence = true;
                }else{
                    db->convergence = false;
                }
                if(old_convergence && db->convergence){
                    conv_reached = true;
                    for(int i=0; i<db->config->bridge_count; i++){
                        if(!db->config->bridges[i].is_connected){
                            conv_reached = false;
                        }
                    }
                    if(conv_reached){
                        log__printf(NULL, MOSQ_LOG_INFO, "Convergence REACHED");
                    }
                }
                return MOSQ_ERR_SUCCESS;
            }
        }
    }
    
    log__printf(NULL, MOSQ_LOG_ERR, "ERROR on update STP.");
    return MOSQ_ERR_STP;
}
#endif

#ifdef WITH_BROKER
int mosquitto__check_keepalive(struct mosquitto_db *db, struct mosquitto *mosq)
#else
int mosquitto__check_keepalive(struct mosquitto *mosq)
#endif
{
	time_t next_msg_out;
	time_t last_msg_in;
	time_t now = mosquitto_time();
#ifndef WITH_BROKER
	int rc;
#endif

	assert(mosq);
#if defined(WITH_BROKER) && defined(WITH_BRIDGE)
	/* Check if a lazy bridge should be timed out due to idle. */
	if(mosq->bridge && mosq->bridge->start_type == bst_lazy
				&& mosq->sock != INVALID_SOCKET
				&& now - mosq->next_msg_out - mosq->keepalive >= mosq->bridge->idle_timeout){

		log__printf(NULL, MOSQ_LOG_NOTICE, "Bridge connection %s has exceeded idle timeout, disconnecting.", mosq->id);
		net__socket_close(db, mosq);
		return MOSQ_ERR_SUCCESS;
	}
#endif
	pthread_mutex_lock(&mosq->msgtime_mutex);
	next_msg_out = mosq->next_msg_out;
	last_msg_in = mosq->last_msg_in;
	pthread_mutex_unlock(&mosq->msgtime_mutex);
	if(mosq->keepalive && mosq->sock != INVALID_SOCKET &&
			(now >= next_msg_out || now - last_msg_in >= mosq->keepalive)){

		if(mosq->state == mosq_cs_connected && mosq->ping_t == 0){
#ifdef WITH_BROKER
			send__pingreq(db, mosq);
#else
            send__pingreq(mosq);
#endif
			/* Reset last msg times to give the server time to send a pingresp */
			pthread_mutex_lock(&mosq->msgtime_mutex);
			mosq->last_msg_in = now;
			mosq->next_msg_out = now + mosq->keepalive;
			pthread_mutex_unlock(&mosq->msgtime_mutex);
		}else{
#ifdef WITH_BROKER
			net__socket_close(db, mosq);
#else
			net__socket_close(mosq);
			pthread_mutex_lock(&mosq->state_mutex);
			if(mosq->state == mosq_cs_disconnecting){
				rc = MOSQ_ERR_SUCCESS;
			}else{
				rc = MOSQ_ERR_KEEPALIVE;
			}
			pthread_mutex_unlock(&mosq->state_mutex);
			pthread_mutex_lock(&mosq->callback_mutex);
			if(mosq->on_disconnect){
				mosq->in_callback = true;
				mosq->on_disconnect(mosq, mosq->userdata, rc);
				mosq->in_callback = false;
			}
			if(mosq->on_disconnect_v5){
				mosq->in_callback = true;
				mosq->on_disconnect_v5(mosq, mosq->userdata, rc, NULL);
				mosq->in_callback = false;
			}
			pthread_mutex_unlock(&mosq->callback_mutex);

			return rc;
#endif
		}
	}
	return MOSQ_ERR_SUCCESS;
}

uint16_t mosquitto__mid_generate(struct mosquitto *mosq)
{
	/* FIXME - this would be better with atomic increment, but this is safer
	 * for now for a bug fix release.
	 *
	 * If this is changed to use atomic increment, callers of this function
	 * will have to be aware that they may receive a 0 result, which may not be
	 * used as a mid.
	 */
	uint16_t mid;
	assert(mosq);

	pthread_mutex_lock(&mosq->mid_mutex);
	mosq->last_mid++;
	if(mosq->last_mid == 0) mosq->last_mid++;
	mid = mosq->last_mid;
	pthread_mutex_unlock(&mosq->mid_mutex);

	return mid;
}


#ifdef WITH_TLS
int mosquitto__hex2bin_sha1(const char *hex, unsigned char **bin)
{
	unsigned char *sha, tmp[SHA_DIGEST_LENGTH];

	if(mosquitto__hex2bin(hex, tmp, SHA_DIGEST_LENGTH) != SHA_DIGEST_LENGTH){
		return MOSQ_ERR_INVAL;
	}

	sha = mosquitto__malloc(SHA_DIGEST_LENGTH);
	memcpy(sha, tmp, SHA_DIGEST_LENGTH);
	*bin = sha;
	return MOSQ_ERR_SUCCESS;
}

int mosquitto__hex2bin(const char *hex, unsigned char *bin, int bin_max_len)
{
	BIGNUM *bn = NULL;
	int len;
	int leading_zero = 0;
	int start = 0;
	size_t i = 0;

	/* Count the number of leading zero */
	for(i=0; i<strlen(hex); i=i+2) {
		if(strncmp(hex + i, "00", 2) == 0) {
			leading_zero++;
			/* output leading zero to bin */
			bin[start++] = 0;
		}else{
			break;
		}
	}

	if(BN_hex2bn(&bn, hex) == 0){
		if(bn) BN_free(bn);
		return 0;
	}
	if(BN_num_bytes(bn) + leading_zero > bin_max_len){
		BN_free(bn);
		return 0;
	}

	len = BN_bn2bin(bn, bin + leading_zero);
	BN_free(bn);
	return len + leading_zero;
}
#endif

FILE *mosquitto__fopen(const char *path, const char *mode, bool restrict_read)
{
#ifdef WIN32
	char buf[4096];
	int rc;
	rc = ExpandEnvironmentStrings(path, buf, 4096);
	if(rc == 0 || rc > 4096){
		return NULL;
	}else{
		if (restrict_read) {
			HANDLE hfile;
			SECURITY_ATTRIBUTES sec;
			EXPLICIT_ACCESS ea;
			PACL pacl = NULL;
			char username[UNLEN + 1];
			int ulen = UNLEN;
			SECURITY_DESCRIPTOR sd;
			DWORD dwCreationDisposition;

			switch(mode[0]){
				case 'a':
					dwCreationDisposition = OPEN_ALWAYS;
					break;
				case 'r':
					dwCreationDisposition = OPEN_EXISTING;
					break;
				case 'w':
					dwCreationDisposition = CREATE_ALWAYS;
					break;
				default:
					return NULL;
			}

			GetUserName(username, &ulen);
			if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
				return NULL;
			}
			BuildExplicitAccessWithName(&ea, username, GENERIC_ALL, SET_ACCESS, NO_INHERITANCE);
			if (SetEntriesInAcl(1, &ea, NULL, &pacl) != ERROR_SUCCESS) {
				return NULL;
			}
			if (!SetSecurityDescriptorDacl(&sd, TRUE, pacl, FALSE)) {
				LocalFree(pacl);
				return NULL;
			}

			sec.nLength = sizeof(SECURITY_ATTRIBUTES);
			sec.bInheritHandle = FALSE;
			sec.lpSecurityDescriptor = &sd;

			hfile = CreateFile(buf, GENERIC_READ | GENERIC_WRITE, 0,
				&sec,
				dwCreationDisposition,
				FILE_ATTRIBUTE_NORMAL,
				NULL);

			LocalFree(pacl);

			int fd = _open_osfhandle((intptr_t)hfile, 0);
			if (fd < 0) {
				return NULL;
			}

			FILE *fptr = _fdopen(fd, mode);
			if (!fptr) {
				_close(fd);
				return NULL;
			}
			return fptr;

		}else {
			return fopen(buf, mode);
		}
	}
#else
	if (restrict_read) {
		FILE *fptr;
		mode_t old_mask;

		old_mask = umask(0077);
		fptr = fopen(path, mode);
		umask(old_mask);

		return fptr;
	}else{
		return fopen(path, mode);
	}
#endif
}

void util__increment_receive_quota(struct mosquitto *mosq)
{
	if(mosq->msgs_in.inflight_quota < mosq->msgs_in.inflight_maximum){
		mosq->msgs_in.inflight_quota++;
	}
}

void util__increment_send_quota(struct mosquitto *mosq)
{
	if(mosq->msgs_out.inflight_quota < mosq->msgs_out.inflight_maximum){
		mosq->msgs_out.inflight_quota++;
	}
}


void util__decrement_receive_quota(struct mosquitto *mosq)
{
	if(mosq->msgs_in.inflight_quota > 0){
		mosq->msgs_in.inflight_quota--;
	}
}

void util__decrement_send_quota(struct mosquitto *mosq)
{
	if(mosq->msgs_out.inflight_quota > 0){
		mosq->msgs_out.inflight_quota--;
	}
}


int util__random_bytes(void *bytes, int count)
{
	int rc = MOSQ_ERR_UNKNOWN;

#ifdef WITH_TLS
	if(RAND_bytes(bytes, count) == 1){
		rc = MOSQ_ERR_SUCCESS;
	}
#elif defined(HAVE_GETRANDOM)
	if(getrandom(bytes, count, 0) == count){
		rc = MOSQ_ERR_SUCCESS;
	}
#elif defined(WIN32)
	HCRYPTPROV provider;

	if(!CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)){
		return MOSQ_ERR_UNKNOWN;
	}

	if(CryptGenRandom(provider, count, bytes)){
		rc = MOSQ_ERR_SUCCESS;
	}

	CryptReleaseContext(provider, 0);
#else
	int i;

	for(i=0; i<count; i++){
		((uint8_t *)bytes)[i] = (uint8_t )(random()&0xFF);
	}
	rc = MOSQ_ERR_SUCCESS;
#endif
	return rc;
}
