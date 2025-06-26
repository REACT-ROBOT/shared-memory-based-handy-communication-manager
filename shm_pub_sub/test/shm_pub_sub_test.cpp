#include <gtest/gtest.h>
#include <libgen.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"
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

TEST(SHMPubSubTest, DifferentDataTypesTest)
{
  // Test with SimpleInt
  {
    irlab::shm::Publisher<SimpleInt> pub("/test_int");
    irlab::shm::Subscriber<SimpleInt> sub("/test_int");

    SimpleInt test_data(42);
    pub.publish(test_data);

    bool is_successed = false;
    SimpleInt result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result, test_data);
    irlab::shm::disconnectMemory("test_int");
  }

  // Test with SimpleFloat
  {
    irlab::shm::Publisher<SimpleFloat> pub("/test_float");
    irlab::shm::Subscriber<SimpleFloat> sub("/test_float");

    SimpleFloat test_data(3.14f);
    pub.publish(test_data);

    bool is_successed = false;
    SimpleFloat result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result, test_data);
    irlab::shm::disconnectMemory("test_float");
  }

  // Test with SimpleDouble
  {
    irlab::shm::Publisher<SimpleDouble> pub("/test_double");
    irlab::shm::Subscriber<SimpleDouble> sub("/test_double");

    SimpleDouble test_data(2.718281828);
    pub.publish(test_data);

    bool is_successed = false;
    SimpleDouble result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result, test_data);
    irlab::shm::disconnectMemory("test_double");
  }

  // Test with ComplexStruct
  {
    irlab::shm::Publisher<ComplexStruct> pub("/test_complex");
    irlab::shm::Subscriber<ComplexStruct> sub("/test_complex");

    ComplexStruct test_data;
    test_data.id = 123;
    test_data.position[0] = 1.0f;
    test_data.position[1] = 2.0f;
    test_data.position[2] = 3.0f;
    test_data.timestamp = 1234567890.123;
    test_data.active = true;

    pub.publish(test_data);

    bool is_successed = false;
    ComplexStruct result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result, test_data);
    irlab::shm::disconnectMemory("test_complex");
  }
}

TEST(SHMPubSubTest, VectorTemplateTest)
{
  // Test with vector<SimpleInt> - basic functionality test
  {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_int_vector");
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_int_vector");

    std::vector<SimpleInt> test_data = { SimpleInt(1), SimpleInt(2), SimpleInt(3) };
    pub.publish(test_data);

    bool is_successed = false;
    std::vector<SimpleInt> result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.size(), test_data.size());
    for (size_t i = 0; i < test_data.size(); ++i) {
      EXPECT_EQ(result[i], test_data[i]);
    }
    irlab::shm::disconnectMemory("test_int_vector");
  }

  // Test with vector<float> - different data type
  {
    irlab::shm::Publisher<std::vector<SimpleFloat>> pub("/test_float_vector");
    irlab::shm::Subscriber<std::vector<SimpleFloat>> sub("/test_float_vector");

    std::vector<SimpleFloat> test_data = { SimpleFloat(1.1f), SimpleFloat(2.2f), SimpleFloat(3.3f), SimpleFloat(4.4f) };
    pub.publish(test_data);

    bool is_successed = false;
    std::vector<SimpleFloat> result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.size(), test_data.size());
    for (size_t i = 0; i < test_data.size(); ++i) {
      EXPECT_EQ(result[i], test_data[i]);
    }
    irlab::shm::disconnectMemory("test_float_vector");
  }

  // Test with vector<ComplexStruct> - complex data type
  {
    irlab::shm::Publisher<std::vector<ComplexStruct>> pub("/test_complex_vector");
    irlab::shm::Subscriber<std::vector<ComplexStruct>> sub("/test_complex_vector");

    std::vector<ComplexStruct> test_data;
    ComplexStruct item1, item2;
    item1.id = 1;
    item1.position[0] = 1.0f; item1.position[1] = 2.0f; item1.position[2] = 3.0f;
    item1.timestamp = 123.456;
    item1.active = true;
    
    item2.id = 2;
    item2.position[0] = 4.0f; item2.position[1] = 5.0f; item2.position[2] = 6.0f;
    item2.timestamp = 789.012;
    item2.active = false;
    
    test_data.push_back(item1);
    test_data.push_back(item2);
    
    pub.publish(test_data);

    bool is_successed = false;
    std::vector<ComplexStruct> result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.size(), test_data.size());
    for (size_t i = 0; i < test_data.size(); ++i) {
      EXPECT_EQ(result[i], test_data[i]);
    }
    irlab::shm::disconnectMemory("test_complex_vector");
  }

  // Test with empty vector
  {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_empty_vector");
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_empty_vector");

    std::vector<SimpleInt> test_data; // Empty vector
    pub.publish(test_data);

    bool is_successed = false;
    std::vector<SimpleInt> result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.size(), 0);
    irlab::shm::disconnectMemory("test_empty_vector");
  }

  // Test with single element vector
  {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_single_vector");
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_single_vector");

    std::vector<SimpleInt> test_data = { SimpleInt(42) };
    pub.publish(test_data);

    bool is_successed = false;
    std::vector<SimpleInt> result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], test_data[0]);
    irlab::shm::disconnectMemory("test_single_vector");
  }

  // Test with large vector
  {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_large_vector");
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_large_vector");

    std::vector<SimpleInt> test_data;
    for (int i = 0; i < 100; ++i) {
      test_data.push_back(SimpleInt(i));
    }
    pub.publish(test_data);

    bool is_successed = false;
    std::vector<SimpleInt> result = sub.subscribe(&is_successed);

    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.size(), test_data.size());
    for (size_t i = 0; i < test_data.size(); ++i) {
      EXPECT_EQ(result[i], test_data[i]);
    }
    irlab::shm::disconnectMemory("test_large_vector");
  }

  // Test vector size changes - publish different sizes sequentially
  // Note: Current implementation keeps the maximum vector size in shared memory
  {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_resize_vector");
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_resize_vector");

    // First publish small vector
    std::vector<SimpleInt> small_data = { SimpleInt(1), SimpleInt(2) };
    pub.publish(small_data);

    bool is_successed = false;
    std::vector<SimpleInt> result1 = sub.subscribe(&is_successed);
    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result1.size(), 2);
    EXPECT_EQ(result1[0], small_data[0]);
    EXPECT_EQ(result1[1], small_data[1]);

    // Then publish larger vector (this will expand shared memory)
    std::vector<SimpleInt> large_data;
    for (int i = 10; i < 20; ++i) {
      large_data.push_back(SimpleInt(i));
    }
    pub.publish(large_data);

    std::vector<SimpleInt> result2 = sub.subscribe(&is_successed);
    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result2.size(), 10);
    for (size_t i = 0; i < large_data.size(); ++i) {
      EXPECT_EQ(result2[i], large_data[i]);
    }

    // Then publish medium size vector
    std::vector<SimpleInt> medium_data = { SimpleInt(100), SimpleInt(200), SimpleInt(300), SimpleInt(400), SimpleInt(500) };
    pub.publish(medium_data);

    std::vector<SimpleInt> result3 = sub.subscribe(&is_successed);
    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result3.size(), 5);
    
    // Verify the first 5 elements contain the correct data
    for (size_t i = 0; i < medium_data.size(); ++i) {
      EXPECT_EQ(result3[i], medium_data[i]);
    }
    // Note: Elements 5-9 may contain garbage data from previous large_data

    irlab::shm::disconnectMemory("test_resize_vector");
  }
}

TEST(SHMPubSubTest, MultiThreadTest)
{
  constexpr int NUM_MESSAGES = 100;
  std::vector<bool> received(NUM_MESSAGES, false);
  std::atomic<int> message_count(0);

  // Publisher thread
  std::thread publisher_thread([&]() {
    irlab::shm::Publisher<SimpleInt> pub("/test_multithread");
    
    for (int i = 0; i < NUM_MESSAGES; ++i) {
      SimpleInt data(i);
      pub.publish(data);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // Subscriber thread
  std::thread subscriber_thread([&]() {
    irlab::shm::Subscriber<SimpleInt> sub("/test_multithread");
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(3);

    while (message_count.load() < NUM_MESSAGES) {
      auto current_time = std::chrono::steady_clock::now();
      if (current_time - start_time > timeout) {
        break;
      }
      bool is_successed = false;
      SimpleInt result = sub.subscribe(&is_successed);
      
      if (is_successed && result.value >= 0 && result.value < NUM_MESSAGES) {
        received[result.value] = true;
        message_count.fetch_add(1);
      }
      
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  publisher_thread.join();
  subscriber_thread.join();

  // Verify all messages were received
  int received_count = 0;
  for (bool r : received) {
    if (r) received_count++;
  }
  
  EXPECT_GT(received_count, NUM_MESSAGES * 0.8); // Allow some message loss in concurrent scenario
  irlab::shm::disconnectMemory("test_multithread");
}

TEST(SHMPubSubTest, TimeoutTest)
{
  irlab::shm::Publisher<SimpleInt> pub("/test_timeout");
  irlab::shm::Subscriber<SimpleInt> sub("/test_timeout");

  // Test timeout without any data - waitFor expects microseconds, not milliseconds
  auto start_time = std::chrono::steady_clock::now();
  bool result = sub.waitFor(100000); // 100ms = 100,000 microseconds
  auto end_time = std::chrono::steady_clock::now();
  
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  EXPECT_EQ(result, false);
  // Relax timing requirements as system timing can vary
  EXPECT_GE(duration.count(), 50); // Allow more tolerance for timing variations
  EXPECT_LE(duration.count(), 200);

  // Test with data available
  SimpleInt test_data(42);
  pub.publish(test_data);
  
  start_time = std::chrono::steady_clock::now();
  result = sub.waitFor(100000); // 100ms = 100,000 microseconds
  end_time = std::chrono::steady_clock::now();
  
  duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  EXPECT_EQ(result, true);
  EXPECT_LT(duration.count(), 100); // Should return quickly when data is available

  irlab::shm::disconnectMemory("test_timeout");
}

TEST(SHMPubSubTest, DataExpirationTest)
{
  irlab::shm::Publisher<SimpleInt> pub("/test_expiration");
  irlab::shm::Subscriber<SimpleInt> sub("/test_expiration");

  // Publish data
  SimpleInt test_data(42);
  pub.publish(test_data);

  // Read immediately - should succeed
  bool is_successed = false;
  SimpleInt result = sub.subscribe(&is_successed);
  EXPECT_EQ(is_successed, true);
  EXPECT_EQ(result.value, 42);

  // Wait some time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Read again - data should still be available (no automatic expiration in current implementation)
  result = sub.subscribe(&is_successed);
  EXPECT_EQ(is_successed, true);
  EXPECT_EQ(result.value, 42);

  irlab::shm::disconnectMemory("test_expiration");
}

TEST(SHMPubSubTest, ExtendedErrorHandlingTest)
{
  // Test multiple publishers to same topic
  {
    irlab::shm::Publisher<SimpleInt> pub1("/test_multi_pub");
    irlab::shm::Publisher<SimpleInt> pub2("/test_multi_pub");
    
    SimpleInt data1(10);
    SimpleInt data2(20);
    
    pub1.publish(data1);
    pub2.publish(data2); // Should overwrite data1
    
    irlab::shm::Subscriber<SimpleInt> sub("/test_multi_pub");
    bool is_successed = false;
    SimpleInt result = sub.subscribe(&is_successed);
    
    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.value, 20); // Should get the latest data
    
    irlab::shm::disconnectMemory("test_multi_pub");
  }

  // Test multiple subscribers to same topic
  {
    irlab::shm::Publisher<SimpleInt> pub("/test_multi_sub");
    irlab::shm::Subscriber<SimpleInt> sub1("/test_multi_sub");
    irlab::shm::Subscriber<SimpleInt> sub2("/test_multi_sub");
    
    SimpleInt test_data(100);
    pub.publish(test_data);
    
    bool is_successed1 = false, is_successed2 = false;
    SimpleInt result1 = sub1.subscribe(&is_successed1);
    SimpleInt result2 = sub2.subscribe(&is_successed2);
    
    EXPECT_EQ(is_successed1, true);
    EXPECT_EQ(is_successed2, true);
    EXPECT_EQ(result1.value, 100);
    EXPECT_EQ(result2.value, 100);
    
    irlab::shm::disconnectMemory("test_multi_sub");
  }

  // Test with very long topic names
  {
    std::string long_name = "/test_" + std::string(100, 'a');
    
    EXPECT_NO_THROW({
      irlab::shm::Publisher<SimpleInt> pub(long_name);
      irlab::shm::Subscriber<SimpleInt> sub(long_name);
      
      SimpleInt test_data(99);
      pub.publish(test_data);
      
      bool is_successed = false;
      SimpleInt result = sub.subscribe(&is_successed);
      
      EXPECT_EQ(is_successed, true);
      EXPECT_EQ(result.value, 99);
    });
    
    irlab::shm::disconnectMemory(long_name.substr(1)); // Remove leading '/'
  }
}

TEST(SHMPubSubTest, VectorMultiThreadTest)
{
  constexpr int NUM_MESSAGES = 50;
  constexpr int VECTOR_SIZE = 10;
  std::vector<bool> received(NUM_MESSAGES, false);
  std::atomic<int> message_count(0);

  // Publisher thread for vectors
  std::thread publisher_thread([&]() {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_vector_multithread");
    
    for (int i = 0; i < NUM_MESSAGES; ++i) {
      std::vector<SimpleInt> data;
      for (int j = 0; j < VECTOR_SIZE; ++j) {
        data.push_back(SimpleInt(i * VECTOR_SIZE + j));
      }
      pub.publish(data);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });

  // Subscriber thread for vectors
  std::thread subscriber_thread([&]() {
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_vector_multithread");
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(3);

    while (message_count.load() < NUM_MESSAGES) 
    {
      auto current_time = std::chrono::steady_clock::now();
      if (current_time - start_time > timeout)
      {
        break; // Exit if timeout reached
      }

      bool is_successed = false;
      std::vector<SimpleInt> result = sub.subscribe(&is_successed);
      
      if (is_successed && result.size() == VECTOR_SIZE)
      {
        // Extract message ID from first element
        int msg_id = result[0].value / VECTOR_SIZE;
        if (msg_id >= 0 && msg_id < NUM_MESSAGES && !received[msg_id]) 
        {
          // Verify vector contents
          bool valid_vector = true;
          for (int j = 0; j < VECTOR_SIZE; ++j)
          {
            if (result[j].value != msg_id * VECTOR_SIZE + j)
            {
              valid_vector = false;
              break;
            }
          }
          if (valid_vector)
          {
            received[msg_id] = true;
            message_count.fetch_add(1);
          }
        }
      }
      
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  publisher_thread.join();
  subscriber_thread.join();

  // Verify messages were received
  int received_count = 0;
  for (bool r : received) {
    if (r) received_count++;
  }
  
  EXPECT_GT(received_count, NUM_MESSAGES * 0.7); // Allow some message loss in concurrent scenario
  irlab::shm::disconnectMemory("test_vector_multithread");
}

TEST(SHMPubSubTest, VectorTimeoutTest)
{
  irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_vector_timeout");
  irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_vector_timeout");

  // Test timeout without any data
  auto start_time = std::chrono::steady_clock::now();
  bool result = sub.waitFor(50000); // 50ms timeout
  auto end_time = std::chrono::steady_clock::now();
  
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  EXPECT_EQ(result, false);
  EXPECT_GE(duration.count(), 30); // Allow tolerance for timing variations
  EXPECT_LE(duration.count(), 100);

  // Test with vector data available
  std::vector<SimpleInt> test_data = { SimpleInt(10), SimpleInt(20), SimpleInt(30) };
  pub.publish(test_data);
  
  start_time = std::chrono::steady_clock::now();
  result = sub.waitFor(50000);
  end_time = std::chrono::steady_clock::now();
  
  duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  EXPECT_EQ(result, true);
  EXPECT_LT(duration.count(), 50); // Should return quickly when data is available

  irlab::shm::disconnectMemory("test_vector_timeout");
}

TEST(SHMPubSubTest, VectorErrorHandlingTest)
{
  // Test multiple publishers to same vector topic
  {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub1("/test_vector_multi_pub");
    irlab::shm::Publisher<std::vector<SimpleInt>> pub2("/test_vector_multi_pub");
    
    std::vector<SimpleInt> data1 = { SimpleInt(1), SimpleInt(2) };
    std::vector<SimpleInt> data2 = { SimpleInt(10), SimpleInt(20), SimpleInt(30) };
    
    pub1.publish(data1);
    pub2.publish(data2); // Should overwrite data1
    
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_vector_multi_pub");
    bool is_successed = false;
    std::vector<SimpleInt> result = sub.subscribe(&is_successed);
    
    EXPECT_EQ(is_successed, true);
    EXPECT_EQ(result.size(), 3); // Should get the latest data (data2)
    EXPECT_EQ(result[0].value, 10);
    EXPECT_EQ(result[1].value, 20);
    EXPECT_EQ(result[2].value, 30);
    
    irlab::shm::disconnectMemory("test_vector_multi_pub");
  }

  // Test multiple subscribers to same vector topic
  {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_vector_multi_sub");
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub1("/test_vector_multi_sub");
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub2("/test_vector_multi_sub");
    
    std::vector<SimpleInt> test_data = { SimpleInt(100), SimpleInt(200), SimpleInt(300), SimpleInt(400) };
    pub.publish(test_data);
    
    bool is_successed1 = false, is_successed2 = false;
    std::vector<SimpleInt> result1 = sub1.subscribe(&is_successed1);
    std::vector<SimpleInt> result2 = sub2.subscribe(&is_successed2);
    
    EXPECT_EQ(is_successed1, true);
    EXPECT_EQ(is_successed2, true);
    EXPECT_EQ(result1.size(), 4);
    EXPECT_EQ(result2.size(), 4);
    
    for (size_t i = 0; i < test_data.size(); ++i) {
      EXPECT_EQ(result1[i], test_data[i]);
      EXPECT_EQ(result2[i], test_data[i]);
    }
    
    irlab::shm::disconnectMemory("test_vector_multi_sub");
  }

  // Test rapid vector size changes
  {
    irlab::shm::Publisher<std::vector<SimpleInt>> pub("/test_vector_rapid_change");
    irlab::shm::Subscriber<std::vector<SimpleInt>> sub("/test_vector_rapid_change");
    
    // Publish vectors of rapidly changing sizes
    for (int size = 1; size <= 10; ++size) {
      std::vector<SimpleInt> data;
      for (int i = 0; i < size; ++i) {
        data.push_back(SimpleInt(size * 10 + i));
      }
      pub.publish(data);
      
      bool is_successed = false;
      std::vector<SimpleInt> result = sub.subscribe(&is_successed);
      
      EXPECT_EQ(is_successed, true);
      EXPECT_EQ(result.size(), static_cast<size_t>(size));
      
      // Verify content matches expected pattern
      for (int i = 0; i < size; ++i) {
        EXPECT_EQ(result[i].value, size * 10 + i);
      }
    }
    
    irlab::shm::disconnectMemory("test_vector_rapid_change");
  }
}
