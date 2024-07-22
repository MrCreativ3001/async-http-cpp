#ifndef CPP_ASYNC_HTTP_DESER_H
#define CPP_ASYNC_HTTP_DESER_H

#include "future.h"
#include "utils.h"

namespace deser {

class Deserializer {
    class NameStoreExample {
        bool push(char c);
        void clear();
    };

    class StructVisitorExample {
        typedef Future<void_, bool> VisitFuture;

        // if the value is not deserialized/consumed in using the deserializer
        // false MUST be returned. NameStoreExample refers to the namestore
        // passed into deserializeStruct.
        VisitFuture visit(NameStoreExample* name,
                          Deserializer* deserializer) = delete;
    };

    // template<typename NameStore, typename StructVisitor>
    class DeserializeStructFuture : Future<void_, bool> {};

    template <typename NameStore, typename StructVisitor>
    DeserializeStructFuture deserializeStruct(NameStore nameStore,
                                              StructVisitor visitor) = delete;
};

template <size_t Capacity>
struct SizedNameStore {
    SizedBuffer<Capacity> buffer = SizedBuffer<Capacity>();

    SizedNameStore() {}

    bool push(char c) { return buffer.push(c); }
    void clear() { buffer.clear(); }
};

template <typename Pack, typename Deserializer>
struct MapTIntoDeserializeFutures {};

// MemberIndex starts at 0
template <typename Struct, size_t MemberIndex>
struct biggest_struct_name {
    static constexpr const size_t str_len = template_utils::struct_reflection<
        Struct>::template member_name_length<MemberIndex>;

    static constexpr const int hasNext =
        MemberIndex < template_utils::struct_reflection<Struct>::members - 1;

    typedef typename template_utils::conditional_type<
        hasNext, biggest_struct_name<Struct, MemberIndex + 1>,
        template_utils::max_value<size_t, 0>>::type Next;

    static constexpr const size_t value =
        template_utils::max_value<size_t, str_len, Next::value>::value;
};

template <typename T, typename Deserializer>
struct Deserialize {
    // typedef Future<void_, Optional<T>> DeserializeFuture;

    // static DeserializeFuture deserialize(Deserializer* deserializer);

    static_assert(template_utils::struct_reflection<T>::exists,
                  "Struct T doesn't implement struct_reflection which is "
                  "required for the default implementation of Deserialize!");

    typedef template_utils::struct_reflection<T> Reflection;

    static const size_t MAX_NAME_LENGTH = biggest_struct_name<T, 0>::value;

    // TODO: Better name
    struct DeserializingT {
        DeserializingT() : none(void_()) {}

        BitSet<Reflection::members> deserializedMembers;
        union {
            void_ none;
            T value;
        };
    };

    class StructVisitor {
       public:
        StructVisitor(DeserializingT* value) : value(value) {}

        class VisitFuture : Future<VisitFuture, bool> {
           public:
            VisitFuture(Deserializer* deserializer, BufferRef name,
                        DeserializingT* value)
                : init({deserializer, name}), value(value) {}

            Poll<bool> poll() {
                switch (state) {
                    case State::INIT: {
                        state = State::DESERIALIZE;

                        Deserializer* deserializer = init.deserializer;
                        bool valid = initMemberState(deserializer);
                        if (!valid) {
                            READY(false)
                        }
                    }
                    case State::DESERIALIZE: {
                        Poll<bool> poll = pollMemberDeserialize();
                        if (poll.isPending()) {
                            return Poll<bool>::pending();
                        }
                        bool result = poll.get();
                        if (!result) {
                            READY(false)
                        }

                        READY(true)
                    }
                }
                return Poll<bool>::pending();
            }

           private:
            template <size_t MemberIndex>
            inline bool initMemberState2(Deserializer* deserializer) {
                typedef
                    typename Reflection::member_types::template N<MemberIndex>
                        MemberType;

                BufferRef name = this->init.memberName;
                const char* memberName =
                    Reflection::template member_name<MemberIndex>;
                if (name == BufferRef(memberName)) {
                    this->deserializeMember.currentMember = MemberIndex;
                    auto future =
                        Deserialize<MemberType, Deserializer>::deserialize(
                            deserializer);
                    this->deserializeMember.future.setPtr(&future);

                    return true;
                }

                if constexpr (MemberIndex > 0) {
                    return initMemberState2<MemberIndex - 1>(deserializer);
                }
                return false;
            }
            inline bool initMemberState(Deserializer* deserializer) {
                return initMemberState2<Reflection::members - 1>(deserializer);
            }

            template <size_t MemberIndex>
            inline Poll<bool> pollMemberDeserialize2() {
                typedef
                    typename Reflection::member_types::template N<MemberIndex>
                        MemberType;
                typedef typename Deserialize<MemberType,
                                             Deserializer>::DeserializeFuture
                    MemberDeserializeFuture;

                if (this->deserializeMember.currentMember == MemberIndex) {
                    MemberDeserializeFuture* future =
                        this->deserializeMember.future
                            .template asPtr<MemberDeserializeFuture>();
                    if (value->deserializedMembers
                            .template get<MemberIndex>()) {
                        READY(false)
                    }

                    AWAIT_PTR(future, result)
                    if (result.isEmpty()) {
                        READY(false)
                    }

                    // mark member as initialized
                    value->deserializedMembers.template set<MemberIndex>(true);
                    MemberType* member =
                        Reflection::template getMember<MemberIndex>(
                            &value->value);
                    *member = result.get();

                    READY(true)
                }
                if constexpr (MemberIndex > 0) {
                    return pollMemberDeserialize2<MemberIndex - 1>();
                }
                return Poll<bool>::ready(false);
            }
            Poll<bool> pollMemberDeserialize() {
                return pollMemberDeserialize2<Reflection::members - 1>();
            }

            enum class State { INIT, DESERIALIZE } state = State::INIT;

            DeserializingT* value;
            union {
                struct {
                    Deserializer* deserializer;
                    BufferRef memberName;
                } init;
                struct {
                    char currentMember;
                    typename template_utils::unpack<
                        typename MapTIntoDeserializeFutures<
                            typename Reflection::member_types,
                            Deserializer>::type,
                        UnpackIntoUnion>::type future;
                } deserializeMember;
            };
        };

        VisitFuture visit(SizedNameStore<MAX_NAME_LENGTH>* name,
                          Deserializer* deserializer) {
            return VisitFuture(deserializer, name->buffer.asRef(), value);
        };

       private:
        DeserializingT* value;
    };

    class DeserializeFuture : Future<DeserializeFuture, Optional<T>> {
       public:
        DeserializeFuture(Deserializer* deserializer)
            : deserializer(deserializer) {}

        Poll<Optional<T>> poll() {
            switch (state) {
                case State::INIT: {
                    auto future = deserializer->deserializeStruct(
                        SizedNameStore<MAX_NAME_LENGTH>(),
                        StructVisitor(&value));
                    INIT_AWAIT(DESERIALIZE_STRUCT, deserializeStructFuture,
                               future, result)
                    if (!result) {
                        READY(Optional<T>::empty())
                    }

                    // Check all members are present
                    for (int i = 0; i < Reflection::members; i++) {
                        if (!value.deserializedMembers.get(i)) {
                            READY(Optional<T>::empty())
                        }
                    }

                    READY(Optional<T>::of(value.value))
                }
            }
            return Poll<Optional<T>>::pending();
        }

       private:
        enum class State {
            INIT,
            DESERIALIZE_STRUCT,
            DESERIALIZE_MEMBERS,
        } state = State::INIT;
        Deserializer* deserializer;

        DeserializingT value;
        union {
            typename Deserializer::template DeserializeStructFuture<
                SizedNameStore<MAX_NAME_LENGTH>, StructVisitor>
                deserializeStructFuture;
        };
    };

    static DeserializeFuture deserialize(Deserializer* deserializer) {
        return DeserializeFuture(deserializer);
    }
};

template <typename... T, typename Deserializer>
struct MapTIntoDeserializeFutures<template_utils::pack<T...>, Deserializer> {
    typedef template_utils::pack<
        typename Deserialize<T, Deserializer>::DeserializeFuture...>
        type;
};

}  // namespace deser

#endif