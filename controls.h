#pragma once
 
extern volatile bool          playbackButtonPressed;
extern volatile unsigned long last_playback_interrupt_time;
 
void initControls();
 