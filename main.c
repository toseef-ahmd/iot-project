#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <random.h>
#include <time.h>

#include "thread.h"
#include "ztimer.h"
#include "shell.h"
#include "msg.h"
#include "net/emcute.h"
#include "net/ipv6/addr.h"


#include "mutex.h"


/* Add lps331ap related include here */
#include "lpsxxx.h"
#include "lpsxxx_params.h"


/* Add lsm303dlhc related include here */
#include "lsm303dlhc.h"
#include "lsm303dlhc_params.h"

#include "xtimer.h"


#define DELAY (55000LU * US_PER_MS)

int16_t temp = 0;
uint16_t pres = 0;

//DEFINE - EMCUTE
#ifndef EMCUTE_ID
#define EMCUTE_ID ("station0")
#endif

#ifndef EMCUTE_DEFAULT_PORT
#define EMCUTE_DEFAULT_PORT (1885)
#endif

#define EMCUTE_PRIO (THREAD_PRIORITY_MAIN - 1)

#define NUMOFSUBS (1U)
#define TOPIC_MAXLEN (128U)

#define STATUS_LEN 5

#define MAIN_QUEUE_SIZE (8)

static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

static char status_MQTT[STATUS_LEN] = "";

typedef struct sensors{
    int temp;
    int press;
} t_sensors;

static char stack[THREAD_STACKSIZE_DEFAULT];

char stack_loop[THREAD_STACKSIZE_MAIN];
char stack_measure[THREAD_STACKSIZE_MAIN];

kernel_pid_t tmain, tmes;

static emcute_sub_t subscriptions[NUMOFSUBS];
static char topics[NUMOFSUBS][TOPIC_MAXLEN];

/* Declare the lps331ap device variable here */
static lpsxxx_t lpsxxx;


/* Declare the lsm303dlhc device variable here */
static lsm303dlhc_t lsm303dlhc;


static int pub(char* topic, const char* data, int qos){
    emcute_topic_t t;
    unsigned flags = EMCUTE_QOS_0;

    switch(qos){
        case 1:
            flags |= EMCUTE_QOS_1;
            break;
        case 2:
            flags |= EMCUTE_QOS_2;
            break;
        default:
            flags |= EMCUTE_QOS_0;
            break;
    }

    t.name = MQTT_TOPIC_OUT;
    if(emcute_reg(&t) != EMCUTE_OK){
        puts("PUB ERROR: Unable to obtain Topic ID");
        return 1;
    }
    if(emcute_pub(&t, data, strlen(data), flags) != EMCUTE_OK){
        printf("PUB ERROR: unable to publish data to topic '%s [%i]'\n", t.name, (int)t.id);
        return 1;
    }

    printf("Suc: Published %s on topic %s\n", data, topic);
    return 0;
}

//This function create a stringified json collecting data from the sensors
const char* data_parse(t_sensors* sensors){
    static char json[128];

    sprintf(json, "{\"1. temperature\": \"%i.%u\", \"2. Pressure\": \"%d\"}",
                   ((sensors->temp)/100), ((sensors->temp)%100), sensors->press);
    
    return json;
}

void *main_loop(void *arg){
    (void)arg;
    tmain = thread_getpid();
    t_sensors sensors;

    xtimer_ticks32_t last= xtimer_now();
    while(1) {
        // srand(time(NULL));
        lpsxxx_read_temp(&lpsxxx, &temp);
        lpsxxx_read_pres(&lpsxxx, &pres);

        sensors.temp = temp;
        sensors.press = pres;

        
        printf("Pressure: %uhPa\n", pres);

        
        printf("Temperature: %i.%u°C\n", (temp / 100), (temp % 100));

        printf("Publishing data to MQTT Broker\n");
        pub(MQTT_TOPIC_OUT, data_parse(&sensors), 0);
        //printf("json %s",data_parse(&sensors));
        xtimer_sleep(2);

        printf("\n");

        xtimer_periodic_wakeup(&last, DELAY);
        
    }
}

static void *emcute_thread(void *arg)
{
    (void)arg;
    emcute_run(EMCUTE_DEFAULT_PORT, EMCUTE_ID);
    return NULL;    /* should never be reached */
}

static void on_pub(const emcute_topic_t *topic, void *data, size_t len){
    char *in = (char *)data;

    char comm[len+1];

    printf("### got publication for topic '%s' [%i] ###\n",
           topic->name, (int)topic->id);
    for (size_t i = 0; i < len; i++) {
        //printf("%c", in[i]);
        comm[i] = in[i];
    }
    comm[len] = '\0';
    printf("%s", comm);

    //function for cleaning "status_MQTT" array buffer string
    for(size_t i = 0; i < STATUS_LEN; i++){
        if(i < len){
            status_MQTT[i] = in[i];
        } else {
            status_MQTT[i] = 0;
        }
    }

    puts("\n");
}

int setup_mqtt(void)
{
    /* initialize our subscription buffers */
    memset(subscriptions, 0, (NUMOFSUBS * sizeof(emcute_sub_t)));

    xtimer_sleep(10);

    printf("Dev_id: %s\n",EMCUTE_ID);

    /* start the emcute thread */
    thread_create(stack, sizeof(stack), EMCUTE_PRIO, 0, emcute_thread, NULL, "emcute");

    sock_udp_ep_t gw = {
        .family = AF_INET6,
        .port = SERVER_PORT
    };

    char *message = "connected";
    size_t len = strlen(message);

    /* parse address */
    if (ipv6_addr_from_str((ipv6_addr_t *)&gw.addr.ipv6, SERVER_ADDR) == NULL) {
        printf("error parsing IPv6 address\n");
        return 1;
    }

    if (emcute_con(&gw, true, MQTT_TOPIC_OUT, message, len, 0) != EMCUTE_OK) {
        printf("error: unable to connect to [%s]:%i\n", SERVER_ADDR, (int)gw.port);
        return 1;
    }

    printf("Connected on [%s]:%i\n", SERVER_ADDR, (int)gw.port);

    /* setup subscription to topic*/
    
    unsigned flags = EMCUTE_QOS_0;
    subscriptions[0].cb = on_pub;
    strcpy(topics[0], MQTT_TOPIC_IN);
    subscriptions[0].topic.name = MQTT_TOPIC_IN;

    if (emcute_sub(&subscriptions[0], flags) != EMCUTE_OK) {
        printf("error: unable to subscribe to %s\n", MQTT_TOPIC_IN);
        return 1;
    }

    printf("Now subscribed to %s\n", MQTT_TOPIC_IN);

    return 1;
}

static int cmd_status(int argc, char *argv[]){
    (void)argc;
    (void)argv;

    printf("WELCOME!!!\n");
    return 0;
}

static int cmd_mqtt_status(int argc, char *argv[]){
    (void)argc;
    (void)argv;

    printf("Network Information: %s \nPORT: %d \nTOPIC_IN: %s/%s TOPIC_OUT: %s\n", SERVER_ADDR, SERVER_PORT,MQTT_TOPIC_IN,EMCUTE_ID,MQTT_TOPIC_OUT);
    return 0;
}

static const shell_command_t commands[] = {
    { "status", "get a status report", cmd_status },
    { "mqtt_status", "mqtt status report", cmd_mqtt_status},
    { NULL, NULL, NULL }
};

int main(void)
{
    msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);

    /* Initialize the lps331ap sensor here */
    lpsxxx_init(&lpsxxx, &lpsxxx_params[0]);


    /* Initialize the lsm303dlhc sensor here */
    lsm303dlhc_init(&lsm303dlhc, &lsm303dlhc_params[0]);

    setup_mqtt();
    xtimer_sleep(2);


    // thread_create(lsm303dlhc_stack, sizeof(lsm303dlhc_stack), THREAD_PRIORITY_MAIN - 1,
    //               0, lsm303dlhc_thread, NULL, "lsm303dlhc");


    puts("[###STARTING MEASUREMENTS###]\n\n");
    thread_create(stack_loop, sizeof(stack_loop), EMCUTE_PRIO, 0, main_loop, NULL, "main_loop");
    puts("Thread Initiated!");

   

    /* Everything is ready, let's start the shell now */
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}