job: job.o job.h error.c enq deq stat Demo DemoForTask10
	cc -o job job.o job.h error.c
enq: enq.o job.h error.c
	cc -o enq enq.o job.h error.c
deq: deq.o job.h error.c
	cc -o deq deq.o job.h error.c
stat: stat.o job.h error.c
	cc -o stat stat.o job.h error.c
job.o: job.c
	cc -g -c job.c
enq.o: enq.c
	cc -g -c enq.c
deq.o: deq.c
	cc -g -c deq.c
stat.o: stat.c
	cc -g -c stat.c
Demo: Demo.c
	cc -o Demo Demo.c
DemoForTask10: DemoForTask10.c
	cc -o DemoForTask10 DemoForTask10.c
clean:
	rm job enq deq stat Demo job.o deq.o enq.o stat.o DemoForTask10
