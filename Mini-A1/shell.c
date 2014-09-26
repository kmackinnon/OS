/**
 * Keith MacKinnon
 * 260460985
 * September 26, 2014
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>		// signal
#include <unistd.h>		// fork()
#include <sys/types.h> 	// pid_t

#define MAX_LINE 80 	// 80 chars per line, per command
#define MAX_JOBS 10		// allow up to 10 background processes
#define MAX_HISTORY 10 	// allows user access to 10 previous commands

typedef struct hist_entry {
  int number;
  int is_invalid;
  char *command;
} hist_elem;

typedef struct job_entry {
	pid_t pid;
	char *command;
} job_elem;

hist_elem *history[MAX_HISTORY]; 	// global history array
job_elem *jobs[MAX_JOBS]; 			// global jobs array

job_elem *fg; // foreground process information

int hist_index = 0, hist_count = 1;
int jobs_index = 0;
pid_t sh_pid;

// marks the most recent history element as invalid
void invalidate_last() {
	if (hist_count > 1) {
		hist_count--; 
	}
	history[hist_index - 1]->is_invalid = 1;
}

// returns the most recent history element
hist_elem *last_history() {
	return history[hist_index - 1] ? history[hist_index - 1]: NULL;
}

// finds the most recent command of the last 10 valid commands
hist_elem *find_history(char first) {
	int i;
	int recent_ten = 0;

	if (hist_index > 9) {
		recent_ten = hist_index - 10;
	}

	// search the 10 most recent commands down from most recent
	for (i = hist_index - 1; i >= recent_ten; i--) {
		if (history[i] && history[i]->command[0] == first) {
			return history[i];
		}

		if (history[i]->is_invalid && recent_ten > 0) {
			recent_ten--; // if history is invalid, show extra previous commands
		}
	}
	return NULL; // no entry in history with that letter
}

// adds an element to the history array
void add_history(char *cmd) {
	int i;
	for (i = 0; i < MAX_LINE; i++) {
		if (cmd && cmd[i] == '\n') {
			cmd[i] = '\0'; // create a C string
		}
	}

	history[hist_index] = malloc(sizeof(hist_elem));
	history[hist_index]->number = hist_count++;
	history[hist_index]->command = cmd;
	hist_index++;
}

// prints last 10 valid commands from newest to oldest
void print_history() {	
	int less_ten = 0;

	if (hist_index > 9) {
		less_ten = hist_index - 10;
	}

	int i;
	for (i = hist_index - 1; i >= less_ten; i--) {

		if (!history[i]->is_invalid) {
			printf("%d: %s\n", history[i]->number, history[i]->command);
		} else if (less_ten > 0) {
			less_ten--; // if history is invalid, print extra previous commands
		}
	}
}

// copies the real command to the inputBuffer
int retrieve_command(char inputBuffer[], char *args[]) {
	int is_historic = 0;
	hist_elem *element;

	// check if there is a specific command to run
	if (!args[1]) {
		element = last_history();
	} else {
		char first_letter = args[1][0];
		element = find_history(first_letter);
	}

	// check validity of command
	if (element) {
		char *cmd = element-> command;
		printf("%s\n", cmd);
		strncpy(inputBuffer, cmd, MAX_LINE);
		strcat(inputBuffer, "\n");
		is_historic = 1;				
	} else {
		printf("No command beginning with %c found.\n", args[1][0]);
	}
	return is_historic; // tells main() that command is old
}

// sets the foreground process group
void setfgpgrp(int pid) {
	// ignore since program will be stopped when tcsetpgrp is set
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);

	int pgid = 0;

	if (pid == 0) {
		// foreground the shell
		setpgid(sh_pid, sh_pid);
		pgid = getpgid(sh_pid);
	} else {
		// foreground a child process
		setpgid(pid, pid);
		pgid = getpgid(pid);
	}

	tcsetpgrp(STDIN_FILENO, pgid);
	tcsetpgrp(STDOUT_FILENO, pgid);
	tcsetpgrp(STDERR_FILENO, pgid);

	// return SIGTTOU/SIGTTIN to default behaviour
	signal(SIGTTOU, SIG_DFL);
	signal(SIGTTIN, SIG_DFL);
}

// return whether or not a child process is alive
int is_alive(int pid) {
	int status = 0;
	return (waitpid(pid, &status, WNOHANG) == 0);
}

// adds a background process to the jobs array
void add_job(pid_t pid, char *cmd) {

	// add child to jobs list of background processes
	jobs[jobs_index] = malloc(sizeof(job_elem));
	jobs[jobs_index]->pid = pid;
	jobs[jobs_index]->command = cmd;

	jobs_index++;
	setfgpgrp(0); // pass control back to shell
}

// remove a background process from jobs array
void remove_job(int pid) {
	int i;
	for (i = 0; i < MAX_JOBS; i++) {
		if (jobs[i] && jobs[i]->pid == pid) {
			free(jobs[i]);
			jobs[i] = NULL;
		}
	}
}

// list all background jobs
void list_background_jobs() {
	int i;
	for (i = 0; i < MAX_JOBS; i++) {
		if (jobs[i]) {
			if (is_alive(jobs[i]->pid)) {
				printf("%d\t%s\n", jobs[i]->pid, jobs[i]->command);
			} else {
				printf("No current jobs\n");
				free(jobs[i]);
				jobs[i] = NULL;
			}
		}
	}
}

// takes a job pid and sends it to the foreground
void send_to_foreground(pid_t pid, char *cmd) {
	if (!fg) {
		fg = malloc(sizeof(job_elem));
	}

	// update the foreground struct
	fg->pid = pid;
	fg->command = cmd;

	// pass i/o control to child process
	setfgpgrp(fg->pid);

	int status = 0;
	waitpid(pid, &status, WUNTRACED);

	// if the pid was stopped, change shell to foreground process and send SIGCONT to child
	if (WIFSTOPPED(status)) {
		// send SIGCONT to child
		kill(fg->pid, SIGCONT);
		remove_job(pid);
		add_job(fg->pid, fg->command);
	
	} else if (WIFEXITED(status)) {			
		// the child finished so pass i/o control back to shell
		setfgpgrp(0);
	
	} else if (WIFSIGNALED(status)) {
		setfgpgrp(0);
	}
}

// validates fg input and calls send_to_foreground
void foreground(char *args[]) {
	if (args[1]) {
		int f_pid = strtoimax(args[1], NULL, 10);

		int i;
		for (i = 0; i < MAX_JOBS; i++) {
			if (jobs[i] && jobs[i]->pid == f_pid && is_alive(f_pid)) {
				send_to_foreground(jobs[i]->pid, jobs[i]->command);
				break;
			}
			if (i == MAX_JOBS - 1) {
				printf("No job with pid %d found.\n", f_pid);
			}
		}  

	} else {
		printf("fg requires a pid to send to foreground.\n");
	}
}

/**
 * setup() reads in the next command line, separating it into distinct tokens
 * using whitespace as delimiters. setup() sets the args parameter as a 
 * null-terminated string.
 */
int setup(char inputBuffer[], char *args[], int *background) {
	int length, // # of chars in the command line
		i,		// loop index for accessing inputBuffer array
		start,	// index where beginning of next command param is 
		ct;		// index of where to place the next param into args[]

	ct = 0;

	if (inputBuffer[0] == '\0') {
		// read what the user enters on the command line
		length = read(STDIN_FILENO, inputBuffer, MAX_LINE);
	} else {
		length = strlen(inputBuffer);
	}

	start = -1;
	
	if (length == 0) {
		exit(0); // ^d was entered, end of user command stream
	}

	if (length < 0) {
		perror("error reading the command");
		exit(-1); // terminate with error code -1
	}

	// do not add history commands or a blank line to history
	if (inputBuffer[0] != '\n') {
		if (inputBuffer[0] != 'r' && (inputBuffer[1] != ' ' || inputBuffer[1] != '\n')) {
			add_history(strdup(inputBuffer));
		}
	}

	// examine every char in the inputBuffer
	for (i = 0; i < length; i++) {
		switch(inputBuffer[i]) {
			case ' ':
			case '\t': // argument separators
				if (start != -1) {
					args[ct] = &inputBuffer[start]; // set up pointer
					ct++;
				}
				inputBuffer[i] = '\0'; // add a null char; make a C string
				start = -1;
				break;
			case '\n': // should be final char examined
 				if (start != -1) {
 					args[ct] = &inputBuffer[start];
 					ct++;
				}
				inputBuffer[i] = '\0';
				args[ct] = NULL; // no more arguments to this command
				break;
			default: // some other character
				if (start == -1) {
					start = i;
					if (inputBuffer[i] == '&') {
						*background = 1;
						inputBuffer[i] = '\0';
						start = -1; // prevent whitespace as argument
					}
				}
		}
	}

	args[ct] = NULL; // just in case the input line was > 80

	return 0;
}

/** 
 *	the steps are
 * (1) fork a child process using fork()
 * (2) the child process will invoke execvp()
 * (3) if background == 0, the parent will wait
 *	   otherwise returns to the setup() function 
*/	
int main(void) {
	char inputBuffer[MAX_LINE];	// buffer to hold command cmd
	int background;				// equals 1 if command is followed by '&'
	char *args[MAX_LINE/2];		// command line of 80 has max of 40 arguments
	
	int is_historic = 0;		// whether the command is old
	sh_pid = getpid(); 			// store parent pid so we can transfer control back

	// zero the arrays just in case
	int i;
	for (i = 0; i < MAX_LINE / 2; i++) {
		args[i] = NULL;
	}
	for (i = 0; i < MAX_JOBS; i++) {
		jobs[i] = NULL;
	}
	for (i = 0; i < MAX_HISTORY; i++) {
		history[i] = NULL;
	}

	signal(SIGINT, SIG_IGN); // setup signal handlers

	while(1) {	// program terminates normally inside of setup
		background = 0;

		// if a command from history is not being executed
		if (!is_historic) {
			printf("COMMAND -> ");
			fflush(stdout);
			memset(inputBuffer, 0, MAX_LINE);
		}
		is_historic = 0;		
		
		int setup_status = setup(inputBuffer, args, &background); // get next command
	
		if (setup_status != 0) {
			printf("\nSomething in setup() went wrong\n");
			continue;
		}

		// user enters an empty line
		if (!args[0]) {
			continue;
		}		

		// custom commands below
		if (strcmp(args[0], "r") == 0) {
			is_historic = retrieve_command(inputBuffer, args);
			continue;
		
		} else if (strcmp(args[0], "history") == 0) {
			print_history();
			continue;
		
		} else if (strcmp(args[0], "cd") == 0) {
			chdir(args[1]);
			continue;

		} else if (strcmp(args[0], "jobs") == 0) {
			list_background_jobs();
			continue;
		
		} else if (strcmp(args[0], "fg") == 0) {
			foreground(args); // find background job and send to foreground
			continue;

		} else if (strcmp(args[0], "exit") == 0) {
			exit(0);
		} 

		// no need to implement "pwd" since shell does it for me

		pid_t pid = fork();
		int status = 0; // child process status

		if (pid == 0) { // child process

			setpgid(0, 0);
			status = execvp(args[0], args);

			// execvp only returns when an error occurs
			if (status != 0) {
				printf("Invalid command.\n");
				exit(EXIT_FAILURE);
			}

			exit(EXIT_SUCCESS);

		} else if (pid > 0) { // parent process

			if (background == 0) { // foreground
				
				waitpid(pid, &status, 0); // parent will wait for child
				if (status != 0) {
					invalidate_last();
				}

			} else { // background &
				add_job(pid, strdup(args[0]));	
			}

		} else {
			exit(EXIT_FAILURE);
		}

	}
}
