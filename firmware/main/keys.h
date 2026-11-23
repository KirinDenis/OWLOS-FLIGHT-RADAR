/* keys - the dial (up, down, pressed) and the MENU and EXIT keys of the Elecrow CrowPanel e-paper boards.
 * BOOT is apart, button.h. a board without them never gets a key */
#pragma once

void keys_init(void);
/* the next key pressed, one of ui.h's KEY_*, or 0. the presses wait in a queue meanwhile: the glass
 * takes seconds to change, and the UI's task waits that long */
int  keys_poll(void);
