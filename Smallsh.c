#include "Smallsh.h" /* include file for example */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* program buffers and work pointers */
static char inpbuf[MAXBUF], tokbuf[2*MAXBUF], *ptr = inpbuf, *tok = tokbuf;
int intr_p = 0;
char  dir[256];
char *prompt =  " Command>"; /* prompt */
int fg_pid = 0;

int userin(p) /* print prompt and read a line */
char *p;
{
  int c, count;
  /* initialization for later routines */
  ptr = inpbuf;
  tok = tokbuf;

  /* display prompt */
  printf("%s ", strcat(getcwd(dir, 256), p));

  for(count = 0;;){
    if((c = getchar()) == EOF)
      return(EOF);

    if(count < MAXBUF)
      inpbuf[count++] = c;

    if(c == '\n' && count < MAXBUF){
      inpbuf[count] = '\0';
      return(count);
    }

    /* if line too long restart */
    if(c == '\n'){
      printf("smallsh: input line too long\n");
      count = 0;
      printf("%s ", p);
    }
  }
}


static char special[] = {' ', '\t', '&', ';', '\n', '\0'};

int inarg(c) /* are we in an ordinary argument */
char c;
{
  char *wrk;
  for(wrk = special; *wrk != '\0'; wrk++)
    if(c == *wrk)
      return(0);

  return(1);
}

int gettok(outptr) /* get token and place into tokbuf */
char **outptr;
{
  int type;

  *outptr = tok;

  /* strip white space */
  for(;*ptr == ' ' || *ptr == '\t'; ptr++)
    ;

  *tok++ = *ptr;

  switch(*ptr++){
    case '\n':
      type = EOL; break;
    case '&':
      type = AMPERSAND; break;
    case ';':
      type = SEMICOLON; break;
    default:
      type = ARG;
      while(inarg(*ptr))
        *tok++ = *ptr++;
  }

  *tok++ = '\0';
  return(type);
}

/*SIGINT와 SIGQUIT를 Catch하는 핸들러 함수입니다*/
void cat_sig(int signo){
  if(signo == SIGINT)
  {
    printf("\nsmallsh is terminated by SIGINT\n");
    exit(1);
  }
  else if(signo == SIGQUIT)
  {

    printf("\nsmallsh is terminated by SIGQUIT\n");
    exit(1);
  } 
}

void handle_int(int s) {
  int c;
  if(!fg_pid) {
    /* ctrl-c hit at shell prompt */
    return;
  }
  if(intr_p) {
    printf("\ngot it, signalling\n");
    kill(fg_pid, SIGTERM);
    fg_pid = 0;
  } else {
    printf("\nignoring, type ^C again to interrupt\n");
    intr_p = 1;
  }
}

/* execute a command with optional wait */
int runcommand1(cline, where)
char **cline;
int where;
{
  int pid, exitstat, ret;

  /* ignore signal (linux) */
  struct sigaction sa_ign, sa_conf;
  sa_ign.sa_handler = SIG_IGN;
  sigemptyset(&sa_ign.sa_mask);
  sa_ign.sa_flags = SA_RESTART;

  sa_conf.sa_handler = cat_sig;
  sigemptyset(&sa_conf.sa_mask);
  sa_conf.sa_flags = SA_RESTART;


  if((pid = fork()) < 0){
    perror("smallsh");
    return(-1);
  }

  if(pid == 0){
    sigaction(SIGINT, &sa_ign, NULL);
    sigaction(SIGQUIT, &sa_ign, NULL);

    execvp(*cline, cline);
    perror(*cline);
    exit(127);
  } else {
    fg_pid = pid;
  }

  /* code for parent */
  /* if background process print pid and exit */
  if(where == BACKGROUND){
    fg_pid = 0;
    printf("[Process id %d]\n", pid);
    return(0);
  }

  /* wait until process pid exits */
  sigaction(SIGINT, &sa_conf, NULL);
  sigaction(SIGQUIT, &sa_conf, NULL);

  while( (ret=wait(&exitstat)) != pid && ret != -1)
    ;

  fg_pid = 0;
  return(ret == -1 ? -1 : exitstat);
}

//PROC가 두개일경우 실행하는 함수입니다
int runcommand2 (char *com1[], char *com2[]){
  int p[2], status;
  
  switch (fork())
  {
  case -1:   ("1st fork call in join");
  case 0: break;
  default: wait(&status); return(status);
  }
  
  if (pipe(p) == -1) perror("pipe call in join");

  switch (fork())
  {
  case -1: perror ("2nd fork call in join");
  case 0:
    dup2 (p[1],1); /*표준 출력이 파이프로 가게 한다*/
    close (p[0]);  /*화일 기술자를 절약한다. */
    close (p[1]);
    execvp (com1[0], com1); /* com1: ls */
    perror("1st execvp call in join");
  default:
    dup2(p[0], 0); /* 표준 입력이 파이프로부터 오게 한다 */
    close (p[0]);
    close (p[1]);
    execvp (com2[0], com2); /* com2: grep */
    perror("2nd execvp call in join");
  }
}


void procline() /* process input line */
{
  char *pipe1[MAXARG+1];
  char *pipe2[MAXARG+1];
  char *arg[MAXARG+1]; /* pointer array for runcommand */
  int toktype; /* type of token in command */
  int narg; /* numer of arguments so far */
  int type; /* FOREGROUND or BACKGROUND? */
  int p_chk = 0;
  int i, j, k, l;

  /* reset intr flag */
  intr_p = 0;

  for(narg = 0;;){ /* loop forever */
    /* take action according to token type */
    switch(toktype = gettok(&arg[narg])){
      case ARG:
        if(narg < MAXARG)
          narg++;
        break;

      case EOL:
      case SEMICOLON:
      case AMPERSAND:
        type = (toktype == AMPERSAND) ? BACKGROUND : FOREGROUND;

        if(narg != 0){
          arg[narg] = NULL;
          for(i = 0;  i < narg; i++)
          {
            if(!strcmp(arg[i], "|")) /* '|'를 발견하면 pipe에 들어갈 두개의 문자열을 각각 설정합니다 */
            {
              for(j = 0; j < i; j++)
              {
                pipe1[j] = arg[j]; /* '|'앞의 문자열들을 저장 */
              }
              for(k = i + 1; k < narg; k++)
              {
                pipe2[k - i - 1] = arg[k]; /* '|'앞의 문자열들을 저장 */
              }
              p_chk = 1;
            }
          }

         i = 0;
         j = 0;
         k = 0;

          if(p_chk == 0)
          {
            if(!strcmp(arg[0], "cd")) /* cd를 입력받으면 cd command를 수행합니다 */
            {
              chdir(arg[1]); 
            }
            else
            {
              runcommand1(arg, type);/* PROC가 한개일 경우 파이프를 돌리지 않는 runcommand1을 수행합니다 */
            }
          }
          else
          {
            runcommand2(pipe1, pipe2);/* PROC가 두개일 경우 파이프를 돌리는 runcommand2를 수행합니다 */
            memset(pipe1, 0, sizeof(pipe1));
            memset(pipe2, 0, sizeof(pipe2));
          }
        }

        if(toktype == EOL)
          return;

        narg = 0;
        break;
    }
  }
}

int main()
{
  /* sigaction struct (linux) */
  struct sigaction sa;

  sa.sa_handler = cat_sig;/*시그널 핸들러를 시그널 캐치하는 핸들러로 설정합니다*/
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;

  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);

  while(userin(prompt) != EOF)
    procline();
}