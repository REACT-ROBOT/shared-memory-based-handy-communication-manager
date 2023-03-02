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
list_usage()
{
  std::cout << "Usage: " << progname << " list [options]" << std::endl;
  std::cout << "options:" << std::endl;
  printf("\t-i [ID]\t\tshared memory ID (default: 1000)\n");
  printf("\t-x [ID]\t\toutput by the XYZ format\n");
  printf("\t-y [ID]\t\toutput by the YCbCr ormat\n");
  printf("\t-S [dir]\tsaving mode (arg. is a saved directory)\n");
  printf("\t-p [time]\tsampling time (time unit is ms)\n");
  printf("\t-n [num]\tnumber of frames (default: 3 frames)\n");
  printf("\t-V \t\tshow version\n");
  printf("\t-h\t\tthis message\n");
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

  while ( ( opt = getopt(argc, argv, "i:S:s:p:n:yx8r:g:b:h?") ) > 0) {
    switch (opt) {
    case 'i':
      //shm_id = optarg;
      break;
    case 'S':
      //save_flag = 1;
      //dir = optarg;
      break;
    case 's':
      //sscanf(optarg, "%dx%d", &width, &height);
      break;
    case 'p':
      //sample_time_ms = atoi(optarg)/* ms */;
      break;
    case 'n':
      //frame_num = atoi(optarg);
      break;
    case 'y':
      //mode = irlab::YUV_MODE;
      break;
    case 'x':
      //mode = irlab::XYZ_MODE;
      break;
    case '8':
      //mode = irlab::GRAY8;
      break;
    case 'h':
    case '?':
    default:
      general_usage();
      return 1;
    }

  }

  return 0;
}
