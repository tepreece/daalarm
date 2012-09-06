/*
Copyright (c) 2009 - 2012 Thomas Preece

Permission is hereby granted, free of charge, to any person obtaining a 
copy of this software and associated documentation files (the 
"Software"), to deal in the Software without restriction, including 
without limitation the rights to use, copy, modify, merge, publish, 
distribute, sublicense, and/or sell copies of the Software, and to 
permit persons to whom the Software is furnished to do so, subject to 
the following conditions:

The above copyright notice and this permission notice shall be included 
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS 
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <alsa/asoundlib.h>

/*                              *\
* START OF CONFIGURABLE SECTION *
\*                              */

/* audio parameters */
#define DEVICE "plughw:0,0"
#define RATE 44100
#define CHANNELS 1
#define BUFFER 100
#define FRAME 1024

/* dead-air paramters */
#define SECONDS_TRIP 30
#define SECONDS_NOTIFY 5
#define THRESHOLD 100
#define THRESHOLD_SAMPLES 900

/*                              *\
*  END OF CONFIGURABLE SECTION  *
\*                              */


/* program constants */
#define WAITING 0
#define TRIPPED 1
#define STOPPED 2

#define NORMAL 0
#define DEADAIR 1

int status = WAITING;

int notify_status = NORMAL;

void aerror(const char *msg, int r) {
	fprintf(stderr, "ALSA %s: %s\n", msg, snd_strerror(r));
}

void sigint(int sig) {
	status = STOPPED;
}

void notify(int pid, int signal) {
	if (pid == -1) return;
	kill(pid, signal);	
}

int main(int argc, char *argv[]) {
	int i, p, r, dir;
	snd_pcm_uframes_t buffer_size, period_size, f;
	snd_pcm_t *pcm;
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	short abuf[FRAME * CHANNELS];
	short* ptr;
	
	snd_pcm_hw_params_alloca(&hw_params);
	snd_pcm_sw_params_alloca(&sw_params);
	
	int max_frames = SECONDS_TRIP * (RATE / FRAME);
	int frames_1 = max_frames / 3;
	int frames_2 = frames_1 * 2;
	int frames_notify = SECONDS_NOTIFY * (RATE / FRAME);
	int count_frames = 0;
	
	// parameters
	int notify_pid=-1;	
	short trip = 0;
	
	// get the parameters from the command line
	int opt = 0;
	while ((opt = getopt(argc, argv, "p:t")) != -1) {
		switch (opt) {
			case 'p':
				sscanf(optarg, "%d", &notify_pid);
				break;
			case 't':
				trip = 1;
				break;
		}
	}
	
	// print a welcome message
	printf("Dead Air Alarm v0.3 by Thomas Preece\n");
	printf("http://tpreece.net\n");
	printf("Sample rate: %d\n", RATE);
	printf("Silence threshold: %d\n", THRESHOLD);
	printf("Frame size: %d\n", FRAME);
	printf("Silent samples required per frame: %d\n", THRESHOLD_SAMPLES);
	
	if (notify_pid == -1)
		printf("Will not notify any other process.\n");
	else
		printf("Will notify process %d after approx %d seconds.\n", notify_pid, SECONDS_NOTIFY);
	
	if (trip)
		printf("Will trip after approx %d seconds %d frames) of silence.\n", SECONDS_TRIP, max_frames);
	else
		printf("Will not trip.\n");
	
	// Tell the other process that there's no dead air.
	// This may not actually be true, but it's better than it thinking there
	// is dead air indefinitely, because we haven't notified it otherwise.
	notify(notify_pid, SIGUSR2);

	// open and set up the audio device
	printf("Opening audio device... ");
	
	r = snd_pcm_open(&pcm, DEVICE, SND_PCM_STREAM_CAPTURE, 0);
	if (r < 0) {
		aerror("open", r);
		return -1;
	}
	
	r = snd_pcm_hw_params_any(pcm, hw_params);
	if (r < 0) {
		aerror("hw_params_any", r);
		return -1;
	}
	
	r = snd_pcm_hw_params_set_rate_resample(pcm, hw_params, 1);
	if (r < 0) {
		aerror("hw_params_set_rate_resample", r);
		return -1;
	}
	
	r = snd_pcm_hw_params_set_access(pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (r < 0) {
		aerror("hw_params_set_access", r);
		return -1;
	}
	
	r = snd_pcm_hw_params_set_format(pcm, hw_params, SND_PCM_FORMAT_S16);
	if (r < 0) {
		aerror("hw_params_set_format", r);
		return -1;
	}
	
	r = snd_pcm_hw_params_set_rate(pcm, hw_params, RATE, 0);
	if (r < 0) {
		aerror("hw_params_set_rate", r);
		return -1;
	}
	
	r = snd_pcm_hw_params_set_channels(pcm, hw_params, CHANNELS);
	if (r < 0) {
		aerror("hw_params_set_channels", r);
		return -1;
	}
	
	p = BUFFER * 1000; // convert milliseconds to microseconds
	r = snd_pcm_hw_params_set_buffer_time_near(pcm, hw_params, &p, &dir);
	if (r < 0) {
		aerror("hw_params_set_buffer_time_near", r);
		return -1;
	}
	
	r = snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size);
	if (r < 0) {
		aerror("hw_params_get_buffer_size", r);
		return -1;
	}
	
	f = FRAME;
	r = snd_pcm_hw_params_set_period_size_near(pcm, hw_params, &f, &dir);
	if (r < 0) {
		aerror("hw_params_set_period_time_near", r);
		return -1;
	}
	
	r = snd_pcm_hw_params_get_period_size(hw_params, &period_size, &dir);
	if (r < 0) {
		aerror("hw_params_get_period_size", r);
		return -1;
	}
	
	r = snd_pcm_hw_params(pcm, hw_params);
	if (r < 0) {
		aerror("hw_params", r);
		return -1;
	}

	r = snd_pcm_sw_params_current(pcm, sw_params);
	if (r < 0) {
		aerror("sw_params_current", r);
		return -1;
	}

	r = snd_pcm_sw_params(pcm, sw_params);
	if (r < 0) {
		aerror("sw_params", r);
		return -1;
	}
	
	printf("done\n");
	
	// set up SIGINT so that we can terminate the program with control+C
	(void) signal(SIGINT, sigint);
	
	while (status==WAITING) {
		int count_samples = 0;
		// read a frame of audio
		r = snd_pcm_readi(pcm, abuf, FRAME);
		if (r < 0) {
			aerror("snd_pcm_readi", r);
			if (r == -EPIPE) {
				snd_pcm_prepare(pcm);
				continue;
			}
			return -1;
		}
		
		// check each sample
		ptr = abuf;
		for (i=0; i<FRAME; i++, ptr++) {
			if (abs(*ptr)<THRESHOLD)
				count_samples++;
		}
		
		if (count_samples > THRESHOLD_SAMPLES) {
			count_frames++;
			if (notify_status==NORMAL)
				fputc('O', stderr);
			else
				fputc('!', stderr);
			if (count_frames==frames_notify) {
				notify_status=DEADAIR;
				notify(notify_pid, SIGUSR1);
			}
		} else {
			count_frames = 0;
			fputc('.', stderr);
			if (notify_status==DEADAIR)
				notify(notify_pid, SIGUSR2);
		}
		
		if (count_frames==max_frames)
			if (trip)
				status = TRIPPED;
	}
	
	// close audio
	snd_pcm_close(pcm); // ignore any errors, we're closing down anyway
	
	if (status==TRIPPED) {
		printf("\nALARM TRIPPED!\n");
		return 1;
	} else {
		printf("\nTerminated\n");
		return 0;
	}
}

