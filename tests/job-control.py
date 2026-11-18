#!/usr/bin/env python3
"""PTY integration check for Ctrl-Z, jobs, bg/fg, and Ctrl-C."""
import os, pty, select, signal, sys, tempfile, time

shell=os.path.abspath(os.path.join(os.path.dirname(__file__),"..","mshell"))
with tempfile.TemporaryDirectory() as home:
    pid,fd=pty.fork()
    if pid==0:
        os.environ["HOME"]=home
        os.execv(shell,[shell])
    def read_until(needle,timeout=4):
        data=b""; end=time.time()+timeout
        while time.time()<end and needle not in data:
            ready,_,_=select.select([fd],[],[],.1)
            if ready:
                try:data+=os.read(fd,4096)
                except OSError:break
        if needle not in data: raise RuntimeError(f"timeout waiting for {needle!r}; got {data[-500:]!r}")
        return data
    try:
        read_until(b"$ ")
        os.write(fd,b"sleep 15 | cat\n");time.sleep(.3);os.write(fd,b"\x1a")
        stopped=read_until(b"Stopped",5)
        os.write(fd,b"jobs\n"); listed=read_until(b"$ ")
        if b"Stopped" not in stopped+listed: raise RuntimeError("job was not reported stopped")
        os.write(fd,b"bg %1\n");read_until(b"$ ")
        os.write(fd,b"fg %1\n");time.sleep(.3);os.write(fd,b"\x03")
        read_until(b"$ ",5)
        os.write(fd,b"exit\n");os.waitpid(pid,0)
        print("PTY job-control checks passed")
    except Exception:
        try:os.kill(pid,signal.SIGKILL)
        except ProcessLookupError:pass
        os.waitpid(pid,0)
        raise
