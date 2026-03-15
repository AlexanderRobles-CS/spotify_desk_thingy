#pragma once
 
extern volatile bool          playbackButtonPressed;
extern volatile unsigned long last_playback_interrupt_time;

extern volatile bool          prevButtonPressed;
extern volatile unsigned long last_prev_interrupt_time;

extern volatile bool          skipButtonPressed;
extern volatile unsigned long last_skip_interrupt_time;

void initControls();