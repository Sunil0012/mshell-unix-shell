# Mini Unix Shell

A small interactive shell written in C for Linux/POSIX systems. It demonstrates process creation, process groups, pipes, file descriptor redirection, signals, and terminal job control.

## Build and run

```sh
make
./mshell
# or run a command script
./mshell examples.sh
```

Use `make clean` to remove the compiled executable.

## Features

- Run programs with `fork` and `execvp`; search executable names through `PATH`.
- Pipelines: `command1 | command2 | command3`.
- Input/output redirection: `<`, `>`, `>>`, `2>`, and `2>>`.
- Here-documents: `cat << END` followed by body text and a delimiter line.
- Filename globbing such as `*.c` and `data?.txt`.
- Background jobs with `&` and a `jobs` listing.
- Interactive job control: Ctrl-C stops a foreground process group; Ctrl-Z suspends it; `fg` and `bg` resume jobs.
- Builtins: `cd [dir|-]`, `pwd`, `export`, `unset`, `jobs`, `fg [%job]`, `bg [%job]`, `history`, `help`, and `exit`.
- Aliases with `alias name=value`, listing with `alias`, removal with `unalias name`, and sourcing with `source file` or `. file`.
- Environment variables: `NAME=value`, `export NAME=value`, and `$NAME` / `${NAME}` expansion.
- Command lists with `;`, `&&`, and `||`; conditional operators short-circuit based on the previous foreground command's exit status.
- Persistent command history in `~/.mshell_history`, viewed with `history`; each submitted line is saved immediately.
- Timestamped command trace in `~/.mshell_trace.log` using local time and ISO-style timestamps.
- `$?` expands to the previous foreground command's exit status.
- Script input using `./mshell path/to/script`.
- Script control flow in sourced/script files: `if`/`else`/`fi`, `for NAME in values; do`/`done`, and no-argument `name() { ... }` functions.
- Process launch trace with `./mshell --trace [script]`; prints fork stages, PIDs, and process groups.
- Single and double quoted arguments, plus backslash escaping.

## Examples

```text
mshell$ pwd
mshell$ cat < input.txt | grep hello > matches.txt
mshell$ sleep 30 &
[1] 12345
mshell$ jobs
mshell$ fg %1
```

`cd`, `export`, `unset`, and variable assignments are handled in the shell process when used alone. A builtin used in a pipeline runs in that pipeline's child process. This is an educational Unix-style shell, not a fully compatible implementation of Bash, zsh, or every historical Unix shell. It does not implement command substitution or the full shell grammar. Aliases currently expand a command name to one word.

`make test` runs smoke checks, a Bash compatibility comparison, and a PTY job-control integration check for Ctrl-Z, `jobs`, `bg`, `fg`, and Ctrl-C across a pipeline. `sh benchmarks/compare.sh` runs an alternating, warmed-up comparison against Bash (defaults: 1000 external launches and 15 measured runs per shell), reports median/IQR/min/max and the median paired difference, and writes raw samples plus paired deltas to CSV. Configure it with `sh benchmarks/compare.sh --commands 3000 --runs 25 --csv results.csv`; pass `--csv -` to disable CSV output. Sourced files execute in the current shell and support the documented control constructs. The script grammar is intentionally limited: blocks cannot nest and functions take no positional parameters.

Run `./run-mshell.sh` and enter `source demo.ms` for the interactive project demonstration. The demo ends with a foreground `sleep`; press Ctrl-C to continue it, or Ctrl-Z and use `bg`/`fg` to demonstrate job control.

## Project concepts

- `fork` creates a process for each external command; `execvp` replaces that child with the requested program.
- `pipe` and `dup2` connect adjacent commands in a pipeline.
- Redirection opens files and replaces the command's standard input or output.
- Each pipeline has a process group. The shell transfers the terminal to foreground groups and regains it after completion or suspension.
- Signal dispositions are ignored by the interactive shell where needed and restored to defaults in child processes.
- Background children are reaped and tracked so completed jobs do not remain zombies.
