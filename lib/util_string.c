//
//  util_string.c
//  libmosquitto
//
//  Created by drosdesd on 20/09/2019.
//

#include "util_string.h"

int strint(char *string)
{
    char *eptr;
    long result;
    
    result = strtol(string, &eptr, 10);
    
    /* If the result is 0, test for an error */
    if (result == 0){
        if (errno == EINVAL){
            printf("Conversion error occurred: %d\n", errno);
            return MOSQ_ERR_STP; //ADD MOSQ ERR CONVERSION
        }
        if (errno == ERANGE){
            printf("The value provided was out of range\n");
            return MOSQ_ERR_STP;
        }
    }
    
    assert(result <= INT_MAX);
    assert(result >= INT_MIN);
    return (int)result;
}
