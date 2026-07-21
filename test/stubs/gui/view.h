// SPDX-License-Identifier: MIT
// Host test stub for <gui/view.h>. The View lifecycle functions are inert
// dummies: the tests only call the static word-wrap logic in view_about.c, not
// the allocation/draw/input paths that would touch a real View. The
// with_view_model macro mirrors the SDK's C expansion so view_about.c compiles.
#pragma once
#include <gui/gui.h>
#include <input/input.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct View View;

typedef enum
{
    ViewModelTypeNone,
    ViewModelTypeLockFree,
    ViewModelTypeLocking,
} ViewModelType;

typedef void (*ViewDrawCallback)(Canvas *canvas, void *model);
typedef bool (*ViewInputCallback)(InputEvent *event, void *context);

// A single static scratch model backs view_get_model for link purposes; the
// tests never drive these paths.
static unsigned char _stub_view_model[256];

static inline View *
view_alloc (void)
{
    return (View *)_stub_view_model;
}
static inline void
view_free (View *view)
{
    (void)view;
}
static inline void *
view_get_model (View *view)
{
    (void)view;
    return _stub_view_model;
}
static inline void
view_allocate_model (View *view, ViewModelType t, size_t size)
{
    (void)view;
    (void)t;
    (void)size;
}
static inline void
view_commit_model (View *view, bool update)
{
    (void)view;
    (void)update;
}
static inline void
view_set_context (View *view, void *context)
{
    (void)view;
    (void)context;
}
static inline void
view_set_draw_callback (View *view, ViewDrawCallback cb)
{
    (void)view;
    (void)cb;
}
static inline void
view_set_input_callback (View *view, ViewInputCallback cb)
{
    (void)view;
    (void)cb;
}

#define with_view_model(view, type, code, update) \
    {                                             \
        type = view_get_model(view);              \
        { code };                                 \
        view_commit_model(view, update);          \
    }
