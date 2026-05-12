/*
** process.c -
*/

#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/string.h"
#include "mruby/variable.h"
#include "mruby/error.h"
#include "mruby/hash.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static mrb_value mrb_f_exit_common(mrb_state *mrb, int bang);
static mrb_value mrb_procstat_new(mrb_state *mrb, mrb_int pid, mrb_int status);

static struct {
  const char *name;
  int no;
} signals[] = {
#include "signals.cstub"
  { NULL, 0 }
};

#if MRUBY_RELEASE_NO < 10000
static struct RClass *
mrb_module_get(mrb_state *mrb, const char *name)
{
  return mrb_class_get(mrb, name);
}
#endif

/*
 * Redirects fd `src` to `dest` using dup2.
 * Closes src after redirecting.
 */
static void
redirect_fd(mrb_state *mrb, int src, int dest)
{
  if (src != dest) {
    if (dup2(src, dest) == -1) {
      mrb_sys_fail(mrb, "dup2 failed");
    }
    close(src);
  }
}

/*
 * Extracts a file descriptor from a Ruby IO object or Fixnum.
 * Returns -1 on failure.
 */
static int
fd_from_value(mrb_state *mrb, mrb_value val)
{
  if (mrb_fixnum_p(val)) {
    return mrb_fixnum(val);
  }
  if (mrb_respond_to(mrb, val, mrb_intern_lit(mrb, "to_i"))) {
    val = mrb_funcall(mrb, val, "to_i", 0);
    if (mrb_fixnum_p(val)) {
      return mrb_fixnum(val);
    }
  }
  return -1;
}

/*
 * Process.spawn([cmd, arg0], arg1, ...) or
 * Process.spawn(cmd, arg1, ..., opts)
 * Process.spawn(env, cmd, arg1, ..., opts)
 *
 * Returns the PID of the child process.
 */
static mrb_value
mrb_f_spawn(mrb_state *mrb, mrb_value klass)
{
  mrb_value *argv;
  mrb_int argc;
  mrb_int i;
  mrb_value prog = mrb_nil_value();
  mrb_value prog0 = mrb_nil_value();
  mrb_value opts = mrb_nil_value();
  int start = 0;
  pid_t pid;
  int out_fd = -1;
  int err_fd = -1;
  int in_fd = -1;

  mrb_get_args(mrb, "*", &argv, &argc);
  if (argc == 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments (0 for 1+)");
  }

  /* Check for options hash at the end */
  if (argc > 0 && mrb_hash_p(argv[argc - 1])) {
    opts = argv[argc - 1];
    argc--;
  }

  /* Parse options */
  if (!mrb_nil_p(opts)) {
    mrb_value out_val, err_val, in_val;

    out_val = mrb_hash_get(mrb, opts, mrb_symbol_value(mrb_intern_lit(mrb, "out")));
    if (!mrb_nil_p(out_val)) {
      out_fd = fd_from_value(mrb, out_val);
      if (out_fd < 0) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "out option must respond to #to_i");
      }
    }

    err_val = mrb_hash_get(mrb, opts, mrb_symbol_value(mrb_intern_lit(mrb, "err")));
    if (!mrb_nil_p(err_val)) {
      err_fd = fd_from_value(mrb, err_val);
      if (err_fd < 0) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "err option must respond to #to_i");
      }
    }

    in_val = mrb_hash_get(mrb, opts, mrb_symbol_value(mrb_intern_lit(mrb, "in")));
    if (!mrb_nil_p(in_val)) {
      in_fd = fd_from_value(mrb, in_val);
      if (in_fd < 0) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "in option must respond to #to_i");
      }
    }
  }

  /* First argument could be [cmd, arg0] or just cmd */
  if (mrb_array_p(argv[0])) {
    mrb_value *arr;
    mrb_int arr_len;
    arr = RARRAY_PTR(argv[0]);
    arr_len = RARRAY_LEN(argv[0]);
    if (arr_len < 1) {
      mrb_raise(mrb, E_ARGUMENT_ERROR, "first element of array must be a command");
    }
    prog = arr[0];
    if (arr_len > 1) {
      prog0 = arr[1];
    } else {
      prog0 = prog;
    }
    start = 1;
  } else {
    prog = argv[0];
    prog0 = argv[0];
    /* If there's only one argument + maybe opts, use shell */
    if (argc == 1) {
      const char *cmd = mrb_string_value_cstr(mrb, &argv[0]);
      char buf[4];
      snprintf(buf, sizeof(buf), "%d", STDERR_FILENO + 1);

      fflush(stdout);
      fflush(stderr);

      pid = fork();
      if (pid == 0) {
        if (out_fd >= 0) redirect_fd(mrb, out_fd, STDOUT_FILENO);
        if (err_fd >= 0) redirect_fd(mrb, err_fd, STDERR_FILENO);
        if (in_fd >= 0) redirect_fd(mrb, in_fd, STDIN_FILENO);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
      }
      if (pid < 0) {
        mrb_sys_fail(mrb, "fork failed");
      }
      return mrb_fixnum_value(pid);
    }
    start = 1;
  }

  /* Build argv array for execvp */
  {
    const char **cargv;
    cargv = (const char **)mrb_alloca(mrb, sizeof(char *) * (argc - start + 2));
    cargv[0] = mrb_string_value_cstr(mrb, &prog0);
    for (i = start; i < argc; i++) {
      cargv[i - start + 1] = mrb_string_value_cstr(mrb, &argv[i]);
    }
    cargv[argc - start + 1] = NULL;

    fflush(stdout);
    fflush(stderr);

    pid = fork();
    if (pid == 0) {
      if (out_fd >= 0) redirect_fd(mrb, out_fd, STDOUT_FILENO);
      if (err_fd >= 0) redirect_fd(mrb, err_fd, STDERR_FILENO);
      if (in_fd >= 0) redirect_fd(mrb, in_fd, STDIN_FILENO);
      execvp(mrb_string_value_cstr(mrb, &prog), cargv);
      /* If we get here, exec failed. Check for ENOENT. */
      _exit(errno == ENOENT ? 127 : 126);
    }
    if (pid < 0) {
      mrb_sys_fail(mrb, "fork failed");
    }
    return mrb_fixnum_value(pid);
  }
}

mrb_value
mrb_f_kill(mrb_state *mrb, mrb_value klass)
{
  mrb_int pid, argc;
  mrb_value *argv, sigo;
  int i, sent, signo = 0;
  size_t symlen;
  const char *name;
#if MRUBY_RELEASE_NO < 10000
  size_t namelen;
#else
  mrb_int namelen;
#endif

  mrb_get_args(mrb, "oi*", &sigo, &pid, &argv, &argc);
  if (mrb_fixnum_p(sigo)) {
    signo = mrb_fixnum(sigo);
  } else if (mrb_string_p(sigo) || mrb_symbol_p(sigo)) {
    if (mrb_string_p(sigo)) {
      name = RSTRING_PTR(sigo);
      namelen = (size_t)RSTRING_LEN(sigo);
    } else {
      name = mrb_sym2name_len(mrb, mrb_symbol(sigo), &namelen);
    }
    if (namelen >= 3 && strncmp(name, "SIG", 3) == 0) {
      name += 3;
      namelen -= 3;
    }
    for (i = 0; signals[i].name != NULL; i++) {
      symlen = strlen(signals[i].name);
      if (symlen == namelen && strncmp(name, signals[i].name, symlen) == 0) {
        signo = signals[i].no;
        break;
      }
    }
      if (signals[i].name == NULL) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "unsupported name `SIG%S'", mrb_str_new(mrb, name, namelen));
    }
  } else {
    mrb_raisef(mrb, E_TYPE_ERROR, "bad signal type %S",
               mrb_obj_value(mrb_class(mrb, sigo)));
  }

  sent = 0;
  if (kill(pid, signo) == -1)
    mrb_sys_fail(mrb, "kill");
  sent++;

  while (argc-- > 0) {
    if (!mrb_fixnum_p(*argv)) {
      mrb_raisef(mrb, E_TYPE_ERROR, "wrong argument type %S (expected Fixnum)",
                 mrb_obj_value(mrb_class(mrb, *argv)));
    }
    if (kill(mrb_fixnum(*argv), signo) == -1)
      mrb_sys_fail(mrb, "kill");
    sent++;
    argv++;
  }
  return mrb_fixnum_value(sent);
}

static mrb_value
mrb_f_fork(mrb_state *mrb, mrb_value klass)
{
  mrb_value b;
  int pid;

  mrb_get_args(mrb, "&", &b);

  switch (pid = fork()) {
  case 0:
    mrb_gv_set(mrb, mrb_intern_lit(mrb, "$$"), mrb_fixnum_value((mrb_int)getpid()));
    if (!mrb_nil_p(b)) {
      mrb_yield_argv(mrb, b, 0, NULL);
      _exit(0);
    }
    return mrb_nil_value();

  case -1:
    mrb_sys_fail(mrb, "fork failed");
    return mrb_nil_value();

  default:
    return mrb_fixnum_value(pid);
  }
}

static int
mrb_waitpid(int pid, int flags, int *st)
{
  int result;

retry:
  result = waitpid(pid, st, flags);
  if (result < 0) {
    if (errno == EINTR) {
      goto retry;
    }
    return -1;
  }

  return result;
}

static mrb_value
mrb_f_waitpid(mrb_state *mrb, mrb_value klass)
{
  mrb_int pid, flags = 0;
  int status;

  mrb_get_args(mrb, "i|i", &pid, &flags);

  if ((pid = mrb_waitpid(pid, flags, &status)) < 0)
    mrb_sys_fail(mrb, "waitpid failed");

  if (!pid && (flags & WNOHANG)) {
    mrb_gv_set(mrb, mrb_intern_lit(mrb, "$?"), mrb_nil_value());
    return mrb_nil_value();
  }

  mrb_gv_set(mrb, mrb_intern_lit(mrb, "$?"), mrb_procstat_new(mrb, pid, status));
  return mrb_fixnum_value(pid);
}

mrb_value
mrb_f_sleep(mrb_state *mrb, mrb_value klass)
{
  mrb_int argc;
  mrb_value *argv;
  time_t beg, end;

  beg = time(0);
  mrb_get_args(mrb, "*", &argv, &argc);
  if (argc == 0) {
    sleep((32767<<16)+32767);
  } else if(argc == 1) {
    struct timeval tv;
    int n;

    if (mrb_fixnum_p(argv[0])) {
      tv.tv_sec = mrb_fixnum(argv[0]);
      tv.tv_usec = 0;
    } else {
      tv.tv_sec = mrb_float(argv[0]);
      tv.tv_usec = (mrb_float(argv[0]) - tv.tv_sec) * 1000000.0;
    }


    n = select(0, 0, 0, 0, &tv);
    if (n < 0)
      mrb_sys_fail(mrb, "mrb_f_sleep failed");
  } else {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong # of arguments");
  }

  end = time(0) - beg;

  return mrb_fixnum_value(end);
}

mrb_value
mrb_f_system(mrb_state *mrb, mrb_value klass)
{
  int ret;
  mrb_value *argv, pname;
  const char *path;
  mrb_int argc;
  void (*chfunc)(int);

  fflush(stdout);
  fflush(stderr);

  mrb_get_args(mrb, "*", &argv, &argc);
  if (argc == 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments");
  }

  pname = argv[0];
  path = mrb_string_value_cstr(mrb, &pname);

  chfunc = signal(SIGCHLD, SIG_DFL);
  ret = system(path);
  signal(SIGCHLD, chfunc);

  if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
    return mrb_true_value();
  }

  return mrb_false_value();
}

mrb_value
mrb_f_exit(mrb_state *mrb, mrb_value klass)
{
  return mrb_f_exit_common(mrb, 0);
}

mrb_value
mrb_f_exit_bang(mrb_state *mrb, mrb_value klass)
{
  return mrb_f_exit_common(mrb, 1);
}

static mrb_value
mrb_f_exit_common(mrb_state *mrb, int bang)
{
  mrb_value status;
  int istatus, n;

  n = mrb_get_args(mrb, "|o", &status);
  if (n == 0) {
    status = (bang) ? mrb_false_value() : mrb_true_value();
  }

  if (mrb_type(status) == MRB_TT_TRUE) {
    istatus = EXIT_SUCCESS;
  } else if (mrb_type(status) == MRB_TT_FALSE) {
    istatus = EXIT_FAILURE;
  } else {
    status = mrb_convert_type(mrb, status, MRB_TT_FIXNUM, "Integer", "to_int");
    istatus = mrb_fixnum(status);
  }

  if (bang) {
    _exit(istatus);
  } else {
    exit(istatus);
  }
}

mrb_value
mrb_f_pid(mrb_state *mrb, mrb_value klass)
{
  return mrb_fixnum_value((mrb_int)getpid());
}

mrb_value
mrb_f_ppid(mrb_state *mrb, mrb_value klass)
{
  return mrb_fixnum_value((mrb_int)getppid());
}

static mrb_value
mrb_procstat_new(mrb_state *mrb, mrb_int pid, mrb_int status)
{
  struct RClass *cls;
  cls = mrb_class_get_under(mrb, mrb_module_get(mrb, "Process"), "Status");
  return mrb_funcall(mrb, mrb_obj_value(cls), "new", 2, mrb_fixnum_value(pid), mrb_fixnum_value(status));
}

static mrb_value
mrb_procstat_coredump(mrb_state *mrb, mrb_value self)
{
#ifdef WCOREDUMP
  int i = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@status")));
  return mrb_bool_value(WCOREDUMP(i));
#else
  return mrb_false_value();
#endif
}

static mrb_value
mrb_procstat_exitstatus(mrb_state *mrb, mrb_value self)
{
  int i = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@status")));
  if (WIFEXITED(i)) {
    return mrb_fixnum_value(WEXITSTATUS(i));
  } else {
    return mrb_nil_value();
  }
}

static mrb_value
mrb_procstat_exited(mrb_state *mrb, mrb_value self)
{
  int i = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@status")));
  return mrb_bool_value(WIFEXITED(i));
}

static mrb_value
mrb_procstat_signaled(mrb_state *mrb, mrb_value self)
{
  int i = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@status")));
  return mrb_bool_value(WIFSIGNALED(i));
}

static mrb_value
mrb_procstat_stopped(mrb_state *mrb, mrb_value self)
{
  int i = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@status")));
  return mrb_bool_value(WIFSTOPPED(i));
}

static mrb_value
mrb_procstat_stopsig(mrb_state *mrb, mrb_value self)
{
  int i = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@status")));
  if (WIFSTOPPED(i)) {
    return mrb_fixnum_value(WSTOPSIG(i));
  } else {
    return mrb_nil_value();
  }
}

static mrb_value
mrb_procstat_termsig(mrb_state *mrb, mrb_value self)
{
  int i = mrb_fixnum(mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@status")));
  if (WIFSIGNALED(i)) {
    return mrb_fixnum_value(WTERMSIG(i));
  } else {
    return mrb_nil_value();
  }
}

void
mrb_mruby_process_gem_init(mrb_state *mrb)
{
  struct RClass *p, *s;

  mrb_define_method(mrb, mrb->kernel_module, "exit",   mrb_f_exit,   MRB_ARGS_OPT(1));
  mrb_define_method(mrb, mrb->kernel_module, "exit!", mrb_f_exit_bang, MRB_ARGS_OPT(1));
  mrb_define_method(mrb, mrb->kernel_module, "fork",   mrb_f_fork,   MRB_ARGS_NONE());
  mrb_define_method(mrb, mrb->kernel_module, "sleep",  mrb_f_sleep,  MRB_ARGS_ANY());
  mrb_define_method(mrb, mrb->kernel_module, "system", mrb_f_system, MRB_ARGS_ANY());

  p = mrb_define_module(mrb, "Process");
  mrb_define_class_method(mrb, p, "kill",    mrb_f_kill,    MRB_ARGS_ANY());
  mrb_define_class_method(mrb, p, "fork",    mrb_f_fork,    MRB_ARGS_NONE());
  mrb_define_class_method(mrb, p, "waitpid", mrb_f_waitpid, MRB_ARGS_ANY());
  mrb_define_class_method(mrb, p, "pid",     mrb_f_pid,     MRB_ARGS_NONE());
  mrb_define_class_method(mrb, p, "ppid",    mrb_f_ppid,    MRB_ARGS_NONE());
  mrb_define_class_method(mrb, p, "spawn",   mrb_f_spawn,   MRB_ARGS_ANY());

  s = mrb_define_class_under(mrb, p, "Status", mrb->object_class);
  mrb_define_method(mrb, s, "coredump?", mrb_procstat_coredump, MRB_ARGS_NONE());
  mrb_define_method(mrb, s, "exited?", mrb_procstat_exited, MRB_ARGS_NONE());
  mrb_define_method(mrb, s, "exitstatus", mrb_procstat_exitstatus, MRB_ARGS_NONE());
  mrb_define_method(mrb, s, "signaled?", mrb_procstat_signaled, MRB_ARGS_NONE());
  mrb_define_method(mrb, s, "stopped?", mrb_procstat_stopped, MRB_ARGS_NONE());
  mrb_define_method(mrb, s, "stopsig", mrb_procstat_stopsig, MRB_ARGS_NONE());
  mrb_define_method(mrb, s, "termsig", mrb_procstat_termsig, MRB_ARGS_NONE());

  mrb_define_const(mrb, p, "WNOHANG", mrb_fixnum_value(WNOHANG));
  mrb_define_const(mrb, p, "WUNTRACED", mrb_fixnum_value(WUNTRACED));

  mrb_gv_set(mrb, mrb_intern_lit(mrb, "$$"), mrb_fixnum_value((mrb_int)getpid()));
  mrb_gv_set(mrb, mrb_intern_lit(mrb, "$?"), mrb_nil_value());
}

void
mrb_mruby_process_gem_final(mrb_state *mrb)
{
}
