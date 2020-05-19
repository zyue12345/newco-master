#include "fiber.h"
#include "log.h"
#include "scheduler.h"
#include<assert.h>
#include <atomic>

namespace newco {

static std::atomic<uint64_t> s_fiber_id {0};
static std::atomic<uint64_t> s_fiber_count {0};

static thread_local Fiber* t_fiber = nullptr;
static thread_local Fiber::ptr t_threadFiber = nullptr;

class MallocStackAllocator {
public:
    static void* Alloc(size_t size) {
        return malloc(size);
    }

    static void Dealloc(void* vp, size_t size) {
        return free(vp);
    }
};

using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId() {
    if(t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

Fiber::Fiber() {
    m_state = EXEC;
    SetThis(this);

  
    getcontext(&m_ctx);

    ++s_fiber_count;
     
    yolanda_msgx("Fiber::Fiber main");
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller)
    :m_id(++s_fiber_id)
    ,m_cb(cb) {
    ++s_fiber_count;
    m_stacksize = stacksize;

    m_stack = StackAllocator::Alloc(m_stacksize);

    getcontext(&m_ctx);
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    if(!use_caller) {
        makecontext(&m_ctx, &Fiber::MainFunc, 0);
    } else {
        makecontext(&m_ctx, &Fiber::CallerMainFunc, 0);
    }

    yolanda_msgx( "Fiber::Fiber id=%d",m_id);
}

Fiber::~Fiber() {
    --s_fiber_count;
    if(m_stack) {
        assert(m_state == TERM
                || m_state == INIT);

        StackAllocator::Dealloc(m_stack, m_stacksize);
    } else {
        assert(!m_cb);
        assert(m_state == EXEC);

        Fiber* cur = t_fiber;
        if(cur == this) {
            SetThis(nullptr);
        }
    }
     yolanda_msgx( "Fiber::~Fiber id=%d",m_id);
}

//重置协程函数，并重置状态
//INIT，TERM, EXCEPT
void Fiber::reset(std::function<void()> cb) {
    assert(m_stack);
    assert(m_state == TERM
            || m_state == INIT);
    m_cb = cb;

    assert(!getcontext(&m_ctx));
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = INIT;
}

void Fiber::call() {
    SetThis(this);
    m_state = EXEC;
 
   swapcontext(&t_threadFiber->m_ctx, &m_ctx);
}

void Fiber::back() {
    SetThis(t_threadFiber.get());
 
    swapcontext(&m_ctx, &t_threadFiber->m_ctx);
}

//切换到当前协程执行
void Fiber::swapIn() {
    SetThis(this);
    assert(m_state != EXEC);
    m_state = EXEC;
 
    swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx);

}
//切换到后台执行
void Fiber::swapOut() {
    SetThis(Scheduler::GetMainFiber());
  
    swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx);
}

//设置当前协程
void Fiber::SetThis(Fiber* f) {
    t_fiber = f;
}

//返回当前协程
Fiber::ptr Fiber::GetThis() {
    if(t_fiber) {
        return t_fiber->shared_from_this();
    }
    Fiber::ptr main_fiber(new Fiber);
    t_threadFiber = main_fiber;
    return t_fiber->shared_from_this();
}

//协程切换到后台，并且设置为Ready状态
void Fiber::YieldToReady() {
    Fiber::ptr cur = GetThis();
    cur->m_state = READY;
    cur->swapOut();
}

//协程切换到后台，并且设置为Hold状态
void Fiber::YieldToHold() {
    Fiber::ptr cur = GetThis();
    cur->m_state = HOLD;
    cur->swapOut();
}

//总协程数
uint64_t Fiber::TotalFibers() {
    return s_fiber_count;
}

void Fiber::MainFunc() {
    Fiber::ptr cur = GetThis();
     assert(cur);
     cur->m_cb();
     cur->m_cb = nullptr;
     cur->m_state = TERM;

    
    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->swapOut();
    
}

void Fiber::CallerMainFunc() {
    Fiber::ptr cur = GetThis();
    assert(cur);
    cur->m_cb();
    cur->m_cb = nullptr;
    cur->m_state = TERM;
  
    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->back();

}

}
