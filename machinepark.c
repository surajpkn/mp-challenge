#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <unistd.h> 

#include "json.h"
#include "machinepark.h"

#include <curl/curl.h>
#include <math.h>

/* Global variables */
char *machine_list_url = "http://machinepark.actyx.io/api/v1/machines";
char *env_sensor_url = "http://machinepark.actyx.io/api/v1/env-sensor";
char *machine_detail_base_url = "http://machinepark.actyx.io/api/v1/machine/";
double frequency = 5; // 5 seconds
double seconds_history = 300; // 5 minutes
double window_size, pwindow_size;

// must be as many - 1 as components_t
int num_machines[] = {NUM_TOTAL, NUM_DMG_DMC, NUM_DMG_DMU, NUM_DMG_NTX, NUM_DMG_NZX, NUM_KASOTEC_A7, NUM_KASOTEC_A13, NUM_PERNDORFER_WSS, NUM_TRUMPF_3000, NUM_TRUMPF_7000, NUM_DMG_LASTERTEC};

/*******************************************************
 *                                                     *
 *                     CURL                            *
 *                                                     *
 *******************************************************/

/* Write callback function for curl 
 */
size_t curl_write (void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->data = realloc(mem->data, mem->size + realsize + 1);
    if(mem->data == NULL) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

/* Function to make http request and get data
 */
int fetch_curl (char *url, chunk_t *chunk)
{
    CURLcode res;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, chunk);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if(res != CURLE_OK) {
        printf ("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return -1;
    } 
    return 0;
}

/*******************************************************
 *                                                     *
 *                     Utilities                       *
 *                                                     *
 *******************************************************/

int send_alert (machine_t *machine, double avg)
{
    printf ("ALERT for machine %s, with avg = %f\n", machine->uuid, avg);

    return 0;
}

int llist_entry_create (llist_t **entry) 
{
    *entry = (llist_t *) malloc (sizeof (llist_t));
    (*entry)->next = NULL;
    (*entry)->prev = NULL;
    (*entry)->data = (phist_t *)malloc (sizeof (phist_t));
    (*entry)->data->avg_current = (double *) malloc (sizeof (double) * CMP_END);
    (*entry)->data->rho_cur_ratio = (double *) malloc (sizeof (double) * CMP_END);

    return 0;
}

int llist_entry_destroy (llist_t **entry)
{
    if (*entry) {
        if ((*entry)->data) {
            if ((*entry)->data->avg_current)
                free ((*entry)->data->avg_current);
            if ((*entry)->data->rho_cur_ratio)
                free ((*entry)->data->rho_cur_ratio);    
        }        
        free (*entry);
        *entry = NULL;
    }
    return 0;
}

/* unused in this version 
 */
int phist_entry_create (phist_t **entry)
{
    *entry = (phist_t *)malloc (sizeof (phist_t));
    (*entry)->avg_current = (double *) malloc (sizeof (double) * CMP_END);
    (*entry)->rho_cur_ratio = (double *) malloc (sizeof (double) * CMP_END);
    return 0;
}

/* unused in this version
 */
int phist_entry_destroy (phist_t **entry)
{
    if (*entry) {
        if ((*entry)->avg_current)
            free ((*entry)->avg_current);
        if ((*entry)->rho_cur_ratio)
            free ((*entry)->rho_cur_ratio);
        free (*entry);
        *entry = NULL;
    }
    return 0;
}

void print_phist_data (llist_t *head)
{
    llist_t *ptr = head;
    while (ptr) {
        char buf1[21], buf2[21];
        strftime (buf1, 21, "%Y-%m-%dT%H:%M:%S", &ptr->data->starttime);
        strftime (buf2, 21, "%Y-%m-%dT%H:%M:%S", &ptr->data->endtime);
        printf("Starttime: %s, Endtime: %s, Average Temperature: %f, Average Pressure: %f, Average Humidity: %f, RHO:%f Currents: 0:%f, 1:%f, 2:%f, 3:%f, 4:%f, 5:%f, 6:%f, 7:%f, 8:%f, 9:%f, 10:%f\n", buf1, buf2, 
                ptr->data->avg_temperature, ptr->data->avg_pressure, ptr->data->avg_humidity, ptr->data->rho, ptr->data->avg_current[0], ptr->data->avg_current[1], ptr->data->avg_current[2], ptr->data->avg_current[3], 
                ptr->data->avg_current[4], ptr->data->avg_current[5], ptr->data->avg_current[6], ptr->data->avg_current[7], ptr->data->avg_current[8], ptr->data->avg_current[9], ptr->data->avg_current[10]);
        ptr = ptr->next;
    }
}

void print_operations_summary (opsum_t *summary, int size) 
{
    int i = 0;
    for (i = 0; i < size; i++) {
        printf("--- Printing Summary %d of %d ---\n", i+1, size);
        printf("Average Temperature:%f, Average Pressure:%f, Average Humidity:%f, Average air density:%f, Airdensity variance:%f\nratios--: 0:%f, 1:%f, 2:%f, 3:%f, 4:%f, 5:%f, 6:%f, 7:%f, 8:%f, 9:%f, 10:%f\nCurrents: 0:%f, 1:%f, 2:%f, 3:%f, 4:%f, 5:%f, 6:%f, 7:%f, 8:%f, 9:%f, 10:%f\nVariance: 0:%f, 1:%f, 2:%f, 3:%f, 4:%f, 5:%f, 6:%f, 7:%f, 8:%f, 9:%f, 10:%f\n", 
                summary->avg_temp, summary->avg_pres, summary->avg_humd, summary->avg_rho, summary->rho_variance,
                summary->avg_ratio[0], summary->avg_ratio[1], summary->avg_ratio[2], summary->avg_ratio[3], summary->avg_ratio[4], summary->avg_ratio[5], 
                summary->avg_ratio[6], summary->avg_ratio[7], summary->avg_ratio[8], summary->avg_ratio[9], summary->avg_ratio[10],
                summary->avg_current[0], summary->avg_current[1], summary->avg_current[2], summary->avg_current[3], summary->avg_current[4], summary->avg_current[5],
                summary->avg_current[6], summary->avg_current[7], summary->avg_current[8], summary->avg_current[9], summary->avg_current[10],
                summary->variance[0], summary->variance[1], summary->variance[2], summary->variance[3], summary->variance[4], summary->variance[5],
                summary->variance[6], summary->variance[7], summary->variance[8], summary->variance[9], summary->variance[10]);

    }

}

int short_period_over (struct tm tm, struct tm prev_tm) 
{
    int64_t timenow = mktime (&tm);
    int64_t timeprev = mktime (&prev_tm);
    
    if ((timenow - timeprev) >= (PERIOD_SHORT * 60 * 60))
        return 1;
    
    return 0;
}

int compare (const void *a, const void *b)
{
    return ( *(int*)a - *(int*)b );
}

int find_next_long_timestop (int *timestops, int wsize, int current_hour, int *next_timestop, int *prev_timestop, int *index)
{
    int i = 0;
    int *new_timestops = (int*) malloc (sizeof (int) * (wsize + 1));
    for (i = 0; i < wsize; i++)
        if (current_hour == timestops[i]) {
            i += 1;
            if (i >= wsize) 
                i = 0;
            *next_timestop = timestops[i];
            *prev_timestop = current_hour;
            *index = i;
            free (new_timestops);
            return 0;
        } else {
            new_timestops[i] = timestops[i];
        }

    new_timestops[wsize] = current_hour;
    qsort (new_timestops, wsize+1, sizeof (int), compare);

    for (i = 0; i < wsize+1; i++) {
        if (new_timestops[i] == current_hour) {
            if (i-1 >= 0)
                *prev_timestop = new_timestops[i-1];
            else
                *prev_timestop = new_timestops[wsize];
            *index = i;
            i += 1;
            if (i >= wsize+1)
                i = 0;
            *next_timestop = new_timestops[i];
            free (new_timestops);
            return 0;
        }
    }
    // not found
    return -1;
}

/*******************************************************
 *                                                     *
 *                 Math Operations                     *
 *                                                     *
 *******************************************************/
double air_density (double temp, double humd, double pres)
{
    /* calculate saturated vapor pressure */
    double Psv = 611.2 * pow (2.718, (16.67 * temp) / (temp + 243.5));
    double Pv = (humd / 100) * Psv;
    double Pd = (pres*100) - Pv;
    double Rv = 461.4964;
    double Rd = 287.0531;
    double Tk = temp + 273.15;
    double rho = (Pd / (Rd * Tk)) + (Pv / (Rv * Tk));
    
    return rho;
}

int air_density_current_ratio (double rho, double *currents, double *ratios)
{   
    int i = 0;
    for (i = 0; i < CMP_END; i++) {
        ratios[i] = rho / currents[i];
    }
    return 0;
}

int compute_variance (double *values, int size, double *variance)
{
    int i = 0;
    double mean = 0;
    double sum = 0;
    double sum_squared_difference = 0; 
    
    for (i = 0; i < size; i++) {
        sum += values[i];
    }
    mean = sum / size;

    for (i = 0; i < size; i++) {
        sum_squared_difference += pow ((values[i] - mean), 2);
    }
    *variance = sum_squared_difference / size; 
    return 0;
}

/*******************************************************
 *                                                     *
 *                 Period Operation                    *
 *                                                     *
 *******************************************************/

int compute_short_period_averages (machine_t machines[], sensor_t *sensor, mmdat_t *pshort_hist, struct tm start_time, struct tm end_time)
{
    printf ("================Computing short period averages======================\n");
    int i = 0;
    int j = 0;
    
    double temp_sum, temp_avg;
    double pres_sum, pres_avg;
    double humd_sum, humd_avg;
    double rho;

    double *type_sum = (double*) malloc (sizeof (double) * CMP_END);
    memset (type_sum, 0, sizeof (double) * CMP_END);
    double *type_avg = (double*) malloc (sizeof (double) * CMP_END);
    memset (type_avg, 0, sizeof (double) * CMP_END);

    /* Compute average energy consumption of all the machines */
    for (i = 0; i < NUM_TOTAL; i++) {
        // Compute for this machine
        int tmp_sum = 0;
        int avg = 0;
        for (j = 0; j < (machines[i].phead - 1); j++) {
            tmp_sum += machines[i].current_periodwindow[j].current;
        }
        if (machines[i].phead - 1 > 0)
            avg = tmp_sum / (machines[i].phead - 1);
        else 
            avg = 0;

        // Sum up the avg for this machine type
        type_sum[machines[i].type] += avg;
        type_sum[CMP_ALL] += avg;

        // Adjust
        machines[i].current_periodwindow[0] = machines[i].current_periodwindow[machines[i].phead];
        machines[i].phead = 1;
    }    
    /* Compute total average */
    for (i = 0; i < CMP_END; i++) {
        type_avg[i] = type_sum[i] / num_machines[i]; 
    }

    /* Compute average temp, humidty and pressure and air density*/
    for (i = 0; i < sensor->size - 1; i++) {
        temp_sum += sensor->temperature[i];
        humd_sum += sensor->humidity[i];
        pres_sum += sensor->pressure[i];
    }
    if (sensor->size - 1 > 0) {
        temp_avg = temp_sum / (sensor->size - 1);
        humd_avg = humd_sum / (sensor->size - 1);
        pres_avg = pres_sum / (sensor->size - 1);
        rho = air_density (temp_avg, humd_avg, pres_avg);
    
        // adjust
        sensor->temperature[0] = sensor->temperature[sensor->size];
        sensor->humidity[0] = sensor->humidity[sensor->size];
        sensor->pressure[0] = sensor->pressure[sensor->size];
        sensor->size = 1;
    } else {
        temp_avg = 0;
        humd_avg = 0;
        pres_avg = 0;
        rho = 0;
    }

    if (pshort_hist->size == STORAGE_SHORT) {
        llist_t *penultimate = pshort_hist->last->prev;
        llist_entry_destroy (&(pshort_hist->last));
        pshort_hist->last = penultimate;
        pshort_hist->last->next = NULL;
        pshort_hist->size -= 1;
    }
    
    /* make an entry */
    llist_t *entry;
    llist_entry_create (&entry);
    entry->data->starttime = start_time;
    entry->data->endtime = end_time;
    entry->data->avg_temperature = temp_avg;
    entry->data->avg_humidity = humd_avg;
    entry->data->avg_pressure = pres_avg;
    entry->data->rho = rho;
    for (i = 0; i < CMP_END; i++) {
        entry->data->avg_current[i] = type_avg[i];
    }
    if (pshort_hist->head != NULL) {
        entry->next = pshort_hist->head;
        pshort_hist->head->prev = entry; 
    }
    pshort_hist->head = entry;
    pshort_hist->size += 1;
    
    air_density_current_ratio (entry->data->rho, entry->data->avg_current, entry->data->rho_cur_ratio);

    /* Free memory */
    free (type_sum);
    free (type_avg);

    return 0;
}

int compute_long_period_averages (mmdat_t *pshort_hist, mmdat_t *plong_hist, llist_t **prev_head, struct tm starttime, struct tm endtime) 
{
    printf ("================Computing LONG period averages======================\n");
    int rc = -1;
    int i = 0;
    int count = 0;
    double rho_sum = 0;
    double temp_sum = 0;
    double pres_sum = 0;
    double humd_sum = 0;
    double *type_sum = (double *) malloc (sizeof (double) * CMP_END);
    memset (type_sum, 0, sizeof (double) * CMP_END);
    double rho_avg = 0;
    double temp_avg = 0;
    double pres_avg = 0;
    double humd_avg = 0;
    double *type_avg = (double *) malloc (sizeof (double) * CMP_END);
    memset (type_avg, 0, sizeof (double) * CMP_END);
    
    llist_t *head, *orig_head, *last_iter_head;
 
    if (!(*prev_head)) {
        printf ("Prev head is NULL, no data in short history\n");
        return 0;
    }
    
    orig_head = *prev_head;
    head = *prev_head;
    while (head) {
        temp_sum += head->data->avg_temperature;
        pres_sum += head->data->avg_pressure;
        humd_sum += head->data->avg_humidity;
        rho_sum += head->data->rho;
        for (i = 0; i < CMP_END; i++) {
            type_sum[i] += head->data->avg_current[i];
        }
        count++;
        last_iter_head = head;
        head = head->prev;
    }

    if (count > 0) {
        temp_avg = temp_sum / count;
        pres_avg = pres_sum / count;
        humd_avg = humd_sum / count;
        rho_avg = rho_sum / count;
        for (i = 0; i < CMP_END; i++) {
            type_avg[i] = type_sum[i] / count;
        }
    }

    if (plong_hist->size == STORAGE_LONG) {
        llist_t *penultimate = plong_hist->last->prev;
        llist_entry_destroy (&(plong_hist->last));
        plong_hist->last = penultimate;
        plong_hist->last->next = NULL;
        plong_hist->size -= 1;
    }

    /* make an entry */
    llist_t *entry;
    llist_entry_create (&entry);
    entry->data->starttime = starttime;
    entry->data->endtime = endtime;
    entry->data->avg_temperature = temp_avg;
    entry->data->avg_humidity = humd_avg;
    entry->data->avg_pressure = pres_avg;
    entry->data->rho = rho_avg;
    for (i = 0; i < CMP_END; i++) {
        entry->data->avg_current[i] = type_avg[i];
    }
    if (plong_hist->head != NULL) {
        entry->next = plong_hist->head;
        plong_hist->head->prev = entry;
    }
    plong_hist->head = entry;
    plong_hist->size += 1; 

    air_density_current_ratio (entry->data->rho, entry->data->avg_current, entry->data->rho_cur_ratio);   
     
    /* free memory */
    free (type_sum);
    free (type_avg);

    rc = 0;
    return rc;
}

int update_operations_summary (mmdat_t *plong_hist, opsum_t *summary) 
{
    //printf ("------------Update summary\n");
    int rc = -1;
    int count = 0;
    int i, j;
    double temp_sum = 0, humd_sum = 0, pres_sum = 0, rho_sum = 0;
    double temp_avg = 0, humd_avg = 0, pres_avg = 0, rho_avg = 0;
    double cur_sum = 0, ratio_sum;
    
    double **ratios = (double **)malloc (sizeof(double*) * CMP_END);
    for (i = 0; i < CMP_END; i++) {
        ratios[i] = (double *)malloc (sizeof(double) * plong_hist->size);
    }
    double **currents = (double **)malloc (sizeof(double*) * CMP_END);
    for (i = 0; i < CMP_END; i++) {
        currents[i] = (double *)malloc (sizeof(double) * plong_hist->size);
    }
    double *rho_collect = (double *) malloc (sizeof (double) * plong_hist->size);
    
 
    llist_t *head = plong_hist->head;   
    while (head) {
        for (i = 0; i < CMP_END; i++) {
            ratios[i][count] = head->data->rho_cur_ratio[i];
            currents[i][count] = head->data->avg_current[i];
        }
        temp_sum += head->data->avg_temperature;
        humd_sum += head->data->avg_humidity;
        pres_sum += head->data->avg_pressure;
        rho_sum += head->data->rho;
        rho_collect[count] = head->data->rho;
        count++;
        head = head->next;
    }

    summary->avg_temp = temp_sum / plong_hist->size;
    summary->avg_humd = humd_sum / plong_hist->size;
    summary->avg_pres = pres_sum / plong_hist->size;
    summary->avg_rho = rho_sum / plong_hist->size;

    for (i = 0; i < CMP_END; i++) {
        compute_variance (ratios[i], plong_hist->size, &summary->variance[i]);
        cur_sum = 0;
        for (j = 0; j < plong_hist->size; j++) {
            cur_sum += currents[i][j];
        }
        summary->avg_current[i] = cur_sum / plong_hist->size;
        ratio_sum = 0;
        for (j = 0; j < plong_hist->size; j++) {
            ratio_sum += ratios[i][j];
        }
        summary->avg_ratio[i] = ratio_sum / plong_hist->size;
    }
    compute_variance (rho_collect, count, &summary->rho_variance);

    for (i = 0; i < CMP_END; i++) {
        free (ratios[i]);
        free (currents[i]);
    }
    free (ratios);
    free (currents);
    free (rho_collect);

    rc = 0;
    return rc;
}

/*******************************************************
 *                                                     *
 *              Monitoring Operations                  *
 *                                                     *
 *******************************************************/
/* Init a single machine. 
 * Extracts the machine name and assigns the correct
 * component type for it
 */
int machine_single_init (machine_t *machine) 
{
    int rc = -1;
    char *url;
    asprintf (&url, "%s%s", machine_detail_base_url, machine->uuid);
    chunk_t chunk;
    chunk.data = malloc (1);
    chunk.size = 0;

    int res = fetch_curl (url, &chunk);
    if (res < 0) {
        printf ("ERROR: Could not fetch machine detail during machine init\n");
        return rc;
    }
    
    json_object *jdetail = json_tokener_parse (chunk.data);
    json_object *tmp = NULL;
    json_object_object_get_ex (jdetail, "name", &tmp);
    const char *name = json_object_get_string (tmp);
    if (name == NULL) {
        printf ("ERROR: Name of machine could not be dervied\n");
        return rc;
    }

    machine->name = strdup (name);
    //printf ("Name of the machine: %s\n", name);

    if (strstr (name, "DMC")) {
        machine->type = CMP_DMG_DMC;
    } else if (strstr (name, "DMU")) {
        machine->type = CMP_DMG_DMU;
    } else if (strstr (name, "NTX")) {
        machine->type = CMP_DMG_NTX;
    } else if (strstr (name, "NZX") != NULL) {
        machine->type = CMP_DMG_NZX;
    } else if (strstr (name, "A7")) {
        machine->type = CMP_KASOTEC_A7;
    } else if (strstr (name, "A13")) {
        machine->type = CMP_KASOTEC_A13;
    } else if (strstr (name, "WSS")) {
        machine->type = CMP_PERNDORFER_WSS;
    } else if (strstr (name, "3000")) {
        machine->type = CMP_TRUMPF_3000;
    } else if (strstr (name, "7000")) {
        machine->type = CMP_TRUMPF_7000;
    } else if (strstr (name, "Lasertec")) {
        machine->type = CMP_DMG_LASTERTEC;
    } else {
        printf ("ERROR: Name could not be found in list\n");
        return rc;
    }

    free (chunk.data);
    free (jdetail);

    rc = 0;
    return rc;
}

/* machines_init()
 * Initializes the machine and senor data by fetching from the URL
 */
int machines_init (machine_t machines[], sensor_t *sensor) 
{
    int i = 0;
    int rc = -1;
    json_object *mlist;
    int len = 0;

    /* init the chunk */
    chunk_t chunk;
    chunk.data = malloc (1);
    chunk.size = 0;
    
    /* Fetch the data */
    rc = fetch_curl (machine_list_url, &chunk);
    if ((rc < 0) || (chunk.size == 0)) {
        printf ("fetching machine list failed\n");
        return rc;
    }
    rc = -1;

    /* parse and create json */
    mlist = json_tokener_parse (chunk.data);
    len = json_object_array_length (mlist);
    if (len == 0) {
        printf ("FATAL ERROR: machine list is not an array\n");
        return rc;
    }

    window_size = (int)ceil((1/frequency)*seconds_history);
    pwindow_size = (int)ceil(PERIOD_SHORT*60*60 / frequency);
    printf ("Window size to be created = %d\n", (int)ceil((1/frequency)*seconds_history));
    
    /* iterate and store machine uuids */
    for (i = 0; i < len; i++) {
        const char *mstr = json_object_get_string (json_object_array_get_idx (mlist, i));
        strncpy (machines[i].uuid, &mstr[18], 36);
        machines[i].uuid[36] = '\0';
        machines[i].current_cur = 0;
        machines[i].current_threshold = 0;
        machines[i].current_avgwindow = (cw_t *) malloc (sizeof (cw_t) * window_size);
        memset (machines[i].current_avgwindow, 0, sizeof(cw_t) * window_size);
        machines[i].current_periodwindow = (cw_t *) malloc (sizeof (cw_t) * (pwindow_size + 1));
        memset (machines[i].current_periodwindow, 0, sizeof(cw_t) * pwindow_size);
        machines[i].head = 0;
        machines[i].phead = 0;
    }

    /* Fetch all the machine names/types */
    for (i = 0; i < NUM_TOTAL; i++) {
        rc = machine_single_init (&machines[i]);
        if (rc < 0) {
            printf ("ERROR: Could not init machine i: %d\n", i);
        }
    }
    
    /* Allocate memory for sensor data */
    sensor->pressure = (double *) malloc (sizeof (double) * (pwindow_size + 1));
    sensor->temperature = (double *) malloc (sizeof (double) * (pwindow_size + 1));
    sensor->humidity = (double *) malloc (sizeof (double) * (pwindow_size + 1));

    /* free memory */
    json_object_put (mlist);    
    free (chunk.data);

    printf ("Initiation Complete\n");

    rc = 0;
    return rc;
}

/* Get the sensor readings and 
 * the local time at the machine site
 */
int get_sensor_readings (sensor_t *sensor, struct tm *tm)
{
    int rc = -1;

    /* init chunk */
    chunk_t chunk;
    chunk.data = malloc (1);
    chunk.size = 0;

    rc = fetch_curl (env_sensor_url, &chunk);
    if ((rc < 0) || (chunk.size == 0)) {
        fprintf (stderr, "fetching sensor details failed\n");
        return rc;
    }

    json_object *jdetail = json_tokener_parse (chunk.data);
    json_object *jtemp = NULL, *jpres = NULL, *jhumd = NULL;
    json_object_object_get_ex (jdetail, "temperature", &jtemp);
    json_object_object_get_ex (jdetail, "pressure", &jpres);
    json_object_object_get_ex (jdetail, "humidity", &jhumd);

    /* update the period window */
    if (sensor->size == pwindow_size) {
        printf ("ERROR: phead on window_size in sensor. buffer needs clear up\n");
        return rc;
    }

    sensor->temperature[sensor->size] = json_object_get_double (json_object_array_get_idx (jtemp, 1));
    sensor->pressure[sensor->size] = json_object_get_double (json_object_array_get_idx (jpres, 1));
    sensor->humidity[sensor->size] = json_object_get_double (json_object_array_get_idx (jhumd, 1));
    sensor->size += 1;

    /* get time */
    const char *time_str = json_object_get_string (json_object_array_get_idx (jtemp, 0));
    strptime (time_str, "%Y-%m-%dT%H:%M:%S", tm);

    //printf ("Sensor data = %s and time = %s and timeepoch = %ld\n", chunk.data, time_str, mktime(tm));

    /* free memory */
    free (chunk.data);
    free (jdetail);

    rc = 0;
    return rc;
}


/* Monitor/operate on 1 machine. 
 * Updates the machine data, updates average and 
 * if there is a current > threshold, then sends an alert
 */

int monitor_machine (machine_t *machine)
{
    int rc = -1;
    char *url;

    /* create the machine url and init chunk */
    asprintf (&url, "%s%s", machine_detail_base_url, machine->uuid);
    chunk_t chunk;
    chunk.data = malloc (1);
    chunk.size = 0;

    /* fetch the new machine data */
    rc = fetch_curl (url, &chunk);
    if ((rc < 0) || (chunk.size == 0)) {
        printf ("fetching machine detail for machine %s failed\n", machine->uuid);
        return rc;
    }   

    /* fetch current and current alert */
    json_object *jdetail = json_tokener_parse (chunk.data);
    json_object *tmp = NULL;
    json_object_object_get_ex (jdetail, "current", &tmp);
    if (tmp == NULL) {
        printf ("ERROR: Could not get current for machine %s\n", machine->uuid);
        return rc;
    }
    machine->current_cur = json_object_get_double (tmp);
    json_object_object_get_ex (jdetail, "current_alert", &tmp);
    if (tmp == NULL) {
        printf ("ERROR: Could not get current_alert for machine %s\n", machine->uuid);
    }
    machine->current_threshold = json_object_get_double (tmp);
    //printf ("machine = %s, current = %f, current_alert = %f\n", machine->uuid, machine->current_cur, machine->current_threshold);

    /* Implementation with timestamp for each window entry */
    /* send alert if current is greater than threshold */
    int64_t timenow = epochtime ();
    if (machine->current_cur > machine->current_threshold) {
        int i = 0;
        double sum = 0, avg = 0;
        int count = 0;
        int head_dup = machine->head - 1;
        if (head_dup < 0) 
            head_dup = window_size - 1;
        
        while ((machine->current_avgwindow[head_dup].timestamp != 0) && (machine->current_avgwindow[head_dup].timestamp > timenow - seconds_history) && (head_dup != machine->head)) {
            sum += machine->current_avgwindow[head_dup].current;
            count++;
            head_dup -= 1;
            if (head_dup < 0) 
                head_dup = window_size - 1;
        }

        if (count > 0)
            avg = sum / count;
        else 
            avg = machine->current_avgwindow[head_dup].current;

        send_alert (machine, avg);
    }
    
    /* update the average window */
    machine->current_avgwindow[machine->head].current = machine->current_cur;
    machine->current_avgwindow[machine->head].timestamp = timenow;
    machine->head = (machine->head == window_size - 1) ? 0 : machine->head + 1; 

    /* update the period window */
    if (machine->phead == pwindow_size) {
        printf ("ERROR: phead on window_size. buffer needs clear up\n");
        return rc;
    }
    machine->current_periodwindow[machine->phead].current = machine->current_cur;
    machine->phead++;

    /* free memory */
    json_object_put (jdetail);
    free (chunk.data);
    
    rc = 0;
    return rc;
}


/* monitor()
 * The principal function that monitors the machines 
 */
int monitor (machine_t machines[], sensor_t *sensor, int run_mins, mmdat_t *pshort_hist, mmdat_t *plong_hist, int *timestops, int wsize, opsum_t *summary) 
{
    int rc = -1; 
    int i = 0;  

    int64_t timenow = epochtime ();
    int64_t starttime = timenow;
    int64_t endtime = timenow + (run_mins * 60);
    
    int index, tmpindex;
    struct tm prev_tm = {0}, tm = {0}, p_starttime = {0}, p_endtime = {0};
    int next_timestop, prev_timestop;
    
    llist_t **prev_short_head = &(pshort_hist->head);
    
    if (run_mins == 0) 
        timenow = 0;

    /* Initial step to basically initialize time */
    rc = get_sensor_readings (sensor, &prev_tm);
    if (rc < 0) {
        printf ("ERROR: Retrieving sensor readings failed\n");
        return rc;
    }
    
    rc = find_next_long_timestop (timestops, wsize, prev_tm.tm_hour, &next_timestop, &prev_timestop, &index);
    if (rc < 0) {
        printf ("ERROR: Could not find the next timestop\n");
        return -1;
    }
    printf ("Current time = %d, next_timestop = %d\n", prev_tm.tm_hour, next_timestop);

    p_starttime = prev_tm;
    p_starttime.tm_hour = prev_timestop;
    p_starttime.tm_min = 0;
    p_starttime.tm_sec = 0;
   
 
    while (timenow < endtime) {
    
        printf ("Starting new iteration\n");
        /* Retrieve environmental data and time */
        rc = get_sensor_readings (sensor, &tm);
        if (rc < 0) {
            printf ("ERROR: Retrieving sensor readings failed\n");
            return rc;
        }
        
        /* monitor/operate on each machine */
        for (i = 0; i < 243; i++) {         
            rc = monitor_machine (&machines[i]);
            if (rc < 0) {
                printf ("ERROR: operations on machine %s failed\n", machines[i].uuid);
                return rc;
            }
        }

        /* Short update */
        if (short_period_over (tm, prev_tm)) {
            compute_short_period_averages (machines, sensor, pshort_hist, prev_tm, tm);    
            prev_tm = tm;
            print_phist_data (pshort_hist->head);
        }


        /* Long update */
#if 1
        if (tm.tm_hour == next_timestop) {
            p_endtime = tm;
            p_endtime.tm_min = 0;
            p_endtime.tm_sec = 0;
            compute_long_period_averages (pshort_hist, &plong_hist[index], prev_short_head, p_starttime, p_endtime);
            update_operations_summary (&plong_hist[index], &summary[index]);
            prev_short_head = &(pshort_hist->head->prev);
            index += 1;
            if (index >= wsize)
                index = 0; 
            next_timestop = timestops[index];
            p_starttime = p_endtime;
            break;
        }
#endif

#if 0 
        /* Code for testing purpuses only */
        timenow = epochtime();
        if (timenow - starttime >= 300) {
            p_endtime = tm;
            p_endtime.tm_hour = next_timestop;
            p_endtime.tm_min = 0;
            p_endtime.tm_sec = 0;
            compute_long_period_averages (pshort_hist, &plong_hist[0], prev_short_head, p_starttime, p_endtime);
            print_phist_data (plong_hist[0].head);
            update_operations_summary (&plong_hist[0], &summary[0]);
            print_operations_summary (summary, 1);
            index += 1;
            if (index >= wsize)
                index = 0;
            next_timestop = timestops[index];
            starttime = timenow;
            p_starttime = p_endtime; 
        }
#endif
 
        /* sleep for frequency seconds */
        usleep (frequency * 1000000);

        if (run_mins == 0)
            timenow = 0;
        else
            timenow = epochtime ();
   } 

    rc = 0;
    return 0;
}

/*******************************************************
 *                                                     *
 *                     MAIN                            *
 *                                                     *
 *******************************************************/

/* Main function
 */
int main (int argc, char *argv[]) 
{
    int i = 0;
    int run_mins = 0;
    machine_t machines[243];
    sensor_t sensor;

    /* Retrieve how long we want to monitor */
    if (argc > 1) {
        run_mins = strtol (argv[1], NULL, 10);
    }
    printf ("Monitoring set for %d minutes (0 = indefinite)\n", run_mins);

    /* Intialize machine data */
    int rc = machines_init (machines, &sensor);
    if (rc < 0) {
        printf ("Error: Machine initialization failed\n");
        return rc;
    }

    /* Create structs */
    mmdat_t *pshort_hist = (mmdat_t*) malloc (sizeof (mmdat_t));
    pshort_hist->head = NULL;
    pshort_hist->last = NULL;
    pshort_hist->size = 0;
    int wsize = 24 / PERIOD_LONG;
    mmdat_t *plong_hist = (mmdat_t *) malloc (sizeof (mmdat_t) * wsize);
    plong_hist->head = NULL;
    plong_hist->last = NULL;
    plong_hist->size = 0;

    int timeseed = 21;
    int *timestops = (int *)malloc (sizeof (int) * wsize);
    for (i = 0; i < wsize; i++) {
        timestops[i] = timeseed + i*PERIOD_LONG;
        if (timestops[i] >= 24) 
            timestops[i] = timestops[i] - 24;
    }

    opsum_t *summary = (opsum_t *) malloc (sizeof (opsum_t) * wsize);
    memset (summary, 0, sizeof (opsum_t) * wsize);
    for (i = 0; i < wsize; i++) {
        summary->avg_current = (double *)malloc (sizeof (double) * CMP_END);
        summary->avg_ratio = (double *)malloc (sizeof (double) * CMP_END);
        summary->variance = (double *)malloc (sizeof (double) * CMP_END);
    }


    /* Start monitor */
    rc = monitor (machines, &sensor, run_mins, pshort_hist, plong_hist, timestops, wsize, summary);
    if (rc < 0) {
        printf ("Failure while monitoring machines\n");
        return -1;
    }

    /* free memory */
    for (i = 0; i < 243; i++)
        free (machines[i].current_avgwindow);
    free (timestops);
    free (pshort_hist);
    free (plong_hist);   
    free (summary->avg_current);
    free (summary->avg_ratio);
    free (summary->variance);
    free (summary);

    /* Exit */
    printf ("Monitoring for stipulated time complete. Exiting...\n");
    
    return 0;
}




