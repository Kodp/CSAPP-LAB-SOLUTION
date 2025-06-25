#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    va_list ap;
    const char* fmt;
    const char* file;
    struct tm* time;
    void* udata;
    int line;
    int level;
} log_Event;

typedef void (*log_LogFn)(log_Event* ev);

// 日志级别枚举
enum { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };

// API 函数声明
const char* log_level_string(int level);
void log_set_level(int level);
void log_set_quiet(bool enable);
int log_add_callback(log_LogFn fn, void* udata, int level);
int log_add_fp(FILE* fp, int level);
void log_log(int level, const char* file, int line, const char* fmt, ...);



// ======== 以下是实现部分 ========
#ifdef LOG_IMPLEMENTATION

#define MAX_CALLBACKS 32
#define LOG_USE_COLOR  // 注释此行可禁用颜色

// 回调函数结构
typedef struct {
    log_LogFn fn;
    void* udata;
    int level;
} Callback;

// 全局状态
static struct {
    int level;
    bool quiet;
    Callback callbacks[MAX_CALLBACKS];
} L = {.level = LOG_INFO};  // 默认日志级别

// 级别字符串
static const char* level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

// 颜色定义 (ANSI codes)
#ifdef LOG_USE_COLOR
static const char* level_colors[] = {
    "\x1b[94m", "\x1b[36m", "\x1b[32m", 
    "\x1b[33m", "\x1b[31m", "\x1b[35m"
};
#endif

// 标准输出回调函数
static void stdout_callback(log_Event* ev) {
    char buf[16];
    buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';
    
#ifdef LOG_USE_COLOR
    fprintf(ev->udata, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ",
            buf, level_colors[ev->level], level_strings[ev->level], 
            ev->file, ev->line);
#else
    fprintf(ev->udata, "%s %-5s %s:%d: ",
            buf, level_strings[ev->level], ev->file, ev->line);
#endif
            
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fprintf(ev->udata, "\n");
    fflush(ev->udata);
}

// 文件回调函数
static void file_callback(log_Event* ev) {
    char buf[64];
    buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
    fprintf(ev->udata, "%s %-5s %s:%d: ",
            buf, level_strings[ev->level], ev->file, ev->line);
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fprintf(ev->udata, "\n");
    fflush(ev->udata);
}

// 初始化事件结构
static void init_event(log_Event* ev, void* udata) {
    if (!ev->time) {
        time_t t = time(NULL);
        ev->time = localtime(&t);
    }
    ev->udata = udata;
}

// 获取日志级别名称
const char* log_level_string(int level) { 
    return level_strings[level]; 
}

// 设置全局日志级别
void log_set_level(int level) { 
    L.level = level; 
}

// 启用/禁用默认stderr输出
void log_set_quiet(bool enable) { 
    L.quiet = enable; 
}

// 添加自定义回调
int log_add_callback(log_LogFn fn, void* udata, int level) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!L.callbacks[i].fn) {
            L.callbacks[i] = (Callback){fn, udata, level};
            return 0;
        }
    }
    return -1;
}

// 添加文件记录器
int log_add_fp(FILE* fp, int level) {
    return log_add_callback(file_callback, fp, level);
}

// 核心日志函数
void log_log(int level, const char* file, int line, const char* fmt, ...) {
    log_Event ev = {
        .fmt = fmt,
        .file = file,
        .line = line,
        .level = level,
    };

    // 默认stderr输出
    if (!L.quiet && level >= L.level) {
        init_event(&ev, stderr);
        va_start(ev.ap, fmt);
        stdout_callback(&ev);
        va_end(ev.ap);
    }

    // 调用所有注册的回调
    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        Callback* cb = &L.callbacks[i];
        if (level >= cb->level) {
            init_event(&ev, cb->udata);
            va_start(ev.ap, fmt);
            cb->fn(&ev);
            va_end(ev.ap);
        }
    }
}

#endif // LOG_IMPLEMENTATION
#endif // LOG_H