#define _POSIX_C_SOURCE 200809L

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
#include <dirent.h>     //ADDED: opendir/readdir for wildcards

typedef struct{
    char **args;
    int argc;
    char *input_file;
    char *output_file;
    int append;
} command;

typedef struct{
    command *commands;
    int count;
    int capacity;
} pipeline;

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

int is_operator(const char *token){
    return strcmp(token, "<") == 0 ||
           strcmp(token, ">") == 0 ||
           strcmp(token, ">>") == 0 ||
           strcmp(token, "|") == 0;
}

//resolve the path for which (find path of executable)
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

//==================== ADDED START: print program result in the spec's format ====================
//print the result of a finished program (interactive mode only)
//status is the raw value from waitpid, so exit codes and signals stay separate
void report_status(int status){
    if(!interactive_mode){
        return;
    }

    if(WIFEXITED(status) && WEXITSTATUS(status) != 0){
        printf("Exited with status %d\n", WEXITSTATUS(status));
    }
    else if(WIFSIGNALED(status)){
        printf("Terminated by signal: %s\n", strsignal(WTERMSIG(status)));
    }

    fflush(stdout);
}
//==================== ADDED END ====================

//redirection
//<, >, >>
int redirection(command *cmd){
    // < input// 
    if(cmd->input_file != NULL){
        int fd = open(cmd-> input_file, O_RDONLY);
        if (fd < 0){
            perror(cmd-> input_file);
            return -1;
        }

        if(dup2(fd, STDIN_FILENO) < 0){
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }
    ////////////

    // > & >> output//
    if(cmd-> output_file != NULL){
        int flags = O_WRONLY | O_CREAT;

        if(cmd->append){
            flags |= O_APPEND;
        }
        else{
            flags |= O_TRUNC;
        }

        int fd = open(cmd->output_file, flags, 0640);

        if(fd < 0){
            perror(cmd->output_file);
            return -1;
        }

        if(dup2(fd, STDOUT_FILENO) < 0){
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }
    ///////////

    return 0;
}


//execute external like ls, etc.
int exexternal(command *cmd){
    char *path = resolve_program(cmd->args[0]);

    if(path == NULL){
        fprintf(stderr, "mysh: path not found: %s\n", cmd->args[0]);
        return -1;
    }

    pid_t pid = fork();

    if(pid < 0){
        perror("fork");
        free(path);
        return -1;
    }

    //Child process
    if(pid == 0){

        if(!interactive_mode){

            int null_fd = open("/dev/null", O_RDONLY);

            if(null_fd < 0){
                perror("/dev/null");
                _exit(126);
            }

            if(dup2(null_fd, STDIN_FILENO) < 0){
                perror("dup2");
                _exit(126);
            }

            close(null_fd);
        }

        if(redirection(cmd) < 0){
                _exit(1);
            }

        execv(path, cmd->args);

        perror(cmd->args[0]);
        _exit(126);
    }

    //parent process
    int status;

    while(waitpid(pid, &status, 0) < 0){

        if(errno == EINTR){
            continue;
        }

        perror("waitpid");
        free(path);
        return 1;
    }

    free(path);

    report_status(status); //ADDED: print "Exited with status N" / "Terminated by signal"

    if(WIFEXITED(status)){
        return WEXITSTATUS(status);
    }

    if(WIFSIGNALED(status)){
        return 128 + WTERMSIG(status);
    }

    return 1;
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

    char *home = getenv("HOME");

    if(home != NULL &&
       strncmp(cwd, home, strlen(home)) == 0 &&
       (cwd[strlen(home)] == '/' || cwd[strlen(home)] == '\0')){

        printf("~%s$ ", cwd + strlen(home));
    }
    else{
        printf("%s$ ", cwd);
    }

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
            if(cmd->input_file != NULL ||i + 1 >= count||is_operator(tokens[i + 1])){
                fprintf(stderr, "Syntax error: invalid use of <\n"); //CHANGED: typo Syntex -> Syntax
                free(temp_args);
                return -1;
            }
            cmd->input_file = tokens[i+1];
            i++;
            continue;
        }

        if(strcmp(tokens[i], ">")==0){
            if(cmd->output_file != NULL ||i + 1 >= count ||is_operator(tokens[i + 1])){
                fprintf(stderr, "Syntax error: invalid use of >\n"); //CHANGED: typo Syntex -> Syntax
                free(temp_args);
                return -1;
            }
            cmd->output_file = tokens[i+1];
            cmd->append = 0;
            i++;
            continue;
        }

        if(strcmp(tokens[i], ">>")==0){
            if(cmd->output_file != NULL ||i + 1 >= count ||is_operator(tokens[i + 1])){
                fprintf(stderr, "Syntax error: invalid use of >>\n"); //CHANGED: typo Syntex -> Syntax
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

    if(arg_count == 0){
        fprintf(stderr, "Syntax error: missing command\n"); //ADDED: so every syntax error prints exactly one message
        free(temp_args);
        return -1;
    }

    temp_args[arg_count] = NULL;
    cmd->args = temp_args;
    cmd->argc = arg_count;

    return 0;
}

//parsing pipeline
int parse_pipeline(char **token, int count, pipeline *p){
    //initialization
    p -> commands = calloc((count+1), sizeof(command));
    if(p -> commands == NULL){
        return -1;
    }
    p -> count = 0;
    p -> capacity = count+1;
    int start = 0;
    for(int i = 0; i <= count; i++){
        if(i == count || strcmp(token[i], "|") == 0){
            int length = i - start;
            if(length == 0){
                fprintf(stderr, "Empty command before/after pipe\n");
                for(int j = 0; j < p->count; j++){
                    free(p->commands[j].args);
                }

                free(p->commands);
                p->commands = NULL;
                p->count = 0;
                return -1;
            }
            if(parse_command(token + start, length, &p->commands[p->count]) == -1){
                //CHANGED: removed "Invalid command in pipe" (parse_command already printed the error)
                for(int j = 0; j < p->count; j++){
                    free(p->commands[j].args);
                }
                free(p->commands);
                p->commands = NULL;
                p->count = 0;
                return -1;
            }
            p->count++;
            start = i + 1;
        }
    }

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

//executing built in with redirection
int exbuiltinRedirection(command *cmd){
    //saving
    fflush(stdout);
    
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);

    if(saved_stdin < 0 || saved_stdout < 0){
        perror("dup");
        if(saved_stdin >= 0) close(saved_stdin);
        if(saved_stdout >= 0) close(saved_stdout);
        return -1;
    }

    if(redirection(cmd) < 0){
        dup2(saved_stdin, STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);

        close(saved_stdin);
        close(saved_stdout);
        return -1;
    }

    int status = exbuiltin(cmd);
    fflush(stdout);

    if(dup2(saved_stdin, STDIN_FILENO) < 0){
        perror("dup2");
        status = 1;
    }

    if(dup2(saved_stdout, STDOUT_FILENO) < 0){
        perror("dup2");
        status = 1;
    }

    close(saved_stdin);
    close(saved_stdout);

    return status;
}


////wildcard(2.3)
// the *.txt
typedef struct{
    char **data;
    int len;
    int cap;
} arglist;

static int arglist_init(arglist *l){
    l->len = 0;
    l->cap = 8;
    l->data = malloc(l->cap * sizeof(char *));

    if(l->data == NULL){
        return -1;
    }

    l->data[0] = NULL;
    return 0;
}

//append s to the list (the list takes ownership of s)
static int arglist_push(arglist *l, char *s){
    if(s == NULL){
        return -1;
    }

    if(l->len + 1 >= l->cap){
        int new_cap = l->cap * 2;
        char **temp = realloc(l->data, new_cap * sizeof(char *));

        if(temp == NULL){
            free(s);
            return -1;
        }

        l->data = temp;
        l->cap = new_cap;
    }

    l->data[l->len] = s;
    l->len++;
    l->data[l->len] = NULL;
    return 0;
}

static int compare_names(const void *a, const void *b){
    return strcmp(*(char * const *)a, *(char * const *)b);
}

static int glob_into(arglist *l, const char *pattern){
    const char *slash = strrchr(pattern, '/');
    const char *file = slash ? slash + 1 : pattern;   //last part of the path
    const char *star = strchr(file, '*');

    //* only in the directory part, or more than one * in the name: keep as-is
    if(star == NULL || strchr(star + 1, '*') != NULL){
        return arglist_push(l, strdup(pattern));
    }

    size_t dirlen = slash ? (size_t)(slash - pattern + 1) : 0;  //includes the '/'
    char *dirpath = slash ? strndup(pattern, dirlen) : strdup(".");

    if(dirpath == NULL){
        return -1;
    }

    DIR *dir = opendir(dirpath);
    free(dirpath);

    if(dir == NULL){//directory doesn't exist, so no matches
        return arglist_push(l, strdup(pattern));
    }

    size_t prelen = star - file;      //characters before the *
    const char *suffix = star + 1;    //characters after the *
    size_t suflen = strlen(suffix);

    int first_match = l->len;
    struct dirent *entry;

    while((entry = readdir(dir)) != NULL){
        const char *name = entry->d_name;
        size_t len = strlen(name);

        if(prelen == 0 && name[0] == '.'){
            continue;
        }

        if(len < prelen + suflen ||
           strncmp(name, file, prelen) != 0 ||
           strcmp(name + len - suflen, suffix) != 0){
            continue;
        }

        //rebuild the full path: directory part + matching name
        char *full = malloc(dirlen + len + 1);

        if(full == NULL){
            closedir(dir);
            return -1;
        }

        memcpy(full, pattern, dirlen);
        strcpy(full + dirlen, name);

        if(arglist_push(l, full) == -1){
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);

    if(l->len == first_match){//no matches
        return arglist_push(l, strdup(pattern));
    }

    //sort just the names we added, so the output is predictable
    qsort(l->data + first_match, l->len - first_match, sizeof(char *), compare_names);
    return 0;
}

int expand_wildcards(command *cmd){
    arglist l;

    if(arglist_init(&l) == -1){
        free(cmd->args);
        cmd->args = NULL;
        cmd->argc = 0;
        return -1;
    }

    int result = 0;

    for(int i = 0; i < cmd->argc; i++){
        char *arg = cmd->args[i];

        if(strchr(arg, '*') != NULL){
            result = glob_into(&l, arg);
        }
        else{
            result = arglist_push(&l, strdup(arg));
        }

        if(result == -1){
            break;
        }
    }

    //old array only held pointers into tokens, so only the array is freed
    free(cmd->args);
    cmd->args = l.data;
    cmd->argc = l.len;

    return result;
}

//expand wildcards in every command of the pipeline
int expand_pipeline_wildcards(pipeline *p){
    int result = 0;

    for(int i = 0; i < p->count; i++){
        if(expand_wildcards(&p->commands[i]) == -1){
            result = -1;
        }
    }

    return result;
}

////pipping(2.5)
// the |

//executing pipeline
int expipeline(pipeline *p){
    int n = p->count;
    if(n == 0){
        return 0;
    }

    int (*pipes)[2] = NULL;

    if(n > 1){
        pipes = malloc((n - 1) * sizeof(int[2]));

        if(pipes == NULL){
            perror("malloc");
            return -1;
        }

        for(int i = 0; i < n - 1; i++){
            if(pipe(pipes[i]) < 0){
                perror("pipe");
                for(int j = 0; j < i; j++){
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                free(pipes);
                return -1;
            }
        }
    }

    pid_t *pids = malloc(n * sizeof(pid_t)); //child(ren) pid

    if(pids == NULL){
        perror("malloc");

        for(int i = 0; i < n - 1; i++){
            close(pipes[i][0]);
            close(pipes[i][1]);
        }

        free(pipes);
        return 1;
    }

    int started = 0;
    for(int i = 0; i < n; i++){

        pid_t pid = fork();

        if(pid < 0){
            perror("fork");
            break;
        }

        //child process
        if(pid == 0){
            if(i == 0 && !interactive_mode){

                int null_fd = open("/dev/null", O_RDONLY);

                if(null_fd < 0){
                    perror("/dev/null");
                    _exit(126);
                }

                if(dup2(null_fd, STDIN_FILENO) < 0){
                    perror("dup2");
                    _exit(126);
                }

                close(null_fd);
            }

            //input
            if(i > 0){

                if(dup2(pipes[i-1][0], STDIN_FILENO) < 0){
                    perror("dup2");
                    _exit(126);
                }
            }

            //output
            if(i < n - 1){

                if(dup2(pipes[i][1], STDOUT_FILENO) < 0){
                    perror("dup2");
                    _exit(126);
                }
            }

            // Close all original pipe descriptors
            for(int j = 0; j < n - 1; j++){
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            command *cmd = &p->commands[i];

            if(redirection(cmd) < 0){
                _exit(1);
            }

            // Execute built-in
            if(is_builtin(cmd)){

                int status = exbuiltin(cmd);
                fflush(stdout);
                fflush(stderr);
                _exit(status == 0 ? 0 : (status > 0 ? status : 1));
            }

            // Execute external 
            char *path = resolve_program(cmd->args[0]);
            if(path == NULL){
                fprintf(stderr, "mysh: path not found: %s\n", cmd->args[0]);
                _exit(127);
            }
            execv(path, cmd->args);

            perror(cmd->args[0]);
            free(path);
            _exit(126);
        }

        //parent
        pids[started] = pid;
        started++;
    }

    for(int i = 0; i < n - 1; i++){
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    free(pipes);

    // Wait for all child processes
    int last_status = -1;

    for(int i = 0; i < started; i++){

        int status;
        pid_t result;
        do{
            result = waitpid(pids[i], &status, 0);
        }while(result < 0 && errno == EINTR);

        if(result < 0){
            perror("waitpid");
            last_status = -1;
            continue;
        }
        if(i == n - 1){
            report_status(status);

            if(WIFEXITED(status)){
                last_status = WEXITSTATUS(status);
            }
            else if(WIFSIGNALED(status)){
                last_status = 128 + WTERMSIG(status);
            }
            else{
                last_status = -1;
            }
        }
    }
    free(pids);
    return last_status;
}

////error detecting
//If ANY? like syntax error


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

        pipeline p;
        if(parse_pipeline(tokens, count, &p) == -1){

            free(tokens);
            free(commands);
            continue;
        }

        if(expand_pipeline_wildcards(&p) == -1){
            perror("malloc");
        }

        int status = 0;

        if(p.count == 1){
            command *cmd = &p.commands[0];
            if(is_builtin(cmd)){
                status = exbuiltinRedirection(cmd);
            }
            else{
                status = exexternal(cmd);
            }
        }
        else{
            status = expipeline(&p);
        }

        //exit
        for(int i = 0; i < p.count; i++){

            if(strcmp(p.commands[i].args[0], "exit") == 0){
                should_exit = 1;
            }
        }

        //after wildcard expansion every argument string is malloc'd, so free them too
        for(int i = 0; i < p.count; i++){
            if(p.commands[i].args != NULL){
                for(int j = 0; j < p.commands[i].argc; j++){
                    free(p.commands[i].args[j]);
                }
            }
            free(p.commands[i].args);
        }

        free(p.commands);

        (void)status;
        //freeing
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
