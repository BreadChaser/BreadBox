#pragma once
#include <stdbool.h>
typedef struct { int x, y; bool held, down; } input_contact_t;
input_contact_t input_contact(void);
void input_transport_handler(void (*poll)(void));
