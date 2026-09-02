#!/usr/bin/env python3
"""shm_pub_sub の Python バインディングの回帰テスト（R04-F16）

3 つの C++ クラスを同じ "Publisher" / "Subscriber" という名前で登録していたため、
最後に登録した float 版しかモジュールから見えなかった。その結果
`Publisher("topic", False, 3)` は bool ではなく float のトピックを作り、
C++ 側の Publisher<bool> / Publisher<int> のトピックは Python から
一切購読できなかった。
"""
import os
import struct
import sys
import unittest

import shm_pub_sub

# ShmHeader 内のオフセット（shm_base.hpp の定義に対応）
OFFSET_ELEMENT_CAPACITY = 40
OFFSET_ELEMENT_SIZE = 104
OFFSET_PAYLOAD_KIND = 120


def read_header(topic):
    with open("/dev/shm/shm_" + topic, "rb") as f:
        b = f.read(192)
    return {
        "element_capacity": struct.unpack_from("<Q", b, OFFSET_ELEMENT_CAPACITY)[0],
        "element_size": struct.unpack_from("<Q", b, OFFSET_ELEMENT_SIZE)[0],
        "payload_kind": struct.unpack_from("<I", b, OFFSET_PAYLOAD_KIND)[0],
    }


def cleanup(*topics):
    for t in topics:
        for name in os.listdir("/dev/shm"):
            if name == "shm_" + t or name.startswith("shm_" + t + "#"):
                try:
                    os.remove("/dev/shm/" + name)
                except OSError:
                    pass


class PythonBindingTest(unittest.TestCase):
    def setUp(self):
        cleanup("pyt_bool", "pyt_int", "pyt_float")

    def tearDown(self):
        cleanup("pyt_bool", "pyt_int", "pyt_float")

    def test_each_type_is_reachable_by_name(self):
        """型ごとのクラスがモジュールから見えること"""
        for name in ("PublisherBool", "PublisherInt", "PublisherFloat",
                     "SubscriberBool", "SubscriberInt", "SubscriberFloat"):
            self.assertTrue(hasattr(shm_pub_sub, name), name + " が公開されていない")

    def test_sample_value_selects_the_payload_type(self):
        """見本の値の型で、実際に作られるトピックの型が決まること

        修正前は bool を渡しても 4 バイトの float トピックになっていた。
        """
        pub_bool = shm_pub_sub.Publisher("pyt_bool", False, 3)
        pub_bool.publish(True)
        self.assertEqual(read_header("pyt_bool")["element_capacity"], 1,
                         "bool のトピックが 1 バイトになっていない")

        pub_int = shm_pub_sub.Publisher("pyt_int", 0, 3)
        pub_int.publish(42)
        self.assertEqual(read_header("pyt_int")["element_capacity"], 4)

        pub_float = shm_pub_sub.Publisher("pyt_float", 0.0, 3)
        pub_float.publish(1.5)
        self.assertEqual(read_header("pyt_float")["element_capacity"], 4)

    def test_values_round_trip_with_their_own_type(self):
        """読み出した値が元の型のまま返ること

        修正前は bool が 1.0、int が 42.0 という float で返っていた。
        """
        shm_pub_sub.Publisher("pyt_bool", False, 3).publish(True)
        shm_pub_sub.Publisher("pyt_int", 0, 3).publish(42)
        shm_pub_sub.Publisher("pyt_float", 0.0, 3).publish(1.5)

        value, ok = shm_pub_sub.Subscriber("pyt_bool", False).subscribe()
        self.assertTrue(ok)
        self.assertIs(value, True, "bool が bool として返っていない")

        value, ok = shm_pub_sub.Subscriber("pyt_int", 0).subscribe()
        self.assertTrue(ok)
        self.assertIsInstance(value, int)
        self.assertNotIsInstance(value, float)
        self.assertEqual(value, 42)

        value, ok = shm_pub_sub.Subscriber("pyt_float", 0.0).subscribe()
        self.assertTrue(ok)
        self.assertIsInstance(value, float)
        self.assertAlmostEqual(value, 1.5)

    def test_unsupported_sample_type_is_rejected(self):
        """対応していない型は、黙って別の型として扱わずに弾くこと"""
        with self.assertRaises(TypeError):
            shm_pub_sub.Publisher("pyt_bad", "文字列")
        with self.assertRaises(TypeError):
            shm_pub_sub.Subscriber("pyt_bad", [1, 2, 3])


if __name__ == "__main__":
    unittest.main()
