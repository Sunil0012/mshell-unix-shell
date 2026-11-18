# mshell desktop launcher

This launcher opens the actual C shell, named `mshell`, in a separate system terminal window. The shell runs with your normal user permissions, so commands you enter can access files and programs available to your user.

Submitted command lines are appended immediately to `~/.mshell_history` and timestamped in `~/.mshell_trace.log`. These files are plain text in your home directory.

## Launch

```sh
./run-mshell.sh
```

The script builds `mshell` with `make` if needed, then launches it using `x-terminal-emulator`. To use a different installed terminal, set `TERMINAL` when launching, for example:

```sh
TERMINAL=xterm ./run-mshell.sh
```

For an application-menu entry, copy `mshell.desktop` into `~/.local/share/applications/`. If this project folder is moved, update the absolute path in the desktop file first.
