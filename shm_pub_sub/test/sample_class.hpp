//!
//! @file test1.h
//! ローカルおよび共有メモリに関するサンプルクラス
//! @note 記法はROSに準拠する
//!       http://wiki.ros.org/ja/CppStyleGuide
//! 
//! @author M.Hijikata
//!

#ifndef __COMMON_H__
#define __COMMON_H__

class ClassTest 
{
public:
  ClassTest();
  ~ClassTest() {};
  ClassTest(const ClassTest& test);
  ClassTest& operator=(const ClassTest& test);

  int a;
  int b;
  int c[5];
};

ClassTest::ClassTest()
{
}

ClassTest::ClassTest(const ClassTest& test)
: a(test.a)
, b(test.b)
{
  for (int i = 0; i < 5; i++)
  {
    c[i] = test.c[i];
  }
}

ClassTest&
ClassTest::operator=(const ClassTest& test)
{
  a = test.a;
  b = test.b;
  for (int i = 0; i < 5; i++)
  {
    c[i] = test.c[i];
  }

  return *this;
}

class BadClass : public ClassTest
{
public:
  int d;
};

#endif //__COMMON_H__
