# _THE_ Shell

## Overview
A fully functional Unix-style shell, closely mimicking the core behavior of `bash` and `zsh`, implemented in C for Yale's CPSC 323: Systems Programming & Computer Organization. This supports job control, process pipelines, I/O redirection, signals, environment variables, and built-in commands — all without relying on any shell libraries or wrappers.

## Features
Supports:
- Foreground and background job execution
- Arbitrary-length command pipelines (e.g., cmd1 | cmd2 | cmd3)
- Input/output/error redirection (<, >, 2>, >>, etc.)
- Custom signal forwarding and terminal state restoration
- Shell built-ins: cd, exit, export, jobs, fg, bg
- Environment variable resolution (e.g., $HOME, $PATH)
- Command substitution and globbing support (limited)

## Reflections
This pset was a huge milestone in my systems programming journey. It pushed me from writing code that works to writing code that behaves, especially under pressure from background jobs and suspended processes. Lesson: Adding boolean markers to your function's parameters is your friend, not foe.
