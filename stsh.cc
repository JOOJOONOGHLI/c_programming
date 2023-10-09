/**
 * File: stsh.cc
 * -------------
 * Defines the entry point of the stsh executable.
 */

#include "stsh-parser/stsh-parse.h"
#include "stsh-parser/stsh-readline.h"
#include "stsh-parser/stsh-parse-exception.h"
#include "stsh-signal.h"
#include "stsh-job-list.h"
#include "stsh-job.h"
#include "stsh-process.h"
#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>  // for fork
#include <signal.h>  // for kill
#include <sys/wait.h>
#include <assert.h>
#include "fork-utils.h" // this needs to be the last #include in the list
using namespace std;

static STSHJobList joblist; // the one piece of global data we need so signal handlers can access it

static void unblockReleventSignals() {
    sigset_t additions;
	sigemptyset(&additions);
	sigaddset(&additions, SIGCHLD);
    sigaddset(&additions, SIGINT);
	sigaddset(&additions, SIGTSTP);

    sigprocmask(SIG_UNBLOCK, &additions, NULL);
    
}

static bool signalsAreBlocked(const vector<int>& signals) {
  sigset_t empty, current;
  sigemptyset(&empty);
  sigprocmask(SIG_BLOCK, &empty, &current);
  for (int sig: signals) {
    if (!sigismember(&current, sig)) return false;
  }
  return true;
}

static void fg(const pipeline& pipeline) {

    // block signals before fg call
    sigset_t additions, existingmask;
	sigemptyset(&additions);
    sigemptyset(&existingmask); // take out if necessary
	sigaddset(&additions, SIGCHLD);
    sigaddset(&additions, SIGINT);
	sigaddset(&additions, SIGTSTP);
	sigprocmask(SIG_BLOCK, &additions, &existingmask);

    // get jobNum from input
    char *input = pipeline.commands[0].tokens[0];
    if(input == nullptr) throw STSHException("Usage: fg <jobid>.");
    int jobNum = atoi(input);
    if(!jobNum) throw STSHException("Usage: fg <jobid>.");
    if (!joblist.containsJob(jobNum)) throw STSHException("fg " + to_string(jobNum) + ": No such job.");
    
    // get job and continue the process group
    STSHJob& job = joblist.getJob(jobNum);
    for (const auto& p : job.getProcesses()) {
        if (p.getState() == kStopped) {
            kill(-job.getGroupID(), SIGCONT);
            break;
        }
    }

    // set job state to foreground
    job.setState(kForeground);

    assert(signalsAreBlocked({SIGCHLD}));
    while(joblist.hasForegroundJob()) {
        assert(signalsAreBlocked({SIGCHLD}));
		sigsuspend(&existingmask);
        assert(signalsAreBlocked({SIGCHLD}));
	}
    assert(signalsAreBlocked({SIGCHLD}));

    // unblock signals after fg call
	sigprocmask(SIG_UNBLOCK, &additions, NULL);
}

static void bg(const pipeline& pipeline) {
    
    // get JobNum from input
    char *input = pipeline.commands[0].tokens[0];
    if(input == nullptr) throw STSHException("Usage: bg <jobid>.");
    int jobNum = atoi(input);
    if(!jobNum) throw STSHException("Usage: bg <jobid>.");
    if (!joblist.containsJob(jobNum)) throw STSHException("bg " + to_string(jobNum) + ": No such job.");

    // get job and set its state to background
    STSHJob& job = joblist.getJob(jobNum);
    job.setState(kBackground);
    
    // send continue signal to process group of job
	pid_t pgid = job.getGroupID();
	kill(-pgid, SIGCONT);
    
}

static void slay(const pipeline& pipeline) {

    char *inputOne = pipeline.commands[0].tokens[0];
    if (inputOne == nullptr) throw STSHException("Usage: slay <jobid> <index> | <pid>.");
    int firstNum = atoi(inputOne);
    
    char *inputTwo = pipeline.commands[0].tokens[1];

    if (inputTwo == nullptr) { // one argument: only PID (firstNum)
        if (!joblist.containsProcess(firstNum)) throw STSHException("No process with pid " + to_string(firstNum));

        // kill singular process
        kill(firstNum, SIGKILL);
    } else { // two arguments: both job ID (firstNum) and process index (secondNum)
        int secondNum = atoi(inputTwo);
        if (!joblist.containsJob(firstNum)) throw STSHException("No job with id of " + to_string(firstNum));
        
        // get job
        STSHJob& job = joblist.getJob(firstNum);

        // get the process within the job that needs to be killed
        
        // get all the processes within the job
        vector<STSHProcess>& processes = job.getProcesses();

        // index into processes vector and get PID
        STSHProcess& process = processes[secondNum];
        if (!job.containsProcess(secondNum)) throw STSHException("No process with pid at index " + to_string(firstNum));
        int pid = process.getID();

        // kill singular process
        kill(pid, SIGKILL);
        
    }
}

static void halt(const pipeline& pipeline) {

    char *inputOne = pipeline.commands[0].tokens[0];
    if (inputOne == nullptr) throw STSHException("Usage: halt <jobid> <index> | <pid>.");
    int firstNum = atoi(inputOne);
    
    char *inputTwo = pipeline.commands[0].tokens[1];

    if (inputTwo == nullptr) { // one argument: only PID (firstNum)
        if (!joblist.containsProcess(firstNum)) throw STSHException("No process with pid " + to_string(firstNum));

        // kill singular process
        kill(firstNum, SIGKILL);
    } else { // two arguments: both job ID (firstNum) and process index (secondNum)
        int secondNum = atoi(inputTwo);
        if (!joblist.containsJob(firstNum)) throw STSHException("No job with id of " + to_string(firstNum));
        
        // get job
        STSHJob& job = joblist.getJob(firstNum);

        // get the process within the job that needs to be killed
        
        // get all the processes within the job
        vector<STSHProcess>& processes = job.getProcesses();

        // index into processes vector and get PID
        STSHProcess& process = processes[secondNum];
        if (!job.containsProcess(secondNum)) throw STSHException("No process with pid at index " + to_string(firstNum));
        int pid = process.getID();

        // kill singular process
        kill(pid, SIGTSTP);
        
    }
}

static void cont(const pipeline& pipeline) {

    char *inputOne = pipeline.commands[0].tokens[0];
    if (inputOne == nullptr) throw STSHException("Usage: cont <jobid> <index> | <pid>.");
    int firstNum = atoi(inputOne);
    
    char *inputTwo = pipeline.commands[0].tokens[1];

    if (inputTwo == nullptr) { // one argument: only PID (firstNum)
        if (!joblist.containsProcess(firstNum)) throw STSHException("No process with pid " + to_string(firstNum));

        // kill singular process
        kill(firstNum, SIGKILL);
    } else { // two arguments: both job ID (firstNum) and process index (secondNum)
        int secondNum = atoi(inputTwo);
        if (!joblist.containsJob(firstNum)) throw STSHException("No job with id of " + to_string(firstNum));
        
        // get job
        STSHJob& job = joblist.getJob(firstNum);

        // get the process within the job that needs to be killed
        
        // get all the processes within the job
        vector<STSHProcess>& processes = job.getProcesses();

        // index into processes vector and get PID
        STSHProcess& process = processes[secondNum];
        if (!job.containsProcess(secondNum)) throw STSHException("No process with pid at index " + to_string(firstNum));
        int pid = process.getID();

        // kill singular process
        kill(pid, SIGCONT);
        
    }
}

/**
 * Function: handleBuiltin
 * -----------------------
 * Examines the leading command of the provided pipeline to see if
 * it's a shell builtin, and if so, handles and executes it.  handleBuiltin
 * returns true if the command is a builtin, and false otherwise.
 */


static const string kSupportedBuiltins[] = {"quit", "exit", "fg", "bg", "slay", "halt", "cont", "jobs"};
static const size_t kNumSupportedBuiltins = sizeof(kSupportedBuiltins)/sizeof(kSupportedBuiltins[0]);
static bool handleBuiltin(const pipeline& pipeline) {
    const string& command = pipeline.commands[0].command;
    auto iter = find(kSupportedBuiltins, kSupportedBuiltins + kNumSupportedBuiltins, command);
    if (iter == kSupportedBuiltins + kNumSupportedBuiltins) return false;
    size_t index = iter - kSupportedBuiltins;

    switch (index) {
    case 0:
    case 1: exit(0);
    case 2: fg(pipeline); break;
    case 3: bg(pipeline); break;
    case 4: slay(pipeline); break;
    case 5: halt(pipeline); break;
    case 6: cont(pipeline); break;
    case 7: cout << joblist; break;
    default: throw STSHException("Internal Error: Builtin command not supported."); // or not implemented yet
    }
  
    return true;
}

static void sigchld_handler(int sig) {
	while(true) {
		int status;
		pid_t pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED);

		if(pid <= 0) break;

        if (WIFEXITED(status)) {
            if (!joblist.containsProcess(pid)) return; // take out if necessary
            STSHJob& job = joblist.getJobWithProcess(pid);
            assert(job.containsProcess(pid));
            STSHProcess& process = job.getProcess(pid);
            process.setState(kTerminated);
            joblist.synchronize(job);
            
        }

        if (WIFSIGNALED(status)) {
            if (!joblist.containsProcess(pid)) return;
            STSHJob& job = joblist.getJobWithProcess(pid);
            assert(job.containsProcess(pid));
            STSHProcess& process = job.getProcess(pid);
            process.setState(kTerminated);
            joblist.synchronize(job);
            
        }

        if (WIFSTOPPED(status)) {
            if (!joblist.containsProcess(pid)) return;
            STSHJob& job = joblist.getJobWithProcess(pid);
            assert(job.containsProcess(pid));
            STSHProcess& process = job.getProcess(pid);
            process.setState(kStopped);
            joblist.synchronize(job);
            
        }

        if (WIFCONTINUED(status)) {
            if (!joblist.containsProcess(pid)) return;
            STSHJob& job = joblist.getJobWithProcess(pid);
            assert(job.containsProcess(pid));
            STSHProcess& process = job.getProcess(pid);
            process.setState(kRunning);
            joblist.synchronize(job);
        }
        
	}

    // give stdin control back to shell
    if (!joblist.hasForegroundJob()) {
        tcsetpgrp(STDIN_FILENO, getpgrp());
    }
}

static void sigint_handler(int sig) {
	if(joblist.hasForegroundJob()) {
		STSHJob& job = joblist.getForegroundJob();
	    pid_t pgid = job.getGroupID();
	    kill(-pgid, SIGINT);
	}	
}

static void sigtstp_handler(int sig) {
	if(joblist.hasForegroundJob()) {
		STSHJob& job = joblist.getForegroundJob();
        pid_t pgid = job.getGroupID();
		kill(-pgid, SIGTSTP);
	}	
}

/**
 * Function: installSignalHandlers
 * -------------------------------
 * Installs user-defined signals handlers for four signals
 * (once you've implemented signal handlers for SIGCHLD, 
 * SIGINT, and SIGTSTP, you'll add more installSignalHandler calls) and 
 * ignores two others.
 *
 * installSignalHandler is a wrapper around a more robust version of the
 * signal function we've been using all quarter.  Check out stsh-signal.cc
 * to see how it works.
 */


static void installSignalHandlers() {
    installSignalHandler(SIGQUIT, [](int sig) { exit(0); });
    installSignalHandler(SIGTTIN, SIG_IGN);
    installSignalHandler(SIGTTOU, SIG_IGN);
    installSignalHandler(SIGCHLD, sigchld_handler);
    installSignalHandler(SIGINT, sigint_handler);
    installSignalHandler(SIGTSTP, sigtstp_handler);
}

/**
 * Function: createJob
 * -------------------
 * Creates a new job on behalf of the provided pipeline.
 */

static void createJob(const pipeline& p) {

    STSHJobState jobState = kForeground;
    if (p.background) jobState = kBackground;
    
    STSHJob& job = joblist.addJob(jobState);
    const vector<command>& commands = p.commands;
    size_t numCommands = commands.size();
    pid_t pgid = 0;

    // allocate file descriptors
    int fds[(numCommands - 1) * 2];
    for (int i = 0; i < numCommands - 1; i ++) {
        pipe2(fds + i * 2, O_CLOEXEC);
    }

    for (int i = 0; i < p.commands.size(); i++) {
        pid_t pid = fork();
        if (pid == 0) { // child
            setpgid(pid, job.getGroupID());
            // rewire file descriptors
            if (i == 0) { // first command in pipeline

                // if input exists, rewire input in pipeline
                if (!p.input.empty()) {
                    int inputFd = open(p.input.c_str(), O_RDONLY);
                    if (inputFd == -1) throw STSHException("Could not open \"" + p.input + "\".");
					dup2(inputFd, STDIN_FILENO);
					close(inputFd);
				}
                
                if (p.commands.size() > 1) {
                    close(fds[i * 2]); // close read end
                    dup2(fds[(i * 2) + 1], STDOUT_FILENO); // rewire standard output to write end
                    close(fds[(i * 2) + 1]); // close original write end
                }

            }
            
            if (i == p.commands.size() - 1) { // last command in pipeline
                
                // if output exists, rewire output in pipeline
                if (!p.output.empty()) {
                    int outputFd = open(p.output.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0644);
                    if (outputFd == -1) throw STSHException("Could not open \"" + p.output + "\".");
                    dup2(outputFd, STDOUT_FILENO);
                    close(outputFd);
				}

                if (p.commands.size() > 1) {
                    close(fds[((i - 1) * 2) + 1]); // close write end
                    dup2(fds[(i - 1) * 2], STDIN_FILENO); // rewire standard input to read end
                    close(fds[(i - 1) * 2]); // close original read end
                }

            }
            
            if ((i != 0) && (i != p.commands.size() - 1))  { // everything in between

                close(fds[((i - 1) * 2) + 1]); // close write end
                dup2(fds[(i - 1) * 2], STDIN_FILENO); // rewire standard input to read end
                close(fds[(i - 1) * 2]); // close original read end
                
                close(fds[i * 2]); // close read end
                dup2(fds[(i * 2) + 1], STDOUT_FILENO); // rewire standard output to write end
                close(fds[(i * 2) + 1]); // close original write end
                
            }
            
            // set up arguments
            char *arguments[kMaxArguments + 2] = {NULL};
            arguments[0] = const_cast<char*>(p.commands[i].command);
            for (int j = 0; j <= kMaxArguments && p.commands[i].tokens[j] != NULL; j++) {
                // arguments
                arguments[j + 1] = p.commands[i].tokens[j];
            }
            
            // execvp
            int error = execvp(arguments[0], arguments);
            if (error < 0) throw STSHException(string(p.commands[i].command) + ": Command not found.");
        }

       job.addProcess(STSHProcess(pid, p.commands[i]));
            
       if (i == 0) {
           pgid = pid;
           setpgid(pid, pgid);
       }
            
            
    }

    // close fds
	for(int i = 0; i < p.commands.size() - 1; i++) {
		close(fds[i * 2]); // close read end
		close(fds[(i * 2) + 1]); // close write end
    }

    sigset_t additions, existingmask;
	sigemptyset(&additions);
    sigemptyset(&existingmask); // take out if necessary
	sigaddset(&additions, SIGCHLD);
    sigaddset(&additions, SIGINT);
	sigaddset(&additions, SIGTSTP);
	sigprocmask(SIG_BLOCK, &additions, &existingmask);

	if (p.background) {
		string str = "";
		str += "[" + to_string(job.getNum()) + "]";
		cout << str << " ";
		vector<STSHProcess>& processes = job.getProcesses();
		for(unsigned int i = 0; i < processes.size(); i++) {
			cout << processes[i].getID() << "  ";
		}
		cout << endl;
	}

	if (joblist.hasForegroundJob()) {
		int err = tcsetpgrp(STDIN_FILENO, pgid);
		if (err == -1) {
			if(errno != ENOTTY) {
				throw STSHException("Error.");
			}
		}
	}

    assert(signalsAreBlocked({SIGCHLD}));
	while(joblist.hasForegroundJob()) {
        assert(signalsAreBlocked({SIGCHLD}));
		sigsuspend(&existingmask);
        assert(signalsAreBlocked({SIGCHLD}));
	}
    assert(signalsAreBlocked({SIGCHLD}));
    
	sigprocmask(SIG_UNBLOCK, &additions, NULL);

	pid_t parent = getpgid(getpid());
	int err = tcsetpgrp(STDIN_FILENO, parent);
	if(err == -1) {
		if(errno != ENOTTY) {
			throw STSHException("A more serious problem happens.");
		}
	}	
}

/**
 * Function: main
 * --------------
 * Defines the entry point for a process running stsh.
 * The main function is little more than a read-eval-print
 * loop (i.e. a repl).  
 */

int main(int argc, char *argv[]) {
  pid_t stshpid = getpid();
  installSignalHandlers();
  rlinit(argc, argv);
  while (true) {
    string line;
    if (!readline(line)) break;
    if (line.empty()) continue;
    try {
      pipeline p(line);
      bool builtin = handleBuiltin(p);
      if (!builtin) createJob(p);
    } catch (const STSHException& e) {
      cerr << e.what() << endl;
      unblockReleventSignals();
      if (getpid() != stshpid) exit(0); // if exception is thrown from child process, kill it
    }
  }

  return 0;
}