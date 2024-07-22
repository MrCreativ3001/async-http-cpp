#ifndef CPP_ASYNC_HTTP_SER_H
#define CPP_ASYNC_HTTP_SER_H

#include "future.h"
#include "utils.h"

// TODO: Put into namespace

class Serializer {
    class SerializeStruct {
        // template<typename T>
        typedef Future<void_, bool> SerializeFieldFuture;

        template <typename T>
        SerializeFieldFuture serializeField(BufferRef name, T* value) = delete;

        typedef Future<void_, bool> EndFuture;
        EndFuture end() = delete;
    };

    typedef Future<void_, Optional<SerializeStruct>> SerializeStructFuture;

    SerializeStructFuture serializeStruct() = delete;
};

template <typename T, typename Serializer>
struct Serialize {
    // typedef Future<void_, bool> SerializeFuture;

    // static SerializeFuture serialize(Serializer* serializer, T* value);

    static_assert(template_utils::struct_reflection<T>::exists,
                  "Struct T doesn't implement struct_reflection which is "
                  "required for the default implementation of Serialize!");

    typedef template_utils::struct_reflection<T> Reflection;

    class SerializeFuture : Future<SerializeFuture, bool> {
       private:
        template <size_t MemberIndex>
        class SerializeMembersFuture
            : Future<SerializeMembersFuture<MemberIndex>, bool> {
           private:
            static constexpr const bool isLastMember =
                template_utils::struct_reflection<T>::members - 1 ==
                MemberIndex;

            // TODO: Do something about this pollNext. This should use if
            // constexpr instead!
            Poll<bool> pollNext(void_* v) { return Poll<bool>::ready(true); }
            template <size_t MemberIndex2>
            Poll<bool> pollNext(SerializeMembersFuture<MemberIndex2>* future) {
                return future->poll();
            }

           public:
            SerializeMembersFuture(
                typename Serializer::SerializeStruct* serializeStruct, T* value)
                : serializeStruct(serializeStruct), value(value) {}

            Poll<bool> poll() {
                switch (state) {
                    case State::INIT: {
                        auto future = serializeStruct->template serializeField<
                            typename Reflection::member_types::template N<
                                MemberIndex>>(
                            BufferRef(
                                Reflection::template member_name<MemberIndex>),
                            Reflection::template getMember<MemberIndex>(value));

                        INIT_AWAIT(SERIALIZE_MEMBER, serializeFuture, future,
                                   result)
                        if (!result) {
                            READY(false)
                        }

                        if (isLastMember) {
                            READY(true)
                        }

                        NextFuture future = NextFuture(serializeStruct, value);
                        INIT_STATE(NEXT, nextFuture, future)

                        Poll<bool> poll = pollNext(&nextFuture);
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
            typedef typename template_utils::struct_reflection<
                T>::member_types::template N<MemberIndex>
                Type;
            typedef typename template_utils::conditional_type<
                isLastMember, void_,
                SerializeMembersFuture<MemberIndex + 1>>::type NextFuture;

            enum class State {
                INIT,
                SERIALIZE_MEMBER,
                NEXT
            } state = State::INIT;
            // TODO: try to  remove serializer and t from here and only have it
            // in the main future(this wastes space)
            typename Serializer::SerializeStruct* serializeStruct;
            T* value;
            union {
                typename Serializer::SerializeStruct::
                    template SerializeFieldFuture<
                        typename Reflection::member_types::template N<
                            MemberIndex>>
                        serializeFuture;
                // void or next member
                NextFuture nextFuture;
            };
        };

       public:
        SerializeFuture(Serializer* serializer, T* value)
            : serializer(serializer), value(value) {}

        Poll<bool> poll() {
            switch (state) {
                case State::INIT: {
                    INIT_AWAIT(SERIALIZE_STRUCT, serializeStructFuture,
                               serializer->serializeStruct(), result)
                    if (result.isEmpty()) {
                        READY(false)
                    }
                    serializeStruct = result.get();

                    INIT_AWAIT(
                        SERIALIZE_MEMBERS, serializeMembers,
                        SerializeMembersFuture<0>(&serializeStruct, value),
                        result)
                    if (!result) {
                        READY(false)
                    }

                    INIT_AWAIT(SERIALIZE_END, serializeStructEndFuture,
                               serializeStruct.end(), result)
                    if (!result) {
                        READY(false)
                    }

                    READY(true)
                }
            }
            return Poll<bool>::pending();
        }

       private:
        enum class State {
            INIT,
            SERIALIZE_STRUCT,
            SERIALIZE_MEMBERS,
            SERIALIZE_END,
        } state = State::INIT;
        Serializer* serializer;
        union {
            typename Serializer::SerializeStruct serializeStruct;
        };
        T* value;
        union {
            typename Serializer::SerializeStructFuture serializeStructFuture;
            typename Serializer::SerializeStruct::EndFuture
                serializeStructEndFuture;

            SerializeMembersFuture<0> serializeMembers;
        };
    };

    static SerializeFuture serialize(Serializer* serializer, T* value) {
        return SerializeFuture(serializer, value);
    }
};

// Default value implementations

template <size_t Capacity, typename Serializer>
struct Serialize<SizedBuffer<Capacity>, Serializer> {
    typedef typename Serialize<BufferRef, Serializer>::SerializeFuture
        SerializeFuture;

    static SerializeFuture serialize(Serializer* serializer,
                                     SizedBuffer<Capacity>* value) {
        // The value WILL exist as long as the future because we are given a
        // pointer
        BufferRef ref = value->asRef();
        // TODO: try to let ref be alive as long as the future returned from
        // serialize
        return Serialize<BufferRef, Serializer>::serialize(serializer, &ref);
    }
};

#endif