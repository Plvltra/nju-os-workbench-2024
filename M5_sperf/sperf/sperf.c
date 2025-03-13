#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/reg.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <pcre.h>
#include "map.h"

typedef struct {
    int start;
    int end;
} Range;

typedef struct {
    const char* name;
    double time;
} SysCall;

int compare(const void *a, const void *b) {
    SysCall *call_a = (SysCall *)a;
    SysCall *call_b = (SysCall *)b;
    if (call_b->time - call_a->time > 0)
        return 1;
    else
        return -1;
}

int min(int a, int b) {
    return (a < b) ? a : b;
}

int percentage(double value)
{
    return value * 100 + 0.5;
}

void ptrace_ls()
{
    pid_t child;
    long orig_rax = 0;
    int status;
    struct user_regs_struct regs;

    child = fork();
    if (child == 0)
    {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        execl("/bin/ls", "ls", NULL);
    }
    else
    {
        while (1)
        {
            wait(&status);
            if (WIFEXITED(status))
            {
                break;
            }
            long reg_offset;
#ifdef __x86_64__
            reg_offset = ORIG_RAX;
#else
            reg_offset = ORIG_EAX;
#endif
            orig_rax = ptrace(PTRACE_PEEKUSER, child, sizeof(long) * reg_offset, NULL);
            fprintf(stderr, "System call: %ld\n", orig_rax);
            ptrace(PTRACE_SYSCALL, child, NULL, NULL);
        }
    }
}

int count_lines(const char *buffer) {
    int lines = 0;
    const char *p = buffer;
    while (*p != '\0') {
        if (*p == '\n') {
            lines++;
        }
        p++;
    }
    if (p != buffer && *(p - 1) != '\n') {
        lines++;
    }
    return lines;
}

static int parse_syscall(char *buffer, Range *name, Range *time)
{
    /** Example
    *  mprotect(0x5594db802000, 4096, PROT_READ) = 0 <0.000059>
    *  mprotect(0x7f28ffea2000, 4096, PROT_READ) = 0 <0.000040>
    *  munmap(0x7f28ffe63000, 70289)           = 0 <0.000060>
    *  write(2, "Usage: time [-apvV] [-f format] "..., 177Usage: time [-apvV] [-f format] [-o file] [--append] [--verbose]
    *      [--portability] [--format=format] [--output=file] [--version]
    *      [--quiet] [--help] command [arg...]
    *  ) = 177 <0.000065>
    */
    const char *error;
    int error_offset;
    pcre *re;
    const char *pattern = "(.*?)\\(.*?<([0-9]+(\\.[0-9]+)?)>\n";
    // Compile the regex pattern
    re = pcre_compile(pattern, 0, &error, &error_offset, NULL);
    if (re == NULL) {
        fprintf(stderr, "PCRE compilation failed at offset %d: %s\n", error_offset, error);
        return -1;
    }

    // Execute the regex
    int ovector[30];
    int rc;
    const char *ptr = buffer;
    int idx = 0;
    int offset = 0;
    while ((rc = pcre_exec(re, NULL, ptr, strlen(ptr), 0, 0, ovector, 30)) >= 0) {
        // two match groups
        name[idx].start = ovector[2] + offset;
        name[idx].end = ovector[3] + offset;
        time[idx].start = ovector[4] + offset;
        time[idx].end = ovector[5] + offset;
        idx++;
        ptr += ovector[1];
        offset += ovector[1];
    }

    pcre_free(re);
    return idx;
}

void dump()
{
    // // TODO: 使用ansi escape code实现彩色递归输出全屏颜色
    // struct winsize w;
    // // 使用ioctl系统调用获取终端的窗口大小
    // if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
    //     perror("ioctl");
    //     return 1;
    // }
    // // 打印终端的行数和列数
    // printf("行数: %d\n", w.ws_row);
    // printf("列数: %d\n", w.ws_col);
}

// usage: ./sperf-64 ls -a
int main(int argc, char *argv[], char *envp[]) {
    // for (int i = 0; i < argc; i++) {
    //     assert(argv[i]);
    //     printf("argv[%d] = %s\n", i, argv[i]);
    // }
    // assert(!argv[argc]);
    // for (int i = 0; ; i++) {
    //     if (!envp[i])
    //         break;
    //     printf("envp[%d] = %s\n", i, envp[i]);
    // }

    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("[pipe]");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        close(pipefd[0]); // close read
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        FILE *file = fdopen(pipefd[1], "w");
        if (setvbuf(file, NULL, _IOLBF, 0) != 0) {
            perror("Failed to set line buffering");
            return EXIT_FAILURE;
        }
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IOLBF, 0);

        char *strace_path = "/usr/bin/strace";
        char *strace_argv[100];
        char fd_path[30];
        sprintf(fd_path, "/proc/%d/fd/%d", getpid(), pipefd[1]);
        const int FIXED_LENGTH = 3;
        strace_argv[0] = "strace";
        strace_argv[1] = "-T";
        strace_argv[2] = "-o";
        strace_argv[3] = fd_path;
        for (int i = 1; i <= argc; i++) // NULL include
        {
            strace_argv[FIXED_LENGTH + i] = argv[i];
        }
        execve(strace_path, strace_argv, envp);
    }
    else
    {
        close(pipefd[1]); // close write
        char buffer[65536];
        pid_t status = 0;
        double passed_time = 0;
        map_double_t used_time;
        map_init(&used_time);
        while (status == 0) // child process still run
        {
            usleep(100000); // delay 100ms
            passed_time += 0.1;

            memset(buffer, 0, sizeof(buffer));
            int read_count = read(pipefd[0], buffer, sizeof(buffer)); // TODO: read与write同步(容易读入半行) ./sperf-64 ../test/matrix.out
            // printf("%s", buffer);

            // process output of strace -T 
            int line_num = count_lines(buffer);
            Range *names = malloc(sizeof(Range) * line_num);
            Range *times = malloc(sizeof(Range) * line_num);
            int call_count = parse_syscall(buffer, names, times);
            assert(call_count > 0);
            for (int i = 0; i < call_count; i++)
            {
                char name[50];
                char time_str[50];
                memset(name, '\0', sizeof(name));
                memset(time_str, '\0', sizeof(time_str));
                int name_len = names[i].end - names[i].start;
                int time_len = times[i].end - times[i].start;
                assert(name_len < 50);
                strncpy(name, buffer + names[i].start, name_len);
                strncpy(time_str, buffer + times[i].start, time_len);

                char *end_ptr;
                double time = strtod(time_str, &end_ptr);
                assert(end_ptr == time_str + strlen(time_str));

                double *value = map_get(&used_time, name);
                double new_value = (value != NULL) ? *value + time : time;
                map_set(&used_time, name, new_value);
                // printf("name: %s\ttime: %f\n", name, time);
            }

            // get top5 syscalls
            SysCall *syscalls = malloc(sizeof(SysCall) * call_count);
            const char *key;
            map_iter_t iter = map_iter(&used_time);
            int idx = 0;
            while ((key = map_next(&used_time, &iter))) {
                // printf("%s -> %f\n", key, *map_get(&used_time, key));
                double value = *map_get(&used_time, key);
                syscalls[idx].name = key;
                syscalls[idx].time = value;
                idx++;
            }
            qsort(syscalls, call_count, sizeof(SysCall), compare);

            printf("====================\n");
            printf("Time: %.1fs\n", passed_time);
            const int TOP_K = 5;
            double others_time = passed_time;
            for (int i = 0; i < min(TOP_K, call_count); i++)
            {
                others_time -= syscalls[i].time;
                int percent = percentage(syscalls[i].time / passed_time);
                printf("%s (%d%%)\n", syscalls[i].name, percent);
            }
            if (call_count > TOP_K)
            {
                int percent = percentage(others_time / passed_time);
                printf("others (%d%%)\n", percent);
            }
            fflush(stdout);

            status = waitpid(pid, &status, WNOHANG);
            if (status == -1)
            {
                perror("waitpid failed");
                exit(EXIT_FAILURE);
            }

            free(syscalls);
            free(names);
            free(times);
        }
        map_deinit(&used_time);
    }
    return 0;
}

//缓冲不够存0.1ms的输出，加大
//细想read的逻辑
