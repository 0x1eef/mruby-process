## About

mruby-process provides the Process module and related Kernel methods
for mruby. It is a port of [iij/mruby-process](https://github.com/iij/mruby-process)
with additional features.

## Examples

#### Process.spawn

`Process.spawn` forks a child process and executes the given command.
It returns the PID and does not wait for the child to finish.

```ruby
pid = Process.spawn("echo", "hello")
Process.waitpid(pid)
puts $?.success?  # => true
```

#### IO.pipe

You can redirect stdout or stderr to an IO object using the `out:` and
`err:` options. Combined with `IO.pipe`, this lets you capture the
output of a spawned command.

```ruby
r, w = IO.pipe
pid = Process.spawn("echo", "hello", out: w)
w.close
puts r.read       # => "hello\n"
r.close
Process.waitpid(pid)
```

#### Kernel.fork

`Kernel#fork` creates a child process and runs the given block in it.
The child can exit with `Kernel#exit!` to set its exit status.

```ruby
pid = fork do
  puts "in child"
  exit!(0)
end
Process.waitpid(pid)
```

#### Process::Status

`Process::Status` provides access to the exit status, signal information,
and predicates about a completed child process.

```ruby
pid = Process.spawn("false")
Process.waitpid(pid)
puts $?.exitstatus  # => 1
puts $?.exited?     # => true
puts $?.success?    # => false
```

## Integration

Add to your mruby build config:

```ruby
MRuby::Build.new("app") do |conf|
  conf.toolchain
  conf.gembox "default"
  conf.gem github: "0x1eef/mruby-process", branch: "main"
end
```

Dependencies are declared in [mrbgem.rake](mrbgem.rake):

| Dependency | Purpose |
|---|---|
| [mruby-io](https://github.com/iij/mruby-io) | IO.pipe, IO.select |

## License

MIT
<br>
See [LICENSE](./LICENSE)
