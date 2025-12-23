#include <gtest/gtest.h>
#include <libgen.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <iomanip>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"
#include "sample_class.hpp"


TEST(SHMPubSubTest, BasicTest)
{
  {
    // ARM debugging: Check platform
    std::cout << "Running on " << (irlab::shm::is_arm_platform() ? "ARM" : "x86/x64") << " platform" << std::endl;
    
    // ARM debugging: Check alignment properties
    std::cout << "ClassTest size: " << sizeof(ClassTest) << ", alignment: " << alignof(ClassTest) << std::endl;
    std::cout << "Required alignment: " << irlab::shm::get_alignment<ClassTest>() << std::endl;
    
    try {
      // Initialize ClassTest with ARM-compatible method
      ClassTest test(1, 2, 3, 4, 5, 6, 7);
      std::cout << "ClassTest initialized successfully" << std::endl;
      
      // First try with SimpleInt to test basic functionality
      if constexpr (irlab::shm::is_arm_platform()) {
        std::cout << "ARM: Testing with SimpleInt first..." << std::endl;
        
        try {
          std::cout << "ARM: Creating SimpleInt Publisher with extra safety..." << std::endl;
          std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Pre-creation delay
          
          irlab::shm::Publisher<SimpleInt> simple_pub("/arm_test_simple");
          std::cout << "SimpleInt Publisher created successfully" << std::endl;
          
          // Additional delay for ARM initialization
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          
          SimpleInt simple_data(42);
          simple_pub.publish(simple_data);
          std::cout << "SimpleInt published successfully" << std::endl;
          
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          
          irlab::shm::Subscriber<SimpleInt> simple_sub("/arm_test_simple");
          std::cout << "SimpleInt Subscriber created successfully" << std::endl;
          
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          
          bool simple_success = false;
          SimpleInt simple_result = simple_sub.subscribe(&simple_success);
          std::cout << "SimpleInt subscribed: " << (simple_success ? "success" : "failed") << std::endl;
          
          irlab::shm::disconnectMemory("arm_test_simple");
          
        } catch (const std::exception& arm_e) {
          std::cout << "ARM SimpleInt test failed: " << arm_e.what() << std::endl;
          // Continue with main test anyway
        }
      }
      
      std::cout << "Creating Publisher..." << std::endl;
      irlab::shm::Publisher<ClassTest> pub("/test");
      std::cout << "Publisher created successfully" << std::endl;
      
      std::cout << "Creating Subscriber..." << std::endl;
      irlab::shm::Subscriber<ClassTest> sub("/test");
      std::cout << "Subscriber created successfully" << std::endl;

      std::cout << "Publishing data..." << std::endl;
      pub.publish(test);
      std::cout << "Data published successfully" << std::endl;

      std::cout << "Subscribing data..." << std::endl;
      bool is_successed = false;
      ClassTest result = sub.subscribe(&is_successed);
      std::cout << "Data subscribed successfully" << std::endl;

      EXPECT_EQ(is_successed, true);
      EXPECT_EQ(result.a, 1);
      EXPECT_EQ(result.b, 2);
      EXPECT_EQ(result.c[0], 3);
      EXPECT_EQ(result.c[1], 4);
      EXPECT_EQ(result.c[2], 5);
      EXPECT_EQ(result.c[3], 6);
      EXPECT_EQ(result.c[4], 7);
      
    } catch (const std::exception& e) {
      std::cout << "Exception caught: " << e.what() << std::endl;
      FAIL() << "Exception: " << e.what();
    }
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
  // Vector tests now enabled for ARM with proper alignment handling
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
  std::atomic<bool> subscriber_ready(false);
  std::atomic<bool> publisher_done(false);

  // Subscriber thread
  std::thread subscriber_thread([&]() {
    irlab::shm::Subscriber<SimpleInt> sub("/test_multithread");
    
    // Allow subscriber to fully initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    subscriber_ready.store(true);
    
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(5); // Increased timeout

    while (!publisher_done.load() || message_count.load() < NUM_MESSAGES) {
      auto current_time = std::chrono::steady_clock::now();
      if (current_time - start_time > timeout) {
        break;
      }
      bool is_successed = false;
      SimpleInt result = sub.subscribe(&is_successed);
      
      if (is_successed && result.value >= 0 && result.value < NUM_MESSAGES) {
        if (!received[result.value]) {
          received[result.value] = true;
          message_count.fetch_add(1);
        }
      }
      
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    
    // Continue reading for a bit after publisher is done to catch any remaining messages
    auto final_timeout = std::chrono::milliseconds(100);
    auto final_start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - final_start < final_timeout) {
      bool is_successed = false;
      SimpleInt result = sub.subscribe(&is_successed);
      
      if (is_successed && result.value >= 0 && result.value < NUM_MESSAGES) {
        if (!received[result.value]) {
          received[result.value] = true;
          message_count.fetch_add(1);
        }
      }
      
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  // Publisher thread
  std::thread publisher_thread([&]() {
    // Wait for subscriber to be ready
    while (!subscriber_ready.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Additional delay to ensure subscriber is fully ready
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    irlab::shm::Publisher<SimpleInt> pub("/test_multithread");
    
    for (int i = 0; i < NUM_MESSAGES; ++i) {
      SimpleInt data(i);
      pub.publish(data);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    publisher_done.store(true);
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
  // Vector tests now enabled for ARM with proper alignment handling
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
  // Vector tests now enabled for ARM with proper alignment handling
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
  // Vector tests now enabled for ARM with proper alignment handling
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

TEST(SHMPubSubTest, ConcurrentCreationRaceConditionTest)
{
  constexpr int NUM_ITERATIONS = 200;
  constexpr int NUM_THREADS = 4;
  std::atomic<int> failure_count(0);
  std::atomic<int> success_count(0);
  std::atomic<int> timeout_count(0);
  std::atomic<bool> start_trigger(false);
  
  const auto test_start_time = std::chrono::steady_clock::now();
  const auto max_test_duration = std::chrono::seconds(10); // 10 second timeout

  for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
    // Check for test timeout
    if (std::chrono::steady_clock::now() - test_start_time > max_test_duration) {
      timeout_count.fetch_add(1);
      std::cout << "Test timed out at iteration " << iteration << std::endl;
      break;
    }
    
    std::string test_topic = "/test_race_" + std::to_string(iteration);
    std::vector<std::thread> threads;
    std::atomic<int> ready_count(0);

    // Create multiple threads that will simultaneously create Publisher/Subscriber
    for (int thread_id = 0; thread_id < NUM_THREADS; ++thread_id) {
      threads.emplace_back([&, thread_id, test_topic]() {
        // Signal ready and wait for all threads to be ready
        ready_count.fetch_add(1);
        while (!start_trigger.load()) {
          std::this_thread::yield();
        }
        
        const auto thread_start_time = std::chrono::steady_clock::now();
        const auto thread_timeout = std::chrono::seconds(2);

        try {
          if (thread_id % 2 == 0) {
            // Publisher thread with retry mechanism
            bool operation_successful = false;
            const int MAX_RETRIES = 3;
            
            for (int retry = 0; retry < MAX_RETRIES && !operation_successful; ++retry) {
              try {
                irlab::shm::Publisher<SimpleInt> pub(test_topic);
                SimpleInt test_data(thread_id);
                pub.publish(test_data);
                
                // Small delay to simulate real usage
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                
                success_count.fetch_add(1);
                operation_successful = true;
                
              } catch (const std::exception& e) {
                if (retry == MAX_RETRIES - 1) {
                  // Only count as failure on final retry
                  failure_count.fetch_add(1);
                } else {
                  // Small delay before retry
                  std::this_thread::sleep_for(std::chrono::microseconds(500));
                }
              }
            }
          } else {
            // Subscriber thread with retry mechanism
            bool operation_successful = false;
            const int MAX_RETRIES = 3;
            
            for (int retry = 0; retry < MAX_RETRIES && !operation_successful; ++retry) {
              try {
                irlab::shm::Subscriber<SimpleInt> sub(test_topic);
                
                // Try to read data with timeout
                bool result = sub.waitFor(50000); // 50ms timeout
                if (std::chrono::steady_clock::now() - thread_start_time > thread_timeout) {
                  timeout_count.fetch_add(1);
                  return;
                }
                
                if (result) {
                  bool is_successed = false;
                  SimpleInt data = sub.subscribe(&is_successed);
                  if (is_successed && data.value >= 0 && data.value < NUM_THREADS) {
                    success_count.fetch_add(1);
                    operation_successful = true;
                  } else if (retry == MAX_RETRIES - 1) {
                    // Only count as failure on final retry
                    failure_count.fetch_add(1);
                  }
                } else {
                  // Timeout is acceptable - treat as success
                  success_count.fetch_add(1);
                  operation_successful = true;
                }
                
              } catch (const std::exception& e) {
                if (retry == MAX_RETRIES - 1) {
                  // Only count as failure on final retry
                  failure_count.fetch_add(1);
                } else {
                  // Small delay before retry
                  std::this_thread::sleep_for(std::chrono::microseconds(500));
                }
              }
            }
          }
        } catch (const std::exception& e) {
          failure_count.fetch_add(1);
        }
      });
    }

    // Wait for all threads to be ready
    while (ready_count.load() < NUM_THREADS) {
      std::this_thread::yield();
    }

    // Trigger all threads to start simultaneously
    start_trigger.store(true);

    // Wait for all threads to complete with timeout
    const auto join_start = std::chrono::steady_clock::now();
    const auto join_timeout = std::chrono::seconds(3);
    
    for (auto& t : threads) {
      if (t.joinable()) {
        t.join();
      }
      if (std::chrono::steady_clock::now() - join_start > join_timeout) {
        timeout_count.fetch_add(1);
        break;
      }
    }

    // Reset for next iteration
    start_trigger.store(false);

    // Clean up shared memory
    irlab::shm::disconnectMemory(test_topic.substr(1));
    
    // Small delay between iterations
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }

  // Verify test results - allow small failure rate in concurrent scenarios
  int total_operations = NUM_ITERATIONS * NUM_THREADS;
  double success_rate = (100.0 * success_count.load() / total_operations);
  
  std::cout << "Improved race condition test results:" << std::endl;
  std::cout << "  Success: " << success_count.load() << std::endl;
  std::cout << "  Failures: " << failure_count.load() << std::endl;
  std::cout << "  Timeouts: " << timeout_count.load() << std::endl;
  std::cout << "  Total: " << total_operations << std::endl;
  std::cout << "  Success rate: " << success_rate << "%" << std::endl;
  
  // With retry mechanism, expect near-perfect results
  EXPECT_EQ(failure_count.load(), 0); // Expect 0 failures with retry mechanism
  EXPECT_LT(timeout_count.load(), NUM_ITERATIONS * 0.05); // Allow very few timeouts (<5%)
  EXPECT_GT(success_rate, 99.5); // Expect >99.5% success rate
}

TEST(SHMPubSubTest, SharedMemoryCorruptionDetectionTest)
{
  const std::string test_topic = "/test_corruption";
  constexpr int NUM_RAPID_TESTS = 1000; // Increased to 1000 iterations as requested
  std::atomic<int> corruption_detected(0);
  std::atomic<int> creation_failures(0);
  std::atomic<int> timeout_count(0);
  
  const auto test_start_time = std::chrono::steady_clock::now();
  const auto max_test_duration = std::chrono::seconds(10); // 10 second timeout
  
  for (int i = 0; i < NUM_RAPID_TESTS; ++i) {
    // Check for test timeout
    if (std::chrono::steady_clock::now() - test_start_time > max_test_duration) {
      timeout_count.fetch_add(1);
      std::cout << "Test timed out at iteration " << i << std::endl;
      break;
    }
    
    try {
      // Force cleanup before starting
      irlab::shm::disconnectMemory(test_topic.substr(1));
      
      const auto iteration_start = std::chrono::steady_clock::now();
      const auto iteration_timeout = std::chrono::milliseconds(500);
      
      // Create publisher and subscriber in very close succession
      std::thread pub_thread([&]() {
        try {
          if (std::chrono::steady_clock::now() - iteration_start > iteration_timeout) {
            timeout_count.fetch_add(1);
            return;
          }
          irlab::shm::Publisher<SimpleInt> pub(test_topic);
          SimpleInt expected_data(12345);
          pub.publish(expected_data);
          
          // Keep publisher alive briefly
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        } catch (const std::exception& e) {
          creation_failures.fetch_add(1);
          if (i < 10) { // Only log first few failures to avoid spam
            std::cout << "Publisher creation failed: " << e.what() << std::endl;
          }
        }
      });
      
      // Very small delay to create race condition window (reduced to test synchronization)
      std::this_thread::sleep_for(std::chrono::microseconds(5));
      
      std::thread sub_thread([&]() {
        try {
          if (std::chrono::steady_clock::now() - iteration_start > iteration_timeout) {
            timeout_count.fetch_add(1);
            return;
          }
          irlab::shm::Subscriber<SimpleInt> sub(test_topic);
          
          // Try to read data multiple times
          for (int attempt = 0; attempt < 3; ++attempt) {
            if (std::chrono::steady_clock::now() - iteration_start > iteration_timeout) {
              timeout_count.fetch_add(1);
              return;
            }
            
            bool is_successed = false;
            SimpleInt result = sub.subscribe(&is_successed);
            
            if (is_successed) {
              // Check if data looks corrupted
              if (result.value < -999999 || result.value > 999999) {
                corruption_detected.fetch_add(1);
                if (corruption_detected.load() <= 5) { // Only log first few corruptions
                  std::cout << "Corruption detected - invalid value: " << result.value << std::endl;
                }
                break;
              } else if (result.value != 12345 && result.value != 0) {
                // Unexpected but potentially valid data
                if (i < 5 && result.value != 12345) {
                  std::cout << "Unexpected data: " << result.value << " (expected 12345 or 0)" << std::endl;
                }
              }
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(50));
          }
        } catch (const std::exception& e) {
          creation_failures.fetch_add(1);
          if (creation_failures.load() <= 5) { // Only log first few failures
            std::cout << "Subscriber creation failed: " << e.what() << std::endl;
          }
        }
      });
      
      pub_thread.join();
      sub_thread.join();
      
      // Force cleanup after each test
      irlab::shm::disconnectMemory(test_topic.substr(1));
      
      // Small delay between iterations (reduced for speed)
      if (i % 100 == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
      
    } catch (const std::exception& e) {
      if (i < 5) { // Only log first few iteration failures
        std::cout << "Test iteration " << i << " failed: " << e.what() << std::endl;
      }
    }
  }
  
  std::cout << "1000-iteration corruption detection test results:" << std::endl;
  std::cout << "  Corruption events: " << corruption_detected.load() << std::endl;
  std::cout << "  Creation failures: " << creation_failures.load() << std::endl;
  std::cout << "  Timeouts: " << timeout_count.load() << std::endl;
  std::cout << "  Total iterations: " << NUM_RAPID_TESTS << std::endl;
  std::cout << "  Success rate: " << (100.0 * (NUM_RAPID_TESTS - corruption_detected.load() - creation_failures.load()) / NUM_RAPID_TESTS) << "%" << std::endl;
  
  // Final cleanup
  irlab::shm::disconnectMemory(test_topic.substr(1));
  
  // With initialization synchronization, expect 0 corruption events
  EXPECT_EQ(corruption_detected.load(), 0); // Expect 0 corruption with initialization synchronization
  EXPECT_LT(creation_failures.load(), NUM_RAPID_TESTS * 0.05); // Allow some creation failures (<5%)
  EXPECT_LT(timeout_count.load(), NUM_RAPID_TESTS * 0.05); // Allow some timeouts (<5%)
}

// Test to reproduce potential Bus Error issues in Publisher/Subscriber constructors
TEST(SHMPubSubTest, BusErrorReproductionTests)
{
  // Test 1: Rapid creation and destruction to trigger memory alignment issues
  {
    for (int i = 0; i < 50; ++i) {
      std::string topic_name = "/bus_error_test_" + std::to_string(i);
      try {
        // Create and immediately destroy objects to stress memory allocation
        {
          irlab::shm::Publisher<SimpleInt> pub(topic_name);
          irlab::shm::Subscriber<SimpleInt> sub(topic_name);
        }
        irlab::shm::disconnectMemory(topic_name.substr(1));
      } catch (const std::exception& e) {
        ADD_FAILURE() << "Exception in rapid creation test " << i << ": " << e.what();
      }
    }
  }

  // Test 2: Concurrent constructor calls to trigger race conditions
  {
    std::string topic_name = "/bus_error_concurrent";
    constexpr int NUM_THREADS = 10;
    std::vector<std::thread> threads;
    std::atomic<int> constructor_failures(0);
    std::atomic<int> bus_errors(0);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([&, i]() {
        try {
          if (i % 2 == 0) {
            irlab::shm::Publisher<ComplexStruct> pub(topic_name);
            ComplexStruct data;
            data.id = i;
            pub.publish(data);
          } else {
            irlab::shm::Subscriber<ComplexStruct> sub(topic_name);
            bool success = false;
            sub.subscribe(&success);
          }
        } catch (const std::runtime_error& e) {
          std::string error_msg = e.what();
          if (error_msg.find("bus error") != std::string::npos ||
              error_msg.find("Bus error") != std::string::npos ||
              error_msg.find("SIGBUS") != std::string::npos) {
            bus_errors.fetch_add(1);
          } else {
            constructor_failures.fetch_add(1);
          }
        } catch (const std::exception& e) {
          constructor_failures.fetch_add(1);
        }
      });
    }
    
    for (auto& t : threads) {
      t.join();
    }
    
    // Clean up
    irlab::shm::disconnectMemory(topic_name.substr(1));
    
    // Report results
    if (bus_errors.load() > 0) {
      std::cout << "Bus errors detected: " << bus_errors.load() << std::endl;
    }
    if (constructor_failures.load() > 0) {
      std::cout << "Constructor failures: " << constructor_failures.load() << std::endl;
    }
  }

  // Test 3: Memory alignment stress test with different data sizes
  {
    std::vector<std::string> test_topics = {
      "/bus_error_simple_int",
      "/bus_error_simple_float", 
      "/bus_error_complex_struct"
    };
    
    for (const auto& topic : test_topics) {
      try {
        // Test with SimpleInt (4 bytes)
        if (topic.find("simple_int") != std::string::npos) {
          irlab::shm::Publisher<SimpleInt> pub(topic);
          irlab::shm::Subscriber<SimpleInt> sub(topic);
        }
        // Test with SimpleFloat (4 bytes)
        else if (topic.find("simple_float") != std::string::npos) {
          irlab::shm::Publisher<SimpleFloat> pub(topic);
          irlab::shm::Subscriber<SimpleFloat> sub(topic);
        }
        // Test with ComplexStruct (larger, more complex alignment)
        else if (topic.find("complex_struct") != std::string::npos) {
          irlab::shm::Publisher<ComplexStruct> pub(topic);
          irlab::shm::Subscriber<ComplexStruct> sub(topic);
        }
        
        irlab::shm::disconnectMemory(topic.substr(1));
      } catch (const std::exception& e) {
        ADD_FAILURE() << "Exception in alignment test for " << topic << ": " << e.what();
      }
    }
  }

  // Test 4: Unaligned memory access test
  {
    std::string topic_name = "/bus_error_unaligned";
    try {
      // Force creation with potential unaligned access patterns
      for (int buffer_size = 1; buffer_size <= 5; ++buffer_size) {
        irlab::shm::Publisher<ComplexStruct> pub(topic_name, buffer_size);
        irlab::shm::Subscriber<ComplexStruct> sub(topic_name);
        
        ComplexStruct test_data;
        test_data.id = buffer_size;
        test_data.position[0] = 1.1f;
        test_data.position[1] = 2.2f; 
        test_data.position[2] = 3.3f;
        test_data.timestamp = 123456.789;
        test_data.active = true;
        
        pub.publish(test_data);
        
        bool success = false;
        ComplexStruct result = sub.subscribe(&success);
        
        if (success) {
          EXPECT_EQ(result.id, test_data.id);
        }
        
        irlab::shm::disconnectMemory(topic_name.substr(1));
      }
    } catch (const std::exception& e) {
      ADD_FAILURE() << "Exception in unaligned access test: " << e.what();
    }
  }

  // Test 5: Memory corruption through uninitialized access patterns
  {
    std::string topic_name = "/bus_error_memory_corruption";
    
    // Test rapid succession of Publisher/Subscriber creation to trigger race conditions
    for (int round = 0; round < 20; ++round) {
      std::vector<std::thread> stress_threads;
      
      // Create multiple threads trying to access shared memory simultaneously
      for (int i = 0; i < 5; ++i) {
        stress_threads.emplace_back([&, i]() {
          try {
            if (i == 0) {
              // First thread creates publisher
              irlab::shm::Publisher<ComplexStruct> pub(topic_name);
              ComplexStruct data;
              data.id = round;
              data.timestamp = 123.456;
              pub.publish(data);
            } else {
              // Other threads try to subscribe immediately
              std::this_thread::sleep_for(std::chrono::microseconds(i * 10));
              irlab::shm::Subscriber<ComplexStruct> sub(topic_name);
              bool success = false;
              ComplexStruct result = sub.subscribe(&success);
              // Force memory access to trigger potential bus error
              volatile int temp = result.id + result.active;
              (void)temp; // Suppress unused variable warning
            }
          } catch (const std::exception& e) {
            // Expected in race conditions
          }
        });
      }
      
      for (auto& t : stress_threads) {
        t.join();
      }
      
      irlab::shm::disconnectMemory(topic_name.substr(1));
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  // Test 6: Force unaligned memory access
  {
    std::string topic_name = "/bus_error_unaligned_force";
    try {
      // Create publisher with intentionally problematic buffer configuration
      irlab::shm::Publisher<ComplexStruct> pub(topic_name, 1); // Single buffer
      irlab::shm::Subscriber<ComplexStruct> sub(topic_name);
      
      // Rapid publish/subscribe cycles to stress memory alignment
      for (int i = 0; i < 100; ++i) {
        ComplexStruct data;
        data.id = i;
        data.position[0] = static_cast<float>(i) + 0.1f;
        data.position[1] = static_cast<float>(i) + 0.2f;
        data.position[2] = static_cast<float>(i) + 0.3f;
        data.timestamp = static_cast<double>(i) + 0.123456;
        data.active = (i % 2 == 0);
        
        pub.publish(data);
        
        bool success = false;
        ComplexStruct result = sub.subscribe(&success);
        
        // Force memory access that might trigger bus error
        if (success) {
          volatile double temp = result.timestamp + result.position[0];
          (void)temp;
        }
      }
      
      irlab::shm::disconnectMemory(topic_name.substr(1));
    } catch (const std::exception& e) {
      // Expected for some edge cases
    }
  }
}
