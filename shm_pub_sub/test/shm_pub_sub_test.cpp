#include <gtest/gtest.h>
#include <libgen.h>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"
#include "sample_class.hpp"


TEST(SHMPubSubTest, BasicTest)
{
  {
    ClassTest test;
    irlab::shm::Publisher<ClassTest> pub("/test");
    irlab::shm::Subscriber<ClassTest> sub("/test");

    test.a = 1;
    test.b = 2;
    test.c[0] = 3;
    test.c[1] = 4;
    test.c[2] = 5;
    test.c[3] = 6;
    test.c[4] = 7;

    pub.publish(test);

    bool is_successed = false;
    ClassTest result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.a, 1);
    EXPECT_EQ(result.b, 2);
    EXPECT_EQ(result.c[0], 3);
    EXPECT_EQ(result.c[1], 4);
    EXPECT_EQ(result.c[2], 5);
    EXPECT_EQ(result.c[3], 6);
    EXPECT_EQ(result.c[4], 7);
  }


  irlab::shm::disconnectMemory("test");
}

TEST(SHMPubSubTest, ConstructorErrorTest)
{
  {
    EXPECT_THROW(
      irlab::shm::Publisher<BadClass> pub("test"),
      std::runtime_error
    );
  }
  {
    EXPECT_THROW(
      irlab::shm::Publisher<ClassTest> pub(""),
      std::runtime_error
    );
  }

  {
    EXPECT_THROW(
      irlab::shm::Subscriber<BadClass> sub("test"),
      std::runtime_error
    );
  }
  {
    EXPECT_THROW(
      irlab::shm::Subscriber<ClassTest> sub(""),
      std::runtime_error
    );
  }

}
