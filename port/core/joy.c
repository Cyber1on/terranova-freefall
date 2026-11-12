/* joy.c -- joysticks.  The game's joystick library (joy_init / joy_center / joy_read_axes /
 * joy_read_buttons, third-party drivers for the PC game port and CH / Thrustmaster / Gravis
 * sticks) is replaced by SDL3's joystick support; TN.CFG's "joystick 3" (none) is respected.
 * The raw game port (201h) reads as "nothing connected". */
#include <SDL3/SDL.h>
#include <string.h>
#include "platform.h"
#include "port.h"

static SDL_Joystick *stick;

uint8_t port_joy_port(void) { return 0xF0; }
void port_joy_trigger(void) {}

long joy_init(long mode)
{
    SDL_JoystickID *ids;
    int n = 0;

    stick = NULL;
    if ((mode & 0xF) == 3)
        return 0;
    ids = SDL_GetJoysticks(&n);
    if (ids && n > 0)
        stick = SDL_OpenJoystick(ids[0]);
    SDL_free(ids);
    if (stick)
        port_log("Joystick: %s\n", SDL_GetJoystickName(stick));
    return stick != NULL;
}

long joy_center(void)
{
    return 0;
}

/* axes: x, y, throttle, rudder as signed bytes (-128..127, 0 = centre) */
long joy_read_axes(signed char *axes)
{
    int i;
    memset(axes, 0, 4);
    if (!stick)
        return 0;
    for (i = 0; i < 4 && i < SDL_GetNumJoystickAxes(stick); i++)
        axes[i] = (signed char)(SDL_GetJoystickAxis(stick, i) >> 8);
    return 0;
}

long joy_read_buttons(void)
{
    long b = 0;
    int i;
    if (!stick)
        return 0;
    for (i = 0; i < 4 && i < SDL_GetNumJoystickButtons(stick); i++)
        if (SDL_GetJoystickButton(stick, i))
            b |= 1 << i;
    return b;
}
