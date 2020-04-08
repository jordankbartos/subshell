/*******************************************************************************
 * Title: smallsh
 * Author: Jordan K Bartos
 * Date: November 11, 2019
 *
 * Description: This program is a very basic shell program. It has three build-
 *   in commands:
 *     1) exit - terminates the shell and any child processes
 *     2) status - prints the exit status/signal of the last foreground process
 *     3) cd - changes the working directory
 *
 *   It also performs other non built-in commands either in the foreground or
 *   the background. It can also support file redirection with > and < 
 *   operators.
 * 
 *   Send a SIGINT signal to the shell to terminate a foreground process, but
 *   not the shell. Send a SIGTSTP signal to the shell to disable the ability to
 *   run processes in the shell
 *   
*******************************************************************************/
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

/* preprocessor defines, global variables, and flags */
#define TRUE 1
#define FALSE 0

#define ARBITRARY_MAX_WORD_LENGTH 200
#define MAX_INPUT_SIZE 2052
#define MAX_NUMBER_ARGS 512

#define NUM_BUILT_INS 3
#define CD_CODE 0
#define STATUS_CODE 1
#define EXIT_CODE 2
#define COMMENT_CODE 3

char builtInCommands[NUM_BUILT_INS][24] = {"cd\0", "status\0", "exit\0"};
char inputRedirectionFileName[ARBITRARY_MAX_WORD_LENGTH];
char outputRedirectionFileName[ARBITRARY_MAX_WORD_LENGTH];

int background_allowed = TRUE;
int previous_background_allowed = TRUE;
int inputRedirectionFlag = 0;
int outputRedirectionFlag = 0;
int backgroundFlag = 0;
int foregroundProcessRunning = 0;


/*******************************************************************************
 *               void stringFromInt(int value, char* string)
 * Description: converts a decimal integer to a string
 * Input:
 *   int value - the number to convert to a string
 *   char* string - the string into which the results will be stored
 * Output:
 *   None. Results are stored in char* string
 * Limitation:
 *   Only converts numbers up to 99. Always writes 2 digits to string
*******************************************************************************/
void stringFromInt(int value, char* string) {
  int ones = value % 10;
  int tens = value / 10;

  /* if the value is less than 10, only use one digit */
  if(value < 10) {
    string[0] = ones + 48;
  }
  /* else use both digits */
  else {
    string[0] = tens + 48;
    string[1] = ones + 48;
  }
}


/*******************************************************************************
 *                 int getStringLength(char* string)
 * determines the length of a string and is re-entrant unlike strlen()
 * Input: char* - a pointer to a string of chars which is null-terminated
 * Output: int, the length of the string before a null-terminator was reached
*******************************************************************************/
int getStringLength(char* string) {
  int len = 0;
  while(string[len] != '\0') {
    ++len;
  }
  return len;
}


/*******************************************************************************
 *                  void catchSIGINT(int sigNumber)
 * Description: this function handles a SIGINT call. It prevents the termination
 *   of the shell program while child processes are killed. 
*******************************************************************************/
void catchSIGINT(int sigNumber) {
  return;
}


/*******************************************************************************
 *                  void shellCatchSIGTSTP(int sigNumber)
 * Description: this function handles a SIGTSTP signal received by the shell. 
 *   It prevents the termination of the shell program and prints an informative
 *   message. It also flips background_allowed to the opposite value. Either
 *   allowing or dis-allowing background execution of programs
*******************************************************************************/
void shellCatchSIGTSTP(int sigNumber) {
  /* flip background_allowed to opposite value */
  background_allowed ^= 1;

  /* if a foreground process is currently running, wait for it to complete
     before printing a message */
  char message[100];
  memset(message, '\0', 100);
  if(foregroundProcessRunning == TRUE) {
    return;
  }
  /* if a foreground process is not currently running, print a message about
     whether background processes are allowed immediately */
  else if (background_allowed == 0) {
    strcpy(message, "\nEntering foreground-only mode (& is now ignored)\n:");
  }
  else {
    strcpy(message, "\nExiting foreground-only mode\n:");
  }
  write(STDOUT_FILENO, message, getStringLength(message));
  /* set the duel flags equal to eachother so that the message will not be
     repeated by the main shell program */
  previous_background_allowed = background_allowed;
  return;
}
 


/*******************************************************************************
 *                        struct processArray
 * This struct will hold data about the currently running background processes.
 * It functions as a dynamic array so that as many background processes as 
 * needed may be run and the program can track them all.
*******************************************************************************/

typedef struct processArray {
  int capacity;
  int size;
  int * array;
} processes;


/*******************************************************************************
 *                       processes* createProcessArray()
 * Returns a pointer to a fully initialized processArray 
 * Input:
 *   none
 * Output:
 *   A pointer to a new processes struct that is initalized with valid data
*******************************************************************************/
processes* createProcessArray() {
  /* allocate a new processArray and set its member variables accordingly */
  processes* newProcesses = malloc(sizeof(processes));
  newProcesses->capacity = 10;
  newProcesses->size = 0;

  /* allocate memory for the dynamic array and set its contents to all 0*/
  newProcesses->array = malloc(sizeof(int) * newProcesses->capacity);
  int i;
  for(i = 0; i < newProcesses->capacity; ++i) {
    newProcesses->array[i] = 0;
  }

  return newProcesses;
}


/*******************************************************************************
 *                       process printProcesses(processes*)
 * prints all of the pids in the processes array
 * Input:
 *   pointer to processes
 * Output:
 *   none
 *   displays current bg process ids to stdout
*******************************************************************************/
void printProcesses(processes* procs) {
  int i = 0;
  for(i = 0; i < procs->capacity; ++i) {
    printf("procs[%d]: %d\n", i, procs->array[i]);
    fflush(stdout);
  }
}


/*******************************************************************************
 *                      void destroyProcessArray(proc)
 * deallocates memory for proc and sets to NULL
 * Input: 
 *   a pointer to a processes struct
 * Output: 
 *   none
*******************************************************************************/
void destroyProcessArray(processes* procs) {
  assert(procs != NULL);

  free(procs->array);
  free(procs);
  procs = NULL;
}


/*******************************************************************************
 *                       void doubleCapacity(processes*)
 * doubles the capacity of the dynamic array managed by the processes struct
 * Input:
 *   a pointer to a processes struct
 * Output:
 *   none
 *   doubles the capacity of the dynamic array
*******************************************************************************/
void doubleCapacity(processes* proc) {
  /* allocate new memory for the processes array and initialize all values 
     to 0*/
  int* newArray = malloc(sizeof(int) * proc->capacity * 2);
  int i;
  for(i = 0; i < proc->capacity * 2; ++i) {
    newArray[i] = 0;
  }

  /* copy all values from old array over to the new array */
  for(i = 0; i < proc->capacity; ++i ) {
    newArray[i] = proc->array[i];
  }

  /* deallocate old array, and reset pointer to the new array */
  free(proc->array);
  proc->array = newArray;
  proc->capacity *= 2;
}


/*******************************************************************************
 *                   void processesAdd(processes*, int)
 * adds a pid to the processes array
 * Input:
 *   processes * proc - a pointer to a precesses struct dynamic array
 *   int val - the value (pid) to be added to the array
*******************************************************************************/
void processesAdd(processes* proc, int val) {
  assert(proc != NULL);

  int index = 0;

  /* double the capacity if the array is maxed out */
  if(proc->capacity == proc->size) {
    doubleCapacity(proc);
    /* ensure that the second half of the array is set to all 0s. I though
       I took care of that in doubleCapacity, but now I'm afraid to change
       this */
    for(index = proc->size; index < proc->capacity; ++index) {
      proc->array[index] = 0;
    }
  }

  /* find the next empty slot to put the new pid */
  index = 0;
  while(index < proc->capacity) {
    if(proc->array[index] == 0) {
      proc->array[index] = val;
      proc->size += 1;
      return;
    }
    index++;
  }
}


/*******************************************************************************
 *               void processesRemove(processes*, int)
 * finds the pid in the array and removes it from the dynamic array
 * Input: 
 *   processes* proc: a poniter to a processes struct dynamic array
 *   int val - the pid of the process to be removed
*******************************************************************************/
void processesRemove(processes* proc, int val) {
  assert(proc != NULL);

  /* search for the value in the array */
  int i;
  for(i = 0; i < proc->capacity; ++i) {
    if(proc->array[i] == val) {
      int j;
      /* if it is found, filter the rest of the values down so there isn't a
         gap in the array */
      for(j = i + 1; j < proc->capacity; ++j) {
        proc->array[j -1] = proc->array[j];
      }
      /* set the final value to 0, decrement the size and return */
      proc->array[j - 1] = 0;
      proc->size -= 1;
      return;
    }
  }
}



/*******************************************************************************
 *                   void setInterrupts()
 * Description: sets up all the interrupts for int main()
 * This code is taken from CS344 Lectures in block 3 by Benjamin Brewster
*******************************************************************************/
void setInterrupts() {
  /* SIGINT handler */
  /* the SIGINT handler will block all other interrupts until it is finished,
     and system calls will restart after the handler returns.*/
  struct sigaction SIGINT_action = {{0}};
  SIGINT_action.sa_handler = catchSIGINT;
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_flags = SA_RESTART;
  sigaction(SIGINT, &SIGINT_action, NULL);

  /* SIGTSTP handler */
  /* the SIGTSTP handler will block all other interrupts until it is finished,
     and system calls will restart after the handler returns.*/
  struct sigaction SIGTSTP_action = {{0}};
  SIGTSTP_action.sa_handler = shellCatchSIGTSTP;
  sigfillset(&SIGTSTP_action.sa_mask);
  SIGTSTP_action.sa_flags = SA_RESTART;
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);

  return;
}


/*******************************************************************************
 *                          displayBGMessage
 * Description: detects a change in the background allowed flag and displays a 
 *   message if it has flipped
 * Input: 
 *   none
 *   All of the information this function needs is stored in two global flag
 *   variables
 * Output:
 *   none
 *   displays messages to stdout
*******************************************************************************/
void displayBGMessage() {
  /* if the flags are equal, then no change in background allowed state has
     occured since a message was printed to the user. Do nothing. */
  if(background_allowed == previous_background_allowed) {
    return;
  }

  /* else we know the flag has flipped. Print the appropriate message and update
     the value of previous flag */
  else if( background_allowed == 0 ) {
    printf("\nEntering foreground-only mode (& is now ignored)\n");
    fflush(stdout);
  }
  else {
    printf("\nExiting foreground-only mode\n");
    fflush(stdout);
  }
  previous_background_allowed = background_allowed;
}


/*******************************************************************************
 *                void prompt(char* promptInput, size_t bufferSize)
 * prints a prompt and gets user input for the next command.
*******************************************************************************/
void prompt(char** promptInput, size_t bufferSize) {
  displayBGMessage();
  /* display initial prompt */
  write(STDOUT_FILENO, ":", 1);
  /* while getline fails, keep printing prompts and attempt to collect input*/
  while(getline(promptInput, &bufferSize, stdin) == -1) {
    clearerr(stdin);
    displayBGMessage();
    write(STDOUT_FILENO, ":", 1);
  }
  /* remove the trailing \n from getline. this method is taken from the CS344
     reading 3.3 at Oregon State University which can be found here:
     https://oregonstate.instructure.com/courses/1738958/pages/3-dot-3-
                                                advanced-user-input-with-getline
  */
  (*promptInput)[strcspn(*promptInput, "\n")] = '\0';

}


/*******************************************************************************
 *                   bool isBuiltIn(char*)
 * Determines if a command is a built-in command
 * Input: char* userInput
 * Output: 
 *   The index of the built-in commands as defined at the top of this file, or
 *   -1 if the input is not in the list of built-in commands.
*******************************************************************************/
int isBuiltIn(char* userInput) {
  
  /* if userInput begins with '#', return COMMENT_CODE */
  /* treat no input the same as comments */
  if (userInput == NULL || userInput[0] == '#' || userInput[0] == '\0') {
    return COMMENT_CODE;
  }
  

  /* determine which index of BUILT_IN_COMMANDS it is, if any */
  int i;
  for(i = 0; i < NUM_BUILT_INS; ++i) {
    if(strcmp(userInput, builtInCommands[i]) == 0) {
      return i;
    }
  }

  return -1;
}

/*******************************************************************************
 *                     void clearArgs(char** args)
 * frees all the memory for dynamically allocated args
*******************************************************************************/
void clearArgs(char** args) {
  int i;
  for(i = 0; i < MAX_NUMBER_ARGS; ++i) {
    if(args[i] != NULL) {
      free(args[i]);
      args[i] = NULL;
    }
    args[i] = NULL;
  }
}


/*******************************************************************************
 *                         void getArgs(char*, char**)
 * Description: takes the user input and parses it into individual words which
 *   populate the char** args when the funcion ends
 * Input:
 *   - char* promptInput - a string of text containing all of the command-line 
 *       args
 *   - char** args - a pointer to an array of char*s which will be populated
 *       with the arguments
*******************************************************************************/
void getArgs(char* promptInput, char** args) {
  int argsIndex = 0;
  int promptIndex = 0;
  int currWordIndex = 0;
  char* currWord = malloc(sizeof(char) * ARBITRARY_MAX_WORD_LENGTH);
  memset(currWord, '\0', ARBITRARY_MAX_WORD_LENGTH);

  /* clear out args array before beginning */
  clearArgs(args);
    
  while( promptInput[promptIndex] != '\0') {
    /* if the character is not white-space, add it to the current word */
    if (promptInput[promptIndex] != ' ' && promptInput[promptIndex] != '\t') {
      currWord[currWordIndex] = promptInput[promptIndex];
      currWordIndex++;
    }
    /* else the current character is white-space and it's time to save the 
       current word and begin a new one */
    else {
      /* copy the word */
      currWord[currWordIndex] = '\0';
      args[argsIndex] = malloc(sizeof(char) * (currWordIndex + 1));
      assert(args[argsIndex] != NULL);
      strcpy(args[argsIndex], currWord);
      argsIndex += 1;
      /* reset currWord and currWordIndex */
      memset(currWord, '\0', ARBITRARY_MAX_WORD_LENGTH);
      currWordIndex = 0;
    }
    promptIndex += 1;
  }

  /* get the last word */
  currWord[currWordIndex] = '\0';
  args[argsIndex] = malloc(sizeof(char) * (currWordIndex + 1));
  assert(args[argsIndex] != NULL);
  strcpy(args[argsIndex], currWord);

  free(currWord);
}


/*******************************************************************************
 *                 char** initializeArgs()
 * Description: sets all pointers in args to NULL
 * Input: 
 *   none
 * Output: 
 *   a pointer to an array of char* set to NULL
*******************************************************************************/
char** initializeArgs() {
  /* malloc space for MAX_NUMBER_ARGS in args */
  char** args = malloc(sizeof(char*) * MAX_NUMBER_ARGS);
  assert(args != NULL);

  /* set each to NULL */
  int i;
  for(i = 0; i < MAX_NUMBER_ARGS; ++i) {
    args[i] = NULL;
  }
  
  return args;
}


/*******************************************************************************
 *                     void destroyArgs(char**)
 * Description: frees all memory allocated for the arguments array
 * Input: char** args - pointer to array of char*
 * Output: none
*******************************************************************************/
void destroyArgs(char** args) {
  clearArgs(args);
  free(args);
  args = NULL;
}

/*******************************************************************************
 *                   int isWord(char*) 
 * Returns true if the char* is not a special argument character and if it is
 * not null. Otherwise returns false
*******************************************************************************/
int isWord(char* word) {

  /* if the word is empty (because either the pointer is set to null or 
     the string is empty), return false */
  if(word == NULL) {
    return FALSE;
  }
  if(word[0] == '\0') {
    return FALSE;
  }

  /* if the word begins with a special character, return false */
  if(word[0] == '<' || word[0] == '>' || word[0] == '&') {
    if(word[1] == '\0') {
      return FALSE;
    }
  }

  /* it must be a "word" for the purposes of our program. Return true */
  return TRUE;
}


/*******************************************************************************
 *         argsFilterDown(char** args,int actualIndex, int examineInex)
 * Filters down the arguments to fill in the gaps left by file redirection
 * commands and the like
 * Input:
 *   char** args - the pointer to the list of arguments (char*s)
 *   int* actualIndex - the index at which the argument at examine index should
 *     be placed
 *   int* examineIndex - the index of the argument that is being moved down
*******************************************************************************/
void argsFilterDown(char** args, int* actualIndex, int* examineIndex) {
  /* make sure the two arguments are not the same and that the one being
     replaced is not null */
  if (args[*actualIndex] != args[*examineIndex]) {
    if (args[*actualIndex] != NULL) {
      /* free dynamically allocated memory */
      free(args[*actualIndex]);
    }
    /* perform the swap and */
    args[*actualIndex] = args[*examineIndex];
    args[*examineIndex] = NULL;
  }
  /* increment both indices */
  *actualIndex += 1;
  *examineIndex += 1;
}


/*******************************************************************************
 *                       replaceDoubleDollars(char*)
 * This function examines a string of characters four instances of '$$'. If '$$'
 * is found, it is replaced with the pid of the currently running process.
*******************************************************************************/
void replaceDoubleDollars(char** arg) {
  assert(arg != NULL);
  assert(*arg != NULL);
  int index = 0;
  /* iterate through the list until the next index is the null terminator. Since
     we must examine one index ahead for the double $$ */
  while (((*arg)[index] != '\0') && ((*arg)[index + 1] != '\0')) {
    if((*arg)[index] == '$' && (*arg)[index + 1] == '$') {
      
      /* get length of original arg, allocate memory for a newArg, get the pid
         as a string */
      int lenNewArg = getStringLength(*arg) + 20;
      char* newArg = malloc(sizeof(char) * lenNewArg);
      memset(newArg, '\0', lenNewArg);

      char pidString[20];
      memset(pidString, '\0', 20);
      sprintf(pidString, "%d", getpid());
      int pidLen = getStringLength(pidString);
      
      /* copy the argument up until the $$ */
      int subIndex = 0;
      for(subIndex = 0; subIndex < index; subIndex++) {
        newArg[subIndex] = (*arg)[subIndex];
      }

      /* add the pid */
      strcpy(newArg + index, pidString);
      /* copy the rest of the arg minus the two $ characters being replaced*/
      strcpy(newArg + index + pidLen, (*arg) + index + 2);

      /* rearrange the pointers to replace old arg with new arg and free dyn
         memory */
      free(*arg);
      *arg = newArg;
      return;
    }
    index += 1;
  }
}

/*******************************************************************************
 *                    void parseArgs(char**)
 * Description: This function examines the list of arguments and does two things
 *   1) Looks for special operators - such as file redirection or background
 *      commands. In which case it sets flags and sets relevent global 
 *      variables
 *   2) Rearranges the arguments in args to move filter relevent commands down
 *      torwards args[0] as the special operators and their arguments are
 *      removed
 * Input:
 *   char** args - the array of pointers to characters which are the processed
 *     user input
 * Output:
 *   none.
 *   modifies char** args 
*******************************************************************************/
void parseArgs(char** args) {
  int examineIndex = 0;
  int actualIndex = 0;
  
  while(args[examineIndex] != NULL) {

    /* if args at examine index is '<', then set file redirection input */
    if(strcmp(args[examineIndex], "<") == 0) {
      if(isWord(args[examineIndex + 1])) {
        /* then examineIndex + 1 is the name of a file for file input 
           redirection. Set the flag and save the file name*/
        inputRedirectionFlag = 1;
        memset(inputRedirectionFileName, '\0', ARBITRARY_MAX_WORD_LENGTH);
        strcpy(inputRedirectionFileName, args[examineIndex + 1]);

        /* rearrange current working indecies */
        examineIndex += 2;
      }
      /* if the next argument is not a minimally valid word, then we will treat
         this special character as a regular argument. */
      else {
        argsFilterDown(args, &actualIndex, &examineIndex);
      }
    }

    /* if args at examine index is '>', then set file redirection output */
    else if (strcmp(args[examineIndex], ">") == 0) {
      if(isWord(args[examineIndex + 1])) {
        /* then examineIndex + 1 is the name of a file for file output 
           redirection. Set the flag and save the file name*/
        outputRedirectionFlag = 1;
        memset(outputRedirectionFileName, '\0', ARBITRARY_MAX_WORD_LENGTH);
        strcpy(outputRedirectionFileName, args[examineIndex + 1]);

        /* rearrange current working indecies */
        examineIndex += 2;
      }
      /* if the next argument is not a minimally valid word, then we will treat
         this special character as a regular argument. */
      else {
        argsFilterDown(args, &actualIndex, &examineIndex);
      }
    }
    /* replace && with pid number */
    else if (strcmp(args[examineIndex], "$$") == 0) {
      char* pidString = malloc(sizeof(char) * 12);
      memset(pidString, '\0', 12);
      sprintf(pidString, "%d", getpid());
      free(args[examineIndex]);
      args[examineIndex] = pidString;
    }

    /* if args at examine index is '&', then set background flag */
    else if (strcmp(args[examineIndex], "&") == 0) {
      /* if the & is the last argument, set the background flag */
      if(args[examineIndex + 1] == NULL) {
        backgroundFlag = TRUE;
        if(background_allowed == FALSE) {
          backgroundFlag = FALSE;
        }
        examineIndex += 1;
      }
      /* if it isn't the last argument, treat it like a normal argument */
      else {
        argsFilterDown(args, &actualIndex, &examineIndex);
      }
    }

    /* else, the current argument being analyzed should be treated as a regular
       argument. Filter it down to its spot in args */
    else {
      /* search for '$$' in the string, and replace it with the pid if it is there */
      replaceDoubleDollars(&args[examineIndex]);
      argsFilterDown(args, &actualIndex, &examineIndex);
    }
  }
  if(args[actualIndex] != NULL) {
    free(args[actualIndex]);
  }
  args[actualIndex] = NULL;
}


/*******************************************************************************
 *                        void resetFlags()
 * resets the input, output, and background flags
*******************************************************************************/
void resetFlags() {
  inputRedirectionFlag = 0;
  outputRedirectionFlag = 0;
  backgroundFlag = 0;
}


/*******************************************************************************
 *                       void changeDirectory(char* path)
 * Changes the directory to the path name specified
*******************************************************************************/
void changeDirectory(char* path) {
  if(path == NULL) {
    chdir(getenv("HOME"));
  }
  else {
    chdir(path);
  }
}


/*******************************************************************************
 *                         struct Results
 * A struct to hold the code and a boolean value indicating whether the previous
 * process was terminated by signal or not
*******************************************************************************/
typedef struct Results {
  int code;
  int sig;
} result;


/*******************************************************************************
 *                        void spawnProcess(args)
 * Description: spawns a new process via fork(), then sets the appropriate file
 *   handlers if the relevent flags are set and then calls exec(), calling the
 *   new process
 * Input: list of arguments
 * Output: none
*******************************************************************************/
void spawnProcess(char** args, processes* procs, result* status) {
  assert(args != NULL);
  int results = 0;
  int outputFile;
  int inputFile;

  /* create a copy of the current process */
  int pid = fork();

  switch (pid) {
    /* pid is -1, error */
    case -1:
      perror("fork() failed.\n");
      break;

    /* pid is 0, child process */
    case 0:
      /* set the child process to ignore SIGTSTP interrupts */
      signal(SIGTSTP, SIG_IGN);

      /* set file redirection if flags are set */
      /* set input redirection */
      if(inputRedirectionFlag) {
        inputFile = open(inputRedirectionFileName, O_RDONLY);
        
        /* print error messages and exit child process if file redirection 
           is invalid */
        if(inputFile == -1) {
          printf("cannot open %s for input\n", inputRedirectionFileName);
          fflush(stdout);
          exit(1);
        }
        dup2(inputFile, 0);
      }
      else if(backgroundFlag) {
        inputFile = open("/dev/null", O_RDONLY);
        dup2(inputFile, 0);
      }

      /* set output redirection */
      if(outputRedirectionFlag) {
        outputFile = open(outputRedirectionFileName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        dup2(outputFile, 1);
      }
      else if(backgroundFlag) {
        outputFile = open("/dev/null", O_WRONLY | O_CREAT, 0666);
        dup2(outputFile, 1);
      }

      /* call exec to execute other program, preserving file redirection */
      if(execvp(args[0], args) == -1) {
        char errorString[100];
        memset(errorString, '\0', 100);
        strcpy(errorString, strerror(errno));
        errorString[0] += 32;
        printf("%s: %s\n", args[0], errorString);
        fflush(stdout);
        exit(1);
      }
      
    /* pid is a valid pid of the child process */
    /* this is the parent */
    default:
      /* call waitpid() if the background flag is not set. Otherwise resume
         normal operation */
      if(backgroundFlag) {
        printf("background pid is %d\n", pid);
        fflush(stdout);
        processesAdd(procs, pid);
      }
      else {
        /* set and unset flags to prevent signal handler output during waitpid()
           blocking */
        foregroundProcessRunning = TRUE;
        waitpid(pid, &results, 0);
        foregroundProcessRunning = FALSE;

        /* get the status of the terminated process */
        if (WIFEXITED(results) != 0) {
          status->sig = FALSE;
          status->code = WEXITSTATUS(results);
        }  
        else if(WIFSIGNALED(results) != 0) {
          status->sig = TRUE;
          status->code = WTERMSIG(results);
          printf("terminated by signal %d\n", status->code);
          fflush(stdout);
        }
      }
      break;
  };
}
  

/*******************************************************************************
 *                          void cleanupProcs(procs)
 * Iterates through the processes struct dynamic array and checks if the
 * processes therin have completed. If they have, a message is printed to 
 * stdout
 * Input:
 *   processes* pointer to processes struct
 * Output:
 *   return: none
 *   processes struct is modified as processes terminate
 *   messages are printed to the screen
*******************************************************************************/
void cleanupProcs(processes* procs) {
  int i;
  int results;
  int pid;
  int signal;
  int code;

  /* iterate through the processes dynamic array */
  for(i = 0; i < procs->size; ++i) {
    /* check the status of each without blocking */
    pid = waitpid(procs->array[i], &results, WNOHANG);
    /* if the bg process terminated since the last check,
       check the exit status */
    if(pid > 0) {
      /* get the exit status */
      if (WIFEXITED(results) != 0) {
        signal = FALSE;
        code = WEXITSTATUS(results);
      }
      else if (WIFSIGNALED(results) != 0) {
        signal = TRUE;
        code = WTERMSIG(results);
      }

      /* and display the results */
      printf("background pid %d is done: ", pid);
      if(signal) {
        printf("terminated by signal %d\n", code);
      }
      else {
        printf("exit value %d\n", code);
      }
      fflush(stdout);

      /* remove the process from the processes array */
      processesRemove(procs, pid);
      /* decrement i to account for the recuction in size and altered
         placement of items in the dynamic array */
      i--;
    }
  }
}


/*******************************************************************************
 *                          int main()
 * Description: begins the execution of smallsh
 * Input: none
 * Output: int - exit status
*******************************************************************************/
int main() {
  /* initialize variables and structs */
  char* promptInput = NULL;                /*char* to hold prompt input args */
  size_t inputBuffer = MAX_INPUT_SIZE;     /*size of maximum allowed input */
  int pid = getpid();                      /*pid of current smallsh process*/
  result status;                   /*empty results struct*/
  processes* procs = createProcessArray(); /*dyn array of bg processes */
  char** args = initializeArgs();          /*initialize array of arguments */


  /* sets the interrupt handlers for smallsh */
  setInterrupts();

  /* display the pid of smallsh */
  printf("shallsh pid: %d\n", pid);
  fflush(stdout);

  while(TRUE){
    /* look for terminated bg processes and display termination info for them */
    cleanupProcs(procs);
    /* display a prompt and collect input and process the input */
    prompt(&promptInput, inputBuffer);
    getArgs(promptInput, args);
    parseArgs(args);
    int commandCode = isBuiltIn(args[0]);

    /* switch to execute the proper command */
    switch(commandCode) {
      /* if the first arg was cd, call change directory with the path */
      case CD_CODE:
        changeDirectory(args[1]);
        break;

      /* if the code was status, display the exit code of the most recently term
         inated fg process */
      case STATUS_CODE:
        if(status.sig == FALSE) {
          printf("exit value %d\n", status.code);
          fflush(stdout);
        }
        else {
          printf("terminated by signal %d\n", status.code);
          fflush(stdout);
        }
        break;

      /* if exit was the argument, clean up the program and exit */
      case EXIT_CODE:
        free(promptInput);
        destroyArgs(args);
        destroyProcessArray(procs);
        exit(0);
        break;
  
      /* if the command was a comment, do nothing */
      case COMMENT_CODE:
        break;

      /* if it was none of these then spawn a new process to attempt to execute
         the command*/
      default:
        spawnProcess(args, procs, &status);
        break;

    };
    /* reset the flags at the end of every loop */
    resetFlags();
  }
}


/*******************************************************************************
*******************************************************************************/
