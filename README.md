## About

mruby-process provides the Process module and related Kernel methods
for mruby. It is a port of [iij/mruby-process](https://github.com/iij/mruby-process)
with additional features.

## Examples

### Spawning a command

```ruby
pid = Process.spawn("echo", "hello")
Process.waitpid(pid)
puts $?.success?  # => true
```

### Capturing output

```ruby
r, w = IO.pipe
pid = Process.spawn("echo", "hello", out: w)
w.close
puts r.read       # => "hello\n"
r.close
Process.waitpid(pid)
```

### Forking a child

```ruby
pid = fork do
  puts "in child"
  exit!(0)
end
Process.waitpid(pid)
```

### Process status

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
