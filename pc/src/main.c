#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int main(void) {
  const char *device_path = "/dev/ttyACM0";
  char input[128];

  int fd = open(device_path, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  struct termios tty;
  if (tcgetattr(fd, &tty) != 0) {
    perror("tcgetattr");
    close(fd);
    return 1;
  }

  cfsetispeed(&tty, B115200);
  cfsetospeed(&tty, B115200);

  tty.c_cflag |= CLOCAL | CREAD;
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
  tty.c_cflag &= ~CRTSCTS;
#endif

  tty.c_lflag = 0;
  tty.c_iflag = 0;
  tty.c_oflag = 0;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 10;

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    perror("tcsetattr");
    close(fd);
    return 1;
  }

  while (true) {
    printf("pc> ");
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) {
      break;
    }

    if (strcmp(input, "quit\n") == 0 || strcmp(input, "exit\n") == 0) {
      break;
    }

    if (write(fd, input, strlen(input)) < 0) {
      perror("write");
      close(fd);
      return 1;
    }

    char response[64];
    for (int i = 0; i < 5; i++) {
      ssize_t count = read(fd, response, sizeof(response) - 1);
      if (count < 0) {
        perror("read");
        close(fd);
        return 1;
      }

      if (count == 0) {
        break;
      }

      response[count] = '\0';
      printf("%s", response);
    }
    printf("\n");
  }

  close(fd);
  return 0;
}
