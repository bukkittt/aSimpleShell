#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct{
    char **args;
    int argc;
    char *input_file;
    char *output_file;
    int append;
} command;

static int interactive_mode = 0;
static int should_exit = 0;

////helper methods
//
//helper for buildins

//return true if it is our builtin
int is_builtin(command *cmd){

    if(cmd->argc == 0){
        return 0;
    }

    char *name = cmd->args[0];

    return strcmp(name, "cd") == 0 ||
           strcmp(name, "pwd") == 0 ||
           strcmp(name, "which") == 0 ||
           strcmp(name, "exit") == 0;
}

//resolve the path for which
char *resolve_program(const char *name){

    if(strchr(name, '/') != NULL){

        if(access(name, X_OK) == 0){
            char *result = malloc(strlen(name) + 1);

            if(result == NULL){
                return NULL;
            }

            strcpy(result, name);
            return result;
        }

        return NULL;
    }

    const char *directories[] = {
        "/usr/local/bin",
        "/usr/bin",
        "/bin"
    };

    for(int i = 0; i < 3; i++){

        size_t size = strlen(directories[i]) + strlen(name) + 2;

        char *candidate = malloc(size);

        if(candidate == NULL){
            return NULL;
        }

        snprintf(candidate, size, "%s/%s", directories[i], name);

        if(access(candidate, X_OK) == 0){
            return candidate;
        }

        free(candidate);
    }

    return NULL;
}



////input

//reading from the command line
char *read_command(int fd){

    int capacity = 128;
    int length = 0;

    char *buffer = malloc(capacity);

    if(buffer == NULL){
        return NULL;
    }

    char c;
    while (1){
        ssize_t byte = read(fd, &c, 1);

        if(byte == -1){//error
            perror("read");
            free(buffer);
            return NULL;
        }

        if(byte == 0){//reaching end of line
            if (length == 0) {//nothing is in the command line
                free(buffer);
                return NULL;
            }
            break;
        }

        if(c == '\n'){
            break;
        }

        if(length + 1 >= capacity){//increase size if needed
            capacity *= 2;
            char *temp = realloc(buffer, capacity);

            if(temp == NULL){
                free(buffer);
                return NULL;
            }

            buffer = temp;
        }

        buffer[length] = c;
        length++;
    }


    buffer[length] = '\0';
    return buffer;
}

//printing promt in specific pattern
void promt_printing(void){
    char *cwd = getcwd(NULL, 0);

    if(cwd == NULL){
        perror("getcwd");
        return;
    }

    printf("%s$ ", cwd);

    free(cwd);
    fflush(stdout);
}

////tokenizing and parsing
//
//tokenizing - seperating command line to tokens(sequence of char without white space)
//returns the number of token on success, -1 on failure
int tokenize(char *command, char*** addr_to_tokens){
    int capacity = 8;
    int count = 0;

    char **tokens = malloc(capacity * sizeof(char *));

    if(tokens == NULL){
        return -1;
    }

    char *comment = strchr(command, '#');//find where the comment start

    if(comment != NULL){
        *comment = '\0';
    }

    char *saveptr;
    char *token = strtok_r(command, " \t\r\n", &saveptr);

    while(token != NULL){

        if(count + 1 >= capacity){//increase the arr size if needed
            capacity *= 2;
            char **temp = realloc(tokens, capacity * sizeof(char *));
            
            if(temp == NULL){
                free(tokens);
                return -1;
            }

            tokens = temp;
        }

        tokens[count] = token;
        count++;

        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    tokens[count] = NULL;
    *addr_to_tokens = tokens;
    return count;
}

//parsing
int parse_command(char **tokens, int count, command *cmd){

    cmd->args = NULL;
    cmd->argc = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append = 0;

    char **temp_args = malloc((count + 1)* sizeof(char*));
    if(temp_args == NULL){
        return -1;
    }

    int arg_count = 0;

    for(int i = 0; i < count; i++){

        if(strcmp(tokens[i], "<")==0){

            if(i + 1 >= count){
                free(temp_args);
                return -1;
            }
            cmd->input_file = tokens[i+1];
            i++;
            continue;
        }

        if(strcmp(tokens[i], ">")==0){
            if(i + 1 >= count){
                free(temp_args);
                return -1;
            }
            cmd->output_file = tokens[i+1];
            cmd->append = 0;
            i++;
            continue;
        }

        if(strcmp(tokens[i], ">>")==0){
            if(i + 1 >= count){
                free(temp_args);
                return -1;
            }
            cmd->output_file = tokens[i+1];
            cmd->append = 1;
            i++;
            continue;
        }
        temp_args[arg_count] = tokens[i];
        arg_count++;
    }

    temp_args[arg_count] = NULL;
    cmd->args = temp_args;
    cmd->argc = arg_count;

    return 0;
}

////Build-in Command(section 3)
//cd, pwd, which, exit

//cd
int builtin_cd(command *cmd){
    if(cmd->argc>2){
        fprintf(stderr, "cd: too many arguments\n");
        return -1;
    }

    char *path;

    if(cmd->argc == 1){
        path = getenv("HOME");

        if(path == NULL){
            fprintf(stderr, "cd: HOME not set\n");
            return -1;
        }
    }
    else{
        path = cmd->args[1];
    }

    if(chdir(path) == -1){
        perror("cd");
        return -1;
    }

    return 0;
}

//pwd
int builtin_pwd(command *cmd){
    if(cmd->argc != 1){
        fprintf(stderr, "pwd: too many arguments\n");
        return 1;
    }
    char *cwd = getcwd(NULL, 0);

    if(cwd == NULL){
        perror("getcwd");
        return -1;
    }

    printf("%s\n", cwd);

    free(cwd);

    return 0;
}

//which
int builtin_which(command * cmd){
    if (cmd->argc > 2){
        fprintf(stderr, "which: too many arguments\n");
        return -1;
    }
    if (cmd->argc < 2){
        fprintf(stderr, "which: missing arguments\n");
        return -1;
    }
    int status = 0;

    for(int i = 1; i < cmd->argc; i++){

        char *path = resolve_program(cmd->args[i]);

        if(path == NULL){
            fprintf(stderr, "which: %s not found\n", cmd->args[i]);
            status = 1;
        }
        else{
            printf("%s\n", path);
            free(path);
        }
    }
    return status;
}

//exit
int builtin_exit(void){
    should_exit = 1;
    return 0;
}

//executing builtin
int exbuiltin(command *cmd){

    char *name = cmd->args[0];

    if(strcmp(name, "pwd") == 0){
        return builtin_pwd(cmd);
    }

    if(strcmp(name, "cd") == 0){
        return builtin_cd(cmd);
    }

    if(strcmp(name, "exit") == 0){
        return builtin_exit();
    }

    if(strcmp(name, "which") == 0){
        return builtin_which(cmd);
    }

    return 1;
}


////wildcard(2.3)
//TODO

////redirection(2.4)
//TODO
//

////pipping(2.5)
//TODO
//

////error checking
//If ANY?


//
////main
int main(int argc, char *argv[]){
    int fd;

    if(argc > 2){
        fprintf(stderr, "too many arguments.");
        return EXIT_FAILURE;
    }

    if(argc == 2){
        fd = open(argv[1], O_RDONLY);

        if(fd < 0){
            perror("open");
            return EXIT_FAILURE;
        }
    }

    else{
        fd = STDIN_FILENO;
    }

    interactive_mode = isatty(fd);

    if(interactive_mode) printf("Welcome to myshell!\n");

    while(1){
        if(interactive_mode){
            promt_printing();
        }

        char *commands = read_command(fd);

        if(commands==NULL)
            break;

        char **tokens;

        int count = tokenize(commands, &tokens);

        if(count == -1){
            fprintf(stderr, "Tokenization failed\n");
            free(commands);
            continue;
        }

        if(count == 0){
            free(tokens);
            free(commands);
            continue;
        }

        command cmd;

        if(parse_command(tokens, count, &cmd) == -1){
            fprintf(stderr, "Invalid command\n");

            free(tokens);
            free(commands);
            continue;
        }

        ///{{{{

        int status = 0;

        if(is_builtin(&cmd)){
            status = exbuiltin(&cmd);
        }
        else{
            printf("External command: %s\n", cmd.args[0]);
        }

        free(cmd.args);
        free(tokens);
        free(commands);

        if(should_exit){
            break;
        }
    }
    if (interactive_mode)
        printf("Exiting my shell.\n");

    if (argc == 2)
        close(fd);

    return EXIT_SUCCESS;
}

