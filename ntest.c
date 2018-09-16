#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>

typedef struct child{
    pid_t pid;
    char * job;
    char * status;
}child_t;

typedef struct node{
    child_t kid;
    struct node * next;
}node_t;

typedef struct job{
    pid_t pid;
    char * command;
    char * status;
    char * loc;
}job_t;

//GLOBALS
node_t * BGhead;
node_t * FGhead;
pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;
bool background;
int numArgs = 0;
//pid_t j [10];
int numjobs = 0;
int last_stat = 0;

job_t js [10];




/*
  Function Declarations for builtin shell commands:
 */
int lsh_cd(char **args);
int lsh_help(char **args);
int lsh_exit(char **args);
int jobs();
int echo(char **args);



/*
  List of builtin commands, followed by their corresponding functions.
 */
char *builtin_str[] = {
        "cd",
        "help",
        "exit",
        "jobs",
        "echo"
};

int (*builtin_func[]) (char **) = {
        &lsh_cd,
        &lsh_help,
        &lsh_exit,
        &jobs,
        &echo,
};

int lsh_num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
}

/*
  Builtin function implementations.
*/

int jobs(){

    int i;
    for(i=0;i<numjobs;i++){
        printf("[%i] %d %s %s\n", i, js[i].pid, js[i].status, js[i].loc);
    }

    return 1;
}

int echo(char **args){
    if(strcmp(args[1],"$?") == 0){
        printf("Last Exit Stat: %i\n",last_stat);
    }else{
        int i;
        for(i=1;i<numArgs;i++){
            printf("%s ",args[i]);
        }
        printf("\n");
    }
}


/**
   @brief Bultin command: change directory.
   @param args List of args.  args[0] is "cd".  args[1] is the directory.
   @return Always returns 1, to continue executing.
 */
int lsh_cd(char **args)
{
    if (args[1] == NULL) {
        fprintf(stderr, "lsh: expected argument to \"cd\"\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("lsh");
        }
    }
    return 1;
}

/**
   @brief Builtin command: print help.
   @param args List of args.  Not examined.
   @return Always returns 1, to continue executing.
 */
int lsh_help(char **args)
{
    int i;
    printf("Tim Clerico's Bad Shell\n");
    printf("Type program names and arguments, and hit enter.\n");
    printf("The following are built in:\n");

    for (i = 0; i < lsh_num_builtins(); i++) {
        printf("  %s\n", builtin_str[i]);
    }

    printf("Use the man command for information on other programs.\n");
    return 1;
}

/**
   @brief Builtin command: exit.
   @param args List of args.  Not examined.
   @return Always returns 0, to terminate execution.
 */
int lsh_exit(char **args)
{
    return 0;
}

void update_status(char * nstat, pid_t pid){
    int i;

    for (i=0;i<numjobs;i++){
        if(pid == js[i].pid){
            js[i].status = nstat;
        }
    }

}

void addJob(pid_t pid, char * command, char * status, char * location){

    //printf("Atempting to add job: %d\n",pid);

    //printf("Add command: %s\n",command);

    job_t njob;
    njob.pid = pid;
    //njob.command = command;
    njob.status = status;
    njob.loc = location;

    js[numjobs] = njob;
    //printf("%i",numjobs);

}

void termJob(pid_t pid){

    //printf("Atempting to terminate job: %d",pid);

    int x;
    int ndx = 0;
    //find index of job in array
    while(js[ndx].pid!=pid){
        ndx++;
    }
    //shift all jobs after over one
    for(x=ndx;x<numjobs-1;x++){
        js[x] = js[x+1];
    }
    numjobs--;

}

/*SIG HANDELERS*/
void kid_died(int sig){
    //get pid of terminating child
    pid_t pid = getpid();
    int status;
    pid_t wpid  = waitpid(pid,&status,WUNTRACED | WNOHANG);
    //printf("Child exited with status: %d\n", WEXITSTATUS(status));
    last_stat = WEXITSTATUS(status);

    //pid_t pid;
    pid = wait(NULL);


    printf("PID %d exited. \n", pid);

    //loop through jobs for pid
    termJob(pid);



}

void suspend_proc(int sig){
    pid_t pid = js[numjobs-1].pid;

    printf("Suspending: %d", pid);

    kill(pid,SIGSTOP);
    update_status("Suspended",pid);

    fclose(stdin);
    fopen("/dev/null","r");

}

void termProc(int sig){
    //signal(sig,SIG_IGN);

    printf("Terminating Process: ");
    pid_t pid = js[numjobs-1].pid;
    //pid_t pid;
    printf("%d\n",pid);
    kill(pid,SIGKILL);

    signal(sig,SIG_DFL);
    raise(sig);
}




void init_shell(){
    //printf("Initializing Shell");
    //make sure shell is running interactively as foreground
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if(shell_is_interactive){
        //printf("Shell is Interacitve");
        //loop until in foreground
        while(tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp())){
            kill(- shell_pgid, SIGINT);
        }

        //Ignore interacitve and job-control signals
        signal(SIGINT, SIG_IGN);
        signal(SIGSTOP, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGCHLD, SIG_IGN);

        //place shell in it's own pgroup
        shell_pgid = getpid();
        if(setpgid (shell_pgid, shell_pgid) < 0){
            perror("Couldn't put shell in its own process group");
            exit(1);
        }

        //Take Control of Terminal
        tcsetpgrp(shell_terminal, shell_pgid);

        //save default terminal atts for shell
        tcgetattr(shell_terminal, &shell_tmodes);
    }

}


/**
  @brief Launch a program and wait for it to terminate.
  @param args Null terminated list of arguments (including program).
  @return Always returns 1, to continue execution.
 */
int FGlaunch(char **args)
{
    pid_t pid, wpid;
    int status;


    //printf("In FG Launch\n");

    //reset all signals back to default
    signal(SIGINT, SIG_DFL);
    signal(SIGSTOP, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

//    //signal handelers
/*    signal(SIGCHLD, kid_died);
    signal(SIGINT, termProc);
    signal(SIGSTOP, suspend_proc);*/


    pid = fork();

    //printf("job to be added: %s", args[0]);
    addJob(pid,args[0],"Active","FG");
    numjobs++;
    if (pid == 0) {
        // Child process
        //push new node
        //create new kid struct and add to new node.
        printf("Starting foreground job: %d",pid);
        if (execvp(args[0], args) == -1) {
            perror("lsh");
        }
        exit(EXIT_FAILURE);

    } else if (pid < 0) {
        // Error forking
        perror("lsh");

    } else {
        signal(SIGINT, SIG_IGN);
        signal(SIGSTOP, SIG_IGN);

        // Parent process
        do {

            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}
int BGlaunch(char **args)
{
    pid_t pid, wpid;
    int status;

    //printf("In BG Launch\n");



    //reset signals back to defualt
    //signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    //handelers
    signal(SIGCHLD, kid_died);
    signal(SIGSTOP, suspend_proc);

    pid = fork();

    //addBG(args[0],pid);

    //printf("PID: %i\n", pid);

    addJob(pid,args[0],"Active","BG");
    numjobs++;

    if (pid == 0) {
        // Child process
        fclose(stdin);
        fopen("/dev/null","r");
        if (execvp(args[0], args) == -1) {
            perror("lsh");
        }
        exit(EXIT_FAILURE);

    } else if (pid < 0) {
        // Error forking
        perror("lsh");

    } else {
        // Parent process
        do {
            //printf("starting background job %d\n", pid);
            wpid = waitpid(pid, &status, WNOHANG | WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}





/**
   @brief Execute shell built-in or launch program.
   @param args Null terminated list of arguments.
   @return 1 if the shell should continue running, 0 if it should terminate
 */
int lsh_execute(char **args)
{
    int i;

    //printf("In Execute\n");

    if (args[0] == NULL) {
        // An empty command was entered.
        return 1;
    }

    for (i = 0; i < lsh_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    if(background){
        //printf("BG attempt\n");
        background = false;
        return BGlaunch(args);
    }else{
        return FGlaunch(args);
    }

}

#define LSH_RL_BUFSIZE 1024
/**
   @brief Read a line of input from stdin.
   @return The line from stdin.
 */
char *lsh_read_line(void)
{
    int bufsize = LSH_RL_BUFSIZE;
    int position = 0;
    char *buffer = malloc(sizeof(char) * bufsize);
    int c;

    if (!buffer) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        // Read a character
        c = getchar();

        // If we hit EOF, replace it with a null character and return.
        if (c == EOF || c == '\n') {
            buffer[position] = '\0';
            return buffer;
        } else {
            buffer[position] = c;
        }
        position++;

        // If we have exceeded the buffer, reallocate.
        if (position >= bufsize) {
            bufsize += LSH_RL_BUFSIZE;
            buffer = realloc(buffer, bufsize);
            if (!buffer) {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}

#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM " \t\r\n\a"
char **lsh_split_line(char *line)
{
    //printf("in Split Line\n");

    int bufsize = LSH_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, LSH_TOK_DELIM);
    while (token != NULL) {
        if(strcmp(token,"&") != 0){
            tokens[position] = token;
            //printf("%s\n",token);
            numArgs++;
            position++;
        }else{
            //printf("COMP SUCC");
            background = true;
        }


        if (position >= bufsize) {
            bufsize += LSH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, LSH_TOK_DELIM);
    }
    //printf("numargs: %i\n", numArgs);
    tokens[position] = NULL;
    return tokens;
}

/**
   @brief Loop getting input and executing it.
 */
void lsh_loop(void)
{
    char *line;
    char **args;
    //char **nargs;
    int status;


    do {

        //signal(SIGINT, termProc);

        printf("icsh> ");
        line = lsh_read_line();
        args = lsh_split_line(line);

        if(background){
            //printf("attempt BG\n");
            status = lsh_execute(args);
        }else{
            status = lsh_execute(args);
        }

        numArgs = 0;
        free(line);
        free(args);
    } while (status);
}

/**
   @brief Main entry point.
   @param argc Argument count.
   @param argv Argument vector.
   @return status code
 */
int main(int argc, char **argv)
{
    printf("==================================\n");
    printf("Welcome to Tim's Really Bad Shell\n");
    printf("Enjoy My First Every C Program\n");
    printf("==================================\n\n");
    //initialize the shell
    init_shell();

    // Run command loop.
    lsh_loop();


    return EXIT_SUCCESS;
}