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

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif
  

void *th_func(void *p_arg);


#define MSG_CMD_MASTER_START        10       //arg master context id
#define MSG_CMD_SYNC                1
#define MSG_CMD_FOLLOW_UP           2
#define MSG_CMD_DELAY_REQ           3
#define MSG_CMD_DELAY_RESP          4
#define MSG_CMD_MASTER_DONE         11


typedef struct msg_s {
    unsigned long long int uid __attribute__ ((aligned(CACHELINE_SIZE)));
    int msg_cmd;
    int arg;
    unsigned long long int t1;
    unsigned long long int t2;
    unsigned long long int t3;
    unsigned long long int t4;
    int data[4];
} msg_t ;

msg_t g_msgs[PRIV_CORES_MAX];       //global cache aligned message areas


typedef struct context_s {
    msg_t *p_msg  __attribute__ ((aligned(CACHELINE_SIZE)));                       //location to recieve a message
    pthread_t   thread_id;              //
    int id;
    int tpid;                           //os thread process id
    int setaffinity;                    //if set core to bind to
    char *name;
    int ready;
    //results
    unsigned long long int t1 __attribute__ ((aligned(CACHELINE_SIZE)));
    unsigned long long int t2;
    unsigned long long int t3;
    unsigned long long int t4;
    long long int offset;

} context_t;
context_t   contexts[PRIV_CORES_MAX + 1];

//notes
//context 0 main
//context n thread and core n

int g_ncores = 24;          //Todo learn dynamically
typedef struct core_stats_s {
    unsigned long long int t1 ;
    unsigned long long int t2;
    unsigned long long int t3;
    unsigned long long int t4;
    unsigned long long int offset;
}core_stats_t;

typedef struct master_stats_s {
    core_stats_t perCore[PRIV_CORES_MAX];
}master_stats_t;

master_stats_t master_stats[PRIV_CORES_MAX];

void usage(){
    printf("-h     help\n\n");
}


int main(int argc, char **argv){	
    int opt;
    int i,j,k,l, state, run;
    context_t *this = &contexts[0];
    cpu_set_t my_set;        /* Define your cpu_set bit mask. */
    char cwork[64];
    char work[64];
    unsigned long long int uidOld = 0;
    unsigned long long int tsc = 0;
    context_t *p_cont;

    msg_t *p_sendMsg;
    msg_t *p_recvMsg;

    this->name = "Main";
    this->tpid = gettid();
    printf("%s PID %d %d\n", this->name, this->tpid, gettid());

    //init context objects
    for (i = 0; i < PRIV_CORES_MAX; i++) {
        contexts[i].id = i;
        contexts[i].setaffinity = i;
        contexts[i].p_msg = &g_msgs[i];
    }

    while((opt = getopt(argc, argv, "h")) != -1) 
    { 
        switch(opt) 
        { 
        case 'h': 
            usage();
            return 0;
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
    CPU_SET(this->setaffinity, &my_set);
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);

     //create a thread for each core
     for (i = 1;  i < g_ncores; i++) {
         pthread_create(&contexts[i].thread_id, NULL, th_func, (void *) &contexts[i]);
     }

     //wait for then to all come up
     do {
         for (i = 1, j = 1;  i < g_ncores; i++) {
             if (contexts[i].ready) {
                 j++;
             }
         }         
     } while (j < g_ncores);
     printf("all cores ready %d\n", j);

     //sleep(1);

     i = 1; //master core
     state = 0;  //iteration state
     p_recvMsg = this->p_msg;
     run = 1;
     while (run) {
             switch (state) {
             case 0:    //start a new master
                 p_sendMsg = &g_msgs[i];
                 p_sendMsg->msg_cmd = MSG_CMD_MASTER_START;
                 p_sendMsg->uid = __rdtsc();
                 state++;
                 break;
             case 1:    //start sent wait for done
                 while (p_recvMsg->uid == uidOld) {
                 }
                 tsc = __rdtsc();
                 uidOld = p_recvMsg->uid;
                 if (p_recvMsg->msg_cmd == MSG_CMD_MASTER_DONE) {
                     //printf("main recieved master_done\n");
                     //get stats before next run
                     for (j = 1; j < g_ncores; j++) {
                         if (j == i) {
                             master_stats[i].perCore[j].offset = 0;
                             master_stats[i].perCore[j].t1 = 0;
                             master_stats[i].perCore[j].t2 = 0;
                             master_stats[i].perCore[j].t3 = 0;
                             master_stats[i].perCore[j].t4 = 0;
                         }
                         else {
                             master_stats[i].perCore[j].offset = contexts[j].offset;
                             master_stats[i].perCore[j].t1 = contexts[j].t1;
                             master_stats[i].perCore[j].t2 = contexts[j].t1;
                             master_stats[i].perCore[j].t3 = contexts[j].t1;
                             master_stats[i].perCore[j].t4 = contexts[j].t1;
                         }
                     }
                     i++;
                     if (i >= g_ncores) {
                         run = 0;
                     }
                     state = 0;
                 }
                 break;

             default:
                 break;

             }
         }
     printf("          :   ");
     for (i = 1;  i < g_ncores; i++) {
         printf("%02d     ", i);
     }
     printf("\n");
     for (i = 1;  i < g_ncores; i++) {
         printf("Master %02d : ", i);

         for (j = 1; j < g_ncores; j++) {
             if (j == i) {
                 printf("XXXXXX ");
             }
             else {
                 printf("%06d ",  (int) master_stats[i].perCore[j].offset);
             }
         }
         printf("\n");
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
void *th_func(void *p_arg){
    context_t *this = (context_t *) p_arg;
    cpu_set_t my_set;        /* Define your cpu_set bit mask. */
    int i = 0;
    msg_t *p_msg;
    msg_t *p_sendMsg;
    unsigned long long int uidOld;
    unsigned long long int tsc;
    int state = 0;
    long long int a, b;

    this->name = "func";
    this->tpid = gettid();
    /*
    printf("Thread %s_%02d PID %d %d\n", this->name,
                                       this->id, 
                                       this->tpid, 
                                       gettid());
*/
    CPU_ZERO(&my_set); 
    CPU_SET(this->setaffinity, &my_set);
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);

    p_msg = this->p_msg;;
    this->ready = 1;

    while (1){

        while (p_msg->uid == uidOld) {
        }
        tsc = __rdtsc();
        uidOld = p_msg->uid;
        //printf("thd %02d cmd %d \n", this->id, p_msg->msg_cmd);
        if (state == 0) {
            //waiting for anything
            switch (p_msg->msg_cmd) {
            case MSG_CMD_MASTER_START:
                //master
                    for (i = 1; i < g_ncores; i++) {
                        if (i == this->id) {
                            continue;
                        }
                        state = 10;
                        //slave id
                        p_sendMsg = contexts[i].p_msg;
                        do {
                            switch (state) {
                            case 10:  //send sync
                                p_sendMsg->arg = this->id;
                                p_sendMsg->msg_cmd = MSG_CMD_SYNC;
                                p_sendMsg->t1 = __rdtsc();
                                p_sendMsg->uid = p_sendMsg->t1;
                                //this->t1 = p_sendMsg->t1;
                                //printf("thd %02d send  MSG_CMD_SYNC to %d \n", this->id, i);
                                state++;
                                break;
                            case 11:  //waiting for delay_req
                                while (p_msg->uid == uidOld) {
                                }
                                tsc = __rdtsc();
                                uidOld = p_msg->uid;
                                if (p_msg->msg_cmd == MSG_CMD_DELAY_REQ) {
                                     //printf("thd %02d cmd %d \n", this->id, p_msg->msg_cmd);
                                    //this->t4 = tsc;
                                    p_sendMsg->t4 = tsc;
                                    p_sendMsg->msg_cmd = MSG_CMD_DELAY_RESP;
                                    p_sendMsg->uid = __rdtsc();
                                    //printf("thd %02d send  MSG_CMD_DELAY_RESP to %d \n", this->id, i);
                                    state = 0;
                                }
                                break;
                              default:
                                break;
                            }
                        }while (state > 10);
                    }
                //printf("thd %02d master done \n", this->id);
                state = 0;
                p_sendMsg = contexts[0].p_msg;
                p_sendMsg->msg_cmd = MSG_CMD_MASTER_DONE;
                p_sendMsg->uid = __rdtsc();
                break;
            case MSG_CMD_SYNC:
                //slave
                state = 1;
                //printf("thd %02d p_msg->arg %d \n", this->id, p_msg->arg);
                p_sendMsg = contexts[p_msg->arg].p_msg;
                do {
                    switch (state) {
                    case 1: //recieved sync

                        p_msg->t2 = tsc;
                        p_sendMsg->msg_cmd = MSG_CMD_DELAY_REQ;
                        p_msg->t3 = __rdtsc();

                        p_sendMsg->uid = p_msg->t3 ;
                        state ++;
                        break;
                    case 2: //waiting for delay_resp
                        while (p_msg->uid == uidOld) {
                        }
                        tsc = __rdtsc();
                        uidOld = p_msg->uid;
                        if (p_msg->msg_cmd == MSG_CMD_DELAY_RESP) {
                            //we have everything to calculate offset
                            this->t1 = p_msg->t1;
                            this->t2 = p_msg->t2;
                            this->t3 = p_msg->t3;
                            this->t4 = p_msg->t4;
                            a = (int)(this->t1 - this->t2);
                            b = (int)(this->t3 - this->t4);
                            this->offset = (long long int)(a-b)/2;
                            state = 0;
                        }
                        break;
                    default:
                        break;

                    }
                } while (state > 0);




                break;
            default:
                break;
            }
        }
    }
}


