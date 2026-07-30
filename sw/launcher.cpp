#include <gpu.h>

int frontend_main(int argc, char **argv);

int main(int argc, char **argv) {
  int app_argc = 0;
  char **app_argv = nullptr;
  if (memu_runtime_init(argc, argv, &app_argc, &app_argv) != 0)
    return 2;
  int result = frontend_main(app_argc, app_argv);
  memu_runtime_fini();
  return result;
}
