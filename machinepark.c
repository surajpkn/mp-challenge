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
double frequency = 0.2;	// 1/5th of a second
double seconds_history = 300; // 5 minutes
int window_size;

/* Helper for CURL */
typedef struct MemoryStruct {
  char *data;
  size_t size;
} chunk_t;

/*******************************************************
 *                                                     *
 *                     Utilities                       *
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


int send_alert (machine_t *machine, double avg)
{
	printf ("ALERT for machine %s, with avg = %f\n", machine->uuid, avg);

	return 0;
}

/*******************************************************
 *                                                     *
 *                     Operations                      *
 *                                                     *
 *******************************************************/

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

	/* parse and create json */
	mlist = json_tokener_parse (chunk.data);
	len = json_object_array_length (mlist);
	if (len == 0) {
		printf ("FATAL ERROR: machine list is not an array\n");
		return rc;
	}

	window_size = (int)ceil((1/frequency)*seconds_history);
	printf ("Window size to be created = %d\n", (int)ceil((1/frequency)*seconds_history));
	
	/* iterate and store machine uuids */
	for (i = 0; i < len; i++) {
		const char *mstr = json_object_get_string (json_object_array_get_idx (mlist, i));
		strncpy (machines[i].uuid, &mstr[18], 36);
		machines[i].uuid[36] = '\0';
		machines[i].current_cur = 0;
		machines[i].current_threshold = 0;
		machines[i].current_avgwindow = (double*) malloc (sizeof(double) * window_size);
		machines[i].size = 0;
		machines[i].head = 0;
	}

	/* free memory */
	json_object_put (mlist);	
	free (chunk.data);

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

	/* send alert if current is greater than threshold */
	if (machine->current_cur > machine->current_threshold) {
		int i = 0;
		double sum = 0, avg = 0;
	 	for (i = 0; i < machine->size; i++)
			sum += machine->current_avgwindow[i];
		if (machine->size != 0)
			avg = sum / machine->size;	
		send_alert (machine, avg);
	}

	/* update the average window */
	machine->current_avgwindow[machine->head] = machine->current_cur;
	machine->head = (machine->head == window_size - 1) ? 0 : machine->head + 1;
	if (machine->size < window_size)
		machine->size++;

	/* free memory */
	json_object_put (jdetail);
	free (chunk.data);
	
	rc = 0;
	return rc;
}


/* monitor()
 * The principal function that monitors the machines 
 */
int monitor (machine_t machines[], sensor_t *sensor, int run_mins) 
{
   	int rc = -1; 
	int i = 0;	
	int64_t timenow = epochtime ();		
	int64_t endtime = timenow + (run_mins * 60);

	if (run_mins == 0) 
		timenow = 0;

	while (timenow < endtime) {
	
		//printf ("Starting new iteration\n");	
		/* monitor/operate on each machine */
		for (i = 0; i < 243; i++) {			
			rc = monitor_machine (&machines[i]);
			if (rc < 0) {
				printf ("ERROR: operations on machine %s failed\n", machines[i].uuid);
				return rc;
			}
		}
 
		/* sleep for 0.2 seconds */
		usleep (200000);

		if (run_mins == 0)
			timenow = 0;
		else 
			timenow = epochtime();
	} 

	rc = 0;
	return 0;
}



/*******************************************************
 *                                                     *
 *                     MAIN		                       *
 *                                                     *
 *******************************************************/

/* Main
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
    printf ("Monitoring set for %d minutes (0 = indefinite)\n", run_mins); fflush(0); 

    /* Intialize machine data */
    int rc = machines_init (machines, &sensor);
    if (rc < 0) {
        printf ("Error: Machine initialization failed\n");
        return rc;
    }

    /* Start monitor */
    rc = monitor (machines, &sensor, run_mins);
	if (rc < 0) {
		printf ("Failure while monitoring machines\n");
		return -1;
	}

	/* free memory */
	for (i = 0; i < 243; i++)
		free (machines[i].current_avgwindow);

    /* Exit */
    printf ("Monitoring for stipulated time complete. Exiting...\n");
    
    return 0;
}




