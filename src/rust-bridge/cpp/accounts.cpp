#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace rust {
inline namespace cxxbridge1 {
// #include "rust/cxx.h"

#ifndef CXXBRIDGE1_PANIC
#define CXXBRIDGE1_PANIC
template <typename Exception>
void panic [[noreturn]] (const char *msg);
#endif // CXXBRIDGE1_PANIC

struct unsafe_bitcopy_t;

namespace {
template <typename T>
class impl;
} // namespace

template <typename T>
::std::size_t size_of();
template <typename T>
::std::size_t align_of();

#ifndef CXXBRIDGE1_RUST_STRING
#define CXXBRIDGE1_RUST_STRING
class String final {
public:
  String() noexcept;
  String(const String &) noexcept;
  String(String &&) noexcept;
  ~String() noexcept;

  String(const std::string &);
  String(const char *);
  String(const char *, std::size_t);
  String(const char16_t *);
  String(const char16_t *, std::size_t);

  static String lossy(const std::string &) noexcept;
  static String lossy(const char *) noexcept;
  static String lossy(const char *, std::size_t) noexcept;
  static String lossy(const char16_t *) noexcept;
  static String lossy(const char16_t *, std::size_t) noexcept;

  String &operator=(const String &) &noexcept;
  String &operator=(String &&) &noexcept;

  explicit operator std::string() const;

  const char *data() const noexcept;
  std::size_t size() const noexcept;
  std::size_t length() const noexcept;
  bool empty() const noexcept;

  const char *c_str() noexcept;

  std::size_t capacity() const noexcept;
  void reserve(size_t new_cap) noexcept;

  using iterator = char *;
  iterator begin() noexcept;
  iterator end() noexcept;

  using const_iterator = const char *;
  const_iterator begin() const noexcept;
  const_iterator end() const noexcept;
  const_iterator cbegin() const noexcept;
  const_iterator cend() const noexcept;

  bool operator==(const String &) const noexcept;
  bool operator!=(const String &) const noexcept;
  bool operator<(const String &) const noexcept;
  bool operator<=(const String &) const noexcept;
  bool operator>(const String &) const noexcept;
  bool operator>=(const String &) const noexcept;

  void swap(String &) noexcept;

  String(unsafe_bitcopy_t, const String &) noexcept;

private:
  struct lossy_t;
  String(lossy_t, const char *, std::size_t) noexcept;
  String(lossy_t, const char16_t *, std::size_t) noexcept;
  friend void swap(String &lhs, String &rhs) noexcept { lhs.swap(rhs); }

  std::array<std::uintptr_t, 3> repr;
};
#endif // CXXBRIDGE1_RUST_STRING

#ifndef CXXBRIDGE1_RUST_STR
#define CXXBRIDGE1_RUST_STR
class Str final {
public:
  Str() noexcept;
  Str(const String &) noexcept;
  Str(const std::string &);
  Str(const char *);
  Str(const char *, std::size_t);

  Str &operator=(const Str &) &noexcept = default;

  explicit operator std::string() const;

  const char *data() const noexcept;
  std::size_t size() const noexcept;
  std::size_t length() const noexcept;
  bool empty() const noexcept;

  Str(const Str &) noexcept = default;
  ~Str() noexcept = default;

  using iterator = const char *;
  using const_iterator = const char *;
  const_iterator begin() const noexcept;
  const_iterator end() const noexcept;
  const_iterator cbegin() const noexcept;
  const_iterator cend() const noexcept;

  bool operator==(const Str &) const noexcept;
  bool operator!=(const Str &) const noexcept;
  bool operator<(const Str &) const noexcept;
  bool operator<=(const Str &) const noexcept;
  bool operator>(const Str &) const noexcept;
  bool operator>=(const Str &) const noexcept;

  void swap(Str &) noexcept;

private:
  class uninit;
  Str(uninit) noexcept;
  friend impl<Str>;

  std::array<std::uintptr_t, 2> repr;
};
#endif // CXXBRIDGE1_RUST_STR

#ifndef CXXBRIDGE1_RUST_SLICE
#define CXXBRIDGE1_RUST_SLICE
namespace detail {
template <bool>
struct copy_assignable_if {};

template <>
struct copy_assignable_if<false> {
  copy_assignable_if() noexcept = default;
  copy_assignable_if(const copy_assignable_if &) noexcept = default;
  copy_assignable_if &operator=(const copy_assignable_if &) &noexcept = delete;
  copy_assignable_if &operator=(copy_assignable_if &&) &noexcept = default;
};
} // namespace detail

template <typename T>
class Slice final
    : private detail::copy_assignable_if<std::is_const<T>::value> {
public:
  using value_type = T;

  Slice() noexcept;
  Slice(T *, std::size_t count) noexcept;

  Slice &operator=(const Slice<T> &) &noexcept = default;
  Slice &operator=(Slice<T> &&) &noexcept = default;

  T *data() const noexcept;
  std::size_t size() const noexcept;
  std::size_t length() const noexcept;
  bool empty() const noexcept;

  T &operator[](std::size_t n) const noexcept;
  T &at(std::size_t n) const;
  T &front() const noexcept;
  T &back() const noexcept;

  Slice(const Slice<T> &) noexcept = default;
  ~Slice() noexcept = default;

  class iterator;
  iterator begin() const noexcept;
  iterator end() const noexcept;

  void swap(Slice &) noexcept;

private:
  class uninit;
  Slice(uninit) noexcept;
  friend impl<Slice>;
  friend void sliceInit(void *, const void *, std::size_t) noexcept;
  friend void *slicePtr(const void *) noexcept;
  friend std::size_t sliceLen(const void *) noexcept;

  std::array<std::uintptr_t, 2> repr;
};

template <typename T>
class Slice<T>::iterator final {
public:
  using iterator_category = std::random_access_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = typename std::add_pointer<T>::type;
  using reference = typename std::add_lvalue_reference<T>::type;

  reference operator*() const noexcept;
  pointer operator->() const noexcept;
  reference operator[](difference_type) const noexcept;

  iterator &operator++() noexcept;
  iterator operator++(int) noexcept;
  iterator &operator--() noexcept;
  iterator operator--(int) noexcept;

  iterator &operator+=(difference_type) noexcept;
  iterator &operator-=(difference_type) noexcept;
  iterator operator+(difference_type) const noexcept;
  iterator operator-(difference_type) const noexcept;
  difference_type operator-(const iterator &) const noexcept;

  bool operator==(const iterator &) const noexcept;
  bool operator!=(const iterator &) const noexcept;
  bool operator<(const iterator &) const noexcept;
  bool operator<=(const iterator &) const noexcept;
  bool operator>(const iterator &) const noexcept;
  bool operator>=(const iterator &) const noexcept;

private:
  friend class Slice;
  void *pos;
  std::size_t stride;
};

template <typename T>
Slice<T>::Slice() noexcept {
  sliceInit(this, reinterpret_cast<void *>(align_of<T>()), 0);
}

template <typename T>
Slice<T>::Slice(T *s, std::size_t count) noexcept {
  assert(s != nullptr || count == 0);
  sliceInit(this,
            s == nullptr && count == 0
                ? reinterpret_cast<void *>(align_of<T>())
                : const_cast<typename std::remove_const<T>::type *>(s),
            count);
}

template <typename T>
T *Slice<T>::data() const noexcept {
  return reinterpret_cast<T *>(slicePtr(this));
}

template <typename T>
std::size_t Slice<T>::size() const noexcept {
  return sliceLen(this);
}

template <typename T>
std::size_t Slice<T>::length() const noexcept {
  return this->size();
}

template <typename T>
bool Slice<T>::empty() const noexcept {
  return this->size() == 0;
}

template <typename T>
T &Slice<T>::operator[](std::size_t n) const noexcept {
  assert(n < this->size());
  auto ptr = static_cast<char *>(slicePtr(this)) + size_of<T>() * n;
  return *reinterpret_cast<T *>(ptr);
}

template <typename T>
T &Slice<T>::at(std::size_t n) const {
  if (n >= this->size()) {
    panic<std::out_of_range>("rust::Slice index out of range");
  }
  return (*this)[n];
}

template <typename T>
T &Slice<T>::front() const noexcept {
  assert(!this->empty());
  return (*this)[0];
}

template <typename T>
T &Slice<T>::back() const noexcept {
  assert(!this->empty());
  return (*this)[this->size() - 1];
}

template <typename T>
typename Slice<T>::iterator::reference
Slice<T>::iterator::operator*() const noexcept {
  return *static_cast<T *>(this->pos);
}

template <typename T>
typename Slice<T>::iterator::pointer
Slice<T>::iterator::operator->() const noexcept {
  return static_cast<T *>(this->pos);
}

template <typename T>
typename Slice<T>::iterator::reference Slice<T>::iterator::operator[](
    typename Slice<T>::iterator::difference_type n) const noexcept {
  auto ptr = static_cast<char *>(this->pos) + this->stride * n;
  return *reinterpret_cast<T *>(ptr);
}

template <typename T>
typename Slice<T>::iterator &Slice<T>::iterator::operator++() noexcept {
  this->pos = static_cast<char *>(this->pos) + this->stride;
  return *this;
}

template <typename T>
typename Slice<T>::iterator Slice<T>::iterator::operator++(int) noexcept {
  auto ret = iterator(*this);
  this->pos = static_cast<char *>(this->pos) + this->stride;
  return ret;
}

template <typename T>
typename Slice<T>::iterator &Slice<T>::iterator::operator--() noexcept {
  this->pos = static_cast<char *>(this->pos) - this->stride;
  return *this;
}

template <typename T>
typename Slice<T>::iterator Slice<T>::iterator::operator--(int) noexcept {
  auto ret = iterator(*this);
  this->pos = static_cast<char *>(this->pos) - this->stride;
  return ret;
}

template <typename T>
typename Slice<T>::iterator &Slice<T>::iterator::operator+=(
    typename Slice<T>::iterator::difference_type n) noexcept {
  this->pos = static_cast<char *>(this->pos) + this->stride * n;
  return *this;
}

template <typename T>
typename Slice<T>::iterator &Slice<T>::iterator::operator-=(
    typename Slice<T>::iterator::difference_type n) noexcept {
  this->pos = static_cast<char *>(this->pos) - this->stride * n;
  return *this;
}

template <typename T>
typename Slice<T>::iterator Slice<T>::iterator::operator+(
    typename Slice<T>::iterator::difference_type n) const noexcept {
  auto ret = iterator(*this);
  ret.pos = static_cast<char *>(this->pos) + this->stride * n;
  return ret;
}

template <typename T>
typename Slice<T>::iterator Slice<T>::iterator::operator-(
    typename Slice<T>::iterator::difference_type n) const noexcept {
  auto ret = iterator(*this);
  ret.pos = static_cast<char *>(this->pos) - this->stride * n;
  return ret;
}

template <typename T>
typename Slice<T>::iterator::difference_type
Slice<T>::iterator::operator-(const iterator &other) const noexcept {
  auto diff = std::distance(static_cast<char *>(other.pos),
                            static_cast<char *>(this->pos));
  return diff / this->stride;
}

template <typename T>
bool Slice<T>::iterator::operator==(const iterator &other) const noexcept {
  return this->pos == other.pos;
}

template <typename T>
bool Slice<T>::iterator::operator!=(const iterator &other) const noexcept {
  return this->pos != other.pos;
}

template <typename T>
bool Slice<T>::iterator::operator<(const iterator &other) const noexcept {
  return this->pos < other.pos;
}

template <typename T>
bool Slice<T>::iterator::operator<=(const iterator &other) const noexcept {
  return this->pos <= other.pos;
}

template <typename T>
bool Slice<T>::iterator::operator>(const iterator &other) const noexcept {
  return this->pos > other.pos;
}

template <typename T>
bool Slice<T>::iterator::operator>=(const iterator &other) const noexcept {
  return this->pos >= other.pos;
}

template <typename T>
typename Slice<T>::iterator Slice<T>::begin() const noexcept {
  iterator it;
  it.pos = slicePtr(this);
  it.stride = size_of<T>();
  return it;
}

template <typename T>
typename Slice<T>::iterator Slice<T>::end() const noexcept {
  iterator it = this->begin();
  it.pos = static_cast<char *>(it.pos) + it.stride * this->size();
  return it;
}

template <typename T>
void Slice<T>::swap(Slice &rhs) noexcept {
  std::swap(*this, rhs);
}
#endif // CXXBRIDGE1_RUST_SLICE

#ifndef CXXBRIDGE1_RUST_BOX
#define CXXBRIDGE1_RUST_BOX
template <typename T>
class Box final {
public:
  using element_type = T;
  using const_pointer =
      typename std::add_pointer<typename std::add_const<T>::type>::type;
  using pointer = typename std::add_pointer<T>::type;

  Box() = delete;
  Box(Box &&) noexcept;
  ~Box() noexcept;

  explicit Box(const T &);
  explicit Box(T &&);

  Box &operator=(Box &&) &noexcept;

  const T *operator->() const noexcept;
  const T &operator*() const noexcept;
  T *operator->() noexcept;
  T &operator*() noexcept;

  template <typename... Fields>
  static Box in_place(Fields &&...);

  void swap(Box &) noexcept;

  static Box from_raw(T *) noexcept;

  T *into_raw() noexcept;

  /* Deprecated */ using value_type = element_type;

private:
  class uninit;
  class allocation;
  Box(uninit) noexcept;
  void drop() noexcept;

  friend void swap(Box &lhs, Box &rhs) noexcept { lhs.swap(rhs); }

  T *ptr;
};

template <typename T>
class Box<T>::uninit {};

template <typename T>
class Box<T>::allocation {
  static T *alloc() noexcept;
  static void dealloc(T *) noexcept;

public:
  allocation() noexcept : ptr(alloc()) {}
  ~allocation() noexcept {
    if (this->ptr) {
      dealloc(this->ptr);
    }
  }
  T *ptr;
};

template <typename T>
Box<T>::Box(Box &&other) noexcept : ptr(other.ptr) {
  other.ptr = nullptr;
}

template <typename T>
Box<T>::Box(const T &val) {
  allocation alloc;
  ::new (alloc.ptr) T(val);
  this->ptr = alloc.ptr;
  alloc.ptr = nullptr;
}

template <typename T>
Box<T>::Box(T &&val) {
  allocation alloc;
  ::new (alloc.ptr) T(std::move(val));
  this->ptr = alloc.ptr;
  alloc.ptr = nullptr;
}

template <typename T>
Box<T>::~Box() noexcept {
  if (this->ptr) {
    this->drop();
  }
}

template <typename T>
Box<T> &Box<T>::operator=(Box &&other) &noexcept {
  if (this->ptr) {
    this->drop();
  }
  this->ptr = other.ptr;
  other.ptr = nullptr;
  return *this;
}

template <typename T>
const T *Box<T>::operator->() const noexcept {
  return this->ptr;
}

template <typename T>
const T &Box<T>::operator*() const noexcept {
  return *this->ptr;
}

template <typename T>
T *Box<T>::operator->() noexcept {
  return this->ptr;
}

template <typename T>
T &Box<T>::operator*() noexcept {
  return *this->ptr;
}

template <typename T>
template <typename... Fields>
Box<T> Box<T>::in_place(Fields &&...fields) {
  allocation alloc;
  auto ptr = alloc.ptr;
  ::new (ptr) T{std::forward<Fields>(fields)...};
  alloc.ptr = nullptr;
  return from_raw(ptr);
}

template <typename T>
void Box<T>::swap(Box &rhs) noexcept {
  using std::swap;
  swap(this->ptr, rhs.ptr);
}

template <typename T>
Box<T> Box<T>::from_raw(T *raw) noexcept {
  Box box = uninit{};
  box.ptr = raw;
  return box;
}

template <typename T>
T *Box<T>::into_raw() noexcept {
  T *raw = this->ptr;
  this->ptr = nullptr;
  return raw;
}

template <typename T>
Box<T>::Box(uninit) noexcept {}
#endif // CXXBRIDGE1_RUST_BOX

#ifndef CXXBRIDGE1_RUST_BITCOPY_T
#define CXXBRIDGE1_RUST_BITCOPY_T
struct unsafe_bitcopy_t final {
  explicit unsafe_bitcopy_t() = default;
};
#endif // CXXBRIDGE1_RUST_BITCOPY_T

#ifndef CXXBRIDGE1_RUST_VEC
#define CXXBRIDGE1_RUST_VEC
template <typename T>
class Vec final {
public:
  using value_type = T;

  Vec() noexcept;
  Vec(std::initializer_list<T>);
  Vec(const Vec &);
  Vec(Vec &&) noexcept;
  ~Vec() noexcept;

  Vec &operator=(Vec &&) &noexcept;
  Vec &operator=(const Vec &) &;

  std::size_t size() const noexcept;
  bool empty() const noexcept;
  const T *data() const noexcept;
  T *data() noexcept;
  std::size_t capacity() const noexcept;

  const T &operator[](std::size_t n) const noexcept;
  const T &at(std::size_t n) const;
  const T &front() const noexcept;
  const T &back() const noexcept;

  T &operator[](std::size_t n) noexcept;
  T &at(std::size_t n);
  T &front() noexcept;
  T &back() noexcept;

  void reserve(std::size_t new_cap);
  void push_back(const T &value);
  void push_back(T &&value);
  template <typename... Args>
  void emplace_back(Args &&...args);
  void truncate(std::size_t len);
  void clear();

  using iterator = typename Slice<T>::iterator;
  iterator begin() noexcept;
  iterator end() noexcept;

  using const_iterator = typename Slice<const T>::iterator;
  const_iterator begin() const noexcept;
  const_iterator end() const noexcept;
  const_iterator cbegin() const noexcept;
  const_iterator cend() const noexcept;

  void swap(Vec &) noexcept;

  Vec(unsafe_bitcopy_t, const Vec &) noexcept;

private:
  void reserve_total(std::size_t new_cap) noexcept;
  void set_len(std::size_t len) noexcept;
  void drop() noexcept;

  friend void swap(Vec &lhs, Vec &rhs) noexcept { lhs.swap(rhs); }

  std::array<std::uintptr_t, 3> repr;
};

template <typename T>
Vec<T>::Vec(std::initializer_list<T> init) : Vec{} {
  this->reserve_total(init.size());
  std::move(init.begin(), init.end(), std::back_inserter(*this));
}

template <typename T>
Vec<T>::Vec(const Vec &other) : Vec() {
  this->reserve_total(other.size());
  std::copy(other.begin(), other.end(), std::back_inserter(*this));
}

template <typename T>
Vec<T>::Vec(Vec &&other) noexcept : repr(other.repr) {
  new (&other) Vec();
}

template <typename T>
Vec<T>::~Vec() noexcept {
  this->drop();
}

template <typename T>
Vec<T> &Vec<T>::operator=(Vec &&other) &noexcept {
  this->drop();
  this->repr = other.repr;
  new (&other) Vec();
  return *this;
}

template <typename T>
Vec<T> &Vec<T>::operator=(const Vec &other) & {
  if (this != &other) {
    this->drop();
    new (this) Vec(other);
  }
  return *this;
}

template <typename T>
bool Vec<T>::empty() const noexcept {
  return this->size() == 0;
}

template <typename T>
T *Vec<T>::data() noexcept {
  return const_cast<T *>(const_cast<const Vec<T> *>(this)->data());
}

template <typename T>
const T &Vec<T>::operator[](std::size_t n) const noexcept {
  assert(n < this->size());
  auto data = reinterpret_cast<const char *>(this->data());
  return *reinterpret_cast<const T *>(data + n * size_of<T>());
}

template <typename T>
const T &Vec<T>::at(std::size_t n) const {
  if (n >= this->size()) {
    panic<std::out_of_range>("rust::Vec index out of range");
  }
  return (*this)[n];
}

template <typename T>
const T &Vec<T>::front() const noexcept {
  assert(!this->empty());
  return (*this)[0];
}

template <typename T>
const T &Vec<T>::back() const noexcept {
  assert(!this->empty());
  return (*this)[this->size() - 1];
}

template <typename T>
T &Vec<T>::operator[](std::size_t n) noexcept {
  assert(n < this->size());
  auto data = reinterpret_cast<char *>(this->data());
  return *reinterpret_cast<T *>(data + n * size_of<T>());
}

template <typename T>
T &Vec<T>::at(std::size_t n) {
  if (n >= this->size()) {
    panic<std::out_of_range>("rust::Vec index out of range");
  }
  return (*this)[n];
}

template <typename T>
T &Vec<T>::front() noexcept {
  assert(!this->empty());
  return (*this)[0];
}

template <typename T>
T &Vec<T>::back() noexcept {
  assert(!this->empty());
  return (*this)[this->size() - 1];
}

template <typename T>
void Vec<T>::reserve(std::size_t new_cap) {
  this->reserve_total(new_cap);
}

template <typename T>
void Vec<T>::push_back(const T &value) {
  this->emplace_back(value);
}

template <typename T>
void Vec<T>::push_back(T &&value) {
  this->emplace_back(std::move(value));
}

template <typename T>
template <typename... Args>
void Vec<T>::emplace_back(Args &&...args) {
  auto size = this->size();
  this->reserve_total(size + 1);
  ::new (reinterpret_cast<T *>(reinterpret_cast<char *>(this->data()) +
                               size * size_of<T>()))
      T(std::forward<Args>(args)...);
  this->set_len(size + 1);
}

template <typename T>
void Vec<T>::clear() {
  this->truncate(0);
}

template <typename T>
typename Vec<T>::iterator Vec<T>::begin() noexcept {
  return Slice<T>(this->data(), this->size()).begin();
}

template <typename T>
typename Vec<T>::iterator Vec<T>::end() noexcept {
  return Slice<T>(this->data(), this->size()).end();
}

template <typename T>
typename Vec<T>::const_iterator Vec<T>::begin() const noexcept {
  return this->cbegin();
}

template <typename T>
typename Vec<T>::const_iterator Vec<T>::end() const noexcept {
  return this->cend();
}

template <typename T>
typename Vec<T>::const_iterator Vec<T>::cbegin() const noexcept {
  return Slice<const T>(this->data(), this->size()).begin();
}

template <typename T>
typename Vec<T>::const_iterator Vec<T>::cend() const noexcept {
  return Slice<const T>(this->data(), this->size()).end();
}

template <typename T>
void Vec<T>::swap(Vec &rhs) noexcept {
  using std::swap;
  swap(this->repr, rhs.repr);
}

template <typename T>
Vec<T>::Vec(unsafe_bitcopy_t, const Vec &bits) noexcept : repr(bits.repr) {}
#endif // CXXBRIDGE1_RUST_VEC

#ifndef CXXBRIDGE1_RUST_OPAQUE
#define CXXBRIDGE1_RUST_OPAQUE
class Opaque {
public:
  Opaque() = delete;
  Opaque(const Opaque &) = delete;
  ~Opaque() = delete;
};
#endif // CXXBRIDGE1_RUST_OPAQUE

#ifndef CXXBRIDGE1_IS_COMPLETE
#define CXXBRIDGE1_IS_COMPLETE
namespace detail {
namespace {
template <typename T, typename = std::size_t>
struct is_complete : std::false_type {};
template <typename T>
struct is_complete<T, decltype(sizeof(T))> : std::true_type {};
} // namespace
} // namespace detail
#endif // CXXBRIDGE1_IS_COMPLETE

#ifndef CXXBRIDGE1_LAYOUT
#define CXXBRIDGE1_LAYOUT
class layout {
  template <typename T>
  friend std::size_t size_of();
  template <typename T>
  friend std::size_t align_of();
  template <typename T>
  static typename std::enable_if<std::is_base_of<Opaque, T>::value,
                                 std::size_t>::type
  do_size_of() {
    return T::layout::size();
  }
  template <typename T>
  static typename std::enable_if<!std::is_base_of<Opaque, T>::value,
                                 std::size_t>::type
  do_size_of() {
    return sizeof(T);
  }
  template <typename T>
  static
      typename std::enable_if<detail::is_complete<T>::value, std::size_t>::type
      size_of() {
    return do_size_of<T>();
  }
  template <typename T>
  static typename std::enable_if<std::is_base_of<Opaque, T>::value,
                                 std::size_t>::type
  do_align_of() {
    return T::layout::align();
  }
  template <typename T>
  static typename std::enable_if<!std::is_base_of<Opaque, T>::value,
                                 std::size_t>::type
  do_align_of() {
    return alignof(T);
  }
  template <typename T>
  static
      typename std::enable_if<detail::is_complete<T>::value, std::size_t>::type
      align_of() {
    return do_align_of<T>();
  }
};

template <typename T>
std::size_t size_of() {
  return layout::size_of<T>();
}

template <typename T>
std::size_t align_of() {
  return layout::align_of<T>();
}
#endif // CXXBRIDGE1_LAYOUT

namespace detail {
template <typename T, typename = void *>
struct operator_new {
  void *operator()(::std::size_t sz) { return ::operator new(sz); }
};

template <typename T>
struct operator_new<T, decltype(T::operator new(sizeof(T)))> {
  void *operator()(::std::size_t sz) { return T::operator new(sz); }
};
} // namespace detail

template <typename T>
union MaybeUninit {
  T value;
  void *operator new(::std::size_t sz) { return detail::operator_new<T>{}(sz); }
  MaybeUninit() {}
  ~MaybeUninit() {}
};
} // namespace cxxbridge1
} // namespace rust

enum class QuicEventKind : ::std::uint8_t;
struct QuicEvent;
struct QuicCert;
struct AccountLogin;
struct AccountClientLogin;
struct AccountSignedCert;
struct AccountClient;
struct AccountGameServer;
struct QuicTransport;

#ifndef CXXBRIDGE1_ENUM_QuicEventKind
#define CXXBRIDGE1_ENUM_QuicEventKind
// The kind of a [`QuicEvent`].
enum class QuicEventKind : ::std::uint8_t {
  // A connection was established (`conn_id` valid; `data` holds the
  // peer's leaf certificate DER on the server side).
  Connected = 0,
  // A connection closed (`data` holds a UTF-8 reason string).
  Disconnected = 1,
  // A reliable, ordered message arrived (`data` is the payload).
  Reliable = 2,
  // An unreliable datagram arrived (`data` is the payload).
  Unreliable = 3,
};
#endif // CXXBRIDGE1_ENUM_QuicEventKind

#ifndef CXXBRIDGE1_STRUCT_QuicEvent
#define CXXBRIDGE1_STRUCT_QuicEvent
// An event drained from a [`QuicTransport`] once per tick.
struct QuicEvent final {
  // What happened.
  ::QuicEventKind kind;
  // The connection it relates to.
  ::std::uint64_t conn_id;
  // Event payload (see [`QuicEventKind`] for the meaning).
  ::rust::Vec<::std::uint8_t> data;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_QuicEvent

#ifndef CXXBRIDGE1_STRUCT_QuicCert
#define CXXBRIDGE1_STRUCT_QuicCert
// A freshly generated self-signed certificate and key.
struct QuicCert final {
  // Non-empty if generation failed (then the other fields are empty).
  ::rust::String error;
  // Certificate in DER encoding.
  ::rust::Vec<::std::uint8_t> cert_der;
  // PKCS#8 private key in DER encoding.
  ::rust::Vec<::std::uint8_t> key_der;
  // SHA-256 fingerprint of the certificate's `SubjectPublicKeyInfo`
  // (32 bytes); what a client pins to identify the server.
  ::rust::Vec<::std::uint8_t> public_key_fingerprint;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_QuicCert

#ifndef CXXBRIDGE1_STRUCT_AccountLogin
#define CXXBRIDGE1_STRUCT_AccountLogin
// Result of resolving a client certificate to an account and logging it in.
struct AccountLogin final {
  // Non-empty if resolution/login failed.
  ::rust::String error;
  // The account id, or 0 if the client has no account (public-key only).
  ::std::int64_t account_id;
  // The client's public-key fingerprint (32 bytes).
  ::rust::Vec<::std::uint8_t> public_key;
  // True if a new account row was created by this login.
  bool created;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_AccountLogin

#ifndef CXXBRIDGE1_STRUCT_AccountClientLogin
#define CXXBRIDGE1_STRUCT_AccountClientLogin
// Result of a client login attempt.
struct AccountClientLogin final {
  // Non-empty if login failed.
  ::rust::String error;
  // The logged-in account id (0 on failure).
  ::std::int64_t account_id;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_AccountClientLogin

#ifndef CXXBRIDGE1_STRUCT_AccountSignedCert
#define CXXBRIDGE1_STRUCT_AccountSignedCert
// A freshly account-signed certificate to present to a game server, with
// its private key for the QUIC mutual-TLS handshake.
struct AccountSignedCert final {
  // Non-empty if signing failed.
  ::rust::String error;
  // The signed certificate in DER encoding.
  ::rust::Vec<::std::uint8_t> cert_der;
  // The ed25519 private key (PKCS#8 DER) matching the certificate.
  ::rust::Vec<::std::uint8_t> key_der;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_AccountSignedCert

#ifndef CXXBRIDGE1_STRUCT_AccountClient
#define CXXBRIDGE1_STRUCT_AccountClient
// Client-side account operations (login, sign).
struct AccountClient final : public ::rust::Opaque {
  // Empty if opened successfully, else the reason.
  ::rust::String error() const noexcept;

  // Requests an emailed login token for `email`. The user receives a
  // code by email and passes it to `login`. Returns "" on success.
  ::rust::String request_login_token_email(::rust::Str email) const noexcept;

  // Logs in with the emailed token (creating the account on first login)
  // and persists the session.
  ::AccountClientLogin login(::rust::String token_hex) const noexcept;

  // Produces a fresh account-signed certificate to present over QUIC.
  ::AccountSignedCert sign() const noexcept;

  // Logs out the current session. Returns "" on success.
  ::rust::String logout() const noexcept;

  ~AccountClient() = delete;

private:
  friend ::rust::layout;
  struct layout {
    static ::std::size_t size() noexcept;
    static ::std::size_t align() noexcept;
  };
};
#endif // CXXBRIDGE1_STRUCT_AccountClient

#ifndef CXXBRIDGE1_STRUCT_AccountGameServer
#define CXXBRIDGE1_STRUCT_AccountGameServer
// Game-server account database + certificate verifier.
struct AccountGameServer final : public ::rust::Opaque {
  // Empty if opened successfully, else the reason.
  ::rust::String error() const noexcept;

  // Drops all trusted account-server CA keys.
  void clear_ca_certs() noexcept;

  // Adds a trusted account-server CA certificate (DER). Returns false if
  // the certificate is unusable.
  bool add_ca_cert(::rust::Slice<const ::std::uint8_t> cert_der) noexcept;

  // Downloads + trusts the account server's CA certificates from
  // `account_server_url`. Returns the number loaded, or -1 on error.
  ::std::int64_t load_account_ca_certs(::rust::Str account_server_url) noexcept;

  // Resolves the client's certificate to an account id and auto-logs the
  // user in (creating the account row on first login).
  ::AccountLogin login_by_cert(::rust::Slice<const ::std::uint8_t> peer_cert_der) const noexcept;

  // Renames the account user. `account_id` 0 means public-key-only.
  // Returns an empty string on success, else the error.
  ::rust::String rename(::std::int64_t account_id, ::rust::Slice<const ::std::uint8_t> public_key, ::rust::Str name) const noexcept;

  ~AccountGameServer() = delete;

private:
  friend ::rust::layout;
  struct layout {
    static ::std::size_t size() noexcept;
    static ::std::size_t align() noexcept;
  };
};
#endif // CXXBRIDGE1_STRUCT_AccountGameServer

#ifndef CXXBRIDGE1_STRUCT_QuicTransport
#define CXXBRIDGE1_STRUCT_QuicTransport
// A pollable QUIC endpoint (client or server).
struct QuicTransport final : public ::rust::Opaque {
  // Empty if the endpoint was created successfully, else the reason.
  ::rust::String error() const noexcept;

  // Drains all currently available events (non-blocking).
  ::rust::Vec<::QuicEvent> poll_events() const noexcept;

  // Queues a reliable, ordered message on a connection.
  void send_reliable(::std::uint64_t conn_id, ::rust::Slice<const ::std::uint8_t> data) const noexcept;

  // Queues an unreliable datagram on a connection.
  void send_unreliable(::std::uint64_t conn_id, ::rust::Slice<const ::std::uint8_t> data) const noexcept;

  // Closes a connection.
  void disconnect(::std::uint64_t conn_id) const noexcept;

  // The local UDP port the endpoint is bound to (0 if in error state).
  ::std::uint16_t local_port() const noexcept;

  ~QuicTransport() = delete;

private:
  friend ::rust::layout;
  struct layout {
    static ::std::size_t size() noexcept;
    static ::std::size_t align() noexcept;
  };
};
#endif // CXXBRIDGE1_STRUCT_QuicTransport

extern "C" {
void cxxbridge1$ddnet_accounts_version(::rust::String *return$) noexcept;
::std::size_t cxxbridge1$AccountClient$operator$sizeof() noexcept;
::std::size_t cxxbridge1$AccountClient$operator$alignof() noexcept;

::AccountClient *cxxbridge1$account_client_open(::rust::Str account_server_url, ::rust::Str secure_dir) noexcept;

void cxxbridge1$AccountClient$error(const ::AccountClient &self, ::rust::String *return$) noexcept;

void cxxbridge1$AccountClient$request_login_token_email(const ::AccountClient &self, ::rust::Str email, ::rust::String *return$) noexcept;

void cxxbridge1$AccountClient$login(const ::AccountClient &self, ::rust::String *token_hex, ::AccountClientLogin *return$) noexcept;

void cxxbridge1$AccountClient$sign(const ::AccountClient &self, ::AccountSignedCert *return$) noexcept;

void cxxbridge1$AccountClient$logout(const ::AccountClient &self, ::rust::String *return$) noexcept;
::std::size_t cxxbridge1$AccountGameServer$operator$sizeof() noexcept;
::std::size_t cxxbridge1$AccountGameServer$operator$alignof() noexcept;

::AccountGameServer *cxxbridge1$account_game_server_open(::rust::Str sqlite_path) noexcept;

void cxxbridge1$AccountGameServer$error(const ::AccountGameServer &self, ::rust::String *return$) noexcept;

void cxxbridge1$AccountGameServer$clear_ca_certs(::AccountGameServer &self) noexcept;

bool cxxbridge1$AccountGameServer$add_ca_cert(::AccountGameServer &self, ::rust::Slice<const ::std::uint8_t> cert_der) noexcept;

::std::int64_t cxxbridge1$AccountGameServer$load_account_ca_certs(::AccountGameServer &self, ::rust::Str account_server_url) noexcept;

void cxxbridge1$AccountGameServer$login_by_cert(const ::AccountGameServer &self, ::rust::Slice<const ::std::uint8_t> peer_cert_der, ::AccountLogin *return$) noexcept;

void cxxbridge1$AccountGameServer$rename(const ::AccountGameServer &self, ::std::int64_t account_id, ::rust::Slice<const ::std::uint8_t> public_key, ::rust::Str name, ::rust::String *return$) noexcept;
::std::size_t cxxbridge1$QuicTransport$operator$sizeof() noexcept;
::std::size_t cxxbridge1$QuicTransport$operator$alignof() noexcept;

void cxxbridge1$quic_generate_self_signed(::QuicCert *return$) noexcept;

void cxxbridge1$quic_load_or_generate_identity(::rust::Str cert_path, ::rust::Str key_path, ::QuicCert *return$) noexcept;

::QuicTransport *cxxbridge1$quic_server(::rust::Str bind_addr, ::rust::Slice<const ::std::uint8_t> cert_der, ::rust::Slice<const ::std::uint8_t> key_der) noexcept;

::QuicTransport *cxxbridge1$quic_client(::rust::Str server_addr, ::rust::Str server_name, ::rust::Slice<const ::std::uint8_t> pinned_fingerprint, ::rust::Slice<const ::std::uint8_t> client_cert_der, ::rust::Slice<const ::std::uint8_t> client_key_der) noexcept;

void cxxbridge1$QuicTransport$error(const ::QuicTransport &self, ::rust::String *return$) noexcept;

void cxxbridge1$QuicTransport$poll_events(const ::QuicTransport &self, ::rust::Vec<::QuicEvent> *return$) noexcept;

void cxxbridge1$QuicTransport$send_reliable(const ::QuicTransport &self, ::std::uint64_t conn_id, ::rust::Slice<const ::std::uint8_t> data) noexcept;

void cxxbridge1$QuicTransport$send_unreliable(const ::QuicTransport &self, ::std::uint64_t conn_id, ::rust::Slice<const ::std::uint8_t> data) noexcept;

void cxxbridge1$QuicTransport$disconnect(const ::QuicTransport &self, ::std::uint64_t conn_id) noexcept;

::std::uint16_t cxxbridge1$QuicTransport$local_port(const ::QuicTransport &self) noexcept;
} // extern "C"

// Returns a human readable version string for the account bridge.
::rust::String ddnet_accounts_version() noexcept {
  ::rust::MaybeUninit<::rust::String> return$;
  cxxbridge1$ddnet_accounts_version(&return$.value);
  return ::std::move(return$.value);
}

::std::size_t AccountClient::layout::size() noexcept {
  return cxxbridge1$AccountClient$operator$sizeof();
}

::std::size_t AccountClient::layout::align() noexcept {
  return cxxbridge1$AccountClient$operator$alignof();
}

// Connects an account client to `account_server_url`, storing session
// data under `secure_dir`. Always returns a handle; check `error`.
::rust::Box<::AccountClient> account_client_open(::rust::Str account_server_url, ::rust::Str secure_dir) noexcept {
  return ::rust::Box<::AccountClient>::from_raw(cxxbridge1$account_client_open(account_server_url, secure_dir));
}

::rust::String AccountClient::error() const noexcept {
  ::rust::MaybeUninit<::rust::String> return$;
  cxxbridge1$AccountClient$error(*this, &return$.value);
  return ::std::move(return$.value);
}

::rust::String AccountClient::request_login_token_email(::rust::Str email) const noexcept {
  ::rust::MaybeUninit<::rust::String> return$;
  cxxbridge1$AccountClient$request_login_token_email(*this, email, &return$.value);
  return ::std::move(return$.value);
}

::AccountClientLogin AccountClient::login(::rust::String token_hex) const noexcept {
  ::rust::MaybeUninit<::AccountClientLogin> return$;
  cxxbridge1$AccountClient$login(*this, &token_hex, &return$.value);
  return ::std::move(return$.value);
}

::AccountSignedCert AccountClient::sign() const noexcept {
  ::rust::MaybeUninit<::AccountSignedCert> return$;
  cxxbridge1$AccountClient$sign(*this, &return$.value);
  return ::std::move(return$.value);
}

::rust::String AccountClient::logout() const noexcept {
  ::rust::MaybeUninit<::rust::String> return$;
  cxxbridge1$AccountClient$logout(*this, &return$.value);
  return ::std::move(return$.value);
}

::std::size_t AccountGameServer::layout::size() noexcept {
  return cxxbridge1$AccountGameServer$operator$sizeof();
}

::std::size_t AccountGameServer::layout::align() noexcept {
  return cxxbridge1$AccountGameServer$operator$alignof();
}

// Opens (creating if needed) the SQLite account database at `path` and
// prepares it. Always returns a handle; check [`AccountGameServer::error`].
::rust::Box<::AccountGameServer> account_game_server_open(::rust::Str sqlite_path) noexcept {
  return ::rust::Box<::AccountGameServer>::from_raw(cxxbridge1$account_game_server_open(sqlite_path));
}

::rust::String AccountGameServer::error() const noexcept {
  ::rust::MaybeUninit<::rust::String> return$;
  cxxbridge1$AccountGameServer$error(*this, &return$.value);
  return ::std::move(return$.value);
}

void AccountGameServer::clear_ca_certs() noexcept {
  cxxbridge1$AccountGameServer$clear_ca_certs(*this);
}

bool AccountGameServer::add_ca_cert(::rust::Slice<const ::std::uint8_t> cert_der) noexcept {
  return cxxbridge1$AccountGameServer$add_ca_cert(*this, cert_der);
}

::std::int64_t AccountGameServer::load_account_ca_certs(::rust::Str account_server_url) noexcept {
  return cxxbridge1$AccountGameServer$load_account_ca_certs(*this, account_server_url);
}

::AccountLogin AccountGameServer::login_by_cert(::rust::Slice<const ::std::uint8_t> peer_cert_der) const noexcept {
  ::rust::MaybeUninit<::AccountLogin> return$;
  cxxbridge1$AccountGameServer$login_by_cert(*this, peer_cert_der, &return$.value);
  return ::std::move(return$.value);
}

::rust::String AccountGameServer::rename(::std::int64_t account_id, ::rust::Slice<const ::std::uint8_t> public_key, ::rust::Str name) const noexcept {
  ::rust::MaybeUninit<::rust::String> return$;
  cxxbridge1$AccountGameServer$rename(*this, account_id, public_key, name, &return$.value);
  return ::std::move(return$.value);
}

::std::size_t QuicTransport::layout::size() noexcept {
  return cxxbridge1$QuicTransport$operator$sizeof();
}

::std::size_t QuicTransport::layout::align() noexcept {
  return cxxbridge1$QuicTransport$operator$alignof();
}

// Generates an ed25519 self-signed certificate + key. On failure the
// returned [`QuicCert`] has a non-empty `error`.
::QuicCert quic_generate_self_signed() noexcept {
  ::rust::MaybeUninit<::QuicCert> return$;
  cxxbridge1$quic_generate_self_signed(&return$.value);
  return ::std::move(return$.value);
}

// Loads the server's persistent identity from `cert_path`/`key_path`,
// generating and saving a new one if absent. Keeps the server's
// fingerprint stable across restarts. On failure `error` is non-empty.
::QuicCert quic_load_or_generate_identity(::rust::Str cert_path, ::rust::Str key_path) noexcept {
  ::rust::MaybeUninit<::QuicCert> return$;
  cxxbridge1$quic_load_or_generate_identity(cert_path, key_path, &return$.value);
  return ::std::move(return$.value);
}

// Starts a QUIC server bound to `bind_addr` (e.g. "0.0.0.0:8303"),
// presenting the given certificate and requiring a client cert.
// Always returns a handle; check [`QuicTransport::error`].
::rust::Box<::QuicTransport> quic_server(::rust::Str bind_addr, ::rust::Slice<const ::std::uint8_t> cert_der, ::rust::Slice<const ::std::uint8_t> key_der) noexcept {
  return ::rust::Box<::QuicTransport>::from_raw(cxxbridge1$quic_server(bind_addr, cert_der, key_der));
}

// Connects a QUIC client to `server_addr`. If `pinned_fingerprint` is
// 32 bytes it pins the server's public-key fingerprint; if empty the
// server cert is accepted unconditionally (insecure, LAN only). The
// client presents `client_cert_der`/`client_key_der` as its identity.
// Always returns a handle; check [`QuicTransport::error`].
::rust::Box<::QuicTransport> quic_client(::rust::Str server_addr, ::rust::Str server_name, ::rust::Slice<const ::std::uint8_t> pinned_fingerprint, ::rust::Slice<const ::std::uint8_t> client_cert_der, ::rust::Slice<const ::std::uint8_t> client_key_der) noexcept {
  return ::rust::Box<::QuicTransport>::from_raw(cxxbridge1$quic_client(server_addr, server_name, pinned_fingerprint, client_cert_der, client_key_der));
}

::rust::String QuicTransport::error() const noexcept {
  ::rust::MaybeUninit<::rust::String> return$;
  cxxbridge1$QuicTransport$error(*this, &return$.value);
  return ::std::move(return$.value);
}

::rust::Vec<::QuicEvent> QuicTransport::poll_events() const noexcept {
  ::rust::MaybeUninit<::rust::Vec<::QuicEvent>> return$;
  cxxbridge1$QuicTransport$poll_events(*this, &return$.value);
  return ::std::move(return$.value);
}

void QuicTransport::send_reliable(::std::uint64_t conn_id, ::rust::Slice<const ::std::uint8_t> data) const noexcept {
  cxxbridge1$QuicTransport$send_reliable(*this, conn_id, data);
}

void QuicTransport::send_unreliable(::std::uint64_t conn_id, ::rust::Slice<const ::std::uint8_t> data) const noexcept {
  cxxbridge1$QuicTransport$send_unreliable(*this, conn_id, data);
}

void QuicTransport::disconnect(::std::uint64_t conn_id) const noexcept {
  cxxbridge1$QuicTransport$disconnect(*this, conn_id);
}

::std::uint16_t QuicTransport::local_port() const noexcept {
  return cxxbridge1$QuicTransport$local_port(*this);
}

extern "C" {
::AccountClient *cxxbridge1$box$AccountClient$alloc() noexcept;
void cxxbridge1$box$AccountClient$dealloc(::AccountClient *) noexcept;
void cxxbridge1$box$AccountClient$drop(::rust::Box<::AccountClient> *ptr) noexcept;

::AccountGameServer *cxxbridge1$box$AccountGameServer$alloc() noexcept;
void cxxbridge1$box$AccountGameServer$dealloc(::AccountGameServer *) noexcept;
void cxxbridge1$box$AccountGameServer$drop(::rust::Box<::AccountGameServer> *ptr) noexcept;

::QuicTransport *cxxbridge1$box$QuicTransport$alloc() noexcept;
void cxxbridge1$box$QuicTransport$dealloc(::QuicTransport *) noexcept;
void cxxbridge1$box$QuicTransport$drop(::rust::Box<::QuicTransport> *ptr) noexcept;

void cxxbridge1$rust_vec$QuicEvent$new(const ::rust::Vec<::QuicEvent> *ptr) noexcept;
void cxxbridge1$rust_vec$QuicEvent$drop(::rust::Vec<::QuicEvent> *ptr) noexcept;
::std::size_t cxxbridge1$rust_vec$QuicEvent$len(const ::rust::Vec<::QuicEvent> *ptr) noexcept;
::std::size_t cxxbridge1$rust_vec$QuicEvent$capacity(const ::rust::Vec<::QuicEvent> *ptr) noexcept;
const ::QuicEvent *cxxbridge1$rust_vec$QuicEvent$data(const ::rust::Vec<::QuicEvent> *ptr) noexcept;
void cxxbridge1$rust_vec$QuicEvent$reserve_total(::rust::Vec<::QuicEvent> *ptr, ::std::size_t new_cap) noexcept;
void cxxbridge1$rust_vec$QuicEvent$set_len(::rust::Vec<::QuicEvent> *ptr, ::std::size_t len) noexcept;
void cxxbridge1$rust_vec$QuicEvent$truncate(::rust::Vec<::QuicEvent> *ptr, ::std::size_t len) noexcept;
} // extern "C"

namespace rust {
inline namespace cxxbridge1 {
template <>
::AccountClient *Box<::AccountClient>::allocation::alloc() noexcept {
  return cxxbridge1$box$AccountClient$alloc();
}
template <>
void Box<::AccountClient>::allocation::dealloc(::AccountClient *ptr) noexcept {
  cxxbridge1$box$AccountClient$dealloc(ptr);
}
template <>
void Box<::AccountClient>::drop() noexcept {
  cxxbridge1$box$AccountClient$drop(this);
}
template <>
::AccountGameServer *Box<::AccountGameServer>::allocation::alloc() noexcept {
  return cxxbridge1$box$AccountGameServer$alloc();
}
template <>
void Box<::AccountGameServer>::allocation::dealloc(::AccountGameServer *ptr) noexcept {
  cxxbridge1$box$AccountGameServer$dealloc(ptr);
}
template <>
void Box<::AccountGameServer>::drop() noexcept {
  cxxbridge1$box$AccountGameServer$drop(this);
}
template <>
::QuicTransport *Box<::QuicTransport>::allocation::alloc() noexcept {
  return cxxbridge1$box$QuicTransport$alloc();
}
template <>
void Box<::QuicTransport>::allocation::dealloc(::QuicTransport *ptr) noexcept {
  cxxbridge1$box$QuicTransport$dealloc(ptr);
}
template <>
void Box<::QuicTransport>::drop() noexcept {
  cxxbridge1$box$QuicTransport$drop(this);
}
template <>
Vec<::QuicEvent>::Vec() noexcept {
  cxxbridge1$rust_vec$QuicEvent$new(this);
}
template <>
void Vec<::QuicEvent>::drop() noexcept {
  return cxxbridge1$rust_vec$QuicEvent$drop(this);
}
template <>
::std::size_t Vec<::QuicEvent>::size() const noexcept {
  return cxxbridge1$rust_vec$QuicEvent$len(this);
}
template <>
::std::size_t Vec<::QuicEvent>::capacity() const noexcept {
  return cxxbridge1$rust_vec$QuicEvent$capacity(this);
}
template <>
const ::QuicEvent *Vec<::QuicEvent>::data() const noexcept {
  return cxxbridge1$rust_vec$QuicEvent$data(this);
}
template <>
void Vec<::QuicEvent>::reserve_total(::std::size_t new_cap) noexcept {
  return cxxbridge1$rust_vec$QuicEvent$reserve_total(this, new_cap);
}
template <>
void Vec<::QuicEvent>::set_len(::std::size_t len) noexcept {
  return cxxbridge1$rust_vec$QuicEvent$set_len(this, len);
}
template <>
void Vec<::QuicEvent>::truncate(::std::size_t len) {
  return cxxbridge1$rust_vec$QuicEvent$truncate(this, len);
}
} // namespace cxxbridge1
} // namespace rust
