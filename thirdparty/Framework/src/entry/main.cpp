extern void initMain(int argc, char** argv);
extern bool mainLoop(void);
extern void destroyMain(void);

int main(int argc, char** argv) {
	initMain(argc, argv);
	while (mainLoop()) {}
	destroyMain();
	return 0;
}
