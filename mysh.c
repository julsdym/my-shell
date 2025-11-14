#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_TOKENS 256
#define MAX_PIPES 64
#define BUFFER_SIZE 1024

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_INPUT_REDIR,
    TOKEN_OUTPUT_REDIR,
    TOKEN_AND,
    TOKEN_OR
} TokenType;

typedef struct {
    char *value;
    TokenType type;
} Token;

typedef struct {
    char **args;
    int arg_count;
    char *input_file;
    char *output_file;
} Command;

int last_exit_status = 0;
int interactive_mode = 0;

// Function prototypes
void read_command(int fd, char *buffer, int *len);
int tokenize(char *line, Token **tokens);
int parse_command(Token *tokens, int token_count, Command **commands, int *is_pipeline, int *is_conditional, char *cond_type);
int execute_command(Command *commands, int cmd_count, int is_pipeline);
int execute_builtin(Command *cmd);
char *find_program(const char *name);
void free_tokens(Token *tokens, int count);
void free_commands(Command *commands, int count);
void print_error(const char *msg);

int main(int argc, char *argv[]) {
    int input_fd = STDIN_FILENO;
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [script_file]\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    if (argc == 2) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
    }
    
    interactive_mode = isatty(input_fd);
    
    if (interactive_mode) {
        printf("Welcome to my shell!\n");
    }
    
    char buffer[BUFFER_SIZE];
    int buf_len = 0;
    
    while (1) {
        if (interactive_mode) {
            printf("mysh> ");
            fflush(stdout);
        }
        
        read_command(input_fd, buffer, &buf_len);
        
        if (buf_len == 0) {
            break;
        }
        
        buffer[buf_len] = '\0';
    
        Token *tokens;
        int token_count = tokenize(buffer, &tokens);
        
        if (token_count == 0) {
            buf_len = 0;
            continue; 
        }
        
        Command *commands;
        int cmd_count;
        int is_pipeline = 0;
        int is_conditional = 0;
        char cond_type[4] = "";
        
        int parse_result = parse_command(tokens, token_count, &commands, &is_pipeline, &is_conditional, cond_type);
        
        if (parse_result < 0) {
            last_exit_status = 1;
            free_tokens(tokens, token_count);
            buf_len = 0;
            continue;
        }
        
        cmd_count = parse_result;
        
        if (is_conditional) {
            if (strcmp(cond_type, "and") == 0 && last_exit_status != 0) {
                free_tokens(tokens, token_count);
                free_commands(commands, cmd_count);
                buf_len = 0;
                continue;
            }
            if (strcmp(cond_type, "or") == 0 && last_exit_status == 0) {
                free_tokens(tokens, token_count);
                free_commands(commands, cmd_count);
                buf_len = 0;
                continue;
            }
        }
    
        if (cmd_count > 0 && commands[0].arg_count > 0) {
            if (strcmp(commands[0].args[0], "exit") == 0) {
                free_tokens(tokens, token_count);
                free_commands(commands, cmd_count);
                break;
            }
            if (strcmp(commands[0].args[0], "die") == 0) {
                for (int i = 1; i < commands[0].arg_count; i++) {
                    printf("%s", commands[0].args[i]);
                    if (i < commands[0].arg_count - 1) printf(" ");
                }
                if (commands[0].arg_count > 1) printf("\n");
                free_tokens(tokens, token_count);
                free_commands(commands, cmd_count);
                if (input_fd != STDIN_FILENO) close(input_fd);
                return EXIT_FAILURE;
            }
        }
        
        int result = execute_command(commands, cmd_count, is_pipeline);
        last_exit_status = (result == 0) ? 0 : 1;
        
        free_tokens(tokens, token_count);
        free_commands(commands, cmd_count);
        buf_len = 0;
    }
    
    if (interactive_mode) {
        printf("mysh: exiting\n");
    }
    
    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }
    
    return EXIT_SUCCESS;
}

void read_command(int fd, char *buffer, int *len) {
    *len = 0;
    char c;
    ssize_t n;
    
    while ((n = read(fd, &c, 1)) > 0) {
        buffer[*len] = c;
        (*len)++;
        
        if (c == '\n') {
            break;
        }
        
        if (*len >= BUFFER_SIZE - 1) {
            break;
        }
    }
}

int tokenize(char *line, Token **tokens) {
    Token *token_array = malloc(sizeof(Token) * MAX_TOKENS);
    int count = 0;
    int i = 0;
    int len = strlen(line);
    
    while (i < len) {
        while (i < len && isspace(line[i])) i++;
        
        if (i >= len) break;
        
        if (line[i] == '#') {
            break;
        }
        
        if (line[i] == '|') {
            token_array[count].value = strdup("|");
            token_array[count].type = TOKEN_PIPE;
            count++;
            i++;
            continue;
        }
        
        if (line[i] == '<') {
            token_array[count].value = strdup("<");
            token_array[count].type = TOKEN_INPUT_REDIR;
            count++;
            i++;
            continue;
        }
        
        if (line[i] == '>') {
            token_array[count].value = strdup(">");
            token_array[count].type = TOKEN_OUTPUT_REDIR;
            count++;
            i++;
            continue;
        }
        
        int start = i;
        while (i < len && !isspace(line[i]) && line[i] != '|' && line[i] != '<' && line[i] != '>' && line[i] != '#') {
            i++;
        }
        
        if (i > start) {
            int word_len = i - start;
            char *word = malloc(word_len + 1);
            strncpy(word, line + start, word_len);
            word[word_len] = '\0';
            
            if (strcmp(word, "and") == 0) {
                token_array[count].value = word;
                token_array[count].type = TOKEN_AND;
            } else if (strcmp(word, "or") == 0) {
                token_array[count].value = word;
                token_array[count].type = TOKEN_OR;
            } else {
                token_array[count].value = word;
                token_array[count].type = TOKEN_WORD;
            }
            count++;
        }
    }
    
    *tokens = token_array;
    return count;
}

int parse_command(Token *tokens, int token_count, Command **commands, int *is_pipeline, int *is_conditional, char *cond_type) {
    Command *cmd_array = malloc(sizeof(Command) * MAX_PIPES);
    int cmd_idx = 0;
    int i = 0;
    cmd_array[cmd_idx].args = malloc(sizeof(char*) * MAX_TOKENS);
    cmd_array[cmd_idx].arg_count = 0;
    cmd_array[cmd_idx].input_file = NULL;
    cmd_array[cmd_idx].output_file = NULL;
    
    *is_conditional = 0;
    if (token_count > 0 && (tokens[0].type == TOKEN_AND || tokens[0].type == TOKEN_OR)) {
        *is_conditional = 1;
        strcpy(cond_type, tokens[0].value);
        i = 1;
    }
    
    *is_pipeline = 0;
    
    while (i < token_count) {
        Token *t = &tokens[i];
        
        if (t->type == TOKEN_PIPE) {
            *is_pipeline = 1;
            cmd_idx++;
            cmd_array[cmd_idx].args = malloc(sizeof(char*) * MAX_TOKENS);
            cmd_array[cmd_idx].arg_count = 0;
            cmd_array[cmd_idx].input_file = NULL;
            cmd_array[cmd_idx].output_file = NULL;
            i++;
            continue;
        }
        
        if (t->type == TOKEN_INPUT_REDIR) {
            if (i + 1 >= token_count || tokens[i + 1].type != TOKEN_WORD) {
                fprintf(stderr, "Syntax error: expected filename after <\n");
                for (int j = 0; j <= cmd_idx; j++) {
                    free(cmd_array[j].args);
                }
                free(cmd_array);
                return -1;
            }
            cmd_array[cmd_idx].input_file = strdup(tokens[i + 1].value);
            i += 2;
            continue;
        }
        
        if (t->type == TOKEN_OUTPUT_REDIR) {
            if (i + 1 >= token_count || tokens[i + 1].type != TOKEN_WORD) {
                fprintf(stderr, "Syntax error: expected filename after >\n");
                for (int j = 0; j <= cmd_idx; j++) {
                    free(cmd_array[j].args);
                }
                free(cmd_array);
                return -1;
            }
            cmd_array[cmd_idx].output_file = strdup(tokens[i + 1].value);
            i += 2;
            continue;
        }
        
        if (t->type == TOKEN_WORD) {
            cmd_array[cmd_idx].args[cmd_array[cmd_idx].arg_count] = strdup(t->value);
            cmd_array[cmd_idx].arg_count++;
            i++;
            continue;
        }
        
        i++;
    }
    for (int j = 0; j <= cmd_idx; j++) {
        cmd_array[j].args[cmd_array[j].arg_count] = NULL;
    }
    
    *commands = cmd_array;
    return cmd_idx + 1;
}

int execute_builtin(Command *cmd) {
    if (strcmp(cmd->args[0], "cd") == 0) {
        if (cmd->arg_count != 2) {
            fprintf(stderr, "cd: wrong number of arguments\n");
            return -1;
        }
        if (chdir(cmd->args[1]) < 0) {
            perror("cd");
            return -1;
        }
        return 0;
    }
    
    if (strcmp(cmd->args[0], "pwd") == 0) {
        char cwd[BUFFER_SIZE];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
            return 0;
        } else {
            perror("pwd");
            return -1;
        }
    }
    
    if (strcmp(cmd->args[0], "which") == 0) {
        if (cmd->arg_count != 2) {
            return -1;
        }
        if (strcmp(cmd->args[1], "cd") == 0 || strcmp(cmd->args[1], "pwd") == 0 ||
            strcmp(cmd->args[1], "which") == 0 || strcmp(cmd->args[1], "exit") == 0 ||
            strcmp(cmd->args[1], "die") == 0) {
            return -1;
        }
        
        char *path = find_program(cmd->args[1]);
        if (path) {
            printf("%s\n", path);
            free(path);
            return 0;
        }
        return -1;
    }
    
    if (strcmp(cmd->args[0], "exit") == 0) {
        return 0;
    }
    
    return -1;
}

char *find_program(const char *name) {
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0) {
            return strdup(name);
        }
        return NULL;
    }
    const char *dirs[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    for (int i = 0; i < 3; i++) {
        char path[BUFFER_SIZE];
        snprintf(path, sizeof(path), "%s/%s", dirs[i], name);
        if (access(path, X_OK) == 0) {
            return strdup(path);
        }
    }
    
    return NULL;
}

int execute_command(Command *commands, int cmd_count, int is_pipeline) {
    if (cmd_count == 1 && !is_pipeline && commands[0].arg_count > 0) {
        const char *cmd_name = commands[0].args[0];
        if (strcmp(cmd_name, "cd") == 0 || strcmp(cmd_name, "pwd") == 0 ||
            strcmp(cmd_name, "which") == 0 || strcmp(cmd_name, "exit") == 0) {
            return execute_builtin(&commands[0]);
        }
    }
    
    int pipes[MAX_PIPES][2];
    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            return -1;
        }
    }
    
    pid_t pids[MAX_PIPES];
    int redirect_stdin_to_null = !interactive_mode && !isatty(STDIN_FILENO);
    
    for (int i = 0; i < cmd_count; i++) {
        Command *cmd = &commands[i];
        
        if (cmd->arg_count == 0) continue;
                char *prog_path = find_program(cmd->args[0]);
        if (!prog_path) {
            fprintf(stderr, "%s: command not found\n", cmd->args[0]);
            for (int j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return -1;
        }
        
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            free(prog_path);
            return -1;
        }
        
        if (pid == 0) {
            if (cmd->input_file) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd < 0) {
                    perror(cmd->input_file);
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            } else if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            } else if (redirect_stdin_to_null) {
                int null_fd = open("/dev/null", O_RDONLY);
                dup2(null_fd, STDIN_FILENO);
                close(null_fd);
            }
            if (cmd->output_file) {
                int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
                if (fd < 0) {
                    perror(cmd->output_file);
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            } else if (i < cmd_count - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            for (int j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            execv(prog_path, cmd->args);
            perror(prog_path);
            exit(EXIT_FAILURE);
        }
        
        pids[i] = pid;
        free(prog_path);
    }
    
    for (int i = 0; i < cmd_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    int last_status = 0;
    for (int i = 0; i < cmd_count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == cmd_count - 1) {
            last_status = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
        }
    }
    
    return last_status;
}

void free_tokens(Token *tokens, int count) {
    for (int i = 0; i < count; i++) {
        free(tokens[i].value);
    }
    free(tokens);
}

void free_commands(Command *commands, int count) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < commands[i].arg_count; j++) {
            free(commands[i].args[j]);
        }
        free(commands[i].args);
        if (commands[i].input_file) free(commands[i].input_file);
        if (commands[i].output_file) free(commands[i].output_file);
    }
    free(commands);
}
