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

// ARM-safe aligned structure
struct alignas(8) ComplexStruct
{
  int id;                    // 4 bytes
  float position[3];         // 12 bytes
  double timestamp;          // 8 bytes (requires 8-byte alignment on ARM)
  bool active;               // 1 byte
  char padding[3];           // Explicit padding to ensure 8-byte alignment
  
  ComplexStruct() : id(0), timestamp(0.0), active(false) {
    position[0] = position[1] = position[2] = 0.0f;
    padding[0] = padding[1] = padding[2] = 0; // Initialize padding
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

// Compile-time verification for ARM compatibility
static_assert(sizeof(ComplexStruct) % 8 == 0, 
              "ComplexStruct must be 8-byte aligned for ARM processors");
static_assert(std::is_standard_layout<ComplexStruct>::value, 
              "ComplexStruct must be standard layout for shared memory");
static_assert(std::is_trivially_copyable<ComplexStruct>::value, 
              "ComplexStruct must be trivially copyable for shared memory");

#endif //__COMMON_H__
