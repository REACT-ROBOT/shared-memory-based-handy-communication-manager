//! @file shm_pub_sub_conformance_test.cpp
//! @brief 本体 2 つ（scalar / vector）を適合性スイートに掛ける
//!
//! 契約そのものは shm_pub_sub_conformance.hpp に 1 箇所だけ書いてある。
//! 外部リポジトリの特殊化（cv::Mat / Lidar2dScanData / PointCloud2DScanData）も
//! 同じヘッダを取り込んで Traits を書くだけで同じ検査を受けられる。

#include "shm_pub_sub_conformance.hpp"

#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

// ---------------------------------------------------------------------------
// scalar: 固定長 POD
//
// makeLarge() を大きくしても publish は速いままなので、capture 時刻の検査は
// 「判定不能」として SKIP される見込みである。それでよい。この特殊化で
// commit 時点の時刻を打つと遅れは数 us しか出ず、実害も無い。
// 遅れが問題になるのは serialize に実時間がかかる型の方である。
//
// NOTE: SHM_DECLARE_LAYOUT は shm_schema<T> を irlab::shm へ特殊化するので、
//       型ごとグローバルスコープに置くこと。無名名前空間に入れると
//       「別の名前空間での特殊化」になってコンパイルできない。
// ---------------------------------------------------------------------------
struct BigPod
{
  uint32_t seed;
  uint32_t fill[4095];  // 16KB
};

SHM_DECLARE_LAYOUT(BigPod, seed, fill);

namespace
{

struct ScalarTraits
{
  using Payload = BigPod;
  static const char *name() { return "Scalar"; }

  static Payload makeSmall(uint32_t seed)
  {
    Payload p{};
    p.seed = seed;
    for (auto &v : p.fill)
    {
      v = seed;
    }
    return p;
  }
  static Payload makeLarge(uint32_t seed) { return makeSmall(seed); }
  static uint32_t seedOf(const Payload &p) { return p.seed; }
  static bool     equals(const Payload &a, const Payload &b)
  {
    if (a.seed != b.seed)
    {
      return false;
    }
    for (size_t i = 0; i < sizeof(a.fill) / sizeof(a.fill[0]); ++i)
    {
      if (a.fill[i] != b.fill[i])
      {
        return false;
      }
    }
    return true;
  }
};

// ---------------------------------------------------------------------------
// vector: 可変長
//
// makeLarge() を十分大きくすると publish に実時間がかかるので、
// capture 時刻を入口で打っているかを実際に判定できる。
// ---------------------------------------------------------------------------
struct VectorTraits
{
  using Payload = std::vector<uint32_t>;
  static const char *name() { return "Vector"; }

  static Payload makeSmall(uint32_t seed) { return Payload(16, seed); }
  //! memcpy に実時間がかかる大きさにする（16M 要素 = 64MB）
  static Payload makeLarge(uint32_t seed) { return Payload(16u * 1024u * 1024u, seed); }
  static uint32_t seedOf(const Payload &p) { return p.empty() ? 0u : p[0]; }
  static bool     equals(const Payload &a, const Payload &b) { return a == b; }
};

}  // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(Scalar, SHMSpecializationConformance, ScalarTraits);
INSTANTIATE_TYPED_TEST_SUITE_P(Vector, SHMSpecializationConformance, VectorTraits);
