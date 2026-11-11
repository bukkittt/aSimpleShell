## A Simple Shell
Mark Filip : mf1185
Tinson Dong : td584

Building and running
make            builds ./mysh
make clean      remove the binary

./mysh          interactive mode when stdin is terminal
./mysh <file>   batch mode, reading commands from a file


Design

Input
-----
read_command() reads one byte at a time with read() and stops at the \n.
A command finishes running before any more input is read.
tokenize() splits a line on whitespace and drops everything after #

Parsing
-------
check_syntax() rejects a line before parsing if it has:
    "< <"
    a redirect with no file name
    | at the start or end
    "| |"


Wildcards
---------

expand_wildcards() replaces each token containing * with the matching file names, sorted

Patterns that begin with * skip hidden files
if nothign matches, the token doesnt change.
The arg list is built as a growable array since the number of matches isnt known in advance

Execution
---------

A single built-in runs in the sheel process thru exbuiltinRedirection(). It saves stdin and stdout with dup(),
applies the redirection, runs the built-in and then restores both. 
Everything else goes through expipeline(), including a single program,
  which is treated as a pipeline of one. It creates n-1 pipes and forks one
  child per command. Each child sets up its input and output as follows:
    It dup2()s its pipe ends onto stdin and stdout.
    In batch mode, the first command reads from /dev/null.
    It closes every pipe fd.
    It calls redirection(). Redirection is applied last, so < > >> take
      priority over a pipe.
  The child then either runs a built-in and exits, or calls execv(). The
  parent closes all pipe ends and waits for every child. The line succeeds
  if the last command succeeds.

Programs
  A name containing / is used as a path. A bare name is searched for in
  /usr/local/bin, /usr/bin, and /bin, in that order, using access(X_OK).
  The which built-in uses the same search.

Status and exit
  In interactive mode, mysh prints "Exited with status N" when a program
  exits with a non-zero code. It prints "Terminated by signal: X" (using
  strsignal) when a program is killed by a signal.

  exit anywhere on a line ends the shell once the whole line has finished.
  This includes exit inside a pipeline such as "exit | echo hi > f". End
  of input also ends the shell.

# test plan:

### Tokenization and Parsing
1. echo hello world
    - will not print error message
    - print "hello world"
2. echo hello # hahahahaha
    - print "hello"
3. #????////asdhihv
    - print nothing


### Redirection
1. ls > / ls < / ls >>
    - invalid use of each redirection
2. ls > > output.txt
    - invalid use
3. echo hello > output.txt
    - write hello into output.txt file
    - create ouput.txt file if it is not originally exist
- running echo goodbye > output.txt will replace the "hello" with "goodbye"
- running echo goodbye >> output.txt will apend "goodbye" with "hello"
4. cat < output.txt
    - print the content of the output.txt file
5. cat < input.txt > output.txt
    - take input.txt as input and save output to output.txt

### Build in command

1. cd
    - cd . --> Will not change anything
    - cd .. --> will traverse to the parent directory
    - cd.. --> not found
    - cd path/to/destintion --> traverse through the path specified

2. pwd
    - the path to the current directory
3. which
    - which -> missing arguments
    - which ls -> print the path to an executable
    - which ls -> process multiple arguments
4. exit
    - exit -> terminate the program
    - exit ??? -> still terminate
    - exit | echo hello -> print hello and then terminate
5. any command + extra -> invalid

### External command
1. ls -> listing all the file in the current directory
2. echo hello -> print hello
3. ./executable -> execute that executable

### Piping
1. | ls / ls | / ls | | cd
    - invalid pipeline
2. ls | wc -l
    - print the number of files in directory

### Wildcard
1. echo *.txt
    - print all .txt file
2. echo foo*bar
    - print all file with the maching start and end

