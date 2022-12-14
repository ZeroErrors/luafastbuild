// This file defines a template that is used to bypass member access restrictions
#pragma once

#define ACCESS_BYPASS_DEFINE_MEMBER(type, member_type, member)                             \
    namespace __ ## type { struct member; };                                        \
    member_type type::* __get_member( __ ## type::member );                         \
    template class MemberGetter<                                                    \
        member_type type::*,                                                        \
        & type::member,                                                             \
        __ ## type::member                                                          \
    >;                                                                              \
    namespace __ ## type                                                            \
    {                                                                               \
        struct member                                                               \
        {                                                                           \
            static member_type * ptr( type * ptr )                                  \
            {                                                                       \
                return &(ptr->* __get_member( member() ));                          \
            }                                                                       \
        };                                                                          \
    }                                                                               \

// https://stackoverflow.com/questions/15110526/allowing-access-to-private-members
template< typename type, type value, typename tag >
class MemberGetter {
    friend type __get_member(tag) { return value; }
};

#define ACCESS_BYPASS_DEFINE_REINTERPRET_CAST_MEMBER(type, member_type, member, cast_type) \
    namespace __ ## type { struct member; };                                        \
    cast_type type::* __get_member( __ ## type::member );                           \
    template class MemberReinterpretCastGetter<                                     \
        member_type type::*,                                                        \
        & type::member,                                                             \
        cast_type type::*,                                                          \
        __ ## type::member                                                          \
    >;                                                                              \
    namespace __ ## type                                                            \
    {                                                                               \
        struct member                                                               \
        {                                                                           \
            static cast_type * ptr( type * ptr )                                    \
            {                                                                       \
                return &(ptr->* __get_member( member() ));                          \
            }                                                                       \
        };                                                                          \
    }                                                                               \

// https://stackoverflow.com/questions/15110526/allowing-access-to-private-members
template< typename member_type, member_type value, typename cast_type, typename tag >
class MemberReinterpretCastGetter {
    friend cast_type __get_member(tag) { return reinterpret_cast<cast_type>( value ); }
};

#define ACCESS_BYPASS_DEFINE_FUNC(type, func_return, func_args, func)               \
    namespace __ ## type {                                                          \
        struct func;                                                                \
        typedef func_return(type::* __ ## func)func_args;                           \
    };                                                                              \
    __ ## type::__ ## func __get_func( __ ## type::func );                          \
    template class FuncGetter<                                                      \
        __ ## type::__ ## func,                                                     \
        & type::func,                                                               \
        __ ## type::func                                                            \
    >;                                                                              \
    namespace __ ## type                                                            \
    {                                                                               \
        struct func                                                                 \
        {                                                                           \
            template < class ... ARGS >                                             \
            static func_return call( type * ptr, ARGS && ... args )                 \
            {                                                                       \
                return (ptr->* __get_func( func() ))( args... );                    \
            }                                                                       \
        };                                                                          \
    }                                                                               \

// https://stackoverflow.com/questions/15110526/allowing-access-to-private-members
template< typename type, type value, typename tag >
class FuncGetter {
    friend type __get_func(tag) { return value; }
};