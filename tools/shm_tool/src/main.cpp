#include <iostream>
#include <getopt.h>
#include <string.h>

#include "shm_base.hpp"

enum MODE
{
  LIST_MODE,
  REMOVE_MODE,
};

char *progname;

void
general_usage()
{
  std::cout << progname << " is a command-line tool to operate shared memory that shm used" << std::endl << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << "\t" << progname << " list\tlist up shared memory" << std::endl;
  std::cout << "\t" << progname << " remove\tremove shared memory" << std::endl;
}

void
remove_usage()
{
  std::cout << "Usage: " << progname << " remove <shm_name>" << std::endl;
}

int
main(int argc, char *argv[])
{
  int opt;
  MODE mode;

  progname = basename(argv[0]);

  if (argc < 2)
  {
    general_usage();
    return 1;
  }

  if (!strncmp(argv[1], "list", 4))
  {
    mode = LIST_MODE;
  }
  else if (!strncmp(argv[1], "remove", 6))
  {
    mode = REMOVE_MODE;
  }

  FILE *fp;
  char buf[256];
  std::string buf_str;
  const char* format[] = {" ", "\t\t", "\t", "\t", "\t", " ", " ", " ", "\t", ""};
  switch (mode)
  {
  case LIST_MODE:
    fp = popen("ls -l /dev/shm/", "r");
    fgets(buf, sizeof(buf), fp);
    std::cout << "Permission Hard-link\tUser\tGroup\tSize\tTimestamp\tShared memory name" << std::endl;
    while (1) 
    {
      fgets(buf, sizeof(buf), fp);
      if(feof(fp))
      {
        break;
      }

      if(ferror(fp))
      {
        fprintf(stderr, "input stream error\n");
        break;
      }
      buf_str = buf;
      buf_str = regex_replace(buf_str, std::regex("shm_"), "");
      {
        auto offset = std::string::size_type(0);
        for (int i = 0; i < 10; i++) 
	{
          auto pos = buf_str.find(" ", offset);
          if (pos == std::string::npos) {
            std::cout << buf_str.substr(offset);
            break;
          }
	  std::cout << buf_str.substr(offset, pos - offset) << format[i];
          offset = pos + 1;
        }
      }
    }
    (void) pclose(fp);
    break;
  case REMOVE_MODE:
    if (argc < 3)
    {
      remove_usage();
      return 1;
    }
    irlab::shm::disconnectMemory(argv[2]);
    break;
  default:
    general_usage();
  }

  return 0;
}
