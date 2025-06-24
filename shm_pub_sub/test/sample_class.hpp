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

struct SimpleInt
{
  int value;
  
  SimpleInt() : value(0) {}
  SimpleInt(int v) : value(v) {}
  
  bool operator==(const SimpleInt& other) const {
    return value == other.value;
  }
};

struct SimpleFloat
{
  float value;
  
  SimpleFloat() : value(0.0f) {}
  SimpleFloat(float v) : value(v) {}
  
  bool operator==(const SimpleFloat& other) const {
    return value == other.value;
  }
};

struct SimpleDouble
{
  double value;
  
  SimpleDouble() : value(0.0) {}
  SimpleDouble(double v) : value(v) {}
  
  bool operator==(const SimpleDouble& other) const {
    return value == other.value;
  }
};

struct ComplexStruct
{
  int id;
  float position[3];
  double timestamp;
  bool active;
  
  ComplexStruct() : id(0), timestamp(0.0), active(false) {
    position[0] = position[1] = position[2] = 0.0f;
  }
  
  bool operator==(const ComplexStruct& other) const {
    return id == other.id && 
           position[0] == other.position[0] &&
           position[1] == other.position[1] &&
           position[2] == other.position[2] &&
           timestamp == other.timestamp &&
           active == other.active;
  }
};

#endif //__COMMON_H__
