#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "json.h"
#include <uuid/uuid.h>
#include <time.h>

typedef struct machine {
    char        uuid[37];				/* uuid with a null character */
	double      current_cur;			/* The current value */
    double      current_threshold;		/* The current threshold */
    double      *current_avgwindow;		/* A static yet circular buffer using size and head variables */
	int 		size;					/* The current size of current_avgwindow */
	int 		head;					/* The current head of current_avgwindow */
} machine_t;

typedef struct sensor {
    char    timestamp[20];				/* Sensor timestamp */
    double  pressure;					/* Pressure */
    double  temperature;				/* Temperature */
    double  humidity;					/* Humidity */
} sensor_t;

static inline int64_t epochtime ()
{
    return (int64_t) time (NULL);
}
