#include <stdio.h>
#include <unistd.h>

int main() {
    fprintf(stderr, "dummy_stderr(1)                                = 0 <9999>\n");
    printf("dummy_stdout(1)                                = 0 <9999>\n");
    fflush(stdout);
    return 0;
}
