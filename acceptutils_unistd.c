#include <unistd.h>

int unistd_chdir(const char *path) { return chdir(path); }

int unistd_close(int fildes) { return close(fildes); }

int unistd_execvp(const char *file, char *const argv[]) {
  return execvp(file, argv);
}

void unistd_exit(int status) { return _exit(status); }

int unistd_fork(void) { return fork(); }

int unistd_getpid(void) { return getpid(); }

int unistd_getuid(void) { return getuid(); }

int unistd_pipe(int fildes[2]) { return pipe(fildes); }
