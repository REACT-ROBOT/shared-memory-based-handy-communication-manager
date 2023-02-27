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

#include "shm_action.hpp"
#include "test1.hpp"

#define DEFAULT_NAME  "/test"

// *******************************************************************

void
requesting(std::string name)
{
  ClassTest test;
  irlab::shm::ActionClient<int, ClassTest, float> client(name);

  while (!client.waitForServer(1000000))
  {
    std::cout << "Wait for server connect" << std::endl;
  }

  for (int i = 0; i < 10; i++)
  {
    if (!client.sendGoal(i))
    {
      std::cout << "Failed to send goal" << std::endl;
      continue;
    }
    std::cout << "sendGoal: Goal = " << i << std::endl;

    while (!client.waitForResult(500000))
    {
      float feedback = client.getFeedback();
      std::cout << "Feedback: " << feedback << std::endl;
    }

    test = client.getResult();
    std::cout << "response: test.a =\t" << test.a << std::endl;
    std::cout << "response: test.b =\t" << test.b << std::endl;
    for (int index = 0; index < 5; index++)
    {
      std::cout << "response: test.c[" << index << "] =\t" << test.c[index] << std::endl;
    }

    sleep(1);
  }
}

void
responsing(std::string name)
{
  irlab::shm::ActionServer<int, ClassTest, float> server(name);
  bool is_preempted = false;

  for (int i = 0; i < 10; i++)
  {
    server.waitNewGoalAvailable();
    int goal = server.acceptNewGoal();

    for (int count = 0; count < 3; count++)
    {
      if (server.isPreemptRequested())
      {
        server.setPreempted();
        is_preempted = true;
        break;
      }
      server.publishFeedback(static_cast<float>(count));
      sleep(1);
    }

    if (is_preempted)
    {
      is_preempted = false;
    }
    else
    {
      ClassTest result;
      result.a = goal;
      result.b = 2*goal;
      for (int index = 0; index < 5; index++)
      {
        result.c[index] = goal * index;
      }
      server.publishResult(result);
    }
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
