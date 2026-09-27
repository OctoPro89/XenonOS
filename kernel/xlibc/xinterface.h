#pragma once

#define INTERFACE_MEMBER_DECL(type, name) \
    type name;

#define INTERFACE_METHOD_DECL(ret, name, args) \
    ret (*name) args;

/**
 * Creates an interface
 */
#define INTERFACE(name, members, methods) \
    typedef struct name { \
        members(INTERFACE_MEMBER_DECL) \
        methods(INTERFACE_METHOD_DECL) \
    } name;

#define INTERFACE_DERIVED(name, base, members, methods) \
    typedef struct name { \
        base base; \
        members(INTERFACE_MEMBER_DECL) \
        methods(INTERFACE_METHOD_DECL) \
    } name;

#define INTERFACE_MEMBER_IMPL(type, name) \
    type name;

#define INTERFACE_METHOD_IMPL(ret, name, args) \
    ret (*name) args;

/**
 * Implements an interface's members and methods
 */
#define INTERFACE_IMPLEMENT(members, methods) \
    members(INTERFACE_MEMBER_IMPL) \
    methods(INTERFACE_METHOD_IMPL)