//!
//! @file shm_ps_python.cpp
//! @brief メモリの格納方法を規定するクラスの実装
//! @note 記法はROSに準拠する
//!       http://wiki.ros.org/ja/CppStyleGuide
//! 
//!

#include <iostream>
#include <boost/python.hpp>

#include "shm_pub_sub.hpp"

class PublisherBool : irlab::shm::Publisher<bool>
{
public:
  PublisherBool(std::string name = "", bool arg = false, int buffer_num = 3)
  : Publisher<bool>(name, buffer_num)
  {};

  // ! pythonでは参照渡しができないための変換関数
  void _publish(bool data)
  {
    publish(data);
  };
};

class PublisherInt : irlab::shm::Publisher<int>
{
public:
  PublisherInt(std::string name = "", int arg = 0, int buffer_num = 3)
  : Publisher<int>(name, buffer_num)
  {};

  // ! pythonでは参照渡しができないための変換関数
  void _publish(int data)
  {
    publish(data);
  };
};

class PublisherFloat : irlab::shm::Publisher<float>
{
public:
  PublisherFloat(std::string name = "", float arg = 0.0f, int buffer_num = 3)
  : Publisher<float>(name, buffer_num)
  {};

  // ! pythonでは参照渡しができないための変換関数
  void _publish(float data)
  {
    publish(data);
  };
};

class SubscriberBool : irlab::shm::Subscriber<bool>
{
public:
  SubscriberBool(std::string name = "", bool arg = false)
  : Subscriber<bool>(name)
  {};
  
  boost::python::tuple _subscribe()
  {
    bool is_success;
    bool result = subscribe(&is_success);
    return boost::python::make_tuple(result, is_success);
  };
};

class SubscriberInt : irlab::shm::Subscriber<int>
{
public:
  SubscriberInt(std::string name = "", int arg = 0)
  : Subscriber<int>(name)
  {};
  
  boost::python::tuple _subscribe()
  {
    bool is_success;
    int result = subscribe(&is_success);
    return boost::python::make_tuple(result, is_success);
  };
};

class SubscriberFloat : irlab::shm::Subscriber<float>
{
public:
  SubscriberFloat(std::string name = "", float arg = 0.0f)
  : Subscriber<float>(name)
  {};
  
  boost::python::tuple _subscribe()
  {
    bool is_success;
    float result = subscribe(&is_success);
    return boost::python::make_tuple(result, is_success);
  };
};

BOOST_PYTHON_MODULE(shm_pub_sub) {
  boost::python::class_<PublisherBool>("Publisher")
    .def(boost::python::init<std::string, bool, int>())
    .def("publish",  &PublisherInt::_publish)
  ;
  boost::python::class_<PublisherInt>("Publisher")
    .def(boost::python::init<std::string, int, int>())
    .def("publish",  &PublisherInt::_publish)
  ;
  boost::python::class_<PublisherFloat>("Publisher")
    .def(boost::python::init<std::string, float, int>())
    .def("publish", &PublisherFloat::_publish)
  ;

  boost::python::class_<SubscriberBool>("Subscriber")
    .def(boost::python::init<std::string, bool>())
    .def("subscribe", &SubscriberBool::_subscribe)
  ;
  boost::python::class_<SubscriberInt>("Subscriber")
    .def(boost::python::init<std::string, int>())
    .def("subscribe", &SubscriberInt::_subscribe)
  ;
  boost::python::class_<SubscriberFloat>("Subscriber")
    .def(boost::python::init<std::string, float>())
    .def("subscribe", &SubscriberFloat::_subscribe)
  ;
}

