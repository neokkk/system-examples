#define _POSIX_C_SOURCE 200809L
#define THREAD_NUM 2
#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n\a"
#define BUFSIZE 1024

#include <input.h>

typedef struct _sig_ucontext {
  unsigned long uc_flags;
  struct ucontext *uc_link;
  stack_t uc_stack;
  // struct sigcontext uc_mcontext;
  sigset_t uc_sigmask;
} sig_ucontext_t;

static pthread_mutex_t global_message_mutex = PTHREAD_MUTEX_INITIALIZER;
static mqd_t monitor_queue;
static sensor_data_t* sensor_data;

static char global_message[BUFSIZE];

static void sigsegv_handler(int sig_num, siginfo_t* info, void* ucontext)
{
  void* array[50];
  void* caller_address;
  char** messages;
  int size, i;
  sig_ucontext_t* uc;

  uc = (sig_ucontext_t*)ucontext;

  /* Get the address at the time the signal was raised */
  // caller_address = (void*) uc ->uc_mcontext.x86_64;  // RIP: x86_64 specific     arm_pc: ARM

  fprintf(stderr, "\n");

  if (sig_num == SIGSEGV)
    printf("signal %d (%s), address is %p from %p\n", sig_num, strsignal(sig_num), info->si_addr,
           (void*) caller_address);
  else
    printf("signal %d (%s)\n", sig_num, strsignal(sig_num));

  size = backtrace(array, 50);
  /* overwrite sigaction with caller's address */
  // array[1] = caller_address;
  // messages = backtrace_symbols(array, size);

  /* skip first stack frame (points here) */
  // for (i = 1; i < size && messages != NULL; ++i) {
  //   printf("[bt]: (%d) %s\n", i, messages[i]);
  // }

  // free(messages);
  exit(-1);
}

char* builtin_str[] = {
  "send",
  "mu",
  "sh",
  "mq",
  "exit"
};

int toy_num_builtins()
{
  return sizeof(builtin_str) / sizeof(char*);
}

int toy_send(char** args)
{
  printf("send message: %s\n", args[1]);
  return 0;
}

int toy_mutex(char** args)
{
  if (args[1] == NULL) {
    return -1;
  }

  printf("save message: %s\n", args[1]);

  if (pthread_mutex_lock(&global_message_mutex) != 0) {
    perror("pthread_mutex_lock error");
    return -1;
  }

  strcpy(global_message, args[1]);
  
  if (pthread_mutex_unlock(&global_message_mutex)) {
    perror("pthread_mutex_unlock error");
    return -1;
  }
  
  return 0;
}

int toy_exit(char** args)
{
  return 0;
}

int toy_shell(char** args)
{
  pid_t pid;
  int status;

  pid = fork();
  if (pid == 0) {
    if (execvp(args[0], args) < 0) {
      perror("execvp error");
    }
    exit(-1);
  } else if (pid < 0) {
    perror("fork error");
  } else {
    do
    {
      waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
  }

  return 0;
}

int toy_message_queue(char** args)
{
  int mqretcode;
  toy_msg_t msg;

  if (args[1] == NULL || args[2] == NULL) {
    return 1;
  }

  if (!strcmp(args[1], "camera")) {
    msg.msg_type = atoi(args[2]);
    msg.param1 = 0;
    msg.param2 = 0;
    mqretcode = mq_send("/camera_queue", (void*)&msg, sizeof(msg), 0);
    assert(mqretcode == 0);
  }

  return 0;
}

int (*builtin_func[])(char**) = {
  &toy_send,
  &toy_mutex,
  &toy_shell,
  &toy_message_queue,
  &toy_exit
};

int toy_execute(char** args)
{
  int i;

  if (args[0] == NULL) {
    return -1;
  }

  for (i = 0; i < toy_num_builtins(); i++) {
    if (strcmp(args[0], builtin_str[i]) == 0) {
      return (*builtin_func[i])(args);
    }
  }

  return 0;
}

char* toy_read_line(void)
{
  char* line = NULL;
  ssize_t bufsize = 0;

  if (getline(&line, &bufsize, stdin) == -1) {
    if (feof(stdin)) {
      exit(EXIT_SUCCESS);
    } else {
      perror(": getline\n");
      exit(-1);
    }
  }
  return line;
}

char** toy_split_line(char* line)
{
  int bufsize = TOK_BUFSIZE, position = 0;
  char** tokens = malloc(bufsize * sizeof(char*));
  char *token, **tokens_backup;

  if (!tokens) {
    fprintf(stderr, "toy: allocation error\n");
    exit(-1);
  }

  token = strtok(line, TOK_DELIM);
  while (token != NULL) {
    tokens[position] = token;
    position++;

    if (position >= bufsize) {
      bufsize += TOK_BUFSIZE;
      tokens_backup = tokens;
      tokens = realloc(tokens, bufsize * sizeof(char*));

      if (!tokens) {
        free(tokens_backup);
        fprintf(stderr, "toy: allocation error\n");
        exit(EXIT_FAILURE);
      }
    }

    token = strtok(NULL, TOK_DELIM);
  }

  tokens[position] = NULL;
  return tokens;
}

void toy_loop()
{
  char* line;
  char** args;
  int status;

  do {
    printf("TOY> ");
    line = toy_read_line();
    args = toy_split_line(line);
    status = toy_execute(args);

    free(line);
    free(args);
  } while (status);
}

int get_shm_id(key_t shm_key, int size) {
  int shmid;
  if ((shmid = shmget(shm_key, size, 0666)) < 0) {
    perror("shmget error");
    printf("errno: %d\n", errno);
    exit(-1);
  }
  printf("shmid: %d\n", shmid);
  return shmid;
}

void* command_thread(void* arg)
{
  toy_loop();
  exit(EXIT_SUCCESS);
}

void* sensor_thread(void* arg)
{
  int thread_id = (int)arg;
  toy_msg_t msg;
  int semid, shmid;
  int mqretcode;

  shmid = get_shm_id(SHM_SENSOR_KEY, sizeof(sensor_data_t));

  while (1) {
    posix_sleep_ms(5000);

    if (sensor_data != NULL) {
      sensor_data ->humidity = rand() % 40;
      sensor_data ->temperature = rand() % 20 + 20;
      sensor_data ->pressure = 30;
    }

    msg.msg_type = 1;
    msg.param1 = shmid;
    msg.param2 = 0;
    mqretcode = mq_send(monitor_queue, (char *)&msg, sizeof(msg), 0);
    assert(mqretcode == 0);
  }

  sem_release(semid, 0); // V
}

int input()
{
  printf("input process is running!\n");

  pthread_t threads[THREAD_NUM];
  void* (*thread_funcs[THREAD_NUM])(void*) = {command_thread, sensor_thread};
  struct sigaction sa;

  sa.sa_flags = SA_RESTART | SA_SIGINFO;
  sa.sa_sigaction = sigsegv_handler;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGSEGV, &sa, NULL) < 0) { // SIGSEVG 시그널 핸들러 등록
    perror("sigaction error\n");
    return -1;
  }

  for (int i = 0; i < THREAD_NUM; i++) {
    if (pthread_create(&threads[i], NULL, thread_funcs[i], i) != 0) {
      perror("pthread_create error\n");
      return -1;
    }
  }

  sensor_data = (sensor_data_t*)shm_create(SHM_SENSOR_KEY, sizeof(sensor_data_t)); // 센서 데이터 공유 메모리 생성
  if (sensor_data < (void*)0) { // 센서 데이터 공유 메모리 생성
    perror("shm_create error\n");
    return -1;
  }

  monitor_queue = mq_open("/monitor_queue", O_RDWR);
  if (monitor_queue == -1) {
    perror("mq_open error\n");
    return -1;
  }

  while (1) {
    sleep(1);
  }

  return 0;
}

pid_t create_input()
{
  pid_t input_pid;
  const char *process_name = "input";

  printf("입력 프로세스를 생성합니다.\n");

  switch (input_pid = fork()) {
    case -1:
      perror("input fork error");
      exit(-1);
    case 0:
      if (prctl(PR_SET_NAME, process_name) < 0) {
        perror("prctl error");
        exit(-1);
      }
      input();
      break;
    default:
      printf("input pid: %d\n", input_pid);
  }

  return input_pid;
}
