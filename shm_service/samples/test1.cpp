//!
//! @file test1.cpp
//! @brief ローカルおよび共有メモリに関するサンプルプログラム
//! @note 記法はROSに準拠する
//!       http://wiki.ros.org/ja/CppStyleGuide
//! 
//! @author M.Hijikata
//!

#include <iostream>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>

#include "shm_service.hpp"
#include "test1.hpp"

#define DEFAULT_NAME  "/test"

// *******************************************************************

void
requesting(std::string name)
{
  ClassTest test;
  irlab::shm::ServiceClient<int, ClassTest> client(name);

  for (int i = 0; i < 10; i++)
  {
    if (client.call(i, &test))
    {
      std::cout << "request: " << i << std::endl;
      std::cout << "response: test.a =\t" << test.a << std::endl;
      std::cout << "response: test.b =\t" << test.b << std::endl;
      for (int index = 0; index < 5; index++)
      {
        std::cout << "response: test.c[" << index << "] =\t" << test.c[index] << std::endl;
      }
    }
    else
    {
      std::cout << "Service Request failed\n";
    }
    
    sleep(1);
  }
}

ClassTest
makeResponse(int req)
{
  ClassTest test;
  test.a = req;
  test.b = 2*req;
  for (int i = 0; i < 5; i++)
  {
    test.c[i] = req * i;
  }

  return test;
}

void
responsing(std::string name)
{
  irlab::shm::ServiceServer<int, ClassTest> server(name, makeResponse);

  for (int i = 0; i < 10; i++)
  {
    sleep(2);
  }
}

void
usage(char *prog_name)
{
  fprintf(stderr, "Usage: %s {-w|-r} {-k name|-K name}\n", prog_name);
  fprintf(stderr, "options:\n");
  fprintf(stderr, "\t-w\t\texecute the writing\n");
  fprintf(stderr, "\t-r\t\texecute the reading\n");
  fprintf(stderr, "\t-k name\t\tset the shm name\n");
  fprintf(stderr, "\t-K name\t\tkill shared memory\n");
  exit(1);
}

int
main(int argc, char **argv)
{
  std::string name = DEFAULT_NAME;
  int opt, flag = 0;

  if (argc == 1) 
  {
    usage(argv[0]);
  }

  do 
  {
    opt = getopt(argc, argv, "wrk:K:h?");
    switch (opt) 
    {
    case 'w':
      flag = 1;
      break;
    case 'r':
      flag = 2;
      break;
    case 'k':
      name = optarg;
      break;
    case 'K':
//      shmRemoveKey(optarg);
      break;
    case 'h':
    case '?':
      usage(argv[0]);
    }
  } while (opt > 0);

  switch (flag) 
  {
    case 1:
      requesting(name);
      break;
    case 2:
      responsing(name);
      break;
    default:
      break;
  }

  return 0;
}
