# frozen_string_literal: true

describe "Process" do
  context "as a module" do
    it "is exposed as Module" do
      expect(Process.class).must_equal Module
    end
  end

  context "for process identifiers" do
    context "::pid" do
      let(:pid) { Process.pid }

      it "returns an integer" do
        expect(pid).must_be_kind_of Integer
      end

      it "is positive" do
        expect(pid > 0).must_equal true
      end
    end

    context "::ppid" do
      let(:ppid) { Process.ppid }

      it "returns an integer" do
        expect(ppid).must_be_kind_of Integer
      end

      it "is positive" do
        expect(ppid > 0).must_equal true
      end
    end

    context "::uid" do
      let(:uid) { Process.uid }

      it "returns an integer" do
        expect(uid).must_be_kind_of Integer
      end

      it "is non-negative" do
        expect(uid >= 0).must_equal true
      end
    end

    context "::euid" do
      let(:euid) { Process.euid }

      it "returns an integer" do
        expect(euid).must_be_kind_of Integer
      end

      it "is non-negative" do
        expect(euid >= 0).must_equal true
      end
    end
  end

  context "::kill" do
    it "accepts signal 0 for the current process" do
      expect(Process.kill(0, Process.pid)).must_equal 1
    end

    context "when signaling a spawned child" do
      let(:pid) { Process.spawn("true") }

      it "sends a signal by name" do
        Process.kill("TERM", pid)
        Process.waitpid(pid)
        expect($?.signaled?).must_equal true
      end

      it "returns the number of signals sent" do
        result = Process.kill("TERM", pid)
        Process.waitpid(pid)
        expect(result).must_equal 1
      end
    end
  end

  context "::spawn" do
    context "when running simple commands" do
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

    context "when redirecting streams" do
      let(:stdout_pipe) { IO.pipe }
      let(:stderr_pipe) { IO.pipe }

      it "redirects stdout" do
        r, w = stdout_pipe
        pid = Process.spawn("echo", "hello", out: w)
        w.close
        output = r.read
        r.close
        Process.waitpid(pid)
        expect(output).must_equal "hello\n"
      end

      it "redirects stderr" do
        r, w = stdout_pipe
        pid = Process.spawn("sh", "-c", "echo hello >&2", err: w)
        w.close
        output = r.read
        r.close
        Process.waitpid(pid)
        expect(output).must_equal "hello\n"
      end

      it "redirects both stdout and stderr" do
        out_r, out_w = stdout_pipe
        err_r, err_w = stderr_pipe
        pid = Process.spawn("sh", "-c", "echo out; echo err >&2", out: out_w, err: err_w)
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
    end
  end

  context "::waitpid" do
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

  context "::waitpid2" do
    context "with WNOHANG" do
      let(:pid) do
        fork do
          loop {}
        end
      end

      it "returns nil values before the child exits" do
        result_pid, status = waitpid2_with_wnohang(pid)
        expect(result_pid).must_be_nil
        expect(status).must_be_nil
        Process.kill(:TERM, pid)
        loop do
          result_pid, status = waitpid2_with_wnohang(pid)
          next unless result_pid
          expect(result_pid).must_equal pid
          expect(status.signaled?).must_equal true
          break
        end
      end
    end
  end

  context "::Status" do
    context "for equivalent exit values" do
      it "compares equal" do
        pid1 = process_status_fork(123)
        pid2 = process_status_fork(123)
        _, status1 = Process.waitpid2(pid1)
        _, status2 = Process.waitpid2(pid2)
        expect(status1 == status2).must_equal true
      end
    end

    context "for exited children" do
      let(:pid) { process_status_fork(exitcode) }
      let(:exitcode) { 42 }
      let(:status) do
        _, child_status = Process.waitpid2(pid)
        child_status
      end

      it "has exitstatus" do
        expect(status.exitstatus).must_equal 42
      end

      it "has pid" do
        expect(status.pid).must_equal pid
      end

      it "reports exited?" do
        expect(status.exited?).must_equal true
      end

      it "converts to integers and strings" do
        expect(status.to_i).must_be_kind_of Integer
        expect(status.to_int).must_be_kind_of Integer
        expect(status.to_s).must_equal status.to_i.to_s
      end
    end

    context "for successful children" do
      let(:pid) { Process.spawn("true") }

      before do
        Process.waitpid(pid)
      end

      it "reports success?" do
        expect($?.success?).must_equal true
      end
    end

    context "for signaled children" do
      let(:pid) do
        fork do
          sleep 10
        end
      end
      let(:status) do
        Process.kill(15, pid)
        _, child_status = Process.waitpid2(pid)
        child_status
      end

      it "reports signaled?" do
        expect(status.signaled?).must_equal true
      end

      it "has termsig" do
        expect(status.termsig).must_equal 15
      end
    end
  end
end

describe "Kernel" do
  context "$$" do
    it "matches Process.pid" do
      expect($$).must_equal Process.pid
    end
  end

  context "#system" do
    it "returns true for a successful command" do
      expect(system("true")).must_equal true
    end

    it "returns false for a failing command" do
      expect(system("false")).must_equal false
    end
  end

  context "#fork" do
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

  context "#exit!" do
    it "exits with a status" do
      pid = fork do
        exit!(42)
      end
      Process.waitpid(pid)
      expect($?.exitstatus).must_equal 42
    end
  end
end

def process_status_fork(exitcode = 0)
  pid = fork
  exit!(exitcode) if !pid
  pid
end

def waitpid2_with_wnohang(pid)
  Process.waitpid2(pid, Process::WNOHANG)
end

Minitest.run(ARGV) || exit(1)
