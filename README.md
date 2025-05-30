# crash: A Minimal Shell written in C
This project implements a simplified UNIX shell called `crash`, written in C. It supports interactive job control, command execution, process management, and signal forwarding. The shell mimics basic behavior of `bash`.  
To run, just run `crash` and type in commands as one would in a normal shell.

## Overview
The shell supports launching and managing user-defined processes in both foreground and background modes. It also supports shell-level job tracking, basic shell commands, and forwarding of key signals such as `SIGINT`, `SIGTSTP`, and `SIGCONT`.

This project takes into account handling process state transitions, concurrency, and signal safety.

## Features
### Core Shell Functionality
- Tokenized parsing of commands with `&`, `;`, and `\n` as separators
- Launching background and foreground processes with argument support
- Up to 32 concurrent jobs
- Handles Ctrl+C, Ctrl+Z, and Ctrl+\ keyboard signals

### Built-In Shell Commands
- `quit` — exit the shell
- `jobs` — list active or suspended jobs
- `nuke [PID|%JID ...]` — kill specific jobs or processes
- `fg [PID|%JID]` — resume a job in the foreground
- `bg [PID|%JID ...]` — resume one or more jobs in the background

### Job Management
- Sequential job IDs starting from 1
- Internal job table to track job state (`running`, `suspended`, `finished`, etc.)
- Foreground job blocking behavior (shell input paused)
- Background jobs run concurrently and do not block shell input

### Signal Handling
- SIGCHLD used to detect job termination or suspension
- SIGINT (Ctrl+C) and SIGTSTP (Ctrl+Z) forwarded to foreground processes
- SIGCONT used to resume suspended jobs
- Safe signal handling using `sigaction`, signal masks, and signal-safe functions

## Technical Highlights
- Process management mimics LINUX systems using `fork`, `execvp`, `kill`, and `waitpid`
- Low-level signal handling (`sigaction`, `SA_RESTART`, `sigemptyset`)
- Fully self-contained implementation in `crash.c`
