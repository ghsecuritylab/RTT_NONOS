#include <rthw.h>
#include <rtthread.h>

#ifdef RT_USING_MODULE
#include <dlmodule.h>
#endif

#if defined(RT_USING_HOOK)
#ifndef RT_USING_IDLE_HOOK
#define RT_USING_IDLE_HOOK
#endif
#endif

#ifdef RT_USING_SMP
#define _CPUS_NR RT_CPUS_NR
#else
#define _CPUS_NR 1
#endif

extern rt_list_t rt_thread_defunct;

static struct rt_thread idle[_CPUS_NR];

#ifdef RT_USING_IDLE_HOOK
#ifndef RT_IDEL_HOOK_LIST_SIZE
#define RT_IDEL_HOOK_LIST_SIZE 4
#endif

static void (*idle_hook_list[RT_IDEL_HOOK_LIST_SIZE])();

/**
 * @ingroup Hook
 * This function sets a hook function to idle thread loop. When the system performs
 * idle loop, this hook function should be invoked.
 *
 * @param hook the specified hook function
 *
 * @return RT_EOK: set OK
 *         -RT_EFULL: hook list is full
 *
 * @note the hook function must be simple and never be blocked or suspend.
 */
rt_err_t rt_thread_idle_sethook(void (*hook)(void))
{
    rt_size_t i;
    rt_base_t level;
    rt_err_t ret = -RT_EFULL;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    for (i = 0; i < RT_IDEL_HOOK_LIST_SIZE; i++)
    {
        if (idle_hook_list[i] == RT_NULL)
        {
            idle_hook_list[i] = hook;
            ret = RT_EOK;
            break;
        }
    }
    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    return ret;
}

/**
 * delete the idle hook on hook list
 *
 * @param hook the specified hook function
 *
 * @return RT_EOK: delete OK
 *         -RT_ENOSYS: hook was not found
 */
rt_err_t rt_thread_idle_delhook(void (*hook)(void))
{
    rt_size_t i;
    rt_base_t level;
    rt_err_t ret = -RT_ENOSYS;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    for (i = 0; i < RT_IDEL_HOOK_LIST_SIZE; i++)
    {
        if (idle_hook_list[i] == hook)
        {
            idle_hook_list[i] = RT_NULL;
            ret = RT_EOK;
            break;
        }
    }
    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    return ret;
}

#endif

/* Return whether there is defunctional thread to be deleted. */
rt_inline int _has_defunct_thread(void)
{
    /* The rt_list_isempty has prototype of "int rt_list_isempty(const rt_list_t *l)".
     * So the compiler has a good reason that the rt_thread_defunct list does
     * not change within rt_thread_idle_excute thus optimize the "while" loop
     * into a "if".
     *
     * So add the volatile qualifier here. */
    const volatile rt_list_t *l = (const volatile rt_list_t *)&rt_thread_defunct;

    return l->next != l;
}

/**
 * @ingroup Thread
 *
 * This function will perform system background job when system idle.
 */
void rt_thread_idle_excute(void)
{
    /* Loop until there is no dead thread. So one call to rt_thread_idle_excute
     * will do all the cleanups. */
    while (_has_defunct_thread())
    {
        rt_base_t lock;
        rt_thread_t thread;

        RT_DEBUG_NOT_IN_INTERRUPT;

        /* disable interrupt */
        lock = rt_hw_interrupt_disable();

        /* re-check whether list is empty */
        if (_has_defunct_thread())
        {
            /* get defunct thread */
            thread = rt_list_entry(rt_thread_defunct.next,
                                   struct rt_thread,
                                   tlist);

            /* remove defunct thread */
            rt_list_remove(&(thread->tlist));
                                   
            /* invoke thread cleanup */
            if (thread->cleanup != RT_NULL)
                thread->cleanup(thread);
            
            /* if it's a system object, not delete it */
            if (rt_object_is_systemobject((rt_object_t)thread) == RT_TRUE)
            {
                /* detach this object */
                rt_object_detach((rt_object_t)thread);

                /* enable interrupt */
                rt_hw_interrupt_enable(lock);

                return;
            }
        }
        else
        {
            /* enable interrupt */
            rt_hw_interrupt_enable(lock);

            /* may the defunct thread list is removed by others, just return */
            return;
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(lock);

#ifdef RT_USING_HEAP
        /* delete thread object */
        rt_object_delete((rt_object_t)thread);
#endif
    }
}

static void rt_thread_idle_entry(void *parameter)
{

#ifdef RT_USING_IDLE_HOOK
    rt_size_t i;

    for (i = 0; i < RT_IDEL_HOOK_LIST_SIZE; i++)
    {
        if (idle_hook_list[i] != RT_NULL)
        {
            idle_hook_list[i]();
        }
    }
#endif
    rt_thread_idle_excute();
}

/**
 * @ingroup SystemInit
 *
 * This function will initialize idle thread, then start it.
 *
 * @note this function must be invoked when system init.
 */
void rt_thread_idle_init(void)
{
    rt_ubase_t i;
    char tidle_name[RT_NAME_MAX];

    for (i = 0; i < _CPUS_NR; i++)
    {
        rt_sprintf(tidle_name, "tidle%d", i);
        rt_thread_init(&idle[i],
                       tidle_name,
                       rt_thread_idle_entry,
                       RT_NULL,
                       0);
        /* startup */
        rt_thread_startup(&idle[i]);
    }
}

/**
 * @ingroup Thread
 *
 * This function will get the handler of the idle thread.
 *
 */
rt_thread_t rt_thread_idle_gethandler(void)
{
#ifdef RT_USING_SMP
    register int id = rt_hw_cpu_id();
#else
    register int id = 0;
#endif

    return (rt_thread_t)(&idle[id]);
}