#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <signal.h>
#include <time.h>

#define SEM_KEY 1234
#define SHM_KEY 5678
#define NUM_FLOWERS 40
#define ILL 1
#define HEALTHY 0

int sem_id;
int shm_id;
int *flowers;

// Процедура очистка семафоров и разделяемой памяти
void clear_resources() {
    semctl(sem_id, 0, IPC_RMID);
    shmdt(flowers);
    shmctl(shm_id, IPC_RMID, NULL);
}

// Процедура-обработчик сигнала прерывания
void sigint_handler(int signum) {
    printf("Получен сигнал SIGINT. Очищение ресурсов...\n");
    clear_resources();
    exit(EXIT_SUCCESS);
}

// Процедура для инициализации общей памяти
void init_shared_memory() {
    shm_id = shmget(SHM_KEY, NUM_FLOWERS * sizeof(int), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Ошибка при инициализации общей памяти");
        exit(EXIT_FAILURE);
    }
    flowers = (int *) shmat(shm_id, NULL, 0);
    if (flowers == (void *) -1) {
        perror("Ошибка при обращении к общей памяти");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < NUM_FLOWERS; i++) {
        flowers[i] = HEALTHY; // Задание всех цветков как здоровых
    }
}

// Процедура для инициализации POSIX-семафоров
void init_semaphores() {
    sem_id = semget(SEM_KEY, 2, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Ошибка при создании семафора");
        exit(EXIT_FAILURE);
    }
    union semun {
        int val;
        struct semid_ds *buf;
        unsigned short *array;
    } sem_attr;
    sem_attr.val = 1;
    semctl(sem_id, ILL, SETVAL, sem_attr);
    semctl(sem_id, HEALTHY, SETVAL, sem_attr);
}

// Проверка состояния цветка садовниками
void check_flower(int index) {
    struct sembuf sem_op[2];
    sem_op[0].sem_num = index % 2; // Операции с семафором в зависимости от состояния цветка
    sem_op[1].sem_num = (index + 1) % 2;
    sem_op[0].sem_op = sem_op[1].sem_op = -1;
    sem_op[0].sem_flg = sem_op[1].sem_flg = SEM_UNDO;
    semop(sem_id, &sem_op[0], 2); // Захват семафоров
    int this_flower = flowers[index];
    int other_flower = flowers[(index + 1) % NUM_FLOWERS];
    if (this_flower == ILL && other_flower == ILL) { // Одновременное поливание двумя садовниками
        printf("Два цветочника пытались полить цветок с индексом %d в одно время, но семафор не дал им столкнуться!\n",
               index);
        sleep(2); // Задержка перед следующей проверкой, чтобы цветки успели выйти из состояния вялости
        printf("Цветок с индексом %d полит\n", index);
    }
    sem_op[0].sem_op = sem_op[1].sem_op = 1;
    semop(sem_id, &sem_op[0], 2); // Освобождение семафоров
}

int main(int argc, char *argv[]) {
    srand(time(NULL)); // Инициализируем генератор случайных чисел

    // Установка обработчика сигнала прерывания
    sigset_t sigset;
    sigemptyset(&sigset);
    signal(SIGINT, sigint_handler);
    signal(SIGQUIT, SIG_IGN);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    // Инициализация общей памяти и семафоров
    init_shared_memory();
    init_semaphores();

    // Создание процессов цветов
    pid_t pids[NUM_FLOWERS];
    for (int i = 0; i < NUM_FLOWERS; i++) {
        pids[i] = fork();
        if (pids[i] == -1) {
            perror("Ошибка при разветвлении процесса цветка ");
            exit(EXIT_FAILURE);
        } else if (pids[i] == 0) { // Дочерний процесс цветка
            while (1) {
                sleep(rand() % 5 + 1); // Ожидание перед началом завядания цветка
                flowers[i] = ILL; // Установка цветка в состояние начала увядания
                printf("Цветок с индексом %d начал увядать\n", i);
                check_flower(i); // Проверка цветка садовниками
                flowers[i] = HEALTHY; // Установка цветка в состояние здоровья
            }
        }
    }

    // Ожидание завершения всех дочерних процессов цветков
    for (int i = 0; i < NUM_FLOWERS; i++) {
        waitpid(pids[i], NULL, 0);
    }

    printf("Программа завершена.\n");

    clear_resources(); // Очистка ресурсов

    return 0;
}
