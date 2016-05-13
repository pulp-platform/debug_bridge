#include "bridge.h"

int main(int argc, char **argv) {
  unsigned int portNumber = 4567;

  int i;
  for (i=1; i<argc; i++)
  {
    if (strcmp(argv[i], "-p") == 0)
    {
      i++;
      if (i >= argc) {
        fprintf(stderr, "Option -p should take an argument\n");
        exit(-1);
      }
      portNumber = atoi(argv[i]);
    }
    else
    {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      exit(-1);
    }
  }

  Bridge *bridge = new Bridge(unknown, portNumber);
  bridge->mainLoop();
  delete bridge;

  return 0;
}
