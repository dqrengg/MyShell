
// myshell.c

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

// 指令数量
#define NUM_OF_CMD 19

// 指令编号
#define CMD_BG 1
#define CMD_CD 2
#define CMD_CLR 3
#define CMD_DIR 4
#define CMD_ECHO 5
#define CMD_EXEC 6
#define CMD_EXIT 7
#define CMD_FG 8
#define CMD_HELP 9
#define CMD_JOBS 10
#define CMD_PWD 11
#define CMD_SET 12
#define CMD_SHIFT 13
#define CMD_TEST 14
#define CMD_TIME 15
#define CMD_UMASK 16
#define CMD_UNSET 17
#define CMD_DECLARE 18
#define CMD_ERROR -1

// $0-$9长度
#define DOLLAR_ENV_NUM 10
#define DOLLAR_ENV_LEN 32

// 输入长度上限
#define MAXLINE 512
#define CMD_LEN 128
#define PIPE_NUM 20
#define ARG_LEN 32
#define MAX_ARG 20

// job上限
#define JOB_NUM 20

// 运行状态数量
#define STAT_NUM 5

// 运行状态
#define STAT_RUNNING 0
#define STAT_DONE 1
#define STAT_SUSPENDED 2
#define STAT_CONTINUED 3
#define STAT_TERMINATED 4

// =====================================================================

// job定义
typedef struct job job;
struct job
{
    int job_num;
    pid_t pid;
    int status;
    int is_fg;
    char cmd[MAXLINE];
};

// 环境变量
extern char **environ;

// 记录$0-$9
char *dollar_env[DOLLAR_ENV_NUM];

// 记录jobs
job *jobs_list[JOB_NUM];
int cur_job_num = 1;

// 信号处理函数
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);

// 函数定义
void init_shell(int argc, char *argv[]);
void handle_job(char *line);
int get_background_flag(char *cmd);
int add_job(pid_t pid, char *cmd, int fg);
void print_job_info(job *j);
void do_line(char *line);
void handle_pipe(char **cmds, int num);
int parse_pipe(char *line, char **cmds);
void handle_buildin_cmd(char *cmd);
int get_buildin_cmd(char *cmd);
void do_cmd(char *cmd);
void parse_space(char *str, char **parsed);
void handle_redirect(char **args);
int handle_env(char **args);
int get_cmd(char *cmd);
void handle_cmd(char **args);
void bg(char **args);
void cd(char **args);
void clr();
void dir(char **args);
void declare(char **args);
void echo(char **args);
void exec(char **args);
void my_exit();
void fg(char **args);
void help(char **args);
void jobs();
void pwd();
void set(char **args);
void shift(char **args);
void test();
void my_time();
void my_umask(char **args);
void unset(char **args);
void error_cmd(char **args);

// ======================================================================

// 程序入口
int main(int argc, char *argv[])
{
    // 初始化shell
    init_shell(argc, argv);

    // 注册信号
    // ctrl+c
    signal(SIGINT, sigint_handler);
    // ctrl+z
    signal(SIGTSTP, sigtstp_handler);
    signal(SIGCHLD, sigchld_handler);

    // 输入
    char line[MAXLINE];

    // 输出shell提示符
    char *buf = NULL;
    printf("myshell:%s> ", getcwd(buf, 0));
    free(buf);

    // 输入
    while (fgets(line, MAXLINE, stdin) != NULL)
    {
        // 替换换行符为'/0'
        int i = 0;
        while (line[i] != '\n')
            i++;
        line[i] = '\0';

        if (strlen(line))
        {
            // do_line(line);
            handle_job(line);
        }

        // 打印shell提示符
        char *buf = NULL;
        printf("myshell:%s> ", getcwd(buf, 0));
        free(buf);
    }
}

// 初始化shell
void init_shell(int argc, char *argv[])
{
    // 初始化环境变量$1-$9
    for (int i = 0; i < DOLLAR_ENV_NUM; i++)
    {
        dollar_env[i] = (char *)malloc(sizeof(char) * DOLLAR_ENV_LEN);
    }
    // 赋值环境变量$1-$9
    for (int i = 0; i < DOLLAR_ENV_NUM; i++)
    {
        if (i < argc)
        {
            strcpy(dollar_env[i], argv[i]);
        }
        else
        {
            strcpy(dollar_env[i], "\0");
        }
    }

    // 设置环境变量SHELL=$HOME/myshell
    char *pwd = NULL;
    char shell_path[64] = "";
    strcpy(shell_path, getcwd(pwd, 0));
    free(pwd);
    strcat(shell_path, "/myshell");
    setenv("shell", shell_path, 1);

    // 初始化jobs
    for (int i = 0; i < JOB_NUM; i++)
    {
        jobs_list[i] = NULL;
    }

    return;
}


// 处理job
void handle_job(char *line)
{
    // 检查是否为build in指令
    if (get_buildin_cmd(line))
    {
        handle_buildin_cmd(line);
    }
    else
    {
        // 检查是否背景执行
        int is_bg = get_background_flag(line);

        pid_t pid = fork();
        if (pid < 0)
        {
            printf("fork error\n");
            return;
        }
        else if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);

            do_line(line);
            
            exit(0);
        }
        else
        {
            // 背景执行
            if (is_bg)
            {
                int i = add_job(pid, line, 0);
                print_job_info(jobs_list[i]);
            }
            // 前景执行
            else
            {
                add_job(pid, line, 1);
                waitpid(pid, NULL, 0);
            }
        }
    }
    
    return;
}

// 检查是否为背景作业
int get_background_flag(char *cmd)
{
    char *pos = strchr(cmd, '&');
    if (pos != NULL)
    {
        // 更改'&'为' '
        *pos = ' ';
        return 1;
    }

    return 0;
}

// 新增job
int add_job(pid_t pid, char *cmd, int fg)
{
    for (int i = 0; i < JOB_NUM; i++)
    {
        if (jobs_list[i] == NULL)
        {
            job *new_job = (job *)malloc(sizeof(job));
            new_job->job_num = cur_job_num++;
            new_job->pid = pid;
            new_job->status = STAT_RUNNING;
            new_job->is_fg = fg;
            strcpy(new_job->cmd, cmd);

            jobs_list[i] = new_job;

            return i;
        }
    }

    printf("too many jobs\n");

    return 0;
}

// 打印job信息
void print_job_info(job *j)
{
    // job状态
    char *stat[STAT_NUM];
    stat[STAT_RUNNING] = "RUNNING";
    stat[STAT_DONE] = "DONE";
    stat[STAT_SUSPENDED] = "SUSPENDED";
    stat[STAT_CONTINUED] = "CONTINUED";
    stat[STAT_TERMINATED] = "TERMINATED";

    printf("[%d]\t%s\t\t%s\n", j->job_num, stat[j->status], j->cmd);
}

// 处理整行
void do_line(char *line)
{
    // 单个指令
    char *cmds[PIPE_NUM];
    for (int i = 0; i < PIPE_NUM; i++)
    {
        cmds[i] = (char *)malloc(sizeof(char) * CMD_LEN);
        strcpy(cmds[i], "\0");
    }

    // 分割管道
    int n = parse_pipe(line, cmds);
    // 执行管道
    handle_pipe(cmds, n);

    // 释放内存
    for (int i = 0; i < PIPE_NUM; i++)
    {
        free(cmds[i]);
    }
    
    return;
}

// 处理build in指令
void handle_buildin_cmd(char *cmd)
{
    // 参数
    char *args[MAX_ARG];
    for (int i = 0; i < MAX_ARG; i++)
    {
        args[i] = (char *)malloc(sizeof(char) * ARG_LEN);
        strcpy(args[i], "\0");
    }

    // 分割参数
    parse_space(cmd, args);
    // 环境变量替换
    if (handle_env(args)) return;
    // 运行指令
    handle_cmd(args);

    // 释放内存
    for (int i = 0; i < MAX_ARG; i++)
    {
        free(args[i]);
    }

    return;
}

// 识别build in指令
int get_buildin_cmd(char *cmd)
{
    char cmd_copy[CMD_LEN];
    strcpy(cmd_copy, cmd);
    char *args0 = strtok(cmd_copy, " ");

    // build in指令
    char *buildin_cmds[10];
    buildin_cmds[0] = "bg";
    buildin_cmds[1] = "cd";
    buildin_cmds[2] = "declare";
    buildin_cmds[3] = "clr";
    buildin_cmds[4] = "exit";
    buildin_cmds[5] = "fg";
    buildin_cmds[6] = "set";
    buildin_cmds[7] = "shift";
    buildin_cmds[8] = "umask";
    buildin_cmds[9] = "unset";

    // 逐个比较
    for (int i = 0; i < 10; i++)
    {
        if (strcmp(args0, buildin_cmds[i]) == 0)
        {
            return 1;
        }
    }

    return 0;
}

// 分割管道
int parse_pipe(char *line, char **cmds)
{
    int i = 0;
    char s[2] = "|";
    char *ptr;

    ptr = strtok(line, s);
    while (ptr)
    {
        strcpy(cmds[i++], ptr);
        ptr = strtok(NULL, s);
    }
    return i;
}

// 执行管道
void handle_pipe(char **cmds, int num)
{
    int pipe_fd[PIPE_NUM+1][2];

    // 创建管道
    for (int i = 0; i < num; i++)
    {
        if (pipe(pipe_fd[i]))
        {
            printf("pipe error\n");
            return;
        }
    }

    for (int i = 0; i < num; i++)
    {
        // 创建子进程运行指令
        pid_t pid = fork();
        if (pid < 0)
        {
            printf("fork error\n");
            return;
        }
        // 子进程
        else if (pid == 0)
        {
            // 检查管道是否结束
            if (i != (num-1))
            {
                dup2(pipe_fd[i][1], STDOUT_FILENO);
            }
            if (i != 0)
            {
                dup2(pipe_fd[i-1][0], STDIN_FILENO);
            }
            
            // 关闭文件
            for (int j = 0; j < num; j++)
            {
                close(pipe_fd[j][0]);
                close(pipe_fd[j][1]);
            }

            // 处理指令
            do_cmd(cmds[i]);

            exit(0);
        }
        else
        {
            wait(NULL);
        }
    }

    // 关闭文件
    for (int j = 0; j < num; j++)
    {
        close(pipe_fd[j][0]);
        close(pipe_fd[j][1]);
    }

    return;
}

// 处理指令
void do_cmd(char *cmd)
{
    // 指令参数声明，分配内存
    char *args[MAX_ARG];
    for (int i = 0; i < MAX_ARG; i++)
    {
        args[i] = (char *)malloc(sizeof(char) * ARG_LEN);
        strcpy(args[i], "\0");
    }

    // 分割参数
    parse_space(cmd, args);
    // 环境变量替换
    if (handle_env(args)) return;
    // 重定向处理
    handle_redirect(args);
    // 运行指令
    handle_cmd(args);

    // 释放内存
    for (int i = 0; i < MAX_ARG; i++)
    {
        free(args[i]);
    }

    return;
}

// 按空格分割
void parse_space(char *str, char **parsed)
{
    int i = 0;
    char s[2] = " ";
    char *ptr;

    ptr = strtok(str, s);
    while (ptr)
    {
        strcpy(parsed[i++], ptr);
        ptr = strtok(NULL, s);
    }

    // for (int i = 0; strlen(parsed[i]); i++)
    // {
    //     printf("%s\n", parsed[i]);
    // }

    return;
}

// 重定向处理
void handle_redirect(char **args)
{
    // 重定向文件名
    char input_file[ARG_LEN];
    char output_file[ARG_LEN];

    // 查找有无重定向
    for (int i = 0; strlen(args[i]), i < MAX_ARG; i++)
    {
        // 输入重定向
        if (strcmp(args[i], "<") == 0)
        {
            strcpy(input_file, args[i+1]);
            int fd = open(input_file, O_RDONLY, 0);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        // 输出重定向
        else if (strcmp(args[i], ">") == 0)
        {
            strcpy(output_file, args[i+1]);
            int fd = open(output_file, 
                            O_CREAT | O_TRUNC | O_WRONLY, 
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        // 输出重定向
        else if (strcmp(args[i], ">>") == 0)
        {
            strcpy(output_file, args[i+1]);
            int fd = open(output_file, 
                            O_CREAT | O_APPEND | O_WRONLY, 
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        // 无需移位参数
        else
        {
            continue;
        }

        // 把重定向符从参数去掉
        for (int j = i; j < (MAX_ARG-2); j++)
        {
            strcpy(args[j], args[j+2]);
        }
    }

    return;
}

// 环境变量处理
int handle_env(char **args)
{
    for (int i = 0; i < MAX_ARG; i++)
    {
        if (*args[i] == '$')
        {
            int result = atoi(args[i] + 1);
            // $1-$9
            if (result > 0 && result < DOLLAR_ENV_NUM)
            {
                strcpy(args[i], dollar_env[result]);
            }
            else if (result == 0)
            {
                char *value = getenv(args[i] + 1);
                //printf("%s\n", value);

                // $0
                if (strcmp(args[i] + 1, "0") == 0)
                {
                    strcpy(args[i], dollar_env[0]);
                }
                // 环境变量
                else if (value != NULL)
                {
                    strcpy(args[i], value);
                }
                // 错误处理
                else
                {
                    // printf("value: %s\n" ,value);
                    // printf("arg: %s\n", args[i] + 1);
                    printf("error environ variable \"%s\"\n", args[i]);
                    return 1;
                }
            }
        }
    }

    return 0;
}

// 获取指令编号
int get_cmd(char *cmd)
{
    int cmd_num = CMD_ERROR;

    // 指令
    char *cmd_list[NUM_OF_CMD];
    cmd_list[CMD_BG] = "bg";
    cmd_list[CMD_CD] = "cd";
    cmd_list[CMD_CLR] = "clr";
    cmd_list[CMD_DIR] = "dir";
    cmd_list[CMD_ECHO] = "echo";
    cmd_list[CMD_EXEC] = "exec";
    cmd_list[CMD_EXIT] = "exit";
    cmd_list[CMD_FG] = "fg";
    cmd_list[CMD_HELP] = "help";
    cmd_list[CMD_JOBS] = "jobs";
    cmd_list[CMD_PWD] = "pwd";
    cmd_list[CMD_SET] = "set";
    cmd_list[CMD_SHIFT] = "shift";
    cmd_list[CMD_TEST] = "test";
    cmd_list[CMD_TIME] = "time";
    cmd_list[CMD_UMASK] = "umask";
    cmd_list[CMD_UNSET] = "unset";
    cmd_list[CMD_DECLARE] = "declare";

    // 逐个比较
    for (int i = 1; i < NUM_OF_CMD; i++)
    {
        if (strcmp(cmd, cmd_list[i]) == 0)
        {
            cmd_num = i;
            break;
        }
    }

    return cmd_num;
}

// 处理指令
void handle_cmd(char **args)
{
    // 根据指令执行
    switch (get_cmd(args[0]))
    {
    case CMD_BG:
        bg(args);
        break;
    case CMD_CD:
        cd(args);
        break;
    case CMD_CLR:
        clr();
        break;
    case CMD_DIR:
        dir(args);
        break;
    case CMD_DECLARE:
        declare(args);
        break;
    case CMD_ECHO:
        echo(args);
        break;
    case CMD_EXEC:
        exec(args);
        break;
    case CMD_EXIT:
        my_exit();
        break;
    case CMD_FG:
        fg(args);
        break;
    case CMD_HELP:
        help(args);
        break;
    case CMD_JOBS:
        jobs();
        break;
    case CMD_PWD:
        pwd();
        break;
    case CMD_SET:
        set(args);
        break;
    case CMD_SHIFT:
        shift(args);
        break;
    case CMD_TEST:
        test();
        break;
    case CMD_TIME:
        my_time();
        break;
    case CMD_UMASK:
        my_umask(args);
        break;
    case CMD_UNSET:
        unset(args);
        break;
    case CMD_ERROR:
    default:
        error_cmd(args);
        break;
    }

    return;
}

// bg指令
void bg(char **args)
{
    int i;
    int job_num;
    pid_t pid = 0;

    // job number
    if (args[1][0] = '%')
    {
        job_num = atoi(args[1]+1);
        for (i = 0; i < JOB_NUM; i++)
        {
            if (jobs_list[i] != NULL)
            {
                if (jobs_list[i]->job_num == job_num)
                {
                    pid = jobs_list[i]->pid;
                    break;
                }
            }
        }

        if (pid == 0)
        {
            printf("bg: error job number: %s\n", args[0]+1);
            return;
        }
    }
    // pid
    else
    {
        if ((pid = atoi(args[1])) == 0)
        {
            printf("bg: error pid: %s\n", args[0]);
            return;
        }

        int flag = 0;
        for (i = 0; i < JOB_NUM; i++)
        {
            if (jobs_list[i] != NULL)
            {
                if (jobs_list[i]->pid == pid)
                {
                    flag = 1;
                    break;
                }
            }
        }

        if (flag == 0)
        {
            printf("bg: process didn't exist, pid: %s\n", args[0]);
            return;
        }
    }

    // 发送信号
    if (kill(-pid, SIGCONT) < 0)
    {
        printf("bg: send SINCONT error, pid: %d\n", pid);
        return;
    }

    jobs_list[i]->status = STAT_CONTINUED;
    print_job_info(jobs_list[i]);

    return;
}

// cd 指令
void cd(char **args)
{
    if (strlen(args[1]) == 0)
    {
        return;
    }
    if (chdir(args[1]) != 0)
    {
        printf("Can't find \"%s\" directory\n", args[1]);
    }
    return;
}

// clr指令
void clr()
{
    printf("\033[H\033[J");
    return;
}

// dir指令
void dir(char **args)
{
    DIR *dp;
    struct dirent *entry;
    char *dir_name = strlen(args[1]) ? args[1] : ".";

    // 打开文件夹
    if ((dp = opendir(dir_name)) == NULL)
    {
        printf("Can't find \"%s\" directory\n", args[1]);
        return;
    }

    // 循环打印文件名
    while ((entry = readdir(dp)) != NULL)
    {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            printf("%s\n", entry->d_name);
        }
    }

    // 关闭文件夹
    closedir(dp);

    return;
}

// declare指令
void declare(char **args)
{
    // 循环新增变量
    for (int i = 1; strlen(args[i]) != 0 && i < MAX_ARG; i++)
    {
        //printf("arg: %s\n", args[i]);
        char *name = (char *)malloc(sizeof(char) * ARG_LEN);
        // 变量名
        name = strtok(args[i], "=");
        char *value = (char *)malloc(sizeof(char) * ARG_LEN);
        // 变量值
        value = strtok(NULL, "=");
        // 设置变量
        if (setenv(name, value, 1) == -1)
        {
            printf("declare: error argument \"%s=%s\"\n", name, value);
            break;
        }
    }

    return;
}

// echo指令
void echo(char **args)
{
    // 循环打印
    for (int i = 1; strlen(args[i]); i++)
    {
        printf("%s ", args[i]);
    }
    printf("\n");

    return;
}

// exec指令
void exec(char **args)
{
    // 新增parent环境变量
    char *pwd = NULL;
    char shell_path[64] = "";
    strcpy(shell_path, getcwd(pwd, 0));
    free(pwd);
    strcat(shell_path, "/myshell");
    setenv("parent", shell_path, 1);

    // 准备参数
    char *argv[MAX_ARG];
    for (int i = 0; i < MAX_ARG - 1; i++)
    {
        if (strlen(args[i + 1]))
        {
            argv[i] = args[i + 1];
        }
        else
        {
            argv[i] = NULL;
        }
    }
    argv[MAX_ARG] = NULL;

    // 调用execvp
    if (execvp(argv[0], argv) == -1)
    {
        printf("exec: execvp error\n");
    }

    return;
}

// exit指令
void my_exit()
{
    exit(0);
}

// fg指令
void fg(char **args)
{
    int i;
    int job_num;
    pid_t pid = 0;

    // job number
    if (args[1][0] = '%')
    {
        job_num = atoi(args[1]+1);
        for (i = 0; i < JOB_NUM; i++)
        {
            if (jobs_list[i] != NULL)
            {
                if (jobs_list[i]->job_num == job_num)
                {
                    pid = jobs_list[i]->pid;
                    break;
                }
            }
        }

        if (pid == 0)
        {
            printf("fg: error job number: %s\n", args[0]+1);
            return;
        }
    }
    // pid
    else
    {
        if ((pid = atoi(args[1])) == 0)
        {
            printf("fg: error pid: %s\n", args[0]);
            return;
        }

        int flag = 0;
        for (i = 0; i < JOB_NUM; i++)
        {
            if (jobs_list[i] != NULL)
            {
                if (jobs_list[i]->pid == pid)
                {
                    flag = 1;
                    break;
                }
            }
        }

        if (flag == 0)
        {
            printf("fg: process didn't exist, pid: %s\n", args[0]);
            return;
        }
    }

    // 继续执行
    if (jobs_list[i]->status == STAT_SUSPENDED)
    {
        // 发送信号
        if (kill(-pid, SIGCONT) < 0)
        {
            printf("fg: send SINCONT error, pid: %d\n", pid);
            return;
        }

        // 更改job信息
        jobs_list[i]->status = STAT_CONTINUED;
    }

    jobs_list[i]->is_fg = 1;
    print_job_info(jobs_list[i]);

    // 等待子进程
    waitpid(pid, NULL, 0);

    return;
}

// help指令
void help(char **args)
{
    // 总览
    if (strlen(args[1]) == 0)
    {
        printf("myshell by dqrengg\n");
        printf("support command:\n");
        printf("\tbg\n");
        printf("\tcd\n");
        printf("\tclr\n");
        printf("\tdir\n");
        printf("\tdeclare\n");
        printf("\techo\n");
        printf("\texec\n");
        printf("\texit\n");
        printf("\tfg\n");
        printf("\tjobs\n");
        printf("\tpwd\n");
        printf("\tset\n");
        printf("\tshift\n");
        printf("\ttest\n");
        printf("\ttime\n");
        printf("\tumask\n");
        printf("\tunset\n");
        printf("use \"help [cmd]\" to get more info\n");
    }
    // 指令帮助
    else
    {
        switch (get_cmd(args[1]))
        {
            case CMD_BG:
                printf("usage: bg <pid>\n");
                printf("move <pid> to background\n");
                break;
            case CMD_CD:
                printf("usage: cd <dir>\n");
                printf("change directory to <dir>\n");
                break;
            case CMD_CLR:
                printf("usage: clr\n");
                printf("clear screen\n");
                break;
            case CMD_DIR:
                printf("usage: dir <dir>\n");
                printf("list file in <dir>\n");
                break;
            case CMD_DECLARE:
                printf("usage: declare <name=value>\n");
                printf("declare a environ variable\n");
                break;
            case CMD_ECHO:
                printf("usage: echo <string>\n");
                printf("print <string> on screen\n");
                break;
            case CMD_EXEC:
                printf("uasge: exec <proc> [args...]\n");
                printf("execute <proc> with arguments\n");
                break;
            case CMD_EXIT:
                printf("usage: exit\n");
                printf("exit shell\n");
                break;
            case CMD_FG:
                printf("usage: fg <pid>\n");
                printf("move <pid> to front ground\n");
                break;
            case CMD_HELP:
                printf("usage: help [cmd]\n");
                printf("show help page\n");
                break;
            case CMD_JOBS:
                printf("uasge: jobs\n");
                printf("show jobs list\n");
                break;
            case CMD_PWD:
                printf("uasge: pwd\n");
                printf("show current work directory\n");
                break;
            case CMD_SET:
                printf("usage: set [var...]\n");
                printf("show all environ variables or set $1-$9 with <var>\n");
                break;
            case CMD_SHIFT:
                printf("uasge: shift [t]\n");
                printf("shift $1-$9 [t] times\n");
                break;
            case CMD_TEST:
                printf("test <exp>\n");
                printf("test <exp> value\n");
                break;
            case CMD_TIME:
                printf("uasge: time\n");
                printf("show system time\n");
                break;
            case CMD_UMASK:
                printf("usage: umask [mask]\n");
                printf("set new mask with [mask]\n");
                break;
            case CMD_UNSET:
                printf("usage: unset [var]\n");
                printf("unset environ variable\n");
                break;
            case CMD_ERROR:
            default:
                printf("help: error command\n");
                break;
        }
    }
}

// jobs指令
void jobs()
{
    // 循环打印
    for (int i = 0; i < JOB_NUM; i++)
    {
        if (jobs_list[i])
        {
            print_job_info(jobs_list[i]);
        }
    }
    return;
}

// pwd指令
void pwd()
{
    // 调用getcwd()
    char *buf = NULL;
    printf("%s\n", getcwd(buf, 0));
    free(buf);

    return;
}

// set指令
void set(char **args)
{
    // 无参数打印全部环境变量
    if (strlen(args[1]) == 0)
    {
        for (int i = 0; environ[i] != NULL; i++)
        {
            printf("%s\n", environ[i]);
        }
    }
    // 有参数更新$1-$9
    else
    {
        for (int i = 1; i < DOLLAR_ENV_NUM; i++)
        {
            strcpy(dollar_env[i], args[i]);
        }
    }

    return;
}

// shift指令
void shift(char **args)
{
    int time;

    // 无参数等于shift 1
    if (strlen(args[1]) == 0)
    {
        time = 1;
    }
    else if ((time = atoi(args[1])) == 0)
    {
        printf("set: error argument \"%s\"\n", args[1]);
        return;
    }

    // 平移$1-$9
    for (int j = 0; j < time; j++)
    {
        for (int i = 1; i < DOLLAR_ENV_NUM-1; i++)
        {
            strcpy(dollar_env[i], dollar_env[i+1]);
        }
        strcpy(dollar_env[DOLLAR_ENV_NUM - 1], "\0");
    }
}


// test指令暂不支持
void test()
{
    printf("true\n");
    return;
}

// time指令
void my_time()
{
    time_t timep;
    time(&timep);
    printf("%s", ctime(&timep));
    return;
}

// umask指令
void my_umask(char **args)
{
    // 无参数，显示目前mask
    if (strlen(args[1]) == 0)
    {
        unsigned int mask;
        umask((mask = umask(0)));
        printf("%04o\n", mask);
    }
    // 有参数，更新mask
    else
    {
        unsigned int mask = 0;
        int i = 0;
        while (args[1][i] != '\0')
        {
            mask = mask * 8 + (args[1][i] - '0');
            i++;
        }

        umask(mask);
    }

    return;
}

// unset指令
void unset(char **args)
{
    // 调用unsetenv
    for (int i = 0; strlen(args[i]) && i < MAX_ARG; i++)
    {
        if (unsetenv(args[1]))
        {
            printf("set: error argument \"%s\"\n", args[1]);
        }
    }

    return;
}

// 错误处理
void error_cmd(char **args)
{
    printf("Command \"%s\" not found\n", args[0]);
    return;
}

// SIGCHLD信号处理
void sigchld_handler(int sig)
{
    pid_t pid;
    int status;

    if ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        // printf("get SIGCHLD pid: %d\n", pid);

        if (WIFSTOPPED(status))
        {
            for (int i = 0; i < JOB_NUM; i++)
            {
                if (jobs_list[i] != NULL)
                {
                    if (jobs_list[i]->pid == pid)
                    {
                        jobs_list[i]->status = STAT_SUSPENDED;
                        jobs_list[i]->is_fg = 0;
                        return;
                    }
                }
            }
        }
        else
        {
            for (int i = 0; i < JOB_NUM; i++)
            {
                if (jobs_list[i] != NULL)
                {
                    if (jobs_list[i]->pid == pid)
                    {
                        free(jobs_list[i]);
                        jobs_list[i] = NULL;
                        return;
                    }
                }
            }
        }
    }

    return;
}

// SIGTSTP信号处理
void sigtstp_handler(int sig)
{
    printf("\n");

    for (int i = 0; i < JOB_NUM; i++)
    {
        if (jobs_list[i] != NULL)
        {
            if (jobs_list[i]->is_fg)
            {
                jobs_list[i]->status = STAT_SUSPENDED;
                jobs_list[i]->is_fg = 0;
                kill(-(jobs_list[i]->pid), sig);
                return;
            }
        }
    }

    return;
}

// SININT信号处理
void sigint_handler(int sig)
{
    printf("\n");

    for (int i = 0; i < JOB_NUM; i++)
    {
        if (jobs_list[i] != NULL)
        {
            if (jobs_list[i]->is_fg)
            {
                jobs_list[i]->status = STAT_TERMINATED;
                jobs_list[i]->is_fg = 0;
                kill(jobs_list[i]->pid, sig);
                return;
            }
        }
    }
    return;
}

// SIGQUIT信号处理
void sigquit_handler(int sig)
{
    return;
}