#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "json.h"
#include <uuid/uuid.h>
#include <time.h>

/* periods in hours */
#define PERIOD_SHORT 0.05
#define PERIOD_LONG 1

/* storage history in number of days*/
#define STORAGE_SHORT 100
#define STORAGE_LONG 10

/* Number of componentns */
#define NUM_TOTAL 243
#define NUM_DMG_DMC 15
#define NUM_DMG_DMU 96
#define NUM_DMG_NTX 36
#define NUM_DMG_NZX 24
#define NUM_KASOTEC_A7 24
#define NUM_KASOTEC_A13 6
#define NUM_PERNDORFER_WSS 9
#define NUM_TRUMPF_3000 12
#define NUM_TRUMPF_7000 18
#define NUM_DMG_LASTERTEC 3

/* Component types including ALL */
typedef enum {
    CMP_ALL,
    CMP_DMG_DMC, 
    CMP_DMG_DMU,
    CMP_DMG_NTX,
    CMP_DMG_NZX,
    CMP_KASOTEC_A7,
    CMP_KASOTEC_A13,
    CMP_PERNDORFER_WSS,
    CMP_TRUMPF_3000,
    CMP_TRUMPF_7000,
    CMP_DMG_LASTERTEC,
    CMP_END
} components_t;

typedef struct current_window {
    double      current;
    int64_t     timestamp;
} __attribute__((packed)) cw_t;

typedef struct machine {
    char            uuid[37];               /* uuid with a null character */
    char            *name;                  /* Name of the machine */
    components_t    type;                   /* Machine type such as mill, lathe etc */
    double          current_cur;            /* The current value */
    double          current_threshold;      /* The current threshold */
    cw_t            *current_avgwindow;     /* A static yet circular buffer using size and head variables */
    cw_t            *current_periodwindow;  /* An array for storing energy consumption over a period */
    int             head;                   /* The current head of current_avgwindow */
    int             phead;                  /* The head pointer for period window */
} __attribute__((packed)) machine_t;

typedef struct sensor {
    char        timestamp[20];          /* Sensor timestamp */
    double      *pressure;              /* Pressure */
    double      *temperature;           /* Temperature */
    double      *humidity;              /* Humidity */
    int         size;                   /* size */
} __attribute__((packed)) sensor_t;

typedef struct period_history {
    struct tm   starttime;              /* Timeframe start */
    struct tm   endtime;                /* Timeframe end */ 
    double      avg_temperature;        /* Average temperature during timeframe */
    double      avg_humidity;           /* Average humidity during timeframe */
    double      avg_pressure;           /* Average pressure during timeframe */
    double      rho;                    /* Air density */
    double      *avg_current;           /* Average current of different components until CMP_END*/
    double      *rho_cur_ratio;         /* Ratio of the air density and current - Larger the value, better it is */         
} phist_t;

typedef struct llist {
    phist_t         *data;              /* Linked list data */
    struct llist    *next;              /* Linked list next */
    struct llist    *prev;              /* Linked list previous */
} llist_t;

typedef struct master_data {
    llist_t     *head;                  /* The head element */
    llist_t     *last;                  /* The last element */
    int         size;                   /* The size */
} mmdat_t;

typedef struct operation_summary {
    double      *avg_current;           /* The average current during the timeslot */
    double      *avg_ratio;             /* Average ratios */
    double      *variance;              /* Variances of ratios*/
    double      rho_variance;          /* Air density variance */
    double      avg_temp;               /* Average temperature */
    double      avg_humd;               /* Average humidity */
    double      avg_pres;               /* Average pressure */
    double      avg_rho;                /* Averasge air density */
} opsum_t;

/* Helper for CURL */
typedef struct MemoryStruct {
    char *data;
    size_t size;
} chunk_t;

static inline int64_t epochtime ()
{
    return (int64_t) time (NULL);
}
