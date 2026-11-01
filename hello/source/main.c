/*
 * Phase 1 gate: prove the build -> ELF -> SELF path before any real code exists.
 *
 * Prints a marker over the LV2 debug console (the TTY log RPCS3 shows) and then
 * keeps the process alive briefly, so the log is readable even though a bare
 * hello-world exits immediately.
 */
#include <stdio.h>
#include <unistd.h>

#include <sys/process.h>
#include <sysutil/sysutil.h>

SYS_PROCESS_PARAM(1001, 0x100000)

int main(int argc, char **argv)
{
    printf("hello-ps3: alive\n");
    printf("hello-ps3: built %s %s\n", __DATE__, __TIME__);
    printf("hello-ps3: argc=%d\n", argc);
    fflush(stdout);

    sleep(3);

    printf("hello-ps3: done\n");
    fflush(stdout);
    return 0;
}
