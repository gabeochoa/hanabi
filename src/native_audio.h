#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void native_audio_play(const char* path);
void native_audio_pause(void);
void native_audio_stop(void);
int native_audio_is_playing(const char* path);
double native_audio_position(const char* path);
double native_audio_duration(const char* path);
int native_audio_peaks(const char* path, float* out, int count);

#ifdef __cplusplus
}
#endif
