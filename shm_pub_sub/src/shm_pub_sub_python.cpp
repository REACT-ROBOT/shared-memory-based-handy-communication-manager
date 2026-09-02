//!
//! @file shm_pub_sub_python.cpp
//! @brief メモリの格納方法を規定するクラスの実装
//! @note \~english     The notation is complianted ROS Cpp style guide.
//!       \~japanese-en 記法はROSに準拠する
//!       \~            http://wiki.ros.org/ja/CppStyleGuide
//!

#include <iostream>
#include <memory>
#include <string>
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

namespace
{

//! @brief 見本の値の型から、対応する Publisher を作る
//! @details Python 側の型で特殊化を選ぶ。**bool は int の派生なので先に見る**。
//!          対応していない型は黙って別の型として扱わず、その場で TypeError にする。
boost::python::object
makePublisher(const std::string &name, const boost::python::object &sample, int buffer_num)
{
  PyObject *o = sample.ptr();
  if (PyBool_Check(o))
  {
    return boost::python::object(std::make_shared<PublisherBool>(name, false, buffer_num));
  }
  if (PyLong_Check(o))
  {
    return boost::python::object(std::make_shared<PublisherInt>(name, 0, buffer_num));
  }
  if (PyFloat_Check(o))
  {
    return boost::python::object(std::make_shared<PublisherFloat>(name, 0.0f, buffer_num));
  }
  PyErr_SetString(PyExc_TypeError,
                  "shm_pub_sub.Publisher(): the sample value must be a bool, an int or a float. "
                  "It selects which payload type the topic carries. "
                  "For other types, write a specialization in C++ (see cv::Mat / Lidar2dScanData).");
  boost::python::throw_error_already_set();
  return boost::python::object();
}

//! @brief 見本の値の型から、対応する Subscriber を作る
boost::python::object
makeSubscriber(const std::string &name, const boost::python::object &sample)
{
  PyObject *o = sample.ptr();
  if (PyBool_Check(o))
  {
    return boost::python::object(std::make_shared<SubscriberBool>(name, false));
  }
  if (PyLong_Check(o))
  {
    return boost::python::object(std::make_shared<SubscriberInt>(name, 0));
  }
  if (PyFloat_Check(o))
  {
    return boost::python::object(std::make_shared<SubscriberFloat>(name, 0.0f));
  }
  PyErr_SetString(PyExc_TypeError,
                  "shm_pub_sub.Subscriber(): the sample value must be a bool, an int or a float. "
                  "It selects which payload type the topic carries.");
  boost::python::throw_error_already_set();
  return boost::python::object();
}

}  // namespace

// ============================================================================
// Python への公開
//
// 型ごとに**別々の名前**で登録する。以前は 3 つの C++ クラスを同じ
// "Publisher" / "Subscriber" という名前で登録していたため、最後に登録した
// float 版しかモジュールから見えなかった（R04-F16）。
// その結果 `Publisher("topic", False, 3)` は bool ではなく float のトピックを作り、
// C++ 側の Publisher<bool> / Publisher<int> のトピックは Python から
// 一切購読できなかった。
//
// 従来どおり `Publisher(name, 見本の値, buffer_num)` とも書けるよう、
// 見本の値の型で振り分けるファクトリを同じ名前で用意する。
// ============================================================================
BOOST_PYTHON_MODULE(shm_pub_sub) {
  boost::python::class_<PublisherBool, std::shared_ptr<PublisherBool>, boost::noncopyable>("PublisherBool")
    .def(boost::python::init<std::string, bool, int>())
    .def("publish",  &PublisherBool::_publish)
  ;
  boost::python::class_<PublisherInt, std::shared_ptr<PublisherInt>, boost::noncopyable>("PublisherInt")
    .def(boost::python::init<std::string, int, int>())
    .def("publish",  &PublisherInt::_publish)
  ;
  boost::python::class_<PublisherFloat, std::shared_ptr<PublisherFloat>, boost::noncopyable>("PublisherFloat")
    .def(boost::python::init<std::string, float, int>())
    .def("publish", &PublisherFloat::_publish)
  ;

  boost::python::class_<SubscriberBool, std::shared_ptr<SubscriberBool>, boost::noncopyable>("SubscriberBool")
    .def(boost::python::init<std::string, bool>())
    .def("subscribe", &SubscriberBool::_subscribe)
  ;
  boost::python::class_<SubscriberInt, std::shared_ptr<SubscriberInt>, boost::noncopyable>("SubscriberInt")
    .def(boost::python::init<std::string, int>())
    .def("subscribe", &SubscriberInt::_subscribe)
  ;
  boost::python::class_<SubscriberFloat, std::shared_ptr<SubscriberFloat>, boost::noncopyable>("SubscriberFloat")
    .def(boost::python::init<std::string, float>())
    .def("subscribe", &SubscriberFloat::_subscribe)
  ;

  // 見本の値の型で振り分ける従来どおりの書き方
  boost::python::def("Publisher", &makePublisher,
                     (boost::python::arg("name"), boost::python::arg("sample"),
                      boost::python::arg("buffer_num") = 3));
  boost::python::def("Subscriber", &makeSubscriber,
                     (boost::python::arg("name"), boost::python::arg("sample")));
}
