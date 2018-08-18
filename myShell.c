/*修海博3160105286*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <pwd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>

#define MAX_PATH_LEN 256	//最大路径长度
#define MAX_LINE 80			//最大命令长度
#define MAX_OPER 64 		//最大操作
#define MAX_NAME 256		//最大名称字符串的长度

extern char** environ;
char* cmdArray[MAX_LINE];
int cmdStringNum;
int is_pipe = 0;			//标记"|"符号后面的位置
int is_bg = 0;				//标记是否需要 后台执行
int is_internal = -1;		//标记是否是内部语句
int ever; 					//用于存放umask是否被修改了
mode_t newMask;				//用于存放全局的umask
int pipe_fd[2];
int newnum;					//用于记录myshell文件中新的指令长度
int values1;
int values2;

//建立月份的数组
char* monthArray[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", 
					"Aug", "Sep", "Oct", "Nov", "Dec"};
//建立一周天数的数组
char* weekArray[7] = {"Mon", "Tues", "Wed", "Thur", "Fri", "Sat", "Sun"};
//建立操作名称数组，根据用户输入进行匹配
char* commandNameArray[23] = { "quit", "exit", "clear", "clr", "pwd", 
	"echo", "time", "environ", "cd", "help", "exec", "test", "umask", "jobs",
	"fg", "bg", "myshell", "dir", "set", "unset", "shift"};


void is_internal_cmd();
void set();
void unset();
void initdate()
{
	memset(cmdArray, 0, MAX_LINE);
	cmdStringNum = 0;
	return;
}


void init()
{
	is_pipe = 0;
	is_bg = 0;
	char hostname[MAX_NAME];
	char pathname[MAX_PATH_LEN];
	struct passwd *pwd;
	pwd = getpwuid(getuid());
	//通过库函数获取hostname和pathname
	gethostname(hostname, MAX_NAME);
	getcwd(pathname, MAX_PATH_LEN);
	//打印用户名和hostname，参数为绿色高亮
	printf("\033[1;32m%s@%s:\033[0m", pwd->pw_name, hostname);
	//记录这个时候~代表的长度，用于下面的比较
	int m = strlen(pwd->pw_dir);
	//如果前m位相同只需要打印~
	if (!strncmp(pathname,pwd->pw_dir,m))
	{
		//打印路径，参数为紫色高亮
		printf("\033[1;35m~%s\033[0m",pathname+strlen(pwd->pw_dir));
	}
	else 
	{
		//打印路径，参数为紫色高亮
		printf("\033[1;35m%s\033[0m", pathname);
	}
	if (geteuid() == 0)
	{
		//如果是root用户打印#
		printf("# ");
	}
	else
	{
		//如果是普通用户打印$
		printf("$ ");
	}

}
int readCommand()
{
	int count = 0;
	char str[MAX_LINE];
	char* helper;
	initdate();
	fgets(str, MAX_LINE, stdin);
	if (str[strlen(str) - 1] == '\n')
	{
		str[strlen(str) - 1] = '\0';
	}
	helper = strtok(str, " ");
	while (helper != NULL)
	{
		cmdArray[count] = (char*)malloc(sizeof(*helper));
		strcpy(cmdArray[count++], helper);
		helper = strtok(NULL, " ");
	}
	cmdStringNum = count;

}

/*判断语句是否为管道*/
void is_pipe_cmd()
{
	//将is_pipe的值初始化为0
	is_pipe = 0;
	char oper[MAX_OPER];
	if (cmdStringNum >= 1)
	{
		for (int i = 1; i < cmdStringNum; i++)
		{
			strcpy(oper, cmdArray[i]);//通过oper符号确定是否需要管道操作
			if (oper != NULL)
			{
				if (!strcmp(oper, "|"))
				{
					//如果检测到|则将is_pipe的值标记为|后面的第一个
					cmdArray[i] = NULL;
					is_pipe = i + 1;
					break;
				}
			}
		}
		return;
	}
}
/*判断语句是否需要后台进行*/
void is_bg_cmd()
{
	//将is_bg的值初始化为0
	is_bg = 0;
	char oper[MAX_OPER];
	if (cmdStringNum >= 1)
	{
		for (int i = 0; i < cmdStringNum; i++)
		{
			strcpy(oper, cmdArray[i]);//通过oper符号确定是否需要管道操作
			if (!strcmp(oper, "&"))
			{
				//如果检测到&将其替换
				cmdArray[i] = NULL;
				cmdStringNum--;
				is_bg = 1;//将is_bg的值改变为1，说明需要后台进行
			}
		}
	}
}
/*判断语句是否是重定向*/
void is_io_redirection()
{
	char oper[MAX_OPER];//存放操作符号
	char file[MAX_NAME];//存放文件名字
	//实现外部命令的重定向
	for (int i = 1; i < cmdStringNum; i++)
	{
		if (cmdArray[i] != NULL)
		{
			strcpy(oper, cmdArray[i]);
			if (!strcmp(oper, ">"))
			{
				int m = i + 1;
				//将操作符号的下一个字符串保存为文件
				if (cmdArray[m] != NULL)
				{
					strcpy(file, cmdArray[m]);
				}
				//使用open()打开文件
				//O_CREAT：如果打开文件不存在则创建
				//O_TRUNC：如果文件存在以只读或只写来打开
				//O_WRONLY：用只写方式打开文件
				int output=open(cmdArray[i+1],O_WRONLY|O_APPEND|O_CREAT,0666);
				dup2(output, 1);
				close(output);
				cmdArray[i] = NULL;
				i++;
				continue;
			}
			else if (!strcmp(oper, ">>"))
			{
				//使用open()打开文件
				//O_CREAT：如果打开文件不存在则创建
				//O_TRUNC：如果文件存在以只读或只写来打开
				//O_WRONLY：用只写方式打开文件
				int output = open(file,O_CREAT|O_TRUNC|O_WRONLY,0600);
				dup2(output, 1);
				close(output);
				cmdArray[i] = NULL;
				i++;
				continue;
			}
			else if (!strcmp(oper, "<"))
			{
				int input=open(cmdArray[i+1],O_CREAT|O_RDONLY,0666);
				dup2(input, 0);
				close(input);
				cmdArray[i] = NULL;
				i++;
			}
		}
	}

}
/*判断语句能否在外部语句中执行*/
void is_external_cmd(int m)
{
	int a;
	int res = execvp(cmdArray[m], cmdArray + m);
	if (res < 0)
	{
		printf("myshell: command not found\n");
	}
}
void do_pipe(int pos)
{
	pid_t pid;
	//子进程
	if ((pid = fork()) == 0)
	{
		close(pipe_fd[1]);
		dup2(pipe_fd[0], 0);
		is_external_cmd(pos);
	}
	else
	{
		//父进程，关闭写通道
		close(pipe_fd[1]);
		waitpid(pid, NULL, 0);
	}
}

void mypwd()
{
	char path[MAX_PATH_LEN];
	getcwd(path,sizeof(path));
	if (path != NULL)
	{
		printf("%s\n",path);
	}
	else
	{
		perror("Error in myShell: getcwd");//报错
		exit(1);
	}
}
void myecho()
{
	int i;
	int next;
	for (i = 1; i < cmdStringNum; i++)
	{
		printf("%s", *(cmdArray + i));
		next = i + 1;
		if (next == cmdStringNum)
		{
			break;
		}
		else
		{
			printf(" ");
		}
	}
	if (i <= cmdStringNum -1)
		printf("\n");
}

void myecho_redirect()
{
	int f;
	pid_t pid;
	char file[MAX_NAME];//存放文件名字
	char oper[MAX_OPER];//存放操作符号
	int num = cmdStringNum;
	
	//操作符号后面的file要记录
	for (int i = 1; i < cmdStringNum; i++)
	{
		//将文件名存放在file,操作方式保存在oper
		strcpy(oper, cmdArray[i]);
		if (strcmp(oper, ">") == 0 || strcmp(oper, ">>") == 0)
		{
			int m = i + 1;
			//将操作符号的下一个字符串保存为文件
			if (cmdArray[m] != NULL)
			{
				strcpy(file, cmdArray[m]);
			}
			else
			{
				printf("bash: syntax error near unexpected token `newline'\n");
				return;
			}
		}
		//根据用户输入的操作符号进行操作
		if (strcmp(oper, ">") == 0)
		{
			//使用open()打开文件
			//O_CREAT：如果打开文件不存在则创建
			//O_TRUNC：如果文件存在以只读或只写来打开
			//O_WRONLY：用只写方式打开文件
			f = open(file,O_CREAT|O_TRUNC|O_WRONLY,0600);
			//如果文件打开失败打印错误信息
			if (f < 0)
			{
				perror("Error in open file");
				return;
			}
			if ((pid = fork()) == 0)
			{
				//将stdout重新定向到文件
				dup2(f, 1);
				for (int j = 1; j < i; j++)
				{
					printf("%s ", cmdArray[j]);
				}
				exit(0);
			}
			else if (pid > 0)
			{
				//等待子进程结束
				waitpid(pid, NULL, 0);
			}
			close(f);
		}
		if (strcmp(oper, ">>") == 0)
		{
			f = open(file,O_CREAT|O_APPEND|O_WRONLY,0600);
			//如果文件打开失败打印错误信息
			if (f < 0)
			{
				perror("Error in open file");
				return;
			}
			pid = fork();
			if (pid == 0)
			{
				//将stdout重新定向到文件
				dup2(f, 1);
				for (int j = 1; j < i; j++)
				{
					printf("%s ", cmdArray[j]);
				}
				exit(0);
			}
			else if (pid > 0)
			{
				//等待子进程结束
				waitpid(pid, NULL, 0);
			}
			close(f);
		}
	}
}


void mycd()
{
	int res;
	char path[MAX_PATH_LEN];
	//通过getuid()获得用户的id，通过getpwuid获得用户信息
	struct passwd* user = getpwuid(getuid());
	//通过字符串数量判断指令长度
	if (cmdStringNum == 2 && strcmp(cmdArray[1], "~") == 0)
	{
		//通过chdir改变当前的路径,res检测是否执行成功
		//如果返回值不是0则需要报错，打印错误信息
		res = chdir(user->pw_dir);
		//上面调用了passwd结构中的pw_dir代表用户的根目录
		if (res != 0)
		{
			printf("%s :No such file or directory\n", cmdArray[1]);
			printf("hhhh\n");
		}
		return;
	} 
	//检测cd命令后面的路径参数
	if (cmdStringNum > 1) 
	{
		//通过chdir改变当前的路径,res检测是否执行成功
		//如果返回值不是0则需要报错，打印错误信息
		res = chdir(cmdArray[1]);
		if (res != 0)
		{
			printf("%s :No such file or directory\n", cmdArray[1]);
		}
	}
	//如果用户的输入只是cd
	else
	{
		strcpy(path, user->pw_dir);
		res = chdir(path);
		if (res != 0)
		{
			perror("Error in myShell: chdir");
		}
	}
}

void mytime()
{
	int weekday;
	int month;
	time_t tvar;
	struct tm *tp;
	time(&tvar);
	tp = localtime(&tvar);
	//测试的时候发现需要-1
	weekday = tp->tm_wday - 1;
	month = tp->tm_mon;
	//目标时间：Tue Aug 14 06:44:06 PDT 2018
	//打印星期月份和几号
	printf("%s ", weekArray[weekday]);	
	printf("%s ", monthArray[month]);
	printf("%d ",tp->tm_mday);
	//打印小时分钟和秒
	printf("%02d:",tp->tm_hour);
	printf("%02d:",tp->tm_min);
	printf("%02d ",tp->tm_sec);
	printf("PDT ");
	//打印年份，需要+1900
	printf("%d\n",1900 + tp->tm_year);
}



void myhelp()
{
	if (cmdStringNum == 1)
	{
		printf("GNU bash, version 4.4.12(1)-release (x86_64-pc-linux-gnu)\n");
		printf("These shell commands are defined internally.  Type `help' to see this list.\n");
		printf("Type `help name' to find out more about the function `name'.\n");
		printf("Use `info bash' to find out more about the shell in general.\n");
		printf("Use `man -k' or `info' to find out more about commands not in this list.\n");
		printf("\n");
		printf("A star (*) next to a name means that the command is disabled.\n");
		 printf("\n");
		 printf("job_spec [&]                            history [-c] [-d offset] [n] or hist>\n");
		 printf("(( expression ))                        if COMMANDS; then COMMANDS; [ elif C>\n");
		 printf(". filename [arguments]                  jobs [-lnprs] [jobspec ...] or jobs >\n");
		 printf(":                                       kill [-s sigspec | -n signum | -sigs>\n");
		 printf("[ arg... ]                              let arg [arg ...]\n");
		 printf("[[ expression ]]                        local [option] name[=value] ...\n");
		 printf("alias [-p] [name[=value] ... ]          logout [n]\n");
		 printf("bg [job_spec ...]                       mapfile [-d delim] [-n count] [-O or>\n");
		 printf("bind [-lpsvPSVX] [-m keymap] [-f file>  popd [-n] [+N | -N]\n");
		 printf("break [n]                               printf [-v var] format [arguments]\n");
		 printf("builtin [shell-builtin [arg ...]]       pushd [-n] [+N | -N | dir]\n");
		 printf("caller [expr]                           pwd [-LP]\n");
		 printf("case WORD in [PATTERN [| PATTERN]...)>  read [-ers] [-a array] [-d delim] [->\n");
		 printf("cd [-L|[-P [-e]] [-@]] [dir]            readarray [-n count] [-O origin] [-s>\n");
		 printf("command [-pVv] command [arg ...]        readonly [-aAf] [name[=value] ...] o>\n");
		 printf("compgen [-abcdefgjksuv] [-o option] [>  return [n]\n");
		 printf("complete [-abcdefgjksuv] [-pr] [-DE] >  select NAME [in WORDS ... ;] do COMM>\n");
		 printf("compopt [-o|+o option] [-DE] [name ..>  set [-abefhkmnptuvxBCHP] [-o option->\n");
		 printf("continue [n]                            shift [n]\n");
		 printf("coproc [NAME] command [redirections]    shopt [-pqsu] [-o] [optname ...]\n");
		 printf("declare [-aAfFgilnrtux] [-p] [name[=v>  source filename [arguments]\n");
		 printf("dirs [-clpv] [+N] [-N]                  suspend [-f]\n");
		 printf("disown [-h] [-ar] [jobspec ... | pid >  test [expr]\n");
		 printf("echo [-neE] [arg ...]                   time [-p] pipeline\n");
		 printf("enable [-a] [-dnps] [-f filename] [na>  times\n");
		 printf("eval [arg ...]                          trap [-lp] [[arg] signal_spec ...]\n");
		 printf("exec [-cl] [-a name] [command [argume>  true\n");
		 printf("exit [n]                                type [-afptP] name [name ...]\n");
		 printf("export [-fn] [name[=value] ...] or ex>  typeset [-aAfFgilnrtux] [-p] name[=v>\n");
		 printf("false                                   ulimit [-SHabcdefiklmnpqrstuvxPT] [l>\n");
		 printf("fc [-e ename] [-lnr] [first] [last] o>  umask [-p] [-S] [mode]\n");
		 printf("fg [job_spec]                           unalias [-a] name [name ...]\n");
		 printf("for NAME [in WORDS ... ] ; do COMMAND>  unset [-f] [-v] [-n] [name ...]\n");
		 printf(" for (( exp1; exp2; exp3 )); do COMMAN>  until COMMANDS; do COMMANDS; done\n");
		 printf("function name { COMMANDS ; } or name >  variables - Names and meanings of so>\n");
		 printf("getopts optstring name [arg]            wait [-n] [id ...]\n");
		 printf("hash [-lr] [-p pathname] [-dt] [name >  while COMMANDS; do COMMANDS; done\n");
		 printf("help [-dms] [pattern ...]               { COMMANDS ; }\n");

	}
	else if (cmdStringNum == 2)
	{
		if(strcmp(cmdArray[1],"bg")==0){
			printf("bg: bg [job_spec ...]\n");
    		printf("Move jobs to the background.\n");
			printf("\n");    
    		printf("Place the jobs identified by each JOB_SPEC in the background, as if they\n");
    		printf("had been started with `&'.  If JOB_SPEC is not present, the shell's notion\n");
    		printf("of the current job is used.\n");
    		printf("\n");
		    printf("Exit Status:\n");
    		printf("Returns success unless job control is not enabled or an error occurs.\n");
		}
		else if(strcmp(cmdArray[1],"cd")==0){
			printf("cd: cd [-L|[-P [-e]] [-@]] [dir]\n");
			printf("    Change the shell working directory.\n");
			printf("    \n");
			printf("    Change the current directory to DIR.  The default DIR is the value of the\n");
			printf("    HOME shell variable.\n");
			printf("    \n");
			printf("    The variable CDPATH defines the search path for the directory containing\n");
			printf("    DIR.  Alternative directory names in CDPATH are separated by a colon (:).\n");
			printf("    A null directory name is the same as the current directory.  If DIR begins\n");
			printf("    with a slash (/), then CDPATH is not used.\n");
			printf("    \n");
			printf("    If the directory is not found, and the shell option `cdable_vars' is set,\n");
			printf("    the word is assumed to be  a variable name.  If that variable has a value,\n");
			printf("    its value is used for DIR.\n");
			printf("    \n");
			printf("    Options:\n");
			printf("      -L	force symbolic links to be followed: resolve symbolic\n");
			printf("    		links in DIR after processing instances of `..'\n");
			printf("      -P	use the physical directory structure without following\n");
			printf("    		symbolic links: resolve symbolic links in DIR before\n");
			printf("    		processing instances of `..'\n");
			printf("      -e	if the -P option is supplied, and the current working\n");
			printf("    		directory cannot be determined successfully, exit with\n");
			printf("   		a non-zero status\n");
			printf("      -@	on systems that support it, present a file with extended\n");
			printf("    		attributes as a directory containing the file attributes\n");
			printf("    \n");
			printf("    The default is to follow symbolic links, as if `-L' were specified.\n");
			printf("    `..' is processed by removing the immediately previous pathname component\n");
			printf("    back to a slash or the beginning of DIR.\n");
			printf("    \n");
			printf("    Exit Status:\n");
			printf("    Returns 0 if the directory is changed, and if $PWD is set successfully when\n");
			printf("    -P is used; non-zero otherwise.\n");

		}
		else if(strcmp(cmdArray[1],"continue")==0){
			printf("usage:valid only in for, while, or until loop\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"echo")==0){
			printf("usage:print strings after echo,redirection is supported\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"exec")==0){
			printf("usage:execute a command and replace the current process\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"exit")==0){
			printf("usage:quit the shell directly\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"fg")==0){
			printf("fg: fg [job_spec]\n");
			printf("    Move job to the foreground.\n");
			printf("    Place the job identified by JOB_SPEC in the foreground, making it the\n");
    		printf("    current job.  If JOB_SPEC is not present, the shell's notion of the\n");
    		printf("	current job is used.\n");
    		printf("\n");
    		printf("Exit Status:\n");
    		printf("Status of command placed in foreground, or failure if an error occurs.");
		}
		else if(strcmp(cmdArray[1],"jobs")==0){
			printf("usage:check the processes running in the system\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"pwd")==0){
			printf("usage:print the current working directory\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"set")==0){
			printf("usage:set shell variables\n");
			printf("options            descriptions\n");
			printf("/				   not supported currently\n");
		}
		else if(strcmp(cmdArray[1],"shift")==0){
			printf("usage:shift user's inputs\n");
			printf("options            descriptions\n");
			printf("/               not supported currently\n");
		}
		else if(strcmp(cmdArray[1],"test")==0){
			printf("usage:check file attributes, 4 options are supported so far\n");
			printf("options            descriptions\n");
			printf("[-l]               test if the file is a symbolic link\n");
			printf("[-b]               test if the file is a block device\n");
			printf("[-c]               test if the file is a character device\n");
			printf("[-d]               test if the file is a directory\n");
		}
		else if(strcmp(cmdArray[1],"time")==0){
			printf("usage:show the current time in an elegant format\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"umask")==0){
			printf("usage:change the value of umask\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"unset")==0){
			printf("usage:unset shell variables\n");
			printf("options            descriptions\n");
			printf("/				   not supported currently\n");
		}
		else if(strcmp(cmdArray[1],"clr")==0){
			printf("usage:clear screen\n");
			printf("options            descriptions\n");
			printf("none               see the manual,pls\n");
		}
		else if(strcmp(cmdArray[1],"dir")==0){
			printf("usage:list the file names in the target directory\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"environ")==0){
			printf("usage:list all the environment variables\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"help")==0){
			printf("usage:show the manual of help/get help info of a sepcified command\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"quit")==0){
			printf("usage:quit the shell with thank-you information\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
		else if(strcmp(cmdArray[1],"myshell")==0){
			printf("usage:execute a batchfile\n");
			printf("options            descriptions\n");
			printf("none               see the manual,plz\n");
		}
	}
	else 
	{
		printf("myShell: syntax error for help!\n");
		printf("you can input: help or help [function]\n");
	}
}


void myenviron()
{
	//经查阅，实现environ需要在全局设置environ外部变量
	//实现的过程只需要打印即可
	char** env = environ;
	while (*env)
	{
		printf("%s\n", *env);
		env++;
	}
}
void myenviron_redirect()
{
	pid_t pid;
	char file[MAX_NAME];//存放文件名字
	char oper[MAX_OPER];//存放操作符号
	//将文件名存放在file,操作方式保存在oper
	strcpy(file, cmdArray[2]);
	strcpy(oper, cmdArray[1]);
	//如果输入的操作符错误则需打印提示信息
	if (strcmp(oper, ">") != 0 && strcmp(oper, ">>") != 0)
	{
		printf("myshell: syntax error in operation\n");
		printf("you can type help environ for more information\n");
		return;
	}
	//2. 路径打开过程出错
	if (file == NULL)
	{
		perror("Error in get file name");
		return;
	}
	if (!strcmp(oper, ">>"))
	{
		//使用open()打开文件
		//O_CREAT：如果打开文件不存在则创建
		//O_APPEND: 每次写都加载到文件尾端
		//O_WRONLY：用只写方式打开文件
		int f = open(file,O_CREAT|O_APPEND|O_WRONLY,0600);
		//如果open过程失败
		if (f < 0)
		{
			perror("Error in open file");
			return;
		}
		//进程条件判断语句
		if ((pid = fork()) == 0)
		{
			//子进程
			dup2(f, 1);
			for(int i=0;environ[i] != NULL;i++)
			{
				printf("%s\n",environ[i]);
			}
			exit(0);
		}
		else if (pid > 0)
		{
			//父进程
			//等待子进程的输入文件操作结束
			waitpid(pid, NULL, 0);
		}
		close(f);
	}
	if (!strcmp(oper, ">"))
	{
		//使用open()打开文件
		//O_CREAT：如果打开文件不存在则创建
		//O_TRUNC：如果文件存在以只读或只写来打开
		//O_WRONLY：用只写方式打开文件
		int f = open(file,O_CREAT|O_TRUNC|O_WRONLY,0600);
		//如果open过程失败
		if (f < 0)
		{
			perror("Error in open file");
			return;
		}
		//进程条件判断语句
		if ((pid = fork()) == 0)
		{
			//将stdout重新定向
			dup2(f, 1);
			for(int i=0;environ[i]!=NULL;i++)
			{
				printf("%s\n",environ[i]);
			}
			exit(0);
		}
		else if (pid > 0)
		{
			//父进程
			//等待子进程的输入文件操作结束
			waitpid(pid, NULL, 0);
		}
		close(f);

	}


}


void mytest()
{
	char oper[MAX_OPER];//存放操作符号
	char filename[MAX_LINE];//存储文件名
	//首先检测输入是否符合标准
	if (cmdStringNum != 3)
	{
		//test的输入只能是三个字符，如果不是则打印错误信息
		printf("myshell: test: incorrect number of arguments\n");
		printf("the format is 'test [op] [filename]'\n");
		printf("for more information: use 'help test'\n");
		return ;		
	}
	strcpy(oper, cmdArray[1]);
	strcpy(filename, cmdArray[2]);
	//如果输入符合规定，进入处理语句环节
	FILE* fp;
	fp = fopen(filename, "r");//用只读的方式打开文件
	int mode;
	if (fp == NULL)
	{
		printf("No such file or directory\n");
	}
	if (fp != NULL)
	{
		struct stat buf;
		//调用stat存储文件信息到buf
		stat(filename, &buf);
		//新建ret用来保存
		int ret = buf.st_mode;
		//检测stat的过程是否出现错误
		if (&ret == NULL || &buf == NULL)
		{
			printf("Error in stat function.");
			return;
		}
		//如果没有错误进入参数分析阶段
		//每一次判断，用ret与对应的结果与操作
		//如果用户输入-l
		if (strcmp(oper, "-l") == 0)
		{
			mode = ret & S_IFLNK;
			if (mode == S_IFLNK)
			{
				//说明是symbolic link
				printf("yes,this is a symbolic link\n");
			}
			if (mode != S_IFLNK)
			{
				//printf("no,this is NOT a symbolic link\n");
			}
		}
		//如果用户输入-b
		else if (strcmp(oper, "-b") == 0)
		{
			mode = ret & S_IFBLK;
			//printf("sdfsadf\n");
			if (mode == S_IFBLK)
			{
				//说明是block device
				printf("yes,this is a block device\n");
			}
			if (mode != S_IFBLK)
			{
				//printf("no,this is NOT a block device\n");
			}
		}
		//如果用户输入-c
		else if (strcmp(oper, "-c") == 0)
		{
			mode = ret & S_IFCHR;
			if (mode == S_IFCHR)
			{
				//说明是character device
				printf("yes,this is a character device\n");
			}
			if (mode != S_IFCHR)
			{
				//printf("no,this is NOT a character device\n");
			}
		}
		//如果用户输入-d
		else if (strcmp(cmdArray[1], "-d") == 0)
		{
			mode = ret & S_IFDIR;
			if (mode == S_IFDIR)
			{
				//说明是directory device
				printf("yes,this is a directory device\n");
			}
			if (mode != S_IFDIR)
			{
				printf("no,this is NOT a directory device\n");
			}
		}
		//其他的非法输入
		else
		{
			printf("myshell: test: only 4 options are allowed:\n");
			printf("[-l],[-b],[-c],[-d]\n");
			printf("for more information: use 'help test'\n");
			return;
		}
	}	
}


void mydir()
{
	
	char path[MAX_PATH_LEN];//存放路径信息
	char file[MAX_NAME];//存放文件名字
	DIR *dir;//存放目录信息
	struct dirent *dp;
	getcwd(path,sizeof(path));//获取路径名
	pid_t pid;
	//读取dir以及相关信息
	dir = opendir(path);
	dp = readdir(dir);
	printf("the directory under the current path is:\n");
	if (dir == NULL)
	{
		perror("Error in open dir");
		return;
	}
	if (dp == NULL)
	{
		perror("Error in read dir");
		return;
	}
	while (dp != NULL)
	{
		printf("%s\n", dp->d_name);
		dp = readdir(dir);
	}
}
void mydir_redirect()
{
	//重定向的过程>>向文件追加
	//重定向对过程>重写整个文件
	char path[MAX_PATH_LEN];//存放路径信息
	char file[MAX_NAME];//存放文件名字
	char oper[MAX_OPER];//存放操作符号
	getcwd(path, MAX_PATH_LEN);
	DIR* dir = opendir(path);
	struct dirent *dp;
	//将文件名存放在file,操作方式保存在oper
	strcpy(file, cmdArray[2]);
	strcpy(oper, cmdArray[1]);
	pid_t pid;
	//1. 文件读取过程出错
	
	if (dir == NULL)
	{
		perror("Error in open dir");
		return;
	}
	//2. 路径打开过程出错
	if (file == NULL)
	{
		perror("Error in get file name");
		return;
	}
	//检验输入的操作符号
	if (strcmp(oper, ">") != 0 && strcmp(oper, ">>") != 0)
	{
		printf("myshell: syntax error in operation\n");
		printf("you can type help echo for more information\n");
		return;
	}
	if (!strcmp(oper, ">>"))
	{
		//使用open()打开文件
		//O_CREAT：如果打开文件不存在则创建
		//O_APPEND: 每次写都加载到文件尾端
		//O_WRONLY：用只写方式打开文件
		int f = open(file,O_CREAT|O_APPEND|O_WRONLY,0600);
		//如果open过程失败
		if (f < 0)
		{
			perror("Error in open file");
			return;
		}
		//如果open过程失败
		if (f < 0)
		{
			perror("Error in open file");
			return;
		}
		//创建子进程用于重新定向
		//进程条件判断语句
		if ((pid = fork()) == 0)
		{
			//子进程
			while ((dp = readdir(dir)) != NULL)
			{
				//将stdout重新定向
				dup2(f, 1);
				//在文件末尾写入
				printf("%s\n", dp->d_name);
			}
			exit(0);
		}
		else if (pid > 0)
		{
			//父进程
			//等待子进程的输入文件操作结束
			waitpid(pid, NULL, 0);
		}
		close(f);
	}
	else if (!strcmp(oper, ">"))
	{
		//使用open()打开文件
		//O_CREAT：如果打开文件不存在则创建
		//O_TRUNC：如果文件存在以只读或只写来打开
		//O_WRONLY：用只写方式打开文件
		int f = open(file,O_CREAT|O_TRUNC|O_WRONLY,0600);
		//如果open过程失败
		if (f < 0)
		{
			perror("Error in open file");
			return;
		}
		//创建子进程用于重新定向

		//进程条件判断语句
		if ((pid = fork()) == 0)
		{
			//子进程
			while ((dp = readdir(dir)) != NULL)
			{
				//将stdout重新定向
				dup2(f, 1);
				printf("%s\n", dp->d_name);
			}
			exit(0);
		}
		else if (pid > 0)
		{
			//父进程
			//等待子进程的输入文件操作结束
			waitpid(pid, NULL, 0);
		}
		close(f);
	}

}

int parseMask(int b)
{
	int Mask;
	switch(b)
	{
		case 0:
			Mask = 0000;
			break;
		case 1:
			Mask = 0001;
			break;
		case 2:
			Mask = 0002;
			break;
		case 3:
			Mask = 0003;
			break;
		case 4:
			Mask = 0004;
			break;
		case 5 :
			Mask = 0005;
			break;
		case 6 :
			Mask = 0006;
			break;
		case 7 :
			Mask = 0007;
			break;
		default:
			printf("myshell: out of range\n");
	}
	return Mask;
}

void myumask()
{
	int rMask, wMask, xMask;
	int a, b, c, d;
	
	
	//abcd用于存放用户输入的mask
	if (cmdStringNum > 1)
	{
		a = (int)(cmdArray[1][0]) - 48;
		b = (int)(cmdArray[1][1]) - 48;
		c = (int)(cmdArray[1][2]) - 48;
		d = (int)(cmdArray[1][3]) - 48;
	}
	
	if (cmdStringNum == 1 && ever == 0)
	{ 
		//如果用户只输入一个umask则打印默认值0022
		printf("0022\n");	
		return;
	}
	//如果此时已经是被改变了
	else if (cmdStringNum == 1 && ever == 1)
	{
		//如果用户只输入一个umask打印当前mask
		printf("%04o\n", newMask);	
		return;
	}
	if (cmdStringNum == 2 && a == 0)
	{
		//ever用于存放mask是否已经被更改了
		ever = 1;
		rMask = parseMask(b) * 64;
		wMask = parseMask(c) * 8;
		xMask = parseMask(d) * 1;
		//将bcd的值分别赋值给r，w，x
		int sum = rMask + wMask + xMask;
		newMask = sum;
		umask(newMask);
		printf("myshell: umask changed successfully\n");
		printf("myshell: umask value: %04o\n", newMask);
	}
}

void myclr(){
	//保存光标设置
	printf("\033[s"); 
	//清除屏幕
	printf("\033[2J");
	//把光标移动到合适的位置
	printf("\033[H");
}

void myjobs()
{
	pid_t pid;
	
	//创建子进程用于重新定向
	pid = fork();
	//进程条件判断语句
	if (pid == 0)
	{
		//子进程
		//ps是程序名
		execlp("ps", "ps", NULL);
	}
	else if (pid > 0)
	{
		//父进程
		//等待子进程的输入文件操作结束
		waitpid(pid, NULL, 0);
	}
}

void myfg(pid_t pid)
{
	pid_t pgid;
	//将pid进程的进程组ID（PGID）设置为pgid
	setpgid(pid, pgid);
	int result;
	//设置进程组为前台进程组
	result = tcsetpgrp(1, getpgid(pgid));
	//根据返回值判断是否修改设置成功
	if ( result == 0)
	{
		//向对应的进程发送SIG_CONT信号
		kill(pgid, SIGCONT);
		//等待子进程的输入文件操作结束
		waitpid(pgid, NULL, WUNTRACED);
	}
	else if (result == -1)
	{
		//如果没有设置成功输出相应信息
		printf("myshell: fg: no such job\n");
	}

}

void mybg(pid_t pid)
{
	//向对应的进程发送SIG_CONT信号
	if (kill(pid, SIGCONT) < 0)
	{
		//如果有错就打印提示信息
		printf("myshell: bg: no such job\n");
	}
	else 
	{
		//等待子进程的输入文件操作结束
		waitpid(pid, NULL, WUNTRACED);
	}
}

void myexec()
{
	pid_t pid;
	//只有exec后面有东西的时候才进行，否则return
	if (cmdStringNum > 1)
	{
		//pid = fork();
		if ((pid = fork()) == 0)
		{
			//子进程，退出
			exit(0);
		}
		else if (pid > 0)
		{
			//调用execvp()
			if (execvp(cmdArray[1], cmdArray + 1) < 0)
			{
				printf("myShell: command not found\n");
			}
			//exit(0);
		}
	}
	return;
}

void myshell_op()
{
	char oper[MAX_OPER];
	char file[MAX_NAME];
	pid_t pid;
	char* cptr;
	int fmark;
	strcpy(oper, cmdArray[0]);
	strcpy(file, cmdArray[1]);
	FILE *fp = fopen(file, "r");
	FILE *helper = fopen(file, "r");
	if (fp == NULL)
	{
		printf("myshell: no such file\n");
		return;
	}
	//printf("myshell\n");
	int count = 0;
	while ((fmark = fgetc(fp)) != EOF)
	{
		//获取文件命令的个数
		if (fmark == '\n')
		{
			count++;
		}
		if (fmark == ' ')
			continue;
	}
	//如果命令是零个直接退出
	if (count == 0)
	{
		return;
	}
	//对于每一个命令
	for (int i = 0; i < count; i++)
	{
		initdate();
		int num = 0;
		newnum = 0;
		for (int j = 0; j < MAX_OPER; j++)
		{
			oper[j] = '\0';
		}
		if (helper == NULL)
		{
			printf("myshell: no such file\n");
			return;
		}
		fgets(oper, MAX_OPER, helper);
		oper[strlen(oper) - 1] = '\0';
		cptr = strtok(oper,  " ");
		while(cptr != NULL)
		{
			cmdArray[num] = (char*)malloc(sizeof(*cptr));
			newnum = num++;
			strcpy(cmdArray[newnum], cptr);
			cptr = strtok(NULL, " ");
		}
		cmdStringNum = newnum;
		//printf("指令书%d\n", cmdStringNum);
		is_internal_cmd();
		if (is_internal > 0)
		{
			continue;
		}
		is_pipe_cmd();
		if (is_pipe != 0)
		{
			pipe(pipe_fd);
		}
		if ((pid = fork()) == 0)
		{
			//获取子进程pid
			int thispid = getpid();
			signal(SIGINT,SIG_DFL);//默认是终止进程
			signal(SIGTSTP,SIG_DFL);//默认是暂停进程
			signal(SIGCONT,SIG_DFL);//默认是继续这个进程
			if (is_pipe != 0)
			{
				close(pipe_fd[0]);
				dup2(pipe_fd[1], 1);
			}
			if (is_bg == 1)
			{
				is_external_cmd(0);
				exit(0);
			}
			is_io_redirection();
			is_external_cmd(0);
			break;
		}
		else if (pid > 0)
		{
			//信号操作
			signal(SIGINT,SIG_IGN);
			signal(SIGTSTP,SIG_IGN);
			signal(SIGCONT,SIG_DFL);
			if (is_bg == 1)
			{
				//忽视SIGCHLD信号
				signal(SIGCHLD,SIG_IGN);
			}
			else
			{
				//等待子进程结束
				waitpid(pid, NULL, WUNTRACED);
			}
			//执行pipe
			do_pipe(is_pipe);
		}
	}
}





int main()
{
	printf("=============MyShell==============\n");
	printf("Welcome to my shell\n");
	printf("It is a unix-like shell program\n");
	printf("Programer: XiuHaibo 3160105286\n");
	int should_run = 1;
	pid_t pid;
	while (should_run)
	{
		init();
		readCommand();
		if (cmdStringNum == 0)
			continue;
		is_bg_cmd();
		//printf("是bg吗？  is_bg = %d\n", is_bg);
		is_pipe_cmd();
		//printf("是pipe吗？  is_pipe = %d\n", is_pipe);
		is_internal_cmd();
		if (is_internal)
		{
			continue;
		}
			
		if (is_pipe != 0)
		{
			pipe(pipe_fd);
		}


		if ((pid = fork()) == 0)
		{
			int t = getpid();
			//处理信号SIGINT停止进程
			signal(SIGINT, SIG_DFL);
			signal(SIGTSTP, SIG_DFL);
			signal(SIGCONT, SIG_DFL);
			if (is_pipe != 0)
			{
				close(pipe_fd[0]);//关闭读
				dup2(pipe_fd[1],1);//把stdout重定向到pipe_fd[1]
			}

			if (is_bg == 1)
			{
				printf("myshell: in background: the job's pid: [%d]\n",t);
				is_external_cmd(0);//执行外部命令
				return 0;
			}
			is_io_redirection();
			//printf("hhh\n");
			is_external_cmd(0);
			return 0;
		}
		else if (pid > 0)
		{
			signal(SIGINT,SIG_IGN);//忽视这个信号
			signal(SIGTSTP,SIG_IGN);//忽视这个信号
			signal(SIGCONT,SIG_DFL);//用默认的方式处理SIGCONT
			if(is_bg == 1){//如果需要在后台执行
				signal(SIGCHLD,SIG_IGN);//忽视SIGCHLD
			}
			else
			{
				waitpid(pid, NULL, WUNTRACED);
			}
			if (is_pipe != 0)
			{
				do_pipe(is_pipe);
			}
			
		}
	}


	return 0;
}
/*处理正常的内部命令*/
void is_internal_cmd()
{
	char oper[MAX_OPER];
	int result;
	int echo_r_flag = 0;
	pid_t pid;
	is_internal = 1;
	strcpy(oper, cmdArray[0]);
	for (int i = 0 ; i < 21; i++)
	{
		//如果经判断找到相应的函数则在这里进行滚结果匹配
		if (strcmp(oper, commandNameArray[i]) == 0)
		{
			result = i;
			//改变 is_internal的值为1说明是内部指令
			is_internal = 1;
		}
	}
	//根据结果的不同，用case进行函数分配
	switch (result)
	{
		case 0://打印再见信息，并退出程序
			printf("Thanks for using myShell, bye:)\n");
			sleep (1);
			exit(0);
			break;
		case 1://直接退出程序
			exit(0);
			break;
		case 2://执行清屏操作
			myclr();
			break;
		case 3:
			myclr();
			break;
		case 4:
			mypwd();
			break;
		case 5:
			//用户输入echo
			for (int j = 1; j < cmdStringNum; j++)
			{
				if (strcmp(cmdArray[j], ">") == 0 || strcmp(cmdArray[j], ">>") == 0 )
				{
					//进入重定向函数并改变flag
					myecho_redirect();
					echo_r_flag = 1;
					break;
				}
			}
			//flag = 0说明没有重定向
			if (echo_r_flag == 0)
			{
				myecho();
			}
			break;
		case 6:
			mytime();
			break;
		case 7:
			if (cmdArray[1] != NULL && (strcmp(cmdArray[1], ">") == 0 || strcmp(cmdArray[1], ">>") == 0))
			{
				myenviron_redirect();
				break;
			}
			myenviron();
			break;
		case 8:
			mycd();
			break;
		case 9:
			if (cmdArray[1] != NULL && (strcmp(cmdArray[1], ">") == 0 || strcmp(cmdArray[1], ">>") == 0))
			{
				//myhelp_redirect();
				break;
			}
			myhelp();
			break;
		case 10:
			myexec();
			break;
		case 11:
			mytest();
			break;
		case 12:
			myumask();
			break;
		case 13:
			myjobs();
			break;
		case 14:
			//fg命令，将任务从后台转移到前台
			if (cmdArray[1] != NULL)
			{
				//将字符串转换为整性数获取pid
				pid = atoi(cmdArray[1]);
			}
			else
			{
				printf("myShell: fg: current: no such job");
				break;
			}	
			myfg(pid);
			break;
		case 15://bg命令
			
			if (cmdArray[1] != NULL)
			{
				//将字符串转换为整性数获取pid
				pid = atoi(cmdArray[1]);
			}
			else 
			{
				//经查找资料，没有参数的时候自动填1
				//前提是只有一个程序挂起
				strcpy(cmdArray[1], "1");
				pid = atoi(cmdArray[1]);
			}
			mybg(pid);
			break;
		case 16:
			myshell_op();
			break;
		case 17:
			if (cmdArray[1] != NULL && (strcmp(cmdArray[1], ">") == 0 || strcmp(cmdArray[1], ">>") == 0))
				{
					mydir_redirect();
					break;
				}
			mydir();
			break;
		case 18:
			myset();
			break;
		case 19:
			myunset();
			break;
		default:
			is_internal = 0;
		//	printf("myshell: %s: command not found\n", cmdArray[0]);
			break;
	}

}

void myset()
{
	char valueName[MAX_NAME];
	if (cmdStringNum != 3)
	{
		//如果不符合规范
		printf("Syntax error for set!\n");
		printf("Please type help for more information\n");
		return;
	}
	else 
	{
		//valueName保存变量名字
		strcpy(valueName, cmdArray[1]);
		if (strcmp(valueName, "s1") == 0)
		{
			values1 = int(cmdArray[2]) - 48;
			printf("New value of s1 is %d\n", values1);
		}
		if (strcmp(valueName, "s1") == 0)
		{
			values2 = int(cmdArray[2]) - 48;
			printf("New value of s2 is %d\n", values2);
		}
	}
}
void myunset()
{
	char valueName[MAX_NAME];
	if (cmdStringNum != 2)
	{
		//如果不符合规范
		printf("Syntax error for set!\n");
		printf("Please type help for more information\n");
		return;
	}
	else
	{
		strcpy(valueName, cmdArray[1]);
		if (strcmp(valueName, "s1") == 0)
		{
			printf("Old value of s1 is %d\n", values1);
			printf("s1 has been deleted\n", values1);
		}
		if (strcmp(valueName, "s1") == 0)
		{
			printf("Old value of s2 is %d\n", values2);
			printf("s2 has been deleted\n", values2);
		}
	}
}