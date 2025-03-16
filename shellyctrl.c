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
 * Last Modified On: Sun Mar 16 10:18:33 2025
 * Update Count    : 105
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


/* Move this into cfgf.h  */
int
cfgf_set_s(int argc, const char** argv, char** s)
{
    if ( argc != 2 )
	return -1;
    
    if (*s)
	free( *s );
    *s = strdup( argv[1] );
    if ( !*s || !*(*s) )
	return -1;

    return 0;
}

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
/* power-topic <power-topic> */

#define default_POWER_TOPIC		"power/topic"

char* power_topic		= 0;

int
set_power_topic( int argc, const char** argv)
{
    int i = cfgf_set_s(argc, argv, &power_topic);
    if ( i )
	return i;
    if ( opt_v )
	printf("power-topic %s\n", power_topic);    
    return 0;
}


/* This is the topic we listen to for mode. */
/* mode-topic <mode-topic> */

#define default_MODE_TOPIC		"shelly/012345/mode"

char* mode_topic		= 0;

int
set_mode_topic( int argc, const char** argv)
{
    int i = cfgf_set_s(argc, argv, &mode_topic);
    if ( i )
	return i;
    if ( opt_v )
	printf("mode-topic %s\n", mode_topic);    
    return 0;
}


/* This is the topic that the shelly plug uses for input command */
/* cmd-topic <shelly-topic> */

#define default_CMD_TOPIC	"shellies/shellyplug-s-012345/relay/0/command"

char* cmd_topic		= 0;

int
set_cmd_topic( int argc, const char** argv)
{
    int i = cfgf_set_s(argc, argv, &cmd_topic);
    if ( i )
	return i;

    if ( opt_v )
	printf("cmd-topic %s\n", cmd_topic);
    
    return 0;
}


/* This is the topic that the shelly plug uses to annouce its state */
/* state-topic <shelly-topic> */

#define default_STATE_TOPIC	"shellies/shellyplug-s-012345/relay/0"

char* state_topic	= 0;

int
set_state_topic( int argc, const char** argv)
{
    int i = cfgf_set_s(argc, argv, &state_topic);
    if ( i )
	return i;

    if ( opt_v )
	printf("state-topic %s\n", state_topic);
    
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

    printf("timeout: %lu\n", timeout );

    return 0;
}



/* If the power is above on_power for more than on_count times,  */
/* we turn on the poer. */
/* POn <on-power> <on-count> */

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

    printf("POn: %lu %lu\n", on_P, on_N );

    return 0;
}


/* If the power is less than off_power for off_count times, */
/* the power is turned off. */
/* POff <off-power> <off-count> */

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


    printf("POff: %lu %lu\n", off_P, off_N );

    return 0;
}



cfgf_tagtab_t tagtab[] = {
			  {"mqtt",		3, set_mqtt },
			  {"power-topic",	1, set_power_topic },
			  {"mode-topic",	1, set_mode_topic },
			  {"cmd-topic",		1, set_cmd_topic },
			  {"state-topic",	1, set_state_topic },
			  {"timeout",		1, set_timeout},
			  {"POn",		2, set_pon },
			  {"POff",		2, set_poff },
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

#if 0
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
#endif

#if 0
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
#endif



/*****************************************************************************/
/* sctrl loop */

/* Global mosquitto handle. */
struct mosquitto* mqc = 0;

/* States, if the shelly is turned on or off */
#define STATE_OFF	(0)
#define STATE_ON	(1)
#define STATE_TIMEOUT	(2)
int sctrl_state = STATE_OFF;


/* Protected Globals */
pthread_mutex_t mtx;
pthread_cond_t	cv;

/* These are seet by the mqtt thread */
int sctrl_P = 0;			/* most receent power value from mqtt */
int shelly_state = STATE_OFF;		/* most recent shelly state */

#define MODE_OFF	(0)
#define MODE_ON		(1)
#define MODE_POWER	(10)
int sctrl_mode = MODE_OFF;




void
sctrl_init()
	/* Init the state. */
{

    pthread_mutex_init( &mtx, 0 );
    pthread_cond_init( &cv, 0 );

    sctrl_state = STATE_OFF;
    sctrl_P = 0;
    sctrl_mode = MODE_OFF;
}



void
sctrl_publish_state()
{
    int status;
    const char val_ON[]  = "on";
    const char val_OFF[] = "off";
    const char* val = ((sctrl_state == STATE_ON) ? val_ON : val_OFF );
    int lval = strlen(val);
    
    if ( opt_v ) {
	printf("Publish state: %s = \"%s\" %d\n", cmd_topic, val, lval);
    }
    if ( opt_n ) {
	status = MOSQ_ERR_SUCCESS;
    }
    else {
	status = mosquitto_publish(mqc, 0,
				   cmd_topic, 
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





/* ========================================================================== */


void
mqt_set_data( int power, int state, int mode )
/* mqtt thread ONLY! */
/* .. set the global variables and signal */
{
    if ( opt_v )
	printf("Data: P=%d state=%d mode=%d\n", power,state,mode);

    pthread_mutex_lock( &mtx );
    do {
	sctrl_P = power;
	shelly_state = state;
	sctrl_mode = mode;
	pthread_cond_signal( &cv );
    } while(0);
    pthread_mutex_unlock( &mtx );

}





static unsigned max_timeout_ctr = 3;

void
sctrl_loop()
	/* Loop for the main thread. */
{
    unsigned timeout_ctr = 0;
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

	    /* Loop to allow continued sleep */
	    for(;;) {
		/* Wait for input at most dt s */
		i = pthread_cond_timedwait( &cv, &mtx, &ts );
		if ( i == EINTR ) {
		    /* Interrupt -- ignore, but contine to sleep. */
		    /* .. just continue to enter the _timedwait again. */
		    continue;
		}
	    
		else if ( i == ETIMEDOUT ) {
		    /* Timeout --  */
		    ++timeout_ctr;
		    sctrl_state = STATE_OFF;
		    on_ctr = 0;
		    off_ctr = 0;

		    printf("Timeout! timeout_ctr = %d\n",timeout_ctr);
		    sctrl_publish_state();

		    if ( timeout_ctr > max_timeout_ctr ) {
			/* Abort execution, send off to shelly 2 more times */
			printf("Too many Timeouts.  Exiting!\n");
			sleep( 2 );
			sctrl_publish_state();
			sleep( 2 );
			sctrl_publish_state();
			sleep( 2 );
			exit( EXIT_FAILURE );
		    }
		    /* Break sleep loop */
		    break;
		}

		else {
		    /* The condvar was signalled. */
		    int P_val = sctrl_P;
		    int new_state = sctrl_state;
		    
		    timeout_ctr = 0;	/* Clear timeout counter. */

		    if ( opt_v ) {
			printf("P : %d (%ld,%ld) on_ctr=%lu "
			       "off_ctr=%lu, shelly_state=%d, sctrl_mode=%d\n",
			       P_val,on_P,off_P,
			       on_ctr,off_ctr,shelly_state,sctrl_mode);
		    }

		    /* Update counters. */
		    if ( P_val > (int)on_P ) {
			++on_ctr;
		    }
		    else {
			on_ctr = 0;
		    }
		    if ( P_val < (int)off_P ) {
			++off_ctr;
		    }
		    else {
			off_ctr = 0;
		    }

		    /* Figure out next state */
		    if ( sctrl_mode == MODE_OFF ) {
			new_state = STATE_OFF;
			on_ctr = 0;
			off_ctr = 0;
		    }
		    else if ( sctrl_mode == MODE_ON ) {
			new_state = STATE_ON;
			on_ctr = 0;
			off_ctr = 0;
		    }
		    else if ( sctrl_mode == MODE_POWER ) {
			if ( off_ctr >= off_N ) {
			    new_state = STATE_OFF;
			    on_ctr = 0;
			}
			else if ( on_ctr >= on_N ) {
			    new_state = STATE_ON;
			    off_ctr = 0;
			}
			else {
			    new_state = sctrl_state;
			}
		    }

		    if ( new_state != sctrl_state ) {
			printf("New state: %d --> %d (shelly=%d)\n",
			       sctrl_state,new_state,shelly_state);
			sctrl_state = new_state;
			sctrl_publish_state();
		    }
		    else if ( new_state != shelly_state ) {
			printf("Nudge shelly: %d --> %d\n",
			       shelly_state,new_state);
			sctrl_state = new_state;
			sctrl_publish_state();
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


/* This is called by the mqtt thread, and is where the main action feeding
 * the main loop with information happens.
 */
void
mq_message_callback(struct mosquitto *mqc, void *obj,
		    const struct mosquitto_message *msg)
{
    
    const char*	topic = msg->topic;
    const char*	pload = msg->payload;

    static int state = STATE_OFF;
    static int mode = MODE_OFF;
    
    if ( opt_v )
	printf("mqtt data: %s = %s\n", topic, pload);


    /* POWER ------------------------------------------------------- */
    if ( !strcmp(topic, power_topic) ) {
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
	mqt_set_data( P_int, state, mode );
    }

    /* Shelly State ------------------------------------------------ */
    else if ( !strcmp(topic, state_topic) ) {
	if ( !strcmp("on",pload ) ) {
	    state = 1;
	}
	else if ( !strcmp("off",pload) ) {
	    state = 0;
	}
	else {
	    /* Bad state, keep old value. */
	    printf(".. Bad state: \"%s\"\b",pload);
	}
    }

    /* MODE -------------------------------------------------------- */
    else if ( !strcmp(topic, mode_topic) ) {
	if ( !strcmp("0",pload) ) {
	    mode = 0;
	}
	else if ( !strcmp("1",pload) ) {
	    mode = 1;
	}
	else if ( !strcmp("10",pload) ) {
	    mode = 10;
	}
	else {
	    /* Bad mode, keep old value. */
	    printf(".. Bad mode: \"%s\"\b",pload);
	}
    }

    /* Unrecognised topic ----------------------------------------- */
    else {
	/* Print the 25 1'st, then be silent. */
	static int bad_count = 25;
	if ( bad_count ) {
	    printf("Ignored topic(%d): %s\n",bad_count,topic);
	    --bad_count;
	}
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
    mq_sub("power-topic",	power_topic );
    mq_sub("mode-topic",	mode_topic );
    mq_sub("state-topic",	state_topic );
}

void
mq_connect_callback(struct mosquitto *mqc, void *obj, int result)
{
    printf("MQ Connected: %d\n", result);
    if ( result != 0 ) {
	/* Something is wrong.  Wait before retry */
	sleep(5);
    }
    mq_subscribe();
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

    /* Set default values for the MQTT server. */
    mqtt_id = strdup( default_MQTT_ID );
    mqtt_broker = strdup( default_MQTT_BROKER );
    /* set when declared: mqtt_port = MQTT_PORT; */

    /* Set default topics. */
    power_topic		= strdup( default_POWER_TOPIC );
    cmd_topic		= strdup( default_CMD_TOPIC );
    state_topic		= strdup( default_STATE_TOPIC );
    mode_topic		= strdup( default_MODE_TOPIC );


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

    chdir("/");				/* Prevent fs hogging. */

    printf("Starting.\n");
	 
    mq_init();
    /* Done in connect callback! mq_subscribe(); */

    
    /* Run the network loop in a background thread, call returns quickly. */
    if ( opt_v )
	printf("Launching mosquitto loop.\n");
    i = mosquitto_loop_start(mqc);
    if(i != MOSQ_ERR_SUCCESS){
	mosquitto_destroy(mqc);
	fprintf(stderr, "Error: %s\n", mosquitto_strerror(i));
	return EXIT_FAILURE;
    }


    sleep(1);
    
    sctrl_loop();
    
    mq_fini();

        
    printf("Ending.\n");

    /* Should not come here */
    return EXIT_FAILURE;
}
