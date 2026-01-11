// Animation subsystem declarations

#pragma once

// FreeRTOS task implementing simple pet animations.  The animation
// task posts movement commands to the display task via a queue
// defined in display.c.  It runs continuously once the game has
// started.
void anim_task(void *arg);
