# frozen_string_literal: true

describe "Process" do
  describe "::pid" do
    it "returns an integer" do
      expect(Process.pid).must_be_kind_of Integer
    end

    it "is positive" do
      expect(Process.pid > 0).must_equal true
    end
  end

  describe "::ppid" do
    it "returns an integer" do
      expect(Process.ppid).must_be_kind_of Integer
    end
  end

  describe "::kill" do
    it "sends a signal by name" do
      r, w = IO.pipe
      pid = Process.spawn("true")
      Process.kill("TERM", pid)
      Process.waitpid(pid)
      expect($?.signaled?).must_equal true
      r.close
      w.close
    end

    it "returns the number of signals sent" do
      pid = Process.spawn("true")
      result = Process.kill("TERM", pid)
      Process.waitpid(pid)
      expect(result).must_equal 1
    end
  end

  describe "::spawn" do
    it "returns a PID" do
      pid = Process.spawn("true")
      expect(pid).must_be_kind_of Integer
      Process.waitpid(pid)
    end

    it "runs a command" do
      pid = Process.spawn("true")
      Process.waitpid(pid)
      expect($?.success?).must_equal true
    end

    it "captures exit status" do
      pid = Process.spawn("false")
      Process.waitpid(pid)
      expect($?.exitstatus).must_equal 1
    end

    it "accepts arguments" do
      pid = Process.spawn("echo", "hello")
      Process.waitpid(pid)
      expect($?.success?).must_equal true
    end

    it "redirects stdout" do
      r, w = IO.pipe
      pid = Process.spawn("echo", "hello", out: w)
      w.close
      output = r.read
      r.close
      Process.waitpid(pid)
      expect(output).must_equal "hello\n"
    end

    it "redirects stderr" do
      r, w = IO.pipe
      pid = Process.spawn("sh", "-c", "echo hello >&2", err: w)
      w.close
      output = r.read
      r.close
      Process.waitpid(pid)
      expect(output).must_equal "hello\n"
    end

    it "redirects both stdout and stderr" do
      out_r, out_w = IO.pipe
      err_r, err_w = IO.pipe
      pid = Process.spawn("sh", "-c", "echo out; echo err >&2",
                          out: out_w, err: err_w)
      out_w.close
      err_w.close
      stdout = out_r.read
      stderr = err_r.read
      out_r.close
      err_r.close
      Process.waitpid(pid)
      expect(stdout).must_equal "out\n"
      expect(stderr).must_equal "err\n"
    end

    it "raises Errno::ENOENT for a non-existent command" do
      expect do
        Process.spawn("./this-command-does-not-exist")
      end.must_raise Errno::ENOENT
    end

    it "runs a command via shell with a single string" do
      pid = Process.spawn("echo hello")
      Process.waitpid(pid)
      expect($?.success?).must_equal true
    end
  end

  describe "::waitpid" do
    it "waits for a child process" do
      pid = Process.spawn("true")
      result = Process.waitpid(pid)
      expect(result).must_equal pid
    end

    it "sets $?" do
      pid = Process.spawn("false")
      Process.waitpid(pid)
      expect($?).must_be_instance_of Process::Status
    end
  end

  describe "::Status" do
    it "has exitstatus" do
      pid = Process.spawn("false")
      Process.waitpid(pid)
      expect($?.exitstatus).must_equal 1
    end

    it "has success?" do
      pid = Process.spawn("true")
      Process.waitpid(pid)
      expect($?.success?).must_equal true
    end

    it "has pid" do
      pid = Process.spawn("true")
      Process.waitpid(pid)
      expect($?.pid).must_equal pid
    end

    it "has exited?" do
      pid = Process.spawn("true")
      Process.waitpid(pid)
      expect($?.exited?).must_equal true
    end
  end
end

describe Kernel do
  describe "#system" do
    it "returns true for a successful command" do
      expect(system("true")).must_equal true
    end

    it "returns false for a failing command" do
      expect(system("false")).must_equal false
    end
  end

  describe "#fork" do
    it "returns a PID" do
      pid = fork do
        exit!(0)
      end
      expect(pid).must_be_kind_of Integer
      Process.waitpid(pid)
      expect($?.success?).must_equal true
    end

    it "runs a block in a child" do
      r, w = IO.pipe
      pid = fork do
        w.write("hello")
        w.close
        exit!(0)
      end
      w.close
      output = r.read
      r.close
      Process.waitpid(pid)
      expect(output).must_equal "hello"
    end
  end

  describe "#exit!" do
    it "exits with a status" do
      pid = fork do
        exit!(42)
      end
      Process.waitpid(pid)
      expect($?.exitstatus).must_equal 42
    end
  end
end
