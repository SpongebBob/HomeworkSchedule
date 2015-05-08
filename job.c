#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"

int jobid=0;
int siginfo=1;
int fifo;
int globalfd;
int siggoon = 0;
int siggrasp = 0;

struct waitqueue *head=NULL;
struct waitqueue *next=NULL,*current =NULL;

void setsiggoon()
{
    siggoon = 1;
}
/* 调度程序 */
void scheduler()
{
	struct jobinfo *newjob=NULL;
	struct jobcmd cmd;
	int  count = 0;
	//这个函数的作用是什么？DATALEN是在job.h里定义，为sizeof(struct jobcmd)
	bzero(&cmd,DATALEN);
	if((count=read(fifo,&cmd,DATALEN))<0)//count 只是为了debug用？
		error_sys("read fifo failed");
#ifdef DEBUG
	if(count){
		printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n",cmd.type,cmd.defpri,cmd.data);
	}
	else
		printf("no data read\n");
#endif

	/* 更新等待队列中的作业 */
	updateall();

	switch(cmd.type){
	case ENQ:
		#ifdef DEBUG_LJL_JOB
			printf("In the 'ENQ' case\n");
		#endif
		do_enq(newjob,cmd);
  		if(siggrasp)
    	    {
            next = jobselect();
            jobswitch();
            siggrasp = 0;
            return;
        }
		break;
	case DEQ:
		#ifdef DEBUG_LJL_JOB
			printf("In the 'DEQ' case\n");
		#endif
		do_deq(cmd);
      
		break;
	case STAT:
		#ifdef DEBUG_LJL_JOB
			printf("In the 'STAT' case\n");
		#endif
		do_stat(cmd);
		break;
	default:
		break;
	}

    if(!canswitch()||!hasequalpri())
    {
        return;
    }
	/* 选择高优先级作业 */
	next=jobselect();

	#ifdef DEBUG_LJL_JOB
		if(next)
			printf("The selected job is %s\n",next->job->cmdarg[0]);
	#endif
	/* 作业切换 */
	jobswitch();
}

int hasequalpri(){
	struct waitqueue *p;
	int nowpri;
	if(current==NULL)
		return 1;
	nowpri=current->job->curpri;
	for(p=head;p!=NULL;p=p->next){
		if(p->job->curpri>=nowpri)
			return 1;
	}
	return 0;

}


int canswitch(){
    
    int t;
    //has a higher pri,return 1;
    if(current==NULL)
        return 1;
    
    t=current->job->round_time;
    //	printf("%d\n",t);
    //	printf("%d\n",current->job->curpri);
    // time to switch, need to go;
    switch(current->job->curpri){
        case 1:
            return t>=5;
        case 2:
            return t>=2;
        case 3:
            return t>=1;
        default:
            return 1;
    }
    return 0;
}


int allocjid()
{
	return ++jobid;
}

void updateall()
{
	struct waitqueue *p,*temp;

	/* 更新作业运行时间 */
	if(current)
    	{
        	current->job->round_time += 1;
		current->job->run_time += 1; /* 加1代表1000ms */
		printf("current_job_round_time=%d\n",current->job->round_time);
		printf("current_job_pri=%d\n",current->job->curpri); 	   
	}
	/* 更新作业等待时间及优先级 */
    
	for(p = head; p != NULL; ){
		p->job->wait_time += 1000;
		if(p->job->wait_time >= 10000 && p->job->curpri < 3){
			//p->job->curpri++;
			//p->job->wait_time = 0;
            	temp = p->next;
            	movejobtoend(p);
            	p=temp;
            	continue;
		}
	p = p->next;
	}
}
/*because i has prothejob for i need to move to the end*/
void movejobtoend(struct waitqueue *p){
    struct waitqueue *pre=head;
    struct waitqueue *q=head;
    if(head==NULL)
        return;
    if(p->next==NULL){
        p->job->curpri++;
        p->job->wait_time=0;
        return;
    }
    while(q!=p&&q!=NULL){
        pre=q;
        q=q->next;
    }
    if(pre==q)
        head=head->next;
    else
        pre->next=q->next;
    while(pre->next!=NULL)
        pre=pre->next;
    pre->next=p;
    p->next=NULL;
    p->job->curpri++;
    p->job->wait_time = -1000;
}



struct waitqueue* jobselect()
{
	struct waitqueue *p,*prev,*select,*selectprev;
	int highest = -1;

	select = NULL;
	selectprev = NULL;
	if(head){
		/* 遍历等待队列中的作业，找到优先级最高的作业 */
		for(prev = head, p = head; p != NULL; prev = p,p = p->next){
			if(p->job->curpri > highest){
				select = p;
				selectprev = prev;
				highest = p->job->curpri;
			}
		}
		selectprev->next = select->next;
		//当前选中的作业应该是独立的，这样在放入等待队列时不会引起作业丢失
		//select->next = NULL;
		if (select == selectprev)
			//等待队列中只有当前一个作业
			head = head->next;
	}
	return select;
}

void jobswitch()
{
	char timebuf[BUFLEN];
	struct waitqueue *p;
	int i;

//执行jobswitch前的信息
#ifdef DEBUG_LJL_TASK9
	//当前进程信息
	printf("Before jobswitch, current:\n");
	if(current){
		printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}else{
		printf("NULL\n");
	}

	//当前等待队列信息
	printf("Before jobswitch, waitqueue:\n");
	if(head){
		printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
		for(p=head;p!=NULL;p=p->next){
			strcpy(timebuf,ctime(&(p->job->create_time)));
			timebuf[strlen(timebuf)-1]='\0';
			printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
				p->job->jid,
				p->job->pid,
				p->job->ownerid,
				p->job->run_time,
				p->job->wait_time,
				timebuf,
				"READY");
		}
	}else if(next){
		printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
		p = next;
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}else{
		printf("NULL\n");
	}

#endif


	if(current && current->job->state == DONE){ /* 当前作业完成 */
		/* 作业完成，删除它 */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* 释放空间 */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL)
	/* 没有作业要运行 */
	{
		return;
	}
	else if (next != NULL && current == NULL){ 
		/* 开始新的作业 */
		printf("start a new job\n");
		current = next;
		next = NULL;
		current->job->state = RUNNING;
        	current->job->round_time = 0;
		kill(current->job->pid,SIGCONT);
		return;
	}
	else if (next != NULL && current != NULL){ 
		/* 切换作业 */
		printf("switch to Pid: %d\n",next->job->pid);
		kill(current->job->pid,SIGSTOP);
		current->job->curpri = current->job->defpri;
		current->job->wait_time = 0;
      		current->job->round_time = 0;
		current->job->state = READY;
		current->next = NULL;
 
		/* 放回等待队列 */
		//为什么不修改current的next指针？
		if(head){
			for(p = head; p->next != NULL; p = p->next){
				#ifdef DEBUG_LJL_JOB
					printf("放回等待队列扫描：%d\n",p->job->jid);
				#endif

			}
                p->next = current;
		}else{
			head = current;
		}
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time = 0;
		kill(current->job->pid,SIGCONT);
		return;
	}else{ /* next == NULL且current != NULL，不切换 */
		return;
	}

//执行jobswitch后的信息
#ifdef DEBUG_LJL_TASK9
	//当前进程信息
	printf("After jobswitch, current:\n");
	if(current){
		printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}else{
		printf("NULL\n");
	}

	//当前等待队列信息
	printf("After jobswitch, waitqueue:\n");
	if(head){
		printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
		for(p=head;p!=NULL;p=p->next){
			strcpy(timebuf,ctime(&(p->job->create_time)));
			timebuf[strlen(timebuf)-1]='\0';
			printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
				p->job->jid,
				p->job->pid,
				p->job->ownerid,
				p->job->run_time,
				p->job->wait_time,
				timebuf,
				"READY");
		}
	}else{
		printf("NULL\n");
	}

#endif

	return ;
}

void sig_handler(int sig,siginfo_t *info,void *notused)
{
	int status;
	int ret;
#ifdef DEBUG_LJL_TASK10
	char timebuf[BUFLEN];
	struct waitqueue *p = NULL;
#endif

	switch (sig) {
		case SIGVTALRM: /* 到达计时器所设置的计时间隔 */
			scheduler();
			return;
		case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
			ret = waitpid(-1,&status,WNOHANG);//WNOHANG 若pid指定的子进程没有结束，则waitpid()函数返回0，不予以等待。若结束，则返回该子进程的ID。
			if (ret == 0)//pid指定的子进程没有结束
				return;
			//WIFEXITED：子进程正常退出（"exit"或"_exit"），此宏返回非0
			//WEXITSTATUS(status)：当WIFEXITED返回非零值时，可以用这个宏来提取子进程的返回值
			if(WIFEXITED(status)){
				//子进程正常退出了
				current->job->state = DONE;
				printf("normal termation, exit status = %d\n",WEXITSTATUS(status));
				//任务十
				#ifdef DEBUG_LJL_TASK10
					//当前进程信息
					printf("Current:\n");
					if(current){
						printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
						strcpy(timebuf,ctime(&(current->job->create_time)));
						timebuf[strlen(timebuf)-1]='\0';
						printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
							current->job->jid,
							current->job->pid,
							current->job->ownerid,
							current->job->run_time,
							current->job->wait_time,
							timebuf,"RUNNING");
					}else{
						printf("NULL\n");
					}

					//当前等待队列信息
					printf("Waitqueue:\n");
					if(head){
						printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
						for(p=head;p!=NULL;p=p->next){
							strcpy(timebuf,ctime(&(p->job->create_time)));
							timebuf[strlen(timebuf)-1]='\0';
							printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
								p->job->jid,
								p->job->pid,
								p->job->ownerid,
								p->job->run_time,
								p->job->wait_time,
								timebuf,
								"READY");
						}
					}else if(next){
						printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
						p = next;
						strcpy(timebuf,ctime(&(p->job->create_time)));
						timebuf[strlen(timebuf)-1]='\0';
						printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
							p->job->jid,
							p->job->pid,
							p->job->ownerid,
							p->job->run_time,
							p->job->wait_time,
							timebuf,
							"READY");
					}else{
						printf("NULL\n");
					}

				#endif

			//WIFSIGNALED(int status):如果子进程是因为信号而结束则此宏值为真
			//WTERMSIG(status)：当WIFSIGNALED为真时，可以用这个宏来取得取得子进程因信号而中止的信号代码
			}else if (WIFSIGNALED(status)){
				//子进程是因为信号而结束
				printf("abnormal termation, signal number = %d\n",WTERMSIG(status));
			//WIFSTOPPED：若为当前正处于暂停状态的子进程返回的状态，则为真
			//WSTOPSIG：当WIFSTOPPED为真时，可通过这个宏来取得使子进程暂停的信号编号
			}else if (WIFSTOPPED(status)){
				//子进程当前正处于暂停状态
				printf("child stopped, signal number = %d\n",WSTOPSIG(status));
			}
			return;
			default:
				return;
	}
}

void do_enq(struct jobinfo *newjob,struct jobcmd enqcmd)
{
	struct waitqueue *newnode,*p;
	int i=0,pid;
	char *offset,*argvec,*q;
	char **arglist;
	sigset_t zeromask;

	sigemptyset(&zeromask);

	/* 封装jobinfo数据结构 */
	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));
	newjob->jid = allocjid();
	newjob->defpri = enqcmd.defpri;
	newjob->curpri = enqcmd.defpri;
	newjob->ownerid = enqcmd.owner;
	newjob->state = READY;
	newjob->create_time = time(NULL);
	newjob->wait_time = 0;
	newjob->run_time = 0;
    newjob->round_time = 0;
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum){
		if(*offset == ':'){
			*offset++ = '\0';
			q = (char*)malloc(offset - argvec);
			strcpy(q,argvec);
			arglist[i++] = q;
			argvec = offset;
		}else
			offset++;
	}
    if(current!=NULL&&newjob->curpri>current->job->curpri){
        siggrasp=1;
    }

	arglist[i] = NULL;

#ifdef DEBUG

	printf("enqcmd argnum %d\n",enqcmd.argnum);
	for(i = 0;i < enqcmd.argnum; i++)
		printf("parse enqcmd:%s\n",arglist[i]);

#endif

	/*向等待队列中增加新的作业*/
	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->next =NULL;
	newnode->job=newjob;

	if(head)
	{
		for(p=head;p->next != NULL; p=p->next);
		p->next =newnode;
	}else
		head=newnode;

	/*为作业创建进程*/
	if((pid=fork())<0)
		error_sys("enq fork failed");

	if(pid==0){
		newjob->pid =getpid();
		/*阻塞子进程,等等执行*/
        kill(getppid(),SIGUSR1);
		raise(SIGSTOP);
#ifdef DEBUG

		printf("begin running\n");
		for(i=0;arglist[i]!=NULL;i++)
			printf("arglist %s\n",arglist[i]);
#endif

		/*复制文件描述符到标准输出*/
		dup2(globalfd,1);
		/* 执行命令 */
		if(execv(arglist[0],arglist)<0)
			printf("exec failed\n");
		exit(1);
	}else{
        signal(SIGUSR1,setsiggoon);
        while(siggoon==0){

        }
        siggoon=0;
		newjob->pid=pid;
	}
}

void do_deq(struct jobcmd deqcmd)
{
	int deqid,i;
	struct waitqueue *p,*prev,*select,*selectprev;
	deqid=atoi(deqcmd.data);

#ifdef DEBUG
	printf("deq jid %d\n",deqid);
#endif

	/*current jodid==deqid,终止当前作业*/
	if (current && current->job->jid ==deqid){
		printf("teminate current job\n");
		kill(current->job->pid,SIGKILL);
		for(i=0;(current->job->cmdarg)[i]!=NULL;i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i]=NULL;
		}
		free(current->job->cmdarg);
		free(current->job);
		free(current);
		current=NULL;
	}
	else{ /* 或者在等待队列中查找deqid */
		#ifdef DEBUG_LJL_DEQ
			printf("Task to delete is not current task\n");
		#endif
		select=NULL;
		selectprev=NULL;
		
        if(head){
            for(prev=head,p=head;p!=NULL;prev=p,p=p->next)
                if(p->job->jid==deqid){
                    select=p;
                    selectprev=prev;
                    break;
                }
            if(select==NULL){
                #ifdef DEBUG_LJL_DEQ
                printf("can't find this job :%d\n",deqid);
                #endif
                return;
            }
            else if(select==head)
                head=head->next;
            else{
                selectprev->next=select->next;
            }
        }
        

		if(select){
			for(i=0;(select->job->cmdarg)[i]!=NULL;i++){
				free((select->job->cmdarg)[i]);
				(select->job->cmdarg)[i]=NULL;
			}
			free(select->job->cmdarg);
			free(select->job);
			free(select);
			select=NULL;
		}
	}
}

void do_stat(struct jobcmd statcmd)
{
	struct waitqueue *p;
	char timebuf[BUFLEN];
	/*
	*打印所有作业的统计信息:
	*1.作业ID
	*2.进程ID
	*3.作业所有者
	*4.作业运行时间
	*5.作业等待时间
	*6.作业创建时间
	*7.作业状态
	*/

	/* 打印信息头部 */
#ifdef DEBUG_LJL_STAT
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCURPRI\tSTATE\n");
	if(current){
		printf("%d\t%d\t%d\t%d\t%d\t\t%d\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			current->job->curpri,
			"RUNNING");
	}
	for(p=head;p!=NULL;p=p->next){
		printf("%d\t%d\t%d\t%d\t%d\t\t%d\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			p->job->curpri,
			"READY");
	}
#else
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
	if(current){
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}
	for(p=head;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}
#endif
}

int main()
{
	struct timeval interval;
	struct itimerval new,old;
	struct stat statbuf;
	struct sigaction newact,oldact1,oldact2;

	if(stat("/tmp/server",&statbuf)==0){
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server",0666)<0)
		error_sys("mkfifo failed");
	/* 在非阻塞模式下打开FIFO */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* 建立信号处理函数 */
	newact.sa_sigaction=sig_handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags=SA_SIGINFO;
	sigaction(SIGCHLD,&newact,&oldact1);
	sigaction(SIGVTALRM,&newact,&oldact2);

	/* 设置时间间隔为1000毫秒 */
	interval.tv_sec=1;
	interval.tv_usec=0;

	new.it_interval=interval;
	new.it_value=interval;
	setitimer(ITIMER_VIRTUAL,&new,&old);

	while(siginfo==1);

	close(fifo);
	close(globalfd);
	return 0;
}
