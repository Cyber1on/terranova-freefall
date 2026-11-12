/* keys.h -- keybindings (keys.c) */
#ifndef TN_KEYS_H
#define TN_KEYS_H
#include <stdio.h>
#include <stdint.h>

/* "physical keys" are SDL scan codes, then these */
enum {
    PORT_KEY_MOUSE_MIDDLE = 512,        /* SDL_SCANCODE_COUNT */
    PORT_KEY_MOUSE_4,
    PORT_KEY_MOUSE_5,
    PORT_KEY_WHEEL_UP,                  /* a wheel notch is a short key press (platform.c) */
    PORT_KEY_WHEEL_DOWN,
    PORT_KEY_MOUSELOOK,                 /* Freefall's actions, not keys: the mouselook switch, */
    PORT_KEY_MOUSELOOK_HOLD,            /* mouselook off while held, */
    PORT_KEY_GIMBAL,                    /* gimbal mode on / off */
    PORT_KEY_SHOW_FPS,                  /* the FPS meter on / off */
    PORT_KEY_COUNT
};

void port_keys_reset(void);             /* the controls style's keys back to its defaults */
void port_keys_load(void (*get)(const char *key, char *out, int size));
void port_keys_save(FILE *f);
int port_keys_bound(const char *name, int slot);  /* help.c: an action's key in this style */
int port_keys_translate(int k);         /* physical key -> game key (0 = nothing) */
void port_keys_mission_frame(void);
int port_keys_in_mission(void);
uint64_t port_keys_mission_seen(void);    /* when the mission's controls last ran (us) */
/* the options screen's Keys page */
int port_keys_count(void);
const char *port_keys_label(int a);
int port_keys_get(int a, int slot);
int port_keys_slot_used(int a, int slot);
int port_keys_set(int a, int slot, int k);  /* returns the action that gave its key up, or -1 */
const char *port_keys_name(int k);
#endif
