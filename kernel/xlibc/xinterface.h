#pragma once

#define INTERFACE_MEMBER_DECL(type, name) \
    type name;

#define INTERFACE_METHOD_DECL(ret, name, args) \
    ret (*name) args;

#define INTERFACE(name, members, methods) \
    typedef struct name { \
        members(INTERFACE_MEMBER_DECL) \
        methods(INTERFACE_METHOD_DECL) \
    } name;

#define INTERFACE_METHOD_IMPL(type, ret, name, args) \
    ret type##_##name args;

#define INTERFACE_MEMBER_IMPL(type, name) \
    type name;

#define INTERFACE_IMPLEMENT(type, methods, members) \
    methods(INTERFACE_METHOD_IMPL) \
    members(INTERFACE_MEMBER_IMPL)