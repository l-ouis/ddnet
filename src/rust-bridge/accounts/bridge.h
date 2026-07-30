#pragma once
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
#if __cplusplus >= 201703L
#include <string_view>
#endif
#if __cplusplus >= 202002L
#include <ranges>
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
#endif // __clang__

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
#ifdef __cpp_char8_t
  String(const char8_t *s);
  String(const char8_t *s, std::size_t len);
#endif

  static String lossy(const std::string &) noexcept;
  static String lossy(const char *) noexcept;
  static String lossy(const char *, std::size_t) noexcept;
  static String lossy(const char16_t *) noexcept;
  static String lossy(const char16_t *, std::size_t) noexcept;

  String &operator=(const String &) & noexcept;
  String &operator=(String &&) & noexcept;

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

  Str &operator=(const Str &) & noexcept = default;

  explicit operator std::string() const;
#if __cplusplus >= 201703L
  explicit operator std::string_view() const;
#endif

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
  copy_assignable_if &operator=(const copy_assignable_if &) & noexcept = delete;
  copy_assignable_if &operator=(copy_assignable_if &&) & noexcept = default;
};
} // namespace detail

template <typename T>
class Slice final
    : private detail::copy_assignable_if<std::is_const<T>::value> {
public:
  using value_type = T;

  Slice() noexcept;
  Slice(T *, std::size_t count) noexcept;

  template <typename C>
  explicit Slice(C &c) : Slice(c.data(), c.size()) {}

  Slice &operator=(const Slice<T> &) & noexcept = default;
  Slice &operator=(Slice<T> &&) & noexcept = default;

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

#ifdef __cpp_deduction_guides
template <typename C>
explicit Slice(C &c)
    -> Slice<std::remove_reference_t<decltype(*std::declval<C>().data())>>;
#endif // __cpp_deduction_guides

template <typename T>
class Slice<T>::iterator final {
public:
#if __cplusplus >= 202002L
  using iterator_category = std::contiguous_iterator_tag;
#else
  using iterator_category = std::random_access_iterator_tag;
#endif
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
  friend inline iterator operator+(difference_type lhs, iterator rhs) noexcept {
    return rhs + lhs;
  }
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

#if __cplusplus >= 202002L
static_assert(std::ranges::contiguous_range<rust::Slice<const uint8_t>>);
static_assert(std::contiguous_iterator<rust::Slice<const uint8_t>::iterator>);
#endif

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
  return diff / static_cast<typename Slice<T>::iterator::difference_type>(
                    this->stride);
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

  Box &operator=(Box &&) & noexcept;

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
Box<T> &Box<T>::operator=(Box &&other) & noexcept {
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

  Vec &operator=(Vec &&) & noexcept;
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
Vec<T> &Vec<T>::operator=(Vec &&other) & noexcept {
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
} // namespace cxxbridge1
} // namespace rust

#if __cplusplus >= 201402L
#define CXX_DEFAULT_VALUE(value) = value
#else
#define CXX_DEFAULT_VALUE(value)
#endif

namespace accounts {
  enum class EAccountEventKind : ::std::uint8_t;
  enum class EAccountErrorKind : ::std::uint8_t;
  enum class ECredentialAuthOp : ::std::uint8_t;
  enum class EAccountOp : ::std::uint8_t;
  struct SAccountCredential;
  struct SAccountEvent;
  struct SAccountProfile;
  enum class EQuicEventKind : ::std::uint8_t;
  struct SQuicEvent;
  struct SGameServerLogin;
  struct SServerIdentity;
  struct CAccountsClient;
  struct CQuicClient;
  struct CQuicServer;
  struct CAccountsGameServer;
}

namespace accounts {
#ifndef CXXBRIDGE1_ENUM_accounts$EAccountEventKind
#define CXXBRIDGE1_ENUM_accounts$EAccountEventKind
// What kind of operation an account event belongs to.
enum class EAccountEventKind : ::std::uint8_t {
  CREDENTIAL_AUTH_EMAIL_TOKEN = 0,
  CREDENTIAL_AUTH_STEAM_TOKEN = 1,
  ACCOUNT_EMAIL_TOKEN = 2,
  ACCOUNT_STEAM_TOKEN = 3,
  LOGIN = 4,
  LOGOUT = 5,
  LOGOUT_ALL = 6,
  DELETE = 7,
  LINK_CREDENTIAL = 8,
  UNLINK_CREDENTIAL = 9,
  ACCOUNT_INFO = 10,
  CERT_AND_KEY = 11,
};
#endif // CXXBRIDGE1_ENUM_accounts$EAccountEventKind

#ifndef CXXBRIDGE1_ENUM_accounts$EAccountErrorKind
#define CXXBRIDGE1_ENUM_accounts$EAccountErrorKind
// Error class of a failed account operation.
enum class EAccountErrorKind : ::std::uint8_t {
  NONE = 0,
  HTTP = 1,
  FS = 2,
  RATE_LIMITED = 3,
  VPN_BAN = 4,
  WEB_VALIDATION_NEEDED = 5,
  OTHER = 6,
};
#endif // CXXBRIDGE1_ENUM_accounts$EAccountErrorKind

#ifndef CXXBRIDGE1_ENUM_accounts$ECredentialAuthOp
#define CXXBRIDGE1_ENUM_accounts$ECredentialAuthOp
// Operations that need a credential auth token.
enum class ECredentialAuthOp : ::std::uint8_t {
  LOGIN = 0,
  LINK_CREDENTIAL = 1,
  UNLINK_CREDENTIAL = 2,
};
#endif // CXXBRIDGE1_ENUM_accounts$ECredentialAuthOp

#ifndef CXXBRIDGE1_ENUM_accounts$EAccountOp
#define CXXBRIDGE1_ENUM_accounts$EAccountOp
// Operations that need an account token.
enum class EAccountOp : ::std::uint8_t {
  LOGOUT_ALL = 0,
  LINK_CREDENTIAL = 1,
  DELETE = 2,
};
#endif // CXXBRIDGE1_ENUM_accounts$EAccountOp

#ifndef CXXBRIDGE1_STRUCT_accounts$SAccountCredential
#define CXXBRIDGE1_STRUCT_accounts$SAccountCredential
// A linked credential of an account.
struct SAccountCredential final {
  // "email" or "steam".
  ::rust::String m_Kind;
  // Partially masked email address or steam id.
  ::rust::String m_Identifier;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_accounts$SAccountCredential

#ifndef CXXBRIDGE1_STRUCT_accounts$SAccountEvent
#define CXXBRIDGE1_STRUCT_accounts$SAccountEvent
// Completion of an asynchronous account operation.
struct SAccountEvent final {
  // False if no event was pending.
  bool m_Valid CXX_DEFAULT_VALUE(false);
  // Id that was returned when the operation was started. 0 means
  // the event is unsolicited, currently only LOGOUT events for a
  // profile that was removed as side effect of CERT_AND_KEY (the
  // removed profile key is in the payload).
  ::std::uint64_t m_RequestId CXX_DEFAULT_VALUE(0);
  // Operation this event belongs to.
  ::accounts::EAccountEventKind m_Kind;
  // Whether the operation succeeded.
  bool m_Success CXX_DEFAULT_VALUE(false);
  // Error class, NONE on success.
  ::accounts::EAccountErrorKind m_ErrorKind;
  // Human readable error description, empty on success.
  ::rust::String m_Error;
  // Human readable warning for operations that succeeded in a
  // degraded way, e.g. CERT_AND_KEY falling back to a self signed
  // certificate.
  ::rust::String m_Warning;
  // Operation specific payload: profile key for LOGIN, token for
  // steam token operations, url for WEB_VALIDATION_NEEDED errors,
  // removed profile key for unsolicited LOGOUT events.
  ::rust::String m_Payload;
  // Certificate in der format, for CERT_AND_KEY.
  ::rust::Vec<::std::uint8_t> m_aCertDer;
  // Private session key in pkcs8 der format, for CERT_AND_KEY.
  ::rust::Vec<::std::uint8_t> m_aKeyDer;
  // Account id, for ACCOUNT_INFO.
  ::std::int64_t m_AccountId CXX_DEFAULT_VALUE(0);
  // Account creation date as displayable string, for ACCOUNT_INFO.
  ::rust::String m_CreationDate;
  // Linked credentials, for ACCOUNT_INFO.
  ::rust::Vec<::accounts::SAccountCredential> m_vCredentials;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_accounts$SAccountEvent

#ifndef CXXBRIDGE1_STRUCT_accounts$SAccountProfile
#define CXXBRIDGE1_STRUCT_accounts$SAccountProfile
// Info about a stored account profile.
struct SAccountProfile final {
  // Key of the profile, `acc_<account id>`.
  ::rust::String m_Key;
  // Display name, usually derived from the login credential.
  ::rust::String m_DisplayName;
  // Whether this is the currently active profile.
  bool m_Current CXX_DEFAULT_VALUE(false);

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_accounts$SAccountProfile

#ifndef CXXBRIDGE1_ENUM_accounts$EQuicEventKind
#define CXXBRIDGE1_ENUM_accounts$EQuicEventKind
// What kind of transport event happened.
enum class EQuicEventKind : ::std::uint8_t {
  NONE = 0,
  CONNECTED = 1,
  CHUNK = 2,
  DISCONNECTED = 3,
};
#endif // CXXBRIDGE1_ENUM_accounts$EQuicEventKind

#ifndef CXXBRIDGE1_STRUCT_accounts$SQuicEvent
#define CXXBRIDGE1_STRUCT_accounts$SQuicEvent
// A transport event of a QUIC endpoint.
struct SQuicEvent final {
  // False if no event was pending.
  bool m_Valid CXX_DEFAULT_VALUE(false);
  // Kind of the event.
  ::accounts::EQuicEventKind m_Kind;
  // Id of the peer the event belongs to. Always 0 on the client.
  ::std::uint64_t m_PeerId CXX_DEFAULT_VALUE(0);
  // CONNECTED: remote address of the peer as string.
  ::rust::String m_Addr;
  // CONNECTED on the server: certificate the peer presented during
  // the TLS handshake, in der format.
  ::rust::Vec<::std::uint8_t> m_aCertDer;
  // CHUNK: the payload.
  ::rust::Vec<::std::uint8_t> m_aData;
  // CHUNK: whether it was sent as unreliable datagram.
  bool m_Unreliable CXX_DEFAULT_VALUE(false);
  // DISCONNECTED: reason.
  ::rust::String m_Reason;
  // DISCONNECTED: whether the peer or the network caused the
  // disconnect, rather than the local side.
  bool m_Remote CXX_DEFAULT_VALUE(false);

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_accounts$SQuicEvent

#ifndef CXXBRIDGE1_STRUCT_accounts$SGameServerLogin
#define CXXBRIDGE1_STRUCT_accounts$SGameServerLogin
// Result of resolving the account of a connecting client.
struct SGameServerLogin final {
  // False if no login was pending.
  bool m_Valid CXX_DEFAULT_VALUE(false);
  // Id that was returned by `BeginLogin`.
  ::std::uint64_t m_RequestId CXX_DEFAULT_VALUE(0);
  // The account id, 0 if the client has no (valid) account or the
  // database registration failed (the client must be treated as
  // anonymous then).
  ::std::int64_t m_AccountId CXX_DEFAULT_VALUE(0);
  // Sha256 fingerprint of the public key of the client certificate.
  ::rust::Vec<::std::uint8_t> m_aPublicKeyHash;
  // Whether this account was seen the first time on this server.
  bool m_NewAccount CXX_DEFAULT_VALUE(false);
  // Error description if the login could not be resolved.
  ::rust::String m_Error;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_accounts$SGameServerLogin

#ifndef CXXBRIDGE1_STRUCT_accounts$SServerIdentity
#define CXXBRIDGE1_STRUCT_accounts$SServerIdentity
// Persistent TLS identity of a game server.
struct SServerIdentity final {
  // Empty on success, error description otherwise.
  ::rust::String m_Error;
  // Self signed certificate in der format.
  ::rust::Vec<::std::uint8_t> m_aCertDer;
  // Private key in pkcs8 der format.
  ::rust::Vec<::std::uint8_t> m_aKeyDer;
  // Sha256 fingerprint of the subject public key info of the
  // certificate. This is what clients pin.
  ::rust::Vec<::std::uint8_t> m_aPublicKeyHash;

  using IsRelocatable = ::std::true_type;
};
#endif // CXXBRIDGE1_STRUCT_accounts$SServerIdentity

#ifndef CXXBRIDGE1_STRUCT_accounts$CAccountsClient
#define CXXBRIDGE1_STRUCT_accounts$CAccountsClient
// Client side account manager, see `AccountsClient` in the
// `ddnet-accounts-bridge` crate.
struct CAccountsClient final : public ::rust::Opaque {
  // The error that occurred during creation, empty if none.
  ::rust::String Error() const noexcept;

  // Polls the next completed operation.
  ::accounts::SAccountEvent PollEvent() const noexcept;

  // Requests a credential auth token, sent to the given email
  // address. Empty `SecretKeyHex` means no secret key.
  ::std::uint64_t CredentialAuthEmailToken(::rust::Str Email, ::accounts::ECredentialAuthOp Op, ::rust::Str SecretKeyHex) const noexcept;

  // Requests a credential auth token for the given steam session
  // ticket. On success the token is in the payload of the event.
  ::std::uint64_t CredentialAuthSteamToken(::rust::Slice<::std::uint8_t const> SteamTicket, ::accounts::ECredentialAuthOp Op, ::rust::Str SecretKeyHex) const noexcept;

  // Requests an account token, sent to the given email address.
  ::std::uint64_t AccountEmailToken(::rust::Str Email, ::accounts::EAccountOp Op, ::rust::Str SecretKeyHex) const noexcept;

  // Requests an account token for the given steam session ticket.
  // On success the token is in the payload of the event.
  ::std::uint64_t AccountSteamToken(::rust::Slice<::std::uint8_t const> SteamTicket, ::accounts::EAccountOp Op, ::rust::Str SecretKeyHex) const noexcept;

  // Logs in with the credential auth token that was sent by email.
  ::std::uint64_t LoginEmail(::rust::Str Email, ::rust::Str CredentialAuthTokenHex) const noexcept;

  // Logs in with a credential auth token obtained for a steam
  // session ticket.
  ::std::uint64_t LoginSteam(::rust::Str SteamUserName, ::rust::Str CredentialAuthTokenHex) const noexcept;

  // Logs out the given profile and removes it from disk.
  ::std::uint64_t Logout(::rust::Str ProfileKey) const noexcept;

  // Logs out all other sessions of the account of the profile.
  ::std::uint64_t LogoutAll(::rust::Str ProfileKey, ::rust::Str AccountTokenHex) const noexcept;

  // Deletes the account of the given profile.
  ::std::uint64_t Delete(::rust::Str ProfileKey, ::rust::Str AccountTokenHex) const noexcept;

  // Links another credential to the account of the given profile.
  ::std::uint64_t LinkCredential(::rust::Str ProfileKey, ::rust::Str AccountTokenHex, ::rust::Str CredentialAuthTokenHex) const noexcept;

  // Unlinks a credential from the account of the given profile.
  ::std::uint64_t UnlinkCredential(::rust::Str ProfileKey, ::rust::Str CredentialAuthTokenHex) const noexcept;

  // Fetches the account info of the given profile.
  ::std::uint64_t AccountInfo(::rust::Str ProfileKey) const noexcept;

  // Requests a certificate and session key for connecting to a game
  // server, also used to refresh a certificate that is about to
  // expire. Also works without an account (self signed cert, with a
  // warning in the event).
  ::std::uint64_t RequestCertAndKey() const noexcept;

  // Currently stored profiles.
  ::rust::Vec<::accounts::SAccountProfile> Profiles() const noexcept;

  // Switches the active profile.
  void SetProfile(::rust::Str ProfileKey) const noexcept;

  // Changes the display name of a profile.
  void SetProfileDisplayName(::rust::Str ProfileKey, ::rust::Str DisplayName) const noexcept;

  ~CAccountsClient() = delete;

private:
  friend ::rust::layout;
  struct layout {
    static ::std::size_t size() noexcept;
    static ::std::size_t align() noexcept;
  };
};
#endif // CXXBRIDGE1_STRUCT_accounts$CAccountsClient

#ifndef CXXBRIDGE1_STRUCT_accounts$CQuicClient
#define CXXBRIDGE1_STRUCT_accounts$CQuicClient
// Client side of the QUIC transport.
struct CQuicClient final : public ::rust::Opaque {
  // Polls the next transport event.
  ::accounts::SQuicEvent PollEvent() noexcept;

  // Sends a chunk to the server. Returns false if the chunk could
  // not even be queued.
  bool Send(::rust::Slice<::std::uint8_t const> Data, bool Unreliable) const noexcept;

  // Closes the connection with the given reason.
  void Close(::rust::Str Reason) const noexcept;

  // Milliseconds since the last time data arrived from the server,
  // 0 while the connection is not established.
  ::std::uint64_t MillisSinceReceive() const noexcept;

  // Current smoothed round trip time to the server in milliseconds.
  ::std::uint64_t RttMillis() const noexcept;

  ~CQuicClient() = delete;

private:
  friend ::rust::layout;
  struct layout {
    static ::std::size_t size() noexcept;
    static ::std::size_t align() noexcept;
  };
};
#endif // CXXBRIDGE1_STRUCT_accounts$CQuicClient

#ifndef CXXBRIDGE1_STRUCT_accounts$CQuicServer
#define CXXBRIDGE1_STRUCT_accounts$CQuicServer
// Server side of the QUIC transport.
struct CQuicServer final : public ::rust::Opaque {
  // The error that occurred while opening the endpoint, empty if
  // none.
  ::rust::String Error() const noexcept;

  // The port the endpoint is bound to, 0 on error.
  ::std::uint16_t Port() const noexcept;

  // Polls the next transport event.
  ::accounts::SQuicEvent PollEvent() noexcept;

  // Sends a chunk to the given peer. Returns false if the chunk
  // could not even be queued.
  bool Send(::std::uint64_t PeerId, ::rust::Slice<::std::uint8_t const> Data, bool Unreliable) const noexcept;

  // Closes the connection to the given peer with the given reason.
  void ClosePeer(::std::uint64_t PeerId, ::rust::Str Reason) const noexcept;

  // Current smoothed round trip time to the peer in milliseconds.
  ::std::uint64_t RttMillis(::std::uint64_t PeerId) const noexcept;

  // Milliseconds since the last stream frame or datagram arrived
  // from the peer, -1 if the peer is unknown. QUIC keep alives do
  // not count, so this is an application level liveness signal.
  ::std::int64_t MillisSinceReceive(::std::uint64_t PeerId) const noexcept;

  ~CQuicServer() = delete;

private:
  friend ::rust::layout;
  struct layout {
    static ::std::size_t size() noexcept;
    static ::std::size_t align() noexcept;
  };
};
#endif // CXXBRIDGE1_STRUCT_accounts$CQuicServer

#ifndef CXXBRIDGE1_STRUCT_accounts$CAccountsGameServer
#define CXXBRIDGE1_STRUCT_accounts$CAccountsGameServer
// Game server side account manager.
struct CAccountsGameServer final : public ::rust::Opaque {
  // The hard configuration error (invalid account server url) that
  // occurred during creation, empty if none. Network failures are
  // not reported here, initialization keeps retrying.
  ::rust::String Error() const noexcept;

  // Starts resolving the account for the given client certificate.
  // Logins that arrive before initialization finished are queued.
  ::std::uint64_t BeginLogin(::rust::Slice<::std::uint8_t const> CertDer) const noexcept;

  // Polls the next resolved login.
  ::accounts::SGameServerLogin PollLogin() const noexcept;

  ~CAccountsGameServer() = delete;

private:
  friend ::rust::layout;
  struct layout {
    static ::std::size_t size() noexcept;
    static ::std::size_t align() noexcept;
  };
};
#endif // CXXBRIDGE1_STRUCT_accounts$CAccountsGameServer

// Creates the account manager with profile storage below
// `BasePath` and the given account server url.
::rust::Box<::accounts::CAccountsClient> CreateAccountsClient(::rust::Str BasePath, ::rust::Str AccountServerUrl) noexcept;

// Creates the client and starts connecting to `Addr` in the
// background. `BindAddr` is the local IP without port to bind the
// endpoint to, empty for the unspecified address of the target's
// address family; a bind address that cannot be parsed, bound or
// whose address family does not match the target fails the
// connect. The server certificate is verified against the sha256
// fingerprint `ServerPubKeyHash` (32 bytes). `CertDer` and
// `KeyDer` are the own certificate and pkcs8 session key.
::rust::Box<::accounts::CQuicClient> CreateQuicClient(::rust::Str Addr, ::rust::Str BindAddr, ::rust::Slice<::std::uint8_t const> ServerPubKeyHash, ::rust::Slice<::std::uint8_t const> CertDer, ::rust::Slice<::std::uint8_t const> KeyDer, ::std::uint64_t IdleTimeoutMs) noexcept;

// Opens a QUIC endpoint on `BindAddr` with the given TLS identity,
// usually from `LoadOrGenerateServerIdentity`.
::rust::Box<::accounts::CQuicServer> CreateQuicServer(::rust::Str BindAddr, ::rust::Slice<::std::uint8_t const> CertDer, ::rust::Slice<::std::uint8_t const> KeyDer, ::std::uint64_t IdleTimeoutMs, ::std::size_t MaxPeers) noexcept;

// Creates the account manager. `DbFilePath` is the sqlite database
// for the user table, `StoragePath` caches the account server
// certificates. Returns immediately, initialization runs in the
// background and is retried until it succeeds.
::rust::Box<::accounts::CAccountsGameServer> CreateAccountsGameServer(::rust::Str DbFilePath, ::rust::Str StoragePath, ::rust::Str AccountServerUrl) noexcept;

// Loads the server key from `KeyPath`, generating and persisting a
// new one if the file does not exist, and creates a self signed
// certificate for it.
::accounts::SServerIdentity LoadOrGenerateServerIdentity(::rust::Str KeyPath) noexcept;

// Seconds until the given der certificate expires. Negative if
// already expired.
::std::int64_t CertExpiresInSeconds(::rust::Slice<::std::uint8_t const> CertDer) noexcept;
} // namespace accounts

#ifdef __clang__
#pragma clang diagnostic pop
#endif // __clang__
