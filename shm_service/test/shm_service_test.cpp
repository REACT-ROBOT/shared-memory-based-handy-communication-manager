#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

#include "shm_base.hpp"
#include "shm_service.hpp"

// Test data structures
class SimpleInt
{
public:
    SimpleInt() : value(0) {}
    SimpleInt(int v) : value(v) {}
    bool operator==(const SimpleInt &other) const { return value == other.value; }

    int value;
};

class SimpleFloat
{
public:
    SimpleFloat() : value(0.0f) {}
    SimpleFloat(float v) : value(v) {}
    bool operator==(const SimpleFloat &other) const { return std::abs(value - other.value) < 1e-6; }

    float value;
};

class ClassTest
{
public:
    ClassTest() : a(0), b(0)
    {
        for (int i = 0; i < 5; i++)
        {
            c[i] = 0;
        }
    }

    ClassTest(const ClassTest &test) : a(test.a), b(test.b)
    {
        for (int i = 0; i < 5; i++)
        {
            c[i] = test.c[i];
        }
    }

    ClassTest &operator=(const ClassTest &test)
    {
        a = test.a;
        b = test.b;
        for (int i = 0; i < 5; i++)
        {
            c[i] = test.c[i];
        }
        return *this;
    }

    bool operator==(const ClassTest &other) const
    {
        if (a != other.a || b != other.b)
            return false;
        for (int i = 0; i < 5; i++)
        {
            if (c[i] != other.c[i])
                return false;
        }
        return true;
    }

    int a;
    int b;
    int c[5];
};

class BadClass
{
public:
    BadClass() {}
    virtual ~BadClass() {}
    virtual void someMethod() = 0;
};

// Non-POD class for testing (has virtual functions)
class NonPODClass
{
public:
    NonPODClass() : value(0) {}
    virtual ~NonPODClass() {}
    virtual void someMethod() {}

    int value;
};

// Service callback functions
int addOneService(int request)
{
    return request + 1;
}

SimpleInt doubleService(SimpleInt request)
{
    return SimpleInt(request.value * 2);
}

ClassTest processClassTest(ClassTest request)
{
    ClassTest response;
    response.a = request.a * 2;
    response.b = request.b + 10;
    for (int i = 0; i < 5; i++)
    {
        response.c[i] = request.c[i] * 3;
    }
    return response;
}

float divideService(float request)
{
    return request / 2.0f;
}

// Test fixture for ServiceServer and ServiceClient
class SHMServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Clean up any existing shared memory
        irlab::shm::disconnectMemory("test_service");
        irlab::shm::disconnectMemory("test_int_service");
        irlab::shm::disconnectMemory("test_float_service");
        irlab::shm::disconnectMemory("test_class_service");
    }

    void TearDown() override
    {
        // Clean up shared memory after each test
        irlab::shm::disconnectMemory("test_service");
        irlab::shm::disconnectMemory("test_int_service");
        irlab::shm::disconnectMemory("test_float_service");
        irlab::shm::disconnectMemory("test_class_service");
        irlab::shm::disconnectMemory("test_basic_service");
        irlab::shm::disconnectMemory("test_simple_int_service");
        irlab::shm::disconnectMemory("test_class_test_service");
        irlab::shm::disconnectMemory("test_float_service_unique");
        irlab::shm::disconnectMemory("test_rapid_requests_service");
        irlab::shm::disconnectMemory("test_reconnection_service");
        irlab::shm::disconnectMemory("test_large_data_service");
        irlab::shm::disconnectMemory("test_performance_service");

        // Additional cleanup - wait a bit to ensure cleanup is complete
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

// Basic functionality tests
TEST_F(SHMServiceTest, BasicServiceCallTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    // Create server in a separate thread
    std::thread server_thread([&]()
                              {
        try {
            irlab::shm::ServiceServer<int, int> server("/test_basic_service", addOneService);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            // Handle any exceptions to prevent thread termination issues
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

    // Wait for server to be ready
    while (!server_ready.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Additional delay to ensure server is fully initialized
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create client and make requests
    irlab::shm::ServiceClient<int, int> client("/test_basic_service");

    for (int i = 0; i < 5; i++)
    {
        int response;
        bool success = client.call(i, &response);

        EXPECT_TRUE(success);
        EXPECT_EQ(response, i + 1);
    }

    // Signal server to stop and wait for thread to finish
    test_done.store(true);
    if (server_thread.joinable())
    {
        server_thread.join();
    }
}

TEST_F(SHMServiceTest, SimpleIntServiceTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    std::thread server_thread([&]()
                              {
        try {
            irlab::shm::ServiceServer<SimpleInt, SimpleInt> server("/test_int_service", doubleService);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

    // Wait for server to be ready
    while (!server_ready.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    irlab::shm::ServiceClient<SimpleInt, SimpleInt> client("/test_int_service");

    SimpleInt request(10);
    SimpleInt response;
    bool success = client.call(request, &response);

    EXPECT_TRUE(success);
    EXPECT_EQ(response.value, 20);

    // Signal server to stop and wait for thread to finish
    test_done.store(true);
    if (server_thread.joinable())
    {
        server_thread.join();
    }
}

TEST_F(SHMServiceTest, ClassTestServiceTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    std::thread server_thread([&]()
                              {
        try {
            irlab::shm::ServiceServer<ClassTest, ClassTest> server("/test_class_service", processClassTest);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Wait for server to be ready
    while (!server_ready.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    irlab::shm::ServiceClient<ClassTest, ClassTest> client("/test_class_service");

    ClassTest request;
    request.a = 5;
    request.b = 10;
    for (int i = 0; i < 5; i++)
    {
        request.c[i] = i + 1;
    }

    ClassTest response;
    bool success = client.call(request, &response);

    EXPECT_TRUE(success);
    EXPECT_EQ(response.a, 10);
    EXPECT_EQ(response.b, 20);
    for (int i = 0; i < 5; i++)
    {
        EXPECT_EQ(response.c[i], (i + 1) * 3);
    }

    // Signal server to stop and wait for thread to finish
    test_done.store(true);
    if (server_thread.joinable())
    {
        server_thread.join();
    }
}

TEST_F(SHMServiceTest, FloatServiceTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    std::thread server_thread([&]()
                              {
        try {
            irlab::shm::ServiceServer<float, float> server("/test_float_service", divideService);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Wait for server to be ready
    while (!server_ready.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    irlab::shm::ServiceClient<float, float> client("/test_float_service");

    float request = 10.0f;
    float response;
    bool success = client.call(request, &response);

    EXPECT_TRUE(success);
    EXPECT_FLOAT_EQ(response, 5.0f);

    // Signal server to stop and wait for thread to finish
    test_done.store(true);
    if (server_thread.joinable())
    {
        server_thread.join();
    }
}

// Dummy service function for NonPODClass (won't be called due to constructor exception)
int nonPODClassService(NonPODClass req)
{
    return req.value;
}

// Error handling tests
TEST_F(SHMServiceTest, NonPODClassErrorTest)
{
    // Test ServiceServer constructor with non-POD class
    try
    {
        irlab::shm::ServiceServer<NonPODClass, int> server("/test_simple_int_service", nonPODClassService);
        FAIL() << "Expected std::runtime_error";
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_TRUE(true);
    }

    // Test ServiceClient constructor with non-POD class
    try
    {
        irlab::shm::ServiceClient<NonPODClass, int> client("/test_class_test_service");
        FAIL() << "Expected std::runtime_error";
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_TRUE(true);
    }
}

TEST_F(SHMServiceTest, ClientCallWithoutServerTest)
{
    irlab::shm::ServiceClient<int, int> client("/nonexistent_service");

    int response;
    bool success = client.call(42, &response);

    EXPECT_FALSE(success);
}

int multiplyByTwo(int req)
{
    return req * 2;
}

TEST_F(SHMServiceTest, MultipleClientsTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    std::thread server_thread([&]()
                              {
        try {
            irlab::shm::ServiceServer<int, int> server("/test_float_service_unique", multiplyByTwo);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

    // Give server more time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Create multiple client threads with proper synchronization
    std::vector<std::thread> client_threads;
    std::vector<bool> results(3, false);

    for (int i = 0; i < 3; i++)
    {
        client_threads.emplace_back([&, i]()
                                    {
            // Add small delay to stagger client connections
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 50));
            
            irlab::shm::ServiceClient<int, int> client("/test_float_service_unique");
            int response;
            bool success = client.call(i + 1, &response);
            results[i] = success && (response == (i + 1) * 2);
            
            // Debug output
            if (!success) {
                std::cout << "Client " << i << " failed to call service" << std::endl;
            } else if (response != (i + 1) * 2) {
                std::cout << "Client " << i << " got wrong response: " << response 
                          << " expected: " << (i + 1) * 2 << std::endl;
            } });
    }

    for (auto &t : client_threads)
    {
        t.join();
    }

    // Signal server to stop and wait for thread to finish
    test_done.store(true);
    if (server_thread.joinable())
    {
        server_thread.join();
    }

    // Check that all clients succeeded
    for (int i = 0; i < 3; i++)
    {
        EXPECT_TRUE(results[i]) << "Client " << i << " failed";
    }
}

TEST_F(SHMServiceTest, RapidRequestsTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    std::thread server_thread([&]()
                              {
        try {
            irlab::shm::ServiceServer<int, int> server("/test_rapid_requests_service", addOneService);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Wait for server to be ready
    while (!server_ready.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    irlab::shm::ServiceClient<int, int> client("/test_rapid_requests_service");

    // Make rapid requests
    for (int i = 0; i < 50; i++)
    {
        int response;
        bool success = client.call(i, &response);

        EXPECT_TRUE(success);
        EXPECT_EQ(response, i + 1);
    }

    // Signal server to stop and wait for thread to finish
    test_done.store(true);
    if (server_thread.joinable())
    {
        server_thread.join();
    }
}

int tripleService(int req)
{
    return req * 3;
}

TEST_F(SHMServiceTest, ServiceReconnectionTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    // First create and destroy a service
    {
        std::thread server_thread([&]()
                                  {
        try {
            irlab::shm::ServiceServer<int, int> server("/test_reconnection_service", addOneService);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Wait for server to be ready
        while (!server_ready.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        irlab::shm::ServiceClient<int, int> client("/test_reconnection_service");
        int response;
        bool success = client.call(10, &response);
        EXPECT_TRUE(success);
        EXPECT_EQ(response, 11);

        // Signal server to stop and wait for thread to finish
        test_done.store(true);
        if (server_thread.joinable())
        {
            server_thread.join();
        }
    }

    // Now create a new service with the same name
    {
        std::thread server_thread([&]()
                                  {
            irlab::shm::ServiceServer<int, int> server("/test_service", tripleService);
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        irlab::shm::ServiceClient<int, int> client("/test_service");
        int response;
        bool success = client.call(10, &response);
        EXPECT_TRUE(success);
        EXPECT_EQ(response, 30);

        // Signal server to stop and wait for thread to finish
        test_done.store(true);
        if (server_thread.joinable())
        {
            server_thread.join();
        }
    }
}

struct LargeData
{
    int values[1000];

    LargeData()
    {
        for (int i = 0; i < 1000; i++)
        {
            values[i] = 0;
        }
    }
};

LargeData processLargeData(LargeData req)
{
    LargeData response;
    for (int i = 0; i < 1000; i++)
    {
        response.values[i] = req.values[i] * 2;
    }
    return response;
}

TEST_F(SHMServiceTest, LargeDataTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    std::thread server_thread([&]()
                              {
        try {
            irlab::shm::ServiceServer<LargeData, LargeData> server("/test_large_data_service", processLargeData);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Wait for server to be ready
    while (!server_ready.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    irlab::shm::ServiceClient<LargeData, LargeData> client("/test_large_data_service");

    LargeData request;
    for (int i = 0; i < 1000; i++)
    {
        request.values[i] = i;
    }

    LargeData response;
    bool success = client.call(request, &response);

    EXPECT_TRUE(success);
    for (int i = 0; i < 1000; i++)
    {
        EXPECT_EQ(response.values[i], i * 2);
    }

    // Signal server to stop and wait for thread to finish
    test_done.store(true);
    if (server_thread.joinable())
    {
        server_thread.join();
    }
}

// Performance test
TEST_F(SHMServiceTest, PerformanceTest)
{
    std::atomic<bool> server_ready(false);
    std::atomic<bool> test_done(false);

    std::thread server_thread([&]()
                              {
        try {
            irlab::shm::ServiceServer<int, int> server("/test_performance_service", addOneService);
            server_ready.store(true);
            
            // Keep server alive until test is done
            while (!test_done.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server thread exception: " << e.what() << std::endl;
        } });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Wait for server to be ready
    while (!server_ready.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    irlab::shm::ServiceClient<int, int> client("/test_performance_service");

    const int num_requests = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_requests; i++)
    {
        int response;
        bool success = client.call(i, &response);
        EXPECT_TRUE(success);
        EXPECT_EQ(response, i + 1);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Performance test: " << num_requests << " requests in "
              << duration.count() << " ms" << std::endl;
    std::cout << "Average time per request: "
              << (double)duration.count() / num_requests << " ms" << std::endl;

    // Signal server to stop and wait for thread to finish
    test_done.store(true);
    if (server_thread.joinable())
    {
        server_thread.join();
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}