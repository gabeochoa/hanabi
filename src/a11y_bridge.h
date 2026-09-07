#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    const char* role;
    const char* parent;
    unsigned long long entity;
    float x;
    float y;
    float width;
    float height;
    int enabled;
    int selected;
    int has_submenu;
    int expanded;
    int focused;
} NativeA11yNode;

void native_a11y_publish(const NativeA11yNode* nodes, size_t count);

size_t native_a11y_published_count(void);

void native_a11y_describe(const char* name, char* out, size_t cap);

size_t native_a11y_child_count(const char* name);

void native_a11y_parent_of(const char* name, char* out, size_t cap);

void native_a11y_role_of(const char* name, char* out, size_t cap);

unsigned long long native_a11y_take_pressed(void);

int native_a11y_perform_press(const char* name);

#ifdef __cplusplus
}
#endif
