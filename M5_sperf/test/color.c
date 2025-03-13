// Simplest
// #include <stdio.h>
// #include <unistd.h>  // 用于sleep函数

// int main() {
//     // 定义ANSI背景颜色代码
//     const char *colors[] = {
//         "\033[41m", // 红色背景
//         "\033[42m", // 绿色背景
//         "\033[43m", // 黄色背景
//         "\033[44m", // 蓝色背景
//         "\033[45m", // 紫色背景
//         "\033[46m", // 青色背景
//     };

//     int numColors = sizeof(colors) / sizeof(colors[0]);

//     // 无限循环显示颜色块
//     for (int i = 0; ; i = (i + 1) % numColors) {
//         // 打印颜色块
//         // printf("%s  \033[0m", colors[i]); // 打印带背景颜色的空格并重置颜色
//         printf("%s", colors[i]); // 打印带背景颜色的空格并重置颜色
//         fflush(stdout);

//         // 等待一段时间
//         sleep(1); // 每个颜色显示1秒

//         // 回到行首
//         printf("\r");
//     }

//     return 0;
// }



// Progress Bar
// #include <stdio.h>

// void printBar(int value, int max) {
//     int barLength = 50;  // 图表的最大长度
//     int numBlocks = (value * barLength) / max;  // 根据值计算要显示的块数

//     // 选择颜色：红色、黄色、绿色
//     const char *color;
//     if (value < max / 3) {
//         color = "\033[31m";  // 红色
//     } else if (value < (2 * max) / 3) {
//         color = "\033[33m";  // 黄色
//     } else {
//         color = "\033[32m";  // 绿色
//     }

//     // 打印块状图
//     printf("%3d: ", value);
//     printf("%s", color);  // 设置颜色
//     for (int i = 0; i < numBlocks; ++i) {
//         printf("█");
//     }
//     printf("\033[0m\n");  // 重置颜色
// }

// int main() {
//     // 示例数据
//     int data[] = {10, 30, 50, 70, 90};
//     size_t dataSize = sizeof(data) / sizeof(data[0]);

//     // 找出最大值以便规范化
//     int maxValue = data[0];
//     for (size_t i = 1; i < dataSize; ++i) {
//         if (data[i] > maxValue) {
//             maxValue = data[i];
//         }
//     }

//     // 打印每个数据点的柱状图
//     for (size_t i = 0; i < dataSize; ++i) {
//         printBar(data[i], maxValue);
//     }

//     return 0;
// }



// whole screen operation
// #include <stdio.h>
// #include <unistd.h>

// int main() {
//     // 隐藏光标
//     printf("\e[?25l");

//     // 开始颜色循环
//     for (;;) {
//         for (int r = 0; r < 6; ++r) {
//             for (int g = 0; g < 6; ++g) {
//                 for (int b = 0; b < 6; ++b) {
//                     // 计算 256 色背景颜色代码
//                     int color_code = 16 + r * 36 + g * 6 + b;

//                     // 设置背景颜色
//                     printf("\e[48;5;%dm", color_code);

//                     // 清屏（这里使用转义序列清屏）
//                     printf("\e[2J");

//                     // 光标移动到左上角
//                     printf("\e[H");

//                     // 短暂延迟以产生动画效果
//                     usleep(100000); // 100,000 微秒，即 0.1 秒
//                 }
//             }
//         }
//     }

//     // 恢复光标 (虽然无限循环中不会执行)
//     printf("\e[?25h");

//     return 0;
// }



#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/reg.h>
#include <sys/ioctl.h>

int main() {
    // 定义ANSI背景颜色代码
    const char *colors[] = {
        "\e[37;41m", // 红色背景
        "\e[37;42m", // 绿色背景
        "\e[37;43m", // 黄色背景
        "\e[37;44m", // 蓝色背景
        "\e[37;45m", // 紫色背景
        "\e[37;46m", // 青色背景
    };

    int numColors = sizeof(colors) / sizeof(colors[0]);

    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        perror("ioctl");
        return 1;
    }
    printf("行数: %d\n", w.ws_row);
    printf("列数: %d\n", w.ws_col);

    int row = 1;
    int col = 1;
    // 无限循环显示颜色块
    for (int i = 0; ; i = (i + 1) % numColors) {
        // 打印颜色块
        printf("%sa\e[0m", colors[i]); // 打印带背景颜色的空格并重置颜色
        fflush(stdout);

        // 等待一段时间
        usleep(100000); // 每个颜色显示1秒

        // 回到行首
        // printf("\r");
        printf("\e[%d;%dH", row, col);
        col++;
        if (col > w.ws_col)
        {
            col = 1;
            row++;
        }
    }

    return 0;
}