#include <stdio.h>
#include <unistd.h>

int main() {
    int times = 10;
    while (times--) {
        // 调用 getpid() 系统调用
        pid_t pid = getpid();
        // 打印出进程 ID
        // printf("Process ID: %d\n", pid);

        // 延迟以避免过多输出
        usleep(100000); // 延迟100毫秒
    }
    return 0;
}
