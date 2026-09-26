#pragma once

#define INTERFACE_MEMBER_DECL(type, name) \
    type name;

#define INTERFACE_METHOD_DECL(ret, name, args) \
    ret (*name) args;

/**
 * Creates an interface and puts void* in "self" as a placeholder
 */
#define INTERFACE(name, members, methods) \
    typedef struct name { \
        members(INTERFACE_MEMBER_DECL) \
        methods(INTERFACE_METHOD_DECL, void*) \
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
 * Implements an interface's members and methods, replacing void* with the implementer's type
 */
#define INTERFACE_IMPLEMENT(newtype, members, methods) \
    methods(INTERFACE_METHOD_IMPL, newtype*) \
    members(INTERFACE_MEMBER_IMPL)