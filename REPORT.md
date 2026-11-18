# MShell: Design and Implementation of a Unix-Style Command Shell

**Operating Systems Project Report**  
**Student:** Sunil Naik  
**Repository:** https://github.com/Sunil0012/mshell-unix-shell  
**Platform:** Linux  
**Implementation language:** C (POSIX APIs)

## Abstract

MShell is an educational command interpreter implemented in C. It launches programs with `fork` and `execvp`, connects pipelines with `pipe` and `dup2`, redirects file descriptors, and manages foreground and background process groups. It also includes shell conveniences such as environment-variable expansion, filename globbing, command history, aliases, sourceable scripts, and a limited script control-flow language. A terminal-oriented test suite checks command behavior and interactive job control. A benchmark compares repeated external process launches with Bash.

## Goals

- Demonstrate process creation and replacement using `fork` and `execvp`.
- Demonstrate inter-process communication with pipelines.
- Implement redirection, background execution, and interactive job control.
- Provide reproducible tests and performance measurements.
- Keep the supported shell grammar explicit and testable.

## Implementation overview

The shell reads an input line, tokenizes words and operators, performs variable expansion, and parses a pipeline into commands. Each command is forked. The child joins the pipeline process group, configures its standard file descriptors, then runs a builtin or replaces itself with an external program. The parent closes unused pipe ends, records child PIDs, and either waits for the foreground process group or returns to the prompt for a background job.

Interactive startup places the shell in its own process group and gives it control of the terminal. Foreground jobs receive terminal ownership while they run. Ctrl-C and Ctrl-Z are delivered by the terminal to the foreground process group; the shell then reclaims terminal ownership. Job records track the process group and child states so stopped pipelines can be resumed with `fg` or `bg`.

## Features implemented

- External command execution through `PATH` with `fork` and `execvp`.
- Pipelines using `|`.
- Input, output, append, and standard-error redirection: `<`, `>`, `>>`, `2>`, `2>>`.
- Here-documents with `<<`.
- Background jobs using `&`; `jobs`, `fg`, and `bg` builtins.
- Signal and terminal job control, including Ctrl-C and Ctrl-Z.
- Environment assignment, `export`, `unset`, `$NAME`, `${NAME}`, and `$?`.
- Filename globbing, quoted arguments, and backslash escaping.
- Builtins including `cd`, `pwd`, `history`, aliases, and `source`.
- `;`, `&&`, and `||` command lists.
- Limited scripts with `if`/`else`/`fi`, `for`/`done`, and no-argument functions.
- Persistent command history and timestamped command traces in the user's home directory.
- Optional `--trace` output showing pipeline stages, PIDs, and process groups.

## Validation

Run the checks from the repository root:

```sh
make test
```

The test target runs three checks:

1. Smoke tests exercise conditional execution, globbing, stderr redirection, here-documents, aliases, sourcing, script control flow, and exit status.
2. Compatibility tests compare a deliberately small shared command subset against Bash.
3. A PTY integration test exercises a multi-process pipeline under Ctrl-Z, `jobs`, `bg`, `fg`, and Ctrl-C.

The compatibility test covers only the commands it invokes; it does not establish full POSIX or Bash compatibility.

## Performance study

The benchmark launches `/bin/true` 3,000 times per shell and measures elapsed wall time. It compares MShell with Bash using alternating execution order and reports medians and spread. The recorded 25-run experiment produced:

| Shell | Median elapsed | IQR | Minimum | Maximum |
|---|---:|---:|---:|---:|
| MShell | 1.6134 s | 0.0272 s | 1.5821 s | 1.7849 s |
| Bash | 1.5816 s | 0.0502 s | 1.5189 s | 1.7042 s |

The median difference (MShell minus Bash) was +0.0318 s, approximately 2% of the Bash median. The distributions overlap, and one MShell run was noticeably slower than the rest. This is a modest observed difference, not proof of a stable performance advantage. The workload primarily measures the overhead of repeatedly launching and waiting for external processes; it does not measure parsing, pipelines, or job control. Raw observations are included in [`benchmarks/results.csv`](benchmarks/results.csv).

Reproduce or vary the experiment with:

```sh
sh benchmarks/compare.sh --commands 3000 --runs 25 --csv results.csv
```

For a rigorous comparison, record the machine model, kernel, compiler flags, and system load, and repeat the experiment under comparable conditions.

## Limitations

MShell is a course project, not a complete Bash or POSIX shell. Its grammar is intentionally limited: script blocks cannot nest, functions do not accept positional parameters, and aliases expand a command name to one word. Command substitution and many standard shell expansions and constructs are absent. Benchmark results depend on the host and background load. The shell should be tested on the target Linux distribution before a live demonstration.

## Conclusion

MShell demonstrates core operating-systems mechanisms through a runnable shell: process creation, file-descriptor management, IPC, signals, process groups, and terminal job control. Automated tests and the benchmark provide evidence for its behavior and performance while clearly bounding the supported grammar.
