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

// ARM-compatible trivially copyable structure
struct alignas(4) ClassTest 
{
  int a;
  int b;
  int c[5];
  
  // Default initialization - keeps it trivially copyable
  ClassTest() = default;
  
  // For testing purposes, provide initialization constructor
  ClassTest(int a_val, int b_val, int c0, int c1, int c2, int c3, int c4)
    : a(a_val), b(b_val), c{c0, c1, c2, c3, c4} {}
};

// Compile-time verification for ARM compatibility
static_assert(std::is_trivially_copyable<ClassTest>::value, 
              "ClassTest must be trivially copyable for ARM shared memory");
static_assert(std::is_standard_layout<ClassTest>::value, 
              "ClassTest must be standard layout for ARM shared memory");

// Non-standard layout class for testing error conditions
class BadClass : public ClassTest
{
public:
  int d;
  // This class is intentionally not standard layout due to inheritance
};

// ARM-compatible aligned structures
struct alignas(4) SimpleInt
{
  int value;
  
  SimpleInt() = default;
  SimpleInt(int v) : value(v) {}
  
  bool operator==(const SimpleInt& other) const {
    return value == other.value;
  }
};

struct alignas(4) SimpleFloat
{
  float value;
  
  SimpleFloat() = default;
  SimpleFloat(float v) : value(v) {}
  
  bool operator==(const SimpleFloat& other) const {
    return value == other.value;
  }
};

struct alignas(8) SimpleDouble
{
  double value;
  
  SimpleDouble() = default;
  SimpleDouble(double v) : value(v) {}
  
  bool operator==(const SimpleDouble& other) const {
    return value == other.value;
  }
};

// Compile-time verification for ARM compatibility
static_assert(std::is_trivially_copyable<SimpleInt>::value && std::is_standard_layout<SimpleInt>::value, 
              "SimpleInt must be trivially copyable and standard layout for ARM");
static_assert(std::is_trivially_copyable<SimpleFloat>::value && std::is_standard_layout<SimpleFloat>::value, 
              "SimpleFloat must be trivially copyable and standard layout for ARM");
static_assert(std::is_trivially_copyable<SimpleDouble>::value && std::is_standard_layout<SimpleDouble>::value, 
              "SimpleDouble must be trivially copyable and standard layout for ARM");

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
