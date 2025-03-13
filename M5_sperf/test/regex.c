#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

int main() {
    // 要匹配的文本
    const char *text = "ABC( <DEF> ) and GHI( <JKL> )";
    
    // 正则表达式模式
    const char *pattern = "([A-Z]+)\\( <([A-Z]+)> \\)";
    
    // 编译正则表达式
    regex_t regex;
    int reti = regcomp(&regex, pattern, REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
        return 1;
    }
    
    // 匹配正则表达式
    regmatch_t matches[3]; // 匹配结果
    const char *p = text;
    while (regexec(&regex, p, 3, matches, 0) == 0) {
        // 提取匹配的子串
        for (int i = 1; i < 3; i++) { // 从1开始，0是整个匹配
            int start = matches[i].rm_so;
            int end = matches[i].rm_eo;
            printf("Match %d: %.*s\n", i, end - start, p + start);
        }
        p += matches[0].rm_eo; // 移动指针，查找下一个匹配
    }
    
    // 释放正则表达式
    regfree(&regex);
    
    return 0;
}