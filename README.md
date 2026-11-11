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

