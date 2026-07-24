#pragma once

#define SIGWINCH 28

typedef void (*sighandler_t)(int);

#define SIG_ERR  ((sighandler_t)-1)
#define SIG_DFL  ((sighandler_t)0)
#define SIG_IGN  ((sighandler_t)1)

sighandler_t signal(int signum, sighandler_t handler);
