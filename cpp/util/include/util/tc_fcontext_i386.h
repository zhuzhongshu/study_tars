#ifndef _TC_CONTEXT_FCONTEXT_I386H
#define _TC_CONTEXT_FCONTEXT_I386H

#include <stdint.h>


/*
这一部分跟boost.context模块的头文件类似，又有点不同，参见
https://www.boost.org/doc/libs/1_53_0/libs/context/doc/html/context/context/boost_fcontext.html
不知道是不是版本不一样
*/
namespace tars
{

extern "C" 
{

struct stack_t
{
    void            *   sp;
    std::size_t         size;

    stack_t()
    : sp( 0)
    , size( 0)
    {}
};

struct fcontext_t
{
    uint32_t     fc_greg[6];
    stack_t      fc_stack;
    uint32_t     fc_freg[2];

    fcontext_t()
    : fc_greg()
    , fc_stack()
    , fc_freg()
    {}
};

}

}

#endif
