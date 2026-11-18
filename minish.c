#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_ARGS 128
#define MAX_CMDS 32
#define MAX_JOBS 64
#define LINE_SIZE 4096

typedef struct { char *argv[MAX_ARGS]; char *in, *out, *append, *err, *err_append, *heredoc; } Command;
typedef struct { int id; pid_t pgid, last_pid; char *line; int running, notified, remaining, stopped; } Job;
typedef struct { pid_t pid; int job_id; } Child;
typedef struct { char *name; char **body; int lines; } Function;
static Job jobs[MAX_JOBS];
static Child children[MAX_JOBS*MAX_CMDS];
static Function functions[32];static int function_count=0;
static int next_job=1, interactive=0;
static char *history[1000]; static size_t history_count=0;
static char *alias_names[64], *alias_values[64]; static int alias_count=0;
static int last_status=0;
static int trace_mode=0;
static int foreground_waiting=0;
static pid_t shell_pgid;
static struct termios shell_tmodes;

static void reap_jobs(void);
static void trim_job(Job *j) { free(j->line); memset(j,0,sizeof(*j)); }
static Job *find_job_id(int id) { for(int i=0;i<MAX_JOBS;i++) if(jobs[i].id==id) return &jobs[i]; return NULL; }
static Job *add_job(pid_t p,const char *line,int running,int count,pid_t *pids) {
  for(int i=0;i<MAX_JOBS;i++) if(!jobs[i].id) { jobs[i]=(Job){.id=next_job++,.pgid=p,.last_pid=count?pids[count-1]:0,.line=strdup(line),.running=running,.remaining=count}; for(int k=0;k<count;k++)for(size_t z=0;z<sizeof(children)/sizeof(children[0]);z++)if(!children[z].pid){children[z]=(Child){pids[k],jobs[i].id};break;} return &jobs[i]; }
  fprintf(stderr,"mshell: job table full\n"); return NULL;
}
static void child_signal_defaults(void) { int sigs[]={SIGINT,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU,SIGCHLD}; for(size_t i=0;i<sizeof(sigs)/sizeof(*sigs);i++) signal(sigs[i],SIG_DFL); }
static void init_shell(void) {
  interactive=isatty(STDIN_FILENO);
  if(!interactive) return;
  while(tcgetpgrp(STDIN_FILENO)!=(shell_pgid=getpgrp())) kill(-shell_pgid,SIGTTIN);
  signal(SIGINT,SIG_IGN); signal(SIGQUIT,SIG_IGN); signal(SIGTSTP,SIG_IGN); signal(SIGTTIN,SIG_IGN); signal(SIGTTOU,SIG_IGN); signal(SIGCHLD,SIG_DFL);
  shell_pgid=getpid(); if(setpgid(shell_pgid,shell_pgid)<0 && errno!=EACCES && errno!=EPERM) perror("setpgid");
  tcsetpgrp(STDIN_FILENO,shell_pgid); tcgetattr(STDIN_FILENO,&shell_tmodes);
}
/* Tokenizer supports single/double quotes, backslash escapes, and shell operators. */
static int tokenize(const char *s,char **t,int cap) {
  int n=0; size_t i=0;
  while(s[i]) {
    while(isspace((unsigned char)s[i])) i++;
    if(!s[i]) break;
    if(n>=cap-1) { fprintf(stderr,"mshell: too many tokens\n"); return -1; }
    if(s[i]=='2'&&s[i+1]=='>') { char b[4]="2>"; i+=2; if(s[i]=='>')b[2]=s[i++]; t[n++]=strdup(b); continue; }
    if(strchr("|<>&",s[i])) { char b[3]={s[i++],0,0}; if((b[0]=='>'&&s[i]=='>')||(b[0]=='&'&&s[i]=='&')||(b[0]=='<'&&s[i]=='<')) b[1]=s[i++]; t[n++]=strdup(b); continue; }
    char *b=calloc(strlen(s+i)+1,1); size_t k=0; char quote=0;
    while(s[i] && (quote || (!isspace((unsigned char)s[i])&&!strchr("|<>&",s[i])))) {
      char c=s[i++];
      if(!quote && (c=='\''||c=='"')) { quote=c; continue; }
      if(quote==c) { quote=0; continue; }
      if(c=='\\' && quote!='\'' && s[i]) c=s[i++];
      b[k++]=c;
    }
    if(quote) { fprintf(stderr,"mshell: unmatched quote\n"); free(b); return -1; }
    t[n++]=b;
  }
  t[n]=NULL; return n;
}
static void free_tokens(char **t,int n) { for(int i=0;i<n;i++) free(t[i]); }
static void save_input(const char *line,const char *home) {
  if(!*line||!home)return;
  char path[4096],stamp[64]; time_t now=time(NULL); struct tm tm_now;
  if(!localtime_r(&now,&tm_now))return;
  strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%S%z",&tm_now);
  if(snprintf(path,sizeof(path),"%s/.mshell_history",home)<(int)sizeof(path)) { FILE *f=fopen(path,"a"); if(f){fprintf(f,"%s\n",line);fclose(f);} }
  if(snprintf(path,sizeof(path),"%s/.mshell_trace.log",home)<(int)sizeof(path)) { FILE *f=fopen(path,"a"); if(f){fprintf(f,"%s\t%s\n",stamp,line);fclose(f);} }
}
static void expand_token(char *s) {
  char result[LINE_SIZE]; size_t w=0;
  for(size_t i=0;s[i]&&w<sizeof(result)-1;) {
    if(s[i]=='$') {
      i++; char name[256]; size_t k=0;
      if(s[i]=='?') { i++; snprintf(name,sizeof(name),"%d",last_status); const char *v=name; while(*v&&w<sizeof(result)-1)result[w++]=*v++; continue; }
      else if(s[i]=='{') { i++; while(s[i]&&s[i]!='}'&&k<sizeof(name)-1) name[k++]=s[i++]; if(s[i]=='}') i++; name[k]=0; }
      else { while((isalnum((unsigned char)s[i])||s[i]=='_')&&k<sizeof(name)-1) name[k++]=s[i++]; name[k]=0; }
      const char *v=k?getenv(name):"$"; if(v) while(*v&&w<sizeof(result)-1) result[w++]=*v++;
    } else result[w++]=s[i++];
  }
  result[w]=0; strcpy(s,result);
}
static int parse(char **t,int n,Command *cmd,int *count,int *bg) {
  memset(cmd,0,sizeof(Command)*MAX_CMDS); *count=1; *bg=0; int a=0;
  for(int i=0;i<n;i++) {
    char *x=t[i]; Command *c=&cmd[*count-1];
    if(!strcmp(x,"|")) { if(a==0||*count>=MAX_CMDS) return -1; a=0; (*count)++; }
    else if(!strcmp(x,"<")||!strcmp(x,">")||!strcmp(x,">>")||!strcmp(x,"2>")||!strcmp(x,"2>>")||!strcmp(x,"<<")) {
      if(i+1>=n) return -1; char **dst=!strcmp(x,"<")?&c->in:!strcmp(x,"<<")?&c->heredoc:!strcmp(x,"2>")?&c->err:!strcmp(x,"2>>")?&c->err_append:!strcmp(x,">")?&c->out:&c->append;
      if(*dst) return -1; *dst=t[++i];
    } else if(!strcmp(x,"&")) { if(i!=n-1) return -1; *bg=1; }
    else if(!strcmp(x,"&&")) return -1;
    else { if(a>=MAX_ARGS-1) return -1; c->argv[a++]=x; }
  }
  return a?0:-1;
}
static Job *job_for_child(pid_t pid) { for(size_t i=0;i<sizeof(children)/sizeof(children[0]);i++)if(children[i].pid==pid)return find_job_id(children[i].job_id);return NULL; }
static void forget_child(pid_t pid) { for(size_t i=0;i<sizeof(children)/sizeof(children[0]);i++)if(children[i].pid==pid){children[i]=(Child){0,0};return;} }
static void child_status(Job *j,pid_t pid,int st) { if(!j)return; if(WIFSTOPPED(st)){j->stopped++;if(j->stopped>=j->remaining)j->running=0;if(foreground_waiting&&pid==j->last_pid)last_status=128+WSTOPSIG(st);}else if(WIFCONTINUED(st)){if(j->stopped>0)j->stopped--;j->running=1;}else if(WIFEXITED(st)||WIFSIGNALED(st)){if(foreground_waiting&&pid==j->last_pid)last_status=WIFEXITED(st)?WEXITSTATUS(st):128+WTERMSIG(st);forget_child(pid);if(j->remaining>0)j->remaining--;if(j->stopped>j->remaining)j->stopped=j->remaining;} }
static void reap_jobs(void) {
  int st; pid_t p;
  while((p=waitpid(-1,&st,WNOHANG|WUNTRACED|WCONTINUED))>0) {
    Job *j=job_for_child(p);if(!j)continue;child_status(j,p,st);
    if(j->remaining==0){if(j->running)fprintf(stderr,"[%d] Done %s\n",j->id,j->line);trim_job(j);}
  }
}
static void list_jobs(void) { reap_jobs(); for(int i=0;i<MAX_JOBS;i++) if(jobs[i].id) printf("[%d] %s %s\n",jobs[i].id,jobs[i].running?"Running":"Stopped",jobs[i].line); }
static void wait_foreground(Job *j) {
  if(interactive) tcsetpgrp(STDIN_FILENO,j->pgid);
  int st; pid_t p; j->running=1;foreground_waiting=1;
  while(j->remaining>0&&j->stopped<j->remaining){p=waitpid(-j->pgid,&st,WUNTRACED|WCONTINUED);if(p<0){if(errno==EINTR)continue;break;}child_status(j,p,st);}
  foreground_waiting=0;
  if(interactive) { tcsetpgrp(STDIN_FILENO,shell_pgid); tcgetattr(STDIN_FILENO,&shell_tmodes); tcsetattr(STDIN_FILENO,TCSADRAIN,&shell_tmodes); }
  if(j->remaining>0){j->running=0;last_status=128+SIGTSTP;printf("[%d] Stopped %s\n",j->id,j->line);}else trim_job(j);
}
static void run_line(char *line);
static void run_script(FILE *f);
static int builtin(Command *c) {
  char **v=c->argv; if(!v[0]) return 1; last_status=0;
  if(strchr(v[0],'=') && !strchr(v[0],' ')) { char *eq=strchr(v[0],'='); *eq=0; if(setenv(v[0],eq+1,1)<0) {perror("setenv");last_status=1;} *eq='='; return 1; }
  if(!strcmp(v[0],"export")) { if(!v[1]) { extern char **environ; for(char **e=environ;*e;e++) puts(*e); } else for(int i=1;v[i];i++) { char *eq=strchr(v[i],'='); if(eq){*eq=0;setenv(v[i],eq+1,1);*eq='=';} else if(!getenv(v[i])) setenv(v[i],"",1); } return 1; }
  if(!strcmp(v[0],"history")) { for(size_t i=0;i<history_count;i++) printf("%4zu  %s\n",i+1,history[i]); return 1; }
  if(!strcmp(v[0],"alias")) { if(!v[1]){for(int i=0;i<alias_count;i++)printf("alias %s='%s'\n",alias_names[i],alias_values[i]);return 1;} char *eq=strchr(v[1],'=');if(!eq||eq==v[1]){fprintf(stderr,"alias: use alias name=value\n");last_status=2;return 1;}*eq=0;int i;for(i=0;i<alias_count;i++)if(!strcmp(alias_names[i],v[1]))break;if(i==alias_count&&alias_count<64){alias_names[i]=strdup(v[1]);alias_count++;}if(i<64){free(alias_values[i]);alias_values[i]=strdup(eq+1);}*eq='=';return 1; }
  if(!strcmp(v[0],"unalias")){if(v[1])for(int i=0;i<alias_count;i++)if(!strcmp(alias_names[i],v[1])){free(alias_names[i]);free(alias_values[i]);alias_names[i]=alias_names[alias_count-1];alias_values[i]=alias_values[alias_count-1];alias_count--;break;}return 1;}
  if(!strcmp(v[0],"source")||!strcmp(v[0],".")){if(!v[1]){fprintf(stderr,"source: filename required\n");last_status=2;return 1;}FILE *f=fopen(v[1],"r");if(!f){perror(v[1]);last_status=1;return 1;}run_script(f);fclose(f);return 1;}
  if(!strcmp(v[0],"pwd")) { char cwd[4096]; if(getcwd(cwd,sizeof(cwd))) puts(cwd); else perror("pwd"); return 1; }
  if(!strcmp(v[0],"unset")) { for(int i=1;v[i];i++) unsetenv(v[i]); return 1; }
  if(!strcmp(v[0],"cd")) { char old[4096],cwd[4096]; const char *d=v[1]?v[1]:getenv("HOME"); if(!strcmp(d,"-")){d=getenv("OLDPWD");if(!d){fprintf(stderr,"cd: OLDPWD not set\n");last_status=1;return 1;}} if(getcwd(old,sizeof(old))&&chdir(d)==0){setenv("OLDPWD",old,1);if(getcwd(cwd,sizeof(cwd))){setenv("PWD",cwd,1);if(v[1]&&!strcmp(v[1],"-"))puts(cwd);}}else {perror("cd");last_status=1;} return 1; }
  if(!strcmp(v[0],"jobs")) { list_jobs(); return 1; }
  if(!strcmp(v[0],"help")) { puts("Builtins: cd [dir|-], pwd, export, unset, jobs, fg [%job], bg [%job], history, help, exit. Supports variables, pipelines, redirection, background jobs, scripts, quotes, Ctrl-C and Ctrl-Z. History persists in ~/.mshell_history."); return 1; }
  if(!strcmp(v[0],"exit")) exit(0);
  if(!strcmp(v[0],"fg")||!strcmp(v[0],"bg")) {
    int id=0; if(v[1]) id=atoi(v[1][0]=='%'?v[1]+1:v[1]); else { for(int i=0;i<MAX_JOBS;i++) if(jobs[i].id) id=jobs[i].id; }
    Job *j=find_job_id(id); if(!j) { fprintf(stderr,"%s: no such job\n",v[0]); return 1; }
    if(kill(-j->pgid,SIGCONT)<0) perror(v[0]); else j->running=1;
    if(!strcmp(v[0],"fg")) wait_foreground(j); else printf("[%d] %s &\n",j->id,j->line); return 1;
  }
  return 0;
}
static void run_command(char *line);
static void run_line(char *line) {
  char *part=line; char quote=0; int escaped=0, execute_segment=1;
  for(char *p=line;;p++) { char ch=*p; if(escaped){escaped=0;continue;} if(ch=='\\'&&quote!='\''){escaped=1;continue;} if((ch=='\''||ch=='"')&&(!quote||quote==ch)){quote=quote?0:ch;continue;}
    int sep=(!quote&&(ch==';'||ch=='\n'||(ch=='&'&&p[1]=='&')||(ch=='|'&&p[1]=='|'))); if(!ch||sep){ char op=sep?ch:0; *p=0;
      char *s=part; while(isspace((unsigned char)*s))s++; if(*s&&execute_segment){ char *tok[MAX_ARGS*2]; int nt=tokenize(s,tok,MAX_ARGS*2); if(nt>0){ for(int z=0;z<nt;z++) expand_token(tok[z]); Command cs[MAX_CMDS]; int cn,bg;
        if(parse(tok,nt,cs,&cn,&bg)<0) { fprintf(stderr,"mshell: invalid command syntax\n"); last_status=2; } else run_command(s);
        free_tokens(tok,nt); }
      } if(!ch) break; if((op=='&'||op=='|')&&p[1]==op){p++;execute_segment=(op=='&')?(last_status==0):(last_status!=0);}else execute_segment=1; part=p+1;
    }
  }
  return;
}
static int line_is(const char *s,const char *word){while(isspace((unsigned char)*s))s++;size_t n=strlen(word);return !strncmp(s,word,n)&&(!s[n]||isspace((unsigned char)s[n])||s[n]==';');}
static void execute_script_lines(char **lines,int first,int last){
  for(int i=first;i<last;i++){
    char *s=lines[i];while(isspace((unsigned char)*s))s++;if(!*s||*s=='#')continue;
    char *hd=strstr(s,"<<");if(hd){char *d=hd+2;while(isspace((unsigned char)*d))d++;char delim[128];size_t dl=0;while(d[dl]&&!isspace((unsigned char)d[dl])&&dl<sizeof(delim)-1){delim[dl]=d[dl];dl++;}delim[dl]=0;int end=i+1;while(end<last){char *candidate=lines[end];while(isspace((unsigned char)*candidate))candidate++;candidate[strcspn(candidate,"\r\n")]=0;if(!strcmp(candidate,delim))break;end++;}if(end>=last){fprintf(stderr,"mshell: missing heredoc delimiter %s\n",delim);last_status=2;return;}char tmp[]="/tmp/mshell-script-heredoc-XXXXXX";int fd=mkstemp(tmp);if(fd<0){perror("heredoc");last_status=1;return;}for(int k=i+1;k<end;k++)dprintf(fd,"%s",lines[k]);close(fd);size_t prefix=(size_t)(hd-s);char *cmd=calloc(strlen(s)+strlen(tmp)+8,1);memcpy(cmd,s,prefix);sprintf(cmd+prefix,"< %s %s",tmp,d+dl);run_line(cmd);free(cmd);unlink(tmp);i=end;continue;}
    if(!strncmp(s,"if ",3)){
      char *cond=strdup(s+3),*then=strstr(cond,"; then");if(then)*then=0;run_line(cond);free(cond);int split=-1,end=-1,depth=0;
      for(int j=i+1;j<last;j++){if(line_is(lines[j],"if"))depth++;else if(line_is(lines[j],"fi")){if(!depth){end=j;break;}depth--;}else if(!depth&&line_is(lines[j],"else"))split=j;}
      if(end<0){fprintf(stderr,"mshell: missing fi\n");last_status=2;return;}int from=(last_status==0)?i+1:(split>=0?split+1:end);int to=(last_status==0&&split>=0)?split:end;for(int j=from;j<to;j++){char *b=strdup(lines[j]);run_line(b);free(b);}i=end;continue;
    }
    if(!strncmp(s,"for ",4)){
      char var[128]={0};char *in=strstr(s+4," in ");char *do_mark=strstr(s,"; do");if(!in||!do_mark){fprintf(stderr,"mshell: expected 'for NAME in values; do'\n");last_status=2;continue;}size_t vn=(size_t)(in-(s+4));if(vn>=sizeof(var)){last_status=2;continue;}memcpy(var,s+4,vn);*do_mark=0;char *vals=strdup(in+4);int end=i+1,depth=0;for(;end<last;end++){if(line_is(lines[end],"for"))depth++;else if(line_is(lines[end],"done")){if(!depth)break;depth--;}}if(end>=last){fprintf(stderr,"mshell: missing done\n");free(vals);last_status=2;return;}char *words[MAX_ARGS];int nw=tokenize(vals,words,MAX_ARGS);for(int w=0;w<nw;w++)expand_token(words[w]);for(int w=0;w<nw;w++){setenv(var,words[w],1);for(int j=i+1;j<end;j++){char *b=strdup(lines[j]);run_line(b);free(b);}}free_tokens(words,nw);free(vals);i=end;continue;
    }
    char *open=strstr(s,"() {");if(open){*open=0;int end=i+1;while(end<last&&!line_is(lines[end],"}"))end++;if(end>=last){fprintf(stderr,"mshell: missing } in function\n");last_status=2;return;}if(function_count<32){Function *fn=&functions[function_count++];fn->name=strdup(s);fn->lines=end-i-1;fn->body=calloc((size_t)fn->lines,sizeof(char*));for(int k=0;k<fn->lines;k++)fn->body[k]=strdup(lines[i+1+k]);}i=end;continue;}
    char *b=strdup(s);run_line(b);free(b);
  }
}
static void run_script(FILE *f){char **lines=NULL;size_t count=0,cap=0;char *line=NULL;size_t linecap=0;while(getline(&line,&linecap,f)>=0){if(count==cap){cap=cap?cap*2:32;lines=realloc(lines,cap*sizeof(*lines));}lines[count++]=strdup(line);}free(line);execute_script_lines(lines,0,(int)count);for(size_t i=0;i<count;i++)free(lines[i]);free(lines);}
static void run_command(char *line) {
  char *tok[MAX_ARGS*2]; int n=tokenize(line,tok,MAX_ARGS*2); if(n<=0) { if(n==0) return; return; }
  for(int i=0;i<n;i++) expand_token(tok[i]);
  for(int f=0;f<function_count;f++)if(!strcmp(tok[0],functions[f].name)){for(int k=0;k<functions[f].lines;k++){char *body=strdup(functions[f].body[k]);run_line(body);free(body);}free_tokens(tok,n);return;}
  for(int a=0;a<alias_count;a++)if(!strcmp(tok[0],alias_names[a])){free(tok[0]);tok[0]=strdup(alias_values[a]);break;}
  Command cmds[MAX_CMDS]; int nc,bg;
  if(parse(tok,n,cmds,&nc,&bg)<0) { fprintf(stderr,"mshell: invalid command syntax\n"); free_tokens(tok,n); return; }
  char *heredoc_paths[MAX_CMDS]={0};
  for(int c=0;c<nc;c++) if(cmds[c].heredoc) {
    char tmpl[]="/tmp/mshell-heredoc-XXXXXX"; int fd=mkstemp(tmpl); if(fd<0){perror("heredoc");last_status=1;free_tokens(tok,n);return;}
    heredoc_paths[c]=strdup(tmpl); char *body=NULL;size_t bodycap=0;ssize_t len;
    while((len=getline(&body,&bodycap,stdin))>=0){if(len&&body[len-1]=='\n')body[len-1]=0;if(!strcmp(body,cmds[c].heredoc))break;dprintf(fd,"%s\n",body);}
    free(body);close(fd);if(len<0)fprintf(stderr,"mshell: warning: end of input before heredoc delimiter\n");cmds[c].in=heredoc_paths[c];
  }
  if(nc==1 && !bg && builtin(&cmds[0])) { for(int c=0;c<nc;c++)if(heredoc_paths[c]){unlink(heredoc_paths[c]);free(heredoc_paths[c]);} free_tokens(tok,n); return; }
  char *expanded[MAX_CMDS][MAX_ARGS]; int expanded_n[MAX_CMDS]={0};
  for(int c=0;c<nc;c++) for(int a=0;cmds[c].argv[a];a++) {
    glob_t matches={0}; int gr=glob(cmds[c].argv[a],0,NULL,&matches);
    if(gr==0) { for(size_t m=0;m<matches.gl_pathc&&expanded_n[c]<MAX_ARGS-1;m++)expanded[c][expanded_n[c]++]=strdup(matches.gl_pathv[m]); globfree(&matches); }
    else expanded[c][expanded_n[c]++]=strdup(cmds[c].argv[a]);
  }
  for(int c=0;c<nc;c++){for(int a=0;a<expanded_n[c];a++)cmds[c].argv[a]=expanded[c][a];cmds[c].argv[expanded_n[c]]=NULL;}
  int prev=-1; pid_t pgid=0; pid_t last=0, launched[MAX_CMDS];int launched_n=0;
  for(int i=0;i<nc;i++) {
    int fd[2]={-1,-1}; if(i<nc-1 && pipe(fd)<0) { perror("pipe"); break; }
    if(trace_mode)fprintf(stderr,"[trace] fork stage=%d/%d command=%s\n",i+1,nc,cmds[i].argv[0]);
    pid_t p=fork(); if(p<0) { perror("fork"); break; }
    if(p==0) {
      child_signal_defaults(); pid_t group=pgid?pgid:getpid(); setpgid(0,group);
      if(prev>=0) dup2(prev,STDIN_FILENO); if(fd[1]>=0) dup2(fd[1],STDOUT_FILENO);
      if(prev>=0) close(prev); if(fd[0]>=0) close(fd[0]); if(fd[1]>=0) close(fd[1]);
      if(cmds[i].in) { int f=open(cmds[i].in,O_RDONLY); if(f<0){perror(cmds[i].in);_exit(126);} dup2(f,0);close(f); }
      char *out=cmds[i].out?cmds[i].out:cmds[i].append; if(out) { int flags=O_WRONLY|O_CREAT|(cmds[i].append?O_APPEND:O_TRUNC); int f=open(out,flags,0666); if(f<0){perror(out);_exit(126);}dup2(f,1);close(f); }
      char *err=cmds[i].err?cmds[i].err:cmds[i].err_append; if(err){int f=open(err,O_WRONLY|O_CREAT|(cmds[i].err_append?O_APPEND:O_TRUNC),0666);if(f<0){perror(err);_exit(126);}dup2(f,2);close(f);}
      if(builtin(&cmds[i])) _exit(0); execvp(cmds[i].argv[0],cmds[i].argv); perror(cmds[i].argv[0]); _exit(errno==ENOENT?127:126);
    }
    if(!pgid) pgid=p; setpgid(p,pgid); if(launched_n<MAX_CMDS)launched[launched_n++]=p; if(trace_mode)fprintf(stderr,"[trace] pid=%ld pgid=%ld\n",(long)p,(long)pgid); last=p; if(prev>=0)close(prev); if(fd[1]>=0)close(fd[1]); prev=fd[0];
  }
  if(prev>=0)close(prev);
  if(pgid) { Job *j=add_job(pgid,line,1,launched_n,launched); if(j) { if(bg) { printf("[%d] %d\n",j->id,(int)pgid); last_status=0; } else wait_foreground(j); } else while(waitpid(-pgid,NULL,0)>0){} }
  (void)last; for(int c=0;c<nc;c++){for(int a=0;a<expanded_n[c];a++)free(expanded[c][a]);if(heredoc_paths[c]){unlink(heredoc_paths[c]);free(heredoc_paths[c]);}} free_tokens(tok,n);
}
int main(int argc,char **argv) {
  const char *script=NULL;for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--trace"))trace_mode=1;else if(!script)script=argv[i];else{fprintf(stderr,"usage: %s [--trace] [script]\n",argv[0]);return 2;}}
  if(script){FILE *f=fopen(script,"r");if(!f){perror(script);return 1;} if(dup2(fileno(f),STDIN_FILENO)<0){perror("dup2");return 1;} fclose(f);}
  const char *home=getenv("HOME"); char histpath[4096]; if(home&&snprintf(histpath,sizeof(histpath),"%s/.mshell_history",home)<(int)sizeof(histpath)){FILE *hf=fopen(histpath,"r");if(hf){char *hline=NULL;size_t hcap=0;while(history_count<1000&&getline(&hline,&hcap,hf)>=0){hline[strcspn(hline,"\r\n")]=0;if(*hline)history[history_count++]=strdup(hline);}free(hline);fclose(hf);}}
  init_shell(); char *line=NULL; size_t cap=0;
  if(interactive) {
    printf("\033]0;mshell // shell lab\007\033[2J\033[H");
    printf("\033[38;2;93;230;190m  M S H E L L  \n  UNIX SYSTEMS LAB\n  =================\033[0m\n");
    printf("\033[38;2;139;155;163m  UNIX SYSTEMS LAB  /  C PROCESS SHELL\n  Type help to explore. Ctrl-D exits.\033[0m\n\n");
  }
  if(script){run_script(stdin);free(line);return last_status;}
  for(;;) { reap_jobs(); if(interactive) { printf("\033[38;2;93;230;190mmshell\033[38;2;119;136;145m:\033[38;2;237;242;239m%s\033[38;2;93;230;190m$ \033[0m",getenv("PWD")?getenv("PWD"):"~"); fflush(stdout); }
    ssize_t len=getline(&line,&cap,stdin); if(len<0) { if(interactive) putchar('\n'); break; }
    if(len&&line[len-1]=='\n') line[len-1]=0;
    if(*line&&history_count<1000) history[history_count++]=strdup(line);
    save_input(line,home);
    run_line(line);
  }
  free(line); for(size_t i=0;i<history_count;i++)free(history[i]); return 0;
}
