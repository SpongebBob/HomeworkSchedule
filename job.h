#ifndef JOB_H
#define JOB_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/types.h>

#ifndef DEBUG
#define DEBUG
#endif

#undef DEBUG

//用于调试的宏
#define DEBUG_LJL
#ifdef DEBUG_LJL
//#define DEBUG_LJL_JOB
//#define DEBUG_LJL_ENQ
//#define DEBUG_LJL_DEQ
#define DEBUG_LJL_STAT
//#define DEBUG_LJL_TASK9
#define DEBUG_LJL_TASK10
#endif

#define BUFLEN 100  
#define GLOBALFILE "screendump"

enum jobstate{
    READY,RUNNING,DONE
};

enum cmdtype{
    ENQ=-1,DEQ=-2,STAT=-3 
};
struct jobcmd{
    enum cmdtype type;
    int argnum;
    int owner;
    int defpri;//初始优先级
    char data[BUFLEN];//这个data是用来存什么的？运行参数么？
};

//
#define DATALEN sizeof(struct jobcmd)

//修改命名习惯
//typedef struct job_tag struct jobinfo;

struct jobinfo{
    int jid;              /* 作业ID */
    int pid;              /* 进程ID */
    char** cmdarg;        /* 命令参数 */
    int defpri;           /* 默认优先级 */
    int curpri;           /* 当前优先级 */
    int ownerid;          /* 作业所有者ID */
    int wait_time;        /* 作业在等待队列中等待时间 */
    time_t create_time;   /* 作业创建时间 */
    int run_time;         /* 作业运行时间 */
    int round_time;
    enum jobstate state;  /* 作业状态 */
};

struct waitqueue{
    struct waitqueue *next;
    struct jobinfo *job;
};

void scheduler();  //作业调度函数
void sig_handler(int sig,siginfo_t *info,void *notused);//信号处理函数
int allocjid();//分配作业ID
void add_queue(struct jobinfo *job);//向等待队列中添加作业
void del_queue(struct jobinfo *job);//删除作业
void do_enq(struct jobinfo *newjob,struct jobcmd enqcmd);//执行入队操作
void do_deq(struct jobcmd deqcmd);//执行出队操作
void do_stat(struct jobcmd statcmd);//实行stat命令
void updateall();//更新等待队列中各个作业的信息
struct waitqueue* jobselect();//从等待队列中选取下一个作业
void jobswitch();//作业轮转
int canswitch();
void movejobtoend(struct waitqueue *p );

int hasequalpri();

void error_doit(int errnoflag,const char *fmt,va_list ap);
void error_sys(const char *fmt,...);
void error_msg(const char *fmt,...);
void error_quit(const char *fmt,...);

#endif

