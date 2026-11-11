#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

enum {
  TOOL_LIST_TABLES = 0,
  TOOL_DESCRIBE_TABLE,
  TOOL_QUERY,
  TOOL_EXECUTE,
  TOOL_GET_INDEXES,
  TOOL_GET_FOREIGN_KEYS,
  TOOL_DATABASE_INFO,
  TOOL_EXPLAIN_QUERY,
  TOOL_COUNT,
  TESTS_PER_ITERATION = TOOL_COUNT
};

static const char *k_tool_names[TOOL_COUNT] = {
	"sqlite_list_tables",
	"sqlite_describe_table",
	"sqlite_query",
	"sqlite_execute",
	"sqlite_get_indexes",
	"sqlite_get_foreign_keys",
	"sqlite_database_info",
	"sqlite_explain_query"};

#ifdef _WIN32
typedef LONG64 counter_t;
typedef HANDLE thread_handle_t;
#define THREAD_RETURN DWORD WINAPI
#else
typedef long long counter_t;
typedef pthread_t thread_handle_t;
#define THREAD_RETURN void *
#endif

typedef struct {
#ifdef _WIN32
  HANDLE stdin_write;
  HANDLE stdout_read;
  PROCESS_INFORMATION pi;
#else
  int stdin_write;
  int stdout_read;
  pid_t pid;
#endif
} mcp_process_t;

typedef struct {
  const char *server_exe;
  const char *db_path;
  int iterations;
  int worker_index;
  counter_t *total_calls;
  counter_t *success_calls;
  counter_t *per_tool_total;
  counter_t *per_tool_success;
} worker_args_t;

static void counter_add(counter_t *target, counter_t value) {
#ifdef _WIN32
  InterlockedAdd64(target, value);
#else
  __sync_fetch_and_add(target, value);
#endif
}

#ifdef _WIN32
static int write_line(HANDLE h, const char *line) {
  DWORD written = 0;
  size_t n = strlen(line);
  if (!WriteFile(h, line, (DWORD)n, &written, NULL) || written != (DWORD)n) {
	return 0;
  }
  if (!WriteFile(h, "\n", 1, &written, NULL) || written != 1) {
	return 0;
  }
  return 1;
}

static int read_line(HANDLE h, char *out, size_t out_size) {
  size_t i = 0;
  for (;;) {
	char ch = '\0';
	DWORD read = 0;
	if (!ReadFile(h, &ch, 1, &read, NULL) || read == 0) {
	  return 0;
	}
	if (ch == '\n') {
	  break;
	}
	if (ch == '\r') {
	  continue;
	}
	if (i + 1 < out_size) {
	  out[i++] = ch;
	}
  }
  out[i] = '\0';
  return 1;
}
#else
static int write_all(int fd, const char *buf, size_t len) {
  size_t written = 0;
  while (written < len) {
	ssize_t rc = write(fd, buf + written, len - written);
	if (rc <= 0) {
	  return 0;
	}
	written += (size_t)rc;
  }
  return 1;
}

static int write_line(int fd, const char *line) {
  size_t n = strlen(line);
  return write_all(fd, line, n) && write_all(fd, "\n", 1);
}

static int read_line(int fd, char *out, size_t out_size) {
  size_t i = 0;
  for (;;) {
	char ch = '\0';
	ssize_t rc = read(fd, &ch, 1);
	if (rc <= 0) {
	  return 0;
	}
	if (ch == '\n') {
	  break;
	}
	if (ch == '\r') {
	  continue;
	}
	if (i + 1 < out_size) {
	  out[i++] = ch;
	}
  }
  out[i] = '\0';
  return 1;
}
#endif

static int response_ok(const char *line) {
  return strstr(line, "\"error\":{") == NULL;
}

static int send_request(mcp_process_t *proc, const char *json) {
  char line[65536];
  if (!write_line(proc->stdin_write, json)) {
	return 0;
  }
  if (!read_line(proc->stdout_read, line, sizeof(line))) {
	return 0;
  }
  return response_ok(line);
}

static int start_mcp_process(mcp_process_t *proc, const char *server_exe, const char *db_path) {
#ifdef _WIN32
  HANDLE child_stdout_read = NULL;
  HANDLE child_stdout_write = NULL;
  HANDLE child_stdin_read = NULL;
  HANDLE child_stdin_write = NULL;

  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0)) {
	return 0;
  }
  if (!SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
	CloseHandle(child_stdout_read);
	CloseHandle(child_stdout_write);
	return 0;
  }

  if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) {
	CloseHandle(child_stdout_read);
	CloseHandle(child_stdout_write);
	return 0;
  }
  if (!SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0)) {
	CloseHandle(child_stdout_read);
	CloseHandle(child_stdout_write);
	CloseHandle(child_stdin_read);
	CloseHandle(child_stdin_write);
	return 0;
  }

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = child_stdin_read;
  si.hStdOutput = child_stdout_write;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  ZeroMemory(&proc->pi, sizeof(proc->pi));

  char cmdline[2048];
  snprintf(cmdline, sizeof(cmdline), "\"%s\" \"%s\"", server_exe, db_path);

  int ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &proc->pi) ? 1 : 0;

  CloseHandle(child_stdin_read);
  CloseHandle(child_stdout_write);

  if (!ok) {
	CloseHandle(child_stdout_read);
	CloseHandle(child_stdin_write);
	return 0;
  }

  proc->stdin_write = child_stdin_write;
  proc->stdout_read = child_stdout_read;
  return 1;
#else
  int to_child[2] = {-1, -1};
  int from_child[2] = {-1, -1};

  if (pipe(to_child) != 0) {
	return 0;
  }
  if (pipe(from_child) != 0) {
	close(to_child[0]);
	close(to_child[1]);
	return 0;
  }

  pid_t pid = fork();
  if (pid < 0) {
	close(to_child[0]);
	close(to_child[1]);
	close(from_child[0]);
	close(from_child[1]);
	return 0;
  }

  if (pid == 0) {
	dup2(to_child[0], STDIN_FILENO);
	dup2(from_child[1], STDOUT_FILENO);

	close(to_child[0]);
	close(to_child[1]);
	close(from_child[0]);
	close(from_child[1]);

	execlp(server_exe, server_exe, db_path, (char *)NULL);
	_exit(127);
  }

  close(to_child[0]);
  close(from_child[1]);

  proc->stdin_write = to_child[1];
  proc->stdout_read = from_child[0];
  proc->pid = pid;
  return 1;
#endif
}

static void stop_mcp_process(mcp_process_t *proc) {
#ifdef _WIN32
  if (proc->stdin_write) {
	CloseHandle(proc->stdin_write);
	proc->stdin_write = NULL;
  }
  if (proc->stdout_read) {
	CloseHandle(proc->stdout_read);
	proc->stdout_read = NULL;
  }
  if (proc->pi.hProcess) {
	WaitForSingleObject(proc->pi.hProcess, 5000);
	CloseHandle(proc->pi.hProcess);
	proc->pi.hProcess = NULL;
  }
  if (proc->pi.hThread) {
	CloseHandle(proc->pi.hThread);
	proc->pi.hThread = NULL;
  }
#else
  if (proc->stdin_write >= 0) {
	close(proc->stdin_write);
	proc->stdin_write = -1;
  }
  if (proc->stdout_read >= 0) {
	close(proc->stdout_read);
	proc->stdout_read = -1;
  }
  if (proc->pid > 0) {
	int status = 0;
	waitpid(proc->pid, &status, 0);
	proc->pid = 0;
  }
#endif
}

static int run_tool_call(mcp_process_t *proc, int *next_id, int tool_index, const char *arguments_json) {
  char req[4096];
  snprintf(req,
		   sizeof(req),
		   "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}",
		   (*next_id)++,
		   k_tool_names[tool_index],
		   arguments_json);
  return send_request(proc, req);
}

static int run_single_iteration(mcp_process_t *proc,
								int worker_index,
								int iteration,
								int *next_id,
								counter_t *per_tool_total,
								counter_t *per_tool_success) {
  int success = 0;
  char args[1024];

  per_tool_total[TOOL_LIST_TABLES]++;
  if (run_tool_call(proc, next_id, TOOL_LIST_TABLES, "{}")) {
	per_tool_success[TOOL_LIST_TABLES]++;
	success++;
  }

  per_tool_total[TOOL_DESCRIBE_TABLE]++;
  if (run_tool_call(proc, next_id, TOOL_DESCRIBE_TABLE, "{\"table\":\"actor\"}")) {
	per_tool_success[TOOL_DESCRIBE_TABLE]++;
	success++;
  }

  per_tool_total[TOOL_QUERY]++;
  if (run_tool_call(proc,
					next_id,
					TOOL_QUERY,
					"{\"sql\":\"SELECT actor_id, first_name FROM actor LIMIT 1\"}")) {
	per_tool_success[TOOL_QUERY]++;
	success++;
  }

  snprintf(args,
		   sizeof(args),
		   "{\"sql\":\"CREATE TABLE IF NOT EXISTS __mcp_tool_test_%d_%d(id INTEGER); DROP TABLE __mcp_tool_test_%d_%d;\"}",
		   worker_index,
		   iteration,
		   worker_index,
		   iteration);
  per_tool_total[TOOL_EXECUTE]++;
  if (run_tool_call(proc, next_id, TOOL_EXECUTE, args)) {
	per_tool_success[TOOL_EXECUTE]++;
	success++;
  }

  per_tool_total[TOOL_GET_INDEXES]++;
  if (run_tool_call(proc, next_id, TOOL_GET_INDEXES, "{\"table\":\"actor\"}")) {
	per_tool_success[TOOL_GET_INDEXES]++;
	success++;
  }

  per_tool_total[TOOL_GET_FOREIGN_KEYS]++;
  if (run_tool_call(proc, next_id, TOOL_GET_FOREIGN_KEYS, "{\"table\":\"rental\"}")) {
	per_tool_success[TOOL_GET_FOREIGN_KEYS]++;
	success++;
  }

  per_tool_total[TOOL_DATABASE_INFO]++;
  if (run_tool_call(proc, next_id, TOOL_DATABASE_INFO, "{}")) {
	per_tool_success[TOOL_DATABASE_INFO]++;
	success++;
  }

  per_tool_total[TOOL_EXPLAIN_QUERY]++;
  if (run_tool_call(proc, next_id, TOOL_EXPLAIN_QUERY, "{\"sql\":\"SELECT * FROM actor LIMIT 1\"}")) {
	per_tool_success[TOOL_EXPLAIN_QUERY]++;
	success++;
  }

  return success;
}

static THREAD_RETURN worker_main(
#ifdef _WIN32
	LPVOID arg
#else
	void *arg
#endif
) {
  worker_args_t *w = (worker_args_t *)arg;
  counter_t local_total = 0;
  counter_t local_success = 0;
  counter_t local_per_tool_total[TOOL_COUNT] = {0};
  counter_t local_per_tool_success[TOOL_COUNT] = {0};

  mcp_process_t proc;
  memset(&proc, 0, sizeof(proc));
#ifndef _WIN32
  proc.stdin_write = -1;
  proc.stdout_read = -1;
  proc.pid = 0;
#endif

  if (!start_mcp_process(&proc, w->server_exe, w->db_path)) {
	local_total = (counter_t)w->iterations * TESTS_PER_ITERATION;
	for (int t = 0; t < TOOL_COUNT; t++) {
	  local_per_tool_total[t] = w->iterations;
	}
	goto done;
  }

  int id = 1;
  if (!send_request(&proc,
					"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2026-07-28\",\"capabilities\":{},\"clientInfo\":{\"name\":\"sqlite-mcp-tool-tester\",\"version\":\"1.0\"}}}")) {
	stop_mcp_process(&proc);
	local_total = (counter_t)w->iterations * TESTS_PER_ITERATION;
	for (int t = 0; t < TOOL_COUNT; t++) {
	  local_per_tool_total[t] = w->iterations;
	}
	goto done;
  }
  id = 2;

  if (!send_request(&proc, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}")) {
	stop_mcp_process(&proc);
	local_total = (counter_t)w->iterations * TESTS_PER_ITERATION;
	for (int t = 0; t < TOOL_COUNT; t++) {
	  local_per_tool_total[t] = w->iterations;
	}
	goto done;
  }
  id = 3;

  for (int i = 0; i < w->iterations; i++) {
	local_success +=
		run_single_iteration(&proc, w->worker_index, i + 1, &id, local_per_tool_total, local_per_tool_success);
	local_total += TESTS_PER_ITERATION;
  }

  stop_mcp_process(&proc);

done:
  counter_add(w->total_calls, local_total);
  counter_add(w->success_calls, local_success);
  for (int t = 0; t < TOOL_COUNT; t++) {
	counter_add(&w->per_tool_total[t], local_per_tool_total[t]);
	counter_add(&w->per_tool_success[t], local_per_tool_success[t]);
  }
#ifdef _WIN32
  return (local_total == local_success) ? 0 : 1;
#else
  return NULL;
#endif
}

int main(int argc, char **argv) {
  if (argc < 3 || argc > 5) {
	fprintf(stderr,
			"Usage: %s <path-to-sqlite-mcp[.exe]> <path-to-db> [iterations-per-worker=20] [concurrency=4]\n",
			argv[0]);
	return 1;
  }

  const char *server_exe = argv[1];
  const char *db_path = argv[2];
  int iterations = (argc >= 4) ? atoi(argv[3]) : 20;
  int concurrency = (argc >= 5) ? atoi(argv[4]) : 4;

  if (iterations <= 0 || concurrency <= 0) {
	fprintf(stderr, "iterations and concurrency must be > 0\n");
	return 1;
  }

  thread_handle_t *threads = (thread_handle_t *)calloc((size_t)concurrency, sizeof(thread_handle_t));
  worker_args_t *args = (worker_args_t *)calloc((size_t)concurrency, sizeof(worker_args_t));
  if (!threads || !args) {
	free(threads);
	free(args);
	fprintf(stderr, "Out of memory\n");
	return 2;
  }

  counter_t total_calls = 0;
  counter_t success_calls = 0;
  counter_t per_tool_total[TOOL_COUNT] = {0};
  counter_t per_tool_success[TOOL_COUNT] = {0};

  int started_threads = 0;
  for (int i = 0; i < concurrency; i++) {
	args[i].server_exe = server_exe;
	args[i].db_path = db_path;
	args[i].iterations = iterations;
	args[i].worker_index = i + 1;
	args[i].total_calls = &total_calls;
	args[i].success_calls = &success_calls;
	args[i].per_tool_total = per_tool_total;
	args[i].per_tool_success = per_tool_success;

#ifdef _WIN32
	threads[i] = CreateThread(NULL, 0, worker_main, &args[i], 0, NULL);
	if (!threads[i]) {
	  fprintf(stderr, "Failed to create worker thread %d\n", i + 1);
	  break;
	}
#else
	if (pthread_create(&threads[i], NULL, worker_main, &args[i]) != 0) {
	  fprintf(stderr, "Failed to create worker thread %d\n", i + 1);
	  break;
	}
#endif
	started_threads++;
  }

  for (int i = 0; i < started_threads; i++) {
#ifdef _WIN32
	WaitForSingleObject(threads[i], INFINITE);
	CloseHandle(threads[i]);
#else
	pthread_join(threads[i], NULL);
#endif
  }

  free(threads);
  free(args);

  counter_t failed_calls = total_calls - success_calls;
  double pct = total_calls > 0 ? (100.0 * (double)success_calls / (double)total_calls) : 0.0;

  printf("SQLite MCP tool stress test complete.\n");
  printf("Total calls: %lld\n", (long long)total_calls);
  printf("Successful calls: %lld\n", (long long)success_calls);
  printf("Failed calls: %lld\n", (long long)failed_calls);
  printf("Overall success rate: %.2f%%\n", pct);
  printf("\nPer-tool success rates:\n");
  for (int t = 0; t < TOOL_COUNT; t++) {
	counter_t t_total = per_tool_total[t];
	counter_t t_success = per_tool_success[t];
	counter_t t_failed = t_total - t_success;
	double t_pct = t_total > 0 ? (100.0 * (double)t_success / (double)t_total) : 0.0;
	printf("- %s: %.2f%% (%lld/%lld, failed=%lld)\n",
		   k_tool_names[t],
		   t_pct,
		   (long long)t_success,
		   (long long)t_total,
		   (long long)t_failed);
  }

  return failed_calls == 0 ? 0 : 3;
}
