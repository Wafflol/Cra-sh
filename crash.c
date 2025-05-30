#include <bits/types/sigset_t.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>

#define MAXLINE 1024
#define MAXJOBS 32

typedef struct {
    bool valid; //valid = 0 means free job 
    size_t jobNum;
    size_t pid;
    char jobNumStr[32];
    char pidStr[32];
    char status[32];
    char processName[MAXLINE + 128];
} Job;


int jobs = 0;
int activeJobs = 0;
Job jobList[MAXJOBS];
int fg_pid = 0;

sigset_t globalMask;

int findFreeJobInd() {
    for (int i = 0; i < MAXJOBS; i++) {
        if (!jobList[i].valid) {
            return i; 
        }
    }
    return -1;
}

Job* findJobByPID(size_t pid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobList[i].valid && jobList[i].pid == pid) {
            return &jobList[i];
        }
    }
    return NULL;
}

Job* findJobByJobNum(size_t jobNum) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobList[i].valid && jobList[i].jobNum == jobNum) {
            return &jobList[i];
        }
    }
    return NULL;
}

char* toString(Job* job) {
    char* out = malloc(2048);
    if (out == NULL) {
        return NULL;
    }
    snprintf(out, 2048, "[%d] (%d)  %s  %s", (int) job->jobNum, 
             (int) job->pid, job->status, job->processName);
    return out;
}

bool addJob(size_t pid, char* status, const char* processName) {
    if (activeJobs >= 32) {
        return false;
    }
    sigset_t mask, old_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &old_mask);
    int ind = findFreeJobInd();
    Job newJob;
    char pidStr[32];
    char jobStr[32];
    char processNameStr[MAXLINE + 128];
    char statusStr[32];

    newJob.valid = true; // set valid

    newJob.jobNum = jobs + 1; //set job num
    snprintf(jobStr, sizeof(jobStr), "%d", (int) jobs+1); //set job str buffer
    strcpy(newJob.jobNumStr, jobStr); //set job str

    newJob.pid = pid;
    snprintf(pidStr, sizeof(pidStr), "%d", (int) pid);
    strcpy(newJob.pidStr, pidStr); //set pid str

    snprintf(statusStr, sizeof(statusStr), "%s", status);
    strcpy(newJob.status, statusStr);

    snprintf(processNameStr, sizeof(processNameStr), "%s", processName);
    strcpy(newJob.processName, processNameStr);

    jobList[ind] = newJob;
    activeJobs++;
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
    return true;
}

bool removeJob(size_t pid) {
    Job* job = findJobByPID(pid);
    if (!job) return false;
    job->valid = 0;
    activeJobs--;
    return true;
}

void sigTstpHandler(int signo) {
    sigprocmask(SIG_BLOCK, &globalMask, NULL);
    Job* job = findJobByPID(fg_pid);
    strcpy(job->status, "suspended");
    printf("changed status\n");
    char buffer[64];
    memset(buffer, 0, 64);
    strcat(buffer, "[");
    strcat(buffer, job->jobNumStr);
    strcat(buffer, "] (");
    strcat(buffer, job->pidStr);
    strcat(buffer, ")  suspended  ");
    strcat(buffer, job->processName);
    strcat(buffer, "\n");
    write(STDOUT_FILENO, buffer, strlen(buffer));
    if (fg_pid != 0) {
        kill(fg_pid, SIGTSTP);
    }
    sigprocmask(SIG_UNBLOCK, &globalMask, NULL);
}

void sigQuitHandler(int signo) {
    sigprocmask(SIG_BLOCK, &globalMask, NULL);
    if (fg_pid != 0) {
        kill(fg_pid, SIGQUIT);
    }
    else {
        exit(0);
    }
    sigprocmask(SIG_UNBLOCK, &globalMask, NULL);
}

void sigIntHandler(int signo) {
    sigprocmask(SIG_BLOCK, &globalMask, NULL);
    if (fg_pid != 0) {
        kill(fg_pid, SIGINT);
    }
    sigprocmask(SIG_UNBLOCK, &globalMask, NULL);
}

void sigChldHandler(int signo) {
    sigprocmask(SIG_BLOCK, &globalMask, NULL); //block signals

    pid_t child_pid;
    int status;
    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        //child dead, remove from joblist
        Job* child = findJobByPID(child_pid);
        char buff[MAXLINE * 2];
        if (WIFEXITED(status)) {
            strcpy(buff, "[");
            strcat(buff, child->jobNumStr);
            strcat(buff, "] (");
            strcat(buff, child->pidStr);
            strcat(buff, ")  finished  ");
            strcat(buff, child->processName);
            strcat(buff, "\n");
            write(STDOUT_FILENO, buff, strlen(buff));
        }
        // Check if the child crashed (terminated by signal)
        else if (WIFSIGNALED(status)) {
            strcpy(buff, "[");
            strcat(buff, child->jobNumStr);
            strcat(buff, "] (");
            strcat(buff, child->pidStr);
            if (WCOREDUMP(status)) {
                strcat(buff, ")  killed (core dumped)  ");
            }
            else {
                strcat(buff, ")  killed  ");
            }
            strcat(buff, child->processName);
            strcat(buff, "\n");
            write(STDOUT_FILENO, buff, strlen(buff));
        }
        //remove from joblist
        removeJob(child->pid);
    }
    sigprocmask(SIG_UNBLOCK, &globalMask, NULL); //unblock signals
}

void runFG(int p1) {
    fg_pid = p1;
    int status;
    while (waitpid(p1, &status, WUNTRACED | WNOHANG) == 0) {
        if (WIFSTOPPED(status)) {
            break;
        }
        usleep(50);
    }
    fg_pid = 0;
}

void eval(const char **toks, bool bg) { // bg is true iff command ended with &
    assert(toks);
    if (*toks == NULL) return;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD); // block sigchld for main mask
    // If quit
    if (strcmp(toks[0], "quit") == 0) {
        if (toks[1] != NULL) {
            const char *msg = "ERROR: quit takes no arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));
        } 
        else {
            exit(0);
        }
    }
    else if (strcmp(toks[0], "jobs") == 0) {
        if (toks[1] != NULL) {
            const char *msg = "ERROR: jobs takes no arguments\n";
            write(STDERR_FILENO, msg, strlen(msg));
        }
        else {
            for (int i = 0; i < MAXJOBS; i++) {
                Job* currJob = &jobList[i];
                if (currJob->valid) {
                    char buffer[2048];
                    snprintf(buffer, sizeof(buffer), "%s\n", toString(currJob));
                    write(STDOUT_FILENO, buffer, strlen(buffer));
                }
            }
        }
    }
    else if (strcmp(toks[0], "nuke") == 0) {
        // check no args
        // nuke all
        if (toks[1] == NULL) {
            for (int i = 0; i < 32; i++) {
                Job* toKill = &jobList[i];
                if (!toKill->valid) continue;
                kill(toKill->pid, SIGKILL);
            }
        }

        // loop thru all args
        // kill all processess in args
        else {
            // loop through all args 
            for(int i = 1; toks[i] != NULL; i++) {
                //figure out if job or pid or none
                const char* currTok = toks[i];
                bool isJob = false;
                if (currTok[0] == '%') {
                    isJob = true;
                }
                if (isJob) {
                    char* endptr;
                    int num = strtol(currTok + 1, &endptr, 10);
                    char buffer[2048];
                    if (*endptr != '\0') {
                        snprintf(buffer, sizeof(buffer), "ERROR: bad argument for nuke: %s\n", toks[i]);
                        write(STDERR_FILENO, buffer, strlen(buffer));
                    }
                    //find job
                    else {
                        Job* jobToKill = findJobByJobNum(num);
                        if (!jobToKill) {
                            snprintf(buffer, sizeof(buffer), "ERROR: no job %s\n", toks[i] + 1);
                            write(STDERR_FILENO, buffer, strlen(buffer));
                        }
                        else {
                            kill(jobToKill->pid, SIGKILL);
                        }
                    }
                }
                else {
                    char* endptr;
                    int num = strtol(currTok, &endptr, 10);
                    char buffer[2048];
                    if (*endptr != '\0') {
                        snprintf(buffer, sizeof(buffer), "ERROR: bad argument for nuke: %s\n", toks[i]);
                        write(STDERR_FILENO, buffer, strlen(buffer));
                    }
                    //find job
                    else {
                        Job* jobToKill = findJobByPID(num);
                        if (!jobToKill) {
                            snprintf(buffer, sizeof(buffer), "ERROR: no PID %s\n", toks[i]);
                            write(STDERR_FILENO, buffer, strlen(buffer));
                        }
                        else {
                            kill(jobToKill->pid, SIGKILL);
                        }
                    }
                }
            }
        }
    }
    else if (strcmp(toks[0], "fg") == 0) {
        if (!toks[1] || toks[2]) {
            char buffer[64];
            snprintf(buffer, 64, "ERROR: fg needs exactly one argument\n");
            write(STDERR_FILENO, buffer, strlen(buffer));
        }
        else {
            // check if job
            if (toks[1][0] == '%') {
                char* endptr;
                int num = strtol(toks[1] + 1, &endptr, 10);
                char buffer[MAXLINE];
                if (*endptr != '\0') {
                    snprintf(buffer, sizeof(buffer), "ERROR: bad argument for fg: %s\n", toks[1]);
                    write(STDERR_FILENO, buffer, strlen(buffer));
                }
                //find job
                else {
                    Job* jobToFG = findJobByJobNum(num);
                    if (!jobToFG) {
                        snprintf(buffer, sizeof(buffer), "ERROR: no job %s\n", toks[1] + 1);
                        write(STDERR_FILENO, buffer, strlen(buffer));
                    }
                    else if (jobToFG->status[0] != 's'){ //hack for checking for suspended
                        //bring to fg
                        runFG(jobToFG->pid);
                    }
                    else {
                        kill(jobToFG->pid, SIGCONT);
                    }
                }
            }
            else {
                char* endptr;
                int num = strtol(toks[1], &endptr, 10);
                char buffer[2048];
                if (*endptr != '\0') {
                    snprintf(buffer, sizeof(buffer), "ERROR: bad argument for fg: %s\n", toks[1]);
                    write(STDERR_FILENO, buffer, strlen(buffer));
                }
                //find job
                else {
                    Job* jobToFG = findJobByPID(num);
                    if (!jobToFG) {
                        snprintf(buffer, sizeof(buffer), "ERROR: no PID %s\n", toks[1]);
                        write(STDERR_FILENO, buffer, strlen(buffer));
                    }
                    else {
                        //bring to fg
                        runFG(jobToFG->pid);
                    }
                }
            }
        }
    }
    else if (strcmp(toks[0], "bg") == 0) {
        if (!toks[1]) {
            char buffer[MAXLINE];
            snprintf(buffer, sizeof(buffer), "ERROR: bg needs some arguments\n");
            write(STDERR_FILENO, buffer, strlen(buffer));
        }
        else {
            // loop through all args 
            for(int i = 1; toks[i] != NULL; i++) {
                //figure out if job or pid or none
                const char* currTok = toks[i];
                if (currTok[0] == '%') {
                    char* endptr;
                    int num = strtol(currTok + 1, &endptr, 10);
                    char buffer[2048];
                    if (*endptr != '\0') {
                        snprintf(buffer, sizeof(buffer), "ERROR: bad argument for bg: %s\n", toks[i]);
                        write(STDERR_FILENO, buffer, strlen(buffer));
                    }
                    //find job
                    else {
                        Job* jobToBG = findJobByJobNum(num);
                        if (!jobToBG) {
                            snprintf(buffer, sizeof(buffer), "ERROR: no job %s\n", toks[i] + 1);
                            write(STDERR_FILENO, buffer, strlen(buffer));
                        }
                        else {
                            kill(jobToBG->pid, SIGCONT);
                            strcpy(jobToBG->status, "running");
                        }
                    }
                }
                else {
                    char* endptr;
                    int num = strtol(currTok, &endptr, 10);
                    char buffer[2048];
                    if (*endptr != '\0') {
                        snprintf(buffer, sizeof(buffer), "ERROR: bad argument for bg: %s\n", toks[i]);
                        write(STDERR_FILENO, buffer, strlen(buffer));
                    }
                    //find job
                    else {
                        Job* jobToBG = findJobByPID(num);
                        if (!jobToBG) {
                            snprintf(buffer, sizeof(buffer), "ERROR: no PID %s\n", toks[i]);
                            write(STDERR_FILENO, buffer, strlen(buffer));
                        }
                        else {
                            kill(jobToBG->pid, SIGCONT);
                            strcpy(jobToBG->status, "running");
                        }
                    }
                }
            }
        }
    }
    // Everything else
    else {
        // if concurrent jobs >= 32
        if (activeJobs >= 32) {
            const char *msg = "ERROR: too many jobs\n";
            write(STDERR_FILENO, msg, strlen(msg));
            return;
        }
        sigprocmask(SIG_BLOCK, &mask, NULL); //block signals
        pid_t p1 = fork();
        if (p1 == 0) {
            sigprocmask(SIG_UNBLOCK, &mask, NULL); //unblock signals
            setpgid(0, 0);

            if (execvp(toks[0], (char * const *) toks) == -1) {
                char *msg = "ERROR: cannot run ";
                char buffer[MAXLINE + strlen(msg) + 1];
                snprintf(buffer, sizeof(buffer), "%s%s\n", msg, toks[0]);
                write(STDERR_FILENO, buffer, strlen(buffer));
            }
            else {
            }
            exit(0);
        }
        else {
            setpgid(p1, p1);
            addJob(p1, "running", toks[0]);
            sigprocmask(SIG_UNBLOCK, &mask, NULL); //unblock signals
            if (bg) {
                char buffer[2048];
                snprintf(buffer, sizeof(buffer), "[%d] (%d)  running  %s\n", jobs+1, p1, toks[0]);
                write(STDOUT_FILENO, buffer, strlen(buffer));
            }
            else { // foreground job
                runFG(p1);
            }
            jobs++;
        }
    }
}

void parse_and_eval(char *s) {
    assert(s);
    const char *toks[MAXLINE+1];

    while (*s != '\0') {
        bool end = false;
        bool bg = false;
        int t = 0;

        while (*s != '\0' && !end) {
            while (*s == '\n' || *s == '\t' || *s == ' ') ++s;
            if (*s != ';' && *s != '&' && *s != '\0') toks[t++] = s;
            while (strchr("&;\n\t ", *s) == NULL) ++s;
            switch (*s) {
            case '&':
                bg = true;
                end = true;
                break;
            case ';':
                end = true;
                break;
            }
            if (*s) *s++ = '\0';
        }
        toks[t] = NULL;
        eval(toks, bg);
    }
}

void prompt() {
    const char *prompt = "crash> ";
    ssize_t nbytes = write(STDOUT_FILENO, prompt, strlen(prompt));
}

int repl() {
    char *buf = NULL;
    size_t len = 0;
    while (prompt(), getline(&buf, &len, stdin) != -1) {
        parse_and_eval(buf);
    }

    if (buf != NULL) free(buf);
    if (ferror(stdin)) {
        perror("ERROR");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    struct sigaction sigChld, sigInt, sigQuit, sigTSTP;
    sigChld.sa_handler = sigChldHandler;
    sigInt.sa_handler = sigIntHandler;
    sigQuit.sa_handler = sigQuitHandler;
    sigTSTP.sa_handler = sigTstpHandler;

    sigInt.sa_flags = SA_RESTART;
    sigQuit.sa_flags = SA_RESTART;
    sigChld.sa_flags = SA_RESTART;
    sigTSTP.sa_flags = SA_RESTART;

    sigemptyset(&sigChld.sa_mask);
    sigemptyset(&sigInt.sa_mask);
    sigemptyset(&sigQuit.sa_mask);
    sigemptyset(&sigTSTP.sa_mask);

    sigaction(SIGCHLD, &sigChld, NULL);
    sigaction(SIGINT, &sigInt, NULL); 
    sigaction(SIGQUIT, &sigQuit, NULL);
    sigaction(SIGTSTP, &sigTSTP, NULL);

    //set up global mask
    sigemptyset(&globalMask);
    sigaddset(&globalMask, SIGINT);
    sigaddset(&globalMask, SIGCHLD);
    sigaddset(&globalMask, SIGQUIT);
    sigaddset(&globalMask, SIGTSTP);
    sigaddset(&globalMask, SIGCONT);

    for (int i = 0; i < MAXJOBS; i++) {
        jobList[i].valid = 0;
    }
    return repl();
}
