#define _GNU_SOURCE
#include <assert.h>
#include <sched.h> /* getcpu */
#include <stdio.h> 
#include <stdlib.h> 
#include <sys/types.h> 
#include <unistd.h>   
#include <pthread.h> 
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <x86intrin.h>
#pragma intrinsic(__rdtsc)


//My defines
#define PRIV_CORES_MAX          32   //24 for workstation
#define SAMPLES_MAX             1024
#define TX_DELAY                9000

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif
  
void *th_funcRx(void *p_arg);
void *th_funcTx(void *p_arg);




typedef struct msg_s {
    unsigned long long int uid __attribute__ ((aligned(CACHELINE_SIZE)));
    int data[1];
} msg_t ;

msg_t g_msgs;       //global cache aligned message areas


typedef struct context_s {
    msg_t *p_msg  __attribute__ ((aligned(CACHELINE_SIZE)));                       //location to recieve a message
    pthread_t   thread_id;              //
    int id;
    int tpid;                           //os thread process id
    int setaffinity;                    //if set core to bind to
    char *name;
    volatile int ready;
    volatile int done;
    //results

    int statsIndex;
    unsigned long long int uidSamples[SAMPLES_MAX][2];

} context_t;
context_t   contexts[PRIV_CORES_MAX + 1];

//notes
//context 0 main
//context 1 tx threadTx
//context 2 rx through (2 + i_threadsRx)

int g_ncores = 24;          //Todo learn dynamically


//test config, avoids commandline args
int i_threadsRx = 4;                //commandline -t x overrides
int i_threadstx = 1;                //fixed


void usage(){
    printf("-h     help\n-t     rx thread count 1-23\n-s        tx core afinity\n-c      rx cores x,y,z[,a,b,...]\n");
}

int scatter[SAMPLES_MAX];

int main(int argc, char **argv){	
    int opt;
    int i,j,k,l;
    context_t *this = &contexts[0];
    cpu_set_t my_set;        /* Define your cpu_set bit mask. */
    char cwork[64];
    char work[64];
    unsigned long long int first, secnd, work0, work1;
    context_t *p_cont;

    this->name = "Main";
    this->tpid = gettid();
    printf("%s PID %d %d\n", this->name, this->tpid, gettid());

    //init context objects
    for (i = 0; i < PRIV_CORES_MAX; i++) {
        contexts[i].id = i;
        contexts[i].setaffinity = -1;
        contexts[i].p_msg = &g_msgs;
        contexts[i].ready = 0;
        contexts[i].done = 0;
    }

    //defaults
    contexts[0].setaffinity = 0;    //main
    contexts[1].setaffinity = 1;    //tx
    contexts[2].setaffinity = 2;    //rx_1
    contexts[3].setaffinity = 3;    //rx_2
    contexts[4].setaffinity = 4;    //rx_3
    contexts[5].setaffinity = 5;    //rx_4

    while((opt = getopt(argc, argv, "ht:m:s:c:")) != -1) 
    { 
        switch(opt) 
        { 
        case 'h': 
            usage();
            return 0;
            break;

        case 't':  
                i = atoi(optarg);
                if (i < 1 && i > (PRIV_CORES_MAX - 2)) {
                    printf("t range 1 - %d error %d\n", (PRIV_CORES_MAX - 2), i);
                    return 1;
                }
                i_threadsRx = i;
                printf("threadsRx: %s\n", optarg); 
                break; 

        case 'm':  
                //Todo add duplicate check
                i = atoi(optarg);
                if (i > PRIV_CORES_MAX) {
                    printf("s range 0 - %d error %d\n", (PRIV_CORES_MAX), i);
                    return 1;
                }
                printf("pin TxThread 0 to %s\n", optarg);
                contexts[0].setaffinity = i;
                break;

        case 's':  
                //Todo add duplicate check
                i = atoi(optarg);
                if (i > PRIV_CORES_MAX) {
                    printf("s range 0 - %d error %d\n", (PRIV_CORES_MAX), i);
                    return 1;
                }
                printf("pin TxThread 1 to %s\n", optarg);
                contexts[1].setaffinity = i;
                break;

        case 'c':  
                //Todo add duplicate check
                printf("coresRx: %s \n", optarg); 
                //coma seperated list 
                strcpy(cwork, optarg);
                i = 0;
                j = 0;
                k = 0;
                while (cwork[i] != 0) {
                    if (cwork[i] == ' ') {
                        i++;
                        continue;
                    }
                    while ((cwork[i] >= '0') && (cwork[i] <= '9')) {
                        work[j] = cwork[i];
                        work[j+1] = '\0';
                        i++;
                        j++;
                    }
                    if(cwork[i] == '\0'){
                        l = atoi(work);
                        if (l > PRIV_CORES_MAX) {
                            printf("s range 0 - %d error %d\n", (PRIV_CORES_MAX), l);
                            return 1;
                        }
                        printf("pin RxThread %d to %s\n", k, work);
                        contexts[k + 2].setaffinity = l;
                    }
                    if (cwork[i] == ',') {
                        l = atoi(work);
                        if (l > PRIV_CORES_MAX) {
                            printf("s range 0 - %d error %d\n", (PRIV_CORES_MAX), l);
                            return 1;
                        }
                        printf("pin RxThread %d to %s\n", k, work);
                        contexts[k + 2].setaffinity = l;
                        k++;
                        j = 0;
                        i++;
                    }
                }
                break; 

  
        default:
            usage();
            return 0;
                break;

        } 
    } 
    printf("\n");

    //set main thread afinity
    CPU_ZERO(&my_set); 
     if (this->setaffinity >= 0) {
         CPU_SET(this->setaffinity, &my_set);
         sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
     }

     //create a thread for each core
     //TxThread
     pthread_create(&contexts[1].thread_id, NULL, th_funcTx, (void *) &contexts[1]);
     //RxThreads
     for (i = 2;  i < i_threadsRx + 3; i++) {
         pthread_create(&contexts[i].thread_id, NULL, th_funcRx, (void *) &contexts[i]);
     }

     //wait for then to all come up
     do {
         for (i = 1, j = 0;  i < i_threadsRx + 3; i++) {
             if (contexts[i].ready) {
                 j++;
             }
         }         
         sleep(1);
         printf("ready wait loop j = %d \n", j);
     } while (j < i_threadsRx + 1);
     printf("all cores ready %d\n", j);


     //wait for threadRx done
     do {
         for (i = 2, j = 2;  i <= i_threadsRx + 2; i++) {
             if (contexts[i].ready) {
                 j++;
             }
         }         
     } while (j < i_threadsRx + 3);
     printf("Rx cores done %d\n", j);

     //check that each rx detected the values
     for (j = 0; j < SAMPLES_MAX; j++) {
         first = contexts[ 2].uidSamples[j][0];
         printf("%04d %lld  ", j, first);
         for (i = 2; i < i_threadsRx + 2; i++) {
             if (first == contexts[i].uidSamples[j][0]) {
                 printf(". ");
             }
             else{
                 printf("X %lld", contexts[i].uidSamples[j][0]);
             }
         }
         printf("\n");
     }



     printf("tx diffs\n");
     for (j = 0, i = 0; j < SAMPLES_MAX; j++) {
         work1 =  contexts[ 1].uidSamples[j][0] - contexts[ 1].uidSamples[i][0];
         if (work1 == 0) {
             continue;
         }
         printf("%04d %lld\n", j, work1);
         i = j;
     }
     //tx jitter
     printf("tx jitter\n");
     first = TX_DELAY - (SAMPLES_MAX/2);
     for (j = 0; j < SAMPLES_MAX; j++) {
         scatter[j] = 0;
     }
     printf("center %d\n", TX_DELAY);
     l = 5;
     for (j = 0, i = 0; j < SAMPLES_MAX; j++) {
         work1 =  contexts[ 1].uidSamples[j][0] - contexts[ 1].uidSamples[i][0];
         if (work1 == 0) {
             continue;
         }
         if (work1 > (first + (SAMPLES_MAX) -1)) {
             scatter[(SAMPLES_MAX/l) -1] ++;
         }
         else if (work1 < first) {
                 scatter[0]++;
         }
         else {
             //work1 > work
             k = work1 -first;
             k = k/l;
             scatter[k]++;
         }
         i = j;
     }
     for (i = 0; i < SAMPLES_MAX/l; i++) {
         printf("%03d %d\n", i*l + (int)first, scatter[i]);
     }




	return 0;
}


/**
 * @brief 
 * 
 * @author martin (1/27/23)
 * 
 * @param p_arg 
 * 
 * @return void* 
 */
void *th_funcTx(void *p_arg){
    context_t *this = (context_t *) p_arg;
    cpu_set_t my_set;        /* Define your cpu_set bit mask. */
    int i_destCnt = i_threadsRx;
    int i = 0;
    msg_t *p_msg;
    unsigned long long int work;
    unsigned long long int work1;
    unsigned long long int prev;

    this->name = "txFunc";
    this->tpid = gettid();
    printf("Thread %s_%d PID %d %d\n", this->name,
                                       this->id, 
                                       this->tpid, 
                                       gettid());

    CPU_ZERO(&my_set); 
    if (this->setaffinity >= 0) {
        CPU_SET(this->setaffinity, &my_set);
        sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    }
    p_msg = this->p_msg;;
    this->ready = 1;

    i = 0;
    prev = __rdtsc();
    work = prev + (unsigned long long int)TX_DELAY;
    while (1){

        //sleep(0.001);
        do {
                    work1 =  __rdtsc();
        }while (work > work1);
        if ((work1 - prev) > (TX_DELAY + 100)) {
            work = work1 + (unsigned long long int)TX_DELAY;
            prev = work1;
        }
        else {
            p_msg->uid = work1;
            work = work1 + (unsigned long long int)TX_DELAY;
            prev = work1;
            this->uidSamples[i][0] = work1;
            i++;
        }

        if (i >= SAMPLES_MAX) {
            while (1) {
            }
        }
    }
}




/**
 * @brief 
 * 
 * @author martin (1/27/23)
 * 
 * @param p_arg 
 * 
 * @return void* 
 */
void *th_funcRx(void *p_arg){
    context_t *this = (context_t *) p_arg;
    cpu_set_t my_set;        /* Define your cpu_set bit mask. */
    msg_t *p_msg = this->p_msg;
    unsigned long long int uidOld = 0;
    unsigned long long int tsc = 0;

    this->name = "rxFunc";
    this->tpid = gettid();
    printf("Thread %s_%d PID %d %d\n", this->name,
                                       this->id, 
                                       this->tpid, 
                                       gettid());

    CPU_ZERO(&my_set); 
    if (this->setaffinity >= 0) {
        CPU_SET(this->setaffinity, &my_set);
        sched_setaffinity(0, sizeof(cpu_set_t), &my_set);
    }

    this->ready = 1;

    while (1){
        while (p_msg->uid == uidOld) {
        }
        tsc = __rdtsc();
        uidOld = p_msg->uid;
        if (this->statsIndex < SAMPLES_MAX) {
        this->uidSamples[this->statsIndex][0] = uidOld;
        this->uidSamples[this->statsIndex][1] = tsc;
        this->statsIndex++;
        }
        else {
                this->done = 1;
        }

    }
}


