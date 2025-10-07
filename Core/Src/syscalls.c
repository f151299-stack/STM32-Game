/* syscalls.c -- минимальные реализации системных функций для bare-metal */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#ifdef USE_UART_PRINTF
/* Если хочешь, чтобы printf шел по UART,
   раскомментируй #define USE_UART_PRINTF в проекте и
   объявь extern UART_HandleTypeDef huart2; (или другой UART).
*/
#include "stm32f4xx_hal.h"
extern UART_HandleTypeDef huart2;
#endif

int _close(int file) { return -1; }
int _fstat(int file, struct stat *st) {
	if (st) {
		st->st_mode = S_IFCHR;
		return 0;
	}
	errno = EBADF;
	return -1;
}
int _isatty(int file) { return 1; }
int _lseek(int file, int ptr, int dir) { return 0; }

_ssize_t _read(int file, char *ptr, size_t len) {
	(void)file; (void)ptr; (void)len;
	return 0;
}

_ssize_t _write(int file, const char *ptr, size_t len) {
	(void)file;
#ifdef USE_UART_PRINTF
	if (len && ptr) {
		HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
	}
	return len;
#else
	/* Заглушка: просто "съедает" вывод (printf ничего не выведет). */
	(void)ptr; (void)len;
	return len;
#endif
}

void _exit(int status) {
	(void)status;
	while (1) {}
}

int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
