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

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"
#include "test1.hpp"

#define DEFAULT_NAME  "/test"

// *******************************************************************

void
writing(std::string name)
{
  ClassTest test;
  irlab::shm::Publisher<ClassTest> pub(name);
  std::vector<ClassTest> test_vector(3);
  irlab::shm::Publisher<std::vector<ClassTest>> pub_vector("test_vector");

  for (int i = 0; i < 10; i++)
  {
    test.a = i;
    test.b = test.a+test.b;
    test.c[0] = test.b/(test.b+1);
    test.c[1] = test.b-test.c[0];
    test.c[2] = test.b*test.c[0];
    test.c[3]++;
    test.c[4]--;

    test_vector.resize(i/3+3);
    test_vector[0] = test;
    test_vector[1].a = test.a * i;
    test_vector[1].b = test.b * i;
    test_vector[1].c[0] = test.c[0] * i;
    test_vector[1].c[1] = test.c[1] * i;
    test_vector[1].c[2] = test.c[2] * i;
    test_vector[1].c[3] = test.c[3] * i;
    test_vector[1].c[4] = test.c[4] * i;
    test_vector[2].a = test.a * i;
    test_vector[2].b = test.b * i;
    test_vector[2].c[0] = test.c[0] * i;
    test_vector[2].c[1] = test.c[1] * i;
    test_vector[2].c[2] = test.c[2] * i;
    test_vector[2].c[3] = test.c[3] * i;
    test_vector[2].c[4] = test.c[4] * i;

    std::cout << "write: test.a =\t" << test.a << std::endl;
    std::cout << "write: test.b =\t" << test.b << std::endl;
    for (int index = 0; index < 5; index++)
    {
      std::cout << "write: test.c[" << index << "] =\t" << test.c[index] << std::endl;
    }
  
    for (int vector_index = 0; vector_index < 3; vector_index++)
    {
      std::cout << "write: test_vector[" << vector_index << "].a =\t" << test_vector[vector_index].a << std::endl;
      std::cout << "write: test_vector[" << vector_index << "].b =\t" << test_vector[vector_index].b << std::endl;
      for (int index = 0; index < 5; index++)
      {
        std::cout << "write: test_vector[" << vector_index << "].c[" << index << "] =\t" << test_vector[vector_index].c[index] << std::endl;
      }
    }

    pub.publish(test);
    pub_vector.publish(test_vector);
    
    sleep(1);
  }
}


void
reading(std::string name)
{
  irlab::shm::Subscriber<ClassTest> sub(name);
  irlab::shm::Subscriber<std::vector<ClassTest>> sub_vector("test_vector");

  for (int i = 0; i < 10; i++)
  {
    bool is_success;
    sub.waitFor(2000000); /* 2.0[sec]*/
    auto test = sub.subscribe(&is_success);
    if (is_success == false)
    {
      std::cout << "subscribe failed" << std::endl;
      continue;
    }
    std::cout << "read: test.a =\t" << test.a << std::endl;
    std::cout << "read: test.b =\t" << test.b << std::endl;
    for (int index = 0; index < 5; index++)
    {
      std::cout << "read: test.c[" << index << "] =\t" << test.c[index] << std::endl;
    }

    auto test_vector = sub_vector.subscribe(&is_success);
    if (is_success == false)
    {
      std::cout << "subscribe vector failed" << std::endl;
    }  
    for (size_t vector_index = 0; vector_index < test_vector.size(); vector_index++)
    {
      std::cout << "read: test_vector[" << vector_index << "].a =\t" << test_vector[vector_index].a << std::endl;
      std::cout << "read: test_vector[" << vector_index << "].b =\t" << test_vector[vector_index].b << std::endl;
      for (int index = 0; index < 5; index++)
      {
        std::cout << "read: test_vector[" << vector_index << "].c[" << index << "] =\t" << test_vector[vector_index].c[index] << std::endl;
      }
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
      writing(name);
      break;
    case 2:
      reading(name);
      break;
    default:
      break;
  }

  return 0;
}
