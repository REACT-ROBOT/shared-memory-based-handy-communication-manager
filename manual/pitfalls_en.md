# ⚠️ Common Pitfalls
[English | [日本語](../docs_jp/md_manual_pitfalls_jp.html)]

Things that compile, work in isolation, and cause trouble later. Every item here was
reproduced before it was written down.

- [1. `subscribe()` expires after 2 seconds](#1-subscribe-expires-after-2-seconds)
- [2. The reference from `subscribe()` dies on the second call](#2-the-reference-from-subscribe-dies-on-the-second-call)
- [3. `/` in a topic name becomes `_`](#3--in-a-topic-name-becomes-_)
- [4. 0660 becomes 0640 because of umask](#4-0660-becomes-0640-because-of-umask)
- [5. `buffer_num` decides how much history you keep](#5-buffer_num-decides-how-much-history-you-keep)
- [6. Where the format declaration has to go](#6-where-the-format-declaration-has-to-go)
- [7. `max_skew_us = 0` removes the bound](#7-max_skew_us--0-removes-the-bound)
- [8. Python has no time-machine API](#8-python-has-no-time-machine-api)
- [9. Getting `shm_tool` onto your PATH](#9-getting-shm_tool-onto-your-path)
- [10. Picking up a stale `shm_base.so`](#10-picking-up-a-stale-shm_baseso)

---

## 1. `subscribe()` expires after 2 seconds

**This is the most likely cause of "the publisher is running but nothing arrives".**

`subscribe()` returns the newest sample, but sets `state = false` if its
`capture_monotonic_us` is older than **2 seconds by default**. A 1 Hz topic, or a process
you just resumed from a breakpoint, hits this.

```cpp
Publisher<int> pub("slow"); Subscriber<int> sub("slow");
pub.publish(7);
bool ok = false;
sub.subscribe(&ok);          // ok = true
sleep(3);
sub.subscribe(&ok);          // ok = false  ← the data is still there
```

Change the deadline, or switch it off:

```cpp
sub.setDataExpiryTime_us(10 * 1000 * 1000);  // 10 s
sub.setDataExpiryTime_us(0);                 // 0 = never expires
```

`subscribe()` is **not a queue**. It keeps returning the same sample until a newer one
arrives, so polling faster than the publisher repeats data rather than losing it. To find
out whether you missed anything, watch `SampleInfo::sequence` for gaps.

---

## 2. The reference from `subscribe()` dies on the second call

The return value is a **reference into an internal double buffer**. It survives one more
call and is overwritten by the **second**. One call does not break it, which is exactly
why this gets shipped.

```cpp
const int &r = sub.subscribe(&ok);   // r == 11
sub.subscribe(&ok);                  // r == 11  ← still alive
sub.subscribe(&ok);                  // r == 33  ← gone
```

If you keep it, **copy by value**:

```cpp
const int value = sub.subscribe(&ok);
```

The double buffer exists so that a *failed* `subscribe()` cannot corrupt the value it
last returned (reads always go into the buffer that is not currently being handed out).
That extends the lifetime by one call; it is not a retention guarantee.

---

## 3. `/` in a topic name becomes `_`

ROS habits make `/odom` tempting, but `/` is replaced with `_`. As a result
**`"a/b"` and `"a_b"` are silently the same topic.**

```cpp
Publisher<int>  pub("a/b");   // really /dev/shm/shm_a_b
Subscriber<int> sub("a_b");   // connects to it, and succeeds
```

These are rejected with an exception:

| Name | Result |
|---|---|
| contains `'#'` | exception — reserved as the separator in `/shm_<topic>#<gen>-<nonce>` |
| longer than 200 chars | exception (`shared memory name is too long`) |

`/` is not rejected. If you want a namespace, use `_` or `.` and agree never to mix `/`
in.

---

## 4. 0660 becomes 0640 because of umask

`DEFAULT_PERM` is `0660`, but `shm_open()` **subtracts the creating process's `umask`**.
Under `umask 0022` — the default on most distributions — the segment ends up `0640`, and
another user in the same group can **read but not write**.

```
created under umask 0022:  -rw-r----- 1 user group ... shm_topic
```

If several users or containers write to the same topic, set `umask 0002` **before**
starting the publisher. The permissions of an existing segment do not change, so
`shm_tool remove` it and let it be recreated.

---

## 5. `buffer_num` decides how much history you keep

`buffer_num` is the slot count, and therefore also **the length of the history**:

```
retention ≈ buffer_num ÷ publish rate
```

A 10 Hz sensor at the default `buffer_num = 3` keeps only **300 ms**. Asking
`subscribeAlignedTo()` / `subscribeAt()` for a sample 500 ms back returns `TooOld`. What
is actually held can be measured:

```cpp
const RetentionWindow w = sub.getRetentionWindow();
```

```bash
shm_tool doctor    # the "履歴" column reports the measured window
```

Raise `buffer_num` too when many writers publish concurrently: a writer looks for a free
slot with `trylock`, and publishing fails when every slot is taken.

---

## 6. Where the format declaration has to go

`SHM_DECLARE_LAYOUT()` derives a layout hash from each member's offset, size, name and
type, so you get detection of "same size, members reordered" without maintaining a
version number by hand.

```cpp
struct Pose { double x, y, theta; };
SHM_DECLARE_LAYOUT(Pose, x, y, theta);   // at global scope, outside any namespace
```

Leaving out even one member is a **compile error** — `layout_covers_type()` sees the gap.
List all of them, in declaration order.

For a type whose wire format is decided by `serialize()` rather than by its memory layout
(`cv::Mat`, a custom scan type), the layout cannot tell you anything, so the revision is
carried by hand:

```cpp
SHM_DECLARE_SERIALIZED_FORMAT(MyScan, 1);   // bump to 2 when serialize() changes
```

**Two constraints on placement.** Both are accepted by gcc 11 on x86 and only fail when
you build on a Raspberry Pi 4 (gcc 12).

1. **Inside the include guard.** Outside it, a translation unit that pulls the header in
   twice transitively re-expands the macro and redefines `shm_schema<T>`.
2. **Before the `Publisher` / `Subscriber` specializations.** Their `contractOf()` calls
   `schema_version_of<T>()`, so a declaration after them is "specialization after
   instantiation", which is ill-formed. When instantiation happens varies by compiler
   version.

Nested types must be declared **before** the type that contains them, for the same
reason:

```cpp
struct Vec2 { double x, y; };
struct Path { Vec2 a; Vec2 b; };
SHM_DECLARE_LAYOUT(Vec2, x, y);      // first
SHM_DECLARE_LAYOUT(Path, a, b);      // second — it folds in Vec2's version
```

The reverse order gives `specialization of 'irlab::shm::shm_schema<Vec2>' after
instantiation`.

To migrate one package at a time, build it with `-DSHM_REQUIRE_LAYOUT=ON`: publishing or
subscribing an undeclared type then becomes a compile error. `shm_tool doctor` marks
which live topics are still undeclared.

---

## 7. `max_skew_us = 0` removes the bound

`max_skew_us` is a required argument of `subscribeAlignedTo()`, but passing **`0` skips
the skew check entirely**. `Nearest` always returns the closest existing sample, so a
sample hours away still comes back as `Success`.

```cpp
scan_sub.subscribeAlignedTo(odom_info, &st, 0);       // unbounded — don't
scan_sub.subscribeAlignedTo(odom_info, &st, 50000);   // reject beyond 50 ms
```

Only the application knows what skew is acceptable, so always state a real number.

Note also that the `SampleInfo` from a *failed* `subscribe()` is all zeros. Passing it as
the reference would mean searching for time 0; that is rejected with `InvalidReference`.

---

## 8. Python has no time-machine API

The Python binding exposes `Publisher` / `Subscriber` for `bool`, `int` and `float`, with
`publish()` and `subscribe()`. Everything below is **C++ only**:

- `subscribeAt()` / `subscribeAlignedTo()` / `SampleInfo` / `RetentionWindow`
- `setDataExpiryTime_us()` / `waitFor()`
- `std::vector<T>` and custom types

Write nodes that need time alignment in C++.

To import it, put the module directory `<build>/python` on `PYTHONPATH`. It does **not**
go into site-packages (`PYTHON_INSTALL_DIR` defaults to `${CMAKE_BINARY_DIR}/python`).

```bash
export PYTHONPATH=$PWD/build/python:$PYTHONPATH
export LD_LIBRARY_PATH=$HOME/.local/lib:$LD_LIBRARY_PATH   # to resolve shm_base.so

python3 -c "
import shm_pub_sub
p = shm_pub_sub.Publisher('py_demo', 0, 3)   # (topic, a value of the type, buffer_num)
s = shm_pub_sub.Subscriber('py_demo', 0)
p.publish(42)
print(s.subscribe())        # -> (42, True)   a (value, ok) tuple
"
```

Unlike C++, `subscribe()` returns a **tuple of the value and the success flag**, and the
second constructor argument is a sample value whose type selects the `Bool` / `Int` /
`Float` variant.

---

## 9. Getting `shm_tool` onto your PATH

`shm_tool` is produced by this repository's build; it is not packaged separately.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -j$(nproc)
cmake --install build          # $HOME/.local/bin/shm_tool

export PATH=$HOME/.local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/lib:$LD_LIBRARY_PATH
```

Without installing, it is at `build/tools/shm_tool/shm_tool`. Either way it needs
`LD_LIBRARY_PATH` (see the next item).

| Command | Purpose |
|---|---|
| `shm_tool list` | list the segments in `/dev/shm` |
| `shm_tool doctor [topic]` | read the headers and report ABI, format, retention and faults |
| `shm_tool remove <topic>` | remove a topic and every generation segment it owns |

`doctor` changes its exit code when something needs attention, so it works as a
pre-flight check in a startup script.

---

## 10. Picking up a stale `shm_base.so`

Because the libraries carry no `lib` prefix, `shm_pub_sub.so` records `shm_base.so` as a
**bare** `DT_NEEDED` name and has no `RUNPATH`. Resolution therefore comes down to
whichever copy appears first on `LD_LIBRARY_PATH`.

An old workspace `build/lib` left on that path produces:

```
shm_tool: symbol lookup error: shm_tool: undefined symbol:
  _ZN5irlab3shm15disconnectTopicERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
```

After updating the libraries, always check which one you got:

```bash
ldd $(which shm_tool) | grep shm
ldd ./your_program    | grep shm
```

For the same reason, replacing the `.so` does **not** update processes that are already
running. After an ABI bump, restart every process that uses the topic; `shm_tool doctor`
points out segments still on the old ABI.
