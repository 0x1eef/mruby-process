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
#include <sys/stat.h>
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
 * Checks if an executable exists at the given path.
 * Returns 1 if executable, 0 if not, -1 on error.
 */
static int
executable_exists(const char *path)
{
  struct stat st;
  if (stat(path, &st) == -1) {
    return 0;
  }
  return (st.st_mode & S_IXUSR) ? 1 : 0;
}

/*
 * Searches PATH for an executable named `cmd`.
 * Returns a malloc'd string with the full path, or NULL.
 * Caller must free the returned string.
 */
static char *
search_path(const char *cmd)
{
  const char *path_env;
  char *path_copy, *dir, *full_path;
  size_t cmd_len, dir_len;

  path_env = getenv("PATH");
  if (!path_env) return NULL;

  path_copy = strdup(path_env);
  if (!path_copy) return NULL;

  cmd_len = strlen(cmd);
  dir = strtok(path_copy, ":");
  while (dir) {
    dir_len = strlen(dir);
    full_path = (char *)malloc(dir_len + 1 + cmd_len + 1);
    if (!full_path) { free(path_copy); return NULL; }
    memcpy(full_path, dir, dir_len);
    full_path[dir_len] = '/';
    memcpy(full_path + dir_len + 1, cmd, cmd_len + 1);
    if (executable_exists(full_path)) {
      free(path_copy);
      return full_path;
    }
    free(full_path);
    dir = strtok(NULL, ":");
  }
  free(path_copy);
  return NULL;
}

/*
 * Checks whether the given command name can be executed.
 * Raises Errno::ENOENT if not found.
 */
static void
check_command(mrb_state *mrb, const char *cmd)
{
  if (strchr(cmd, '/')) {
    if (!executable_exists(cmd)) {
      errno = ENOENT;
      mrb_sys_fail(mrb, cmd);
    }
  } else {
    char *found = search_path(cmd);
    if (!found) {
      errno = ENOENT;
      mrb_sys_fail(mrb, cmd);
    }
    free(found);
  }
}

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
    check_command(mrb, mrb_string_value_cstr(mrb, &prog));
  } else {
    prog = argv[0];
    prog0 = argv[0];
    /* If there's only one argument + maybe opts, use shell */
    if (argc == 1) {
      const char *cmd = mrb_string_value_cstr(mrb, &argv[0]);

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
    check_command(mrb, mrb_string_value_cstr(mrb, &prog));
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
      _exit(errno == ENOENT ? 127 : 126);
    }
    if (pid < 0) {
      mrb_sys_fail(mrb, "fork failed");
    }
    return mrb_fixnum_value(pid);
  }
}
