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

#ifdef WITH_WEBSOCKETS
#include <libwebsockets.h>
#endif



#ifdef WITH_BROKER
int init_list(PORT_LIST** head, char *type)
{
    *head = NULL;
    return MOSQ_ERR_SUCCESS;
}

void print_list(PORT_LIST* head, char *type)
{
    PORT_LIST * temp;
    fprintf(stdout, "List %s:", type);
    for (temp = head; temp; temp = temp->next){
        fprintf(stdout, " %d,", temp->broker.port);
    }
    fprintf(stdout, "\n");
}

PORT_LIST* add(PORT_LIST* node, BROKER broker)
{
    PORT_LIST* temp = (PORT_LIST*) malloc(sizeof (PORT_LIST));
    if (temp == NULL) {
        exit(EXIT_FAILURE);
        //return MOSQ_ERR_NOMEM; // no memory available
    }
    temp->broker = broker;
    temp->next = node;
    node = temp;
    log__printf(NULL, MOSQ_LOG_INFO, "Broker %d, added!", broker.port);
    return node;
}

bool in_list(PORT_LIST* head, char *address, int port)
{
    PORT_LIST* current = head;
    while(current!= NULL){
        if(current->broker.port == port) return true;
        current = current->next;
    }
    return false;
}

PORT_LIST* delete_node(PORT_LIST* head, BROKER broker)
{    
    PORT_LIST* temp, *prev;
    temp = head;
    if(temp != NULL && temp->broker.port == broker.port){
        head = temp->next;
        free(temp);
        return head;
    }
    
    while(temp!=NULL && temp->broker.port != broker.port){
        prev = temp;
        temp = temp->next;
    }
    
    if(temp == NULL) return head;
    prev->next = temp->next;
    
    free(temp);
    return head;
}

PORT_LIST* empty_list(PORT_LIST *head)
{
    PORT_LIST* current = head;
    PORT_LIST* next;
    
    while (current!=NULL) {
        next = current->next;
        free(current);
        current = next;
    }
    head = NULL;
    return head;
}

int set__ports(struct mosquitto__stp *status, int msg_root_port, int msg_root_pid, int msg_distance, int msg_port, int msg_pid)
{
    int my_root_port, my_root_pid;
    int my_distance;
    int my_port, my_pid;
    
    my_root_port = status->my_root->port;
    my_root_pid = status->my_root->res->pid;
    my_distance = status->distance;
    my_port = status->my->port;
    my_pid = status->my->res->pid;
    
    if(my_root_pid < msg_root_pid){
        //check distance if less it's a DP else let's see
        log__printf(NULL, MOSQ_LOG_INFO, "SET designated 1");
        return DESIGNATED_PORT;
    }
    
    if(my_root_pid == msg_root_pid){
        if(my_distance < msg_distance){
            if(my_pid < msg_pid){
                log__printf(NULL, MOSQ_LOG_INFO, "SET designated 2");
                return DESIGNATED_PORT; //not always
            }else if(my_pid > msg_pid){
                log__printf(NULL, MOSQ_LOG_INFO, "SET BLOCK 1");
                return BLOCKED_PORT;
            }else{
                return NO_PORT;
            }
        } else if(my_distance > msg_distance){
            log__printf(NULL, MOSQ_LOG_INFO, "SET ROOT 1");
            return ROOT_PORT;
        } else if(my_distance == msg_distance){
            if(my_pid < msg_pid){
                log__printf(NULL, MOSQ_LOG_INFO, "SET designated 2");
                return DESIGNATED_PORT;
            }else if(my_pid > msg_pid){
                log__printf(NULL, MOSQ_LOG_INFO, "SET BLOCK 2");
                return BLOCKED_PORT;
            }else{
                return NO_PORT;
            }
        } else{
            log__printf(NULL, MOSQ_LOG_INFO, "SET NO 1");
            return NO_PORT;
        }
    }
    
    if(my_root_pid > msg_root_pid){
        log__printf(NULL, MOSQ_LOG_INFO, "SET root 2");
        return ROOT_PORT;
    }
    
    log__printf(NULL, MOSQ_LOG_INFO, "SET NO 2");
    return NO_PORT;
}


int update__stp_properties(struct mosquitto__stp *stp, struct mosquitto__bridge *bridge, struct mosquitto__bpdu__packet *packet)
{
    BROKER temp;
    int origin_port, claimed_root_port;
    int origin_pid, root_distance;
    int claimed_root_pid;
    int old_root;
    //TODO -> Avoid atoi
    origin_port = atoi(packet->origin_port);
    claimed_root_port = atoi(packet->root_port);
    origin_pid = atoi(packet->origin_pid);
    root_distance = atoi(packet->distance);
    claimed_root_pid = atoi(packet->root_pid);
    
    int my_pid = stp->my->res->pid;
    int port_next_status = NO_PORT;
    
    /* ERROR PART */
    /* Origin and node = same address */
    if(stp->my->address == packet->origin_address && stp->my->port == origin_port){
        log__printf(NULL, MOSQ_LOG_WARNING, "Packet coming from the same address and port of the broker itself");
        if(stp->my->_id == packet->origin_id){
            log__printf(NULL, MOSQ_LOG_WARNING, "...and even the ID is the same");
        }
        return MOSQ_ERR_STP;
    }
    
    if(my_pid == origin_pid){
        log__printf(NULL, MOSQ_LOG_WARNING, "Same PID/ADDRESS");
        return MOSQ_ERR_STP;
    }
    
    /* PORT STATUS UPDATE */
    port_next_status = set__ports(stp, claimed_root_port, claimed_root_pid, root_distance, origin_port, origin_pid);
   
    temp.address = "NULL";
    temp.port = origin_port;
    switch (port_next_status) {
        case DESIGNATED_PORT:
            log__printf(NULL, MOSQ_LOG_INFO, "Port %d is DESIGNATED", temp.port);
            if(!in_list(bridge->designated_ports, NULL, temp.port)){
                bridge->designated_ports = add(bridge->designated_ports, temp);
            }
            break;
        case ROOT_PORT:
            stp->my_root->res->pid = claimed_root_pid;
            stp->my_root->port = claimed_root_port;
            stp->distance = root_distance + 1;
            if(packet->origin_id){
                stp->my_root->_id = packet->origin_id;
            }
            if(packet->origin_address){
                stp->my_root->address = packet->origin_address;
            }
            /* Add in root port list */
            log__printf(NULL, MOSQ_LOG_INFO, "Port %d is ROOT", temp.port);
           
            /* Empty_list + obtain old root */
            old_root = bridge->root_port;
            // TODO strange cases
            if(old_root != 0 & old_root != temp.port){ //set old root as block
                log__printf(NULL, MOSQ_LOG_INFO, "Port %d is BLOCK", old_root);
                if(!in_list(bridge->block_ports, NULL, old_root)){
                    BROKER block;
                    block.address = NULL;
                    block.port = old_root;
                    bridge->block_ports = add(bridge->block_ports, block);
                }
            }
            bridge->root_port = temp.port;
            break;
        case BLOCKED_PORT:
            /* Add in block port list */
            log__printf(NULL, MOSQ_LOG_INFO, "Port %d is BLOCK", temp.port);
            if(!in_list(bridge->block_ports, NULL, temp.port)){
                bridge->block_ports = add(bridge->block_ports, temp);
            }
            break;
        case NO_CHANGE:
            break;
        case NO_PORT:
            log__printf(NULL, MOSQ_LOG_WARNING, "NO PORT error, impossible to have %d without port.", stp->my->port);
            return MOSQ_ERR_STP;
            break;
        default:
            log__printf(NULL, MOSQ_LOG_WARNING, "Wrong port status for %d.", stp->my->port);
            return MOSQ_ERR_STP;
            break;
    }
    
    log__printf(NULL, MOSQ_LOG_INFO, "\nList ROOT: %d", bridge->root_port);
    print_list(bridge->designated_ports, "DESIGNATED");
    print_list(bridge->block_ports, "BLOCK");

    return MOSQ_ERR_SUCCESS;
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
			send__pingreq(db->stp, mosq);
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
