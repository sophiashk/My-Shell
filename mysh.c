#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_TOKENS 256
#define MAX_TOKEN_LEN 1024
#define BUFFER_SIZE 4096

typedef struct {
    char **items;
    size_t size;
    size_t capacity;
} ArrayList;

ArrayList* arraylist_create() {
    ArrayList *list = malloc(sizeof(ArrayList));
    list->capacity = 16;
    list->size = 0;
    list->items = malloc(sizeof(char*) * list->capacity);
    return list;
}

void arraylist_add(ArrayList *list, char *item) {
    if (list->size >= list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, sizeof(char*) * list->capacity);
    }
    list->items[list->size++] = item;
}

void arraylist_free(ArrayList *list) {
    for (size_t i = 0; i < list->size; i++) {
        free(list->items[i]);
    }
    free(list->items);
    free(list);
}

typedef struct {
    ArrayList *args;
    char *input_file;
    char *output_file;
} Command;

typedef struct {
    Command **commands;
    int num_commands;
    int is_conditional;  // 0=none, 1=and, 2=or
} Job;

Command* command_create() {
    Command *cmd = malloc(sizeof(Command));
    cmd->args = arraylist_create();
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    return cmd;
}

void command_free(Command *cmd) {
    arraylist_free(cmd->args);
    free(cmd->input_file);
    free(cmd->output_file);
    free(cmd);
}

Job* job_create() {
    Job *job = malloc(sizeof(Job));
    job->commands = malloc(sizeof(Command*) * MAX_TOKENS);
    job->num_commands = 0;
    job->is_conditional = 0;
    return job;
}

void job_free(Job *job) {
    for (int i = 0; i < job->num_commands; i++) {
        command_free(job->commands[i]);
    }
    free(job->commands);
    free(job);
}

char* find_program(const char *name) {
    if (strchr(name, '/')) {
        return strdup(name);
    }
    
    const char *paths[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    for (int i = 0; i < 3; i++) {
        char *full_path = malloc(strlen(paths[i]) + strlen(name) + 2);
        sprintf(full_path, "%s/%s", paths[i], name);
        if (access(full_path, X_OK) == 0) {
            return full_path;
        }
        free(full_path);
    }
    return NULL;
}

int is_builtin(const char *name) {
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0 || 
           strcmp(name, "which") == 0 || strcmp(name, "exit") == 0 || 
           strcmp(name, "die") == 0;
}

int execute_builtin(Command *cmd, int *should_exit) {
    char *name = cmd->args->items[0];
    
    if (strcmp(name, "cd") == 0) {
        if (cmd->args->size != 2) {
            fprintf(stderr, "cd: wrong number of arguments\n");
            return 1;
        }
        if (chdir(cmd->args->items[1]) != 0) {
            perror("cd");
            return 1;
        }
        return 0;
    }
    
    if (strcmp(name, "pwd") == 0) {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("pwd");
            return 1;
        }
        printf("%s\n", cwd);
        return 0;
    }
    
    if (strcmp(name, "which") == 0) {
        if (cmd->args->size != 2) {
            return 1;
        }
        if (is_builtin(cmd->args->items[1])) {
            return 1;
        }
        char *path = find_program(cmd->args->items[1]);
        if (path == NULL) {
            return 1;
        }
        printf("%s\n", path);
        free(path);
        return 0;
    }
    
    if (strcmp(name, "exit") == 0) {
        *should_exit = 1;
        return 0;
    }
    
    if (strcmp(name, "die") == 0) {
        for (size_t i = 1; i < cmd->args->size; i++) {
            if (i > 1) printf(" ");
            printf("%s", cmd->args->items[i]);
        }
        if (cmd->args->size > 1) printf("\n");
        *should_exit = 2;
        return 1;
    }
    
    return 1;
}

int execute_job(Job *job, int last_status, int is_batch, int *should_exit) {
    if (job->num_commands == 0) {
        return last_status;
    }
    
    // Handle conditionals
    if (job->is_conditional == 1 && last_status != 0) {  // and
        return last_status;
    }
    if (job->is_conditional == 2 && last_status == 0) {  // or
        return last_status;
    }
    
    // Single command (possibly with redirection)
    if (job->num_commands == 1) {
        Command *cmd = job->commands[0];
        
        if (cmd->args->size == 0) {
            return 0;
        }
        
        // Handle built-ins
        if (is_builtin(cmd->args->items[0])) {
            // Save original stdin/stdout
            int saved_stdin = dup(0);
            int saved_stdout = dup(1);
            
            // Handle redirection for built-ins
            if (cmd->input_file) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd < 0) {
                    perror(cmd->input_file);
                    close(saved_stdin);
                    close(saved_stdout);
                    return 1;
                }
                dup2(fd, 0);
                close(fd);
            }
            
            if (cmd->output_file) {
                int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
                if (fd < 0) {
                    perror(cmd->output_file);
                    dup2(saved_stdin, 0);
                    close(saved_stdin);
                    close(saved_stdout);
                    return 1;
                }
                dup2(fd, 1);
                close(fd);
            }
            
            int result = execute_builtin(cmd, should_exit);
            
            // Restore stdin/stdout
            dup2(saved_stdin, 0);
            dup2(saved_stdout, 1);
            close(saved_stdin);
            close(saved_stdout);
            
            return result;
        }
        
        // External command
        char *path = find_program(cmd->args->items[0]);
        if (path == NULL) {
            fprintf(stderr, "%s: command not found\n", cmd->args->items[0]);
            return 1;
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            if (is_batch && !isatty(0)) {
                int fd = open("/dev/null", O_RDONLY);
                if (fd >= 0) {
                    dup2(fd, 0);
                    close(fd);
                }
            }
            
            if (cmd->input_file) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd < 0) {
                    perror(cmd->input_file);
                    exit(1);
                }
                dup2(fd, 0);
                close(fd);
            }
            
            if (cmd->output_file) {
                int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
                if (fd < 0) {
                    perror(cmd->output_file);
                    exit(1);
                }
                dup2(fd, 1);
                close(fd);
            }
            
            // Add NULL terminator for execv
            char **argv = malloc(sizeof(char*) * (cmd->args->size + 1));
            for (size_t i = 0; i < cmd->args->size; i++) {
                argv[i] = cmd->args->items[i];
            }
            argv[cmd->args->size] = NULL;
            
            execv(path, argv);
            perror(path);
            exit(1);
        }
        
        free(path);
        
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
    }
    
    // Pipeline
    int pipes[job->num_commands - 1][2];
    pid_t pids[job->num_commands];
    
    // Create all pipes
    for (int i = 0; i < job->num_commands - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            return 1;
        }
    }
    
    // Fork all processes
    for (int i = 0; i < job->num_commands; i++) {
        Command *cmd = job->commands[i];
        
        if (cmd->args->size == 0) {
            continue;
        }
        
        char *path = NULL;
        if (!is_builtin(cmd->args->items[0])) {
            path = find_program(cmd->args->items[0]);
            if (path == NULL) {
                fprintf(stderr, "%s: command not found\n", cmd->args->items[0]);
                // Clean up
                for (int j = 0; j < job->num_commands - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                return 1;
            }
        }
        
        pids[i] = fork();
        if (pids[i] == 0) {
            // Child process
            if (is_batch && !isatty(0) && i == 0) {
                int fd = open("/dev/null", O_RDONLY);
                if (fd >= 0) {
                    dup2(fd, 0);
                    close(fd);
                }
            }
            
            // Redirect input from previous pipe
            if (i > 0) {
                dup2(pipes[i-1][0], 0);
            }
            
            // Redirect output to next pipe
            if (i < job->num_commands - 1) {
                dup2(pipes[i][1], 1);
            }
            
            // Close all pipe fds
            for (int j = 0; j < job->num_commands - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            if (is_builtin(cmd->args->items[0])) {
                int dummy = 0;
                exit(execute_builtin(cmd, &dummy));
            } else {
                // Add NULL terminator for execv
                char **argv = malloc(sizeof(char*) * (cmd->args->size + 1));
                for (size_t j = 0; j < cmd->args->size; j++) {
                    argv[j] = cmd->args->items[j];
                }
                argv[cmd->args->size] = NULL;
                
                execv(path, argv);
                perror(path);
                exit(1);
            }
        }
        
        free(path);
    }
    
    // Close all pipes in parent
    for (int i = 0; i < job->num_commands - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all processes and get status of last one
    int last_exit = 0;
    for (int i = 0; i < job->num_commands; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == job->num_commands - 1) {
            last_exit = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
        }
    }
    
    return last_exit;
}

Job* parse_line(char *line) {
    Job *job = job_create();
    Command *current_cmd = command_create();
    job->commands[job->num_commands++] = current_cmd;
    
    char *ptr = line;
    int in_redirect = 0;  // 0=none, 1=input, 2=output
    int first_token = 1;
    
    while (*ptr) {
        // Skip whitespace
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        
        if (*ptr == '\0' || *ptr == '#' || *ptr == '\n') break;
        
        // Check for special tokens
        if (*ptr == '<') {
            in_redirect = 1;
            ptr++;
            continue;
        }
        
        if (*ptr == '>') {
            in_redirect = 2;
            ptr++;
            continue;
        }
        
        if (*ptr == '|') {
            current_cmd = command_create();
            job->commands[job->num_commands++] = current_cmd;
            ptr++;
            first_token = 0;
            continue;
        }
        
        // Extract token
        char token[MAX_TOKEN_LEN];
        int i = 0;
        while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && 
               *ptr != '<' && *ptr != '>' && *ptr != '|' && *ptr != '#') {
            token[i++] = *ptr++;
        }
        token[i] = '\0';
        
        if (first_token && job->num_commands == 1) {
            if (strcmp(token, "and") == 0) {
                job->is_conditional = 1;
                first_token = 0;
                continue;
            }
            if (strcmp(token, "or") == 0) {
                job->is_conditional = 2;
                first_token = 0;
                continue;
            }
        }
        first_token = 0;
        
        if (in_redirect == 1) {
            current_cmd->input_file = strdup(token);
            in_redirect = 0;
        } else if (in_redirect == 2) {
            current_cmd->output_file = strdup(token);
            in_redirect = 0;
        } else {
            arraylist_add(current_cmd->args, strdup(token));
        }
    }
    
    return job;
}

int main(int argc, char *argv[]) {
    int input_fd = 0;
    int is_interactive = 0;
    
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    if (argc == 2) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
    }
    
    is_interactive = isatty(input_fd);
    
    if (is_interactive) {
        printf("Welcome to my shell!\n");
    }
    
    char buffer[BUFFER_SIZE];
    int buf_pos = 0;
    int last_status = 0;
    
    while (1) {
        if (is_interactive) {
            printf("mysh> ");
            fflush(stdout);
        }
        
        // Read until we get a newline
        char line[BUFFER_SIZE];
        int line_pos = 0;
        int eof_reached = 0;
        
        while (1) {
            if (buf_pos == 0) {
                ssize_t n = read(input_fd, buffer, BUFFER_SIZE);
                if (n <= 0) {
                    if (line_pos == 0) {
                        if (is_interactive) {
                            printf("mysh: exiting\n");
                        }
                        if (input_fd != 0) close(input_fd);
                        return EXIT_SUCCESS;
                    }
                    eof_reached = 1;
                    line[line_pos] = '\0';
                    break;
                }
                buf_pos = n;
            }
            
            int found_newline = 0;
            for (int i = 0; i < buf_pos; i++) {
                if (buffer[i] == '\n') {
                    line[line_pos] = '\0';
                    memmove(buffer, buffer + i + 1, buf_pos - i - 1);
                    buf_pos -= i + 1;
                    found_newline = 1;
                    break;
                }
                line[line_pos++] = buffer[i];
            }
            
            if (found_newline) {
                break;
            }
            
            buf_pos = 0;
        }
        
        // Parse and execute
        Job *job = parse_line(line);
        int should_exit = 0;
        last_status = execute_job(job, last_status, !is_interactive, &should_exit);
        job_free(job);
        
        if (should_exit == 1) {
            if (is_interactive) {
                printf("mysh: exiting\n");
            }
            if (input_fd != 0) close(input_fd);
            return EXIT_SUCCESS;
        }
        
        if (should_exit == 2) {
            if (input_fd != 0) close(input_fd);
            return EXIT_FAILURE;
        }
        
        if (eof_reached) {
            if (is_interactive) {
                printf("mysh: exiting\n");
            }
            if (input_fd != 0) close(input_fd);
            return EXIT_SUCCESS;
        }
    }
    
    return EXIT_SUCCESS;
}
