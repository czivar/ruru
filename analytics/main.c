#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <IP2Location.h>
#include <sqlite3.h> 
#include <arpa/inet.h>
#include <pthread.h>
#include <json-c/json.h>
#include <curl/curl.h>

int debug = 0;

static char * locationdb = "data/ip2location-db5.bin";
static char * proxydb = "data/proxy.db";
static char * asndb = "data/asn.db";

typedef struct {
	unsigned int asnumber;
	char * asname;
} asinfo;

struct thread_args {
	char *publish;
	char *influx;
    	char *bind;
};

/* Description: Convert the IP address (v4) into number */
static uint32_t 
ip2no(char* ipstring)
{
    uint32_t ip = inet_addr(ipstring);
    uint8_t *ptr = (uint8_t *) &ip;
    uint32_t a = 0;

    if (ipstring != NULL)
    {
        a =  (uint8_t)(ptr[3]);
        a += (uint8_t)(ptr[2]) * 256;
        a += (uint8_t)(ptr[1]) * 256 * 256;
        a += (uint8_t)(ptr[0]) * 256 * 256 * 256;
    }
    return a;
}

asinfo *
get_asn(char* ip, sqlite3_stmt *stmt)
{
	uint32_t ipnumber = ip2no(ip);
	asinfo * info;

	info = (asinfo *) malloc(sizeof (asinfo));

	sqlite3_bind_int64(stmt, 1, ipnumber); 
	sqlite3_step(stmt);
	info->asnumber = sqlite3_column_int(stmt, 0);
	// This returned pointed ir only valid for a set of time
	if (info->asnumber != 0){
		//info->asname = (char *) sqlite3_column_text(stmt, 1);
		info->asname = malloc ( sizeof(char) * strlen((char *)sqlite3_column_text(stmt, 1))+1 );
		memcpy(info->asname, (char *) sqlite3_column_text(stmt, 1), strlen((char *)sqlite3_column_text(stmt, 1))+1);
	} else {
		info->asname = malloc ( sizeof(char) * 2 );
		memcpy(info->asname, "-", 2);
	}

	sqlite3_reset(stmt);
   	//printf("AS results: AS%u is %s\n", info->asnumber, info->asname);
	return info;
}

char *
get_proxy_type(char* ip, sqlite3_stmt *stmt)
{
	uint32_t ipnumber = ip2no(ip);
	// Proxy type can be: VPN, TOR, PUB, WEB, DCH
	char *proxy_type;

	sqlite3_bind_int64(stmt, 1, ipnumber); 
	sqlite3_step(stmt);
	if ( sqlite3_column_text(stmt, 0) ){
		proxy_type = malloc ( sizeof(char) * strlen((char *)sqlite3_column_text(stmt, 0))+1 );
		memcpy(proxy_type, (char *) sqlite3_column_text(stmt, 0), strlen((char *)sqlite3_column_text(stmt, 0))+1);
   		//printf("Proxy results: %s\n", proxy_type);
	} else {
		proxy_type = malloc ( sizeof(char) * 2 );
                memcpy(proxy_type, "-", 2);
	}

	sqlite3_reset(stmt);
	return proxy_type;
}

/* call the location api */
IP2LocationRecord *
get_location(char ip[30], IP2Location *ip2location){
	IP2LocationRecord *record = IP2Location_get_all(ip2location, ip);
	return record;
}

/* convert hex to decimal IP quad */
int 
ip_hex_to_dquad(const char *input, char *output, size_t outlen)
{
	unsigned int a, b, c, d;

	if (sscanf(input, "%2x%2x%2x%2x", &a, &b, &c, &d) != 4){
		return -1;
	}

	snprintf(output, outlen, "%u.%u.%u.%u", a, b, c, d);
	return 0;
}

int
count_escapes(char* src) 
{
	int count = 0;
	char c;

	while ((c = *(src++))) {
		switch(c) {
			case ',': 
			case ' ': 
			case '=': 
				count++;
				break;
		}
	}

	return count;
}

/* Escaping for influx... */
int 
expand_escapes(char* dest, const char* src) 
{
	char c;
	int count = 0;

	while ((c = *(src++))) {
		switch(c) {
			case ',': 
			case ' ': 
			case '=': 
				*(dest++) = '\\';
				*(dest++) = c;
				count++;
				break;
			default:
				*(dest++) = c;
		}
	}

	*dest = '\0'; /* Ensure nul terminator */
	return count;
}

/* Parse incoming messages */
int 
parse_message(char message[256], IP2Location *ip2location, sqlite3_stmt *stmt, sqlite3_stmt *stmt_proxy, void *publisher, CURL *curl, char *influx_hostname)
{
	char *source_ip_hex;
	char *destination_ip_hex;
	char *latency_external;
	char *latency_internal;
	char source_ip[30];
	char destination_ip[30];
	IP2LocationRecord *source_location;
	IP2LocationRecord *destination_location;
	int i;
	asinfo *destination_as;
	asinfo *source_as;
	json_object *json;
	zmq_msg_t msg;
	const char *jsonstring;
	CURLcode res;
	char *influxstring;
	int influxstring_len;
	char *source_country_escaped; 
	char *source_city_escaped;
	char *source_asname_escaped; 
	char *source_proxy_type;
	char *destination_country_escaped;
	char *destination_city_escaped;
	char *destination_asname_escaped;
	char *destination_proxy_type;
	int escaped = 0;
	int latency_int_internal;
	int latency_int_external;
	int latency_int_total;

	// posititions in the string
	int lastpos = 0;
	int retrieved = 0;

	for (i=0; i<256; i++){
		if (message[i] == '-'){
			if (lastpos == 0){
				lastpos = i;
				 continue;
			}
			switch (retrieved++){
			case 0: 
				source_ip_hex = malloc ( sizeof(char) * ( i-lastpos ) );
				memcpy(source_ip_hex, &message[lastpos+1], (i-lastpos-1));
				source_ip_hex[i-lastpos-1] = '\0';
				break;
			case 1: 
				destination_ip_hex = malloc ( sizeof(char) * ( i-lastpos ) );
				memcpy(destination_ip_hex, &message[lastpos+1], (i-lastpos-1));
				destination_ip_hex[i-lastpos-1] = '\0';
				break;
			case 2: 
				latency_external = malloc ( sizeof(char) * ( i-lastpos ) );
				memcpy(latency_external, &message[lastpos+1], (i-lastpos-1));
				latency_external[i-lastpos-1] = '\0';
				break;
			case 3: 
				latency_internal = malloc ( sizeof(char) * ( i-lastpos ) );
				memcpy(latency_internal, &message[lastpos+1], (i-lastpos-1));
				latency_internal[i-lastpos-1] = '\0';
				break;
			}
			lastpos = i;
		}
		if (message[i] == '\0') break;
	}

	if (retrieved != 4){
		printf("Message parsing failed (less or more than 4 token are retrieved), skipping this message");
		if (latency_external) free(latency_external);
		if (latency_internal) free(latency_internal);
		if (destination_ip_hex) free(destination_ip_hex);
		if (source_ip_hex) free(source_ip_hex);
		return -1;
	}

	ip_hex_to_dquad(source_ip_hex, source_ip, sizeof(source_ip));
	ip_hex_to_dquad(destination_ip_hex, destination_ip, sizeof(destination_ip));

	// IP -> location
	source_location = get_location(source_ip, ip2location);
	destination_location = get_location(destination_ip, ip2location);
	
	// IP -> Proxy
	source_proxy_type = get_proxy_type(source_ip, stmt_proxy);
	destination_proxy_type = get_proxy_type(destination_ip, stmt_proxy);

	// IP -> ASN
	destination_as = get_asn(destination_ip, stmt);
	source_as = get_asn(source_ip, stmt);
	
	// Latency to int
	latency_int_internal = atoi (latency_internal);
	latency_int_internal = latency_int_internal / 1000;
	latency_int_external = atoi (latency_external);
	latency_int_external = latency_int_external / 1000;
	latency_int_total = latency_int_internal + latency_int_external;
	

	json = json_object_new_object();
	json_object_object_add(json, "source_country", json_object_new_string(source_location->country_long));
	json_object_object_add(json, "source_countrycode", json_object_new_string(source_location->country_short));
	json_object_object_add(json, "source_city", json_object_new_string(source_location->city));
	json_object_object_add(json, "source_lat", json_object_new_double(source_location->latitude));
	json_object_object_add(json, "source_long", json_object_new_double(source_location->longitude));
	json_object_object_add(json, "source_asn", json_object_new_int(source_as->asnumber));
	json_object_object_add(json, "source_as", json_object_new_string(source_as->asname));
	json_object_object_add(json, "source_proxy_type", json_object_new_string(source_proxy_type));
	json_object_object_add(json, "destination_country", json_object_new_string(destination_location->country_long));
	json_object_object_add(json, "destination_countrycode", json_object_new_string(destination_location->country_short));
	json_object_object_add(json, "destination_city", json_object_new_string(destination_location->city));
	json_object_object_add(json, "destination_lat", json_object_new_double(destination_location->latitude));
	json_object_object_add(json, "destination_long", json_object_new_double(destination_location->longitude));
	json_object_object_add(json, "destination_asn", json_object_new_int(destination_as->asnumber));
	json_object_object_add(json, "destination_as", json_object_new_string(destination_as->asname));
	json_object_object_add(json, "destination_proxy_type", json_object_new_string(destination_proxy_type));
	json_object_object_add(json, "latency_internal", json_object_new_int(latency_int_internal));
	json_object_object_add(json, "latency_external", json_object_new_int(latency_int_external));
	json_object_object_add(json, "latency_total", json_object_new_int(latency_int_total));


	jsonstring = json_object_to_json_string(json);

	zmq_msg_init_size (&msg, strlen(jsonstring));
	memcpy(zmq_msg_data(&msg), jsonstring, strlen(jsonstring));
	
	if (debug) printf("Parsed JSON: %s stlen %lu\n", jsonstring, strlen(jsonstring)); 

	//Send it to socket
	zmq_msg_send(&msg, publisher, 0);

	//escaping string values
	source_country_escaped = malloc(1 + sizeof(char) * (strlen(source_location->country_long) + count_escapes(source_location->country_long)));
	escaped += expand_escapes(source_country_escaped, source_location->country_long);
	
	source_city_escaped = malloc(1 + sizeof(char) * (strlen(source_location->city) + count_escapes(source_location->city)));
	escaped += expand_escapes(source_city_escaped, source_location->city);
	
	source_asname_escaped = malloc(1 + sizeof(char) * (strlen(source_as->asname) + count_escapes(source_as->asname)));
	escaped += expand_escapes(source_asname_escaped, source_as->asname);
	
	destination_country_escaped = malloc(1 + sizeof(char) * (strlen(destination_location->country_long) + count_escapes(destination_location->country_long)));
	escaped += expand_escapes(destination_country_escaped, destination_location->country_long);
	
	destination_city_escaped = malloc(1 + sizeof(char) * (strlen(destination_location->city) + count_escapes(destination_location->city)));
	escaped += expand_escapes(destination_city_escaped, destination_location->city);
	
	destination_asname_escaped = malloc(1 + sizeof(char) * (strlen(destination_as->asname) + count_escapes(destination_as->asname)));
	escaped += expand_escapes(destination_asname_escaped, destination_as->asname);

	// this must be enough
	influxstring_len = strlen(jsonstring) + escaped + 200;
	//printf("Escaped: %d, total size: %u \n", escaped, influxstring_len);
	
	influxstring = (char *) malloc(sizeof(char) * influxstring_len + 1);
	snprintf(influxstring, 
		influxstring_len, 
		"latency,"
		"source_country=%s,"
		"source_countrycode=%s,"
		"source_city=%s,"
		"source_lat=%f,"
		"source_long=%f,"
		"source_asn=%u,"
		"source_as=%s,"
		"source_proxy_type=%s,"
		"destination_country=%s,"
		"destination_countrycode=%s,"
		"destination_city=%s,"
		"destination_lat=%f,"
		"destination_long=%f,"
		"destination_asn=%u,"
		"destination_as=%s,"
		"destination_proxy_type=%s "
		"internal=%d,external=%d,total=%d",
		source_country_escaped, 
		source_location->country_short, 
		source_city_escaped, 
		source_location->latitude, 
		source_location->longitude, 
		source_as->asnumber, 
		source_asname_escaped,
		source_proxy_type,
		destination_country_escaped, 
		destination_location->country_short, 
		destination_city_escaped, 
		destination_location->latitude, 
		destination_location->longitude, 
		destination_as->asnumber, 
		destination_asname_escaped,
		destination_proxy_type,
		latency_int_internal,
		latency_int_external, 
		latency_int_total);

	//Send it to Influx
	if(curl) {
		/* First set the URL that is about to receive our POST. This URL can
		   just as well be a https:// URL if that is what should receive the
		   data. */ 
		curl_easy_setopt(curl, CURLOPT_URL, influx_hostname);
                //printf("Curl request");
		/* Now specify the POST data */ 
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, influxstring);

		/* Perform the request, es will get the return code */ 
		res = curl_easy_perform(curl);
		/* Check for errors */ 
		if(res != CURLE_OK){
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		}
	}

	//Free everything malloced
	zmq_msg_close (&msg);
	json_object_put(json);
	IP2Location_free_record(source_location);
	IP2Location_free_record(destination_location);
	free(source_ip_hex);
	free(destination_ip_hex);
	free(destination_as->asname);
	free(destination_as);
	free(source_as->asname);
	free(source_as);
	free(latency_external);
	free(latency_internal);
	free(influxstring);
	free(source_country_escaped);
	free(source_asname_escaped);
	free(source_city_escaped);
	free(source_proxy_type);
	free(destination_country_escaped);
	free(destination_asname_escaped);
	free(destination_city_escaped);
	free(destination_proxy_type);
	return 0;
}


/* This is the part that runs mult-threaded and each connects to a socket */
void *
process_socket(void* argp){
	// TODO: as we are only reading, one handle for sqlite and ip2location might be enough
	sqlite3 *ip2asn;
	sqlite3 *ip2proxy;
	sqlite3_stmt *stmt;
	sqlite3_stmt *stmt_proxy;
	IP2Location *ip2location;
	void *context = zmq_ctx_new ();
	void *requester = zmq_socket (context, ZMQ_SUB);
	void *publisher = zmq_socket (context, ZMQ_PUB);
	struct thread_args *args = argp;
	char *hostname = args->bind;
	char *publish_hostname = args->publish;
	char *influx_hostname = args->influx;
	CURL *curl;

	int rc = zmq_bind (requester, hostname);
	assert (rc == 0);

	rc = zmq_connect (publisher, publish_hostname);
	assert (rc == 0);
	//rc = zmq_connect (publisher, "tcp://127.0.0.1:6000");

	char *filter = "LAT-";
    	rc = zmq_setsockopt (requester, ZMQ_SUBSCRIBE, filter, strlen (filter));
	assert (rc == 0);
	
	rc = sqlite3_open(asndb, &ip2asn);
	assert (rc == 0);

	rc = sqlite3_open(proxydb, &ip2proxy);
	assert (rc == 0);
	
	// create reusable sqlite3 stmt
	sqlite3_prepare_v2(ip2asn, "select asn,\"as\" from ip2location_asn where ip_from <= ?1 and ip_to >= ?1 limit 1;", -1, &stmt, NULL);
	
	// create reusable sqlite3 stmt
	sqlite3_prepare_v2(ip2proxy, "select proxy_type from ip2proxy where ip_from <= ?1 and ip_to >= ?1 limit 1;", -1, &stmt_proxy, NULL);

	ip2location = IP2Location_open(locationdb);
	printf("IP2Location API version: %s (%lu)\n", IP2Location_api_version_string(), IP2Location_api_version_num());

	curl = curl_easy_init();

	while (1){
		char buffer[256];
		int size = zmq_recv (requester, buffer, sizeof(buffer), 0);
		//printf("buffer: %s\n", buffer);
		buffer[size] = '\0';
		parse_message(buffer, ip2location, stmt, stmt_proxy, publisher, curl, influx_hostname);
	}

	curl_easy_cleanup(curl);
	sqlite3_close(ip2asn);
	sqlite3_close(ip2proxy);

	free(args);
	zmq_close (requester);
	zmq_ctx_destroy (context);
	return 0;
}

int 
main (int argc, char **argv)
{
	// attach to all DPDK sockets
	int cores = 0;
	int i, err, mode;
	pthread_t thread_id[100];
	char *publish_hostname;
	char *influx_hostname;
	char *strlist[argc];	

	struct thread_args *args = malloc(sizeof *args);

	curl_global_init(CURL_GLOBAL_ALL);

	//Command line argument parsing
	if (argc < 2){
		printf("Run as: %s --bind tcp://127.0.0.1:5502 tcp://127.0.0.1:5503 --publish tcp://0.0.0.0:6000 --influx http://localhost:8086/write?db=rtt\n",
			argv[0]);
		return -1;
	}
	
	for ( i = 1; i<argc; i++){
		if(strcmp(argv[i], "--debug") == 0){
			debug = 1;		
			continue;
		}
		if(strcmp(argv[i], "--influx") == 0){
			mode = 3;		
			continue;
		}
		if(strcmp(argv[i], "--publish") == 0){
			mode = 2;		
			continue;
		}
		if(strcmp(argv[i], "--bind") == 0){
			mode = 1;		
			continue;
		}
		// bind addresses are coming
		if(mode == 1){
			strlist[cores] = malloc(1 + strlen(argv[i]));
			strcpy(strlist[cores], argv[i]);	
			cores++;
		}
		if(mode == 2){
			publish_hostname = malloc(1 + strlen(argv[i]));
			strcpy(publish_hostname, argv[i]);
		}
		if(mode == 3){
			influx_hostname = malloc(1 + strlen(argv[i]));
			strcpy(influx_hostname, argv[i]);
		}
	}

	if (publish_hostname == NULL || strlist[0] == NULL){
		printf("Argument parsing failed.\n");
		return -1;
	} else {
		printf("Publishing on: %s\n", publish_hostname);
		for ( i = 0; i < cores; i++) {
			printf("Binding / listening on: %s\n", strlist[i]);
		}
	}

	for ( i = 0; i < cores; i++) {
		args->bind = strlist[i];
		args->publish = publish_hostname;
		args->influx = influx_hostname;
		printf("Trying to start listener thread on %s", args->bind);
		err = pthread_create(&thread_id[i], NULL, &process_socket, args);
		if (err != 0){
			printf("\n Can't create thread :[%s]", strerror(err));
		} else {
			printf("\n Thread %i created successfully\n", i);
		}
		sleep(2);	
	}

	
	// wait until all threads finish
	for ( i = 0; i < cores; i++) {
		pthread_join(thread_id[i], NULL);
	}
	
	curl_global_cleanup();

	if (publish_hostname) free(publish_hostname);
	for ( i =0; i < cores; i++){
		if(strlist[cores]) free(strlist[cores]);
	}

	free (args);
	return 0;
}


