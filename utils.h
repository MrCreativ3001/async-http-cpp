#ifndef CPP_ASYNC_HTTP_UTILS_H
#define CPP_ASYNC_HTTP_UTILS_H

#include <type_traits>

#include "new"

struct void_ {
    void_() = default;

    // TODO: remove this constructor
    template <typename... Types>
    void_(Types...) {}
};

namespace template_utils {

template <typename T>
T &&declval() noexcept {
    static_assert(true, "declval() must not be used in an evaluated context");
}

template <typename T, T value_>
struct const_value {
    static constexpr const T value = value_;
};

template <bool B, typename T, typename F>
struct conditional_type {
    typedef T type;
};

template <typename T, typename F>
struct conditional_type<false, T, F> {
    typedef F type;
};

template <typename V, bool B, V T, V F>
struct conditional_value {
    static constexpr const V value = T;
};

template <typename V, V T, V F>
struct conditional_value<V, false, T, F> {
    static constexpr const V value = F;
};

template <typename T>
struct is_pointer {
    static const bool value = false;
};

template <typename T>
struct is_pointer<T *> {
    static const bool value = true;
};

template <typename T>
struct is_pointer<T *const> {
    static const bool value = true;
};

template <typename T>
struct is_pointer<T *volatile> {
    static const bool value = true;
};

template <typename T>
struct is_pointer<T *const volatile> {
    static const bool value = true;
};

template <typename T>
struct remove_pointer {
    typedef T type;
};

template <typename T>
struct remove_pointer<T *> {
    typedef T type;
};

template <typename T>
struct remove_pointer<T *const> {
    typedef T type;
};

template <typename T>
struct remove_pointer<T *volatile> {
    typedef T type;
};

template <typename T>
struct remove_pointer<T *const volatile> {
    typedef T type;
};

template <typename T1, typename T2>
struct is_type_equal {
    static constexpr const bool value = false;
};

template <typename T>
struct is_type_equal<T, T> {
    static constexpr const bool value = true;
};

template <size_t N>
constexpr size_t const_str_length(char const (&)[N]) {
    return N - 1;
}

template <typename T, T... values>
struct max_value {};

template <typename T, T value1, T... others>
struct max_value<T, value1, others...> {
    static constexpr const T value =
        max(value1, max_value<T, others...>::value);
};

template <typename T, T value1>
struct max_value<T, value1> {
    static constexpr const T value = value1;
};

template <typename T>
constexpr typename remove_pointer<T>::type &&move(T &&t) {
    return static_cast<typename remove_pointer<T>::type>(t);
}

template <typename... T>
struct pack {
    static const bool exists = false;

    template <size_t Nth>
    using N = void_;

    static const size_t length = 0;
};

template <typename T, typename... Ts>
struct pack<T, Ts...> {
    static const bool exists = true;
    typedef T current;
    typedef pack<Ts...> next;

    static const size_t length = 1 + next::length;

    template <size_t Nth>
    using N =
        typename conditional_type<Nth == 0, current,
                                  typename next::template N<Nth - 1>>::type;
};

template <typename Pack, typename UnpackInto>
struct unpack {};
template <typename UnpackInto, typename... Ts>
struct unpack<pack<Ts...>, UnpackInto> {
    typedef typename UnpackInto::template type<Ts...> type;
};

template <typename T, T... Values>
struct t_pack {
    static const bool exists = false;

    static const size_t length = 0;
};

template <typename T, T V, T... Vs>
struct t_pack<T, V, Vs...> {
    static const bool exists = true;

    static constexpr const T current = V;
    typedef t_pack<T, Vs...> next;

    template <size_t Nth>
    static constexpr const T N = Nth == 0 ? current : next::template N<Nth - 1>;

    static const size_t length = 1 + next::length;
};

template <typename Struct>
struct struct_reflection {
    static constexpr const bool exists = false;

    static constexpr const size_t members = 0;

    typedef pack<> member_types;

    template <size_t MemberIndex>
    static constexpr const char *member_name = nullptr;
    template <size_t MemberIndex>
    static constexpr const size_t member_name_length = 0;

    template <size_t MemberIndex>
    static member_types::N<MemberIndex> *getMember(Struct *s) = delete;
};

template <typename Fn>
struct function_signature {
    static constexpr const bool value = false;
};

template <typename Return_, typename... Args_>
struct function_signature<Return_ (*)(Args_...)> {
    static constexpr const bool value = true;
    typedef Return_ Return;
    typedef pack<Args_...> Args;
};

};  // namespace template_utils

#define REFLECTION_STRUCT_END(...) REFLECTION_STRUCT_END_(__VA_ARGS__)
#define REFLECTION_STRUCT_END_(...) __VA_ARGS__##_end

#define REFLECTION_STRUCT_MEMBERS_COUNT(members) \
    (0 REFLECTION_STRUCT_END(REFLECTION_STRUCT_MEMBERS_COUNT_a members))
#define REFLECTION_STRUCT_MEMBERS_COUNT_a(...) REFLECTION_STRUCT_MEMBERS_COUNT_b
#define REFLECTION_STRUCT_MEMBERS_COUNT_b(name) \
    +1 REFLECTION_STRUCT_MEMBERS_COUNT_a
#define REFLECTION_STRUCT_MEMBERS_COUNT_a_end

// creates static constexpr const size_t index_##name
#define REFLECTION_STRUCT_MEMBERS_INDEX(members) \
    REFLECTION_STRUCT_END(REFLECTION_STRUCT_MEMBERS_INDEX_a_start members)
#define REFLECTION_STRUCT_MEMBERS_INDEX_a_start(...) \
    REFLECTION_STRUCT_MEMBERS_INDEX_b_start
#define REFLECTION_STRUCT_MEMBERS_INDEX_b_start(name)         \
    static constexpr const size_t index_##name = 0;           \
    typedef template_utils::const_value<size_t, index_##name> \
        REFLECTION_STRUCT_MEMBERS_INDEX_a
#define REFLECTION_STRUCT_MEMBERS_INDEX_a(...) REFLECTION_STRUCT_MEMBERS_INDEX_b
#define REFLECTION_STRUCT_MEMBERS_INDEX_b(name)                                \
    prev_index_##name;                                                         \
    static constexpr const size_t index_##name = prev_index_##name::value + 1; \
    typedef template_utils::const_value<size_t, index_##name>                  \
        REFLECTION_STRUCT_MEMBERS_INDEX_a
#define REFLECTION_STRUCT_MEMBERS_INDEX_a_end prev_end;

#define REFLECTION_STRUCT_MEMBER_TYPES(members) \
    REFLECTION_STRUCT_END(REFLECTION_STRUCT_MEMBER_TYPES_start members)
#define REFLECTION_STRUCT_MEMBER_TYPES_start(...) \
    __VA_ARGS__ REFLECTION_STRUCT_MEMBER_TYPES_b
#define REFLECTION_STRUCT_MEMBER_TYPES_a(...) \
    , __VA_ARGS__ REFLECTION_STRUCT_MEMBER_TYPES_b
#define REFLECTION_STRUCT_MEMBER_TYPES_b(name) REFLECTION_STRUCT_MEMBER_TYPES_a
#define REFLECTION_STRUCT_MEMBER_TYPES_a_end

#define REFLECTION_STRUCT_MEMBER_NAMES_DECLARE(members) \
    REFLECTION_STRUCT_END(REFLECTION_STRUCT_MEMBER_NAMES_DECLARE_a members)
#define REFLECTION_STRUCT_MEMBER_NAMES_DECLARE_a(...) \
    REFLECTION_STRUCT_MEMBER_NAMES_DECLARE_b
#define REFLECTION_STRUCT_MEMBER_NAMES_DECLARE_b(name)       \
    static constexpr const char *member_name_##name = #name; \
    REFLECTION_STRUCT_MEMBER_NAMES_DECLARE_a
#define REFLECTION_STRUCT_MEMBER_NAMES_DECLARE_a_end

#define REFLECTION_STRUCT_MEMBER_NAMES_VARIABLE(members)              \
    template <size_t MemberIndex>                                     \
    static constexpr const char *member_name = REFLECTION_STRUCT_END( \
        REFLECTION_STRUCT_MEMBER_NAMES_VARIABLE_a members)
#define REFLECTION_STRUCT_MEMBER_NAMES_VARIABLE_a(...) \
    REFLECTION_STRUCT_MEMBER_NAMES_VARIABLE_b
#define REFLECTION_STRUCT_MEMBER_NAMES_VARIABLE_b(name) \
    MemberIndex == index_##name ? member_name_##name    \
                                : REFLECTION_STRUCT_MEMBER_NAMES_VARIABLE_a
#define REFLECTION_STRUCT_MEMBER_NAMES_VARIABLE_a_end nullptr;

#define REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_DECLARE(members) \
    REFLECTION_STRUCT_END(                                     \
        REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_DECLARE_a members)
#define REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_DECLARE_a(...) \
    REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_DECLARE_b
#define REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_DECLARE_b(name) \
    static constexpr const size_t member_name_length_##name = \
        template_utils::const_str_length(#name);              \
    REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_DECLARE_a
#define REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_DECLARE_a_end

#define REFLECTION_STRUCT_MEMBER_NAMES_LENGTH(members) \
    template <size_t MemberIndex>                      \
    static constexpr const size_t member_name_length = \
        REFLECTION_STRUCT_END(REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_a members)
#define REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_a(...) \
    REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_b
#define REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_b(name)       \
    MemberIndex == index_##name ? member_name_length_##name \
                                : REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_a
#define REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_a_end 0;

#define REFLECTION_STRUCT_GET_MEMBER(name, members)                   \
    template <size_t MemberIndex>                                     \
    static member_types::N<MemberIndex> *getMember(name *s) {         \
        REFLECTION_STRUCT_END(REFLECTION_STRUCT_GET_MEMBER_a members) \
    }
#define REFLECTION_STRUCT_GET_MEMBER_a(...) REFLECTION_STRUCT_GET_MEMBER_b
#define REFLECTION_STRUCT_GET_MEMBER_b(name)             \
    if (MemberIndex == index_##name) {                   \
        return (member_types::N<MemberIndex> *)&s->name; \
    } else                                               \
        REFLECTION_STRUCT_GET_MEMBER_a
#define REFLECTION_STRUCT_GET_MEMBER_a_end \
    if (true) {                            \
        return nullptr;                    \
    }

#define IMPL_REFLECTION_STRUCT(name, members_)                                 \
    template <>                                                                \
    struct template_utils::struct_reflection<name> {                           \
        static constexpr const bool exists = true;                             \
                                                                               \
        static constexpr const size_t members =                                \
            REFLECTION_STRUCT_MEMBERS_COUNT(members_);                         \
                                                                               \
        typedef template_utils::pack<REFLECTION_STRUCT_MEMBER_TYPES(members_)> \
            member_types;                                                      \
                                                                               \
        REFLECTION_STRUCT_MEMBERS_INDEX(members_)                              \
                                                                               \
        REFLECTION_STRUCT_MEMBER_NAMES_DECLARE(members_)                       \
        REFLECTION_STRUCT_MEMBER_NAMES_VARIABLE(members_)                      \
                                                                               \
        REFLECTION_STRUCT_MEMBER_NAMES_LENGTH_DECLARE(members_)                \
        REFLECTION_STRUCT_MEMBER_NAMES_LENGTH(members_)                        \
                                                                               \
        REFLECTION_STRUCT_GET_MEMBER(name, members_)                           \
    };

#define REFLECTION_STRUCT_MEMBERS_DECLARATION(members) \
    REFLECTION_STRUCT_END(REFLECTION_STRUCT_MEMBERS_DECLARATION_a members)
#define REFLECTION_STRUCT_MEMBERS_DECLARATION_a(...) \
    __VA_ARGS__ REFLECTION_STRUCT_MEMBERS_DECLARATION_b
#define REFLECTION_STRUCT_MEMBERS_DECLARATION_b(name) \
    name;                                             \
    REFLECTION_STRUCT_MEMBERS_DECLARATION_a
#define REFLECTION_STRUCT_MEMBERS_DECLARATION_a_end

#define REFLECTION_STRUCT(name, members)               \
    struct name {                                      \
        REFLECTION_STRUCT_MEMBERS_DECLARATION(members) \
    };                                                 \
    IMPL_REFLECTION_STRUCT(name, members)

template <size_t Length, size_t Align = alignof(size_t)>
struct Aligned {
    alignas(Align) char data[Length];
};

template <typename... Ts>
class Tuple {
   public:
    typedef template_utils::pack<> pack;
    static constexpr const bool empty = true;
    static constexpr const size_t length = 0;

    template <size_t Index>
    using TypeAt = void_;

    explicit Tuple() {}

    template <size_t Index>
    inline TypeAt<Index> *atPtr() {
        // this is empty
        return nullptr;
    }
};

template <typename T, typename... Ts>
class Tuple<T, Ts...> {
   public:
    typedef Tuple<Ts...> Next;
    typedef template_utils::pack<T, Ts...> pack;

    static constexpr const bool empty = false;
    static constexpr size_t length = 1 + Next::length;

    explicit Tuple(T t, Ts... ts) : value(t), next(Next(ts...)) {}
    explicit Tuple(T t, Next ts) : value(t), next(ts) {}

    template <size_t Index>
    using TypeAt = typename template_utils::conditional_type<
        Index == 0, T, typename Next::template TypeAt<Index - 1>>::type;

    template <size_t Index>
    inline TypeAt<Index> *atPtr() {
        if constexpr (Index == 0) {
            return (TypeAt<Index> *)&value;
        } else {
            return (TypeAt<Index> *)next.template atPtr<Index - 1>();
        }
    }

    template <size_t Index>
    inline TypeAt<Index> at() {
        return *atPtr<Index>();
    }

    inline Tuple<Ts...> *asNext() { return &next; }

    // Caller must have: Return call(Types...);
    template <typename Caller, typename Return>
    inline Return call(Caller caller) {
        return call2<Caller, Return, Tuple<T, Ts...>>(caller, this);
    }

   private:
    template <typename Caller, typename Return, typename TupleTy,
              typename... Extracted>
    inline static Return call2(Caller caller, TupleTy *tuple,
                               Extracted... extracted) {
        if constexpr (TupleTy::pack::length == 0) {
            return caller.call(extracted...);
        } else {
            return call2<Caller, Return, typename TupleTy::Next, Extracted...,
                         typename TupleTy::pack::current>(
                caller, tuple->asNext(), extracted..., tuple->template at<0>());
        }
    }

    T value;
    Next next;
};

template <typename... Ts>
class Union {
   public:
    static constexpr const bool empty = false;

    template <typename I>
    static constexpr bool isTypeAllowed() {
        return false;
    }
};

template <typename T, typename... Ts>
class Union<T, Ts...> {
   private:
    static constexpr const size_t size =
        template_utils::max_value<size_t, sizeof(T), sizeof(Ts)...>::value;

   public:
    static constexpr const bool empty = true;

    template <typename I>
    static constexpr bool isTypeAllowed() {
        return template_utils::is_type_equal<T, I>::value ||
               Union<Ts...>::template isTypeAllowed<I>();
    }

    template <typename I>
    static constexpr void checkType() {
        static_assert(isTypeAllowed<I>(), "Invalid union type");
    }

    explicit Union() {}

    template <typename I>
    void set(I i) {
        setPtr(&i);
    }

    template <typename I>
    void setPtr(I *i) {
        checkType<I>();
        memcpy(&this->data.data, i, sizeof(I));
    }

    template <typename I>
    I *asPtr() {
        checkType<I>();
        return (I *)&data.data;
    }

    template <typename I>
    I as() {
        return *asPtr<I>();
    }

   private:
    Aligned<size> data;
};

struct UnpackIntoUnion {
    template <typename... Ts>
    using type = Union<Ts...>;
};

template <typename T>
class Optional {
   private:
    bool exists;
    union {
        void_ none;
        T value;
    };

   public:
    Optional() : exists(false), none() {}

    static Optional<T> empty() {
        Optional<T> optional;
        optional.exists = false;
        return optional;
    }

    static Optional<T> of(T value) {
        Optional<T> optional;
        optional.exists = true;
        optional.value = value;
        return optional;
    }

    bool isPresent() { return exists; }

    bool isEmpty() { return !isPresent(); }

    T get() {
        if (!exists) {
            // TODO: print error
        }
        return value;
    }

    T *getPtr() {
        if (!exists) {
            // TODO: print error
            return nullptr;
        }
        return &value;
    }
};

// String
size_t stringLength(const char *string) {
    int i = 0;
    while (string[i] != '\0') {
        i += 1;
    }
    return i;
}

bool isCharSpaceOrTab(char c) { return c == ' ' || c == '\t'; }

bool isCharNotSpaceOrTab(char c) { return !isCharSpaceOrTab(c); }

bool isCharWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isCharNotWhitespace(char c) { return !isCharWhitespace(c); }

// Buffers

// A reference to a buffer of data with length.
struct BufferRef {
    size_t length;
    char *data;

    BufferRef() : length(0), data(nullptr) {}

    BufferRef(char *data, size_t length) : length(length), data(data) {}

    explicit BufferRef(const char *string)
        : length(string == nullptr ? 0 : stringLength(string)),
          data((char *)string) {}

    bool operator==(BufferRef other) {
        if (length != other.length) {
            return false;
        }
        for (int i = 0; i < length; i += 1) {
            if (data[i] != other.data[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(BufferRef other) { return !this->operator==(other); }

    bool operator==(const char *string) {
        return this->operator==(BufferRef(string));
    }

    bool operator!=(const char *string) { return !this->operator==(string); }

    bool equalsIgnoreCase(BufferRef other) {
        if (length != other.length) {
            return false;
        }
        for (int i = 0; i < length; i += 1) {
            if (tolower(data[i]) != tolower(other.data[i])) {
                return false;
            }
        }
        return true;
    }

    bool equalsIgnoreCase(const char *string) {
        return this->equalsIgnoreCase(BufferRef(string));
    }

    BufferRef asRef(size_t length) {
        return BufferRef(data, min(length, this->length));
    }

    char *copyToCString() {
        char *string = (char *)malloc(length + 1);
        memcpy(string, data, length);
        string[length] = '\0';
        return string;
    }
};

// A stack allocated buffer with a fixed capacity
template <size_t Capacity>
struct SizedBuffer {
    size_t length = 0;
    char data[Capacity];

    SizedBuffer() = default;

    explicit SizedBuffer(BufferRef ref) : length(min(Capacity, ref.length)) {
        for (int i = 0; i < length; i += 1) {
            data[i] = ref.data[i];
        }
    }

    bool push(char c) {
        if (length >= Capacity) {
            return false;
        }
        data[length] = c;
        length++;
        return true;
    }

    void clear() { length = 0; }

    BufferRef asRef() { return BufferRef(data, length); }

    BufferRef asFullRef() { return BufferRef(data, Capacity); }

    BufferRef asRef(size_t length) {
        return BufferRef(data, min(length, Capacity));
    }
};

// A heap allocated buffer
class HeapBuffer {
   public:
    HeapBuffer() : length(0), capacity(0), data(nullptr) {}

    explicit HeapBuffer(size_t capacity)
        : length(0), capacity(capacity), data((char *)malloc(capacity)) {}

    template <size_t Length>
    explicit HeapBuffer(SizedBuffer<Length> buffer)
        : length(buffer.length),
          capacity(Length),
          data((char *)malloc(Length)) {
        memcpy(data, buffer.data, length);
    }

    HeapBuffer(const HeapBuffer &other)
        : length(other.length),
          capacity(other.capacity),
          data((char *)malloc(other.capacity)) {  // copy constructor
        memcpy(data, other.data, length);
    }

    HeapBuffer(HeapBuffer &&other) {  // move constructor
        swap(other);
    }

    HeapBuffer &operator=(const HeapBuffer &other) {  // copy assignment
        if (this == &other) return *this;

        HeapBuffer(other).swap(*this);
        return *this;
    }

    HeapBuffer &operator=(HeapBuffer &&other) {  // move assignment
        if (this == &other) return *this;

        swap(other);
        return *this;
    }

    ~HeapBuffer() {
        if (data != nullptr) {
            free(data);
        }
    }

    void resize(size_t newSize) {
        if (newSize <= capacity) {
            return;
        }

        char *newData = (char *)malloc(newSize);
        if (data != nullptr) {
            memcpy(newData, data, length);
        }

        free(data);
        data = newData;
        length = newSize;
        capacity = newSize;
    }

    void clear() { length = 0; }

    // Will always return true
    bool push(char c) {
        if (length + 1 >= capacity) {
            resize(max(1, (int)ceil(capacity * 1.5)));
        }
        data[length] = c;
        length++;
        return true;
    }

    BufferRef asRef() { return BufferRef(data, length); }

    BufferRef asRef(size_t length) {
        return BufferRef(data, min(length, this->length));
    }

    BufferRef asFullRef() { return BufferRef(data, capacity); }

   private:
    size_t length;
    size_t capacity;
    char *data;

    void swap(HeapBuffer &other) {
        this->length = other.length;
        this->capacity = other.capacity;
        this->data = other.data;

        other.length = 0;
        other.capacity = 0;
        other.data = nullptr;
    }
};

template <size_t Bits>
class BitSet {
   public:
    BitSet() {}

    inline bool get(size_t bit) {
        BitLocation location = getLocation(bit);
        return get(location);
    }
    template <size_t Bit>
    inline bool get() {
        constexpr BitLocation location = getLocation(Bit);
        return get(location);
    }
    inline void set(size_t bit, bool value) {
        BitLocation location = getLocation(bit);
        set(location, value);
    }
    template <size_t Bit>
    inline void set(bool value) {
        constexpr BitLocation location = getLocation(Bit);
        set(location, value);
    }

   private:
    struct BitLocation {
        size_t index;
        unsigned char shift;
        unsigned char mask;
    };
    static constexpr BitLocation getLocation(size_t bit) {
        unsigned char shift = bit % 8;
        return {.index = (size_t)floor(bit / 8.0),
                .shift = shift,
                .mask = (unsigned char)(1 << shift)};
    }
    static constexpr const char FIRST_MASK = 0x1;

    static constexpr const size_t length = (size_t)ceil(Bits / 8.0);
    unsigned char array[length] = {0};

    inline bool get(BitLocation location) {
        unsigned char byte = array[location.index];
        return (byte & location.mask) != 0;
    }
    inline void set(BitLocation location, bool value) {
        unsigned char byte = array[location.index];
        unsigned char invertedMask = ~location.mask;
        // This byte doesn't contain the bit we're trying to set.
        unsigned char newByte = byte & invertedMask;
        // Now we can just or the bitshifted value to the new byte.
        if (value) {
            newByte |= location.mask;
        }
        array[location.index] = newByte;
    };
};

namespace variant {
template <typename... Ts>
struct size_of_max;

template <typename T>
struct size_of_max<T> {
    static const size_t value = sizeof(T);
};

template <typename T, typename... Ts>
struct size_of_max<T, Ts...> {
    static const size_t value = sizeof(T) > size_of_max<Ts...>::value
                                    ? sizeof(T)
                                    : size_of_max<Ts...>::value;
};

typedef size_t TypeTy;

template <typename C, typename... Ts>
struct type_id;

template <typename C, typename T>
struct type_id<C, T> {
    static const TypeTy value =
        template_utils::is_type_equal<C, T>::value ? 1 : 0;
    static const bool valid = value != 0;
};

template <typename C, typename T, typename... Ts>
struct type_id<C, T, Ts...> {
    static const TypeTy value = template_utils::is_type_equal<C, T>::value
                                    ? sizeof...(Ts) + 1
                                    : type_id<C, Ts...>::value;
    static const bool valid = value != 0;
};

// Required for placement new
#include <new>

template <typename... Ts>
class Variant {
   private:
    static_assert(sizeof...(Ts) > 0, "Variant must have at least one type!");

    TypeTy type = 0;
    // TODO: make this aligned storage
    char data[size_of_max<Ts...>::value];

   public:
    template <typename T>
    static bool isValidType() {
        return type_id<T, Ts...>::valid;
    }

    template <typename T>
    static TypeTy typeIdOf() {
        static_assert(type_id<T, Ts...>::valid, "Type not in Variant!");
        return type_id<T, Ts...>::value;
    }

    template <typename T>
    explicit Variant(T value) : type(0), data() {
        type = typeIdOf<T>();
        new (&data) T(value);
    }

    template <typename T>
    static Variant of(T value) {
        return Variant(value);
    }

    size_t getType() const { return type; }

    template <typename T>
    bool is() const {
        return typeIdOf<T>() == getType();
    }

    template <typename T>
    Optional<T *> as() const {
        if (!is<T>()) {
            return Optional<T *>::empty();
        }
        T *ptr = (T *)data;
        return Optional<T *>::of(ptr);
    }

    // TODO: Those constructors/destructors are wrongly implemented and
    // could contain double frees copy constructor
   private:
    template <typename T>
    inline void copyOne(const Variant &other) {
        if (other.is<T>()) {
            this->type = other.type;
            T *ptr = (T *)this->data;
            T &otherPtr = (T &)other.data;
            new (ptr) T(otherPtr);
        } else {
            type = 0;
        }
    }
    template <typename T, typename... Others>
    inline void copy(const Variant &other) {
        if (other.is<T>()) {
            this->type = other.type;
            T *ptr = (T *)this->data;
            T &otherPtr = (T &)other.data;
            new (ptr) T(otherPtr);
        } else {
            if constexpr (sizeof...(Others) > 0) {
                copy<Others...>(other);
            } else {
                copyOne<T>(other);
            }
        }
    }

   public:
    Variant(const Variant &other) { copy<Ts...>(other); }
    Variant &operator=(const Variant &other) {
        if (this == &other) return *this;

        copy<Ts...>(other);

        return *this;
    }

    // move constructor
   private:
    template <typename T>
    inline void moveOne(Variant &&other) {
        if (other.is<T>()) {
            this->type = other.type;
            T *ptr = (T *)data;
            T &otherPtr = (T &)other.data;
            new (ptr) T(template_utils::move(otherPtr));
            other.type = 0;
        } else {
            type = 0;
        }
    }
    template <typename T, typename... Others>
    inline void move(Variant &&other) {
        if (other.is<T>()) {
            this->type = other.type;
            T *ptr = (T *)data;
            T &otherPtr = (T &)other.data;
            new (ptr) T(template_utils::move(otherPtr));
            other.type = 0;
        } else {
            if constexpr (sizeof...(Others) > 0) {
                move<Others...>(template_utils::move(other));
            } else {
                moveOne<T>(template_utils::move(other));
            }
        }
    }

   public:
    Variant(Variant &&other) {
        move<Ts...>(template_utils::move(other));
        other.type = 0;
    }
    Variant &operator=(Variant &&other) {
        if (this == &other) return *this;

        move<Ts...>(template_utils::move(other));

        return *this;
    }

    // destructor
   private:
    template <typename T>
    inline void destroyOne() {
        if (is<T>()) {
            this->type = 0;
            T *ptr = (T *)data;
            ptr->~T();
        }
    }
    template <typename T, typename... Others>
    inline void destroy() {
        if (is<T>()) {
            this->type = 0;
            T *ptr = (T *)data;
            ptr->~T();
        } else {
            // TODO
            if constexpr (sizeof...(Others) > 0) {
                destroy<Others...>();
            } else {
                this->type = 0;
            }
        }
    }

   public:
    ~Variant() { destroy<Ts...>(); }
};

}  // namespace variant

#endif
