extern int main(void);

extern void exit_process(int status);

__attribute__((noreturn)) void _start(void) {
    int result = main();

    exit_process(result);

    __builtin_unreachable();
}