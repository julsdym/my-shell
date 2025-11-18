#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <glob.h>


#define true 1
#define false 0
#define MAX_COMMANDS 16


#define STATUS_EXIT 200
#define STATUS_DIE 201
typedef struct Command {
    char *path;
    char **args;
    int arg_length;
    int fd_in;
    int fd_out;
} Command;


volatile int active = 1;
void handle() {
    active = 0;
}
void install_handlers() {
    struct sigaction act;
    act.sa_handler = handle;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, NULL);
}
Command *newCommand() {
    Command *cmd = malloc(sizeof(Command));
    cmd->path = calloc(1, BUFSIZ);
    cmd->args = NULL;
    cmd->arg_length = 0;
    cmd->fd_in = STDIN_FILENO;
    cmd->fd_out = STDOUT_FILENO;
    return cmd;
}
void freeCommand(Command *cmd) {
    if (cmd->args != NULL) {
        for (int i = 0; i < cmd->arg_length; i++)
            free(cmd->args[i]);
        free(cmd->args);
    }
    free(cmd->path);
    free(cmd);
}
void freeCommandList(Command **cmds, int length) {
    for (int i = 0; i < length; i++)
        freeCommand(cmds[i]);
}
char **add_to_args(Command *cmd, char **args, int *len, char *token) {
    if (cmd->path[0] == '\0') {
        strcpy(cmd->path, token);
        free(token);
        return args;
    }
    (*len)++;
    args = realloc(args, (*len) * sizeof(char*));
    args[*len - 1] = token;
    return args;
}
int try_execute(char *path, Command *cmd) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {


            char **exec_args = malloc((cmd->arg_length + 2) * sizeof(char*));
            exec_args[0] = cmd->path;
            for (int i = 0; i < cmd->arg_length; i++)
                exec_args[i + 1] = cmd->args[i];
            exec_args[cmd->arg_length + 1] = NULL;


            execv(path, exec_args);
            perror("execv");
            exit(1);
        }
        perror("file not executable");
        exit(1);
    }
    return -1;
}
int non_built_in(Command *cmd) {
    if (strchr(cmd->path, '/'))
        return try_execute(cmd->path, cmd) == 0 ? 0 : 1;


    const char *dirs[] = {
        "/usr/local/bin/",
        "/usr/bin/",
        "/bin/"
    };


    char path[PATH_MAX];
    for (int i = 0; i < 3; i++) {
        strcpy(path, dirs[i]);
        strcat(path, cmd->path);
        if (try_execute(path, cmd) == 0)
            return 0;
    }
    perror("command not found");
    exit(1);
}


int execute_command(Command *cmd) {
    if (cmd->path[0] == '\0')
        return 0;


    if (!strcmp(cmd->path, "pwd")) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            write(cmd->fd_out, cwd, strlen(cwd));
            write(cmd->fd_out, "\n", 1);
            if (cmd->fd_out != STDOUT_FILENO) close(cmd->fd_out);
            return 0;
        }
        perror("pwd");
        return 1;
    }


    if (!strcmp(cmd->path, "cd")) {
        if (cmd->arg_length > 1) return 1;


        if (cmd->arg_length == 0) {
            char *h = getenv("HOME");
            return (h && chdir(h) == 0) ? 0 : (perror("cd"), 1);
        }
        return (chdir(cmd->args[0]) == 0) ? 0 : (perror("cd"), 1);
    }


    if (!strcmp(cmd->path, "which")) {
        if (cmd->arg_length != 1) return 1;


        const char *t = cmd->args[0];
        if (!strcmp(t, "cd") || !strcmp(t, "pwd") ||
            !strcmp(t, "which") || !strcmp(t, "exit") ||
            !strcmp(t, "die"))
            return 1;


        const char *dirs[] = {"/usr/local/bin/", "/usr/bin/", "/bin/"};
        char p[PATH_MAX];
        struct stat st;


        for (int i = 0; i < 3; i++) {
            strcpy(p, dirs[i]);
            strcat(p, t);
            if (stat(p, &st) == 0 && (st.st_mode & 0111)) {
                write(cmd->fd_out, p, strlen(p));
                write(cmd->fd_out, "\n", 1);
                return 0;
            }
        }
        return 1;
    }


    if (!strcmp(cmd->path, "exit"))
        return STATUS_EXIT;


    if (!strcmp(cmd->path, "die")) {
        for (int i = 0; i < cmd->arg_length; i++) {
            write(cmd->fd_out, cmd->args[i], strlen(cmd->args[i]));
            if (i + 1 != cmd->arg_length) write(cmd->fd_out, " ", 1);
        }
        write(cmd->fd_out, "\n", 1);
        return STATUS_DIE;
    }


    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }


    if (pid == 0) {
        if (!isatty(STDIN_FILENO) && cmd->fd_in == STDIN_FILENO) {
            int dn = open("/dev/null", O_RDONLY);
            if (dn >= 0) dup2(dn, STDIN_FILENO);
        }


        if (cmd->fd_in != STDIN_FILENO) {
            dup2(cmd->fd_in, STDIN_FILENO);
            close(cmd->fd_in);
        }
        if (cmd->fd_out != STDOUT_FILENO) {
            dup2(cmd->fd_out, STDOUT_FILENO);
            close(cmd->fd_out);
        }


        non_built_in(cmd);
        exit(1);
    }


    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
int main(int argc, char **argv) {
    install_handlers();
    if (argc == 1)
        write(1, "\e[1;92mWelcome to my shell!\n", 27);


    char c, buf[BUFSIZ];
    int bytes, pos, command_count = 0;
    int inR = 0, outR = 0, esc = 0,  comment = 0;
    int first = 0, cond = 0, last = 0;


    if (argc > 1) {
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }


    while (active) {
        if (argc == 1) {
            write(1, "\e[1;95mmysh>\x1b[0m ", 16);
        }


        pos = comment = first = cond = 0;
        inR = outR = esc = 0;


        Command *cmd = newCommand();
        Command *list[MAX_COMMANDS];
        list[command_count++] = cmd;


        while ((bytes = read(0, &c, 1)) >= 0) {


            if (c == '#' && !esc && !comment) {
                comment = 1;
                continue;
            }
            if (comment && c != '\n' && bytes != 0)
                continue;


            if ((c==' '||c=='\n'||c=='<'||c=='>'||c=='|'||bytes==0) && !esc) {


                if (pos == 0 && bytes != 0 && c != '\n') {
                    if (c=='<') inR=1;
                    else if (c=='>') outR=1;
                    else if (c=='|') {
                        int fd[2]; pipe(fd);
                        list[command_count-1]->fd_out = fd[1];
                        cmd = newCommand();
                        cmd->fd_in = fd[0];
                        list[command_count++] = cmd;
                    }
                    continue;
                }


                buf[pos] = '\0';


                if (inR) {
                    cmd->fd_in = open(buf, O_RDONLY);
                    if (cmd->fd_in<0) perror("open");
                    inR=0;
                }
                else if (outR) {
                    cmd->fd_out = open(buf,O_WRONLY|O_CREAT|O_TRUNC,0640);
                    if (cmd->fd_out<0) perror("open");
                    outR=0;
                }
                else {


                    if (!first) {
                        if (!strcmp(buf,"and")) { cond=1; first=1; goto skip; }
                        if (!strcmp(buf,"or"))  { cond=2; first=1; goto skip; }
                        first = 1;
                    }


                    char *arg = strdup(buf);
                    cmd->args = add_to_args(cmd, cmd->args, &cmd->arg_length, arg);
                }


skip:
                if (c=='<') inR=1;
                else if (c=='>') outR=1;
                else if (c=='|') {
                    int fd[2]; pipe(fd);
                    list[command_count-1]->fd_out = fd[1];
                    cmd = newCommand();
                    cmd->fd_in = fd[0];
                    list[command_count++] = cmd;
                }


                if (c=='\n' || bytes==0) {


                    int run = 1;
                    if (cond==1 && last!=0) run=0;
                    if (cond==2 && last==0) run=0;


                    if (run) {
                        int status = 0;
                        for (int i=0;i<command_count;i++) {
                            int r = execute_command(list[i]);


                            if (r == STATUS_EXIT) {
                                freeCommandList(list, command_count);
                                goto end;
                            }
                            if (r == STATUS_DIE) {
                                freeCommandList(list, command_count);
                                return 1;
                            }
                            if (r != 0) status = 1;


                            if (list[i]->fd_in!=STDIN_FILENO) close(list[i]->fd_in);
                            if (list[i]->fd_out!=STDOUT_FILENO) close(list[i]->fd_out);
                        }
                        last = status;
                    }


                    break;
                }


                memset(buf,0,sizeof(buf));
                pos = 0;
            }
            else if (c=='\\' && !esc) {
                esc = 1;


            }
            else if (c!='\n') {
                buf[pos++] = c;
                esc = 0;
            }
        }


        freeCommandList(list, command_count);
        command_count = 0;
    }


end:
    if (argc == 1)
        write(1, "\e[1;91mmysh: exiting\n", 19);


    return 0;
}










