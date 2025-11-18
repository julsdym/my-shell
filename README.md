Julia Dymnicki jd1604
Mateo Paneso map686

To ensure that our implementation of mysh.c behaves correctly according to the project specifications, we will design a comprehensive test suite that evaluates the shell's behavior. Our testing strategy will include coverage of interactive mode, batch mode, command parsing, execution syntax, built-in command behavior, redirection, pipelines, and conditionals. Because the project emphasizes correctness in parsing, process control, and I/O behavior, our test cases will focus on ensuring the shell behaves predictably in normal, boundary, and error scenarios.

Interactive Mode
We will test interactive mode by running mysh in a terminal to verify that isatty() identifies terminal input, the presence of the welcome message, correct prompt formatting, immediate command execution, and the printing of a goodbye message upon termination.  Our tests will include issuing commands with and without arguments, checking that the shell does not block after receiving full lines, and ensuring that standard input is handled correctly.
When “./mysh” is typed, a welcome message is expected, and the prompt “mysh> “ should appear. “exit” or “die” should trigger a goodbye message.
Interactive Mode Test Cases:


./mysh


cd subdir
echo hello


cd subsubdir
pwd


cd directory_that_does_not_exist


cd ../..
exit


Batch Mode: File or Piped Input
We will test batch mode by creating a simple script and running it using “./msh name_of_file” and “cat name_of_file | ./mysh”. These tests confirm that no prompts or welcome messages appear, that commands execute in order, and that input from non-terminal sources causes child processes to receive /dev/null as stdin. We will also verify that reaching EOF terminates the shell cleanly, and that child processes will have their stdin redirected to /dev/null by using ‘cat’, which should immediately exit, produce no output, and not hang.  
Typing in “echo \”echo hello\” > batch1.txt” should provide the output of only “hello”
Batch Mode Test cases:


cat myscript.sh
./mysh myscript.sh



cat myscript.sh | ./msh

Built-In Commands:
cd: We will test that “cd <dir>” changes directories for relative and absolute paths, and that a nonexistent directory prints an error and fails.
pwd: We will test that “pwd” will print the current working directory.
which: We will test that the correct path is printed for the program which mysh would use as if it were to start that program, and built-in directories and non-existent directories fail.
exit and die: We will test that exit terminates shell with success and works inside pipelines ( foo | exit ends shell after foo runs). We will test that die prints all arguments and terminates the shell with EXIT_FAILURE.
Test Cases:
cd to relative path
cd .
pwd

cd to absolute path
cd /
pwd

exit inside pipeline
echo hi | exit


Redirection:
Input “<”: We will test that if the file exists, it will read the program from the file, and if it is missing, it will print an error.
Output “>”: We expect the shell to create a new file with mode 0640, truncate an existing file, and fail if it cannot open the file.  
We will also test combined redirection for both orders (i.e. foo < in > out, foo > out < in). We will test to see if output redirection works (echo hi > out.txt cat out.txt), if overwriting works (echo first > test.txt echo second > test.txt cat test.txt), and if input redirection works (echo foo > in.txt cat < in.txt)
Redirection Test cases: 


echo hi > out.txt
cat out.txt



echo first > test.txt
echo second > test.txt
cat test.txt



echo foo > in.txt
cat < in.txt



cat < in.txt > out2.txt
cat out2.txt




Pipelines: 
We will test pipelines with built-in commands (e.g. pwd | cat), ensure that no redirection is combined with pipelines, we will verify file descriptors using dup2(), and that only the last process exit status defines pipeline success.
We will test that “foo bar | baz quux” will start a pipeline with 2 processes, with the first redirecting output to a pipe, and the second having input redirected to the pipe.
We will test that “foo | bar | baz | quux” will start a pipeline with 4 processes, connecting them with 3 pipes.  We will also test redirection in combination with pipelining (ls | grep .c > cfiles.txt cat cfiles.txt)
Pipelining Test Cases:


and foo bar < baz


foo bar | baz quux


foo | bar | baz | quux


Conditionals:
We will test that “and foo bar < baz” will redirect standard input to “baz”.
We will test that “foo or bar or baz” will have “baz” executed only if both “foo” and “bar” fail.
We will test that “foo and bar or baz” will have “baz” execute if “foo” fails or if “foo” succeeds and ‘baz’ fails.
We will test that in “foo or die whoops bar” will have “whoops” printed and terminate if “foo” fails.
Conditional Test Cases:


foo
or bar
or baz



foo
or die whoops
bar



foo
and bar
or baz


Syntax Errors:
We will test that the proper syntax errors will exit with status EXIT_FAILURE, such as “< <” or built-in commands with an improper amount of arguments, the last program in a pipeline fails, a command fails, multiple “>” or “<” in unsupported order, “|” at start/end, or condition tokens (and, or) in invalid positions.  We will also ensure that if a comment is included in an argument, it is ignored and not printed to the terminal. (Example: ./mysh hello #hi -> only prints ‘hello’)
Syntax Error Test Cases:
invalid redirection
cat < < file.txt

< file1 < file2 ls
ls > out1 > out2
ls < in1 > < in2

comments (should not print comment)
echo hello #world

invalid conditional token placement
and ls
ls and
ls and or ls
ls or or echo hi

invalid pipelining
| ls
ls |
| ls |

built-in with wrong number of args
cd a b
which a b
exit a
pwd a







