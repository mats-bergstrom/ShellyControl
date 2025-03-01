/*                               -*- Mode: C -*- 
 * Copyright (C) 2024, Mats Bergstrom
 * $Id$
 * 
 * File name       : shellyctrl.c
 * Description     : Control a shelly plug
 * 
 * Author          : Mats Bergstrom
 * Created On      : Sun Oct  6 10:04:28 2024
 * 
 * Last Modified By: Mats Bergstrom
 * Last Modified On: Sat Mar  1 13:31:50 2025
 * Update Count    : 46
 */






#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <mosquitto.h>

#include <pthread.h>


#include "cfgf.h"

/* Move this into cfgf.h  */
#define CFGF_UL(N,X) do {			\
    unsigned long val;				\
    val = strtoul(argv[N],&s,10);		\
    if ( !s || *s )				\
	return -1;				\
    X = val;					\
} while(0)




int opt_v = 0;				/* Verbose printing */
int opt_n = 0;				/* NoActive, do not send mqtt data */



/*****************************************************************************/
/* Config file handling */
/* mqtt   <broker> <port> <id> */

/* MQTT ID, broker and port */
#define default_MQTT_BROKER	"127.0.0.1"
#define default_MQTT_ID		"shellyctrl"

char* mqtt_broker = 0;
int   mqtt_port = 1883;
char* mqtt_id = 0;

int
set_mqtt( int argc, const char** argv)
{
    if ( argc != 4 )
	return -1;
    
    if (mqtt_broker)
	free( mqtt_broker );
    mqtt_broker = strdup( argv[1] );
    if ( !mqtt_broker || !*mqtt_broker )
	return -1;

    mqtt_port = atoi(argv[2]);
    if ( (mqtt_port < 1) || (mqtt_port > 65535) )
	return -1;

    if (mqtt_id)
	free( mqtt_id );
    mqtt_id = strdup( argv[3] );
    if ( !mqtt_id || !*mqtt_id )
	return -1;

    if ( opt_v )
	printf("mqtt %s %d %s\n", mqtt_broker, mqtt_port, mqtt_id);
    
    return 0;
}


/* This is the topic we listen to for power values. */
/* ptopic <power-topic> */

#define default_TOPIC_POWER		"power/topic"

char* topic_power		= 0;

int
set_ptopic( int argc, const char** argv)
{
    if ( argc != 2 )
	return -1;
    
    if (topic_power)
	free( topic_power );
    topic_power = strdup( argv[1] );
    if ( !topic_power || !*topic_power )
	return -1;

    if ( opt_v )
	printf("ptopic %s\n", topic_power);
    
    return 0;
}


/* This is the topic that the shelly plug uses */
/* stopic <shelly-topic> */

#define default_SHELLY_TOPIC		"shellies/shellyplug-s-012345"

char* topic_shelly		= 0;

int
set_stopic( int argc, const char** argv)
{
    if ( argc != 2 )
	return -1;
    
    if (topic_shelly)
	free( topic_shelly );
    topic_shelly = strdup( argv[1] );
    if ( !topic_shelly || !*topic_shelly )
	return -1;

    if ( opt_v )
	printf("stopic %s\n", topic_shelly);
    
    return 0;
}


/* Time out.  If mqtt power values do not arrive in this time  */
/* the the shelly is turned off */
/* timeout <seconds> */

unsigned long timeout	= 120;

int
set_timeout( int argc, const char** argv )
{
    char* s = 0;
    
    if ( argc != 2 )
	return -1;

    CFGF_UL(1,timeout);

    printf("pon: %lu\n", timeout );

    return 0;
}



/* If the power is above on_power for more than on_count times,  */
/* we turn on the poer. */
/* pon <on-power> <on-count> */

unsigned long on_P	= 750;
unsigned long on_N	= 50;


int
set_pon( int argc, const char** argv )
{
    char* s = 0;
    
    if ( argc != 3 )
	return -1;

    CFGF_UL(1,on_P);
    CFGF_UL(2,on_N);

    printf("pon: %lu %lu\n", on_P, on_N );

    return 0;
}


/* If the power is less than off_power for off_count times, */
/* the power is turned off. */
/* poff <off-power> <off-count> */

unsigned long off_P	= 100;
unsigned long off_N	= 20;


int
set_poff( int argc, const char** argv )
{
    char* s = 0;
    
    if ( argc != 3 )
	return -1;

    CFGF_UL(1,off_P);
    CFGF_UL(2,off_N);


    printf("pon: %lu %lu\n", off_P, off_N );

    return 0;
}



cfgf_tagtab_t tagtab[] = {
			  {"mqtt",		3, set_mqtt },
			  {"ptopic",		1, set_ptopic },
			  {"stopic",		1, set_stopic },
			  {"timeout",		1, set_timeout},
			  {"pon",		2, set_pon },
			  {"poff",		2, set_poff },
			  {0,0,0}
};


/*****************************************************************************/
/* Misc support */

void
my_gettime(struct timespec* ts)
	/* Get current time or bomb */
{
    int i;
    i = clock_gettime(CLOCK_REALTIME, ts);
    if ( i ) {
	perror("clock_gettime: ");
	exit( EXIT_FAILURE );
    }
}

void
add_time_sec(struct timespec* t, const struct timespec* now, unsigned dt)
{
    t->tv_sec  = now->tv_sec + dt;
    t->tv_nsec = now->tv_nsec;
}

void
my_sleep( const struct timespec* ts )
{
    int i;
    i = clock_nanosleep( CLOCK_REALTIME, TIMER_ABSTIME, ts, 0);
    if ( i ) {
	perror("clock_nanosleep: ");
	exit( EXIT_FAILURE );
    }
}

int
is_past_time( struct timespec* reset_ts, unsigned int dt )
{
    struct timespec now_ts;
    my_gettime( &now_ts );
    /* Have we passed the reset time yet? */
    if ( now_ts.tv_sec < reset_ts->tv_sec ) return 0; /* Nope */

    /* Yes, set the next reset time */
    add_time_sec( reset_ts, &now_ts, dt );
    return 1;
}

/*****************************************************************************/
/* sctrl loop */

/* Global mosquitto handle. */
struct mosquitto* mqc = 0;

/* Protected Globals */
pthread_mutex_t mtx;
pthread_cond_t	cv;

/* These are seet by the mqtt thread */
int		mqtt_P_val;		/* most receent power value */
unsigned long	mqtt_P_ctr;		/* updated as heart-beat */


/* State(s) */
#define SCTRL_STATE_OFF (0)
#define SCTRL_STATE_ON	(1)
unsigned sctrl_state = SCTRL_STATE_OFF;



void
sctrl_init()
	/* Init the state. */
{

    pthread_mutex_init( &mtx, 0 );
    pthread_cond_init( &cv, 0 );

    sctrl_state = SCTRL_STATE_OFF;
    mqtt_P_val = 0;
    mqtt_P_ctr = 0;
}


void

sctrl_publish_state()
{
    int status;
    const char val_ON[]  = "on";
    const char val_OFF[] = "off";
    const char* val = ((sctrl_state == SCTRL_STATE_ON) ? val_ON : val_OFF );
    int lval = strlen(val);
    
    if ( opt_v ) {
	printf("Publish state: %s = \"%s\" %d\n", topic_shelly, val, lval);
    }
    if ( opt_n ) {
	status = MOSQ_ERR_SUCCESS;
    }
    else {
	status = mosquitto_publish(mqc, 0,
				   topic_shelly, 
				   lval,
				   val,
				   0,
				   false ); /* retain is off */
    }
    if ( status != MOSQ_ERR_SUCCESS ) {
	printf("mosquitto_publish FAILED: %d\n",status);

	sleep(2);
	abort();
    }

}


void
sctrl_set_state(unsigned state)
{
    if ( state !=sctrl_state ) {
	printf("Setting state: %u\n", sctrl_state);
	sctrl_state = state;

	sctrl_publish_state();
    }
    else if ( state == SCTRL_STATE_OFF ) {
	sctrl_publish_state();
    }
}





/* ========================================================================== */







void
sctrl_handle_POWER( int P )
/* mqtt thread ONLY! */
/* .. set mqtt_P_val and _ctr and signal cond var. */
{
    if ( 0 && opt_v )
	printf("Power: P=%d\n", P);

    pthread_mutex_lock( &mtx );
    do {
	mqtt_P_val = P;
	++mqtt_P_ctr;
	pthread_cond_signal( &cv );
    } while(0);
    pthread_mutex_unlock( &mtx );

}




void
sctrl_timeout()
{
    /* go to state off */
    sctrl_set_state( SCTRL_STATE_OFF );
}



void
sctrl_loop()
{
    /*  We ignore checking the mqtt_P_ctr for now...  */

    unsigned long on_ctr = 0;
    unsigned long off_ctr = 0;

    printf("Starting loop.\n");

    /* Loop forever. */
    for(;;) {

	pthread_mutex_lock( &mtx );
	do {
	    int i;
	    struct timespec tnow;
	    struct timespec ts;
	    unsigned dt = timeout;

	    my_gettime( &tnow );
	    add_time_sec( &ts, &tnow, dt );

	    if ( opt_v )
		printf("Sleeping: %u\n", dt);

	    /* Loop to allow continues sleep */
	    for(;;) {
		/* Wait for input at most dt s */
		i = pthread_cond_timedwait( &cv, &mtx, &ts );
		if ( i == EINTR ) {
		    /* Interrupt -- ignore, but contine to sleep. */
		    /* do nothing -- keep in sleep loop */
		}
	    
		else if ( i == ETIMEDOUT ) {
		    /* Timeout -- update run and send that */
		    if ( opt_v )
			printf("Timeout!\n");
		    on_ctr = 0;
		    off_ctr = 0;
		    sctrl_timeout();

		    /* Break sleep loop */
		    break;
		}

		else {
		    /* The cond var was signalled. */
		    int P_val = mqtt_P_val;

		    if ( opt_v ) {
			printf("P : %d (%ld,%ld) %lu %lu\n",
			       P_val,on_P,off_P,
			       on_ctr,off_ctr);
		    }
		    
		    if ( P_val > (int)on_P ) {
			++on_ctr;
		    }
		    else {
			on_ctr = 0;
		    }
		    if ( P_val > (int)off_P ) {
			++off_ctr;
		    }
		    else {
			off_ctr = 0;
		    }

		    if ( on_ctr >= on_N ) {
			sctrl_set_state(SCTRL_STATE_ON);
			off_ctr = 0;
		    }

		    if ( off_ctr > off_N ) {
			sctrl_set_state(SCTRL_STATE_OFF);
			on_ctr = 0;
		    }


		    break;
		    /* NOT REACHED! */
		}
		
	    }				/* END sleep loop */

	} while(0); 			/* END mutex loop */
	pthread_mutex_unlock( &mtx );

    } /* Loop forever */

    /* NOT REACHED */
}



/*****************************************************************************/
/* Mosquitto handling */



/* Global mosquitto handle. */
/*
Defined above.
struct mosquitto* mqc = 0;
*/

#define MAX_TOPIC_LEN		(80)
char topic_val[MAX_TOPIC_LEN];

/* This is called by the mqtt thread! */
void
mq_message_callback(struct mosquitto *mqc, void *obj,
		    const struct mosquitto_message *msg)
{
    
    const char*    topic = msg->topic;
    const char*    pload = msg->payload;

    if ( opt_v )
	printf("mqtt data: %s = %s\n", topic, pload);


    if ( !strcmp(topic, topic_power) ) {
	double P_dbl;
	int P_int;
	char* end_ptr = 0;
	errno = 0;
	P_dbl = strtod( pload, &end_ptr );
	if ( !errno && *end_ptr != '\0' ) {
	    printf("bad payload, %s=%s\n",topic,pload);
	    return;
	}

	P_int = -1000 * P_dbl;
	sctrl_handle_POWER( P_int );
    }

    else {
	static int bad_count = 25;
	if ( bad_count ) {
	    printf("Ignored topic(%d): %s\n",bad_count,topic);
	    --bad_count;
	}
    }

    
}

#if 0
/* UNUSED! */
void
mq_publish()
{
    int i;
    for ( i = 0; tab[i].addr; ++i ) {
	size_t l;
	int status;
	l = strnlen( topic_val[i], MAX_TOPIC_LEN );

	if ( opt_v ) {
	    printf(" %s %s\n", tab[i].topic, topic_val[i] );
	}

	if ( !opt_n && i && (l >0) ) {
	    status = mosquitto_publish(mqc, 0,
				       tab[i].topic,
				       l,
				       topic_val[i], 1, true );
	    if ( status != MOSQ_ERR_SUCCESS) {
		perror("mosquitto_publish: ");
		exit( EXIT_FAILURE );
	    }

	    status = mosquitto_loop_write( mqc, 1 );
	}
    }
}
#endif


void
mq_connect_callback(struct mosquitto *mqc, void *obj, int result)
{
    printf("MQ Connected: %d\n", result);
    if ( result != 0 ) {
	/* Something is wrong.  Wait before retry */
	sleep(5);
    }
}


void
mq_disconnect_callback(struct mosquitto *mqc, void *obj, int result)
{
    printf("MQ Disonnected: %d\n", result);
}



void
mq_init()
	/* Initialise the mosquitto lib */
{
    int i;
    if ( opt_v )
	printf("mq_init()\n");
    i = mosquitto_lib_init();
    if ( i != MOSQ_ERR_SUCCESS) {
	perror("mosquitto_lib_init: ");
	exit( EXIT_FAILURE );
    }
    
    mqc = mosquitto_new(mqtt_id, true, 0);
    if ( !mqc ) {
	perror("mosquitto_new: ");
	exit( EXIT_FAILURE );
    }

    mosquitto_connect_callback_set(mqc, mq_connect_callback);
    mosquitto_disconnect_callback_set(mqc, mq_disconnect_callback);
    mosquitto_message_callback_set(mqc, mq_message_callback);

    i = mosquitto_connect(mqc, mqtt_broker, mqtt_port, 60);
    if ( i != MOSQ_ERR_SUCCESS) {
	perror("mosquitto_connect: ");
	exit( EXIT_FAILURE );
    }


}


void
mq_sub(const char* s, const char* t)
{
    if ( !t || !*t ) {
	printf("topic %s missing!\n",s);
	return;
    }
    
    mosquitto_subscribe(mqc, NULL, t, 0);
}

void
mq_subscribe()
	/* Subscribe to topics */
{
    mq_sub("power",   topic_power );
}

void
mq_fini()
{
    int i;
    if ( opt_v )
	printf("mq_fini()\n");

    if ( mqc ) {
	mosquitto_destroy(mqc);
	mqc = 0;
    }

    i = mosquitto_lib_cleanup();
    if ( i != MOSQ_ERR_SUCCESS) {
	perror("mosquitto_lib_cleanup: ");
	exit( EXIT_FAILURE );
    }
}




/*****************************************************************************/

void
print_usage()
{
    fprintf(stderr,"Usage: sctrl [-v] [-n] config-file\n");
    exit( EXIT_FAILURE );
}


int
main( int argc, const char** argv )
{
    int i;

    setbuf( stdout, 0 );		/* No buffering */

    chdir("/");

    /* Set default values for the MQTT server. */
    mqtt_id = strdup( default_MQTT_ID );
    mqtt_broker = strdup( default_MQTT_BROKER );
    /* set when declared: mqtt_port = MQTT_PORT; */

    /* Set default topics. */
    topic_power		= strdup( default_TOPIC_POWER );
    topic_shelly	= strdup( default_SHELLY_TOPIC );


    sctrl_init();

    
    --argc; ++argv;			/* Jump over first arg */
    while( argc && *argv && **argv ) {
	if ( !strcmp("-v", *argv) ) {
	    opt_v = 1;
	    if ( opt_v )
		printf("Verbose mode.\n");
	}
	else if ( !strcmp("-n", *argv) ) {
	    opt_n = 1;
	    if ( opt_v )
		printf("No-Active mode.\n");
	}
	else if ( **argv == '-' ) {
	    printf("Illegal argument.");
	    print_usage();
	}
	else if ( argc != 1 ) {
	    printf("Illegal argument.");
	    print_usage();
	    break;
	}
	else {
	    /* Read config file */
	    int status = cfgf_read_file( *argv, tagtab );
	    if ( status ) {
		fprintf(stderr,"Errors in config file.\n");
		exit( EXIT_FAILURE );
	    }
	}
	--argc; ++argv;
    }

    printf("Starting.\n");
	 
    mq_init();
    mq_subscribe();

    
    /* Run the network loop in a background thread, call returns quickly. */
    if ( opt_v )
	printf("Launching mosquitto loop.\n");
    i = mosquitto_loop_start(mqc);
    if(i != MOSQ_ERR_SUCCESS){
	mosquitto_destroy(mqc);
	fprintf(stderr, "Error: %s\n", mosquitto_strerror(i));
	return EXIT_FAILURE;
    }

    sctrl_loop();
    
    mq_fini();

        
    printf("Ending.\n");

    /* Should not come here */
    return EXIT_FAILURE;
}
