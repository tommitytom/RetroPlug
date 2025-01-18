#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


extern void initMain(int argc, char** argv);
extern bool mainLoop(void);
extern void destroyMain(void);

void handler(int sig) {
    void *array[10];
    size_t size = backtrace(array, 10);
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

int main(int argc, char** argv) {
    printf("RetroPlug 0.4.0\n");

	signal(SIGSEGV, handler);

    printf("initmain\n");
	initMain(argc, argv);
	while (mainLoop()) {}
	destroyMain();
	return 0;
}
