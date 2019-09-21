//
//  util_string.h
//  libmosquitto
//
//  Created by drosdesd on 20/09/2019.
//

#ifndef UTIL_STRING_H
#define UTIL_STRING_H

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>

#include "tls_mosq.h"
#include "mosquitto.h"
#include "mosquitto_internal.h"
#ifdef WITH_BROKER
#  include "mosquitto_broker_internal.h"
#endif

int strint(char *string);

#endif /* util_string_h */
