**Trace2sleep kernel module for almost all devices**

(Trace2sleep is developed by tanish2k09@xda-developers.com using knowledge of sweep2wake module by Dennis Rassmann)


It is a kernel module and must be compiled into the kernel

**Why is this made?**
This was made because the existing gesture sleep module (sweep2sleep) sometimes conflicted with gesture made while gaming or navigating.
The screen used to turn off unintentionally.
Also, sweep2wake module (as was made without edits) cannot be used without disabling sweep2sleep.

This module fixes two things :
1) Allow sweep2wake to be fully standalone, and remove sweep2sleep.
2) Make such a gesture for sleep which makes it difficult to trigger accidentally, but naturally fluid when doing intentionally.

**How it works :**
When the screen is on, you can swipe an arc, starting from one bottom corner, making an arc upwards to the middle of the phone (slightly less than middle of height of screen, for easier accessibility) and then curving back down to the other bottom corner.

The tuneables X_HALF, Y_MAX, LOWER_BOUND_RADIUS, UPPER_BOUND_RADIUS, Y_INTERCEPT_AT_SIDES are defined in the module and must be changed according to the device. I have added 1080p configuration as default, and also commented out the 720p configuration below it.
X_HALF : It's value is half of the horizontal resolution of device.
Y_MAX : It's value is the vertical resolution of device.
LOWER_BOUND_RADIUS and UPPER_BOUND_RADIUS : These are the radii of the circles which are hypothetically centred at the bottom of the device screen, and horizontally in the middle. The region between the two circles on screen is the region which the touch input is allowed to move in.

We start by detecting the touch input near the bottom corners, whose size is calculated according to X_HALF and the radii.
When the touch input is placed within that corner region, the path between the circles comes to play. Now as long as the finger is within that region, the gesture is working fine. Now the finger must be moved from that bottom corner to the other bottom corner.
If the finger touches the two hypothetical circular bounds, the gesture is reset and is no longer detected until next corner input.
If the finger successfully reaches the other bottom corner, the screen turns off with an emulated  power button press.

The corner trigger and the circular bounds help to ensure that the gesture was intentional. It is highly unlikely that such a curved gesture is made accidentally.

Trust me, it sounds much more complex than it is, and way harder to trigger than it really is.


**Here are the instructions to add t2s (the files are named trace2wake because of choosing sweep2wake as template)**
(**Note :** If you have added sweep2wake before, adding this will be much much easier)

0) **Add sweep2wake to your kernel first**

1) Replace the given sweep2wake.c file car11ully (usually you'll only need to change the driver name at line 239, the main function, and need to remove s2w_s2sonly variable).

2) Carefully replace sweep2wake.h also, considering the added and removed variables. Rest is same.

3) After successfully "porting" the custom sweep2wake.c file, (which actually functions like swipe2wake (in any direction) and removes sweep2sleep because we're trying to add trace2sleep) try to compile the kernel right now.

4) If the kernel compiles, proceed to next steps. If kernel does not compile, you need to debug what you did wrong. Probably sweep2wake.c

5) Read the readme files in subdirectories to get the commits related to that part of code.

6) Proceed according to the corresponding "README"s and keep your brain on fire.




**Credits : (all those who helped in any intentional and unintentional way)**
1) Tanish2k09 (dat me tho)
2) Maxr1998 (@xda-developers.com) (He accidentally provided me the correction to my code's mistakes)
3) Dennis Rassmann (for giving us sweep2wake module which I studied to make this module)
4) Linux and android devs also!
