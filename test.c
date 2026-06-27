#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define RC_OK 0
#define RC_ERR_FILE_READ_FOPEN 16
#define RC_ERR_PARSE_ERROR_EMPTY_COMMAND 20
#define RC_ERR_PARSE_ERROR_EMPTY_MENU 21

static int failures = 0;

static void fail(const char *name, const char *message) {
  fprintf(stderr, "FAIL %s: %s\n", name, message);
  failures++;
}

static int write_all(int fd, const char *content) {
  size_t size = strlen(content);
  size_t written = 0;

  while (written < size) {
    ssize_t n = write(fd, content + written, size - written);
    if (n < 0)
      return -1;
    written += (size_t)n;
  }

  return 0;
}

static int make_temp_file(char *path, size_t path_size, const char *content) {
  int fd;

  if (snprintf(path, path_size, "/tmp/picknrun-test-XXXXXX") >= (int)path_size)
    return -1;

  fd = mkstemp(path);
  if (fd < 0)
    return -1;

  if (write_all(fd, content) != 0) {
    close(fd);
    unlink(path);
    return -1;
  }

  if (close(fd) != 0) {
    unlink(path);
    return -1;
  }

  return 0;
}

static int make_temp_path(char *path, size_t path_size) {
  int fd;

  if (snprintf(path, path_size, "/tmp/picknrun-test-XXXXXX") >= (int)path_size)
    return -1;

  fd = mkstemp(path);
  if (fd < 0)
    return -1;

  close(fd);
  unlink(path);
  return 0;
}

static int decode_status(int status) {
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return 255;
}

static int run_shell(const char *command) {
  int status = system(command);
  if (status == -1)
    return 255;
  return decode_status(status);
}

static int run_picknrun(const char *name, const char *config,
                        const char *keys) {
  char config_path[64];
  char input_path[64];
  char output_path[64];
  char command[512];
  int rc;

  if (make_temp_file(config_path, sizeof(config_path), config) != 0) {
    fail(name, "could not write config file");
    return 255;
  }

  if (make_temp_file(input_path, sizeof(input_path), keys) != 0) {
    unlink(config_path);
    fail(name, "could not write input file");
    return 255;
  }

  if (make_temp_path(output_path, sizeof(output_path)) != 0) {
    unlink(config_path);
    unlink(input_path);
    fail(name, "could not reserve output file");
    return 255;
  }

  snprintf(command, sizeof(command),
           "TERM=xterm timeout 5 script -q -e -c './picknrun --file %s' "
           "/dev/null < %s > %s 2>&1",
           config_path, input_path, output_path);

  rc = run_shell(command);

  unlink(config_path);
  unlink(input_path);
  unlink(output_path);

  return rc;
}

static int file_contains(const char *path, const char *expected) {
  FILE *fp = fopen(path, "rb");
  char buffer[256];
  size_t n;

  if (fp == NULL)
    return 0;

  n = fread(buffer, 1, sizeof(buffer) - 1, fp);
  fclose(fp);
  buffer[n] = '\0';

  return strcmp(buffer, expected) == 0;
}

static void expect_exit(const char *name, const char *config, const char *keys,
                        int expected_rc) {
  int actual_rc = run_picknrun(name, config, keys);
  if (actual_rc != expected_rc) {
    char message[128];
    snprintf(message, sizeof(message), "expected exit %d, got %d", expected_rc,
             actual_rc);
    fail(name, message);
    return;
  }

  printf("PASS %s\n", name);
}

static void expect_direct_exit(const char *name, const char *command,
                               int expected_rc) {
  int actual_rc = run_shell(command);
  if (actual_rc != expected_rc) {
    char message[128];
    snprintf(message, sizeof(message), "expected exit %d, got %d", expected_rc,
             actual_rc);
    fail(name, message);
    return;
  }

  printf("PASS %s\n", name);
}

static void test_help(void) {
  expect_direct_exit("help exits cleanly",
                     "./picknrun -h >/tmp/picknrun-help.out", RC_OK);
  unlink("/tmp/picknrun-help.out");
}

static void test_missing_file(void) {
  expect_direct_exit("missing file returns file error",
                     "./picknrun --file /tmp/picknrun-test-missing >/dev/null",
                     RC_ERR_FILE_READ_FOPEN);
}

static void test_valid_configs_quit(void) {
  expect_exit("valid action with final newline", "Item $ echo ok\n", "q",
              RC_OK);
  expect_exit("valid action without final newline", "Item $ echo ok", "q",
              RC_OK);
  expect_exit("valid submenu without final newline", "Menu\n- Item $ echo ok",
              "q", RC_OK);
}

static void test_empty_inputs_are_rejected(void) {
  expect_exit("empty config", "", "q", RC_ERR_PARSE_ERROR_EMPTY_MENU);
  expect_exit("blank config", " \t\n", "q", RC_ERR_PARSE_ERROR_EMPTY_MENU);
  expect_exit("empty submenu", "Menu\n", "q", RC_ERR_PARSE_ERROR_EMPTY_MENU);
  expect_exit("empty submenu without final newline", "Menu", "q",
              RC_ERR_PARSE_ERROR_EMPTY_MENU);
}

static void test_empty_commands_are_rejected(void) {
  expect_exit("empty command with final newline", "Item $   \n", "q",
              RC_ERR_PARSE_ERROR_EMPTY_COMMAND);
  expect_exit("empty command without final newline", "Item $   ", "q",
              RC_ERR_PARSE_ERROR_EMPTY_COMMAND);
}

static void test_action_command_is_not_truncated(void) {
  char marker_path[64];
  char config[256];

  if (make_temp_path(marker_path, sizeof(marker_path)) != 0) {
    fail("action command is not truncated", "could not reserve marker path");
    return;
  }

  snprintf(config, sizeof(config), "Run $ printf ACTION_OK > %s", marker_path);

  expect_exit("execute action without final newline", config, "\nq", RC_OK);

  if (!file_contains(marker_path, "ACTION_OK")) {
    fail("action command is not truncated",
         "marker file did not contain ACTION_OK");
  } else {
    printf("PASS action command is not truncated\n");
  }

  unlink(marker_path);
}

static void test_menu_navigation_and_action(void) {
  char marker_path[64];
  char config[256];

  if (make_temp_path(marker_path, sizeof(marker_path)) != 0) {
    fail("menu navigation and action", "could not reserve marker path");
    return;
  }

  snprintf(config, sizeof(config),
           "Menu\n- First $ printf FIRST > %s\n- Second $ printf SECOND > %s\n",
           marker_path, marker_path);

  expect_exit("execute action", config, "ljlq", RC_OK);

  if (file_contains(marker_path, "FIRST") ||
      !file_contains(marker_path, "SECOND")) {
    fail("menu navigation and action", "marker file did not contain SECOND");
  } else {
    printf("PASS menu navigation and action\n");
  }

  unlink(marker_path);
}

int main(void) {
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  test_help();
  test_missing_file();
  test_valid_configs_quit();
  test_empty_inputs_are_rejected();
  test_empty_commands_are_rejected();
  test_action_command_is_not_truncated();
  test_menu_navigation_and_action();

  if (failures != 0) {
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }

  printf("all tests passed\n");
  return 0;
}
