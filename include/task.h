#ifndef GUARD_TASK_H
#define GUARD_TASK_H

#define HEAD_SENTINEL 0xFE
#define TAIL_SENTINEL 0xFF
#define TASK_NONE TAIL_SENTINEL

#define NUM_TASKS 16
#define NUM_TASK_DATA 32

typedef void (*TaskFunc)(u8 taskId);

struct Task
{
    TaskFunc func;
    bool8 isActive;
    u8 prev;
    u8 next;
    u8 priority;
#ifdef VER_64BIT
    // Some tasks keep structs with pointers in data (e.g. ListMenu)
    ALIGNED(8) s16 data[NUM_TASK_DATA];
#else
    s16 data[NUM_TASK_DATA];
#endif
    TaskFunc followupFunc;
    union {
        void *genericPtr[2];
        uintptr_t intPtr[2];
        void (*funcPtr)(void);
        TaskFunc funcPtr_task;
        struct Sprite *spritePtr;
        struct Pokemon *monPtr;
    } ptr;
#ifdef VER_64BIT
    // Set/GetWordTaskArg keep the low 32 bits of a word in two data[] elements,
    // as on the GBA. A pointer is wider here, so the whole value is kept too.
    uintptr_t wordArgs[NUM_TASK_DATA];
#endif
};

extern struct Task gTasks[];

void ResetTasks(void);
u8 CreateTask(TaskFunc func, u8 priority);
void DestroyTask(u8 taskId);
void RunTasks(void);
void TaskDummy(u8 taskId);
void SetTaskFuncWithFollowupFunc(u8 taskId, TaskFunc func, TaskFunc followupFunc);
void SwitchTaskToFollowupFunc(u8 taskId);
bool8 FuncIsActiveTask(TaskFunc func);
u8 FindTaskIdByFunc(TaskFunc func);
u8 GetTaskCount(void);
void SetWordTaskArg(u8 taskId, u8 dataElem, uintptr_t value);
uintptr_t GetWordTaskArg(u8 taskId, u8 dataElem);

#endif // GUARD_TASK_H
