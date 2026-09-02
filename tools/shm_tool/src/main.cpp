#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shm_base.hpp"

enum MODE
{
  LIST_MODE,
  REMOVE_MODE,
  DOCTOR_MODE,
};

char *progname;

void
general_usage()
{
  std::cout << progname << " is a command-line tool to operate shared memory that shm used" << std::endl << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << "\t" << progname << " list\tlist up shared memory" << std::endl;
  std::cout << "\t" << progname << " remove\tremove shared memory (all layout generations)" << std::endl;
  std::cout << "\t" << progname << " doctor [topic]\tinspect segments and report what needs attention" << std::endl;
}

namespace
{

using irlab::shm::ShmHeader;
using irlab::shm::SlotRecord;

//! doctor が 1 セグメントについて集めた情報
struct SegmentReport
{
  std::string name;        //!< "shm_" を除いた名前
  std::string topic;       //!< 世代サフィックスを除いたトピック名
  bool        has_suffix   = false;  //!< "#<世代>-<ノンス>" が付いているか
  uint64_t    name_gen     = 0;      //!< 名前が名乗る世代
  uint64_t    name_nonce   = 0;      //!< 名前が名乗るノンス
  uint64_t    file_size = 0;
  bool        readable  = false;

  bool     header_ok = false;
  uint32_t magic = 0, abi_major = 0, payload_kind = 0, schema_version = 0, state = 0;
  uint64_t generation = 0, buf_num = 0, element_capacity = 0, element_size = 0;
  uint64_t latest_tag = 0, segment_nonce = 0, boot_id = 0;

  size_t   valid_samples  = 0;
  uint64_t retention_us   = 0;  //!< 実際に保持している履歴の時間幅
  bool     retention_seen = false;

  std::vector<std::string> problems;  //!< 対処が要るもの（★）
  std::vector<std::string> notes;     //!< 助言。動作はしている
};

//! @brief 1 セグメントを読み取る。共有メモリは読み取り専用で開き、書き換えない。
SegmentReport
inspectSegment(const std::string &entry)
{
  SegmentReport r;
  r.name  = entry.substr(4);  // "shm_" を除く
  r.topic = r.name;
  const auto hash = r.topic.find('#');
  if (hash != std::string::npos)
  {
    const std::string suffix = r.topic.substr(hash + 1);
    r.topic                  = r.topic.substr(0, hash);
    const auto dash          = suffix.find('-');
    if (dash != std::string::npos)
    {
      try
      {
        r.name_gen    = std::stoull(suffix.substr(0, dash), nullptr, 10);
        r.name_nonce  = std::stoull(suffix.substr(dash + 1), nullptr, 16);
        r.has_suffix  = true;
      }
      catch (const std::exception &)
      {
        r.problems.push_back("★世代セグメント名の書式が不正");
      }
    }
    else
    {
      r.problems.push_back("★世代セグメント名の書式が不正");
    }
  }

  const int fd = ::shm_open(("/" + entry).c_str(), O_RDONLY, 0);
  if (fd < 0)
  {
    r.problems.push_back("開けない (" + std::string(std::strerror(errno)) + ")");
    return r;
  }
  struct stat st;
  if (::fstat(fd, &st) != 0 || st.st_size <= 0)
  {
    ::close(fd);
    r.problems.push_back("サイズを取得できない");
    return r;
  }
  r.file_size = static_cast<uint64_t>(st.st_size);

  void *base = ::mmap(nullptr, r.file_size, PROT_READ, MAP_SHARED, fd, 0);
  ::close(fd);
  if (base == MAP_FAILED)
  {
    r.problems.push_back("mmap できない");
    return r;
  }
  r.readable = true;

  if (r.file_size < sizeof(ShmHeader))
  {
    r.problems.push_back("ヘッダより小さい (作りかけ？)");
    ::munmap(base, r.file_size);
    return r;
  }

  const ShmHeader *h = reinterpret_cast<const ShmHeader *>(base);
  r.magic            = h->magic;
  r.abi_major        = h->abi_major;
  r.state            = h->state.load();
  r.generation       = h->generation;
  r.buf_num          = h->buf_num;
  r.element_capacity = h->element_capacity;
  r.element_size     = h->element_size;
  r.payload_kind     = h->payload_kind;
  r.schema_version   = h->schema_version;
  r.latest_tag       = h->latest_generation.load();
  r.segment_nonce    = h->segment_nonce;
  r.boot_id          = h->boot_id_hash;

  if (r.magic == 0)
  {
    r.problems.push_back("★未初期化（作成途中で止まった残骸の可能性）");
    ::munmap(base, r.file_size);
    return r;
  }
  if (r.magic != irlab::shm::RingBuffer::SHM_MAGIC)
  {
    r.problems.push_back("★別形式または shm v1 → shm_tool remove が必要");
    ::munmap(base, r.file_size);
    return r;
  }
  r.header_ok = true;

  if (r.abi_major != irlab::shm::RingBuffer::ABI_MAJOR)
  {
    r.problems.push_back("★ABI " + std::to_string(r.abi_major) + "（このビルドは " +
                         std::to_string(irlab::shm::RingBuffer::ABI_MAJOR) + "）→ shm_tool remove が必要");
    ::munmap(base, r.file_size);
    return r;
  }
  if (r.state != irlab::shm::RingBuffer::INITIALIZED)
  {
    r.problems.push_back("★初期化されていない");
    ::munmap(base, r.file_size);
    return r;
  }
  if (r.boot_id != 0 && r.boot_id != irlab::shm::getBootIdHash())
  {
    r.problems.push_back("★再起動前に作られた残骸（時刻が意味を持たない）");
  }
  // 名前が名乗るノンスとヘッダのノンスが一致すること。
  // ライブラリの attachGeneration() も接続時に同じ照合をしている（R03-F03）。
  if (r.has_suffix && (r.name_nonce != r.segment_nonce || r.name_gen != r.generation))
  {
    char expected[32], actual[32];
    std::snprintf(expected, sizeof(expected), "%" PRIu64 "-%012" PRIx64, r.name_gen, r.name_nonce);
    std::snprintf(actual, sizeof(actual), "%" PRIu64 "-%012" PRIx64, r.generation, r.segment_nonce);
    r.problems.push_back(std::string("★名前とヘッダが食い違う（名前は #") + expected + "、ヘッダは #" + actual +
                         "）→ 手で複製・改名した可能性");
  }
  if (r.schema_version == 0)
  {
    // 動作はするが、同サイズの並べ替えを検出できない。移行の目印になる。
    r.notes.push_back("書式が未宣言（SHM_DECLARE_LAYOUT を付けると同サイズの並べ替えも検出できる）");
  }

  // スロットを触る前に、ライブラリと同じ検証を通す（R05）。
  //
  // 以前はここで自前の緩い検査しかしておらず、次の 2 つで落ちていた。
  //   - buf_num * slot_size が uint64 で折り返り、ガードを通過してマッピング外を読む
  //   - slot_offset が 64 バイト境界に載っておらず、非アラインな atomic を読む
  //     （x86 では動くが **aarch64 では SIGBUS**）
  // さらに、ライブラリが接続を拒否する状態（buf_num 範囲外など）を「OK」と
  // 報告していた。doctor の目的は「そのまま使えるか」を答えることなので、
  // ライブラリの判定をそのまま使うのが正しい。
  std::string reason;
  const auto  verdict = irlab::shm::RingBuffer::inspectLayout(
      static_cast<const unsigned char *>(base), r.file_size, &reason, nullptr, 0, r.name);
  if (verdict != irlab::shm::RingBuffer::LayoutVerdict::Usable)
  {
    r.problems.push_back(std::string("★") + reason);
    ::munmap(base, r.file_size);
    return r;
  }

  // 実際に保持している履歴の時間幅を数える。B/C の判断材料になる。
  //
  // ここには slot_size / buf_num / slots_end / slot_offset の妥当性を見る if が
  // あったが、4 項すべてが恒真だった。上で inspectLayout() が Usable を返した
  // 時点で、それぞれ次の検査を通っている。
  //   slot_size == sizeof(SlotRecord)        "slot size mismatch" で fail
  //   buf_num   ∈ [1, MAX_BUFFER_NUM]        "buf_num is out of range" で fail
  //   slot_offset は computeLayout の値と一致 "the offsets recorded in the header
  //                                           disagree..." で fail。その値は
  //                                           alignUp(sizeof(ShmHeader),
  //                                           alignof(SlotRecord)) なので
  //                                           alignof(SlotRecord) の倍数
  //   slots_end <= total_size <= mapping_size "mapping is smaller than the
  //                                           layout" で fail
  // 恒真の条件を残すと、偽になり得ると読ませたうえ、万一偽になれば履歴 0 と
  // 黙って誤報告することになるので、条件ごと外した。
  {
    uint64_t oldest = UINT64_MAX, newest = 0;
    for (uint64_t i = 0; i < h->buf_num; ++i)
    {
      const SlotRecord *slot =
          reinterpret_cast<const SlotRecord *>(static_cast<const unsigned char *>(base) + h->slot_offset + i * h->slot_size);
      if (slot->sequence.load() == 0)
      {
        continue;
      }
      const uint64_t t = slot->capture_monotonic_us.load();
      if (t == 0)
      {
        continue;
      }
      ++r.valid_samples;
      oldest = std::min(oldest, t);
      newest = std::max(newest, t);
    }
    if (r.valid_samples > 0)
    {
      r.retention_seen = true;
      r.retention_us   = newest - oldest;
    }
  }

  ::munmap(base, r.file_size);
  return r;
}

const char *
kindName(uint32_t kind)
{
  switch (kind)
  {
    case 1:  return "scalar";
    case 2:  return "vector";
    case 3:  return "serialized";
    default: return "unknown";
  }
}

std::string
humanSize(uint64_t bytes)
{
  char buf[32];
  if (bytes >= (1ULL << 20))
  {
    std::snprintf(buf, sizeof(buf), "%.1f MiB", static_cast<double>(bytes) / (1 << 20));
  }
  else if (bytes >= (1ULL << 10))
  {
    std::snprintf(buf, sizeof(buf), "%.1f KiB", static_cast<double>(bytes) / (1 << 10));
  }
  else
  {
    std::snprintf(buf, sizeof(buf), "%" PRIu64 " B", bytes);
  }
  return buf;
}

//! @return int 問題が 1 つも無ければ 0、あれば 1（スクリプトから使えるように）
int
runDoctor(const std::string &only_topic)
{
  DIR *dir = ::opendir("/dev/shm");
  if (dir == nullptr)
  {
    std::cerr << "cannot open /dev/shm" << std::endl;
    return 1;
  }
  std::vector<std::string> entries;
  for (struct dirent *e = ::readdir(dir); e != nullptr; e = ::readdir(dir))
  {
    const std::string name = e->d_name;
    if (name.rfind("shm_", 0) != 0)
    {
      continue;
    }
    if (!only_topic.empty())
    {
      // "shm_<topic>" そのものか、"shm_<topic>#..." だけを見る
      const std::string prefix = "shm_" + only_topic;
      if (name != prefix && name.rfind(prefix + "#", 0) != 0)
      {
        continue;
      }
    }
    entries.push_back(name);
  }
  ::closedir(dir);
  std::sort(entries.begin(), entries.end());

  if (entries.empty())
  {
    std::cout << (only_topic.empty() ? "shm のセグメントはありません。"
                                     : ("トピック '" + only_topic + "' のセグメントはありません。"))
              << std::endl;
    return 0;
  }

  std::vector<SegmentReport> reports;
  reports.reserve(entries.size());
  for (const std::string &entry : entries)
  {
    reports.push_back(inspectSegment(entry));
  }

  // 現世代でない世代セグメントを見つける。root の世代タグが正本。
  for (SegmentReport &r : reports)
  {
    if (r.name.find('#') == std::string::npos || !r.header_ok)
    {
      continue;
    }
    const auto root = std::find_if(reports.begin(), reports.end(),
                                   [&](const SegmentReport &x) { return x.name == r.topic && x.header_ok; });
    if (root == reports.end())
    {
      r.problems.push_back("★root セグメントが無い（トピックごと消し忘れ）");
      continue;
    }
    const uint64_t live_generation = irlab::shm::unpackGeneration(root->latest_tag);
    const uint64_t live_nonce      = irlab::shm::unpackNonce(root->latest_tag);
    if (r.generation != live_generation)
    {
      r.problems.push_back("★古い世代の残骸（現世代は " + std::to_string(live_generation) + "）");
    }
    else if (r.segment_nonce != live_nonce)
    {
      char live[32];
      std::snprintf(live, sizeof(live), "%" PRIu64 "-%012" PRIx64, live_generation, live_nonce);
      r.problems.push_back(std::string("★世代の切り替え競争に負けた残骸（現世代は #") + live + "）");
    }
  }

  constexpr const char *HEADER_FORMAT = "%-34s %4s %4s %-11s %10s %5s %10s %10s %9s  %s\n";
  std::printf(HEADER_FORMAT, "segment", "abi", "gen", "kind", "capacity", "buf", "schema", "size", "履歴", "状態");

  int problem_count = 0;
  int note_count    = 0;
  for (const SegmentReport &r : reports)
  {
    std::string retention = "-";
    if (r.retention_seen)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%" PRIu64 " ms", r.retention_us / 1000);
      retention = buf;
    }
    std::string schema = "-";
    if (r.header_ok)
    {
      if (r.schema_version == 0)
      {
        schema = "未宣言";
      }
      else
      {
        char b[16];
        std::snprintf(b, sizeof(b), "%08x", r.schema_version);
        schema = std::string("0x") + b;
      }
    }
    const std::string abi      = r.header_ok ? std::to_string(r.abi_major) : "-";
    const std::string gen      = r.header_ok ? std::to_string(r.generation) : "-";
    const std::string capacity = r.header_ok ? humanSize(r.element_capacity) : "-";
    const std::string bufnum   = r.header_ok ? std::to_string(r.buf_num) : "-";
    const std::string status   = r.problems.empty() ? (r.notes.empty() ? "OK" : "OK（下の注記を参照）") : r.problems.front();

    std::printf(HEADER_FORMAT, r.name.c_str(), abi.c_str(), gen.c_str(),
                r.header_ok ? kindName(r.payload_kind) : "-", capacity.c_str(), bufnum.c_str(), schema.c_str(),
                humanSize(r.file_size).c_str(), retention.c_str(), status.c_str());
    for (size_t i = 1; i < r.problems.size(); ++i)
    {
      std::printf("%-34s %4s %4s %-11s %10s %5s %10s %10s %9s  %s\n", "", "", "", "", "", "", "", "", "",
                  r.problems[i].c_str());
    }
    for (const std::string &note : r.notes)
    {
      std::printf("%-34s %4s %4s %-11s %10s %5s %10s %10s %9s  - %s\n", "", "", "", "", "", "", "", "", "",
                  note.c_str());
    }
    if (!r.problems.empty())
    {
      ++problem_count;
    }
    if (!r.notes.empty())
    {
      ++note_count;
    }
  }

  std::printf("\n%zu 個のセグメント: 対処が必要 %d 件、注記 %d 件\n", reports.size(), problem_count, note_count);
  if (problem_count > 0)
  {
    std::printf("★ が付いたものは 'shm_tool remove <topic>' で片付けてから、"
                "そのトピックを使う全プロセスを起動し直してください。\n");
  }
  // 注記だけなら動作はしているので 0 を返す（スクリプトから使えるように）
  return problem_count > 0 ? 1 : 0;
}

}  // namespace

void
remove_usage()
{
  std::cout << "Usage: " << progname << " remove <shm_name>" << std::endl;
}

void
doctor_usage()
{
  std::cout << "Usage: " << progname << " doctor [topic]" << std::endl;
  std::cout << "  /dev/shm のセグメントをすべて読み、そのまま使えるかを報告する。" << std::endl;
  std::cout << "  ★ が付いたものは shm_tool remove で片付けてから、" << std::endl;
  std::cout << "  そのトピックを使う全プロセスを起動し直すこと。" << std::endl;
  std::cout << "  終了コード: 対処が必要なものがあれば 1、無ければ 0。" << std::endl;
}

int
main(int argc, char *argv[])
{
  // mode を未初期化のまま switch に入れていた。"list" でも "remove" でも
  // ない引数を渡すと不定値で分岐する未定義動作になっていたため、
  // 既定値を持たせて未知のコマンドは明示的に弾く。
  MODE mode = LIST_MODE;

  progname = basename(argv[0]);

  if (argc < 2)
  {
    general_usage();
    return 1;
  }

  // strncmp の長さ指定では "listXXX" のような前方一致も通っていた。
  // コマンド名は完全一致で判定する。
  if (!strcmp(argv[1], "list"))
  {
    mode = LIST_MODE;
  }
  else if (!strcmp(argv[1], "remove"))
  {
    mode = REMOVE_MODE;
  }
  else if (!strcmp(argv[1], "doctor"))
  {
    mode = DOCTOR_MODE;
  }
  else
  {
    std::cerr << progname << ": unknown command '" << argv[1] << "'" << std::endl << std::endl;
    general_usage();
    return 1;
  }

  FILE *fp;
  char buf[256];
  std::string buf_str;
  // ls -l の 9 欄（permission / link / user / group / size / 月 / 日 / 時刻 / 名前）
  static constexpr size_t FIELD_NUM = 9;
  const char *format[FIELD_NUM] = { " ", "\t\t", "\t", "\t", "\t", " ", " ", " ", "\t" };
  switch (mode)
  {
  case DOCTOR_MODE:
    // トピックを指定すると、そのトピックのセグメントだけを見る。
    // 移行作業で 1 つずつ潰すときや、スクリプトから使うときに便利。
    return runDoctor(argc >= 3 ? argv[2] : std::string());
  case LIST_MODE:
    fp = popen("ls -l /dev/shm/", "r");
    // popen が NULL を返したら feof(NULL) で落ちていた
    if (fp == nullptr)
    {
      std::cerr << progname << ": cannot list /dev/shm" << std::endl;
      return 1;
    }
    std::cout << "Permission Hard-link\tUser\tGroup\tSize\tTimestamp\tShared memory name" << std::endl;
    while (1) 
    {
      // fgets の返り値を見ていなかったため、読めなかった行で
      // 前回の buf をもう一度処理していた
      if (fgets(buf, sizeof(buf), fp) == nullptr)
      {
        if (ferror(fp))
        {
          fprintf(stderr, "input stream error\n");
        }
        break;
      }
      buf_str = buf;
      if (buf_str.find("shm_") == std::string::npos)
      {
        continue;
      }
      buf_str = regex_replace(buf_str, std::regex("shm_"), "");
      {
        // ls -l はサイズ欄を右詰めするので、空白は 1 個とは限らない。
        // 区切りを 1 文字と決めうちしていたため、幅の狭い行では空のフィールドが
        // 生まれて列が丸ごとずれ、名前が消えていた。空白の**連続**で区切る。
        // 併せて、10 フィールドを最後まで回った行は改行を出しておらず、
        // 次の行と繋がって表示されていた。
        std::vector<std::string> fields;
        for (std::string::size_type offset = 0; offset < buf_str.size();)
        {
          const auto begin = buf_str.find_first_not_of(" \t\n", offset);
          if (begin == std::string::npos)
          {
            break;
          }
          auto end = buf_str.find_first_of(" \t\n", begin);
          if (end == std::string::npos)
          {
            end = buf_str.size();
          }
          // 名前に空白を含むセグメントがあり得るので、最後の欄は行末まで残す
          if (fields.size() == FIELD_NUM - 1)
          {
            end = buf_str.find_last_not_of(" \t\n") + 1;
          }
          fields.push_back(buf_str.substr(begin, end - begin));
          offset = end;
        }
        for (size_t i = 0; i < fields.size(); ++i)
        {
          std::cout << fields[i] << (i + 1 < fields.size() ? format[i] : "");
        }
        std::cout << std::endl;
      }
    }
    (void) pclose(fp);
    break;
  case REMOVE_MODE:
    if (argc < 3)
    {
      remove_usage();
      return 1;
    }
    try
    {
      // 形式 v3 ではレイアウト世代ごとに /shm_<topic>#<N>-<ノンス> が増えるため、
      // トピック名だけを消しても古い世代が残る。世代ごと片付ける。
      // 返り値を見ていなかったため、存在しないトピックの remove も成功扱い
      // （exit 0）になっていた。スクリプトから使えるように結果を返す。
      if (irlab::shm::disconnectTopic(argv[2]) != 0)
      {
        std::cerr << progname << ": no segment was removed for topic '" << argv[2] << "'" << std::endl;
        return 1;
      }
    }
    catch (const std::exception &e)
    {
      std::cerr << progname << ": " << e.what() << std::endl;
      return 1;
    }
    break;
  }

  return 0;
}
