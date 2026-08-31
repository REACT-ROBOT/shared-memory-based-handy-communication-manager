#include <iostream>
#include <stdexcept>
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
  std::cout << "\t" << progname << " remove\tremove shared memory (all layout generations)" << std::endl;
}

void
remove_usage()
{
  std::cout << "Usage: " << progname << " remove <shm_name>" << std::endl;
}

int
main(int argc, char *argv[])
{
  // mode を未初期化のまま switch に入れていた。"list" でも "remove" でも
  // ない引数を渡すと不定値で分岐する未定義動作になっていたため、
  // 既定値を持たせて未知のコマンドは明示的に弾く。
  MODE mode = LIST_MODE;

  progname = basename(argv[0]);

  if (argc < 2)
  {
    general_usage();
    return 1;
  }

  // strncmp の長さ指定では "listXXX" のような前方一致も通っていた。
  // コマンド名は完全一致で判定する。
  if (!strcmp(argv[1], "list"))
  {
    mode = LIST_MODE;
  }
  else if (!strcmp(argv[1], "remove"))
  {
    mode = REMOVE_MODE;
  }
  else
  {
    std::cerr << progname << ": unknown command '" << argv[1] << "'" << std::endl << std::endl;
    general_usage();
    return 1;
  }

  FILE *fp;
  char buf[256];
  std::string buf_str;
  const char* format[] = {" ", "\t\t", "\t", "\t", "\t", " ", " ", " ", "\t", ""};
  switch (mode)
  {
  case LIST_MODE:
    fp = popen("ls -l /dev/shm/", "r");
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
      if (buf_str.find("shm_") == std::string::npos)
      {
        continue;
      }
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
    try
    {
      // 形式 v3 ではレイアウト世代ごとに /shm_<topic>#<N> が増えるため、
      // トピック名だけを消しても古い世代が残る。世代ごと片付ける。
      irlab::shm::disconnectTopic(argv[2]);
    }
    catch (const std::exception &e)
    {
      std::cerr << progname << ": " << e.what() << std::endl;
      return 1;
    }
    break;
  }

  return 0;
}
