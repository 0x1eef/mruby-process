## About

mruby-process provides the Process module and related Kernel methods
for mruby. It is a port of [iij/mruby-process](https://github.com/iij/mruby-process)
with extra features like Process.spawn.

## Quick start

#### Process.spawn

Process.spawn forks a child process and executes the given command.
It returns the PID and does not wait for the child to finish:

```ruby
pid = Process.spawn("echo", "hello")
Process.waitpid(pid)
puts $?.success?
```

#### IO.pipe

You can redirect stdout or stderr to an IO object using the out: and
err: options. Combined with IO.pipe, this lets you capture the output
of a spawned command:

```ruby
r, w = IO.pipe
pid = Process.spawn("echo", "hello", out: w)
w.close
puts r.read
r.close
Process.waitpid(pid)
```

#### Kernel.fork

Kernel#fork creates a child process and runs the given block in it.
The child can exit with Kernel#exit! to set its exit status:

```ruby
pid = fork do
  puts "in child"
  exit!(0)
end
Process.waitpid(pid)
```

#### Process::Status

Process::Status provides the exit status and predicates for a completed
child process:

```ruby
pid = Process.spawn("false")
Process.waitpid(pid)
puts $?.exitstatus
puts $?.exited?
puts $?.success?
```

## Features

**Process.pid** <br>
Returns the process ID of the current process.

**Process.ppid** <br>
Returns the process ID of the parent process.

**Process.kill** <br>
Sends a signal to a process by name or number.

**Process.fork** <br>
Creates a child process.

**Process.waitpid** <br>
Waits for a child process to exit and captures its status in `$?`.

**Process.spawn** <br>
Forks a child and executes a command with optional
redirection of stdin, stdout, and stderr. Raises
Errno::ENOENT when the command is not found.

**Process::Status** <br>
Provides the exit status and signal information for a
completed child process. Includes exitstatus, exited?,
signaled?, stopped?, stopsig, termsig, and coredump?.

**Kernel#system** <br>
Runs a command via the shell and returns true or false.

**Kernel#fork** <br>
Creates a child process that runs the given block.

**Kernel#exit** <br>
Exits the process with the given status code.

**Kernel#exit!** <br>
Exits the process immediately without calling exit
handlers.

**Kernel#sleep** <br>
Suspends the process for the given number of seconds.

**$$** <br>
The process ID of the current process.

**$?** <br>
The Process::Status of the last child process to complete.

## Integration

Add to your mruby build config:

```ruby
MRuby::Build.new("app") do |conf|
  conf.toolchain
  conf.gembox "default"
  conf.gem github: "0x1eef/mruby-process", branch: "main"
end
```

Dependencies are declared in mrbgem.rake:

| Dependency | Purpose |
|---|---|
| mruby-io | IO.pipe, IO.select |

## License

MIT
<br>
See LICENSE
